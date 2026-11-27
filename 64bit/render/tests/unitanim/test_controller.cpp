// render/tests/unitanim/test_controller.cpp - issue 4403: the controller entry (controller.h; unit-controller.md 1, 4,
// 5, 9, 13.1) over hand-built logic and a graph double: the clock, the machine through events, the X1 cross-fade
// rule, triggers, forced choices, skips, the pre-check, and one negative test per status the runtime raises (DoD F).
#include <cstring>

#include "anim_testkit.h"

using namespace c5r::unitanim;
using namespace anim_test;

namespace {

const double kH = 1.0 / 30.0;

TimelineEvent ev(EventKind k, int player = 0, int id = 1) {
    TimelineEvent e;
    e.kind = k;
    e.player = player;
    e.id = id;
    return e;
}

TimelineEvent createEv(int player, int id, const std::string& type = "W") {
    TimelineEvent e = ev(EventKind::Create, player, id);
    e.unit = Rig::unit(player, id, type);
    return e;
}

AnimCode unitCode(const Controller& c, int player, int id) {
    const AnimStatus* s = c.status(player, id);
    return s ? s->code : AnimCode::Ok;
}

// The status of a fresh unit of type W on a rig whose member art is `m` (logic put by `prep`).
template <class Prep>
AnimStatus statusWith(const c5r::units::MemberArt& m, Prep prep, bool forced = false) {
    Rig r;
    r.opt.forced = forced;
    r.open();
    c5r::units::UnitArt a;
    a.art_row = "ART_X";
    a.members.push_back(m);
    r.art.rows["X"] = a;
    prep(r);
    TimelineFrame f = Rig::frame(1, kH);
    f.events.push_back(createEv(0, 5, "X"));
    CHECK_ANIM(r.c.apply(f), AnimCode::Ok);
    const AnimStatus* s = r.c.status(0, 5);
    CHECK(s != nullptr);
    if (s && !s->ok()) {
        for (auto& p : r.c.programs()) CHECK(!(p.player == 0 && p.id == 5));   // no program for a failed unit
    }
    return s ? *s : AnimStatus::Fail(AnimCode::UnitFailed, "no unit");
}

}  // namespace

C5R_TEST(create_settles_to_idle_and_steps_in_whole_frames) {
    Rig r;
    r.opt.clock.mode = ClockMode::Steps;   // the whole-step reading (a comparison since MC #2089)
    r.open(3, 0.0f);
    TimelineFrame f = Rig::frame(1, 0);
    f.events.push_back(createEv(0, 1));
    CHECK_ANIM(r.c.apply(f), AnimCode::Ok);
    CHECK_EQ(r.c.programs().size(), static_cast<size_t>(3));
    const FigureProgram* p = r.prog(0, 1, 0);
    CHECK(p != nullptr);
    CHECK_EQ(p->state, 1);
    CHECK(p->program.single());
    CHECK(p->program.nodes[0].ec == 1000 || p->program.nodes[0].ec == 2000);
    CHECK_EQ(p->program.nodes[0].t, 0.0f);
    // 30 frames at the frame cap: 30 steps, 1 s
    for (uint64_t s = 2; s < 32; ++s) CHECK_ANIM(r.c.apply(Rig::frame(s, kH)), AnimCode::Ok);
    CHECK_NEAR(r.prog(0, 1, 0)->program.nodes[0].t, 1.0, 1e-4);
    // the capture regime (7.2): a stalled frame advances 0.2 s, the short one after it nothing
    CHECK_ANIM(r.c.apply(Rig::frame(40, 1.3)), AnimCode::Ok);
    CHECK_NEAR(r.prog(0, 1, 2)->program.nodes[0].t, 1.2, 1e-4);
    CHECK_ANIM(r.c.apply(Rig::frame(41, 0.025)), AnimCode::Ok);
    CHECK_NEAR(r.prog(0, 1, 2)->program.nodes[0].t, 1.2, 1e-4);
    CHECK_EQ(r.c.statuses().size(), static_cast<size_t>(1));
    CHECK(r.c.statuses()[0].status.ok());
}

C5R_TEST(random_idle_variant_is_per_figure_and_seeded) {
    // R0 at creation picks idle A (0.6) or B (0.4) per figure; the same seed gives the same picks
    std::vector<int32_t> first;
    for (int run = 0; run < 2; ++run) {
        Rig r;
        r.opt.seed = 77;
        r.open(12, 0.0f);
        TimelineFrame f = Rig::frame(1, 0);
        f.events.push_back(createEv(0, 1));
        CHECK_ANIM(r.c.apply(f), AnimCode::Ok);
        std::vector<int32_t> ecs;
        for (auto& p : r.c.programs()) ecs.push_back(p.program.nodes[0].ec);
        if (run == 0) first = ecs;
        else CHECK(ecs == first);
    }
    bool a = false, b = false;
    for (auto e : first) {
        a = a || e == 1000;
        b = b || e == 2000;
    }
    CHECK(a && b);
}

C5R_TEST(start_scene_and_figure_start_times) {
    Rig r;
    r.open(2, 0.0f);
    c5r::units::UnitScene sc;
    c5r::units::UnitInput u = Rig::unit(1, 9);
    c5r::units::FigureInput fi;
    fi.figure = 1;
    fi.t = 2.5f;
    u.figures.push_back(fi);
    sc.units.push_back(u);
    r.c.start(sc);
    CHECK_ANIM(r.c.apply(Rig::frame(1, kH)), AnimCode::Ok);
    CHECK_NEAR(r.prog(1, 9, 0)->program.nodes[0].t, kH, 1e-5);
    CHECK_NEAR(r.prog(1, 9, 1)->program.nodes[0].t, 2.5 + kH, 1e-5);
}

