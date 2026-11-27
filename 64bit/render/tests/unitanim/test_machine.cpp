// render/tests/unitanim/test_machine.cpp - issue 4403: the state machine interpreter (machine.h; unit-controller.md
// 6.2): binding and its refusals, the update rule, and DoD D's distributions over 10^5 updates.
#include <cmath>
#include <cstdio>
#include <set>

#include "anim_testkit.h"
#include "c5r/unitanim/random.h"

using namespace c5r::unitanim;
using namespace anim_test;

namespace {

const double kZ = 3.2905;
bool inInterval(long k, long n, double p) {
    const double m = n * p, sd = std::sqrt(n * p * (1 - p));
    return std::fabs(k - m) <= kZ * sd;
}

class Host : public MachineHost {
public:
    std::set<int32_t> complete;
    bool fire = false;          // roll answer when no rng
    Rng* rng = nullptr;
    int rolls = 0, options = 0;
    int32_t optionValue = -1;   // >= 0: the forced answer
    AnimStatus optionStatus;
    bool ecComplete(int32_t ec, float) override { return complete.count(ec) != 0; }
    AnimStatus roll(size_t, float chance, bool& fires) override {
        ++rolls;
        fires = rng ? rng->roll(chance) : fire;
        return AnimStatus::Ok();
    }
    AnimStatus option(size_t, const c5m::FsmCondition& c, int32_t& value) override {
        ++options;
        if (!optionStatus.ok()) return optionStatus;
        value = optionValue >= 0 ? optionValue : drawOption(c, rng ? rng->uniform() : 0.0);
        return AnimStatus::Ok();
    }
};

c5m::StateMachine two(std::vector<c5m::FsmCondition> conds) {
    c5m::StateMachine m;
    m.default_state = 1;
    m.states = {state(1, "a.dge"), state(2, "b.dge"), state(3, "c.dge")};
    m.sockets = {socket(1, 2, conds), socket(2, 1, {})};
    return m;
}

int idOf(const BoundMachine& b, const MachineState& s) { return b.fsm.states[s.state].id; }

}  // namespace

C5R_TEST(bind_ok_groups_and_sockets) {
    BoundMachine b;
    CHECK_ANIM(bindMachine(idleMachine(), "omni.fsmxml", b), AnimCode::Ok);
    CHECK_EQ(b.fsm.states[b.default_state].id, 2);
    CHECK_EQ(b.out[b.index[1]].size(), static_cast<size_t>(3));
    BoundMachine g;
    CHECK_ANIM(bindMachine(two({checkPath(4, 1), orOp(), checkPath(4, 2), stateTime(0.1f), orOp()}), "x", g),
               AnimCode::Ok);
    CHECK_EQ(g.groups[0].size(), static_cast<size_t>(3));   // [0,1) [2,4) [5,5)
    CHECK_EQ(g.groups[0][1].first, static_cast<size_t>(2));
    CHECK_EQ(g.groups[0][2].first, g.groups[0][2].second);
}

C5R_TEST(refuse_bad_socket) {
    BoundMachine b;
    c5m::StateMachine m = two({});
    m.sockets.push_back(socket(2, 8, {}));
    CHECK_ANIM(bindMachine(m, "x.fsmxml", b), AnimCode::BadSocket);
    CHECK(b.fsm.states.empty());
    m = two({});
    m.default_state = 5;
    CHECK_ANIM(bindMachine(m, "x.fsmxml", b), AnimCode::BadSocket);
    m = two({});
    m.states.push_back(state(1, "dup.dge"));
    CHECK_ANIM(bindMachine(m, "x.fsmxml", b), AnimCode::BadSocket);
    m = two({});
    m.sockets.push_back(socket(9, 1, {}));
    CHECK_ANIM(bindMachine(m, "x.fsmxml", b), AnimCode::BadSocket);
}

