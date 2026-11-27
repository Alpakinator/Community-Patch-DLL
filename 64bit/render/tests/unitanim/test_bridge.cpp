// render/tests/unitanim/test_bridge.cpp - issue 4404 part B: the event bridge (bridge.h; unit-controller.md 1, 6.5,
// 14). Positive: every event kind's code, EnableActions per member, the damage rule's figures (X4) through the
// controller, a work table, the tables from a database, anti_air in the .c5anim file. Negative: one test per refusal.
#include <cstdint>
#include <string>
#include <vector>

#include "anim_testkit.h"
#include "c5r/unitanim/bridge.h"
#include "c5r/unitanim/timeline.h"

using namespace anim_test;
using c5r::units::MemberArt;
using c5r::units::UnitArt;
using c5r::units::UnitInput;

namespace {

const double kH = 1.0 / 30.0;

const char* kWarriorWords = "Idle Attack RunCharge AttackCity Bombard Death BombardDefend Run Fortify CombatReady Walk "
                            "AttackCharge";

BridgeTables tables() {
    BridgeTables t;
    auto words = [](const std::string& s) {
        std::set<std::string> w;
        size_t at = 0;
        while (at < s.size()) {
            size_t sp = s.find(' ', at);
            w.insert(s.substr(at, sp == std::string::npos ? std::string::npos : sp - at));
            if (sp == std::string::npos) break;
            at = sp + 1;
        }
        return w;
    };
    t.words["M"] = words(kWarriorWords);
    t.words["CIV"] = words("Idle Death BombardDefend Run");
    t.words["AA"] = words(std::string(kWarriorWords) + " AttackSurfaceToAir");
    t.build_event["BUILD_MINE"] = "ENTITY_EVENT_MINE";
    t.build_event["BUILD_NOTHING"] = "";
    return t;
}

UnitArt artOf(std::vector<std::pair<std::string, int>> members) {
    UnitArt a;
    a.art_row = "ART_T";
    for (auto& m : members) {
        MemberArt x;
        x.member = m.first;
        x.count = m.second;
        a.members.push_back(x);
    }
    return a;
}

TimelineEvent ev(EventKind k, int a = 0, int b = 0) {
    TimelineEvent e;
    e.kind = k;
    e.player = 0;
    e.id = 1;
    e.a = a;
    e.b = b;
    return e;
}
TimelineEvent act(ActionKind k, const std::string& arg = "") {
    TimelineEvent e = ev(EventKind::Action);
    e.action = k;
    e.argument = arg;
    return e;
}

UnitInput unitIn() {
    UnitInput u = Rig::unit(0, 1);
    u.max_hp = 100;
    return u;
}

// One request; its value (or -1 with the status checked against `want`).
int32_t code(ActionBridge& b, const TimelineEvent& e, const UnitArt& art, AnimCode want = AnimCode::Ok) {
    std::vector<Request> out;
    AnimStatus s = b.requestsFor(e, unitIn(), art, out);
    CHECK_ANIM(s, want);
    if (!s.ok()) {
        CHECK(out.empty());
        return -1;
    }
    if (out.empty()) return 0;
    CHECK_EQ(out.size(), static_cast<size_t>(1));
    CHECK_EQ(out[0].path, 4);
    return out[0].value;
}

// A machine accepting every bridge code from idle (state 1, ec 1000): state <code> on P4==<code>, back to 1 on
// P4==1000; graphs "s<code>.dge" with ec 1000 (idle) so every state has a program.
c5m::StateMachine allCodes() {
    c5m::StateMachine m;
    m.fsm = "FGrannyStateMachine";
    m.default_state = 1;
    m.states = {state(1, "idle1.dge", 1000)};
    for (int c : {1100, 1120, 1130, 1140, 1160, 1180, 1195, 1200, 1280, 1400, 1500, 1800, 1940, 1990}) {
        m.states.push_back(state(c, "s" + std::to_string(c) + ".dge", c));
        m.sockets.push_back(socket(1, c, {checkPath(4, c)}));
        if (c != 1200) m.sockets.push_back(socket(c, 1, {checkPath(4, 1000)}));
    }
    return m;
}

struct BridgeRig {
    Rig r;
    BridgeTables t = tables();
    ActionBridge b{t};
    void open(std::vector<std::pair<std::string, int>> members) {
        r.opt.bridge = &b;
        r.open();
        UnitArt a = artOf(members);
        for (auto& m : a.members) {
            MemberArt full = memberArt(m.member, m.count, "all.fsmxml", warriorClips());
            m = full;
        }
        r.art.rows["T"] = a;
        CHECK_ANIM(r.c.logic()->putMachine("all.fsmxml", allCodes()), AnimCode::Ok);
        r.c.logic()->putGraph("idle1.dge", dag({1000}));
        for (int c : {1100, 1120, 1130, 1140, 1160, 1180, 1195, 1200, 1280, 1400, 1500, 1800, 1940, 1990})
            r.c.logic()->putGraph("s" + std::to_string(c) + ".dge", dag({1000}));
        TimelineFrame f = Rig::frame(1, kH);
        TimelineEvent cr = ev(EventKind::Create);
        cr.unit = unitIn();
        cr.unit.unit_type = "T";
        f.events.push_back(cr);
        CHECK_ANIM(r.c.apply(f), AnimCode::Ok);
    }
    AnimCode apply(const TimelineEvent& e, uint64_t seq) {
        TimelineFrame f = Rig::frame(seq, kH);
        f.events.push_back(e);
        CHECK_ANIM(r.c.apply(f), AnimCode::Ok);
        const AnimStatus* s = r.c.status(0, 1);
        return s ? s->code : AnimCode::UnitFailed;
    }
    int state(uint32_t figure) const {
        const FigureProgram* p = r.prog(0, 1, figure);
        return p ? p->state : -1;
    }
};

}  // namespace

