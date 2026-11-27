// render/tests/unitanim/test_combat.cpp - issue 4775: the combat choreography core (combat.h; combat-choreography.md
// 2-7, 13.3-13.5). Positive: melee, ranged, a city as the target, 1 figure against 12 and 12 against 1, a kill with the
// victory chance, the deaths by the damage rule, the hit moments at the Action / Transfer triggers, determinism per
// seed, the death removal, the export to timeline frames and their replay through the controller with the ActionBridge.
// Negative: Quick Combat, the limit (a missing clip, 4770's rule), and every refusal status with its text.
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <tuple>

#include "anim_testkit.h"
#include "c5r/unitanim/bridge.h"
#include "c5r/unitanim/combat.h"
#include "c5r/unitanim/timeline.h"

using namespace anim_test;

namespace {

const double kH = 1.0 / 30.0;

std::set<std::string> words(const std::string& s) {
    std::set<std::string> w;
    std::istringstream in(s);
    std::string x;
    while (in >> x) w.insert(x);
    return w;
}
const char* kSoldier = "Idle Attack RunCharge AttackCity Bombard Death BombardDefend Run Fortify CombatReady Walk "
                       "AttackCharge";

CombatMemberArt soldier(const std::string& name, int n) {
    CombatMemberArt m;
    m.member = name;
    m.count = n;
    m.domain = "Land";
    m.enable = words(kSoldier);
    m.victory = true;
    CombatWeapon blade;
    m.weapons.push_back(blade);
    CombatWeapon torch;   // the thrown torch (Vs_City), a Transfer
    torch.projectile = true;
    torch.vs_city = true;
    torch.flight = 0.4f;
    m.weapons.push_back(torch);
    return m;
}
// 5059: a member whose EnableActions carry the word Victory (none in VP; stock's victory needs it, 18.36)
CombatMemberArt withVictoryWord(CombatMemberArt m) {
    m.enable.insert("Victory");
    return m;
}
CombatMemberArt archer(const std::string& name, int n) {
    CombatMemberArt m = soldier(name, n);
    m.weapons.clear();
    CombatWeapon arrow;
    arrow.projectile = true;
    arrow.flight = 0.5f;
    m.weapons.push_back(arrow);
    CombatWeapon fire = arrow;
    fire.vs_city = true;
    m.weapons.push_back(fire);
    return m;
}

CombatUnitFigures unit(int32_t player, int32_t id, std::vector<CombatMemberArt> members) {
    CombatUnitFigures u;
    u.player = player;
    u.id = id;
    u.members = members;
    for (uint32_t i = 0; i < u.figures(); ++i) u.standing.push_back(static_cast<uint16_t>(i));
    return u;
}

// Clip timings: per code; the warrior's Action and Transfer times of unit-controller.md 6.4.
class Clips : public CombatClips {
public:
    std::map<int32_t, ActionTiming> t;
    std::set<int32_t> missing;
    int calls = 0;
    Clips() {
        t[1100] = timing(1.0f, {0.4889f});
        t[1140] = timing(1.0f, {0.3630f});
        t[1160] = timing(1.2f, {0.5759f}, true);
        t[1180] = timing(1.5f, {0.5778f}, true);
        t[1200] = timing(2.5f, {});
        for (int32_t c : {1000, 1120, 1280, 1400, 1600, 1800}) t[c] = timing(1.0f, {});
    }
    static ActionTiming timing(float d, std::vector<float> hits, bool transfer = false) {
        ActionTiming a;
        a.found = true;
        a.duration = d;
        a.hits = hits;
        a.transfer = transfer;
        return a;
    }
    ActionTiming timing(int32_t, int32_t, const CombatMemberArt& m, int32_t code) override {
        ++calls;
        ActionTiming a;
        if (missing.count(code) || !t.count(code)) {
            a.why = m.member + " code " + std::to_string(code) + ": clip file not in the install (test)";
            return a;
        }
        return t[code];
    }
};

CombatInput melee(int32_t afinal, int32_t dfinal) {
    CombatInput in;
    in.seq = 41;
    in.kind = kCombatKindUnit;
    in.flags = kCombatVisualize | kCombatDefenderRetaliates | kCombatGameFlagsRead;
    in.attacker.owner = 0;
    in.attacker.unit = 1;
    in.attacker.final_damage = afinal;
    in.defender.owner = 63;
    in.defender.unit = 2;
    in.defender.final_damage = dfinal;
    return in;
}

size_t count(const CombatResult& r, int32_t player, FigState s) {
    size_t n = 0;
    for (auto& x : r.steps)
        if (x.player == player && x.state == s) ++n;
    return n;
}
std::set<uint16_t> killed(const CombatResult& r, int32_t player) {
    std::set<uint16_t> k;
    for (auto& e : r.events)
        if (e.ev.kind == EventKind::Kill && e.ev.player == player)
            for (auto f : e.ev.figures.figures) k.insert(f);
    return k;
}
std::string dump(const CombatResult& r) {
    std::ostringstream o;
    o << r.end << "|";
    for (auto& s : r.steps)
        o << s.time << "," << s.player << "," << s.figure << "," << static_cast<int>(s.state) << "," << s.code << ","
          << s.target_figure << ";";
    for (auto& h : r.hits) o << h.time << "," << h.figure << "," << h.target_figure << "," << static_cast<int>(h.result);
    return o.str();
}
// every strike's hits land at its start plus the trigger time (plus the flight for a Transfer)
void checkHitMoments(const CombatResult& r, double flight) {
    for (auto& h : r.hits) {
        double start = -1;
        for (auto& s : r.steps)
            if (s.player == h.player && s.figure == h.figure && s.state == FigState::Strike && s.time <= h.launch + 1e-9)
                start = s.time;
        CHECK(start >= 0);
        double trig = h.code == 1100 ? 0.4889 : h.code == 1140 ? 0.3630 : h.code == 1160 ? 0.5759 : 0.5778;
        // a trigger is taken at the first step at or after it: launch within one step of start + trigger
        if (!(h.launch + 1e-6 >= start + trig && h.launch < start + trig + kH + 1e-6))
            c5r::test::fail(__FILE__, __LINE__, "hit of figure " + std::to_string(h.figure) + " code " +
                                                    std::to_string(h.code) + ": launch " + std::to_string(h.launch) +
                                                    ", strike start " + std::to_string(start));
        CHECK_NEAR(h.time - h.launch, h.projectile ? flight : 0.0, kH + 1e-6);
    }
}

}  // namespace

C5R_TEST(melee_deaths_follow_the_damage_and_hits_land_at_the_action_trigger) {
    Clips clips;
    CombatParams p;
    p.seed = 7;
    p.charge_any = false;   // 4737: this test's subject is the 4775 run-and-1100 path (the charge: below, its own test)
    // attacker 5 figures, 19 damage: round(0.95) = 1 dies; defender 38: round(1.9) = 2 die; the defender owes 1 hit
    CombatResult r = choreograph(melee(19, 38), unit(0, 1, {soldier("W", 5)}), unit(63, 2, {soldier("W", 5)}), clips, p);
    CHECK(r.status.ok());
    CHECK(!r.at_limit);
    CHECK(r.end > 1.0 && r.end < 12.0);
    CHECK_EQ(r.attacker.planned, 1u);
    CHECK_EQ(r.attacker.died, 1u);
    CHECK_EQ(r.defender.planned, 2u);
    CHECK_EQ(r.defender.died, 2u);
    CHECK_EQ(r.defender.owed, 1u);
    CHECK_EQ(r.defender.paid, 1u);
    CHECK_EQ(r.attacker.owed, 0u);   // the defender has no ranged member (13.4)
    // LastFirst: the last figures die, the first is the protected survivor
    CHECK(killed(r, 63) == std::set<uint16_t>({3, 4}));
    CHECK(killed(r, 0) == std::set<uint16_t>({4}));
    CHECK_EQ(r.defender.protected_figure, 0);
    // the attackers approached (run 1400, the soldier is no rush member), then struck with 1100
    bool ran = false, attacked = false;
    for (auto& s : r.steps) {
        if (s.player == 0 && s.state == FigState::Approach) {
            ran = true;
            CHECK_EQ(s.approach, 1);
            CHECK_EQ(s.code, 1400);
            CHECK_EQ(s.target_player, 63);
            CHECK(s.target_figure >= 0);
        }
        if (s.state == FigState::Strike) {
            attacked = true;
            CHECK_EQ(s.code, 1100);
        }
    }
    CHECK(ran && attacked);
    checkHitMoments(r, 0);
    // every death is a Death hit; a dying figure is removed after its 1200 clip (2.5 s, under the 3 s cap)
    size_t deaths = 0;
    for (auto& h : r.hits)
        if (h.result == HitResult::Death) ++deaths;
    CHECK_EQ(deaths, 3u);
    for (auto& s : r.steps)
        if (s.state == FigState::Dying) {
            bool removed = false;
            for (auto& x : r.steps)
                if (x.player == s.player && x.figure == s.figure && x.state == FigState::Removed) {
                    CHECK_NEAR(x.time - s.time, 2.5, kH + 1e-6);
                    removed = true;
                }
            CHECK(removed);
        }
    // events: run, combat_ready, attack, kill; no victory (no unit was killed, VictoryRule::OpponentKilled)
    std::set<int> kinds;
    for (auto& e : r.events) {
        CHECK(!e.ev.figures.all);
        CHECK(!e.ev.figures.figures.empty());
        if (e.ev.kind == EventKind::Action) kinds.insert(static_cast<int>(e.ev.action));
        CHECK(e.ev.action != ActionKind::Victory);
    }
    CHECK(kinds.count(static_cast<int>(ActionKind::Run)) && kinds.count(static_cast<int>(ActionKind::Attack)) &&
          kinds.count(static_cast<int>(ActionKind::CombatReady)) && kinds.count(static_cast<int>(ActionKind::Idle)));
    for (size_t i = 1; i < r.events.size(); ++i) CHECK(r.events[i].time >= r.events[i - 1].time);
}

C5R_TEST(deterministic_for_a_seed) {
    Clips c1, c2, c3;
    CombatParams p;
    p.seed = 11;
    auto a = unit(0, 1, {soldier("W", 8)});
    auto d = unit(63, 2, {soldier("W", 8)});
    CombatResult r1 = choreograph(melee(30, 60), a, d, c1, p);
    CombatResult r2 = choreograph(melee(30, 60), a, d, c2, p);
    CHECK(r1.status.ok() && r2.status.ok());
    CHECK_EQ(dump(r1), dump(r2));
    // another seed changes the tie-breaking draws (targets) somewhere over a few seeds
    bool differs = false;
    for (uint64_t s = 12; s < 20 && !differs; ++s) {
        p.seed = s;
        Clips c;
        if (dump(choreograph(melee(30, 60), a, d, c, p)) != dump(r1)) differs = true;
    }
    CHECK(differs);
    (void)c3;
}

C5R_TEST(ranged_bombard_lands_after_the_flight_and_the_defender_bombard_defends) {
    Clips clips;
    CombatParams p;
    CombatInput in = melee(0, 40);   // 5 figures, 40: 2 die
    in.flags = kCombatVisualize | kCombatRanged;   // no retaliation
    CombatResult r = choreograph(in, unit(0, 1, {archer("A", 2)}), unit(63, 2, {soldier("W", 5)}), clips, p);
    CHECK(r.status.ok());
    CHECK(!r.at_limit);
    CHECK_EQ(r.defender.died, 2u);
    CHECK_EQ(r.defender.paid, 1u);
    CHECK_EQ(r.attacker.owed, 0u);
    for (auto& s : r.steps) {
        CHECK(s.state != FigState::Approach && s.state != FigState::Charge);   // ranged: no approach
        if (s.state == FigState::Strike) CHECK_EQ(s.code, 1180);
        if (s.player == 63 && s.time == 0) {
            CHECK(s.state == FigState::Defend);
            CHECK_EQ(s.code, 1280);
        }
    }
    CHECK_EQ(count(r, 63, FigState::Strike), 0u);
    CHECK(!r.hits.empty());
    for (auto& h : r.hits) CHECK(h.projectile);
    checkHitMoments(r, 0.5);
}