C5R_TEST(refuse_unknown_condition) {
    BoundMachine b;
    c5m::FsmCondition u;
    u.type = c5m::CondType::Unknown;
    u.type_name = "FFSMConditionSomethingNew";
    CHECK_ANIM(bindMachine(two({u}), "x.fsmxml", b), AnimCode::UnknownCondition);
    const c5m::CondType unimplemented[] = {c5m::CondType::AnyComplete, c5m::CondType::EventCopyVariable,
                                           c5m::CondType::EventSetVariable, c5m::CondType::Variable,
                                           c5m::CondType::True, c5m::CondType::False};
    for (auto t : unimplemented) CHECK_ANIM(bindMachine(two({cond(t)}), "x.fsmxml", b), AnimCode::UnknownCondition);
    c5m::FsmCondition c = checkPath(4, 1000);
    c.method = 2;
    CHECK_ANIM(bindMachine(two({c}), "x.fsmxml", b), AnimCode::UnknownCondition);
    c5m::FsmCondition t = stateTime(0.2f);
    t.method = 3;
    CHECK_ANIM(bindMachine(two({t}), "x.fsmxml", b), AnimCode::UnknownCondition);
    t.method = 1;   // read as method 5 (4408: the worker and engineer entries)
    CHECK_ANIM(bindMachine(two({t}), "x.fsmxml", b), AnimCode::Ok);
}

C5R_TEST(refuse_bad_logic_values) {
    BoundMachine b;
    CHECK_ANIM(bindMachine(two({checkPath(9, 1)}), "x.fsmxml", b), AnimCode::BadLogicFile);
    CHECK_ANIM(bindMachine(two({setPath(-1, 1)}), "x.fsmxml", b), AnimCode::BadLogicFile);
    CHECK_ANIM(bindMachine(two({randomPath(12, 1)}), "x.fsmxml", b), AnimCode::BadLogicFile);
    CHECK_ANIM(bindMachine(two({randomPath(0, 1, {{0, 0.0f}, {1, 0.0f}})}), "x.fsmxml", b), AnimCode::BadLogicFile);
    CHECK_ANIM(bindMachine(two({randomPath(0, 1, {{0, -0.5f}, {1, 1.0f}})}), "x.fsmxml", b), AnimCode::BadLogicFile);
    CHECK_ANIM(bindMachine(two({randomPath(0, -2)}), "x.fsmxml", b), AnimCode::BadLogicFile);
    CHECK_ANIM(bindMachine(two({roll(1.5f)}), "x.fsmxml", b), AnimCode::BadLogicFile);
}

C5R_TEST(update_and_of_tests_or_of_groups) {
    BoundMachine b;
    CHECK_ANIM(bindMachine(two({checkPath(4, 1100), checkPath(2, 1), orOp(), checkPath(4, 1500)}), "x", b),
               AnimCode::Ok);
    Host h;
    MachineState s;
    s.state = b.default_state;
    int taken = -2;
    s.opts.v[4] = 1100;
    CHECK_ANIM(updateMachine(b, s, h, taken), AnimCode::Ok);
    CHECK_EQ(taken, -1);                 // first group: P2 is 0
    s.opts.v[2] = 1;
    CHECK_ANIM(updateMachine(b, s, h, taken), AnimCode::Ok);
    CHECK_EQ(taken, 0);
    CHECK_EQ(idOf(b, s), 2);
    s.state = b.default_state;
    s.opts.v[4] = 1500;
    s.opts.v[2] = 0;
    CHECK_ANIM(updateMachine(b, s, h, taken), AnimCode::Ok);
    CHECK_EQ(taken, 0);                  // second group
    // not-equal
    BoundMachine n;
    CHECK_ANIM(bindMachine(two({checkPath(4, 1000, false)}), "x", n), AnimCode::Ok);
    MachineState t;
    t.opts.v[4] = 1000;
    CHECK_ANIM(updateMachine(n, t, h, taken), AnimCode::Ok);
    CHECK_EQ(taken, -1);
    t.opts.v[4] = 0;
    CHECK_ANIM(updateMachine(n, t, h, taken), AnimCode::Ok);
    CHECK_EQ(taken, 0);
}

