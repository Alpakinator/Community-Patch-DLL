# Quicker Combat Animations: a brief fight as an Extra (ticket 0785, design)

The owner, 2026-11-25 22:1x (Extras idea 2, beta 6) [reworded for privacy]: a "Quicker Combat Animations" option, like Civilization VI's quick
combat, that still shows an animation but a very brief one: drop the opening line-up of the fight, play the
start into one attack, then the end of the animation sped up 2-3x; options to be shown as MP4 videos first.

This file is the design: what a fight consists of today (stock and route S), what each variant cuts or speeds up, how
the EXE keeps getting every event it waits for, what it costs, the `[Extras]` key and the implementation tickets. The
videos are described in section 8 (they are in the owner's deliverables folder, never in git, COMMON 27).

Written 2026-11-25 from 22:20 EST by impl-0785 (account B), branch `impl-0785-quick-combat` from main c597bc653.
Marks: `[C]` read in this repository's code by this ticket (file named), `[C 0042]` / `[L 0042]` a fact as epic 0042's
research gives it (not re-checked here; the section is cited), `[L]` seen in this ticket's own live run (section 8),
`[I]` inferred. No EXE address, no build-26577625 data (D202), nothing from the EXE or the game's files (D83).

## 0. In short

- **Route S makes this cheap.** Since D210 the server owns each fight's clock and choreography (the EXE's Combat and
  Projectile systems are off under `[Renderer] ServerCombat = 1`, the shipped default `[C: proxy/src/proxy_route_s.h]`).
  The EXE waits for one thing per fight: the proxy's hold on the fight's `RunCombatSim` record, dropped at the server's
  `OwnerEnd` for that fight (`unit-combat-server.md` 4, 6; `proxy/src/proxy_combat_hold.h`). Everything the player sees
  from the EXE (the unit flags back, the damage number, the death, the advance, the next fight of the same unit, the
  rest of an AI turn) follows that drop in the EXE's own order. A shorter server fight therefore moves all of those
  earlier together and keeps their order: nothing in the EXE has to change and no EXE byte is written (D228 not needed).
- **What a fight is made of** (stock, measured live by 0042's W52 on 174 fights, `unit-combat-server.md` 18.15.1
  `[L 0042]`): a reforming melee attack (Great War Infantry class) is reform 0.72 s + turn 0.41 s + strikes 3.32 s +
  the walk home 1.13 s = 5.58 s; a ranged attack 4.1-4.5 s, of which the line-up (reform + turn) is 0.5-0.7 s and the
  walk home 1.0-1.1 s; a melee on a city 3.50 s; a cavalry charge kill 6.98 s. An ancient Tiny volley or melee is 4.0-4.9
  s (`unit-combat-server.md` 0, P4). Route S reproduces these phases with its own knobs (`reform_first`,
  `reform_ends_at_arrival`, `attack_from_stance`, `turn_first`, `walk_home`, all on by default since 2026-11-22).