// ------------------------------------------------------------------------------------------------ codes
C5R_TEST(every_event_kind_maps_to_its_code) {
    BridgeTables t = tables();
    t.work_codes["ENTITY_EVENT_MINE"] = 1940;
    ActionBridge b(t);
    const UnitArt w = artOf({{"M", 2}});
    CHECK_EQ(code(b, ev(EventKind::Fortify, 1), w), 1500);
    CHECK_EQ(code(b, ev(EventKind::Fortify, 0), w), 1000);
    CHECK_EQ(code(b, ev(EventKind::Move, 2, 3), w), 1400);
    CHECK_EQ(code(b, ev(EventKind::Arrive, 1), w), 1000);
    CHECK_EQ(code(b, ev(EventKind::Destroyed, 1), w), 1200);
    CHECK_EQ(code(b, ev(EventKind::Kill), w), 1200);
    CHECK_EQ(code(b, act(ActionKind::Attack), w), 1100);
    CHECK_EQ(code(b, act(ActionKind::AttackCharge), w), 1140);
    CHECK_EQ(code(b, act(ActionKind::Charge), w), 1120);
    CHECK_EQ(code(b, act(ActionKind::AttackCity), w), 1160);
    CHECK_EQ(code(b, act(ActionKind::Bombard), w), 1180);
    CHECK_EQ(code(b, act(ActionKind::BombardDefend), w), 1280);
    CHECK_EQ(code(b, act(ActionKind::Victory), w), 1800);
    CHECK_EQ(code(b, act(ActionKind::Captured), w), 1195);
    CHECK_EQ(code(b, act(ActionKind::Activate), w), 1990);
    CHECK_EQ(code(b, act(ActionKind::Work, "BUILD_MINE"), w), 1940);
    CHECK_EQ(code(b, act(ActionKind::AntiAir), artOf({{"AA", 1}})), 1130);
    // no request
    CHECK_EQ(code(b, ev(EventKind::Turn, 3, 1), w), 0);
    CHECK_EQ(code(b, ev(EventKind::Embark, 1), w), 0);
    CHECK_EQ(code(b, ev(EventKind::Garrison, 1), w), 0);
    CHECK_EQ(code(b, ev(EventKind::Visibility, 1), w), 0);
    // destroyed and kill mark figures dead; the others do not
    std::vector<Request> out;
    CHECK_ANIM(b.requestsFor(ev(EventKind::Destroyed, 0), unitIn(), w, out), AnimCode::Ok);
    CHECK(out[0].dies && out[0].figures.all);
    CHECK_ANIM(b.requestsFor(act(ActionKind::Attack), unitIn(), w, out), AnimCode::Ok);
    CHECK(!out[0].dies);
    // the action's figure set is passed on
    TimelineEvent some = act(ActionKind::Attack);
    some.figures.all = false;
    some.figures.figures = {1};
    CHECK_ANIM(b.requestsFor(some, unitIn(), w, out), AnimCode::Ok);
    CHECK(!out[0].figures.all);
    CHECK_EQ(out[0].figures.figures.size(), static_cast<size_t>(1));
    // the word table of 6.5
    CHECK_EQ(std::string(actionWord(1130)), std::string("AttackSurfaceToAir"));
    CHECK_EQ(std::string(actionWord(1450)), std::string("Walk"));
    CHECK(actionWord(1800) == nullptr && actionWord(1990) == nullptr && actionWord(1940) == nullptr);
}