// 4783 (W33, C2; unit-combat-server.md 18.2, combat-choreography.md 13.4): a lone striker kills every figure of the
// defender; the defender's owed hit goes with its first death as the EXE's hit pays it. With the 4775 rule (knob off) no
// figure is left to take the owed hit after the last death and the fight holds to its 12 s limit (the battleship's
// infantry kill offline: 14 of 14 dead at 9.48 s, then 12.00 s).
C5R_TEST(a_killed_sides_owed_hit_goes_with_its_first_death) {
    for (int c2 = 0; c2 < 2; ++c2) {
        Clips clips;
        clips.t[1180] = Clips::timing(1.0f, {0.3f}, true);   // one Transfer per strike: no hit in flight at the end
        CombatParams p;
        p.owed_paid_by_any_hit = c2 != 0;
        CombatInput in = melee(0, 100);   // the defender killed
        in.flags = kCombatVisualize | kCombatRanged;
        CombatResult r = choreograph(in, unit(0, 1, {archer("A", 1)}), unit(63, 2, {soldier("W", 6)}), clips, p);
        CHECK(r.status.ok());
        CHECK_EQ(r.defender.planned, 6u);
        CHECK_EQ(r.defender.died, 6u);
        CHECK_EQ(r.defender.owed, 1u);
        if (c2) {
            CHECK(!r.at_limit);
            CHECK_EQ(r.defender.paid, 1u);
            CHECK(r.end < 10.0);
            // the fight ends when the last dying figure is removed: its death plus the 1200 clip (2.5 s)
            double lastDeath = 0;
            for (auto& h : r.hits)
                if (h.result == HitResult::Death) lastDeath = std::max(lastDeath, h.time);
            CHECK_NEAR(r.end, lastDeath + 2.5, 2 * kH + 1e-6);
        } else {
            CHECK(r.at_limit);
            CHECK_NEAR(r.end, 12.0, 1e-9);
            CHECK_EQ(r.defender.paid, 0u);
        }
    }
}

// 4733 (W36; combat-choreography.md 13.4 [C], unit-combat-server.md 18.8): the attacker's three strikers kill both figures
// of a killed defender before the defender's slow strike lands. With the 4775 rule (knob off) no defending figure is left
// to play the attacker's two planned deaths and the fight holds to its 12 s limit (live W0-b, fights 59984 and 128427:
// a 14-figure attacker, 0 of 4-6 deaths played). Stock spares the defender's last standing figure while the attacker
// still has deaths open: it is only hit, lands its strikes, and dies at the first landing after the attacker's last death.
namespace {
class SlowDefender : public Clips {
public:
    ActionTiming timing(int32_t player, int32_t id, const CombatMemberArt& m, int32_t code) override {
        if (player == 63 && code == 1100) return Clips::timing(2.0f, {1.8f});
        return Clips::timing(player, id, m, code);
    }
};
}  // namespace

C5R_TEST(a_killed_sides_last_figure_is_spared_while_the_other_side_has_deaths_open) {
    for (int spare = 0; spare < 2; ++spare) {
        SlowDefender clips;
        CombatParams p;
        p.spare_last_figure = spare != 0;
        // 4746: the timings below are the rule before 4746 (the waiting defenders combat ready from the start); with
        // ready_after_move the defenders' first strike waits for their 1600 entry (1 s here): checked after the loop
        p.ready_after_move = false;
        // 5057 W85: the timings below are the rule before 5057 too (the type 2 defender strikes before the attacker's first
        // landing; with defender_waits_first_landing the old rule's defenders die before they strike); the default below
        p.defender_waits_first_landing = false;
        // attacker 14 figures at 15 damage: 2 planned deaths; defender 2 figures killed
        CombatResult r = choreograph(melee(15, 100), unit(0, 1, {soldier("W", 14)}), unit(63, 2, {soldier("W", 2)}), clips, p);
        CHECK(r.status.ok());
        CHECK_EQ(r.attacker.planned, 2u);
        CHECK_EQ(r.defender.planned, 2u);
        CHECK_EQ(r.defender.died, 2u);
        CHECK_EQ(r.defender.paid, 1u);
        size_t spared = 0;
        double lastAttackerDeath = 0, lastDefenderDeath = 0;
        for (auto& h : r.hits) {
            if (h.target_player == 63 && h.result != HitResult::Death) ++spared;
            if (h.result == HitResult::Death && h.target_player == 0) lastAttackerDeath = std::max(lastAttackerDeath, h.time);
            if (h.result == HitResult::Death && h.target_player == 63) lastDefenderDeath = std::max(lastDefenderDeath, h.time);
        }
        if (spare) {
            CHECK(!r.at_limit);
            CHECK(r.end < 10.0);
            CHECK_EQ(r.attacker.died, 2u);
            CHECK(spared >= 1u);                               // the last figure was only hit at least once
            CHECK(lastDefenderDeath > lastAttackerDeath);      // it died after the attacker's last death was played
            CHECK(count(r, 63, FigState::Strike) >= 2u);        // and struck twice to play them
        } else {
            CHECK(r.at_limit);
            CHECK_NEAR(r.end, 12.0, 1e-9);
            CHECK_EQ(r.attacker.died, 0u);
            CHECK_EQ(count(r, 63, FigState::Strike) > 0u, true);   // its strikes started, none landed
        }
    }
    // 4746: the default (ready_after_move) spares the last figure the same way, later by the defenders' 1600 entry
    SlowDefender clips;
    CombatResult r = choreograph(melee(15, 100), unit(0, 1, {soldier("W", 14)}), unit(63, 2, {soldier("W", 2)}), clips,
                                 CombatParams());
    CHECK(r.status.ok() && !r.at_limit);
    CHECK_EQ(r.attacker.died, 2u);
    CHECK_EQ(r.defender.died, 2u);
    CHECK(count(r, 63, FigState::Strike) >= 2u);
}

// 4738 (W36; unit-combat-server.md 18.9 form C, live fe44 event id 120850): a melee attack without positions by a unit
// whose weapon is a projectile (the Great War Infantry's HasShortRangedAttack) against a melee-only defender. With the
// 4775 rule the attackers stood still, none was engaged, the defender found no target and the attacker's planned deaths
// held the fight to the limit; every attacking figure of a melee attack now approaches (the EXE's rule, 4776 M3). A
// defender with a projectile weapon (needs no engaged target) ends either way.
// 4737 x 4738 (W43): with charge_any (default on) such an attacker whose member enables RunCharge and AttackCharge (the
// test's archer has the soldier's enable words) charges instead of running: 1120, then its first strike 1140, as the
// EXE's target step does for every approaching attacker (exe-audio.md 13.3 [C]); charge_any off: the run (1400).
C5R_TEST(an_unplaced_melee_attacker_with_a_projectile_weapon_approaches) {
    for (int charge = 0; charge < 2; ++charge)
    for (int on = 0; on < 2; ++on) {
        for (int defArcher = 0; defArcher < 2; ++defArcher) {
            Clips clips;
            CombatParams p;
            p.unplaced_attacker_approaches = on != 0;
            p.charge_any = charge != 0;
            // attacker 14 at 20 damage: 3 planned deaths; defender 8 at 40: 3 planned (no positions given)
            CombatResult r = choreograph(melee(20, 40), unit(0, 1, {archer("G", 14)}),
                                         unit(63, 2, {defArcher ? archer("C", 8) : soldier("M", 8)}), clips, p);
            CHECK(r.status.ok());
            CHECK_EQ(r.attacker.planned, 3u);
            const size_t runs = count(r, 0, FigState::Approach), charges = count(r, 0, FigState::Charge);
            const bool approached = runs + charges > 0;
            if (on) {   // 4737: the approach is a charge with charge_any, a run without
                CHECK_EQ(charges > 0, charge != 0);
                CHECK_EQ(runs > 0, charge == 0);
                std::set<uint16_t> struck;
                for (auto& x : r.steps)
                    if (x.player == 0 && x.state == FigState::Strike && struck.insert(static_cast<uint16_t>(x.figure)).second)
                        CHECK_EQ(x.code, charge ? 1140 : 1100);
                CHECK(!struck.empty());
            }
            if (on || defArcher) {
                CHECK(!r.at_limit);
                CHECK_EQ(r.attacker.died, 3u);
                CHECK_EQ(r.defender.died, 3u);
                CHECK_EQ(approached, on != 0);
            } else {
                CHECK(r.at_limit);
                CHECK_EQ(r.attacker.died, 0u);
                CHECK(!approached);
                CHECK_EQ(count(r, 63, FigState::Strike), 0u);   // the melee defender never found a target
            }
        }
    }
}

// 4783 (W33, C1; combat-choreography.md 5.4 [C]): one landing strikes max(1, ceil(U(Min, Max) % x the target's figures))
// figures when the weapon names VisKillStrength, the aimed one and the nearest others, as many as planned deaths are open.
namespace {
CombatMemberArt gunner(const std::string& name, int n, float lo, float hi) {
    CombatMemberArt m = archer(name, n);
    for (auto& w : m.weapons) {
        w.vis_kill_min = lo;
        w.vis_kill_max = hi;
    }
    return m;
}
// deaths per landing: the Death hits grouped by (striker, figure, launch)
std::map<std::tuple<int, int, double>, int> deathsPerLanding(const CombatResult& r) {
    std::map<std::tuple<int, int, double>, int> g;
    for (auto& h : r.hits)
        if (h.result == HitResult::Death) g[std::make_tuple(static_cast<int>(h.id), static_cast<int>(h.figure), h.launch)]++;
    return g;
}
}  // namespace

C5R_TEST(a_hit_strikes_as_many_figures_as_its_vis_kill_strength) {
    CombatInput in = melee(0, 100);   // 14 figures killed by a lone gunner
    in.flags = kCombatVisualize | kCombatRanged;
    for (int c1 = 0; c1 < 2; ++c1) {
        Clips clips;
        CombatParams p;
        p.seed = 5;
        p.vis_kill = c1 != 0;
        CombatResult r = choreograph(in, unit(0, 1, {gunner("G", 1, 25.0f, 50.0f)}), unit(63, 2, {soldier("W", 14)}), clips, p);
        CHECK(r.status.ok());
        CHECK_EQ(r.defender.planned, 14u);
        if (c1) {
            CHECK(!r.at_limit);
            CHECK_EQ(r.defender.died, 14u);
            CHECK(r.end < 10.0);
            auto g = deathsPerLanding(r);
            CHECK(g.size() >= 2 && g.size() <= 4);   // 14 deaths at 4-7 per landing
            size_t i = 0;
            for (auto& kv : g) {
                if (++i < g.size()) CHECK(kv.second >= 4 && kv.second <= 7);   // ceil(0.25 x 14) .. ceil(0.50 x 14)
                else CHECK(kv.second >= 1 && kv.second <= 7);                  // the last: what was left
            }
            // the extra figures' records carry no effect, sound or trigger; one record per figure, no figure twice
            std::set<int> dead;
            for (auto& h : r.hits)
                if (h.result == HitResult::Death) CHECK(dead.insert(h.target_figure).second);
            CHECK_EQ(dead.size(), 14u);
            // the same seed gives the same run
            Clips c2;
            CHECK_EQ(dump(choreograph(in, unit(0, 1, {gunner("G", 1, 25.0f, 50.0f)}), unit(63, 2, {soldier("W", 14)}), c2, p)),
                     dump(r));
        } else {
            CHECK(r.at_limit);   // one figure per landing: 14 strikes of 1.5 s do not fit in 12 s
            for (auto& kv : deathsPerLanding(r)) CHECK_EQ(kv.second, 1);
        }
    }
    // Max - Min below 1: the fraction is Min, ceil(0.30 x 14) = 5 per landing; deaths capped at the planned ones
    {
        Clips clips;
        CombatInput part = melee(0, 50);   // 14 x 50 % = 7 die
        part.flags = kCombatVisualize | kCombatRanged;
        CombatResult r = choreograph(part, unit(0, 1, {gunner("G", 1, 30.0f, 30.5f)}), unit(63, 2, {soldier("W", 14)}), clips,
                                     CombatParams());
        CHECK(r.status.ok() && !r.at_limit);
        CHECK_EQ(r.defender.died, 7u);
        auto g = deathsPerLanding(r);
        CHECK_EQ(g.size(), 2u);
        if (g.size() == 2u) {
            CHECK_EQ(g.begin()->second, 5);
            CHECK_EQ(std::next(g.begin())->second, 2);
        }
        CHECK(r.defender.protected_figure >= 0);
        for (auto& h : r.hits)
            if (h.result == HitResult::Death) CHECK(h.target_figure != r.defender.protected_figure);
    }
    // Max 0 (NULL): one figure per landing, as before
    {
        Clips clips;
        CombatInput part = melee(0, 40);
        part.flags = kCombatVisualize | kCombatRanged;
        CombatResult r = choreograph(part, unit(0, 1, {gunner("G", 2, 20.0f, 0.0f)}), unit(63, 2, {soldier("W", 5)}), clips,
                                     CombatParams());
        CHECK(r.status.ok() && !r.at_limit);
        for (auto& kv : deathsPerLanding(r)) CHECK_EQ(kv.second, 1);
    }
}

