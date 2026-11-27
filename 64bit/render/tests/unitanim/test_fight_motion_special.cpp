// render/tests/unitanim/test_fight_motion_special.cpp - issue 4726 (epic 0042): motion.h's FightMotion with 4777's special
// fights (unit-combat-server.md "The adapter's special inputs (4726)"): a third participant (the interceptor) and the
// damage members are moved and faced; an aircraft's height (FigureMove z0 / z1) in FigurePose::z, in the offsets'
// heights and on the placed figure's z through FightPlacement; the nuke's plot target and a member without figures as
// centres only. Synthetic data only (no game files; the names are test strings). Each check with a negative case where
// the function can fail.
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

bool approxEq(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

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
CombatMemberArt bomber(const std::string& name, int n) {
    CombatMemberArt m;
    m.member = name;
    m.count = n;
    m.domain = "Air";
    m.enable = words(kPlane);
    m.motion.move_rate = 5.4f;   // 345.6 world units per second
    m.motion.altitude = 90.0f;
    CombatWeapon gun;
    gun.effect = "TEST_GUN_HIT";
    m.weapons.push_back(gun);
    CombatWeapon bomb;
    bomb.projectile = true;
    bomb.dropped = true;
    bomb.target_ground = true;
    bomb.speed = 0.9f;
    m.weapons.push_back(bomb);
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
        if (!t.count(code)) {
            a.why = m.member + " code " + std::to_string(code) + ": not bound (test)";
            return a;
        }
        return t[code];
    }
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

// The interception of test_combat_special (4777): a bomber (2 figures, killed) on a land unit, intercepted by a fighter.
struct Interception {
    CombatInput in = record(100, 0, kCombatBombing);
    CombatUnitFigures B = placed(unit(0, 1, {bomber("B", 2)}), 0, 0);
    CombatUnitFigures D = placed(unit(63, 2, {soldier("W", 5)}), 256, 0);
    CombatUnitFigures F = placed(unit(63, 5, {bomber("F", 3)}), 300, 64);
    CombatResult r;
    Interception() {
        in.attacker.damage = 0;
        in.interceptor.owner = 63;
        in.interceptor.unit = 5;
        in.interceptor.damage = 100;
        Clips clips;
        r = choreograph(in, B, D, {F}, clips, CombatParams());
        if (!r.status.ok()) ::c5r::test::fail(__FILE__, __LINE__, "Interception: " + r.status.text);
    }
};

const FigureMove* firstMove(const CombatResult& r, int32_t player, int32_t id, uint16_t fig, bool back) {
    for (auto& m : r.moves)
        if (m.player == player && m.id == id && m.figure == fig && m.back == back) return &m;
    return nullptr;
}

// A flat 6 x 8 plot height field at height 0 (the EXE's plot spacing), so that a figure's z is its ZOffset plus its height.
heightfield::HeightGrid flat() {
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
    g.h.assign(static_cast<size_t>(g.samplesX) * g.samplesY, 0);
    return g;
}

}  // namespace

