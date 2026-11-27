// render/tests/unitanim/test_combat_kinds.cpp - issue 4775 (lead-0042 #7555): the additive action kinds run (1400),
// combat_ready (1600) and idle (1000) for a figure set; 4750 (lead-0042 #10392): walk (1450), appended. The .c5anim
// reader and writer, the ActionBridge's codes and its EnableActions check. Negative: an argument on the new kinds, a
// misspelt kind, a member without the word. 4750: a file written before walk existed reads and writes back unchanged.
#include <string>

#include "anim_testkit.h"
#include "c5r/unitanim/bridge.h"
#include "c5r/unitanim/timeline.h"

using namespace anim_test;

namespace {

c5r::units::UnitScene scene() {
    c5r::units::UnitScene sc;
    c5r::units::PlayerInput p;
    p.player = 0;
    p.colour = "PLAYERCOLOR_ROME";
    p.art_style = "ARTSTYLE_EUROPEAN";
    p.era = "ERA_ANCIENT";
    sc.players.push_back(p);
    c5r::units::UnitInput u = Rig::unit(0, 1, "UNIT_WARRIOR");
    sc.units.push_back(u);
    return sc;
}

std::string one(const std::string& line) { return "c5anim\t1\nscene\t-\t7\tfree\n" + line + "\nframe\t1\t0.1\t-\t-\n"; }

c5r::units::UnitArt art(const std::string& member, int n) {
    c5r::units::UnitArt a;
    a.art_row = "ART_T";
    c5r::units::MemberArt m;
    m.member = member;
    m.count = n;
    a.members.push_back(m);
    return a;
}

int32_t code(ActionBridge& b, ActionKind k, const c5r::units::UnitArt& a, AnimCode want = AnimCode::Ok) {
    TimelineEvent e;
    e.kind = EventKind::Action;
    e.player = 0;
    e.id = 1;
    e.action = k;
    e.figures.all = false;
    e.figures.figures = {0, 2};
    c5r::units::UnitInput u = Rig::unit(0, 1);
    std::vector<Request> out;
    AnimStatus st = b.requestsFor(e, u, a, out);
    CHECK_ANIM(st, want);
    if (!st.ok()) return -1;
    CHECK_EQ(out.size(), static_cast<size_t>(1));
    CHECK(!out[0].figures.all);
    CHECK_EQ(out[0].figures.figures.size(), static_cast<size_t>(2));
    return out[0].value;
}

}  // namespace

C5R_TEST(new_kinds_round_trip_in_the_timeline_file) {
    const c5r::units::UnitScene sc = scene();
    const char* names[] = {"run", "combat_ready", "idle", "walk"};
    const ActionKind kinds[] = {ActionKind::Run, ActionKind::CombatReady, ActionKind::Idle, ActionKind::Walk};
    for (int i = 0; i < 4; ++i) {
        Timeline tl;
        TimelineStatus st = parseTimeline(one(std::string("ev\t0\t1\taction\t") + names[i] + "\t-\t0,3"), &sc, tl);
        CHECK(st.ok());
        CHECK(tl.frames.size() == 1 && tl.frames[0].events.size() == 1);
        CHECK(tl.frames[0].events[0].action == kinds[i]);
        CHECK(!tl.frames[0].events[0].figures.all);
        CHECK_EQ(std::string(actionKindName(kinds[i])), std::string(names[i]));
        std::string back;
        CHECK(writeTimeline(tl, &sc, back).ok());
        CHECK(back.find(std::string("\taction\t") + names[i] + "\t-\t0,3") != std::string::npos);
    }
    // the existing names keep their values (additive: anti_air is still 12)
    CHECK_EQ(static_cast<int>(ActionKind::AntiAir), 12);
    CHECK_EQ(static_cast<int>(ActionKind::Run), 13);
    CHECK_EQ(static_cast<int>(ActionKind::Idle), 15);   // 4750: walk is appended after it
    CHECK_EQ(static_cast<int>(ActionKind::Walk), 16);
}