C5R_TEST(a_city_as_the_target_takes_its_owed_hit) {
    Clips clips;
    CombatParams p;
    CombatInput in = melee(12, 70);
    in.kind = kCombatKindCity;
    in.defender.unit = -1;
    in.defender.city = 7;
    in.defender.max_hp = 258;
    CombatResult r = choreograph(in, unit(0, 1, {soldier("W", 4)}), CombatUnitFigures(), clips, p);
    CHECK(r.status.ok());
    CHECK(!r.at_limit);
    CHECK(r.defender.city);
    CHECK_EQ(r.defender.planned, 0u);
    CHECK_EQ(r.defender.paid, 1u);
    CHECK_EQ(r.attacker.planned, 0u);   // 4 figures, 12 of 100: round(0.48) = 0
    CHECK_EQ(count(r, 0, FigState::Strike), 1u);
    for (auto& s : r.steps)
        if (s.state == FigState::Strike) {
            CHECK_EQ(s.code, 1160);   // attack city with the thrown torch (a Transfer)
            CHECK_EQ(s.target_figure, -1);
        }
    CHECK_EQ(r.hits.size(), 1u);
    CHECK(r.hits[0].projectile && r.hits[0].target_figure == -1 && r.hits[0].result == HitResult::OwedHit);
    checkHitMoments(r, 0.4);
}

C5R_TEST(one_figure_against_twelve_and_twelve_against_one) {
    Clips clips;
    clips.t[1100] = Clips::timing(0.8f, {0.4889f});
    CombatParams p;
    // 1 attacker figure, 10 damage: no death; 12 defenders, 50 damage: 6 die, 1 owed hit: six strikes in turn, the owed
    // hit paid by the first death (4783 C2, combat-choreography.md 13.4 [C]; seven with the 4775 rule)
    CombatResult r = choreograph(melee(10, 50), unit(0, 1, {soldier("S", 1)}), unit(63, 2, {soldier("W", 12)}), clips, p);
    CHECK(r.status.ok());
    CHECK(!r.at_limit);
    CHECK_EQ(r.defender.died, 6u);
    CHECK_EQ(r.defender.paid, 1u);
    CHECK_EQ(count(r, 0, FigState::Strike), 6u);
    {
        Clips c0;
        c0.t[1100] = Clips::timing(0.8f, {0.4889f});
        CombatParams p0 = p;
        p0.owed_paid_by_any_hit = false;
        CombatResult r0 = choreograph(melee(10, 50), unit(0, 1, {soldier("S", 1)}), unit(63, 2, {soldier("W", 12)}), c0, p0);
        CHECK_EQ(count(r0, 0, FigState::Strike), 7u);
    }
    CHECK(killed(r, 63) == std::set<uint16_t>({6, 7, 8, 9, 10, 11}));
    CHECK_EQ(r.attacker.protected_figure, 0);
    // 12 against a lone defender figure that survives: the protected figure is the only one, so it is targeted (0557
    // n1/n2: one-figure ships ended in 3-4 s, not at the limit)
    Clips c2;
    CombatResult q = choreograph(melee(30, 30), unit(0, 1, {soldier("W", 12)}), unit(63, 2, {soldier("S", 1)}), c2, p);
    CHECK(q.status.ok());
    CHECK(!q.at_limit);
    CHECK_EQ(q.defender.protected_figure, 0);
    CHECK_EQ(q.defender.died, 0u);
    CHECK_EQ(q.defender.paid, 1u);
    CHECK_EQ(q.attacker.planned, 4u);   // 12 x 30 % = 3.6 -> 4
    CHECK_EQ(q.attacker.died, 4u);      // the lone defender strikes them down
    CHECK(q.end < 12.0);
}

// 4743 W81: with charge_any off, as this test was written (its subject is the 3-in-4 draw among figures whose last action
// is 1600 / 1000). With charge_any on (the default since W81) an attacker whose last action is its charge attack (1140)
// is not victory-eligible ([C 5.3]'s last-action test) and ends Done, so the rate over all attackers is lower.
// 5059: the members carry the enable word Victory (with victory_needs_word on, the default, a member without it never takes
// victory: a_victory_needs_the_enable_word).
C5R_TEST(a_kill_takes_every_figure_and_victory_is_drawn_about_three_in_four) {
    long eligible = 0, won = 0;
    for (uint64_t s = 0; s < 300; ++s) {
        Clips clips;
        CombatParams p;
        p.charge_any = false;   // 4743 W81: see above
        p.seed = s;
        CombatResult r = choreograph(melee(3, 100), unit(0, 1, {withVictoryWord(soldier("W", 4))}),
                                     unit(63, 2, {withVictoryWord(soldier("W", 3))}), clips, p);
        CHECK(r.status.ok());
        CHECK(r.defender.killed);
        CHECK_EQ(r.defender.planned, 3u);
        CHECK_EQ(r.defender.died, 3u);
        CHECK_EQ(r.defender.protected_figure, -1);
        CHECK(killed(r, 63) == std::set<uint16_t>({0, 1, 2}));
        for (auto& x : r.steps)
            if (x.player == 0 && (x.state == FigState::Victory || x.state == FigState::Done)) {
                ++eligible;
                if (x.state == FigState::Victory) {
                    ++won;
                    CHECK_EQ(x.code, 1800);
                }
            }
    }
    CHECK_EQ(eligible, 1200L);   // 4 attackers x 300 combats, each ends once
    const double rate = static_cast<double>(won) / eligible, pp = 0.74;
    CHECK(std::fabs(rate - pp) <= 3.2905 * std::sqrt(pp * (1 - pp) / eligible));
    // no victory where the opponent lives (OpponentKilled), some with VictoryRule::Always
    Clips c;
    CombatParams p;
    p.victory = VictoryRule::Always;
    CombatResult a = choreograph(melee(3, 50), unit(0, 1, {withVictoryWord(soldier("W", 4))}),
                                 unit(63, 2, {withVictoryWord(soldier("W", 3))}), c, p);
    CHECK(a.status.ok());
    CHECK(count(a, 0, FigState::Victory) + count(a, 63, FigState::Victory) > 0);
    // a member that does not take 1800 never plays it (with the word too)
    Clips c2;
    CombatMemberArt w = withVictoryWord(soldier("W", 4));
    w.victory = false;
    CombatResult b = choreograph(melee(3, 100), unit(0, 1, {w}), unit(63, 2, {soldier("W", 3)}), c2, CombatParams());
    CHECK_EQ(count(b, 0, FigState::Victory), 0u);
}

C5R_TEST(targeted_deaths_spare_the_protected_figure) {
    Clips clips;
    CombatParams p;
    p.deaths = DeathPick::Targeted;
    CombatResult r = choreograph(melee(0, 60), unit(0, 1, {soldier("W", 6)}), unit(63, 2, {soldier("W", 6)}), clips, p);
    CHECK(r.status.ok());
    CHECK_EQ(r.defender.died, 4u);   // 6 x 60 % = 3.6 -> 4
    CHECK(!killed(r, 63).count(static_cast<uint16_t>(r.defender.protected_figure)));
    CHECK_EQ(killed(r, 63).size(), 4u);
}

C5R_TEST(rush_members_charge_and_strike_with_charge_attack) {
    Clips clips;
    CombatMemberArt h = soldier("H", 3);
    h.rush = true;
    CombatResult r = choreograph(melee(10, 40), unit(0, 1, {h}), unit(63, 2, {soldier("W", 5)}), clips, CombatParams());
    CHECK(r.status.ok());
    CHECK(count(r, 0, FigState::Charge) > 0);
    bool charge_attack = false;
    for (auto& s : r.steps) {
        if (s.state == FigState::Charge) {
            CHECK_EQ(s.code, 1120);
            CHECK_EQ(s.approach, 2);
        }
        if (s.player == 0 && s.state == FigState::Strike && s.code == 1140) charge_attack = true;
    }
    CHECK(charge_attack);   // 1140 after 1120 (0321 5.2)
    checkHitMoments(r, 0);
    // a rush member without AttackCharge (VP's horseman, knight): run charge, then combat ready, then 1100
    Clips c2;
    CombatMemberArt k = soldier("K", 3);
    k.rush = true;
    k.enable.erase("AttackCharge");
    CombatResult q = choreograph(melee(10, 40), unit(0, 1, {k}), unit(63, 2, {soldier("W", 5)}), c2, CombatParams());
    CHECK(q.status.ok());
    for (auto& s : q.steps)
        if (s.player == 0 && s.state == FigState::Strike) {
            CHECK_EQ(s.code, 1100);
            bool readied = false;
            for (auto& x : q.steps)
                if (x.player == 0 && x.figure == s.figure && x.state == FigState::Ready && x.time < s.time) readied = true;
            CHECK(readied);
        }
}

// 4737: stock's target step sends every attacking figure out of reach into the charge state 0xC (1120), rush or not
// (exe-audio.md 13.3 [C]); with AttackCharge it strikes with 1140 and never enters combat ready (1600) first, so its
// 1600 clip's FOOT_ALWAYS / FORTIFY_VOX are not played (stock's swordsman, W37's Tiny pair [L])
// (4743: charge_any was off by default from W46 and is on again since W81; this test names it on, and
// charge_any_is_on_by_default pins the default)
C5R_TEST(a_soldier_charges_by_default_and_skips_combat_ready) {
    Clips clips;
    CombatParams p;
    p.charge_any = true;
    CombatMemberArt s = soldier("S", 12);   // the swordsman's shape: RunCharge and AttackCharge, no rush formation
    CHECK(!s.rush);
    CombatResult r = choreograph(melee(10, 40), unit(0, 1, {s}), unit(63, 2, {soldier("W", 12)}), clips, p);
    CHECK(r.status.ok() && !r.at_limit);
    CHECK(count(r, 0, FigState::Charge) > 0);
    CHECK_EQ(count(r, 0, FigState::Approach), 0u);
    std::set<uint16_t> struck;
    for (auto& x : r.steps) {
        if (x.player != 0) continue;
        if (x.state == FigState::Charge) {
            CHECK_EQ(x.code, 1120);
            CHECK_EQ(x.approach, 2);
        }
        if (x.state == FigState::Strike && struck.insert(static_cast<uint16_t>(x.figure)).second) {
            CHECK_EQ(x.code, 1140);   // its first strike: the charge attack
            for (auto& y : r.steps)   // no combat-ready entry before it
                if (y.player == 0 && y.figure == x.figure && y.time < x.time) CHECK(y.code != 1600);
        }
    }
    CHECK(!struck.empty());
    checkHitMoments(r, 0);
    // off (the 4775 rule): the same soldier runs (1400) and enters combat ready (1600) before its 1100
    Clips c2;
    CombatParams old;
    old.charge_any = false;
    CombatResult q = choreograph(melee(10, 40), unit(0, 1, {s}), unit(63, 2, {soldier("W", 12)}), c2, old);
    CHECK(q.status.ok());
    CHECK_EQ(count(q, 0, FigState::Charge), 0u);
    bool ready1600 = false;
    for (auto& x : q.steps)
        if (x.player == 0 && x.code == 1600) ready1600 = true;
    CHECK(ready1600);
    // a member whose charge art is missing keeps the run (no new hold to the limit) [I]
    Clips c3;
    c3.missing.insert(1140);
    CombatResult m = choreograph(melee(10, 40), unit(0, 1, {s}), unit(63, 2, {soldier("W", 12)}), c3, p);
    CHECK(m.status.ok() && !m.at_limit);
    CHECK_EQ(count(m, 0, FigState::Charge), 0u);
}

