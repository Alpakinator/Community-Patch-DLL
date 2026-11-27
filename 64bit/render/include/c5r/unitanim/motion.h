// c5r/unitanim/motion.h - figure motion in a fight (epic 0042, issue 4776; research/05-graphics-composition/
// unit-combat-server.md "Figure motion (4776)"). Written from that section's description of the EXE's behaviour (D83: no
// decompiler output, no EXE bytes).
//
// Three parts:
//   - combatPositions: the figures' start positions for the choreography core (combat.h), from the unit's placement
//     (placement.h's placeUnit), so that the core times each approach by its distance (CombatUnitFigures::x, y).
//   - FightMotion: one choreographed fight (CombatResult) as each figure's world position and heading over time: the
//     approach moves the core recorded (run, run charge, shuffle), the figure facing its target while it stands, and
//     after the fight's end the return: every survivor walks back into its slot of the unit's placement after the
//     damage (the survivors close up into slots 0..m-1 in figure order, unit-formations.md 7: the reform). 4742: with
//     CombatParams::walk_home the core walks the survivors home inside the fight (its moves; a Reface step faces the
//     placement's heading), so this after-end walk only closes what is left (the limit, a victory figure, a living count
//     or a root / facing that differs after the fight).
//   - FightPlacement: a units::PlacementSource that wraps another (the module's own placement) and moves the figures of
//     a unit in a fight by FightMotion's offsets in the unit's own frame (placement.h's applyFigureOffsets). The renderer
//     uses one placement per unit for every pass, so the scene and the reflection pass draw the same figures; a unit with
//     no fight set is placed exactly as by the inner source. The server sets it with UnitRenderer::setPlacementSource
//     after the 4981 extras' root override (the inner placement already uses that root), and feeds each fighting unit
//     its fight's clock (4782 / 4774).
// Nothing throws; failures are Status / UnitStatus values.
#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <tuple>
#include <utility>
#include <vector>

#include "c5r/status.h"
#include "c5r/unitanim/combat.h"
#include "c5r/units/placement.h"
#include "c5r/units/units.h"

namespace c5r { namespace unitanim {

// The start positions of `figs`'s standing figures from the unit's placement (placeUnit over `in` with every figure
// living: figure i in slot i of the n-member layout, whatever the damage): x, y for each standing figure, the centre at
// the unit's root. A standing figure beyond the unit's figures -> BadLayout. Also the errors of placeUnit.
Status combatPositions(const units::FormationTable& table, const heightfield::HeightGrid& g,
                       const units::PlacementInput& in, CombatUnitFigures& figs);

// The yaw (placement.h's convention: radians about +z, the figure facing its local -y) that faces world direction
// (dx, dy). (0, 0) gives 0.
float yawFacing(float dx, float dy);

struct FigurePose {
    float x = 0, y = 0;              // world units
    float yaw = 0;                   // radians, as FigurePlacement::yaw
    bool moving = false;             // on a move at this moment
    float z = 0;                     // 4726: height above the figure's ground placement (an aircraft's flight, FigureMove
                                     // z0 -> z1 along the move; 0 on the ground)
    bool yaw_known = true;           // 4726: false when nothing gives the figure a heading (no move, no target, the
                                     // opponent's centre not known: a city attacker whose plot the feed does not carry);
                                     // offsets() then keep the placement's yaw
};

class FightMotion {
public:
    // Builds the motion of a fight. `r` must be Ok and `attacker` / `defender` the figures it was run with, with their
    // start positions (x, y); a city side has none. Checks: a refused result, a unit side without positions, a move or
    // step naming a figure that is not a standing figure [BadLayout].
    Status build(const CombatResult& r, const CombatUnitFigures& attacker, const CombatUnitFigures& defender);
    // 4726 (additive): with the other participants' figures (CombatRun::start's `others`: the interceptor, the damage
    // members that are units), found by (player, id) for each of `r.others` that took part with figures
    // (SideOutcome::figures); each needs its start positions as the two sides do. A participant without figures (a city,
    // the nuke's plot, a member whose figures were not given) is a centre only (its figures' centre when given).
    Status build(const CombatResult& r, const CombatUnitFigures& attacker, const CombatUnitFigures& defender,
                 const std::vector<CombatUnitFigures>& others);

