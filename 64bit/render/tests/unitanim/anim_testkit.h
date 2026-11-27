// anim_testkit.h - helpers of the 4403 runtime tests (epic 0374): hand-built machines, a graph double standing in for
// 4409's evaluator, an art source and an in-memory file provider. No game data.
#pragma once
#include <cmath>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "c5models/art.h"
#include "c5models/logic.h"
#include "c5r/unitanim/controller.h"
#include "c5r/unitanim/graph.h"
#include "testkit.h"

#define CHECK_ANIM(st, want) do { ::c5r::unitanim::AnimStatus a_ = (st); if (a_.code != (want)) \
    ::c5r::test::fail(__FILE__, __LINE__, std::string("CHECK_ANIM(" #st "): want ") + ::c5r::unitanim::name(want) + \
                      ", got " + ::c5r::unitanim::name(a_.code) + ": " + a_.text); \
    else if (a_.code != ::c5r::unitanim::AnimCode::Ok && a_.text.empty()) \
    ::c5r::test::fail(__FILE__, __LINE__, "CHECK_ANIM(" #st "): status without a text"); } while (0)

#define CHECK_NEAR(a, b, tol) do { double x_ = (a), y_ = (b); if (!(std::fabs(x_ - y_) <= (tol))) \
    ::c5r::test::fail(__FILE__, __LINE__, "CHECK_NEAR(" #a ", " #b "): " + std::to_string(x_) + " vs " + \
                      std::to_string(y_)); } while (0)

namespace anim_test {

using namespace c5r::unitanim;

// ------------------------------------------------------------------------------------------- machines
inline c5m::FsmCondition cond(c5m::CondType t) {
    c5m::FsmCondition c;
    c.type = t;
    c.type_name = c5m::cond_type_name(t);
    return c;
}
inline c5m::FsmCondition checkPath(int path, int value, bool equal = true) {
    c5m::FsmCondition c = cond(c5m::CondType::CheckPathOption);
    c.method = equal ? 1 : 6;
    c.path = path;
    c.value = value;
    c.fvalue = static_cast<float>(value);
    return c;
}
inline c5m::FsmCondition setPath(int path, int value) {
    c5m::FsmCondition c = cond(c5m::CondType::EventSetPathOption);
    c.path = path;
    c.value = value;
    c.fvalue = static_cast<float>(value);
    return c;
}
inline c5m::FsmCondition randomPath(int path, int value, std::vector<c5m::FsmWeight> w = {}) {
    c5m::FsmCondition c = cond(c5m::CondType::EventRandomPathOption);
    c.path = path;
    c.value = value;
    c.fvalue = static_cast<float>(value);
    c.use_weights = !w.empty();
    c.weights = w;
    return c;
}
inline c5m::FsmCondition ecComplete(int ec) {
    c5m::FsmCondition c = cond(c5m::CondType::ECComplete);
    c.value = ec;
    return c;
}
inline c5m::FsmCondition stateTime(float s) {
    c5m::FsmCondition c = cond(c5m::CondType::CheckStateTime);
    c.method = 5;
    c.fvalue = s;
    return c;
}
inline c5m::FsmCondition roll(float p, bool invert = false) {
    c5m::FsmCondition c = cond(c5m::CondType::RandomRoll);
    c.fvalue = p;
    c.invert = invert;
    return c;
}
inline c5m::FsmCondition orOp() { return cond(c5m::CondType::OrOperator); }

inline c5m::FsmState state(int id, const std::string& asset, int action = 0) {
    c5m::FsmState s;
    s.id = id;
    s.type = "FGrannyState";
    s.name = "S" + std::to_string(id);
    s.asset = asset;
    s.action = action;
    return s;
}
inline c5m::FsmSocket socket(int from, int to, std::vector<c5m::FsmCondition> c) {
    c5m::FsmSocket s;
    s.from = from;
    s.to = to;
    s.conditions = c;
    return s;
}

// A small idle machine in the omni shape (unit-controller.md 6.2): 2 chooser -> 1 idle (R0 picks A/B); 1 -> 6 on a
// roll; 6 -> 3 fidget; 3 -> 1 on EC(1040); 1/3/6 -> 7 fortify on P4==1500; 7 -> 1 on P4==1000; 1 -> 9 attack on
// P4==1100 (S4=0), 9 -> 1 on EC(1100).
inline c5m::StateMachine idleMachine(float rollChance = 0.0007f) {
    c5m::StateMachine m;
    m.fsm = "FGrannyStateMachine";
    m.default_state = 2;
    m.states = {state(1, "idle.dge", 1000), state(2, ""), state(3, "fidget.dge", 1000), state(6, "idlecont.dge"),
                state(7, "fortify.dge", 1500), state(9, "attack.dge", 1100)};
    m.sockets = {
        socket(2, 7, {checkPath(4, 1500)}),
        socket(2, 1, {randomPath(0, 1, {{0, 0.6f}, {1, 0.4f}})}),
        socket(1, 7, {checkPath(4, 1500)}),
        socket(1, 9, {checkPath(4, 1100), setPath(4, 0)}),
        socket(1, 6, {roll(rollChance)}),
        socket(6, 3, {}),
        socket(3, 1, {checkPath(4, 1000), ecComplete(1040)}),
        socket(3, 7, {checkPath(4, 1500)}),
        socket(7, 1, {checkPath(4, 1000)}),
        socket(9, 1, {ecComplete(1100)}),
    };
    return m;
}

// ------------------------------------------------------------------------------------------- the graph double
// A graph of one or more sources (the DAG's Sources by ec); the source played is Sources[int(opts[select])] when the
// graph has several, else the first. Durations and loop flags per ec from the factory.
struct ClipInfo { float duration = 1; bool loop = true; };

class FakeGraph : public GraphInstance {
public:
    FakeGraph(std::vector<int32_t> ecs, std::vector<std::string> clips, std::map<int32_t, ClipInfo> info, int select)
        : ecs_(ecs), clips_(clips), info_(info), select_(select) {}
    void restart() override { t_ = prev_ = 0; time_ = 0; looped_ = false; }
    void advance(float dt, const PathOptions& o) override {
        const ClipInfo ci = info(o);
        prev_ = t_;
        looped_ = false;
        t_ += dt;
        time_ += dt;
        if (ci.loop && t_ >= ci.duration) {
            t_ = std::fmod(t_, ci.duration);
            looped_ = true;
        } else if (!ci.loop && t_ > ci.duration) {
            t_ = ci.duration;
        }
    }
    AnimStatus program(const PathOptions& o, PoseProgram& out) const override {
        out.clear();
        PoseNode n;
        size_t k = pick(o);
        n.clip = clips_[k];
        n.ec = ecs_[k];
        n.t = t_;
        out.nodes.push_back(n);
        return AnimStatus::Ok();
    }
    bool complete(const PathOptions& o) const override {
        const ClipInfo ci = info(o);
        return !ci.loop && t_ >= ci.duration - 1e-6f;
    }
    void active(const PathOptions& o, std::vector<ActiveSource>& out) const override {
        ActiveSource a;
        size_t k = pick(o);
        a.ec = ecs_[k];
        a.clip = clips_[k];
        a.t = t_;
        a.prev_t = prev_;
        a.looped = looped_;
        out.push_back(a);
    }
    float stateTime() const override { return time_; }

private:
    size_t pick(const PathOptions& o) const {
        if (ecs_.size() < 2 || select_ < 0) return 0;
        int k = static_cast<int>(o.v[select_]);
        return k >= 0 && static_cast<size_t>(k) < ecs_.size() ? static_cast<size_t>(k) : 0;
    }
    ClipInfo info(const PathOptions& o) const {
        auto it = info_.find(ecs_[pick(o)]);
        return it == info_.end() ? ClipInfo() : it->second;
    }
    std::vector<int32_t> ecs_;
    std::vector<std::string> clips_;
    std::map<int32_t, ClipInfo> info_;
    int select_;
    float t_ = 0, prev_ = 0, time_ = 0;
    bool looped_ = false;
};

// Binds a DagFile's dags[0].source_ecs through the member's EcMap (EcNotBound for a missing ec; UnknownNode when the
// graph holds a node of type Unknown), as 4409's bindGraph does for these two statuses.
class FakeFactory : public GraphFactory {
public:
    std::map<int32_t, ClipInfo> info;
    int select = 0;      // the path that selects among several sources
    int binds = 0;
    AnimStatus bind(const c5m::DagFile& f, const EcMap& ecs, const std::string& name,
                    std::unique_ptr<GraphInstance>& out) override {
        ++binds;
        out.reset();
        const c5m::DagGraph& g = f.dags.at(0);
        for (auto& n : g.nodes)
            if (n.type == c5m::DagNodeType::Unknown)
                return AnimStatus::Fail(AnimCode::UnknownNode, name + ": node '" + n.name + "'");
        std::vector<std::string> clips;
        for (int32_t ec : g.source_ecs) {
            std::string clip;
            for (size_t i = 0; i < ecs.ecs.size(); ++i)
                if (ecs.ecs[i] == ec) clip = ecs.clips[i];
            if (clip.empty()) return AnimStatus::Fail(AnimCode::EcNotBound, name + ": ec " + std::to_string(ec));
            clips.push_back(clip);
        }
        out.reset(new FakeGraph(g.source_ecs, clips, info, select));
        return AnimStatus::Ok();
    }
};

inline c5m::DagFile dag(std::vector<int32_t> ecs) {
    c5m::DagFile f;
    c5m::DagGraph g;
    g.source_ecs = ecs;
    c5m::DagNode root;
    root.type = c5m::DagNodeType::Subgraph;
    root.name = "root";
    g.nodes.push_back(root);
    f.dags.push_back(g);
    return f;
}

// ------------------------------------------------------------------------------------------- art and files
class MemFiles : public c5m::FileProvider {
public:
    std::map<std::string, std::string> files;
    bool read(const std::string& name, c5m::Bytes& out, std::string* origin) override {
        auto it = files.find(name);
        if (it == files.end()) return false;
        out.assign(it->second.begin(), it->second.end());
        if (origin) *origin = "mem:" + name;
        return true;
    }
};

inline c5r::units::MemberArt memberArt(const std::string& member, int count, const std::string& machine,
                                       std::vector<std::pair<std::string, int>> clips,
                                       std::vector<std::string> triggers = {}) {
    c5r::units::MemberArt m;
    m.member = member;
    m.count = count;
    m.scale = 0.14f;
    m.files.fxsxml.name = member + ".fxsxml";
    m.files.fxsxml.found = true;
    if (!machine.empty()) {
        c5m::FileRef r;
        r.name = machine;
        r.found = true;
        m.files.state_machines.push_back(r);
    }
    for (auto& c : clips) {
        c5m::ResolvedAnimation a;
        a.file.name = c.first;
        a.file.found = true;
        a.ec = {c.second};
        a.ec_text = std::to_string(c.second);
        m.files.animations.push_back(a);
    }
    for (auto& t : triggers) {
        c5m::ResolvedTrigger r;
        r.file.name = t;
        r.file.found = true;
        m.files.timed_triggers.push_back(r);
    }
    return m;
}

class FakeArt : public ArtSource {
public:
    std::map<std::string, c5r::units::UnitArt> rows;   // by unit_type
    AnimStatus resolve(const c5r::units::UnitInput& u, const c5r::units::PlayerInput*,
                       c5r::units::UnitArt& out) override {
        auto it = rows.find(u.unit_type);
        if (it == rows.end()) return AnimStatus::Fail(AnimCode::UnitFailed, u.unit_type + ": NoArtRow");
        out = it->second;
        return AnimStatus::Ok();
    }
};

inline std::vector<std::pair<std::string, int>> warriorClips() {
    return {{"w_idlea.gr2", 1000}, {"w_idleb.gr2", 2000}, {"w_fidgeta.gr2", 1040}, {"w_fortify_idle.gr2", 1520},
            {"w_attacka.gr2", 1100}};
}

// A controller over the idle machine: one unit type "W" (member "M", `figures` figures).
struct Rig {
    MemFiles files;
    FakeArt art;
    FakeFactory graphs;
    Controller c;
    ControllerOptions opt;
    void open(int figures = 3, float rollChance = 0.0007f, const std::string& machineName = "omni.fsmxml") {
        c5r::units::UnitArt a;
        a.art_row = "ART_W";
        a.members.push_back(memberArt("M", figures, machineName, warriorClips()));
        art.rows["W"] = a;
        graphs.info[1000] = ClipInfo{6.0f, true};
        graphs.info[2000] = ClipInfo{6.0f, true};
        graphs.info[1040] = ClipInfo{5.0f, false};
        graphs.info[1520] = ClipInfo{4.0f, true};
        graphs.info[1100] = ClipInfo{1.0f, false};
        c.open(files, art, graphs, opt);
        CHECK_ANIM(c.logic()->putMachine(machineName, idleMachine(rollChance)), AnimCode::Ok);
        c.logic()->putGraph("idle.dge", dag({1000, 2000}));
        c.logic()->putGraph("idlecont.dge", dag({1000, 2000}));
        c.logic()->putGraph("fidget.dge", dag({1040}));
        c.logic()->putGraph("fortify.dge", dag({1520}));
        c.logic()->putGraph("attack.dge", dag({1100}));
    }
    static c5r::units::UnitInput unit(int player, int id, const std::string& type = "W") {
        c5r::units::UnitInput u;
        u.player = player;
        u.id = id;
        u.unit_type = type;
        return u;
    }
    static TimelineFrame frame(uint64_t seq, double dt) {
        TimelineFrame f;
        f.seq = seq;
        f.dt = dt;
        return f;
    }
    const FigureProgram* prog(int player, int id, uint32_t figure) const {
        for (auto& p : c.programs())
            if (p.player == player && p.id == id && p.figure == figure) return &p;
        return nullptr;
    }
};

}  // namespace anim_test