// 4743: charge_any was OFF by default from W46 (MC #9883) to W81: with it on before charge_wait, a charge's 1140 strike
// could be asked of a figure whose machine is not in its run loop (the only state that takes 1140), and the fight held
// to the 12 s limit (unit-combat-server.md 18.13); the fix: a_charge_attack_waits_for_the_machine_to_take_it below.
// 4743 W81 (MC #11142, after W79's live S26-S30, unit-combat-server.md 18.28, 18.28.1): ON by default again. By default a
// non-rush soldier with RunCharge and AttackCharge charges (1120) and strikes first with 1140, with no combat-ready
// entry (1600) before it; with charge_any off (the 4775 rule) it runs (1400), enters combat ready and strikes 1100.
C5R_TEST(charge_any_is_on_by_default) {
    CHECK(CombatParams().charge_any);
    Clips clips;
    CombatMemberArt s = soldier("S", 12);   // RunCharge and AttackCharge, no rush formation
    CHECK(!s.rush);
    CombatResult r = choreograph(melee(10, 40), unit(0, 1, {s}), unit(63, 2, {soldier("W", 12)}), clips, CombatParams());
    CHECK(r.status.ok() && !r.at_limit);
    CHECK(count(r, 0, FigState::Charge) > 0);
    CHECK_EQ(count(r, 0, FigState::Approach), 0u);
    std::set<uint16_t> struck;
    for (auto& x : r.steps)
        if (x.player == 0 && x.state == FigState::Strike && struck.insert(static_cast<uint16_t>(x.figure)).second)
            CHECK_EQ(x.code, 1140);   // each figure's first strike: the charge attack
    CHECK(!struck.empty());
    // off: the 4775 rule
    Clips c2;
    CombatParams off;
    off.charge_any = false;
    CombatResult q = choreograph(melee(10, 40), unit(0, 1, {s}), unit(63, 2, {soldier("W", 12)}), c2, off);
    CHECK(q.status.ok() && !q.at_limit);
    CHECK_EQ(count(q, 0, FigState::Charge), 0u);
    CHECK(count(q, 0, FigState::Approach) > 0);
    bool strike = false;
    for (auto& x : q.steps)
        if (x.player == 0 && x.state == FigState::Strike) {
            CHECK_EQ(x.code, 1100);
            strike = true;
        }
    CHECK(strike);
}

// 4746 (W57; exe-audio.md 13.3 [C], unit-combat-server.md 10.10 P2 (b), 10.12): stock's state setter requests combat
// ready (1600) for a waiting figure (state 2) only when its current request, or with none its last action, is a movement
// (1400 run, 1450 shuffle, 1120 run charge) and its member enables Walk (1450); otherwise it makes no new request (an
// idle defender waiting for its turn stays in its idle clip). State 5 (ready to strike) always requests 1600. Counts the
// Wait steps that request 1600 against that rule (the figure's last request before the step, 1000 at rest).
static size_t waitingCombatReadyAgainstStock(const CombatResult& r, const std::set<std::string>& walkers) {
    std::map<std::tuple<int32_t, int32_t, uint16_t>, int32_t> last;
    size_t bad = 0;
    for (auto& s : r.steps) {
        const auto k = std::make_tuple(s.player, s.id, s.figure);
        const auto it = last.find(k);
        const int32_t prev = it == last.end() ? 1000 : it->second;
        const bool moved = prev == 1400 || prev == 1450 || prev == 1120;
        const bool walk = walkers.count(std::to_string(s.player) + ":" + std::to_string(s.id)) != 0;
        if (s.state == FigState::Wait && s.code == 1600 && !(moved && walk)) ++bad;
        if (s.code != 0) last[k] = s.code;
    }
    return bad;
}

// 4746: a melee fight's waiting figures (the defenders before an attacker is engaged; attackers without a free target)
// do not enter combat ready, as stock's idle figures do not, so their 1600 clip's tier -1 FOOT_ALWAYS / FORTIFY_VOX are
// not sent (W44's Tiny pair: 10 fortify vox and 10-20 foot dirt per melee fight on route S, none on stock [L]); a figure
// that strikes still enters combat ready first (state 5), except a figure whose first strike is its charge attack (1140:
// with charge_any, the default since 4743 W81, a charging attacker skips the entry, 4737)
C5R_TEST(a_waiting_figure_enters_combat_ready_only_after_a_move) {
    const std::set<std::string> both = {"0:1", "63:2"};
    for (int dfinal : {40, 100}) {   // a melee fight and a kill
        Clips clips;
        CombatResult r = choreograph(melee(10, dfinal), unit(0, 1, {soldier("S", 12)}), unit(63, 2, {soldier("W", 12)}),
                                     clips, CombatParams());
        CHECK(r.status.ok() && !r.at_limit);
        CHECK_EQ(waitingCombatReadyAgainstStock(r, both), 0u);
        // the defenders strike, and every striking figure requested 1600 in a Ready step before its first strike
        std::set<std::pair<int32_t, uint16_t>> struck;
        size_t defStrikes = 0, chargeStrikes = 0;
        for (auto& x : r.steps) {
            if (x.state != FigState::Strike || !struck.insert({x.player, x.figure}).second) continue;
            defStrikes += x.player == 63;
            if (x.code == 1140) {   // 4743 W81: a charge attack, no entry before it (4737)
                CHECK_EQ(x.player, 0);
                ++chargeStrikes;
                continue;
            }
            bool readied = false;
            for (auto& y : r.steps)
                if (y.player == x.player && y.figure == x.figure && y.time <= x.time && y.state == FigState::Ready &&
                    y.code == 1600)
                    readied = true;
            CHECK(readied);
        }
        CHECK(defStrikes > 0);
        CHECK(chargeStrikes > 0);   // 4743 W81: the swordsman's figures charge by default
    }
    // a member without Walk: no waiting figure of it requests 1600 at all
    Clips clips;
    CombatMemberArt s = soldier("S", 12), w = soldier("W", 12);
    s.enable.erase("Walk");
    w.enable.erase("Walk");
    CombatResult q = choreograph(melee(10, 40), unit(0, 1, {s}), unit(63, 2, {w}), clips, CombatParams());
    CHECK(q.status.ok() && !q.at_limit);
    CHECK_EQ(waitingCombatReadyAgainstStock(q, {}), 0u);
    // the knob: on by default; off is the rule before 4746 (the waiting defenders request 1600 at the start)
    CHECK(CombatParams().ready_after_move);
    Clips c2;
    CombatParams old;
    old.ready_after_move = false;
    CombatResult o = choreograph(melee(10, 40), unit(0, 1, {soldier("S", 12)}), unit(63, 2, {soldier("W", 12)}), c2, old);
    CHECK(o.status.ok() && !o.at_limit);
    CHECK(waitingCombatReadyAgainstStock(o, both) > 0);
}

// 4750 (W61, unit-combat-server.md 10.14): a ranged attack's non-answering target takes stock's phase-2 stance (0xF:
// 1450) before bombard defend, so its machine takes 1280 on the silent 1285 branch; members without Walk, ships, a
// missing 1450 clip and the knob off request 1280 at once. The fight's hits and end do not move.
namespace {
// per figure of `player`: the code of its first Defend step and the time of its first Defend step with 1280
struct DefendSeq {
    int32_t first = 0;
    double at1280 = -1;
    bool walkEvent = false;
};
std::map<uint16_t, DefendSeq> defendSeq(const CombatResult& r, int32_t player) {
    std::map<uint16_t, DefendSeq> m;
    for (auto& s : r.steps) {
        if (s.player != player || s.state != FigState::Defend) continue;
        DefendSeq& d = m[s.figure];
        if (d.first == 0) d.first = s.code;
        if (s.code == 1280 && d.at1280 < 0) d.at1280 = s.time;
    }
    for (auto& e : r.events)
        if (e.ev.kind == EventKind::Action && e.ev.player == player && e.ev.action == ActionKind::Walk) {
            std::vector<uint16_t> who = e.ev.figures.figures;
            if (e.ev.figures.all)
                for (auto& kv : m) who.push_back(kv.first);
            for (uint16_t f : who) m[f].walkEvent = true;
        }
    return m;
}
CombatInput rangedNoAnswer() {
    CombatInput in = melee(0, 40);   // 5 figures, 40: 2 die
    in.flags = kCombatVisualize | kCombatRanged;
    return in;
}
}  // namespace

C5R_TEST(a_non_answering_target_takes_the_phase_2_stance_before_bombard_defend) {
    CHECK(CombatParams().defend_from_stance);
    CHECK_NEAR(CombatParams().defend_stance_s, 0.45, 1e-12);
    Clips clips;
    clips.t[1450] = Clips::timing(0.8f, {});
    // 5053 W153: end_after_dying off, as written (4750, before 5053): its end shuffle is a Walk (1450) request of its own,
    // which this case's walk count would take for the phase 2 stance (e1 in test_combat_special.cpp covers it)
    CombatParams p;
    p.end_after_dying = false;
    CombatResult r = choreograph(rangedNoAnswer(), unit(0, 1, {archer("A", 2)}), unit(63, 2, {soldier("W", 5)}), clips, p);
    CHECK(r.status.ok() && !r.at_limit);
    const auto seq = defendSeq(r, 63);
    CHECK_EQ(seq.size(), static_cast<size_t>(5));
    for (auto& kv : seq) {
        CHECK_EQ(kv.second.first, 1450);   // stock's 0xF first, sent to the controller as a walk (1450) request
        CHECK(kv.second.walkEvent);
        CHECK(kv.second.at1280 >= 0.45 - 1e-9 && kv.second.at1280 < 0.45 + kH + 1e-6);
    }
    CHECK_EQ(count(r, 63, FigState::Strike), 0u);
    CHECK_EQ(r.defender.died, 2u);

    // the knob off: 1280 at once (the rule before 4750); the strikes, hits and the end are the same
    Clips c2;
    c2.t[1450] = Clips::timing(0.8f, {});
    CombatParams off;
    off.defend_from_stance = false;
    off.end_after_dying = false;   // 5053 W153: as p
    CombatResult o = choreograph(rangedNoAnswer(), unit(0, 1, {archer("A", 2)}), unit(63, 2, {soldier("W", 5)}), c2, off);
    CHECK(o.status.ok() && !o.at_limit);
    for (auto& kv : defendSeq(o, 63)) {
        CHECK_EQ(kv.second.first, 1280);
        CHECK_NEAR(kv.second.at1280, 0.0, 1e-9);
        CHECK(!kv.second.walkEvent);
    }
    CHECK_NEAR(o.end, r.end, 1e-9);
    CHECK_EQ(o.hits.size(), r.hits.size());
    for (size_t i = 0; i < o.hits.size() && i < r.hits.size(); ++i) {
        CHECK_NEAR(o.hits[i].time, r.hits[i].time, 1e-9);
        CHECK_EQ(o.hits[i].target_figure, r.hits[i].target_figure);
    }

    // no stance: a member without Walk, a ship, a missing 1450 clip
    CombatMemberArt noWalk = soldier("W", 5);
    noWalk.enable.erase("Walk");
    CombatMemberArt ship = soldier("SHIP", 1);
    ship.domain = "Sea";
    struct Case {
        const char* name;
        CombatMemberArt target;
        bool clip;
    } cases[] = {{"no Walk", noWalk, true}, {"ship", ship, true}, {"no 1450 clip", soldier("W", 5), false}};
    for (auto& c : cases) {
        Clips cc;
        if (c.clip) cc.t[1450] = Clips::timing(0.8f, {});
        CombatResult x = choreograph(rangedNoAnswer(), unit(0, 1, {archer("A", 2)}), unit(63, 2, {c.target}), cc,
                                     CombatParams());
        CHECK(x.status.ok());
        for (auto& kv : defendSeq(x, 63)) {
            if (kv.second.first != 1280 || std::fabs(kv.second.at1280) > 1e-9)
                c5r::test::fail(__FILE__, __LINE__, std::string(c.name) + ": figure " + std::to_string(kv.first) +
                                                        " first Defend code " + std::to_string(kv.second.first));
        }
    }

    // a defender that answers takes no stance (it strikes)
    Clips c3;
    c3.t[1450] = Clips::timing(0.8f, {});
    CombatInput ans = rangedNoAnswer();
    ans.flags |= kCombatDefenderRetaliates;
    CombatResult a = choreograph(ans, unit(0, 1, {archer("A", 2)}), unit(63, 2, {archer("B", 5)}), c3, CombatParams());
    CHECK(a.status.ok());
    for (auto& s : a.steps) CHECK(!(s.player == 63 && s.code == 1450));
}