C5R_TEST(enable_actions_per_member) {
    BridgeTables t = tables();
    ActionBridge b(t);
    // a civilian member: idle, death, run, bombard defend; no attack, fortify, charge
    const UnitArt civ = artOf({{"CIV", 1}});
    CHECK_EQ(code(b, ev(EventKind::Move), civ), 1400);
    CHECK_EQ(code(b, ev(EventKind::Arrive), civ), 1000);
    CHECK_EQ(code(b, ev(EventKind::Fortify, 0), civ), 1000);
    CHECK_EQ(code(b, act(ActionKind::BombardDefend), civ), 1280);
    CHECK_EQ(code(b, act(ActionKind::Activate), civ), 1990);   // no word: the machine decides
    CHECK_EQ(code(b, act(ActionKind::Captured), civ), 1195);
    // a mixed unit: a request reaching only the warrior figures passes, one reaching the civilian fails
    const UnitArt mixed = artOf({{"M", 2}, {"CIV", 1}});
    TimelineEvent front = act(ActionKind::Attack);
    front.figures.all = false;
    front.figures.figures = {0, 1};
    CHECK_EQ(code(b, front, mixed), 1100);
    front.figures.figures = {2};
    CHECK_EQ(code(b, front, mixed, AnimCode::BadEvent), -1);
}

// ------------------------------------------------------------------------------------------------ refusals
C5R_TEST(refuses_without_art) {
    BridgeTables t = tables();
    ActionBridge b(t);
    std::vector<Request> out;
    CHECK_ANIM(b.requests(ev(EventKind::Fortify, 1), unitIn(), out), AnimCode::BadEvent);
    CHECK(out.empty());
}

C5R_TEST(refuses_a_word_the_member_lacks) {
    BridgeTables t = tables();
    ActionBridge b(t);
    const UnitArt civ = artOf({{"CIV", 2}});
    CHECK_EQ(code(b, act(ActionKind::Attack), civ, AnimCode::BadEvent), -1);
    CHECK_EQ(code(b, act(ActionKind::Charge), civ, AnimCode::BadEvent), -1);
    CHECK_EQ(code(b, act(ActionKind::AttackCity), civ, AnimCode::BadEvent), -1);
    // 0439: fortify is posted by the EXE with no word test (a civilian's sleep, 1500, has no word in its row)
    CHECK_EQ(code(b, ev(EventKind::Fortify, 1), civ), 1500);
}

// 5071 (unit-combat-server.md 18.46): 1140 is taken from a member with RunCharge and no AttackCharge (VP's knight row:
// the EXE's strike after a 1120 asks 1140 when RunCharge is enabled, 0321 5.2 state 10), and still refused without either
C5R_TEST(charge_attack_with_run_charge_alone) {
    BridgeTables t = tables();
    auto words = t.words["M"];
    words.erase("AttackCharge");
    t.words["KN"] = words;
    words.erase("RunCharge");
    t.words["NORUN"] = words;
    ActionBridge b(t);
    CHECK_EQ(code(b, act(ActionKind::AttackCharge), artOf({{"KN", 3}})), 1140);
    CHECK_EQ(code(b, act(ActionKind::AttackCharge), artOf({{"NORUN", 3}}), AnimCode::BadEvent), -1);
    CHECK_EQ(code(b, act(ActionKind::AttackCharge), artOf({{"M", 3}})), 1140);
}

C5R_TEST(refuses_anti_air_without_attack_surface_to_air) {
    BridgeTables t = tables();
    ActionBridge b(t);
    CHECK_EQ(code(b, act(ActionKind::AntiAir), artOf({{"M", 1}}), AnimCode::BadEvent), -1);
}