- **The variants** (section 3): V1 skip the line-up, play the first strike at 1x, the rest at 2x; V2 the same at 3x;
  V3 skip the line-up, play the first strike, then end the fight through its normal end (the deaths and the survivors'
  return applied in a short blend); V4 (for comparison) the whole fight at 2x. The line-up skip is route S's existing
  knobs switched off for that fight; the speed-up is one new number, a per-fight playback rate applied to the fight's
  clock and to its figures' clip clocks together; V3 is one new early end through the core's normal end test (lead-0042
  #16107: never the limit's forced end).
- **Projected lengths** (section 3.3, from the stock phase means; the videos measure them on stock footage): V1
  shortens a 5.6 s reforming melee to about 2.7 s, V2 to about 2.2 s, V3 to about 1.4 s; an ancient melee of about
  4.6 s to about 2.6 / 2.1 / 1.4 s `[I]`.
- **The videos** (section 8, `[L]` stock footage, retimed): stock's own fights on the laptop, every displayed frame,
  then V1-V4 made from that footage. Panel lengths from the order: melee 4.40 s stock -> V1 2.46 / V2 2.07 / V3 1.66;
  ranged 5.00 -> 2.05 / 1.82 / 1.70; mounted charge 6.50 -> 3.60 / 2.90 / 1.85; city attack 3.25 -> 2.35 / 2.08 / 1.88;
  reforming gun melee 6.30 -> 2.87 / 2.24 / 1.32.
- **The prototype** (section 9, offline, 120 runs PASS): the rate after the first landing keeps clips, choreography and
  the trigger stream together (route S's fights 0.58-0.85x of their length at 2x / 3x, no fight at the limit). It also
  showed that **route S's line-up knobs are not where its time before the first hit goes**: switching off the reform,
  the turn and the phase-2 stance saved 0-0.2 s, and made ranged and city attacks slower; route S's run-up is mostly
  the approach and the combat-ready entry. So the shipped form should speed the run-up too (a pre-strike rate), not
  switch stock-parity knobs off.
- **The key** (section 6): `[Extras] QuickCombatAnimations`, values `0` off (the default, D255), `1` = V1, `2` = V2,
  `3` = V3 (final values after the owner's pick from the videos); offered on the installer's options page; in effect
  only with `ServerCombat = 1` and the game's Quick Combat off (with Quick Combat on there is no fight to shorten).
- **Cost** (section 7): four implementation tickets, about 17-24 h in all; the core change is small, most of the cost
  is the sound behaviour under a faster clock and the live acceptance.

## 1. Scope and premises

- Display only. The DLL resolves the fight before anything is drawn (`combat-choreography.md` 1 item 1 `[C 0042]`): the
  damage, the deaths, the capture and the advance are decided at the order. A shorter picture changes no game result,
  no save and nothing sent to other players. So the Extra belongs in `[Extras]`, not in a gameplay category.
- Only fights the server plays: route S, `ServerCombat = 1`. With `ServerCombat = 0` the EXE plays its own
  simulation (D147) and the Extra does nothing (one log line), because a change there would need EXE code writes.
- Only visualised fights. With the game's Quick Combat on the DLL makes no combat call at all (`combat-choreography.md`
  7 `[C 0042]`): there is no fight to shorten; a fight the player does not see is not visualised either.

## 2. The phases of a fight, stock and route S

### 2.1 Stock, by phase (`unit-combat-server.md` 18.15, 18.15.1 `[C 0042]`, `[L 0042]`)

The EXE's simulation runs a fight by fight type and phase: phase 1 the reform (a side whose `ReformBeforeCombat` names
it re-lays its formation in place, turned to the opponent, and every figure runs to its new slot), phase 2 the turn
(ships turn broadside, land figures wait in 0xF), then the strikes (types 1 and 4: both sides strike from where they
stand; type 2: the attacker charges and the defender waits for the first landing; type 0: a city target), then the end:
every finished figure that is away from its home slot runs back, so `EndCombatSim` comes after the walk home.

Live means of W52's run (Huge turn 350-359, stock EXE build 4390913, no patch, 2026-11-22), seconds:

| Class (n) | reform | turn | strikes | end (walk home) | whole |
|---|---|---|---|---|---|
| melee, type 1, reforming attacker (23) | 0.722 | 0.406 | 3.322 | 1.126 | 5.576 |
| melee on a city, type 0 (10) | 0.317 | 0.182 | 2.234 | 0.767 | 3.499 |
| cavalry charge kill, type 2 (1) | 0.014 | 0.598 | 2.928 + 3.444 | 0.812 | 6.984 |
| ranged, type 1, reforming attacker (32) | 0.254 | 0.468 | 2.688 | 1.119 | 4.530 |
| ranged, type 1, no reform (35) | 0.033 | 0.477 | 2.633 | 0.971 | 4.114 |
| ranged on a city, type 0 (34) | 0.030 | 0.194 | 1.608 | 1.110 | 2.942 |

Ancient units on Tiny (`unit-combat-server.md` 0, P4 `[L 0042]`): an archer's volley on a warrior 4.03-4.87 s, a
warrior's melee on a warrior 4.53-4.77 s, Run to End. The opening line-up the owner names [reworded for privacy] is phases 1 and 2 plus,
for a gun unit or an archer, the combat-ready stance (1600) before the first strike.

### 2.2 Route S (as on main 2026-11-25)

The server's fight clock (`server/src/units/srv_units_fights.{h,cpp}`, `FightClock`) starts each fight at the DLL's
combat record, steps it once per server frame by the frame's `dt`, waits per unit and as a barrier like the EXE's queue,
and ends it at the choreography's own end or the 12 s / 20 s limit; it then sends the fight's `OwnerEnd` (the trigger
stream, then the proxy) `[C: srv_units_fights.h]`. The choreography is the 4775 core (`render/src/unitanim/combat.cpp`,
`CombatRun`), stepped live against the animation controller (`ControllerFightPlayer` in `srv_units_anim.cpp`): a strike's
clock starts when the figure's attack clip plays, hits land at the clip's Action trigger, deaths follow the damage percent
`[C]`. Stock's phases are built in as `CombatParams` knobs, on by default: `reform_first` (phase 1, 18.17),
`reform_ends_at_arrival` and `attack_from_stance` (phases 1 + 2 as stock ends them, 18.23), `turn_first` (the turn
before a standing fight, 10.11), `ready_after_move` (10.12), `walk_home` (the survivors' walk inside the fight, 18.18),
`charge_any` (10.10 / 18.28) `[C: srv_render_host.cpp's render_test_units_* keys]`.

So every phase the variants touch already has a name and a switch in the core; the clock is in one place.

## 3. The variants

### 3.1 What each one does, in route S's terms

| Variant | Line-up (reform, turn, combat-ready entry) | First strike | Rest of the fight (other strikes, deaths, victory poses, walk home) |
|---|---|---|---|
| V1 | skipped: `reform_first`, `turn_first`, the combat-ready wait off for this fight; the attackers strike from their own slots (a charge still runs to contact) | 1x | 2x |
| V2 | as V1 | 1x | 3x |
| V3 | as V1 | 1x | not played: the fight ends after the first strike's hit through the core's normal end (deaths applied, survivors refaced in their slots) with a 0.3-0.4 s blend of the figures to their end poses |
| V4 (comparison) | played | 2x | 2x |

"First strike" is the first landing of the fight (the first `CombatHit` whose landing time has passed, in the core's
result) plus the target's reaction clip, so the player sees one full blow land. A ranged volley's first landing is the
first arrow or shell arriving, so the volley is seen in full flight once.

### 3.2 How the rate works

One number per fight, `rate` (1 until the first landing, then 2 or 3), applied in three places at once, so that the
figures' clips, the choreography and the motions stay on one clock:
1. **The fight clock**: `FightClock::frame` adds `dt x rate` to the fight's elapsed time (its limit and its motions
   read that time), and passes `dt x rate` to `FightPlayer::advance` (so `CombatRun` and `FightMotion` see the fight's
   own time).
2. **The figures' clips**: the controller advances each unit's clock by the frame's `dt` (`Controller::apply`,
   `u.clock.take(frame.dt, ...)` `[C: render/src/unitanim/controller.cpp]`); a per-unit rate (a map from unit key to
   rate, set each frame for the units of a playing fight and cleared at the fight's end) makes it `dt x rate` for those
   units only. Every other unit on the map keeps 1x.
3. **The walk home and the motion after the end**: `AnimRunner`'s motions step `m.t += f.dt` `[C: srv_units_anim.cpp]`;
   for a fight's units they step by `dt x rate` until the motion ends.

The trigger stream (`srv_trigger_tap`, `srv_trigger_emitter`) and the per-clip `ClipEnded` owner ends read the
controller's clip instances and crossed triggers, so they follow the scaled clip clock by construction; this is the
first thing the prototype's offline test checks (lead-0042 #16107 point 2) `[I]`.

### 3.3 Projected lengths (from 2.1's means, before any measurement)

With L the line-up (reform + turn, plus the combat-ready entry where stock has one), F the time from the end of the
line-up to the first landing plus its reaction, R the rest: V1 = F + R / 2, V2 = F + R / 3, V3 = F + 0.35, V4 =
(L + F + R) / 2. F is not in W52's table; one attack clip and its hit reaction is about 0.8-1.2 s `[I: the warrior's
1100 Action trigger at 0.49 s (combat-choreography.md 6), plus the hit reaction]`; with F = 1.0 s:

| Fight (stock whole) | L | F | R | V1 | V2 | V3 | V4 |
|---|---|---|---|---|---|---|---|
| reforming melee, type 1 (5.58 s) | 1.13 | 1.0 | 3.45 | 2.7 | 2.2 | 1.4 | 2.8 |
| melee on a city (3.50 s) | 0.50 | 1.0 | 2.00 | 2.0 | 1.7 | 1.4 | 1.8 |
| ranged, reforming (4.53 s) | 0.72 | 1.0 | 2.81 | 2.4 | 1.9 | 1.4 | 2.3 |
| ancient melee, Tiny (about 4.6 s) | 0.4 | 1.0 | 3.2 | 2.6 | 2.1 | 1.4 | 2.3 |

The measured cut points of the recorded stock fights and the lengths each video panel shows are in section 8.

## 4. What the EXE waits for, and why each stays correct

| What the player or the EXE needs | Where it comes from under route S | Under a variant |
|---|---|---|
| the fight's end in the EXE: `EndCombatSim` reaches Lua, the flags show again (UnitFlagManager hides on Run, shows on End) | the proxy's hold on the `RunCombatSim` record is dropped at the first EXE frame after the server's `OwnerEnd` for that fight; `EndCombatSim` waits behind it in the EXE's queue (`combat-choreography.md` 3.5 `[C 0042]`, `proxy_combat_hold.h`) | the `OwnerEnd` comes earlier; the order Run -> End -> damage stays the EXE's own. V3 must reach the end through the core's normal end test so the end step, the reface and the hand-over run as in a normal fight (lead-0042 #16107 point 3) |
| the damage number, the death, the removal, the advance, the capture | the DLL's `SetDamage`, `DestroyedInCombat`, `Destroyed` and `Moved` records name the units, so the EXE's queue holds them behind the held record (`combat-choreography.md` 3.2, 8 items 1-2 `[C 0042]`) | the same records, released earlier, in the same order |
| the next fight of the same unit, and every later record in an AI turn the human watches (the barrier) | the per-unit wait and the barrier in `FightClock` (fights start one frame after the one they wait for) and the EXE's own queue behind the hold | unchanged rules; each fight is shorter, so an AI turn with many fights the human sees ends sooner. This is the player-visible gain besides the shorter fight |
| the 12 s / 20 s limit | `FightClock`, in fight time | in fight time (the scaled clock): a fight at 3x reaches the limit after 4 s of wall time at the most; the limit stays the fault path and is counted |
| the proxy's watchdog (the limit + 2 s, wall time) | `proxy_combat_hold.h` `kLimitMs` + `kGraceMs` | a faster fight only moves the `OwnerEnd` earlier; the watchdog is unchanged and is never the way a fight ends |
| the per-clip owner ends (`ClipEnded`) and the trigger stream's frame records | the controller's clip instances | follow the scaled clip clock (3.2); checked offline in the prototype |
| the combat sounds (strike, impact, death, battle beds) | the trigger stream's Sound triggers, played by the shell's sound executor through the EXE's audio (`unit-combat-server.md` 10) at the clip's trigger times | sent `rate` times as often; each sound plays at its own length, so at 3x they overlap or are cut short. Ticket T3 (section 7) decides the rule: keep every first-strike sound, thin repeated strike and impact sounds of one unit to one per original interval, end the battle bed with the fight |
| the camera's move to the target | the DLL's `lookAt` at the order (`unit-combat-server.md` 0, S1 `[L 0042]`) | unchanged (the camera is not part of the fight) |
| the attacker's mission timer | the DLL: `GetPostCombatDelay()` 1 with Quick Combat off (`combat-choreography.md` 7 `[C 0042]`); the DLL does not wait for the picture | unchanged |
| unit positions for the EUI flags | the server's fight motion (epic 0042) | follow the scaled motion |

Nothing in this table needs an EXE byte, a new EXE call or a DLL change. The only shared parts the variants touch
are in the server and the render library (section 5).

## 5. What it costs in code

| Part | Files | What changes | Size `[I]` |
|---|---|---|---|
| rate in the fight clock | `server/src/units/srv_units_fights.{h,cpp}` | `Fight::rate` (default 1); `frame()` steps `elapsed += dt x rate` and calls `advance(dt x rate)`; a `FightPlayer::rateFor(fight)` query | 20-40 lines |
| per-unit clip rate | `render/include/c5r/unitanim/controller.h`, `render/src/unitanim/controller.cpp` | `setRate(UnitKey, double)` / `clearRates()`; `apply` takes `dt x rate` for a unit with a rate | 15-25 lines |
| first landing and the early end | `render/include/c5r/unitanim/combat.h`, `render/src/unitanim/combat.cpp` | one query `CombatRun::firstLandingDone()`; for V3 one `CombatParams` field that makes the normal end test in `Sim::tick` true after the first landing's reaction (built on 5072's version of that test) | 30-60 lines |
| the wiring | `server/src/units/srv_units_anim.cpp` (`ControllerFightPlayer`, the motions), `srv_units_scene.cpp`, `server/src/render/srv_render_host.cpp` | the fight's rate from the variant and the run's first landing; the line-up knobs off per fight; the rates to the controller each frame; one host key | 60-100 lines |
| the option | `proxy/src/proxy_options*`, `docs/OPTIONS.md`, the installer's page, `server.cfg` line | `[Extras] QuickCombatAnimations` read, logged, written to `server.cfg` for the server; refused values named | as the other extras |
| sounds under the rate | `server/src/effects/srv_trigger_emitter.*` (or the shell's executor) | the thinning rule of section 4 | 40-80 lines |

Every builder of a changed shared file links in the merge check (the runbook's builders table, MC merge rule); the
scene library holds no fight, so the offline library replays are not expected to change, and a `render_config` key
answers rule A as 4737, 4735 and 4745 did (`offline-scene-harness.md` 35-37).

## 6. The `[Extras]` key, and the game's Quick Combat

- **Name and values.** `[Extras] QuickCombatAnimations`: `0` off (default, D255: the game's fights as stock plays
  them), `1` V1 (line-up skipped, rest at 2x), `2` V2 (rest at 3x), `3` V3 (first strike, then the result). The values
  are final only after the owner's choice from the videos; a value the owner does not want is not offered.
- **Category.** It changes base graphics (the picture of a fight) and nothing else, so it is a visual addition and
  needs `EnableVisualAdditions = 1` like the others (`docs/OPTIONS.md`, "Extras"). Open for the MC: the installer may
  offer it on its own line, since a player who wants brief fights may not want the other additions.
- **Needs** `ServerCombat = 1` (route S); with `0` the line does nothing and the logs say so in one line
  (`options: extras: QuickCombatAnimations: off: ServerCombat = 0`).
- **The game's Quick Combat** (the game option `GAMEOPTION_QUICK_COMBAT`, set per game in the setup screen and in the
  in-game options; the EXE's user options `SinglePlayerQuickCombatEnabled` / `MultiplayerQuickCombatEnabled`) stays
  as it is: on = no fight at all (the DLL makes no combat call); off = the fight, shortened by this Extra when it is on.
  The two do not interact: with Quick Combat on the Extra has nothing to act on. Civilization VI's "quick combat" is
  between the two, which is what this Extra adds.
- **Possible later value** `[I]`: shorten only the fights of other players' turns (the AI turns the human watches),
  where the barrier makes each fight a wait; keep the human's own attacks at full length.
- **Multiplayer.** Display only, read per machine: two players may use different values without any effect on the
  game (the DLL's results do not depend on the picture).

- **The owner's choice (D270, 2026-11-26 00:2x)**, after the videos [reworded for privacy]: V2 was chosen, to be offered as the one .ini option for now, in the extras.
  So the key is one on/off line: `[Extras] QuickCombatAnimations` = `0` off (the default), `1` = V2 (line-up skipped, the first strike
  at 1x, the rest at 3x). V1, V3 and V4 are not offered. Tickets: 0796 T1, 0797 T2, 0798 T3, 0799 T4 (beta 6).

## 7. Implementation tickets (proposed; ids from the MC)

| Ticket | What | Depends on | Size |
|---|---|---|---|
| T1 core | the rate in `FightClock`, the per-unit clip rate in the controller (both prototyped on `impl-0785-quick-combat`, section 9), a pre-strike rate for the run-up (section 9 item 3: not the line-up knobs), V3's early end through the normal end test; TEST keys default 0; offline: trigger_scene / combat_scene lengths per variant and kind, the trigger stream and `ClipEnded` on the scaled clock, no fight at the limit, ASan | 0042's open combat branches merged (5061, 5072, 5074, lead-0042 #16107) | 6-8 h |
| T2 option | `[Extras] QuickCombatAnimations` in the proxy's options, the server.cfg line, the logs, `docs/OPTIONS.md`, the installer's options page, D118 refusal text for bad values; the rule A answer (render_config key) | T1 | 3-4 h |
| T3 sounds | the sound rule under a rate above 1 (section 4): which sounds keep, thinning, the battle bed's end; live listening pairs against stock | T1 | 3-5 h |
| T4 acceptance | live pairs on the product path (laptop or a workstation instance): each value against stock for melee, ranged, city, charge, a kill, two fights in a row on one unit and an AI turn with fights the human sees; checks: `EndCombatSim` and the damage events after the server's end, no hold to the watchdog, no fight at the limit, flags back, the turn ends; the pictures and an MP4 for the owner | T1-T3 | 4-6 h |

About 16-23 h in all. A prototype of T1's first half (the rate, measured offline) is on this ticket's branch (section 9).

## 8. The videos (the owner's MP4s)

**Where** (never in git, COMMON 27): the owner's deliverables folder, `Desktop/c5-64bit-patch-deliverables/quick-combat-0785/`:
`0785-melee.mp4`, `0785-ranged.mp4`, `0785-mounted.mp4`, `0785-city.mp4`, `0785-gunmelee.mp4` (each 2 x 2 panels:
STOCK, V1, V2, V3; 1280 x 934, H.264, 30 fps, 160-258 frames, 21-36 MB) and `V4-whole-fight-2x/` (the same five, STOCK
beside V4); `videos_0785.json` in each folder (sizes, frames, panel lengths). SHA-256 of the main five: city 1ad47ab5...,
gunmelee 3c20e4cb..., melee cf4b5100..., mounted db97b371..., ranged a3895195... Every panel is captioned
"DIAGNOSTIC MOCK-UP: stock footage retimed (laptop, stock EXE, no Vibrant Textures); not the patch's rendering" (D112,
D94: the laptop's Steam install has no Vibrant Textures). No sound.

**How** `[L]`: laptop tb1 (the machine's own Steam EXE, stock: no proxy, no server, the instance's 32-bit VP DLL; mods cp,
vp, eui), headed testbed job `20261125-235903-1300` (2026-11-25 23:59-00:06 EST) of
`server/evidence/0785/fight_capture_job.py`: a copy of `TB-TINY_0008` (sha256 b2ceb663..., Tiny, turn 8), Quick
Combat sent off and read back false, Quick Movement off, the minor civs met in memory (no greeting popup), the popups
closed before each fight (none were open), the pointer moved off the fight; per fight two units spawned by Lua, the
camera on the pair (one-argument LookAt, 3 wheel ticks in, the plot's pixel checked on the EXE's camera), the order by
the selection net message, DXGI desktop duplication of every displayed frame (30 fps: the laptop's frame cap; 241-331
frames per fight, interval p50 33.3 ms), colour JPEG at 1/1.6. A Lua listener stamped the events; the restore by
SHA-256 and `provision.py verify` passed (exit 0). The variants are made by `make_videos_0785.py` from that footage with
the cut points of `server/evidence/0785/cuts_0785.json` (chosen by eye on contact sheets; section 3.1's rules: line-up
cut with a 0.15 s blend, first strike at 1x, the rest at 2x / 3x with the frames an output frame spans averaged, or a
0.35 s blend to the end frame). So V1-V3 show the stock picture retimed: what a faster fight clock does to the figures
(their clips and motions scale together), with two differences from a real implementation: the line-up cut is a blend
where the real thing would not lay the figures out at all, and anything off the fight (water, other units) is sped up
too in the mock-up.

**Stock's fights as recorded** `[L, Lua clock of the job; RunCombatSim -> EndCombatSim]` and the panel lengths (order
to the end frame, s):

| Fight | Run -> End | cuts: line-up end / first strike shown / end | STOCK | V1 | V2 | V3 | V4 |
|---|---|---|---|---|---|---|---|
| melee: Swordsman on a barbarian Swordsman | 4.10 | 1.0 / 2.1 / 4.4 | 4.40 | 2.46 | 2.07 | 1.66 | 2.23 |
| ranged: Archer on a barbarian Warrior (two plots) | 4.70 | 2.4 / 3.6 / 5.0 | 5.00 | 2.05 | 1.82 | 1.70 | 2.50 |
| mounted: Horseman on a barbarian Spearman | 6.13 | 1.0 / 2.3 / 6.5 | 6.50 | 3.60 | 2.90 | 1.85 | 3.28 |
| city: Swordsman on M'banza-Kongo (city state) | 2.97 | 0.25 / 1.6 / 3.25 | 3.25 | 2.35 | 2.08 | 1.88 | 1.64 |
| reforming melee: Great War Infantry on a Machine Gun | 5.96 | 1.75 / 2.5 / 6.3 | 6.30 | 2.87 | 2.24 | 1.32 | 3.19 |

What the sheets show `[L, by eye]`: the melee line-up is about 0.75 s (both sides take their stance before the charge),
the GWI's reform about 1.5 s (figures re-laid in place, then aiming), the ranged attack about 1.9 s from the order to
the volley's launch (the camera's move to the target, the archers' stance), the city attack almost none (the
swordsmen run at once); the walk home 0.5-1.0 s in every kind; the damage numbers appear about 0.2 s after
`EndCombatSim`. An earlier job (`20261125-232503-28d3`) is not used: a city state's greeting popup covered four of its
five fights (the reason for the meet-and-close steps); its Run -> End times were 3.96 / 4.65 / 5.49 / 3.00 / 5.77 s
(melee, ranged, mounted, city, reforming melee), within 0.2 s of these except the mounted fight (0.64 s shorter).

Incident on the way (`[L]`, resolved): job `20261125-233832-21f8` met every player in memory, the major civs too; their
first-contact leader screens silenced the Lua channel and the harness stop refused (exit 6, game left running). MC-B's
(A) (#16318): job `20261125-235559-854c` pressed Escape three times (the channel answered), the harness stop passed, the
job's backup was restored by hash and `provision.py verify` passed. Rule for later fight drivers: meet minor civs only.

## 9. The prototype on this branch (TEST ONLY, never merged by 0785)

Branch `impl-0785-quick-combat` (lead-0042's yes #16107: TEST keys default off, measured offline, the combat core
untouched). What is built `[C: the code]`:
- `FightPlayer::rate(const Fight&)` (default 1) and `Fight::rate`; `FightClock::frame` steps a started fight by
  `dt x rate` (its elapsed time, its limit and the player's `advance`); `FightClock::unitRate(owner, unit)` gives the
  rate of the unit's started fight (`server/src/units/srv_units_fights.{h,cpp}`).
- `Controller::setRate(UnitKey, rate)` / `clearRates()`; `apply` takes `dt x rate` for such a unit
  (`render/include/c5r/unitanim/controller.h`, `render/src/unitanim/controller.cpp`).
- `AnimRunner::setQuick(rate, skipLineup)` (off: 0); `ControllerFightPlayer::rate`: 1 until a hit of the run's result
  has landed 0.35 s ago in fight time, then the quick rate; with `skipLineup` the fight's `reform_first`, `turn_first`
  and `attack_from_stance` are off; every frame the fighters' controller rates come from `FightClock::unitRate`; a
  residual walk after the end steps at the quick rate (`server/src/units/srv_units_anim.{h,cpp}`). No host key yet
  (the live wiring is T1's), so the server as built on this branch behaves as main.
- `trigger_scene.exe` (`server/tests/effects/trigger_scene.cpp`): `--quick-rate R` (1 < R <= 8) and
  `--quick-skip-lineup`, the same rule in its own player and the same controller rates, and a `quick:` line
  (`firstLanding`, `rateFrom`, `fightEnd` in fight time, `wallEnd` in wall time from the fight's start to its OwnerEnd,
  `atLimit`).
- Driver `server/evidence/0785/quick_scenes_0785.py`: five kinds x two cases x four arms (off; V1 = rate 2 + skip;
  V2 = rate 3 + skip; R2 = rate 2, line-up kept) x seeds 1-3, every run through trigger_scene's own checks of the
  trigger stream (R1-R8 of `unit-combat-server.md` 7.4 on the scaled clock).

**Offline result** `[C: run 2026-11-25 23:00-23:14 EST, workstation (loaded: an indication of lengths in fight
frames, not a timing claim), trigger_scene from this branch @ 2026-11-25 22:49 tree, the pure_vp install copy, 0327's
database copy; F:/c5-64bit-work/0785/qs.csv, qs.log]`: **RESULT PASS, 120 runs, 0 not OK, 0 at the limit**. Means
over cases and seeds, wall seconds from the fight's start to its OwnerEnd (first landing in brackets):

| Kind (cases) | off | V1 (2x after the first landing, line-up off) | V2 (3x, line-up off) | R2 (2x, line-up kept) |
|---|---|---|---|---|
| ancient melee (Swordsman, Warrior pairs) | 3.27 (0.80) | 2.25 (0.80), 0.69x | 1.92, 0.59x | 2.25, 0.69x |
| reforming melee, type 1 (GWI / Infantry on MG, turned away) | 4.57 (2.30) | 3.37 (2.10), 0.74x | 3.07, 0.67x | 3.63, 0.79x |
| mounted (Horseman on Spearman, Knight on Pikeman) | 4.19 (1.09) | 2.84, 0.68x | 2.42, 0.58x | 2.84, 0.68x |
| ranged (Archer on Warrior, Cannon on Musketman) | 3.22 (1.92) | 2.70 (1.97), 0.84x | 2.58, 0.80x | 2.75, 0.85x |
| melee on a city (Swordsman, GWI) | 2.56 (1.56) | 2.58 (1.92), 1.01x | 2.49, 0.97x | 2.25, 0.88x |

**What this says** `[C for the numbers, I for the reading]`:
1. The rate after the first landing works on every kind, with the trigger stream's checks passing and no fight at the
   limit: the clock, the clips and the triggers stay together (lead-0042's point 2 of #16107, offline).
2. **Route S's line-up knobs are not where the time before the first hit goes.** Switching off `reform_first`,
   `turn_first` and `attack_from_stance` saved 0.2 s of the reforming melee's 2.3 s to its first landing, nothing in
   the ancient and mounted cases, and made the ranged and city cases' first landing 0.05-0.36 s *later* (without the
   phase-2 stance the attacker plays its whole combat-ready entry before the strike `[I: 18.23's attack_hold rule]`).
   Under route S most of the time before the first hit is the approach (melee figures run to contact: route S's type 1
   approach is 1.0-1.7 s, `unit-combat-server.md` 18.15, where stock strikes from its slots), the combat-ready entry and
   a projectile's flight.
3. So in the shipped choreography the owner's wish [reworded for privacy] to skip the line-up is best done as **the run-up at a higher rate too**
   (for example 1.5x until the first strike's clip starts, then 1x through the first landing, then 2x or 3x), not by
   switching the phase knobs off; the knobs stay as they are (they are stock parity, D66). This is T1's first
   measurement to make (a pre-strike rate, the same `rate()` hook); the videos (section 8) show the stock picture with
   its line-up cut, which is what a faster run-up approaches.
4. Ranged and city fights gain least (0.80-1.0x) because most of their length is before the first landing; V3 (cut
   after the first landing) or a faster run-up is what shortens them.

Not built: V3's early end (T1, through 5072's end test in `Sim::tick`, lead-0042 #16107 point 3); the pre-strike rate;
the host key and the live path; `server.exe` was not linked. The changed server sources were compiled and tested by
`build_units_tests.py` (clang-cl /W3 /WX, every `server/src/units/*.cpp` with this branch's render library):
`test_scene_units: 107 of 107 cases passed` (2026-11-25 23:2x EST, the pure_vp install, 0327's database) `[C]`; no
ASan run and no rule A run (the library holds no fight, `offline-scene-harness.md` 35).

## 10. Not verified, open

- Every projected length in 3.3 (`[I]`); section 8 measures the stock cut points on footage (by eye, one fight each).
- No live route S fight was played faster: the prototype has no host key; the EXE side (hold, `EndCombatSim`, flags,
  damage numbers) under a faster server fight is reasoned (section 4), not seen. T4's live pairs are the check.
- The mock-ups retime stock's picture; route S's own picture of the same fights was not recorded for this ticket.
- The sound behaviour at 2x and 3x (T3); the videos carry no sound.
- Whether the trigger stream and `ClipEnded` follow the scaled clock exactly (T1's offline check). Answered
  2026-11-26 by 0796 (section 11.4: R1-R8 pass on the scaled clock in 120 runs; the hit effects needed a per-hit
  mapping to real time, 11.2).
- The owner's choice of values and of the category (section 6).

## 11. T1 as built (ticket 0796, D270 V2)

Written 2026-11-26 from 13:10 EST by W178 (account B, under epic 0042's claim), branch `w178-0796` from main 4a36d087a.
The prototype of section 9 ported by hand onto main's code (after 5061, 5072, 5074; never merged from
`impl-0785-quick-combat`). Marks as in the head of this file.

### 11.1 The rule, in one place `[C]`

`render/include/c5r/unitanim/quick_combat.h` (header only): `QuickCombat {rate, pre_rate, react}` and
`QuickCombatRate(result, elapsed, q, ranged, released)`, asked by the fight clock before every step of a started fight:

- off (`rate <= 1`): 1, always (the rule before 0796; every `dt x 1` below is `dt` exactly, so main's arms are unchanged);
- `pre_rate` until the **first strike starts**: the first hit the core holds (hits enter the result at their landing),
  a city's first shot at its launch, and before both: in a melee fight a figure's first `Strike` step, in a ranged fight
  (the record's `kCombatRanged`) the release of its first projectile (a `Transfer` trigger of the attacker's figures in
  the controller's last apply);
- 1 from there until the first landing plus `react` (0.35 s, the target's reaction);
- `rate` after it.

`QuickCombatV2()` is the product's V2: rate 3, react 0.35, pre_rate as chosen in 11.4.

Two findings that shaped the rule `[C: trigger_scene --dump-core steps.csv, 2026-11-26]`: (1) route S's melee approach is a
run charge: the attacker's figures enter `Charge` (1120) at t 0, so a `Charge` step is run-up, not the strike (with
`Charge` counted the run-up rate never applied to a melee); (2) an archer's `Strike` step starts 1.63 s before its arrow
lands (Archer on Warrior, two plots: strike 0.633, first landing 2.266): the draw and aim are inside the strike state, so
with the strike state at 1x a ranged fight kept 2.0 s at 1x and could not shorten towards its panel; the release is
where stock's video shows "the volley in full flight once" (3.1), so a ranged fight's 1x part starts there.

### 11.2 Where the rate acts `[C]`

| Part | File, function | What changes (off: nothing) |
|---|---|---|
| the fight clock | `srv_units_fights.{h,cpp}`, `FightClock::frame` step (2) | `FightPlayer::rate(f)` (new virtual, default 1); `f.rate`; `f.wall += dt`; `f.elapsed += dt x rate`; `advance(f, dt x rate)`; the limit in fight time; `unitRate(owner, unit)` = the rate of the fights stepped in the last frame (their end frame too) |
| the clip clocks | `controller.{h,cpp}`, `Controller::setRate` / `clearRates`, `apply` | a unit with a rate takes `dt x rate`, its per-frame cap (`max_steps`, `max_dt`) scaled the same, so a hitch drops the same real time as at 1x |
| the server's player | `srv_units_anim.{h,cpp}`: `ControllerFightPlayer::rate`, `AnimRunner::apply`, `setQuickCombat` | the rule on the run's result; each frame the fighters' controller rates from `unitRate` right before `m_ctl.apply` (the clock's frame runs earlier in the same server frame, in `AnimFeed`) |
| hit effects and city shots | `srv_trigger_tap.{h,cpp}` `AnimTriggerTap::fightClock` | the routes time a hit as fight start + hit time; with a rate each new hit's (and city shot's) time T goes to the routes as `wall - (elapsed - T) / rate` of the step it was taken in, so a hit taken in its landing frame is neither scheduled late nor dropped as late (a first version shifted the start by `wall - elapsed`: lateness then counted in fight seconds, and at 3x a hit could pass the routes' 0.1 s late drop) |
| unit projectiles | none (`srv_effect_routes` unchanged) | a unit projectile's flight keeps its planned real-time path. A projectile released in the phase at 3x would land later than its core hit by up to 2/3 of its flight, or be force-ended at the fight's owner end; measured 0 such releases in every case (11.4), so no change in T1 (lead-0042 #17723: an `addFlight` hunk scaling the path by the rate was written, then taken out as untested); a follow-up for 0798/0799 |
| the switch | `srv_units_scene.{h,cpp}` `UnitsOptions::quickCombat` (0 off, 1 V2) | the runner's `setQuickCombat(QuickCombatV2())` and one log line |

What follows by construction: `ClipEnded` (the clock compares clip instances, which end sooner on the scaled clip
clock), the trigger stream's frame records (the crossed triggers of the scaled clips, one record set per real frame), the
fight motion (`FightMotion` at `f.elapsed`), the geometry hooks (`onFightRun` at fight time), the fight's `OwnerEnd`
earlier. A walk after the fight's end (residual, `m.t += f.dt`) stays at 1x with its clips at 1x (the prototype stepped
the motion at the rate with the clip at 1x); with `walk_home` on (the default) the walk home is inside the fight. The
line-up knobs and every 50xx parity knob stay as they are (ON in every arm).

The test key (added after main's merge with 0753, lead-0042 #17632, #17810): `render_test_units_quick_combat` (TEST
ONLY, 0 default | 1 V2) in `srv_render_host.cpp` (`g_unitsQuickCombat`, `UnitsOptions::quickCombat`, written to
`render_config` as "0" by default, parsed 0 or 1, anything else refused), read by `sceneshot.cpp` (absent = 0; said only
when 1), with refusal rows in `test_render_host.py` and the default in `test_recorder_host.py`. Rule A: with the key 0
nothing the server draws changes; the scene library holds no fight. Not in T1: the player key `[Extras]
QuickCombatAnimations` (T2, 0797).

### 11.3 Offline tools `[C]`

`trigger_scene` (`server/tests/effects/trigger_scene.cpp`): `--quick-v2`, `--quick-rate R`, `--quick-pre-rate P`,
`--quick-react S`, `--quick-report`; a line `quick: rate= pre= react= firstStrike= firstLanding= rateFrom= fightEnd=
wallEnd= atLimit= endReason= transfersAtRate=` (fight time but wallEnd, the real seconds from the fight's start frame to
its OwnerEnd; transfersAtRate: projectiles released in the phase at the rate). The driver
`server/evidence/0796/w178/quick_scenes_0796.py` (five kinds x two cases, case 0 the video's pair; arms off, V2, V2p1,
V2p15, V2p3 (V2 with the run-up at 1 / 1.5 / 3); `--effects --geometry`; every run through
trigger_scene's own checks R1-R8 of `unit-combat-server.md` 7.4) and the build and run script
`server/evidence/0796/w178/allb178.sh`. `test_units_fights.cpp` `testPlaybackRate`: the clock at 3x (own time, the
player's steps, the end, the limit at 12 s fight time = 4 s real, `unitRate` in the end frame) and bad rates as 1.

### 11.4 Offline results `[C]`

Run 2026-11-26 14:31-14:37 EST (final; the P choice from the run of 14:09-14:22 with the same lengths), workstation
(loaded: fight lengths in frames of 1/30 s, not a timing claim), trigger_scene of `w178-0796` at 194515aa2 plus the
working tree of 14:28 (the per-hit mapping), the pure_vp install copy, 0327's database copy, `--effects --geometry`,
seeds 1-3; `F:/c5-64bit-work/0042/w178/allb/q-p3.csv`, `logs/quick_p3.log`. **RESULT PASS: 120 runs, every run
trigger_scene OK (R1-R8 on the scaled clock), 0 at the limit, every V2 run shorter than its off run, the rate after the
first landing in every V2 run; 0 hit effects late, 0 flights forced at the owner end, 0 projectiles released in the
phase at 3x.** Means over seeds, real seconds from the fight's start frame to its OwnerEnd:

| Kind, case 0 = the video's pair (case 1) | off | V2 (P 2) | V2, P 1.5 | V2, P 3 | panel V2 (stock) | V2 / panel | V2/off vs panel V2/stock |
|---|---|---|---|---|---|---|---|
| melee: Swordsman on Swordsman (Warrior on Warrior) | 3.27 (3.83) | 1.60 (2.03) | 1.67 | 1.53 | 2.07 (4.40) | 0.77 (0.98) | 0.49 vs 0.47 |
| ranged: Archer on Warrior, two plots (Cannon on Musketman) | 3.30 (3.13) | 2.30 (1.57) | 2.47 | 2.07 | 1.82 (5.00) | 1.26 (0.86) | 0.70 vs 0.36 |
| mounted: Horseman on Spearman (Knight on Pikeman) | 4.92 (5.74) | 2.08 (2.37) | 2.27 | 1.84 | 2.90 (6.50) | 0.72 (0.82) | 0.42 vs 0.45 |
| city: Swordsman on a city (GWI on a city) | 2.42 (2.70) | 1.97 (1.69) | 2.13 | 1.97 | 2.08 (3.25) | 0.95 (0.81) | 0.81 vs 0.64 |
| reforming melee: GWI on MG, turned (Infantry on MG) | 4.57 (4.57) | 2.53 (2.53) | 2.80 | 3.07 | 2.24 (6.30) | 1.13 (1.13) | 0.55 vs 0.36 |

The run-up rate: P 2 is `QuickCombatV2()`'s (lead-0042 #17723). Sum of the five case-0 misses against the panels: P 1.5
104 %, **P 2 95 %**, P 3 119 %; P 1 (the run-up at 1x, run of 14:09) is longer in every kind (melee 1.80, ranged 2.87,
mounted 2.60, city 2.27, reforming 3.30).

**The ticket's 10 % to the panels is met for city (-5 %) only** (and for the second melee and ranged pairs: -2 %, -14 %).
Why the others miss `[C for the numbers, I for the reading]`:
- melee and mounted are short because route S's own fights are: off, route S plays the video's pairs in 3.27 and
  4.92 s where stock took 4.10 and 6.13 s (Run -> End, section 8); the speed-up itself matches the video (V2/off 0.49
  and 0.42 against the panels' V2/stock 0.47 and 0.45). The gap is a parity matter of the off fights (5068's charge of
  all 12 riders may lengthen mounted), not of the rate.
- ranged and reforming melee are long because stock's video cut a long line-up that route S does not have in that
  form: stock's archer stands 2.4 s from the order before its volley (the camera, the stance), the GWI 1.75 s; route
  S's archer releases at about 1.2 s of fight time `[I: from the V2 and off lengths]` and its arrow lands at 2.3 s, the GWI's first strike starts at 1.53
  s. The 1x part (the release and the flight, or the swing) is longer under route S than the video's 1.2 s / 0.75 s.
- So a rule that also matched the ranged and reforming panels would need a shorter 1x part (for example the first
  strike from its landing minus a fixed span), which D270's "the first strike at 1x" does not give; not tuned per kind
  (lead-0042 #17632).

Knob-off identity `[C]`: `run_charge_scenes` 51/51 and `run_trigger_scenes` 19/19 (`--effects --geometry`), main's
trigger_scene (4a36d087a) against this branch's, every output file byte-equal (75 + 19), 2026-11-26 14:37-14:45.
Rule A `[C]` (MC-B's go #17897): `run_all.py --only offline-equals-live`, the C: library copy (17 bundles, matching the
master), asset root mods548-vt, branch at bae56f8d4 (main 6d5baae43 with 0753 merged, the test key in): **PASS 18
passed, 0 failed** (797 s), 2026-11-26 16:23-16:51.

Builders and suites at bae56f8d4 / c745d2ca6 (after main 6d5baae43's merge) `[C]`, 2026-11-26 16:02-18:37: server release
on pins-5.5 PASS; units fights, `test_scene_units` 108/108, unit extras 14/14, unitfeed 24 passed, effect routes, trigger
emitter, queue test, extras test PASS; render ctest PASS (0 skipped); every c5render consumer of `compiled_by.py`'s list
links (farm, unit shadow, forest, fow, overlay scene, scene shadow, ice, water, derived, scene bundle, sceneshot
`--smoke`, models, cut tap, decal, exe field, pages, live terrain, render tests); identity again against a main reference
rebuilt from 6d5baae43 in a detached worktree: 51/51 + 19/19, 75 + 19 files byte-equal. ASan (MSVC): `test_scene_units`
108/108, `run_units_fights_test --asan` OK. `test_render_host.py` on the loaded workstation (18:05-18:35, two other teams'
builds): 9 of 147 failed, all timing or throughput (a tap load not seen in time, draws 46 of 75, pacing ratios); rerun
alone (18:55-19:10): this branch 2 failed (`test_feed_drained_every_iteration`, `test_second_server_on_one_section_refused`),
main 6d5baae43's own build 2 failed (`test_recording_draws_with_the_flag_unless_told[0-0]`, `[1-8]`), each of the 9
passing in at least one of the two runs: load flakes, not 0796's (0796 adds one parsed key). The new refusal rows
(`render_test_units_quick_combat` "2", "on") pass. `test_recorder_host.py`: 19/20 in the loaded run
(`test_bundle_from_the_host`: the recording did not finish, "render thread did not stop within 5 s", before the new
assert), 1/1 alone. `trigger_scene --asan` does not compile on main (MSVC C4456 at main's `for(const FigureStep& s ...)`
loops under /WX, known since 5052): built with `CL=-wd4456` in its out folder's `vcvars-env.json` (the builder runs cl
in that cached environment; a `/wd4456` value is turned into a path by MSYS), no source change. Under ASan then
(19:08-19:11): the five video pairs off and V2 (`quick_scenes_0796.py --arms off,V2 --seeds 1 --cases 0`) 10 runs PASS,
0 ASan reports; the unitanim controller and combat tests through `render/build.py --asan --test` 0 ASan reports.

Seen on the way `[C]`: with P 3 the reforming melee's first `Strike` step comes at 0.367 s instead of 1.533 s and the
fight is longer (3.07 s): at a rate r the core takes r steps of 1/30 s per frame on one frame's controller feedback, so
a high run-up rate changes the choreography's waits `[I]`; at P 2 and in the 3x phase every case kept its checks.

### 11.5 What T2-T4 need from T1

- **T2 (0797, the option):** `UnitsOptions::quickCombat = 1` from `[Extras] QuickCombatAnimations = 1` (server.cfg);
  `QuickCombatV2()` holds the values; the test key `render_test_units_quick_combat` is T1's (11.2), so T2 maps the
  player key onto `UnitsOptions::quickCombat` (or onto that key) and rule A keeps answering through it.
- **T3 (0798, sounds):** the trigger stream sends every Sound trigger the scaled clips cross (one record set per real
  frame); a clip at 3x crosses its triggers three times as often. Unit projectiles: a projectile released in the 3x phase
  keeps its real-time flight (lands late by up to 2/3 of its flight or is cut at the owner end); measured 0 such
  releases in the ten cases (single volleys only: the archers' and the city's one volley, the cannon's one shot); a
  fight with a second volley after the first landing would show it (the removed `addFlight` hunk: path duration / the
  fight's rate at launch).
- **T4 (0799, live):** nothing in T1 was seen live. The EXE side (the hold, `EndCombatSim`, the damage numbers, the
  barrier) is reasoned in section 4; the clip cap per frame (`max_dt`) is scaled with the rate, so a 3x fight at
  below 15 fps keeps its clips with its clock.

### 11.6 Not verified (T1)

- No live run (T4). No server.exe fight at a rate (the test key exists, but no game was run with it; the server's path
  is the same rule as trigger_scene's player, built and tested by the units builders).
- Fights with several volleys, air fights, nukes, ship fights at a rate: not among the ten cases.
- The sound at 3x (T3).

Sources: `unit-combat-server.md` (0, 3, 4, 6, 10, 18.15-18.23), `combat-choreography.md` (1, 3, 4, 5, 7, 8),
`offline-scene-harness.md` 35-37, `tools/capture/README.md`, the code files named.