// 4743 (W46, unit-combat-server.md 18.13): a feedback double with the omni machine's rule for the charge
// (unit-controller.md 6.2 [C]): 1140 is taken only from the run loop, which a figure reaches 0.1 s after a 1120 reached
// it in combat ready; 1100 is taken in combat ready; per frame only the last request of a figure counts (path 4 is a
// level, the frame's events are applied before the machine steps), and the machine advances once per frame by
// min(dt, 0.2 s) (the controller's continuous clock, clock.h max_dt).
class RunLoopMachine : public CombatFeedback {
public:
    enum { kReady, kStarting, kRun, kAttack };
    struct F {
        int state = kReady;
        double t = 0;
        int32_t code = 0;
    };
    std::map<std::pair<int32_t, uint16_t>, F> figs;   // (player, figure)
    std::map<int32_t, uint16_t> counts;               // figures per player
    int played1140 = 0;
    bool accepts(int32_t player, int32_t, uint16_t figure, int32_t code) override {
        const F& f = figs[std::make_pair(player, figure)];
        return code == 1140 ? f.state == kRun : f.state == kReady;
    }
    bool playing(int32_t player, int32_t, uint16_t figure, int32_t code) override {
        const F& f = figs[std::make_pair(player, figure)];
        return f.state == kAttack && f.code == code;
    }
    static int32_t codeOf(ActionKind a) {
        switch (a) {
        case ActionKind::Charge: return 1120;
        case ActionKind::AttackCharge: return 1140;
        case ActionKind::Attack: return 1100;
        case ActionKind::CombatReady: return 1600;
        case ActionKind::Run: return 1400;
        default: return 1000;
        }
    }
    void frame(const std::vector<TimedEvent>& evs, double dt) {
        std::map<std::pair<int32_t, uint16_t>, int32_t> last;
        for (auto& e : evs) {
            if (e.ev.kind != EventKind::Action) continue;
            std::vector<uint16_t> who = e.ev.figures.figures;
            if (e.ev.figures.all)
                for (uint16_t i = 0; i < counts[e.ev.player]; ++i) who.push_back(i);
            for (uint16_t x : who) last[std::make_pair(e.ev.player, x)] = codeOf(e.ev.action);
        }
        for (auto& kv : last) {
            F& f = figs[kv.first];
            const int32_t c = kv.second;
            if (c == 1120 && f.state == kReady) f = F{kStarting, 0, 0};
            else if (c == 1140 && f.state == kRun) f = F{kAttack, 0, 1140};
            else if (c == 1100 && f.state == kReady) f = F{kAttack, 0, 1100};
            else if (c != 1120 && c != 1140 && c != 1100 && (f.state == kStarting || f.state == kRun)) f = F{};
            if (f.state == kAttack && f.code == 1140 && f.t == 0) ++played1140;
        }
        for (auto& kv : figs) {
            F& f = kv.second;
            f.t += std::min(dt, 0.2);
            if (f.state == kStarting && f.t >= 0.1) f = F{kRun, 0, 0};
            if (f.state == kAttack && f.t >= 1.0) f = F{};
        }
    }
};

CombatResult runWithFrames(const CombatParams& p, RunLoopMachine& fb, double firstDt) {
    Clips clips;
    CombatMemberArt s = soldier("S", 6);   // RunCharge and AttackCharge, no rush formation: charges with charge_any
    CombatUnitFigures a = unit(0, 1, {s}), d = unit(63, 2, {soldier("W", 10)});
    fb.counts[0] = 6;
    fb.counts[63] = 10;
    CombatRun run;
    CHECK(run.start(melee(40, 100), a, d, clips, p, &fb).ok());
    std::vector<TimedEvent> evs;
    double dt = firstDt;
    for (int k = 0; k < 2000; ++k) {
        evs.clear();
        const bool done = run.advance(dt, evs);
        fb.frame(evs, dt);
        dt = kH;
        if (done) break;
    }
    return run.result();
}

// 4743: the S15 form. The charge (unplaced: 1.0 s) begins and ends inside one long first frame, so the machine gets the
// 1140 without the 1120 before it and stays in combat ready; with the 4737 rule (charge_wait < 0) the strike never
// plays and the fight holds to the limit. With the wait, the figure strikes 1140 once its machine is in the run loop,
// else it stops in combat ready and strikes 1100; with short frames the charge attack is still played.
C5R_TEST(a_charge_attack_waits_for_the_machine_to_take_it) {
    CombatParams old;
    old.charge_any = true;
    old.charge_wait = -1;
    RunLoopMachine m0;
    CombatResult r0 = runWithFrames(old, m0, 2.0);
    CHECK(r0.status.ok() && r0.at_limit);
    CHECK_EQ(m0.played1140, 0);
    CombatParams p;
    p.charge_any = true;
    CHECK(p.charge_wait > 0);
    RunLoopMachine m1;
    CombatResult r1 = runWithFrames(p, m1, 2.0);
    CHECK(r1.status.ok() && !r1.at_limit);
    bool struck1100 = false;
    for (auto& x : r1.steps)
        if (x.player == 0 && x.state == FigState::Strike && x.code == 1100) struck1100 = true;
    CHECK(struck1100);
    // short frames: the 1120 reaches the machine in an earlier frame; the charge attack plays, as before
    RunLoopMachine m2;
    CombatResult r2 = runWithFrames(p, m2, kH);
    CHECK(r2.status.ok() && !r2.at_limit);
    CHECK(m2.played1140 > 0);
    // charge_any off: no charge, no hold
    CombatParams off;
    off.charge_any = false;
    RunLoopMachine m3;
    CombatResult r3 = runWithFrames(off, m3, 2.0);
    CHECK(r3.status.ok() && !r3.at_limit);
    CHECK_EQ(count(r3, 0, FigState::Charge), 0u);
}

C5R_TEST(quick_combat_has_no_choreography) {
    Clips clips;
    CombatInput in = melee(19, 38);
    in.flags |= kCombatQuickCombat;
    CombatResult r = choreograph(in, unit(0, 1, {soldier("W", 5)}), unit(63, 2, {soldier("W", 5)}), clips, CombatParams());
    CHECK(r.status.code == CombatCode::QuickCombat);
    CHECK(!r.status.text.empty());
    CHECK(r.steps.empty() && r.events.empty() && r.hits.empty());
    CHECK_EQ(clips.calls, 0);
    CHECK(combatFrames(r, kH, 1).empty());
    // the quick bit without the "read" bit is not read
    in.flags = static_cast<uint16_t>(in.flags & ~kCombatGameFlagsRead);
    CHECK(choreograph(in, unit(0, 1, {soldier("W", 5)}), unit(63, 2, {soldier("W", 5)}), clips, CombatParams())
              .status.ok());
}

C5R_TEST(a_missing_attack_clip_holds_the_combat_to_the_limit) {
    Clips clips;
    clips.missing.insert(1100);
    CombatParams p0;
    p0.charge_any = false;   // 4737: the subject is the missing 1100 (a charging soldier strikes with 1140)
    CombatResult r = choreograph(melee(19, 38), unit(0, 1, {soldier("W", 5)}), unit(63, 2, {soldier("W", 5)}), clips, p0);
    CHECK(r.status.ok());
    CHECK(r.at_limit);
    CHECK_NEAR(r.end, 12.0, 1e-9);
    CHECK_EQ(r.defender.died, 0u);   // no strike ever lands; the damage event after the combat takes them out
    bool noted = false;
    for (auto& n : r.notes)
        if (n.text.find("MissingClip") == 0 && n.text.find("1100") != std::string::npos) noted = true;
    CHECK(noted);
    // at the limit every figure ends (0xB) with an idle request, no victory
    CHECK_EQ(count(r, 0, FigState::Done), 5u);
    CHECK_EQ(count(r, 63, FigState::Done), 5u);
    // the 20 s limit is a parameter (air combats are 4777's)
    Clips c2;
    c2.missing.insert(1100);
    CombatParams p;
    p.limit = 20.0;
    p.charge_any = false;   // 4737: as above
    CHECK_NEAR(choreograph(melee(19, 38), unit(0, 1, {soldier("W", 5)}), unit(63, 2, {soldier("W", 5)}), c2, p).end, 20.0,
               1e-9);
    // a missing death clip: removed at the 3 s cap
    Clips c3;
    c3.missing.insert(1200);
    CombatResult d = choreograph(melee(19, 38), unit(0, 1, {soldier("W", 5)}), unit(63, 2, {soldier("W", 5)}), c3,
                                 CombatParams());
    CHECK(d.status.ok() && !d.at_limit);
    for (auto& s : d.steps)
        if (s.state == FigState::Dying)
            for (auto& x : d.steps)
                if (x.player == s.player && x.figure == s.figure && x.state == FigState::Removed)
                    CHECK_NEAR(x.time - s.time, 3.0, kH + 1e-6);
}

C5R_TEST(every_refusal_names_its_cause) {
    Clips clips;
    auto A = unit(0, 1, {soldier("W", 5)});
    auto D = unit(63, 2, {soldier("W", 5)});
    auto refused = [&](const CombatInput& in, const CombatUnitFigures& a, const CombatUnitFigures& d, CombatCode want,
                       const CombatParams& p) {
        CombatResult r = choreograph(in, a, d, clips, p);
        if (r.status.code != want)
            c5r::test::fail(__FILE__, __LINE__, std::string("want ") + combatCodeName(want) + ", got " +
                                                    combatCodeName(r.status.code) + ": " + r.status.text);
        CHECK(!r.status.text.empty());
        CHECK(r.steps.empty() && r.events.empty() && r.hits.empty() && r.notes.empty());
    };
    const CombatParams p;
    CombatInput in = melee(0, 40);
    in.kind = 26;
    refused(in, A, D, CombatCode::UnknownKind, p);
    in = melee(0, 40);
    in.flags = kCombatDefenderRetaliates;
    refused(in, A, D, CombatCode::NotVisualised, p);
    // 4777: CityAttacker, Nuclear, AirCombat and AreaMembers are played since 4777 (test_combat_special.cpp,
    // every_former_refusal_is_played); their names stay
    // BadInput
    refused(melee(0, 101), A, D, CombatCode::BadInput, p);
    refused(melee(-1, 40), A, D, CombatCode::BadInput, p);
    in = melee(0, 40);
    in.defender.max_hp = 0;
    refused(in, A, D, CombatCode::BadInput, p);
    refused(melee(0, 40), unit(0, 9, {soldier("W", 5)}), D, CombatCode::BadInput, p);   // ids differ
    in = melee(0, 40);
    in.defender.city = 4;   // unit and city both
    refused(in, A, D, CombatCode::BadInput, p);
    CombatUnitFigures bad = D;
    bad.standing = {2, 1};
    refused(melee(0, 40), A, bad, CombatCode::BadInput, p);
    bad.standing = {0, 5};
    refused(melee(0, 40), A, bad, CombatCode::BadInput, p);
    refused(melee(0, 40), A, unit(63, 2, {soldier("W", 65)}), CombatCode::BadInput, p);
    refused(melee(0, 40), A, unit(63, 2, {}), CombatCode::BadInput, p);
    CombatParams q;
    q.step = 0;
    refused(melee(0, 40), A, D, CombatCode::BadInput, q);
    q = CombatParams();
    q.limit = std::nan("");
    refused(melee(0, 40), A, D, CombatCode::BadInput, q);
    q = CombatParams();
    q.victory_threshold = 100;
    refused(melee(0, 40), A, D, CombatCode::BadInput, q);
    // NoFigures
    bad = D;
    bad.standing.clear();
    refused(melee(0, 40), A, bad, CombatCode::NoFigures, p);
    CHECK_EQ(std::string(combatCodeName(CombatCode::Nuclear)), std::string("Nuclear"));
}

C5R_TEST(a_figure_that_cannot_attack_bombard_defends) {
    Clips clips;
    CombatMemberArt civ;
    civ.member = "CIV";
    civ.count = 2;
    civ.enable = words("Idle Death BombardDefend Run");
    CombatResult r = choreograph(melee(0, 60), unit(0, 1, {soldier("W", 3)}), unit(63, 2, {civ}), clips, CombatParams());
    CHECK(r.status.ok());
    CHECK(!r.at_limit);
    CHECK_EQ(count(r, 63, FigState::Strike), 0u);
    for (auto& s : r.steps)
        if (s.player == 63 && s.time == 0) CHECK(s.state == FigState::Defend && s.code == 1280);
    // a naval unit plays the general rule with a note (4777's specifics)
    CombatMemberArt ship = soldier("SHIP", 1);
    ship.domain = "Sea";
    Clips c2;
    CombatResult n = choreograph(melee(17, 51), unit(0, 1, {ship}), unit(63, 2, {ship}), c2, CombatParams());
    CHECK(n.status.ok() && !n.at_limit);
    CHECK(!n.notes.empty() && n.notes[0].text.find("Naval") == 0);
}

C5R_TEST(a_city_target_takes_the_attackers_planned_deaths_at_its_hit) {
    Clips clips;
    CombatInput in = melee(12, 70);   // 5 figures, 12 of 100: round(0.6) = 1 dies; the city cannot strike it
    in.kind = kCombatKindCity;
    in.defender.unit = -1;
    in.defender.city = 7;
    in.defender.max_hp = 258;
    CombatResult r = choreograph(in, unit(0, 1, {soldier("W", 5)}), CombatUnitFigures(), clips, CombatParams());
    CHECK(r.status.ok());
    CHECK(!r.at_limit);   // not the literal 12 s hold: 0557 c1 / c2 ended in 2.97 / 3.50 s
    CHECK_EQ(r.attacker.planned, 1u);
    CHECK_EQ(r.attacker.died, 1u);
    CHECK(killed(r, 0) == std::set<uint16_t>({4}));
    CHECK_EQ(r.defender.paid, 1u);
    CHECK(r.end < 6.0);
}

