// render/tests/unitanim/test_controller_drawn.cpp - issue 4468 (epic 0375): the controller with a drawn set
// (controller.h apply(frame, drawn); unit-controller.md 18). Over the real graph evaluator (bindGraph) and hand-built
// logic (no game file, D83), controllers take the same timeline: A draws every unit on every frame, B only the units of
// a schedule (units hidden and shown again, events and failures while hidden, a unit removed and created again).
// 0450: a unit B does not draw holds its clock (the EXE's rule, unit-controller.md 18, settled by 0442 [L]), so on the
// frames B draws a unit its programs and triggers are byte-equal to those of a controller that draws it on every frame
// of the timeline with dt 0 on the frames B did not draw it, and its status is equal on every frame (before 0450: equal
// to A's). B builds no program for a hidden unit unless its graph's program can fail (GraphInstance::programMayFail),
// and then the failure lands on the frame it happened.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "anim_testkit.h"

using namespace c5r::unitanim;
using namespace anim_test;
using c5m::DagNodeType;

namespace {

// ------------------------------------------------------------------------------------------- real graphs (bindGraph)
c5m::DagEdge edge(const char* name, int32_t from) {
    c5m::DagEdge e;
    e.name = name;
    e.input_node = from;
    return e;
}
c5m::DagNode node(DagNodeType t, const char* type_name, std::vector<c5m::DagEdge> in = {}) {
    c5m::DagNode n;
    n.type = t;
    n.type_name = std::string("TypeMarker_NodeData") + type_name;
    n.name = type_name;
    n.has_node_data = true;
    n.inputs = std::move(in);
    return n;
}
c5m::DagNode source(int32_t index, int32_t loops, bool cont) {
    c5m::DagNode n = node(DagNodeType::PoseAnimSource, "PoseAnimSource", {edge("Speed", -1), edge("Offset", -1)});
    n.source_index = index;
    n.loop_count = loops;
    n.clamped_loop = true;
    n.continue_offset = cont;
    n.has_continue_offset = true;
    n.data_edge_count = 1;
    n.speed = 1;
    return n;
}
c5m::DagNode callback(int hint, float value) {
    c5m::DagNode n = node(DagNodeType::ScalarCallback, "ScalarCallback", {edge("Input (0)", -1), edge("Input (1)", -1)});
    n.callback_hint = hint;
    n.callback_value = value;
    n.has_callback_value = true;
    return n;
}
c5m::DagFile file(c5m::DagGraph g) {
    g.root = 0;
    c5m::DagFile f;
    f.dags.push_back(g);
    return f;
}
// Subgraph -> Selection(path `path`, default `def`) over two looping sources (ContinueOffset 1).
c5m::DagFile selGraph(int32_t ecA, int32_t ecB, int path, int32_t def) {
    c5m::DagGraph g;
    g.source_ecs = {ecA, ecB};
    g.nodes.push_back(node(DagNodeType::Subgraph, "Subgraph", {edge("Pose out", 1)}));                    // 0
    g.nodes.push_back(node(DagNodeType::Selection, "Selection",
                           {edge("Selector", 2), edge("Pose in (0)", 3), edge("Pose in (1)", 4)}));       // 1
    g.nodes.back().default_edge = def;
    g.nodes.push_back(callback(7, static_cast<float>(path)));                                             // 2
    g.nodes.push_back(source(0, 0, true));                                                                // 3
    g.nodes.push_back(source(1, 0, true));                                                                // 4
    return file(g);
}
// Subgraph -> one source.
c5m::DagFile oneGraph(int32_t ec, int32_t loops) {
    c5m::DagGraph g;
    g.source_ecs = {ec};
    g.nodes.push_back(node(DagNodeType::Subgraph, "Subgraph", {edge("Pose out", 1)}));
    g.nodes.push_back(source(0, loops, false));
    return file(g);
}
// The fidget: Blend(Param = hint 5 over 0.3 s, From = the idle selection on path 0, To = the fidget, once).
c5m::DagFile fidgetGraph() {
    c5m::DagGraph g;
    g.source_ecs = {1000, 2000, 1040};
    g.nodes.push_back(node(DagNodeType::Subgraph, "Subgraph", {edge("Pose out", 1)}));                    // 0
    g.nodes.push_back(node(DagNodeType::Blend, "Blend",
                           {edge("Param", 3), edge("From", 2), edge("To", 4), edge("Mask", -1)}));         // 1
    g.nodes.push_back(node(DagNodeType::Selection, "Selection",
                           {edge("Selector", 5), edge("Pose in (0)", 6), edge("Pose in (1)", 7)}));        // 2
    g.nodes.back().default_edge = 0;
    g.nodes.push_back(callback(5, 0.3f));                                                                 // 3
    g.nodes.push_back(source(2, 1, false));                                                               // 4 fidget
    g.nodes.push_back(callback(7, 0));                                                                    // 5
    g.nodes.push_back(source(0, 0, true));                                                                // 6
    g.nodes.push_back(source(1, 0, true));                                                                // 7
    return file(g);
}
// A Blend whose To edge is missing: binds (a source is reachable), its program fails once the weight reaches To.
// A Blend whose To has no edge (0439: the From alone); with noFrom, From has none either (no source: NoProgress).
c5m::DagFile halfGraph(int32_t ec, bool noFrom) {
    c5m::DagGraph g;
    g.source_ecs = {ec};
    g.nodes.push_back(node(DagNodeType::Subgraph, "Subgraph", {edge("Pose out", 1)}));
    g.nodes.push_back(node(DagNodeType::Blend, "Blend",
                           {edge("Param", 2), edge("From", noFrom ? -1 : 3), edge("To", -1), edge("Mask", -1)}));
    g.nodes.push_back(callback(5, 0.1f));
    g.nodes.push_back(source(0, 0, false));
    return file(g);
}

class RealFactory : public GraphFactory {
public:
    GraphShare* share = nullptr;   // 0470: bind through a GraphShare (the product's ClipGraphFactory does)
    AnimStatus bind(const c5m::DagFile& f, const EcMap& ecs, const std::string& name,
                    std::unique_ptr<GraphInstance>& out) override {
        EcMap m = ecs;   // the test's members carry no durations: every clip 6 s, the fidget 2 s, the attack 1 s
        m.durations.clear();
        for (int32_t ec : m.ecs) m.durations.push_back(ec == 1040 ? 2.0f : ec == 1100 ? 1.0f : 6.0f);
        if (share) return bindGraph(f, m, name, GraphOptions(), *share, out);
        return bindGraph(f, m, name, out);
    }
};

c5m::TriggerSystem triggerFile() {
    c5m::TriggerSystem t;
    t.type = "FGrannyTimedTriggerSystem";
    c5m::FtsTrigger a;
    a.type = c5m::TriggerType::Sound;
    a.type_name = "Sound";
    a.id = 1;
    a.ec = 1000;
    a.time = 0.5f;
    a.attrs["event"] = "STEP";
    c5m::FtsTrigger b;
    b.type = c5m::TriggerType::Effect;
    b.type_name = "Effect";
    b.id = 2;
    b.ec = 1040;
    b.time = 1.0f;
    b.attrs["event"] = "FIDGET";
    t.triggers = {a, b};
    return t;
}

// A controller over real graphs: type "W" (3 figures, the idle machine, triggers) and type "F" (2 figures, the idle
// machine whose fortify state's graph has a Selection on path 4 with no usable default: its program fails at 1500).
struct Pair {
    MemFiles files;
    FakeArt art;
    RealFactory graphs;
    Controller c;
    ControllerOptions opt;
    void open(float roll = 0.05f) {
        c5r::units::UnitArt w;
        w.art_row = "ART_W";
        w.members.push_back(memberArt("M", 3, "omni.fsmxml", warriorClips(), {"m.ftsxml"}));
        art.rows["W"] = w;
        c5r::units::UnitArt f;
        f.art_row = "ART_F";
        f.members.push_back(memberArt("N", 2, "f.fsmxml", warriorClips()));
        art.rows["F"] = f;
        opt.seed = 4468;
        c.open(files, art, graphs, opt);
        CHECK_ANIM(c.logic()->putMachine("omni.fsmxml", idleMachine(roll)), AnimCode::Ok);
        c5m::StateMachine fm = idleMachine(roll);
        for (auto& s : fm.states)
            if (s.id == 7) s.asset = "badfort.dge";
        CHECK_ANIM(c.logic()->putMachine("f.fsmxml", fm), AnimCode::Ok);
        c.logic()->putGraph("idle.dge", selGraph(1000, 2000, 0, 0));
        c.logic()->putGraph("idlecont.dge", selGraph(1000, 2000, 0, 0));
        c.logic()->putGraph("fidget.dge", fidgetGraph());
        c.logic()->putGraph("fortify.dge", oneGraph(1520, 0));
        c.logic()->putGraph("attack.dge", oneGraph(1100, 1));
        c.logic()->putGraph("badfort.dge", selGraph(1520, 1000, 4, 7));
        c.logic()->putTriggers("m.ftsxml", triggerFile());
    }
};

std::string bits(float v) {
    uint32_t u = 0;
    std::memcpy(&u, &v, 4);
    char b[16];
    std::snprintf(b, sizeof(b), "%08x", u);
    return b;
}
std::string text(const FigureProgram& p) {
    std::string s = std::to_string(p.player) + "," + std::to_string(p.id) + "," + std::to_string(p.figure) + "," +
                    std::to_string(p.member) + "," + std::to_string(p.state) + ":";
    for (auto& n : p.program.nodes)
        s += std::to_string(static_cast<int>(n.kind)) + "/" + n.clip + "/" + std::to_string(n.ec) + "/" + bits(n.t) + "/" +
             std::to_string(n.a) + "/" + std::to_string(n.b) + "/" + bits(n.w) + "/" + std::to_string(n.mask) + ";";
    for (auto& m : p.program.masks) s += "m" + m.binding + bits(m.default_weight) + bits(m.target_weight) + ";";
    return s;
}
std::string text(const TriggerEvent& t) {
    return std::to_string(t.player) + "," + std::to_string(t.id) + "," + std::to_string(t.figure) + "," +
           std::to_string(t.trigger) + "," + std::to_string(t.ec) + "," + bits(t.time) + "," + bits(t.offset) + "," + t.event;
}
std::string text(const UnitAnimStatus& s) {
    return std::to_string(s.player) + "," + std::to_string(s.id) + ":" + name(s.status.code) + ":" + s.status.text;
}

TimelineEvent ev(EventKind k, int player, int id, int a = 0) {
    TimelineEvent e;
    e.kind = k;
    e.player = player;
    e.id = id;
    e.a = a;
    return e;
}
TimelineEvent create(int player, int id, const std::string& type) {
    TimelineEvent e = ev(EventKind::Create, player, id);
    e.unit = Rig::unit(player, id, type);
    return e;
}

typedef Controller::UnitKey Key;

// B's drawn set on frame `seq`.
std::set<Key> drawnAt(uint64_t seq) {
    std::set<Key> d;
    if (seq == 1 || seq >= 50) d.insert(Key(0, 1));               // shown, hidden 2..49, shown again
    if (seq <= 10 || seq >= 70) d.insert(Key(0, 2));              // fortified, unfortified and damaged while hidden
    if (seq >= 120) d.insert(Key(0, 3));                          // attacked, removed and created again while hidden
    if (seq >= 75) d.insert(Key(1, 4));                           // ordered to fortify while hidden (frame 70)
    if (seq >= 140) d.insert(Key(1, 6));                          // created hidden and fortified at once: fails hidden
    if (seq >= 30 && seq < 40) d.insert(Key(0, 5));               // never held at all: drawn by the caller, not by the controller
    return d;
}

std::vector<TimelineEvent> eventsAt(uint64_t seq) {
    std::vector<TimelineEvent> e;
    if (seq == 1) {
        e.push_back(create(0, 1, "W"));
        e.push_back(create(0, 2, "W"));
        e.push_back(create(0, 3, "W"));
        e.push_back(create(1, 4, "F"));
    }
    if (seq == 20) e.push_back(ev(EventKind::Fortify, 0, 2, 1));
    if (seq == 40) {
        TimelineEvent a = ev(EventKind::Action, 0, 3);
        a.action = ActionKind::Attack;
        e.push_back(a);
    }
    if (seq == 60) e.push_back(ev(EventKind::Fortify, 0, 2, 0));
    if (seq == 65) e.push_back(ev(EventKind::Damage, 0, 2, 40));
    if (seq == 70) e.push_back(ev(EventKind::Fortify, 1, 4, 1));   // F's fortify graph fails at 1500
    if (seq == 90) {
        e.push_back(ev(EventKind::Remove, 0, 3));
        e.push_back(create(0, 3, "W"));
    }
    if (seq == 100) {   // 0450: the settle takes F's fortify state at once (no clock): its program fails, hidden
        e.push_back(create(1, 6, "F"));
        e.push_back(ev(EventKind::Fortify, 1, 6, 1));
    }
    if (seq == 130) {   // a work order the basic bridge has no code for: BadEvent, while drawn (0439: an unread 1400
        TimelineEvent w = ev(EventKind::Action, 0, 1);   // is a no-op now, so the failure is made this way)
        w.action = ActionKind::Work;
        w.argument = "BUILD_ROAD";
        e.push_back(w);
    }
    return e;
}

const double kDt[] = {1.0 / 30.0, 0.05, 0.3, 0.0, 1.0 / 60.0, 0.21, 0.017};

// 0450 (unit-controller.md 18, settled by 0442's rdc18 capture [L]: the EXE holds the clock of a unit it does not draw):
// the rule this test asserts changed. Before 0450 (4468) every unit advanced on every frame and B's drawn programs were
// compared with A's (every unit drawn). Now B is compared unit by unit with a reference R_k that draws every unit on
// every frame of the same timeline whose dt is 0 on the frames B did not draw unit k (its events kept): on the frames B
// draws unit k, B's programs and triggers of k are byte-equal to R_k's, and k's status is equal to R_k's on every frame.
// A still runs: it must differ from B for units shown after a hidden stretch with time in it (the hold is real).
const Key kUnits[] = {Key(0, 1), Key(0, 2), Key(0, 3), Key(1, 4), Key(1, 6)};
const size_t kRefs = sizeof(kUnits) / sizeof(kUnits[0]);

std::vector<std::string> programsOf(const Controller& c, const Key& k) {
    std::vector<std::string> out;
    for (auto& p : c.programs())
        if (Key(p.player, p.id) == k) out.push_back(text(p));
    return out;
}
std::vector<std::string> triggersOf(const Controller& c, const Key& k) {
    std::vector<std::string> out;
    for (auto& t : c.triggers())
        if (Key(t.player, t.id) == k) out.push_back(text(t));
    return out;
}
std::string statusOf(const Controller& c, const Key& k) {
    const AnimStatus* s = c.status(k.first, k.second);
    return s ? std::string(name(s->code)) + ":" + s->text : std::string("(not held)");
}

}  // namespace