// 0439 (was refuses_a_member_without_combats_row): a member with no Combats row is not checked against words (VP's
// COLONIST2-4, which the EXE draws); a member with a row still is, also in the same unit
C5R_TEST(member_without_combats_row_is_not_checked) {
    BridgeTables t = tables();
    ActionBridge b(t);
    const UnitArt col = artOf({{"COLONIST", 1}});
    CHECK_EQ(code(b, ev(EventKind::Move), col), 1400);
    CHECK_EQ(code(b, ev(EventKind::Fortify, 0), col), 1000);
    CHECK_EQ(code(b, ev(EventKind::Arrive), col), 1000);
    CHECK_EQ(code(b, act(ActionKind::Attack), col), 1100);   // no word mask known: the machine decides
    const UnitArt mixed = artOf({{"COLONIST", 1}, {"CIV", 1}});
    CHECK_EQ(code(b, act(ActionKind::Attack), mixed, AnimCode::BadEvent), -1);   // CIV's row lacks Attack
}

// 0439 (was refuses_work_without_a_source): the EXE's tool by the build's Type (unit-controller.md 14.1)
C5R_TEST(work_tool_by_build_type) {
    BridgeTables t = tables();   // no work table: the EXE's rule
    ActionBridge b(t);
    const UnitArt w = artOf({{"CIV", 1}});   // no word check for work, so a civilian row passes
    const struct { const char* build; int32_t code; } cases[] = {
        {"BUILD_FARM", 1920},         {"BUILD_TERRACE_FARM", 1920}, {"BUILD_POLDER", 1920},
        {"BUILD_MINE", 1940},         {"BUILD_QUARRY", 1940},       {"BUILD_ROAD", 1910},
        {"BUILD_RAILROAD", 1910},     {"BUILD_SCRUB_FALLOUT", 1910}, {"BUILD_REMOVE_MARSH", 1910},
        {"BUILD_REMOVE_ROUTE", 1910}, {"BUILD_REMOVE_FOREST", 1930}, {"BUILD_REMOVE_JUNGLE", 1930},
        {"BUILD_PASTURE", 1950},      {"BUILD_CITADEL", 1950},      {"BUILD_FISHING_BOATS", 1950},
        {"BUILD_UNKNOWN", 1950},      {"BUILD_NOTHING", 1950},      {"build_farm", 1950},   // exact Type only
    };
    for (auto& c : cases) {
        CHECK_EQ(workTool(c.build), c.code);
        CHECK_EQ(code(b, act(ActionKind::Work, c.build), w), c.code);
    }
    CHECK_EQ(code(b, act(ActionKind::Work, ""), w), 1000);   // the stop (build -1): the EXE posts IDLE
}

// A caller's work table (4404's form, by EntityEvent) replaces the rule, and keeps its refusals
C5R_TEST(work_table_replaces_the_rule) {
    BridgeTables t = tables();
    t.work_codes["ENTITY_EVENT_MINE"] = 1960;
    ActionBridge b(t);
    const UnitArt w = artOf({{"M", 1}});
    CHECK_EQ(code(b, act(ActionKind::Work, "BUILD_MINE"), w), 1960);
    CHECK_EQ(code(b, act(ActionKind::Work, ""), w), 1000);   // the stop needs no table
    t.build_event["BUILD_ROAD"] = "ENTITY_EVENT_SHOVEL";
    std::vector<Request> out;
    AnimStatus s = b.requestsFor(act(ActionKind::Work, "BUILD_ROAD"), unitIn(), w, out);
    CHECK_ANIM(s, AnimCode::BadEvent);
    CHECK(s.text.find("ENTITY_EVENT_SHOVEL") != std::string::npos);
    CHECK(out.empty());
    CHECK_EQ(code(b, act(ActionKind::Work, "BUILD_UNKNOWN"), w, AnimCode::BadEvent), -1);   // not in Builds
    CHECK_EQ(code(b, act(ActionKind::Work, "BUILD_NOTHING"), w, AnimCode::BadEvent), -1);   // NULL EntityEvent
}