C5R_TEST(a_strike_waits_for_the_entry_into_combat_ready) {
    Clips clips;
    clips.t[1600] = Clips::timing(0.9f, {});
    CombatInput in = melee(0, 40);
    in.flags = kCombatVisualize | kCombatRanged;
    // 5050 (b) off: the entry from idle at once (attack_from_stance is on by default since 5050 W74)
    CombatParams noStance;
    noStance.attack_from_stance = false;
    CombatResult r = choreograph(in, unit(0, 1, {archer("A", 2)}), unit(63, 2, {soldier("W", 5)}), clips, noStance);
    CHECK(r.status.ok());
    double first = 1e9;
    for (auto& s : r.steps)
        if (s.state == FigState::Strike) first = std::min(first, s.time);
    CHECK(first + 1e-9 >= 0.9);   // 0321 5.3: 5 -> 10 when its animation is done
    CHECK(first < 0.9 + kH + 1e-9);
    // the default (5050 (b) on): no 1450 clip here, so the phase-2 hold (attack_stance_s) and then the whole entry
    {
        CombatResult d = choreograph(in, unit(0, 1, {archer("A", 2)}), unit(63, 2, {soldier("W", 5)}), clips, CombatParams());
        CHECK(d.status.ok());
        double fd = 1e9;
        for (auto& s : d.steps)
            if (s.state == FigState::Strike) fd = std::min(fd, s.time);
        const double hold = CombatParams().attack_stance_s;
        CHECK(fd + 1e-9 >= hold + 0.9);
        CHECK(fd < hold + 0.9 + 2 * kH + 1e-9);
    }
    // max_in_flight 1: one strike at a time against the defender
    CombatParams p;
    p.max_in_flight = 1;
    Clips c2;
    CombatResult q = choreograph(melee(0, 40), unit(0, 1, {soldier("W", 5)}), unit(63, 2, {soldier("W", 5)}), c2, p);
    CHECK(q.status.ok() && !q.at_limit);
    std::vector<std::pair<double, double>> spans;
    for (auto& s : q.steps)
        if (s.player == 0 && s.state == FigState::Strike) spans.push_back(std::make_pair(s.time, s.time + 1.0));
    CHECK_EQ(spans.size(), 2u);   // 2 deaths, the owed hit paid by the first (4783 C2; 3 with the 4775 rule)
    for (size_t i = 1; i < spans.size(); ++i) CHECK(spans[i].first + 1e-9 >= spans[i - 1].first + 0.4889);
}

namespace {
// 5057: a VP knight's shape (RunCharge, no AttackCharge, RushAttackFormation, ShortMoveRadius 24) and placed units
CombatMemberArt rider(const std::string& name, int n) {
    CombatMemberArt m = soldier(name, n);
    m.enable.erase("AttackCharge");
    m.rush = true;
    m.motion.short_move_radius = 24.0f;
    return m;
}
CombatUnitFigures placed(int32_t player, int32_t id, CombatMemberArt m, const std::vector<std::pair<int, int>>& at) {
    m.count = static_cast<int>(at.size());
    CombatUnitFigures u = unit(player, id, {m});
    for (auto& p : at) {
        u.x.push_back(static_cast<float>(p.first));
        u.y.push_back(static_cast<float>(p.second));
    }
    return u;
}
// three riders at x 0 heading +x; eight spearmen in two columns at x 100 and x 140 (the second column in line behind)
CombatUnitFigures riders() { return placed(0, 1, rider("K", 3), {{0, 0}, {0, 12}, {0, 24}}); }
CombatUnitFigures spearmen(const CombatMemberArt& m) {
    return placed(63, 2, m, {{100, 0}, {100, 8}, {100, 16}, {100, 24}, {140, 0}, {140, 8}, {140, 16}, {140, 24}});
}
// the end of the figure's 1120 move that ends at `t` (a landing at the arrival), -1 when none
double moveEndAt(const CombatResult& r, int32_t player, uint16_t figure, double t) {
    for (auto& m : r.moves)
        if (m.player == player && m.figure == figure && (m.code == 1120 || m.code == 1450) && std::fabs(m.t1 - t) <= kH + 1e-6)
            return m.t1;
    return -1;
}
double firstHitOf(const CombatResult& r, int32_t player, int figure) {
    double t = -1;
    for (auto& h : r.hits)
        if (h.player == player && !h.city && (figure < 0 || h.figure == figure) && (t < 0 || h.time < t)) t = h.time;
    return t;
}
double firstStrikeOf(const CombatResult& r, int32_t player) {
    for (auto& s : r.steps)
        if (s.player == player && s.state == FigState::Strike) return s.time;
    return -1;
}
}  // namespace

// 5057 (A) (CombatParams::rush_ride_through, unit-combat-server.md 18.29, 18.29.1): stock's knights kill each Spearman
// figure as a rider reaches it in the run charge and ride on, with no run stop and no combat-ready entry before their hits
// [L 18.29]. On (the default): every hit of a rider before it ever strikes is a landing at the arrival of one of its 1120
// moves (code 1120, no attack clip), no rider requests 1600 before its first hit, and a rider whose next target lies in
// line rides on in the same 1120 (a Charge step with no request) to land again. Off (the rule before 5057): no landing at
// an arrival; every rider strike (1100) follows its combat-ready entry (1600).
C5R_TEST(a_rush_rider_lands_at_its_arrival_and_rides_through) {
    CombatParams p;
    CHECK(p.rush_ride_through);   // the default (MC #11162)
    p.ride_strikes_while_held = false;   // 5057's rule alone (5071's strikes from the run: the next test)
    size_t landings = 0, rideOns = 0, rideOnLandings = 0;
    for (uint64_t seed = 1; seed <= 8; ++seed) {
        p.seed = seed;
        Clips clips;
        CombatResult r = choreograph(melee(10, 60), riders(), spearmen(soldier("S", 8)), clips, p);
        CHECK(r.status.ok() && !r.at_limit);
        CHECK(r.defender.planned > 0 && r.defender.died == r.defender.planned);
        for (auto& h : r.hits) {
            if (h.player != 0 || h.code != 1120) continue;
            ++landings;
            CHECK(h.time == h.launch && h.trigger == -1);
            if (moveEndAt(r, 0, h.figure, h.time) < 0)
                c5r::test::fail(__FILE__, __LINE__, "seed " + std::to_string(seed) + ": a 1120 hit of figure " +
                                                        std::to_string(h.figure) + " at " + std::to_string(h.time) +
                                                        " is not at the end of one of its moves");
        }
        for (uint16_t fig = 0; fig < 3; ++fig) {
            const double first = firstHitOf(r, 0, fig);
            for (auto& s : r.steps)
                if (s.player == 0 && s.figure == fig && s.code == 1600) CHECK(first >= 0 && s.time >= first);
        }
        for (auto& s : r.steps)
            if (s.player == 0 && s.state == FigState::Charge && s.code == 0) {
                ++rideOns;
                for (auto& h : r.hits)   // the ride-on's landing: at the end of the move it starts
                    if (h.player == 0 && h.figure == s.figure && h.code == 1120 && h.time > s.time + 1e-9) {
                        double end = -1;
                        for (auto& m : r.moves)
                            if (m.player == 0 && m.figure == s.figure && m.t0 == s.time) end = m.t1;
                        CHECK(end > s.time);
                        if (std::fabs(h.time - end) <= kH + 1e-6) ++rideOnLandings;
                        break;
                    }
            }
    }
    CHECK(landings > 0);
    CHECK(rideOns > 0);
    CHECK_EQ(rideOnLandings, rideOns);
    // off: today's route S
    CombatParams off;
    off.rush_ride_through = false;
    for (uint64_t seed = 1; seed <= 4; ++seed) {
        off.seed = seed;
        Clips clips;
        CombatResult q = choreograph(melee(10, 60), riders(), spearmen(soldier("S", 8)), clips, off);
        CHECK(q.status.ok());
        for (auto& h : q.hits) CHECK(!(h.player == 0 && h.code == 1120));
        for (auto& s : q.steps)
            if (s.player == 0 && s.state == FigState::Strike) {
                CHECK_EQ(s.code, 1100);
                bool readied = false;
                for (auto& x : q.steps)
                    if (x.player == 0 && x.figure == s.figure && x.code == 1600 && x.time <= s.time) readied = true;
                CHECK(readied);
            }
        for (auto& s : q.steps) CHECK(!(s.player == 0 && s.state == FigState::Charge && s.code == 0));
    }
    // a member with AttackCharge (none of VP's rush members) keeps its 1140 from the charge with the knob on
    CombatMemberArt h = soldier("H", 3);
    h.rush = true;
    Clips c3;
    CombatResult k = choreograph(melee(10, 60), placed(0, 1, h, {{0, 0}, {0, 12}, {0, 24}}), spearmen(soldier("S", 8)), c3,
                                 CombatParams());
    CHECK(k.status.ok());
    for (auto& x : k.hits) CHECK(!(x.player == 0 && x.code == 1120));
}

// 5071 (CombatParams::ride_strikes_while_held, unit-combat-server.md 18.46): stock's riders strike again from their run
// after the ride (1140, the knight's swing and attack vox, with no run stop and no combat-ready entry), and the ride
// leaves the defender's last planned death to such a strike. On (the default): rider strikes in every seed, each a 1140
// straight after the figure's 1120 or a 1100 after its own 1140, no rider 1600 before its last strike, the defender's
// last death not a ride landing (1120), each side's deaths equal to the rule off and the fight longer by at most one 1140
// clip + 0.5 s. Off: no 1140 of a rider (5057's rule). A member without a 1140 clip keeps 5057's rule byte for byte.
C5R_TEST(riders_strike_from_the_run_after_the_ride) {
    CHECK(CombatParams().ride_strikes_while_held);   // the default (lead-0042 #14462)
    size_t s1140 = 0, s1100 = 0;
    for (uint64_t seed = 1; seed <= 8; ++seed) {
        CombatParams on, off;
        on.seed = off.seed = seed;
        off.ride_strikes_while_held = false;
        Clips c1, c2;
        CombatResult r = choreograph(melee(10, 60), riders(), spearmen(soldier("S", 8)), c1, on);
        CombatResult q = choreograph(melee(10, 60), riders(), spearmen(soldier("S", 8)), c2, off);
        CHECK(r.status.ok() && q.status.ok() && !r.at_limit && !q.at_limit);
        CHECK(r.defender.planned > 0 && r.defender.died == r.defender.planned);
        CHECK_EQ(r.defender.died, q.defender.died);
        CHECK_EQ(r.attacker.died, q.attacker.died);
        // longer by at most one strike from the run and its arrival (the riders finish the clip they started)
        if (!(r.end <= q.end + c1.t[1140].duration + 0.5 + 1e-9))
            c5r::test::fail(__FILE__, __LINE__, "seed " + std::to_string(seed) + ": end " + std::to_string(r.end) +
                                                    " against " + std::to_string(q.end) + " off\n" + dump(r) +
                                                    "\nOFF\n" + dump(q));
        size_t strikes = 0;
        double lastStrike = -1;
        for (size_t i = 0; i < r.steps.size(); ++i) {
            const FigureStep& s = r.steps[i];
            if (s.player != 0 || s.state != FigState::Strike) continue;
            ++strikes;
            lastStrike = std::max(lastStrike, s.time);
            int32_t prev = 0;
            for (size_t j = 0; j < i; ++j) {
                const FigureStep& x = r.steps[j];
                if (x.player == 0 && x.figure == s.figure && x.code != 0) prev = x.code;
            }
            if (s.code == 1140) {
                ++s1140;
                CHECK_EQ(prev, 1120);
            } else {
                ++s1100;
                CHECK_EQ(s.code, 1100);
                CHECK_EQ(prev, 1140);
            }
        }
        if (strikes == 0) c5r::test::fail(__FILE__, __LINE__, "seed " + std::to_string(seed) + ": no rider strike");
        for (auto& s : r.steps) CHECK(!(s.player == 0 && s.code == 1600 && s.time < lastStrike));
        const CombatHit* last = nullptr;
        for (auto& h : r.hits)
            if (h.target_player == 63 && h.result == HitResult::Death && (!last || h.time >= last->time)) last = &h;
        CHECK(last != nullptr && last->code != 1120);
        for (auto& h : q.hits) CHECK(!(h.player == 0 && h.code == 1140));
        for (auto& s : q.steps) CHECK(!(s.player == 0 && s.code == 1140));
    }
    CHECK(s1140 > 0);
    // no 1140 clip: 5057's rule, the same fight with the rule on and off
    CombatParams on, off;
    off.ride_strikes_while_held = false;
    Clips c3, c4;
    c3.missing.insert(1140);
    c4.missing.insert(1140);
    CombatResult a = choreograph(melee(10, 60), riders(), spearmen(soldier("S", 8)), c3, on);
    CombatResult b = choreograph(melee(10, 60), riders(), spearmen(soldier("S", 8)), c4, off);
    CHECK(a.status.ok() && b.status.ok());
    CHECK_EQ(dump(a), dump(b));
    (void)s1100;
}