C5R_TEST(drawn_set_programs_and_triggers_byte_equal) {
    Pair a, b;
    Pair r[kRefs];
    a.open();
    b.open();
    for (auto& x : r) x.open();
    size_t compared = 0, hiddenFrames = 0, triggersCompared = 0, differFromA = 0;
    bool sawFail100 = false, sawFail75 = false;
    for (uint64_t seq = 1; seq <= 160; ++seq) {
        TimelineFrame f;
        f.seq = seq;
        f.dt = seq == 1 ? 0.0 : kDt[seq % 7];
        f.events = eventsAt(seq);
        const std::set<Key> d = drawnAt(seq);
        CHECK_ANIM(a.c.apply(f), AnimCode::Ok);
        CHECK_ANIM(b.c.apply(f, &d), AnimCode::Ok);
        CHECK_EQ(a.c.statuses().size(), b.c.statuses().size());   // the same units held
        for (size_t k = 0; k < kRefs; ++k) {
            TimelineFrame g = f;
            if (!d.count(kUnits[k])) g.dt = 0;   // the unit's clock holds while B does not draw it
            CHECK_ANIM(r[k].c.apply(g), AnimCode::Ok);
            CHECK_EQ(statusOf(b.c, kUnits[k]), statusOf(r[k].c, kUnits[k]));
            if (!d.count(kUnits[k])) continue;
            const std::vector<std::string> pb = programsOf(b.c, kUnits[k]);
            CHECK(pb == programsOf(r[k].c, kUnits[k]));
            if (pb != programsOf(a.c, kUnits[k])) ++differFromA;
            compared += pb.size();
            const std::vector<std::string> tb = triggersOf(b.c, kUnits[k]);
            CHECK(tb == triggersOf(r[k].c, kUnits[k]));
            triggersCompared += tb.size();
        }
        // B: programs and triggers only for drawn units
        for (auto& p : b.c.programs()) CHECK(d.count(Key(p.player, p.id)) == 1);
        for (auto& t : b.c.triggers()) CHECK(d.count(Key(t.player, t.id)) == 1);
        const Controller::ApplyCounts& ca = a.c.lastApply();
        const Controller::ApplyCounts& cb = b.c.lastApply();
        CHECK_EQ(ca.quiet, 0u);
        CHECK_EQ(ca.drawn, a.c.units());
        CHECK_EQ(cb.drawn + cb.quiet, b.c.units());
        hiddenFrames += cb.quiet;
        // W's graphs cannot fail: no program for a hidden W. F's fortify graph can: (1, 6), created hidden with its
        // fortify order on frame 100, enters it in its settle (no clock needed) and fails there, hidden, on that frame;
        // (1, 4), ordered to fortify on frame 70 while hidden, holds its idle state until it is drawn (frame 75)
        CHECK_EQ(cb.quiet_programs, seq == 100 ? 1u : 0u);
        if (seq == 100) {
            const AnimStatus* s = b.c.status(1, 6);
            sawFail100 = s && !s->ok() && s->code == AnimCode::BadLogicFile;
        }
        if (seq == 74) CHECK(b.c.status(1, 4) && b.c.status(1, 4)->ok());   // A has failed it on frame 70
        if (seq == 75) {
            const AnimStatus* s = b.c.status(1, 4);
            sawFail75 = s && !s->ok() && s->code == AnimCode::BadLogicFile;
        }
    }
    for (auto& st : b.c.statuses()) std::printf("  status %s\n", text(st).c_str());
    CHECK(sawFail100);
    CHECK(sawFail75);
    // the failure of a drawn unit (frame 130's work order on (0, 1)) is the same in both
    const AnimStatus* s = b.c.status(0, 1);
    CHECK(s && s->code == AnimCode::BadEvent);
    std::printf("  compared %zu figure programs and %zu triggers; %zu hidden unit-frames; %zu unit-frames differ from "
                "the every-unit-advanced rule\n", compared, triggersCompared, hiddenFrames, differFromA);
    CHECK(compared > 500 && triggersCompared > 5 && hiddenFrames > 200 && differFromA > 50);
}