C5R_TEST(fortify_and_attack_through_events) {
    Rig r;
    r.open(2, 0.0f);
    TimelineFrame f = Rig::frame(1, kH);
    f.events.push_back(createEv(0, 1));
    CHECK_ANIM(r.c.apply(f), AnimCode::Ok);
    TimelineFrame g = Rig::frame(2, kH);
    TimelineEvent fo = ev(EventKind::Fortify);
    fo.a = 1;
    g.events.push_back(fo);
    CHECK_ANIM(r.c.apply(g), AnimCode::Ok);
    CHECK_EQ(r.prog(0, 1, 0)->state, 7);
    CHECK_EQ(r.prog(0, 1, 0)->program.nodes[0].ec, 1520);
    CHECK_EQ(r.prog(0, 1, 0)->program.nodes[0].t, 0.0f);   // entered after the step's advance: t 0 (P02, P04)
    TimelineFrame h = Rig::frame(3, kH);
    fo.a = 0;
    h.events.push_back(fo);
    CHECK_ANIM(r.c.apply(h), AnimCode::Ok);
    CHECK_EQ(r.prog(0, 1, 0)->state, 1);
    // attack: P4 1100 -> state 9 (S4 = 0 consumes the request), back to idle when the clip completes (1 s)
    TimelineFrame k = Rig::frame(4, kH);
    TimelineEvent at = ev(EventKind::Action);
    at.action = ActionKind::Attack;
    k.events.push_back(at);
    CHECK_ANIM(r.c.apply(k), AnimCode::Ok);
    CHECK_EQ(r.prog(0, 1, 1)->state, 9);
    CHECK_EQ(r.prog(0, 1, 1)->program.nodes[0].ec, 1100);
    for (uint64_t s = 5; s < 5 + 31; ++s) CHECK_ANIM(r.c.apply(Rig::frame(s, kH)), AnimCode::Ok);
    CHECK_EQ(r.prog(0, 1, 1)->state, 1);
}

C5R_TEST(attack_on_some_figures_only) {
    Rig r;
    r.open(3, 0.0f);
    TimelineFrame f = Rig::frame(1, kH);
    f.events.push_back(createEv(0, 1));
    TimelineEvent at = ev(EventKind::Action);
    at.action = ActionKind::Attack;
    at.figures.all = false;
    at.figures.figures = {2};
    f.events.push_back(at);
    CHECK_ANIM(r.c.apply(f), AnimCode::Ok);
    CHECK_EQ(r.prog(0, 1, 0)->state, 1);
    CHECK_EQ(r.prog(0, 1, 2)->state, 9);
}

C5R_TEST(cross_fade_rule_idle_to_fidget) {
    // A rule given as data (the default list is empty since 4406: the graphs carry the fades): the omni machine's
    // 6 -> 3 over 0.3 s: the last program frozen, the fidget fading in linearly
    CHECK(defaultCrossFades().empty());
    Rig r;
    CrossFadeRule rule;
    rule.machine = "1_omni_state_01.fsmxml";
    rule.from_state = 6;
    rule.to_state = 3;
    rule.duration = 0.3f;
    r.opt.fades = {rule};
    r.open(1, 1.0f, "1_omni_state_01.fsmxml");
    TimelineFrame f = Rig::frame(1, 0);
    f.events.push_back(createEv(0, 1));
    CHECK_ANIM(r.c.apply(f), AnimCode::Ok);
    CHECK_EQ(r.prog(0, 1, 0)->state, 1);
    CHECK_ANIM(r.c.apply(Rig::frame(2, kH)), AnimCode::Ok);   // idle advances, the roll fires: -> 6
    CHECK_EQ(r.prog(0, 1, 0)->state, 6);
    CHECK_ANIM(r.c.apply(Rig::frame(3, kH)), AnimCode::Ok);   // 6 advances, -> 3 with the rule
    const FigureProgram* p = r.prog(0, 1, 0);
    CHECK_EQ(p->state, 3);
    CHECK_EQ(p->program.nodes.size(), static_cast<size_t>(3));
    CHECK_EQ(p->program.nodes[2].kind, PoseNode::Blend);
    CHECK_EQ(p->program.nodes[2].w, 0.0f);
    const float frozen_t = p->program.nodes[0].t;
    CHECK_NEAR(frozen_t, kH, 1e-5);                          // the chooser's graph after its one step
    // 6 steps (0.2 s) into the fidget: w = 0.2 / 0.3, the old pose still at its frozen time
    for (uint64_t s = 4; s < 10; ++s) CHECK_ANIM(r.c.apply(Rig::frame(s, kH)), AnimCode::Ok);
    p = r.prog(0, 1, 0);
    CHECK_EQ(p->program.nodes.size(), static_cast<size_t>(3));
    CHECK_EQ(p->program.nodes[0].t, frozen_t);
    CHECK_EQ(p->program.nodes[1].ec, 1040);
    CHECK_NEAR(p->program.nodes[1].t, 0.2, 1e-5);
    CHECK_EQ(p->program.nodes[2].a, 0);
    CHECK_EQ(p->program.nodes[2].b, 1);
    CHECK_NEAR(p->program.nodes[2].w, 0.2 / 0.3, 1e-4);
    for (uint64_t s = 10; s < 14; ++s) CHECK_ANIM(r.c.apply(Rig::frame(s, kH)), AnimCode::Ok);
    CHECK(r.prog(0, 1, 0)->program.single());                // the fade has ended
    // no rule for another machine name: no fade
    Rig q;
    q.open(1, 1.0f, "other.fsmxml");
    TimelineFrame g = Rig::frame(1, 0);
    g.events.push_back(createEv(0, 1));
    CHECK_ANIM(q.c.apply(g), AnimCode::Ok);
    CHECK_ANIM(q.c.apply(Rig::frame(2, kH)), AnimCode::Ok);
    CHECK_ANIM(q.c.apply(Rig::frame(3, kH)), AnimCode::Ok);
    CHECK_EQ(q.prog(0, 1, 0)->state, 3);
    CHECK(q.prog(0, 1, 0)->program.single());
    // the default options: no fade on the omni machine either
    Rig d;
    d.open(1, 1.0f, "1_omni_state_01.fsmxml");
    TimelineFrame h = Rig::frame(1, 0);
    h.events.push_back(createEv(0, 1));
    CHECK_ANIM(d.c.apply(h), AnimCode::Ok);
    CHECK_ANIM(d.c.apply(Rig::frame(2, kH)), AnimCode::Ok);
    CHECK_ANIM(d.c.apply(Rig::frame(3, kH)), AnimCode::Ok);
    CHECK_EQ(d.prog(0, 1, 0)->state, 3);
    CHECK(d.prog(0, 1, 0)->program.single());
}