    double end() const { return end_; }   // the fight's end (CombatResult::end), seconds from its start
    // The pose of one figure at t seconds from the fight's start (t past the end: where it ended). False: not a figure
    // of the fight.
    bool pose(double t, int32_t player, int32_t id, uint16_t figure, FigurePose& out) const;
    // True when (player, id) is a unit participant of the fight with figures (4726: the interceptor and the damage
    // members too).
    bool hasUnit(int32_t player, int32_t id) const;

    // The offsets of the unit's placed figures at t (placement.h's FigureOffset, in the unit's own frame of `p`): up to
    // the end each fighting figure at its pose; after the end the survivors walk back, each at its member's MoveRate x
    // 64, from where it ended into the slot its rank takes in `p` (rank i of the surviving figures in figure order ->
    // the i-th figure `p` places). Returns true while any figure of the unit is off its slot (the caller keeps the
    // fight's clock running for the return); false when every figure stands in its slot (`out` empty).
    bool offsets(double t, int32_t player, int32_t id, const units::UnitPlacement& p,
                 std::vector<units::FigureOffset>& out) const;
    // 4726 (additive): the same, with each offset's height above the ground placement (`heights[i]` for `out[i]`:
    // FigurePose::z; an aircraft still in the air after the end comes down along its walk back) [I].
    bool offsets(double t, int32_t player, int32_t id, const units::UnitPlacement& p,
                 std::vector<units::FigureOffset>& out, std::vector<float>& heights) const;

private:
    struct Fig {
        int32_t player = -1, id = -1;
        uint16_t figure = 0;
        float x0 = 0, y0 = 0;        // start
        float rate = 0.78125f;       // MoveRate (the return walk)
        bool died = false;           // it died in the fight (a Dying step)
        std::vector<size_t> legs;    // indices into moves_, in time order
        std::vector<std::pair<double, std::tuple<int32_t, int32_t, int32_t>>> targets;   // step time -> target
    };
    const Fig* find(int32_t player, int32_t id, uint16_t figure) const;
    void position(const Fig& f, double t, float& x, float& y, float& z, bool& moving, float& dirx, float& diry) const;
    // 4726: the participant whose centre (player, id) faces when it has no target: the defender for the attacker, the
    // attacker for every other participant [I: 0321 4.1, the defender, the interceptor and the members fight the attacker]
    const std::pair<float, float>* opponentCentre(int32_t player, int32_t id) const;

    double end_ = 0;
    std::vector<FigureMove> moves_;
    std::map<std::tuple<int32_t, int32_t, uint16_t>, Fig> figs_;
    // the participants in CombatResult order: attacker, defender, then r.others (4726); per participant its start
    // centroid (a unit with figures), else its centre when given (a city, the nuke's plot)
    std::vector<std::pair<int32_t, int32_t>> sides_;
    std::vector<bool> figured_, centred_;
    std::vector<std::pair<float, float>> centre_;
};

// The placement wrapper (see the head of this file).
class FightPlacement : public units::PlacementSource {
public:
    explicit FightPlacement(units::PlacementSource* inner = nullptr) : inner_(inner) {}
    void setInner(units::PlacementSource* inner) { inner_ = inner; }
    // (player, id) is in a fight (or returning from one) `t` seconds after the fight's start. Replaces an earlier set.
    void set(int32_t player, int32_t id, std::shared_ptr<const FightMotion> m, double t);
    void clear(int32_t player, int32_t id);
    void clearAll() { units_.clear(); }
    size_t active() const { return units_.size(); }
    // The inner source's placement, then the offsets of the unit's fight when one is set (4726: and an aircraft's height
    // added to its figure's z after applyFigureOffsets). A failed offset is PlacementFailed with the text (the unit is
    // not drawn, as any placement failure, unit-renderer.md 7).
    units::UnitStatus place(const units::PlacementRequest& r, units::UnitPlacement& out) override;
    // Counters: placements that took offsets; of them, units that were back in their slots (no offset).
    uint64_t moved() const { return moved_; }
    uint64_t settled() const { return settled_; }

private:
    units::PlacementSource* inner_;
    std::map<std::pair<int32_t, int32_t>, std::pair<std::shared_ptr<const FightMotion>, double>> units_;
    uint64_t moved_ = 0, settled_ = 0;
};

}}  // namespace c5r::unitanim
