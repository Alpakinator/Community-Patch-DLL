// render/tests/unitanim/test_combat_special.cpp - issue 4777: the special fights of the combat choreography core
// (combat.h; unit-combat-server.md "Special cases (4777)"): a city as the attacker (its strikes from its centre), the
// aircraft's flight out and back, 0321 13.4's air opening rule with 0477's submarine fix and stock's submarine loop, the
// answer of a ship (1130) and of a land figure (1180) to an air side, an interception (participant 2), a naval duel (ships
// strike from where they stand), a nuclear strike (the plot target, the area members, the effect wait, the Atomic
// Bomb's run to the 20 s limit), a garrison as an area member, and every former refusal of 4775 played; 4735's turn and
// 4745 + 4747's reform before a fight. Synthetic data
// only (no game files; the names are test strings).
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <sstream>
#include <string>

#include "anim_testkit.h"
#include "c5r/unitanim/combat.h"
#include "c5r/unitanim/motion.h"   // 4735: yawFacing, FightMotion

using namespace anim_test;

namespace {

// 5050 W74 (unit-combat-server.md 18.23.2): reform_ends_at_arrival and attack_from_stance are on by default; a case
// written before 5050 that measures another rule (4735's turn, 4747's reform, 4748's city melee) runs with both off, as
// it did, and checks the default beside it where the two meet
CombatParams before5050() {
    CombatParams p;
    p.reform_ends_at_arrival = false;
    p.attack_from_stance = false;
    return p;
}
bool sameResult(const CombatResult& a, const CombatResult& b);   // below (4745's cases)

std::set<std::string> words(const std::string& s) {
    std::set<std::string> w;
    std::istringstream in(s);
    std::string x;
    while (in >> x) w.insert(x);
    return w;
}
const char* kSoldier = "Idle Attack RunCharge AttackCity Bombard Death BombardDefend Run Fortify CombatReady Walk "
                       "AttackCharge";
const char* kPlane = "Idle Attack Bombard Death Run";
const char* kShip = "Idle Attack RunCharge AttackCity Bombard Death BombardDefend Run Fortify CombatReady "
                    "AttackSurfaceToAir";

CombatMemberArt soldier(const std::string& name, int n) {
    CombatMemberArt m;
    m.member = name;
    m.count = n;
    m.domain = "Land";
    m.enable = words(kSoldier);
    m.victory = true;
    CombatWeapon blade;
    blade.sound = "METAL";
    m.weapons.push_back(blade);
    return m;
}
CombatMemberArt rifle(const std::string& name, int n) {   // a ranged member (can answer, 0321 13.3)
    CombatMemberArt m = soldier(name, n);
    m.weapons[0].projectile = true;
    m.weapons[0].sound = "BULLET";
    return m;
}
CombatMemberArt bomber(const std::string& name, int n) {
    CombatMemberArt m;
    m.member = name;
    m.count = n;
    m.domain = "Air";
    m.enable = words(kPlane);
    m.motion.move_rate = 5.4f;   // 345.6 world units per second
    m.motion.altitude = 90.0f;
    CombatWeapon gun;            // Index 0: the guns
    gun.effect = "TEST_GUN_HIT";
    gun.sound = "GUN";
    m.weapons.push_back(gun);
    CombatWeapon bomb;           // Index 1: the dropped bomb
    bomb.projectile = true;
    bomb.dropped = true;
    bomb.target_ground = true;
    bomb.speed = 0.9f;           // 57.6 world units per second: 90 / 57.6 = 1.5625 s of fall
    bomb.sound = "BOMB";
    m.weapons.push_back(bomb);
    return m;
}
CombatMemberArt ship(const std::string& name, int anti_air) {
    CombatMemberArt m;
    m.member = name;
    m.count = 1;
    m.domain = "Sea";
    m.enable = words(kShip);
    m.anti_air = anti_air;
    CombatWeapon gun;
    gun.projectile = true;
    gun.effect = "TEST_SHELL_IMPACT";
    m.weapons.push_back(gun);
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
// every standing figure at (x + 4 i, y), the root at (x, y)
CombatUnitFigures placed(CombatUnitFigures u, float x, float y) {
    u.x.clear();
    u.y.clear();
    for (size_t i = 0; i < u.standing.size(); ++i) {
        u.x.push_back(x + 4.0f * static_cast<float>(i));
        u.y.push_back(y);
    }
    u.has_centre = true;
    u.centre_x = x;
    u.centre_y = y;
    return u;
}
CombatUnitFigures centre(float x, float y) {
    CombatUnitFigures u;
    u.has_centre = true;
    u.centre_x = x;
    u.centre_y = y;
    return u;
}

class Clips : public CombatClips {
public:
    std::map<int32_t, ActionTiming> t;
    std::map<std::string, std::set<int32_t>> missing;   // member -> codes it binds no clip to
    double effect = -1;
    Clips() {
        t[1100] = timing(1.0f, {0.4889f});
        t[1130] = timing(1.2f, {0.5f});
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
        ActionTiming a;
        auto it = missing.find(m.member);
        if ((it != missing.end() && it->second.count(code)) || !t.count(code)) {
            a.why = m.member + " code " + std::to_string(code) + ": not bound (test)";
            return a;
        }
        return t[code];
    }
    double effectDuration(const std::string&) override { return effect; }
};

CombatInput record(int32_t afinal, int32_t dfinal, uint16_t flags) {
    CombatInput in;
    in.seq = 77;
    in.kind = kCombatKindUnit;
    in.flags = static_cast<uint16_t>(kCombatVisualize | kCombatGameFlagsRead | flags);
    in.attacker.owner = 0;
    in.attacker.unit = 1;
    in.attacker.final_damage = afinal;
    in.defender.owner = 63;
    in.defender.unit = 2;
    in.defender.final_damage = dfinal;
    return in;
}

size_t countSteps(const CombatResult& r, int32_t player, FigState s, int32_t code = -1) {
    size_t n = 0;
    for (auto& x : r.steps)
        if (x.player == player && x.state == s && (code < 0 || x.code == code)) ++n;
    return n;
}
}  // namespace

C5R_TEST(every_former_refusal_is_played) {
    Clips clips;
    const CombatParams p;
    auto A = unit(0, 1, {soldier("W", 5)});
    auto D = unit(63, 2, {soldier("W", 5)});
    auto played = [&](const CombatInput& in, const CombatUnitFigures& a, const CombatUnitFigures& d) {
        CombatResult r = choreograph(in, a, d, clips, p);
        if (!r.status.ok())
            c5r::test::fail(__FILE__, __LINE__, std::string("refused: ") + combatCodeName(r.status.code) + ": " +
                                                    r.status.text);
        CHECK(!r.steps.empty());
        return r;
    };
    CombatInput in = record(0, 40, kCombatRanged);   // CityAttacker
    in.kind = kCombatKindCity;
    in.attacker.unit = -1;
    in.attacker.city = 3;
    CombatResult r = played(in, CombatUnitFigures(), D);
    CHECK(!r.at_limit);
    in = record(0, 40, kCombatDefenderRetaliates);   // Nuclear (a unit defender)
    in.nuclear_level = 2;
    r = played(in, A, D);
    in = record(0, 40, kCombatDefenderRetaliates | kCombatBombing);   // a bombing run, then an air sweep
    played(in, A, D);
    in = record(0, 40, kCombatDefenderRetaliates | kCombatAirSweep);
    played(in, A, D);
    r = played(record(0, 40, kCombatDefenderRetaliates), unit(0, 1, {bomber("B", 2)}), D);   // an air attacker
    CHECK_NEAR(r.limit, 20.0, 1e-9);
    r = played(record(0, 40, kCombatDefenderRetaliates), A, unit(63, 2, {bomber("B", 2)}));   // an air defender
    CHECK_NEAR(r.limit, 20.0, 1e-9);
    in = record(0, 40, kCombatDefenderRetaliates);   // AreaMembers: one member without figures
    in.members.push_back(CombatMemberIn());
    in.members[0].player = 5;
    in.members[0].id = 9;
    in.members[0].max_hp = 100;
    r = played(in, A, D);
    CHECK_EQ(r.others.size(), 1u);
    CHECK(!r.others[0].figures);
    CHECK(!r.notes.empty() && r.notes[0].text.find("Members: 1 of 1") == 0);
    // still refused: a defender naming neither unit nor city outside a nuclear strike; a bad 4777 parameter; > 64 members
    in = record(0, 40, 0);
    in.defender.unit = -1;
    CHECK(choreograph(in, A, D, clips, p).status.code == CombatCode::BadInput);
    CombatParams q;
    q.effect_wait = -1;
    CHECK(choreograph(record(0, 40, 0), A, D, clips, q).status.code == CombatCode::BadInput);
    q = CombatParams();
    q.air_flight = std::nan("");
    CHECK(choreograph(record(0, 40, 0), A, D, clips, q).status.code == CombatCode::BadInput);
    in = record(0, 40, 0);
    in.members.resize(65);
    CHECK(choreograph(in, A, D, clips, p).status.code == CombatCode::BadInput);
}

C5R_TEST(a_city_attacker_strikes_from_its_centre) {
    Clips clips;
    CombatParams p;
    p.city_effect = "TEST_CITY_IMPACT";
    p.city_sound = "TEST_CITY_SHOT";
    CombatInput in = record(0, 60, kCombatRanged);   // the DLL's city strike: ranged, no retaliation
    in.kind = kCombatKindCity;
    in.attacker.unit = -1;
    in.attacker.city = 3;
    in.attacker.owner = 0;
    in.attacker.max_hp = 300;
    // 6 figures, 60 of 100: round(2.4) = 2 survive... livingFigures decides; the planned deaths and the owed hit
    CombatResult r = choreograph(in, centre(0, 0), placed(unit(63, 2, {soldier("S", 6)}), 128, 0), clips, p);
    CHECK(r.status.ok());
    CHECK(!r.at_limit);
    CHECK(r.attacker.city && !r.attacker.figures);
    CHECK(r.defender.planned > 0);
    CHECK_EQ(r.defender.died, r.defender.planned);
    CHECK_EQ(r.defender.paid, 1u);
    // the volley: one shot per need, from the city's centre, the first at city_strike_at + city_flight
    size_t shots = 0;
    double first = 1e9;
    for (auto& h : r.hits) {
        CHECK(h.city && h.code == 0 && h.figure == 0 && h.player == 0 && h.id == -1);
        CHECK(h.has_origin && h.origin[0] == 0 && h.origin[1] == 0);
        CHECK(h.has_pos && h.pos[0] >= 128);
        CHECK(h.effect == "TEST_CITY_IMPACT" && h.sound == "TEST_CITY_SHOT");
        first = std::min(first, h.time);
        ++shots;
    }
    CHECK_EQ(shots, static_cast<size_t>(r.defender.planned + 1));
    CHECK_NEAR(first, p.city_strike_at + p.city_flight, 1.0 / 30.0 + 1e-9);
    // the defender does not answer: 1280 bombard defend, no strike
    CHECK_EQ(countSteps(r, 63, FigState::Strike), 0u);
    CHECK(countSteps(r, 63, FigState::Defend, 1280) > 0);
    // a city defender's return fire now leaves the city's centre too (4775's rule, 4777's origin)
    CombatInput m = record(12, 70, kCombatDefenderRetaliates);
    m.kind = kCombatKindCity;
    m.defender.unit = -1;
    m.defender.city = 7;
    m.defender.max_hp = 258;
    CombatResult c = choreograph(m, placed(unit(0, 1, {soldier("W", 5)}), 0, 0), centre(128, 0), clips, p);
    CHECK(c.status.ok() && !c.at_limit);
    bool fromCity = false;
    for (auto& h : c.hits)
        if (h.city) fromCity = h.has_origin && h.origin[0] == 128 && h.result == HitResult::Death;
    CHECK(fromCity);
}

C5R_TEST(an_aircraft_flies_out_strikes_and_flies_home) {
    Clips clips;
    CombatParams p;
    CombatInput in = record(0, 40, kCombatDefenderRetaliates | kCombatBombing);
    in.attacker.damage = 0;   // no answer from the ground (13.4's opening rule)
    auto B = placed(unit(0, 1, {bomber("B", 1)}), 0, 0);
    auto D = placed(unit(63, 2, {soldier("W", 5)}), 256, 0);
    CombatResult r = choreograph(in, B, D, clips, p);
    CHECK(r.status.ok());
    CHECK(!r.at_limit);
    CHECK_NEAR(r.limit, 20.0, 1e-9);
    // out: one air move at the altitude, back: one air move down to the base
    const FigureMove* out = nullptr;
    const FigureMove* back = nullptr;
    for (auto& mv : r.moves) {
        if (mv.player != 0) continue;
        CHECK(mv.air);
        if (mv.back) back = &mv;
        else if (!out) out = &mv;
    }
    CHECK(out != nullptr && back != nullptr);
    if (out && back) {
        CHECK_NEAR(out->t0, 0.0, 1e-9);
        CHECK_NEAR(out->z1, 90.0, 1e-6);
        CHECK_NEAR(out->x0, 0.0, 1e-6);
        // to within MoveRadius (3.5) of the target figure at 345.6 units per second
        const double d = std::sqrt((out->x1 - out->x0) * (out->x1 - out->x0) + (out->y1 - out->y0) * (out->y1 - out->y0));
        CHECK_NEAR(out->t1 - out->t0, d / (5.4 * 64.0), 1e-6);
        CHECK(back->t0 > out->t1);
        CHECK_NEAR(back->x1, 0.0, 1e-6);
        CHECK_NEAR(back->z1, 0.0, 1e-6);
        CHECK(r.end + 1.0 / 30.0 + 1e-9 >= back->t1);   // the fight ends when it is home (or its last death is played)
        CHECK(r.end < back->t1 + 2.5 + 1e-9);
    }
    // the strike: 1180 bombard over the ground with the dropped bomb, which falls 90 / (0.9 x 64) s and lands below it
    CHECK(countSteps(r, 0, FigState::Strike, 1180) > 0);
    bool bomb = false;
    for (auto& h : r.hits)
        if (h.player == 0) {
            CHECK(h.projectile && h.sound == "BOMB");
            CHECK_NEAR(h.time - h.launch, 90.0 / (0.9 * 64.0), 1e-6);
            CHECK(h.has_origin && h.has_pos && h.pos[0] == h.origin[0] && h.origin[2] == 90.0f);
            bomb = true;
        }
    CHECK(bomb);
    CHECK_EQ(r.defender.died, r.defender.planned);
    // no victory pose for an aircraft; the defender bombard-defends (damage 0: 13.4)
    CHECK_EQ(countSteps(r, 0, FigState::Victory), 0u);
    CHECK_EQ(countSteps(r, 63, FigState::Strike), 0u);
    // without positions: out and back take air_flight each, no moves
    CombatResult n = choreograph(in, unit(0, 1, {bomber("B", 1)}), unit(63, 2, {soldier("W", 5)}), clips, p);
    CHECK(n.status.ok() && !n.at_limit && n.moves.empty());
    double outEnd = -1, backStart = -1;
    for (auto& s : n.steps)
        if (s.player == 0 && s.state == FigState::Approach) {
            if (outEnd < 0) outEnd = s.time + p.air_flight;
            else backStart = s.time;
        }
    CHECK(outEnd > 0 && backStart > outEnd);
    // the fight ends when the bomber is home or the last dying figure is removed, whichever is later (4783 C2: the owed
    // hit goes with the first death, so the bomber flies home one strike earlier and the last removal can come after it)
    double lastRemoved = 0;
    for (auto& s : n.steps)
        if (s.state == FigState::Removed) lastRemoved = std::max(lastRemoved, s.time);
    CHECK_NEAR(n.end, std::max(backStart + p.air_flight, lastRemoved), 1.0 / 30.0 + 1e-9);
}

C5R_TEST(the_air_opening_rule_0477s_fix_and_the_answer_to_an_air_side) {
    Clips clips;
    CombatParams p;
    auto B = [] { return unit(0, 1, {bomber("B", 1)}); };
    // stock: a bomber on a submarine whose machine takes no 1130, the attacker damaged (5) and retaliation set: the
    // submarine must answer and cannot: the fight holds to the 20 s limit (0321 12.2, 13.4-13.5 [L 0456, 0477])
    CombatInput in = record(5, 31, kCombatDefenderRetaliates | kCombatBombing);
    in.attacker.damage = 5;
    CombatResult stock = choreograph(in, B(), unit(63, 2, {ship("SUB", 0)}), clips, p);
    CHECK(stock.status.ok());
    CHECK(stock.at_limit);
    CHECK_NEAR(stock.end, 20.0, 1e-9);
    CHECK_EQ(stock.attacker.owed, 1u);
    CHECK_EQ(stock.attacker.paid, 0u);
    bool noAA = false;
    for (auto& n : stock.notes) noAA = noAA || n.text.find("NoAntiAir") == 0;
    CHECK(noAA);
    CHECK(countSteps(stock, 63, FigState::Strike, 1130) > 0);
    // 0477's fix: the record carries the attacker's damage in this combat as 0 and no retaliation: the submarine
    // bombard-defends, the attacker owes nothing, the fight ends by itself (stock with the fix: 1.87-3.87 s [L 0477])
    in.flags = static_cast<uint16_t>(kCombatVisualize | kCombatGameFlagsRead | kCombatBombing);
    in.attacker.damage = 0;
    CombatResult fixed = choreograph(in, B(), unit(63, 2, {ship("SUB", 0)}), clips, p);
    CHECK(fixed.status.ok() && !fixed.at_limit);
    CHECK(fixed.end < 6.0);
    CHECK_EQ(fixed.attacker.owed, 0u);
    CHECK(countSteps(fixed, 63, FigState::Defend, 1280) > 0);
    CHECK_EQ(countSteps(fixed, 63, FigState::Strike), 0u);
    // a ship without a 1130 clip whose machine is not known: it requests 1130 and plays its attack (13.5: a frigate's
    // 3.4 s); the attacker's owed hit is paid
    in = record(5, 31, kCombatDefenderRetaliates | kCombatBombing);
    in.attacker.damage = 5;
    Clips noClip;
    noClip.missing["FRIGATE"] = {1130};
    CombatResult frigate = choreograph(in, B(), unit(63, 2, {ship("FRIGATE", -1)}), noClip, p);
    CHECK(frigate.status.ok() && !frigate.at_limit);
    CHECK(countSteps(frigate, 63, FigState::Strike, 1130) > 0);
    CHECK_EQ(frigate.attacker.paid, 1u);
    // a land figure answers with 1180; with the damage not known (-1) the retaliate byte decides
    in.attacker.damage = -1;
    CombatResult land = choreograph(in, B(), unit(63, 2, {rifle("INF", 4)}), clips, p);
    CHECK(land.status.ok() && !land.at_limit);
    CHECK(countSteps(land, 63, FigState::Strike, 1180) > 0);
    CHECK_EQ(land.attacker.paid, 1u);
    // the defenders fire only at an aircraft that has arrived over them
    double arrive = 1e9, firstAnswer = 1e9;
    for (auto& s : land.steps) {
        if (s.player == 0 && s.state == FigState::Ready) arrive = std::min(arrive, s.time);
        if (s.player == 63 && s.state == FigState::Strike) firstAnswer = std::min(firstAnswer, s.time);
    }
    CHECK(firstAnswer + 1e-9 >= arrive);
}

C5R_TEST(an_interception_is_participant_2) {
    Clips clips;
    CombatParams p;
    // a bomber (2 figures, killed by the interception: 100 of 100) intercepted by a fighter (3 figures, unhurt)
    CombatInput in = record(100, 0, kCombatBombing);
    in.attacker.damage = 0;
    in.interceptor.owner = 63;
    in.interceptor.unit = 5;
    in.interceptor.damage = 100;
    auto B = placed(unit(0, 1, {bomber("B", 2)}), 0, 0);
    auto D = placed(unit(63, 2, {soldier("W", 5)}), 256, 0);
    auto F = placed(unit(63, 5, {bomber("F", 3)}), 300, 64);
    CombatResult r = choreograph(in, B, D, {F}, clips, p);
    CHECK(r.status.ok());
    CHECK(!r.at_limit);
    CHECK_EQ(r.others.size(), 1u);
    CHECK(r.others[0].figures && r.others[0].player == 63 && r.others[0].id == 5);
    // the bomber's two figures die by the fighter's guns (1100: air against air), and the defender owes nothing (the
    // attacker is killed and there is an interceptor, 13.4)
    CHECK_EQ(r.attacker.planned, 2u);
    CHECK_EQ(r.attacker.died, 2u);
    CHECK_EQ(r.defender.owed, 0u);
    CHECK(countSteps(r, 63, FigState::Strike, 1100) > 0);
    for (auto& h : r.hits)
        if (h.result == HitResult::Death && h.target_player == 0) {
            CHECK(h.player == 63 && h.id == 5);
            CHECK(h.effect == "TEST_GUN_HIT" && h.sound == "GUN");
        }
    // the fighter flies out and home
    size_t out = 0, back = 0;
    for (auto& mv : r.moves)
        if (mv.player == 63 && mv.id == 5) (mv.back ? back : out)++;
    CHECK(out > 0 && back > 0);
    // an aborted interception (the attacker's final damage 0, the interceptor's damage > 0, VP's GenerateAirCombatInfo):
    // the attacker owes the interception one hit [I], the defender one
    in.attacker.final_damage = 0;
    CombatResult a = choreograph(in, B, D, {F}, clips, p);
    CHECK(a.status.ok() && !a.at_limit);
    CHECK_EQ(a.attacker.owed, 1u);
    CHECK_EQ(a.attacker.paid, 1u);
    CHECK_EQ(a.defender.owed, 1u);
    // the interceptor's figures not given: it takes part without figures, named in a note
    CombatResult g = choreograph(in, B, D, clips, p);
    CHECK(g.status.ok());
    CHECK(!g.others[0].figures);
    bool named = false;
    for (auto& n : g.notes) named = named || n.text.find("Members: the interceptor") == 0;
    CHECK(named);
}

C5R_TEST(a_naval_duel_approaches_or_strikes_from_where_the_ships_stand) {
    Clips clips;
    CombatParams p;
    CombatInput in = record(17, 51, kCombatDefenderRetaliates);
    auto A = placed(unit(0, 1, {ship("IRONCLAD", -1)}), 0, 0);
    auto D = placed(unit(63, 2, {ship("FRIGATE", -1)}), 64, 0);
    // the default (ships_approach): 4776's land rule, the ship moves to its target first
    CombatResult m = choreograph(in, A, D, clips, p);
    CHECK(m.status.ok() && !m.at_limit);
    CHECK(!m.moves.empty());
    CHECK(!m.notes.empty() && m.notes[0].text.find("Naval") == 0);
    // off: the ship strikes from where it stands: no move, shorter
    p.ships_approach = false;
    CombatResult r = choreograph(in, A, D, clips, p);
    CHECK(r.status.ok() && !r.at_limit);
    CHECK(r.moves.empty());
    CHECK(countSteps(r, 0, FigState::Strike) > 0);
    CHECK_EQ(countSteps(r, 0, FigState::Approach), 0u);
    CHECK(m.end > r.end);
}

C5R_TEST(a_nuke_is_an_area_strike_that_waits_for_its_effect) {
    CombatParams p;
    // a Nuclear Missile (a collision attacker: it dies at its own hit) on a plot with no defender; two damage members: a
    // unit with figures (killed) and a city
    CombatMemberArt missile = bomber("MISSILE", 1);
    missile.weapons.clear();
    CombatWeapon warhead;
    warhead.one_shot = true;   // the member's HasCollisionAttack (M6)
    warhead.effect = "TEST_NUCLEAR_BOMB";
    warhead.sound = "TEST_1TON";
    warhead.area_delay = 0.5f;
    warhead.wait_effect = true;
    missile.weapons.push_back(warhead);
    CombatInput in = record(0, 0, kCombatBombing);
    in.nuclear_level = 3;
    in.defender.unit = -1;
    in.defender.owner = -1;
    CombatMemberIn u;
    u.player = 63;
    u.id = 8;
    u.final_damage = 100;
    u.max_hp = 100;
    CombatMemberIn c;
    c.player = 63;
    c.id = 4;
    c.flags = 1;
    c.final_damage = 80;
    c.max_hp = 300;
    in.members = {u, c};
    auto M = placed(unit(0, 1, {missile}), 0, 0);
    auto V = placed(unit(63, 8, {soldier("V", 4)}), 260, 10);
    Clips clips;
    CombatResult r = choreograph(in, M, centre(256, 0), {V}, clips, p);
    CHECK(r.status.ok());
    CHECK(!r.at_limit);
    CHECK_NEAR(r.limit, 20.0, 1e-9);
    CHECK_EQ(r.others.size(), 2u);
    CHECK(r.others[0].figures && !r.others[1].figures && r.others[1].city);
    CHECK_EQ(r.others[0].died, 4u);
    // the blast: one hit on the plot with the effect and sound; the member's four deaths 0.5 s later, as area hits
    double blast = -1;
    for (auto& h : r.hits)
        if (!h.area && h.player == 0) {
            CHECK(h.effect == "TEST_NUCLEAR_BOMB" && h.sound == "TEST_1TON");
            CHECK(h.has_pos && h.pos[0] == 256 && h.target_figure == -1);
            blast = h.time;
        }
    CHECK(blast > 0);
    size_t area = 0;
    for (auto& h : r.hits)
        if (h.area) {
            CHECK_NEAR(h.time, blast + 0.5, 1.0 / 30.0 + 1e-9);
            CHECK(h.target_player == 63 && h.target_id == 8 && h.result == HitResult::Death);
            ++area;
        }
    CHECK_EQ(area, 4u);
    // the missile dies at its own hit; the fight waits effect_wait for the effect after the blast
    CHECK(countSteps(r, 0, FigState::Dying, 1200) == 1);
    CHECK_NEAR(r.end, blast + p.effect_wait, 1.0 / 30.0 + 1e-9);
    // the effect player names its length: that wins
    Clips named;
    named.effect = 2.0;
    CombatResult e = choreograph(in, M, centre(256, 0), {V}, named, p);
    CHECK(e.status.ok() && !e.at_limit);
    CHECK(e.end < blast + 0.5 + 2.5 + 2.0 / 30.0);   // then the member's deaths (0.5 s, a 2.5 s death clip) end it
    CHECK(e.end < r.end);
    // the Atomic Bomb: a dropped bomb with WaitForEffectCompletion and no HitEffect holds to the 20 s limit, as stock
    // ([L 0553] 6 of 6); with unnamed_effect_holds off it ends by itself
    CombatMemberArt atom = bomber("ATOM", 1);
    atom.weapons[1].wait_effect = true;
    atom.weapons[1].area_delay = 0.5f;
    atom.weapons[1].effect.clear();
    in.nuclear_level = 2;
    CombatResult b = choreograph(in, placed(unit(0, 1, {atom}), 0, 0), centre(256, 0), {V}, clips, p);
    CHECK(b.status.ok());
    CHECK(b.at_limit);
    CHECK_NEAR(b.end, 20.0, 1e-9);
    CHECK_EQ(b.others[0].died, 4u);   // the blast's deaths come long before the limit
    p.unnamed_effect_holds = false;
    CombatResult b2 = choreograph(in, placed(unit(0, 1, {atom}), 0, 0), centre(256, 0), {V}, clips, p);
    CHECK(b2.status.ok() && !b2.at_limit);
    CHECK(b2.end < 10.0);
}

C5R_TEST(a_garrison_is_an_area_member_of_a_city_attack) {
    Clips clips;
    CombatParams p;
    // a swordsman on a city with a garrison (0557 g1: the garrison took 17 of 100)
    CombatInput in = record(18, 24, kCombatDefenderRetaliates);
    in.kind = kCombatKindCity;
    in.defender.unit = -1;
    in.defender.city = 7;
    in.defender.max_hp = 258;
    CombatMemberIn g;
    g.player = 63;
    g.id = 9;
    g.final_damage = 60;
    g.max_hp = 100;
    in.members = {g};
    auto A = placed(unit(0, 1, {soldier("S", 5)}), 0, 0);
    auto G = placed(unit(63, 9, {soldier("G", 5)}), 120, 4);
    CombatResult r = choreograph(in, A, centre(128, 0), {G}, clips, p);
    CHECK(r.status.ok() && !r.at_limit);
    CHECK_EQ(r.others.size(), 1u);
    CHECK(r.others[0].planned > 0);
    CHECK_EQ(r.others[0].died, r.others[0].planned);
    // the garrison's deaths fall at the attacker's first hit on the city; its figures bombard-defend meanwhile
    double firstOnCity = 1e9, firstArea = 1e9;
    for (auto& h : r.hits) {
        if (!h.area && h.player == 0 && h.target_id == -1) firstOnCity = std::min(firstOnCity, h.time);
        if (h.area) firstArea = std::min(firstArea, h.time);
    }
    CHECK_NEAR(firstArea, firstOnCity, 1.0 / 30.0 + 1e-9);
    CHECK(countSteps(r, 63, FigState::Defend, 1280) > 0);
    CHECK_EQ(countSteps(r, 63, FigState::Strike), 0u);
    // deterministic
    CombatResult again = choreograph(in, A, centre(128, 0), {G}, clips, p);
    CHECK_EQ(again.steps.size(), r.steps.size());
    CHECK_NEAR(again.end, r.end, 1e-12);
}

namespace {
// the controller's live answers: a ship's machine that takes 1130 but plays its attack clip (13.5)
class ShipFeedback : public CombatFeedback {
public:
    bool accepts(int32_t, int32_t, uint16_t, int32_t) override { return true; }
    bool playing(int32_t player, int32_t, uint16_t, int32_t code) override {
        return player == 63 ? code == 1100 : true;
    }
};
}  // namespace

C5R_TEST(a_live_ship_answers_1130_with_its_attack_clip) {
    Clips noClip;
    noClip.missing["FRIGATE"] = {1130};
    CombatParams p;
    CombatInput in = record(5, 31, kCombatDefenderRetaliates | kCombatBombing);
    in.attacker.damage = 5;
    ShipFeedback fb;
    CombatRun run;
    CHECK(run.start(in, unit(0, 1, {bomber("B", 1)}), unit(63, 2, {ship("FRIGATE", -1)}), {}, noClip, p, &fb).ok());
    std::vector<TimedEvent> out;
    bool end = false;
    for (int k = 0; k < 30 * 25 && !end; ++k) end = run.advance(1.0 / 30.0, out);
    CHECK(end);
    const CombatResult& r = run.result();
    CHECK(!r.at_limit);
    CHECK_EQ(r.attacker.paid, 1u);
    bool aa = false;
    for (auto& e : out) aa = aa || (e.ev.player == 63 && e.ev.action == ActionKind::AntiAir);
    CHECK(aa);
}

// ---- 4735 (unit-combat-server.md 10.11): the turn before a fight whose figures stand (CombatParams::turn_first)
namespace {
const double kPi = 3.14159265358979323846;
CombatUnitFigures facingWay(CombatUnitFigures u, float dx, float dy) {
    u.has_facing = true;
    u.facing_yaw = yawFacing(dx, dy);
    return u;
}
double firstStep(const CombatResult& r, int32_t player, FigState s) {
    for (auto& x : r.steps)
        if (x.player == player && x.state == s) return x.time;
    return -1;
}
double firstHit(const CombatResult& r) { return r.hits.empty() ? -1 : r.hits.front().time; }
}  // namespace

C5R_TEST(a_ranged_attacker_facing_away_turns_first_with_the_run) {
    Clips clips;
    CombatParams p = before5050();   // 5050 apart (its stance is a Turn too); the default below
    const CombatInput in = record(0, 40, kCombatRanged);
    // the attacker faces west, its target stands east of it; the defender already faces the attacker
    const CombatUnitFigures A = facingWay(placed(unit(0, 1, {rifle("R", 3)}), 0, 0), -1, 0);
    const CombatUnitFigures D = facingWay(placed(unit(63, 2, {soldier("S", 6)}), 128, 0), -1, 0);
    CombatResult r = choreograph(in, A, D, clips, p);
    CHECK(r.status.ok());
    CHECK(!r.at_limit);
    CHECK_EQ(countSteps(r, 0, FigState::Turn, 1400), 3u);   // every attacker figure, the run requested
    CHECK_EQ(countSteps(r, 63, FigState::Turn), 0u);        // within 30 degrees: no turn
    const double want = kPi / (2 * kPi * 0.75);   // 180 degrees at TurnRateMin + 0.5 x (Max - Min) = 0.75 turns per second
    size_t turns = 0;
    for (auto& m : r.moves) {
        if (!m.turn) continue;
        ++turns;
        CHECK(m.player == 0 && m.id == 1 && m.code == 1400);
        CHECK_NEAR(m.t0, 0, 1e-9);
        CHECK_NEAR(m.t1 - m.t0, want, 1e-4);
        CHECK(m.x0 == m.x1 && m.y0 == m.y1);   // in place
        CHECK_NEAR(m.yaw0, yawFacing(-1, 0), 1e-6);
        CHECK_NEAR(std::remainder(m.yaw1 - yawFacing(1, 0), 2 * kPi), 0, 1e-3);   // ends facing the defender (east)
    }
    CHECK_EQ(turns, 3u);
    for (auto& s : r.steps)   // turned: combat ready, then the bombard, as before
        if (s.player == 0 && s.state == FigState::Ready) CHECK(s.time >= want - 1e-9);
    // off: the rule before 4735 (no turn, no move); the strike and the first hit come the turn's length earlier
    CombatParams q = before5050();
    q.turn_first = false;
    const CombatResult r0 = choreograph(in, A, D, clips, q);
    CHECK(r0.status.ok());
    CHECK_EQ(countSteps(r0, 0, FigState::Turn), 0u);
    CHECK(r0.moves.empty());
    CHECK_NEAR(firstStep(r, 0, FigState::Strike) - firstStep(r0, 0, FigState::Strike), want, 1.0 / 30.0 + 1e-6);
    CHECK_NEAR(firstHit(r) - firstHit(r0), want, 1.0 / 30.0 + 1e-6);
    CHECK(r.end > r0.end);
    CHECK_EQ(r.defender.died, r0.defender.died);
    // the default (5050 on, W74): a figure that turned has moved, so 5050 (b) takes no stance, and without a reform (a)
    // changes nothing: the same fight as before 5050
    CHECK(sameResult(choreograph(in, A, D, clips, CombatParams()), r));
    // the timeline asks the controller for the run of the turning figures at 0
    bool run = false;
    for (auto& e : r.events)
        if (e.time == 0 && e.ev.player == 0 && e.ev.kind == EventKind::Action && e.ev.action == ActionKind::Run) run = true;
    CHECK(run);
    // the motion: in place, the heading the shorter way over the turn, then facing its target
    FightMotion fm;
    CHECK(fm.build(r, A, D).ok());
    FigurePose a0, mid, after;
    CHECK(fm.pose(0, 0, 1, 0, a0) && fm.pose(want / 2, 0, 1, 0, mid) && fm.pose(want + 0.1, 0, 1, 0, after));
    CHECK_NEAR(a0.yaw, yawFacing(-1, 0), 1e-4);
    CHECK_NEAR(std::fabs(std::remainder(mid.yaw - yawFacing(-1, 0), 2 * kPi)), kPi / 2, 1e-3);
    CHECK_NEAR(std::remainder(after.yaw - yawFacing(1, 0), 2 * kPi), 0, 1e-2);
    CHECK(mid.x == A.x[0] && mid.y == A.y[0] && !mid.moving);
}

C5R_TEST(a_city_volley_target_facing_away_turns_and_the_volley_keeps_its_time) {
    Clips clips;
    CombatParams p;
    CombatInput in = record(0, 60, kCombatRanged);
    in.kind = kCombatKindCity;
    in.attacker.unit = -1;
    in.attacker.city = 3;
    in.attacker.max_hp = 300;
    const CombatUnitFigures D = facingWay(placed(unit(63, 2, {soldier("S", 6)}), 128, 0), 1, 0);   // faces away from the city
    const CombatResult r = choreograph(in, centre(0, 0), D, clips, p);
    CHECK(r.status.ok());
    CHECK(!r.at_limit);
    CHECK_EQ(countSteps(r, 63, FigState::Turn, 1400), 6u);
    CHECK(countSteps(r, 63, FigState::Defend, 1280) > 0);   // turned, it bombard-defends as before
    CombatParams q;
    q.turn_first = false;
    const CombatResult r0 = choreograph(in, centre(0, 0), D, clips, q);
    CHECK_EQ(countSteps(r0, 63, FigState::Turn), 0u);
    CHECK_NEAR(firstHit(r), firstHit(r0), 1e-9);   // the city's volley does not wait for its target's turn
    CHECK_EQ(r.defender.died, r0.defender.died);
}

C5R_TEST(no_turn_when_facing_the_opponent_unknown_facing_no_run_clip_or_a_melee_fight) {
    Clips clips;
    CombatParams p = before5050();   // 5050 apart: its stance is a Turn too (P2 checks it)
    const CombatUnitFigures D = facingWay(placed(unit(63, 2, {soldier("S", 6)}), 128, 0), -1, 0);
    // facing its target already (within 30 degrees)
    CombatResult r = choreograph(record(0, 40, kCombatRanged), facingWay(placed(unit(0, 1, {rifle("R", 3)}), 0, 0), 1, 0.3f),
                                 D, clips, p);
    CHECK(r.status.ok());
    CHECK_EQ(countSteps(r, 0, FigState::Turn), 0u);
    // the unit's heading not given (has_facing false): the rule before 4735
    r = choreograph(record(0, 40, kCombatRanged), placed(unit(0, 1, {rifle("R", 3)}), 0, 0), D, clips, p);
    CHECK_EQ(countSteps(r, 0, FigState::Turn), 0u);
    // no run clip: no turn (no hold to the limit through a missing clip)
    Clips noRun;
    noRun.missing["R"] = {1400};
    r = choreograph(record(0, 40, kCombatRanged), facingWay(placed(unit(0, 1, {rifle("R", 3)}), 0, 0), -1, 0), D, noRun, p);
    CHECK(r.status.ok());
    CHECK(!r.at_limit);
    CHECK_EQ(countSteps(r, 0, FigState::Turn), 0u);
    // a melee attack: its figures approach (they run anyway); nobody turns first
    r = choreograph(record(0, 40, kCombatDefenderRetaliates), facingWay(placed(unit(0, 1, {soldier("A", 3)}), 0, 0), -1, 0),
                    facingWay(placed(unit(63, 2, {soldier("S", 6)}), 128, 0), 1, 0), clips, p);
    CHECK(r.status.ok());
    CHECK_EQ(countSteps(r, 0, FigState::Turn) + countSteps(r, 63, FigState::Turn), 0u);
    // a bad turn parameter is used as clamped (turn_max below turn_min: turn_min)
    CombatParams b;
    b.turn_min = 0.5;
    b.turn_max = 0.1;
    r = choreograph(record(0, 40, kCombatRanged), facingWay(placed(unit(0, 1, {rifle("R", 3)}), 0, 0), -1, 0), D, clips, b);
    CHECK(r.status.ok());
    for (auto& m : r.moves)
        if (m.turn) CHECK_NEAR(m.t1 - m.t0, 0.5, 1e-9);
}

// ---- 4745 + 4747 (unit-combat-server.md 18.15, 18.15.1, 18.17): stock's phase 1, the reform (CombatParams::reform_first)
namespace {
CombatMemberArt reformer(CombatMemberArt m, int mask) {
    m.motion.reform = mask;
    return m;
}
// the last reform arrival (the end of the moves that start with a Reform step); -1 when none
double reformArrival(const CombatResult& r) {
    double t = -1;
    for (auto& s : r.steps) {
        if (s.state != FigState::Reform) continue;
        for (auto& m : r.moves)
            if (!m.turn && m.player == s.player && m.id == s.id && m.figure == s.figure && m.t0 == s.time) t = std::max(t, m.t1);
    }
    return t;
}
bool sameResult(const CombatResult& a, const CombatResult& b) {
    if (a.end != b.end || a.at_limit != b.at_limit || a.steps.size() != b.steps.size() || a.hits.size() != b.hits.size() ||
        a.moves.size() != b.moves.size() || a.events.size() != b.events.size())
        return false;
    for (size_t i = 0; i < a.steps.size(); ++i)
        if (a.steps[i].time != b.steps[i].time || a.steps[i].state != b.steps[i].state || a.steps[i].code != b.steps[i].code ||
            a.steps[i].figure != b.steps[i].figure || a.steps[i].player != b.steps[i].player)
            return false;
    for (size_t i = 0; i < a.hits.size(); ++i)
        if (a.hits[i].time != b.hits[i].time || a.hits[i].target_figure != b.hits[i].target_figure) return false;
    for (size_t i = 0; i < a.moves.size(); ++i)
        if (a.moves[i].t0 != b.moves[i].t0 || a.moves[i].t1 != b.moves[i].t1 || a.moves[i].x1 != b.moves[i].x1) return false;
    return true;
}
}  // namespace

// R1 (GWI on a machine gun): the attacker's ReformBeforeCombat 1 names it; facing away from its target it re-lays its
// figures about its root turned to face the defender, each runs to the nearest new slot, and no figure of either side
// strikes or lands a hit before the last one has arrived. Fails on the code before 4745 (no Reform step).
C5R_TEST(r1_a_reforming_attacker_re_lays_before_anyone_strikes) {
    Clips clips;
    CombatParams p;
    const CombatInput in = record(20, 45, kCombatDefenderRetaliates);
    // the attacker's figures at (0, 0), (4, 0), (8, 0), root (0, 0), facing west; the defender east of it at 128
    const CombatUnitFigures A = facingWay(placed(unit(0, 1, {reformer(rifle("G", 3), 1)}), 0, 0), -1, 0);
    const CombatUnitFigures D = facingWay(placed(unit(63, 2, {rifle("M", 3)}), 128, 0), -1, 0);
    const CombatResult r = choreograph(in, A, D, clips, p);
    CHECK(r.status.ok());
    CHECK(!r.at_limit);
    // turned 180 degrees about the root: slots (0, 0), (-4, 0), (-8, 0); figure 0 stands, 1 and 2 run 8 and 16 units
    CHECK_EQ(countSteps(r, 0, FigState::Reform, 1400), 2u);
    CHECK_EQ(countSteps(r, 63, FigState::Reform), 0u);   // ReformBeforeCombat 0: the defender stands
    size_t legs = 0;
    for (auto& m : r.moves) {
        if (m.t0 != 0 || m.player != 0) continue;
        ++legs;
        CHECK(m.code == 1400 && !m.turn);
        CHECK_NEAR(m.y1, 0, 1e-4);
        CHECK(m.x1 < 0);   // the formation now lies west of the root, facing east
        CHECK_NEAR(m.t1, std::fabs(m.x1 - m.x0) / 50.0, 1e-4);   // the default MoveRate 0.78125 x 64 = 50 units per second
    }
    CHECK_EQ(legs, 2u);
    const double arrive = reformArrival(r);
    CHECK_NEAR(arrive, 16.0 / 50.0, 1e-4);
    for (auto& s : r.steps)   // nobody looks, approaches or strikes during the reform
        if (s.state == FigState::Strike || s.state == FigState::Approach || s.state == FigState::Charge ||
            s.state == FigState::Ready)
            CHECK(s.time >= arrive - 1e-9);
    CHECK(firstHit(r) >= arrive);
    CHECK_EQ(countSteps(r, 0, FigState::Turn), 0u);
    // the arrival: the run stops and the figure stands (1000) until the phase ends
    CHECK_EQ(countSteps(r, 0, FigState::Look, 1000), 2u);
    // off: the rule before 4745, no Reform step; the fight is shorter by the reform
    CombatParams q;
    q.reform_first = false;
    const CombatResult r0 = choreograph(in, A, D, clips, q);
    CHECK(r0.status.ok());
    CHECK_EQ(countSteps(r0, 0, FigState::Reform) + countSteps(r0, 63, FigState::Reform), 0u);
    CHECK(r.end > r0.end);
    CHECK_EQ(r.defender.died, r0.defender.died);
    CHECK_EQ(r.attacker.died, r0.attacker.died);
    // the motion: the reform's run, then (standing) facing the defender
    FightMotion fm;
    CHECK(fm.build(r, A, D).ok());
    FigurePose mid, after;
    CHECK(fm.pose(arrive / 2, 0, 1, 2, mid) && fm.pose(arrive + 0.01, 0, 1, 2, after));
    CHECK(mid.moving);
    CHECK_NEAR(after.x, -8.0f, 1e-3);
}

// R2: a rush member's ReformBeforeCombat 2 names the defender: it reforms when attacked (WWI tank on a Lancer), not when
// it attacks (Lancer on a Destroyer)
C5R_TEST(r2_the_defender_bit_reforms_only_a_defender) {
    Clips clips;
    CombatParams p;
    CombatMemberArt lancer = reformer(soldier("L", 3), 2);
    lancer.rush = true;
    // the lancer defends, facing east (away from the attacker west of it)
    CombatResult r = choreograph(record(10, 45, kCombatDefenderRetaliates),
                                 facingWay(placed(unit(0, 1, {reformer(rifle("T", 3), 1)}), 0, 0), 1, 0),
                                 facingWay(placed(unit(63, 2, {lancer}), 128, 0), 1, 0), clips, p);
    CHECK(r.status.ok());
    CHECK(!r.at_limit);
    CHECK(countSteps(r, 63, FigState::Reform, 1400) > 0);
    CHECK_EQ(countSteps(r, 0, FigState::Reform), 0u);   // the tank faces its target already: its slots do not move
    CHECK(firstHit(r) >= reformArrival(r));
    // the lancer attacks, facing away from its target: 2 names the defender only, no reform
    r = choreograph(record(25, 30, kCombatDefenderRetaliates), facingWay(placed(unit(0, 1, {lancer}), 0, 0), -1, 0),
                    facingWay(placed(unit(63, 2, {ship("D", 1)}), 128, 0), -1, 0), clips, p);
    CHECK(r.status.ok());
    CHECK_EQ(countSteps(r, 0, FigState::Reform) + countSteps(r, 63, FigState::Reform), 0u);
}

// R3 / R4: no ReformBeforeCombat (Warrior on Warrior) is the same fight to the frame with the knob on or off; a reforming
// unit with the knob off is the same fight as without the column
C5R_TEST(r3_r4_no_reform_column_or_knob_off_is_todays_fight_to_the_frame) {
    Clips clips;
    CombatParams on, off;
    off.reform_first = false;
    const CombatInput in = record(20, 40, kCombatDefenderRetaliates);
    const CombatUnitFigures A = facingWay(placed(unit(0, 1, {soldier("W", 4)}), 0, 0), -1, 0);
    const CombatUnitFigures D = facingWay(placed(unit(63, 2, {soldier("W", 4)}), 128, 0), 1, 0);
    const CombatResult a = choreograph(in, A, D, clips, on), b = choreograph(in, A, D, clips, off);
    CHECK(a.status.ok() && b.status.ok());
    CHECK(sameResult(a, b));
    const CombatUnitFigures AR = facingWay(placed(unit(0, 1, {reformer(soldier("W", 4), 3)}), 0, 0), -1, 0);
    const CombatUnitFigures DR = facingWay(placed(unit(63, 2, {reformer(soldier("W", 4), 3)}), 128, 0), 1, 0);
    const CombatResult c = choreograph(in, AR, DR, clips, off);
    CHECK(c.status.ok());
    CHECK(sameResult(b, c));
    const CombatResult d = choreograph(in, AR, DR, clips, on);   // and on, both sides reform
    CHECK(countSteps(d, 0, FigState::Reform) > 0 && countSteps(d, 63, FigState::Reform) > 0);
    // without the unit's heading (has_facing false) or without positions: no reform
    CombatUnitFigures noFacing = AR;
    noFacing.has_facing = false;
    CHECK_EQ(countSteps(choreograph(in, noFacing, D, clips, on), 0, FigState::Reform), 0u);
    CHECK_EQ(countSteps(choreograph(in, unit(0, 1, {reformer(soldier("W", 4), 1)}), unit(63, 2, {soldier("W", 4)}), clips, on),
                        0, FigState::Reform),
             0u);
}

// 4747: a ranged attacker reforms too (phase 1 for every fight kind); it then faces its target and takes no turn_first
// turn; a city's volley waits for its reforming target
C5R_TEST(a_ranged_attacker_reforms_instead_of_turning_and_a_city_volley_waits) {
    Clips clips;
    CombatParams p = before5050();   // 5050 apart: its stance is a Turn too (P1 / P2 check it)
    const CombatInput in = record(0, 40, kCombatRanged);
    const CombatUnitFigures A = facingWay(placed(unit(0, 1, {reformer(rifle("C", 3), 1)}), 0, 0), -1, 0);
    const CombatUnitFigures D = facingWay(placed(unit(63, 2, {soldier("S", 6)}), 128, 0), -1, 0);
    const CombatResult r = choreograph(in, A, D, clips, p);
    CHECK(r.status.ok());
    CHECK(!r.at_limit);
    CHECK_EQ(countSteps(r, 0, FigState::Reform, 1400), 2u);
    CHECK_EQ(countSteps(r, 0, FigState::Turn), 0u);
    const double arrive = reformArrival(r);
    CHECK(arrive > 0);
    CHECK(firstStep(r, 0, FigState::Strike) >= arrive);
    CombatParams q = before5050();
    q.reform_first = false;
    const CombatResult r0 = choreograph(in, A, D, clips, q);
    CHECK_EQ(countSteps(r0, 0, FigState::Turn, 1400), 3u);   // off: 4735's turn as before
    // a city attacks a defender whose ReformBeforeCombat names it, standing turned away: the volley leaves after the reform
    CombatInput ci = record(0, 60, kCombatRanged);
    ci.kind = kCombatKindCity;
    ci.attacker.unit = -1;
    ci.attacker.city = 3;
    ci.attacker.max_hp = 300;
    const CombatUnitFigures DR = facingWay(placed(unit(63, 2, {reformer(soldier("S", 6), 2)}), 128, 0), 1, 0);
    const CombatResult c = choreograph(ci, centre(0, 0), DR, clips, p);
    CHECK(c.status.ok());
    CHECK(!c.at_limit);
    CHECK(countSteps(c, 63, FigState::Reform) > 0);
    CHECK(c.hits.front().launch >= reformArrival(c) - 1e-9);
}

// ---- 4742 (unit-combat-server.md 18.18): the survivors' walk home inside the fight (CombatParams::walk_home)
namespace {
struct Walk {
    size_t walkers = 0, refaces = 0;
    double start = -1, arrive = -1;
    bool homes_ok = true;     // every walk ends at the start slot of the walker's rank
    bool refaces_ok = true;   // every walker refaces (1000) at its arrival, within one step
};
// the walks of `player` in `r`: Done steps that request the run (1400) and their moves; `u` the side's figures as given
// (their start positions: figure i of a `placed` unit at u.x[i], u.y[i])
Walk walksOf(const CombatResult& r, int32_t player, const CombatUnitFigures& u, double step) {
    Walk w;
    std::set<uint16_t> died;
    for (auto& s : r.steps)
        if (s.player == player && (s.state == FigState::Dying || s.state == FigState::Removed)) died.insert(s.figure);
    for (auto& s : r.steps) {
        if (s.player != player) continue;
        if (s.state == FigState::Reface) ++w.refaces;
        if (s.state != FigState::Done || s.code != 1400) continue;
        ++w.walkers;
        if (w.start < 0 || s.time < w.start) w.start = s.time;
        int rank = 0;
        for (uint16_t g : u.standing)
            if (g < s.figure && !died.count(g)) ++rank;
        const FigureMove* mv = nullptr;
        for (auto& m : r.moves)
            if (!m.turn && m.player == s.player && m.id == s.id && m.figure == s.figure && m.t0 == s.time) mv = &m;
        if (!mv) {
            w.homes_ok = false;
            continue;
        }
        w.arrive = std::max(w.arrive, mv->t1);
        if (std::fabs(mv->x1 - u.x[rank]) > 1e-3f || std::fabs(mv->y1 - u.y[rank]) > 1e-3f) w.homes_ok = false;
        bool refaced = false;
        for (auto& q : r.steps)
            if (q.player == s.player && q.id == s.id && q.figure == s.figure && q.state == FigState::Reface && q.code == 1000 &&
                q.time >= mv->t1 - 1e-9 && q.time <= mv->t1 + step + 1e-9)
                refaced = true;
        if (!refaced) w.refaces_ok = false;
    }
    return w;
}
bool sameHits(const CombatResult& a, const CombatResult& b) {
    if (a.hits.size() != b.hits.size()) return false;
    for (size_t i = 0; i < a.hits.size(); ++i)
        if (a.hits[i].time != b.hits[i].time || a.hits[i].target_figure != b.hits[i].target_figure ||
            a.hits[i].figure != b.hits[i].figure || a.hits[i].result != b.hits[i].result)
            return false;
    return true;
}
// the placement after the fight: the survivors (figures 0..m-1) in slots 0..m-1 of a `placed` unit
c5r::units::UnitPlacement afterPlacement(const CombatUnitFigures& u, uint32_t living) {
    c5r::units::UnitPlacement p;
    p.rootX = u.centre_x;
    p.rootY = u.centre_y;
    p.yaw = 0;
    p.figures = static_cast<uint32_t>(u.standing.size());
    p.living = living;
    for (uint32_t i = 0; i < living; ++i) {
        c5r::units::FigurePlacement f;
        f.figure = static_cast<uint16_t>(i);
        f.slot = static_cast<uint16_t>(i);
        f.x = u.x[i];
        f.y = u.y[i];
        p.placed.push_back(f);
    }
    return p;
}
}  // namespace

// H3 (melee, Swordsman on a Warrior): the attackers run to contact and, once the fight's deaths are played, the
// survivors run home inside the fight (Done with 1400), reface at their slots (Reface with 1000) and the fight ends at
// the last arrival; FightMotion then has nothing left to walk after the end. Fails on the code before 4742 (no Done step
// with 1400; the walk only after the end).
C5R_TEST(h3_melee_survivors_walk_home_inside_the_fight) {
    Clips clips;
    CombatParams p;
    p.charge_any = false;   // 4743 W81: as written (W54, charge_any off then); the default (on) below
    p.defender_waits_first_landing = false;   // 5057 W85: as written (before 5057); the default arm below takes it
    p.melee_reach = false;   // 0743 W137: as written (before 0743); the default arm below takes it
    p.defender_step = false;   // 5061 W139: as written (the defender stands, before 5061); the default arm below takes it
    const CombatInput in = record(30, 60, kCombatDefenderRetaliates);
    const CombatUnitFigures A = facingWay(placed(unit(0, 1, {soldier("S", 4)}), 0, 0), 1, 0);
    const CombatUnitFigures D = facingWay(placed(unit(63, 2, {soldier("W", 4)}), 128, 0), -1, 0);
    const CombatResult r = choreograph(in, A, D, clips, p);
    CHECK(r.status.ok());
    CHECK(!r.at_limit);
    const Walk w = walksOf(r, 0, A, p.step);
    CHECK(w.walkers > 0);
    CHECK(w.homes_ok);
    CHECK(w.refaces_ok);
    CHECK_EQ(w.refaces, w.walkers);
    CHECK(r.end >= w.arrive - 1e-9 && r.end <= w.arrive + p.step + 1e-9);   // the fight ends at the last arrival
    CHECK(w.start > firstHit(r));
    for (auto& h : r.hits) CHECK(h.time <= w.start + 1e-9);   // every landing is before the walk
    for (auto& s : r.steps)   // no strike or approach after the walk's start (the fight's deaths are all played)
        if (s.time > w.start + 1e-9) CHECK(s.state != FigState::Strike && s.state != FigState::Approach);
    // off: the rule before 4742: no walk inside the fight, the same strikes and deaths, the fight shorter by the walk
    // (the walk runs while the last figures fall: it starts before the old end, the last fall)
    CombatParams q;
    q.charge_any = false;   // 4743 W81: as p
    q.defender_waits_first_landing = false;   // 5057 W85: as p
    q.melee_reach = false;   // 0743 W137: as p
    q.defender_step = false;   // 5061 W139: as p
    q.walk_home = false;
    const CombatResult r0 = choreograph(in, A, D, clips, q);
    CHECK(r0.status.ok());
    CHECK_EQ(countSteps(r0, 0, FigState::Done, 1400) + countSteps(r0, 63, FigState::Done, 1400), 0u);
    CHECK_EQ(countSteps(r0, 0, FigState::Reface) + countSteps(r0, 63, FigState::Reface), 0u);
    CHECK(sameHits(r, r0));
    CHECK_EQ(r.attacker.died, r0.attacker.died);
    CHECK_EQ(r.defender.died, r0.defender.died);
    CHECK(r.end > r0.end + 0.5);
    CHECK(w.start <= r0.end + 1e-9);
    // the motion: running home mid-walk, then the placement's heading; nothing left to walk after the end, while the
    // rule before 4742 walks after the end
    FightMotion fm, fm0;
    CHECK(fm.build(r, A, D).ok() && fm0.build(r0, A, D).ok());
    FigurePose mid, home;
    CHECK(fm.pose((w.start + w.arrive) / 2, 0, 1, 0, mid) && fm.pose(r.end, 0, 1, 0, home));
    CHECK(mid.moving);
    CHECK(!home.moving && !home.yaw_known);
    const c5r::units::UnitPlacement after = afterPlacement(A, A.figures() - r.attacker.died);
    std::vector<c5r::units::FigureOffset> offs;
    CHECK(!fm.offsets(r.end + 0.2, 0, 1, after, offs));
    CHECK(offs.empty());
    CHECK(fm0.offsets(r0.end + 0.2, 0, 1, after, offs));
    // 4743 W81: the default (charge_any on: the Swordsman charges, 1120 / 1140): the survivors still walk home inside the
    // fight, reface at their slots, every landing is before the walk and the fight ends at the last arrival
    {
        const CombatResult d = choreograph(in, A, D, clips, CombatParams());
        CHECK(d.status.ok() && !d.at_limit);
        CHECK(countSteps(d, 0, FigState::Charge) > 0u);
        const Walk wd = walksOf(d, 0, A, p.step);
        CHECK(wd.walkers > 0);
        CHECK(wd.homes_ok);
        CHECK(wd.refaces_ok);
        CHECK_EQ(wd.refaces, wd.walkers);
        // 0743 (melee_reach, on by default): the attackers stop 16 units short of the defender, so their walk home is
        // shorter and may end before the last figure's fall: the walk ends inside the fight, the fight no later than the
        // later of the two
        CHECK(d.end >= wd.arrive - 1e-9);
        for (auto& h : d.hits) CHECK(h.time <= wd.start + 1e-9);
    }
}

// H1 (ranged, an Archer whose ReformBeforeCombat names its side, standing turned away): its figures re-lay facing the
// target (4745), shoot, and walk back from the re-laid slots to their start slots inside the fight; the defender, which
// never moved, does not walk
C5R_TEST(h1_a_reformed_ranged_attacker_walks_back_from_its_re_laid_slots) {
    Clips clips;
    CombatParams p;
    const CombatInput in = record(0, 40, kCombatRanged);
    const CombatUnitFigures A = facingWay(placed(unit(0, 1, {reformer(rifle("A", 3), 1)}), 0, 0), -1, 0);
    const CombatUnitFigures D = facingWay(placed(unit(63, 2, {soldier("S", 6)}), 128, 0), -1, 0);
    const CombatResult r = choreograph(in, A, D, clips, p);
    CHECK(r.status.ok());
    CHECK(!r.at_limit);
    CHECK_EQ(countSteps(r, 0, FigState::Reform, 1400), 2u);
    const Walk w = walksOf(r, 0, A, p.step);
    CHECK_EQ(w.walkers, 2u);   // figures 1 and 2 ran 8 and 16 units in the reform; figure 0 stood at the root
    CHECK(w.homes_ok && w.refaces_ok);
    CHECK_NEAR(w.arrive - w.start, 16.0 / 50.0, 1e-4);
    CHECK_EQ(walksOf(r, 63, D, p.step).walkers, 0u);
    // the fight ends at the later of the walk's last arrival and the rule before 4742's end (the defender's falls)
    CombatParams q;
    q.walk_home = false;
    const CombatResult r0 = choreograph(in, A, D, clips, q);
    CHECK(sameHits(r, r0));
    CHECK(w.start <= r0.end + 1e-9);
    CHECK(r.end >= w.arrive - 1e-9 && r.end >= r0.end - 1e-9);
    CHECK(r.end <= std::max(w.arrive, r0.end) + p.step + 1e-9);
}

// H2 (siege, a two-figure gun crew that shuffles: its walk home lies inside ShortMoveRadius): the walk is a 1450 shuffle
// move at ShortMoveRate, the step still requests the run (1400; the machine has no 1450 request)
C5R_TEST(h2_a_siege_crew_shuffles_home_inside_short_move_radius) {
    Clips clips;
    CombatParams p;
    const CombatInput in = record(0, 30, kCombatRanged);
    CombatMemberArt gun = reformer(rifle("C", 2), 1);
    gun.motion.short_move_radius = 24.0f;
    gun.motion.short_move_rate = 0.35f;   // 22.4 world units per second
    const CombatUnitFigures A = facingWay(placed(unit(0, 1, {gun}), 0, 0), -1, 0);
    const CombatUnitFigures D = facingWay(placed(unit(63, 2, {soldier("S", 4)}), 128, 0), -1, 0);
    const CombatResult r = choreograph(in, A, D, clips, p);
    CHECK(r.status.ok());
    CHECK(!r.at_limit);
    const Walk w = walksOf(r, 0, A, p.step);
    CHECK_EQ(w.walkers, 1u);
    CHECK(w.homes_ok && w.refaces_ok);
    size_t shuffles = 0;
    for (auto& m : r.moves)
        if (m.player == 0 && m.t0 >= w.start - 1e-9 && m.code == 1450) {
            ++shuffles;
            CHECK_NEAR(m.t1 - m.t0, 8.0 / 22.4, 1e-4);   // the reform turned slot 1 from x 4 to x -4: 8 units back
        }
    CHECK_EQ(shuffles, 1u);
}

// The 12 s limit cuts a walk: the fight ends at its limit, the walking figure's move ends where it was, it is not
// refaced, and FightMotion's walk after the end finishes it; a fight without positions is the same fight on or off
C5R_TEST(the_limit_cuts_a_walk_home_and_no_positions_means_no_walk) {
    Clips clips;
    CombatParams p;
    const CombatInput in = record(30, 60, kCombatDefenderRetaliates);
    const CombatUnitFigures A = facingWay(placed(unit(0, 1, {soldier("S", 4)}), 0, 0), 1, 0);
    const CombatUnitFigures D = facingWay(placed(unit(63, 2, {soldier("W", 4)}), 128, 0), -1, 0);
    const CombatResult full = choreograph(in, A, D, clips, p);
    const Walk w = walksOf(full, 0, A, p.step);
    CHECK(w.walkers > 0 && w.arrive > w.start + 0.4);
    CombatParams q;
    q.limit = w.start + 0.2;
    const CombatResult r = choreograph(in, A, D, clips, q);
    CHECK(r.status.ok());
    CHECK(r.at_limit);
    CHECK_NEAR(r.end, q.limit, 1e-9);
    CHECK_EQ(countSteps(r, 0, FigState::Reface), 0u);
    size_t cut = 0;
    for (auto& m : r.moves)
        if (m.player == 0 && m.t0 >= w.start - 1e-9) {
            CHECK(m.t1 <= q.limit + p.step + 1e-9);
            ++cut;
        }
    CHECK(cut > 0);
    FightMotion fm;
    CHECK(fm.build(r, A, D).ok());
    std::vector<c5r::units::FigureOffset> offs;
    CHECK(fm.offsets(r.end + 0.05, 0, 1, afterPlacement(A, A.figures() - r.attacker.died), offs));   // the fallback walks
    // no positions: nothing to walk; on and off give the same fight
    CombatParams off;
    off.walk_home = false;
    const CombatUnitFigures An = unit(0, 1, {soldier("S", 4)}), Dn = unit(63, 2, {soldier("W", 4)});
    const CombatResult a = choreograph(in, An, Dn, clips, p), b = choreograph(in, An, Dn, clips, off);
    CHECK(sameResult(a, b));
    CHECK_EQ(countSteps(a, 0, FigState::Done, 1400), 0u);
}

// 4748 (W58, unit-combat-server.md 18.21): a melee attack on a city. Stock (type 0, phases 1, 2, 4) lets the attacking
// figures strike the city from where they stand [L W52: five Lancer attacks on city 13132, no rider moved]; route S
// charged (a rush member) or ran (any other) one figure to the city's centre and so added its run stop (1440) and the
// combat-ready entry before its 1160. Default: no approach, no move, the strike asked after the 1600 entry alone.
namespace {
CombatMemberArt rider(const std::string& name, int n) {   // a rush member without AttackCharge (VP's cavalry, 18.13)
    CombatMemberArt m = soldier(name, n);
    m.rush = true;
    m.enable.erase("AttackCharge");
    return m;
}
CombatInput onCity(int32_t afinal) {
    CombatInput in = record(afinal, 30, kCombatDefenderRetaliates);
    in.kind = kCombatKindCity;
    in.defender.unit = -1;
    in.defender.city = 7;
    in.defender.max_hp = 200;
    return in;
}
double firstStrikeStep(const CombatResult& r, int32_t player) {
    double t = 1e9;
    for (auto& s : r.steps)
        if (s.player == player && s.state == FigState::Strike) t = std::min(t, s.time);
    return t;
}
}  // namespace

C5R_TEST(a_melee_attack_on_a_city_strikes_from_where_the_figures_stand) {
    Clips clips;
    const CombatParams p = before5050();   // 5050 (b) apart: the entry from idle as written; the default below
    for (const CombatMemberArt& m : {rider("L", 5), soldier("G", 6)}) {   // a Lancer and a gun infantry stand-in
        const CombatInput in = onCity(25);
        const CombatUnitFigures A = placed(unit(0, 1, {m}), 0, 0);
        const CombatResult r = choreograph(in, A, centre(128, 0), clips, p);
        CHECK(r.status.ok() && !r.at_limit);
        CHECK_EQ(countSteps(r, 0, FigState::Charge), 0u);     // today: the rider charges (1120) to the centre
        CHECK_EQ(countSteps(r, 0, FigState::Approach), 0u);   // ... a non-rush figure runs (1400)
        size_t moved = 0;
        for (auto& mv : r.moves)
            if (mv.player == 0 && mv.t0 < firstStrikeStep(r, 0)) ++moved;
        CHECK_EQ(moved, 0u);
        // the strike is asked after the combat-ready entry (1.0 s here) and a step, with no run stop before it
        CHECK(firstStrikeStep(r, 0) <= clips.t[1600].duration + 2.0 / 30.0 + 1e-9);
        CHECK_EQ(r.attacker.died, r.attacker.planned);
        CHECK(r.end < 6.0);
        // the default (5050 (b) on, W74): the figures still strike from where they stand, after the phase-2 stance or
        // hold (attack_stance_s; no 1450 clip here: the hold and then the whole entry)
        {
            const CombatResult d = choreograph(in, A, centre(128, 0), clips, CombatParams());
            CHECK(d.status.ok() && !d.at_limit);
            size_t movedD = 0;
            for (auto& mv : d.moves)
                if (mv.player == 0 && mv.t0 < firstStrikeStep(d, 0)) ++movedD;
            CHECK_EQ(movedD, 0u);
            CHECK(firstStrikeStep(d, 0) <= CombatParams().attack_stance_s + clips.t[1600].duration + 3.0 / 30.0 + 1e-9);
            CHECK_EQ(d.attacker.died, d.attacker.planned);
        }
        // 4748 (W59): off (city_in_place false) is the rule before 4748: the rider charges, the other figure runs, to the
        // city's centre before its strike, and the fight is longer
        CombatParams off = before5050();
        off.city_in_place = false;
        off.charge_any = false;   // 4743 W81: as written (W59, charge_any off then: the non-rush figure runs)
        const CombatResult o = choreograph(in, A, centre(128, 0), clips, off);
        CHECK(o.status.ok() && !o.at_limit);
        CHECK_EQ(countSteps(o, 0, m.rush ? FigState::Charge : FigState::Approach) > 0u, true);
        size_t movedOff = 0;
        for (auto& mv : o.moves)
            if (mv.player == 0 && mv.t0 < firstStrikeStep(o, 0)) ++movedOff;
        CHECK(movedOff > 0u);
        CHECK(firstStrikeStep(o, 0) > firstStrikeStep(r, 0));
        CHECK(o.end > r.end);
    }
}

// ---- 5050 (W65; unit-combat-server.md 18.22, 18.23): stock's phases 1 + 2 (CombatParams::reform_ends_at_arrival,
// attack_from_stance)
namespace {
// the first step of `player` in any state but the reform's (Reform, or Look with 1000 / nothing)
double firstActionStep(const CombatResult& r, int32_t player) {
    for (auto& s : r.steps)
        if (s.player == player && s.state != FigState::Reform &&
            !(s.state == FigState::Look && (s.code == 1000 || s.code == 0)))
            return s.time;
    return -1;
}
bool walkEventFor(const CombatResult& r, int32_t player) {
    for (auto& e : r.events)
        if (e.ev.kind == EventKind::Action && e.ev.player == player && e.ev.action == ActionKind::Walk) return true;
    return false;
}
}  // namespace

// P1 (a): a reforming ranged attacker (4747's case) with a 0.9 s run stop: before 5050 its phase 1 ends at the last
// arrival plus the stop; with reform_ends_at_arrival at the arrival (stock: 0xD -> 3 on arrival, 18.22), and the whole
// fight is shorter by the stop. Without a reform the knob changes nothing.
C5R_TEST(p1_phase_1_ends_at_the_last_arrival_not_after_the_run_stop) {
    CHECK(CombatParams().reform_ends_at_arrival);   // on by default since 5050 W74 (MC #10667 / #10726); off: the rule before
    Clips clips;
    clips.t[1440] = Clips::timing(0.9f, {});
    const CombatInput in = record(0, 40, kCombatRanged);
    const CombatUnitFigures A = facingWay(placed(unit(0, 1, {reformer(rifle("C", 3), 1)}), 0, 0), -1, 0);
    const CombatUnitFigures D = facingWay(placed(unit(63, 2, {soldier("S", 6)}), 128, 0), -1, 0);
    CombatParams off, on;
    off.reform_ends_at_arrival = false;   // the rule before 5050 (explicit since the default is on, W74)
    off.attack_from_stance = false;       // (b) apart: this case measures (a) alone
    on.attack_from_stance = false;
    on.reform_ends_at_arrival = true;
    const CombatResult r0 = choreograph(in, A, D, clips, off), r = choreograph(in, A, D, clips, on);
    CHECK(r0.status.ok() && !r0.at_limit && r.status.ok() && !r.at_limit);
    const double arrive = reformArrival(r);
    CHECK(arrive > 0);
    CHECK_NEAR(reformArrival(r0), arrive, 1e-9);   // the same reform runs
    const double h = on.step;
    // off: nobody acts before the arrival plus the stop; on: the first action is the step after the arrival
    CHECK(firstActionStep(r0, 0) >= arrive + 0.9 - 1e-9);
    CHECK(firstActionStep(r, 0) >= arrive - 1e-9 && firstActionStep(r, 0) < arrive + h + 1e-6);
    CHECK(firstActionStep(r, 63) >= arrive - 1e-9);   // the defender too waits for phase 1
    CHECK_NEAR(firstStep(r0, 0, FigState::Strike) - firstStep(r, 0, FigState::Strike), 0.9, 2 * h + 1e-6);
    CHECK_NEAR(r0.end - r.end, 0.9, 2 * h + 1e-6);
    CHECK_EQ(r.defender.died, r0.defender.died);
    // the default (both knobs on since 5050 W74): phase 1 ends at the arrival as with `on`, the run stop not waited for;
    // (b) then adds its phase 2 (the reformed figure has not moved since: the hold, no 1450 clip here), so the fight is
    // shorter than off but not by the whole stop
    {
        const CombatResult rd = choreograph(in, A, D, clips, CombatParams());
        CHECK(rd.status.ok() && !rd.at_limit);
        CHECK(firstActionStep(rd, 0) >= arrive - 1e-9 && firstActionStep(rd, 0) < arrive + h + 1e-6);
        CHECK(rd.end < r0.end);
    }
    // no reform (facing its target already): the same fight to the frame
    const CombatUnitFigures AF = facingWay(placed(unit(0, 1, {reformer(rifle("C", 3), 1)}), 0, 0), 1, 0);
    CHECK(sameResult(choreograph(in, AF, D, clips, off), choreograph(in, AF, D, clips, on)));
}

// P2 (b): an attacker that strikes from where it stands (a ranged attack) takes stock's phase-2 stance: 1450 (Turn, a
// Walk event) at its first look, then 1600 attack_stance_s later and its strike attack_ready_s after that, instead of
// 1600 at once and the strike after the 1600 entry clip. Skips (the same fight to the frame as off): a ship, a figure
// whose Ready follows a run (a melee approach). A member without Walk or without a 1450 clip holds instead (no request),
// then plays its whole 1600 entry before the strike.
C5R_TEST(p2_an_attacker_that_has_not_moved_takes_the_phase_2_stance_first) {
    CHECK(CombatParams().attack_from_stance);   // on by default since 5050 W74 (MC #10667 / #10726); off: the rule before
    CHECK(CombatParams().attack_hold_without_walk);
    CHECK_NEAR(CombatParams().attack_stance_s, 0.45, 1e-12);
    CHECK_NEAR(CombatParams().attack_ready_s, 0.15, 1e-12);
    Clips clips;
    clips.t[1450] = Clips::timing(0.8f, {});
    const CombatInput in = record(0, 40, kCombatRanged);
    const CombatUnitFigures A = facingWay(placed(unit(0, 1, {rifle("A", 3)}), 0, 0), 1, 0);   // faces its target
    const CombatUnitFigures D = facingWay(placed(unit(63, 2, {soldier("W", 5)}), 128, 0), -1, 0);
    CombatParams off, on;
    off.attack_from_stance = false;       // the rule before 5050 (explicit since the default is on, W74)
    off.reform_ends_at_arrival = false;   // (a) apart: this case measures (b) alone
    on.reform_ends_at_arrival = false;
    on.attack_from_stance = true;
    const CombatResult r0 = choreograph(in, A, D, clips, off), r = choreograph(in, A, D, clips, on);
    CHECK(r0.status.ok() && !r0.at_limit && r.status.ok() && !r.at_limit);
    const double h = on.step;
    CHECK_EQ(countSteps(r, 0, FigState::Turn, 1450), 3u);   // every attacker figure: stock's 0xF
    CHECK_EQ(countSteps(r0, 0, FigState::Turn), 0u);
    CHECK(walkEventFor(r, 0) && !walkEventFor(r0, 0));
    for (auto& s : r.steps)   // per figure: 1450 at 0, then 1600 at the stance's end, never 1600 before it
        if (s.player == 0 && s.state == FigState::Ready && s.code == 1600) CHECK(s.time >= 0.45 - 1e-9 && s.time < 0.45 + h + 1e-6);
    CHECK_NEAR(firstStep(r0, 0, FigState::Ready), 0.0, 1e-9);
    // off: the strike after the 1600 entry clip (1.0 s); on: after the stance (0.45 s) and the stop shuffle (0.15 s)
    CHECK(firstStep(r0, 0, FigState::Strike) >= 1.0 - 1e-9);
    CHECK(firstStep(r, 0, FigState::Strike) >= 0.6 - 1e-9 && firstStep(r, 0, FigState::Strike) < 0.6 + 2 * h + 1e-6);
    CHECK(r.end < r0.end);
    CHECK_EQ(r.defender.died, r0.defender.died);
    CHECK_EQ(r.attacker.died, r0.attacker.died);
    // the default (both knobs on since 5050 W74): the same fight as `on` (no reform here, so (a) changes nothing)
    {
        const CombatResult rd = choreograph(in, A, D, clips, CombatParams());
        CHECK(rd.status.ok() && !rd.at_limit && sameResult(rd, r));
        CHECK_EQ(countSteps(rd, 0, FigState::Turn, 1450), 3u);
    }

    // a ship: no stance, the same fight to the frame as off
    {
        const CombatUnitFigures S = facingWay(placed(unit(0, 1, {ship("S", 0)}), 0, 0), 1, 0);
        const CombatResult x = choreograph(in, S, D, clips, on), x0 = choreograph(in, S, D, clips, off);
        CHECK(x.status.ok() && countSteps(x, 0, FigState::Turn) == 0 && sameResult(x, x0));
    }
    // without Walk (a Gatling crew) or without a 1450 clip: the phase-2 hold (Turn, no request, no Walk event), then 1600
    // from idle and the strike after the whole entry clip (1.0 s); attack_hold_without_walk off: the same fight as off
    CombatMemberArt noWalk = rifle("A", 3);
    noWalk.enable.erase("Walk");
    struct Case {
        const char* name;
        CombatUnitFigures att;
        bool clip;
    } cases[] = {{"no Walk", facingWay(placed(unit(0, 1, {noWalk}), 0, 0), 1, 0), true}, {"no 1450 clip", A, false}};
    CombatParams noHold = on;
    noHold.attack_hold_without_walk = false;
    for (auto& c : cases) {
        Clips cc;
        if (c.clip) cc.t[1450] = Clips::timing(0.8f, {});
        const CombatResult x = choreograph(in, c.att, D, cc, on), x0 = choreograph(in, c.att, D, cc, off),
                           xn = choreograph(in, c.att, D, cc, noHold);
        const bool hold = countSteps(x, 0, FigState::Turn, 0) == 3u && countSteps(x, 0, FigState::Turn, 1450) == 0u &&
                          !walkEventFor(x, 0);
        const double ready = firstStep(x, 0, FigState::Ready), strike = firstStep(x, 0, FigState::Strike);
        const bool timed = ready >= 0.45 - 1e-9 && ready < 0.45 + h + 1e-6 && strike >= ready + 1.0 - 1e-9;
        if (!x.status.ok() || !hold || !timed || x.end <= x0.end || !sameResult(xn, x0))
            c5r::test::fail(__FILE__, __LINE__, std::string(c.name) + ": hold " + std::to_string(hold) + " ready " +
                                                    std::to_string(ready) + " strike " + std::to_string(strike));
    }
    // a melee attacker runs to its target first: its Ready follows the run (the machine is in its run loop), no stance
    const CombatInput mi = record(20, 40, kCombatDefenderRetaliates);
    const CombatResult m = choreograph(mi, A, D, clips, on), m0 = choreograph(mi, A, D, clips, off);
    CHECK(m.status.ok() && m0.status.ok());
    CHECK_EQ(countSteps(m, 0, FigState::Turn, 1450), 0u);
    CHECK(sameResult(m, m0));
}

// P3 (5074, unit-combat-server.md 18.26.4): a turned attacking crew without Walk (a Gatling crew) stops after its turn
// (Look with 1000: its machine's run stop 1440, then idle), and only after the 1440 clip requests 1600 from idle and
// strikes after the whole entry clip; off (the rule before 5074): 1600 at the turn's end and the strike 1.0 s later.
// A member with Walk and a 1450 clip, or without a 1440 clip, keeps the rule before 5074 to the frame.
C5R_TEST(p3_a_turned_crew_without_walk_stops_then_plays_its_whole_entry) {
    CHECK(CombatParams().attack_entry_after_turn);   // on by default (a parity fix)
    CHECK_NEAR(CombatParams().attack_turn_stop_s, 0.0, 1e-12);
    Clips clips;
    clips.t[1440] = Clips::timing(0.9f, {});
    const CombatInput in = record(0, 40, kCombatRanged);
    CombatMemberArt crew = rifle("G", 3);
    crew.enable.erase("Walk");
    const CombatUnitFigures A = facingWay(placed(unit(0, 1, {crew}), 0, 0), -1, 0);   // faces away: it turns
    const CombatUnitFigures D = facingWay(placed(unit(63, 2, {soldier("S", 6)}), 128, 0), -1, 0);
    CombatParams on, off;
    off.attack_entry_after_turn = false;
    const CombatResult r = choreograph(in, A, D, clips, on), r0 = choreograph(in, A, D, clips, off);
    CHECK(r.status.ok() && !r.at_limit && r0.status.ok() && !r0.at_limit);
    const double h = on.step;
    const double turn = kPi / (2 * kPi * 0.75);   // 180 degrees at 0.75 turns per second (4735's case)
    CHECK_EQ(countSteps(r, 0, FigState::Turn, 1400), 3u);
    CHECK_EQ(countSteps(r0, 0, FigState::Turn, 1400), 3u);
    CHECK_EQ(countSteps(r, 0, FigState::Look, 1000), 3u);    // the stop after the turn
    CHECK_EQ(countSteps(r0, 0, FigState::Look, 1000), 0u);
    for (auto& s : r.steps) {
        if (s.player != 0) continue;
        if (s.state == FigState::Look && s.code == 1000) CHECK(s.time >= turn - 1e-9 && s.time < turn + h + 1e-6);
        if (s.state == FigState::Ready && s.code == 1600) CHECK(s.time >= turn + 0.9 - 1e-9 && s.time < turn + 0.9 + 2 * h + 1e-6);
    }
    const double ready = firstStep(r, 0, FigState::Ready), ready0 = firstStep(r0, 0, FigState::Ready);
    CHECK(ready0 >= turn - 1e-9 && ready0 < turn + h + 1e-6);    // off: 1600 at the turn's end
    CHECK(firstStep(r, 0, FigState::Strike) >= ready + 1.0 - 1e-9);   // on: the whole entry clip from idle
    CHECK_NEAR(firstStep(r, 0, FigState::Strike) - firstStep(r0, 0, FigState::Strike), 0.9, 2 * h + 1e-6);
    CHECK(r.end > r0.end);
    CHECK_EQ(r.defender.died, r0.defender.died);
    CHECK_EQ(r.attacker.died, r0.attacker.died);
    // a turn shorter than attack_turn_stance_s (90 degrees: 0.333 s) keeps its run to 0.40 s, then stops
    CHECK_NEAR(CombatParams().attack_turn_stance_s, 0.40, 1e-12);
    {
        const CombatUnitFigures AQ = facingWay(placed(unit(0, 1, {crew}), 0, 0), 0, 1);
        const CombatResult x = choreograph(in, AQ, D, clips, on), x0 = choreograph(in, AQ, D, clips, off);
        CHECK(x.status.ok() && !x.at_limit && x0.status.ok());
        const double q = (kPi / 2) / (2 * kPi * 0.75);
        CHECK_NEAR(firstStep(x0, 0, FigState::Ready), q, h + 1e-6);   // off: at the turn's end
        for (auto& s : x.steps)
            if (s.player == 0 && s.state == FigState::Look && s.code == 1000) CHECK(s.time >= 0.40 - 1e-9 && s.time < 0.40 + h + 1e-6);
        const double rq = firstStep(x, 0, FigState::Ready);
        CHECK(rq >= 0.40 + 0.9 - 1e-9 && rq < 0.40 + 0.9 + 2 * h + 1e-6);
    }
    // a longer stand when attack_turn_stop_s exceeds the 1440 clip
    {
        CombatParams lng = on;
        lng.attack_turn_stop_s = 1.2;
        const CombatResult x = choreograph(in, A, D, clips, lng);
        CHECK(x.status.ok() && !x.at_limit);
        const double rx = firstStep(x, 0, FigState::Ready);
        CHECK(rx >= turn + 1.2 - 1e-9 && rx < turn + 1.2 + 2 * h + 1e-6);
    }
    // the rule before 5074 to the frame: a member with Walk and a 1450 clip; no 1440 clip; a crew that does not turn
    {
        Clips w = clips;
        w.t[1450] = Clips::timing(0.8f, {});
        const CombatUnitFigures AW = facingWay(placed(unit(0, 1, {rifle("R", 3)}), 0, 0), -1, 0);
        CHECK(sameResult(choreograph(in, AW, D, w, on), choreograph(in, AW, D, w, off)));
        Clips n;   // no 1440
        CHECK(sameResult(choreograph(in, A, D, n, on), choreograph(in, A, D, n, off)));
        const CombatUnitFigures AF = facingWay(placed(unit(0, 1, {crew}), 0, 0), 1, 0);   // faces its target
        CHECK(sameResult(choreograph(in, AF, D, clips, on), choreograph(in, AF, D, clips, off)));
    }
}

// ---- 5053 (unit-combat-server.md 18.26 (d)): the end after the deaths (CombatParams::death_by_variant, end_after_dying)
namespace {
// per figure of `player`: the time from its Dying step to its Removed step
std::map<uint16_t, double> dyingTimes(const CombatResult& r, int32_t player) {
    std::map<uint16_t, double> at, d;
    for (auto& s : r.steps) {
        if (s.player != player) continue;
        if (s.state == FigState::Dying) at[s.figure] = s.time;
        if (s.state == FigState::Removed && at.count(s.figure)) d[s.figure] = s.time - at[s.figure];
    }
    return d;
}
double lastStep(const CombatResult& r, int32_t player, FigState s) {
    double t = -1;
    for (auto& x : r.steps)
        if (x.player == player && x.state == s) t = std::max(t, x.time);
    return t;
}
// the controller's live death draw: the defender's even figures play B (2200), its odd ones A (1200)
class DeathFeedback : public CombatFeedback {
public:
    int asked = 0;
    bool accepts(int32_t, int32_t, uint16_t, int32_t) override { return true; }
    bool playing(int32_t, int32_t, uint16_t, int32_t) override { return true; }
    int32_t playingVariant(int32_t player, int32_t, uint16_t figure, int32_t code) override {
        ++asked;
        if (player != 63) return code;
        return figure % 2 == 0 ? code + 1000 : code;
    }
};
CombatResult runLive(const CombatInput& in, const CombatUnitFigures& a, const CombatUnitFigures& d, Clips& clips,
                     const CombatParams& p, CombatFeedback& fb) {
    CombatRun run;
    if (!run.start(in, a, d, clips, p, &fb).ok()) return CombatResult();
    std::vector<TimedEvent> out;
    bool end = false;
    for (int k = 0; k < 30 * 25 && !end; ++k) end = run.advance(1.0 / 30.0, out);
    return run.result();
}
}  // namespace

// D1 (R2): a dying figure lasts the death clip its machine drew. Off (the rule before 5053): every dying figure is
// removed after its member's 1200 clip. On (the default since the MC's word of 2026-11-25), self-timed: A or B 50/50
// from the core's own draw, the fight's other draws untouched (the same hits); live: the clip the controller says the
// machine plays. A member without a 2200 clip keeps A's length (the same fight as off). end_after_dying off in both
// arms, as written (W112), so the case checks death_by_variant alone.
C5R_TEST(d1_a_dying_figure_lasts_the_death_clip_its_machine_drew) {
    CHECK(CombatParams().death_by_variant);
    Clips clips;
    clips.t[1200] = Clips::timing(1.0f, {});
    clips.t[2200] = Clips::timing(1.8f, {});
    const CombatInput in = record(0, 60, kCombatRanged);
    const CombatUnitFigures A = facingWay(placed(unit(0, 1, {rifle("A", 3)}), 0, 0), 1, 0);
    const CombatUnitFigures D = facingWay(placed(unit(63, 2, {soldier("W", 6)}), 128, 0), -1, 0);
    CombatParams off, on;
    off.death_by_variant = false;
    off.end_after_dying = on.end_after_dying = false;   // W153: as written (both off before the MC's word)
    on.death_by_variant = true;
    const double tol = on.step + 1e-6;
    size_t na = 0, nb = 0;
    for (uint64_t seed = 1; seed <= 8; ++seed) {
        off.seed = on.seed = seed;
        const CombatResult r0 = choreograph(in, A, D, clips, off), r = choreograph(in, A, D, clips, on);
        CHECK(r0.status.ok() && !r0.at_limit && r.status.ok() && !r.at_limit);
        CHECK(r0.defender.died > 0 && r.defender.died == r0.defender.died);
        CHECK(sameHits(r, r0));   // the fight's own draws are untouched
        for (auto& kv : dyingTimes(r0, 63)) CHECK_NEAR(kv.second, 1.0, tol);
        for (auto& kv : dyingTimes(r, 63)) {
            if (std::fabs(kv.second - 1.0) <= tol) ++na;
            else if (std::fabs(kv.second - 1.8) <= tol) ++nb;
            else c5r::test::fail(__FILE__, __LINE__, "seed " + std::to_string(seed) + ": dying " + std::to_string(kv.second));
        }
    }
    CHECK(na > 0 && nb > 0);   // both clips drawn over the seeds
    // B is capped at death_cap
    {
        CombatParams capped = on;
        capped.death_cap = 1.5;
        Clips longB = clips;
        longB.t[2200] = Clips::timing(4.0f, {});
        size_t at_cap = 0;
        for (uint64_t seed = 1; seed <= 8; ++seed) {
            capped.seed = seed;
            const CombatResult r = choreograph(in, A, D, longB, capped);
            CHECK(r.status.ok() && !r.at_limit);
            for (auto& kv : dyingTimes(r, 63)) {
                CHECK(kv.second < 1.5 + tol);
                if (kv.second > 1.5 - 1e-9) ++at_cap;
            }
        }
        CHECK(at_cap > 0);
    }
    // no 2200 clip: A's length, the same fight as off
    {
        Clips noB;
        noB.t[1200] = Clips::timing(1.0f, {});
        off.seed = on.seed = 1;
        CHECK(sameResult(choreograph(in, A, D, noB, on), choreograph(in, A, D, noB, off)));
    }
    // live: the controller's draw decides; off never asks it
    {
        off.seed = on.seed = 1;
        DeathFeedback fb, fb0;
        const CombatResult l = runLive(in, A, D, clips, on, fb), l0 = runLive(in, A, D, clips, off, fb0);
        CHECK(l.status.ok() && !l.at_limit && l0.status.ok() && !l0.at_limit);
        CHECK(fb.asked > 0 && fb0.asked == 0);
        const std::map<uint16_t, double> dl = dyingTimes(l, 63), dl0 = dyingTimes(l0, 63);
        CHECK(!dl.empty() && !dl0.empty());
        for (auto& kv : dl) CHECK_NEAR(kv.second, kv.first % 2 == 0 ? 1.8 : 1.0, tol);
        for (auto& kv : dl0) CHECK_NEAR(kv.second, 1.0, tol);
    }
}

// E1 (R1 + R3): the standing figures wait for the dying and a land defender leaves 1280 with a shuffle. Off (the rule
// before 5053): the defender's standing figures end at the last landing, before its dying figures are gone. On: none
// ends before the last removal; each standing defender figure in 1280 requests 1450 (Done) and refaces (1000)
// end_stance_s later; the attacker is not held (R4). Without Walk: no shuffle, the end at the last removal. A melee kill
// (no standing figure of the killed side): the same fight as off. On is the default since the MC's word of 2026-11-25;
// death_by_variant off in both arms, as written (W112), so the case checks end_after_dying alone.
C5R_TEST(e1_the_standing_figures_wait_for_the_dying_then_a_defender_shuffles_out_of_1280) {
    CHECK(CombatParams().end_after_dying);
    CHECK_NEAR(CombatParams().end_stance_s, 0.45, 1e-12);
    Clips clips;
    clips.t[1450] = Clips::timing(0.8f, {});
    clips.t[1200] = Clips::timing(2.0f, {});
    const CombatInput in = record(0, 40, kCombatRanged);
    const CombatUnitFigures A = facingWay(placed(unit(0, 1, {rifle("A", 3)}), 0, 0), 1, 0);
    const CombatUnitFigures D = facingWay(placed(unit(63, 2, {soldier("W", 5)}), 128, 0), -1, 0);
    CombatParams off, on;
    off.end_after_dying = false;
    off.death_by_variant = on.death_by_variant = false;   // W153: as written (both off before the MC's word)
    on.end_after_dying = true;
    const double h = on.step;
    const CombatResult r0 = choreograph(in, A, D, clips, off), r = choreograph(in, A, D, clips, on);
    CHECK(r0.status.ok() && !r0.at_limit && r.status.ok() && !r.at_limit);
    CHECK(r.defender.died > 0 && r.defender.died == r0.defender.died && !r.defender.killed);
    CHECK(sameHits(r, r0));
    CHECK(countSteps(r0, 63, FigState::Defend, 1280) > 0);   // the target stands in bombard defend
    // off: a standing figure ends before the last dying figure is removed; no shuffle
    CHECK(firstStep(r0, 63, FigState::Done) >= 0 && firstStep(r0, 63, FigState::Done) < lastStep(r0, 63, FigState::Removed));
    CHECK_EQ(countSteps(r0, 63, FigState::Done, 1450), 0u);
    // on: the release no earlier than the last removal, one 1450 per standing figure, the end end_stance_s later
    const double removed = lastStep(r, 63, FigState::Removed);
    const size_t standing = static_cast<size_t>(r.defender.standing - r.defender.died);
    CHECK(removed > 0 && standing > 0);
    CHECK(firstStep(r, 63, FigState::Done) >= removed - 1e-9);
    CHECK_EQ(countSteps(r, 63, FigState::Done, 1450), standing);
    CHECK_EQ(countSteps(r, 63, FigState::Reface, 1000), standing);
    CHECK(firstStep(r, 63, FigState::Reface) >= removed + 0.45 - 1e-9);
    CHECK(r.end >= removed + 0.45 - 1e-9 && r.end < removed + 0.45 + 2 * h + 1e-6);
    CHECK(r.end > r0.end);
    bool walk = false;
    for (auto& e : r.events)
        walk = walk || (e.time >= removed - 1e-9 && e.ev.kind == EventKind::Action && e.ev.player == 63 &&
                        e.ev.action == ActionKind::Walk);
    CHECK(walk);
    // the attacker is not held (R4): its figures end as before
    CHECK_NEAR(firstStep(r, 0, FigState::Done), firstStep(r0, 0, FigState::Done), 1e-9);
    // without Walk: no shuffle, the standing figures end at the last removal
    {
        CombatMemberArt noWalk = soldier("W", 5);
        noWalk.enable.erase("Walk");
        const CombatUnitFigures DN = facingWay(placed(unit(63, 2, {noWalk}), 128, 0), -1, 0);
        const CombatResult n = choreograph(in, A, DN, clips, on), n0 = choreograph(in, A, DN, clips, off);
        CHECK(n.status.ok() && !n.at_limit && n0.status.ok());
        CHECK_EQ(countSteps(n, 63, FigState::Done, 1450), 0u);
        const double nr = lastStep(n, 63, FigState::Removed);
        CHECK(nr > 0 && firstStep(n, 63, FigState::Done) >= nr - 1e-9 && firstStep(n, 63, FigState::Done) < nr + h + 1e-6);
        CHECK(firstStep(n0, 63, FigState::Done) < lastStep(n0, 63, FigState::Removed));
    }
    // a melee kill: no standing figure of the killed side is left to hold; the same fight as off
    {
        const CombatInput mi = record(20, 100, kCombatDefenderRetaliates);
        const CombatResult m = choreograph(mi, A, D, clips, on), m0 = choreograph(mi, A, D, clips, off);
        CHECK(m.status.ok() && m0.status.ok() && m0.defender.killed);
        CHECK(sameResult(m, m0));
    }
}

C5R_TEST_MAIN
