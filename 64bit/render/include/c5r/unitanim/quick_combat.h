// c5r/unitanim/quick_combat.h - ticket 0796 (Quick Combat Animations T1 core, D270 V2): the playback rate of one fight
// played faster, as a function of the fight's own (scaled) time and the choreography's result so far. Header only.
// Hand-written for this repository (research/05-graphics-composition/quick-combat-extra.md sections 3, 9 and 11).
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The server's fight clock (srvunits::FightClock) asks its player for the rate of each step; the player answers with
// QuickCombatRate on its CombatRun's result. The clock then steps the fight's time, the choreography and the fighters'
// clip clocks by dt x rate together, so the figures' clips, the core's waits on them and the trigger stream stay on one
// clock. Off (rate <= 1): 1 always, the rule before 0796.
#pragma once
#include "c5r/unitanim/combat.h"

namespace c5r { namespace unitanim {

struct QuickCombat {
    double rate = 0;       // the rate after the first strike has been shown (V2: 3); <= 1 = off (every fight at 1x)
    double pre_rate = 1;   // the rate of the run-up, before the first strike starts (the approach, the combat-ready
                           // entry, the line-up phases as route S plays them); <= 0 = 1
    double react = 0.35;   // seconds of the target's reaction after the first landing still played at 1x
    bool on() const { return rate > 1.0; }
};

// The owner's choice (D270): V2, the first strike at 1x and the rest at 3x; the run-up's rate measured offline by 0796
// (quick-combat-extra.md section 11).
inline QuickCombat QuickCombatV2()
{
    QuickCombat q;
    q.rate = 3.0;
    q.pre_rate = 2.0;
    q.react = 0.35;
    return q;
}

// The rate of the step that starts at `elapsed` (fight time, seconds since the fight's start). Off: 1. On: pre_rate
// until the first strike starts, 1 from there until the first landing plus `react`, then `rate`. The first strike
// starts at the first hit the core holds or a city's first shot at its launch, whichever comes first, and before that
// in a melee fight at a figure's first Strike step (a run charge, 1120, is the run-up; a rider's hit at its arrival ends
// it), in a ranged fight (`ranged`: the record's kCombatRanged) at the release of its first projectile (`released`:
// the caller saw a Transfer trigger of the attacker's figures; the draw and aim before it are the run-up, the flight
// is seen at 1x). A fight with no strike yet stays at pre_rate; one whose first landing is known stays at 1 until its
// reaction is shown.
inline double QuickCombatRate(const CombatResult& r, double elapsed, const QuickCombat& q, bool ranged = false,
                              bool released = false)
{
    if (!q.on())
        return 1.0;
    for (const CombatHit& h : r.hits)
        if (h.time + q.react <= elapsed + 1e-9)
            return q.rate;
    bool struck = !r.hits.empty() || !r.launches.empty() || (ranged && released);
    for (size_t i = 0; !ranged && i < r.steps.size() && !struck && r.steps[i].time <= elapsed + 1e-9; ++i)   // time order
        struck = r.steps[i].state == FigState::Strike;
    if (struck)
        return 1.0;
    return q.pre_rate > 0 ? q.pre_rate : 1.0;
}

}}  // namespace c5r::unitanim