C5R_TEST(update_first_socket_one_transition_events_of_firing_group) {
    c5m::StateMachine m;
    m.default_state = 1;
    m.states = {state(1, "a.dge"), state(2, "b.dge"), state(3, "c.dge")};
    // socket 0 fails (its event must not apply), socket 1 holds with events, socket 2 would hold too
    m.sockets = {socket(1, 3, {setPath(5, 9), checkPath(4, 7)}),
                 socket(1, 2, {setPath(0, 4), checkPath(4, 1), orOp(), setPath(1, 8)}),
                 socket(1, 3, {}),
                 socket(2, 3, {})};
    BoundMachine b;
    CHECK_ANIM(bindMachine(m, "x", b), AnimCode::Ok);
    Host h;
    MachineState s;
    s.state = b.default_state;
    s.opts.v[4] = 1;
    s.time = 3;
    int taken = -1;
    CHECK_ANIM(updateMachine(b, s, h, taken), AnimCode::Ok);
    CHECK_EQ(taken, 1);
    CHECK_EQ(idOf(b, s), 2);             // one transition only (2 -> 3 waits for the next update)
    CHECK_EQ(s.opts.v[0], 4.0f);
    CHECK_EQ(s.opts.v[5], 0.0f);         // the failing socket's event is not applied
    CHECK_EQ(s.opts.v[1], 0.0f);         // nor the other group's
    CHECK_EQ(s.time, 0.0);
    CHECK_ANIM(updateMachine(b, s, h, taken), AnimCode::Ok);
    CHECK_EQ(idOf(b, s), 3);
}

C5R_TEST(update_state_time_ec_complete_roll_short_circuit) {
    BoundMachine b;
    CHECK_ANIM(bindMachine(two({stateTime(0.2f), ecComplete(1100), roll(0.5f)}), "x", b), AnimCode::Ok);
    Host h;
    h.fire = true;
    MachineState s;
    int taken = -1;
    s.time = 0.1;
    CHECK_ANIM(updateMachine(b, s, h, taken), AnimCode::Ok);
    CHECK_EQ(taken, -1);
    CHECK_EQ(h.rolls, 0);                // a failing test stops the group: no draw
    s.time = 6 * (1.0 / 30.0);           // 0.2 in whole steps
    CHECK_ANIM(updateMachine(b, s, h, taken), AnimCode::Ok);
    CHECK_EQ(taken, -1);
    CHECK_EQ(h.rolls, 0);
    h.complete.insert(1100);
    h.fire = false;
    CHECK_ANIM(updateMachine(b, s, h, taken), AnimCode::Ok);
    CHECK_EQ(taken, -1);
    CHECK_EQ(h.rolls, 1);
    h.fire = true;
    CHECK_ANIM(updateMachine(b, s, h, taken), AnimCode::Ok);
    CHECK_EQ(taken, 0);
    // invert
    BoundMachine inv;
    CHECK_ANIM(bindMachine(two({roll(0.5f, true)}), "x", inv), AnimCode::Ok);
    MachineState t;
    h.fire = true;
    CHECK_ANIM(updateMachine(inv, t, h, taken), AnimCode::Ok);
    CHECK_EQ(taken, -1);
    h.fire = false;
    CHECK_ANIM(updateMachine(inv, t, h, taken), AnimCode::Ok);
    CHECK_EQ(taken, 0);
}

C5R_TEST(update_failing_option_leaves_state) {
    BoundMachine b;
    CHECK_ANIM(bindMachine(two({setPath(3, 5), randomPath(0, 1)}), "x", b), AnimCode::Ok);
    Host h;
    h.optionStatus = AnimStatus::Fail(AnimCode::ForcedChoiceMissing, "no record");
    MachineState s;
    int taken = -1;
    CHECK_ANIM(updateMachine(b, s, h, taken), AnimCode::ForcedChoiceMissing);
    CHECK_EQ(taken, -1);
    CHECK_EQ(idOf(b, s), 1);
    CHECK_EQ(s.opts.v[3], 0.0f);
}

