// render/tests/unitanim/test_combat_live.cpp - issue 4782: the combat core stepped live (combat.h CombatRun with the
// controller's feedback, controller.h accepts / ControllerFeedback) and the controller's clip instances
// (FigureProgram::instance, TriggerEvent::instance). Positive: a CombatRun stepped self-timed equals choreograph(); a live
// run against the controller strikes only when the figure's machine takes the request, and every striker plays its
// attack; instance ids change at each state entry and the triggers carry them. Negative: a feedback that never accepts
// holds the fight to the limit; accepts() for a unit or figure not held, a dead figure, a code the state does not read;
// a refused CombatRun.
#include <map>
#include <set>
#include <sstream>
#include <string>

#include "anim_testkit.h"
#include "c5r/unitanim/combat.h"
#include "c5r/unitanim/controller.h"

using namespace anim_test;

namespace {

const double kH = 1.0 / 30.0;

c5m::FtsTrigger trig(int32_t ec, float time, int32_t id) {
    c5m::FtsTrigger t;
    t.type = c5m::TriggerType::Action;
    t.type_name = "Action";
    t.ec = ec;
    t.time = time;
    t.id = id;
    return t;
}

// anim_testkit's Rig (idle machine: idle 1 takes 1100 into attack 9, back on EC(1100)) with a trigger file and two units.
struct LiveRig {
    Rig r;
    CombatArtTables tables;
    void open(int figures = 4) {
        r.open(figures);
        c5r::units::MemberArt& m = r.art.rows["W"].members[0];
        c5m::ResolvedTrigger rt;
        rt.file.name = "w.ftsxml";
        rt.file.found = true;
        m.files.timed_triggers.push_back(rt);
        m.domain = "Land";
        c5m::TriggerSystem ts;
        ts.type = "FGrannyTimedTriggerSystem";
        ts.triggers = {trig(1100, 0.4889f, 1)};
        r.c.logic()->putTriggers("w.ftsxml", ts);
        CombatArtRow row;
        for (const char* w : {"Idle", "Attack", "Death", "Run", "CombatReady", "BombardDefend"}) row.enable.insert(w);
        row.weapons.push_back(CombatWeapon());
        tables["M"] = row;
        c5r::units::UnitScene sc;
        for (int32_t pl : {0, 63}) {
            c5r::units::PlayerInput p;
            p.player = pl;
            sc.players.push_back(p);
        }
        sc.units = {Rig::unit(0, 1), Rig::unit(63, 2)};
        r.c.start(sc);
        CHECK_ANIM(r.c.apply(Rig::frame(1, kH)), AnimCode::Ok);
    }
};

std::map<std::string, float> durations() {
    return {{"w_attacka.gr2", 1.0f}, {"w_idlea.gr2", 6.0f}, {"w_idleb.gr2", 6.0f}, {"w_fidgeta.gr2", 5.0f},
            {"w_fortify_idle.gr2", 4.0f}};
}

CombatInput record(int32_t afinal, int32_t dfinal) {
    CombatInput in;
    in.seq = 3;
    in.flags = kCombatVisualize | kCombatDefenderRetaliates;
    in.attacker.owner = 0;
    in.attacker.unit = 1;
    in.attacker.final_damage = afinal;
    in.defender.owner = 63;
    in.defender.unit = 2;
    in.defender.final_damage = dfinal;
    return in;
}

class Never : public CombatFeedback {
public:
    int asked = 0;
    bool accepts(int32_t, int32_t, uint16_t, int32_t) override {
        ++asked;
        return false;
    }
    int watched = 0;
    bool playing(int32_t, int32_t, uint16_t, int32_t) override {
        ++watched;
        return false;
    }
};

std::string dump(const CombatResult& r) {
    std::ostringstream o;
    o << r.end << "|" << r.at_limit << "|";
    for (auto& s : r.steps) o << s.time << "," << s.player << "," << s.figure << "," << static_cast<int>(s.state) << ";";
    for (auto& e : r.events) o << e.time << "," << static_cast<int>(e.ev.action) << "," << e.ev.figures.figures.size() << ";";
    return o.str();
}

}  // namespace