// A unit not drawn for a stretch, then drawn: its first drawn frame equals the frame of a controller that draws it on
// every frame of a timeline whose dt is 0 for the stretch (0450: its clock held), whatever the stretch (each length on
// its own controllers); and differs from the every-unit-advanced rule once the stretch held time.
C5R_TEST(shown_after_any_hidden_stretch) {
    for (uint64_t from : {2u, 3u, 17u, 31u, 64u, 99u}) {
        Pair a, b, r;
        a.open();
        b.open();
        r.open();
        for (uint64_t seq = 1; seq <= from; ++seq) {
            TimelineFrame f;
            f.seq = seq;
            f.dt = seq == 1 ? 0.0 : kDt[(seq * 3) % 7];
            if (seq == 1) f.events = {create(0, 1, "W")};
            if (seq == from / 2 + 1) f.events.push_back(ev(EventKind::Fortify, 0, 1, 1));
            std::set<Key> d;
            if (seq == from) d.insert(Key(0, 1));
            TimelineFrame g = f;
            if (seq < from) g.dt = 0;
            CHECK_ANIM(a.c.apply(f), AnimCode::Ok);
            CHECK_ANIM(b.c.apply(f, &d), AnimCode::Ok);
            CHECK_ANIM(r.c.apply(g), AnimCode::Ok);
            if (seq < from) CHECK(b.c.programs().empty());
        }
        CHECK_EQ(r.c.programs().size(), 3u);
        CHECK_EQ(b.c.programs().size(), 3u);
        for (size_t i = 0; i < r.c.programs().size() && i < b.c.programs().size(); ++i)
            CHECK_EQ(text(r.c.programs()[i]), text(b.c.programs()[i]));
        if (from >= 17) CHECK(programsOf(a.c, Key(0, 1)) != programsOf(b.c, Key(0, 1)));
    }
}