C5R_TEST(triggers_fire_once_when_crossed) {
    Rig r;
    c5r::units::UnitArt a;
    a.art_row = "ART_T";
    a.members.push_back(memberArt("M", 1, "omni.fsmxml", warriorClips(), {"fx_triggers_m.ftsxml"}));
    r.open(1, 0.0f);
    r.art.rows["T"] = a;
    c5m::TriggerSystem ts;
    ts.type = "FGrannyTimedTriggerSystem";
    c5m::FtsTrigger t;
    t.type = c5m::TriggerType::Action;
    t.type_name = "FTimedTriggerAction";
    t.id = 3;
    t.ec = 1100;
    t.time = 0.4889f;
    ts.triggers.push_back(t);
    t.type = c5m::TriggerType::Sound;
    t.id = 4;
    t.time = 0.0f;
    t.attrs["event"] = "AS2D_X";
    ts.triggers.push_back(t);
    t.type = c5m::TriggerType::Effect;
    t.id = 5;
    t.ec = 1000;          // on the idle: fires each loop
    t.time = 1.0f;
    ts.triggers.push_back(t);
    r.c.logic()->putTriggers("fx_triggers_m.ftsxml", ts);
    TimelineFrame f = Rig::frame(1, kH);
    f.events.push_back(createEv(0, 1, "T"));
    TimelineEvent at = ev(EventKind::Action);
    at.action = ActionKind::Attack;
    f.events.push_back(at);
    CHECK_ANIM(r.c.apply(f), AnimCode::Ok);
    int action = 0, sound = 0;
    float when = -1;
    for (uint64_t s = 2; s < 40; ++s) {
        CHECK_ANIM(r.c.apply(Rig::frame(s, kH)), AnimCode::Ok);
        for (auto& e : r.c.triggers()) {
            if (e.type == c5m::TriggerType::Action) {
                ++action;
                when = r.prog(0, 1, 0)->program.nodes[0].t;
                CHECK_EQ(e.trigger, 3);
                CHECK_EQ(e.ec, 1100);
                CHECK(e.offset >= 0 && e.offset <= kH + 1e-6);
            }
            if (e.type == c5m::TriggerType::Sound) {
                ++sound;
                CHECK_EQ(e.event, std::string("AS2D_X"));
            }
        }
    }
    CHECK_EQ(action, 1);
    CHECK_EQ(sound, 1);
    CHECK(when > 0.4889f && when < 0.4889f + kH + 1e-5);
}

// 4737: a trigger carries its tier attribute (-1 when the file has none, the EXE's default) and the unit's living
// figures (research/02-exe-internals/exe-audio.md 13)
C5R_TEST(triggers_carry_tier_and_living_figures) {
    Rig r;
    c5r::units::UnitArt a;
    a.art_row = "ART_T";
    a.members.push_back(memberArt("M", 3, "omni.fsmxml", warriorClips(), {"fx_triggers_m.ftsxml"}));
    r.open(1, 0.0f);
    r.art.rows["T"] = a;
    c5m::TriggerSystem ts;
    ts.type = "FGrannyTimedTriggerSystem";
    c5m::FtsTrigger t;
    t.type = c5m::TriggerType::Sound;
    t.type_name = "FTimedTriggerSound";
    t.ec = 1100;
    t.time = 0.0f;
    t.id = 4;
    t.tier = 0;
    t.attrs["event"] = "AS3D_DETAIL";
    t.attrs["tier"] = "0";
    ts.triggers.push_back(t);
    t.id = 5;
    t.tier = 2;
    t.attrs["event"] = "AS3D_BED_T2";
    t.attrs["tier"] = "2";
    ts.triggers.push_back(t);
    t.id = 6;
    t.tier = 0;   // the reader's value for an absent attribute
    t.attrs.erase("tier");
    t.attrs["event"] = "AS3D_NO_TIER";
    ts.triggers.push_back(t);
    r.c.logic()->putTriggers("fx_triggers_m.ftsxml", ts);
    TimelineFrame f = Rig::frame(1, kH);
    f.events.push_back(createEv(0, 1, "T"));
    TimelineEvent at = ev(EventKind::Action);
    at.action = ActionKind::Attack;
    f.events.push_back(at);
    CHECK_ANIM(r.c.apply(f), AnimCode::Ok);
    int seen[3] = {0, 0, 0};
    int32_t figures = -2;
    for (uint64_t s = 2; s < 40; ++s) {
        CHECK_ANIM(r.c.apply(Rig::frame(s, kH)), AnimCode::Ok);
        for (auto& e : r.c.triggers()) {
            if (e.type != c5m::TriggerType::Sound) continue;
            figures = e.unit_figures;
            if (e.event == "AS3D_DETAIL") { ++seen[0]; CHECK_EQ(e.tier, 0); }
            if (e.event == "AS3D_BED_T2") { ++seen[1]; CHECK_EQ(e.tier, 2); }
            if (e.event == "AS3D_NO_TIER") { ++seen[2]; CHECK_EQ(e.tier, -1); }
        }
    }
    CHECK(seen[0] >= 1 && seen[1] == seen[0] && seen[2] == seen[0]);   // once per figure that attacks
    CHECK_EQ(figures, 3);   // the unit's three figures, all living
}