// 5072 (CombatParams::type2_end_after_dying, unit-combat-server.md 18.48): stock ends no rider of a type 2 fight while a
// figure of the fight is dying, so the riders walk home after the last death and the fight ends at their arrival (their
// run stop at home comes after the end). On (the default): in every seed no rider is finished (Done or Victory) before
// the fight's last removal and no rider's home stop (Reface 1000 after its 1400 walk) comes before the end; the deaths
// of each side equal the rule off. Off: some seed finishes a rider while a figure is still dying (the rule before 5072).
// A foot attacker (no RushAttackFormation) and a fight with a ranged defender (type 1) are the same fight on and off.
C5R_TEST(type_2_riders_wait_for_the_last_dying_figure) {
    CHECK(CombatParams().type2_end_after_dying);   // the default (lead-0042 #15633)
    size_t earlyOff = 0, fights = 0;
    for (uint64_t seed = 1; seed <= 8; ++seed) {
        CombatParams on, off;
        on.seed = off.seed = seed;
        off.type2_end_after_dying = false;
        Clips c1, c2;
        CombatResult r = choreograph(melee(40, 60), riders(), spearmen(soldier("S", 8)), c1, on);
        CombatResult q = choreograph(melee(40, 60), riders(), spearmen(soldier("S", 8)), c2, off);
        CHECK(r.status.ok() && q.status.ok() && !r.at_limit && !q.at_limit);
        CHECK_EQ(r.defender.died, q.defender.died);
        CHECK_EQ(r.attacker.died, q.attacker.died);
        auto lastRemoved = [](const CombatResult& x) {
            double t = -1;
            for (auto& s : x.steps)
                if (s.state == FigState::Removed) t = std::max(t, s.time);
            return t;
        };
        auto firstRelease = [](const CombatResult& x) {
            double t = -1;
            for (auto& s : x.steps)
                if (s.player == 0 && (s.state == FigState::Done || s.state == FigState::Victory) && (t < 0 || s.time < t))
                    t = s.time;
            return t;
        };
        const double rem = lastRemoved(r);
        if (rem < 0) continue;   // nobody died: nothing to wait for
        ++fights;
        if (!(firstRelease(r) + 1e-9 >= rem))
            c5r::test::fail(__FILE__, __LINE__, "seed " + std::to_string(seed) + ": a rider finished at " +
                                                    std::to_string(firstRelease(r)) + " before the last removal " +
                                                    std::to_string(rem) + "\n" + dump(r));
        std::map<uint16_t, int32_t> prev;
        for (auto& s : r.steps) {
            if (s.player != 0) continue;
            if (s.state == FigState::Reface && s.code == 1000 && prev[s.figure] == 1400) CHECK(s.time + 1e-9 >= r.end);
            if (s.code != 0) prev[s.figure] = s.code;
        }
        const double qrem = lastRemoved(q);
        if (firstRelease(q) >= 0 && firstRelease(q) + 1e-9 < qrem) ++earlyOff;
    }
    CHECK(fights > 0);
    CHECK(earlyOff > 0);   // the rule before 5072 releases riders while a figure dies
    // a foot attacker in a type 2 fight and a type 1 fight (an archer defender): the same fight on and off
    CombatParams on, off;
    off.type2_end_after_dying = false;
    Clips c3, c4, c5, c6;
    CombatUnitFigures foot = placed(0, 1, soldier("F", 3), {{0, 0}, {0, 12}, {0, 24}});
    CombatResult a = choreograph(melee(40, 60), foot, spearmen(soldier("S", 8)), c3, on);
    CombatResult b = choreograph(melee(40, 60), foot, spearmen(soldier("S", 8)), c4, off);
    CHECK(a.status.ok() && b.status.ok());
    CHECK_EQ(dump(a), dump(b));
    CombatResult e = choreograph(melee(40, 60), riders(), spearmen(archer("A", 8)), c5, on);
    CombatResult g = choreograph(melee(40, 60), riders(), spearmen(archer("A", 8)), c6, off);
    CHECK(e.status.ok() && g.status.ok());
    CHECK_EQ(dump(e), dump(g));
}

// 5057 (B) (CombatParams::defender_waits_first_landing, 18.15 phase 3 [C], 18.29): in fight type 2 (no ranged member on
// either side) the defender strikes only from the attacker's first landing on. Shown with the ride-through off (the
// riders then stand in combat ready for its entry before their first strike, so today's defender strikes first). A
// defender with a projectile weapon (type 1 / 4) is not held: its fight is the same with the knob on and off.
C5R_TEST(a_type_2_defender_waits_for_the_first_landing) {
    CombatParams on;
    CHECK(on.defender_waits_first_landing);   // the default (MC #11162)
    on.rush_ride_through = false;
    // 5072 (W167): with the ride-through off and a 1 s run stop these fights last 10-11 s; type2_end_after_dying (on,
    // the default) lets the riders leave only at the last removal (10.47 s in seed 1), and their walk home met stock's
    // 12 s limit (dump of 2026-11-25). The test is about 5057 (B)'s order, not the limit: it runs with 15 s
    on.limit = 15.0;
    CombatParams off = on;
    off.defender_waits_first_landing = false;
    size_t earlierOff = 0;
    for (uint64_t seed = 1; seed <= 6; ++seed) {
        on.seed = off.seed = seed;
        Clips c1, c2;
        c1.t[1440] = c2.t[1440] = Clips::timing(1.0f, {});   // the riders' run stop before their combat-ready entry
        // the riders have a planned death (40 damage): the defender has a need to strike for
        CombatResult r = choreograph(melee(40, 60), riders(), spearmen(soldier("S", 8)), c1, on);
        CombatResult q = choreograph(melee(40, 60), riders(), spearmen(soldier("S", 8)), c2, off);
        CHECK(r.status.ok() && q.status.ok() && !r.at_limit);
        const double attFirst = firstHitOf(r, 0, -1), defStrike = firstStrikeOf(r, 63);
        CHECK(attFirst >= 0);
        CHECK(defStrike < 0 || defStrike + 1e-9 >= attFirst);   // on: nothing of the defender before the first landing
        const double qAtt = firstHitOf(q, 0, -1), qDef = firstHitOf(q, 63, -1);
        if (qDef >= 0 && (qAtt < 0 || qDef < qAtt)) ++earlierOff;   // off: the defender may land first (today)
    }
    CHECK(earlierOff > 0);
    // an infantry type 2 (soldier on soldier, unplaced, the charge on) keeps the order too
    {
        Clips c1;
        CombatResult r = choreograph(melee(40, 60), unit(0, 1, {soldier("A", 6)}), unit(63, 2, {soldier("D", 6)}), c1,
                                     CombatParams());
        CHECK(r.status.ok() && !r.at_limit);
        const double attFirst = firstHitOf(r, 0, -1), defStrike = firstStrikeOf(r, 63);
        CHECK(attFirst >= 0 && (defStrike < 0 || defStrike + 1e-9 >= attFirst));
    }
    // not type 2: an archer defender answers as before, the same with both knobs on and off
    CombatParams all, none;
    none.rush_ride_through = false;
    none.defender_waits_first_landing = false;
    Clips c3, c4;
    CombatResult a = choreograph(melee(10, 60), unit(0, 1, {soldier("A", 6)}), unit(63, 2, {archer("R", 6)}), c3, all);
    CombatResult b = choreograph(melee(10, 60), unit(0, 1, {soldier("A", 6)}), unit(63, 2, {archer("R", 6)}), c4, none);
    CHECK(a.status.ok() && b.status.ok());
    CHECK_EQ(dump(a), dump(b));
}

// 5059 (CombatParams::victory_needs_word, unit-combat-server.md 18.36): stock takes victory only for a member whose
// EnableActions carry the word Victory; no VP member carries it, so stock plays no victory in VP. On (the default): a
// kill by members without the word draws no victory at all, every survivor ends Done; with the word about three in four
// as before. Off (the rule before 5059): the machine's 1800 alone, about three in four without the word. The knob changes
// nothing for a member with the word: on and off give the same fight byte for byte, and off with or without the word too.
C5R_TEST(a_victory_needs_the_enable_word) {
    CHECK(CombatParams().victory_needs_word);   // the default (lead-0042 GO #11696)
    long doneOn = 0, wonOn = 0, wonWord = 0, wonOff = 0, endsOff = 0;
    for (uint64_t s = 0; s < 300; ++s) {
        CombatParams on, off;
        on.charge_any = off.charge_any = false;   // the attackers keep 1600 / 1000 as their last action (as the 3-in-4 test)
        on.seed = off.seed = s;
        off.victory_needs_word = false;
        Clips c1, c2, c3, c4;
        CombatResult a = choreograph(melee(3, 100), unit(0, 1, {soldier("W", 4)}), unit(63, 2, {soldier("W", 3)}), c1, on);
        CombatResult b = choreograph(melee(3, 100), unit(0, 1, {soldier("W", 4)}), unit(63, 2, {soldier("W", 3)}), c2, off);
        CombatResult w = choreograph(melee(3, 100), unit(0, 1, {withVictoryWord(soldier("W", 4))}),
                                     unit(63, 2, {withVictoryWord(soldier("W", 3))}), c3, on);
        CombatResult wo = choreograph(melee(3, 100), unit(0, 1, {withVictoryWord(soldier("W", 4))}),
                                      unit(63, 2, {withVictoryWord(soldier("W", 3))}), c4, off);
        CHECK(a.status.ok() && b.status.ok() && w.status.ok() && wo.status.ok());
        CHECK(a.defender.killed && b.defender.killed);
        CHECK_EQ(count(a, 0, FigState::Victory) + count(a, 63, FigState::Victory), 0u);
        doneOn += static_cast<long>(count(a, 0, FigState::Done));
        wonOn += static_cast<long>(count(a, 0, FigState::Victory));
        wonWord += static_cast<long>(count(w, 0, FigState::Victory));
        wonOff += static_cast<long>(count(b, 0, FigState::Victory));
        endsOff += static_cast<long>(count(b, 0, FigState::Victory) + count(b, 0, FigState::Done));
        // the knob changes nothing for a member with the word, and off is the same with or without the word
        CHECK_EQ(dump(w), dump(wo));
        CHECK_EQ(dump(b), dump(wo));
    }
    CHECK_EQ(wonOn, 0L);
    CHECK_EQ(doneOn, 1200L);   // 4 attackers x 300 combats, each ends once, none in victory
    CHECK_EQ(endsOff, 1200L);
    const double pp = 0.74, sd = 3.2905 * std::sqrt(pp * (1 - pp) / 1200.0);
    CHECK(std::fabs(wonWord / 1200.0 - pp) <= sd);
    CHECK(std::fabs(wonOff / 1200.0 - pp) <= sd);
    // an attacker that wins with charge_any on (the default): no victory without the word either
    for (uint64_t s = 0; s < 40; ++s) {
        CombatParams on;
        on.seed = s;
        Clips c;
        CombatResult r = choreograph(melee(3, 100), unit(0, 1, {soldier("W", 4)}), unit(63, 2, {soldier("W", 3)}), c, on);
        CHECK(r.status.ok());
        CHECK_EQ(count(r, 0, FigState::Victory) + count(r, 63, FigState::Victory), 0u);
    }
}