// rdc18 as a test (unit-controller.md 18): two units of one type created on one frame share one clock; one is not drawn
// for 30 frames of 1/30 s (1.0 s); when both are drawn again the hidden one lags by exactly that time (mod the 6 s loop),
// and the lag stays. No fidget roll (chance 0), so both play the looping idle.
C5R_TEST(held_clock_lags_by_the_hidden_time) {
    Pair p;
    p.open(0.0f);
    float lagAt10 = -1, lagAt41 = -1, lagAt80 = -1;
    for (uint64_t seq = 1; seq <= 80; ++seq) {
        TimelineFrame f;
        f.seq = seq;
        f.dt = seq == 1 ? 0.0 : 1.0 / 30.0;
        if (seq == 1) f.events = {create(0, 1, "W"), create(0, 2, "W")};
        std::set<Key> d;
        d.insert(Key(0, 1));
        if (seq <= 10 || seq > 40) d.insert(Key(0, 2));   // B hidden on frames 11..40: 30 frames of dt
        CHECK_ANIM(p.c.apply(f, &d), AnimCode::Ok);
        if (seq != 10 && seq != 41 && seq != 80) continue;
        float ta = -1, tb = -1;
        for (auto& fp : p.c.programs()) {
            if (fp.figure != 0 || fp.program.nodes.empty()) continue;
            (fp.id == 1 ? ta : tb) = fp.program.nodes[0].t;
        }
        CHECK(ta >= 0 && tb >= 0);
        const float lag = std::fmod(ta - tb + 6.0f, 6.0f);
        (seq == 10 ? lagAt10 : seq == 41 ? lagAt41 : lagAt80) = lag;
    }
    std::printf("  lag of the hidden unit: frame 10 %.5f s, frame 41 %.5f s, frame 80 %.5f s\n", lagAt10, lagAt41, lagAt80);
    CHECK(std::fabs(lagAt10) < 1e-4f);
    CHECK(std::fabs(lagAt41 - 1.0f) < 1e-3f);
    CHECK(std::fabs(lagAt80 - 1.0f) < 1e-3f);
    CHECK_EQ(p.c.lastApply().quiet, 0u);
}