C5R_TEST(skip_rules_and_hook) {
    struct Half : StepHook {
        int steps(uint64_t seq, int32_t, int32_t, int32_t figure, int n) override {
            return seq == 3 && figure == 0 ? 0 : n;
        }
    } hook;
    Rig r;
    SkipRule k;
    k.seq = 2;
    k.player = 0;
    k.id = 1;
    k.figure = 1;
    k.steps = 6;
    r.opt.skips.push_back(k);
    r.opt.hook = &hook;
    r.open(2, 0.0f);
    TimelineFrame f = Rig::frame(1, 0);
    f.events.push_back(createEv(0, 1));
    CHECK_ANIM(r.c.apply(f), AnimCode::Ok);
    CHECK_ANIM(r.c.apply(Rig::frame(2, 1.4)), AnimCode::Ok);   // figure 1 skips this frame's 6 steps
    CHECK_NEAR(r.prog(0, 1, 0)->program.nodes[0].t, 0.2, 1e-5);
    CHECK_NEAR(r.prog(0, 1, 1)->program.nodes[0].t, 0.0, 1e-5);
    CHECK_ANIM(r.c.apply(Rig::frame(3, 1.4)), AnimCode::Ok);   // the hook stops figure 0
    CHECK_NEAR(r.prog(0, 1, 0)->program.nodes[0].t, 0.2, 1e-5);
    CHECK_NEAR(r.prog(0, 1, 1)->program.nodes[0].t, 0.2, 1e-5);
}

C5R_TEST(steer_and_reset_and_remove) {
    Rig r;
    r.graphs.select = 8;   // the graph double selects its source by path 8 (as the turn graphs do)
    r.open(2, 0.0f);
    TimelineFrame f = Rig::frame(1, kH);
    f.events.push_back(createEv(0, 1));
    CHECK_ANIM(r.c.apply(f), AnimCode::Ok);
    CHECK_EQ(r.prog(0, 1, 0)->program.nodes[0].ec, 1000);
    TimelineFrame g = Rig::frame(2, kH);
    TimelineEvent st = ev(EventKind::Steer);
    st.figures.all = false;
    st.figures.figures = {1};
    st.a = 1;
    st.weight = 0.5f;
    g.events.push_back(st);
    CHECK_ANIM(r.c.apply(g), AnimCode::Ok);
    CHECK_EQ(r.prog(0, 1, 0)->program.nodes[0].ec, 1000);
    CHECK_EQ(r.prog(0, 1, 1)->program.nodes[0].ec, 2000);
    TimelineFrame h = Rig::frame(3, kH);
    h.events.push_back(ev(EventKind::Reset));
    CHECK_ANIM(r.c.apply(h), AnimCode::Ok);
    CHECK_NEAR(r.prog(0, 1, 1)->program.nodes[0].t, kH, 1e-6);   // back to time 0, then this frame's step
    CHECK_EQ(r.prog(0, 1, 1)->program.nodes[0].ec, 1000);        // path 8 cleared by the reset
    TimelineFrame k = Rig::frame(4, kH);
    k.events.push_back(ev(EventKind::Remove));
    CHECK_ANIM(r.c.apply(k), AnimCode::Ok);
    CHECK_EQ(r.c.units(), static_cast<size_t>(0));
    CHECK(r.c.programs().empty());
}

C5R_TEST(forced_choices) {
    // forced mode: the creation's R0 (socket 1 of the machine, file order) needs a record
    {
        Rig r;
        r.opt.forced = true;
        r.open(2, 0.0f);
        TimelineFrame f = Rig::frame(1, kH);
        f.events.push_back(createEv(0, 1));
        TimelineEvent c = ev(EventKind::Choice);
        c.point = "option:omni.fsmxml:1";
        c.a = 1;
        f.events.push_back(c);
        CHECK_ANIM(r.c.apply(f), AnimCode::Ok);
        CHECK_EQ(unitCode(r.c, 0, 1), AnimCode::Ok);
        CHECK_EQ(r.prog(0, 1, 0)->program.nodes[0].ec, 2000);
        CHECK_EQ(r.prog(0, 1, 1)->program.nodes[0].ec, 2000);
        // a roll fires only on a record
        TimelineFrame g = Rig::frame(2, kH);
        TimelineEvent rr = ev(EventKind::Choice);
        rr.point = "roll:omni.fsmxml:4";
        rr.a = 1;
        rr.figures.all = false;
        rr.figures.figures = {0};
        g.events.push_back(rr);
        CHECK_ANIM(r.c.apply(g), AnimCode::Ok);
        CHECK_EQ(r.prog(0, 1, 0)->state, 6);
        CHECK_EQ(r.prog(0, 1, 1)->state, 1);
    }
    {
        Rig r;
        r.opt.forced = true;
        r.open(2, 0.0f);
        TimelineFrame f = Rig::frame(1, kH);
        f.events.push_back(createEv(0, 1));
        CHECK_ANIM(r.c.apply(f), AnimCode::Ok);
        CHECK_EQ(unitCode(r.c, 0, 1), AnimCode::ForcedChoiceMissing);
        CHECK(r.c.programs().empty());
    }
    {
        Rig r;
        r.opt.forced = true;
        r.open(2, 0.0f);
        TimelineFrame f = Rig::frame(1, kH);
        f.events.push_back(createEv(0, 1));
        TimelineEvent c = ev(EventKind::Choice);
        c.point = "option:omni.fsmxml:1";
        c.a = 5;   // the option gives 0 or 1
        f.events.push_back(c);
        CHECK_ANIM(r.c.apply(f), AnimCode::Ok);
        CHECK_EQ(unitCode(r.c, 0, 1), AnimCode::BadEvent);
    }
}