// 0439: a unit whose damage falls gets its removed figures back (Controller::revive), lowest index first, each in its
// member's default state; in the dies mode (remove_fallen false) dead figures stay dead
C5R_TEST(damage_falls_revives_removed_figures) {
    BridgeRig br;
    br.open({{"M", 4}});
    CHECK_EQ(br.r.c.programs().size(), 4u);
    CHECK_EQ(br.apply(ev(EventKind::Damage, 60, 0), 2), AnimCode::Ok);   // 4 - round(2.4) = 2 living: 2 and 3 removed
    CHECK_EQ(br.r.c.programs().size(), 2u);
    CHECK(br.r.prog(0, 1, 3) == nullptr && br.r.prog(0, 1, 2) == nullptr);
    CHECK_EQ(br.r.c.lastApply().revived, 0u);
    CHECK_EQ(br.apply(ev(EventKind::Damage, 30, 60), 3), AnimCode::Ok);  // 4 - round(1.2) = 3: figure 2 back
    CHECK_EQ(br.r.c.lastApply().revived, 1u);
    CHECK_EQ(br.r.c.programs().size(), 3u);
    CHECK(br.r.prog(0, 1, 2) != nullptr && br.r.prog(0, 1, 3) == nullptr);
    CHECK_EQ(br.state(2), 1);                                              // the default state (idle)
    CHECK_EQ(br.apply(ev(EventKind::Damage, 0, 30), 4), AnimCode::Ok);    // healed: figure 3 back
    CHECK_EQ(br.r.c.lastApply().revived, 1u);
    CHECK_EQ(br.r.c.programs().size(), 4u);
    for (uint32_t i = 0; i < 4; ++i) CHECK(br.r.prog(0, 1, i) != nullptr);
    CHECK_EQ(br.apply(ev(EventKind::Damage, 0, 0), 5), AnimCode::Ok);     // no change: nothing revived twice
    CHECK_EQ(br.r.c.lastApply().revived, 0u);
    CHECK_EQ(br.r.c.programs().size(), 4u);
    // the dies mode: the fallen figures play death and stay; a fall in damage brings nobody back
    BridgeRig dm;
    dm.b.remove_fallen = false;
    dm.open({{"M", 4}});
    CHECK_EQ(dm.apply(ev(EventKind::Damage, 60, 0), 2), AnimCode::Ok);
    CHECK_EQ(dm.state(3), 1200);
    CHECK_EQ(dm.apply(ev(EventKind::Damage, 0, 60), 3), AnimCode::Ok);
    CHECK_EQ(dm.r.c.lastApply().revived, 0u);
    CHECK_EQ(dm.state(3), 1200);
}

// 4736 (W36; impl-0637's "no pose program ... figure 6", MC #9206): a unit created while damaged holds every figure (the
// create takes no damage rule); its next rise in damage removes only the range between the two living counts, which
// leaves a hole in the middle; a later fall then has enough figures by count and revived nobody, so the placement's
// living figures (0..k-1) asked for a figure the controller did not hold. The revive fills every index below the
// living count.
C5R_TEST(a_heal_after_a_hole_gives_back_every_figure_below_the_living_count) {
    BridgeRig br;
    br.open({{"M", 14}});   // created as a unit at damage 60 is: every figure held
    CHECK_EQ(br.r.c.programs().size(), 14u);
    CHECK_EQ(br.apply(ev(EventKind::Damage, 70, 60), 2), AnimCode::Ok);   // living 6 -> 4: figures 4 and 5 removed
    CHECK(br.r.prog(0, 1, 4) == nullptr && br.r.prog(0, 1, 5) == nullptr && br.r.prog(0, 1, 6) != nullptr);
    CHECK_EQ(br.apply(ev(EventKind::Damage, 40, 70), 3), AnimCode::Ok);   // healed: living 8, drawn 0..7
    for (uint32_t i = 0; i < 8; ++i) CHECK(br.r.prog(0, 1, i) != nullptr);
    CHECK_EQ(br.r.c.lastApply().revived, 2u);
}

