// render/tests/units/test_fight_motion.cpp - issue 4776 (epic 0042): a fight's figure motion. Synthetic data only (an
// invented formation file, a synthetic height field, invented members and clip timings). Covers placement.h's
// applyFigureOffsets, the choreography core's approach by distance (combat.h with positions), motion.h's FightMotion
// (poses, headings, the return and the reform) and FightPlacement (no change with no fight). Each check with a positive
// and a negative case where the function can fail.
#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "c5r/heightfield/heightfield.h"
#include "c5r/unitanim/combat.h"
#include "c5r/unitanim/motion.h"
#include "c5r/units/placement.h"
#include "c5r/units/units.h"
#include "testkit.h"

using namespace c5r;
using namespace c5r::unitanim;

namespace {

const char* kXml =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
    "<Formations>\n"
    "  <Formation><Name>Line</Name><SpacingX>8</SpacingX><SpacingY>9</SpacingY><UnitRandomness>0</UnitRandomness>"
    "<TotalWidth>4</TotalWidth><YOffsetCurved>0</YOffsetCurved><YOffsetInc>0</YOffsetInc></Formation>\n"
    "</Formations>\n";

units::FormationTable table() {
    units::FormationTable t;
    Status st = t.parse(kXml);
    if (!st.ok()) ::c5r::test::fail(__FILE__, __LINE__, "table(): " + st.str());
    return t;
}

// A 6 x 4 plot map, heights rising with x (so a move changes the height), the EXE's plot spacing.
heightfield::HeightGrid grid() {
    heightfield::HeightGrid g;
    g.mapW = 6;
    g.mapH = 8;
    g.samplesX = g.mapW * 64;
    g.samplesY = g.mapH * 64;
    g.ox = static_cast<float>(g.mapW * 32);
    g.oy = static_cast<float>(g.mapH * 32) * g.sy;
    g.waterZ = 0;
    const float m[16] = {64, 0, 0, 0, 32, 64, 0, 0, 0, 0, 1, 0, 32, 128, 0, 1};
    for (int k = 0; k < 16; ++k) g.hexToWorld[k] = m[k];
    g.h.resize(static_cast<size_t>(g.samplesX) * g.samplesY);
    for (uint32_t y = 0; y < g.samplesY; ++y)
        for (uint32_t x = 0; x < g.samplesX; ++x) g.h[g.index(x, y)] = static_cast<uint16_t>(x * 10);
    return g;
}

bool approxEq(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

units::PlacementInput input(int x, int y, int facing, size_t n) {
    units::PlacementInput in;
    in.formation = "Line";
    in.zOffset.assign(n, 0.5f);
    in.plotX = x;
    in.plotY = y;
    in.facing = facing;
    in.seed = 7;
    return in;
}

std::set<std::string> words(const char* s) {
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
    m.weapons.push_back(CombatWeapon());
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
class Clips : public CombatClips {
public:
    std::map<int32_t, ActionTiming> t;
    Clips() {
        t[1100] = timing(1.0f, {0.4889f});
        t[1200] = timing(2.5f, {});
        for (int32_t c : {1000, 1120, 1280, 1400, 1440, 1600, 1800}) t[c] = timing(1.0f, {});
        t[1440] = timing(0.5f, {});
    }
    static ActionTiming timing(float d, std::vector<float> hits) {
        ActionTiming a;
        a.found = true;
        a.duration = d;
        a.hits = hits;
        return a;
    }
    ActionTiming timing(int32_t, int32_t, const CombatMemberArt& m, int32_t code) override {
        ActionTiming a;
        if (!t.count(code)) {
            a.why = m.member + " code " + std::to_string(code) + ": not in the test";
            return a;
        }
        return t[code];
    }
};
CombatInput melee(int32_t afinal, int32_t dfinal) {
    CombatInput in;
    in.seq = 9;
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

// The two units of a fight on adjacent plots (1, 1) and (2, 1), facing each other, with their start positions.
struct Pair {
    heightfield::HeightGrid g = grid();
    units::FormationTable t = table();
    CombatUnitFigures a, d;
    units::PlacementInput pa, pd;
    Pair(int na, int nd) {
        a = unit(0, 1, {soldier("A", na)});
        d = unit(63, 2, {soldier("D", nd)});
        pa = input(1, 1, 1, static_cast<size_t>(na));   // facing 1 (east): towards the defender
        pd = input(2, 1, 4, static_cast<size_t>(nd));   // facing 4 (west)
        Status s = combatPositions(t, g, pa, a);
        if (s.ok()) s = combatPositions(t, g, pd, d);
        if (!s.ok()) ::c5r::test::fail(__FILE__, __LINE__, "Pair: " + s.str());
    }
};

}  // namespace

// ---- placement.h: applyFigureOffsets ----

C5R_TEST(offsets_empty_change_nothing) {
    heightfield::HeightGrid g = grid();
    units::UnitPlacement p, q;
    CHECK_OK(units::placeUnit(table(), g, input(2, 1, 2, 4), p));
    q = p;
    CHECK_OK(units::applyFigureOffsets(g, 0, {}, q));
    CHECK_EQ(q.placed.size(), p.placed.size());
    for (size_t i = 0; i < p.placed.size(); ++i) {
        CHECK(q.placed[i].x == p.placed[i].x && q.placed[i].y == p.placed[i].y && q.placed[i].z == p.placed[i].z);
        CHECK(q.placed[i].yaw == p.placed[i].yaw);
    }
}

C5R_TEST(offsets_move_in_the_unit_frame_and_resample_the_height) {
    heightfield::HeightGrid g = grid();
    units::UnitPlacement p;
    CHECK_OK(units::placeUnit(table(), g, input(2, 1, 2, 4), p));
    units::UnitPlacement q = p;
    units::FigureOffset o;
    o.figure = 1;
    o.dx = 3;
    o.dy = -10;   // ten units towards the unit's front (its local -y)
    o.hasYaw = true;
    o.yaw = 0.25f;
    CHECK_OK(units::applyFigureOffsets(g, 0, {o}, q));
    const float c = std::cos(p.yaw), s = std::sin(p.yaw);
    CHECK(approxEq(q.placed[1].x, p.placed[1].x + c * 3 - s * -10, 1e-4));
    CHECK(approxEq(q.placed[1].y, p.placed[1].y + s * 3 + c * -10, 1e-4));
    // the ZOffset part (0.5) is kept over the new spot's height
    CHECK(approxEq(q.placed[1].z, units::heightAtWorld(g, q.placed[1].x, q.placed[1].y) + 0.5f, 1e-4));
    CHECK(q.placed[1].z != p.placed[1].z);
    CHECK(approxEq(q.placed[1].yaw, 0.25, 1e-6));
    CHECK(q.placed[0].x == p.placed[0].x && q.placed[0].yaw == p.placed[0].yaw);   // others unchanged
    // the water level is the floor at both ends
    units::UnitPlacement w = p;
    CHECK_OK(units::applyFigureOffsets(g, 2000.f, {o}, w));
    CHECK(approxEq(w.placed[1].z, p.placed[1].z, 1e-3));
}

C5R_TEST(offsets_negative) {
    heightfield::HeightGrid g = grid();
    units::UnitPlacement p;
    units::PlacementInput in = input(2, 1, 2, 4);
    in.damage = 50;   // two of four living: figures 2 and 3 are not placed
    CHECK_OK(units::placeUnit(table(), g, in, p));
    CHECK_EQ(p.placed.size(), static_cast<size_t>(2));
    units::UnitPlacement q = p;
    units::FigureOffset o;
    o.figure = 3;
    CHECK_ERR(units::applyFigureOffsets(g, 0, {o}, q), Err::BadLayout);
    o.figure = 0;
    o.dx = std::nanf("");
    CHECK_ERR(units::applyFigureOffsets(g, 0, {o}, q), Err::BadLayout);
    o.dx = 1;
    o.hasYaw = true;
    o.yaw = INFINITY;
    CHECK_ERR(units::applyFigureOffsets(g, 0, {o}, q), Err::BadLayout);
    CHECK(q.placed[0].x == p.placed[0].x);   // a failure leaves it unchanged
}

// ---- combat.h: the approach by distance ----

C5R_TEST(approach_lasts_its_distance) {
    Pair pr(4, 4);
    Clips clips;
    CombatParams prm;
    prm.seed = 3;
    prm.walk_home = false;   // 4742: the approach moves only (the walk home inside the fight adds its own, 18.18)
    prm.melee_reach = false;   // 0743: these measure the rule before 0743 (MoveRadius short of the target figure)
    prm.defender_step = false;   // 5061: the attacker's approach only (the type 2 defender stands, the rule before 5061)
    CombatResult r = choreograph(melee(10, 50), pr.a, pr.d, clips, prm);
    CHECK(r.status.ok());
    CHECK(!r.moves.empty());
    for (auto& m : r.moves) {
        CHECK_EQ(m.player, 0);   // only the attacker moves
        const double d = std::hypot(m.x1 - m.x0, m.y1 - m.y0);
        const double speed = (m.code == 1450 ? 0.78125 : 0.78125) * 64.0;
        CHECK(approxEq(m.t1 - m.t0, d / speed, 1e-4));
        // it stops MoveRadius (3.5) short of its target figure's start
        bool hit = false;
        for (size_t i = 0; i < pr.d.standing.size(); ++i)
            if (approxEq(std::hypot(pr.d.x[i] - m.x1, pr.d.y[i] - m.y1), 3.5, 1e-3)) hit = true;
        CHECK(hit);
    }
    // the Approach step lasts the move: the first move of figure f starts at f's Approach step
    for (auto& m : r.moves) {
        bool step = false;
        for (auto& s : r.steps)
            if (s.player == 0 && s.figure == m.figure && s.state == FigState::Approach && approxEq(s.time, m.t0, 1e-9))
                step = true;
        CHECK(step);
    }
}

C5R_TEST(approach_rates_and_radii_from_the_member) {
    Pair pr(1, 1);
    Clips clips;
    CombatParams prm;
    prm.walk_home = false;   // 4742: the approach moves only (the walk home inside the fight adds its own, 18.18)
    prm.melee_reach = false;   // 0743: these measure the rule before 0743 (MoveRadius short of the target figure)
    prm.defender_step = false;   // 5061: the attacker's approach only (the type 2 defender stands, the rule before 5061)
    CombatResult base = choreograph(melee(10, 50), pr.a, pr.d, clips, prm);
    CHECK(base.status.ok() && base.moves.size() == 1);
    const FigureMove m0 = base.moves[0];
    const double d0 = std::hypot(pr.d.x[0] - pr.a.x[0], pr.d.y[0] - pr.a.y[0]);
    // twice the MoveRate: half the time
    pr.a.members[0].motion.move_rate = 1.5625f;
    CombatResult fast = choreograph(melee(10, 50), pr.a, pr.d, clips, prm);
    CHECK(fast.moves.size() == 1 && approxEq(fast.moves[0].t1 - fast.moves[0].t0, (m0.t1 - m0.t0) / 2, 1e-4));
    // a ShortMoveRadius beyond the distance: a shuffle (1450) at ShortMoveRate
    pr.a.members[0].motion.move_rate = 0.78125f;
    pr.a.members[0].motion.short_move_radius = static_cast<float>(d0 + 1);
    pr.a.members[0].motion.short_move_rate = 0.35f;
    CombatResult sh = choreograph(melee(10, 50), pr.a, pr.d, clips, prm);
    CHECK(sh.moves.size() == 1 && sh.moves[0].code == 1450);
    CHECK(approxEq(sh.moves[0].t1 - sh.moves[0].t0, (d0 - 3.5) / (0.35 * 64.0), 1e-4));
    // a MoveRadius beyond the distance: no move, the figure is engaged at once
    pr.a.members[0].motion.short_move_radius = 0;
    pr.a.members[0].motion.move_radius = static_cast<float>(d0 + 1);
    CombatResult in = choreograph(melee(10, 50), pr.a, pr.d, clips, prm);
    CHECK(in.status.ok() && in.moves.empty());
    CHECK(in.end < base.end);
}

// 0743 (CombatParams::melee_reach, unit-combat-server.md 18.44): stock's melee figure strikes from within its AttackRadius
// plus its target's (8 + 8 = 16 by default) of the nearest opponent figure [C old build]; route S ran to MoveRadius (3.5)
// of a doomed figure at the back of the defender (the owner's beta 4 report: the attackers run much further than stock)
static double nearestStanding(const CombatUnitFigures& d, double x, double y) {
    double best = 1e30;
    for (size_t i = 0; i < d.standing.size(); ++i) best = std::min(best, std::hypot(d.x[i] - x, d.y[i] - y));
    return best;
}
static const FigureMove* firstMove(const CombatResult& r, uint16_t fig) {
    for (auto& m : r.moves)
        if (m.player == 0 && m.figure == fig) return &m;
    return nullptr;
}

C5R_TEST(melee_reach_stops_at_the_two_attack_radii_of_the_nearest_figure) {
    Pair pr(12, 12);
    Clips clips;
    CombatParams on;
    on.seed = 3;
    on.walk_home = false;   // 4742: the approach moves only
    CHECK(on.melee_reach);  // on by default
    CombatParams off = on;
    off.melee_reach = false;
    CombatResult rOn = choreograph(melee(20, 35), pr.a, pr.d, clips, on);
    CombatResult rOff = choreograph(melee(20, 35), pr.a, pr.d, clips, off);
    CHECK(rOn.status.ok() && rOff.status.ok() && !rOn.moves.empty() && !rOff.moves.empty());
    double runOn = 0, runOff = 0;
    for (auto& m : rOn.moves) {
        if (m.player != 0 || firstMove(rOn, m.figure) != &m) continue;
        // on: the run ends 16 units from the nearest standing defender figure (none nearer), as stock's 0xC -> 10
        CHECK(approxEq(nearestStanding(pr.d, m.x1, m.y1), 16.0, 1e-3));
        runOn = std::max(runOn, static_cast<double>(std::hypot(m.x1 - m.x0, m.y1 - m.y0)));
    }
    for (auto& m : rOff.moves) {
        if (m.player != 0 || firstMove(rOff, m.figure) != &m) continue;
        // off, the rule before 0743: MoveRadius (3.5) short of its target figure
        bool hit = false;
        for (size_t i = 0; i < pr.d.standing.size(); ++i)
            if (approxEq(std::hypot(pr.d.x[i] - m.x1, pr.d.y[i] - m.y1), 3.5, 1e-3)) hit = true;
        CHECK(hit);
        runOff = std::max(runOff, static_cast<double>(std::hypot(m.x1 - m.x0, m.y1 - m.y0)));
    }
    CHECK(runOn + 12.5 < runOff);   // at least the reach's difference shorter (16 - 3.5)
    // the reach is the two members' AttackRadius values
    Pair ps(12, 12);
    for (auto& m : ps.a.members) m.motion.attack_radius = 4.0f;
    for (auto& m : ps.d.members) m.motion.attack_radius = 6.0f;
    CombatResult rS = choreograph(melee(20, 35), ps.a, ps.d, clips, on);
    CHECK(rS.status.ok() && !rS.moves.empty());
    for (auto& m : rS.moves)
        if (m.player == 0 && firstMove(rS, m.figure) == &m) CHECK(approxEq(nearestStanding(ps.d, m.x1, m.y1), 10.0, 1e-3));
}

C5R_TEST(melee_reach_targets_the_nearest_candidate) {
    // a kill: every defender figure is doomed, so the first striker's candidates are all of them; on, it aims at the
    // nearest (stock's chooser 0x6F8DC0 [C]); its move runs straight at that figure
    Pair pr(4, 12);
    Clips clips;
    CombatParams on;
    on.seed = 5;
    on.walk_home = false;
    CombatResult r = choreograph(melee(10, 100), pr.a, pr.d, clips, on);
    CHECK(r.status.ok() && !r.moves.empty());
    const FigureMove* m = nullptr;
    for (auto& x : r.moves)
        if (x.player == 0 && (!m || x.t0 < m->t0)) m = &x;
    CHECK(m != nullptr);
    if (!m) return;
    size_t ni = 0;
    for (size_t i = 1; i < pr.d.standing.size(); ++i)
        if (std::hypot(pr.d.x[i] - m->x0, pr.d.y[i] - m->y0) < std::hypot(pr.d.x[ni] - m->x0, pr.d.y[ni] - m->y0)) ni = i;
    const double ux = m->x1 - m->x0, uy = m->y1 - m->y0, vx = pr.d.x[ni] - m->x0, vy = pr.d.y[ni] - m->y0;
    const double cross = (ux * vy - uy * vx) / (std::hypot(ux, uy) * std::hypot(vx, vy));
    CHECK(std::fabs(cross) < 1e-3 && ux * vx + uy * vy > 0);   // towards the nearest defender figure
}

C5R_TEST(melee_reach_leaves_riders_and_ships_alone) {
    // a rush member (RushAttackFormation: stock's riders charge to the target's slots and ride through, 5057 (A)) and a
    // ship keep the rule before 0743 byte for byte, as the knob off does
    Clips clips;
    CombatParams on;
    on.seed = 3;
    on.walk_home = false;
    CombatParams off = on;
    off.melee_reach = false;
    for (int kind = 0; kind < 2; ++kind) {
        Pair pr(5, 12);
        if (kind == 0) pr.a.members[0].rush = true;
        else pr.a.members[0].domain = "Sea";
        CombatResult a = choreograph(melee(15, 40), pr.a, pr.d, clips, on);
        CombatResult b = choreograph(melee(15, 40), pr.a, pr.d, clips, off);
        CHECK(a.status.ok() && b.status.ok());
        CHECK_EQ(a.moves.size(), b.moves.size());
        for (size_t i = 0; i < a.moves.size() && i < b.moves.size(); ++i)
            CHECK(a.moves[i].x1 == b.moves[i].x1 && a.moves[i].y1 == b.moves[i].y1 && a.moves[i].t1 == b.moves[i].t1);
        CHECK(a.end == b.end);
    }
}

C5R_TEST(no_positions_keeps_the_fixed_approach) {
    Pair pr(4, 4);
    pr.a.x.clear();
    pr.a.y.clear();
    pr.d.x.clear();
    pr.d.y.clear();
    Clips clips;
    CombatParams prm;
    CombatResult r = choreograph(melee(10, 50), pr.a, pr.d, clips, prm);
    CHECK(r.status.ok() && r.moves.empty());
    for (auto& s : r.steps)
        if (s.state == FigState::Approach) CHECK(approxEq(s.time, 0, 1e-9));
    // a projectile-weapon attacker approaches with positions (4776) and, since 4738 (unplaced_attacker_approaches, the
    // EXE's rule of 4776 M3), without them too, for the fixed approach time and with no move; the knob off keeps 4775's
    // rule (no approach without positions)
    Pair pp(2, 2);
    pp.a.members[0].weapons[0].projectile = true;
    CombatResult withPos = choreograph(melee(10, 50), pp.a, pp.d, clips, prm);
    CHECK(withPos.status.ok() && !withPos.moves.empty());
    pp.a.x.clear();
    pp.a.y.clear();
    pp.d.x.clear();
    pp.d.y.clear();
    for (int on = 0; on < 2; ++on) {
        CombatParams pk = prm;
        pk.unplaced_attacker_approaches = on != 0;
        CombatResult noPos = choreograph(melee(10, 50), pp.a, pp.d, clips, pk);
        CHECK(noPos.status.ok() && noPos.moves.empty());
        size_t approaches = 0;
        for (auto& s : noPos.steps) approaches += s.state == FigState::Approach ? 1 : 0;
        if (on) CHECK(approaches > 0);
        else CHECK_EQ(approaches, static_cast<size_t>(0));
    }
}

C5R_TEST(positions_negative) {
    Pair pr(4, 4);
    Clips clips;
    CombatParams prm;
    pr.a.x.pop_back();
    CombatResult r = choreograph(melee(10, 50), pr.a, pr.d, clips, prm);
    CHECK(r.status.code == CombatCode::BadInput);
    Pair pn(4, 4);
    pn.d.y[1] = std::nanf("");
    r = choreograph(melee(10, 50), pn.a, pn.d, clips, prm);
    CHECK(r.status.code == CombatCode::BadInput);
    // the damage rule does not matter (every figure in its slot); a standing figure beyond the unit's figures does
    Pair pz(4, 4);
    pz.pa.damage = 50;
    CombatUnitFigures a = unit(0, 1, {soldier("A", 4)});
    CHECK_OK(combatPositions(pz.t, pz.g, pz.pa, a));
    CHECK(a.x.size() == 4 && a.x[3] == pz.a.x[3] && a.y[3] == pz.a.y[3]);
    a.standing.push_back(4);
    CHECK_ERR(combatPositions(pz.t, pz.g, pz.pa, a), Err::BadLayout);
    CHECK(a.x.empty());
}

// ---- motion.h: FightMotion ----

C5R_TEST(motion_follows_the_moves_and_faces_its_way) {
    Pair pr(4, 4);
    Clips clips;
    CombatParams prm;
    CombatResult r = choreograph(melee(10, 50), pr.a, pr.d, clips, prm);
    FightMotion fm;
    CHECK_OK(fm.build(r, pr.a, pr.d));
    CHECK(fm.hasUnit(0, 1) && fm.hasUnit(63, 2) && !fm.hasUnit(5, 5));
    const FigureMove& m = r.moves[0];
    FigurePose p;
    CHECK(fm.pose(m.t0 + (m.t1 - m.t0) / 2, m.player, m.id, m.figure, p));
    CHECK(p.moving);
    CHECK(approxEq(p.x, (m.x0 + m.x1) / 2, 1e-3) && approxEq(p.y, (m.y0 + m.y1) / 2, 1e-3));
    CHECK(approxEq(p.yaw, yawFacing(m.x1 - m.x0, m.y1 - m.y0), 1e-5));
    // facing: yaw a faces (sin a, -cos a)
    CHECK(approxEq(std::sin(yawFacing(3, 4)), 0.6, 1e-5) && approxEq(-std::cos(yawFacing(3, 4)), 0.8, 1e-5));
    CHECK(fm.pose(m.t1 + 0.01, m.player, m.id, m.figure, p));
    CHECK(!p.moving && approxEq(p.x, m.x1, 1e-4) && approxEq(p.y, m.y1, 1e-4));
    // a defender figure stands where it started and faces something on the attacker's side (east of it is the
    // defender's own side: the attacker is to the west)
    CHECK(fm.pose(m.t1, 63, 2, 0, p));
    CHECK(approxEq(p.x, pr.d.x[0], 1e-4) && approxEq(p.y, pr.d.y[0], 1e-4));
    CHECK(std::sin(p.yaw) < 0);
    CHECK(!fm.pose(0, 63, 2, 9, p));   // not a figure of the fight
    // negatives
    FightMotion bad;
    CombatResult refused;
    refused.status.code = CombatCode::QuickCombat;
    CHECK_ERR(bad.build(refused, pr.a, pr.d), Err::BadLayout);
    CombatUnitFigures noPos = pr.a;
    noPos.x.clear();
    CHECK_ERR(bad.build(r, noPos, pr.d), Err::BadLayout);
}

C5R_TEST(motion_offsets_return_and_reform) {
    Pair pr(4, 4);
    Clips clips;
    CombatParams prm;
    prm.walk_home = false;   // 4742: the walk after the end (the rule before 4742; with the walk inside, see below)
    CombatResult r = choreograph(melee(50, 25), pr.a, pr.d, clips, prm);   // the attacker loses 2 of 4
    CHECK(r.status.ok() && r.attacker.died == 2);
    FightMotion fm;
    CHECK_OK(fm.build(r, pr.a, pr.d));
    // during the fight: every placed figure at its pose
    units::UnitPlacement during;
    CHECK_OK(units::placeUnit(pr.t, pr.g, pr.pa, during));   // damage held: all four placed
    std::vector<units::FigureOffset> offs;
    const double tm = r.moves[0].t1;
    CHECK(fm.offsets(tm, 0, 1, during, offs));
    CHECK_EQ(offs.size(), static_cast<size_t>(4));
    units::UnitPlacement moved = during;
    CHECK_OK(units::applyFigureOffsets(pr.g, 0, offs, moved));
    for (auto& fp : moved.placed) {
        FigurePose p;
        CHECK(fm.pose(tm, 0, 1, fp.figure, p));
        CHECK(approxEq(fp.x, p.x, 1e-3) && approxEq(fp.y, p.y, 1e-3) && approxEq(fp.yaw, p.yaw, 1e-5));
    }
    // after the end the damage is applied: two placed; the survivors (figures 0, 1 under LastFirst) walk back
    units::PlacementInput after = pr.pa;
    after.damage = 50;
    units::UnitPlacement p2;
    CHECK_OK(units::placeUnit(pr.t, pr.g, after, p2));
    CHECK_EQ(p2.placed.size(), static_cast<size_t>(2));
    double farthest = 0;
    for (auto& fp : p2.placed) {
        FigurePose e;
        CHECK(fm.pose(r.end, 0, 1, fp.figure, e));
        farthest = std::max(farthest, static_cast<double>(std::hypot(e.x - fp.x, e.y - fp.y)));
    }
    CHECK(farthest > 1);
    CHECK(fm.offsets(r.end + 1e-3, 0, 1, p2, offs));
    CHECK(!offs.empty());
    // the walk shortens with time and ends at the slot (no offset), at MoveRate x 64
    const double back = farthest / 50.0;
    std::vector<units::FigureOffset> later;
    CHECK(fm.offsets(r.end + back * 0.5, 0, 1, p2, later));
    double l0 = 0, l1 = 0;
    for (auto& o : offs) l0 = std::max(l0, static_cast<double>(std::hypot(o.dx, o.dy)));
    for (auto& o : later) l1 = std::max(l1, static_cast<double>(std::hypot(o.dx, o.dy)));
    CHECK(l1 < l0);
    CHECK(!fm.offsets(r.end + back + 0.01, 0, 1, p2, later));
    CHECK(later.empty());
    CHECK(!fm.offsets(1.0, 7, 7, p2, later));   // not a unit of the fight
    // 4742 (CombatParams::walk_home, the default): the survivors walk home inside the fight, to the slots the placement
    // after the damage gives them (the module's own layout): at the fight's end they stand there, nothing walks after it
    CombatParams on;
    const CombatResult rw = choreograph(melee(50, 25), pr.a, pr.d, clips, on);
    CHECK(rw.status.ok() && rw.attacker.died == 2 && !rw.at_limit);
    CHECK(rw.end >= r.end - 1e-9);
    FightMotion fw;
    CHECK_OK(fw.build(rw, pr.a, pr.d));
    double off = 0;
    for (auto& fp : p2.placed) {
        FigurePose e;
        CHECK(fw.pose(rw.end, 0, 1, fp.figure, e));
        off = std::max(off, static_cast<double>(std::hypot(e.x - fp.x, e.y - fp.y)));
        CHECK(!e.yaw_known);   // refaced: the placement's heading
    }
    CHECK(off < 1e-3);
    CHECK(!fw.offsets(rw.end + 1e-3, 0, 1, p2, later));
    CHECK(later.empty());
}

C5R_TEST(fight_placement_no_fight_no_change) {
    struct Inner : units::PlacementSource {
        units::FormationTable t = table();
        units::UnitStatus place(const units::PlacementRequest& r, units::UnitPlacement& out) override {
            units::PlacementInput in = input(r.unit->plot_x, r.unit->plot_y, r.unit->facing, 4);
            in.damage = r.unit->damage;
            Status s = units::placeUnit(t, *r.grid, in, out);
            return s.ok() ? units::UnitStatus::Ok() : units::UnitStatus::fail(units::UnitCode::PlacementFailed, s.str());
        }
    } inner;
    Pair pr(4, 4);
    Clips clips;
    CombatParams prm;
    CombatResult r = choreograph(melee(10, 50), pr.a, pr.d, clips, prm);
    auto fm = std::make_shared<FightMotion>();
    CHECK_OK(fm->build(r, pr.a, pr.d));
    FightPlacement fp(&inner);
    units::UnitInput u;
    u.player = 0;
    u.id = 1;
    u.plot_x = 1;
    u.plot_y = 1;
    u.facing = 1;
    units::PlacementRequest rq;
    rq.unit = &u;
    rq.grid = &pr.g;
    units::UnitPlacement a, b;
    CHECK(inner.place(rq, a).ok());
    CHECK(fp.place(rq, b).ok());   // no fight set: the inner placement exactly
    CHECK_EQ(a.placed.size(), b.placed.size());
    for (size_t i = 0; i < a.placed.size(); ++i)
        CHECK(a.placed[i].x == b.placed[i].x && a.placed[i].y == b.placed[i].y && a.placed[i].z == b.placed[i].z &&
              a.placed[i].yaw == b.placed[i].yaw);
    fp.set(0, 1, fm, r.moves[0].t1);
    CHECK(fp.place(rq, b).ok());
    CHECK_EQ(fp.moved(), static_cast<uint64_t>(1));
    bool differs = false;
    for (size_t i = 0; i < a.placed.size(); ++i) differs = differs || a.placed[i].x != b.placed[i].x;
    CHECK(differs);
    fp.clear(0, 1);
    CHECK(fp.place(rq, b).ok());
    for (size_t i = 0; i < a.placed.size(); ++i) CHECK(a.placed[i].x == b.placed[i].x);
    // negatives: no inner source; no height field with a fight set
    FightPlacement none;
    CHECK(none.place(rq, b).code == units::UnitCode::PlacementFailed);
    fp.set(0, 1, fm, r.moves[0].t1);
    units::PlacementRequest nogrid = rq;
    nogrid.grid = nullptr;
    struct NoGrid : units::PlacementSource {
        units::UnitStatus place(const units::PlacementRequest&, units::UnitPlacement& out) override {
            out = units::UnitPlacement();
            units::FigurePlacement f;
            f.figure = 0;
            out.placed.push_back(f);
            return units::UnitStatus::Ok();
        }
    } ng;
    FightPlacement fp2(&ng);
    fp2.set(0, 1, fm, r.moves[0].t1);
    CHECK(fp2.place(nogrid, b).code == units::UnitCode::PlacementFailed);
}

// issue 4982 (lead-0042 #11560 (1)): a unit FightPlacement holds stands on the EXE's floor for the whole fight and its
// settled tail, whatever foot radius (FixUnitPlacement) its input carries; without a fight the foot radius applies.
C5R_TEST(fight_placement_held_keeps_the_stock_floor) {
    struct Inner : units::PlacementSource {   // as draw_steps.cpp: the unit's foot radius into the placement
        units::FormationTable t = table();
        units::UnitStatus place(const units::PlacementRequest& r, units::UnitPlacement& out) override {
            units::PlacementInput in = input(r.unit->plot_x, r.unit->plot_y, r.unit->facing, 4);
            in.damage = r.unit->damage;
            in.footRadius = r.unit->foot_radius;
            Status s = units::placeUnit(t, *r.grid, in, out);
            return s.ok() ? units::UnitStatus::Ok() : units::UnitStatus::fail(units::UnitCode::PlacementFailed, s.str());
        }
    } inner;
    Pair pr(4, 4);   // the grid rises with x: every figure stands on a slope
    Clips clips;
    CombatParams prm;
    CombatResult r = choreograph(melee(10, 50), pr.a, pr.d, clips, prm);
    CHECK(r.status.ok());
    auto fm = std::make_shared<FightMotion>();
    CHECK_OK(fm->build(r, pr.a, pr.d));
    units::UnitInput stock, feet;
    stock.player = feet.player = 0;
    stock.id = feet.id = 1;
    stock.plot_x = feet.plot_x = 1;
    stock.plot_y = feet.plot_y = 1;
    stock.facing = feet.facing = 1;
    feet.foot_radius = 3.0f;
    units::PlacementRequest rs, rf;
    rs.unit = &stock;
    rf.unit = &feet;
    rs.grid = rf.grid = &pr.g;
    auto same = [](const units::UnitPlacement& a, const units::UnitPlacement& b) {
        if (a.placed.size() != b.placed.size()) return false;
        for (size_t i = 0; i < a.placed.size(); ++i)
            if (a.placed[i].x != b.placed[i].x || a.placed[i].y != b.placed[i].y || a.placed[i].z != b.placed[i].z ||
                a.placed[i].yaw != b.placed[i].yaw)
                return false;
        return true;
    };
    FightPlacement fp(&inner);
    units::UnitPlacement a, b;
    // no fight: the foot radius raises the figures on the slope
    CHECK(fp.place(rs, a).ok() && fp.place(rf, b).ok());
    CHECK(!same(a, b));
    bool higher = true;
    for (size_t i = 0; i < a.placed.size(); ++i) higher = higher && b.placed[i].z > a.placed[i].z;
    CHECK(higher);
    // held: at every sampled time of the fight, then its settled tail, byte-equal to the input without a foot radius
    const double times[] = {0.0, r.moves[0].t1 * 0.5, r.moves[0].t1, r.end * 0.75, r.end, r.end + 0.5, r.end + 60.0};
    for (double tm : times) {
        fp.set(0, 1, fm, tm);
        CHECK(fp.place(rs, a).ok() && fp.place(rf, b).ok());
        CHECK(same(a, b));
    }
    CHECK(fp.settled() > 0);   // the tail was reached (no offsets, the inner placement only)
    CHECK(feet.foot_radius == 3.0f);   // the caller's input is not changed
    // released: the foot radius again (the one-frame step of lead-0042 #11560 (3), accepted at option default 0)
    fp.clear(0, 1);
    CHECK(fp.place(rf, b).ok());
    CHECK(b.placed[0].z > a.placed[0].z);
}

C5R_TEST_MAIN