C5R_TEST(reader_path_through_files) {
    // the machine read by 4402's parse_fsmxml from the provider (no putMachine); a malformed file is BadLogicFile
    Rig r;
    r.files.files["reader.fsmxml"] =
        "<statemachine fsm=\"FGrannyStateMachine\" default_state=\"2\">\n"
        "  <states>\n"
        "    <state id=\"1\" type=\"FGrannyState\" name=\"A\" asset=\"idle.dge\" action=\"1000\" sockets=\"0\" />\n"
        "    <state id=\"2\" type=\"FGrannyState\" name=\"B\" asset=\"\" action=\"0\" sockets=\"1\" />\n"
        "  </states>\n"
        "  <sockets>\n"
        "    <socket from=\"2\" too=\"1\" bidirectional=\"0\" order=\"0\" conditions=\"1\">\n"
        "      <conditions><condition type=\"FFSMConditionEventSetPathOption\" order=\"0\" desc=\"\" path=\"0\" "
        "value=\"1\" /></conditions>\n"
        "    </socket>\n"
        "  </sockets>\n"
        "</statemachine>\n";
    r.files.files["broken.fsmxml"] = "<statemachine fsm=\"F\" default_state=\"1\"><states></statemachine>";
    r.open(1, 0.0f);
    c5r::units::UnitArt a;
    a.art_row = "ART_R";
    a.members.push_back(memberArt("M", 1, "Assets/Units/Reader.fsmxml", warriorClips()));
    r.art.rows["R"] = a;
    a.members[0] = memberArt("N", 1, "broken.fsmxml", warriorClips());
    r.art.rows["B"] = a;
    TimelineFrame f = Rig::frame(1, kH);
    f.events.push_back(createEv(0, 1, "R"));
    f.events.push_back(createEv(0, 2, "B"));
    CHECK_ANIM(r.c.apply(f), AnimCode::Ok);
    CHECK_EQ(unitCode(r.c, 0, 1), AnimCode::Ok);
    CHECK_EQ(r.prog(0, 1, 0)->program.nodes[0].ec, 2000);
    CHECK_ANIM(*r.c.status(0, 2), AnimCode::BadLogicFile);
}

// ---------------------------------------------------------------------------------------------- DoD F: statuses
C5R_TEST(status_machine_not_found) {
    CHECK_ANIM(statusWith(memberArt("Y", 1, "", warriorClips()), [](Rig&) {}), AnimCode::MachineNotFound);
    CHECK_ANIM(statusWith(memberArt("Y", 1, "absent.fsmxml", warriorClips()), [](Rig&) {}), AnimCode::MachineNotFound);
}

C5R_TEST(status_graph_not_found) {
    CHECK_ANIM(statusWith(memberArt("Y", 1, "g.fsmxml", warriorClips()),
                          [](Rig& r) {
                              c5m::StateMachine m = idleMachine();
                              m.states[0].asset = "absent_graph.dge";
                              r.c.logic()->putMachine("g.fsmxml", m);
                          }),
               AnimCode::GraphNotFound);
}

C5R_TEST(status_triggers_not_found_and_unknown_trigger) {
    CHECK_ANIM(statusWith(memberArt("Y", 1, "omni.fsmxml", warriorClips(), {"absent.ftsxml"}), [](Rig&) {}),
               AnimCode::TriggersNotFound);
    CHECK_ANIM(statusWith(memberArt("Y", 1, "omni.fsmxml", warriorClips(), {"odd.ftsxml"}),
                          [](Rig& r) {
                              c5m::TriggerSystem ts;
                              c5m::FtsTrigger t;
                              t.type = c5m::TriggerType::Unknown;
                              t.type_name = "FTimedTriggerSomethingElse";
                              ts.triggers.push_back(t);
                              r.c.logic()->putTriggers("odd.ftsxml", ts);
                          }),
               AnimCode::UnknownTrigger);
}

C5R_TEST(status_bad_logic_file) {
    CHECK_ANIM(statusWith(memberArt("Y", 1, "bad.fsmxml", warriorClips()),
                          [](Rig& r) { r.files.files["bad.fsmxml"] = "<statemachine"; }),
               AnimCode::BadLogicFile);
    CHECK_ANIM(statusWith(memberArt("Y", 1, "omni.fsmxml", warriorClips(), {"bad.ftsxml"}),
                          [](Rig& r) { r.files.files["bad.ftsxml"] = "<trigger_system><x></trigger_system>"; }),
               AnimCode::BadLogicFile);
    CHECK_ANIM(statusWith(memberArt("Y", 1, "dg.fsmxml", warriorClips()),
                          [](Rig& r) {
                              c5m::StateMachine m = idleMachine();
                              m.states[0].asset = "bad.dge";
                              r.c.logic()->putMachine("dg.fsmxml", m);
                              r.files.files["bad.dge"] = "not a granny file";
                          }),
               AnimCode::BadLogicFile);
}

C5R_TEST(status_unknown_condition_and_bad_socket) {
    CHECK_ANIM(statusWith(memberArt("Y", 1, "u.fsmxml", warriorClips()),
                          [](Rig& r) {
                              c5m::StateMachine m = idleMachine();
                              c5m::FsmCondition c;
                              c.type_name = "FFSMConditionSomethingNew";
                              m.sockets[4].conditions.push_back(c);
                              CHECK_ANIM(r.c.logic()->putMachine("u.fsmxml", m), AnimCode::UnknownCondition);
                          }),
               AnimCode::UnknownCondition);
    CHECK_ANIM(statusWith(memberArt("Y", 1, "s.fsmxml", warriorClips()),
                          [](Rig& r) {
                              c5m::StateMachine m = idleMachine();
                              m.sockets[4].to = 44;
                              CHECK_ANIM(r.c.logic()->putMachine("s.fsmxml", m), AnimCode::BadSocket);
                          }),
               AnimCode::BadSocket);
}

C5R_TEST(status_from_the_graph_factory) {
    // EcNotBound and UnknownNode are the evaluator's (4409); the controller passes them on per unit
    CHECK_ANIM(statusWith(memberArt("Y", 1, "omni.fsmxml", {{"w_idlea.gr2", 1000}}), [](Rig&) {}),
               AnimCode::EcNotBound);   // the idle graph's second source, 2000, is not bound
    CHECK_ANIM(statusWith(memberArt("Y", 1, "n.fsmxml", warriorClips()),
                          [](Rig& r) {
                              c5m::StateMachine m = idleMachine();
                              m.states[0].asset = "odd.dge";
                              r.c.logic()->putMachine("n.fsmxml", m);
                              c5m::DagFile d = dag({1000});
                              c5m::DagNode n;
                              n.name = "mystery";
                              d.dags[0].nodes.push_back(n);
                              r.c.logic()->putGraph("odd.dge", d);
                          }),
               AnimCode::UnknownNode);
}