C5R_TEST(an_interception_moves_its_three_participants) {
    Interception x;
    // without the interceptor's figures the motion is refused by name (its moves name no standing figure)
    FightMotion two;
    Status s = two.build(x.r, x.B, x.D);
    CHECK(!s.ok());
    CHECK(s.str().find("63/5") != std::string::npos);
    // with them: every participant of the result
    FightMotion fm;
    CHECK_OK(fm.build(x.r, x.B, x.D, {x.F}));
    CHECK(fm.hasUnit(0, 1) && fm.hasUnit(63, 2) && fm.hasUnit(63, 5));
    CHECK(!fm.hasUnit(63, 6));
    // the fighter flies out climbing to its altitude, holds it, and flies home descending to its base
    const FigureMove* out = firstMove(x.r, 63, 5, 0, false);
    const FigureMove* back = firstMove(x.r, 63, 5, 0, true);
    CHECK(out && back);
    if (!out || !back) return;
    CHECK(out->air && back->air && back->z1 == 0.f && back->z0 == 90.f);
    FigurePose p;
    CHECK(fm.pose(out->t0, 63, 5, 0, p));
    CHECK(approxEq(p.z, out->z0, 1e-4));
    const double mid = 0.5 * (out->t0 + out->t1);
    CHECK(fm.pose(mid, 63, 5, 0, p));
    CHECK(p.moving);
    CHECK(approxEq(p.z, 0.5 * (out->z0 + out->z1), 1e-3));
    CHECK(approxEq(p.x, 0.5 * (out->x0 + out->x1), 1e-3) && approxEq(p.y, 0.5 * (out->y0 + out->y1), 1e-3));
    CHECK(fm.pose(0.5 * (out->t1 + back->t0), 63, 5, 0, p));
    CHECK(approxEq(p.z, out->z1, 1e-4));   // between the legs: at its altitude
    CHECK(fm.pose(0.5 * (back->t0 + back->t1), 63, 5, 0, p));
    CHECK(p.z > 0.f && p.z < 90.f);
    CHECK(fm.pose(back->t1, 63, 5, 0, p));
    CHECK(approxEq(p.z, 0, 1e-4) && approxEq(p.x, 300, 1e-3) && approxEq(p.y, 64, 1e-3));   // home, on the ground
    // (the intercepted bomber dies before it leaves: no move.) A strike that is not intercepted: the bomber is in the
    // air while it flies, two participants as before 4726
    {
        CombatInput in = record(0, 40, kCombatBombing);
        Clips clips;
        CombatResult s2 = choreograph(in, x.B, x.D, clips, CombatParams());
        CHECK(s2.status.ok());
        FightMotion f2;
        CHECK_OK(f2.build(s2, x.B, x.D));
        const FigureMove* bout = firstMove(s2, 0, 1, 0, false);
        CHECK(bout != nullptr);
        if (bout) {
            CHECK(f2.pose(0.5 * (bout->t0 + bout->t1), 0, 1, 0, p));
            CHECK(p.z > 0.f);
        }
    }
    // the land defender never leaves the ground
    for (double t = 0; t <= fm.end(); t += 0.25) {
        CHECK(fm.pose(t, 63, 2, 0, p));
        CHECK(p.z == 0.f);
    }
    // the fighter's offsets carry its height (heights[i] for out[i]); the two-argument offsets() is unchanged
    units::UnitPlacement pl;
    pl.yaw = 0;
    for (uint16_t i = 0; i < 3; ++i) {
        units::FigurePlacement f;
        f.figure = i;
        f.x = 300 + 4.0f * i;
        f.y = 64;
        pl.placed.push_back(f);
    }
    std::vector<units::FigureOffset> offs, offs2;
    std::vector<float> heights;
    CHECK(fm.offsets(mid, 63, 5, pl, offs, heights));
    CHECK_EQ(offs.size(), heights.size());
    CHECK_EQ(offs.size(), 3u);
    CHECK(approxEq(heights[0], 0.5 * (out->z0 + out->z1), 1e-3));
    CHECK(fm.offsets(mid, 63, 5, pl, offs2));
    CHECK_EQ(offs2.size(), offs.size());
    for (size_t i = 0; i < offs.size(); ++i) CHECK(offs2[i].dx == offs[i].dx && offs2[i].dy == offs[i].dy);
    // negative: a unit not in the fight has no offsets and no heights
    CHECK(!fm.offsets(mid, 7, 7, pl, offs, heights));
    CHECK(offs.empty() && heights.empty());
}

C5R_TEST(an_aircraft_left_in_the_air_comes_down_on_its_walk_back) {
    Interception x;
    const FigureMove* out = firstMove(x.r, 63, 5, 0, false);
    CHECK(out != nullptr);
    if (!out) return;
    // the adapter's partial motion (srv_units_anim's ControllerFightPlayer: the result so far, its end cut): cut while
    // the fighter is at its altitude, the fight's end is then the moment the walk back starts
    CombatResult part = x.r;
    part.end = out->t1;
    part.moves.clear();
    for (auto& m : x.r.moves)
        if (m.t0 < part.end) part.moves.push_back(m);
    FightMotion fm;
    CHECK_OK(fm.build(part, x.B, x.D, {x.F}));
    units::UnitPlacement pl;
    for (uint16_t i = 0; i < 3; ++i) {
        units::FigurePlacement f;
        f.figure = i;
        f.x = 300 + 4.0f * i;
        f.y = 64;
        pl.placed.push_back(f);
    }
    std::vector<units::FigureOffset> offs;
    std::vector<float> h;
    CHECK(fm.offsets(part.end + 0.01, 63, 5, pl, offs, h));
    CHECK(!h.empty() && h[0] > 80.f && h[0] < 90.f);
    double prev = 90;
    bool landed = false;
    for (double t = part.end; t < part.end + 10; t += 1.0 / 30.0) {
        if (!fm.offsets(t, 63, 5, pl, offs, h)) {
            landed = true;
            break;
        }
        for (size_t i = 0; i < h.size(); ++i)
            if (offs[i].figure == 0) {
                CHECK(h[i] <= prev + 1e-4);   // it only comes down
                prev = h[i];
            }
    }
    CHECK(landed);   // in its slot on the ground: no offsets
}

