// render/tests/unitanim/test_controller_start.cpp - issue 4406 round 2 (epic 0374): two rules of a state's start in
// the controller (unit-controller.md 15.8), over hand-built logic and a graph double that honours restartFrom:
//   - a state entered from another hands the previous state's clip times to its ContinueOffset sources (enterState
//     calls restartFrom with the previous graph's active sources); a source without it starts at 0;
//   - an air-domain figure is created parked: path 4 = 1000 after the default chooser's own S4=1400, followed at once
//     (airplane shape: 6 -> 1 run, 1 -> 8 on P4==1000, 8 -> 7 idle); a land figure on the same machine stays in the
//     run the default chose, and a machine that never tests P4==1000 is left alone.
#include <cmath>
#include <set>

#include "anim_testkit.h"

using namespace c5r::unitanim;
using namespace anim_test;

namespace {

const double kH = 1.0 / 30.0;

// One looping source per graph; `cont` marks a ContinueOffset 1 source (as 4409's evaluator reads the node).
class ContGraph : public GraphInstance {
public:
    ContGraph(int32_t ec, std::string clip, float duration, bool cont)
        : ec_(ec), clip_(std::move(clip)), dur_(duration), cont_(cont) {}
    void restart() override { t_ = prev_ = time_ = 0; start_ = 0; }
    void restartFrom(const std::vector<ActiveSource>& previous) override {
        restart();
        if (!cont_) return;
        for (auto& a : previous)
            if (a.ec == ec_) {
                t_ = start_ = a.t;
                break;
            }
    }
    void advance(float dt, const PathOptions&) override {
        prev_ = t_;
        t_ = std::fmod(t_ + dt, dur_);
        time_ += dt;
    }
    AnimStatus program(const PathOptions&, PoseProgram& out) const override {
        out.clear();
        PoseNode n;
        n.clip = clip_;
        n.ec = ec_;
        n.t = t_;
        out.nodes.push_back(n);
        return AnimStatus::Ok();
    }
    bool complete(const PathOptions&) const override { return false; }
    void active(const PathOptions&, std::vector<ActiveSource>& out) const override {
        ActiveSource a;
        a.ec = ec_;
        a.clip = clip_;
        a.t = t_;
        a.prev_t = prev_;
        a.duration = dur_;
        out.push_back(a);
    }
    float stateTime() const override { return time_; }

private:
    int32_t ec_;
    std::string clip_;
    float dur_;
    bool cont_;
    float t_ = 0, prev_ = 0, time_ = 0, start_ = 0;
};

class ContFactory : public GraphFactory {
public:
    std::set<std::string> continues;   // graph names whose source has ContinueOffset 1
    AnimStatus bind(const c5m::DagFile& f, const EcMap& ecs, const std::string& name,
                    std::unique_ptr<GraphInstance>& out) override {
        out.reset();
        const int32_t ec = f.dags.at(0).source_ecs.at(0);
        std::string clip;
        for (size_t i = 0; i < ecs.ecs.size(); ++i)
            if (ecs.ecs[i] == ec) clip = ecs.clips[i];
        if (clip.empty()) return AnimStatus::Fail(AnimCode::EcNotBound, name + ": ec " + std::to_string(ec));
        out.reset(new ContGraph(ec, clip, 6.0f, continues.count(name) > 0));
        return AnimStatus::Ok();
    }
};

struct StartRig {
    MemFiles files;
    FakeArt art;
    ContFactory graphs;
    Controller c;
    ControllerOptions opt;
    void open(const c5m::StateMachine& m, const std::string& domain,
              std::vector<std::pair<std::string, int>> clips, std::map<std::string, int32_t> dges) {
        c5r::units::UnitArt a;
        a.art_row = "ART_X";
        c5r::units::MemberArt ma = memberArt("M", 2, "x.fsmxml", clips);
        ma.domain = domain;
        a.members.push_back(ma);
        art.rows["X"] = a;
        c.open(files, art, graphs, opt);
        CHECK_ANIM(c.logic()->putMachine("x.fsmxml", m), AnimCode::Ok);
        for (auto& d : dges) c.logic()->putGraph(d.first, dag({d.second}));
    }
    AnimStatus frame(uint64_t seq, double dt, std::vector<TimelineEvent> evs = {}) {
        TimelineFrame f;
        f.seq = seq;
        f.dt = dt;
        f.events = evs;
        return c.apply(f);
    }
    const FigureProgram* prog(uint32_t figure) const {
        for (auto& p : c.programs())
            if (p.player == 0 && p.id == 1 && p.figure == figure) return &p;
        return nullptr;
    }
};

TimelineEvent ev(EventKind k) {
    TimelineEvent e;
    e.kind = k;
    e.player = 0;
    e.id = 1;
    return e;
}
TimelineEvent createEv() {
    TimelineEvent e = ev(EventKind::Create);
    e.unit.player = 0;
    e.unit.id = 1;
    e.unit.unit_type = "X";
    return e;
}

// 2 chooser -> 1 idle (idle.dge, 1000); 1 -> 4 on P4==1500 (hold.dge: the same idle clip, ContinueOffset 1 when the
// test says so); 4 -> 1 on P4==1000 (idle.dge has no ContinueOffset: back at 0).
c5m::StateMachine continueMachine() {
    c5m::StateMachine m;
    m.fsm = "FGrannyStateMachine";
    m.default_state = 2;
    m.states = {state(1, "idle.dge", 1000), state(2, ""), state(4, "hold.dge", 1500)};
    m.sockets = {socket(2, 1, {}), socket(1, 4, {checkPath(4, 1500)}), socket(4, 1, {checkPath(4, 1000)})};
    return m;
}

// The airplane shape (unit-controller.md 6.2 / 15.8): 6 LAUNCH (chooser) -> 1 RUN writing S4=1400; 1 -> 8 on
// P4==1000; 8 RUN_TO_IDLE -> 7 IDLE (empty socket); 7 -> 1 on P4==1400.
c5m::StateMachine planeMachine(bool testsLanding = true) {
    c5m::StateMachine m;
    m.fsm = "FGrannyStateMachine";
    m.default_state = 6;
    m.states = {state(1, "run.dge", 1400), state(6, ""), state(7, "idle.dge", 1000), state(8, "idle.dge", 0)};
    m.sockets = {socket(6, 1, {setPath(4, 1400)}), socket(7, 1, {checkPath(4, 1400)})};
    if (testsLanding) {
        m.sockets.push_back(socket(1, 8, {checkPath(4, 1000)}));
        m.sockets.push_back(socket(8, 7, {}));
    }
    return m;
}

std::vector<std::pair<std::string, int>> planeClips() { return {{"p_idle.gr2", 1000}, {"p_run.gr2", 1400}}; }

}  // namespace