C5R_TEST(status_no_progress) {
    CHECK_ANIM(statusWith(memberArt("Y", 1, "np.fsmxml", warriorClips()),
                          [](Rig& r) {
                              c5m::StateMachine m;
                              m.default_state = 2;
                              m.states = {state(1, "idle.dge"), state(2, "")};
                              m.sockets = {socket(2, 1, {checkPath(4, 1500)})};   // never at create
                              r.c.logic()->putMachine("np.fsmxml", m);
                          }),
               AnimCode::NoProgress);
}

C5R_TEST(status_unit_failed) {
    Rig r;
    r.open();
    TimelineFrame f = Rig::frame(1, kH);
    f.events.push_back(createEv(0, 3, "NO_SUCH_UNIT"));
    CHECK_ANIM(r.c.apply(f), AnimCode::Ok);
    CHECK_ANIM(*r.c.status(0, 3), AnimCode::UnitFailed);
    CHECK(r.c.programs().empty());
}

// 0439 (a): the DLL's GameplayUnitFortify(unit, 0) at every unit's setup on a civilian machine. civilian_state_01 reads
// 1000 only with a not-equal test: the request is written (the unit stays idle and Ok); on a machine that never reads
// 1000 the request is a no-op (not written, counted), the unit stays Ok and drawn (was BadEvent: AnimFailed, not drawn)
C5R_TEST(civilian_fortify_off_at_setup) {
    auto civilian = [](bool readsIdle) {
        c5m::StateMachine m;
        m.fsm = "FGrannyStateMachine";
        m.default_state = 2;
        m.states = {state(1, "idle.dge", 1000), state(2, ""), state(3, "fidget.dge", 1000), state(6, "idlecont.dge")};
        m.sockets = {socket(2, 1, {setPath(4, 1000)}), socket(1, 6, {roll(0.0f)}), socket(6, 3, {})};
        if (readsIdle) m.sockets.push_back(socket(3, 1, {checkPath(4, 1000, false)}));   // P4!=1000
        else m.sockets.push_back(socket(3, 1, {ecComplete(1040)}));
        return m;
    };
    for (bool reads : {true, false}) {
        Rig r;
        r.open(2, 0.0f, "civ.fsmxml");
        CHECK_ANIM(r.c.logic()->putMachine("civ.fsmxml", civilian(reads)), AnimCode::Ok);
        TimelineFrame f = Rig::frame(1, kH);
        f.events.push_back(createEv(0, 1));
        f.events.push_back(ev(EventKind::Fortify));   // a = 0: the setup's fortify off
        CHECK_ANIM(r.c.apply(f), AnimCode::Ok);
        CHECK_ANIM(*r.c.status(0, 1), AnimCode::Ok);
        CHECK_EQ(r.c.programs().size(), 2u);
        CHECK_EQ(r.c.lastApply().unread_requests, reads ? 0u : 2u);
        const FigureProgram* p = r.prog(0, 1, 0);
        CHECK(p && p->state == 1);
        // later frames: still drawn, still idle
        for (uint64_t seq = 2; seq < 10; ++seq) CHECK_ANIM(r.c.apply(Rig::frame(seq, kH)), AnimCode::Ok);
        CHECK_ANIM(*r.c.status(0, 1), AnimCode::Ok);
        CHECK_EQ(r.c.programs().size(), 2u);
    }
}

C5R_TEST(status_bad_event) {
    // a request the basic bridge gives no code for (work): BadEvent, no program
    {
        Rig r;
        r.open(2, 0.0f);
        TimelineFrame f = Rig::frame(1, kH);
        f.events.push_back(createEv(0, 1));
        TimelineEvent at = ev(EventKind::Action);
        at.action = ActionKind::Work;
        f.events.push_back(at);
        CHECK_ANIM(r.c.apply(f), AnimCode::Ok);
        CHECK_ANIM(*r.c.status(0, 1), AnimCode::BadEvent);
        CHECK(r.c.programs().empty());
    }
    // 0439: a code the machine never reads (bombard, 1180, on the omni test machine) is a no-op, counted per figure:
    // the unit stays Ok in its state and keeps its programs (was BadEvent)
    {
        Rig r;
        r.open(2, 0.0f);
        TimelineFrame f = Rig::frame(1, kH);
        f.events.push_back(createEv(0, 1));
        CHECK_ANIM(r.c.apply(f), AnimCode::Ok);
        const size_t before = r.c.programs().size();
        CHECK(before > 0);
        TimelineFrame g = Rig::frame(2, kH);
        TimelineEvent at = ev(EventKind::Action);
        at.action = ActionKind::Bombard;
        g.events.push_back(at);
        CHECK_ANIM(r.c.apply(g), AnimCode::Ok);
        CHECK_ANIM(*r.c.status(0, 1), AnimCode::Ok);
        CHECK_EQ(r.c.programs().size(), before);
        CHECK_EQ(r.c.lastApply().unread_requests, before);   // one per figure
        TimelineFrame h = Rig::frame(3, kH);
        CHECK_ANIM(r.c.apply(h), AnimCode::Ok);
        CHECK_EQ(r.c.lastApply().unread_requests, static_cast<size_t>(0));   // per frame
    }
    // frame level: not open, a bad dt
    Controller c;
    CHECK_ANIM(c.apply(Rig::frame(1, kH)), AnimCode::BadEvent);
    Rig r;
    r.open();
    CHECK_ANIM(r.c.apply(Rig::frame(1, -1)), AnimCode::BadEvent);
}

C5R_TEST(status_forced_choice_missing_named) {
    CHECK_ANIM(statusWith(memberArt("Y", 1, "omni.fsmxml", warriorClips()), [](Rig&) {}, true),
               AnimCode::ForcedChoiceMissing);
}

