# Community report: melee combat sometimes stuck, one side stays in combat for a long time doing nothing (ticket 0557)

Community report, unconfirmed (the owner, 2026-11-18, decision D187 [reworded for privacy]: to be investigated with caution, as the reports are unverified). Report:
melee combat is sometimes stuck: one side stays in combat for a long time doing nothing.

Timebox: about 1 h of reproduction on Goldberg instances, stock route first, then the product route of the current
pins (batch 4). Work 2026-11-18 from 15:49 EST (first launch 15:56:44), workstation, pool instance `c5h-g` (F:,
profile mods548, mods cp, vp, eui), implementer on account B.

## 1. What the code and the knowledge base say (before any run)

- **The choreography's limits [C, `research/05-graphics-composition/combat-choreography.md` 1, 4.2, 13.4].** A
  visualised combat holds its units' later messages (damage, death, advance, the next combat) until the EXE's combat
  simulation ends. It ends when every figure has reached its end state, or at the time limit: **12 s**, or **20 s
  when an air unit takes part**. The end test of a figure holds every figure in the fight while the attacker or the
  defender has planned deaths not yet played or an owed hit not yet delivered (13.4). The only stuck combat found so
  far (tickets 0456, 0477) was an air strike on a submarine: the submarine's model could not play the anti-air answer
  (1130) the simulation requested, so the owed hit was never delivered and the strike ran to its 20 s limit. A melee
  combat stuck the same way would run to the 12 s limit, with the side that owes the strike standing still: exactly
  the report's "one side stays in combat doing nothing".
- **What a melee attack sends [C, DLL source at the 32-bit pin `i0-ship-batch4` e96457fd5,
  `CvUnitCombat.cpp` `GenerateMeleeCombatInfo` lines 101-280].** Every melee attack on a unit or a city sets
  `DefenderRetaliates = true`. On a city with a garrison, VP gives the garrison a share of the damage and adds it as
  a damage member (a bystander participant, line 131), the only melee case with a participant beyond attacker and
  defender. Other VP melee variants: capture of the defeated unit (`setDefenderCaptured`), heavy charge, no advance
  out of a fortification.
- **In the AI turns** a visible combat is a barrier: every later message waits and the game's update stops until the
  queue drains (combat-choreography.md 12.3). A stuck melee combat there would freeze the AI turn for up to 12 s each.

## 2. Set-up

- Instance `c5h-g` (F:, short-run place), the instance's Steam DX11 EXE, Goldberg wrapper route, frame cap 30, dialog
  watcher on (`hold`). **Stock route:** the 32-bit pin `i0ship-e96457fd-ia` (batch 4) put in with `provision.py set-dll`,
  no proxy layout. Mods cp, vp, eui. The first launch took 12 min to the load screen (15:56:44 -> 16:09, the instance
  on the USB SSD F: with the mods database built on first start).
- Save: a copy `C5H-0557-TINY_0008` of `REL548-TINY_0008` (SHA-256 `b2ceb663...`, Polynesia, Tiny, turn 9 at load,
  loads with Quick Combat and Quick Movement ON [L]). Both switched OFF in game with `Network.SendGameOptions` (the
  message `OptionsMenu.lua`'s OK sends) and read back false (`PreGame.GetQuickCombat()`, the game option, Quick
  Movement).
- Scene: 0553's `setup.lua` (the area of radius 9 round the human capital 51,14 visible; barbarian cities founded at
  47,13, 48,17 and 52,18), 0456's event logger (`RunCombatSim`, `EndCombatSim`, `SerialEventUnitSetDamage`, ...,
  `os.clock` wall seconds).
- Each case (`tools/harness/evidence/0557/m.py case|batch`, `melee.lua`): a fresh human attacker and a fresh defender
  (barbarian, player 63, unless named) on two adjacent free plots near the capital whose other neighbours hold no
  unit, chosen by kind (land, across a river, attacker embarked on the coast, both at sea, a city, a city with a
  garrison); the attack is the interface's move order onto the enemy plot (`GAMEMESSAGE_PUSH_MISSION`,
  `MISSION_MOVE_TO`). Measured: order -> `RunCombatSim` -> `EndCombatSim`, the units' state after (alive, hit
  points, `IsFighting`, `IsBusy`), up to 8 window shots 0.5-15 s after the order (PrintWindow, scale 0.5, Visual
  Checks issue 0557, DIAGNOSTIC; popups closed through Lua before every case, D114). Then the case's units are removed
  (`Kill`) and an attacked city is healed. A case is flagged LONG at 11.5 s or more (the 12 s limit).