// 4750: a timeline written before walk existed (every older action name) reads back to the same kinds and writes back
// byte-identical
C5R_TEST(a_file_written_before_walk_reads_and_writes_back_unchanged) {
    const c5r::units::UnitScene sc = scene();
    const char* old[] = {"attack", "attack_city", "bombard", "charge", "attack_charge", "victory", "bombard_defend",
                         "captured", "activate", "anti_air", "run", "combat_ready", "idle"};
    std::string text = "c5anim\t1\nscene\t-\t7\tfree\n";
    for (const char* n : old) text += std::string("ev\t0\t1\taction\t") + n + "\t-\t0,3\n";
    text += "frame\t1\t0.1\t-\t-\n";
    Timeline tl;
    TimelineStatus st = parseTimeline(text, &sc, tl);
    CHECK(st.ok());
    CHECK(tl.frames.size() == 1 && tl.frames[0].events.size() == sizeof(old) / sizeof(old[0]));
    for (size_t i = 0; i < tl.frames[0].events.size(); ++i) {
        CHECK(tl.frames[0].events[i].action != ActionKind::Walk);
        CHECK_EQ(std::string(actionKindName(tl.frames[0].events[i].action)), std::string(old[i]));
    }
    std::string back;
    CHECK(writeTimeline(tl, &sc, back).ok());
    Timeline again;
    CHECK(parseTimeline(back, &sc, again).ok());
    std::string back2;
    CHECK(writeTimeline(again, &sc, back2).ok());
    CHECK(back == back2);
    for (const char* n : old) CHECK(back.find(std::string("\taction\t") + n + "\t-\t0,3") != std::string::npos);
}

C5R_TEST(new_kinds_refuse_an_argument_and_a_misspelling) {
    const c5r::units::UnitScene sc = scene();
    Timeline tl;
    TimelineStatus st = parseTimeline(one("ev\t0\t1\taction\trun\tX\t*"), &sc, tl);
    CHECK(st.code == TimelineErr::BadValue);
    CHECK(tl.frames.empty());
    st = parseTimeline(one("ev\t0\t1\taction\tcombat-ready\t-\t*"), &sc, tl);
    CHECK(st.code == TimelineErr::BadValue);
    CHECK(st.msg.find("unknown action") != std::string::npos);
    st = parseTimeline(one("ev\t0\t1\taction\tidle\t-\t0,0"), &sc, tl);   // a figure twice
    CHECK(!st.ok());
}

C5R_TEST(bridge_maps_the_new_kinds_and_checks_their_words) {
    BridgeTables t;
    t.words["M"] = {"Idle", "Attack", "Death", "Run", "CombatReady", "Walk"};
    t.words["CIV"] = {"Idle", "Death", "BombardDefend"};
    ActionBridge b(t);
    CHECK_EQ(code(b, ActionKind::Run, art("M", 3)), 1400);
    CHECK_EQ(code(b, ActionKind::CombatReady, art("M", 3)), 1600);
    CHECK_EQ(code(b, ActionKind::Idle, art("M", 3)), 1000);
    // a member whose EnableActions lack Run / CombatReady is refused, naming the member
    CHECK_EQ(code(b, ActionKind::Run, art("CIV", 3), AnimCode::BadEvent), -1);
    CHECK_EQ(code(b, ActionKind::CombatReady, art("CIV", 3), AnimCode::BadEvent), -1);
    CHECK_EQ(code(b, ActionKind::Idle, art("CIV", 3)), 1000);
    // 4750: walk is 1450 and needs the word Walk
    CHECK_EQ(code(b, ActionKind::Walk, art("M", 3)), 1450);
    CHECK_EQ(code(b, ActionKind::Walk, art("CIV", 3), AnimCode::BadEvent), -1);
}

C5R_TEST_MAIN