C5R_TEST(precheck_reachable_states) {
    Rig r;
    r.open();
    c5r::units::UnitArt good;
    good.art_row = "ART_GOOD";
    good.members.push_back(memberArt("M", 2, "omni.fsmxml", warriorClips()));
    c5m::StateMachine m = idleMachine();
    m.states.push_back(state(40, ""));    // a graphless dead end nothing reaches: fine
    CHECK_ANIM(r.c.logic()->putMachine("unreached.fsmxml", m), AnimCode::Ok);
    c5r::units::UnitArt unreached = good;
    unreached.art_row = "ART_UNREACHED";
    unreached.members[0] = memberArt("M2", 2, "unreached.fsmxml", warriorClips());
    m.sockets.push_back(socket(1, 40, {checkPath(4, 1100)}));
    CHECK_ANIM(r.c.logic()->putMachine("deadend.fsmxml", m), AnimCode::Ok);
    c5r::units::UnitArt dead = good;
    dead.art_row = "ART_DEAD";
    dead.members[0] = memberArt("M3", 2, "deadend.fsmxml", warriorClips());
    c5r::units::UnitArt unbound = good;
    unbound.art_row = "ART_UNBOUND";
    unbound.members[0] = memberArt("M4", 2, "omni.fsmxml", {{"w_idlea.gr2", 1000}, {"w_idleb.gr2", 2000}});
    const int before = r.graphs.binds;
    std::vector<ArtCheck> out = r.c.precheck({good, unreached, dead, unbound});
    CHECK_EQ(out.size(), static_cast<size_t>(4));
    CHECK_ANIM(out[0].status, AnimCode::Ok);
    CHECK_ANIM(out[1].status, AnimCode::Ok);
    CHECK_ANIM(out[2].status, AnimCode::NoProgress);
    CHECK_ANIM(out[3].status, AnimCode::EcNotBound);   // the fidget's 1040 (reachable through the roll)
    CHECK_EQ(out[2].art_row, std::string("ART_DEAD"));
    CHECK(r.graphs.binds > before);
}

C5R_TEST(status_names) {
    for (int c = 0; c <= static_cast<int>(AnimCode::UnitFailed); ++c)
        CHECK(std::strcmp(name(static_cast<AnimCode>(c)), "?") != 0);
    CHECK_EQ(std::string(name(AnimCode::NoProgress)), std::string("NoProgress"));
}

// 4738 (W40; the EXE's ECComplete as W42 read it on build 4390913): ECComplete(ec) holds when the time in the current
// state is above the duration of the member's clip for that ec, whatever the state's graph plays. The omni machines'
// ENTER_BOMBARD_DEFEND (15) in small: entered from the fortify loop (22) with path 6 = 1 its graph plays the bombard-
// defend loop 1285, not the enter clip 1280, and 15 -> 18 tests ECComplete(1280) only. With the clip durations known (the
// product's factory) the figure leaves 15 after the 1280 clip's 0.8 s and then takes combat ready (1600); with the old
// reading (the walk of the current graph: a factory that knows no durations) it stays frozen in 15 and never takes it
// (the 12 s limit fights of 4738 forms B and the machine gun's).
namespace {
struct DurFactory : public FakeFactory {
    bool known = true;
    float clipDuration(const std::string& clip) override {
        if (!known) return -1.0f;
        if (clip == "w_bd_enter.gr2") return 0.8f;
        if (clip == "w_bd_idle.gr2") return 2.0f;
        if (clip == "w_combat_ready.gr2") return 0.5f;
        return 4.0f;
    }
};
c5m::StateMachine bombardDefendMachine() {
    c5m::StateMachine m;
    m.fsm = "FGrannyStateMachine";
    m.default_state = 2;
    m.states = {state(1, "idle.dge", 1000), state(2, ""), state(22, "fortify.dge", 1500), state(15, "enterbd.dge"),
                state(18, "bdloop.dge", 1280), state(8, "cready.dge", 1600)};
    m.sockets = {
        socket(2, 1, {}),
        socket(1, 22, {checkPath(4, 1500)}),
        socket(1, 15, {checkPath(4, 1280), setPath(6, 0)}),
        socket(22, 15, {checkPath(4, 1280), setPath(6, 1)}),
        socket(15, 18, {ecComplete(1280)}),
        socket(18, 8, {checkPath(4, 1600)}),
    };
    return m;
}
}  // namespace

C5R_TEST(ec_complete_is_the_state_time_against_the_clip_duration) {
    for (int known = 0; known < 2; ++known) {
        MemFiles files;
        FakeArt art;
        DurFactory graphs;
        graphs.known = known != 0;
        graphs.select = 6;   // a graph of two sources plays Sources[int(path 6)]
        graphs.info[1000] = ClipInfo{6.0f, true};
        graphs.info[1520] = ClipInfo{4.0f, true};
        graphs.info[1280] = ClipInfo{0.8f, false};
        graphs.info[1285] = ClipInfo{2.0f, true};
        graphs.info[1600] = ClipInfo{0.5f, false};
        c5r::units::UnitArt a;
        a.art_row = "ART_W";
        a.members.push_back(memberArt("M", 1, "bd.fsmxml",
                                      {{"w_idlea.gr2", 1000}, {"w_fortify_idle.gr2", 1520}, {"w_bd_enter.gr2", 1280},
                                       {"w_bd_idle.gr2", 1285}, {"w_combat_ready.gr2", 1600}}));
        art.rows["W"] = a;
        Controller c;
        ControllerOptions opt;
        c.open(files, art, graphs, opt);
        CHECK_ANIM(c.logic()->putMachine("bd.fsmxml", bombardDefendMachine()), AnimCode::Ok);
        c.logic()->putGraph("idle.dge", dag({1000}));
        c.logic()->putGraph("fortify.dge", dag({1520}));
        c.logic()->putGraph("enterbd.dge", dag({1280, 1285}));
        c.logic()->putGraph("bdloop.dge", dag({1285}));
        c.logic()->putGraph("cready.dge", dag({1600}));
        auto stateOf = [&]() {
            for (auto& p : c.programs())
                if (p.player == 0 && p.id == 1 && p.figure == 0) return p.state;
            return -1;
        };
        TimelineFrame f = Rig::frame(1, 0.1);
        f.events.push_back(createEv(0, 1));
        TimelineEvent fo = ev(EventKind::Fortify);
        fo.a = 1;
        f.events.push_back(fo);
        CHECK_ANIM(c.apply(f), AnimCode::Ok);
        uint64_t seq = 2;
        for (int k = 0; k < 3; ++k) CHECK_ANIM(c.apply(Rig::frame(seq++, 0.1)), AnimCode::Ok);
        CHECK_EQ(stateOf(), 22);
        TimelineFrame b = Rig::frame(seq++, 0.1);
        TimelineEvent bd = ev(EventKind::Action);
        bd.action = ActionKind::BombardDefend;
        b.events.push_back(bd);
        CHECK_ANIM(c.apply(b), AnimCode::Ok);
        CHECK_EQ(stateOf(), 15);
        for (int k = 0; k < 5; ++k) CHECK_ANIM(c.apply(Rig::frame(seq++, 0.1)), AnimCode::Ok);   // 0.5 s in state 15
        CHECK_EQ(stateOf(), 15);
        for (int k = 0; k < 5; ++k) CHECK_ANIM(c.apply(Rig::frame(seq++, 0.1)), AnimCode::Ok);   // 1.0 s: past 0.8 s
        CHECK_EQ(stateOf(), known ? 18 : 15);
        TimelineFrame r = Rig::frame(seq++, 0.1);
        TimelineEvent cr = ev(EventKind::Action);
        cr.action = ActionKind::CombatReady;
        r.events.push_back(cr);
        CHECK_ANIM(c.apply(r), AnimCode::Ok);
        for (int k = 0; k < 30; ++k) CHECK_ANIM(c.apply(Rig::frame(seq++, 0.1)), AnimCode::Ok);   // 3 s more
        CHECK_EQ(stateOf(), known ? 8 : 15);   // the old reading: frozen in 15, the request never taken
        CHECK_ANIM(*c.status(0, 1), AnimCode::Ok);
    }
}

