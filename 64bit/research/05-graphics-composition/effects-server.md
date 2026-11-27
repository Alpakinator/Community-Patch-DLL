# Effects drawn and timed by the server (epic 0042, route S): projectiles (4792)

Ticket 4792 (epic 0042, I3-C, route S by the owner's D210), worker W15 of team-0042 (account B), branch `w15-4792` from
`epic-0042-combat-movement`, 2026-11-20 from 07:11 EST. The Architect's route S note
(`../07-architecture-decisions/architect-2026-11-20-route-s-combat.md`) 2.3 item 1: projectile models and their flight are
the server's from the start; the trail is a stage A effect that the server moves (`effects-formats.md` 8.1). Marks:
`[C]` read by this worker in the EXE's machine code (a read-only headless Ghidra 12.1.4 pass over a scratch copy of the
analysis project of `../06-tooling-and-workflows/reverse-engineering-setup.md` 2, old build 4390913, PE time stamp
0x546CD0A8; addresses and behaviour only, the decompiler text stays in scratch outside git, D83) or checked in this
repository's code and tests (each names the check); `[I]` this file's reading or a choice made without a read. Nothing
of build 26577625 was read (D200, D202). EXE addresses are the old build's, at ImageBase 0x400000.

## 1. How the EXE flies a projectile (static read, 2026-11-20 07:20-08:35 EST) `[C]`

`effects-formats.md` 7 had the trajectory as "the EXE's code, not read". It is read here. Three parts: the combat
simulation's launcher, a record on the 4 MB queue that carries a whole flight, and the projectile system that moves
the effect along it each frame.

### 1.1 The launcher (0x759110, in the combat simulation)

