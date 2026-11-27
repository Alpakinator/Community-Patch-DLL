// combat.cpp - the combat choreography core (epic 0042, issue 4775; combat.h). Written from combat-choreography.md
// (0321) sections 2-7 and 13.3-13.5 only (D83). Marks in the comments: [C] the rule as 0321 reads it from code, [I] this
// file's reading where 0321 leaves it open (each is listed in unit-combat-server.md "Choreography as built (4775)").
// 4777 (the special cases: a city as a participant, aircraft out and back, the interceptor, naval, nuclear and area
// members) is in unit-combat-server.md "Special cases (4777)"; the participants are a list (0321 4.1: attacker 0,
// defender 1, interceptor 2, then the damage members).
#include "c5r/unitanim/combat.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>

#include "c5r/effects/projectile.h"   // 4792: the flight time rule (effects-server.md 1.1 item 5)
#include "c5r/unitanim/motion.h"     // 4735: yawFacing
#include "c5r/unitanim/random.h"
#include "c5r/units/placement.h"

namespace c5r { namespace unitanim {

uint32_t CombatUnitFigures::figures() const {
    uint32_t n = 0;
    for (auto& m : members) n += static_cast<uint32_t>(std::max(0, m.count));
    return n;
}

const char* combatCodeName(CombatCode c) {
    switch (c) {
    case CombatCode::Ok: return "Ok";
    case CombatCode::QuickCombat: return "QuickCombat";
    case CombatCode::NotVisualised: return "NotVisualised";
    case CombatCode::BadInput: return "BadInput";
    case CombatCode::NoFigures: return "NoFigures";
    case CombatCode::CityAttacker: return "CityAttacker";
    case CombatCode::AirCombat: return "AirCombat";
    case CombatCode::Nuclear: return "Nuclear";
    case CombatCode::AreaMembers: return "AreaMembers";
    case CombatCode::UnknownKind: return "UnknownKind";
    }
    return "?";
}

const char* figStateName(FigState s) {
    switch (s) {
    case FigState::Look: return "look";
    case FigState::Wait: return "wait";
    case FigState::Approach: return "approach";
    case FigState::Ready: return "ready";
    case FigState::Reface: return "reface";
    case FigState::Dying: return "dying";
    case FigState::Removed: return "removed";
    case FigState::Strike: return "strike";
    case FigState::Done: return "done";
    case FigState::Charge: return "charge";
    case FigState::Defend: return "defend";
    case FigState::Reform: return "reform";
    case FigState::Turn: return "turn";
    case FigState::Victory: return "victory";
    }
    return "?";
}

namespace {

const double kEps = 1e-9;
const size_t kNoMove = static_cast<size_t>(-1);

// 4777: a participant's role (0321 4.1: attacker 0, defender 1, interceptor 2, damage members 3..)
enum class Role : uint8_t { Attacker, Defender, Interceptor, Member };

struct Fig {
    uint16_t index = 0;
    uint32_t member = 0;
    FigState st = FigState::Look;
    int32_t last = 1000;          // the last action requested (0321 5.3's victory test reads it); a unit at rest: 1000
    bool can_attack = false;      // flag 0x2000000 clear [C 5.1]
    bool doomed = false;          // LastFirst: one of the planned deaths
    bool protect = false;         // 0x4000000: the protected survivor [C 13.4]
    bool engaged = false;         // a melee figure that has made its approach (defenders strike at engaged figures) [I]
    bool combat_ready = false;    // 1600 has been requested once: the figure is (or is getting) combat ready
    bool live_wait = false;       // 4782 live: the strike is requested, the controller's figure does not play it yet
    double ready_at = 0;          // when its entry into combat ready is done (5 -> 10 "when its animation is done")
    bool reserved = false;        // holds one of the opponent's open needs (a strike under way)
    bool stuck = false;           // its strike's clip is missing: it never completes (4770's rule, 0321 13.4's hold)
    double t0 = 0;                // the state's entry time
    double until = 0;             // Approach / Charge / Dying: when the state ends
    int32_t code = 0;             // Strike: the code
    int32_t play = 0;             // 4777 Strike: the code whose clip times it (1130 answered with the attack clip)
    ActionTiming tm;              // Strike: its timing
    size_t next_hit = 0;
    bool landed = false;          // Strike: a hit of this strike has landed or is in flight (the reservation is spent)
    int tpart = -1, tfig = -1;    // target: participant, figure (-1: the city / plot)
    int strikers = 0;             // figures targeting this one
    float x = 0, y = 0;           // 4776: where it stands (or arrives, during a move), world units; with positions only
    bool ran = false;             // 4776: its last approach was a move (the run stop 1440 comes before combat ready)
    // 4777: an aircraft of the attacker or the interceptor flies out to its target and back to its base
    bool flyer = false;
    bool away = false;            // it has flown out
    bool returning = false;       // on its flight home
    float hx = 0, hy = 0;         // its base (its start)
    size_t move = kNoMove;        // its current move in r_.moves (a flight cut short when it turns home)
    bool turned = false;          // 4735: its turn before the fight is decided (CombatParams::turn_first)
    bool entry_wait = false;      // 4735: it turned; its first strike waits for its combat-ready entry (no live shortcut)
    bool stance = false;          // 4750: Defend with 1450 (stock's phase-2 0xF) until f.until, then 1280
    bool astance = false;         // 5050 (b): Turn with 1450 (an attacker's phase-2 0xF) until f.until, then Ready 1600
    bool astanced = false;        // 5050 (b): its stance is over: its 1600 meets the machine in its run loop
    bool astance_walk = false;    // 5050 (b): the stance was a 1450 (Walk); false: the hold without a request
    bool rode = false;            // 5057 (A): it stopped after a ride-through (stands without combat ready)
    double ride_stop_until = 0;   // 5057 (A): ... and looks again when its run stop (1440) is over
    bool run_strike = false;      // 5071: its next strike is a 1140 from its run (stock's state 10 after a 1120)
    bool held_ride = false;       // 5071: it rides on in its 1120 to a held strike's target and strikes on arrival
    double dying_at = 0;          // 5053: when it began to die (death_by_variant: B's length counts from here)
    bool death_known = false;     // 5053 death_by_variant: the death clip it plays (A or B) is decided
    bool end_shuffle = false;     // 5053 end_after_dying: Done with 1450 (out of 1280) until f.until, then Reface
    bool p2stance = false;        // 5067 S1: a defender figure in the type 2 phase 2 (Turn with 1450) until phase2_end_
    bool home_wait = false;       // 5072: home from its walk, it waits there without a request until the fight's end
    double turn_stop_until = 0;   // 5074: after its turn it stands (1000: the run stop, then idle) and looks from here
};

struct Part {
    Role role = Role::Attacker;
    bool city = false;
    bool plot = false;            // 4777: the nuke's plot target (no defender)
    bool figureless = false;      // 4777: a city, the plot, or a damage member whose figures were not given
    int32_t player = -1, id = -1;
    const CombatUnitFigures* unit = nullptr;
    std::vector<Fig> figs;
    uint32_t standing = 0, planned = 0, died = 0, owed = 0, paid = 0;
    bool killed = false;
    int reserved = 0;             // strikes holding one of this participant's needs
    bool can_answer = false;      // +0x2D3E: a ranged member, or an air side [C 13.3, 13.4]
    bool air = false, sea = false;   // 4777: +0x2D41 an air figure, +0x2D42 a sea figure [C 13.3]
    bool placed = false;          // 4776: the figures' positions are known (a city: its centre)
    float cx = 0, cy = 0;         // 4776: the unit's root or the city's centre
    int need() const { return static_cast<int>(planned - died) + static_cast<int>(owed - paid); }
    int open() const { return need() - reserved; }
};

struct Landing {
    double time = 0, launch = 0;
    int spart = 0, sfig = 0;      // sfig -1: a city's shot
    int32_t code = 0;
    int tpart = -1, tfig = -1;
    bool projectile = false;
    bool first = false;           // the strike's first: it spends the reservation
    bool area = false;            // 4777: an area share (a member's death at the blast)
    bool keep_last = false;       // 5071: a ride landing: its extra struck figures leave the last planned death open
    const CombatWeapon* w = nullptr;   // 4777: the striker's weapon (its effect and sound)
    bool has_origin = false;
    float ox = 0, oy = 0, oz = 0;
    int32_t trigger = -1;
    uint64_t order = 0;
    int32_t shot = -1;            // 0742: a city volley's shot, its index in CombatResult::launches (-1: not a city shot)
};

// hits sorted by time, their trigger ids kept beside them (dropped when the counts differ)
void sortHits(ActionTiming& t) {
    if (t.hit_ids.size() != t.hits.size()) t.hit_ids.clear();
    std::vector<std::pair<float, int32_t>> v;
    for (size_t i = 0; i < t.hits.size(); ++i) v.push_back(std::make_pair(t.hits[i], t.hit_ids.empty() ? -1 : t.hit_ids[i]));
    std::stable_sort(v.begin(), v.end(), [](const std::pair<float, int32_t>& a, const std::pair<float, int32_t>& b) {
        return a.first < b.first;
    });
    for (size_t i = 0; i < v.size(); ++i) {
        t.hits[i] = v[i].first;
        if (!t.hit_ids.empty()) t.hit_ids[i] = v[i].second;
    }
}

CombatStatus fail(CombatCode c, const std::string& t) {
    CombatStatus s;
    s.code = c;
    s.text = t;
    return s;
}

bool has(const CombatMemberArt& m, const char* w) { return m.enable.count(w) != 0; }

class Sim {
public:
    Sim(const CombatInput& in, CombatClips& clips, const CombatParams& p, CombatResult& r)
        : in_(in), clips_(clips), p_(p), r_(r) {}

    CombatStatus setup(const CombatUnitFigures& a, const CombatUnitFigures& d, const std::vector<CombatUnitFigures>& o);
    bool tick();   // one clock step; true when the combat has ended (4782: CombatRun steps it)
    void feedback(CombatFeedback* fb) { fb_ = fb; }
    double time() const { return static_cast<double>(k_) * p_.step; }
    bool done() const { return done_; }

private:
    const CombatMemberArt& mem(int pi, const Fig& f) const { return parts_[pi].unit->members[f.member]; }
    const ActionTiming& timing(int pi, const Fig& f, int32_t code);
    void step(int pi, int fi, FigState st, int32_t code, uint8_t approach = 0);
    void look(int pi, int fi, double t);
    bool turnFirst(int pi, int fi, double t);                            // 4735
    bool defendStance(int pi, int fi, double t);                         // 4750
    bool attackStance(int pi, int fi, double t);                         // 5050 (b)
    bool entryAfterTurn(int pi, int fi, double t);                       // 5074
    void reformFirst(double t);                                          // 4745
    void ready(int pi, int fi, double t, FigState st);
    int targetPart(int pi) const;
    bool pickTarget(int pi, int fi, int ti);
    int32_t strikeCode(int pi, const Fig& f);
    const CombatWeapon* weapon(int pi, const Fig& f, bool city) const;
    void startStrike(int pi, int fi, double t);
    void progress(int pi, int fi, double t);
    void land(const Landing& l);
    int struckCount(const Landing& l);                                   // 4783 C1
    int nearestVictim(int ti, int aimed) const;                          // 4783 C1
    bool spares(const Landing& l) const;                                 // 4733
    void area(const Landing& l);
    void release(int pi, Fig& f);
    void die(int pi, int fi, double t);
    void finish(int pi, int fi, double t, bool at_limit);
    bool walkHome(int pi, int fi, double t);                             // 4742
    bool victoryEligible(int pi, const Fig& f) const;
    bool closeOut();
    void note(double t, const std::string& s) { r_.notes.push_back(CombatNote{t, s}); }
    bool moveTo(int pi, int fi, double t, int32_t code, double& dur, bool no_shuffle = false);   // 4776
    // 0743 (CombatParams::melee_reach): an attacking land figure of a member without RushAttackFormation, not air
    bool reaches(int pi, const Fig& f) const {
        const CombatMemberArt& m = mem(pi, f);
        return (p_.melee_reach && parts_[pi].role == Role::Attacker && !(in_.flags & kCombatRanged) && !parts_[pi].air &&
                !f.flyer && !m.rush && m.domain != "Sea" && m.domain != "Air") ||
               steps(pi, f);   // 5061: the type 2 defender from the attacker's first landing on
    }
    // 5061 (CombatParams::defender_step): a type 2 defender's land figure of a member without RushAttackFormation, both
    // sides placed, from the attacker's first landing on (stock's phase 4, unit-combat-server.md 18.45)
    bool steps(int pi, const Fig& f) const {
        if (!p_.defender_step || !type2_ || !att_landed_) return false;
        const Part& P = parts_[pi];
        if (P.role != Role::Defender || P.air || f.flyer || !P.placed || !parts_[0].placed) return false;
        const CombatMemberArt& m = mem(pi, f);
        return !m.rush && m.domain != "Sea" && m.domain != "Air";
    }
    bool stepIn(int pi, int fi, double t);                               // 5061
    bool rides(int pi, const Fig& f) const;                              // 5057 (A)
    void rideLand(int pi, int fi, double t);                             // 5057 (A)
    bool inLine(int pi, const Fig& f) const;                             // 5057 (A)
    bool reserveTarget(int pi, int fi);                                  // 5057 (A)
    bool runStrikes(int pi, const Fig& f);                               // 5071
    bool keepsLast(const Fig& f) const;                                  // 5071
    void runStrike(int pi, int fi, double t);                            // 5071
    bool heldStrike(int pi, int fi, double t);                           // 5071
    bool takes(int pi, const Fig& f, int32_t code) const;                // 5071
    bool defenderHeld() const;                                           // 5057 (B)
    bool endShuffle(int pi, int fi, double t);                           // 5053 end_after_dying
    void type2Phase2(double t);                                          // 5067 S1
    void flyHome(int pi, int fi, double t);                              // 4777
    void cityVolley(double t);                                           // 4777
    bool defenderAnswers() const;                                        // 4777
    double flightOf(int pi, const Fig& f, const CombatWeapon* w) const;  // 4777
    void position(int pi, int fi, double t, float& x, float& y) const;   // 4777