// 5067 S1 (CombatParams::type2_phase2, unit-combat-server.md 18.29.3): stock starts a type 2 fight's charge only after its
// phase 2, 0.665 s [L W91 n 22], the defender's figures in 0xF meanwhile. On: no attacking figure requests a movement
// (1120 / 1400 / 1450) before type2_phase2_s, every defender figure with Walk and a 1450 clip requests 1450 (Turn) at the
// start, nothing of the defender strikes before the phase's end, and the fight ends later. Off (the default until the
// MC's word): the attackers move at the first step, as before 5067; the phase's length is not read. A defender without a
// 1450 clip waits with no stance. Not type 2 (an archer defender): the same fight on and off.
C5R_TEST(a_type_2_fight_waits_for_stock_s_phase_2_before_the_charge) {
    CombatParams off;
    CHECK(!off.type2_phase2);   // off until the MC's word (lead-0042 #12649)
    CHECK_NEAR(off.type2_phase2_s, 0.665, 1e-9);
    CombatParams on = off;
    on.type2_phase2 = true;
    auto firstMove = [](const CombatResult& r, int32_t player) {
        for (auto& s : r.steps)
            if (s.player == player && (s.code == 1120 || s.code == 1400 || s.code == 1450)) return s.time;
        return -1.0;
    };
    auto stancesAt = [](const CombatResult& r, int32_t player, double t) {
        size_t n = 0;
        for (auto& s : r.steps)
            if (s.player == player && s.state == FigState::Turn && s.code == 1450 && s.time <= t + 1e-9) ++n;
        return n;
    };
    for (uint64_t seed = 1; seed <= 4; ++seed) {
        on.seed = off.seed = seed;
        Clips c1, c2;
        c1.t[1450] = c2.t[1450] = Clips::timing(0.5f, {});
        CombatResult r = choreograph(melee(10, 60), riders(), spearmen(soldier("S", 8)), c1, on);
        CombatResult q = choreograph(melee(10, 60), riders(), spearmen(soldier("S", 8)), c2, off);
        CHECK(r.status.ok() && q.status.ok() && !r.at_limit && !q.at_limit);
        const double mOn = firstMove(r, 0), mOff = firstMove(q, 0);
        CHECK(mOff >= 0 && mOff < kH);                                    // off: the riders charge at once
        CHECK(mOn + 1e-9 >= on.type2_phase2_s && mOn < on.type2_phase2_s + kH + 1e-9);   // on: at the phase's end
        CHECK_EQ(stancesAt(r, 63, 0.0), static_cast<size_t>(8));          // every Spearman figure in the stance at once
        CHECK_EQ(stancesAt(q, 63, 100.0), static_cast<size_t>(0));
        const double dStrike = firstStrikeOf(r, 63);
        CHECK(dStrike < 0 || dStrike + 1e-9 >= on.type2_phase2_s);
        CHECK(r.end > q.end);
        CHECK_EQ(r.defender.died, q.defender.died);
        CHECK_EQ(r.attacker.died, q.attacker.died);
    }
    // off: the phase's length is not read (the fight before 5067, byte for byte)
    {
        CombatParams off2 = off;
        off2.type2_phase2_s = 3.0;
        Clips c1, c2;
        c1.t[1450] = c2.t[1450] = Clips::timing(0.5f, {});
        CHECK_EQ(dump(choreograph(melee(10, 60), riders(), spearmen(soldier("S", 8)), c1, off)),
                 dump(choreograph(melee(10, 60), riders(), spearmen(soldier("S", 8)), c2, off2)));
    }
    // no 1450 clip: the defender waits through the phase with no stance; the riders still wait for its end
    {
        Clips c1;
        CombatResult r = choreograph(melee(10, 60), riders(), spearmen(soldier("S", 8)), c1, on);
        CHECK(r.status.ok());
        CHECK_EQ(stancesAt(r, 63, 100.0), static_cast<size_t>(0));
        CHECK(firstMove(r, 0) + 1e-9 >= on.type2_phase2_s);
    }
    // an infantry type 2 (soldier on soldier, unplaced) waits too
    {
        Clips c1;
        c1.t[1450] = Clips::timing(0.5f, {});
        CombatResult r = choreograph(melee(40, 60), unit(0, 1, {soldier("A", 6)}), unit(63, 2, {soldier("D", 6)}), c1, on);
        CHECK(r.status.ok() && !r.at_limit);
        CHECK(firstMove(r, 0) + 1e-9 >= on.type2_phase2_s);
    }
    // not type 2: an archer defender's fight is the same on and off
    Clips c3, c4;
    c3.t[1450] = c4.t[1450] = Clips::timing(0.5f, {});
    CombatResult a = choreograph(melee(10, 60), unit(0, 1, {soldier("A", 6)}), unit(63, 2, {archer("R", 6)}), c3, on);
    CombatResult b = choreograph(melee(10, 60), unit(0, 1, {soldier("A", 6)}), unit(63, 2, {archer("R", 6)}), c4, off);
    CHECK(a.status.ok() && b.status.ok());
    CHECK_EQ(dump(a), dump(b));
}

// 5061 (CombatParams::defender_step, unit-combat-server.md 18.45): stock's type 2 defender steps in at the attacker's first
// landing [L W91 per figure: 8-9 of 12 figures, each run ending 16.8 units from the nearest attacker figure]. On (the
// default): the defender's figures make approaches or charges, none before the attacker's first landing, each ending
// within the two AttackRadius values (8 + 8) of an attacker figure, figures that never strike among them, and those that
// moved walk home; the deaths are the knob's off arm's. Off (the rule before 5061): no defender approach or charge, and
// defender_step_slack is not read. A rush defender, a defender with a ranged member (not type 2) and an unplaced fight
// are the same on and off.
namespace {
CombatUnitFigures footAttackers() {
    return placed(0, 1, soldier("A", 6), {{0, 0}, {0, 8}, {0, 16}, {0, 24}, {0, 32}, {0, 40}});
}
CombatUnitFigures footDefenders(const CombatMemberArt& m) {
    return placed(63, 2, m, {{64, 0}, {64, 10}, {64, 20}, {64, 30}, {64, 40}, {84, 5}, {84, 15}, {84, 25}, {84, 35}});
}
// where each figure of `player` stands at t: the end of its last move started at or before t, else its start
void standAt(const CombatResult& r, const CombatUnitFigures& u, double t, std::vector<std::pair<double, double>>& at) {
    at.clear();
    for (size_t i = 0; i < u.x.size(); ++i) at.push_back(std::make_pair(u.x[i], u.y[i]));
    for (auto& m : r.moves)
        if (m.player == u.player && m.t0 <= t + 1e-9 && m.figure < at.size()) at[m.figure] = std::make_pair(m.x1, m.y1);
}
bool isStepMove(const CombatResult& r, const FigureMove& m) {
    for (auto& s : r.steps)
        if (s.player == m.player && s.figure == m.figure && std::fabs(s.time - m.t0) < 1e-9 &&
            (s.state == FigState::Approach || s.state == FigState::Charge))
            return true;
    return false;
}
}  // namespace

C5R_TEST(a_type_2_defender_steps_in_at_the_first_landing) {
    CombatParams on;
    CHECK(on.defender_step);   // on by default (lead-0042 #13806)
    CHECK_NEAR(on.defender_step_slack, 1.0, 1e-9);
    CombatParams off = on;
    off.defender_step = false;
    size_t movers = 0, nonStrikers = 0, walks = 0;
    for (uint64_t seed = 1; seed <= 4; ++seed) {
        on.seed = off.seed = seed;
        Clips c1, c2;
        const CombatUnitFigures A = footAttackers(), D = footDefenders(soldier("D", 9));
        CombatResult r = choreograph(melee(30, 60), A, D, c1, on);
        CombatResult q = choreograph(melee(30, 60), A, D, c2, off);
        CHECK(r.status.ok() && q.status.ok() && !r.at_limit && !q.at_limit);
        const double firstLanding = firstHitOf(r, 0, -1);
        CHECK(firstLanding > 0);
        std::set<uint16_t> moved, struck;
        for (auto& s : r.steps)
            if (s.player == 63 && s.state == FigState::Strike) struck.insert(s.figure);
        for (auto& m : r.moves) {
            if (m.player != 63 || !isStepMove(r, m)) continue;
            moved.insert(m.figure);
            CHECK(m.t0 + 1e-9 >= firstLanding);   // phase 4: not before the attacker's first landing
            std::vector<std::pair<double, double>> at;
            standAt(r, A, m.t0, at);
            double nearest = 1e30;
            for (auto& p : at) nearest = std::min(nearest, std::hypot(p.first - m.x1, p.second - m.y1));
            CHECK(nearest <= 16.0 + 1e-3);   // within the two AttackRadius values of an attacker figure
        }
        CHECK(!moved.empty());
        movers += moved.size();
        for (uint16_t f : moved)
            if (!struck.count(f)) ++nonStrikers;
        // the walk home takes the stepped figures back (4742's walk_home, on by default)
        for (auto& s : r.steps)
            if (s.player == 63 && s.state == FigState::Done && s.code == 1400 && moved.count(s.figure)) ++walks;
        // off: the defender never approaches or charges (it strikes from where it stands)
        for (auto& s : q.steps)
            CHECK(!(s.player == 63 && (s.state == FigState::Approach || s.state == FigState::Charge)));
        CHECK_EQ(r.defender.died, q.defender.died);
        CHECK_EQ(r.attacker.died, q.attacker.died);
    }
    CHECK(movers > 4);
    CHECK(nonStrikers > 0);   // (b): figures without a strike to hold step in too
    CHECK(walks > 0);
    // off: the slack is not read (the fight before 5061, byte for byte)
    {
        CombatParams off2 = off;
        off2.defender_step_slack = 9.0f;
        Clips c1, c2;
        CHECK_EQ(dump(choreograph(melee(30, 60), footAttackers(), footDefenders(soldier("D", 9)), c1, off)),
                 dump(choreograph(melee(30, 60), footAttackers(), footDefenders(soldier("D", 9)), c2, off2)));
    }
    // a rush defender, a defender with a ranged member (not type 2) and the fight without positions: on equals off
    {
        CombatMemberArt rush = soldier("D", 9);
        rush.rush = true;
        Clips c1, c2, c3, c4, c5, c6;
        CHECK_EQ(dump(choreograph(melee(30, 60), footAttackers(), footDefenders(rush), c1, on)),
                 dump(choreograph(melee(30, 60), footAttackers(), footDefenders(rush), c2, off)));
        CHECK_EQ(dump(choreograph(melee(30, 60), footAttackers(), footDefenders(archer("R", 9)), c3, on)),
                 dump(choreograph(melee(30, 60), footAttackers(), footDefenders(archer("R", 9)), c4, off)));
        CHECK_EQ(dump(choreograph(melee(30, 60), unit(0, 1, {soldier("A", 6)}), unit(63, 2, {soldier("D", 9)}), c5, on)),
                 dump(choreograph(melee(30, 60), unit(0, 1, {soldier("A", 6)}), unit(63, 2, {soldier("D", 9)}), c6, off)));
    }
}

C5R_TEST(frames_round_trip_through_the_file_and_replay_through_the_controller) {
    Clips clips;
    CombatParams p;
    p.seed = 3;
    CombatResult r = choreograph(melee(19, 38), unit(0, 1, {soldier("M", 5)}), unit(63, 2, {soldier("M", 5)}), clips, p);
    CHECK(r.status.ok());
    std::vector<TimelineFrame> frames = combatFrames(r, kH, 100);
    CHECK(frames.size() >= static_cast<size_t>(r.end / kH));
    size_t n = 0;
    for (size_t i = 0; i < frames.size(); ++i) {
        CHECK_EQ(frames[i].seq, 100 + i);
        for (auto& e : frames[i].events) {
            (void)e;
            ++n;
        }
    }
    CHECK_EQ(n, r.events.size());
    CHECK(combatFrames(r, 0, 1).empty());   // a bad dt gives nothing
    // the timeline file: written and read back equal
    c5r::units::UnitScene sc;
    for (int32_t pl : {0, 63}) {
        c5r::units::PlayerInput pi;
        pi.player = pl;
        pi.colour = "PLAYERCOLOR_ROME";
        pi.art_style = "ARTSTYLE_EUROPEAN";
        pi.era = "ERA_ANCIENT";
        sc.players.push_back(pi);
    }
    sc.units.push_back(Rig::unit(0, 1));
    sc.units.push_back(Rig::unit(63, 2));
    Timeline tl;
    tl.seed = 3;
    tl.frames = frames;
    std::string text;
    TimelineStatus st = writeTimeline(tl, &sc, text);
    CHECK(st.ok());
    Timeline back;
    CHECK(parseTimeline(text, &sc, back).ok());
    CHECK_EQ(back.frames.size(), frames.size());
    std::string again;
    CHECK(writeTimeline(back, &sc, again).ok());
    CHECK_EQ(again, text);
    // replay through the controller with the ActionBridge: every unit stays Ok, the killed figures are dead
    Rig rig;
    BridgeTables t;
    t.words["M"] = words(kSoldier);
    ActionBridge bridge(t);
    rig.opt.bridge = &bridge;
    rig.open(5);
    rig.c.start(sc);
    for (auto& f : back.frames) {
        CHECK_ANIM(rig.c.apply(f), AnimCode::Ok);
        for (auto& s : rig.c.statuses()) CHECK_ANIM(s.status, AnimCode::Ok);
    }
    CHECK(rig.prog(0, 1, 0) != nullptr);
    CHECK(rig.prog(63, 2, 4) != nullptr);   // a killed figure keeps its program (the death clip) until remove
}

C5R_TEST_MAIN