// The graph's own answer: bindGraph's graphs say whether their program can fail.
C5R_TEST(program_may_fail_from_structure) {
    EcMap m;
    m.ecs = {1000, 2000, 1040, 1520};
    m.clips = {"a.gr2", "b.gr2", "f.gr2", "o.gr2"};
    m.durations = {6, 6, 2, 4};
    std::unique_ptr<GraphInstance> g;
    CHECK_ANIM(bindGraph(selGraph(1000, 2000, 0, 0), m, "sel.dge", g), AnimCode::Ok);
    CHECK(g && !g->programMayFail());
    CHECK_ANIM(bindGraph(fidgetGraph(), m, "fidget.dge", g), AnimCode::Ok);
    CHECK(g && !g->programMayFail());
    CHECK_ANIM(bindGraph(oneGraph(1520, 0), m, "one.dge", g), AnimCode::Ok);
    CHECK(g && !g->programMayFail());
    // a Selection with no usable default: a selector out of range fails
    CHECK_ANIM(bindGraph(selGraph(1000, 2000, 4, 7), m, "badsel.dge", g), AnimCode::Ok);
    CHECK(g && g->programMayFail());
    PathOptions o;
    PoseProgram p;
    CHECK_ANIM(g->program(o, p), AnimCode::Ok);   // path 4 = 0: in range
    o.v[4] = 1500;
    CHECK_ANIM(g->program(o, p), AnimCode::BadLogicFile);
    // 0439: a Blend with a missing To edge passes its From whole at every weight (VP's edited modern-ship graphs) and
    // cannot fail; with neither edge the graph reaches no source and is refused at bind
    CHECK_ANIM(bindGraph(halfGraph(1000, false), m, "half.dge", g), AnimCode::Ok);
    CHECK(g && !g->programMayFail());
    PathOptions z;
    CHECK_ANIM(g->program(z, p), AnimCode::Ok);
    CHECK_EQ(p.nodes.size(), 1u);
    g->advance(0.2f, z);   // the weight is 1 now: still From alone, no blend node, no mask
    CHECK_ANIM(g->program(z, p), AnimCode::Ok);
    CHECK_EQ(p.nodes.size(), 1u);
    CHECK(p.nodes[0].kind == PoseNode::Sample && p.nodes[0].ec == 1000 && p.masks.empty());
    CHECK_ANIM(bindGraph(halfGraph(1000, true), m, "none.dge", g), AnimCode::NoProgress);   // no source reached
    // the base class answers true (a graph that does not say)
    FakeGraph fg({1000}, {"a.gr2"}, {}, -1);
    CHECK(fg.programMayFail());
}