    const CombatInput& in_;
    CombatClips& clips_;
    const CombatParams& p_;
    CombatResult& r_;
    std::vector<Part> parts_;
    int interceptor_ = -1;        // 4777: its index (2), or -1
    double limit_ = 12.0;
    bool def_answers_ = false;    // 4777: the defender's figures answer (the retaliate flag, or 13.4's air opening)
    bool city_fired_ = false;     // 4777: the city attacker's volley has left
    bool area_done_ = false;      // 4777: the area shares are scheduled
    double hold_until_ = 0;       // 4777: an effect the fight waits for (WaitForEffectCompletion)
    bool hold_limit_ = false;     // 4777: ... one that never reports its end: the fight holds to its limit
    Rng rng_;
    Rng death_rng_;               // 5053 death_by_variant self-timed: A or B per death, apart from the fight's draws
    double now_ = 0;
    std::vector<Landing> landings_;
    uint64_t order_ = 0;
    std::map<std::tuple<int, uint32_t, int32_t>, ActionTiming> timings_;
    CombatFeedback* fb_ = nullptr;   // 4782: the controller's live figure states (null: self-timed)
    int64_t k_ = 0;
    bool done_ = false;
    bool reform_decided_ = false;  // 4745: phase 1 is laid out (the first step)
    double reform_end_ = 0;       // 4745: every reforming figure has arrived and stopped; nobody looks before it
    bool type2_ = false;          // 5057 (B): stock's fight type 2 (18.15): neither side has a ranged member
    bool att_landed_ = false;     // 5057 (B): the attacker's first landing has come (phase 3 is over)
    bool phase2_decided_ = false; // 5067 S1: the type 2 phase 2 is laid out (the first step at or after phase 1's end)
    double phase2_end_ = 0;       // 5067 S1: nobody looks before it (0: no phase 2)
    bool riders_held_ = false;    // 5072: the last end test held the riders (type2_end_after_dying): they stand
    // 5072: a rider held by type2_end_after_dying (a rush member of the attacker, not an aircraft, in a type 2 fight)
    bool heldRider(size_t pi, const Fig& f) const;
    void homeWaitsReface(double t);   // 5072: the riders that wait at home take their reface (1000) at the end
};

const ActionTiming& Sim::timing(int pi, const Fig& f, int32_t code) {
    auto key = std::make_tuple(pi, f.member, code);
    auto it = timings_.find(key);
    if (it != timings_.end()) return it->second;
    ActionTiming t = clips_.timing(parts_[pi].player, parts_[pi].id, mem(pi, f), code);
    sortHits(t);
    return timings_[key] = t;
}

void Sim::step(int pi, int fi, FigState st, int32_t code, uint8_t approach) {
    Part& P = parts_[pi];
    Fig& f = P.figs[fi];
    f.st = st;
    f.t0 = now_;
    if (code != 0) f.last = code;
    FigureStep s;
    s.time = now_;
    s.player = P.player;
    s.id = P.id;
    s.figure = f.index;
    s.state = st;
    s.code = code;
    s.approach = approach;
    if (f.tpart >= 0) {
        const Part& T = parts_[f.tpart];
        s.target_player = T.player;
        s.target_id = T.id;
        s.target_figure = f.tfig >= 0 ? T.figs[f.tfig].index : -1;
    }
    r_.steps.push_back(s);
}

const CombatUnitFigures* findFigures(const std::vector<CombatUnitFigures>& o, int32_t player, int32_t id) {
    for (auto& u : o)
        if (u.player == player && u.id == id) return &u;
    return nullptr;
}

CombatStatus Sim::setup(const CombatUnitFigures& a, const CombatUnitFigures& d,
                        const std::vector<CombatUnitFigures>& others) {
    // the participants (0321 4.1): attacker, defender, the interceptor when named, then each damage member
    struct Spec {
        CombatSide side;
        const CombatUnitFigures* figs;
        Role role;
        std::string who;
        bool optional;            // a participant that takes part without figures when they are not given
    };
    std::vector<Spec> specs;
    specs.push_back(Spec{in_.attacker, &a, Role::Attacker, "attacker", false});
    specs.push_back(Spec{in_.defender, &d, Role::Defender, "defender", false});
    if (in_.interceptor.unit >= 0)
        specs.push_back(Spec{in_.interceptor, findFigures(others, in_.interceptor.owner, in_.interceptor.unit),
                             Role::Interceptor, "interceptor", true});
    size_t unfigured = 0, unitMembers = 0;
    for (size_t k = 0; k < in_.members.size(); ++k) {
        const CombatMemberIn& m = in_.members[k];
        CombatSide s;
        s.owner = m.player;
        if (m.flags & 1u) s.city = m.id;
        else s.unit = m.id;
        s.final_damage = std::max<int32_t>(0, m.final_damage);
        s.max_hp = m.max_hp > 0 ? m.max_hp : 100;
        s.final_damage = std::min(s.final_damage, s.max_hp);
        s.damage = m.damage;
        const CombatUnitFigures* f = (m.flags & 1u) ? nullptr : findFigures(others, m.player, m.id);
        if (!(m.flags & 1u)) {
            ++unitMembers;
            if (!f) ++unfigured;
        }
        specs.push_back(Spec{s, f, Role::Member, "member " + std::to_string(k), true});
    }
    parts_.resize(specs.size());
    for (size_t pi = 0; pi < specs.size(); ++pi) {
        const CombatSide& s = specs[pi].side;
        const std::string& who = specs[pi].who;
        Part& P = parts_[pi];
        P.role = specs[pi].role;
        if (specs[pi].role == Role::Interceptor) interceptor_ = static_cast<int>(pi);
        const bool nukePlot = pi == 1 && in_.nuclear_level > 0 && s.city < 0 && s.unit < 0;
        if (!nukePlot && (s.city >= 0) == (s.unit >= 0))
            return fail(CombatCode::BadInput, who + ": exactly one of unit and city must be set");
        if (s.max_hp <= 0 || s.final_damage < 0 || s.final_damage > s.max_hp)
            return fail(CombatCode::BadInput, who + ": final damage " + std::to_string(s.final_damage) + " outside 0.." +
                                                  std::to_string(s.max_hp) + " (max hit points)");
        P.city = s.city >= 0;
        P.plot = nukePlot;
        P.player = s.owner;
        P.id = P.city || P.plot ? -1 : s.unit;
        P.killed = s.final_damage >= s.max_hp;
        const CombatUnitFigures* fu = specs[pi].figs;
        if (fu && fu->has_centre) {   // 4776: the unit's root or the city's centre (4777: the nuke's plot)
            if (!std::isfinite(fu->centre_x) || !std::isfinite(fu->centre_y))
                return fail(CombatCode::BadInput, who + ": centre is not finite");
            P.cx = fu->centre_x;
            P.cy = fu->centre_y;
        }
        if (P.city || P.plot || (specs[pi].optional && !fu)) {
            P.figureless = true;
            P.owed = 0;
            P.placed = fu && fu->has_centre;
            continue;
        }
        const CombatUnitFigures& u = *fu;
        if (u.player != s.owner || u.id != s.unit)
            return fail(CombatCode::BadInput, who + ": figures of " + std::to_string(u.player) + "/" + std::to_string(u.id) +
                                                  ", record names " + std::to_string(s.owner) + "/" + std::to_string(s.unit));
        const uint32_t total = u.figures();
        if (total == 0 || total > 64)
            return fail(CombatCode::BadInput, who + ": " + std::to_string(total) + " figures (1..64)");
        for (auto& m : u.members)
            if (m.count < 0) return fail(CombatCode::BadInput, who + ": member " + m.member + " count < 0");
        for (size_t i = 0; i < u.standing.size(); ++i)
            if (u.standing[i] >= total || (i > 0 && u.standing[i] <= u.standing[i - 1]))
                return fail(CombatCode::BadInput, who + ": standing figures not sorted, distinct and below " +
                                                      std::to_string(total));
        if (u.standing.empty())
            return fail(CombatCode::NoFigures, who + " " + std::to_string(s.owner) + "/" + std::to_string(s.unit) +
                                                   ": no figure standing");
        P.unit = &u;
        // 4744 (CombatParams::fight_drawn_figures): only the standing figures the placement draws take part; a unit created
        // damaged is held with every figure (unit-combat-server.md 18.16). None drawn (not expected): every standing one.
        std::vector<size_t> keep;
        for (size_t i = 0; i < u.standing.size(); ++i)
            if (!p_.fight_drawn_figures || u.standing[i] < u.drawn) keep.push_back(i);
        if (keep.empty())
            for (size_t i = 0; i < u.standing.size(); ++i) keep.push_back(i);
        for (size_t k : keep) {
            const uint16_t idx = u.standing[k];
            Fig f;
            f.index = idx;
            uint32_t first = 0, mi = 0;
            while (idx >= first + static_cast<uint32_t>(u.members[mi].count)) first += u.members[mi++].count;
            f.member = mi;
            const CombatMemberArt& m = u.members[mi];
            // [C 5.1]: a figure with none of 1100, 1130, 1140, 1160, 1180 enabled cannot attack
            f.can_attack = has(m, "Attack") || has(m, "AttackSurfaceToAir") || has(m, "AttackCharge") ||
                           has(m, "AttackCity") || has(m, "Bombard");
            // 4777: the side's air and sea bytes [C 13.3]; an aircraft of the attacker or the interceptor flies
            if (m.domain == "Air") P.air = true;
            if (m.domain == "Sea") P.sea = true;
            f.flyer = m.domain == "Air" && (P.role == Role::Attacker || P.role == Role::Interceptor);
            P.figs.push_back(f);
        }
        // 4776: the figures' start positions (one per standing figure), else the approach keeps its fixed length
        if (!u.x.empty() || !u.y.empty()) {
            if (u.x.size() != u.standing.size() || u.y.size() != u.standing.size())
                return fail(CombatCode::BadInput, who + ": " + std::to_string(u.x.size()) + " / " +
                                                      std::to_string(u.y.size()) + " positions for " +
                                                      std::to_string(u.standing.size()) + " standing figures");
            for (size_t i = 0; i < P.figs.size(); ++i) {
                const size_t k = keep[i];   // 4744: the position of the kept standing figure
                if (!std::isfinite(u.x[k]) || !std::isfinite(u.y[k]))
                    return fail(CombatCode::BadInput, who + ": a figure position is not finite");
                P.figs[i].x = P.figs[i].hx = u.x[k];
                P.figs[i].y = P.figs[i].hy = u.y[k];
            }
            P.placed = true;
        }
        for (auto& m : u.members)
            for (auto& w : m.weapons)
                if (w.projectile && !w.vs_city) P.can_answer = true;
        if (P.air) P.can_answer = true;   // 4777 [C 13.3]: +0x2D3E is set for an air side too
        // Deaths [C 13.4]: figures x damage percent / 100 (the 0x4B4B80 rule, units::livingFigures), minus those out.
        uint32_t living = 0;
        Status st = units::livingFigures(total, static_cast<int16_t>(std::min(s.final_damage, 32767)),
                                         static_cast<int16_t>(std::min(s.max_hp, 32767)), -1, living);
        if (!st.ok()) return fail(CombatCode::BadInput, who + ": the figure rule refused: " + st.str());
        P.standing = static_cast<uint32_t>(P.figs.size());
        P.planned = P.standing > living ? P.standing - living : 0;
    }
    // 4777: the limit [C 4.2, 13.2]: 20 s when a figure of any participant is an air figure
    limit_ = p_.limit;
    for (auto& P : parts_)
        if (P.air) limit_ = p_.limit_air;
    r_.limit = limit_;
    // Owed hits [C 13.4]: the defender takes one when there is no interceptor or the attacker keeps figures standing; the
    // attacker one when it has no planned deaths, the defender can answer, retaliates, and the attacker's weapon is not
    // one-shot.
    Part& A = parts_[0];
    Part& D = parts_[1];
    const bool attackerKilled = !A.figureless && A.planned >= A.standing;
    D.owed = (interceptor_ < 0 || !attackerKilled) ? 1 : 0;
    bool one_shot = false;
    if (!A.figureless && !A.unit->members.empty() && !A.unit->members[0].weapons.empty())
        one_shot = A.unit->members[0].weapons[0].one_shot;
    A.owed = (!A.figureless && A.planned == 0 && D.can_answer && (in_.flags & kCombatDefenderRetaliates) && !one_shot)
                 ? 1 : 0;
    def_answers_ = (in_.flags & kCombatDefenderRetaliates) != 0;
    // 4777 [C 13.4]: the air strike's opening phase (the attacker's side air, the defender's not): with the attacker's
    // damage in this combat 0 the defender's figures bombard-defend and the attacker's owed hit is cleared (0477's
    // submarine fix sends exactly this); otherwise they are ready to strike whatever the retaliate byte says. Not known
    // (-1): the retaliate byte decides [I].
    if (A.air && !D.air && !D.figureless && in_.attacker.damage >= 0) {
        def_answers_ = in_.attacker.damage > 0;
        if (in_.attacker.damage == 0) A.owed = 0;
    }
    // 4777 [I]: an interceptor that damages the attacker shows it: the attacker owes it one hit when it has no deaths
    // planned (the DLL's aborted interception sends the attacker's final damage as 0, CvUnitCombat.cpp
    // GenerateAirCombatInfo)
    if (interceptor_ >= 0 && !A.figureless && A.planned == 0 && in_.interceptor.damage > 0) A.owed = 1;
    // 5057 (B) [C 18.15 the type table]: type 2 = an attacker with no HasShortRangedAttack member against a defender with
    // none; Part::can_answer (a projectile weapon) stands for the column [I]; air and sea sides and ranged attacks are
    // other types (not read for ships [I])
    type2_ = !A.figureless && !D.figureless && !A.can_answer && !D.can_answer && !A.air && !D.air && !A.sea && !D.sea &&
             !(in_.flags & kCombatRanged);
    // The protected survivor [C 13.4]: when figures survive, one of them (a LastToDie member first).
    for (size_t pi = 0; pi < parts_.size(); ++pi) {
        Part& P = parts_[pi];
        if (P.figureless) continue;
        if (P.planned >= P.standing) {   // a killed unit: every figure dies, none is protected
            for (auto& f : P.figs) f.doomed = true;
            continue;
        }
        int pick = -1;
        for (size_t i = 0; i < P.figs.size() && pick < 0; ++i)
            if (P.unit->members[P.figs[i].member].last_to_die) pick = static_cast<int>(i);
        // LastFirst: the doomed are the last standing figures (skipping the LastToDie one); else the first survivor
        const size_t n = P.figs.size();
        if (p_.deaths == DeathPick::LastFirst) {
            uint32_t left = P.planned;
            for (size_t k = n; k-- > 0 && left > 0;)
                if (static_cast<int>(k) != pick) {
                    P.figs[k].doomed = true;
                    --left;
                }
        }
        if (pick < 0)
            for (size_t i = 0; i < n && pick < 0; ++i)
                if (!P.figs[i].doomed) pick = static_cast<int>(i);
        if (pick >= 0) P.figs[pick].protect = true;
    }
    rng_.reseed(unitSeed(unitSeed(p_.seed, in_.attacker.owner, in_.attacker.unit) ^ in_.seq, in_.defender.owner,
                         in_.defender.unit));
    death_rng_.reseed(unitSeed(unitSeed(p_.seed ^ 0x5053u, in_.attacker.owner, in_.attacker.unit) ^ in_.seq,
                               in_.defender.owner, in_.defender.unit));   // 5053
    for (size_t pi = 0; pi < parts_.size(); ++pi) {
        const Part& P = parts_[pi];
        SideOutcome o;
        o.player = P.player;
        o.id = P.id;
        o.city = P.city;
        o.killed = P.killed;
        o.standing = P.standing;
        o.planned = P.planned;
        o.owed = P.owed;
        o.figures = !P.figureless;
        for (auto& f : P.figs)
            if (f.protect) o.protected_figure = f.index;
        if (pi == 0) r_.attacker = o;
        else if (pi == 1) r_.defender = o;
        else r_.others.push_back(o);
    }
    if (unfigured > 0)
        note(0, "Members: " + std::to_string(unfigured) + " of " + std::to_string(unitMembers) +
                    " unit damage members take part without figures (their damage shows on the unit after the fight)");
    if (interceptor_ >= 0 && parts_[interceptor_].figureless)
        note(0, "Members: the interceptor takes part without figures");
    return CombatStatus();
}

// 4777: the defending side's figures answer (the retaliate byte, or 13.4's air opening)
bool Sim::defenderAnswers() const { return def_answers_; }

const CombatWeapon* Sim::weapon(int pi, const Fig& f, bool city) const {
    const CombatMemberArt& m = mem(pi, f);
    // 4777: an aircraft over a target that is not in the air drops its bomb when it has one (0321 13.5) [I for the choice]
    if (f.flyer && f.tpart >= 0 && !parts_[f.tpart].air)
        for (auto& w : m.weapons)
            if (w.dropped) return &w;
    const CombatWeapon* first = nullptr;
    for (auto& w : m.weapons) {
        if (city && w.vs_city) return &w;
        if (!w.vs_city && !first) first = &w;
    }
    return first ? first : (m.weapons.empty() ? nullptr : &m.weapons[0]);
}

// The attack action by weapon and target (0321 5.2 state 10, 5.5) [C for the codes; I for the choice].
int32_t Sim::strikeCode(int pi, const Fig& f) {
    const CombatMemberArt& m = mem(pi, f);
    const Part& P = parts_[pi];
    const bool city = f.tpart >= 0 && parts_[f.tpart].city;
    // 4777: an aircraft strikes with bombard over the ground or the sea, with attack in the air; the clip it has wins [I]
    if (m.domain == "Air" && (P.role == Role::Attacker || P.role == Role::Interceptor)) {
        const bool airTarget = f.tpart >= 0 && parts_[f.tpart].air;
        const int32_t first = airTarget ? 1100 : 1180, second = airTarget ? 1180 : 1100;
        const char* w1 = airTarget ? "Attack" : "Bombard";
        const char* w2 = airTarget ? "Bombard" : "Attack";
        if (has(m, w1) && timing(pi, f, first).found) return first;
        if (has(m, w2) && timing(pi, f, second).found) return second;
        return has(m, w1) ? first : (has(m, w2) ? second : 0);
    }
    // 4777 [C 13.5]: a figure answering an air side: a sea figure requests 1130 (anti-air), a land figure 1180;
    // EnableActions does not enter that choice
    if (f.tpart >= 0 && parts_[f.tpart].air && m.domain != "Air") return m.domain == "Sea" ? 1130 : 1180;
    const CombatWeapon* w = weapon(pi, f, city);
    const bool projectile = w && w->projectile;
    const bool ranged = (P.role == Role::Attacker && (in_.flags & kCombatRanged)) ||
                        (P.role != Role::Attacker && projectile);
    if (city) {
        if (ranged && has(m, "Bombard")) return 1180;
        if (has(m, "AttackCity")) return 1160;
    }
    if (ranged && has(m, "Bombard")) return 1180;
    // 5071 [C 0321 5.2 state 10: 1140 after a 1120 when RunCharge is enabled]: a rider striking from its run after a ride
    if (f.run_strike && f.last == 1120 && has(m, "RunCharge")) return 1140;
    if (f.last == 1120 && has(m, "RunCharge") && has(m, "AttackCharge")) return 1140;
    if (has(m, "Attack")) return 1100;
    if (has(m, "Bombard")) return 1180;
    if (has(m, "AttackCharge")) return 1140;
    if (has(m, "AttackCity")) return 1160;
    return 0;
}

// 4777: the participant a figure strikes at: the attacker at the defender (or the interceptor while the defender has no
// open need), the defender and the interceptor at the attacker; a damage member never strikes. -1: none open.
int Sim::targetPart(int pi) const {
    const Part& P = parts_[pi];
    if (P.role == Role::Member) return -1;
    if (P.role != Role::Attacker) return parts_[0].open() > 0 ? 0 : -1;
    if (parts_[1].open() > 0) return 1;
    if (interceptor_ >= 0 && parts_[interceptor_].open() > 0) return interceptor_;
    return -1;
}

bool Sim::pickTarget(int pi, int fi, int ti) {
    Fig& f = parts_[pi].figs[fi];
    Part& T = parts_[ti];
    f.tpart = ti;
    f.tfig = -1;
    if (T.figureless) return true;
    const bool deaths = T.planned > T.died;
    // a melee defender strikes at attacker figures that have made their approach [I]; 4777: every defending figure
    // waits for an aircraft to arrive over it
    const Role role = parts_[pi].role;
    const bool need_engaged =
        role == Role::Defender && (parts_[0].air || (!(in_.flags & kCombatRanged) &&
                                                     !(weapon(pi, f, false) && weapon(pi, f, false)->projectile)));
    std::vector<int> cand;
    int lone = -1, standing = 0;
    for (size_t i = 0; i < T.figs.size(); ++i) {
        const Fig& g = T.figs[i];
        if (g.st == FigState::Dying || g.st == FigState::Removed || g.returning) continue;
        if (need_engaged && !g.engaged) continue;
        ++standing;
        if (g.protect) {   // [C 13.4] every chooser skips it; [I] unless it is the only figure left (0557 n1, n2)
            lone = static_cast<int>(i);
            continue;
        }
        if (p_.deaths == DeathPick::LastFirst && deaths && !g.doomed) continue;
        cand.push_back(static_cast<int>(i));
    }
    if (cand.empty() && p_.deaths == DeathPick::LastFirst && deaths) {
        // the doomed figures are out of reach (dying, or not engaged): any figure but the protected one
        for (size_t i = 0; i < T.figs.size(); ++i) {
            const Fig& g = T.figs[i];
            if (g.st == FigState::Dying || g.st == FigState::Removed || g.protect || g.returning) continue;
            if (need_engaged && !g.engaged) continue;
            cand.push_back(static_cast<int>(i));
        }
    }
    if (cand.empty() && lone >= 0 && standing == 1) cand.push_back(lone);
    if (cand.empty()) {
        f.tpart = -1;
        return false;
    }
    int least = 1 << 30;
    for (int c : cand) least = std::min(least, T.figs[c].strikers);
    std::vector<int> best;
    for (int c : cand)
        if (T.figs[c].strikers == least) best.push_back(c);
    if (best.size() > 1 && reaches(pi, f) && parts_[pi].placed && T.placed) {
        // 0743 (CombatParams::melee_reach): stock's melee chooser (0x6F8DC0 [C], old build) takes the nearest opponent
        // figure not yet claimed by another striker (then the claimed ones); here among the candidates above
        int closest = best[0];   // not "near": a Windows macro
        double cd = 0;
        for (int c : best) {
            const double dx = static_cast<double>(T.figs[c].x) - f.x, dy = static_cast<double>(T.figs[c].y) - f.y;
            const double d = dx * dx + dy * dy;
            if (c == best[0] || d < cd) {
                closest = c;
                cd = d;
            }
        }
        f.tfig = closest;
    } else {
        f.tfig = best.size() == 1 ? best[0] : best[rng_.below(static_cast<int>(best.size()))];
    }
    T.figs[f.tfig].strikers++;
    return true;
}

void Sim::release(int pi, Fig& f) {
    if (f.reserved && !f.landed && f.tpart >= 0) parts_[f.tpart].reserved--;
    f.reserved = false;
    if (f.tpart >= 0 && f.tfig >= 0) parts_[f.tpart].figs[f.tfig].strikers--;
    (void)pi;
    f.tpart = f.tfig = -1;
}

void Sim::startStrike(int pi, int fi, double t) {
    Fig& f = parts_[pi].figs[fi];
    f.entry_wait = false;   // 4735
    const int32_t code = strikeCode(pi, f);
    f.code = code;
    f.play = code;
    f.next_hit = 0;
    f.landed = false;
    f.stuck = false;
    if (code == 0) {   // cannot happen for a figure that can attack; kept loud
        note(t, "NoAttackCode " + mem(pi, f).member);
        f.stuck = true;
        step(pi, fi, FigState::Strike, 0);
        return;
    }
    const CombatMemberArt& m = mem(pi, f);
    if (code == 1130 && m.anti_air == 0) {
        // 4777 [C 13.5, L 0477]: the member's machine has no transition for 1130 (the submarines): the answer never
        // plays, and the fight holds until its limit, as stock's strike on a submarine does
        f.stuck = true;
        f.tm = ActionTiming();
        note(t, "NoAntiAir " + std::to_string(parts_[pi].player) + "/" + std::to_string(parts_[pi].id) + " figure " +
                    std::to_string(f.index) + " " + m.member + ": its machine takes no 1130 (0321 13.5)");
        step(pi, fi, FigState::Strike, code);
        return;
    }
    f.tm = timing(pi, f, code);
    if (code == 1130 && !f.tm.found && m.anti_air != 0) {
        // 4777 [C 13.5]: a ship without an anti-air clip plays its attack instead (its machine maps 1130 to it)
        for (int32_t alt : {1100, 1180}) {
            const ActionTiming& a = timing(pi, f, alt);
            if (a.found) {
                f.tm = a;
                f.play = alt;
                break;
            }
        }
    }
    if (!f.tm.found) {
        // 4770's rule: the controller keeps the figure's state; the strike never plays, its hit never lands, and the
        // combat holds until the limit, as the EXE's held strike does (0321 13.4, 13.5) [I for the server's case]
        f.stuck = true;
        note(t, "MissingClip " + std::to_string(parts_[pi].player) + "/" + std::to_string(parts_[pi].id) + " figure " +
                    std::to_string(f.index) + " " + m.member + " code " + std::to_string(code) + ": " + f.tm.why);
    } else if (f.tm.hits.empty()) {
        note(t, "NoHitTrigger " + m.member + " code " + std::to_string(code));
    }
    step(pi, fi, FigState::Strike, code);
    f.live_wait = fb_ != nullptr && !f.stuck;   // 4782: the strike's clock starts when the figure plays the clip
}

// 4776: the move of an approaching figure to its target (unit-combat-server.md "Figure motion (4776)" M1-M3, [C] 0x6902C0
// and 0x61D7D0): straight at the target figure's position (a city's centre), ending within MoveRadius of it (a city:
// CombatParams::city_reach [I]); a destination nearer than ShortMoveRadius is a shuffle (1450) at ShortMoveRate, else
// the move runs at MoveRate; both x 64 world units per second. False when the figure is already within reach. The move
// is recorded in r_.moves and the figure's position becomes its arrival point. 4777: an aircraft flies at its altitude
// and never shuffles.
bool Sim::moveTo(int pi, int fi, double t, int32_t code, double& dur, bool no_shuffle) {
    Fig& f = parts_[pi].figs[fi];
    const CombatMotion& mo = mem(pi, f).motion;
    if (f.tpart < 0) return false;
    const Part& T = parts_[f.tpart];
    float tx = T.cx, ty = T.cy, reach = std::max(p_.city_reach, mo.move_radius);
    if (!T.figureless && f.tfig >= 0) {
        tx = T.figs[f.tfig].x;
        ty = T.figs[f.tfig].y;
        reach = mo.move_radius;
        // 0743 (CombatParams::melee_reach): stock's melee reach, the two AttackRadius values (0x5C7100 [C])
        if (reaches(pi, f)) reach = std::max(reach, mo.attack_radius + mem(f.tpart, T.figs[f.tfig]).motion.attack_radius);
    }
    const double dx = tx - f.x, dy = ty - f.y, d = std::sqrt(dx * dx + dy * dy);
    if (!(d > reach)) return false;
    const bool shuffle = !f.flyer && !no_shuffle && d < mo.shortRadius();   // 5057: a ride-on stays in its 1120
    const double speed = static_cast<double>(shuffle ? mo.shortRate() : mo.move_rate) * kMoveRateUnits;
    double travel = d - reach;
    if (!T.figureless && f.tfig >= 0 && reaches(pi, f)) {
        // 0743 (CombatParams::melee_reach): stock's figure aims at the nearest opponent figure (0x6F8DC0 [C]) and stops
        // within the reach of the two AttackRadius values; route S's target is the one the death rule allows (LastFirst:
        // the last figures by index, the rear of the formation), so the run is cut where it first comes within that
        // reach of any standing opponent figure: stock's stop, without running into the defender's ranks
        const double ux = dx / d, uy = dy / d;
        for (size_t i = 0; i < T.figs.size(); ++i) {
            const Fig& g = T.figs[i];
            if (g.st == FigState::Dying || g.st == FigState::Removed || g.returning) continue;
            const double r = static_cast<double>(mo.attack_radius) + mem(f.tpart, g).motion.attack_radius;
            const double wx = f.x - g.x, wy = f.y - g.y, b = ux * wx + uy * wy, c = wx * wx + wy * wy - r * r;
            if (c <= 0) return false;   // already within reach of a standing opponent figure: no move
            const double disc = b * b - c;
            if (disc < 0) continue;
            const double s = -b - std::sqrt(disc);
            if (s >= 0 && s < travel) travel = s;
        }
    }
    dur = travel / speed;
    FigureMove mv;
    mv.player = parts_[pi].player;
    mv.id = parts_[pi].id;
    mv.figure = f.index;
    mv.code = shuffle ? 1450 : code;
    mv.t0 = t;
    mv.t1 = t + dur;
    mv.x0 = f.x;
    mv.y0 = f.y;
    f.x = static_cast<float>(f.x + dx / d * travel);
    f.y = static_cast<float>(f.y + dy / d * travel);
    mv.x1 = f.x;
    mv.y1 = f.y;
    if (f.flyer) {
        mv.air = true;
        mv.z0 = f.away ? mo.altitude : 0.0f;
        mv.z1 = mo.altitude;
    }
    f.move = r_.moves.size();
    r_.moves.push_back(mv);
    return true;
}

// 4777: where a figure is at t (on its current move, else where it stands)
void Sim::position(int pi, int fi, double t, float& x, float& y) const {
    const Fig& f = parts_[pi].figs[fi];
    x = f.x;
    y = f.y;
    if (f.move == kNoMove || f.move >= r_.moves.size()) return;
    const FigureMove& m = r_.moves[f.move];
    if (t >= m.t1 || !(m.t1 > m.t0)) return;
    const double u = std::max(0.0, (t - m.t0) / (m.t1 - m.t0));
    x = static_cast<float>(m.x0 + (m.x1 - m.x0) * u);
    y = static_cast<float>(m.y0 + (m.y1 - m.y0) * u);
}

// 4777: an aircraft's flight home: from where it is (a flight out still under way is cut there) straight to its base at
// its MoveRate x 64, descending from its altitude; without positions it lasts CombatParams::air_flight [I]
void Sim::flyHome(int pi, int fi, double t) {
    Part& P = parts_[pi];
    Fig& f = P.figs[fi];
    if (f.reserved || f.st == FigState::Strike) release(pi, f);
    f.tpart = f.tfig = -1;
    const CombatMemberArt& m = mem(pi, f);
    double dur = p_.air_flight;
    if (P.placed) {
        float x = f.x, y = f.y;
        position(pi, fi, t, x, y);
        if (f.move != kNoMove && f.move < r_.moves.size() && t < r_.moves[f.move].t1) {
            FigureMove& cut = r_.moves[f.move];   // the flight out is cut where the figure turns
            cut.t1 = t;
            cut.x1 = x;
            cut.y1 = y;
        }
        f.x = x;
        f.y = y;
        const double dx = f.hx - x, dy = f.hy - y, d = std::sqrt(dx * dx + dy * dy);
        dur = d / (static_cast<double>(m.motion.move_rate) * kMoveRateUnits);
        FigureMove mv;
        mv.player = P.player;
        mv.id = P.id;
        mv.figure = f.index;
        mv.code = has(m, "Run") ? 1400 : 0;
        mv.t0 = t;
        mv.t1 = t + dur;
        mv.x0 = x;
        mv.y0 = y;
        mv.x1 = f.hx;
        mv.y1 = f.hy;
        mv.air = true;
        mv.back = true;
        mv.z0 = m.motion.altitude;
        mv.z1 = 0;
        f.move = r_.moves.size();
        r_.moves.push_back(mv);
        f.x = f.hx;
        f.y = f.hy;
    }
    f.returning = true;
    f.until = t + dur;
    step(pi, fi, FigState::Approach, has(m, "Run") ? 1400 : 0, 1);
}

// 4777: a city attacker's ranged strike (InitCityRangeStrike, 0321 5.5): the city has no figures; its volley leaves the
// city's centre at city_strike_at, one shot per open need of the defender (its planned deaths and its owed hit), the
// shots city_stagger apart, each landing city_flight after it leaves [I for every time]
void Sim::cityVolley(double t) {
    city_fired_ = true;
    Part& T = parts_[1];
    const int n = std::max(0, T.open());
    std::vector<int> order;   // the doomed figures first (LastFirst: last by index), then any standing one
    for (int i = static_cast<int>(T.figs.size()); i-- > 0;)
        if (T.figs[i].doomed && !T.figs[i].protect) order.push_back(i);
    for (int i = 0; i < static_cast<int>(T.figs.size()); ++i)
        if (!T.figs[i].doomed) order.push_back(i);
    for (int k = 0; k < n; ++k) {
        Landing l;
        l.launch = t + k * p_.city_stagger;
        l.time = l.launch + p_.city_flight;
        l.spart = 0;
        l.sfig = -1;
        l.tpart = 1;
        l.tfig = order.empty() ? -1 : order[static_cast<size_t>(k) % order.size()];
        l.projectile = true;
        l.first = true;
        l.has_origin = parts_[0].placed;
        l.ox = parts_[0].cx;
        l.oy = parts_[0].cy;
        l.order = order_++;
        // 0742 (additive): the shot at its LAUNCH, for the trigger stream: the landing's CombatHit comes only when it
        // lands, too late to fly the projectile (unit-combat-server.md 18.43). Aimed at the target figure where it
        // stands now (the landing may strike another doomed figure: land()'s choice)
        CombatHit s;
        s.time = l.time;
        s.launch = l.launch;
        s.player = parts_[0].player;
        s.id = parts_[0].id;
        s.target_player = T.player;
        s.target_id = T.id;
        s.projectile = true;
        s.city = true;
        s.effect = p_.city_effect;
        s.sound = p_.city_sound;
        s.has_origin = l.has_origin;
        s.origin[0] = l.ox;
        s.origin[1] = l.oy;
        s.origin[2] = l.oz;
        if (T.placed) {
            s.has_pos = true;
            const bool fig = l.tfig >= 0 && !T.figureless;
            s.pos[0] = fig ? T.figs[l.tfig].x : T.cx;
            s.pos[1] = fig ? T.figs[l.tfig].y : T.cy;
            if (fig) s.target_figure = T.figs[l.tfig].index;
        }
        s.result = l.tfig >= 0 && T.figs[l.tfig].doomed ? HitResult::Death : HitResult::OwedHit;
        l.shot = static_cast<int32_t>(r_.launches.size());
        s.shot = l.shot;
        r_.launches.push_back(s);
        T.reserved++;
        landings_.push_back(l);
    }
}

// 4735 (CombatParams::turn_first, the comment there): at its first look, a placed figure of a fight whose figures stand
// turns in place to face the opponent's centre when its unit's heading is more than turn_threshold off, playing the run;
// true when it turns (Turn until f.until, then Look again).
bool Sim::turnFirst(int pi, int fi, double t) {
    Part& P = parts_[pi];
    Fig& f = P.figs[fi];
    if (!p_.turn_first || f.turned) return false;
    f.turned = true;
    if (P.figureless || !P.placed || !P.unit || !P.unit->has_facing || f.flyer) return false;
    if (P.role != Role::Attacker && P.role != Role::Defender) return false;
    const CombatMemberArt& m = mem(pi, f);
    const bool ranged = (in_.flags & kCombatRanged) != 0 || parts_[0].city;
    const bool ship = P.role == Role::Attacker && m.domain == "Sea" && !p_.ships_approach;
    if (!ranged && !ship) return false;
    const Part& O = parts_[P.role == Role::Attacker ? 1 : 0];
    if (!O.placed) return false;
    float ox = O.cx, oy = O.cy;
    if (!O.figureless && !O.figs.empty()) {   // a unit: the centre of its standing figures
        double sx = 0, sy = 0;
        for (const Fig& g : O.figs) {
            sx += g.x;
            sy += g.y;
        }
        ox = static_cast<float>(sx / O.figs.size());
        oy = static_cast<float>(sy / O.figs.size());
    }
    if (ox == f.x && oy == f.y) return false;
    const float want = yawFacing(ox - f.x, oy - f.y);
    const float kTwoPi = 6.2831853f;
    const float off = std::remainder(want - P.unit->facing_yaw, kTwoPi);
    if (!(std::fabs(off) > p_.turn_threshold)) return false;
    if (!has(m, "Run") || !timing(pi, f, 1400).found) return false;   // no run clip: no turn (no hold through it)
    // M5 [C]: TurnRateMin + (TurnRateMax - TurnRateMin) x a fraction turns per second; the fraction 0.5 [I]
    const double rate = m.motion.turn_rate_min + 0.5 * (std::max(m.motion.turn_rate_max, m.motion.turn_rate_min) -
                                                         m.motion.turn_rate_min);
    double dur = rate > 0 ? std::fabs(off) / (kTwoPi * rate) : p_.turn_max;
    dur = std::min(std::max(dur, p_.turn_min), std::max(p_.turn_min, p_.turn_max));
    FigureMove mv;
    mv.player = P.player;
    mv.id = P.id;
    mv.figure = f.index;
    mv.code = 1400;
    mv.t0 = t;
    mv.t1 = t + dur;
    mv.x0 = mv.x1 = f.x;
    mv.y0 = mv.y1 = f.y;
    mv.turn = true;
    mv.yaw0 = P.unit->facing_yaw;
    mv.yaw1 = want;
    r_.moves.push_back(mv);
    f.until = t + dur;
    step(pi, fi, FigState::Turn, 1400, 1);
    return true;
}

// 4745 + 4747 (CombatParams::reform_first, the comment there): stock's phase 1 at the fight's first step. Each placed
// attacker or defender whose members' ReformBeforeCombat names its side re-lays its standing figures' slots about its own
// centre, turned from its heading to face the opponent's centre; figures take the nearest new slot and run there (Reform
// until f.until), then stop (1440) and stand. reform_end_: the last arrival plus its run stop (5050 (a),
// CombatParams::reform_ends_at_arrival: the last arrival).
void Sim::reformFirst(double t) {
    const double kTwoPi = 6.283185307179586;
    for (int pi = 0; pi < 2 && pi < static_cast<int>(parts_.size()); ++pi) {
        Part& P = parts_[pi];
        if (P.figureless || !P.placed || !P.unit || !P.unit->has_facing || P.figs.empty()) continue;
        const int bit = P.role == Role::Attacker ? 1 : (P.role == Role::Defender ? 2 : 0);
        bool reforms = false;
        for (const Fig& f : P.figs)
            if (!f.flyer && (mem(pi, f).motion.reform & bit)) reforms = true;
        if (!reforms) continue;
        const Part& O = parts_[pi == 0 ? 1 : 0];
        if (!O.placed) continue;
        float ox = O.cx, oy = O.cy;
        if (!O.figureless && !O.figs.empty()) {   // a unit: the centre of its standing figures (as turnFirst)
            double sx = 0, sy = 0;
            for (const Fig& g : O.figs) {
                sx += g.x;
                sy += g.y;
            }
            ox = static_cast<float>(sx / O.figs.size());
            oy = static_cast<float>(sy / O.figs.size());
        }
        float cx = P.cx, cy = P.cy;
        if (!P.unit->has_centre) {
            double sx = 0, sy = 0;
            for (const Fig& g : P.figs) {
                sx += g.x;
                sy += g.y;
            }
            cx = static_cast<float>(sx / P.figs.size());
            cy = static_cast<float>(sy / P.figs.size());
        }
        if (ox == cx && oy == cy) continue;
        const float want = yawFacing(ox - cx, oy - cy);
        const double d = std::remainder(static_cast<double>(want) - P.unit->facing_yaw, kTwoPi);
        // yaw a faces (sin a, -cos a): adding d turns every offset about the centre by d (counter-clockwise for d > 0)
        const double c = std::cos(d), s = std::sin(d);
        const size_t n = P.figs.size();
        std::vector<float> sx(n), sy(n);
        for (size_t i = 0; i < n; ++i) {
            const double ex = static_cast<double>(P.figs[i].x) - cx, ey = static_cast<double>(P.figs[i].y) - cy;
            sx[i] = static_cast<float>(cx + c * ex - s * ey);
            sy[i] = static_cast<float>(cy + s * ex + c * ey);
        }
        // living figures to slots by nearest: the nearest free (figure, slot) pair first, ties by figure then slot
        std::vector<int> slotOf(n, -1);
        std::vector<bool> taken(n, false);
        for (size_t k = 0; k < n; ++k) {
            int bf = -1, bs = -1;
            double bd = 0;
            for (size_t i = 0; i < n; ++i) {
                if (slotOf[i] >= 0) continue;
                for (size_t j = 0; j < n; ++j) {
                    if (taken[j]) continue;
                    const double dx = static_cast<double>(sx[j]) - P.figs[i].x, dy = static_cast<double>(sy[j]) - P.figs[i].y;
                    const double dd = dx * dx + dy * dy;
                    if (bf < 0 || dd < bd - 1e-9) {
                        bf = static_cast<int>(i);
                        bs = static_cast<int>(j);
                        bd = dd;
                    }
                }
            }
            slotOf[bf] = bs;
            taken[bs] = true;
        }
        for (size_t i = 0; i < n; ++i) {
            Fig& f = P.figs[i];
            f.turned = true;   // it faces the opponent after the reform: no turn_first turn
            if (f.flyer) continue;
            const CombatMemberArt& m = mem(pi, f);
            const float tx = sx[slotOf[i]], ty = sy[slotOf[i]];
            const double dx = static_cast<double>(tx) - f.x, dy = static_cast<double>(ty) - f.y;
            const double dist = std::sqrt(dx * dx + dy * dy);
            if (!(dist > p_.reform_min_move)) continue;   // stands where it is
            const CombatMotion& mo = m.motion;
            const bool shuffle = dist < mo.shortRadius();
            const double speed = static_cast<double>(shuffle ? mo.shortRate() : mo.move_rate) * kMoveRateUnits;
            const double dur = speed > 0 ? dist / speed : p_.approach;
            FigureMove mv;
            mv.player = P.player;
            mv.id = P.id;
            mv.figure = f.index;
            mv.code = shuffle ? 1450 : 1400;
            mv.t0 = t;
            mv.t1 = t + dur;
            mv.x0 = f.x;
            mv.y0 = f.y;
            mv.x1 = tx;
            mv.y1 = ty;
            f.move = r_.moves.size();
            r_.moves.push_back(mv);
            f.x = tx;
            f.y = ty;
            f.until = t + dur;
            double stop = 0;   // the run stop (1440) before it stands, as after an approach (ready()) [I]
            const ActionTiming& st = timing(pi, f, 1440);
            if (st.found) stop = st.duration;
            // 5050 (a): stock's phase 1 ends at the last arrival, not after the run stop (18.22)
            reform_end_ = std::max(reform_end_, f.until + (p_.reform_ends_at_arrival ? 0.0 : stop));
            step(pi, static_cast<int>(i), FigState::Reform, has(m, "Run") ? 1400 : 0, 1);
        }
    }
}

// 4750 (CombatParams::defend_from_stance, the comment there; unit-combat-server.md 10.14): a defender figure on its way
// to Defend first plays stock's phase-2 0xF, a 1450 request, for defend_stance_s; the tick's Defend case then requests
// 1280. True when it took the stance.
bool Sim::defendStance(int pi, int fi, double t) {
    Part& P = parts_[pi];
    Fig& f = P.figs[fi];
    if (!p_.defend_from_stance || P.role != Role::Defender || P.figureless || f.flyer) return false;
    if (parts_[0].air) return false;   // an air attacker: type 5's phases not read (stock's air opening sets 0xE)
    const CombatMemberArt& m = mem(pi, f);
    if (m.domain == "Sea" || m.domain == "Air") return false;   // ships take no 1450 from idle; no Walk either
    if (!has(m, "Walk") || !has(m, "BombardDefend") || !timing(pi, f, 1450).found) return false;
    if (f.last == 1400 || f.last == 1450 || f.last == 1120) return false;   // its machine is already in its run loop
    f.stance = true;
    f.until = t + std::max(p_.defend_stance_s, 0.15);
    step(pi, fi, FigState::Defend, 1450);
    return true;
}

// 5050 (b) (CombatParams::attack_from_stance, the comment there; unit-combat-server.md 10.14 section 4, 18.23): an
// attacking figure on its first way into Ready that has not moved first plays stock's phase-2 0xF, a 1450 request, for
// attack_stance_s (FigState::Turn); the tick's Turn case then calls ready() again, which requests 1600 and lets it strike
// attack_ready_s later (the machine's stop shuffle into combat ready, not the 1600 entry clip). True when it took it.
bool Sim::attackStance(int pi, int fi, double t) {
    Part& P = parts_[pi];
    Fig& f = P.figs[fi];
    if (!p_.attack_from_stance || f.astanced || P.role != Role::Attacker || P.figureless || f.flyer) return false;
    if (P.air) return false;   // an air attacker: type 5's phases not read (as 4750's skip)
    const CombatMemberArt& m = mem(pi, f);
    if (m.domain == "Sea" || m.domain == "Air") return false;   // ships take no 1450 from idle; no Walk either
    const bool walk = has(m, "Walk") && timing(pi, f, 1450).found;
    if (!walk && !p_.attack_hold_without_walk) return false;
    if (f.last == 1400 || f.last == 1450 || f.last == 1120) return false;   // its machine is already in its run loop
    f.astance = true;
    f.astance_walk = walk;
    f.until = t + std::max(p_.attack_stance_s, 0.15);
    step(pi, fi, FigState::Turn, walk ? 1450 : 0);   // without Walk: the phase-2 hold, no request (its machine idles)
    return true;
}

// 5074 (CombatParams::attack_entry_after_turn, the comment there; unit-combat-server.md 18.26.4): at the end of its
// turn_first turn an attacking figure of a land member without Walk stops (1000: its machine's run stop 1440, then idle)
// and looks again only after the stop, so that its 1600 comes from idle and plays the whole entry clip before the strike.
// The turn is stock's 0xF: a turn shorter than attack_turn_stance_s first keeps its run in place to the stance's end.
// True when it took the stance or the stop (the figure stays in Turn until f.until, or is in Look with 1000 until
// f.turn_stop_until).
bool Sim::entryAfterTurn(int pi, int fi, double t) {
    Part& P = parts_[pi];
    Fig& f = P.figs[fi];
    if (!p_.attack_entry_after_turn || P.role != Role::Attacker || P.figureless || f.flyer || P.air) return false;
    if (f.last != 1400) return false;   // the turn played the run (turnFirst's request)
    const CombatMemberArt& m = mem(pi, f);
    if (m.domain == "Sea" || m.domain == "Air") return false;
    if (has(m, "Walk") && timing(pi, f, 1450).found) return false;   // with Walk: 3 asks 1600 of the shuffle (5050 (b))
    if (!has(m, "CombatReady") || !has(m, "Idle")) return false;
    const ActionTiming& s = timing(pi, f, 1440);
    if (!s.found) return false;   // no run stop clip: no new hold
    const double stance_end = f.t0 + p_.attack_turn_stance_s;   // f.t0: the turn's start (turnFirst's step)
    if (t + kEps < stance_end) {   // stock's 0xF lasts its phase 2: the run plays on where the figure stands
        f.until = stance_end;
        return true;
    }
    f.astanced = true;           // ready(): the 5050 (b) no-Walk branch, 1600 from idle with its whole entry
    f.astance_walk = false;
    f.entry_wait = true;
    f.ran = false;
    f.turn_stop_until = t + std::max(static_cast<double>(s.duration), p_.attack_turn_stop_s);
    step(pi, fi, FigState::Look, 1000);
    return true;
}

// 5057 (A) (CombatParams::rush_ride_through, the comment there; unit-combat-server.md 18.29, 18.29.1): a rush member
// without AttackCharge whose charge (its last request 1120) ends at a target figure of a unit lands its hit at the arrival.
bool Sim::rides(int pi, const Fig& f) const {
    if (!p_.rush_ride_through || f.flyer || f.last != 1120 || f.tpart < 0 || f.tfig < 0) return false;
    if (parts_[f.tpart].figureless) return false;
    const CombatMemberArt& m = mem(pi, f);
    return m.rush && !has(m, "AttackCharge");
}

// 5057 (A): the next target lies ahead of the rider, within its ShortMoveRadius of the line of its last move [I]
bool Sim::inLine(int pi, const Fig& f) const {
    if (f.tpart < 0 || f.tfig < 0 || f.move == kNoMove || f.move >= r_.moves.size()) return false;
    const Part& T = parts_[f.tpart];
    if (T.figureless) return false;
    const FigureMove& mv = r_.moves[f.move];
    const double hx = mv.x1 - mv.x0, hy = mv.y1 - mv.y0, hl = std::sqrt(hx * hx + hy * hy);
    if (!(hl > 0)) return false;
    const double vx = T.figs[f.tfig].x - f.x, vy = T.figs[f.tfig].y - f.y;
    const double ahead = (vx * hx + vy * hy) / hl, side = std::fabs(vx * hy - vy * hx) / hl;
    return ahead > 0 && side <= static_cast<double>(mem(pi, f).motion.shortRadius());
}

// 5057 (A): a new target and its reservation, as look() takes them
bool Sim::reserveTarget(int pi, int fi) {
    Fig& f = parts_[pi].figs[fi];
    const int ti = targetPart(pi);
    if (ti < 0 || (p_.max_in_flight > 0 && parts_[ti].reserved >= p_.max_in_flight) || !pickTarget(pi, fi, ti)) return false;
    f.reserved = true;
    f.landed = false;
    parts_[ti].reserved++;
    return true;
}

// 5057 (A): the rider has arrived at its target figure. Its hit lands now (no attack clip: the 1120 keeps playing; the
// landing carries code 1120 and no trigger). Then: a next target in line -> ride on to it in the same 1120 (a move with no
// new request); else stop (1000 from the run loop: the machine's run stop 1440, then idle) and stand, with no 1600.
// [I] the 1140-from-the-run form of the open question would replace the landing below with startStrike() of a 1140 (and
// the ride-on would follow that strike's end instead of the arrival): only this function changes (18.29.1).
void Sim::rideLand(int pi, int fi, double t) {
    Part& P = parts_[pi];
    Fig& f = P.figs[fi];
    const CombatMemberArt& m = mem(pi, f);
    const Fig& g = parts_[f.tpart].figs[f.tfig];
    if (g.st != FigState::Dying && g.st != FigState::Removed) {
        // 5071: the defender's last planned death is not paid in the ride: the rider strikes it from its run (1140)
        // Live, only once its machine takes 1140 (its run loop: a rider that left combat ready for a short 1120 is still
        // starting its run): until then it holds at the arrival, charge_wait at most, then lands as 5057's rider
        if (runStrikes(pi, f) && keepsLast(f)) {
            if (takes(pi, f, 1140)) {
                f.rode = true;
                runStrike(pi, fi, t);
                return;
            }
            if (p_.charge_wait >= 0 && t + kEps < f.until + p_.charge_wait) return;
        }
        Landing l;
        l.time = l.launch = t;
        l.spart = pi;
        l.sfig = fi;
        l.code = 1120;
        l.tpart = f.tpart;
        l.tfig = f.tfig;
        l.w = weapon(pi, f, false);
        l.first = f.reserved && !f.landed;
        l.has_origin = P.placed;
        l.ox = f.x;
        l.oy = f.y;
        l.keep_last = runStrikes(pi, f);   // 5071
        f.landed = true;
        l.order = order_++;
        land(l);
    }
    release(pi, f);
    if (reserveTarget(pi, fi)) {
        double dur = 0;
        if (inLine(pi, f) && P.placed && parts_[f.tpart].placed && moveTo(pi, fi, t, 1120, dur, true)) {
            f.until = t + dur;
            step(pi, fi, FigState::Charge, 0, 2);
            return;
        }
        // not in line, or already in reach (a figure the landing spared, 4733: no second landing without a move): it
        // stops and looks again after its run stop
        release(pi, f);
    }
    // 5071: while the defender still has a planned death or an owed hit open, the rider strikes again from its run
    if (runStrikes(pi, f)) {
        f.rode = true;
        if (heldStrike(pi, fi, t)) return;
    }
    const ActionTiming& s = timing(pi, f, 1440);
    f.ride_stop_until = t + (s.found ? s.duration : 0.0);
    f.ran = false;
    f.rode = true;
    step(pi, fi, FigState::Wait, has(m, "Idle") ? 1000 : 0);
}

// 5071 (CombatParams::ride_strikes_while_held, the comment there; unit-combat-server.md 18.46): the rule applies to a
// rush rider of the attacker, at a figure unit as defender, whose member has a 1140 clip (else 5057's rule stays)
bool Sim::runStrikes(int pi, const Fig& f) {
    if (!p_.rush_ride_through || !p_.ride_strikes_while_held || f.flyer || pi != 0 || parts_.size() < 2) return false;
    const Part& P = parts_[pi];
    const Part& T = parts_[1];
    if (P.role != Role::Attacker || T.role != Role::Defender || T.figureless || !P.placed || !T.placed) return false;
    const CombatMemberArt& m = mem(pi, f);
    return m.rush && !has(m, "AttackCharge") && has(m, "RunCharge") && timing(pi, f, 1140).found;
}

// 5071: the rider's target part is the defender and a landing now would pay its last planned death
bool Sim::keepsLast(const Fig& f) const {
    if (f.tpart != 1) return false;
    const Part& T = parts_[1];
    return T.planned > T.died && T.planned - T.died <= 1;
}

// 5071: a strike from the run: 1140 (strikeCode with run_strike after a 1120); the machine goes back to combat ready by
// itself after it (14 -> 20 -> 12), so a later strike is a 1100 with no new entry. Live, the strike's clock waits for the
// clip (live_wait).
void Sim::runStrike(int pi, int fi, double t) {
    Fig& f = parts_[pi].figs[fi];
    f.held_ride = false;
    f.ran = false;
    f.engaged = true;
    f.run_strike = f.last == 1120;
    startStrike(pi, fi, t);
    f.run_strike = false;
    f.combat_ready = true;
}

// 5071: a rider standing after its ride strikes while the defender has a planned death or an owed hit open (0321 13.4's
// hold; 18.14 S1: melee figures are not gated by the opponent's open need): the nearest standing defender figure (the
// protected survivor only when it is the last), with no reservation; out of reach and still in its run it rides on to it
// in its 1120 and strikes on arrival; out of reach in combat ready it stands. False: it stands.
bool Sim::heldStrike(int pi, int fi, double t) {
    Part& P = parts_[pi];
    Fig& f = P.figs[fi];
    if (!f.rode || !runStrikes(pi, f)) return false;
    Part& T = parts_[1];
    if (T.need() <= 0) return false;
    int best = -1, lone = -1, standing = 0;
    double bestd = 0, loned = 0;
    for (size_t i = 0; i < T.figs.size(); ++i) {
        const Fig& g = T.figs[i];
        if (g.st == FigState::Dying || g.st == FigState::Removed || g.returning) continue;
        ++standing;
        const double dx = static_cast<double>(g.x) - f.x, dy = static_cast<double>(g.y) - f.y;
        const double d = std::sqrt(dx * dx + dy * dy);
        if (g.protect) {
            lone = static_cast<int>(i);
            loned = d;
            continue;
        }
        if (best < 0 || d < bestd) {
            best = static_cast<int>(i);
            bestd = d;
        }
    }
    if (best < 0 && lone >= 0 && standing == 1) {
        best = lone;
        bestd = loned;
    }
    if (best < 0) return false;
    const bool running = f.last == 1120;
    const bool inReach = bestd <= static_cast<double>(mem(pi, f).motion.move_radius);
    if (!inReach && !running) return false;
    if (inReach && !takes(pi, f, running ? 1140 : 1100)) return false;   // live: its machine does not take it now
    release(pi, f);
    f.tpart = 1;
    f.tfig = best;
    T.figs[best].strikers++;
    f.reserved = false;
    f.landed = false;
    double dur = 0;
    if (running && moveTo(pi, fi, t, 1120, dur, true)) {   // out of reach: on in its 1120 (no new request)
        f.held_ride = true;
        f.until = t + dur;
        step(pi, fi, FigState::Charge, 0, 2);
        return true;
    }
    runStrike(pi, fi, t);
    return true;
}

// 5071: live, the figure's machine takes `code` now (the controller's feedback); offline always
bool Sim::takes(int pi, const Fig& f, int32_t code) const {
    return !fb_ || fb_->accepts(parts_[pi].player, parts_[pi].id, f.index, code);
}

// 5057 (B): the defender of a type 2 fight waits while the attacker has not landed and still can
bool Sim::defenderHeld() const {
    if (!p_.defender_waits_first_landing || !type2_ || att_landed_) return false;
    for (const Fig& f : parts_[0].figs) {
        const FigState s = f.st;
        if (s == FigState::Look || s == FigState::Approach || s == FigState::Charge || s == FigState::Ready ||
            s == FigState::Strike || s == FigState::Turn || s == FigState::Reform)
            return true;
    }
    return false;
}

// 5067 S1 (CombatParams::type2_phase2, the comment there; unit-combat-server.md 18.29.3): stock's phase 2 of a type 2
// fight from phase 1's end: nobody looks until phase2_end_; a standing defender figure of a land member with Walk and a
// 1450 clip takes stock's 0xF (Turn with 1450) meanwhile, the attacker's figures stand with no request (stock's 3).
void Sim::type2Phase2(double t) {
    phase2_end_ = t + std::max(p_.type2_phase2_s, 0.0);
    Part& D = parts_[1];
    if (D.role != Role::Defender || D.figureless) return;
    for (size_t i = 0; i < D.figs.size(); ++i) {
        Fig& f = D.figs[i];
        if (f.st != FigState::Look && f.st != FigState::Wait) continue;   // dying, removed, or moving (a reform's run)
        if (f.flyer) continue;
        const CombatMemberArt& m = mem(1, f);
        if (m.domain == "Sea" || m.domain == "Air") continue;
        if (!has(m, "Walk") || !timing(1, f, 1450).found) continue;
        if (f.last == 1400 || f.last == 1450 || f.last == 1120) continue;   // its machine is already in its run loop
        f.p2stance = true;
        f.until = phase2_end_;
        step(1, static_cast<int>(i), FigState::Turn, 1450);
    }
}

void Sim::look(int pi, int fi, double t) {
    if (turnFirst(pi, fi, t)) return;
    Part& P = parts_[pi];
    Fig& f = P.figs[fi];
    const CombatMemberArt& m = mem(pi, f);
    const bool answers = P.role == Role::Attacker || P.role == Role::Interceptor ||
                         (P.role == Role::Defender && defenderAnswers());
    if (!f.can_attack || !answers) {
        // [C 5.1, 13.4]: cannot attack, or a defender that does not retaliate, or a damage member (a bystander, 4777
        // [I]): 1280 bombard defend; 4750: a defender's phase-2 stance (1450) first (CombatParams::defend_from_stance)
        if (f.st != FigState::Defend && !defendStance(pi, fi, t))
            step(pi, fi, FigState::Defend, has(m, "BombardDefend") ? 1280 : 0);
        return;
    }
    // 5057 (B) (CombatParams::defender_waits_first_landing): in type 2 the defender strikes from the attacker's first
    // landing on (18.15 phase 3); until then it waits as a defender without a target does
    if (P.role == Role::Defender && defenderHeld()) {
        if (f.st != FigState::Wait) ready(pi, fi, t, FigState::Wait);
        return;
    }
    const int ti = targetPart(pi);
    if (ti >= 0 && (p_.max_in_flight <= 0 || parts_[ti].reserved < p_.max_in_flight) && pickTarget(pi, fi, ti)) {
        Part& T = parts_[ti];
        f.reserved = true;
        f.landed = false;   // 4776: a new reservation (a figure that re-targets before its next strike must release it;
                            // found offline on 0557 h1 with positions: the leaked reservation held the fight to 12 s)
        T.reserved++;
        const bool placed = P.placed && T.placed;
        if (f.flyer) {
            // 4777: an aircraft flies out to its target (with positions straight at MoveRate x 64 to within MoveRadius
            // of the target figure, or of the target's centre; without, once for air_flight) [I], then strikes
            double dur = p_.air_flight;
            const int32_t code = has(m, "Run") ? 1400 : 0;
            const bool move = placed ? moveTo(pi, fi, t, code, dur) : !f.away;
            if (move) {
                f.away = true;
                f.ran = false;
                f.until = t + dur;
                step(pi, fi, FigState::Approach, code, 1);
                return;
            }
            f.engaged = true;
            ready(pi, fi, t, FigState::Ready);
            return;
        }
        // 4776: with positions, every attacking figure of a melee attack approaches, a projectile-weapon member too (the
        // EXE's fight phase puts the attacker's figures into the charge state 0xC, which moves them to their targets:
        // unit-combat-server.md "Figure motion (4776)" M3 [C]); without positions, 4775's rule (melee weapons only).
        // 4777: a ship approaches as a land figure does (ships_approach, the default: closer to 0557 n1-n3), or strikes
        // from where it stands [I]
        const bool ship = m.domain == "Sea" && !p_.ships_approach;
        const bool melee = !(P.role == Role::Attacker && (in_.flags & kCombatRanged)) && !ship &&
                           (placed || (p_.unplaced_attacker_approaches && P.role == Role::Attacker) ||
                            !(weapon(pi, f, T.city) && weapon(pi, f, T.city)->projectile));
        if (melee && P.role == Role::Attacker && T.city && p_.city_in_place) {
            // 4748 (CombatParams::city_in_place, 18.21 [L]): stock strikes a city from where the figures stand
            f.engaged = true;
            ready(pi, fi, t, FigState::Ready);
            return;
        }
        // 5061 (CombatParams::defender_step): from the attacker's first landing a type 2 defender figure runs to its
        // target as an attacker does under melee_reach (stock's phase 4, 18.45 [L])
        if (melee && ((P.role == Role::Attacker && (placed || !f.engaged)) || (placed && steps(pi, f)))) {
            double dur = p_.approach;
            // 4737: charge_any (the EXE's charge state 0xC for every approaching attacker, exe-audio.md 13.3 [C]); off: rush
            // only. A non-rush member charges only when its 1120 clip (and with AttackCharge its 1140) is in the install,
            // so a member whose charge art is missing keeps the run it had before (no new hold to the limit) [I]
            const bool charge_art = timing(pi, f, 1120).found && (!has(m, "AttackCharge") || timing(pi, f, 1140).found);
            const bool charges = has(m, "RunCharge") && (m.rush || (p_.charge_any && charge_art));
            int32_t code = charges ? 1120 : (has(m, "Run") ? 1400 : 0);
            if (placed && !moveTo(pi, fi, t, code, dur)) {   // already within reach of its target: no move
                f.engaged = true;
                // 5071: a rider still in its run (1140) or combat ready after its run strike (1100) strikes at once
                if (f.rode && (f.last == 1120 || f.combat_ready) && runStrikes(pi, f) &&
                    takes(pi, f, f.last == 1120 ? 1140 : 1100)) {
                    runStrike(pi, fi, t);
                    return;
                }
                // 5057 (A): a rider standing after a ride-through strikes 1100 at once only when its machine takes it
                // (live); else it enters combat ready first, as before
                if (f.rode && fb_ && fb_->accepts(P.player, P.id, f.index, strikeCode(pi, f))) {
                    startStrike(pi, fi, t);
                    return;
                }
                ready(pi, fi, t, FigState::Ready);
                return;
            }
            f.ran = true;
            f.until = t + dur;
            // the approach: run charge for a member with RunCharge (4737 [C]; with charge_any off: rush members only), else run
            if (code == 1120) {
                step(pi, fi, FigState::Charge, 1120, 2);
                if (!timing(pi, f, 1120).found) note(t, "MissingClip " + m.member + " code 1120 (approach shown idle)");
            } else {
                step(pi, fi, FigState::Approach, code, 1);
            }
            return;
        }
        if (ship && P.role == Role::Attacker) f.engaged = true;   // 4777: in reach where it stands
        if (f.rode && fb_ && fb_->accepts(P.player, P.id, f.index, strikeCode(pi, f))) {   // 5057 (A), as above
            startStrike(pi, fi, t);
            return;
        }
        ready(pi, fi, t, FigState::Ready);
        return;
    }
    if (f.can_attack && stepIn(pi, fi, t)) return;   // 5061 (b): no strike to hold, it steps in to the attackers
    if (heldStrike(pi, fi, t)) return;   // 5071: a rider after its ride strikes while the defender has a need open
    if (f.st != FigState::Wait) ready(pi, fi, t, FigState::Wait);
}

// 5061 (CombatParams::defender_step (b), unit-combat-server.md 18.45): a type 2 defender figure with no strike to hold
// steps to the two-AttackRadius reach of the nearest attacker figure that has made its approach, stands and is not
// walking home, as stock's figures all charge in phase 4 [L]; the move is moveTo's (cut at the first standing attacker
// figure within reach, the shuffle inside ShortMoveRadius), the clip the approach's. False when no such figure is out
// of reach by more than defender_step_slack (or the rule does not apply): the caller lets the figure wait.
bool Sim::stepIn(int pi, int fi, double t) {
    Fig& f = parts_[pi].figs[fi];
    if (!steps(pi, f) || f.tpart >= 0) return false;
    // nothing left to strike (no landing due, no side's need): the end test finishes the fight in this step, and a step
    // begun now would be cut by the walk home at once (34 of 174 offline steps did so before this test)
    bool due = !landings_.empty();
    for (const Part& P : parts_) due = due || P.need() > 0;
    if (!due) return false;
    const Part& T = parts_[0];
    const CombatMemberArt& m = mem(pi, f);
    int nearest = -1;   // not "near": a Windows macro
    double nd = 0;
    for (size_t i = 0; i < T.figs.size(); ++i) {
        const Fig& g = T.figs[i];
        if (g.st == FigState::Dying || g.st == FigState::Removed || g.returning || !g.engaged) continue;
        const double dx = static_cast<double>(g.x) - f.x, dy = static_cast<double>(g.y) - f.y;
        const double d = dx * dx + dy * dy;
        if (nearest < 0 || d < nd) {
            nearest = static_cast<int>(i);
            nd = d;
        }
    }
    if (nearest < 0) return false;
    const double r = static_cast<double>(m.motion.attack_radius) + mem(0, T.figs[nearest]).motion.attack_radius;
    if (!(std::sqrt(nd) > r + std::max(0.0f, p_.defender_step_slack))) return false;
    const bool charge_art = timing(pi, f, 1120).found && (!has(m, "AttackCharge") || timing(pi, f, 1140).found);
    const bool charges = has(m, "RunCharge") && (m.rush || (p_.charge_any && charge_art));
    const int32_t code = charges ? 1120 : (has(m, "Run") ? 1400 : 0);
    double dur = 0;
    f.tpart = 0;   // moveTo's destination: that figure, then no target (no strike, no claim)
    f.tfig = nearest;
    const bool moved = moveTo(pi, fi, t, code, dur);
    f.tpart = f.tfig = -1;
    if (!moved) return false;
    f.ran = true;
    f.until = t + dur;
    if (code == 1120) step(pi, fi, FigState::Charge, 1120, 2);
    else step(pi, fi, FigState::Approach, code, 1);
    return true;
}

// Ready (5) or Wait (2) with 1600 combat ready; the first request starts the entry into combat ready, which a strike
// waits for (0321 5.3: 5 -> 10 "when its animation is done") [C for the wait, I that it is the 1600 clip's length].
void Sim::ready(int pi, int fi, double t, FigState st) {
    Fig& f = parts_[pi].figs[fi];
    const CombatMemberArt& m = mem(pi, f);
    if (!has(m, "CombatReady")) {
        step(pi, fi, st, st == FigState::Wait && has(m, "Idle") ? 1000 : 0);
        return;
    }
    if (f.combat_ready) {
        // already combat ready (the machine goes back there after a strike by itself): no new request, which would
        // overwrite an attack request the machine has not read yet (path 4 is a level; 0557 h1 / m1 offline) [I]. The
        // state's own request (0321 5.2: 1600) still counts as the figure's last action for the victory test.
        step(pi, fi, st, 0);
        parts_[pi].figs[fi].last = 1600;
        return;
    }
    // 5050 (b): an attacker's first Ready takes stock's phase-2 stance first; after it, 1600 meets the machine in its run
    // loop (stop shuffle to combat ready, no entry clip) and the strike may start attack_ready_s later
    if (st == FigState::Ready && p_.attack_from_stance) {
        if (f.astanced && f.astance_walk) {
            f.combat_ready = true;
            f.ran = false;
            f.ready_at = t + std::max(p_.attack_ready_s, 0.0);
            step(pi, fi, st, 1600);
            return;
        }
        // without Walk (attack_hold_without_walk): after the hold, 1600 from idle with its whole entry clip before the
        // strike (no live shortcut: stock's crew plays the entry, then fires)
        if (f.astanced) f.entry_wait = true;
        else if (attackStance(pi, fi, t)) return;
    }
    // 4746 [C exe-audio.md 13.3, the state setter's state 2]: a waiting figure requests 1600 only when its last request is
    // a movement and its member enables Walk (1450), 1000 after a movement without Walk, and nothing when it was not
    // moving (it stays in its clip, and is not combat ready: a later Ready requests 1600 and waits for its entry)
    if (st == FigState::Wait && p_.ready_after_move) {
        const bool moved = f.last == 1400 || f.last == 1450 || f.last == 1120;
        if (!moved) {
            step(pi, fi, st, 0);
            return;
        }
        if (!has(m, "Walk")) {
            step(pi, fi, st, has(m, "Idle") ? 1000 : 0);
            return;
        }
    }
    f.combat_ready = true;
    const ActionTiming& r = timing(pi, f, 1600);
    // after a run the machine plays its run stop (1440) before it takes 1600 (0557 h1 offline: 1440 for 0.9 s) [I]
    double stop = 0;
    if (f.engaged && f.ran) {   // 4776: only after a move (a figure already within reach has no run to stop)
        const ActionTiming& s = timing(pi, f, 1440);
        if (s.found) stop = s.duration;
    }
    f.ran = false;
    f.ready_at = t + stop + (r.found ? r.duration : 0.0);
    step(pi, fi, st, 1600);
}

// 4777: a projectile's flight the core times itself (only for the special cases: an aircraft's weapon, a dropped bomb,
// a shot at an air figure; every other projectile keeps CombatWeapon::flight, the caller's): a dropped bomb falls from
// the striker's altitude, any other covers the distance to its target, at ProjectileSpeed x 64 world units per second
// [I: the speed's unit]; 0 when not known
// 4792 (widened): every other Transfer hit with both sides placed lands after its flight too, as the EXE counts it at
// the arrival (effects-server.md 1.4, the 0x34 landing): distance / 64 / speed clamped to 0.001-5 s, the speed the
// weapon's (the caller puts the effect's fMovementSpeed there when it has one), a NULL ProjectileSpeed flying at 2.0
// plots per second (effects-server.md 2.4 [L]). The distance is the ground distance of the two figures (the core has no
// launch or target height) [I].
double Sim::flightOf(int pi, const Fig& f, const CombatWeapon* w) const {
    if (!w || f.tpart < 0) return -1;
    const Part& P = parts_[pi];
    const Part& T = parts_[f.tpart];
    if (!(f.flyer || w->dropped || T.air)) {
        if (!(f.tm.transfer && P.placed && T.placed)) return -1;
        float tx = T.cx, ty = T.cy;
        if (!T.figureless && f.tfig >= 0) {
            tx = T.figs[f.tfig].x;
            ty = T.figs[f.tfig].y;
        }
        const double dx = tx - f.x, dy = ty - f.y;
        return static_cast<double>(effects::flightTime(static_cast<float>(std::sqrt(dx * dx + dy * dy)),
                                                       w->speed > 0 ? w->speed : effects::kNullSpeed));
    }
    if (!(w->speed > 0)) return -1;
    const double speed = static_cast<double>(w->speed) * kMoveRateUnits;
    if (w->dropped) return f.flyer ? static_cast<double>(mem(pi, f).motion.altitude) / speed : 0.0;
    if (!(P.placed && T.placed)) return -1;
    float tx = T.cx, ty = T.cy;
    if (!T.figureless && f.tfig >= 0) {
        tx = T.figs[f.tfig].x;
        ty = T.figs[f.tfig].y;
    }
    const double dx = tx - f.x, dy = ty - f.y;
    return std::sqrt(dx * dx + dy * dy) / speed;
}

void Sim::progress(int pi, int fi, double t) {
    Part& P = parts_[pi];
    Fig& f = P.figs[fi];
    if (f.stuck) return;
    if (f.live_wait) {
        // 4782 live: the hit moments and the strike's end count from the moment the controller's figure plays the
        // attack clip (A or B); until then the strike holds (as the EXE's simulation waits for the model). 4777: a
        // 1130 answered with the attack clip counts that clip.
        if (!fb_->playing(P.player, P.id, f.index, f.code) &&
            !(f.play != f.code && fb_->playing(P.player, P.id, f.index, f.play)))
            return;
        f.live_wait = false;
        f.t0 = t;
    }
    bool spent = false;   // 4777: a one-shot striker's first hit
    while (f.next_hit < f.tm.hits.size() && f.t0 + f.tm.hits[f.next_hit] <= t + kEps) {
        const int32_t trig = f.next_hit < f.tm.hit_ids.size() ? f.tm.hit_ids[f.next_hit] : -1;
        const double launch = f.t0 + f.tm.hits[f.next_hit++];
        if (f.tpart < 0) continue;
        Landing l;
        l.launch = launch;
        l.spart = pi;
        l.sfig = fi;
        l.code = f.code;
        l.trigger = trig;
        l.tpart = f.tpart;
        l.tfig = f.tfig;
        const CombatWeapon* w = weapon(pi, f, parts_[f.tpart].city);
        l.w = w;
        l.projectile = f.tm.transfer || (w && w->dropped);
        const double own = flightOf(pi, f, w);
        if (own >= 0) l.time = launch + own;   // 4777
        else l.time = launch + (f.tm.transfer && w ? std::max(0.f, w->flight) : 0.0);
        l.first = f.reserved && !f.landed;
        l.has_origin = P.placed;
        l.ox = f.x;
        l.oy = f.y;
        l.oz = f.flyer && f.away ? mem(pi, f).motion.altitude : 0.0f;
        f.landed = true;
        l.order = order_++;
        if (l.time <= t + kEps) land(l);
        else landings_.push_back(l);
        if (w && w->one_shot) {
            spent = true;
            break;
        }
    }
    if (spent) {
        // [C 4.2 slot 4]: a collision attacker (HasCollisionAttack, M6: a missile) is reported dead at its own hit
        release(pi, f);
        die(pi, fi, t);
        return;
    }
    if (t - f.t0 + kEps >= f.tm.duration) {
        // the strike is done [C 5.3]: a one-shot weapon's striker dies; else it looks again
        const CombatWeapon* w = weapon(pi, f, f.tpart >= 0 && parts_[f.tpart].city);
        release(pi, f);
        if (w && w->one_shot) {
            die(pi, fi, t);
            return;
        }
        f.st = FigState::Look;
        look(pi, fi, t);
    }
}

void Sim::land(const Landing& l) {
    Part& T = parts_[l.tpart];
    if (l.spart == 0 && l.sfig >= 0) att_landed_ = true;   // 5057 (B): the end of phase 3
    CombatHit h;
    h.time = l.time;
    h.launch = l.launch;
    h.shot = l.shot;   // 0742
    const Part& S = parts_[l.spart];
    h.player = S.player;
    h.id = S.id;
    h.figure = l.sfig >= 0 ? S.figs[l.sfig].index : 0;
    h.code = l.code;
    h.trigger = l.trigger;
    h.target_player = T.player;
    h.target_id = T.id;
    h.projectile = l.projectile;
    h.area = l.area;
    // 4777: what it shows and sounds, where it leaves and lands
    if (l.sfig < 0) {
        h.city = true;
        h.effect = p_.city_effect;
        h.sound = p_.city_sound;
    } else if (l.w) {
        h.effect = l.w->effect;
        h.sound = l.w->sound;
        h.scale = l.w->effect_scale;
    }
    h.has_origin = l.has_origin;
    h.origin[0] = l.ox;
    h.origin[1] = l.oy;
    h.origin[2] = l.oz;
    if (l.first) {
        T.reserved--;
    }
    int victim = -1;
    if (T.planned > T.died && !T.figureless) {
        // a planned death [C 13.4]: the hit figure, or (LastFirst) the doomed figure it was aimed at, else the last
        // doomed figure still standing
        auto alive = [&](int i) {
            return T.figs[i].st != FigState::Dying && T.figs[i].st != FigState::Removed;
        };
        if (l.tfig >= 0 && alive(l.tfig) && !T.figs[l.tfig].protect &&
            (p_.deaths == DeathPick::Targeted || T.figs[l.tfig].doomed))
            victim = l.tfig;
        for (int i = static_cast<int>(T.figs.size()); victim < 0 && i-- > 0;)
            if (alive(i) && !T.figs[i].protect && (p_.deaths == DeathPick::Targeted || T.figs[i].doomed)) victim = i;
    }
    // 4733 [C 13.4]: the participant's last standing figure is spared while the other side still has planned deaths or
    // an owed hit open; it is only hit (it pays an owed hit of its side below), and a later landing kills it
    int spared = -1;
    if (victim >= 0 && spares(l)) {
        spared = victim;
        victim = -1;
    }
    const int at = victim >= 0 ? victim : (spared >= 0 ? spared : l.tfig);
    if (T.placed) {
        h.has_pos = true;
        if (at >= 0 && !T.figureless) {
            h.pos[0] = T.figs[at].x;
            h.pos[1] = T.figs[at].y;
        } else {
            h.pos[0] = T.cx;
            h.pos[1] = T.cy;
        }
        if (l.w && l.w->dropped && l.has_origin) {   // [C 13.5]: a bomb lands below the striker, at ground height
            h.pos[0] = l.ox;
            h.pos[1] = l.oy;
        }
        if (at >= 0 && !T.figureless && T.figs[at].flyer && T.figs[at].away)
            h.pos[2] = mem(l.tpart, T.figs[at]).motion.altitude;
    }
    if (victim >= 0) {
        h.target_figure = T.figs[victim].index;
        h.result = HitResult::Death;
        T.died++;
        // 4783 (W33, C2) [C 13.4]: the EXE's hit pays one owed hit of the struck side with every figure it strikes, a
        // death too, so a killed side's owed hit goes with its first death (no figure is left to take it later)
        if (p_.owed_paid_by_any_hit && T.owed > T.paid) T.paid++;
        r_.hits.push_back(h);
        die(l.tpart, victim, now_);
        // 4783 (W33, C1) [C 5.4]: one hit strikes several figures when its weapon names a VisKillStrength range: the
        // nearest standing figures take the planned deaths still open. Their records carry no effect, sound or trigger
        // (the aimed figure's record has them, as an area share's)
        for (int extra = struckCount(l) - 1; extra > 0 && T.planned > T.died; --extra) {
            if (l.keep_last && T.planned - T.died <= 1) break;   // 5071: the ride leaves the last death to a strike
            const int k = nearestVictim(l.tpart, victim);
            if (k < 0 || spares(l)) break;   // 4733: the last standing figure is spared (only hit)
            CombatHit e = h;
            e.target_figure = T.figs[k].index;
            e.effect.clear();
            e.sound.clear();
            e.scale = 0;
            e.trigger = -1;
            if (T.placed && !(l.w && l.w->dropped)) {
                e.pos[0] = T.figs[k].x;
                e.pos[1] = T.figs[k].y;
            }
            T.died++;
            r_.hits.push_back(e);
            die(l.tpart, k, now_);
        }
        if (!l.area) area(l);
        return;
    }
    h.target_figure = spared >= 0 ? T.figs[spared].index : (l.tfig >= 0 && !T.figureless ? T.figs[l.tfig].index : -1);
    if (T.owed > T.paid) {
        T.paid++;
        h.result = HitResult::OwedHit;
    } else {
        h.result = HitResult::Spent;
    }
    r_.hits.push_back(h);
    if (T.city && l.sfig >= 0) {
        // A city has no figures to strike back: the striking side's planned deaths fall at its first hit on the city
        // [I; the literal hold of 0321 13.4 would run to the 12 s limit, but 0557's recorded melee attacks on a city
        // with a planned death (c1, c2) ended in 2.97 s and 3.50 s on stock]. 4777: the city's shot leaves its centre.
        Part& SP = parts_[l.spart];
        for (int i = static_cast<int>(SP.figs.size()); SP.planned > SP.died && i-- > 0;) {
            Fig& g = SP.figs[i];
            if (g.st == FigState::Dying || g.st == FigState::Removed || g.protect) continue;
            if (p_.deaths == DeathPick::LastFirst && !g.doomed) continue;
            SP.died++;
            CombatHit d = h;
            d.player = T.player;
            d.id = T.id;
            d.figure = 0;
            d.code = 0;
            d.trigger = -1;
            d.target_player = SP.player;
            d.target_id = SP.id;
            d.target_figure = g.index;
            d.projectile = false;
            d.result = HitResult::Death;
            d.area = false;
            d.city = true;
            d.effect = p_.city_effect;
            d.sound = p_.city_sound;
            d.scale = 1;
            d.has_origin = T.placed;
            d.origin[0] = T.cx;
            d.origin[1] = T.cy;
            d.origin[2] = 0;
            d.has_pos = SP.placed;
            d.pos[0] = g.x;
            d.pos[1] = g.y;
            d.pos[2] = 0;
            r_.hits.push_back(d);
            die(l.spart, i, now_);
        }
    }
    if (!l.area) area(l);
}

// 4783 (W33, C1) [C combat-choreography.md 5.4]: how many figures one landing strikes, by the EXE's rule: one when the
// weapon's VisKillStrengthMax is 0; else max(1, ceil(fraction x 0.01 x the target's figures)), the fraction being Min
// when Max - Min is below 1, else Min plus a draw in [0, Max - Min). The draw comes from the fight's seeded generator,
// so a run is the same for a seed. An area share, a city's shot (no weapon) or a figureless target: one.
int Sim::struckCount(const Landing& l) {
    if (!p_.vis_kill || l.area || !l.w || !(l.w->vis_kill_max > 0)) return 1;
    const Part& T = parts_[l.tpart];
    if (T.figureless || !T.unit) return 1;
    const double lo = l.w->vis_kill_min, range = l.w->vis_kill_max - lo;
    const double frac = range < 1.0 ? lo : lo + rng_.uniform() * range;
    const double n = std::ceil(frac * 0.01 * static_cast<double>(T.unit->figures()) - 1e-9);
    return n < 1 ? 1 : static_cast<int>((std::min)(n, 64.0));
}

// 4783 (W33, C1) [C 5.4]: the next figure a hit that strikes several takes: the nearest standing figure to the aimed one
// (squared distance; without positions the last standing first, as land()'s own fallback) that may die now: not the
// protected survivor and, with LastFirst, a doomed one. -1: none.
int Sim::nearestVictim(int ti, int aimed) const {
    const Part& T = parts_[ti];
    int best = -1;
    double bestD = 0;
    for (size_t i = 0; i < T.figs.size(); ++i) {
        const Fig& g = T.figs[i];
        if (static_cast<int>(i) == aimed || g.st == FigState::Dying || g.st == FigState::Removed || g.protect) continue;
        if (p_.deaths == DeathPick::LastFirst && !g.doomed) continue;
        double d = -static_cast<double>(i);
        if (T.placed && aimed >= 0) {
            const double dx = static_cast<double>(g.x) - T.figs[aimed].x, dy = static_cast<double>(g.y) - T.figs[aimed].y;
            d = dx * dx + dy * dy;
        }
        if (best < 0 || d < bestD) {
            best = static_cast<int>(i);
            bestD = d;
        }
    }
    return best;
}

// 4733 (W36) [C combat-choreography.md 13.4]: whether this landing spares the struck participant's last standing figure:
// it is the attacker's or the defender's only figure not dying, and the other side (0 <-> 1) still has planned deaths
// unplayed or an owed hit open. The spared figure is only hit. [I]: area shares and the interceptor or damage members
// (participants 2 and up) are left to the 4775 rule (an area share is one landing per death: sparing would drop it).
bool Sim::spares(const Landing& l) const {
    if (!p_.spare_last_figure || l.area || l.tpart < 0 || l.tpart > 1) return false;
    const Part& T = parts_[l.tpart];
    if (T.figureless) return false;
    int left = 0;
    for (const Fig& g : T.figs)
        if (g.st != FigState::Dying && g.st != FigState::Removed) ++left;
    if (left != 1) return false;
    const Part& O = parts_[l.tpart == 0 ? 1 : 0];
    return !O.figureless && O.need() > 0;
}

// 4777: the area of an attacker's first hit on the defender (or the nuke's plot): every damage member, and with a nuclear
// strike the defender too, loses its planned deaths at once, AreaDamageDelay after the hit ([D] the weapon row; [I] that
// the deaths fall together; 0321 5.5: the damage members are participants 3 onward); a WaitForEffectCompletion weapon
// holds the fight for its effect (CombatClips::effectDuration, else effect_wait; a weapon without a HitEffect holds to the
// limit with unnamed_effect_holds: stock's Atomic Bomb, [L 0553])
void Sim::area(const Landing& l) {
    if (area_done_ || l.spart != 0 || l.tpart != 1) return;
    const bool nuclear = in_.nuclear_level > 0;
    area_done_ = true;
    const double delay = l.w ? std::max(0.0f, l.w->area_delay) : 0.0;
    for (size_t k = 1; k < parts_.size(); ++k) {
        Part& M = parts_[k];
        const bool member = M.role == Role::Member;
        if (!(member || (nuclear && k == 1)) || M.figureless) continue;
        const int left = static_cast<int>(M.planned - M.died) - M.reserved;
        for (int n = 0; n < left; ++n) {
            Landing a = l;
            a.tpart = static_cast<int>(k);
            a.tfig = -1;
            a.area = true;
            a.w = nullptr;   // the blast's effect and sound are the first hit's; the shares carry none
            a.first = true;
            a.time = now_ + delay;
            a.trigger = -1;
            a.order = order_++;
            M.reserved++;
            landings_.push_back(a);
        }
    }
    if (l.w && l.w->wait_effect) {
        if (l.w->effect.empty()) {
            if (p_.unnamed_effect_holds) hold_limit_ = true;
        } else {
            double d = clips_.effectDuration(l.w->effect);
            if (!(d >= 0) || !std::isfinite(d)) d = p_.effect_wait;
            hold_until_ = std::max(hold_until_, l.time + d);
        }
    }
}

void Sim::die(int pi, int fi, double t) {
    Part& P = parts_[pi];
    Fig& f = P.figs[fi];
    if (f.st == FigState::Strike || f.reserved) release(pi, f);
    f.returning = false;
    const ActionTiming& d = timing(pi, f, 1200);
    // [C 5.3]: removed when its 1200 clip has played or 3.0 s have passed
    double len = p_.death_cap;
    if (d.found && d.duration > 0) len = std::min<double>(len, d.duration);
    f.dying_at = t;
    f.death_known = false;
    if (p_.death_by_variant && !fb_) {
        // 5053 R2 self-timed: the core draws A or B 50/50 as the machines do (unit-controller.md 6.5); live the
        // controller's draw is read in the tick (CombatFeedback::playingVariant)
        f.death_known = true;
        if (death_rng_.below(2) == 1) {
            const ActionTiming& b = timing(pi, f, 2200);
            if (b.found && b.duration > 0) len = std::min<double>(p_.death_cap, b.duration);
        }
    }
    f.until = t + len;
    step(pi, fi, FigState::Dying, 1200);
}

// 5072 (CombatParams::type2_end_after_dying, unit-combat-server.md 18.48): the figures the rule holds and lets wait at home
bool Sim::heldRider(size_t pi, const Fig& f) const {
    return p_.type2_end_after_dying && type2_ && parts_[pi].role == Role::Attacker && !f.flyer &&
           mem(static_cast<int>(pi), f).rush;
}

// 5072: in the fight's last step every rider that waits at home refaces (7, 1000): its run stop comes after the end
void Sim::homeWaitsReface(double t) {
    (void)t;
    for (size_t pi = 0; pi < parts_.size(); ++pi)
        for (size_t i = 0; i < parts_[pi].figs.size(); ++i) {
            Fig& f = parts_[pi].figs[i];
            if (!f.home_wait) continue;
            f.home_wait = false;
            step(static_cast<int>(pi), static_cast<int>(i), FigState::Reface, has(mem(static_cast<int>(pi), f), "Idle") ? 1000 : 0);
        }
}

bool Sim::victoryEligible(int pi, const Fig& f) const {
    // [C 5.3]: a survivor whose last action was 1600 or 1000, with 1800 enabled, not an air figure, and its side's
    // count test; the count test read as "the opponent is killed" [I] (VictoryRule)
    const CombatMemberArt& m = mem(pi, f);
    if (!(f.last == 1600 || f.last == 1000) || !m.victory || m.domain == "Air") return false;
    // 5059 (CombatParams::victory_needs_word, the comment there; unit-combat-server.md 18.36): stock's enable word Victory
    if (p_.victory_needs_word && !has(m, "Victory")) return false;
    const Role role = parts_[pi].role;
    if (role == Role::Member) return false;
    if (p_.victory == VictoryRule::Always) return true;
    return parts_[role == Role::Attacker ? 1 : 0].killed;
}

void Sim::finish(int pi, int fi, double t, bool at_limit) {
    Part& P = parts_[pi];
    Fig& f = P.figs[fi];
    if (f.reserved || f.st == FigState::Strike) release(pi, f);
    f.returning = false;
    const CombatMemberArt& m = mem(pi, f);
    // [C 5.3]: refaced; victory about three times in four (random(100) > 25), else 0xB. At the limit 0/1 -> 0xB [C];
    // every other figure is ended the same way at the limit [I].
    if (!at_limit && victoryEligible(pi, f) && rng_.below(100) > p_.victory_threshold) {
        step(pi, fi, FigState::Victory, 1800);
        return;
    }
    if (!at_limit && p_.walk_home && walkHome(pi, fi, t)) return;   // 4742
    if (!at_limit && p_.end_after_dying && endShuffle(pi, fi, t)) return;   // 5053 R3
    step(pi, fi, FigState::Done, has(m, "Idle") ? 1000 : 0);
}

// 5053 (CombatParams::end_after_dying, the comment there; unit-combat-server.md 18.26 (b) R3): a finished defender figure
// in its bombard-defend stance (1280) of a land member with Walk, BombardDefend and a 1450 clip (4750's test) leaves
// the stance with stock's end shuffle: 1450 (FigState::Done) for end_stance_s, then the tick refaces it (Reface, 1000).
// False: it ends at once as before.
bool Sim::endShuffle(int pi, int fi, double t) {
    Part& P = parts_[pi];
    Fig& f = P.figs[fi];
    if (P.role != Role::Defender || P.figureless || f.flyer || f.st != FigState::Defend || f.last != 1280) return false;
    const CombatMemberArt& m = mem(pi, f);
    if (m.domain == "Sea" || m.domain == "Air") return false;   // ships take no 1450; no Walk either
    if (!has(m, "Walk") || !has(m, "BombardDefend") || !timing(pi, f, 1450).found) return false;
    f.end_shuffle = true;
    f.until = t + std::max(p_.end_stance_s, 0.0);
    step(pi, fi, FigState::Done, 1450);
    return true;
}

// 4742 (CombatParams::walk_home, the comment there): a finished survivor of a placed participant walks to the start slot
// of its rank among the participant's survivors (figure order; the victory figures count, they stay drawn), inside the
// fight: its current move is cut where it is (as flyHome cuts a flight), a 1400 run (1450 inside ShortMoveRadius) at the
// 4776 motion, Done with 1400 until it arrives (tick: Reface with 1000). False: no walk (an aircraft, no positions, home
// within walk_home_min_move); the caller ends it as before (a cut move then ends where the figure stands).
bool Sim::walkHome(int pi, int fi, double t) {
    Part& P = parts_[pi];
    Fig& f = P.figs[fi];
    if (P.figureless || !P.placed || f.flyer) return false;
    int rank = 0;
    for (int i = 0; i < fi; ++i) {
        const FigState s = P.figs[i].st;
        if (s != FigState::Dying && s != FigState::Removed) ++rank;
    }
    float hx = f.hx, hy = f.hy;   // its own start slot when no standing figure has the rank's index (4744's gaps)
    for (const Fig& g : P.figs)
        if (g.index == rank) {
            hx = g.hx;
            hy = g.hy;
        }
    float x = f.x, y = f.y;
    position(pi, fi, t, x, y);
    if (f.move != kNoMove && f.move < r_.moves.size() && t < r_.moves[f.move].t1) {
        FigureMove& cut = r_.moves[f.move];   // a move still under way ends where the figure stands now
        cut.t1 = t;
        cut.x1 = x;
        cut.y1 = y;
    }
    f.x = x;
    f.y = y;
    const double dx = static_cast<double>(hx) - x, dy = static_cast<double>(hy) - y;
    const double dist = std::sqrt(dx * dx + dy * dy);
    if (!(dist > p_.walk_home_min_move)) return false;
    const CombatMemberArt& m = mem(pi, f);
    const CombatMotion& mo = m.motion;
    const bool shuffle = dist < mo.shortRadius();
    const double speed = static_cast<double>(shuffle ? mo.shortRate() : mo.move_rate) * kMoveRateUnits;
    const double dur = speed > 0 ? dist / speed : p_.approach;
    FigureMove mv;
    mv.player = P.player;
    mv.id = P.id;
    mv.figure = f.index;
    mv.code = shuffle ? 1450 : 1400;
    mv.t0 = t;
    mv.t1 = t + dur;
    mv.x0 = x;
    mv.y0 = y;
    mv.x1 = hx;
    mv.y1 = hy;
    f.move = r_.moves.size();
    r_.moves.push_back(mv);
    f.x = hx;
    f.y = hy;
    f.until = t + dur;
    f.returning = true;
    release(pi, f);   // no target on its way home
    step(pi, fi, FigState::Done, has(m, "Run") ? 1400 : 0, 1);
    return true;
}

bool Sim::tick() {
    if (done_) return true;
    const double h = p_.step;
    {
        const double t = static_cast<double>(k_++) * h;
        now_ = t;
        if (t + kEps >= r_.limit) {
            // [C 4.2, 5.3]: past the limit every figure is forced to its end state
            for (size_t pi = 0; pi < parts_.size(); ++pi)
                for (size_t i = 0; i < parts_[pi].figs.size(); ++i) {
                    Fig& f = parts_[pi].figs[i];
                    if (f.st == FigState::Dying) step(static_cast<int>(pi), static_cast<int>(i), FigState::Removed, 0);
                    else if (f.st == FigState::Done && f.returning) {
                        // 4742: a walk home cut by the limit ends where the figure is (the walk after the fight's end
                        // finishes it); it stays in 0xB
                        float x = f.x, y = f.y;
                        position(static_cast<int>(pi), static_cast<int>(i), t, x, y);
                        if (f.move != kNoMove && f.move < r_.moves.size() && t < r_.moves[f.move].t1) {
                            FigureMove& cut = r_.moves[f.move];
                            cut.t1 = t;
                            cut.x1 = x;
                            cut.y1 = y;
                        }
                        f.x = x;
                        f.y = y;
                        f.returning = false;
                    } else if (f.st != FigState::Removed && f.st != FigState::Done && f.st != FigState::Victory &&
                               f.st != FigState::Reface)
                        finish(static_cast<int>(pi), static_cast<int>(i), t, true);
                }
            homeWaitsReface(t);   // 5072
            r_.end = r_.limit;
            r_.at_limit = true;
            done_ = true;
            return closeOut();
        }
        // 4745: phase 1, the reform, at the first step
        if (!reform_decided_) {
            reform_decided_ = true;
            if (p_.reform_first) reformFirst(t);
        }
        // 5067 S1: a type 2 fight's phase 2, from the first step at or after phase 1's end
        if (!phase2_decided_ && p_.type2_phase2 && type2_ && t + kEps >= reform_end_) {
            phase2_decided_ = true;
            type2Phase2(t);
        }
        // 4777: a city attacker's volley (4745: not before the reform has ended)
        if (parts_[0].city && !city_fired_ && t + kEps >= p_.city_strike_at && t + kEps >= reform_end_) cityVolley(t);
        // landings due, in time order
        std::stable_sort(landings_.begin(), landings_.end(), [](const Landing& a, const Landing& b) {
            return a.time < b.time || (a.time == b.time && a.order < b.order);
        });
        while (!landings_.empty() && landings_.front().time <= t + kEps) {
            Landing l = landings_.front();
            landings_.erase(landings_.begin());
            land(l);
            std::stable_sort(landings_.begin(), landings_.end(), [](const Landing& a, const Landing& b) {
                return a.time < b.time || (a.time == b.time && a.order < b.order);
            });
        }
        for (size_t pii = 0; pii < parts_.size(); ++pii)
            for (size_t i = 0; i < parts_[pii].figs.size(); ++i) {
                const int pi = static_cast<int>(pii);
                Fig& f = parts_[pi].figs[i];
                const int fi = static_cast<int>(i);
                switch (f.st) {
                case FigState::Look:
                case FigState::Wait:
                    if (t + kEps < reform_end_) break;   // 4745: phase 1 under way: nobody looks yet
                    if (t + kEps < phase2_end_) break;   // 5067 S1: a type 2 fight's phase 2 under way
                    if (t + kEps < f.ride_stop_until) break;   // 5057 (A): a stopping rider plays its run stop first
                    if (t + kEps < f.turn_stop_until) break;   // 5074: a turned crew stops and stands first
                    if (riders_held_ && heldRider(pii, f)) break;   // 5072: a held rider stands (stock's 0x10)
                    look(pi, fi, t);
                    break;
                case FigState::Reform:   // 4745: arrived at its re-laid slot: it stops and stands (looks after phase 1)
                    if (t + kEps >= f.until) step(pi, fi, FigState::Look, has(mem(pi, f), "Idle") ? 1000 : 0);
                    break;
                case FigState::Approach:
                case FigState::Charge:
                    if (t + kEps >= f.until) {
                        if (f.returning) {   // 4777: home again
                            f.away = false;
                            finish(pi, fi, t, false);
                            break;
                        }
                        f.engaged = true;
                        const CombatMemberArt& m = mem(pi, f);
                        if (f.tpart < 0) look(pi, fi, t);
                        // 5071: a held strike's ride-on has arrived: it strikes from its run while its target stands and
                        // the defender's need is open, else it looks again (heldStrike) or stops as after a ride
                        else if (f.st == FigState::Charge && f.held_ride) {
                            const Fig& g = parts_[f.tpart].figs[f.tfig >= 0 ? f.tfig : 0];
                            const bool open = f.tfig >= 0 && g.st != FigState::Dying && g.st != FigState::Removed &&
                                              parts_[1].need() > 0;
                            const bool take = open && takes(pi, f, 1140);
                            // live: its machine not yet in its run loop: it holds, charge_wait at most (as below)
                            if (open && !take && p_.charge_wait >= 0 && t + kEps < f.until + p_.charge_wait) break;
                            f.held_ride = false;
                            if (take)
                                runStrike(pi, fi, t);
                            else if (!heldStrike(pi, fi, t)) {
                                release(pi, f);
                                const ActionTiming& s = timing(pi, f, 1440);
                                f.ride_stop_until = t + (s.found ? s.duration : 0.0);
                                f.ran = false;
                                step(pi, fi, FigState::Wait, has(m, "Idle") ? 1000 : 0);
                            }
                        }
                        // 5057 (A): a rush rider lands its hit at its arrival and rides on or stops (rideLand)
                        else if (f.st == FigState::Charge && rides(pi, f)) rideLand(pi, fi, t);
                        // a run charge goes straight into its strike, so that 1140 follows 1120 (0321 5.2 state 10:
                        // "when the last action was 1120") [I]; a run stops in combat ready first
                        // a member without AttackCharge stops in combat ready first: its machine takes the attack only
                        // from there (0557 h1 / m1 offline: 1100 sent straight after 1120 was never played)
                        else if (f.st == FigState::Charge && has(m, "AttackCharge")) {
                            // 4743 (W46): the machine takes 1140 only from its run loop (omni state 17,
                            // unit-controller.md 6.2). Live, 1140 is asked only once the controller says the figure
                            // takes it; until then the charge holds (the 1120 may still be on its way: sent this frame,
                            // or the machine still leaving combat ready), for charge_wait at most, and then the figure
                            // stops in combat ready and strikes 1100 (the charge_any-off path). Before: a 1140 asked of
                            // a figure in combat ready never played and held the fight to the limit (18.13).
                            const Part& CP = parts_[pi];
                            if (!fb_ || p_.charge_wait < 0 || fb_->accepts(CP.player, CP.id, f.index, 1140))
                                startStrike(pi, fi, t);
                            else if (t + kEps >= f.until + p_.charge_wait) ready(pi, fi, t, FigState::Ready);
                        } else {
                            (void)m;
                            ready(pi, fi, t, FigState::Ready);
                        }
                    }
                    break;
                case FigState::Ready:
                    if (riders_held_ && heldRider(pii, f)) break;   // 5072: a held rider stands (stock's 0x10)
                    // ready -> strike when the target is still there (0321 5.3: 5 -> 10 when a weapon target is found)
                    if (f.tpart >= 0 && (f.tfig < 0 || (parts_[f.tpart].figs[f.tfig].st != FigState::Dying &&
                                                        parts_[f.tpart].figs[f.tfig].st != FigState::Removed))) {
                        // at least one step in combat ready, and its entry clip done; 4782 live: when the figure's
                        // machine takes the attack request now (the controller's state, not the clip lengths)
                        // 4735: after its turn the figure plays its combat-ready entry first (0321 5.3: state 5 requests
                        // 1600 and strikes when its animation is done [C]); its machine, still in the run, would take the
                        // attack at once (offline: the Gatling skipped 1600 and its fight got 0.5 s shorter)
                        const bool go = t + kEps >= f.ready_at ||
                                        (fb_ && !f.entry_wait &&
                                         fb_->accepts(parts_[pi].player, parts_[pi].id, f.index, strikeCode(pi, f)));
                        if (go) startStrike(pi, fi, t);
                    } else {
                        release(pi, f);
                        f.st = FigState::Look;
                        look(pi, fi, t);
                    }
                    break;
                case FigState::Strike:
                    progress(pi, fi, t);
                    break;
                case FigState::Dying:
                    if (p_.death_by_variant && fb_ && !f.death_known) {
                        // 5053 R2 live: the death clip the figure's machine drew decides its length
                        const Part& DP = parts_[pi];
                        const int32_t v = fb_->playingVariant(DP.player, DP.id, f.index, 1200);
                        if (v == 1200) {
                            f.death_known = true;
                        } else if (v == 2200) {
                            f.death_known = true;
                            const ActionTiming& b = timing(pi, f, 2200);
                            if (b.found && b.duration > 0)
                                f.until = f.dying_at + std::min<double>(p_.death_cap, b.duration);
                        }
                    }
                    if (t + kEps >= f.until) step(pi, fi, FigState::Removed, 0);
                    break;
                case FigState::Done:   // 4742: home from its walk: it refaces (7, 1000) [C 0xB's end handler]
                    if (f.returning && t + kEps >= f.until) {
                        f.returning = false;
                        // 5072: a rider home before the fight's end waits there without a request (no run stop before
                        // the end, as stock's live sounds [L]); it refaces in the last step (homeWaitsReface)
                        if (heldRider(pii, f)) {
                            f.home_wait = true;
                            break;
                        }
                        step(pi, fi, FigState::Reface, has(mem(pi, f), "Idle") ? 1000 : 0);
                    } else if (f.end_shuffle && t + kEps >= f.until) {   // 5053 R3: the end shuffle is over
                        f.end_shuffle = false;
                        step(pi, fi, FigState::Reface, has(mem(pi, f), "Idle") ? 1000 : 0);
                    }
                    break;
                case FigState::Turn:   // 4735: turned; it looks for its target as it would have at the start
                    if (f.p2stance) {   // 5067 S1: a defender's type 2 phase-2 stance: it looks at the phase's end
                        if (t + kEps >= phase2_end_) {
                            f.p2stance = false;
                            f.st = FigState::Look;
                            look(pi, fi, t);
                        }
                        break;
                    }
                    if (f.astance) {   // 5050 (b): the attacker's phase-2 stance is over: 1600 from the run loop
                        if (t + kEps >= f.until) {
                            f.astance = false;
                            f.astanced = true;
                            ready(pi, fi, t, FigState::Ready);
                        }
                        break;
                    }
                    if (t + kEps >= f.until) {
                        if (entryAfterTurn(pi, fi, t)) break;   // 5074: the stop first, then 1600 from idle
                        f.st = FigState::Look;
                        f.entry_wait = true;
                        look(pi, fi, t);
                    }
                    break;
                case FigState::Defend:   // 4750: the phase-2 stance is over: bombard defend (the machine's silent branch)
                    if (f.stance && t + kEps >= f.until) {
                        f.stance = false;
                        step(pi, fi, FigState::Defend, 1280);
                    }
                    break;
                default:
                    break;
                }
            }
        // the end test [C 13.4]: every figure is held while a side has planned deaths not played or a hit owed
        bool needs = !landings_.empty() || (parts_[0].city && !city_fired_) || t + kEps < reform_end_ ||   // 4745
                     t + kEps < phase2_end_;                                                               // 5067 S1
        for (auto& P : parts_) needs = needs || P.need() > 0;
        // 5053 R1 (end_after_dying): while a figure of the fight is dying, the standing figures of every side but the
        // attacker keep their state and request (stock's 0x10); they end in the step the last dying figure is removed
        bool any_dying = false;
        for (const auto& P : parts_)
            for (const auto& g : P.figs)
                if (g.st == FigState::Dying) any_dying = true;
        const bool hold_dying = p_.end_after_dying && any_dying;
        // 5072 (type2_end_after_dying): in a type 2 fight the attacker's riders are held as well (stock's 0x10)
        const bool hold_riders = p_.type2_end_after_dying && type2_ && any_dying;
        riders_held_ = hold_riders && !needs;   // 5072: the held riders stand in the next step (no look, no strike)
        if (!needs)
            for (size_t pi = 0; pi < parts_.size(); ++pi)
                for (size_t i = 0; i < parts_[pi].figs.size(); ++i) {
                    if (hold_dying && parts_[pi].role != Role::Attacker) break;   // 5053 R1
                    Fig& f = parts_[pi].figs[i];
                    const FigState s = f.st;
                    if (f.returning) continue;
                    if (hold_riders && heldRider(pi, f))
                        continue;   // 5072: finished in the step the last dying figure is removed
                    if (s == FigState::Look || s == FigState::Wait || s == FigState::Ready || s == FigState::Defend ||
                        s == FigState::Approach || s == FigState::Charge || s == FigState::Turn) {
                        // 4777: an aircraft that has flown out flies home first [I]
                        if (f.flyer && f.away) flyHome(static_cast<int>(pi), static_cast<int>(i), t);
                        else finish(static_cast<int>(pi), static_cast<int>(i), t, false);
                    }
                }
        bool all = landings_.empty() && !hold_limit_ && t + kEps >= hold_until_;
        for (size_t pi = 0; pi < parts_.size() && all; ++pi)
            for (auto& f : parts_[pi].figs)
                if ((f.st != FigState::Removed && f.st != FigState::Done && f.st != FigState::Victory &&
                     f.st != FigState::Reface) ||
                    (f.st == FigState::Done && (f.returning || f.end_shuffle)))   // 4742: walking home; 5053: shuffling
                    all = false;
        if (all) {
            homeWaitsReface(t);   // 5072
            r_.end = t;
            done_ = true;
            return closeOut();
        }
    }
    return false;
}

bool Sim::closeOut() {
    for (size_t pi = 0; pi < parts_.size(); ++pi) {
        SideOutcome& o = pi == 0 ? r_.attacker : (pi == 1 ? r_.defender : r_.others[pi - 2]);
        o.died = parts_[pi].died;
        o.paid = parts_[pi].paid;
    }
    return true;
}

// The step's code as a controller event (Action with its kind, or Kill); false: no event.
bool eventFor(int32_t code, TimelineEvent& ev) {
    ev.kind = EventKind::Action;
    switch (code) {
    case 1000: ev.action = ActionKind::Idle; return true;
    case 1100: ev.action = ActionKind::Attack; return true;
    case 1120: ev.action = ActionKind::Charge; return true;
    case 1130: ev.action = ActionKind::AntiAir; return true;
    case 1140: ev.action = ActionKind::AttackCharge; return true;
    case 1160: ev.action = ActionKind::AttackCity; return true;
    case 1180: ev.action = ActionKind::Bombard; return true;
    case 1200: ev.kind = EventKind::Kill; ev.action = ActionKind::None; return true;
    case 1280: ev.action = ActionKind::BombardDefend; return true;
    case 1400: ev.action = ActionKind::Run; return true;
    case 1450: ev.action = ActionKind::Walk; return true;   // 4750 / 5050: only the stance steps request it
    case 1600: ev.action = ActionKind::CombatReady; return true;
    case 1800: ev.action = ActionKind::Victory; return true;
    default: return false;
    }
}

void buildEvents(const CombatResult& r, size_t from, std::vector<TimedEvent>& events) {
    // per figure, the last request at each time wins (path 4 is a level, unit-controller.md 1); then one event per
    // (time, unit, kind) with its figure set in figure order; the steps from `from` on, appended to `events`
    std::map<std::tuple<double, int32_t, int32_t, uint16_t>, std::pair<size_t, int32_t>> last;   // -> (order, code)
    for (size_t i = from; i < r.steps.size(); ++i) {
        const FigureStep& s = r.steps[i];
        if (s.code == 0) continue;
        last[std::make_tuple(s.time, s.player, s.id, s.figure)] = std::make_pair(i, s.code);
    }
    std::map<std::tuple<double, int32_t, int32_t, int32_t>, std::pair<size_t, std::vector<uint16_t>>> groups;
    for (auto& kv : last) {
        auto k = std::make_tuple(std::get<0>(kv.first), std::get<1>(kv.first), std::get<2>(kv.first), kv.second.second);
        auto& g = groups[k];
        if (g.second.empty() || kv.second.first < g.first) g.first = kv.second.first;
        g.second.push_back(std::get<3>(kv.first));
    }
    std::vector<std::pair<size_t, TimedEvent>> out;
    for (auto& kv : groups) {
        TimedEvent te;
        te.time = std::get<0>(kv.first);
        te.ev.player = std::get<1>(kv.first);
        te.ev.id = std::get<2>(kv.first);
        if (!eventFor(std::get<3>(kv.first), te.ev)) continue;
        std::vector<uint16_t> figs = kv.second.second;
        std::sort(figs.begin(), figs.end());
        te.ev.figures.all = false;
        te.ev.figures.figures = figs;
        out.push_back(std::make_pair(kv.second.first, te));
    }
    std::stable_sort(out.begin(), out.end(), [](const std::pair<size_t, TimedEvent>& a,
                                                const std::pair<size_t, TimedEvent>& b) {
        return a.second.time < b.second.time || (a.second.time == b.second.time && a.first < b.first);
    });
    for (auto& e : out) events.push_back(e.second);
}


// The refusals that need no figures (0321 4.1, 7). 4777: a city attacker, a nuclear strike, an air strike or sweep and
// damage members are played (unit-combat-server.md "Special cases (4777)"), no longer refused here.
CombatStatus checkRecord(const CombatInput& in, const CombatParams& params) {
    if (in.kind != kCombatKindUnit && in.kind != kCombatKindCity)
        return fail(CombatCode::UnknownKind, "record kind " + std::to_string(in.kind) + " (25 unit, 28 city)");
    if ((in.flags & kCombatGameFlagsRead) && (in.flags & kCombatQuickCombat))
        return fail(CombatCode::QuickCombat, "Quick Combat: no simulation; damage and deaths show at once (0321 7)");
    if (!(in.flags & kCombatVisualize))
        return fail(CombatCode::NotVisualised, "visualise flag clear: no simulation (0321 4.1)");
    if (!(params.step > 0) || !std::isfinite(params.step) || !(params.limit > 0) || !std::isfinite(params.limit) ||
        !(params.limit_air > 0) || !std::isfinite(params.limit_air) || params.limit / params.step > 1e6 ||
        params.limit_air / params.step > 1e6 || !(params.death_cap >= 0) || !(params.approach >= 0) ||
        params.victory_threshold < -1 || params.victory_threshold > 99)
        return fail(CombatCode::BadInput, "parameters: step, limit, death cap, approach or victory threshold");
    // 4777's parameters
    if (!(params.air_flight >= 0) || !std::isfinite(params.air_flight) || !(params.city_strike_at >= 0) ||
        !std::isfinite(params.city_strike_at) || !(params.city_flight >= 0) || !std::isfinite(params.city_flight) ||
        !(params.city_stagger >= 0) || !std::isfinite(params.city_stagger) || !(params.effect_wait >= 0) ||
        !std::isfinite(params.effect_wait))
        return fail(CombatCode::BadInput, "parameters: air flight, city strike times or effect wait");
    if (in.members.size() > 64)
        return fail(CombatCode::BadInput, std::to_string(in.members.size()) + " damage members (0321 4.1: at most 64)");
    if (in.interceptor.unit >= 0 && in.interceptor.city >= 0)
        return fail(CombatCode::BadInput, "interceptor: a unit and a city");
    if (in.attacker.city >= 0 && in.attacker.unit < 0 && in.defender.city >= 0)
        return fail(CombatCode::BadInput, "a city attacks a city");
    return CombatStatus();
}

}  // namespace

struct CombatRun::Impl {
    CombatInput in;
    CombatUnitFigures a, d;
    std::vector<CombatUnitFigures> others;
    CombatParams p;
    CombatResult r;
    std::unique_ptr<Sim> sim;
    double acc = 0;
};

CombatRun::CombatRun() : m_(new Impl()) {}
CombatRun::~CombatRun() {}

CombatStatus CombatRun::start(const CombatInput& in, const CombatUnitFigures& attacker,
                              const CombatUnitFigures& defender, CombatClips& clips, const CombatParams& params,
                              CombatFeedback* fb) {
    return start(in, attacker, defender, std::vector<CombatUnitFigures>(), clips, params, fb);
}

CombatStatus CombatRun::start(const CombatInput& in, const CombatUnitFigures& attacker,
                              const CombatUnitFigures& defender, const std::vector<CombatUnitFigures>& others,
                              CombatClips& clips, const CombatParams& params, CombatFeedback* fb) {
    m_.reset(new Impl());
    CombatStatus st = checkRecord(in, params);
    if (!st.ok()) {
        m_->r.status = st;
        return st;
    }
    m_->in = in;
    m_->a = attacker;
    m_->d = defender;
    m_->others = others;
    m_->p = params;
    m_->sim.reset(new Sim(m_->in, clips, m_->p, m_->r));
    st = m_->sim->setup(m_->a, m_->d, m_->others);
    if (!st.ok()) {
        m_->sim.reset();
        m_->r = CombatResult();
        m_->r.status = st;
        return st;
    }
    m_->sim->feedback(fb);
    std::vector<const CombatUnitFigures*> sides = {&m_->a, &m_->d};
    for (auto& o : m_->others) sides.push_back(&o);
    for (const CombatUnitFigures* u : sides)
        for (auto& mm : u->members)
            if (mm.domain == "Sea") {
                // 4777: the sea's rules are built (a ship strikes from where it stands unless ships_approach; it answers
                // an air side with 1130); the note stays for the record ("Naval" is not LOUD in the server's adapter)
                m_->r.notes.push_back(CombatNote{0, "Naval: " + mm.member + " plays the sea's rules (4777)"});
                return st;
            }
    return st;
}

bool CombatRun::step(std::vector<TimedEvent>& out) {
    if (!m_->sim) return true;
    const size_t from = m_->r.steps.size();
    const bool end = m_->sim->tick();
    std::vector<TimedEvent> made;
    buildEvents(m_->r, from, made);
    for (auto& e : made) {
        m_->r.events.push_back(e);
        out.push_back(e);
    }
    return end;
}

bool CombatRun::advance(double dt, std::vector<TimedEvent>& out) {
    if (!m_->sim) return true;
    if (!(dt >= 0) || !std::isfinite(dt)) dt = 0;
    m_->acc += dt;
    bool end = m_->sim->done();
    // every step whose time the caller's clock has reached (the step at t = 0 in the first call)
    while (!end && m_->sim->time() <= m_->acc + 1e-9) end = step(out);
    return end;
}

bool CombatRun::done() const { return !m_->sim || m_->sim->done(); }

const CombatResult& CombatRun::result() const { return m_->r; }

CombatResult choreograph(const CombatInput& in, const CombatUnitFigures& attacker, const CombatUnitFigures& defender,
                         CombatClips& clips, const CombatParams& params) {
    return choreograph(in, attacker, defender, std::vector<CombatUnitFigures>(), clips, params);
}

CombatResult choreograph(const CombatInput& in, const CombatUnitFigures& attacker, const CombatUnitFigures& defender,
                         const std::vector<CombatUnitFigures>& others, CombatClips& clips, const CombatParams& params) {
    CombatRun run;
    if (!run.start(in, attacker, defender, others, clips, params, nullptr).ok()) return run.result();
    std::vector<TimedEvent> sink;
    while (!run.step(sink)) {}
    return run.result();
}

std::vector<TimelineFrame> combatFrames(const CombatResult& r, double dt, uint64_t first_seq) {
    std::vector<TimelineFrame> out;
    if (!r.status.ok() || !(dt > 0) || !std::isfinite(dt)) return out;
    double last = r.end;
    for (auto& e : r.events) last = std::max(last, e.time);
    const size_t n = static_cast<size_t>(std::floor(last / dt + 1e-9)) + 2;
    out.resize(n);
    for (size_t i = 0; i < n; ++i) {
        out[i].seq = first_seq + i;
        out[i].dt = dt;
    }
    for (auto& e : r.events) {
        size_t i = static_cast<size_t>(std::floor(e.time / dt + 1e-9));
        if (i >= n) i = n - 1;
        out[i].events.push_back(e.ev);
    }
    return out;
}

}}  // namespace c5r::unitanim
