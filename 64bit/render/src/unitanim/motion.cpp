// motion.cpp - figure motion in a fight (epic 0042, issue 4776; motion.h). Written from unit-combat-server.md "Figure
// motion (4776)" (D83). Marks: [C] the EXE's rule as that section reads it, [I] this file's reading.
#include "c5r/unitanim/motion.h"

#include <algorithm>
#include <cmath>

namespace c5r { namespace unitanim {

Status combatPositions(const units::FormationTable& table, const heightfield::HeightGrid& g,
                       const units::PlacementInput& in, CombatUnitFigures& figs) {
    // every figure in its slot of the n-member layout (figure i in slot i), whatever the damage rule leaves living: the
    // controller's standing figures are the fight's (a unit at 85 damage may still stand with every figure, 0557 w2)
    units::PlacementInput all = in;
    all.alive = static_cast<int16_t>(in.zOffset.size());
    units::UnitPlacement p;
    C5R_TRY(units::placeUnit(table, g, all, p));
    figs.x.clear();
    figs.y.clear();
    for (uint16_t s : figs.standing) {
        const units::FigurePlacement* f = nullptr;
        for (auto& q : p.placed)
            if (q.figure == s) f = &q;
        if (!f) {
            figs.x.clear();
            figs.y.clear();
            return Status::fail(Err::BadLayout, "combat positions: standing figure " + std::to_string(s) +
                                                    " is not a figure of the unit's " + std::to_string(p.figures));
        }
        figs.x.push_back(f->x);
        figs.y.push_back(f->y);
    }
    figs.has_centre = true;
    figs.centre_x = p.rootX;
    figs.centre_y = p.rootY;
    figs.has_facing = std::isfinite(p.yaw);   // 4735: the heading the formation stands in (the placement's yaw)
    figs.facing_yaw = figs.has_facing ? p.yaw : 0.f;
    return Status::Ok();
}

float yawFacing(float dx, float dy) {
    // placement.h: a figure with yaw a faces (sin a, -cos a) (its local -y; unit-formations.md 3.7)
    if (dx == 0 && dy == 0) return 0;
    return static_cast<float>(std::atan2(static_cast<double>(dx), -static_cast<double>(dy)));
}

Status FightMotion::build(const CombatResult& r, const CombatUnitFigures& attacker, const CombatUnitFigures& defender) {
    return build(r, attacker, defender, std::vector<CombatUnitFigures>());
}

Status FightMotion::build(const CombatResult& r, const CombatUnitFigures& attacker, const CombatUnitFigures& defender,
                          const std::vector<CombatUnitFigures>& others) {
    *this = FightMotion();
    if (!r.status.ok()) return Status::fail(Err::BadLayout, "fight motion: the combat was refused (" + r.status.text + ")");
    end_ = r.end;
    moves_ = r.moves;
    // 4726: every participant of the result (attacker, defender, then the interceptor and the damage members)
    std::vector<const SideOutcome*> outs = {&r.attacker, &r.defender};
    std::vector<const CombatUnitFigures*> us = {&attacker, &defender};
    for (const SideOutcome& o : r.others) {
        outs.push_back(&o);
        const CombatUnitFigures* f = nullptr;
        if (o.id >= 0)
            for (const CombatUnitFigures& c : others)
                if (c.player == o.player && c.id == o.id) f = &c;
        us.push_back(f);
    }
    sides_.assign(outs.size(), std::make_pair(-1, -1));
    figured_.assign(outs.size(), false);
    centred_.assign(outs.size(), false);
    centre_.assign(outs.size(), std::make_pair(0.f, 0.f));
    for (size_t s = 0; s < outs.size(); ++s) {
        const SideOutcome& o = *outs[s];
        sides_[s] = std::make_pair(o.player, o.id);
        if (o.city || !o.figures) {
            // a city, the nuke's plot, a member without figures: its centre only (4777: SideOutcome::figures false)
            const CombatUnitFigures* u = us[s];
            if (u && u->has_centre) {
                centre_[s] = std::make_pair(u->centre_x, u->centre_y);
                centred_[s] = true;
            }
            // 4726: a centre not given (a city attacker: the feed has no city plot) is not faced; before 4726 the figures
            // faced (0, 0) then
            continue;
        }
        if (!us[s])
            return Status::fail(Err::BadLayout, "fight motion: participant " + std::to_string(s) + " (" +
                                                    std::to_string(o.player) + "/" + std::to_string(o.id) +
                                                    ") fought with figures that were not given");
        const CombatUnitFigures& u = *us[s];
        if (u.player != o.player || u.id != o.id)
            return Status::fail(Err::BadLayout, "fight motion: figures of " + std::to_string(u.player) + "/" +
                                                    std::to_string(u.id) + " for side " + std::to_string(o.player) +
                                                    "/" + std::to_string(o.id));
        if (u.x.size() != u.standing.size() || u.y.size() != u.standing.size() || u.standing.empty())
            return Status::fail(Err::BadLayout, "fight motion: unit " + std::to_string(o.player) + "/" +
                                                    std::to_string(o.id) + " has no start positions");
        double sx = 0, sy = 0;
        for (size_t i = 0; i < u.standing.size(); ++i) {
            Fig f;
            f.player = o.player;
            f.id = o.id;
            f.figure = u.standing[i];
            f.x0 = u.x[i];
            f.y0 = u.y[i];
            uint32_t first = 0;
            for (auto& m : u.members) {
                if (f.figure < first + static_cast<uint32_t>(std::max(0, m.count))) {
                    f.rate = m.motion.move_rate;
                    break;
                }
                first += static_cast<uint32_t>(std::max(0, m.count));
            }
            sx += f.x0;
            sy += f.y0;
            figs_[std::make_tuple(f.player, f.id, f.figure)] = f;
        }
        centre_[s] = std::make_pair(static_cast<float>(sx / u.standing.size()),
                                    static_cast<float>(sy / u.standing.size()));
        figured_[s] = centred_[s] = true;
    }
    for (size_t i = 0; i < moves_.size(); ++i) {
        auto it = figs_.find(std::make_tuple(moves_[i].player, moves_[i].id, moves_[i].figure));
        if (it == figs_.end())
            return Status::fail(Err::BadLayout, "fight motion: a move of figure " + std::to_string(moves_[i].figure) +
                                                    " of " + std::to_string(moves_[i].player) + "/" +
                                                    std::to_string(moves_[i].id) + ", not a standing figure");
        it->second.legs.push_back(i);
    }
    for (auto& kv : figs_)
        std::stable_sort(kv.second.legs.begin(), kv.second.legs.end(),
                         [this](size_t a, size_t b) { return moves_[a].t0 < moves_[b].t0; });
    for (auto& st : r.steps) {
        if (st.player < 0 && st.id < 0) continue;
        auto it = figs_.find(std::make_tuple(st.player, st.id, st.figure));
        if (it == figs_.end()) {
            bool figureless = false;   // a city (or the nuke's plot) has no figures
            for (size_t s = 0; s < sides_.size(); ++s)
                if (!figured_[s] && sides_[s] == std::make_pair(st.player, st.id)) figureless = true;
            if (figureless && st.id < 0) continue;
            return Status::fail(Err::BadLayout, "fight motion: a step of figure " + std::to_string(st.figure) + " of " +
                                                    std::to_string(st.player) + "/" + std::to_string(st.id) +
                                                    ", not a standing figure");
        }
        if (st.state == FigState::Dying || st.state == FigState::Removed) it->second.died = true;
        // what it faces from this step on: its target figure, or (no target) the opponent's centre (-1, -1, -1)
        const bool faces = st.state == FigState::Approach || st.state == FigState::Charge ||
                           st.state == FigState::Ready || st.state == FigState::Strike || st.state == FigState::Wait;
        if (st.state == FigState::Dying || st.state == FigState::Removed) continue;   // falls where it stands
        if (faces && st.target_player >= 0)
            it->second.targets.push_back(
                std::make_pair(st.time, std::make_tuple(st.target_player, st.target_id, st.target_figure)));
        else if (st.state == FigState::Defend || st.state == FigState::Look)
            it->second.targets.push_back(std::make_pair(st.time, std::make_tuple(-1, -1, -1)));
        else if (st.state == FigState::Reface)   // 4742: home from its walk, it refaces: the placement's heading (-3)
            it->second.targets.push_back(std::make_pair(st.time, std::make_tuple(-3, -3, -3)));
        // Done / Victory keep the heading they had (a Done figure walking home faces its way while it moves) [I: the EXE
        // refaces to the unit's facing at the end (state 7, 1000); the walk after the end then turns the figure]
    }
    return Status::Ok();
}

const FightMotion::Fig* FightMotion::find(int32_t player, int32_t id, uint16_t figure) const {
    auto it = figs_.find(std::make_tuple(player, id, figure));
    return it == figs_.end() ? nullptr : &it->second;
}

bool FightMotion::hasUnit(int32_t player, int32_t id) const {
    for (size_t s = 0; s < sides_.size(); ++s)
        if (figured_[s] && sides_[s].first == player && sides_[s].second == id && id >= 0) return true;
    return false;
}

const std::pair<float, float>* FightMotion::opponentCentre(int32_t player, int32_t id) const {
    if (sides_.size() < 2) return nullptr;
    const size_t o = (sides_[0].first == player && sides_[0].second == id) ? 1 : 0;
    return centred_[o] ? &centre_[o] : nullptr;
}

void FightMotion::position(const Fig& f, double t, float& x, float& y, float& z, bool& moving, float& dirx,
                           float& diry) const {
    x = f.x0;
    y = f.y0;
    z = 0;
    moving = false;
    dirx = diry = 0;
    for (size_t li : f.legs) {
        const FigureMove& m = moves_[li];
        if (t < m.t0) break;
        if (t >= m.t1 || !(m.t1 > m.t0)) {
            x = m.x1;
            y = m.y1;
            z = m.air ? m.z1 : 0.f;   // 4726: an aircraft stays at the height it reached (its altitude after a flight out)
            continue;
        }
        const double a = (t - m.t0) / (m.t1 - m.t0);   // straight at a constant speed [C 0x61D7D0]
        x = static_cast<float>(m.x0 + (m.x1 - m.x0) * a);
        y = static_cast<float>(m.y0 + (m.y1 - m.y0) * a);
        // 4726: an aircraft's height changes evenly along its move (z0 -> z1: climbing out, descending home) [I: the
        // EXE's AltitudeDecelerationDistance (4776 M1 + 0x60) is not read]
        z = m.air ? static_cast<float>(m.z0 + (m.z1 - m.z0) * a) : 0.f;
        moving = true;
        dirx = m.x1 - m.x0;
        diry = m.y1 - m.y0;
        break;
    }
}

bool FightMotion::pose(double t, int32_t player, int32_t id, uint16_t figure, FigurePose& out) const {
    const Fig* f = find(player, id, figure);
    if (!f) return false;
    const double tt = std::min(t, end_);
    float dx = 0, dy = 0;
    position(*f, tt, out.x, out.y, out.z, out.moving, dx, dy);
    for (size_t li : f->legs) {   // 4735: a turn in place under way: the heading goes the shorter way, evenly
        const FigureMove& m = moves_[li];
        if (!m.turn || tt < m.t0 || tt >= m.t1 || !(m.t1 > m.t0)) continue;
        const double a = (tt - m.t0) / (m.t1 - m.t0);
        const double d = std::remainder(static_cast<double>(m.yaw1) - m.yaw0, 6.283185307179586);
        out.yaw = static_cast<float>(m.yaw0 + d * a);
        out.moving = false;   // it turns where it stands
        return true;
    }
    if (out.moving) {
        out.yaw = yawFacing(dx, dy);   // a moving figure faces its way [I: the turn rate is not modelled]
        return true;
    }
    // standing: it faces its last target (a figure, else the opponent's centre) [I: turned at once]
    std::tuple<int32_t, int32_t, int32_t> tg(-2, -2, -2);
    for (auto& s : f->targets) {
        if (s.first > tt + 1e-9) break;
        tg = s.second;
    }
    if (std::get<0>(tg) == -3) {   // 4742: refaced at home: the placement's heading (the unit's facing)
        out.yaw_known = false;
        return true;
    }
    float tx = 0, ty = 0;
    bool have = false;
    if (std::get<0>(tg) >= 0 && std::get<2>(tg) >= 0) {
        const Fig* g = find(std::get<0>(tg), std::get<1>(tg), static_cast<uint16_t>(std::get<2>(tg)));
        if (g) {
            bool mv = false;
            float a = 0, b = 0, tz = 0;
            position(*g, tt, tx, ty, tz, mv, a, b);
            have = true;
        }
    }
    if (!have && std::get<0>(tg) != -2) {
        // the opponent's centre (a city target, or no target)
        if (const std::pair<float, float>* c = opponentCentre(player, id)) {
            tx = c->first;
            ty = c->second;
            have = true;
        }
    }
    if (have && (tx != out.x || ty != out.y)) {
        out.yaw = yawFacing(tx - out.x, ty - out.y);
        return true;
    }
    // no target yet: the heading of its last move, else the opponent's direction from the start
    for (size_t li : f->legs) {
        const FigureMove& m = moves_[li];
        if (m.t0 > tt) break;
        out.yaw = m.turn ? m.yaw1 : yawFacing(m.x1 - m.x0, m.y1 - m.y0);   // 4735: a turn ends facing yaw1
        have = true;
    }
    if (!have) {
        if (const std::pair<float, float>* c = opponentCentre(player, id))
            out.yaw = yawFacing(c->first - out.x, c->second - out.y);
        else
            out.yaw_known = false;   // 4726: keep the placement's heading
    }
    return true;
}

bool FightMotion::offsets(double t, int32_t player, int32_t id, const units::UnitPlacement& p,
                          std::vector<units::FigureOffset>& out) const {
    std::vector<float> heights;
    return offsets(t, player, id, p, out, heights);
}

bool FightMotion::offsets(double t, int32_t player, int32_t id, const units::UnitPlacement& p,
                          std::vector<units::FigureOffset>& out, std::vector<float>& heights) const {
    out.clear();
    heights.clear();
    if (!hasUnit(player, id)) return false;
    const float c = std::cos(p.yaw), s = std::sin(p.yaw);
    auto push = [&](const units::FigurePlacement& fp, float wx, float wy, float yaw, float z, bool yawKnown = true) {
        // world delta into the unit's frame: R(-yaw) (wx - x, wy - y)
        const float ddx = wx - fp.x, ddy = wy - fp.y;
        units::FigureOffset o;
        o.figure = fp.figure;
        o.dx = c * ddx + s * ddy;
        o.dy = -s * ddx + c * ddy;
        o.hasYaw = yawKnown;
        o.yaw = yawKnown ? yaw : 0.f;
        out.push_back(o);
        heights.push_back(z);
    };
    if (t <= end_) {
        for (auto& fp : p.placed) {
            FigurePose ps;
            if (!pose(t, player, id, fp.figure, ps)) continue;
            push(fp, ps.x, ps.y, ps.yaw, ps.z, ps.yaw_known);
        }
        return true;
    }
    // the return and the reform [I]: rank i of the survivors walks to the i-th placed figure's slot at MoveRate x 64
    std::vector<const Fig*> alive;
    for (auto& kv : figs_)
        if (kv.second.player == player && kv.second.id == id && !kv.second.died) alive.push_back(&kv.second);
    std::sort(alive.begin(), alive.end(), [](const Fig* a, const Fig* b) { return a->figure < b->figure; });
    std::vector<const units::FigurePlacement*> slots;
    for (auto& fp : p.placed) slots.push_back(&fp);
    std::sort(slots.begin(), slots.end(),
              [](const units::FigurePlacement* a, const units::FigurePlacement* b) { return a->figure < b->figure; });
    bool walking = false;
    for (size_t i = 0; i < alive.size() && i < slots.size(); ++i) {
        FigurePose e;
        pose(end_, player, id, alive[i]->figure, e);
        const float ex = slots[i]->x - e.x, ey = slots[i]->y - e.y;
        const double d = std::sqrt(static_cast<double>(ex) * ex + static_cast<double>(ey) * ey);
        // 4726: an aircraft still in the air at the end (a flight home cut by the limit) comes down along its walk back
        // over the longer of the ground distance and its height [I]
        const double span = std::max(d, static_cast<double>(std::max(0.f, e.z)));
        const double walked = (t - end_) * static_cast<double>(alive[i]->rate) * kMoveRateUnits;
        if (walked >= span) continue;   // in its slot: placed as the inner source says
        const double a = walked / span;
        push(*slots[i], static_cast<float>(e.x + ex * a), static_cast<float>(e.y + ey * a),
             d > 0 ? yawFacing(ex, ey) : e.yaw, static_cast<float>(std::max(0.f, e.z) * (1.0 - a)),
             d > 0 || e.yaw_known);
        walking = true;
    }
    if (!walking) {
        out.clear();
        heights.clear();
    }
    return walking;
}

void FightPlacement::set(int32_t player, int32_t id, std::shared_ptr<const FightMotion> m, double t) {
    if (!m) {
        clear(player, id);
        return;
    }
    units_[std::make_pair(player, id)] = std::make_pair(std::move(m), t);
}

void FightPlacement::clear(int32_t player, int32_t id) { units_.erase(std::make_pair(player, id)); }

units::UnitStatus FightPlacement::place(const units::PlacementRequest& r, units::UnitPlacement& out) {
    if (!inner_) return units::UnitStatus::fail(units::UnitCode::PlacementFailed, "fight placement: no inner source");
    auto it = r.unit ? units_.find(std::make_pair(r.unit->player, r.unit->id)) : units_.end();
    // issue 4982 (lead-0042 #11560 (1)): a unit held here (the whole fight and its settled tail) stands on the EXE's floor:
    // its foot radius (FixUnitPlacement) is dropped, since applyFigureOffsets moves each figure from max(heightAtWorld,
    // water), not from the foot base, and would carry the gap as a false ZOffset
    units::PlacementRequest q = r;
    units::UnitInput stockFeet;
    if (it != units_.end() && r.unit->foot_radius != 0.f) {
        stockFeet = *r.unit;
        stockFeet.foot_radius = 0.f;
        q.unit = &stockFeet;
    }
    units::UnitStatus st = inner_->place(q, out);
    if (!st.ok() || !r.unit) return st;
    if (it == units_.end()) return st;   // no fight: exactly the inner placement
    std::vector<units::FigureOffset> offs;
    std::vector<float> heights;
    if (!it->second.first->offsets(it->second.second, r.unit->player, r.unit->id, out, offs, heights)) {
        settled_++;
        return st;
    }
    if (!r.grid) return units::UnitStatus::fail(units::UnitCode::PlacementFailed, "fight placement: no height field");
    for (float h : heights)
        if (!std::isfinite(h))
            return units::UnitStatus::fail(units::UnitCode::PlacementFailed, "fight motion: a height is not finite");
    Status s = units::applyFigureOffsets(*r.grid, r.water_level, offs, out);
    if (!s.ok()) return units::UnitStatus::fail(units::UnitCode::PlacementFailed, "fight motion: " + s.str());
    // 4726: an aircraft's height above its ground placement (applyFigureOffsets kept the figure's ZOffset over the new
    // ground; the flight's height comes on top of it)
    for (size_t i = 0; i < offs.size() && i < heights.size(); ++i) {
        if (heights[i] == 0.f) continue;
        for (auto& pf : out.placed)
            if (pf.figure == offs[i].figure) pf.z += heights[i];
    }
    moved_++;
    return st;
}

}}  // namespace c5r::unitanim
