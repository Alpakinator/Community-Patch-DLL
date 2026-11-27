// render/tests/effects/test_core_flight.cpp - ticket 4792: the choreography core counts a projectile's hit at its
// arrival (combat.cpp Sim::flightOf widened, research/05-graphics-composition/effects-server.md 2.5): with both sides
// placed, a Transfer hit lands distance / 64 / speed after its launch (speed: the weapon's, NULL 2.0), clamped 0.001-5 s;
// without positions the caller's CombatWeapon::flight stays (4775's rule). Helpers copied in small from
// render/tests/unitanim/test_combat.cpp (same repository).
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "c5r/effects/projectile.h"
#include "c5r/unitanim/combat.h"
#include "testkit.h"

using namespace c5r::unitanim;

namespace {

CombatMemberArt archer(const std::string& name, int n, float speed) {
    CombatMemberArt m;
    m.member = name;
    m.count = n;
    m.domain = "Land";
    m.enable = {"Idle", "Attack", "RunCharge", "AttackCity", "Bombard", "Death", "BombardDefend", "Run", "Fortify",
                "CombatReady", "Walk", "AttackCharge"};
    m.victory = true;
    CombatWeapon arrow;
    arrow.projectile = true;
    arrow.flight = 0.25f;   // the caller's value: used only without positions
    arrow.speed = speed;
    m.weapons.push_back(arrow);
    return m;
}
CombatMemberArt soldier(const std::string& name, int n) {
    CombatMemberArt m = archer(name, n, 0);
    m.weapons.clear();
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
void place(CombatUnitFigures& u, float x0, float y0) {
    u.x.clear();
    u.y.clear();
    for (size_t i = 0; i < u.standing.size(); ++i) {
        u.x.push_back(x0 + 4.0f * static_cast<float>(i));
        u.y.push_back(y0);
    }
    u.has_centre = true;
    u.centre_x = x0;
    u.centre_y = y0;
}
class Clips : public CombatClips {
public:
    std::map<int32_t, ActionTiming> t;
    Clips() {
        t[1100] = timing(1.0f, {0.4889f});
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
    ActionTiming timing(int32_t, int32_t, const CombatMemberArt&, int32_t code) override {
        if (!t.count(code)) return ActionTiming();
        return t[code];
    }
};
CombatInput ranged() {
    CombatInput in;
    in.seq = 41;
    in.kind = kCombatKindUnit;
    in.flags = kCombatVisualize | kCombatRanged | kCombatGameFlagsRead;
    in.attacker.owner = 0;
    in.attacker.unit = 1;
    in.defender.owner = 63;
    in.defender.unit = 2;
    in.defender.final_damage = 40;
    return in;
}
bool approx(double a, double b, double eps) { return std::fabs(a - b) <= eps; }

}  // namespace

C5R_TEST(placed_transfer_hits_land_after_the_flight) {
    for (float speed : {0.0f, 4.0f}) {   // 0 = NULL: 2.0 plots per second
        Clips clips;
        CombatParams p;
        CombatUnitFigures a = unit(0, 1, {archer("A", 2, speed)});
        CombatUnitFigures d = unit(63, 2, {soldier("W", 5)});
        place(a, 0, 0);
        place(d, 128, 0);
        CombatResult r = choreograph(ranged(), a, d, clips, p);
        CHECK(r.status.ok() && !r.at_limit);
        CHECK(!r.hits.empty());
        const float sp = speed > 0 ? speed : c5r::effects::kNullSpeed;
        for (auto& h : r.hits) {
            CHECK(h.projectile);
            // the flight covers striker -> target figure; their x differ by 128 + 4 (target - striker index)
            const double flight = h.time - h.launch;
            CHECK(flight > 0.9 * 112.0 / 64.0 / sp && flight < 1.1 * 160.0 / 64.0 / sp);
        }
        // a struck figure falls at the landing, not at the launch: no Death hit lands before its flight
        for (auto& h : r.hits)
            if (h.result == HitResult::Death) CHECK(h.time >= h.launch + 0.5 * 128.0 / 64.0 / sp);
    }
}

C5R_TEST(far_targets_clamp_at_five_seconds_and_unplaced_keeps_the_callers_flight) {
    Clips clips;
    CombatParams p;
    CombatUnitFigures a = unit(0, 1, {archer("A", 1, 0.01f)});   // 1000 units at 0.01 plots/s: far past 5 s
    CombatUnitFigures d = unit(63, 2, {soldier("W", 5)});
    place(a, 0, 0);
    place(d, 1000, 0);
    CombatResult r = choreograph(ranged(), a, d, clips, p);
    CHECK(r.status.ok());
    bool any = false;
    for (auto& h : r.hits)
        if (h.projectile && h.player == 0) {
            any = true;
            CHECK(approx(h.time - h.launch, c5r::effects::kMaxFlight, 1e-3));
        }
    CHECK(any);
    // without positions: the caller's CombatWeapon::flight (0.25 s), 4775's rule unchanged
    CombatUnitFigures a2 = unit(0, 1, {archer("A", 2, 4.0f)});
    CombatUnitFigures d2 = unit(63, 2, {soldier("W", 5)});
    CombatResult r2 = choreograph(ranged(), a2, d2, clips, p);
    CHECK(r2.status.ok() && !r2.hits.empty());
    for (auto& h : r2.hits) CHECK(approx(h.time - h.launch, 0.25, 1e-6));
}

C5R_TEST_MAIN