## 3. Runs

### 3.1 Stock route, human melee attacks, Quick Combat switched OFF (2026-11-18 16:10-16:23 EST) [L]

Times are Lua wall clock (`os.clock`), on the workstation (indications, COMMON 23). "Order" is the Lua chunk that
sent the move order; hit points are before -> after (a city's are its hit points left -> damage after).

| Case | Kind | Attacker | Defender | Order -> Run (s) | Run -> End (s) | Attacker HP | Defender HP |
|---|---|---|---|---|---|---|---|
| t0 | land | Warrior | Barbarian Warrior | - | 4.03 | 100 -> 81 | 100 -> 67 |
| w1 | land | Warrior | Barbarian Warrior | 0.13 | 4.60 | 100 -> 83 | 100 -> 62 |
| w2 | land | Warrior | Barbarian Warrior (85 damage before) | 0.13 | 3.70 | 100 -> 97, advanced | 15 -> killed |
| s1 | land | Swordsman | Spearman | 0.13 | 4.83 | 110 -> 87 | 100 -> 65 |
| h1 | land | Horseman | Barbarian Warrior | 0.12 | 6.81 | 100 -> 84 | 100 -> 49 |
| r1 | across a river | Spearman | Barbarian Warrior | 0.12 | 4.77 | 100 -> 75 | 100 -> 68 |
| a1 | land | Warrior | Archer (ranged defender) | 0.13 | 8.07 | 100 -> 87 | 100 -> 41 |
| k1 | land | Swordsman | Catapult (siege defender) | 0.13 | 6.97 | 110 -> 108, advanced | 100 -> killed |
| m1 | land | Knight | Pikeman | 0.12 | 6.37 | 100 -> 70 | 100 -> 67 |
| m2 | across a river | Longswordsman | Crossbowman | 0.12 | 5.70 | 110 -> 86 | 100 -> 67 |
| i3 | land | Rifleman | Gatling Gun | 0.12 | 5.57 | 115 -> 91 | 100 -> 56 |
| d1 | land | Infantry | Infantry | 0.12 | 4.03 | 125 -> 96 | 125 -> 95 |
| d2 | land | Tank | Artillery | 0.13 | 5.93 | 100 -> 95, advanced | 100 -> killed |
| d3 | across a river | Mechanized Infantry | Machine Gun | 0.13 | 5.37 | 125 -> 99 | 100 -> 67 |
| e1 | from embarkation | Swordsman (embarked, coast) | Barbarian Warrior | 0.33 | 4.37 | 110 -> 89 | 100 -> 62 |
| e2 | from embarkation | Marine (embarked) | Rifleman | 0.33 | 5.30 | 100 -> 84 | 115 -> 55 |
| n1 | sea | Trireme | Galley | 0.13 | 3.01 | 100 -> 83 | 100 -> 49 |
| n2 | sea | Ironclad | Frigate | 0.12 | 3.74 | 100 -> 89 | 100 -> 24 |
| n3 | sea, on an embarked unit | Trireme | Barbarian Warrior (embarked) | 0.13 | 5.87 | 100 -> 100 | 100 -> 4 |
| c1 | city 47,13 | Swordsman | barbarian city, no garrison | 0.13 | 2.97 | 110 -> 98 | 258 -> damage 70 |
| c2 | city 47,13 | Knight | the same | 0.13 | 3.50 | 100 -> 85 | 258 -> damage 63 |
| c3 | city 47,13 | Tank | the same | 0.13 | 4.63 | 100 -> 100 | 258 -> damage 129 |
| g1 | city 48,17 + garrison | Swordsman | city + Barbarian Warrior | 0.13 | 2.67 | 110 -> 82 | garrison 100 -> 83 |
| g2 | city 48,17 + garrison | Knight | city + Spearman | 0.13 | 3.67 | 100 -> 71 | garrison 100 -> 86 |
| g3 | city 48,17 + garrison | Tank | city + Infantry | 0.13 | 5.10 | 100 -> 81 | garrison 125 -> 100 |

**No melee combat came near the 12 s limit: 25 combats, 2.67-8.07 s** (median 4.77 s). Every one had exactly one
`RunCombatSim` and one `EndCombatSim`; after every one the attacker was free again (`IsFighting`, `IsBusy`,
`IsInCombat` false at about +13-16 s), the human turn stayed active and no box appeared. The longest were the
combats against a defender that strikes back with a different weapon or animation (archer 8.07 s, catapult 6.97 s,
horseman's charge 6.81 s). The garrison cases (the only melee combats with a bystander participant, section 1) were
among the shortest.

Not a stall, a refused order: in cases i1 (Musketman on a Musketman, twice) and i2 (Cavalry on a Cannon) no combat
started at all; the units stayed in place with full moves. A probe (`probe_noattack.lua`) read
`Unit:IsOnlyDefensive()` true for the fresh human Musketman and Cavalry (false for a Swordsman), so the DLL refuses
their attacks; what gives them the only-defensive count in this save (a promotion of the unit class; VP's
resource-deficit rule is a guess) is not determined [I] and is not the report's symptom.

### 3.2 Stock route, AI turn: not reached (2026-11-18 16:26-16:40 EST) [L]

The AI scene (`aiscene.lua`: war with Carthage, player 1; three AI Spearmen next to three human Warriors at 60 damage
near the AI capital, three Barbarian Warriors next to three such Warriors near the human capital, all visible to both
teams) was set up after 3.1, but turn 9 could not be ended: `ENDTURN_BLOCKING_PRODUCTION` (notification 5, "Honolulu
is ready for a new construction project") stayed after 0456's `endturn.lua` (`CONTROL_FORCEENDTURN`), the harness's
`turns.py end` (blocked 2 tries out of 2), a `DismissNotification` and a `Game.CityPushOrder` on the capital, whose
queue already held a unit. Case c3's Tank had captured the barbarian city at 47,13 (`Ghuzz`: occupied, in
resistance, never answered); making it a puppet (`TASK_CREATE_PUPPET`) did not clear the block either. Not a
combat symptom (no combat ran, `IsFighting`/`IsBusy` false on every unit read); the cause of the stuck block was
not looked into further. The AI turn was run on the product route from a fresh load instead (3.3).

### 3.3 Product route, human melee attacks and one AI turn (2026-11-18 17:11-17:39 EST) [L]

The proxy layout of the batch-4 pins `x64-c29442e8-p58724e0` put on c5h-g with `provision.py set-proxy` (proxy
`3DFD7521...` in the mod folder, server.exe and x64 DLL in `VP64`, `vpproxy.ini` mode=on as `provision.py` writes it,
32-bit fallback `i0ship-e96457fd-ia`); both build lines of the run folder `Logs/VP64/run/20261118-170133-41812-1`
read `repo 58724e072 dirty=0 dll_pin c29442e8c`, 0 LOUD lines. `VP64Options.ini` not present on the instance, so
every option at its default (the EXE draws the world and, since D147, plays the combat). Launch 16:41:12, load screen
at 17:09 (28 min on F:, the server's start included), the same save copy loaded with Quick Combat ON, the scene
rebuilt, then switched OFF and read back false.

| Case | Kind | Attacker | Defender | Order -> Run (s) | Run -> End (s) | Attacker HP | Defender HP |
|---|---|---|---|---|---|---|---|
| p-w1 | land | Warrior | Barbarian Warrior | 0.11 | 4.39 | 100 -> 79 (then attacked twice in the AI turn) | 100 -> 65 |
| p-w2 | land | Warrior | Barbarian Warrior (85 damage) | 0.12 | 4.60 | 100 -> 96, advanced | 15 -> killed |
| p-h1 | land | Horseman | Barbarian Warrior | 0.12 | 6.25 | 100 -> 84 | 100 -> 36 |
| p-r1 | across a river | Spearman | Barbarian Warrior | 0.44 | 6.24 | 100 -> 79 | 100 -> 52 |
| p-a1 | land | Warrior | Archer | 0.89 | 7.18 | 100 -> 82 | 100 -> 43 |
| p-k1 | land | Swordsman | Catapult | 3.29 | 7.07 | 110 -> 108, advanced | 100 -> killed |
| p-m1 | land | Knight | Pikeman | 0.16 | **11.84** (see below) | 100 -> 72 | 100 -> 73 |
| p-m1n1..n4 | land | Knight | Pikeman, no window shots | - | 6.37, 5.67, 6.10, 5.81 | 100 -> 69-78 | - |
| p-m1s1, s2 | land | Knight | Pikeman, 8 shots as in p-m1 | - | 5.87, 6.84 | - | - |
| p-d1 | land | Infantry | Infantry | 1.03 | 3.61 | 125 -> 105 | 125 -> 84 |
| p-e1 | from embarkation | Swordsman (embarked) | Barbarian Warrior | 0.32 | 4.33 | 110 -> 86 | 100 -> 62 |
| p-n1 | sea | Trireme | Galley | 0.12 | 3.03 | 100 -> 80 | 100 -> 55 |
| p-n3 | sea, on an embarked unit | Trireme | Barbarian Warrior (embarked) | 0.39 | 5.76 | 100 -> 100 | 100 -> 18 |
| p-c1 | city 47,13 | Swordsman | barbarian city | 0.13 | 3.79 | 110 -> 98 | 258 -> damage 69 |
| p-g1 | city 47,13 + garrison | Swordsman | city + Barbarian Warrior | 0.13 | 3.00 | 110 -> 86 | garrison 100 -> 86 |

**The one long combat, p-m1 (11.84 s), was a capture artefact, not a stall [L for the times, I for the cause].**
Its fifth window shot came 7.35 s after the fourth (+6.99 -> +14.34 s after the order), where the shots normally come
2 s apart; the same Knight-on-Pikeman case repeated six times afterwards, four times without shots and twice with the
same eight shots, ran 5.67-6.84 s (the stock route's m1: 6.37 s). A PrintWindow call that waits behind the game's
window thread for seconds holds that thread's frames back; the combat's clock is advanced by the frame time (combat-
choreography.md 4.2), and 11.84 s is about the case's normal 5-6 s plus the 7.35 s gap, which fits a clock that does
not credit such a long frame in full. That the step is clamped was not read in the code [I]; what would confirm it:
one combat with a deliberate multi-second stall of the window thread and its End - Run. Shots on the product route
often took 1-7 s instead of 0.5 s (p-k1, p-r1, p-c1, p-w2 too, without a long combat); the order -> Run latency was
also higher there (up to 3.29 s, p-k1). A player whose game hitches during a combat would see the same: the combat
pauses with the frame and finishes afterwards.

**The AI turn (turn 9 -> 10) [L].** The end-turn request (`turns.py end`, 17:13, reported blocked by
`ENDTURN_BLOCKING_PRODUCTION` as on the stock route) did not end the turn at once, but the turn ended 0.7 s after
p-w1's combat ended (`ActivePlayerTurnEnd` at 2345.39 s; the human's attack made the game re-evaluate the block [I]).
The AI scene of 3.2 was in place (`aiscene.lua`, Carthage at war). The AI turn played six visible melee combats,
strictly one after another (the barrier of combat-choreography.md 3.4 item 5), each ending by itself:

| Attacker | Defender | Run -> End (s) | Result |
|---|---|---|---|
| Carthage Spearman | human Warrior (60 damage) | 7.20 | warrior killed |
| Carthage Spearman | human Warrior (60 damage) | 6.60 | warrior to 81 damage |
| Carthage unit 1005 (its own) | the same Warrior | 6.33 | warrior killed |
| Barbarian Warrior | p-w1's Warrior | 3.70 | 21 -> 45 damage |
| Barbarian Warrior | p-w1's Warrior | 5.63 | 45 -> 65 damage |
| Barbarian Warrior | human Warrior (60 damage, healed to 50) | 5.50 | 50 -> 69 damage |

The next combat started 0.03-1.3 s after the previous `EndCombatSim`; turn 10 came, the human turn was active again
by the next case (p-w2, 17:2x), no box.

## 4. Result

### 4.1 Could not reproduce [L]

**Not reproduced in 50 visible melee combats (2026-11-18 16:10-17:39 EST, about 1 h 45 min from the first launch, of
which about 40 min were the two loads (12 and 28 min)):** 25 human attacks on the stock route, 19 on the product route (13 cases and
the Knight's 6 repeats), and 6 AI and barbarian attacks in one AI turn on the product route. Kinds covered: land,
across a river, from embarkation, at sea, on an embarked unit, on a city, on a city with a garrison (VP's bystander
member); eras ancient to modern; melee, mounted, gunpowder and armour attackers; melee, ranged, siege and naval
defenders; human and AI attackers. Every combat ended by itself in 2.67-8.07 s, far under the EXE's 12 s limit for a
combat without air units, except one product-route case whose 11.84 s came from a 7 s stall of the window capture
(3.3; six repeats 5.67-6.84 s). No combat had a side that stayed still: the frames (Visual Checks, issue 0557)
show both sides fighting, then the units standing apart. Every unit was free again afterwards (`IsFighting`,
`IsBusy`, `IsInCombat` false), the turn could end (turn 9 -> 10 on the product route), and no box, assert or crash
appeared on either route.

What would make a melee combat look stuck, by the code and the knowledge base [I, not seen here]:
- a combat that runs to the 12 s limit because one participant owes a strike its model cannot play, the mechanism of
  the submarine (0477, combat-choreography.md 13); no melee pairing tried here does that, but the 364 member rows of
  VP's database were not all tried;
- a frame stall during the combat (3.3): the combat pauses with the game's frames, which a player would read as a
  side doing nothing; a heavy late-game AI turn with many visible combats under the barrier (combat-choreography.md
  12.3) makes each such pause part of the AI turn.

Not tried in the timebox, each a possible condition of the original report [I]: a late-game save with many units and
long AI turns; mods other than cp, vp, eui (unit packs bring their own models and state machines, the class of
defect 0477 found); great generals or other stacked civilians on the defender's plot; VP's withdrawal and heavy
charge (`IsCanHeavyCharge`, a Knight's push) against a blocked retreat; multiplayer; the server's world renderer
(`ServerRenderer = 1`, not the default).

### 4.2 Side observations

- **Musketman and Cavalry orders refused** (3.1): `Unit:IsOnlyDefensive()` true for fresh human units of both types in
  this save; the move order onto the enemy started no combat. Cause not determined [I]; not the report's symptom.
- **A stuck end-turn block** (3.2, 3.3): after a load of `REL548-TINY_0008`, `ENDTURN_BLOCKING_PRODUCTION`
  (Honolulu) stayed through `endturn.lua`, `turns.py end`, `DismissNotification` and `Game.CityPushOrder` with a unit
  already in the queue; on the product route the turn then ended right after the next human combat. The DLL's
  expiry rule clears a production notification when the city's queue is not empty (`CvNotifications.cpp` around line
  1655, pin e96457fd5) [C], so the block outlived the queue entry until something made the game re-check it [I].
  A harness snag for drivers that end turns on this save, not a combat defect.

### 4.3 Evidence

- Drivers and scene: `tools/harness/evidence/0557/` (`m.py`, `melee.lua`, `after.lua`, `aiscene.lua`,
  `probe_noattack.lua`; 0553's `n.py` and `setup.lua`, 0456's `logger.lua`, `logread.lua` and `endturn.lua` reused).
  Per-case records: `tools/harness/evidence/0557/cases/case-<label>.json` (`s-*` stock, `p-*` product; the AI turn's
  six combats are in `case-p-w1.json`).
- Pictures: Visual Checks, issue 0557, all DIAGNOSTIC (window shots by PrintWindow at half scale, the stock and the
  product routes without the server's world renderer; popups closed through Lua before every case, D114).