// 4736 (W36): the fight form. A unit created at 40 damage holds all 14 figures; its fight kills the last 6 (Kill events:
// they stay held, dead); the damage after it (40 -> 45) erases nothing (8 living either way); a heal to 30 wants 10
// living: figures 8 and 9 must play again (W24's stage-5 series: unit (6, 13653) GWI, "no pose program ... figure 8")
C5R_TEST(a_heal_after_a_fight_gives_back_the_killed_figures_below_the_living_count) {
    for (int on = 0; on < 2; ++on) {
        BridgeRig br;
        br.r.opt.revive_dead = on != 0;   // the server's setting (beside remove_fallen); off: dead figures stay
        br.open({{"M", 14}});
        TimelineEvent k = ev(EventKind::Kill);
        k.figures.all = false;
        for (uint16_t f = 8; f < 14; ++f) k.figures.figures.push_back(f);
        CHECK_EQ(br.apply(k, 2), AnimCode::Ok);
        CHECK_EQ(br.state(8), 1200);
        CHECK_EQ(br.apply(ev(EventKind::Damage, 45, 40), 3), AnimCode::Ok);
        CHECK_EQ(br.apply(ev(EventKind::Damage, 30, 45), 4), AnimCode::Ok);
        if (on) {
            CHECK_EQ(br.r.c.lastApply().revived, 2u);
            for (uint32_t i = 0; i < 10; ++i) CHECK(br.r.prog(0, 1, i) != nullptr && br.state(i) == 1);   // default state
            for (uint32_t i = 10; i < 14; ++i) CHECK_EQ(br.state(i), 1200);   // above the living count: still dead
        } else {
            CHECK_EQ(br.r.c.lastApply().revived, 0u);
            CHECK_EQ(br.state(8), 1200);
            CHECK_EQ(br.state(9), 1200);
        }
    }
}

// 0439 (b): a colonist-like member with no Combats row through the controller: its setup fortify 0, a move and the
// arrival reach its machine (was BadEvent at the first fortify, the unit never drawn)
C5R_TEST(member_without_row_through_the_controller) {
    BridgeRig br;
    br.open({{"COLONIST2", 1}});
    CHECK_EQ(br.apply(ev(EventKind::Fortify, 0), 2), AnimCode::Ok);
    CHECK_EQ(br.apply(ev(EventKind::Move, 2, 3), 3), AnimCode::Ok);
    CHECK_EQ(br.state(0), 1400);
    CHECK_EQ(br.apply(ev(EventKind::Arrive, 2), 4), AnimCode::Ok);
    CHECK_EQ(br.state(0), 1);
    TimelineEvent w = act(ActionKind::Work, "BUILD_MINE");   // the pickaxe, 1940, which the test machine reads
    CHECK_EQ(br.apply(w, 5), AnimCode::Ok);
    CHECK_EQ(br.state(0), 1940);
    CHECK_EQ(br.apply(act(ActionKind::Work, ""), 6), AnimCode::Ok);   // the stop: 1000, back to idle
    CHECK_EQ(br.state(0), 1);
    CHECK_EQ(br.apply(act(ActionKind::Work, "BUILD_FARM"), 7), AnimCode::Ok);   // 1920: not read here: a no-op
    CHECK_EQ(br.r.c.lastApply().unread_requests, 1u);
    CHECK_EQ(br.state(0), 1);
}

C5R_TEST(refuses_paradrop) {
    BridgeTables t = tables();
    ActionBridge b(t);
    CHECK_EQ(code(b, act(ActionKind::Paradrop), artOf({{"M", 1}}), AnimCode::BadEvent), -1);
}

C5R_TEST(refuses_damage_the_figure_rule_refuses) {
    BridgeTables t = tables();
    ActionBridge b(t);
    std::vector<Request> out;
    UnitInput u = unitIn();
    u.max_hp = 0;
    CHECK_ANIM(b.requestsFor(ev(EventKind::Damage, 50, 0), u, artOf({{"M", 3}}), out), AnimCode::BadEvent);
    CHECK_ANIM(b.requestsFor(ev(EventKind::Damage, 50, 0), unitIn(), artOf({}), out), AnimCode::BadEvent);
}