Called when a figure's Transfer trigger fires in a fight. Inputs: the figure, the Transfer's tangent (three floats, in
the figure's local frame), the figure's world matrix, and the start record of the effect that the Transfer's `refid`
row started (the 0x28 record of `effects-formats.md` 8.1: its world matrix, owner key, name hash, scale).

1. **The weapon** (0x5BA1C0): the member's weapon record whose `ID` hash equals the effect's name hash; else the one
   whose `ID` is the hash 0xC89BA0AD; else the first weapon of the figure's index. A weapon record is 0x40 bytes
   (`ArtDefine_UnitMemberCombatWeapons`, read by 0x6EDF90; offsets from the record: + 0x04 `ID` hash, + 0x10
   `ProjectileSpeed` (a value below 0 becomes 2.0), + 0x14 / + 0x18 `ProjectileTurnRateMin` / `Max` (below 0: 3.0 /
   5.0), + 0x1C `HitEffect`, + 0x20 `HitEffectScale` (0 or below, or NULL: the member's own value at + 0x140), + 0x24
   `HitRadius` (below 0: 0), + 0x28 `MissTargetSlopRadius` (below 0: 0), + 0x2C `ProjectileChildEffectScale` (0 or
   below: 0.14), + 0x30 `AreaDamageDelay` (below 0: -1.0), bytes + 0x34 `ContinuousFire`, + 0x35
   `WaitForEffectCompletion`, + 0x36 `TargetGround`, + 0x37 `IsDropped`, + 0x38 / + 0x3C the two tags). A NULL column
   keeps the record's constructor value, which was **not found** (open item 1).
2. **Hit or miss.** The shot is a miss when the target participant needs no more hits (its deaths in flight or done
   reach its planned deaths, or its hits in flight reach its hits owed: the counts at + 0x34 / + 0x2D30 and + 0x2B84 /
   + 0x2B88 of `combat-choreography.md` 5.1); if then the simulation's counter at + 0xC14F4 is above 0, **no projectile
   is launched at all** (the function returns). Otherwise the shot is aimed at the target figure as a hit.
3. **Speed** (`ProjectileSpeed`, in plots per second): the effect definition's `fMovementSpeed` (+ 0xB4 of the
   definition found by the name hash) when it is not 0, else the weapon's + 0x10; at least 0.001. Against a flying
   target (the target figure's flag bit 15) at least the target member's `MoveRate` x 1.1.
4. **The target point** (world units):
   - *Not dropped* (`IsDropped` 0), a target figure: the figure's position (figure + 0x10..0x18), moved in the ground
     plane by a random offset (0x736B20: angle uniform in 0..2 pi, distance uniform in 0..r; the simulation's own LCG
     214013 / 2531011) and set on the terrain height (0x530A10), or on the water level when the figure's flag bit 16
     is set, or offset in 3D (0x65B640) for a flying figure. r = `HitRadius` when above 0, else the target member's
     `TargetRadius` (member combat record + 0x40, default 3.0) for a hit and `MissTargetSlopRadius` + `TargetRadius`
     for a miss; 0 against a flying target.
   - *Not dropped*, no target figure (a participant without one): the participant's centre (+ 0x24 / + 0x28) with r =
     `HitRadius`, else `MissTargetSlopRadius` + 5.0 (0 when the participant's byte + 0x2D41 is set; then a target figure
     is looked up near the shooter instead, 0x4371C0).
   - *Dropped* (`IsDropped` 1, bombs): straight below the figure's path ahead: from the attacker's plot base (+ 0x40 +
     side x 8) plus the figure's x, y, the point `lead` along the figure's heading (sin h, -cos h), lead = altitude x
     `MoveRate` / speed (altitude = the figure's z above the water level; `MoveRate` of the attacker's member through
     0x67CF40), at most 16.0; z = the terrain height there.
   - Then **z += the target member's `TargetHeight`** (+ 0x44, default 0) unless the weapon's `TargetGround` is set (and
     the participant's byte + 0x2D43 is clear).
5. **Flight time** T = |target - launch| / 64 / speed, clamped to 0.001 .. 5.0 s (constants 0x9F23E0 = 1/64, 0x9EC9B8 =
   0.001, 0xA0B0B4 = 5.0). The launch point is the translation of the effect's start matrix: **the bone's world position
   at the moment the Effect trigger of the Transfer's `refid` fired**, not at the Transfer.
6. **The path** is four control points and a kind (1.3). Against a target that is not flying: kind 1 (the record's
   default), points from the arc builder 0x688B70 (1.2) with the launch direction = the Transfer's tangent turned by
   the figure's world matrix (`D3DXVec3TransformNormal`) and normalised. Against a flying target: kind 2 (straight),
   P0 = launch, P3 = target.
7. It posts the flight as one 0x100-byte record of type **0x33** (allocator 0x63C0E0) and, at once, a 0x28 one-shot
   start of the same effect key and name at the path's start (0x611E00 at t = 0), so the effect jumps from the bone to
   the path. It then fills the simulation's hit entry (0x41ABC0: launch, target, speed, flight time, the weapon, the
   target figure, the count of hits this shot carries) whose landing the simulation counts at the arrival.

The 0x33 payload (offsets from the payload, = record + 0x10): + 0x00 the start matrix (16 floats), + 0x40 the owner key
of the effect (16 bytes), + 0x50 the name hash, + 0x54 the scale, + 0x58 the flight time, + 0x5C a byte (1), + 0x60 /
+ 0x64 the simulation's ids of the hit entry, + 0x68 .. + 0x97 the four control points, + 0x98 the kind, + 0x9C a byte
(1; 0 for kind 2). (The 0x60-byte allocator 0x87ED90 also writes type 0x33 on another channel; not read.)

### 1.2 The arc (0x688B70)

Inputs L (launch), T (target), D (launch direction, unit). With V = (T - L) / 2 and Z = (0, 0, 1):

    w = ((D.V)(D.Z) - (V.Z)(D.D)) / ((D.D) - (D.Z)^2)     (denominator |V| when it is below 1e-4)
    Q = L + w D
    a = (Q - L) / 2,  b = (Q - T) / 2
    P0 = L - a,  P1 = L + a,  P2 = T + b,  P3 = T - b

w is the parameter of the vertical line through the chord's midpoint at its closest approach to the launch line, used
along D (the exact arithmetic of the code, 0x688D45-0x688D6C) `[C]`. With kind 1 the path starts at L heading along D
and ends at T heading from Q to T: an arc through about (L / 2 + T / 2 + Q) / 2 at the middle.

### 1.3 The path and its kinds (0x600720; position and tangent at u = 2 t, t = elapsed / T)

The four points P0..P3, two segments, i = floor(u), s = u - i (u = 2 exactly: segment 1 with s = 1). Coefficient tables
at 0xB978E8..0xB97998 `[C, the table bytes]`:

| Kind | Position | Tangent (normalised) | Start / end |
|---|---|---|---|
| 0 (0x751FC0, 0x78EC40) | clamped quadratic B-spline: segment 0 `(1-s)^2 P0 + (2s - 1.5 s^2) P1 + 0.5 s^2 P2`; segment 1 `0.5(1-s)^2 P1 + (0.5 + s - 1.5 s^2) P2 + s^2 P3` | the derivatives `(2s-2, 2-3s, s)` and `(s-1, 1-3s, 2s)` | P0 / P3; not used by the launcher (an exact u = 2 returns P2 there, a quirk) |
| 1 (0x66F520, 0x4BBBF0) | uniform quadratic B-spline, both segments `0.5(1-s)^2 P_i + (0.5 + s - s^2) P_i+1 + 0.5 s^2 P_i+2` | `(s-1, 1-2s, s)` | (P0+P1)/2 = L, (P2+P3)/2 = T |
| 2 (inline) | P0 + normalise(P3 - P0) |P3 - P0| t | normalise(P3 - P0) | P0 / P3 |

### 1.4 The flight each frame and its end

- The projectile system listens to 0x33 (handler 0x49F9D0, registered by 0x7B4170; slot pair (handler, list) at
  `0x1A0E400 + 0x10 + 8 x type` of the machine's block, the 0x34 slot at 0x1A0E5A8 checks the rule) `[C]`. It takes a
  slot of 0x100 bytes (0x469AB0), initialises it (0x7449C0: start time = now minus the last two frame times) and
  0x706200 posts **0x2B** (hand the effect from the trigger's owner key to the key (slot, its sign, + 0x48, + 0x4C)),
  copies the payload into the slot (0x5F20D0, which also stores the inverse of the effect instance's matrix at + 0xB0)
  and, for kind 2 or a cleared + 0x9C byte, posts a **0x35** record (slot, the two ids). `[C]`
- Every frame (0x7454E0, from 0x765200): elapsed = now - start; while elapsed <= T, the position and tangent at
  t = elapsed / T (1.3) give a frame whose second row is the tangent (the model's +y is the flight direction), the others
  from the tangent and the vertical; times the stored + 0xB0 matrix; posted as a **0x29** start-or-move with the slot's
  key, hash and scale: the move of `effects-formats.md` 8.1 (B2) `[C for the row of the tangent and the post; I for
  the exact cross products, which the decompile garbles]`.
- When elapsed > T (0x707840): a **0x36** record (hash, the effect, the slot, the two ids, the last matrix): read as the
  start of the `ProjectileEnd` children at the end transform `[I]`; and, when the + 0x5C byte is set (always, from the
  launcher), a **0x34** record (slot, the two ids): the simulation's landing `[I: its listener at 0x6EC7FC's list]`. The
  slot is marked done. No 0x2A stop is posted: the trail effect ends by its own duration `[C for the absence in
  0x707840]`.

### 1.5 What was not read (open items)

1. *(Answered 2026-11-20 by the capture of 2.4 `[L]`: a NULL `ProjectileSpeed` flies at **2.0 plots per second**;
   replaced "the server takes 2.0 [I] until a capture measures it".)* The weapon record's constructor values for the
   other NULL columns were not read.
2. The exact frame rows of 0x7454E0 beyond the tangent row; how the + 0xB0 inverse interacts with the definition's
   transform (the server takes world = the definition's transform x the path frame) `[I]`.
3. 0x36's handler (children's scale `ProjectileChildEffectScale` [I]) and where `HitEffect` starts (at the landing,
   with `HitEffectScale` [I]).
4. The Transfer row's `speed` attribute: not used by 0x759110 (the tangent is normalised) `[C for this function]`;
   whether another reader uses it is not read.
5. `ProjectileTurnRateMin/Max`: used for the hit entry's + 0x4C (1 / TurnRateMax x speed x 64 / pi / 2, at least
   `HitRadius`), not for the path `[C]`; its meaning (a homing radius) `[I]`.
6. The effect between the Effect trigger and the Transfer (the arrow in the hand): one 0x28 start at the bone, not
   moved with the bone `[I]`.

## 2. As built (render/, `c5r::effects`, 2026-11-20)

Code: `render/include/c5r/effects/projectile.h`, `projectile_model.h`, `render/src/effects/projectile.cpp`,
`projectile_model.cpp` (a globbed module folder, `render/src/effects/CMakeLists.txt`; 4722's emitter keeps its own files).
Written from section 1 only (D83).

- **Weapon rows** (`loadProjectileWeapons`, `pickWeapon`): `ArtDefine_UnitMemberCombatWeapons` with the reader's rules
  of 1.1 item 1; the pick of the EXE (the row whose `ID` is the effect, else `PROJECTILE`, else the first of the index).
  NULL `ProjectileSpeed` takes `kNullSpeed` = 2.0 (`[L]` since 2.4; written as `[I]` first, 1.5 item 1).
- **The flight** (`arcPoints`, `evalPath`, `pathFrame`, `flightTime`): the arc of 1.2, the three kinds of 1.3 with their
  tangents and a velocity in world units per second (the derivative x 2 / T), the frame with the tangent as row 1, the
  time rule. The clamped kind is continuous at its end (P3), where the EXE's exact u = 2 returns P2 (named; the launcher
  never uses that kind).
- **The launch** (`planLaunch`): 1.1 items 3-6 (speed sources and floors, the target point for a hit, a miss, a city, a
  flying target and a dropped bomb, `TargetHeight` / `TargetGround`, the flight time, the arc or the straight path).
  The random offset uses its own generator (`FlightRandom`), not the EXE's LCG draws (0320's P3: not frame-exact).
- **The flights** (`ProjectileSet`): per frame an `EffectStart` at the path's start (once; or none when the trigger route
  of 4793 has started the effect on the bone and the flight takes its handle), then an `EffectMove` with the frame and a
  velocity; at the end the `ProjectileEnd` children (scale `ProjectileChildEffectScale`) and the weapon's `HitEffect`
  start at the end point, and **no stop** is sent (1.4: the EXE posts none); a forced end (the Architect's note 1.5 item
  4) sends `EffectStop` for every flight of the owner in the air and drops its impact.
- **Model modes** (lead-0042 #7986): `EffectOnly` (B, the product default): the VisEffect (model + trail + children) is
  the EXE's to draw, started and moved by the server's records; `ServerModel` (A, off by default, for stage B and the
  offline harness): the set also gives each live flight's model instance, which `appendProjectileDraws` turns into
  material 87 draws of the unit renderer (rigid meshes only, cb0 world rows, `units::ModelCache`; the EXE's own
  projectile shader is SimpleShader, material 71: a named difference until a capture). C (model, no trail) is not built.
- **Records to the emitter (4722, W16 #8010):** `ProjectileRecord` maps 1:1 onto `srveffects` `startEffect` / `moveEffect`
  / `stopEffect` (translation -> pos, `rotationQuaternion(world)` -> orient, velocity, `follows = true`; the children and
  the hit effect as one-shot starts with `follows = false`). The emitter stops every live effect of an owner at its
  OwnerEnd by itself, which is the forced end of the set. The server's adapter (`server/src/effects/`) is written after
  4722 reaches the team branch. W16's form stops a projectile before its impact; the EXE sends no stop at the end
  (1.4): the adapter keeps the EXE's behaviour unless the shell needs the stop (to settle with 4723's executor).
- **Dependencies of B's live run:** the shell's executor (4723 / 4793) and the root cause of 4798's spike crashes
  (4720) before any live run with records; the old-build captures per weapon class against stock come after that, on
  a grant (DIAGNOSTIC, D112).

### 2.1 Tests

`render/tests/effects/test_projectile.cpp` (globbed `render/tests/effects/CMakeLists.txt`; 20 tests): the arc (start,
end, launch direction, the worked w, a vertical launch direction), the three kinds (partition of unity, continuity at
the joint, velocity = finite difference, start / end points, clamping), the frame and its quaternion (round trip over 64
frames), the time rule with its clamps, the launch (hit within `TargetRadius` at `TargetHeight`, misses within slop +
radius, `HitRadius`, `TargetGround`, speed sources and floors, the tangent turned by the figure, a flying target, a city,
a dropped bomb and its 16-unit cap), the flights (start, moves forward with a velocity, the end's child and hit effect
without a stop, the forced end's stop and dropped impact per owner and for all, the trigger-route flight, the model
mode, handles), the weapon rows with every reader rule and `pickWeapon`, the effect's own transform; negatives for every
refusal (paths, launches, flights, tables, a missing model file through `ModelCache`) checking the outputs after the
failure. Install part (`C5R_GAME_DIR`, the pure-VP install of `render_game_dir`): 538 VisEffects loaded from 38 files
(4786's census), the archer's arrow resolved and planned as one material 87 draw of **48 vertices** (0320's count) at
the flight's matrix, all **101** VisEffects with a model resolved to their parts, an unknown and a model-less effect
refused. `render/build.py --test -R c5r_effects` (clang-cl, /W4 /WX): plain 20 of 20 passed, 0 skipped (2026-11-20
08:55 EST); `--asan --mem-cap-mb 4096` (MSVC cl /fsanitize=address): passed, 0 skipped, **0 ASan reports**, peak 258 MB
(09:01 EST) [C: these runs; `render/evidence/0042/4792/test_projectile.{plain,asan}.out.txt`].

### 2.2 Offline on 0557's recorded fights (D137)

Tool `c5r_effects_projscene` (`render/tools/effects/projscene.cpp`) on a `c5r_unitanim_combat_scene --positions` folder:
every projectile hit of the core (hits.csv) -> the striker's member -> its Transfer row (trigger id and ec) -> the Effect
row of its `refid` (the effect name and the moment it started) -> the weapon row (`pickWeapon`) -> `planLaunch` (the
striker's position at the Effect's moment from motion.csv at a stand-in height of 10 units above the flat field `[I]`:
the bone is not posed offline; the target figure's position at the launch; `TargetRadius` / `TargetHeight` / `MoveRate`
of the target's member; a city's plot centre) -> a `ProjectileSet` stepped at 1/30 s to the combat's end, then a forced
end of anything still flying. Driver `render/evidence/0042/4792/proj_scenes.py` (runs 4776's scene driver with
`--positions`, then the tool per case). Run 2026-11-20 08:51-09:00 EST, workstation, `w15-4792` after the team-branch
merge (827fc63d5 + the tool fix), Release, seed 1, the pure-VP install and 4872's database copy:
**`RESULT PASS 19/19`** (`render/evidence/0042/4792/scenes-default.csv`, the flights in `projectiles-default.csv`) [C:
this run]. Pass = both tools exit 0 and every projectile hit became a flight that landed (none at the forced end).

| Case | Recorded (s) | Core end (s) | Flights (class) | Mean flight (s) | Last landing (s) |
|---|---|---|---|---|---|
| s-n1 Trireme vs Galley | 3.01 | 3.83 | 10 (ARROW, the galley's) | 0.098 | 3.30 |
| s-n3 Trireme vs embarked Warrior | 5.87 | 5.80 | 10 (ARROW) | 0.105 | 4.89 |
| s-c1 Swordsman vs city | 2.97 | 3.60 | 1 (METAL: the thrown torch) | 0.289 | 3.23 |
| s-c2 Knight vs city | 3.50 | 5.67 | 1 (METAL: the torch) | 0.287 | 4.37 |
| s-a1 Warrior vs Archer | 8.07 | 5.37 | 2 (ARROW, the archer's answer) | 0.573 | 1.81 |
| s-k1 Swordsman vs Catapult | 6.97 | 4.37 | 1 (BOULDER) | 0.306 | 1.63 |
| s-m2 Longswordsman vs Crossbowman | 5.70 | 4.73 | 3 (ARROW) | 0.575 | 2.09 |
| the other 12 (melee only) | | | 0 | | |

Over all: ARROW 25 flights, mean 0.196 s; BOULDER 1, 0.306 s; METAL 2, 0.288 s; 28 children started
(`PROJ_ARROW_IMPACT` and the torch's and rock's impacts), no `HitEffect` (none of these weapons has one); **no landing
after the core's end** (late 0.000 s in every case). The records of each flight (start, moves along the arc rising to its
apex, the end) are in each case's `projectile_records.txt` (scratch, not committed).

**Named differences found offline:**
1. **The core's hit lands at the Transfer trigger.** `CombatWeapon::flight` is 0 in the combat tables (4775: "the
   caller's"), so the choreography counts a projectile's death or owed hit at its launch, 0.08-0.58 s before the flight
   lands here. The EXE counts it at the arrival (the 0x34 record, 1.4). *(Fixed 2026-11-20, section 2.5: the core now
   lands a placed Transfer hit after its flight; replaced "a hunk ... posted to lead-0042, not made here".)*
2. **The figures stand close in the ship cases** (n1, n3: 10-13 units between launch and target): 4776's approach moves
   the ships' figures into melee reach; the EXE keeps ranged naval figures apart (`unit-combat-server.md` 5.4 names the
   naval approach as unchecked). The flights are therefore short (0.1 s).

**A dropped bomb (0477's AI turn: Heavy Bombers on a submarine).** The core refuses air fights (4777's), so the flight is
computed from the rule alone `[I: inputs]`: the bomber's member `MoveRate` 5.4 (VP's `ART_DEF_UNIT_MEMBER_BOMBER`), bomb
speed 0.9, `IsDropped` and `TargetGround` 1, altitude 90 (`AttackAltitude`'s default, M1 of `unit-combat-server.md`):
lead = min(16, 90 x 5.4 / 0.9 = 540) = **16** units, distance sqrt(90^2 + 16^2) = 91.41, flight 91.41 / 64 / 0.9 =
**1.587 s**, inside 0477's fixed fight of 2.00 s (stock's 20.05 s is the submarine loop, `combat-choreography.md` 13).

### 2.3 Numbers handed on and what checked them (E6)

- Checked against recorded data: the arrow's 48 vertices (0320); the census 538 / 101 (0320, 4786); the 64-unit plot
  (`camera-and-picking.md`, 4776's M2 multiplier 64.0).
- **Checked against stock by the capture of 2.4 `[L]`:** the flight time rule (T = distance / 64 / speed, every flight
  to 4 decimals), the speed sources (`fMovementSpeed` 3.3 over the catapult's NULL row; NULL = 2.0), the path kind 1
  for ground targets, the arc rule of 1.2 (62 control-point sets within 1e-4 units), the flight's scale (the member's
  `Scale`).
- **Still not checked:** the 0.001 / 5 s clamps (no flight near them), the target radii and heights in use (the target
  point is not separable from the random offset in a dump), the straight path against a flying target, the dropped
  bomb's lead, the frame's side and up rows, the forced end.

### 2.4 Capture against stock (c5h-b, 2026-11-20 09:19-09:40 EST) [L]

MC-B's grant (09:17, on lead-0042's START-REQUEST #8038). Instance c5h-b (C:, Goldberg, old build: PE stamp
0x546CD0A8 checked before the launch, profile mods548 with CP / VP / EUI), **stock arm**: the instance's 32-bit DLL as
found (SHA-256 3c2f47da..., the batch-2 instance-aware pin `i0ship-26234371-ia`, a comparison arm older than today's
pins, COMMON 18), no proxy, no server; Combat, Projectile and Particle on as stock; frame cap 30 fps. Save: 0557's
Tiny save copy (SHA-256 b2ceb663..., as `C5H-4792-TINY_0008`, removed after), Quick Combat switched off in game (0557's
`m.py opts off`). Human turn 9. Four attacks from the Lua channel near the capital: an Archer, a Catapult and a Cannon
each on a fresh barbarian Warrior two plots away (`MISSION_RANGE_ATTACK`), and a Swordsman on a barbarian city (0557's
`case ... city`, the thrown torch). After each, one full process dump (`cli.py dump`, 2.1 GB each, on F:, outside git,
D83). Offline, read-only, the dumps were searched for the flight payloads the EXE leaves in its queue buffers and
projectile slots (the effect's name hash, then scale, flight time, the control points and the kind; 1.1 item 7, 1.4)
with scratch scripts (`scan_dump.py`, `check_arc.py` in the worker's scratch folder).

| Attack | Effect (flights found) | Speed from distance / 64 / T | Flight times (s) | Launch-target distance | Scale | Kind |
|---|---|---|---|---|---|---|
| Archer (NULL `ProjectileSpeed`) | `PROJ_ARROW_ARCHER` (12) | **2.0000** every flight | 0.660-1.002 | 84.5-128.2 | 0.14 | 1 |
| Catapult (NULL speed, effect `fMovementSpeed` 3.3) | `CATAPULT_ROCK_PROJECTILE` (2) | **3.3000** | 0.583, 0.656 | 123.2, 138.6 | 0.12 | 1 |
| Swordsman on a city (the torch) | `MELEE_TORCH_PROJECTILE` (11) | **2.0000** | 0.206-0.662 | 26.4-84.7 | 0.14 | 1 |
| Cannon | none | - | - | - | - | - |

- **The time rule and the speed sources hold exactly.** 25 flights, the speed recovered as distance / 64 / T is 2.0000 or
  3.3000 to four decimals; the archer's NULL row flies at 2.0 (1.5 item 1 answered), the catapult takes its effect's
  `fMovementSpeed` over its NULL row (1.1 item 3).
- **The arc rule holds exactly** (`check_arc.py`): from each flight's own launch and end points and its launch direction
  (P1 - P0, up to its sign), 1.2's rule rebuilds all four control points within 1e-4 world units, 62 sets over the
  three effects (the same flight is found more than once: queue buffer and slot copies). For the torch the rule's w is
  negative (its launch direction and Q point backwards), which the rule covers as written.
- **The flight's scale is the firing member's `Scale`** (0.14 archer and swordsman, 0.12 catapult in
  `ArtDefine_UnitMemberInfos`); `ProjectileSpec::scale` takes it (the server's tool now sets it).
- **The cannon fires no projectile**: its trigger file (`FX_Triggers_Cannon.ftsxml`) has no Transfer row; its shot is
  Action-timed with ignite and smoke effects on the gun and the `HitEffect` at the target [C: the file]. So weapon
  classes without a Transfer (the cannon, and the hit-scan weapons) are not 4792's flights; their impact is 4793's
  hit-effect route.
- Launch heights 37-42 above the field near the archers (z of the launch; the target ends 22-29), 20-25 for the torch.
- Not captured: pictures (nothing to see after the flight; the dumps are the measurement), a flying target, a dropped
  bomb, the forced end. The run: claim 09:19, launch 09:26, in game 09:31, attacks 09:31-09:37, stop 09:39 (newest
  autosave settled 12 s, human turn), front end restored and `provision.py verify c5h-b --quick` ok (no errors, no
  changed mutable files), claim released 09:40; no box.

### 2.5 The core counts the hit at the arrival (lead-0042 #8036 / #8037, on top of 4777)

`render/src/unitanim/combat.cpp` `Sim::flightOf` (W14's 4777 function, merged from `w14-4777` first, no second speed
field): besides 4777's cases (an aircraft's weapon, a dropped bomb, a shot at an air figure), **every Transfer hit with
both sides placed** now lands `effects::flightTime(ground distance striker -> target figure or city centre, speed)` after
its launch, speed = `CombatWeapon::speed` (`ProjectileSpeed`) or 2.0 when NULL (2.4 `[L]`), clamped 0.001-5 s. Without
positions the caller's `CombatWeapon::flight` stays (4775's rule; 4777's cases unchanged). Test
`render/tests/effects/test_core_flight.cpp` (2 tests: landings after the flight for NULL and 4.0 speeds, a Death never
before its flight; the 5 s clamp; the unplaced rule unchanged). `render/build.py --test -R
"c5r_unitanim|c5r_units|c5r_effects"`: 41 of 41 passed, 0 skipped (2026-11-20 09:58 EST); `--asan --mem-cap-mb 4096`: 41 of 41, 0 ASan reports, peak 751 MB (10:13 EST) [C: these runs].

0557's 19 fights with positions (`proj_scenes.py`, run 09:59 EST, `scenes-hit-at-arrival.csv`): **RESULT PASS 19/19**;
the 12 melee-only fights unchanged; in the 7 with flights the hits move by the flight:

| Case | Projectile hits | Flight in the core (s) | Core end before -> after (s) | Recorded (s) |
|---|---|---|---|---|
| s-n1 Trireme vs Galley | 10 | 0.027 each | 3.833 -> 3.833 | 3.01 |
| s-n3 Trireme vs embarked Warrior | 10 | 0.027 each | 5.800 -> 5.833 | 5.87 |
| s-c1 Swordsman vs city | 1 | 0.250 | 3.600 -> 3.833 | 2.97 |
| s-c2 Knight vs city | 1 | 0.250 | 5.667 -> 5.900 | 3.50 |
| s-a1 Warrior vs Archer | 2 | 0.567-0.575 | 5.367 -> 5.367 | 8.07 |
| s-k1 Swordsman vs Catapult | 1 | 0.499 | 4.367 -> 4.367 | 6.97 |
| s-m2 Longswordsman vs Crossbowman | 3 | 0.529-0.608 | 4.733 -> 4.733 | 5.70 |

Core / recorded median 1.03 (0.63-1.69), was 1.03 (0.63-1.62); core - recorded median +0.17 s (-2.70 to +2.40), was
(-2.70 to +2.17). Named: (1) the core measures the ground distance between figure roots, so the ships of n1 / n3 (10-13
world units apart in 3D in 2.2, 3.5 on the ground) fly 0.027 s; (2) the catapult (k1) flies at the NULL 2.0 in the core
where the EXE takes the effect's `fMovementSpeed` 3.3 (0.499 s against 2.2's 0.306 s): `loadCombatTables` reads
`ProjectileSpeed` only; the effect's speed is the caller's to set (4774's wiring or the combat tables with the effect
library), open; (3) the city distance is the core's 32-unit stop (4776's `city_reach`), not the torch's aim point.
*(Item (2) closed in server.exe 2026-11-20 by 4728: section 3.2, projectile speeds.)*

## 3. Wired into server.exe (ticket 4728, 2026-11-20)

W25 of team-0042 (account B), branch `w25-4728` from `epic-0042-combat-movement` with `w22-4793` (4793's routes) and
`w20-4726` (4726's adapter) merged in. Closes 4793's four open items (`effects-formats.md` 15.6): the trigger stream of
server.exe creates the effect routes, the server plans each projectile's flight with the core's target, the host gives
the trigger's bone, and the projectile effects' speeds reach the core. Marks as above; `[C]` here = checked in this
repository's code and the tests named in 3.6. Nothing of the EXE or the game's files is committed (D83): the effect
library, the .gr2 skeletons and clips and the trigger files are read from the player's install at run time.

### 3.1 What runs in server.exe

- **`TriggerStream::open`** (`server/src/effects/srv_trigger_stream.cpp`) builds, besides 4722's emitter and sinks: the
  fight geometry (`FightGeometry`, 3.2; always: it is now the emitter's positions), and, when `TriggerStreamConfig::effects`
  (default true) and the host gave its `GameFiles` and provider: the install's effect library (`loadInstallEffects`),
  `EffectNames`, 4793's `EffectRoutes` (the stream's substitutions and scripts, positions = the geometry, the trigger files
  from the runner's controller, the flight planner = the geometry's `plan`) and `AnimTriggerTap::setRoutes`. A library
  that does not load leaves the stream as 4722 built it (sounds, unresolved effect names) and `effectsWhy()` says why;
  UnitsHost logs it LOUD. The stream itself stays off by default (`render_units_triggers`, 4722).
- **UnitsHost** (`server/src/units/srv_units_scene.cpp`, one hunk in the stream's set-up, 0455's seam file): `tc.files =
  &m_env->gf`, `tc.provider = m_provider.get()`, `tc.strings = r->strings()`, and the LOUD line when `routes()` is null.
- **AnimRunner** (`server/src/units/srv_units_anim.{h,cpp}`, three one-line hunks): `strings()` (the .gr2 string
  databases for the bone poser); in `ControllerFightPlayer::start`, before the controller's `combatFigures` copies the
  members' weapons: `tap->fightArt(f, ctl, art, combatTables)`; in `advance`, W22's `fightHits` line becomes
  `tap->fightRun(f.seq, run.result(), run.motion, f.elapsed)` (which calls `fightHits` first).
- **The tap** (`srv_trigger_tap.h`): `fightArt`, `fightRun` and `afterFrame`, hooks the stream sets (unset: no call).

### 3.2 The fight geometry (`server/src/effects/srv_fight_geometry.{h,cpp}`)

- **Figures.** A figure of a running fight stands where the fight's `FightMotion` (4776, with 4726's aircraft height)
  puts it at the fight's clock `f.elapsed` (the step the drawing takes): x, y, yaw; z = max(height field at (x, y),
  water level) + its member's `ZOffset` + the motion's z [C: `placement.h`'s rule]. A figure of a fight without motion (a
  side not placed the frame before) has no figure position: the emitter takes the unit's root, as 4722 did.
- **Bones** (`render/include/c5r/effects/bone_pose.h`, `render/src/effects/bone_pose.cpp`, in `c5render`): the trigger's
  bone (ASCII case ignored; the first skeleton of the member's .gr2 that has it) posed on the figure's pose program of the
  frame, sampled as the unit module's pose step samples (`pose.cpp` `sampleWorld`: the clip's basis brought to the stock
  unit basis, world = parent x local), then placed as the palette rows place a figure: point = s Rz(yaw) p + T,
  orientation = Rz(yaw) R with R's columns normalised, in the quaternion form of 4722's `StartSpec::orient`. A blended
  program is posed with its heavier side at each blend [I], counted. The poser keeps its own `PoseModel` / `ClipCache`
  (the fighting members' only). `figureScale` = the member's `Scale` [I: the hook's intent; the EXE's scale of a trigger
  effect is the live check's].
- **The unit art** of each fight's attacker and defender: at the fight's start the controller's `UnitInput` resolved
  through the runner's `ArtSource` with the owner's `PlayerInput` from `UnitsCore::playerArt` (the controller's rule).
  The interceptor and the area members (4726's `others`) are not resolved: their triggers start at roots [I].
- **The flight planner** (`FightGeometry::plan`, an `EffectRoutes::FlightPlanner`): 4792's `planLaunch` with the core's
  target = the target the striking figure's last `FigureStep` names (`CombatResult::steps`, read as the run grows), at
  its pose now (`TargetKind::Figure`; `FlyingFigure` for an `Air` member); a city target (`target_id` -1) at the fight
  plot's centre (`Participant`); no step target yet: the other side's root, counted `planOpponent`. The weapon row by
  `pickWeapon` (the firing member, the held effect's name); the Transfer's tangent from its trigger row
  (`tangent.x/y/z`; (0, -1, 0) when not written, counted); the firing figure's yaw; `TargetRadius` / `TargetHeight` /
  `MoveRate` of the target's member (`ArtDefine_UnitMemberCombats`); the effect's `fMovementSpeed`; ground heights from
  the frame's grid. **Hit or miss is not known at the launch** (the core picks the victim at the landing, `Sim::land`):
  every flight is planned as a hit [I]. The flight's scale = the firing member's `Scale` (2.4 [L]); the children and the
  hit effect take the target plot's `$(TERRAIN)` (CITY for a city).
- **Projectile speeds** (`TriggerStream::fightArt`; 2.5 item (2)): for each member of the fight's units not seen
  before, the first of its trigger files with a Transfer row goes to 4793's `applyEffectSpeeds` on the runner's
  `CombatArtTables` before `combatFigures` copies the weapons; once per member per stream.

### 3.3 Offline: `trigger_scene --geometry` on 0557's 19 fights

`trigger_scene` (4722's tool) takes `--geometry`: the server's `FightGeometry` replaces the tool's stand-in positions
and planner (the figures on 4776's motion over the flat stand-in field, the bones on the controller's programs with the
install's .gr2, a city's centre from the record's plot); `run_trigger_scenes.py --geometry` passes it and prints a
`geometry:` line per case. A case also fails on a planner refusal, a firing member without art or weapon row, or a
model the poser cannot read. Commands (workstation, through `quietlock.py run`): `build_trigger_scene.py --out DIR
--c5render RBUILD [--asan]`, then `run_trigger_scenes.py --exe ... --game <render_game_dir> --db <4872's db copy>
--work ... --geometry --effects-csv ...`. Results: 3.6.

### 3.4 A repeated frame id is a redraw (the emitter's refusal on the product path)

W24's oldpc run (4778, `unit-combat-server.md` 14.4 on `w24-4778`, ServerUnits 1, `render_units_triggers=1`, a huge
save) logged at least 300 `the trigger stream refused frame N: frame id N not above the last (N)` per arm [L, W24].
**Cause [C, code read 2026-11-20]:** `UnitsHost` steps the controller once per frame the server draws
(`animFrame(t, cam.frameId)` in the scene's prepare), and the server draws one frame id more than once when its feed
changed before the next EXE frame (`AnimFeed::close` counts these `redraws`, `srv_units_anim.cpp`); the tap passes
`AnimFeed::lastFrameId()` to `TriggerEmitter::beginFrame`, which refused any id not above the last. Each such step's
crossed triggers **and its fights' OwnerEnds** were dropped, so a loop owned by a fight that ended in a redraw step could
keep sounding. **Fix (4728):** an id equal to the last is accepted as a redraw, its records stamped with that id
(`vp_trigger.h` R1 allows equal ids; the shell executes them at its next insertion, R2), counted `redraws`; an id below
the last (AnimFeed's "new composite") is stamped with the last id (R1: never decreasing), counted `framesBack`; only a
negative id is refused. Regression case in `server/tests/effects/test_trigger_emitter.cpp`. Not yet seen live.

### 3.5 Not verified, open

- Live: only the smoke of 3.7 (counts, no picture, no shell player of the records: 4727's).
- Hit or miss at the launch (above); the interceptor's and area members' bones; the trigger effect's scale.
- The bone points are checked for plausibility only (3.6: 0-19.6 world units above the figure's origin, mean distance
  0.4-17 per case), not against the drawn palette rows of the same frame; the blend rule (heavier side) is [I].
- In `trigger_scene` the core's combat tables are not given `applyEffectSpeeds` (server.exe's `fightArt` does it): the
  scene's core flies the catapult's rock at 2.0 while the planner flies it at 3.3; the impact and the core's death can
  differ by about 0.2 s there (2.5 item (2)'s numbers).
- Records: with bones a lasting effect on a moving bone sends an EffectMove each frame: 0557's 19 fights went from 355
  EffectMove (roots, W22's stand-ins) to 2,468 (3.6); the ring's budget was not re-checked against it.

### 3.6 Tests (2026-11-20, workstation, `w25-4728`) [C: these runs]

Build trees on C: (`C:/c5-64bit-work/0042/w25/build/rel`, `.../asan`), the install of `render_game_dir` (pure VP 5.4.7
as DLC) and 4872's database copy, every build and run through `quietlock.py run`, one job at a time.

| What | Result |
|---|---|
| `trigger_scene --geometry` on 0557's 19 fights (`run_trigger_scenes.py --geometry`) | **`RESULT PASS 19/19`**; EffectStart 450 (241 following), EffectMove 2,468, EffectStop 241, 0 undefined names; held 28, taken 25, expired 3; **25 flights planned by the server's planner with the core's step target, 0 refused, 2 at a city's centre**, 0 tangents missing; 25 `ProjectileEnd` children; figures posed 5,718 of 5,728 asks (10 without motion); **bones 3,278 asked, 3,278 given** (3,250 starts / moves placed by `boneTransform`), 57 models prepared, 698 blended programs posed by their heavier side, 0 bone names missing, 0 model or clip refusals |
| `trigger_scene --effects` (W22's stand-ins, unchanged) | `RESULT PASS 19/19`, W22's counts (EffectStart 450) |
| `run_effect_routes_test.py`, `run_trigger_emitter_test.py` (with the redraw regression case) | OK (0 failing) |
| `test_scene_units` (build_units_tests.py) | 69 of 69 cases passed |
| `test_unitfeed.py -k "aiturn or special_fights or controller_timeline"` (unitfeed opens the routes as UnitsHost) | 4 passed (0477's AI turn with `--trigger-log`: 2 fights, EffectStart 12, OwnerEnd 210, no position 0) |
| `unitfeed --trigger-log` on the fixture's bundles (the server's AnimRunner + TriggerStream path, D137 rule A) | aiturn (0477's AI turn): 2 fights, 12 effect triggers, 0 refused, arts 4; special (4777's special fights): 4 fights, EffectStart 153, **12 Transfers, 12 flights planned with the core's step target**, 0 refused. Figure poses 0 there: unitfeed's fights run without start positions (no placement before the fight), so no motion and no bones (the server had both live, 3.7) |
| links: `build_units_extras_tests.py`, `build_scenebundle_tests.py` (build only, no rule A run: MC #8300), `build_server.py --config release --offline` | OK (server.exe in 434 s) |

Per case the bone points lie 0.0-19.6 world units above the figure's origin (max distance 24.1; the ships' cases 0.4-1.1
mean, the foot units 11-17 mean): plausible for figures at member scale 0.12-0.14 [I].

**ASan** (MSVC `/fsanitize=address`, 16:22-16:48 EST, the code of the final head): `c5r_effects_test_effect_routes`
passed, 0 ASan reports; `trigger_scene --geometry` ASan build **`RESULT PASS 19/19`**, totals identical to the plain run
(EffectStart 450, EffectMove 2,468, bones 3,278 / 3,278, 25 plans), 0 ASan reports; `run_effect_routes_test --asan` and
`run_trigger_emitter_test --asan` OK (canary caught); `test_scene_units` ASan 69 of 69, 0 reports, peak 1,194 MB;
`test_unitfeed.py` (ASan unitfeed) 4 passed. A second plain pass on the final code: `--geometry` and `--effects` 19/19
again, routes / emitter OK, test_scene_units 69/69, unitfeed 4 passed, server.exe relinked (160 s).

### 3.7 Live smoke (c5h-b, 2026-11-20 16:00-16:18 EST) [L]

Lead-0042's grant #8496 (MC-B 15:51). Instance c5h-b (C:, Goldberg, old build 4390913, profile mods548), arm u1 of
W20's driver (`VP64Options.ini` ServerRenderer = 1, ServerUnits = 1, the product's config.ini) plus the extra key
`render_units_triggers=1`; layout on C: = W20's 4726 layout (x64 DLL b35ad670, proxy 9bd86094, composite 4a2ad069) with
this branch's server.exe; a copy of `C5H-3000-TINY-T0018` (turn 18), Quick Combat off; driver
`server/evidence/0042/4728/run_4728.py`. Cases from the Lua channel near the capital: an Archer and a Catapult each
shooting a barbarian Warrior two plots away, a Swordsman attacking one on the next plot (the city strike did not fire:
`CanRangeStrikeAt` false).
- **s1** (16:00-16:05, server.exe 04985c59): the three fights played (defender damage 34, 41, 92), 0 crash (the EXE's exit
  0x0F is the launcher's stop; no exe-crash record, no server dump), 0 box; the stream's counts were not in the log (the
  totals line printed only at an orderly end, which a launcher stop never reaches).
- **s2** (16:14-16:18, server.exe 0526d825 = `w25-4728` @aa7a45480, with the `units trigger 1s` line): the same fights;
  the last line `fights=3 ss=601 es=144 em=1229 est=112 oe=320 redraws=2 back=0 routes=1 planned=11 bones=1115/1115`:
  the routes open in server.exe, **11 projectile flights planned by the server with the core's target**, every bone asked
  given, 2 redraw frames accepted and **0 "refused frame" lines** (3.4); 0 crash, 0 box. Restore by hash ok and
  `provision.py verify` 0 after each launch; claim 15:57, release 16:20.
- Not checked live: what the shell draws from the records (4727's player is not in this layout), the planner's flight
  times against stock's, a city volley.