// The start's work is bounded by the drawn set (item 2 of 4468): a controller that starts with 300 units of which 5 are
// drawn builds pose programs for those 5 units' figures only, on its first frame and on every frame after; the graph
// binds grow with the units (each figure's graph of the state it enters), the programs do not.
class CountingGraph : public GraphInstance {
public:
    CountingGraph(std::unique_ptr<GraphInstance> g, size_t& programs, size_t& advances)
        : g_(std::move(g)), programs_(programs), advances_(advances) {}
    void restart() override { g_->restart(); }
    void advance(float dt, const PathOptions& o) override {
        ++advances_;
        g_->advance(dt, o);
    }
    AnimStatus program(const PathOptions& o, PoseProgram& out) const override {
        ++programs_;
        return g_->program(o, out);
    }
    bool complete(const PathOptions& o) const override { return g_->complete(o); }
    void active(const PathOptions& o, std::vector<ActiveSource>& out) const override { g_->active(o, out); }
    float stateTime() const override { return g_->stateTime(); }
    void restartFrom(const std::vector<ActiveSource>& p) override { g_->restartFrom(p); }
    bool programMayFail() const override { return g_->programMayFail(); }

private:
    std::unique_ptr<GraphInstance> g_;
    size_t& programs_;
    size_t& advances_;
};
class CountingFactory : public GraphFactory {
public:
    RealFactory inner;
    size_t binds = 0, programs = 0, advances = 0;
    AnimStatus bind(const c5m::DagFile& f, const EcMap& ecs, const std::string& name,
                    std::unique_ptr<GraphInstance>& out) override {
        ++binds;
        std::unique_ptr<GraphInstance> g;
        AnimStatus st = inner.bind(f, ecs, name, g);
        if (g) out.reset(new CountingGraph(std::move(g), programs, advances));
        return st;
    }
};