C5R_TEST(work_table_text) {
    std::map<std::string, int32_t> t;
    CHECK_ANIM(parseWorkTable("# candidate\r\nENTITY_EVENT_MINE\t1940\n\nENTITY_EVENT_SHOVEL\t1910", t), AnimCode::Ok);
    CHECK_EQ(t.size(), static_cast<size_t>(2));
    CHECK_EQ(t["ENTITY_EVENT_SHOVEL"], 1910);
    CHECK_ANIM(parseWorkTable("ENTITY_EVENT_MINE 1940\n", t), AnimCode::BadLogicFile);       // no tab
    CHECK(t.empty());
    CHECK_ANIM(parseWorkTable("ENTITY_EVENT_MINE\t1400\n", t), AnimCode::BadLogicFile);      // not a work code
    CHECK_ANIM(parseWorkTable("ENTITY_EVENT_MINE\t19x0\n", t), AnimCode::BadLogicFile);      // not a number
    CHECK_ANIM(parseWorkTable("A\t1910\nA\t1920\n", t), AnimCode::BadLogicFile);             // twice
    CHECK_ANIM(parseWorkTable("\t1910\n", t), AnimCode::BadLogicFile);                       // empty event
    CHECK_ANIM(parseWorkTable("A\t1910\tx\n", t), AnimCode::BadLogicFile);                   // three fields
}

C5R_TEST(tables_from_a_database) {
    c5m::TsvTables db;
    c5m::Error e;
    CHECK(db.add("ArtDefine_UnitMemberCombats",
                 "UnitMemberType\tEnableActions\nART_M\tIdle Attack  Run\nART_N\t\\N\n", &e));
    BridgeTables t;
    CHECK_ANIM(loadBridgeTables(db, t), AnimCode::BadLogicFile);   // no Builds table
    CHECK(db.add("Builds", "Type\tEntityEvent\nBUILD_MINE\tENTITY_EVENT_MINE\nBUILD_X\t\\N\n", &e));
    CHECK_ANIM(loadBridgeTables(db, t), AnimCode::Ok);
    CHECK_EQ(t.words["ART_M"].size(), static_cast<size_t>(3));
    CHECK(t.words["ART_M"].count("Attack") == 1);
    CHECK(t.words.count("ART_N") == 1 && t.words["ART_N"].empty());
    CHECK_EQ(t.build_event["BUILD_MINE"], std::string("ENTITY_EVENT_MINE"));
    CHECK_EQ(t.build_event["BUILD_X"], std::string(""));
    c5m::TsvTables none;
    CHECK_ANIM(loadBridgeTables(none, t), AnimCode::BadLogicFile);
}

// ------------------------------------------------------------------------------------------------ through the controller
C5R_TEST(controller_takes_the_bridge_codes) {
    BridgeRig br;
    br.open({{"M", 3}});
    CHECK_EQ(br.state(0), 1);
    CHECK_EQ(br.apply(ev(EventKind::Fortify, 1), 2), AnimCode::Ok);
    CHECK_EQ(br.state(0), 1500);
    CHECK_EQ(br.apply(ev(EventKind::Fortify, 0), 3), AnimCode::Ok);
    CHECK_EQ(br.state(2), 1);
    CHECK_EQ(br.apply(ev(EventKind::Move, 1, 2), 4), AnimCode::Ok);
    CHECK_EQ(br.state(1), 1400);
    CHECK_EQ(br.apply(ev(EventKind::Arrive, 1), 5), AnimCode::Ok);
    CHECK_EQ(br.state(1), 1);
    CHECK_EQ(br.apply(act(ActionKind::AttackCity), 6), AnimCode::Ok);
    CHECK_EQ(br.state(0), 1160);
}

C5R_TEST(damage_takes_out_figures_from_the_last_backwards) {
    BridgeRig br;
    br.b.remove_fallen = false;   // 4404's first reading of X4 (the comparison option since #2147)
    br.open({{"M", 4}});
    // 0 -> 30: round(4 x 0.30) = 1 dead -> figure 3 falls
    CHECK_EQ(br.apply(ev(EventKind::Damage, 30, 0), 2), AnimCode::Ok);
    CHECK_EQ(br.state(3), 1200);
    CHECK_EQ(br.state(2), 1);
    // 30 -> 35: still 1 dead, nobody falls
    CHECK_EQ(br.apply(ev(EventKind::Damage, 35, 30), 3), AnimCode::Ok);
    CHECK_EQ(br.state(2), 1);
    // 35 -> 99: round(3.96) = 4, at most n - 1 = 3 below 100 %: figures 1 and 2 fall, 0 lives
    CHECK_EQ(br.apply(ev(EventKind::Damage, 99, 35), 4), AnimCode::Ok);
    CHECK_EQ(br.state(2), 1200);
    CHECK_EQ(br.state(1), 1200);
    CHECK_EQ(br.state(0), 1);
    // the dead keep their death program; an attack request reaches the living figure only
    CHECK_EQ(br.apply(act(ActionKind::Attack), 5), AnimCode::Ok);
    CHECK_EQ(br.state(0), 1100);
    CHECK_EQ(br.state(3), 1200);
    // a fall in damage revives nobody (open, unit-controller.md 14)
    CHECK_EQ(br.apply(ev(EventKind::Damage, 0, 99), 6), AnimCode::Ok);
    CHECK_EQ(br.state(1), 1200);
}