C5R_TEST(a_nuke_faces_its_members_to_the_attacker_and_keeps_the_plot_a_centre) {
    CombatParams p;
    CombatMemberArt missile = bomber("MISSILE", 1);
    missile.weapons.clear();
    CombatWeapon warhead;
    warhead.one_shot = true;
    warhead.effect = "TEST_NUCLEAR_BOMB";
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
    CombatMemberIn w = u;   // a second unit member whose figures the caller does not give
    w.id = 9;
    CombatMemberIn c;
    c.player = 63;
    c.id = 4;
    c.flags = 1;
    c.final_damage = 80;
    c.max_hp = 300;
    in.members = {u, w, c};
    auto M = placed(unit(0, 1, {missile}), 0, 0);
    auto V = placed(unit(63, 8, {soldier("V", 4)}), 260, 10);
    Clips clips;
    CombatResult r = choreograph(in, M, centre(256, 0), {V}, clips, p);
    CHECK(r.status.ok());
    CHECK_EQ(r.others.size(), 3u);
    CHECK(r.others[0].figures && !r.others[1].figures && !r.others[2].figures);
    FightMotion fm;
    CHECK_OK(fm.build(r, M, centre(256, 0), {V}));
    CHECK(fm.hasUnit(63, 8));
    CHECK(!fm.hasUnit(63, 9));   // took part without figures: nothing to move
    CHECK(!fm.hasUnit(-1, -1));  // the plot
    // the member's figures do not move and face the missile's start (the attacker's centre) [I]
    FigurePose ps;
    CHECK(fm.pose(0.1, 63, 8, 0, ps));
    CHECK(approxEq(ps.x, 260, 1e-4) && approxEq(ps.y, 10, 1e-4) && ps.z == 0.f);
    CHECK(approxEq(ps.yaw, yawFacing(0 - 260.f, 0 - 10.f), 1e-4));
    // the missile flies at its altitude towards the plot
    const FigureMove* mo = firstMove(r, 0, 1, 0, false);
    CHECK(mo != nullptr);
    if (mo) {
        CHECK(mo->air);
        CHECK(fm.pose(mo->t1, 0, 1, 0, ps));
        CHECK(approxEq(ps.z, mo->z1, 1e-4));
    }
    // negative: a member that fought with figures but whose figures are not given is refused by name
    FightMotion bad;
    Status s = bad.build(r, M, centre(256, 0), {});
    CHECK(!s.ok());
    CHECK(s.str().find("63/8") != std::string::npos);
    // negative: the member's figures without positions
    CombatUnitFigures nopos = unit(63, 8, {soldier("V", 4)});
    s = bad.build(r, M, centre(256, 0), {nopos});
    CHECK(!s.ok());
}

C5R_TEST(fight_placement_lifts_an_aircraft_by_its_height) {
    Interception x;
    auto fm = std::make_shared<FightMotion>();
    CHECK_OK(fm->build(x.r, x.B, x.D, {x.F}));
    const FigureMove* out = firstMove(x.r, 63, 5, 0, false);
    CHECK(out != nullptr);
    if (!out) return;
    struct Inner : units::PlacementSource {
        units::UnitStatus place(const units::PlacementRequest&, units::UnitPlacement& o) override {
            o = units::UnitPlacement();
            o.rootX = 300;
            o.rootY = 64;
            for (uint16_t i = 0; i < 3; ++i) {
                units::FigurePlacement f;
                f.figure = i;
                f.slot = i;
                f.x = 300 + 4.0f * i;
                f.y = 64;
                f.z = 5;   // the ZOffset over the flat ground
                o.placed.push_back(f);
            }
            o.figures = o.living = 3;
            return units::UnitStatus::Ok();
        }
    } inner;
    heightfield::HeightGrid g = flat();
    FightPlacement fp(&inner);
    units::UnitInput u;
    u.player = 63;
    u.id = 5;
    units::PlacementRequest rq;
    rq.unit = &u;
    rq.grid = &g;
    const double mid = 0.5 * (out->t0 + out->t1);
    fp.set(63, 5, fm, mid);
    units::UnitPlacement pl;
    CHECK(fp.place(rq, pl).ok());
    CHECK_EQ(fp.moved(), static_cast<uint64_t>(1));
    FigurePose ps;
    CHECK(fm->pose(mid, 63, 5, 0, ps));
    CHECK(ps.z > 0.f);
    CHECK(approxEq(pl.placed[0].z, 5.0 + ps.z, 1e-3));
    CHECK(approxEq(pl.placed[0].x, ps.x, 1e-3) && approxEq(pl.placed[0].y, ps.y, 1e-3));
    // a ground unit of the same fight is placed with no height added
    units::UnitInput d;
    d.player = 63;
    d.id = 2;
    rq.unit = &d;
    fp.set(63, 2, fm, mid);
    CHECK(fp.place(rq, pl).ok());
    for (auto& f : pl.placed) CHECK(approxEq(f.z, 5.0, 1e-4));
    // home again after its flight: the fighter's z is its ground placement
    rq.unit = &u;
    const FigureMove* back = firstMove(x.r, 63, 5, 0, true);
    CHECK(back != nullptr);
    if (back) {
        fp.set(63, 5, fm, back->t1);
        CHECK(fp.place(rq, pl).ok());
        CHECK(approxEq(pl.placed[0].z, 5.0, 1e-4));
    }
}

C5R_TEST_MAIN