C5R_TEST(start_work_bounded_by_the_drawn_set) {
    MemFiles files;
    FakeArt art;
    CountingFactory graphs;
    Controller c;
    c5r::units::UnitArt w;
    w.art_row = "ART_W";
    w.members.push_back(memberArt("M", 3, "omni.fsmxml", warriorClips()));
    art.rows["W"] = w;
    ControllerOptions opt;
    c.open(files, art, graphs, opt);
    CHECK_ANIM(c.logic()->putMachine("omni.fsmxml", idleMachine(0.0f)), AnimCode::Ok);
    c.logic()->putGraph("idle.dge", selGraph(1000, 2000, 0, 0));
    c.logic()->putGraph("idlecont.dge", selGraph(1000, 2000, 0, 0));
    c.logic()->putGraph("fidget.dge", fidgetGraph());
    c.logic()->putGraph("fortify.dge", oneGraph(1520, 0));
    c.logic()->putGraph("attack.dge", oneGraph(1100, 1));
    TimelineFrame f;
    f.seq = 1;
    f.dt = 0;
    for (int id = 1; id <= 300; ++id) f.events.push_back(create(0, id, "W"));
    std::set<Key> d;
    for (int id = 1; id <= 5; ++id) d.insert(Key(0, id));
    CHECK_ANIM(c.apply(f, &d), AnimCode::Ok);
    CHECK_EQ(c.units(), 300u);
    CHECK_EQ(c.programs().size(), 15u);
    CHECK_EQ(graphs.programs, 15u);              // the first frame: the drawn units' figures only
    CHECK_EQ(c.lastApply().drawn, 5u);
    CHECK_EQ(c.lastApply().quiet, 295u);
    CHECK_EQ(c.lastApply().quiet_programs, 0u);
    CHECK_EQ(graphs.binds, 900u);                // each figure's idle graph (the chooser is graphless)
    for (uint64_t seq = 2; seq <= 11; ++seq) {
        TimelineFrame g;
        g.seq = seq;
        g.dt = 1.0 / 30.0;
        CHECK_ANIM(c.apply(g, &d), AnimCode::Ok);
    }
    CHECK_EQ(graphs.programs, 15u * 11u);        // 10 more frames: still the drawn figures only
    CHECK_EQ(graphs.advances, 15u * 10u);        // 0450: only the drawn figures advance (one step per frame of 1/30 s)
    for (auto& s : c.statuses()) CHECK(s.status.ok());
}