C5R_TEST(entered_state_continues_previous_clip_time) {
    for (int cont = 0; cont < 2; ++cont) {
        StartRig r;
        if (cont) r.graphs.continues.insert("hold.dge");
        r.open(continueMachine(), "", {{"m_idle.gr2", 1000}}, {{"idle.dge", 1000}, {"hold.dge", 1000}});
        CHECK_ANIM(r.frame(1, 0, {createEv()}), AnimCode::Ok);
        CHECK_EQ(r.prog(0)->state, 1);
        for (uint64_t s = 2; s < 32; ++s) CHECK_ANIM(r.frame(s, kH), AnimCode::Ok);   // 30 frames: t 1.0
        CHECK_NEAR(r.prog(0)->program.nodes[0].t, 1.0, 1e-4);
        TimelineEvent fo = ev(EventKind::Fortify);
        fo.a = 1;
        CHECK_ANIM(r.frame(40, kH, {fo}), AnimCode::Ok);   // the step advances idle to 1.0333, then 1 -> 4
        CHECK_EQ(r.prog(0)->state, 4);
        CHECK_EQ(r.prog(0)->program.nodes[0].ec, 1000);
        // ContinueOffset 1: the held source takes over the idle's clip time; without it, it starts at 0
        CHECK_NEAR(r.prog(0)->program.nodes[0].t, cont ? 1.0 + kH : 0.0, 1e-4);
        CHECK_NEAR(r.prog(1)->program.nodes[0].t, cont ? 1.0 + kH : 0.0, 1e-4);
        CHECK_ANIM(r.frame(41, kH), AnimCode::Ok);
        CHECK_NEAR(r.prog(0)->program.nodes[0].t, cont ? 1.0 + 2 * kH : kH, 1e-4);
        TimelineEvent un = ev(EventKind::Fortify);
        un.a = 0;
        CHECK_ANIM(r.frame(42, kH, {un}), AnimCode::Ok);
        CHECK_EQ(r.prog(0)->state, 1);
        CHECK_NEAR(r.prog(0)->program.nodes[0].t, 0.0, 1e-6);   // idle.dge: no ContinueOffset
    }
}

C5R_TEST(air_figure_created_parked) {
    StartRig r;
    r.open(planeMachine(), "Air", planeClips(), {{"run.dge", 1400}, {"idle.dge", 1000}});
    CHECK_ANIM(r.frame(1, 0, {createEv()}), AnimCode::Ok);
    for (uint32_t k = 0; k < 2; ++k) {
        CHECK(r.prog(k) != nullptr);
        CHECK_EQ(r.prog(k)->state, 7);
        CHECK_EQ(r.prog(k)->program.nodes[0].ec, 1000);
        CHECK_NEAR(r.prog(k)->program.nodes[0].t, 0.0, 1e-6);
    }
    CHECK_ANIM(r.frame(2, kH), AnimCode::Ok);
    CHECK_EQ(r.prog(0)->state, 7);   // parked stays parked
    CHECK_NEAR(r.prog(0)->program.nodes[0].t, kH, 1e-6);
    CHECK_ANIM(r.frame(3, kH, {ev(EventKind::Move)}), AnimCode::Ok);   // flight is requested
    CHECK_EQ(r.prog(0)->state, 1);
    CHECK_EQ(r.prog(0)->program.nodes[0].ec, 1400);
}

C5R_TEST(land_figure_and_machine_without_landing_keep_the_default) {
    {
        StartRig r;   // not an air member: the default's S4=1400 stands
        r.open(planeMachine(), "", planeClips(), {{"run.dge", 1400}, {"idle.dge", 1000}});
        CHECK_ANIM(r.frame(1, 0, {createEv()}), AnimCode::Ok);
        CHECK_EQ(r.prog(0)->state, 1);
        CHECK_EQ(r.prog(0)->program.nodes[0].ec, 1400);
    }
    {
        StartRig r;   // an air member whose machine never tests P4==1000 (the missile shape): no start request
        r.open(planeMachine(false), "Air", planeClips(), {{"run.dge", 1400}, {"idle.dge", 1000}});
        CHECK_ANIM(r.frame(1, 0, {createEv()}), AnimCode::Ok);
        CHECK_EQ(r.prog(0)->state, 1);
        CHECK_EQ(r.prog(0)->program.nodes[0].ec, 1400);
    }
}

C5R_TEST_MAIN