// 4406 r3 (premise change 5, #2141, accepted #2147; 4405 B's Q7): with remove_fallen (the default) the figures that fall are removed at once, with no
// death clip and no program; the living keep theirs and still take requests.
C5R_TEST(damage_removes_fallen_figures_with_the_option) {
    BridgeRig br;
    br.open({{"M", 4}});
    std::vector<Request> out;
    CHECK_ANIM(br.b.requestsFor(ev(EventKind::Damage, 30, 0), unitIn(), artOf({{"M", 4}}), out), AnimCode::Ok);
    CHECK_EQ(out.size(), static_cast<size_t>(1));
    CHECK(out[0].removes && !out[0].dies && out[0].value == 0);
    CHECK_EQ(br.apply(ev(EventKind::Damage, 30, 0), 2), AnimCode::Ok);
    CHECK_EQ(br.state(3), -1);                       // gone: no program
    CHECK_EQ(br.state(2), 1);
    CHECK_EQ(br.r.c.programs().size(), static_cast<size_t>(3));
    CHECK_EQ(br.apply(ev(EventKind::Damage, 99, 30), 3), AnimCode::Ok);
    CHECK_EQ(br.r.c.programs().size(), static_cast<size_t>(1));
    CHECK_EQ(br.state(0), 1);
    CHECK_EQ(br.apply(act(ActionKind::Attack), 4), AnimCode::Ok);
    CHECK_EQ(br.state(0), 1100);
    // the default is the removal (#2147); the 1200 reading stays as an option: damage_takes_out_figures_from_the_last_backwards
    ActionBridge plain(br.t);
    CHECK(plain.remove_fallen);
}

C5R_TEST(controller_refusal_through_the_bridge_names_the_member) {
    BridgeRig br;
    br.open({{"CIV", 2}});
    CHECK_EQ(br.apply(act(ActionKind::Attack), 2), AnimCode::BadEvent);
    const AnimStatus* s = br.r.c.status(0, 1);
    CHECK(s && s->text.find("CIV") != std::string::npos);
    CHECK(br.r.prog(0, 1, 0) == nullptr);   // no program for a failed unit (D118)
}

// ------------------------------------------------------------------------------------------------ the file kind
C5R_TEST(anti_air_round_trips_in_the_timeline_file) {
    c5r::units::UnitScene sc;
    c5r::units::PlayerInput p;
    p.player = 0;
    p.colour = "PLAYERCOLOR_ROME";
    p.art_style = "ARTSTYLE_EUROPEAN";
    p.era = "ERA_ANCIENT";
    sc.players.push_back(p);
    UnitInput u = unitIn();
    u.unit_type = "UNIT_ANTI_AIRCRAFT_GUN";
    sc.units.push_back(u);
    const std::string text = "c5anim\t1\nscene\t-\t7\tfree\nev\t0\t1\taction\tanti_air\t-\t*\nframe\t1\t0.0333333351\t-\t-\n";
    Timeline tl;
    TimelineStatus st = parseTimeline(text, &sc, tl);
    CHECK(st.ok());
    CHECK_EQ(tl.frames.size(), static_cast<size_t>(1));
    CHECK(tl.frames[0].events[0].action == ActionKind::AntiAir);
    CHECK_EQ(std::string(actionKindName(ActionKind::AntiAir)), std::string("anti_air"));
    std::string back;
    CHECK(writeTimeline(tl, &sc, back).ok());
    CHECK(back.find("\taction\tanti_air\t-\t*") != std::string::npos);
    // an argument on anti_air is refused as on the other non-work actions
    Timeline t2;
    st = parseTimeline("c5anim\t1\nscene\t-\t7\tfree\nev\t0\t1\taction\tanti_air\tX\t*\nframe\t1\t0.1\t-\t-\n", &sc, t2);
    CHECK(st.code == TimelineErr::BadValue);
}

C5R_TEST_MAIN