// The frame-level refusals are the same with a drawn set (negative path).
C5R_TEST(drawn_set_frame_refusals) {
    Pair b;
    b.open();
    std::set<Key> d;
    TimelineFrame f;
    f.seq = 1;
    f.dt = -1;
    CHECK_ANIM(b.c.apply(f, &d), AnimCode::BadEvent);
    f.dt = std::nan("");
    CHECK_ANIM(b.c.apply(f, &d), AnimCode::BadEvent);
    Controller closed;
    f.dt = 0;
    CHECK_ANIM(closed.apply(f, &d), AnimCode::BadEvent);
    CHECK(closed.programs().empty() && closed.lastApply().drawn == 0 && closed.lastApply().quiet == 0);
}

// 0470 (unit-controller.md 19.7): the shapes shared (bindGraph with a GraphShare, as the product's ClipGraphFactory
// binds) change no output. Two controllers take the same timeline (the drawn-set schedule, its events and failures),
// one binding every figure's graph on its own, one through a GraphShare: their programs, triggers and statuses are
// byte-equal on every frame, with every unit drawn (A) and with the schedule (B). The census: the same instances, held
// by one shape per (graph file, member) in the shared one and by one each in the other.
C5R_TEST(shared_shapes_controller_byte_equal) {
    for (int sched = 0; sched < 2; ++sched) {
        GraphShare share;
        Pair own, shared;
        shared.graphs.share = &share;
        own.open();
        shared.open();
        size_t programs = 0, triggers = 0;
        for (uint64_t seq = 1; seq <= 160; ++seq) {
            TimelineFrame f;
            f.seq = seq;
            f.dt = seq == 1 ? 0.0 : kDt[seq % 7];
            f.events = eventsAt(seq);
            const std::set<Key> d = drawnAt(seq);
            CHECK_ANIM(own.c.apply(f, sched ? &d : nullptr), AnimCode::Ok);
            CHECK_ANIM(shared.c.apply(f, sched ? &d : nullptr), AnimCode::Ok);
            CHECK_EQ(own.c.programs().size(), shared.c.programs().size());
            for (size_t i = 0; i < own.c.programs().size() && i < shared.c.programs().size(); ++i)
                CHECK_EQ(text(own.c.programs()[i]), text(shared.c.programs()[i]));
            CHECK_EQ(own.c.triggers().size(), shared.c.triggers().size());
            for (size_t i = 0; i < own.c.triggers().size() && i < shared.c.triggers().size(); ++i)
                CHECK_EQ(text(own.c.triggers()[i]), text(shared.c.triggers()[i]));
            CHECK_EQ(own.c.statuses().size(), shared.c.statuses().size());
            for (size_t i = 0; i < own.c.statuses().size() && i < shared.c.statuses().size(); ++i)
                CHECK_EQ(text(own.c.statuses()[i]), text(shared.c.statuses()[i]));
            programs += own.c.programs().size();
            triggers += own.c.triggers().size();
        }
        const Controller::GraphCensus co = own.c.graphCensus(), cs = shared.c.graphCensus();
        std::printf("  %s: %zu programs, %zu triggers compared; instances %zu / %zu, shared blocks %zu / %zu, shapes %zu "
                    "(binds %zu, hits %zu), member-states %zu, files %zu\n",
                    sched ? "schedule" : "every unit drawn", programs, triggers, co.instances, cs.instances,
                    co.shared_blocks, cs.shared_blocks, share.shapes(), share.binds(), share.hits(), cs.member_states,
                    cs.files);
        CHECK(programs > 500 && triggers > 5);
        CHECK_EQ(co.instances, cs.instances);
        CHECK_EQ(co.shared_blocks, co.instances);   // unshared: every instance its own shape
        CHECK(cs.instances > 10 && cs.shared_blocks <= cs.member_states && cs.shared_blocks < cs.instances / 2);
        CHECK(share.shapes() >= cs.shared_blocks && share.hits() + share.shapes() == share.binds());
        size_t ownBytes = 0, sharedBytes = 0;
        for (int p = 0; p < GraphFootprint::kParts; ++p) {
            ownBytes += co.own[p] + co.shared[p];
            sharedBytes += cs.own[p] + cs.shared[p];
        }
        std::printf("  bytes: %zu without the share, %zu with it\n", ownBytes, sharedBytes);
        CHECK(sharedBytes * 2 < ownBytes);
    }
}

C5R_TEST_MAIN