// 4738 (W42's 13.5): two more readings of the same rule. (a) A death state left by either of two ECCompletes (the
// airplane's 1200 / 2200 shape) leaves at the SHORTER clip's length, whichever clip its graph plays (here the longer).
// (b) An ECComplete on a code the member binds no clip to has threshold 0: it holds at the first update with any state
// time (was: never, no source of the code in any graph). Strict: a state time equal to the threshold does not hold.
C5R_TEST(ec_complete_shorter_alternative_and_missing_clip) {
    MemFiles files;
    FakeArt art;
    DurFactory graphs;
    graphs.info[1000] = ClipInfo{6.0f, true};
    graphs.info[1200] = ClipInfo{1.0f, false};
    graphs.info[2200] = ClipInfo{0.4f, false};
    c5r::units::UnitArt a;
    a.art_row = "ART_W";
    a.members.push_back(memberArt("M", 1, "dd.fsmxml",
                                  {{"w_idlea.gr2", 1000}, {"w_death_a.gr2", 1200}, {"w_bd_enter.gr2", 2200}}));
    art.rows["W"] = a;
    Controller c;
    ControllerOptions opt;
    c.open(files, art, graphs, opt);
    c5m::StateMachine m;
    m.fsm = "FGrannyStateMachine";
    m.default_state = 2;
    // 1 idle; 1 -> 13 death on P4 == 1200 (its graph plays 1200, 1.0 s: w_death_a's 4.0 s in DurFactory, see below);
    // 13 -> 21 on EC(1200) or EC(2200) (2200 is w_bd_enter.gr2: 0.8 s in DurFactory); 1 -> 30 on P4 == 1180; 30 -> 1 on
    // EC(9999), a code the member has no clip for
    m.states = {state(1, "idle.dge", 1000), state(2, ""), state(13, "death.dge", 1200), state(21, "dead.dge"),
                state(30, "idle.dge", 1180)};
    m.sockets = {socket(2, 1, {}), socket(1, 13, {checkPath(4, 1200)}), socket(13, 21, {ecComplete(1200)}),
                 socket(13, 21, {ecComplete(2200)}), socket(1, 30, {checkPath(4, 1180), setPath(4, 0)}),
                 socket(30, 1, {ecComplete(9999)})};
    CHECK_ANIM(c.logic()->putMachine("dd.fsmxml", m), AnimCode::Ok);
    c.logic()->putGraph("idle.dge", dag({1000}));
    c.logic()->putGraph("death.dge", dag({1200}));
    c.logic()->putGraph("dead.dge", dag({1200}));
    auto stateOf = [&]() {
        for (auto& p : c.programs())
            if (p.player == 0 && p.id == 1 && p.figure == 0) return p.state;
        return -1;
    };
    // (b) first: bombard (1180) into 30, whose only way out is EC(9999): no clip, threshold 0
    TimelineFrame f = Rig::frame(1, 0.1);
    f.events.push_back(createEv(0, 1));
    CHECK_ANIM(c.apply(f), AnimCode::Ok);
    TimelineFrame g = Rig::frame(2, kH);   // one clock step per frame here: the entry and the exit on separate updates
    TimelineEvent bo = ev(EventKind::Action);
    bo.action = ActionKind::Bombard;
    g.events.push_back(bo);
    CHECK_ANIM(c.apply(g), AnimCode::Ok);
    CHECK_EQ(stateOf(), 30);   // entered at this frame's update (state time 0: EC(9999) does not hold yet, strict >)
    uint64_t seq = 3;
    CHECK_ANIM(c.apply(Rig::frame(seq++, kH)), AnimCode::Ok);
    CHECK_EQ(stateOf(), 1);    // the next update, state time 1/30 s > 0: left (the request was cleared on the way in)
    // (a) the death: the shorter of 1200's 4.0 s and 2200's 0.8 s decides
    TimelineFrame d = Rig::frame(seq++, 0.1);
    d.events.push_back(ev(EventKind::Destroyed));
    CHECK_ANIM(c.apply(d), AnimCode::Ok);
    for (int k = 0; k < 6; ++k) CHECK_ANIM(c.apply(Rig::frame(seq++, 0.1)), AnimCode::Ok);   // 0.6 s
    CHECK_EQ(stateOf(), 13);
    for (int k = 0; k < 4; ++k) CHECK_ANIM(c.apply(Rig::frame(seq++, 0.1)), AnimCode::Ok);   // 1.0 s: past 0.8 s
    CHECK_EQ(stateOf(), 21);
}

C5R_TEST_MAIN