C5R_TEST(draw_option_values) {
    c5m::FsmCondition u = randomPath(0, 2);
    CHECK_EQ(drawOption(u, 0.0), 0);
    CHECK_EQ(drawOption(u, 0.34), 1);
    CHECK_EQ(drawOption(u, 0.9999), 2);
    CHECK_EQ(drawOption(randomPath(0, 0), 0.7), 0);
    c5m::FsmCondition w = randomPath(1, 1, {{3, 0.6f}, {7, 0.4f}});
    CHECK_EQ(drawOption(w, 0.59), 3);
    CHECK_EQ(drawOption(w, 0.61), 7);
    CHECK(optionAllowed(w, 7));
    CHECK(!optionAllowed(w, 1));
    CHECK(optionAllowed(u, 2));
    CHECK(!optionAllowed(u, 3));
    CHECK(!optionAllowed(u, -1));
}

C5R_TEST(distribution_roll_rate_dod_d) {
    // an idle state rolling 0.0007 per update (omni idle) and 0.0001 (fortify), 10^5 updates each
    const float chances[] = {0.0007f, 0.0001f};
    for (float p : chances) {
        BoundMachine b;
        CHECK_ANIM(bindMachine(two({roll(p)}), "x", b), AnimCode::Ok);
        Rng r(unitSeed(4403, 1, static_cast<int32_t>(p * 1e5f)));
        Host h;
        h.rng = &r;
        const long n = 100000;
        long fires = 0;
        for (long i = 0; i < n; ++i) {
            MachineState s;
            s.state = b.default_state;
            int taken = -1;
            CHECK_ANIM(updateMachine(b, s, h, taken), AnimCode::Ok);
            fires += taken == 0;
        }
        CHECK(inInterval(fires, n, p));
        std::printf("DoD D RandomRoll chance %.4f: %ld of %ld updates fired (expected %.1f, 99.9%% interval +-%.1f)\n", p,
                    fires, n, n * p, kZ * std::sqrt(n * p * (1 - p)));
    }
}

C5R_TEST(distribution_random_path_option_dod_d) {
    struct Case { c5m::FsmCondition c; int32_t value; double p; };
    const Case cases[] = {
        {randomPath(0, 1, {{0, 0.6f}, {1, 0.4f}}), 0, 0.6},   // useweights 1, idle A/B after create
        {randomPath(1, 1, {{0, 0.3f}, {1, 0.7f}}), 1, 0.7},   // fidget / swap
        {randomPath(6, 1), 1, 0.5},                           // useweights 0: uniform over 0..1
        {randomPath(2, 2), 2, 1.0 / 3.0},                     // uniform over 0..2
    };
    for (auto& k : cases) {
        BoundMachine b;
        CHECK_ANIM(bindMachine(two({k.c}), "x", b), AnimCode::Ok);
        Rng r(unitSeed(4403, 2, k.c.path));
        Host h;
        h.rng = &r;
        const long n = 100000;
        long hits = 0;
        for (long i = 0; i < n; ++i) {
            MachineState s;
            int taken = -1;
            CHECK_ANIM(updateMachine(b, s, h, taken), AnimCode::Ok);
            hits += s.opts.v[k.c.path] == static_cast<float>(k.value);
        }
        CHECK(inInterval(hits, n, k.p));
        std::printf("DoD D EventRandomPathOption path %d useweights %d value %d: %ld of %ld draws gave %d (expected %.1f, "
                    "99.9%% interval +-%.1f)\n", k.c.path, k.c.use_weights ? 1 : 0, k.c.value, hits, n, k.value,
                    n * k.p, kZ * std::sqrt(n * k.p * (1 - k.p)));
    }
}

C5R_TEST_MAIN