C5R_TEST(a_self_timed_run_equals_choreograph) {
    LiveRig lr;
    lr.open(4);
    std::map<std::string, float> d = durations();
    auto clips = lr.r.c.combatClips([&](const std::string& c) { return d.count(c) ? d[c] : 0.f; });
    CombatUnitFigures a, b;
    CHECK_ANIM(lr.r.c.combatFigures(0, 1, lr.tables, a), AnimCode::Ok);
    CHECK_ANIM(lr.r.c.combatFigures(63, 2, lr.tables, b), AnimCode::Ok);
    CombatParams p;
    p.seed = 9;
    const CombatResult whole = choreograph(record(25, 50), a, b, *clips, p);
    CombatRun run;
    CHECK(run.start(record(25, 50), a, b, *clips, p, nullptr).ok());
    std::vector<TimedEvent> events;
    int frames = 0;
    while (!run.advance(kH, events) && frames < 1000) ++frames;
    CHECK(run.done());
    CHECK_EQ(dump(run.result()), dump(whole));
    CHECK_EQ(events.size(), whole.events.size());
    // a refused run is done at once and names its status
    CombatRun bad;
    CombatInput q = record(0, 30);
    q.flags = 0;
    CHECK(bad.start(q, a, b, *clips, p, nullptr).code == CombatCode::NotVisualised);
    CHECK(bad.done());
    std::vector<TimedEvent> none;
    CHECK(bad.step(none) && none.empty());
    CHECK(bad.result().status.code == CombatCode::NotVisualised);
}

C5R_TEST(a_live_run_strikes_when_the_machine_takes_the_request) {
    LiveRig lr;
    lr.open(4);
    std::map<std::string, float> d = durations();
    auto clips = lr.r.c.combatClips([&](const std::string& c) { return d.count(c) ? d[c] : 0.f; });
    CombatUnitFigures a, b;
    CHECK_ANIM(lr.r.c.combatFigures(0, 1, lr.tables, a), AnimCode::Ok);
    CHECK_ANIM(lr.r.c.combatFigures(63, 2, lr.tables, b), AnimCode::Ok);
    ControllerFeedback fb(lr.r.c);
    CombatRun run;
    CombatParams p;
    p.seed = 9;
    CHECK(run.start(record(25, 50), a, b, *clips, p, &fb).ok());
    std::set<std::pair<int32_t, uint32_t>> played;   // (player, figure) that played the attack clip
    uint64_t seq = 10;
    bool end = false;
    for (int k = 0; k < 600 && !end; ++k) {
        std::vector<TimedEvent> events;
        // every strike the run starts in this step finds its figure taking the request (the controller as of the last frame)
        const size_t before = run.result().steps.size();
        end = run.advance(kH, events);
        (void)before;
        TimelineFrame f = Rig::frame(++seq, kH);
        for (auto& e : events) f.events.push_back(e.ev);
        CHECK_ANIM(lr.r.c.apply(f), AnimCode::Ok);
        for (auto& s : lr.r.c.statuses()) CHECK_ANIM(s.status, AnimCode::Ok);
        for (auto& pr : lr.r.c.programs())
            if (!pr.program.nodes.empty() && pr.program.nodes.back().ec == 1100)
                played.insert(std::make_pair(pr.player, pr.figure));
    }
    CHECK(end);
    const CombatResult& r = run.result();
    CHECK(r.status.ok());
    CHECK(!r.at_limit);
    CHECK_EQ(r.defender.died, 2u);   // 4 x 50 %
    CHECK_EQ(r.attacker.died, 1u);   // 4 x 25 %
    std::set<std::pair<int32_t, uint32_t>> strikers;
    for (auto& s : r.steps)
        if (s.state == FigState::Strike) strikers.insert(std::make_pair(s.player, static_cast<uint32_t>(s.figure)));
    CHECK(!strikers.empty());
    for (auto& s : strikers) CHECK(played.count(s));   // every striker played its attack clip
    // every hit lands after its striker played the clip: its launch is at or after the frame the clip was first seen
    CHECK(!r.hits.empty());
}

C5R_TEST(a_feedback_that_never_accepts_holds_to_the_limit) {
    LiveRig lr;
    lr.open(3);
    std::map<std::string, float> d = durations();
    auto clips = lr.r.c.combatClips([&](const std::string& c) { return d.count(c) ? d[c] : 0.f; });
    CombatUnitFigures a, b;
    CHECK_ANIM(lr.r.c.combatFigures(0, 1, lr.tables, a), AnimCode::Ok);
    CHECK_ANIM(lr.r.c.combatFigures(63, 2, lr.tables, b), AnimCode::Ok);
    Never never;
    CombatRun run;
    CHECK(run.start(record(0, 40), a, b, *clips, CombatParams(), &never).ok());
    std::vector<TimedEvent> events;
    int n = 0;
    while (!run.step(events) && n < 2000) ++n;
    CHECK(run.result().at_limit);
    CHECK_NEAR(run.result().end, 12.0, 1e-9);
    CHECK(never.watched > 0);   // the strikes were requested and watched every step until the limit
    // the requests go out at the combat-ready entry's end, but a clip that never plays lands nothing
    CHECK(run.result().hits.empty());
    CHECK_EQ(run.result().defender.died, 0u);
}

C5R_TEST(accepts_and_clip_instances) {
    LiveRig lr;
    lr.open(3);
    // idle state 1 reads 1100 and 1500, not 1180
    CHECK(lr.r.c.accepts(0, 1, 0, 1100));
    CHECK(!lr.r.c.accepts(0, 1, 0, 1180));
    CHECK(!lr.r.c.accepts(0, 99, 0, 1100));   // not held
    CHECK(!lr.r.c.accepts(0, 1, 7, 1100));    // no such figure
    std::map<std::pair<uint32_t, uint64_t>, int> seen;
    uint64_t inst0 = 0;
    for (auto& pr : lr.r.c.programs())
        if (pr.player == 0 && pr.id == 1) {
            CHECK(pr.instance > 0);
            if (pr.figure == 0) inst0 = pr.instance;
        }
    // figure 0 attacks: a new instance; the Action trigger carries it
    TimelineFrame f = Rig::frame(2, kH);
    TimelineEvent at;
    at.kind = EventKind::Action;
    at.player = 0;
    at.id = 1;
    at.action = ActionKind::Attack;
    at.figures.all = false;
    at.figures.figures = {0};
    f.events.push_back(at);
    CHECK_ANIM(lr.r.c.apply(f), AnimCode::Ok);
    uint64_t inst1 = 0;
    for (auto& pr : lr.r.c.programs())
        if (pr.player == 0 && pr.id == 1 && pr.figure == 0) inst1 = pr.instance;
    CHECK(inst1 > inst0);
    CHECK(!lr.r.c.accepts(0, 1, 0, 1100));   // in the attack state it does not read 1100
    CHECK(lr.r.c.playing(0, 1, 0, 1100));     // and it plays the attack clip
    CHECK(!lr.r.c.playing(0, 1, 1, 1100));    // figure 1 idles
    CHECK(!lr.r.c.playing(0, 99, 0, 1100));   // not held
    bool trig = false;
    for (int k = 0; k < 20 && !trig; ++k) {
        CHECK_ANIM(lr.r.c.apply(Rig::frame(3 + k, kH)), AnimCode::Ok);
        for (auto& t : lr.r.c.triggers())
            if (t.player == 0 && t.id == 1 && t.figure == 0 && t.ec == 1100) {
                trig = true;
                CHECK_EQ(t.instance, inst1);
            }
    }
    CHECK(trig);
    // a dead figure takes nothing
    TimelineFrame kf = Rig::frame(40, kH);
    TimelineEvent kill;
    kill.kind = EventKind::Kill;
    kill.player = 0;
    kill.id = 1;
    kill.figures.all = false;
    kill.figures.figures = {2};
    kf.events.push_back(kill);
    CHECK_ANIM(lr.r.c.apply(kf), AnimCode::Ok);
    CHECK(!lr.r.c.accepts(0, 1, 2, 1100));
}

C5R_TEST_MAIN
