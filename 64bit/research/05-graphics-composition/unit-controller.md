# The unit animation controller (U5, layer L4): input, file form, runtime, oracle (epic 0374)

Epic 0374 (`docs/briefs/epic-0374-units-controller.md`, issue `tracker/issues/0374-units-u5-controller.md`), account B,
lead-0374. Started 2026-11-15 20:42. This file is the epic's C1 note and its running record. Sections 6, 7 and 8 are
written by the workers of 4408, 4407 and 4405; the lead writes the others. Status: **C1 accepted by the MC 2026-11-15 (#2000); complete at the MERGE-REQUEST 2026-11-16, open items in 16** (was: "draft, before the C1 review").

Marks: `[C]` confirmed by the files, code or a dump; `[L]` verified live or on captures; `[I]` inferred.

## 0. In short

The controller is layer L4 of the Architect's increment 3 sheet (3.2): from a timeline of each unit's states and
events it decides, frame by frame and per figure, which clip plays at which clip time, which blends run with which
weights and track masks, and which timed triggers fire. It re-implements the EXE's own animation logic from the
player's files at run time (state machines `.fsmxml`, blend graphs `.dge`, timed triggers `.ftsxml`, the `.fxsxml` ec
bindings; D66, D83): nothing hand-written per unit class.

- **Input** (1, 2): per frame a time step and the unit events that arrived since the last frame, in feed order, in
  renderer terms (created, fortify, move start / turn / arrive, action requested, damage, destroyed, embark, garrison,
  visibility, reset). File form `.c5anim` format 1 (2), filled offline from recordings and by U6 from the feed (3) with
  no change.
- **Output** (4): per figure a small **pose program** (sample clip `c` at `t`; blend `a` and `b` with weight `w` and an
  optional track mask), which the unit module evaluates through an additive blended-pose entry in `pose.h`; for a
  figure with one source it is exactly 0041's `(clip, t)` and is written into `.c5units` format 1 unchanged. Plus the
  trigger events of the frame (Action, Sound, Effect, Transfer) with their times; nobody consumes them in this epic.
- **Where** (5): the file readers in `models/` (the format library), the runtime in `c5r::unitanim`
  (`render/src/unitanim/`), the offline driver `render/tools/unitanim/`. The controller reads no EXE memory.
- **Proof** (10): the EXE's own choices recorded live with RenderDoc in scripted sequences (4405), the recordings
  already made (4407), 0041's `oracle.py` unchanged; one command, no game instance.

## 1. The controller's input: per unit, a timeline of states and events

The controller's world is a list of units keyed by `(player, id)` (contract premise F3), each with its art (the same
`UnitInput` fields 0041's module takes: `unit-renderer.md` 2) and an animation state per figure. It advances only when
the caller gives it a **frame**: a time step `dt` in seconds and the **events** that arrived since the previous frame,
in the order they arrived (feed order; `unit-feed-contract.md` 6). Within a frame the controller applies the events
first, in order, then advances every unit's clocks by the step rule (9, K6), then evaluates the graphs. So an event's
time is the frame it arrives in; nothing in the input carries a wall-clock time of its own. The recorded frames'
steps (Present time differences) are the offline driver's `dt`; U6 supplies the server frame's step (decision 8).

Events (renderer terms; the per-unit fields are those of `UnitInput`):

| Event | Arguments | What the controller does (the bridge, scope 3; the exact map is 4404's, from 6.5) |
|---|---|---|
| `create` | the unit's `UnitInput` fields (art row or type and owner, flags, damage, facing) | binds art, members, state machines, graphs, triggers; every figure enters the machine's default state at clip time 0 (or the fortified / embarked state when the flags say so) |
| `remove` | none | drops the unit at once (no death clip: a unit leaving the scene, e.g. a teleport out of view) |
| `fortify` | on / off | requests the fortify action (1500 family) or its exit |
| `move` | facing, path length in plots | requests the run action (1400 family: start, run) |
| `turn` | new facing, left / right | requests the run turn (1460 / 1480) where the machine has it |
| `arrive` | final facing | requests the run stop (1440 / 2440) |
| `place` | plot, facing | a teleport or rebase: state reset to idle at the new place, no clip of its own |
| `action` | kind: `attack`, `attack_city`, `bombard`, `charge`, `attack_charge`, `victory`, `bombard_defend`, `work <build>`, `captured`, `activate`, `paradrop`; optional figure set | requests the action's ec on path 4 for the named figures (all by default); 0042 decides when and for whom in a combat |
| `damage` | damage, previous damage | figures that the 0x4B4B80 rule (`unit-formations.md` 7) takes out leave the drawn set with no clip outside combat (X4 as corrected 2026-11-16, MC #2147; was "play the death action"); others continue |
| `kill` | figure set | the named figures play the death action (0042's combat choreography names them) |
| `destroyed` | in combat 0 / 1 | every living figure plays the death action, then the unit is kept dead (dead idle 1220 / 2220) until `remove` |
| `embark` | on / off | the unit's art switches to the embarked art (as 0041's flags do); its figures restart in the default state |
| `garrison` | on / off | hidden / shown; clocks keep running [I] |
| `visibility` | visible, blend time | the fade of palette header row 1 over the blend time (0041's P3: (0, 1, 1) fading in) |
| `reset` | none | `GameplayUnitResetAnimationState`: every figure back to the default state at time 0 [I] |
| `choice` | figure set, decision point, value | **forced-choice mode only**: the next random decision at that point on those figures takes this value (decision 5). A decision point is `roll:<socket>` (a `RandomRoll` condition: value 1 fires, 0 does not) or `option:<socket>` (an `EventRandomPathOption`: the chosen value), sockets named by machine file and socket index |

| `steer` | figure set, path-7 weight 0..1, path-8 selector | **added 2026-11-15 21:40 (MC #2017, from 4408's 6.3):** sets the two path options only the EXE writes: 8 selects left / centre / right (run with turn 1460 / 1480, broadside side, aim clips 1700 / 1705 / 1710), 7 is a continuous blend weight (how far into the turn or aim). Producers: 0042 (turning) and the aim target; in this epic the oracle fills it from captures |

**Path 4 is a level** (4408, 6.2): the bridge writes the request code and the machine itself clears it (`S4=0`) when a
one-shot action or a run starts; A/B variants (2100, 2200, ...) are drawn by the machines and never requested; an attack
request on an idle unit plays "enter combat ready" first. So `fortify` on / off writes 1500 / 1000, `move` 1400 (or
1120 / 1450 by 0042's choice), `arrive` 1000, `destroyed` 1200, `reset` 1000, `action` the code of 6.5.

A **figure set** is `*` (all living figures) or a comma list of figure indexes in member order (0041 P8). Figures of a
unit share one state and one clock unless an event names a subset or a random choice separates them (X3).

## 2. The input's file form

`.c5anim`, format 1: UTF-8 text in the manner of `.c5units` (`unit-renderer.md` 3): one record per line, fields
separated by one tab, `#` comment lines, blank lines ignored, LF or CRLF. Strict: an unknown record kind, a wrong field
count, a number that does not parse completely, a value out of range, an event for a unit not created (or created
twice), a `frame` whose sequence number does not increase, or a missing `c5anim 1` header is refused with the line
number and a named code (`BadHeader`, `BadRecord`, `BadValue`, `UnknownUnit`, `Duplicate`, `OutOfOrder`, `TooLarge`),
and nothing of the file is used. Limits as `.c5units` (64 players, 4,096 units, 64 figures per unit, 4,096-byte
lines) plus 10^6 frames.

```
c5anim	1
scene	<.c5units path relative to this file, or ->	<seed u64>	<mode free|forced>
frame	<seq u64>	<dt seconds>	<present_time seconds or ->	<exe_frame or ->
ev	<player>	<id>	<kind>	<args ...>
```

- `scene` names the starting scene: its `unit` lines become `create` events before the first frame (a recorded
  bundle's unit table at load; the `figure` lines, if any, set the starting clip and t of a figure, which is how a
  recorded sequence that starts mid-idle is given its start). `seed` seeds the random source; `mode forced` makes
  every random decision without a `choice` record a failure (`ForcedChoiceMissing`) rather than a draw.
- `frame` closes the events above it into one frame; `seq` is the feed's sequence number of the frame's last record
  (or a counter offline); `present_time` and `exe_frame` are carried to the output for the oracle.
- `ev` records use the kinds of section 1 with their arguments in the table's order; `create`'s arguments are the
  `.c5units` `unit` line's fields after `unit`. Floats `%.9g`.
- The writer and the reader are one source file (`timeline_file.cpp`); a positive round-trip test and a negative test
  per refusal code (DoD G, I).

U6 writes this file from a bundle's `feed.bin` (records 1 and 3-5) by the mapping of section 3 and replays it through
the same reader: D137 rule A applies at U6, and this epic's driver reads the same file.

## 3. The mapping from the unit feed contract

Written into `unit-feed-contract.md` as its section 8b. Kinds are those of contract 2.1, flags of 2.2, records of 3.3.

| Feed | Event | Notes |
|---|---|---|
| unit table (record 3) at load | `create` per unit (the `scene` line offline) | flags bits as `UnitInput.flags` |
| 11 `kUnitCreated` | `create` | fields from the UnitRecord |
| 12 `kUnitMoved` + path record (4) | `move` (facing from the record, length from the path) | `turn` events: 0042's (C2, where the walk turns); U5 offers them |
| `kKindUnitPosition` with bit 24 (arrival) | `arrive` | mode `exe`; in mode `own` 0042 makes it at the path's end |
| 13 `kUnitTeleported`, 30 `kUnitRebased` | `place` | |
| 14 `kUnitDestroyed` | `destroyed 0`, then `remove` after the queued visuals | the queue is 0042's (C2) |
| 15 `kUnitDestroyedInCombat` | `destroyed 1` | after its combat plays (combat-choreography.md 8) |
| 16 `kUnitSetDamage` | `damage` | in a combat 0042 turns it into `kill` of the figures it chooses |
| 17 `kUnitFortify` | `fortify` | |
| 18 `kUnitWork` | `action work <build>` | the ec per build is 4404's (6.5) |
| 19 `kUnitParadrop` | `action paradrop` | [I] |
| 20 `kUnitActivate` | `action activate` | [I]: 1990 build / activate for great people (6.5) |
| 21 `kUnitEmbark` | `embark` | |
| 22 `kUnitGarrison` | `garrison` | |
| 23 `kUnitShouldDimFlag` | none | the flag is the EXE's |
| 24 `kUnitVisibility` | `visibility` (blendTime from the float bits) | |
| 25 `kUnitCombat` + combat record (5) | `action attack` / `attack_city` / `bombard` / `charge` per side, `kill`, `victory` | **0042's** choreography (C3) produces these; U5 only accepts them |
| 29 `kUnitMissionEnd` | none [I] | to be read by 4404 against the EXE's behaviour |
| 31 `kUnitResetAnimationState` | `reset` | |
| city kinds 4-6, 26-28 | none | cities are 0037's |
| server frame / `kKindFrameMark` | `frame` (`dt` = U6's step; `exe_frame` from the mark) | decision 8 |

## 4. The output: per figure, through `PoseSource`, and the trigger events

Per unit and frame: the unit's `UnitInput` as 0041 takes it (flags and fade updated by the events), and per living
figure a **pose program**: an ordered list of nodes, each `sample(clip, ec, t)` or `blend(a, b, w, mask)` with `a`,
`b` earlier nodes, the last node the figure's pose. A blend is per bone: positions lerped, orientations nlerped
(hemisphere-aligned), scale-shear lerped, as `blendfit.py` does; a track mask gives a per-bone weight (bones outside
the mask keep `a`). This is the Granny DAG flattened to what the frame needs: Selection nodes are resolved by the
controller (only the chosen input survives), BlockCurve and ScalarCallback nodes become the numbers `w`, so the pose
step never sees the graph. A figure with one `sample` is exactly 0041's `FigureInput (clip, t)`.

- **To 0041's module:** `ModulePose` gets one additive entry in `pose.h`, `rowsProgram(const PoseRequest&, const
  PoseProgram&, out)`, which samples each source through the clip cache, blends local poses and then runs 0041's
  world, skinning and palette steps unchanged. The controller provides a `PoseSource` (`ControllerPose`) whose
  `rows()` looks up the figure's program by `(req.unit->player, req.unit->id, req.figure)` and calls it; `units.h` and
  `PoseRequest` do not change. Existing behaviour and 0041's tests unchanged (DoD E, G).
- **To files:** frames where every figure has one source are written as `.c5units` format 1 (`figure` lines); the
  driver also writes a per-frame CSV of the programs (`frame, player, id, figure, node, kind, clip, ec, t, a, b, w,
  mask`) that the oracle compares (10).
- **Triggers:** per frame a list `(player, id, figure, kind Sound|Effect|Transfer|Action, ec, trigger id, time in the
  clip, frame time offset, name or effect reference, bone)`. Action triggers are the hit moments 0042 will wait on;
  the others are emitted with no consumer (decision 10).
- **Per unit status** (5): a unit whose state the controller cannot advance gets a named status and no program; 0041's
  rule then applies (not drawn).

## 5. The module's layout, failure statuses and the pre-check

- `models/include/c5models/logic.h`, `models/src/logic_*.cpp` (4402): readers of `.fsmxml` (states, sockets,
  conditions, path options, weights), `.ftsxml` (event tracks, triggers), the `.fxsxml` ec bindings and `<BoneUsage>`
  (extending `parse_fxsxml` additively), and the `.dge` DAG decode on top of the `.gr2` reader (nodes, edges, node data
  variants, sources by ec, track masks). Plain data, no runtime; strict with named errors; a canonical text dump per
  file (`models/tools/logicdump/`) for DoD B's field-by-field comparison with 3100's Python reader.
- `render/include/c5r/unitanim/` and `render/src/unitanim/` (namespace `c5r::unitanim`, linked into `c5render` as
  `src/units/` is, with `c5r_models`): `timeline.h` / `timeline_file.cpp` (2), `logic_cache.*` (the parsed files per
  model, loaded once through `gamefs`), `machine.*` (the state machine interpreter), `graph.*` (the DAG evaluator to
  a pose program), `clock.*` (K6 rule), `random.*` (seeded per unit, forced mode), `bridge.*` (events to path-4
  requests, figure deaths; 4404), `controller.h` / `controller.cpp` (the entry: `open`, `apply(frame)`, `programs()`,
  `triggers()`, `precheck()`), `controller_pose.*` (the `PoseSource`). Tests in `render/tests/unitanim/`, the driver in
  `render/tools/unitanim/`.
- **Statuses** (`AnimStatus`, per unit, the first that applies, with a text naming the file, state, node or record):
  `Ok`, `MachineNotFound`, `GraphNotFound`, `TriggersNotFound` (a file a member names that neither install nor mods
  hold), `BadLogicFile` (a reader refusal), `UnknownCondition`, `UnknownNode`, `UnknownTrigger` (a type the controller
  does not implement), `EcNotBound` (an ec a state, graph source or trigger names that the member's `.fxsxml` lacks),
  `BadSocket` (a socket to an unknown state), `NoProgress` (a state whose graph has no source that advances time and
  no socket out), `ForcedChoiceMissing` (forced mode), `BadEvent` (an event the unit's machine cannot take, such as an
  action its member's `EnableActions` does not list [I: or the EXE ignores it; 4404 reads which]). *(2026-11-16, W-4470
  [C, the EXE's `GameplayUnitFortify` handler 0x619610 read; `unit-server-scene.md` 6.1]: for `fortify 0` the EXE does
  not refuse: it posts the action named `IDLE` (on: `FORTIFY`) for every unit with no test of its type or machine, and
  the DLL sends `fortify 0` at every unit's creation (`CvUnit::setupGraphical`). So the `BadEvent` the controller gives a
  civilian machine that never tests path 4 = 1000 fails units the EXE draws; the EXE's machine reads no socket on that
  value [I: what the EXE's controller does with a value no socket tests is not read].)* *(2026-11-16, issue 0439 [C for
  the code]: corrected and changed. `civilian_state_01` does read 1000: with a not-equal test (`31 -> 30` and `32 -> 30`
  on `P4!=1000`), which the controller's set of read values did not hold (method 1 only). The set now holds method 1 and
  method 6 values, and a request whose value the figure's machine never reads is a **no-op** (not written, counted
  `ApplyCounts::unread_requests`) instead of `BadEvent`: the EXE's handlers post with no test of the machine [C] and no
  socket reads the value, so the unit keeps its state [I: whether the EXE writes the value into path 4 anyway, which
  could change a not-equal test of the current value, is not read]. Civilians' `fortify 0` now writes 1000, which an
  idle civilian already holds.)* Timeline file
  refusals are file-level (2). No status substitutes a default clip or a partial pose (D118, D125, R6/D132).
- **Pre-check** (DoD F): 0041's `precheck` is extended, through the controller, to bind every state machine, graph and
  trigger file every reachable art row uses and to run the static checks above (unknown types, unbound ec, bad
  sockets, states without progress) without a timeline, one status per unit type, for U6 to call at game start.
- **A clip the art file names but no package ships (MC ruling 2026-11-16, recorded by issue 4460 of epic 0375).** The
  pre-check reports it as the warning `ClipMissingInInstall` on an Ok row and the unit is drawn (`unit-renderer.md`
  10.6; VP's one case: the Assyrian siege tower's `U_Assyrian_Siege_Tower_Charge_Attack.gr2`). At run time a request
  for that clip invents no default: the controller keeps the unit's current state (its current clip and time go on) and
  logs one LOUD line per occurrence. **[I]** until a capture shows what the EXE does when that clip is asked for (the MC
  added a capture of the siege tower's charge attack to 0395). Not implemented in `render/src/unitanim/` by 4460 (not
  its file; the controller is not yet wired into the server's scene): ~~owed to the controller's wiring (0375 child 4456)~~
  **owed by 0042** (corrected 2026-11-16 by W-4470: the MC's answer to 4456's ASK 2, `unit-server-scene.md` 6.1; 0375's
  mapping never requests the clip, only `action attack_charge` of 0042's combat choreography does; graphs bind lazily, so
  today such a request fails the unit with `EcNotBound`; was "owed to the controller's wiring (0375 child 4456)") with a
  test that asks for the missing clip and checks the kept state and the one LOUD line.

## 6. The logic files as read

Worker 4408 (account B), 2026-11-15 20:51-21:25 EST. Offline, read only. **Inputs:** c5h-c's install
(`%USERPROFILE%/c5-instances/c5h-c/install`, the 147 `.fpk` packs and the loose `Assets/`) and its MODS folder (Community
Patch, Vox Populi, VP - EUI Compatibility Files); the database copy `F:/c5-64bit-work/0327/db/Civ5DebugDatabase.db`
(c5h-c's, mods applied, SHA-256 `27fc81c6...19b4`, opened immutable). **Look-up:** a name's copy is the one
`render/src/gamefs` `GameFiles` returns (`fpkscan dups` over the install with the MODS folder as the extra loose root,
render build `2c41569e31`); names with one copy take it. **Tools:** `research/05-graphics-composition/evidence/0374/4408/`
(`census.py` and `vfs.py` read the files; `an1.py`, `fsmcheck.py`, `dgean.py`, `hints.py`, `trigan.py`, `actan.py`
summarise; the `*.out.txt` files are their outputs, numbers and identifiers only; `run_dups.sh`, `run_census.sh` the
commands). The full parse (`census.json`, which holds names and attribute values from the files) stays on F: at
`F:/c5-64bit-work/0374/4408/census/` and is not committed (D83). Marks: `[C]` read from the files, `[I]` inferred,
`[L]` needs or has a live capture (4405).

Notation used below: `P<k>==v` / `P<k>!=v` a `CheckPathOption` test of path option k (method 1 / method 6),
`S<k>=v` an `EventSetPathOption`, `R<k>` an `EventRandomPathOption`, `EC(c)` an `ECComplete` on code c, `T>=s` a
`CheckStateTime`, `RR(p)` a `RandomRoll`, `|` an `OrOperator`. State ids are the files' ids; the words describing a
state are ours.

### 6.1 Which files the unit art reaches [C]

Chain: every `ArtDefine_UnitInfoMemberInfos` row (406) -> its member (366 distinct members, all with an
`ArtDefine_UnitMemberInfos` row; 3 without an `ArtDefine_UnitMemberCombats` row) -> `Model` (a path in VP's rows: the
**base name** is looked up, as for every other name) -> `.fxsxml` -> its `<StateMachine>`, `<AnimGraph>`s,
`<TimedTrigger>`s and `<Animation>` files; then every state's `asset` graph of every reached machine. This takes every
art row the database holds (0041 C1 decision 7: what the game can reach), not only the rows a `Units` row names.

| Kind | Reached | Where the active copy is | Several copies | Copies that differ |
|---|---|---|---|---|
| `.fxsxml` | 357 | UnitModels 154, Expansion2Models 89, VP 69, Expansion1Models 20, ConquestDeluxe 9, Upgrade1 6, 6 DLC packs 10 | 53 | 8 |
| `.fsmxml` | 15 | UnitModels 13, Expansion2Models 1 (`engineer_state`), VP 1 (`edited_modern_ship_aim_state_01`); UnitModels' `ship_transport_state` is not reached | 0 | 0 |
| `.dge` | 112 | UnitModels 90, VP 21 (`edited_aim_ms_*`), Expansion2Models 1 | 0 | 0 |
| `.ftsxml` | 268 | UnitModels 135, Expansion2Models 83, Expansion1Models 20, VP 10, others 20 | 48 | 1 |
| animation `.gr2` (names only) | 6,402 | | | |

- **Duplicates that matter** (two copies with different bytes, the game's choice in brackets): `swordsman`,
  `swordsman_v2`, `swordsman_v3`, `barbarian_swordsman`, `barbarian_swordsman_v2`, `barbarian_swordsman_v3` `.fxsxml`
  and `fx_triggers_swordsman.ftsxml` (Upgrade1Models over UnitModels: the same pair 0041 found for the swordsman's
  `.gr2`, `unit-renderer.md` 13.6 P9), `confederate_rifleman.fxsxml` and `union_ironclad.fxsxml` (Expansion2Models
  over Expansion1Models). Every other duplicate of these four kinds is byte-identical, and **no `.fsmxml` or `.dge` has a
  second copy**, so the pack order decides nothing in the logic files beyond these nine. "Loose beats pack" never comes
  into play for them: no reached logic file exists loose in the install, and the VP copies have no pack twin.
- **VP's files** are all imported into the game's file system (`import="1"` in VP's `.modinfo` for all 21 `.dge`, 1
  `.fsmxml`, 18 `.ftsxml` and 220 `.fxsxml` it carries) [C].
- **Outside the format, 1 file:** VP's `edited_fx_triggers_missilecruiser.ftsxml` is not well-formed XML: a comment is
  opened inside an unclosed comment (a `<!--` holding a second `<!--`, closed by one `-->`). Read with "a comment ends
  at the first `-->`" it parses and the commented block (one attack variant's triggers) drops out. A strict XML reader
  refuses it; what the EXE's parser does with it is not known [I: it loads the model in the game]. **For 4402 and DoD
  F:** refusing this file would refuse VP's missile cruiser (4 members of `edited_modern_ship_aim_state_01`).
- **Missing names:** one animation file (`u_assyrian_siege_tower_charge_attack.gr2`, as 3104 found), no logic file.
- **ec a graph names that the model lacks** (6 models): the four spaceship parts (`ss_booster`, `ss_capsule`,
  `ss_engine`, `ss_stasis_chamber`, machine `aim_civilian_state_01`) through two graphs whose sources hold **ec 0**
  (`aim_civilian_enter_bombard_defend.dge`, `aim_enter_sleep_civilian.dge`, 3 sources each); `dopp_a` lacks 1020 / 2020
  (the idle swap clips of `transitions_for_idles.dge`, reached from idle); `u_huns_battering_ram` lacks 1460 / 1480 (it
  lists `mounted_running_loop.dge`, which its machine `1_omni_state_01` never uses). **For DoD F:** a rule that refuses
  a unit for any unresolved source refuses these six at load; the EXE draws them. Either the status is raised only
  when a missing source is evaluated, or such a source yields no pose input; which one is open [L].
- **Track mask with no bone data** (4 VP models, 4410, 13.3): VP's destroyer (`Wickes`), dreadnought, fleet destroyer
  (`Z_Class`) and `Yamato` (machine `edited_modern_ship_aim_state_01`) name the aim layer's track mask in their graphs,
  but no bone of their models carries mask data. The pre-check refuses them (`TrackMaskNotBound`); what the EXE does
  is open [L].
  Correction 2026-11-15 23:51 (4406 round 2, 15.2): their first refusal is `BadLogicFile` (`edited_aim_ms_idle_graph.dge:
  pose edge to node -1`, the graph refused at bind time), before the mask check; they stay refused (D118). (Replaced:
  "The pre-check refuses them (`TrackMaskNotBound`)" as their first status.)
  Correction 2026-11-20 (0617, 13.3.1): stock binds such a mask as no mask and blends unmasked [C]; the controller now
  does the same and the four ships are drawn in every state. (Replaced: "what the EXE does is open [L]".)
- **Every reached `.fxsxml` names a state machine** (357 of 357); **each model names exactly the graphs its machine
  uses** except the ram (one extra). Members per machine: `1_omni_state_01` 124, `civilian_state_01` 95,
  `1_omni_mounted_state_01` 39, `ships_state_01` 38, `roman_legion_state_01` 14, `airplane_state_01` 11,
  `mechanized_aim_state_01` 11, `great_general_state_01` 7, `modern_ship_aim_state_01` 7, `worker_state` 6,
  `aim_civilian_state_01` 4, `edited_modern_ship_aim_state_01` 4, `missle_state_01` 3, `work_boat_state_01` 2,
  `engineer_state` 1 (366).

**Counts of the reached files, for DoD B** (`an1.out.txt`, `dge_census.out.txt`, `triggers.out.txt`):

| What | Count |
|---|---|
| machines / states / sockets | 15 / 299 / 716 |
| conditions | EventSetPathOption 1,840; CheckPathOption 1,487; OrOperator 654; EventRandomPathOption 493; ECComplete 223; CheckStateTime 109; RandomRoll 19 |
| graphs / DAGs | 112 / 112 (one DAG per file, root node always the Subgraph) |
| nodes | PoseAnimSource 691; ScalarCallback 372; Selection 269; Blend 231; Subgraph 112; BlockCurve 57; Trackmask 52; ScalarSource 52 |
| trigger files / triggers | 268 / Sound 11,625; Effect 5,852; Action 1,046; Transfer 576 (19,099), VP's missile cruiser file read with the lenient comment rule; without that file Sound 11,578, Effect 5,799, Action 1,037, Transfer 572 |

These differ from 3100's census (`logic_census.out.txt`: 69 machines, 666 trigger files; `dge_census.out.txt`: 123
graphs) because that census took every file of every archive, leaders and unreached graphs included, and no mods.
DoD B compares like with like: the reached set here, or 3100's archive-wide set re-run with the same readers.

### 6.2 The state machines as behaviour

**The model every machine follows** [C for the structure, counts in `fsmcheck.out.txt`; I where marked]:

- **Path options are a small integer array per machine instance.** The machines test paths 0-6 and write paths 0-6;
  path 4 appears in 1,253 conditions (1,051 tests), paths 0-3, 5 and 6 in 190-646 each. The graphs read paths 0-8 (6.3): **7 and 8 are never
  written by any machine** and must come from the EXE (6.3).
- **Path 4 is the requested action, a level the EXE writes and the machine may clear** [C for the reads and writes;
  I for "the EXE writes it"]. Every value tested on path 4 is a unit action ec (1000-1990; table 6.5) except one `P4!=0`
  in the great general machine. The machines also write path 4 themselves: `S4=0` (the request consumed: 21 in
  `1_omni_state_01`, 36 in the mounted machine, 42 in the worker machines) when a one-shot action or a run starts, and
  `S4=1000` / `S4=1400` in the default states of the civilian, worker, ship, airplane, missile and work boat machines
  (the unit starts idle or, for airplanes and missiles, flying). So a request stays until the machine consumes it or
  the EXE writes another; a unit at rest has 1000 there (`3 -> 1` of the omni machine waits for `P4==1000` before a
  fidget may end).
- **A state** has an id, a graph (`asset`, empty for 2-3 "chooser" states per machine that only branch), an `action`
  ec (what the state reports; 10000 on the run-start states, 0 on transitions), and a `sockets` attribute that is the
  **number of its outgoing sockets** (299 of 299 states), not a list of references [C; correction to `logic.h`'s
  `[I: 4408 confirms]` note on `FsmState::sockets`].
- **A socket** goes `from` -> `too` (the attribute's spelling); `bidirectional` is 0 on all 716; `order` is 0 on 696
  and 1-1500 on 20, and within a state the sockets are written in ascending `order` (0 states otherwise). Its
  `conditions` attribute equals the number of conditions (716 of 716). Each condition carries a float `order`; in every
  socket the conditions are written in ascending `order` (0 exceptions), so file order is evaluation order.
- **A socket's condition list is an OR of groups**, separated by `OrOperator`. A group holds tests (`CheckPathOption`,
  `ECComplete`, `CheckStateTime`, `RandomRoll`) and events (`EventSetPathOption`, `EventRandomPathOption`). Shapes over
  all 1,359 groups: 1 test with 0-4 events is the common form (956); 2-3 tests (379); 4-13 tests (12, mostly the "none
  of these requests" sockets: `P4!=...` chains); event-only groups (12) and empty sockets (11) fire at once. 156 groups write an
  event before their test. Reading [I]: a group fires when all its tests hold; its events are applied when the socket
  is taken (only the firing group's events); a socket fires when any group fires; the first socket in file order whose
  list fires is taken, at most one transition per update. **The EXE's socket evaluation, read 2026-11-21 [C, 13.5]:**
  only the firing group's events are applied (all of them, those written before its tests included); a group whose
  tests fail applies none; but every test of a group is evaluated even after an earlier one failed (no short cut
  inside a group), so a `RandomRoll` after a failing test is still evaluated. (Was: "whether an event placed before a
  failing test is still applied cannot be told from the files".) "First socket in file order, one transition per
  update" is still [I].
- **Condition semantics** (attributes in brackets) [C for the attributes and values; I for the meaning]:
  `CheckPathOption (method, path, value)`: method 1 = equal (1,344), method 6 = not equal (143); no other method is
  used. `EventSetPathOption (path, value)`: values 0-9, 1000, 1400. `EventRandomPathOption (path, value, useweights,
  <weights><weight v w>)`: with `useweights="1"` (157) the value is drawn from the listed weights (0.5 / 0.5, 0.6 /
  0.4, 0.7 / 0.3 met); with `useweights="0"` (336) the draw is read as uniform over 0..value and any listed weights
  as ignored [I]. Forms met: value 1 without weights 263, value 1 with 0.5 / 0.5 57, value 0 3 (always 0 under this
  reading), and **value 1 with weights 0.6 / 0.4 13 times** (path 0 after a stop run): there the two readings differ
  (50 / 50 against 60 / 40), so the idle A/B split after runs is a live check [L]. `ECComplete (ec [, value])`:
  **[C, 2026-11-21, 13.5] the time in the current state is greater than the length of the model's clip for that code
  (0 when the model has none); the graph, the selection and what is playing do not enter, and `value` is not used.**
  (Was [I]: "the clip of that code in the current state's graph has played to its end".) All 223 codes are sources of
  the from-state's graph (223 of 223); `value` appears 3 times (0.98, missile machine; 0.98 is also the EXE's default
  when the attribute is absent), unused by the EXE's test (was: read as "at 98 % of the clip" [I]). `CheckStateTime (method,
  value)`: method 5 (107) = time in state >= value (0.1-0.5 s); method 1 (2, worker and engineer bombard-defend entry)
  read as the same [I: an equality on a float time would never hold]. `RandomRoll (value, invert)`: per update a chance
  of `value` (0.0007 once: omni idle; 0.0001 18 times), `invert` always 0.
- **Every reached machine is connected** from its default state except omni states 28 and 29 (no graph, unreachable);
  each has exactly one state with no exit, the final death state (dead idle). No machine has a state that can only
  be left by a test that never changes (every non-final state has an `ECComplete`, a state time, a random roll or a
  path-4 test on its way out).

**`1_omni_state_01` (124 members, the warrior's)** [C for the edges; I for the words]. Default state 2 (no graph)
chooses at once: `P4==1500` -> 7 (enter fortify), else `R0` (0.6 idle A, 0.4 idle B) -> 1.

| From | To | Fires on | Writes |
|---|---|---|---|
| 1 idle loop (idle A or B by path 0) | 6 idle chooser | `RR(0.0007)` | `R1` 0.3 fidget / 0.7 swap |
| 6 | 3 fidget / 5 A-B swap | `P1==1` / `P1==0` | |
| 5 swap | 1 | `EC(1020)` with P0 0 / `EC(2020)` with P0 1 | `S0` flipped |
| 3 fidget | 1 | `P4==1000 P0==0 EC(1040)` \| `P0==1 EC(2040)` | |
| 1, 3 | 8 enter combat ready | `P4` = 1600, 1100, 1160, 1180, 1800 | S2 (from idle 0, fidget 1), S3 |
| 8 | 12 combat ready loop | `P3==0 EC(1600)` \| `P3==1 T>=0.2` | |
| 12 | 14 attack | `P4==1100` (`R2` A/B 50/50, S1=0) \| `1160` (S1=1) \| `1180` (S1=3); `S4=0` | |
| 14 | 20 -> 12 | the attack clip complete (`EC` 1100/2100 by P2, 1160, 1180, 1140 by P1) | |
| 12 | 11 leave combat ready | `P4==1000` (S5=0) \| `1800` victory (S5=1, S4=0) \| `1500` (S5=2) | S1=2 |
| 11 | 1 / 7 | `P5==0 EC(1640)` \| `P5==1 EC(1800)` / `P5==2 P4==1500` | S0=0 |
| 1, 3, 10, 16, 22 | 4 start run | `P4` = 1400 (S1=0), 1120 charge (S1=1), 1450 shuffle (S1=2) | S2 = where from, `R6` 50/50 |
| 9, 12, 14, 18 | 25 / 27 attack-to-run | the same three requests | S1, S4=0 |
| 4 / 25 / 27 | 17 run loop | `T>=0.15` / `T>=0.2` / `T>=0.1` | |
| 17 | 23 -> 24 -> 17 | switch between run, charge and shuffle (`P4` 1450 / 1120 / 1400 with P1) | S1, then S4=0 |
| 17 | 9 stop run / 26 stop shuffle | `P4` 1000, 1500 (S6=0), 1160, 1180 (S6=1), ... with P1 != 2 / P1 == 2 | `R0` 0.6 / 0.4 |
| 9 | 1 / 12 | `P6==0` and `EC(1440)` (P0 0) or `EC(2440)` (P0 1) / `P6==1 T>=0.15` | |
| 17 | 14 | `P4==1140` charge attack (S1=2) | S4=0 |
| 1, 7, 22, 16 | 7 enter fortify -> 22 fortify loop | `P4==1500`; 7 -> 22 on `EC(1500)` or `T>=0.15` by P6 | |
| 22 | 16 fortify fidget -> 7 | `RR(0.0001)` / `EC(1540)` | |
| 22, 16 | 10 leave fortify -> 1 / 12 / 4 | `P4` 1000, 1600, 1100, 1160, 1180 / run requests | |
| most | 15 enter bombard defend -> 18 loop -> 19 -> 1 | `P4==1280`; `EC(1280)`; 18 -> 19 when P4 is none of nine action codes (`order` 5) | S3, S6 |
| 1, 3, 9, 12, 14, 16, 17, 18, 22, 23, 26 | 13 death -> 21 final | `P4==1200`; `R6` A/B 50/50; S3 = the state it came from (0-9) | 13 -> 21 on `EC(1200)` / `EC(2200)` by P6 |

**What an idle unit does over time:** in state 1 a roll of 0.0007 per update; on success 30 % a fidget (1040 or 2040
by the current idle variant, back to idle when it completes) and 70 % a swap between idle A and B (1020 / 2020, the
variant flips). The expected wait is 1 / (0.0007 x update rate): 24 s at 60 updates/s, 48 s at 30 [I: the update rate
of the machine is K6's question; the live fidget count of 4405 gives it]. A fortified unit rolls 0.0001 for its
fortify fidget (1540). The civilian, worker, great general and aim-civilian machines use 0.0001 with 0.5 / 0.5.

**`1_omni_mounted_state_01` (39 members, the horseman's)** has the same ids and the same shape; differences [C]: the
fidget/swap roll is 0.0001 with 0.5 / 0.5; the start-run sockets also consume the request (`S4=0`); start run lasts
0.18 s; the stop-run state 9 returns to idle on `EC(1440)` (P6 0) or to combat ready via 8 (P6 1); its run loop
graph (`mounted_running_loop.dge`) adds the turn clips 1460 / 1480 through paths 7 and 8 (6.3). The roman legion
machine (14 members) is the omni machine with work codes 1910-1960 added (legions build roads).

**The other machines** (states / sockets; default; what they accept on path 4) [C]: `civilian_state_01` 16 / 31,
default 29 (`S4=1000`, idle A/B 0.6 / 0.4), 1195 captured, 1200 death, 1280, 1400 run, 1500 sleep, 1990 activate;
`aim_civilian_state_01` is the same machine with the aim-layer graphs (4 spaceship parts); `worker_state` 21 / 50 and
`engineer_state` 21 / 50 add 1910-1960 (six work clips by path 2, a switch state pair 8 -> 9 -> 6 for a new work
type) and 1500 sleep; `great_general_state_01` 20 / 40 accepts the attack codes 1100 / 1160 / 1180 and 1600 too;
`ships_state_01`, `modern_ship_aim_state_01`, VP's `edited_modern_ship_aim_state_01` and `mechanized_aim_state_01`
24 / 64-65, default 2, the omni pattern without charge-to-shuffle and with 1130 anti-air on ships;
`airplane_state_01` 8 / 12, default 6 writes `S4=1400` (a plane flies), 1100 / 1180 attack, 1200 death, 1000 land;
`missle_state_01` 7 / 9, default 8 -> 7 idle with `S0=1000`, `S4=1400` on 1120, 1100 / 1160 / 1180 attack ("kaboom",
`EC(1100, 0.98)`), 1200; `work_boat_state_01` 8 / 12, default 9 `S4=1000`, 1400, 1200, 1990 activate (then idle in
an activated loop until 1000 or 1200).

**How a requested action enters and leaves** (all machines) [C for the edges, I for the EXE side]: an action enters
through a socket testing `P4==code` from every state that allows it (deaths from 11 of 29 omni states; attacks only
from combat ready, so an attack request on an idle unit first plays "enter combat ready" and the attack follows on
the same request); one-shot actions (attacks, victory, charge attack) and runs clear the request with `S4=0`; a held
action (fortify 1500, bombard defend 1280, combat ready 1600, run 1400 in the omni machine) holds while the request
stays and leaves when the EXE writes another code; a one-shot action ends on `ECComplete` of its clip and goes to
combat ready (attacks) or idle (victory, swap, fidget); death never leaves.

### 6.3 The graphs as behaviour (for the evaluator, 4409)

Every reached `.dge` holds one DAG whose root is a `Subgraph` node (112 of 112; `SubnodeIndices` = all other nodes,
`MinDuration` 0, the X/Y positions editor data). Node types and what the evaluator must do [C for parameters and
counts, `dge_census.out.txt`, `hints.out.txt`; I for the semantics]:

| Node | Inputs (edge names) | Parameters met (all values) | Behaviour |
|---|---|---|---|
| PoseAnimSource (691) | Speed, Offset (scalars, usually unconnected) | `SourceIndex` into `Sources[]` (0-20), `TrackGroupIndex` -1, `Speed` 1.0, `Offset` 0.0, `LoopCount` 0 (490) or 1 (201), `ClampedLoop` 1, `ContinueOffset` 0 (495) or 1 (196), `AddClampOffset` 0, `EventTrackIndex` 0, `EventTrackSpecified` 0 | samples the clip bound to `Sources[SourceIndex].EventCode` through the model's `.fxsxml`; LoopCount 0 loops forever, 1 plays once and holds the last pose (ClampedLoop); outputs Pose, Duration, Progress |
| Selection (269) | Selector (scalar), Pose in (0..n-1), n = 2-11 | `DefaultEdge` 1 | passes input `int(Selector)`; DefaultEdge 1 when the selector is out of range [I] |
| Blend (231) | Param (scalar), From, To (poses), Mask (track mask) | none | From x (1 - w) + To x w with w = Param, per bone; Mask unconnected (179) or a Trackmask (52) |
| BlockCurve (57) | Input (always a hint-5 callback) | `Blocks` 3 entries, `Scale` 1.0 (54) or 0.25 (3) | a 0..1 ease curve of its input: block types 3 (107) and 0 (64); one block of duration 1.0, the others 0 |
| ScalarCallback (372) | Input (0), Input (1) unconnected, or none (66) | `CallbackBindHint` 7 (295) or 5 (77); `CallbackValue`; `CallbackStringHint` empty | 7: returns path option `CallbackValue`; 5: returns a fade weight over `CallbackValue` seconds |
| ScalarSource (52) | none | `Min` 0, `Max` 1, `Current` 1 | constant 1 |
| Trackmask (52) | none | `BindingIndex` 0 -> the DAG's `TrackMasks[0]` = the binding name of the aim layer, `DefaultWeight` 1, `TargetWeight` 0 | a per-bone weight set: bones bound to the mask take the To pose, the rest keep From [I] |
| Subgraph (112) | Pose out | | the graph's output |

Hint 6 is not used by any reached graph (3100's 2 uses were in unreached files). No `Callback`, `ScalarSelect`,
`ScalarSwitch`, `ScaleOffset`, `Sequence` or `TimeShift` node occurs.

- **Hint 7 = path option** [C by structure, `hints.out.txt`]: in `start_run_charge_shuffle.dge` the "To" Selection
  reads hint 7 value 1 and its inputs are, in order, the clips 1400, 1120, 1450: exactly what the sockets into the
  start-run state write to path 1 (S1 = 0 run, 1 charge, 2 shuffle). Its "From" Selection reads value 2 over the
  idle (by value 0, A/B), fidget, leave-fortify and fortify-fidget clips: exactly S2 = 0, 1, 2, 3 of the sockets
  from those four states. `death_graph.dge` selects A/B by value 6 (the `R6` the death sockets draw) and its "From"
  over 10 clips by value 3 (S3 = 0-9, written by the socket from each state). Uses of hint-7 outputs: Selector 269 (every Selection;
  values 0, 1, 2, 3, 5, 6, 8), PoseAnimSource Offset 42 (values 0, 5, 6), Blend Param 100 (value 7 92, 0 4, 8 4).
- **Path 6 also drives a clip offset**: 39 Offset inputs read hint 7 value 6 (run and run-start sources). The machine
  writes 0 or 1 there (`R6`, 50 / 50). Whether Offset is in seconds or a fraction of the clip is open [L]; as a
  fraction 1.0 would equal 0, so seconds (or a normalised half step) is the likelier reading [I].
- **Paths 7 and 8 are written by the EXE, not by the machines** [C that no machine writes them; I for the meaning]:
  value 8 selects among 3 inputs (left, centre, right: `mounted_running_loop.dge` picks run-blended-with-turn-left,
  run, run-blended-with-turn-right; `ship_attacks.dge` picks the broadside side; the 52 aim graphs pick the aim
  clip 1700 / 1705 / 1710), value 7 is a continuous Blend weight (90 aim + 2 run uses: how far into the turn or aim
  clip). The EXE's source for them (facing change, `TurnRate*`, `HasLeftRightAttack`, the target's bearing) is open;
  0042 owns turning and 4405 can read them from captures.
- **Hint 5 = a fade-in weight over `CallbackValue` seconds** [I, structure strongly for it]: 77 nodes, values 0.1, 0.15
  (29 nodes), 0.18, 0.2, 0.25, 0.3333 and 100 / 1000 (2 each, aim graphs: never completes in practice); every
  BlockCurve input is a hint-5 callback (57 of 57), and 20 Blend Params take a hint-5 output directly. The value is
  the cross-fade length. In 36 of the 74 states whose graph has a hint-5 fade and that are left on a state time, the
  state time equals the fade length (start run 0.15 / 0.15, attack-to-run 0.2 / 0.2, attack-city-to-run 0.1 / 0.1,
  ...); in 38 it differs (omni enter combat ready fades 0.15 and leaves at 0.2; stop run fades 0.1 and leaves at
  0.15; VP's modern-ship states leave at 0.1 after a 0.2 fade). So the machine may leave a state before its fade
  completes; whether the EXE cross-fades between states is X1's question [L].
- **The cross-fade between states is built into the graphs** [C for structure]: a transition state's graph has a
  "From" side that selects, by a path the incoming socket wrote (2 or 3), the clip the unit was playing, with
  `ContinueOffset` 1 on those sources (196 sources), and a "To" side with the new clip; a hint-5 BlockCurve weight
  fades From into To. Reading [I]: `ContinueOffset` 1 makes a source start at the local time its code had in the
  previous state's graph (so the old clip runs on without a jump while it fades out); 0 starts it at 0. The idle
  "dummy" graph `idle_offset_continue_true_graph.dge` is the idle graph with `ContinueOffset` 1, used by the chooser
  state so idle does not restart while the machine branches.
- **BlockCurve shapes** (all 57): (a) 46: [type 3, 1.0 s] [type 0, 0 s] [type 3 flipped in X, 0 s]; (b) 7: [3, 1.0]
  [0, 0] [0 flipped, 0]; (c) 4: [3, 0] [0, 0] [3 flipped, 1.0]. With an input of 0..1 (state time / fade length,
  clamped) the curve is a 1-second-domain ramp: (a) and (b) rise over the first block and hold; (c) is the mirror
  (falls) [I: type 3 an ease (smooth) segment, type 0 a constant; the exact ease (smoothstep, cubic, sine) is X1's
  measurement]. `Scale` 0.25 (3 graphs) scales the output [I].
- **Track masks:** 52 graphs (all aim graphs: mechanized, modern ship (stock and VP), aim civilian) carry one track
  mask whose binding name is the aim layer; the Blend that takes it has Param = a ScalarSource (constant 1; 49 of 52)
  or a callback, From = the body graph and To = the aim selection: the aim bones are replaced by the aim pose, the body by nothing [I].
- **Loop flags:** loops (LoopCount 0) are idles, runs, combat ready, fortify and bombard-defend loops and the
  "From" sides; once (1) are attacks, deaths, fidgets, swaps, enter / leave transitions. `ECComplete` therefore
  always tests a once-played source (223 of 223 codes are sources of their state's graph; not checked per source flag).
  (Checked per source 2026-11-21, 13.5: one exception, the work boat's `3 -> 1 EC(1440)`, whose 1440 source loops.)

### 6.4 Triggers

Root `trigger_system` type `FGrannyTimedTriggerSystem` in all 268 files [C]. Each `<event_track ec>` lists its
tracks: 1 Sound, 2 Effect, 3 Transfer, 4 Action in 7,499-7,569 event tracks; 30 tracks use 5 for Action, a few use
other numbers and spellings (`sound 2`, `Effects`, 4 for Effect); the trigger's `track` attribute points into that
list, so a reader keys a trigger by its `type` attribute, not by the track number [C]. Per type [C]: `repeat` is 0 on
all 19,099 triggers; Action `duration` 0, `action` 1 (1,042) or 0 (4), track 4 (1,028) or 5 (18); Transfer
`duration` 0, `refid`, `speed`, `tangent.x/y/z`; Effect and Sound carry `event` and `bone`, Effect `duration` (226 distinct values, 0 on 2,559). `time` in seconds from the clip start [I, max 9.56 s in the unit files].

Per action ec (reached unit files; count [min median max time]): Action triggers only on attack codes: 1100 214
[0.0, 0.448, 3.31], 2100 194 [0.078, 0.481, 4.98], 1140 111, 1160 180, 1180 176, 2160 70, 2180 70, 1130 21, 2140 9,
1192 1; Transfer (projectiles) on 1100 86, 2100 86, 1160 158, 1180 153, 2160 38, 2180 39, 1140 15. No Action trigger
on runs, deaths, fortify or idles. Several Action triggers per clip occur (volleys, multi-shot units).

| Unit (file) | ec | Triggers | Action / Transfer time |
|---|---|---|---|
| warrior (`fx_triggers_warrior.ftsxml`) | 1100 attack A | Sound 6, Effect 1, Action 1 | Action 0.4889 s |
| | 2100 attack B | Sound 7, Effect 1, Action 1 | Action 0.4741 s |
| | 1140 charge attack | Sound 7, Effect 1, Action 1 | Action 0.4889 s |
| | 1160 / 1180 attack city / bombard | Effect 1, Sound 5, Transfer 1 | Transfer 0.5759 / 0.5778 s (the thrown torch) |
| | 1120, 1200, 1280, 1400, 1500, 1600, 1800, 2200 | Sound only (3-12), 1200 and 2200 one Effect | |
| horseman (`fx_triggers_horseman.ftsxml`) | 1100 / 2100 | Sound 7 / 6, Action 1 | Action 0.4444 / 0.4519 s |
| | 1140 | Effect 1, Sound 10, Action 1 | Action 0.3630 s |
| | 1160 / 1180 | Effect 1, Sound 6, Transfer 1 | Transfer 0.6019 s |
| | 1460 / 1480 turns, 1400, 1120 | Effect 1 each (dust), Sound | |

So for city attacks the "hit" moment of a melee unit is a Transfer, not an Action [C for the files; I that the EXE
uses the Transfer's arrival there].

### 6.5 Event-to-action map candidates

`EnableActions` words over the 366 reached members [C]: Idle 363, Death 363, Run 363, BombardDefend 348, Attack 249,
Bombard 249, RunCharge 236, AttackCity 236, Fortify 236, CombatReady 236, Walk 149, AttackCharge 109,
AttackSurfaceToAir 52; 3 members have no Combats row; `DisableActions` is empty everywhere. The words map onto the
path-4 codes the machines accept as follows [I by name; every code is accepted by the members' machines, table in
`actions.out.txt`]: Idle 1000, Attack 1100 (A/B by the machine), RunCharge 1120, AttackCharge 1140, AttackCity 1160,
Bombard 1180, Death 1200, BombardDefend 1280, Run 1400, Walk 1450 (shuffle) [I: no other code fits; the mounted and
omni machines accept 1450 whether or not the member has Walk], Fortify 1500, CombatReady 1600, AttackSurfaceToAir
1130. Codes with no word: 1800 victory, 1190 / 1195 captured, 1990 activate, 1910-1960 work.

| DLL call (feed kind, `unit-feed-contract.md` 2.1) | Candidate request on path 4 | From the files |
|---|---|---|
| `GameplayUnitCreated` (11) | none: the default state writes 1000 (civilian, worker, ship families) or chooses idle by itself (omni, mounted) | [C] |
| `GameplayUnitMoved` (12) | 1400 run; 1120 charge or 1450 shuffle by the EXE's choice (short move: `ShortMoveRadius` / `ShortMoveRate`?); 1000 at the end | [I] which code per move is 0042's (C2) |
| `GameplayUnitTeleported` (13) | none (placement only) | [I] |
| `GameplayUnitDestroyed` / `DestroyedInCombat` (14 / 15) | 1200 (A/B drawn by the machine) | [C code; I timing] |
| `GameplayUnitSetDamage` (16) | the figures that fall are removed with no clip (X4, corrected 2026-11-16 from "1200 for the figures that fall"; MC #2147) | [L] one unit |
| `GameplayUnitFortify` (17) | 1500 on, 1000 off | [C code] |
| `GameplayUnitWork` (18) | 1910-1960 by build type (worker, engineer, legion machines); which build gives which code is not in the files | [I] |
| `GameplayUnitActivate` (20) | 1990 (civilian, work boat, great general machines) | [I] |
| `GameplayUnitEmbark` (21) | none: a model swap (the embarked art row's own machine starts at its default) | [I] |
| `GameplayUnitCombat` / `GameplayCityCombat` (25 / 28) | 1600 combat ready, then 1100 / 1140 (melee, charge) / 1160 (attack city) / 1180 (bombard, ranged) / 1130 (anti-air) for the attacker; 1280 bombard defend for the defender of a ranged attack; 1800 victory after | [I] 0042's C3 |
| `GameplayUnitResetAnimationState` (31) | 1000 | [I] |
| `GameplayUnitMissionEnd` (29), `Paradrop` (19), `Rebased` (30), `Visibility` (24), `Garrison` (22), `ShouldDimFlag` (23) | none from the files | |
| (no call) | 1195 captured, 1190 | [I] a capture is a `Destroyed` + `Created` pair in the DLL |

Open points: which DLL calls the EXE turns into "combat ready" (1600) and back to 1000; whether 1800 victory is
requested per unit or per figure; the code for a move by a unit without `RunCharge` / `Walk`; the work-code table.

### 6.6 The [I] readings of `model-reader.md` 6.4, re-scored

| Reading | Now | Evidence |
|---|---|---|
| (a) ec codes bind clips; the meaning from the clip names | **confirmed** for binding [C]: every graph source and every `ECComplete` code resolves through the model's `.fxsxml` (223 / 223; 6 models lack a source code, 6.1) | 6.1, 6.2 |
| (b) path 4 is the requested action | **confirmed by the files** [C], **refined**: a level the EXE writes and the machine clears (`S4=0`) or sets (`S4=1000` / `1400`); only action codes are tested on it in the unit machines (leaders use 0-41) | 6.2 |
| (b) 7 path options, indices 0-6 | **corrected**: the machines use 0-6, the graphs read 0-8; 7 and 8 are written by the EXE (turn and aim) | 6.3 |
| (b) conditions between `OrOperator`s are AND-ed | consistent with every socket read [I stays: the order of event application is open] | 6.2 |
| (b) `ECComplete` waits for the current clip; `CheckStateTime` a time in the state; `RandomRoll` a per-update chance | `ECComplete` names a code of the current graph (223 / 223) [C]; **corrected 2026-11-21 [C, 13.5]: `ECComplete` is a state-time test against the model's clip length for the code, not a wait on the playing clip**; `CheckStateTime` reads the same state time [C, 13.5]; the rest stays [I], update rate K6 [L] | 6.2, 13.5 |
| (b) state `action` = the reported current action | stays [I] (10000 on run starts, 0 on transitions) | 6.2 |
| (c) hint 7 returns path option `CallbackValue` | **confirmed by structure** [C] for paths 0-6 (start-run and death graphs match the sockets' writes exactly) | 6.3 |
| (c) hint 5 returns seconds driving a BlockCurve ramp: cross-fades of 0.1-0.15 s | **refined** [I]: a 0..1 fade over 0.1-0.333 s (and 100 / 1000 s in 4 aim uses); in 36 of 74 states the machine leaves at the fade's end, in 38 not | 6.3 |
| (c) a state change swaps graphs; whether the EXE also cross-fades between states is not in the files | **refined**: the transition graphs carry the cross-fade themselves (From = the previous clip continued by `ContinueOffset`, To = the new one); an extra cross-fade by the EXE stays open [L, X1] | 6.3 |
| (d) trigger `time` in seconds from the clip start | stays [I] (max 9.56 s in the reached unit files; work clips run ~10 s) | 6.4 |
| (d) the Action trigger is the hit moment | **refined** [I]: Action triggers exist only on attack codes; city attacks and bombards of melee units carry a Transfer instead | 6.4 |
| (g) Moved -> 1400, UnitCombat -> 1100/2100 or 1160/1180, Fortify -> 1500, death -> 1200/2200, Work -> 1910-1990 | **refined**: 2100 and 2200 are never requested (the machine draws A/B); requests are 1100, 1160, 1180, 1140, 1130, 1200; 1990 is activate, not work | 6.5 |
| (g) the bridge is [I] | stays [I]: the files fix the codes and the states that accept them, not which DLL call requests them | 6.5 |

Not settled by the files (for 4405's captures): the machine's update rate and the `RandomRoll` rate in time (K6); the
ease curve of block type 3 and an EXE cross-fade between states (X1); the Offset unit; how an ec 0 or a missing source
is drawn; paths 7 and 8's values over a turn and an aim.

## 7. Premises from the recordings already made

Written 2026-11-15 from 20:50 EST by the worker of 4407 (branch `task-4407-recorded-fits`). Offline, no game run.
Status: **done for 4407** (2026-11-15 21:55 EST); the fits still running at the hand-back are listed in 7.5.

Inputs (all read in place; nothing of them is committed, D83):
- 3908's pose sequences P01-P05 (`F:/c5-64bit-work/scene-library/poses/`, 18 captures incl. `P05_horseman_attack_try1`;
  c5h-f, 32-bit DLL `i0ship-26234371-ia`, stock config, frame cap 30, VT present, RenderDoc 1.47; the captures'
  `*.bones.json` with RenderDoc's begin and Present timestamps, renderdoc-capture.md 10.3).
- 0041's fresh capture (4353, unit-renderer.md 11.3): U01-U04 (`F:/c5-64bit-work/0041/4353/captures/`), scanned here
  with `tools/scenario/bones.py scan` on 4353's conversions into `F:/c5-64bit-work/0374/4407/bones4353/` (U01 frame
  2676: 51 skinned draws, 210 instances; U02 5287: 12, 57; U03 7817: 24, 96; U04 11720: 24, 114).
- 3901's capture and 0327's fits of it (unit-playback.md 1.3, 5.4): reused, not refitted.
- The art: `F:/c5-64bit-work/0327/extract/fpk/UnitModels/` (0327's extraction).
- Tools: `models/tools/posefit/posefit.py fit --sampler cpp --step 0.01` on `posesample` built from this branch
  (`python -I models/build.py --out C:/c5-64bit-work/0374/4407/models-build --target posesample`, 2026-11-15 20:51,
  exit 0), every clip of the member's `.fxsxml` per block; `blendfit.py`'s blend (per-bone lerp of positions and nlerp
  of orientations in local space, weight w on the second clip) for the blocks no single clip fits (grid 41 x 41 x 21, then
  Nelder-Mead from the 8 best separated grid points). Scripts and the command list:
  `render/evidence/0374/4407/` (committed after the C1 answer). Tables: `F:/c5-64bit-work/0374/4407/table_4407.csv` (one row per
  sequence, frame and figure-mesh: member, mesh, clip, ec, t, s, yaw, residuals, runner-up and its cost ratio, blend
  partner, its t and weight, Present time), `k6_steps_4407.csv`, `blends_*.json`.

### 7.1 Tables

`F:/c5-64bit-work/0374/4407/table_4407.csv` (not committed; D83) holds the oracle table form for the warrior and
barbarian blocks: one row per sequence, frame and figure-mesh. Columns: Present time, draw, instance id, figure (as
followed across frames), member, mesh, clip and its ec (through the member's `.fxsxml`), t, duration, s, yaw, tilt,
the placement T, rotation, rotation-element, translation and relative translation maxima, runner-up clip with its t
and cost ratio, and the blend partner, its t and weight where `blends.py` was run. Figures are followed by the
per-instance id where a mesh's id set is equal in two frames (P02-P05), else by nearest T (P01 alternates id ranges
between captures: 3-8, then 13-18).

| Sequence (scenario action) | Frames (Present step s) | Blocks | Rotation < 0.01 deg | > 0.05 deg | What the figures play |
|---|---|---|---|---|---|
| P01 idle | 4677, 4679, 4681, 4683 (1.470, 1.437, 1.249) | 48 warrior + 40 horseman | 87 | 1 (the cross-fade, 7.3) | idle A / B, fidget A |
| P02 warrior move (41,11) -> (43,11) | 10652, 10654 (1.416) | 24 warrior + 20 barbarian | 32 | 2 (run, 0.064 and 0.115 deg [I: turning figures]) | idle A / B, idle B -> A transition, run; barbarians fortify idle |
| P03 horseman move | 1318, 1320, 1322 (1.127, 1.207) | 30 barbarian + 30 horseman (5 riders, 5 horses) | 30 | 20 (horseman run, 1.0-4.8 deg) | fortify idle; horseman run (no single clip fits, 7.3), then stop run 1440 / 2440 at 0.200 |
| P04 warrior attacks barbarian | 639, 641, 643, 645 (0.967, 0.978, 0.979) | 48 warrior + 40 barbarian | 49 | 12 (6 shuffle seams, below; the 6 barbarian alpha and bravo blocks of 645, combat-ready idle at 0.057 deg) | shuffle -> combat-ready idle -> charge run (warrior); shuffle -> combat-ready idle (barbarian) |
| P05 horseman attacks barbarian | 816, 818 (1.566) | 20 barbarian + 20 horseman | 31 | 2 (35 deg: no clip of the member fits; a horse at 0.19 deg) | barbarian: bombard-defense idle, combat-ready idle, shuffle, death A / B, death A idle; horseman: charge run -> shuffle |
| P05 try 1 | 681, 683, 685 (1.255, 1.223) | 18 barbarian | 9 | 2 (shuffle seams) | shuffle |
| 4353 U01-U04 | 2676, 5287, 7817, 11720 (single frames, 85-173 s apart) | 27 warrior (Warrior2, Warrior4; the `warrior.gr2` mesh pending) + 3 mech | 28 | 2 (mech, 7.3) | idle A / B, fidget A, fortify idle |
| 3901 | 1762 (single frame) | 12 warrior + 6 pathfinder (0327's fits) | 18 | 0 | fortify idle, fortify idle fidget |

- Every block either fits one clip to 0.0005-0.05 deg, or is a cross-fade or seam (7.3), except P05 818 id 12 (a
  barbarian bravo figure in combat, 35 deg against every one of its member's 31 clips [I: a clip of another file, or
  a three-way blend]).
- The shuffle blocks above 0.05 deg (P04, P05 try 1: 7 blocks) all lie at t = 0.00-0.12 s after the wrap of the
  0.6667 s shuffle, and blocks of the same figure away from the seam fit to 0.000-0.002 deg. So the shuffle's loop
  seam is blended over about 0.1-0.15 s [I: an in-graph fade, the hint-5 kind of 6.4 (c)].

| P04 figure | 639 | 641 | 643 | 645 |
|---|---|---|---|---|
| warrior (12 figures, 3 meshes) | shuffle at 12 different t (0.040-0.498) | shuffle, each +0.2 | combat-ready idle 0.1996, all 12 | charge run 0.0000, all 12 (pure) |
| barbarian (10 figures, 3 members) | shuffle at 10 different t | shuffle, each +0.4 | shuffle, each +0.2 | combat-ready idle 0.1996, all 10 |

### 7.2 K6: the clock

**Result: the 0.2 s clamp as stated ("clip time advances by the frame's step, clamped at 0.2 s") does not hold; it is
replaced by the observations below [L] and an open mechanism [I].** Sent to lead-0374 as a PREMISE CHANGE at 21:33
and 21:37 EST, accepted by the MC (#2023).

The captures are every second presented frame (renderdoc-capture.md 10.3), so each step below spans two presented
frames: the uncaptured frame, in which RenderDoc writes the previous capture, and the captured one. Steps are
`t_b - t_a` (plus the clip duration after a wrap) for one figure in the same clip in both frames, both fits under
0.05 deg (`k6_steps_4407.csv`).

| Sequence, pair | Present step (s) | Figures | Clip step (s), min / median / max |
|---|---|---|---|
| P01 4677 -> 4679 | 1.4704 | 11 warriors + 10 horseman blocks (5 riders, 5 horses) | 0.39974 / 0.40003 / 0.40007 (plus 1 at 0.19978: the fidget figure, 7.3) |
| P01 4679 -> 4681 | 1.4370 | 11 + 10 | 0.39991 / 0.40000 / 0.40008 |
| P01 4681 -> 4683 | 1.2488 | 11 + 10 | 0.39959 / 0.39997 / 0.40034 |
| P02 10652 -> 10654 | 1.4156 | 13 (barbarians fortified, a warrior in run and in idle B -> A) | 0.39997 / 0.40000 / 0.40007 |
| P03 1318 -> 1320, 1320 -> 1322 | 1.1268, 1.2071 | 10 + 10 barbarians fortified | 0.39979-0.40004 (plus 1 at 0.20017: a figure that entered fortify idle at 1318) |
| P04 639 -> 641 | 0.9672 | 12 attacking warrior figures (shuffle, 0.6667 s loop) | **0.200** (0.198-0.201) |
| P04 639 -> 641 | 0.9672 | 10 defending barbarian figures (shuffle) | **0.400** (each, read across the wrap where it wraps) |
| P04 641 -> 643 | 0.9782 | the 10 barbarian figures | **0.200** (0.19995-0.20003 where both fits are clean) |
| P05 try 1, 681 -> 683 and 683 -> 685 | 1.2545, 1.2227 | 6 defending barbarian figures (shuffle; the horseman attacks) | **0.400**, then **0.200** (0.19995-0.20179) |
| P05 816 -> 818 | 1.5663 | 5 barbarian figures on one clip in both frames | 0.400 (one figure 0.19958) |

- Every observed step is 0.200 or 0.400 s within 0.0004 s (fit precision), across Present steps of 0.97-1.47 s.
  So in the capture regime **each figure advances either 0.2 s or 0 s per presented frame** [L]. A per-frame
  `min(dt, 0.2)` cannot give exactly 0.2000 over two frames, because the short captured frame would add its own
  0.01-0.06 s. The 0.2 s ceiling is consistent with the data (two long frames give 0.4) [I].
- **The zero frames depend on the unit, not only on the frame [L].** In P04, over the same two frames 639 -> 641, the
  attacking warrior's 12 figures advance 0.2 and the defending barbarian's 10 figures 0.4. In P01 a single figure loses
  one 0.2 step on the frame its fidget is decided (7.3). What makes a unit or figure skip a frame (an event or state
  change being processed, involvement in combat) is not established [I].
- **A figure can hold for two frames in combat [L].** P05 816 -> 818 (1.566 s): one horseman figure (rider and horse)
  stays in shuffle at 0.4386 s in both frames (step 0.000), while the other 4 switch from charge run to shuffle 0.0000.
- **Clip times sit on a 1/30 s lattice [L].** Of 72 distinct well-fitted (sequence, frame, clip, t) groups outside
  the shuffle (rotation error < 0.01 deg), 65 lie within 0.001 s of a multiple of 1/30 s. Uniform times would put
  about 4 there, and the mean distance is 0.0013 s against 0.0083 s for uniform times (`lattice.py`). With the 48
  shuffle groups included it is 72 of 120: the P04 and P05 shuffle times (0.2592, 0.3216, 0.4352, 0.4619, ...) start
  at a per-figure offset off the lattice [I: a random start offset
  of the shuffle]. Together with the 0.2 = 6 x 1/30 steps this points to **a fixed step of 1/30 s, whole steps per
  frame, at most 6 (0.2 s) per frame** [I]. At frame cap 30 this is one step per frame in normal play. Whether 1/30
  is the frame cap (config 30, the only one recorded) or a fixed rate of the EXE is open.
  **Corrected 2026-11-15 23:19 (lead-0374, MC #2089, from 4405 B's single captures, section 8):** the whole-step
  reading is rejected. At caps 30 and 60 groups sharing a clock sit off the 1/30 lattice (cap 30: 4.0415, 3.1083,
  0.1544 s, 8-13 ms off k/30; cap 60: 0.2712, 2.7132, 3.3262 s, off k/30 and k/60) [L]. The lattice above comes from
  runs of exactly capped 1/30 s frames; any longer frame shifts the phase [I]. K6 is now per unit t += min(dt, 0.2)
  per frame plus a per-unit skip rule held as data (section 9).
- Clips loop over their duration (wrap at 6.0 s idle, 0.6667 s run and shuffle; idle and run show no seam, as in
  unit-playback.md 5.6; the shuffle's seam is blended, 7.1).
- For the controller: `dt` cannot be applied as is. The rule must be measured where steps are not saturated: 4405's
  shifted single captures at the frame cap (steps of 1/30 s expected), a second frame cap (60 or none) to separate
  "frame cap" from "fixed 1/30", and one unit in combat beside one that is not, over consecutive frames.

### 7.3 X1: the cross-fade between states

Fits with `blends.py` (local space unless stated; `blends_*.json`, `scan_t1.out`).

**P01 frame 4681, warrior figure `Warrior4` (idle A -> fidget A, a `RandomRoll` state change) [L].**
- Timeline of the figure (its group in brackets): 4677 idle A 5.0342 (5.0342); 4679 idle A **5.2339**, pure (5.4340:
  the figure lost one 0.2 step); 4681 the blend; 4683 fidget A **0.6000**, pure (rotation 0.005 deg).
- The blend: fidget A (new) at t2 = **0.1995-0.2000** with weight **w = 0.680** (0.679-0.687 over every fit variant)
  on idle A (old). Rotation error 0.025 deg at the best old time, against 0.0005-0.002 deg for single-clip blocks, so
  the two-clip local blend is close to the rule but not exact.
- **Blend space: local** (per-bone lerp of positions and nlerp of orientations, then the hierarchy). Model-space
  blending of the skinning matrices is 100 times worse in cost (5.5e-3 against 5.5e-5) at every old time.
- **Old clip: not running with its group, held [L].** With t2 = 0.2, a scan over the old clip's time (best w at each)
  gives the lowest cost at t1 = **5.40** (cost below twice the minimum over 5.33-5.46). At the 4151 "frozen at exit"
  value 5.234 the cost is 5.4 times the minimum. At the running value 5.834 (the group's) it is 46 times, and at
  5.634 (running, one step behind) 15 times. Reading [I]: the old clip advanced once more after 4679 (to about 5.434)
  and was held from the frame the new clip started at 0 (4680; 0.2 at 4681, 0.6 at 4683 after 0.2 per frame).
- **Weight curve: one point.** w(0.2 s after the new clip's start) = 0.680 (or after 0.4 s, if the fade began on the
  frame of the lost step, 4679). Candidates: linear over 0.3 s gives 0.667 (within 0.02); linear over 0.294 s gives it
  exactly; smoothstep or cosine ease over 0.3 s gives 0.74 / 0.75; any fade of 0.1, 0.15 or 0.25 s would be complete
  (w = 1) by 0.2 s and is ruled out for this transition. With the 0.4 s reading: linear 0.59 s, smoothstep 0.65 s.
  The BlockCurve ramp of `model-reader.md` 6.4 (c) needs its block type and scale from 4408 before it can be scored.

**Idle -> run (P02) and combat-ready idle -> charge run (P04) [L]: no fade visible.** On the first captured frame of the
new state the pose is the new clip alone at t = 0.0000 with rotation error 0.000 deg (P02 10654: 4 warrior figures;
P04 645: all 12 warrior figures in `charge_run`). The figures one frame further on show run at 0.2000 with 0.023 deg,
which is run's own residual on this sequence (moving figures fit 0.012-0.023 deg at any t). So either these
transitions have no cross-fade, or their fade is complete within the frame the new clip starts. The fade length depends
on the transition [L]; a single fixed time for every state change is ruled out.

**Charge run -> shuffle (P05 818, the attacking horseman) [L]: no fade visible.** 4 of 5 figures (rider and horse) are
pure shuffle at t = 0.0000 (0.000 deg), as in rows above; the fifth figure was already in shuffle at 816.

**Horseman run (P03) [L]: no single clip.** While the horseman unit moves, every rider and horse block fits its best
clip (run) only to 1.0-4.8 deg. At 1322 every figure is in stop run at 0.1999-0.2000
(1440 or 2440 per figure, 0.016-0.026 deg). So mounted running is a blend [I: run with the turn-run clips 1460 / 1480,
or a speed blend; X5's matter].

**Shuffle -> combat-ready idle (P04 643: 12 warrior figures; 645: 10 barbarian figures).** Every figure at
`combat_ready_idle` 0.1996 s with 0.033 / 0.057 deg: one frame into the new state and slightly worse than a single
clip (a small remaining weight on the old pose [I]; not fitted).

**Corrected 2026-11-16 00:27 (lead-0374, from 4406 round 2, section 15.8):** the mech blocks are idle B under the aim
layer (4410's track mask), not cross-fades: the controller's idle program reproduces id 51 to rotation element 1e-6
and translation 5.6e-5; id 46 is within on rotation (0.00032) and out on translation (0.012) [L]. The text below was
written before the masks were known (was: "not explained").

**The 4353 mech blocks (U01, ids 46 and 51) [L]: not explained.** Single clips (Python sampler with a format-3 decoder
added for this check, s fixed 0.12): idle B 4.5134 s 1.32 / 1.31 deg, fidget B at 0 1.47, either A <-> B transition at
its end 1.49. These are all the same rest-like pose about 1.3 deg away. The local blend search over idle B x fidget B
found nothing better than the single clip (best 12.5 deg: the search failed on this 32-bone mesh, not a result). The
third mech (id 50) is pure idle A (0.0003 deg; idle A is nearly static, so its t is not determined: 0.18, 2.28 and
3.60 s fit alike). Left to 4405.

**What 4405's live sequences must sample (the X1 and K6 list; written 2026-11-15 21:50 at the lead's request).** Each
row is one state pair. Each needs a capture on the frame the request is made and on **every consecutive frame**
(single captures shifted by one frame each, at the frame cap, not multi-frame series, whose steps saturate) for at
least 20 frames, i.e. 0.67 s at 1/30 s. That covers the 0.3-0.6 s fade read in P01 with margin. Repeat each row with
3 start offsets so a skipped step can be told from a fade.

| # | Unit (members) | From -> to (ec) | Why |
|---|---|---|---|
| 1 | warrior (3 meshes, one clip set) | idle A -> fidget A (1000 -> 1040) and back to idle | the P01 fade: weight curve, old clip held, the lost step on the decision frame |
| 2 | warrior | idle A -> idle B through 1020 | a transition clip between idles: fade at both ends or none |
| 3 | warrior | idle -> run (1400) and run -> stop (1440 / 2440) | P02: no fade on the first frame; is there one on any frame; per-figure start stagger (7.4) |
| 4 | horseman (rider + horse) | run with a turn (1460 / 1480) | turn clips exist only for mounted units; rider and horse on one clock |
| 5 | warrior | idle -> fortify (1500) -> fortify idle (1520) -> fortify fidget (1540) | one-shot then loop; the P03 figure that entered fortify idle at t 0 |
| 6 | warrior attacking barbarian | shuffle (1450) -> combat-ready idle (1620) -> charge run (1120) -> attack (1100 / 2100 / 1140) -> victory (1800) or death (1200 / 2200 -> 1220 / 2220) | P04 / P05: the per-unit skipped steps in combat, the shuffle seam fade (about 0.1-0.15 s), the 35 deg block of P05 |
| 7 | mech (32 bones, format 3) | idle B <-> fidget B (2000 / 2040) | the two 4353 blocks this section could not explain |
| 8 | any one unit, two frame caps (30 and 60 or none) | idle | K6: is h = 1/30 the frame cap or a fixed rate |

With 2 units per row (an active unit and a bystander that is not in the action, so a per-unit skip shows against the
bystander), 3 offsets and 20 frames, rows 1-7 are about 840 captures. Rows 1, 3, 6 and 8 alone (about 480) settle
K6 and the three transition kinds DoD D names first.

### 7.4 X3: per-figure clocks

[L] unless marked. Figures are the members' instances of one unit.

- **Idle: one clock per unit, the variant per figure.** P01: of the warrior's 12 figures, 8 share t = 5.0342 at 4677 (6
  playing idle A, among them the figure that fidgets later, and 2 idle B: the A/B variant is chosen per figure, the
  clock is shared). The other 4 are at 4 other times (2.6009, 3.1328, 5.9671, 0.7675), each on its own clock across
  all 4 frames. All 5 horseman figures share
  5.0672: rider and horse of a figure play the same clip at the same t, 3 figures idle B and 2 idle A. Warrior and
  horseman differ by 0.0330 s, one 1/30 step: units placed on consecutive frames keep their own clocks. 3901
  (fortified): 9 of 12 warriors at 1.07629, 3 elsewhere (one in fortify fidget at 2.80105; 1.66834; 0.83395); the
  Shoshone pathfinder's 6 figures at 5 times (two share 3.07615, one in its fidget).
- **Figures separate through fidgets.** The P01 fidget figure (7.3) leaves its group by losing one step and playing
  fidget A. The figures found off the group's time are, by that reading, the ones that fidgeted earlier [I]. Whether
  the roll is per figure or per unit is open: in 4 frames only one figure rolled.
- **Fortified: one clock.** P02: all 10 barbarian figures (3 members) at 5.8669-5.8670 in both frames. P03: 9 of 10 at
  0.0326 / 0.4326 / 0.8326; the tenth (bravo, id 0) at 0.0000 at 1318, then 0.2002, 0.6000: it entered fortify idle
  on frame 1318 and lost one step there.
- **A unit-level state change resynchronises every figure [L].** P04: the 12 warrior figures, in shuffle at 12
  different times (0.040-0.498 s; shuffle starts at a per-figure offset off the 1/30 lattice [I: random]), are all
  at combat-ready idle 0.1996 at 643 and at charge run 0.0000 at 645. The 10 barbarian figures, in shuffle at 10
  different times, are all at combat-ready idle 0.1996 at 645.
- **A move starts figure by figure.** P02 10652 (0.07 s after the move request): 4 figures already in run
  (0.098-0.495 s), 1 in the idle B -> A transition, 7 still idle (one group of 3 at 0.5669, one of 3 at 0.767, one
  in idle B at 0.0000). At 10654: the group of 3 is at run 0.2000 (started 1 frame earlier), the other 4 at run 0.0000
  (starting on that frame), the transition figure still in its transition (0.9338). So figures start running in
  groups, staggered by frames, after finishing a transition already running [L]. The stagger rule (by figure index,
  by distance, random) is open.
- **Fortified figures separate over time [L].** 4353 U03 (frame 7817): the 6 fitted figures of the fortified warrior
  at fortify idle 2.3330 together. U04 (11720, 131 s later, the same unit): 3 figures at 2.8393 and 3 at 1.9330,
  2.1332, 3.3328. U01 (2676): of two warrior units, one has 5 of 6 fitted figures at 4.7324-4.7326 (idle A and B), the
  other 6 figures at 6 times (one in fidget A).
- **In combat, figures play different states.** P05 818 (the barbarian unit being attacked by the horseman): of 10
  figures, 2 in death A at 0.4000, 1 in death A idle at 0.0000, 1 in death B at 0.9330, 4 in combat-ready idle at 4
  different t, 1 in shuffle and 1 unexplained (7.1). The two alpha figures at 816 are both in bombard-defense idle
  at 0.0000 and both in death A 0.4000 at 818: a figure set that dies together shares a clock.

### 7.5 Not verified

- The horseman blocks of P05 try 1 and the `warrior.gr2` mesh of 4353 U01-U04. Their fits were still running at the hand-back (C++ sampler, all 31-33 clips
  per block, about 8 to 30 min per job), so this section does not use them.
- K6's mechanism: why a unit or figure loses a step (7.2); h = 1/30 as frame cap or fixed rate.
- X1's weight curve. There is one point (w 0.680), the old clip's exact held time (5.40 +- 0.06) is poorly
  determined because idle A changes slowly there, and the blend model leaves 0.025 deg (10-50 times a single clip),
  so the rule is near but not exact. Local-space nlerp is the best of the two spaces tried; slerp and
  accumulate-and-normalise were not tried.
- The 4353 mech blocks (7.3) and P05 818 id 12 (35 deg).
- The P02 run residuals (0.012-0.023 deg, 0.064 and 0.115 on two blocks) are read as turning figures, not checked.

## 8. The live oracle: sequences and capture plan

Written 2026-11-15 21:05-22:00 EST by the 4405 worker (account B, branch `task-4405-live-oracle`), part A: the premise
checks (8.1), the plan part B executes (8.2-8.5), and the one idle series allowed before C1 (MC #1985) with its fits
(8.6). Marks as at the top of this file.

### 8.1 Premise checks (2026-11-15 20:49-20:59 EST, each under 15 minutes; all passed)

| Check | Command | Result |
|---|---|---|
| 0302's multi-frame capture on `main` | `python -I -m pytest -q tools/harness/tests/test_capture_frames.py` | 35 passed; the request block (`C5CaptureReq`) is in `import_wrapper.cpp`, `cli.py capture --frames N --mode multi\|loop --min-free-mb` [C]; used live in 8.6 [L] |
| A pool instance with 9070's silent start | `pool.py list`; `provision.py verify c5h-c --quick` | c5h-c, c5h-e, c5h-h, c5h-i free at 20:52; c5h-c verify ok with no error (verify checks the six D148 keys); one warning: a stale `cache/stats.db-journal` (6,704 B, header zeroed, held by no process), harmless and left |
| 9070's `provision.py` fix | read `tools/instances/provision.py` | `set-dll` recovers a missing `dll.kept_sha256` from the kept copy (MC #1868): 4353's prepare crash cannot recur; `runner.py prepare` ran clean in 8.6 |
| The wrapper | `wrapper.py status <c5h-c>/install` | c5h-c held the 1113 wrapper `a62f2c11...` installed 2026-11-14 15:58, older than 0302's request block: `runner.py prepare` backs it up by SHA-256 and installs the worktree's 0302+ wrapper; native harness built in the worktree (`build.bat`, through `quietlock.py run`, exit 0) |
| Runner and Lua kinds today | read `tools/scenario/runner.py`, `lua/*.lua`; `runner.py dry-run poses_tiny.json` | `action.kind` = `move` (MISSION_MOVE_TO), `attack` (the same onto an enemy's plot), `range` (MISSION_RANGE_ATTACK); `delay_s` 0-30 s after the action; `capture_frames` 1-16; scenario key `animations` (quick combat and quick movement off for the process). **Missing**: fortify, set damage, kill, an explicit embark (only a move onto water, 0041's U02), a repeat at shifted delays, a second action on the same units |
| 0041's `units_0041.json` | read the file and 4353's log | land studio [36,7,47,14] on a copy `C5H-0041-CAT0301_0009` of `c5h-9028-tiny-t0008` (the runner refuses a save name without `0301`); U01 eleven static units, settle 8 s; U02 a warrior moved onto the coast (embarks), capture 8 s later; U03/U04 an archer's ranged attack on a barbarian (player 63, always at war), capture 10 s later; one capture per frame; no `animations` key, so **quick combat and quick movement were on**: U02-U04 show the end states, not the walk or the strike [I, from the file] |
| How the fits are made | read `models/tools/posefit/`, 4407's `run_fits.py`; `pytest models/tools/posefit/test_posefit.py` with `POSESAMPLE` | `bones.py scan` writes each capture's `*.bones.json` (VS t27 blocks per skinned draw and instance); `posefit.py fit --sampler cpp --exe posesample.exe --step 0.01` over every clip of the member's `.fxsxml`, art from `F:/c5-64bit-work/0327/extract/fpk/UnitModels/`; `track.py` follows figures across frames by placement; `blendfit.py` fits two-clip cross-fades. `posesample` built at `C:/c5-64bit-work/0374/4405/models-build` (exit 0); 11 tests passed |

Two facts from the DLL source (Community-Patch-DLL checkout on the workstation, read 2026-11-15) that shape the
stimuli [C, source]: (1) `CvUnitMission` starts the fortify clips at once (`triggerFortifyAnimation(true)` ->
`GameplayUnitFortify(unit, 1)`) when a FORTIFY, HEAL, ALERT or SKIP mission starts on a unit that **has not moved this
turn** and can fortify; any other mission sends `GameplayUnitFortify(unit, 0)` only if the unit's `m_bFortified` is
set, and `m_bFortified` is set only at the owner's turn start, for every unit that did not move and can fortify ("bonus
for entrenched units", `CvPlayer.cpp`). So in a running VP game every idle military unit of a player is fortified from
its second turn on: the fortified idle is the common steady state, not the plain idle. (2) `place.lua` calls
`FinishMoves()` on every placed unit, which sets `m_bMovedThisTurn`: a placed unit can never start the fortify clips
in the placing turn. Hence the new placement key of 8.4.

### 8.2 Common setup (every sequence)

- **Place**: one pool instance claimed under 0374 (`pool.py claim --owner 0374 --name c5h-c`, the C: instance used in
  8.6; another free C: instance if c5h-c is taken), `C5_POOL_OWNER`, `C5_JOB_OWNER`, `C5_QUIETLOCK_WHO` = 0374 in the
  shell; no-focus launcher (`runner.py launch`, the harness's cloaked start), the run's guard log and start-up watch
  read after every launch (COMMON 14 c).
- **Build and settings**: 32-bit DLL `i0ship-26234371-ia` named on the command line
  (`--dll <pins_dir>/dlls/i0ship-26234371-ia/CvGameCore_Expansion2.dll`, SHA-256 3C2F47DA..., COMMON 18);
  CP/VP/EUI; stock `config.ini`; frame cap 30 (0157); Vibrant Textures present (D94); RenderDoc 1.47 preloaded by the
  0302+ wrapper; captures to `F:/c5-64bit-work/0374/4405/captures`.
- **Save**: one copy `C5H-0374-UNITS0301_0009` of `c5h-9028-tiny-t0008` (SHA-256 b2ceb663...dcc29163; Tiny 56 x 36,
  turn 9, active player Polynesia; a scenario file names one save, and `units_0374.json` holds every sequence), made
  by `runner.py prepare`; its hash is checked before every launch (the game never saves over it: Q1's copy came back
  unchanged), and every process starts from it (a relaunch needs a new `prepare`: the front-end load token is used
  once, E9). Removed at the end. (Q1 ran on a copy named `C5H-0374-IDLE0301_0009`, the same bytes.)
- **Plots**: the poses studio of 3908 (x 40-45, y 10-13: flat, unowned, probe 2026-11-15 01:42) for idle, fortify,
  moves, melee, death and damage; 0041's coast pair (34,11) -> (33,11) for embark (Polynesia embarks from the start);
  enemies are barbarians (player 63: at war, no declaration popup).
- **Stimuli** run in the order the scenario lists them, through the Lua channel, each answer checked for an error (a
  failed stimulus fails the frame and the run exits non-zero, E7); popups closed through `UIManager` before every shot
  and capture (D114); the interface hidden by Lua (P4).
- **Restore by hash** (AGENTS 4): `runner.py prepare` backs up the harness's standard files, `install/steam_api.dll`,
  `install/c5_wrapper.json`, `instance.json` and both manifests into `C:/c5-64bit-work/0374/4405/backup/<instance>`
  (a fresh folder per series: the backup refuses a used one, P34) before `set-dll` and the wrapper install;
  `runner.py stop` (harness stop, human turn, newest autosave settled) then `runner.py restore` puts each file back
  and checks its hash; `provision.py verify <instance>` after, then the save copies removed and the claim released.
  What to restore is written into the issue log before each launch.

### 8.3 The two capture modes

- **M, multi-frame series** (0302, `capture_frames: N`): RenderDoc captures every second presented frame; captured
  frames are 1.1-1.25 s apart in wall time on 0302's view and **1.57-1.76 s** with Q1's four units at zoom min-1
  (8.6), because each write stalls the game (renderdoc-capture.md 10.3). Each capture carries its frame number and
  Present time (`rdcexport.frame_info`). **Q1 got 4 frames per process, not 7** (the guard measured 114 MB per frame
  and stopped the request after frame 3; frame 4 was already started, 10.4's known limit): plan **M4** per process.
  The clip clock advances **0.400 s between two captured frames** (two presented frames of 0.200 s each, 8.6), so an
  M4 spans 1.2 s of animation: about one action clip (the warrior's attack, death, fortify and stop clips are 1.0 s,
  run 0.667 s, attack city and victory 1.333 s). A longer action is covered by **shifted series**: the same stimulus in
  the next process, with the series started d s of normal-speed play after it (`delay_s`), so the series together
  tile the action (d = 0, 1.2, 2.4 s ...). This mode gives **state sequences** (which clip follows which, K7, X5) and
  **K6 under the stall**.
- **S, shifted singles** (new key `repeat_delays_s`, 8.4): one stimulus repeated on freshly placed units, each time
  followed by one single capture after a requested delay d (for example 0.05, 0.15, 0.3, 0.5, 0.8, 1.2 s). The frame
  before a single capture is an ordinary frame at the frame cap (about 33 ms), so a single samples the transition at
  normal speed; d only spreads the samples. **The offset of each sample is read from the capture itself**, not from the
  wall clock: the new state's clip starts at 0 at the transition (X1's hypothesis), so its fitted t is the time since
  the transition, and the blend weight against that t is X1's curve. The requested d, the Lua answer's return time and
  the capture's trigger time are recorded beside it as a check. Repeats run in one process with the units re-placed
  for each sample (the runner removes and re-places a frame's objects), not by reloading the save per sample: a reload
  costs about 2 minutes and buys nothing for the animation, whose random choices are the EXE's own and not replayed
  by a reload [I]. Each process still starts from its save copy; at most 4 singles per process (the first capture of
  a process keeps about 400 MB of the address space, 10.4; Q1's frames cost 114 MB each). A cross-fade between two
  poses that are equal at the switch is invisible to any capture (Q1's fidget -> idle A: no blend residual 0.2 s after
  the switch, 8.6), so S targets transitions whose poses differ at the switch: idle -> run, idle -> attack.

### 8.4 Runner and Lua additions (part B; additive, each with a test)

| Addition | Where | Arguments | What it does | Tests (hermetic, `tools/scenario/tests/`, new) |
|---|---|---|---|---|
| object key `moves: "keep"` | `place.lua`, `validate_scenario` | `"keep"` or absent | skips `FinishMoves()` and the sleep for that object (fortify needs a unit that has not moved this turn); allowed only for an owner other than the active player, so that the game never auto-selects it and moves the camera (E9) | validate: `keep` on an active-player object refused; rendered Lua contains no `FinishMoves` for the object |
| object key `damage` | `place.lua` | 0-99 | `SetDamage(value, -1, true)` right after `InitUnit` (the figures fall before the capture starts: the death sequence's pre-damaged defender) | validate: -1, 100, a float refused |
| action `fortify` | `lua/action.lua` (new branch), `run_action` | `object` | wakes the unit, `PushMission(MISSION_FORTIFY)`; returns `CanFortify`, the activity and `GetFortifyTurns`; fails the frame when the unit has moved this turn | render; a scripted Lua answer with `ok=false` exits 1 |
| action `damage` | as above | `object`, `value` 1-99 | `SetDamage(value, -1, true)` on a unit already in view (damage outside combat: `GameplayUnitSetDamage`, X4); returns damage before and after and the figure count the EXE will draw [I] | validate ranges |
| action `kill` | as above | `object`, `delay` bool | `Kill(false, -1)`: `GameplayUnitDestroyed`, a removal outside combat (whether any death clip plays is the question) | render |
| `actions` (a list) | `run_frame` | items as `action`, each with `delay_s` | stimuli in order before the capture (fortify then move off; damage then kill) | validate: order kept, at most 4 items, `object` indexes checked per item |
| frame key `carry_objects` | `run_frame` | `true` | the frame acts on the previous frame's units (not re-placed, not cleared before): a second stimulus during one unit's life, e.g. M frames before and after a move | validate: refused on the first frame and after a frame with `clear: true` |
| frame key `repeat_delays_s` | `cmd_run` | list of 1-6 floats in [0, 30] | expands the frame into one frame per delay (`<id>_d<ms>`), each re-placing, acting, waiting d, then one single capture; records `lua_return_s`, `capture_requested_after_s` and the trigger's `t_ms` | validate; the expansion's ids and count; `--max-captures` counts each |

Every Lua chunk keeps the runner's rule: game state changes only on the save copy the scenario names (the name check
stays). The scenario file is `tools/scenario/scenarios/units_0374.json` (one file, a frame group per sequence;
`--frames` selects one sequence per process). README section "Additions of issue 4405".

### 8.5 The sequences

The art rows are the stock rows the unit types name in the active database (0041's reach table): `UNIT_WARRIOR` ->
`ART_DEF_UNIT__WARRIOR` (12 figures: members WARRIOR x6, _V2 x3, _V4 x3), `UNIT_HORSEMAN` -> `ART_DEF_UNIT_HORSEMAN`,
`UNIT_ARCHER` -> `ART_DEF_UNIT_ARCHER`, `UNIT_BARBARIAN_WARRIOR` -> `ART_DEF_UNIT_BARBARIAN_EURO`; the embarked
warrior is drawn as the embark model (0041 U02: 7,722 indexes, 2 instances). "M4@d" = one 4-frame series started d s
of normal play after the stimulus (one process), "S4" = four shifted singles (one process); `animations: true` (quick
combat and quick movement off) on every row with a move or an attack. Disk: 110 MB per capture plus 73 MB of
conversion XML (8.6), so about 0.19 GB per capture while both are kept.

| Seq | Units (owner, plot, facing) | Stimuli in order | Mode (processes) | Captures | Disk | Measures |
|---|---|---|---|---|---|---|
| Q1 idle with fidgets | 2 warriors, 2 horsemen (active, (41,12), (42,12), (43,12), (42,11), facing 2) | none; settle 3 s | M4@0 (done, 8.6) | 4 | 0.7 GB | K6 under the stall; X3 (how figures separate); fidgets seen |
| S-run | warrior (active, (41,11), facing 1) | `move` to (42,11), `repeat_delays_s` [0.05, 0.12, 0.2, 0.35] | S4 | 4 | 0.8 GB | X1 idle -> run (weight against the new clip's t); K6 at the frame cap (steps of about 33 ms, no clamp expected) |
| S-attack | warrior (active, (42,11), facing 1), barbarian warrior (63, (43,11), facing 4) | `attack` to (43,11), `repeat_delays_s` [0.05, 0.12, 0.2, 0.35] | S4 | 4 | 0.8 GB | X1 idle -> attack (and the defender's reaction) |
| Q4 melee attack | as S-attack | `attack` to (43,11) | M4@0, M4@1.2 (2) | 8 | 1.5 GB | the attack clip, the return, idle again; the defender's reaction; X2 (the A/B variant chosen) |
| Q3a short move | warrior (active, (41,11), facing 1) | `move` to (42,11) (one plot) | M4@0, M4@1.2 (2) | 8 | 1.5 GB | X5: run, stop (1440 / 2440), idle again; idle -> run in the series |
| Q3b long move with a bend | horseman (active, (40,13), facing 1) | `move` to (43,11) (three plots, the path bends) | M4@0.6 (1) | 4 | 0.8 GB | X5: run through the bend (turn clips where the machine has them) |
| Q2 fortify on, off | warrior, horseman (player 1, `moves: keep`, (42,12), (43,12), facing 2) | process 1: `fortify` both, M4@0; process 2: `fortify` both, wait 3 s, `move` the warrior to (42,11), M4@0 | M4 (2) | 8 | 1.5 GB | fortify entry (1500, 1.0 s) and fortify idle (1520); the exit from fortify by a move; K7 |
| Q6 death in combat | warrior (active, (42,11)), barbarian warrior (63, (43,11), `damage: 90`) | `attack` to (43,11) | M4@0.5 (1) | 4 | 0.8 GB | death A/B (1200/2200) and dead idle (1220/2220) per figure; X2 for the death variant |
| Q5 ranged attack | archer (active, (41,11)), barbarian warrior (63, (43,11)) | `range` to (43,11) | M4@0 (1) | 4 | 0.8 GB | the archer's attack clips (1100 family); the defender's figures that fall in combat |
| Q7 damage outside combat | warrior (player 1, (42,12)) | `damage` 50 | M4@0 (1) | 4 | 0.8 GB | X4: do figures that fall play the death clip |
| Q8 embark | warrior (active, (34,11), facing 2) | `move` to (33,11) | M4@0.6 (1) | 4 | 0.8 GB | the swap to the embark model and the embarked idle |

Total **56 captures in 14 processes** (Q1's 4 included; decision 9's cap is 60: 4 in reserve for one retake). Cut
from the first draft for the cap: a second series of the long move and of embark, and `kill` outside combat (Q7's
second stimulus; it needs a fifth capture in Q7's process, which the guard will not give). Peak disk about 10.5 GB if
nothing were deleted; the conversion XML is deleted after each scan and the captures after their table rows are
written, except a keep set of at most 6 (one per transition kind) named in the hand-back, so the steady use stays
under 2 GB (cap 25 GB). Order: S-run, S-attack, Q4 (X1's kinds and the attack first), Q3a, Q3b, Q2, Q6, Q5, Q7, Q8.
About 6 minutes per process (2 min to in game, 1 min of frames, stop and restore) = 85 min of game time, plus the
fits (below).

**Fits and the table** (every capture): `bones.py scan` -> `*.bones.json` (numbers only); `posefit.py fit --sampler cpp`
per (capture, model) over every clip of the member's `.fxsxml`; blocks whose best single fit leaves more than 0.5 deg
of rotation error go to `blendfit.py` with the two best clips; `track.py` joins figures across the frames of a series.
The table (CSV on `F:/c5-64bit-work/0374/4405/tables/`, never committed, D83) has one row per sequence, frame and
figure: sequence, frame id, capture frame number, Present time (us, process-relative), unit (type, owner, id), member
and figure (by placement), clip (file and ec through the member's `.fxsxml`), t, blend partner, its t and the weight,
rotation and translation residuals, runner-up and its cost ratio. Numbers summaries go into this section.
**Fit cost** (8.6, an indication from the workstation under load): the full search over all 33 warrior clips took
about 38 minutes for 4 captures with 4 units (4 posefit jobs in parallel beside 4407's own). Part B's driver therefore
first fits each block only against the previous frame's clip and its sockets' targets at the predicted t (previous t
+ the step) and runs the full search only where that leaves more than 0.5 deg; the full search stays the reference for
the first frame of every series and every S capture.

**Stop points** (the usage limit can stop the agent mid-run): every series is one process; a stop file
`C:/c5-64bit-work/0374/4405/STOP` is checked by the runner before each frame and by the driver before each step.
Resuming: read the issue log's last line; if a game of ours runs (`pool.py list`, pid on our claim), wait until the
newest autosave has stopped changing and stop it with `runner.py stop`, then `runner.py restore` with that series'
backup folder and `provision.py verify`; a series whose capture count is short is re-run whole from a new save copy
(a partial series is not merged into the table). Captures already scanned and fitted stay; the log names the next
sequence.

### 8.6 Part A: the idle series (Q1) [L]

**Run.** 2026-11-15, pool instance c5h-c (claim 0374), 32-bit DLL `i0ship-26234371-ia` named on the command line
(SHA-256 3C2F47DA...), CP/VP/EUI, stock config, frame cap 30, VT present, RenderDoc 1.47 by the worktree's 0302+
wrapper; save copy `C5H-0374-IDLE0301_0009` of `c5h-9028-tiny-t0008` (b2ceb663...dcc29163). Scenario (uncommitted
until part B): studio [40,10,45,13], two warriors at (41,12), (43,12) and two horsemen at (42,12), (42,11) of the
active player (Polynesia), facing 2, placed asleep in the placing turn (so not fortified), camera (42,12) zoom min-1,
settle 3 s, interface hidden, popups none, one request of 7 frames. Launch 21:03:41, in game 21:06:05 (guard self-test
ok, start-up watch ok: 860 polls, 0 seen), frame 21:06:30-21:06:58, harness stop 21:08 (no autosave in the run),
restore by SHA-256 21:11 (CP-mod DLL back to e589306e..., wrapper a62f2c11...), `provision.py verify c5h-c` ok, save copy
removed. DIAGNOSTIC shot `vc-20261115-210645-workstation-97b2`.

**Captures.** The guard stopped the request after 3 frames (largest free block 193.6 MB < floor 100 MB + 114 MB per
frame); a fourth frame was already started and was written: frames 1355, 1357, 1359, 1361 (110 MB each), Present
at +0, +1,760.3, +3,435.8, +5,005.7 ms (steps 1,760.3 / 1,675.5 / 1,569.9 ms). `bones.py scan`: 300, 291, 306, 306
draws; 33, 24, 39, 39 skinned; 162 instances each; 7-8 s per capture; conversion XML 73 MB per capture.

**Fits.** `posefit.py fit --sampler cpp --step 0.01` over all 33 warrior clips and all horseman clips, per model
(`warrior.gr2`, `_v2`, `_v4`, `horseman.gr2`). Table: `F:/c5-64bit-work/0374/4405/tables/Q1.csv` (one row per frame and
figure: frame, Present, figure, model, mesh, clip, ec, t, residuals, runner-up; numbers only, copied to
`render/evidence/0374/4405/Q1.csv` with the scripts `run_fits.py` and `table.py` that made it).

Warriors (24 figures = 2 units x 12, 96 blocks):

- **Every block is a single clip at one t**: rotation error at most 0.0016 deg over all 96 blocks; no blend residual.
  Clips seen: idle A (ec 1000), idle B (2000), fidget A (1040). Per frame 1355: 10 figures idle A, 11 idle B, 3 fidget A.
- **K6 under the stall** (69 steps of figures on the same clip in consecutive captured frames): the clip time
  advances **0.400 s** (0.39975-0.40022) between captured frames that are **two presented frames** apart while the
  Present step is 1,570-1,760 ms. So each presented frame advances clip time by **0.200 s** when the real step is
  longer: the clamp of `unit-playback.md` 1.5 holds for both the captured frame and the uncaptured one between (which
  also stalls while RenderDoc writes) [L]. Whether the step equals the real step below 0.2 s is S-run's question.
  (1.5 says "0.200 s of clip time per captured frame" for 3908's P01, whose captures were also two frames apart; the
  numbers there are 4407's to reconcile with these.)
- **Idle loops wrap in the same clip** (12 wraps across 6.0 s: idle A stays A, B stays B; `t` goes 5.6 -> 0.0 -> 0.4 and
  5.934 -> 0.334): no A <-> B transition (ec 1020/2020) and no fidget started at a wrap in these 1.2 s of clip time [L].
- **Fidget end -> idle A, no visible cross-fade** (3 figures): fidget A (5.0 s) at 4.831 in frame 1357 is idle A at
  **0.200** in frame 1359; at 4.899 / 4.826 in 1359 it is idle A at **0.200** in 1361. So the fidget passed its end on the
  uncaptured frame between, idle A began at 0 on that frame (the overflow past 5.0 s is dropped), and 0.2 s later the
  pose is idle A's alone (residual 0.0016 deg). Either the switch has no cross-fade or the two clips' poses are equal at
  the switch (fidget A ends in idle A's start pose [I]); a capture cannot tell these apart, which is why S targets
  idle -> run and idle -> attack [L for the observation, I for the reading].
- **Figure clocks (X3)**: the 24 figures have 13 distinct phases. Six figures across **both** units sit at exactly 5.200
  and four at 5.133-5.134 in frame 1355; others at 5.163 and 5.968 / 5.933; the rest scattered (1.033, 1.1, 2.3, 2.397,
  2.566, 2.696, 4.799). The groups differ by about one or two frame steps at 30 fps (0.033 / 0.067 s), so figures that
  started together keep one clock, and the two units placed by one Lua call started in step [L]; the choice of idle A or
  B is per figure (both occur inside one unit at one phase) [L]. Where the scattered phases come from (earlier fidgets,
  per-figure start offsets) needs the S captures, which see units from their creation.
- **Fidgets**: 3 of 24 warrior figures were in fidget A at the first frame (none in fidget B); with 5.0 s fidgets and
  6.0 s idles that is no estimate of the rate yet: X7's count needs singles spaced over minutes (part B, from the
  reserve if the cap allows).

Horsemen (2 units x 5 figures, rider `Horseman` and horse `HorsemanHorse` meshes; 80 blocks):

- Every block a single clip, rotation error at most 0.0047 deg. **All 20 meshes of both units at one phase**: t = 5.167
  in frame 1355, 5.567, 5.967, then **0.366-0.367** in frame 1361 (wrap of the 6.0 s idle); 14 meshes idle A, 6 idle B.
  **A rider and its horse share clip and t** (the three idle B riders and the three idle B horses) [L]. K6: 36 steps,
  0.39962-0.40011 s, the same rule as the warriors.
- No horseman fidget in these 1.2 s of clip time. Read with the warriors: the horsemen and the 10 warrior figures near
  5.17 (5.133-5.200, within one 30 fps frame of 5.167) are the figures still on the clock they got at placement; the
  scattered warrior phases are figures that left that clock, the three visible ones by a fidget whose end restarts idle
  at 0 [I: the earlier history of the scattered figures was not captured].

**Fit time** (indication, workstation, 4407's fits and other teams' jobs running): warrior.gr2 52 min (48 blocks x 33
clips), `_v2`/`_v4` 38 min (24 blocks each), horseman about 63 min (80 blocks; 4 jobs in parallel, 21:10-22:13).

**Kept** (part A): the four captures `c5h-c_20261115-210341_frame{1355,1357,1359,1361}.rdc` (438 MB), their
`bones/*.bones.json`, the fits and `tables/Q1.csv`; the conversion XML deleted after the scan. Captures used against
decision 9's cap: **4 of 60**. (Corrected 2026-11-16 00:48, part B: the four captures were deleted with the other
non-keep captures; their `bones.json`, fits and table stay. The keep set is in 8.12.)

### 8.7 Not verified (part A)

- The runner and Lua additions of 8.4 are a design: not written, not tested (part B, after the C1 answer).
- The stimuli of 8.5 beyond idle: fortify by mission in the placing turn, the `damage` and `kill` effects, the bend of
  the long move's path and the embark at (33,11) with quick movement off are read from the DLL source and 0041's run,
  not tried.
- That M4 holds for the other sequences: the guard's frame cost depends on the view (Q1: 114 MB per frame).
- K6 below 0.2 s (the step equal to the real step at the frame cap): S-run's question.
- X1 between poses that differ at the switch: no blend block in Q1 (S-run, S-attack).
- The fidget rate (X7): Q1 gives no estimate.

### 8.8 Part B: what was built and run (2026-11-15 22:20 onwards)

Written by the 4405 worker (part B granted by MC-B; lead's adjustments: a frame-cap arm for K6, a bystander beside a
unit in combat, fits only over the clips a state can reach, 4407's multi-start blend search for blocks no single clip
fits; MC #2023, #2063 (D155: dialog check at each turn while an instance is held)).

**Runner and Lua** (committed, `tools/scenario/runner.py`, `lua/action.lua`, `lua/place.lua`; README "Additions of
issue 4405"): the keys of 8.4 as designed (`moves: keep`, `damage`, actions `fortify` / `damage` / `kill`, `actions`
lists, `carry_objects`, `repeat_delays_s`) plus `frame_cap` (the guard's cap for one frame through
`tools/harness/framecap.set_fps`, checked against the guard's own reading and set back). A failed Lua answer fails the
frame before its capture and the run exits 1 (E7). Tests: `tools/scenario/tests/test_runner_4405.py`, 40 passed
(hermetic: validation negatives per key, the expansion of repeats, every action kind's Lua rendered and balanced, the
E7 path, the frame-cap guard check with a fake guard, and the seven older scenario files still valid); `runner.py
dry-run` of `units_0374.json`, `poses_tiny.json`, `units_0041.json` ok. (The landmark and wonder scenario files use
object kinds this runner refuses before and after 4405.)

**Driver** (`render/evidence/0374/4405/series.py`): per frame of `units_0374.json` one game process on pool instance
c5h-h (F:, claimed under 0374; c5h-c was taken): `runner.py prepare` (one backup folder for the whole series,
`C:/c5-64bit-work/0374/4405/b/backup/c5h-h`; pin `i0ship-26234371-ia` named; the save copy's SHA-256 checked before each
launch), a restore-list line in the issue log, launch, `run --frames ID`, harness stop, `bones.py scan` through
`quietlock.py run`, conversion XML deleted; at the end `runner.py restore`, `provision.py verify`, the save copy
removed. The save copy `C5H-0374-UNITS0301_0009` was copied into c5h-h from c5h-c's `c5h-9028-tiny-t0008` (the only
instance holding it), SHA-256 b2ceb663...dcc29163 checked. About 6 minutes per process (2 min to in game).

**Fits**: `render/evidence/0374/4405/fastfit.py` = posefit's own search (`fit_block_sampled`: grid, three zoom
rounds of 41 points, `finish_fit`) with each posesample call made once per (model, clip, round) for all blocks
instead of once per block. On SR30's warrior_v2 (12 blocks x 21 clips) it gives posefit's clip and t to the last
printed digit (t to 1e-5 s, rotation errors equal) in 56 s against about 6 minutes [L, workstation under load].
`fit_b.py` runs it per (sequence, model) over the clips the sequence's figures can reach (idle, fidget, run and
shuffle families for moves; idle, fidget, fortify and run for fortify; every clip for combat, damage and embark), grid
step 0.02 s.

### 8.9 K6 from the frame-cap arm (SR30 at cap 30, SR60 at cap 60) [L]

Shifted single captures of a warrior's one-plot move (41,11) -> (42,11) with an idle horseman of the same player at
(43,12) as bystander, delays 0.05 / 0.12 / 0.2 / 0.35 s after the Lua answer; SR30 at the default cap 30
(c5h-h, 22:36-22:38), SR60 with `frame_cap: 60` (guard read 60; frame numbers 1205 -> 4667 over about 60 s, so about
57 fps reached; 22:42-22:44). Every single capture is an ordinary frame: no stall precedes it.

- **Clip times are not on a 1/30 s lattice in ordinary frames.** SR30: the bystander horseman's 10 meshes share one
  clock per capture (all figures and both meshes at one t): 5.9998 / 0.0014 (on k/30), 2.7003 (on), **4.0415** (8.7 ms
  off k/30), **0.1544-0.1548** (12 ms off), **3.1083** (8 ms off); single-clip residuals 0.0000-0.0041 deg, t resolved
  to about 1e-5 s. SR60: warrior groups at 0.2712, 2.7132, 3.3262, 0.0713 s (off both k/30 and k/60; 0.6832 and
  3.2165 happen to sit on k/60). An accumulator consumed in whole steps of h would put every t on k*h for h = 1/30 or
  1/60: rejected [L].
- Reading [I]: the clock advances by the real frame time, clamped at 0.2 s (the original hypothesis of `unit-playback.md`
  1.5). 4407's lattice (65 of 72 groups within 1 ms of k/30, section 7.2) is what runs of exactly capped 1/30 s frames
  give; one longer frame (a harness shot, a Lua call held in the frame) moves a clock off the lattice by a non-multiple
  of 1/30, and the multi-frame captures then add 0.2 s per stalled frame. Sent as PREMISE to lead-0374 (posted to the MC
  as premise change 3, #2088).
- Not settled here: the zero-step frames of 4407 (P04: the attacker's figures 0.2 behind the defender's over two
  frames); Q4's bystander is the test (8.10).
- **Run starts: staggered entry, random start phase (MC #2089: kept as data for 0042, no guessed offset)** [L]. Per
  single capture, the 12 warrior figures in run and still idle, with e = wall time from the move order's Lua answer to
  the capture request, and the run clip times of the figures in run (`F:/c5-64bit-work/0374/4405/tables/runstart.csv`:
  per capture and figure-mesh the clip, t, residual, e and t - e, the frame number and Present time):

  | Capture | Cap | e (s) | In run / idle | Run t of the figures in run (s) |
  |---|---|---|---|---|
  | SR30 853 | 30 | 0.051 | 8 / 4 | 0.026, 0.130, 0.163, 0.249, 0.302, 0.343, 0.531, 0.538 |
  | SR30 1413 | 30 | 0.159 | 12 / 0 | 0.085 ... 0.641 (12 values) |
  | SR30 2045 | 30 | 0.217 | 12 / 0 | 0.001 ... 0.657 |
  | SR30 2584 | 30 | 0.352 | 12 / 0 | 0.069 ... 0.603 |
  | SR60 1205 | 60 | 0.052 | 4 / 8 | 0.058, 0.152, 0.450, 0.540 |
  | SR60 2342 | 60 | 0.120 | 8 / 4 | 0.069 ... 0.597 |
  | SR60 3516 | 60 | 0.201 | 8 / 4 | 0.071 ... 0.656 |
  | SR60 4667 | 60 | 0.351 | 11 / 1 | 0.000 ... 0.526 |

  Run times larger than e (up to 0.54 s at e = 0.05 s) cannot come from a run started at the order and running at speed
  1 [I: the run clip (0.667 s loop) is entered at a per-figure phase, as the shuffle is, 7.4]; and the figures enter run
  over about 0.1-0.35 s, not together. One capture per repeat gives the distribution, not one figure's history: the
  phase and the entry delay of one figure are not separable from singles (the run loops every 0.667 s). Run blocks fit
  0.002-0.24 deg (moving, turning figures; 4407 7.3).

### 8.10 The sequences: what was captured and what the EXE chose [L unless marked]

All on c5h-h (claim 0374), 2026-11-15 22:31 - 2026-11-16 00:13 EST, pin `i0ship-26234371-ia`, CP/VP/EUI, stock config,
frame cap 30 (SR60: 60), VT present, RenderDoc 1.47, quick combat and quick movement off (`animations`), save copy
`C5H-0374-UNITS0301_0009`; 14 game processes, 51 captures (+ Q1's 4 = **55 of decision 9's 60**). Every process was
stopped through the harness (no turn passed, so no autosave), the instance restored by SHA-256 after each driver run
(`runner.py restore` exit 0, `provision.py verify` exit 0, save copy removed) and released 00:14; the harness status
showed no dialog at any check (D155). DIAGNOSTIC shots: one per frame in Visual Checks (issue 4405), none undescribed.
**The address-space guard gave 2-3 frames per process in combat and damage views** (frame cost up to 196 MB: Q4a 2,
Q2a/Q2b/Q7 3), 4 elsewhere. Tables: `F:/c5-64bit-work/0374/4405/tables/table_4405.csv` (1,324 rows: per sequence,
captured frame and figure-mesh: Present time, model, mesh, clip, ec, t, duration, residuals, runner-up and its cost
ratio, yaw; Q1 included), `summary_4405.csv` (per sequence and model: clips with counts, residual maximum, the clip
steps of figures followed over consecutive captured frames), `runstart.csv` (8.9); per sequence `<SEQ>.csv`. Blend
fits of the blocks no single clip fits: 8.11. Of the 1,324 blocks, 1,260 fit one clip under 0.05 deg; 40 of the other 64 are run-family blocks of moving figures.

| Seq | Stimulus (frames) | What the figures play, in order |
|---|---|---|
| SA | warrior attacks a barbarian; singles 0.05-0.35 s after the order (778, 1629, 2147, 2727) | attacker: **combat-ready idle (1620)** at 0.27-1.30 s on 9-12 figures, or **shuffle (1450)** (one capture, 2147: all 12); defender: half its 10 figures in fortify idle (one clock), half in shuffle at per-figure t. No charge or attack clip within 0.35 s of the order |
| Q4a | the same, M from the order (662, 664) | attacker: shuffle on all 12 figures at 12 different t; defender: 5 of its 10 figures in shuffle, 5 in fortify idle (each barbarian figure is two meshes); bystander horseman idle |
| Q4b | the same, M from 1.2 s after the order (714-720) | attacker: charge run (1120), charge attack (1140), attack A / B (1100 / 2100), combat-ready idle, run back (the figures move: they are followed by placement only within a frame); defender: combat-ready idle, attack A, charge run / charge attack (its counter), **death A (1200) on figures that fall in the combat**, the rest fortify idle |
| Q6 | warrior attacks a barbarian placed at 90 damage; M from 0.5 s (795-801) | attacker: charge run, charge attack, combat-ready idle, run; the defender's death is not in the frames: only 2 alpha figures in combat-ready idle remain drawn (bravo and charlie gone) |
| Q5 | archer's ranged attack (688-694) | archer: shuffle and combat-ready idle (and run on 9 blocks, 0.13-0.51 deg: moving figures); no attack clip in the 1.2 s captured; defender: fortify idle, shuffle, combat-ready idle |
| Q3a1 | warrior moves one plot; M from the order (697-703) | idle A / B and the A -> B transition (1020) finishing, run (1400) on the figures as they start, then **stop run 1440 or 2440** |
| Q3a2 | the same, M from 1.2 s (705-711) | stop run -> idle: **every figure in stop run (1440) goes to idle A, every figure in stop run idle B (2440) to idle B** (21 of 21) |
| Q3b | horseman moves (ordered to (43,11), went to (41,11)); M from 0.6 s (689-695) | run (single clip, 0.001-0.025 deg, unlike 4407's P03 1-4.8 deg), then stop run 1440 -> idle A, `horseman_stpo_run_b` (the file's own spelling, ec 2440) -> idle B on all 10 meshes |
| Q2a / Q2b | `fortify` on a player-1 warrior placed with moves kept; Q2b then a move after 3 s | **no fortify clip**: idle A / B only on all 12 figures. The Lua answer was ok (CanFortify true, not moved, activity 0 = awake after the push); Q2b's move did not move the unit either (x, y and moves unchanged). Reading [I]: a mission pushed on a non-active player's unit during the active player's turn is queued but not started (`CvUnitMission::ContinueMission` runs in that player's update). Fortify on / off stays uncaptured (8.12) |
| Q7 | `damage` 50 on a player-1 warrior (697, 699, 701) | frame 697 (the first captured after the damage): all 12 figures drawn, all idle A / B; 699 and 701: 6 figures drawn, idle. **No death clip on any frame**: the falling figures were removed between 697 and 699 (0.4 s of clip time) without playing death A / B, which last 1.0 s [L for the captured frames; X4 as stated in 9 is not borne out] |
| Q8 | warrior moves onto the coast (33,11), embarks (Lua: embarked true); M from 0.6 s | only the embark model is drawn (index counts 7,722 and 510, in the scene and reflection passes); not fitted: its model file is not identified in the extraction (`transport.gr2` has 8,910 indexes) |

**K6, the per-unit skip (Q4a, the bystander arm the MC asked for)**: over the same two presented frames (662 -> 664,
Present step 923 ms) the attacking warrior's 12 figures advance **0.200 s** (0.19999-0.20143), the defending
barbarian's 10 figures **0.400 s** (fortify idle and shuffle alike) and the bystander horseman's 10 meshes **0.400 s**.
So the skip belongs to the unit given the action, not to the frame, as 4407's P04 showed [L]. In Q4b, Q5 and Q6 steps of
0.2 and 0.0 also occur on attacker and defender figures in combat (`summary_4405.csv`); every idle bystander and every
unit outside the action advances 0.400 in every pair (Q1, Q2, Q3a, Q4a/b horseman, Q7).

**Clip end -> next state [L]** (Q1 fidget, Q3a2 stop run, Q3b): the next state's clip starts at 0 on the frame on which
the old clip's time passes its duration (the overflow is dropped) and advances from the next frame: stop run 0.933
(707) -> idle A 0.200 (709); stop run 0.666 (705) -> idle A 0.000 (707); fidget A 4.831 -> idle A 0.200 two frames on.

**X3 [L]**: a unit-level state change resynchronises its figures (Q3a2: after stop run, the figures are on two or three
clocks; Q4a/SA: shuffle at per-figure t); the stop variant (1440 / 2440) fixes the next idle variant per figure.

### 8.11 X1 and the From clip's time (MC premise 4) [L]

4407's multi-start two-clip search (`render/evidence/0374/4407/blends.py`, local-space blend, grid 41 x 41 x 21 then
Nelder-Mead from 8 starts) on two sets, 2026-11-16 00:31-00:47 (`F:/c5-64bit-work/0374/4405/blends/`):

- **The 24 worst non-run blocks** (`blend_b.py`: pair = best and runner-up single clip): 22 converge to w >= 0.995 on
  the single clip (no blend). The other two are Q4b barbarian figures in death A at t = 0.20-0.21 s with 17.6-23.5 deg
  against every clip of the member and 0.53-0.62 weight on death A against fortify / fortify idle at best: **not a
  two-clip blend; unexplained** (with 4407's P05 818 id 12 at 35 deg, the dying barbarian figures are the one block kind
  the clip set does not reproduce) [L for the residual, the cause open: a ragdoll or physics pose, a third clip, root
  motion].
- **Run -> stop, 6 blocks** (`blend_x1.py`: From = run over its whole loop, To = stop run in [0, 0.4]): the stop clip at
  **t = 0.2000 alone, w = 1.000** (run's weight 0) on all 6; the residual stays **0.2008-0.2010 deg** on 5 of them (equal
  across figures, so systematic, not noise; the Q3a2 block 0.070). So **no run -> stop cross-fade is visible one frame
  after the switch**, and the From clip's time t1 is undetermined there (its weight is 0).
- **The shuffle's loop seam, 18 blocks** at shuffle t = 0.02-0.15 s with 0.05-0.10 deg (Q4a, Q5, SA): From = the same
  shuffle at t1 in [0.4, 0.667], To = the shuffle at t2 in [0, 0.25]: w = 1.000 on 17 (no gain), 0.979 on one (Q5 690
  archer, t1 0.631, t2 0.021, 0.061 -> 0.049 deg). So the seam residual is **not a cross-fade of the shuffle's end into
  its start**; 4407's reading (7.1, a blended seam over 0.1-0.15 s) is not confirmed by a two-clip fit [L].
- With part A: fidget A -> idle A (Q1) and stop run -> idle (Q3a2, Q3b: pure idle at t = 0.000 on the switch frame) show
  no blend either. **The only state cross-fade measured so far stays 4407's P01 4681 (idle A -> fidget A, w 0.680 at
  0.2 s, old pose held)**. Idle -> run and idle -> attack were not caught mid-switch: S-run's singles found the figures
  already in run (or still idle), SA's found shuffle / combat-ready idle, no attack clip within 0.35 s of the order.
  For MC premise 4 the answer from this data is "no From weight on any captured switch except 4407's one block", t1
  per block in `blends/x1_*.json` (every row: t1, t2, w, residual, frame, Present time via the table).

### 8.12 Not verified, cut, and the keep set (part B)

- **Fortify on / off: not captured.** A mission pushed on a non-active player's unit in the active player's turn does
  not start (Q2a, Q2b: activity stays awake, no fortify clip, the move in Q2b did nothing) [L]; an active-player unit
  keeps auto-selection and `quiet.lua` calls `FinishMoves` on it. Ways for a successor: the turn-start fortify of VP
  (`CvPlayer` sets `m_bFortified` on every unit that did not move, 8.1; an end turn under the harness) or exempting
  one placed unit from `quiet.lua` with the camera read after each step (E9).
- **Embark**: the embark model is drawn (Q8) but not fitted: its model file is not identified. *Corrected 2026-11-16
  (0395, 17.3): it is the Polynesian war canoe (`ART_DEF_UNIT_U_POLYNESIAN_WAR_CANOE`, the player's embarked graphic
  override) [C]; Q8's 8 blocks are fitted and scored. Fortify on / off and the aim unit: captured and scored in 17.2 and
  17.4 (the active player's unit placed with its moves after the camera step).*
- **The death clip**: in combat only on Q4b's figures (death A, partly unexplained above); Q6's defender was gone
  before the series (started 0.5 s after the order). Outside combat (Q7) no death clip.
- **The aim unit (MC #2089, off-centre aim for 4410's masks)**: not attempted: it would have cost an item within the
  cap (55 of 60 used).
- **Idle -> run and idle -> attack mid-switch**: not caught (8.11). Singles at 1-frame offsets right at the switch
  (4407's 7.3 list) would need the switch frame known; the order's Lua return is not that frame (run starts staggered).
- K6's skip rule: which of the two frames the attacker loses (the order's or the next) is not separable from
  captures two frames apart; a skip appears also inside combat on both sides (Q4b, Q5, Q6).
- The 0.201 deg residual of stop run at 0.2 s (5 blocks) and the 0.05-0.10 deg near the shuffle's start: unexplained.
- **Keep set** (decision 9): captures `F:/c5-64bit-work/0374/4405/captures/` of Q4a frames 662 and 664 (the skip),
  Q3a2 705 (stop -> idle pairing), Q4b 716 (the death block), SR30 853 and SR60 1205 (the frame-cap arm); every
  other capture deleted after its `bones.json` and table rows were written. The `bones.json` of every capture stay
  (numbers, a few MB each), so every fit can be redone without the captures.


| # | Premise | Mark now | Test, and who |
|---|---|---|---|
| K6 | The clock. **Revised again 2026-11-15 23:19 (MC #2089, from 4405 B's single captures at caps 30 and 60: clip times off the 1/30 lattice [L]; replaces #2023's "a per-unit accumulator consumed in whole steps of h = 1/30 s, at most 6 per frame", which replaced the first "frame's step clamped at 0.2 s")**: per unit, t += min(dt, 0.2) per frame [I: the reading that fits every observation: under capture every figure advances 0.2 s or 0 s per presented frame [L], and runs of capped 1/30 s frames give 4407's lattice]; the cap is a parameter; a per-unit skip rule (a unit advancing 0 over a frame) is held as data and is open. DoD C's free start offset absorbs only a skipped step at an action's start; every other skip is reported per figure; at the MERGE-REQUEST the skip rule is explained [L] or named open with its count | [L] partly | 4407 on the old recordings (every group with a known Present step); 4405 B on shifted single captures at the frame cap (steps of about 33 ms, no clamp expected) and multi-frame series (clamp expected); a step rule that fits every observation, with spread |
| K7 | Transitions follow the files: the state a figure is in, and when it leaves it, are what the machine's sockets and the graph's sources give for the events applied | [I] | 4405's sequences against the controller per frame (DoD C); a failure is reported at once with the frame and figure |
| X1 | The cross-fades. **Added 2026-11-15 23:58 (MC #2117, from 4406 round 2 on P01 4681): a From-side source with ContinueOffset 1 starts at the previous state's clip time and is held there for the fade, as data per graph role [L one point, I in general], to be confirmed on 4405 B's transitions before the MERGE-REQUEST; the EXE-level fade list is empty by default (lead 22:58, MC #2089).** **Revised 2026-11-15 (MC #2023):** the graphs' own fades exactly as read (4408 6.3: transition graphs with a ContinueOffset "From" side and a hint-5 fade of 0.1-0.33 s), plus an EXE-level state cross-fade only where a recording shows one, held as data per transition kind, never guessed; so far idle -> fidget, about 0.3 s linear, old pose frozen when the new clip starts (4407); idle -> run and combat ready -> charge run show no fade on their first captured frame | [L] partly | 4407 (P01 4681, the 4353 mech blocks), 4405 B (idle -> fidget, idle -> run, run -> stop, idle -> attack at many offsets); DoD D |
| X2 | Path option 4 carries the requested action's ec; hint 7 callbacks return a path option's value, hint 5 a time in seconds (the BlockCurve ramp length), hint 6 as read by 4408 | [I] (`model-reader.md` 6.4 (b), (c)) | 4408 from the files (every use consistent); 4405's attack and death sequences (the variant chosen equals the option set) |
| X3 | The figures of a unit enter a state together and keep one clock unless a random choice or a figure-level event separates them [9 of 12 fortified warriors at one t, `unit-playback.md` 1.3] | [L] one capture | 4407 across P01-P05; 4405's idle series (how fidgets separate figures: one roll per figure or per unit) |
| X4 | **Corrected 2026-11-16 01:03 (premise change 5, MC #2147):** figures that fall from damage outside a combat leave the drawn set at once, with no death clip and no dead idle (`Request::removes`); deaths in combat stay as 4404 has them, the defender's death in combat not caught (open). Scope: one warrior, one damage step (50), captures 0.4 s apart, so a death clip shorter than 0.4 s is not excluded; the removal reaches the picture 1-3 frames after the Lua call (15.9). (Was: "play the death clip (and then dead idle) and are then removed from the drawn set by 0041's rule", [I].) | [L] | 4405 B's Q7 (8.10), scored in 15.9 |
| X5 | A move plays start-run (where present), run, turns (1460 / 1480) and stop (1440 / 2440) as the machine's sockets order them on the move and arrive requests | [I] | 4405's short and long move with a turn |
| X6 | Each DLL `GameplayUnit*` call maps to one request of section 1 (6.4 (g)) | [I] | 4408 from the files and `EnableActions`; 4405's sequences for the ones scripted; the rest named open |
| X7 | Random choices: `RandomRoll` is a per-update chance with the file's value; `EventRandomPathOption` draws by the file's weights | [I] | DoD D in distribution (seeded offline runs); the live idle series' fidget count beside it |

A premise that fails against the recordings is reported to MC-B at once (the brief's Reporting).

## 10. The oracle plan and its tolerances

The oracle set is the brief's (Rules, D137 rule B): 4405's new table, 3907's exports and their fits (4153, 0041's P9),
3901's capture, 3908's P01-P05 with Present times, 0041's 4353 capture, and 0041's `oracle.py` with its last report as
the regression floor. One command (`render/tools/unitanim/oracle_anim.py`, 4406) runs all of it with no game instance.

- **C, clip choice and time (per sequence, frame and figure).** The driver plays the sequence's `.c5anim` (its events
  written from the scenario's stimuli, its `dt` from the recorded Present steps) with **one free start offset per
  sequence** (the frame on which the EXE began the action, fitted once) and the EXE's random choices forced from the
  table (`choice` records). Pass: the same clip (ec), or the same blend pair, on every figure whose fit is unambiguous;
  **clip time within 0.005 s** where the controller's step equals the observed step (after K6), else within half the
  frame's step (proposal; the 0.005 s covers the fit's own resolution, `unit-playback.md` 1.3); **blend weight within
  0.02**. Per sequence a table of the figures not covered with a cause each.
- **C on the catalogue.** For 3907 and 3901 (1,479 + 19 unambiguous fits): each fitted clip lies in the set the
  controller's default state can reach for that unit's flags (idle, fortified, embarked), and the controller in forced
  mode with the fit's start reproduces clip and t (t within 0.005 s).
- **D, clock, cross-fades, random parts.** K6 with numbers (4407, 4405 B). X1 and the in-graph cross-fades on at
  least three transition kinds (idle -> fidget, idle -> run, idle -> attack; run -> stop if captured): the
  controller's weight curve within 0.02 of each measured point. Random parts over seeded offline runs of 10^5 updates
  per decision point: each `RandomRoll` rate and each `EventRandomPathOption` weight within a 99.9 % binomial interval
  of the file's value; the live idle series' fidget count reported beside the expected count.
- **E, pose and pixels.** The palettes from the controller's programs through 0041's module equal the captured
  palettes within 0041's tolerances (rotation element < 1e-3, translation < 0.01 world units), cross-fade blocks
  included; draws per COMMON 25 with 0041's floor; `oracle.py` on the whole catalogue gives its merged numbers
  unchanged.
- Report: per sequence, frame and figure (C), per transition (D), per unit and pass (E), with the commands, the
  inputs' SHA-256 and the run time; numbers only (D83).

**The definition of done, made final at C1 (the brief's A-I with these changes):** A as the brief, this file being the
design. B as the brief, plus the counts per file kind, node type, condition type and trigger type of 4408's 6.1 as the
census to equal. C and D with the tolerances above (0.005 s or half a step for t, 0.02 for w, 99.9 % binomial
intervals for the random parts). E, F, G, H, I as the brief; F's statuses are those of 5; G includes the `.c5anim`
reader's negative test per refusal code; H adds `unit-controller.md` sections 6-8 written by the workers. Not covered
by this epic and named so: multi-bone degree 3 on a capture (0379), the Action trigger's consumer (0042), the server's
frame step (U6).

## 11. Decisions at C1

Taken by the lead (the MC informed) and asked of the MC; the MC's answers are quoted here when they come.

1. **Where the code lives** (lead's): as 5: readers in `models/`, runtime `c5r::unitanim`, `pose.h` additive
   (`rowsProgram`), `.c5units` stays format 1. Taken.
2. **The input and its file form** (MC's): sections 1-3; `.c5anim` format 1 is what U6 and 0042 fill. Asked.
3. **The live oracle without U4** (MC's): (a), RenderDoc only (4405), as recommended; (b) only if (a) cannot answer K6
   or X1, time-boxed 2 h, after the layout is written as a description first. Asked; the MC already allowed one idle
   series under (a) before C1 (#1985).
4. **What faithful means for animation**: the MC holds it within D66 (issue log 20:40, #1978); DoD C-E are written to
   it.
5. **Random choices** (lead's): seeded per unit (the unit's `(player, id)` mixed with the scene seed), forced mode
   replaying the recorded choices. Taken.
6. **The boundary with 0042** (MC's): as the brief: U5 accepts the requests of section 1 (with figure sets) and emits
   triggers; 0042 decides when, for which figures, the turns along a path, the positions and the queue. Asked.
7. **Failure policy for U6** (MC's): as 0041's decision 7; the extended pre-check at game start takes D125's route, a
   status in play is an R6 fault (logged LOUD, counted, that unit not drawn); no fixed idle fallback. Asked.
8. **The clock** (MC's): the controller takes `dt` from its caller and applies K6's rule itself. Asked.
9. **Live recordings' place and caps** (MC's): one pool instance under 0374, RenderDoc on Tiny, at most 60 captures
   and 25 GB on `F:/c5-64bit-work/0374/`, captures removed after their tables are written except a keep set the
   hand-back names, harness stop only, SHA-256 backups and restores. Asked.
10. **Triggers beyond Action** (MC's): emitted as named events, no consumer. Asked.
11. **New (lead's):** two children added before C1 so the note rests on numbers: 4407 (the recordings already made:
    K6, X1) and 4408 (the files read as behaviour). Taken; told MC-B in the BREAKDOWN.
12. **New (MC's): the per-figure output is a pose program, not only `(clip, t)`** (4). It keeps `units.h` unchanged
    and makes blends exact; the cost is one additive entry in `pose.h`. Asked with the input (2).

## 12. The readers and the census (4402)

Worker of 4402 (account B), 2026-11-15 21:38-22:05 EST. **Result [C]: the C++ readers read every logic file the VP
database reaches (15 `.fsmxml`, 112 `.dge`, 268 `.ftsxml`), and every copy in the install's 147 packs and the MODS
folder (70 / 309 / 691), with no refusal. Their canonical dumps are byte-equal (SHA-256 per file) to an independent
Python reader's on all 395 + 1,070 files. The counts per condition, node and trigger type equal 6.1 for the reached set
and 3100's census for the archive set.**

**Code** (`models/`, C++14, own code): `include/c5models/logic.h` (the lead's data model; fields added, none changed:
`StateMachine::attrs` / `lenience`, `TriggerSystem::attrs` / `lenience`, `DagNodeType::Layer`, the `DagNode` data
fields every type stores (`external_*_edge_count`, `has_node_data`, `data_edge_count`, `event_track_index`,
`event_track_specified`, `add_clamp_offset`, `has_continue_offset`, `edge_currents`, `callback_string_hint`,
`has_callback_value`, `scalar_min` / `scalar_max`, `subnodes`, `min_duration`, `items`, `min_items`), a `parse_dge`
overload with a string database, and `cond_type_name` / `node_type_name` / `trigger_type_name`); `src/logic_xml.cpp`
(`.fsmxml`, `.ftsxml`), `src/logic_dge.cpp`, `src/logic_dump.cpp` (the canonical text; its format is in the file's
head comment); `art.h` / `src/art_xml.cpp`: `XmlLenience` and a `parse_xml` overload that counts it (the old
signature is unchanged). Test `tests/test_logic.cpp`. Tool `tools/logicdump/`: `logicfiles.py` (copies the files out:
set `reached` through the database and `fpkscan dups`, set `all` = every copy), `logicdump.exe` (C++ dumps and
census), `logicdump.py` (Python dumps with ElementTree and one-off 9012's `gr2read.py`, and `compare`),
`subset_counts.py`.

**Reader rules** [C, tests]:
- **Strict structure.** `.fsmxml`: root `statemachine` (`default_state` required) with one `states` (required) and at
  most one `sockets`; `state` (`id` required) has no children; `socket` (`from`, `too` required) has at most one
  `conditions` of `condition`s (`type` required); a condition has at most `weights` of `weight` (`v`, `w` required).
  `.ftsxml`: root `trigger_system`, at most one `event_tracks` (of `event_track`, `ec` required, of `track`, `id`
  required) and one `triggers` (of `trigger`, `type` required, no children). Any other element: `Err::Xml` with
  the line. A socket's `conditions` attribute, when present, must equal its count. Integers must be integral numbers.
  Booleans 0 / 1 / true / false. Numbers: `strtod` then rounded to float.
- **Typed fields per condition type** as 6.2 reads them. Path-option values are read as numbers: `fvalue` is the
  number, `value` its nearest integer (the leader machines write 1.1 and 1.000000 on path 4; no unit machine does).
  Unknown condition, trigger and node types are kept by name (`Unknown`), not refused: the runtime refuses them.
  Every attribute is also kept as written (`attrs`).
- **The one lenience (MC #2017)**, in `parse_xml`, counted per file: a comment ends at the first `-->` (a body that
  holds `--` is counted), and character data is kept as text, never an error (elements with text are counted). In
  the files: 1 file (`edited_fx_triggers_missilecruiser.ftsxml`, 2 such comments); no logic file has text. Every
  other malformation tested is refused: an unterminated comment, unquoted or duplicate attributes, a mismatched end
  tag, a second root, an unknown entity.
- **`.dge`**: through `RawFile` (all unit and VP graphs have inline strings, header 0x58 = 0); a NULL DAG, node,
  edge, block or source entry, or a root without `DAGs`, is `Err::MissingMember`; the .gr2 reader's codes otherwise.
  Node data: per type as 6.3; `PoseAnimSource` takes `Edges[0]` (one edge in every file); `ContinueOffset` and
  `CallbackValue` are absent from some leader files' types (`has_continue_offset`, `has_callback_value`). Editor
  positions, `InstanceDataSize`, `InstanceOffset`, `ForwardIndex` and `Forwarded` are not read (run-time or editor
  values, 3100).

**DoD B numbers** (`models/evidence/0374/4402/`; files copied by `run_files.sh` to `F:/c5-64bit-work/0374/4402/`
(not committed, D83); `run_cpp.sh` runs `logicdump.exe` through `quietlock.py run` and the comparison; c5h-c's install
and MODS, the 0327 database copy, `fpkscan` of render build `2c41569e31`):

| Set | Files (fsmxml / dge / ftsxml) | Refused | Dumps equal C++ = Python | Counts |
|---|---|---|---|---|
| reached (6.1) | 15 / 112 / 268 | 0 | 395 / 395 | states 299, sockets 716; conditions and nodes as 6.1; triggers Sound 11,625, Effect 5,852, Action 1,046, Transfer 576 |
| all copies (147 packs + 7 loose + MODS) | 70 / 309 / 691 | 0 | 1,070 / 1,070 | node types also Layer 14, ScalarSelect 1 (leader graphs) |
| of these, the archive copies only | 69 / 288 / 666 | 0 | | **= 3100's `logic_census.out.txt`** (CheckPathOption 1,938, EventSetPathOption 2,062, OrOperator 843, EventRandomPathOption 634, ECComplete 445, CheckStateTime 107, RandomRoll 17; 620 states; Sound 18,737, Effect 7,042, Action 1,278, Transfer 603) |
| of these, UnitModels' 123 graphs | 0 / 123 / 0 | 0 | | **= 3100's `dge_census.out.txt`** (PoseAnimSource 698, ScalarCallback 360, Selection 276, Blend 182, Subgraph 123, BlockCurve 59, Trackmask 31, ScalarSource 31) |

`*_compare.tsv` holds per file: index, kind, size, the file's SHA-256, the C++ dump's and the Python dump's SHA-256,
equal. `*_census_{cpp,py}.txt` are the two readers' counts.

**Tests** (DoD G) [C]: `tests/test_logic.cpp`, 166 checks: the typed fields of a synthetic machine (all 7 condition types and an unknown one), trigger file and `.dge` (built with `gr2_writer.h`: Subgraph, PoseAnimSource, ScalarCallback, an unknown node type, a NULL NodeData); the canonical dump text; **48 refusals**, each asserting `Err::Xml` (or the .gr2 codes), a message and a cleared output: 27 for `.fsmxml` (malformed XML, wrong root, missing required attributes, non-numbers, non-integers, bad booleans, unknown or doubled elements at every level, a wrong `conditions` count), 12 for `.ftsxml`, 6 malformations the lenience does not cover, 3 for `.dge` (not a Granny file, truncated, no `DAGs`); and the lenience itself (a nested comment drops what it covers and is counted; stray text is counted). `python models/build.py --test`: 8 / 8 pass (2026-11-15 21:53). `--asan` (cl.exe `/fsanitize=address`): 8 / 8 pass, no sanitizer report and no compiler warning in the new files (21:58); the ASan `logicdump.exe` over the reached set: 0 refusals, no report, 395 dumps byte-equal to the Release build's (`run_asan.sh`). Over all 1,070 copies (`run_asan_all.sh`): the same, 1,070 dumps byte-equal (22:02).

**For 4403 and 4409:** `logic.h` is final for the readers; the runtime reads `FsmCondition::fvalue` for path values
(PathOptions are float, paths 0-8), `value` for codes. `DagNode::type == Unknown` and `!has_node_data` must be
refused by the evaluator with a named status. The reached unit graphs have no Layer or ScalarSelect node.

## 13. The runtime (4403)

### 13.1 What is built, and the rules it applies

Written 2026-11-15 22:00-22:22 EST by the 4403 worker (branch `task-4403-runtime`, which merged 4402's readers
`origin/task-4402-logic-readers` @1c964ba6). Synthetic tests only; no game files and no game run.

**Files.** `render/include/c5r/unitanim/` `clock.h`, `random.h`, `machine.h`, `logic_cache.h`, `controller.h`;
`render/src/unitanim/` `clock.cpp`, `random.cpp`, `machine.cpp`, `logic_cache.cpp`, `controller.cpp`, `status.cpp`;
tests `render/tests/unitanim/test_clock.cpp`, `test_random.cpp`, `test_machine.cpp`, `test_controller.cpp` with the
helper `anim_testkit.h` (hand-built machines, a graph double for 4409's evaluator, an art double, an in-memory file
provider).

**The frame** (`Controller::apply`, sections 1 and 9): (1) the frame's events in order; (2) per unit, graphless
states choose at once with no time ("settle": the default chooser after a create, reset or embark), so a `choice`
record in the same frame as the `create` reaches the creation's draws; (3) the unit's clock gives whole steps;
(4) per step and living figure: the current graph advances by h, the state time (and a running cross-fade's time)
by h, the triggers crossed in that step are emitted, then **one machine update**; (5) the programs are collected.
Consequences [I, chosen to fit 4407's 7.3]: a state entered in a step shows its new clip at t = 0 in that frame
(P02 idle -> run, P04 combat-ready -> charge run, P05 charge run -> shuffle all show t = 0.0000), and `RandomRoll`
is a chance per step of h (so the omni idle's 0.0007 means one fidget-or-swap per about 48 s at h = 1/30).

**The clock** (K6 as revised, MC #2023; `clock.h`): per unit an accumulator of the frame's `dt`, consumed in whole
steps of h = 1/30 s, at most 6 per frame; on reaching the cap the rest of the accumulator is dropped (this gives
P04's 0.200 s per captured pair: 6 steps on the stalled frame, 0 on the short one). h and the cap are
`ClockParams`. The skips 7.2 leaves open are data: `SkipRule` (frame seq, unit, figure or all, steps removed) and a
`StepHook` the caller may implement; neither is used in free running. A non-finite or negative `dt` is `BadEvent`.

**The machine** (`machine.h`; 6.2 as read by 4408): sockets of the current state in file order; a socket is an OR
of AND groups split at `OrOperator`; tests run in file order and stop at the first that fails (so a `RandomRoll`
after a failing test draws nothing); the first socket with a holding group is taken, at most one transition per
update; only the holding group's events are applied, in file order, after its tests; an empty group holds.
`CheckPathOption` methods 1 (equal) and 6 (not equal) on the float path value; `CheckStateTime` methods 5 and 1 as
time in state >= value (a 1e-6 s tolerance for whole steps against the file's float); `ECComplete` = the time in
the current state is above the duration of the member's clip for that ec (0 when the member has none), no graph
looked at (*corrected 2026-11-21 by W40 for ticket 4738, from 13.5 (W42's reading of build 4390913) [C];
was: "the current state's graph has a source of that ec on its selected path and `complete()` holds", which froze a
figure in a state whose graph selects another branch, e.g. the omni ENTER_BOMBARD_DEFEND entered from fortify:
`unit-combat-server.md` 18.12. The controller keeps the old walk only for a graph factory that knows no clip
durations, the test doubles*); `RandomRoll` a chance per update,
`invert` negates; `EventRandomPathOption` by the weights (useweights 1) or uniformly over 0..value (useweights 0;
the 13 sockets with 0.6 / 0.4 under useweights 0 remain the live check of 6.2). Bind-time refusals as section 5:
`BadSocket`, `UnknownCondition` (Unknown, AnyComplete, EventCopyVariable, EventSetVariable, Variable, True, False,
other methods), `BadLogicFile` (path out of 0..8, no positive weight, negative value, a chance outside 0..1).

**Decisions taken here** (the lead may overrule; none changes a premise of sections 0-5):
1. A state without a graph (the choosers) keeps the previous graph **held, not advanced**, and its sockets are tried
   again at once (up to 16 in a row; a figure that reaches no graph is `NoProgress`). Holding fits P01's figure that
   lost one 0.2 s step on the frame its fidget was decided [I].
2. A unit starts with path 4 = 1000, or 1500 with the fortified flag (6.2: "a unit at rest has 1000 there"; the omni
   default state tests 1500); the civilian-family machines write it themselves.
3. **Forced mode:** an `EventRandomPathOption` without a `choice` record is `ForcedChoiceMissing`; a `RandomRoll`
   without one **does not fire** (a "no" is every update's outcome and recordings name firings only; a record 1
   fires it, 0 consumes a "no"). A record outside the option's values is `BadEvent`. Records are queued per figure
   and point (`roll:<machine>:<socket>`, `option:<machine>:<socket>`, socket = file-order index) and also used in
   free mode when present.
4. `ECComplete`'s optional fraction (0.98, the missile machine, 3 uses) is read as complete [I]: `ActiveSource`
   carries no duration. Asked of 4409: an additive `duration` in `ActiveSource` would let the runtime test 98 %.
   **Confirmed in effect 2026-11-21 [C, 13.5]: the EXE reads and writes the value (default 0.98) but its test does not
   use it; there is no 98 % to test.**
   The controller ignores it too (4738, W40).
5. **X1 between states** is data (`CrossFadeRule`: machine file, from-state id, to-state id, seconds; linear;
   the last program frozen, the new state's program fading in by a final Blend node). `defaultCrossFades()` holds
   one rule, `1_omni_state_01.fsmxml` 6 -> 3 (the fidget chooser into the fidget), 0.3 s, from 4407's P01 point; no
   other kind has one. Fades inside the graphs are 4409's.
6. **Triggers** (`.ftsxml`, 6.4): a trigger fires when its source's clip time crosses its time within a step,
   `[prev_t, t)`, and across a wrap `[prev_t, end) + [0, t)`; keyed by its `type` attribute (`TriggerType`); a type
   the reader keeps as Unknown is `UnknownTrigger` for the member. A trigger whose ec no source plays never fires and
   raises nothing (trigger files are shared and name codes a member may lack).
7. **Events** until 4404's bridge: `EventBridge` is the interface; `BasicBridge` maps section 1's codes as far as the
   files fix them (fortify 1500 / 1000, move 1400, arrive 1000, destroyed and kill 1200, the action codes of 6.5) and
   refuses work and paradrop (`BadEvent`, no code in the files). A path-4 request whose value the member's machine never
   tests with method 1 is `BadEvent`. `reset` and `place` re-create the unit (default state, time 0); `embark`
   re-creates it with the flag (the art follows the flags); `garrison`, `damage` update `UnitInput` only; `choice` and
   `steer` (paths 7, 8) as section 1.
8. Living figures: `UnitInput.alive` (>= 0) keeps the first `alive` figures in member order, the dead flag none;
   `kill` and `destroyed` mark figures dead, and they keep their program (the death clip) until `remove`.
9. Graphs come through a `GraphFactory` the caller gives (`bindGraph` of 4409 in the product). The logic cache reads
   each `.fsmxml`, `.dge`, `.ftsxml` once through the file provider (lower-case basename); `put*` gives parsed data
   directly (tests).
10. Random: one xoshiro256** per unit seeded with splitmix64 of (scene seed, player, id); figures draw from their
   unit's generator in figure order, one step at a time (deterministic per seed).

**Pre-check** (`checkArt`, `precheck`; DoD F): per art row and member, the machine (and its bind refusals), every
graph of every state **reachable** from the default state bound through the factory (EcNotBound and UnknownNode are
the evaluator's), every trigger file, and `NoProgress` for a reachable state with neither a graph nor a socket out.
An unreachable dead end (omni 28) is not refused.

**Tests** (`render/build.py --test -R c5r_unitanim`, 2026-11-15 22:15: 5 of 5 passed, 0 skipped; ASan below):
test_clock 5 tests (frame cap, accumulation, the cap's dropped rest, parameters, refusals); test_random 5 (per-unit
seeds, uniformity, roll rates and weights over 10^5 draws, the forced queue); test_machine 11 (binding, the update
rule's parts, distributions); test_controller 23 (whole-step time, per-figure seeded variants, figure start times,
fortify and attack through events, figure sets, the X1 rule with w = 0.2 / 0.3 after 6 steps and its end, triggers
once with their offset, skip rules and the hook, steer / reset / remove, forced choices, 4402's reader through the
file provider, the pre-check, and one negative test per status the runtime raises: MachineNotFound, GraphNotFound,
TriggersNotFound, BadLogicFile (machine, triggers, graph), UnknownCondition, UnknownTrigger, BadSocket, EcNotBound and
UnknownNode (passed through from the factory), NoProgress, ForcedChoiceMissing, BadEvent, UnitFailed; each asserting
the code, a text and no program for the unit).

**ASan** (`render/build.py --asan --mem-cap-mb 4096 --test -R c5r_unitanim`, MSVC cl, 2026-11-15 22:19): 5 of 5
passed, 0 skipped, 0 ASan reports, peak 82 MB. The plain run again after the last change (22:20): 5 of 5.

**DoD D's distributions** [C, seeded synthetic runs through `updateMachine`; `test_machine`'s output, 2026-11-15
22:20]: 10^5 updates or draws each, pass = within the 99.9 % binomial interval (z = 3.29).

| Decision point (as the files use it) | Observed | Expected | Interval half-width |
|---|---|---|---|
| `RandomRoll` 0.0007 (omni idle) | 74 fires | 70.0 | 27.5 |
| `RandomRoll` 0.0001 (fortify, civilians) | 9 fires | 10.0 | 10.4 |
| `EventRandomPathOption` useweights 1, 0.6 / 0.4 (idle A/B) | 60,140 x value 0 | 60,000 | 510 |
| `EventRandomPathOption` useweights 1, 0.3 / 0.7 (fidget / swap) | 69,959 x value 1 | 70,000 | 477 |
| `EventRandomPathOption` useweights 0, value 1 (uniform 0..1) | 50,239 x value 1 | 50,000 | 520 |
| `EventRandomPathOption` useweights 0, value 2 (uniform 0..2) | 33,343 x value 2 | 33,333 | 491 |

These check the runtime against the files' values; the live idle series' fidget count (4405) is the check of the
values against the EXE (X7).

**Not verified here:** anything on real logic files beyond 4402's reader on a synthetic `.fsmxml` (the runtime on the
reached machines and graphs needs 4409's evaluator, 4404's bridge and driver, and 4406's oracle); the update-per-step
reading of `RandomRoll` against a live fidget count; the held-graph reading of chooser states; ECComplete's 0.98.

### 13.2 The graph evaluator and the pose program (4409)

Worker of 4409 (account B), 2026-11-15 22:06-22:25 EST. Code [C, tests]: `render/src/unitanim/graph.cpp` (`bindGraph`,
the evaluator, `blockEase3`, `evalBlockCurve`), `render/include/c5r/unitanim/graph.h` and `pose_program.h` (additive
only: `EcMap::durations` / `duration()`, `ActiveSource::duration` / `once` / `done` / `weight`,
`GraphInstance::restartFrom` with a default that calls `restart()`, `TrackMaskWeights::target_weight`; the calls 4403
uses are unchanged), `render/include/c5r/units/pose.h` / `src/units/pose.cpp` (additive: `poseFigureProgram`,
`ModulePose::rowsProgram`, `ModulePose::clipDuration`), `render/include/c5r/unitanim/controller_pose.h` /
`src/unitanim/controller_pose.cpp` (`ControllerPose : PoseSource`, `makeEcMap`). Tests
`render/tests/unitanim/test_graph.cpp`, `test_pose_program.cpp` (with `pose_fixture.h`, 0041's synthetic model copied
from `tests/units/test_pose.cpp`). `EcMap::find` is defined in `graph.cpp`; 4403 should not define it again.

**What `bindGraph` checks, statically over every node** (so that the pre-check finds it without a timeline): node
types (`UnknownNode` for Unknown, Layer, ScalarSelect and the other EXE-named types, a NULL NodeData, a callback hint
other than 5 and 7); every source's ec in the member's map with a duration (`EcNotBound`, all missing codes named);
at least one source reachable from the root (`NoProgress`); and the structure (`BadLogicFile`: no DAG, root not a
Subgraph, an edge out of range or of the wrong kind, a cycle, a source index outside Sources, a hint-7 value outside
paths 0-8, a track mask index outside TrackMasks). **Every source is checked, not only the selected path**, so the six
models of 6.1 (four spaceship parts with ec-0 sources, `dopp_a`, the Hun ram's unused graph) are refused at bind time
with `EcNotBound`. The ram's graph is in its `.fxsxml` but no state of its machine uses it: binding only the graphs
the machine's states name (the runtime's job, 4403) avoids that one.

**Evaluation (the readings of 6.3, each [I] until 4405's captures score it):**
- Hint 7 = path option `CallbackValue` (0-8). Hint 5 = min(1, time in state / `CallbackValue`).
- BlockCurve: blocks laid end to end by Duration; zero-width blocks cover and change nothing. Type 3 is an ease
  0 -> 1, or 1 -> 0 with FlipX, or 1 minus that with FlipY. Type 0 holds. Before the first block the curve holds the
  first block's start value, after the last block its end value. The output is times Scale. On the unit shapes:
  (a) and (b) rise over 1 and hold 1; (c) falls from 1 to 0.
- **The ease is a named function, `blockEase3` = smoothstep.** At 7.3's one live point (P01, idle A -> fidget A),
  the warrior's fidget graph fades over 0.3333 s. This gives w(0.2 s) = **0.648**, against **0.680** measured.
  Linear would give 0.600. Not tuned; the live fades of 4405 decide.
- Selection: input `round(Selector)`, else `DefaultEdge`.
- PoseAnimSource: clip time = start + Speed x state time + Offset. A connected Offset reads its scalar in seconds;
  path 6 gives 0 or 1 s. LoopCount 0 wraps; n >= 1 plays n times and holds the end, `done`. ContinueOffset 1 takes
  over the previous state's clip time of the same ec through `restartFrom`.
- Blend: w = Param clamped to [0, 1]; without a mask, w 0 / 1 is the one side alone, so a finished fade is a single
  source. **7.3 found the old clip held during the idle -> fidget fade. The files say it continues (`ContinueOffset`
  1, Speed 1), and the evaluator runs it on as read.** The difference is for the oracle to score.
- Masks: a Blend with a Trackmask passes the mask's binding, DefaultWeight and TargetWeight into the program, with
  no bones. **Which bones a binding (`TrackMaskAIM_LAYER`) names is not read yet**, so `rowsProgram` refuses a mask
  with a binding and no bones (`BadInput`, named). The 26 aim members (mechanized, modern ships, aim civilians) are
  therefore not drawn through the controller until it is read. It is open: possibly the model's or the clip's track
  group data.
- `complete()`: every once-played source on the selected path is done. `active()` lists the sources with times, the
  wrap flag and their share of the pose (for triggers and the oracle).

**The pose program in the module.** `rowsProgram` takes a program with one Sample through `poseFigure()` itself,
with the clip time clamped; the controller has already wrapped t. **Its rows are bit-identical (memcmp) to
`ModulePose::rows`** for every t below the clip's duration (test: 36 requests: 3 clips x 4 t x 3 meshes). Blends are
per bone in the clips' file basis, before the basis change:
- positions and scale-shear lerped, orientations nlerped after aligning b to a's hemisphere (blendfit.py's rule);
- bone weight w x m, with m the mask's weight for the bone, or its default weight for bones it does not name;
- a bone at rest on one side takes the skeleton's rest transform;
- then 0041's world, skinning and palette steps, unchanged.
- Refusals (`BadInput` unless noted), each in the test: an empty program, a node that is not earlier, a non-finite
  weight or t, a mask out of range, an unresolved mask binding, mask bones and weights of different length. Also
  `BadModel` for a blend across file bases, and the clip codes of `poseFigure`.
- `ControllerPose::rows` refuses a figure that has no program (`BadInput`, unit and figure named); there is no
  default clip.
- `makeEcMap` builds the member's ec -> clip map with durations from the clip cache. A clip that does not bind is
  left out, with its reason in `notes`, so that a graph using it fails `EcNotBound`.

**Tests:**
- `test_graph`: 7 cases, 16 refusal assertions.
- `test_pose_program`: 4 cases, 12 refusals.
- `render/build.py --test -R c5r_unitanim`: 3 / 3 pass (22:20).
- MC condition (1): `render/build.py --test -R "c5r_units|c5r_unitanim"` and `oracle.py --no-c` (the whole oracle set's pose and placement steps), before (team branch af6287f5, 22:08) and after (aa60aca7, 22:18), logs and hashes in `render/evidence/0374/4409/` (`before.log`, `after.log`, `before_after.txt`). 0041's suites pass 10 / 10 before; after, 12 / 12 with the two new ones. **`pose_rows.f32` (the module's palette rows for all 1,535 catalogue figure-meshes) is byte-identical**; `pose_list.tsv`, `report_units.tsv` and `placement_report.tsv` too; `pose.tsv` is identical except its timing column. C (the GPU draws against the captures) was not run again: its input rows are byte-identical. ASan (`render/build.py --asan --test -R "c5r_units|c5r_unitanim"`, cl.exe /fsanitize=address, 22:21): 12 / 12 pass, 0 ASan reports, no compiler warning in the new files (`asan.log`).

### 13.3 Track masks: where the aim layer's bones come from (4410)

Worker of 4410 (account B), 2026-11-15 22:29-23:16 EST. Offline: files read (never written) from the c5h-b / c5h-c
pool installs, c5h-c's MODS folder (VP as installed there) and the 4402 database copy
(`F:/c5-64bit-work/0327/db/Civ5DebugDatabase.db`), names resolved as the game resolves them (4402's
`logicfiles.py` rules and `fpkscan dups`). Scripts and number tables in `render/evidence/0374/4410/`. **This section
closes the open item of 13.2 ("which bones a binding names is not read yet") and replaces the [I] reading of the
Trackmask row in 6.3 with the [C] facts below.**

**The source [C, by dump of the models' type trees and values].** The `.dge` names a mask only by its binding
(`TrackMasks[0].BindingName` = `TrackMaskAIM_LAYER` in all 52 aim graphs). The weights are in the **member's model
`.gr2`**: every skeleton bone's `ExtendedData` is a variant whose type has one member `GrannyTrackMasks`, a
`Reference` to a struct with one `Real32` member per mask, named exactly like the binding (`TrackMaskAIM_LAYER`)
(`s10_edtype.py`; the first build read it as an inline struct and found nothing on the real files: the test caught it). That value is the
bone's weight. This is the bone-property form of Granny track masks (artist properties on the bones exported into
`ExtendedData`); the string `GrannyTrackMasks` and the binding name are in the unit string databases, and in the 448
files extracted for the 14 stock aim `.fxsxml` they occur only in the 14 model files (`s3_find.py`), never in a clip
or a `.fxsxml`. The `.fxsxml` `<BoneUsage>` lists trigger attachment bones, not mask bones; the aim clips carry
tracks for every bone (tank: 14 of 14), so their bound tracks do not name the mask either.

**The 26 members** (`s7_members.py`, `members_masks.out.txt`; one skeleton each) [C for the data]:

| Machine (members) | Mask data | Bones at weight 1 | All other bones |
|---|---|---|---|
| mechanized aim (11) | every bone: tank, modern armor, mechanized infantry, mobile SAM, WW1 tank, VP armored car (`FordM1918`), mech; landship level 3 all but one | `AIM_DR` (mech: `Mech01Spine`; landship 3: `AIM_DR`) | 0 |
| mechanized aim, German panzer | every bone | **none** (its `AIM_DR` is 0) | 0: the aim layer moves no bone |
| mechanized aim, landship levels 1 and 2 | all but bone `Tank` | none | 0 |
| modern ship aim (7) | every bone: battleship, carrier, missile cruiser, VP Mikasa, VP supercarrier, VP sensor combat ship; late cargo ship all but bone `Cargo_Ship` | the ship's 2-4 `AIM_*_DR` turret bones (cargo ship: none) | 0 |
| aim civilian (4 spaceship parts) | every bone | `AIM_DR` | 0 |
| VP edited modern ship aim (4: destroyer `Wickes`, dreadnought, fleet destroyer `Z_Class`, `Yamato`) | **none: no bone has any ExtendedData** | - | - |

Weights met: 0 and 1 only. The weight-1 bone is a turret pivot: its children (turret, gun, hatch) are at 0, so they
keep the body's local motion and turn with the pivot through the hierarchy.

**Bones without the member (4 skeletons, one bone each) [C].** The missing bone is the mesh's own root-like bone
(`Tank` in the three landships, `Cargo_Ship`). Its local transform is the same in every clip of the member (cargo
ship: one curve hash in all 23 clips; landships: two encodings, a constant identity quaternion or `DaIdentity`, with
the same constant position, `partial_bones.out.txt`, `s9_tankbone.py`), so whatever weight it takes, the blended
pose is the same. ~~The evaluator gives it the Trackmask node's `DefaultWeight` (1 in all 52 graphs), 4409's rule; the
meaning of `DefaultWeight` as "weight of a bone the mask does not name" stays [I], and on the reached data it cannot
change a picture.~~ *Corrected 2026-11-20 (0617, 13.3.1) [C]: stock gives a bone without the binding its parent's weight
(the root 0) and does not use the node's `DefaultWeight`; the controller now does the same. On the reached data the
picture is unchanged either way.*

**`TargetWeight`** (0 in all 52) is carried into the program and not applied; its meaning is open [I]. With
`TargetWeight` read as a replacement weight the aim layer would move nothing, which would make the aim clips dead
data; the reading "not applied" is the one consistent with the files.

**The code [C, tests].**
- `models/`: `c5m::Bone::track_masks` (each scalar member of `ExtendedData.GrannyTrackMasks` as (name, weight)) and
  `Bone::track_mask(name, w)`, read in `gr2_model.cpp` (additive; an `ExtendedData` of any other shape leaves the
  list empty; the canonical summary is unchanged). Test `models/tests/test_logic_masks.cpp`: a synthetic file with
  six bone shapes (the game's Reference form and an inline struct, each with two weights and an array member; a scalar `GrannyTrackMasks`; NULL; a string
  member; a NULL reference) and the real tank (14 of 14 bones, `AIM_DR` 1), German panzer (17 of 17, none at 1) and warrior (no data).
- `render/unitanim/controller_pose`: `meshSkeleton` (pose.cpp's skeleton rule for a mesh), `resolveTrackMasks` (fills
  a binding-only mask from the skeleton: the bones that carry the binding with their weights; the rest keep the default
  weight), `checkTrackMasks` (the pre-check's form over a member's graphs and every skinned mesh's skeleton).
  `ControllerPose::rows` resolves binding-only masks on a copy of the figure's program before
  `ModulePose::rowsProgram`; `pose.cpp` is unchanged (its refusal of an unresolved binding stays as a backstop), so
  13.2's bit-identity (MC condition (1)) is untouched.
- New status `AnimCode::TrackMaskNotBound` (appended to `status.h`, name in `status.cpp`): no bone of the skeleton
  carries the binding, a weight is not finite, or the mesh has no skeleton. *(0617, 2026-11-20: "no bone carries the
  binding" is no longer a refusal: such a mask is no mask, 13.3.1.)* Through `ControllerPose::rows` it is a
  `BadModel` UnitStatus whose text starts `TrackMaskNotBound:`, with the model, mesh, binding and skeleton named,
  rows left identity.
- **VP's edited modern-ship graphs (issue 0439, 2026-11-16) [C for the files; I for the EXE].** In 19 of the 21
  `edited_aim_ms_*.dge` files the masked aim blend (Param a constant 1, Mask the aim layer's track mask, From the body)
  has **no edge on To**: VP disconnected the aim selection, which stays in the file unreached (`edited_aim_ms_idle_graph`:
  node 14 From = the idle selection, To none). The evaluator now takes a Blend with one pose input unconnected as its
  connected input alone (`graph.cpp`, 13.2), so these graphs give the body's program with no mask, and the destroyer,
  dreadnought, fleet destroyer and Yamato are drawn in their idle, fidget, fortify, run and death states. Only
  `edited_aim_ms_bombard_defend_loop_graph` and `edited_aim_ms_modern_ship_attacks` keep a connected masked blend; their
  states are entered by 1280 / attack requests, which only 0042's combat choreography makes, and there the mask that no
  bone of these models carries stays `TrackMaskNotBound` (not drawn) until a capture shows the EXE's picture.
  *Corrected 2026-11-20 (0617, 13.3.1): that refusal made the ships vanish during attacks (seen live by 0042's W20);
  stock blends these two graphs unmasked [C], and so does the controller now.* What the
  EXE's blend does with an unconnected input is not read [I]; that the EXE draws these ships' bodies is [L] (0439's live
  check, unit-server-scene.md 3.5).
- ~~**The 4 VP edited ships stay refused**~~ *(corrected 2026-11-16 by 0439: drawn outside the two combat graphs, item
  above)* (`TrackMaskNotBound`, D118: never drawn with a guessed mask). What the EXE
  does with a binding no bone carries (every bone at `DefaultWeight` 1, i.e. the aim clip replaces the whole body; or
  no mask) is not in the files: open, for a live look at a VP destroyer in 4405's oracle [L]. *Answered 2026-11-20
  (0617, 13.3.1) [C, from the EXE's code]: no mask, the unmasked blend.*
  Correction 2026-11-15 23:51 (4406 round 2, 15.2): the first refusal of these 4 units is `BadLogicFile`
  (`edited_aim_ms_idle_graph.dge: pose edge to node -1`, at bind time), not `TrackMaskNotBound`; they stay refused (D118).
- **The pre-check** (`Controller::checkArt`, wired by 4410 at the lead's request, 2026-11-15): for a member whose
  reachable graphs name a track mask, the model (the `.fxsxml`'s mesh) is read through the controller's files with
  `ControllerOptions::strings` (or `ControllerOptions::model_reader` when set) and `checkTrackMasks` runs over those
  graphs; a model that cannot be read is `TrackMaskNotBound` too ("model not read"). **A caller must pass the unit
  string databases in `strings`**: without them the bones' mask names do not resolve and the 22 members with data
  would be refused as well. Members without a masked graph read no model. Test
  `render/tests/unitanim/test_precheck_masks.cpp` (no masks: no read; all skeletons carry it: Ok; one skeleton or the
  whole model without data, an unreadable model, a missing and a broken file: `TrackMaskNotBound` with member,
  graph, binding and skeleton named).
- Test `render/tests/unitanim/test_graph_masks.cpp`: `meshSkeleton` on the fixture's 4 meshes; `resolveTrackMasks`
  full, partial, none, another binding, no skeleton, a NaN weight (each refusal with code and text); `checkTrackMasks`
  over null, plain and aim graphs; an aim-like graph (Blend of body and aim sources, Param 1, Trackmask) bound with
  `bindGraph`, evaluated and drawn through `ControllerPose::rows`: **memcmp-identical to the same program with the
  mask written out by hand**; the masked bone differs from the body alone, the others equal it; a model without data
  refused with identity rows.

**Builds and regressions** (`render/evidence/0374/4410/results.txt`): `models/build.py --test` and `--asan --test`
9 / 9; `render/build.py --test` and `--asan --test --mem-cap-mb 4096 -R "c5r_units|c5r_unitanim"` 20 / 20 with 0 ASan
reports, once the placement-catalogue test was given the current TINY_0009 height field (its default bundle had been
removed from F: during the run; the test skipped, unrelated to this change). `oracle.py --no-c`: `pose_rows.f32`,
`pose_list.tsv`, `report_units.tsv` and `placement_report.tsv` byte-identical to 4409's after-state.

**The capture check [C for the clip values; the capture cannot score the mask].** The 3907 catalogue
(`F:/c5-64bit-work/0327/4153/w2b/all/instances.csv`) holds 42 instances of 19 aim members (the 4 VP ships among
them), all fitted `ok` by a single idle or fidget clip with a rotation residual of 0 to 2e-4 rad, except two
spaceship-part instances (0.057 and 0.032 rad; not examined here, open). In idle the aim layer's path 8 / 7 values are not known,
but the aim centre clip's weight-1 bones equal the idle clip's: tank `AIM_DR` identity in both (same translation);
battleship `AIM_Front_DR` equal to 1e-8; mech `Mech01Spine` to 5e-6 rad. Left and right aim are large turns (tank
`AIM_DR` +-135 degrees about z; battleship `AIM_Front_DR` about 90 degrees). So a masked centre-aim pose equals the
unmasked body pose below the catalogue's residual: the catalogue cannot tell a mask from none. The mask is scored by
a capture of a unit aiming off centre (an attack at an angle), which is 4405's: open [L].

**Corrected 2026-11-16 00:52 (lead-0374, from 4406 round 2, section 15.8):** the "masked centre-aim pose equals the
unmasked body pose" statement above holds for the tank and battleship bones named, not for the mech: on 4353's mech
blocks the aim layer moves the pose by about 1.25 deg (idle B alone is 1.25 deg away at its best t; idle B under the
resolved `TrackMaskAIM_LAYER` reproduces block 51 to 1e-6) [L]. That is also the first capture check of a resolved aim
mask: the binding by bone name is confirmed on one block [L]. The off-centre aim capture was not made in 4405 B (not
done: the aim unit), so left and right aim stay unscored. *Corrected 2026-11-16 (0395, 17.4): a battleship's ranged
attack captured with its turrets at full left aim (`Aim_Left` at t 0, 0.0000 deg); the controller's program with steer
selector 0, weight 1, under the resolved `TrackMaskAIM_LAYER` reproduces the palette (rotation element 0.0, translation
3.8e-5) [L]. Right aim and partial weights stay unscored.*

#### 13.3.1 What stock does with a mask, and the four VP ships (issue 0617, 2026-11-20)

Ticket 0617, implementer on account B, 2026-11-20 14:29-16:00 EST. **This section replaces three readings of 13.3:**
the refusal of a binding no bone carries, the Trackmask node's `DefaultWeight` as the weight of a bone without the
binding, and "what the EXE does with a binding no bone carries ... open". Code: `render/src/unitanim/controller_pose.cpp`
(`trackMaskUnbound`, `resolveTrackMasks`). Evidence and scripts: `render/evidence/0617/`.

**The refusal as it was [C, files and code].** W20 of epic 0042 saw it live (#8376 on thread 0617, 2026-11-20 12:47:
`UnitRefused ... UNIT_DESTROYER: BadModel: TrackMaskNotBound: wickes.gr2 mesh Wickes: track mask TrackMaskAIM_LAYER:
no bone of skeleton World_Center (64 bones) carries it`). Reproduced offline from the c5h-c pool copy's files: VP's
`Wickes.fxsxml` names 21 `edited_aim_ms_*.dge` graphs; two of them keep a Blend whose From, To and Mask are all
connected, `edited_aim_ms_modern_ship_attacks.dge` (attack state: From = the attack selection of ec 2100 / 1160 / 1100
/ 1130 / 1180, To = the aim selection of ec 1700 / 1705 / 1710, Param = a BlockCurve fed by a hint-5 callback of value
100) and `edited_aim_ms_bombard_defend_loop_graph.dge` (From = ec 1285, To = the aim selection, Param = a BlockCurve fed
by value 1000). `Wickes.gr2` has one skeleton `World_Center`, 64 bones, none with any `ExtendedData`
(`sweep_masks.tsv`). `resolveTrackMasks` found no bone with the binding and returned `TrackMaskNotBound`;
`ControllerPose::rows` turned that into `BadModel`, so the destroyer was **not drawn for every frame of an attack or
a bombard-defend loop** (the pre-check passed it: those states are entered by requests only, not by a socket from
the default state, so `checkArt` never reached the two graphs). Not a reader gap and not an unknown layer name: the
model has no mask data at all.

**What stock does [C, read-only headless decompile of the old DX11 EXE (stamp 0x546CD0A8) in a scratch copy of the
analysed Ghidra project, 2026-11-20; described here in our own words, no listing kept (D83)].** The EXE links
Granny's blend-DAG runtime (granny_dag2_*) and FireEngine's model loader:
1. *Masks are built per model when the model is loaded.* The loader walks the skeleton's bones, takes the first bone
   whose `ExtendedData` has a `GrannyTrackMasks` member, and takes **the mask names from that member's type** (one mask
   per member name). For each name it creates a mask over all bones with default weight 0 and fills it with Granny's
   track-mask extraction called with **default weight 0 and "use the parent for the default" on**: a bone that has the
   name gets its own value, a bone without it gets its parent's weight (already filled, the bones being in parent-first
   order), a root without it gets 0. The masks are registered on the model under their names. (An older-format path
   for files below a Granny version threshold reads `TrackMask*` members straight from `ExtendedData`; the sweep found
   no such member in any unit model.)
2. *A graph instance binds its masks by name through a callback.* For each `TrackMasks[k].BindingName` of the `.dge`
   the DAG instance asks the model for the mask of that name. A name the model has no mask for is bound as **no mask**
   (NULL) with a warning in Granny's log, and **the instance is still built**. The same holds for
   an animation source whose ec the member lacks: bound as nothing with a warning, the instance built (this answers
   6.1's open "missing source" item for the binding step: the EXE does not refuse the graph).
3. *The Trackmask node holds only the bound mask.* Its `DefaultWeight` and `TargetWeight` are not used when the node
   is evaluated: the node returns the mask bound at `BindingIndex`.
4. *The Blend.* With one pose input unconnected it returns the connected one (0439's reading, now [C]); with neither,
   no pose. With both: the weight is Param clamped to [0, 1] (a fixed default when Param is unconnected). If the Mask
   edge gives a mask, every bone takes `mask weight x Param` (bones beyond the mask's count take the mask's default);
   a bone whose mask weight is not above a small epsilon keeps From. If the Mask edge is unconnected **or the mask is
   NULL**, the blend is the plain whole-pose blend: From alone when Param is at about 0, To alone at about 1, else
   every bone lerped by Param.

So for VP's four edited ships stock **draws the unit through both graphs with an unmasked blend**: every bone is
`From x (1 - Param) + To x Param`. [I] for the picture: 13.2 reads a hint-5 callback as `min(1, t / value)` and the
BlockCurve here (one flipped ease block of width 1) as falling from 1 to 0, so Param stays about 1 through an attack
of a few seconds (value 100) and a bombard-defend loop (value 1000): the whole ship takes the aim clips (the
battleship's `Battleship_Aim_*` on the destroyer and Yamato, `Destroyer_Aim_*` on the dreadnought and fleet destroyer)
rather than the attack or bombard-defend clip on its body, where the stock battleship moves only its `AIM_*_DR` turret
pivots. Which bones that moves (`destroyer_tracks.out.txt`): the destroyer's and Yamato's 64 bones include the
battleship's 35, and every battleship clip names exactly those 35 (29 bones of their own have no track and stay at
rest); the dreadnought's 41 bones take 11 of the destroyer clips' 19 tracks, the fleet destroyer's 41 take 17.

**The partial members [C].** By item 1, a bone without the binding takes its parent's weight, not the node's
`DefaultWeight` 1 that 4409 / 4410 gave it. The four partial members (landships 1-3, the late cargo ship) each lack it on
one bone; 13.3 showed that bone's transform is the same in every clip of the member, so the picture does not change
either way; the code now follows stock (`resolveTrackMasks` lists every bone with its weight).

**The sweep [C, `sweep_masks.py`, `sweep_masks.out.txt`, 2026-11-20 15:05].** Every one of the 366 unit members of
the VP database copy (`F:/c5-64bit-work/0327/db/Civ5DebugDatabase.db`, stock rows included), files from the c5h-c
pool copy (VP as mods) with 4402's dups table: 340 members have no Blend with a connected mask; 22 have one, all with
the binding `TrackMaskAIM_LAYER`. By the model's bones: **all 18** (stock tank, modern armour, mechanised infantry,
mobile SAM, WW1 tank, German panzer, mech, battleship, carrier, missile cruiser, the four spaceship parts; VP armoured
car, Mikasa, supercarrier, sensor combat ship), **partial 4** (landships 1-3, which no unit row uses in this database;
`UNIT_CARGO_SHIP`'s late art), **none 4**: `UNIT_DESTROYER` (`wickes.gr2`), `UNIT_DREADNOUGHT`
(`dreadnought_generic.gr2`), `UNIT_FLEET_DESTROYER` (`z_class.gr2`), `UNIT_YAMATO` (`yamato_class.gr2`), all through
the two edited graphs above. No member had a binding missing from the first bone's names (`name-not-first`), and no
old-format bone member. **So the units refused for this reason before 0617 were exactly these 4; after 0617 none.**

**The fix [C, tests].** `trackMaskUnbound(skeleton, binding)`: true when the first bone with mask data does not name
the binding, or no bone has mask data. `resolveTrackMasks` then makes the mask no mask: binding `""`, default weight
1, no bones; `pose.cpp`'s blend gives every bone `m = 1`, which is its unmasked blend, bit for bit. A bound binding
lists every bone with stock's weight (own, else parent's, else 0). `TrackMaskNotBound` stays only for a weight that is
not finite and for a mesh without a skeleton (no unit model has either), and `checkArt`'s "model not read". Tests:
`render/tests/unitanim/test_graph_masks.cpp` (partial by parent, none, another binding, a binding absent from the
first carrying bone; `destroyer_unbound_mask_blends_unmasked`: the attack graph's shape on a model without mask data
at Param 0, 0.03, 0.35 and 1, rows bit-identical to the same program with the mask removed, never identity, and
different from the masked model's), `test_precheck_masks.cpp` (a model without data and one skeleton without data now
pass; a non-finite weight refused by name), `render/tests/units/test_masks_real.cpp` (install-backed: the 4 VP ships
unbound and resolved as no mask, their pre-check over the two real graphs Ok; the battleship and tank full; the cargo
ship's one bone without the binding at its parent's weight).

**Not verified.** No recorded stock pose of any of the 4 ships exists (the scene library and 3907 / 4405 / 0395
captures hold none in an attack or bombard-defend state; 13.3's catalogue instances are idle or fidget): the
unmasked attack pose is checked against stock's code, not against a stock picture [L open]. The Param curve of the two
graphs (hint 5 with values 100 and 1000) is 13.2's [I] reading; if it is wrong the mix of attack and aim clips differs,
but the blend stays unmasked as stock's. What the player saw before 0617: the ship vanished during its attack and its
bombard-defend loop (the server logs `UnitRefused` once per type and code) and reappeared afterwards.

### 13.4 A state whose clip is missing from the install (issue 4770, epic 0042, 2026-11-20)

Built by the 4770 worker (account B, branch `w1-4770`), 2026-11-20 01:41-02:30 EST, for the MC's ruling of 2026-11-16
(answer to 4456's ASK 2; `unit-server-scene.md` 6.1, "The run-time missing clip"). Offline, synthetic tests only; no
game file, no game run. [C: code and tests, as listed below.]

**Where "missing in install" is known [C, code read].** The art step (`models/src/art_resolve.cpp`, `lookup`) sets
`c5m::FileRef::found = files.exists(basename)` for every animation the `.fxsxml` names, so a clip named there and not
shipped (VP's `U_Assyrian_Siege_Tower_Charge_Attack.gr2` for ec 1140, 17.7) has `found` false in
`MemberArt::files.animations`. Before 4770 the controller's member ec map (`Controller::member`) dropped the flag; the
product's `ClipGraphFactory::bind` (`driver.cpp`) reads the absent file as duration 0 (`readOne`: the loader's `missing`
is not kept), and `bindGraph` (`graph.cpp`, step 4) fails `EcNotBound` "... source ec the member lacks: 1140 (... no
duration)". The same text comes from a clip that is there but malformed, and an ec the `.fxsxml` does not name gives
"... lacks: 1140" alone; the code is `EcNotBound` in all three.

**The rule as built (`render/src/unitanim/controller.cpp` `enterState`; `render/src/unitanim/missing_clip.h`).**
- The member keeps `found` per ec entry (`Member::ec_found`, parallel to `EcMap::ecs`).
- A bind failure is **missing-in-install** when its code is `EcNotBound`, every ec of the graph's `dags[0].source_ecs`
  is named by the member, and at least one of them, by the entry `bindGraph` uses (the first that names it,
  `EcMap::find`), has `found` false. An unnamed ec anywhere in the graph's source list makes it any other `EcNotBound`
  (conservative: a listed source no node uses counts too). A graph whose named clips were all found (a malformed clip)
  is any other `EcNotBound` (D118, D125: still fails loud).
- **With a current graph** (a transition, `from` set and `Fig::cur` not null): the figure's machine state goes back to
  what it was before the update that took the transition (state, path options and time in state: `f.ms = prior`); its
  graph, clip and clip time are untouched (the graph had already advanced by the step's h, as it does without the
  request); no fade starts; the unit stays Ok. One LOUD line goes to the controller's sink and the controller's count
  goes up by one:
  `LOUD unitanim: missing in install: unit (P, ID) TYPE, member MEMBER (FXSXML) from figure F, MACHINE state FROM -> TO
  (GRAPH): ec EC clip FILE: the figures keep state FROM (bind text)`.
- **An occurrence** is per unit, member and target state (`Unit::missing`), and ends when an event's request reaches a
  figure of the unit (a path write in `Controller::event`). Until then the machine takes the same socket again on later
  updates while the request stands on its path (the reverted path options keep it), and the state is kept out again
  silently and without a bind. A second request is a new occurrence: one more line, one more count.
- **Without a current graph** (a start: create or revive, `from == SIZE_MAX`, or the default chooser before any graph):
  there is no state to keep; the status is `EcNotBound` as before, its text prefixed `missing in install: ` with the
  missing ecs and clips (the flag), so the unit is AnimFailed and not drawn. Not counted.
- Every other `EcNotBound` (and every other code) is unchanged.

**Consequences [I, the lead may overrule].** While the unanswered request stands, sockets after the kept-out one in the
state's file order are not reached (with the omni idle shape: no fidget roll); the figure plays its current graph on.
Random draws and forced choices the update consumed before the kept-out socket are not given back. The controller's
pre-check (`Controller::checkArt`) is unchanged: it binds every graph reachable through the sockets and still refuses a
member whose reachable graph has a missing clip. It is called by the tests and tools only: the server's pre-check is
the units module's `UnitRenderer::precheck` (`server/src/units/srv_units_scene.cpp`) [C: grep, 2026-11-20]; how that
one treats a missing clip is 0041's and not read here.

**Not in the public headers (the ticket's rule).** In `missing_clip.h` (src only): `kMissingInInstall` (the prefix),
`missingInInstall(status)`, `setMissingClipSink(controller, sink)` (default: the line on stderr), and
`missingClipReverts(controller)` (since `open()`), kept per controller in a registry inside `controller.cpp`. The fields
they stand in for, for the lead to ask the MC at C1: `AnimStatus::missing_in_install` (bool, `status.h`),
`Controller::ApplyCounts::missing_clips` (per apply) or a cumulative `Controller::missingClips()`, and
`ControllerOptions::loud` (a `std::function<void(const std::string&)>` sink) so that the server's
`srv_units_anim.cpp` writes the line to its own log (today it would go to stderr: the server does not set a sink).

**Tests** (`render/tests/unitanim/test_controller_missing_clip.cpp`, 7 tests, over the product's `ClipGraphFactory`
with a table loader, so the real `bindGraph` runs): the flag's text test; a control (the clip present: the attack
request enters state 9); (1) the request on a 3-figure unit with `w_attacka.gr2` missing keeps every figure in state 1
with programs byte-equal to a twin controller that never got the request, for 46 frames, the clip and clip time as
before plus one step, the unit Ok, exactly one line naming unit, member, machine, states, ec and file; (2) a second
request logs once more and the count reads 2, then a fortify request moves both twins on byte-equal; (3) an ec the
`.fxsxml` does not name and (3') a clip found but unreadable both fail `EcNotBound`, unflagged, with no program and no
count; (4) a start on a missing clip, from the default state (`from == SIZE_MAX`) and through the default chooser,
fails `EcNotBound` flagged. Results: see the ticket's log.

### 13.5 How the EXE decides `ECComplete` (issue 4738, epic 0042, W42, 2026-11-21)

**Method.** Build 4390913 only (the DX11 EXE, PE stamp `0x546CD0A8`, ImageBase `0x400000`): read-only headless Ghidra
12.1.4 on a scratch copy of the project `civ5` (`../06-tooling-and-workflows/reverse-engineering-setup.md`), with the
read script of `evidence/0338/Dec.java` plus a range-listing item; decompile, disassembly, data and vtable reads,
2026-11-21 15:40-16:30 EST, by W42 (account B). No game run, no instance. Nothing of build 26577625 was read. The
decompiler output stayed in the local work folder (D83); what follows is the behaviour in words. [C] unless marked.

**Where it is** (VAs of build 4390913). The class's name string `FFSMConditionECComplete` is at VA `0xA9A6C4`
(`0x6998C4`, the value 3100's `exe_animation_strings.out.txt` gives, is its file offset). Type function `0x8E8140`;
maker table `0xA9C50C` (creates a 0x24-byte object, constructor `0x903CB0`); vtable `0xA9CA9C`: attribute read
`0x903DF0` (`ec` as a 16-bit code, `value` as a float), write `0x903E40`, **test `0x903E80`** (slot 5, the slot every
condition class uses for its test: `CheckPathOption` `0x840810`, `CheckStateTime` `0x840700`, `RandomRoll`
`0x840080`, `AnyComplete` `0x903C70`). Binding pass `0x8E8770`, called from `0x8E9AB0` (attaches a model's state
machine to a figure's animation instance and creates the machine instance; reached from `0x8EA410`, which adds a
model to an animation instance). The model's ec table lookup `0x8EF8E0` (a sorted map in the model asset keyed by the
16-bit code). Socket evaluation `0x8410E0`.

**The condition's data.** The code, the `value` (a float; **0.98 when the attribute is absent**, the constructor's
default) and a threshold (a float, 0 when constructed).

**Binding.** Each time a model's state machine is attached to a figure's animation instance (`0x8E9AB0`), the pass
`0x8E8770` walks every state, socket and condition of that machine and, for each `ECComplete`, sets the threshold to
the float at offset 4 of the model's ec-table entry for the condition's code, or to **0 when the model has no entry for
that code**. The `value` takes no part. That entry is the model's animation for the code, and offset 4 is its length in
seconds [I, strong: the EXE's Granny play call prints the name at offset 0 of the same kind of pointer, the public
Granny `granny_animation` layout has Name at 0 and Duration at 4, and the EXE's effect code `0x442140` sets an effect's
duration from this float times a duration scale]. Per model or shared: the pass writes into the machine object the
model asset reaches. The `.fsmxml` is a resource type of the engine's resource manager (`FStateMachineMgr` registers a
loader with id `0x0666676D`, the id the asset's machine component is looked up by; its extension test `0x8E0610`
accepts `.fsmxml`; the parse is `0x8E0F50`), and the resource manager's load `0x8DEF40` returns a cached object by file
name ("Found in cache, file %s, type %d") [C for these]. So **one loaded machine is very likely shared by every model
that names the file** [I, strong: the step from the cache to the asset's component pointer was not traced]. If so,
every `ECComplete` of a shared machine holds the clip lengths of the model attached most recently (each figure
attached re-runs the pass), for all models using that machine: a stock quirk, e.g. a warrior's attack state then ends
after the last-attached omni model's attack clip. Not live-checked; a live check: a unit's attack-state exit time
before and after a figure of another omni model with a different attack length is created.

**The test** (`0x903E80`). It takes the figure's machine instance through the instance-provider interface (the same
call `CheckStateTime` and `CheckPathOption` make), reads that instance's **time in the current state** (the accessor
`CheckStateTime` compares with its value: instance vtable slot 12, a float the instance holds) and returns true when
**time in state > threshold**, strictly (equal is false). It does not look at the graph, the path options, any
Selection or Blend, which clips play, their loop flags, their clip times or weights. The `value` is read and written
with the file but used neither by the test nor by the binding.

So **`ECComplete(ec [, value])` holds when the figure has been in its current state longer than its model's clip for
`ec` lasts, and at once (the first update with a time above 0) when the model has no clip for `ec`.**

**Socket evaluation** (`0x8410E0`). The conditions of a socket are tested in file order. Every test of a group is
evaluated even after an earlier test of that group failed (no short cut inside a group); events are collected per
group. At an `OrOperator`, a group whose tests all held fires the socket with that group's events (the conditions
after it are not evaluated); otherwise a new group starts; at the end the last group decides. The firing group's
events (all of them, also those written before its tests) are applied after the tests; a failing group applies none.
`RandomRoll`'s test reads a fresh value from the instance each time it is evaluated (instance slot 0x38, compared with
the roll's value, `invert` flipping the comparison), so a roll placed after a failing test is still evaluated [C for
the call; I that the call consumes a random draw]. Our machine (13.1) stops a group at its first failing test: the
firing rates are the same, the order in which draws are consumed differs (immaterial while the EXE's random stream is
not reproduced).

**Against the controller's rule** (13.1, `graph.cpp`'s `EcSink`: a source of the ec on the selected path and every
once-played source on that path done). Census over the reached machines with 4408's `census.json` (the logic files as
parsed; `evidence/0042/4738-w42/ecsel.py` and `socks.py`, output `ecsel.out.txt`), per `ECComplete` in its group:
45 of 223 have a source of the ec on every selection, 153 have it on a selection the same group's path tests fix, and
**25 depend on a selection the group does not fix**. In those 25, when the path selects the other branch, the
controller's rule never holds (the figure freezes unless another socket fires), W40's proposed rule ("no source of the
ec on the selection: complete") holds at once, and the EXE waits the clip's length. They are:
- `15 -> 18 EC(1280)` ENTER_BOMBARD_DEFEND (1280 only on path 6 = 0; 6 = 1 when the request came from 12, 14, 16, 17
  or 22) in the omni, mounted, roman legion, ships, modern ship aim, VP's edited modern ship aim and mechanized aim
  machines: **4738's freeze**. The worker's and engineer's `10 -> 11` has `EC(1280) | T>=0.15` and leaves on time.
- mounted `9 -> 8` STOP_RUNNING into combat ready, `P6==1 EC(1440) | P6==1 EC(2440)`, while 1440 / 2440 are on path
  6 = 0 only: a mounted unit whose run stopped with path 6 = 1 freezes in 9 under our rule; the EXE leaves after the
  1440 clip's length (the first group).
- mounted `14 -> 20` group `P1==3 EC(1160)` (1160 is on path 1 = 1): a mounted attack entered with path 1 = 3 never
  completes under our rule; the EXE leaves after 1160's length (at once if the model has no 1160).
- airplane `2 -> 3` death `EC(2200) | EC(1200)` with path 6 choosing the clip: the EXE leaves when the time passes the
  length of 2200 or of 1200, whichever is shorter, whatever plays (at once when the model lacks either code); ours waits
  for the selected clip.
- engineer `35 -> 7` CAPTURE_ACTIVATE `S5=1 EC(1990) | EC(1200) S5=0` (path 1 chooses): the EXE takes the group whose
  clip is shorter (the first on a tie), and its `S5`, not the one that plays.
- great general `8 -> 9` `P4==... EC(1600)` (1600 on path 3 = 0) and the ships' `14 -> 20` attacks (sources by path 8,
  the broadside side the EXE writes): the EXE leaves after the named clip's length whatever path 3 or 8 holds.
Two further differences: in 171 of 223 the graph holds other once-played sources (a From side or other branches) and
the controller waits for those on the selected path too, the EXE only for the time; and the work boat's `3 -> 1 EC(1440)` names a looping source, so the
controller's rule (no once-played source) never holds there, the EXE leaves after 1440's length. The controller's
`>=` on clip time against the EXE's strict `>` on state time can move an exit by one step when a length is a whole
number of steps [I: how the EXE's state time accumulates was not read].

**The right change (for 4738's fix; W40 writes the code).** Replace the graph walk of the `ECComplete` test: it holds
when the figure's time in its current state (the time `CheckStateTime` reads) is strictly greater than the length in
seconds of the member's clip bound to that ec (the member's `EcMap` duration, as `makeEcMap` builds it), with length 0
when the member binds no clip to the ec; no selection, no blend weights, no once/loop flags, no fraction. W40's rule is
**not** the EXE's: it would end state 15 at once instead of after the 1280 clip, end an airplane death at once, and
leave mounted stop-running at once. The per-member length is the rule as the files intend it; reproducing the likely
shared-machine quirk (the last-attached model's lengths for every model of a machine) would need the order of figure
attachment and is a decision for the lead, not part of 4738's fix. Open after this read: the shared binding above
(live check), and how the EXE's state time is advanced (per frame or per step, scaled or not).

## 14. The bridge, the timeline file and the driver (4404)

Part A (the `.c5anim` reader and writer) is the lead's: issue 4404's log. Part B written 2026-11-15 22:29-22:55 EST by
the 4404 part B worker (branch `task-4404-bridge-timeline`). Code [C, tests]; the smoke run [C, offline on the
player's files]; the code choices marked [I] stay open for 4405's captures.

### 14.1 The bridge (`bridge.h`, `src/unitanim/bridge.cpp`)

`ActionBridge` (an `EventBridge`) turns each request-kind event into path-4 requests. Path 4 is a level (1, 6.2).

| Event | Path-4 code | EnableActions word checked |
|---|---|---|
| `fortify 1` / `fortify 0` | 1500 / 1000 | Fortify / Idle |
| `move` | 1400 | Run |
| `arrive` | 1000 | Idle |
| `destroyed` | 1200, every living figure, marked dead | Death |
| `kill <set>` | 1200, the set, marked dead | Death |
| `damage` | the figures that fall are removed, no clip (X4 corrected 2026-11-16, MC #2147; was "1200 on the figures that fall, marked dead") | Death |
| `action attack` | 1100 | Attack |
| `action attack_charge` | 1140 | AttackCharge |
| `action charge` | 1120 | RunCharge |
| `action attack_city` | 1160 | AttackCity |
| `action bombard` | 1180 | Bombard |
| `action anti_air` (new kind, see below) | 1130 | AttackSurfaceToAir |
| `action bombard_defend` | 1280 | BombardDefend |
| `action victory` | 1800 | none |
| `action captured` | 1195 | none |
| `action activate` | 1990 | none |
| `action work <build>` | the work table's code for the build's `Builds.EntityEvent`; with no table, `BadEvent` | none |
| `action paradrop` | `BadEvent`: the files have no code | n/a |
| `turn`, `embark`, `garrison`, `visibility` | no request | n/a |

`choice`, `steer`, `reset`, `place`, `create` and `remove` never reach the bridge: `Controller::event` handles them
(13.1 decision 7), and that is kept.

- **EnableActions per member** [C for the rows; I for the EXE using them this way]. `loadBridgeTables` reads
  `ArtDefine_UnitMemberCombats (UnitMemberType, EnableActions)`, with the words split at spaces, and `Builds (Type,
  EntityEvent)`. A request whose code has a word (6.5's table, `actionWord()`) is checked against every member its
  figures reach, in member order with `count` figures each:
  - a member whose words lack it is `BadEvent`, naming the member, the word and the code;
  - a member with no Combats row is also `BadEvent`, named and not guessed. On the database used here these are the
    three COLONIST2-4 members.

  Codes with no word (1800, 1195, 1990, work) are checked only by the controller's rule: the member's machine must test
  the value with method 1. The DLL calls `GameplayUnitFortify(unit, IsFortified())` for every new unit, so
  `fortify 0` reaches civilians too. It maps to 1000 (Idle), which every member with a row has (363 of 366, 6.5).
  Civilian sleep (1500 in the civilian machines) has no word in their rows (`Idle Death BombardDefend Run`), so
  `fortify 1` on a worker is `BadEvent`. Which DLL call, if any, requests a civilian's 1500 is open.
- **EnableActions and the EXE (issue 0439, 2026-11-16) [C for the reads named; I where marked].** The fortify (0x619610)
  and work (0x564960) handlers post their action with no word test; the EXE parses `EnableActions` into a per-member bit
  mask (the word table at 0xB99260: Idle, Attack, RunCharge, AttackSurfaceToAir, AttackCharge, AttackCity, Bombard,
  Death, BombardDefend, Run, Walk, Fortify, CombatReady, Victory, bit = the word's index; 0x4B8BF0) whose readers were
  not followed (several functions test bits of a member's +0x1B4 [I: combat choices]). So the bridge no longer checks
  words for fortify and work requests, nor for a member with no Combats row (VP's COLONIST2-4, which the EXE draws);
  what such a member's mask holds is not read [I]. The other requests keep the word check.
- **Damage (X4)** [C for the rule's code; I for the percent being the damage, as in `unit-formations.md` 7]. The
  figures that fall come from `units::livingFigures(n, damage, max_hp, -1)`, the 0x4B4B80 rule that 0041's placement
  uses, on the previous and the new damage. Figures `[living_after, living_before)` fall, the last figures first. Each
  gets 1200 on its own machine, and `Request::dies` marks it dead. A fall in damage requests nothing: the EXE revives
  "from the first dead one forward", but a dead figure's machine is in its final state, so the controller cannot
  model a revival. This is an open item. *(2026-11-16, issue 0439: with `remove_fallen` (the product) a fall in damage
  gives back the removed figures that `livingFigures` of the new damage counts, lowest missing index first, each new in
  its member's default state at time 0 (`Controller::revive`); figures kept dead in the `dies` mode stay dead. Without
  this a healed unit's placement drew a figure the controller did not hold: 4466's great war infantry figure 9,
  `UnitRefused` "no pose program". The EXE's state for a revived figure (default state at 0, or something else) is [I].)*
  **Corrected 2026-11-16 01:03 (lead-0374; premise change 5, MC #2147; 4405 B's Q7, 4406 r3):** the figures that fall
  are removed at once with no death clip and no dead idle: `ActionBridge::remove_fallen` (default true) gives
  `Request::removes` and the controller erases them. 4404's "1200, marked dead" stays as the comparison option (driver
  `--damage-death`). Scope [L]: one warrior, one damage step (50), captures 0.4 s apart, so a death clip shorter than 0.4 s is not excluded; the removal reaches the picture 1-3 frames after the Lua call (15.9). Deaths in combat come through the choreography's requests, not this event.
- **Additive changes outside the touches list** (issue log 22:35). In `controller.h` / `controller.cpp`:
  `EventBridge::requestsFor(ev, unit, art, out)`, which by default calls `requests`, and which the controller now
  calls with the unit's resolved art; `Request::dies`; and `Controller::unitInput()` for the driver. In `timeline.h` /
  `timeline_file.cpp`: the action kind `anti_air`. `BasicBridge` stays the controller's default when no bridge is
  given, because 4403's tests use it. The driver and the product pass `ActionBridge`.
- **Work codes** [C for the database and file facts; I for any map]. `GameplayUnitWork(unit, build index)` (the DLL;
  -1 stops the work, the route mission sends 0) names a build. `Builds.EntityEvent` names one of six entity events:
  SHOVEL, BUILD, IRRIGATE, MINE, CHOP and RAILROAD (on this database 33 builds use BUILD, 10 MINE, 6 IRRIGATE, 3 CHOP,
  1 SHOVEL, 1 RAILROAD). The worker's six work clips (ec 1910-1960) are named for tools:
  - 1910: a shovel;
  - 1920: a rake;
  - 1930: an axe;
  - 1940: a pickaxe;
  - 1950: a hammer;
  - 1960: a railroad.

  Only SHOVEL -> 1910 and RAILROAD -> 1960 match by name. MINE -> 1940, CHOP -> 1930, IRRIGATE -> 1920 and
  BUILD -> 1950 are a reading by meaning. The tables `AnimationCategories` and `AnimationPath_Entries` are empty, and
  `EntityEvent_AnimationPaths` maps events to animation paths, not to ec codes. So there is no source table: `work` is
  `BadEvent` unless the caller gives a work table (`parseWorkTable`, the driver's `--work-table`, lines `<EntityEvent>
  <TAB> <code>`). The name-and-meaning candidate above is for 4405 to confirm live, one worker per build kind. The
  timeline cannot express the work stop (-1): open.
  **Answered 2026-11-16 by issue 0439 [C: the EXE's `GameplayUnitWork` handler (slot 153, 0x564960) read with a headless
  Ghidra pass, read-only, on a scratch copy of the `civ5` project (`reverse-engineering-setup.md` 2); the listing stays in
  scratch, D83].** The EXE does not use `Builds.EntityEvent`: it compares the build's **Type** (the build info's type
  string) against a fixed list and asks for a tool by name, which a name table turns into the code (the EXE's own
  label list for these names, 0x61A250, pairs each name with its code: shovel 1910, rake 1920, axe 1930, pickaxe 1940,
  hammer 1950; also work 1900, idle 1000, fortify 1500, activate 1990 and the other action codes of 6.5):
  - `BUILD_FARM`, `BUILD_TERRACE_FARM`, `BUILD_POLDER` -> rake **1920**;
  - `BUILD_MINE`, `BUILD_QUARRY` -> pickaxe **1940** (the mine also starts a sound for the active player's unit);
  - `BUILD_ROAD`, `BUILD_RAILROAD`, `BUILD_SCRUB_FALLOUT`, `BUILD_REMOVE_MARSH`, `BUILD_REMOVE_ROUTE` -> shovel **1910**;
  - `BUILD_REMOVE_FOREST`, `BUILD_REMOVE_JUNGLE` -> axe **1930**;
  - every other build, and a build index whose info is missing -> hammer **1950**;
  - build -1 (the work stop) -> **`IDLE` (1000)**, in another field of the same message (the tools go in one field, `IDLE`
    and the fortify handler's `IDLE` / `FORTIFY` in another [C]; what the two fields do in the EXE's unit system is not
    read [I]: the controller writes both to path 4, which every work-capable machine reads: `worker_state` 3 -> 5 and
    6 -> 2 on `P4==1000`).
  So **1960 (the railroad clip) is never asked for**, the railroad is dug with the shovel, and the name-and-meaning
  candidate above was wrong for MINE (right code), CHOP (no build uses the entity event's name), BUILD (hammer: right for
  most) and RAILROAD (1910, not 1960). The bridge now applies this rule (`workTool`, `bridge.h`); a caller's work table
  still replaces it; the timeline writes the stop as `action work -` and the bridge asks 1000 for it. The handler tests
  neither the unit's type nor its machine nor `EnableActions` [C]. A member whose machine never reads the tool's code
  (the great general's citadel, the work boat's fishing boats: 1950) takes no request (the no-op rule of 5, 0439).
- **Move** stays 1400. The input's `move` has no field for a shuffle (1450) or a charge-move (1120 is `action
  charge`), so 1450 is never requested. Which moves the EXE turns into 1450 (`ShortMoveRadius` /
  `ShortMoveRate`?) is 0042's open question (6.5).
- **Tests** `render/tests/unitanim/test_bridge.cpp`, 15 cases:
  - every kind's code, and the figure set passed on;
  - EnableActions on a civilian and on a mixed unit;
  - one negative test per refusal: no art; a word lacking; anti-air without AttackSurfaceToAir; no Combats row; work
    without a table, with an unknown build, and with a NULL EntityEvent; paradrop; damage the figure rule refuses;
    each work-table text error; the tables missing from the database;
  - through the controller: codes reach the states; damage 0 -> 30 -> 35 -> 99 on 4 figures takes out figure 3, then
    none, then figures 2 and 1 (99 % caps at n - 1 dead), and the dead keep their death state while an attack reaches
    the living one; a refusal names the member and leaves no program (D118);
  - `anti_air` round-trips in the `.c5anim` file and refuses an argument.

### 14.2 The driver (`driver.h`, `src/unitanim/driver.cpp`, `render/tools/unitanim/`)

`unitanim play --timeline FILE.c5anim --game INSTALL --db DB --out DIR [--mods "A;B"] [--work-table FILE]
[--no-c5units]`. The steps, in this order:

1. It reads the timeline with its start scene first (`readTimelineFile`), so a bad file is refused without an install.
2. It opens `gamefs` with the mods as extra loose roots, the database (`SqliteTables`, read-only and immutable),
   0041's `ArtResolver` and string databases, and the bridge's tables.
3. It plays every frame through the controller with `ActionBridge`.
4. It writes into DIR:
   - `programs.csv`: `frame, seq, present_time, player, id, figure, node, kind, clip, ec, t, a, b, w, mask, state`.
     There is one row per pose-program node. `state` (the machine's state id) is an extra column after section 4's.
   - `triggers.csv`: `frame, seq, present_time, player, id, figure, type, ec, trigger, time, offset, event, bone`.
   - `status.csv`: `frame, seq, player, id, status, text`, with every unit in every frame.
   - `frames/<seq, 10 digits>.c5units`: written by 0041's writer for frames where every unit is Ok and every figure
     has one Sample node.

   `frame` is the 0-based index in the file and `seq` the frame record's number. Floats are `%.9g`, and text fields
   are quoted only when needed.

**Exit codes** (`DriverExit`):

| Code | Name | When |
|---|---|---|
| 0 | `Ok` | every frame played and written |
| 1 | `UnitsFailed` | everything was written, but some unit had a status other than Ok; the first is named |
| 2 | `Usage` | bad arguments; the usage line is printed |
| 3 | `TimelineRefused` | a bad or missing file or start scene |
| 4 | `InputsUnavailable` | the install, the database, its tables or the work table |
| 5 | `OutputFailed` | an output could not be written |
| 6 | `FrameRefused` | `Controller::apply` refused a frame |

**`ClipGraphFactory`** is the product's `GraphFactory`. The controller's ec map carries no durations, so the factory
completes it with each clip file's animation-0 duration (as 0041's clip cache binds it) and calls 4409's `bindGraph`.
A clip that does not read keeps 0, and its name and reason are appended to the `EcNotBound` text.

**Tests** in `render/tests/unitanim/test_bridge_driver.cpp`, 4 cases, all run in-process on the test doubles:
- A synthetic 40-frame `.c5anim` with a start scene: 120 program rows, 40 status rows and 40 `.c5units` frames, one of
  which reads back with the programs' (clip, t); exact rows for fortify and a one-figure attack; `--no-c5units`.
- A unit of an unknown type: `UnitsFailed`, named in `status.csv`, no `.c5units` frame, and the other unit still
  played.
- `OutputFailed` (an output folder below a file) and `FrameRefused` (a negative dt).
- `driverMain` exit codes: 2 five ways; 3 for a bad header, a missing start scene and a missing file; 4 for no install.

**Smoke run** (2026-11-15 22:47, `render/evidence/0374/4404/smoke.py`, output `smoke.out.txt`). One warrior
(`ART_DEF_UNIT__WARRIOR`, owner Polynesia, Ancient), idle for 300 frames at dt 1/30, seed 1. Inputs: c5h-c's install
with its three mods and the database `F:/c5-64bit-work/0327/db/Civ5DebugDatabase.db`; render build `953158f5c8`
Release. Outputs are on `F:/c5-64bit-work/0374/4404/smoke/result/`, not committed.

- **Totals.** Exit 0, 300 frames, 12 figures, 0 failed units, 31 clip files read for durations, 1.20 s wall time for
  the 300 frames (an indication).
- **Programs.** 3,642 program rows: 3,621 samples and 21 blends. 288 of 300 frames are single-source and written as
  `.c5units`.
- **States** (rows): idle loop 1: 3,417; fidget 3: 189; swap 5: 34; fidget chooser 6: 2.
- **Idle variants.** 5 figures started on ec 1000 and 7 on ec 2000 (the `R0` draw of 0.6 / 0.4; with 12 figures,
  either reading of which value is A fits).
- **Random events.** One figure swapped (2020), with the swap graph's own blend (w 0.26 and 0.74 on its first two
  frames). One fidgeted (1040), entered through the chooser with the 0.3 s X1 rule. That is 2 events in about 3,600
  figure-steps, against 2.5 expected at 0.0007 per step.
- **Clip times.** They advance by 1/30 per frame and wrap at the idle clips' 6.0 s; a figure that stayed idle shows
  t = 3.966686 at frame 299.
- **Triggers.** None (the warrior's idle clips carry none).
- **The fidget fades twice** [C for the program; I for which reading is right]. The graph's own fade and 4403's X1
  rule (`defaultCrossFades()`, chooser 6 into fidget 3, linear over 0.3 s) both run on the same transition.
  - Each frame's program has the frozen idle sample (node 0), the graph's two sources, the graph's blend (node 3:
    0.028, 0.104, 0.216, 0.352, 0.500, 0.648 at 0.2 s, ...: 4409's smoothstep over 0.3333 s), and the X1 blend over
    all of it (node 4: 0.111 per step).
  - At 0.2 s the fidget's share is therefore 0.667 x 0.648 = 0.432, against the 0.680 that 4407 measured (7.3). Either
    fade alone gives about that value (0.648 or 0.667).
  - One of the two should go: probably the X1 rule, since 4409's reading puts the fade in the graph. This is for the
    lead and 4406's oracle to decide. The driver reports what the runtime does.
- **The first frame took 0 steps** [C]. A dt written as `%.9g` (0.0333333333) is just below h = 1/30, so 4403's
  accumulator carries it and the steps lag one frame behind the frame count. A recorded sequence's dt from Present
  times does not have this exactness problem; offline timelines should write dt with more digits, or accept the lag.

**Builds and sanitizer** (2026-11-15):
- Plain build (`render/build.py --test -R c5r_unitanim`, clang-cl, 22:46): 9 of 9 passed, 0 skipped.
- MSVC ASan (`--asan --mem-cap-mb 4096 --test -R "c5r_unitanim|c5r_units_test_scene_file"`, 22:50): 10 of 10
  passed, 0 skipped, 0 ASan reports, peak 187 MB.
- The ASan build of the driver on the same smoke inputs (22:51) gave exit 0, no AddressSanitizer report, and a
  `programs.csv` byte-identical to the plain build's (3.5 s instead of 1.2 s).

### 14.3 Open (4404)

- The fidget's double fade (14.2): which of the two fades stays.

- The work-table candidate (14.1) and the work stop (-1).
- Which DLL call requests a civilian's sleep (1500).
- Revivals when damage falls.
- 1450 shuffle moves (0042).
- `kUnitMissionEnd` (29), which section 3 left to 4404, has no request from the files (6.5). The bridge has no event
  for it, and none is added until a capture shows what the EXE does.

## 15. The oracle run (4406)

Written 2026-11-15 23:01-23:40 EST by the 4406 worker (branch `task-4406-oracle-run`; the same worker as 4404 B). This
is round 1, on the data available: 4407's tables, 4405's Q1 and 0041's catalogue. 4405 B's sequences are not in it;
the worker resumes when they land. Offline, no game instance.

- **The command:** `python -I -X utf8 render/tools/unitanim/oracle_anim.py --exe <render build>/tools/unitanim/unitanim.exe --out F:/c5-64bit-work/0374/4406/oracle`.
- **The run:** 2026-11-15 23:28-23:33, 166 driver runs, 302 s.
- **Inputs:**
  - c5h-c's install with its three mods;
  - the database copy `F:/c5-64bit-work/0327/db/Civ5DebugDatabase.db`;
  - 4408's parse `F:/c5-64bit-work/0374/4408/census/census.json`, used for the reachability causes;
  - 4407's `table_4407.csv` and `blends_a.json`;
  - 4405's `Q1.csv`;
  - 0041's catalogue scenes `render/evidence/0041/4355/scenes/`.
- **Outputs:** `F:/c5-64bit-work/0374/4406/oracle/` holds per-step tables per clock reading. The catalogue table of the
  final round is in `oracle_cat2/`. `render/evidence/0374/4406/` holds `summary.json`, `summary_catalogue_r2.json` and
  the 0041 check logs: numbers only (D83).
- **The tests:** `test_oracle_anim.py`. Three guard cases pass (exit 4: OUT outside the work folders; exit 2: an
  unknown step or clock; exit 3: an input missing), and one positive transitions run (UNITANIM set). 4 of 4 passed
  (23:38).
- **The suites:** plain unitanim suites 12 of 12 passed, 0 skipped. MSVC ASan (`--asan --mem-cap-mb 4096 --test -R
  c5r_unitanim`, 23:39): 12 of 12 passed, 0 skipped, 0 ASan reports, peak 82 MB.

### 15.1 Changes made for the oracle (all additive)

- **No default EXE-level cross-fade** (item 1, lead's decision 22:59). `defaultCrossFades()` is empty. `CrossFadeRule`
  stays as data (`ControllerOptions::fades`, the driver's `--fade`). `test_controller` passes its rule explicitly and
  checks that the default gives no fade.
- **K6 continuous clock** (lead 23:15; MC #2089 at 23:20 made it the default). `ClockMode::Continuous`: per unit one
  update per frame of `min(dt, max_dt = 0.2)`, with nothing carried. `ClockMode::Steps` (the whole-step accumulator
  of 13.1) is kept for two tests and as a comparison line only. Supporting changes: `Controller::step` takes the step
  length; the driver has `--clock steps|continuous` (default continuous); there is a new test
  `test_clock_continuous.cpp`.
- **Driver additions:** `--skip SEQ:PLAYER:ID:FIGURE|*:STEPS` (K6 skips as data, reported, never folded into a
  tolerance), `--fade`, and `ControllerOptions::strings` set for 4410's pre-check.
- **0041's `oracle.py`:** its default height field is now the TINY_0009 bundle as re-recorded at format 3
  (`F:/c5-64bit-work/scene-library/bundles/C5H-0301-TINY_0009_20261115-225124_workstation/`, MC #2108; its 35 members
  are present with their sizes, and the height field's SHA-256 equals the manifest's). The old bundle is in
  `scene-library/retired/`. `test_placement_catalogue.cpp`'s default (0364's) is unchanged, and the runs below pass the
  retired bundle to it through `C5R_UNITS_HEIGHTFIELD`.

### 15.2 C on the catalogue (DoD C, second part)

0041's 20 catalogue scenes carry 4354's fits as figure lines: 229 units, 1,088 fitted figure lines. For each unit,
`oracle_anim` does the following:
- it finds its forced-choice points by running in forced mode and reading `ForcedChoiceMissing`;
- it plays frame 0 with the variant point at 0 and at 1;
- it chooses, per figure, the value whose clip is the fitted one;
- it requires frame 0 (dt 0) to give the fitted clip at the fitted t, within 0.005 s.

| Result | Figures | Notes |
|---|---|---|
| pass (single source) | 994 | t equal to the fit, max \|dt\| = 0.000000 |
| pass on the main sample of an aim program | 15 | 4410's members (tank, panzer, modern armour, mobile SAM): a 3-node program, the idle sample under a masked aim blend (1710 centre, w 1), the idle clip and t equal to the fit |
| not covered: the fitted clip is reachable from the default state but is not its first graph | 58 | fidgets (1040 / 2040: 43), transitions (1020 / 2020: 14), one idle B (the F Company's 10th figure). They need the figure's history; recordings from creation (4405 B) cover them |
| **fail: not reachable from the default state** | **17** | **airplanes** (fighter, SPAD, Zero, WW1 fighter and bomber, bomber, jet fighter, stealth bomber, B-17) fitted on their idle clip on the ground: their machine's default writes `S4=1400` (flying, 6.2), and nothing in the timeline requests their landing (1000) [C for the controller; I for the EXE: a parked plane is requested 1000 by some DLL call not in 6.5's table]. Open for the bridge / U6 |
| unit `BadLogicFile` | 4 units (destroyer, dreadnought, fleet destroyer, Yamato) | `edited_aim_ms_idle_graph.dge: pose edge to node -1`: VP's edited modern-ship graph. 4410 lists these 4 as `TrackMaskNotBound`, but the evaluator already refuses the graph at bind time |

225 units are Ok and 4 fail (the ships). By the 0.005 s tolerance, every covered figure plays the fitted clip at the
fitted t, so the scene's figure line and forced choices reproduce 4354's fits.

Correction 2026-11-16 00:16 (round 2, 15.8): the 17 airplane figures now pass (an air figure is created parked). The
table's "fail" row is the round-1 result. The 4 ships' figures are counted under not covered in round 2, which makes
1,011 pass, 15 pass on the main sample and 62 not covered.

### 15.3 C on the recorded sequences (Q1, P01)

Each recorded figure (rider for horse: 8.6) whose first frame is an idle loop (ec 1000 or 2000) starts there: a unit
of its art row, the figure of its member, its t, and its variant forced. The recorded Present steps become the
frames: each captured pair is two presented frames, each half the Present step. Each later captured frame must give
the recorded ec at the recorded t, within 0.005 s.

| Sequence | Pass | K6 skip (reported) | Not covered |
|---|---|---|---|
| Q1 (1355-1361, 34 figures) | 124 rows | 0 | 12 rows: 3 figures starting in fidget A (a non-default start) |
| P01 (4677-4683, 17 figures) | 65 | 1: `P01/Warrior4/0`, 6 steps (0.2 s) at 4679 | 2: that figure's fidget frames (a random decision not in the table) |

The two clock readings give the same rows here: every frame is saturated, so each frame advances 0.2 s under either.
The data that separates them (frame caps 30 and 60) is 4405 B's.

### 15.4 P01 frame 4681 scored both ways (item 1)

The fidget figure `P01/Warrior4/0` (member figure 9 of the warrior) was played from 4677 at its t (5.0342). The
recorded lost 0.2 s and the fidget decision are given as data:
- continuous: a skip of one update at 4678, the roll and R1 forced on 4679, the fidget entered on 4680;
- steps: 6 steps skipped at 4679, the roll on 4680's first step, 4 steps of 4680 skipped.

4407's blend fit at 4681 (`blends_a.json`) is fidget A at t2 0.1989 with **w 0.686**, the old idle A held near 5.00
(7.3 gave 0.680 and 5.40 from its scan).

| Reading | Fidget t at 4681 | Fidget share at 4681 | w − recorded | Within 0.02 |
|---|---|---|---|---|
| no EXE-level fade (the default now): the graph's own fade, 4409's smoothstep over 0.3333 s | 0.2000 | **0.648** | **−0.038** | no |
| the former X1 rule on top (linear 0.3 s) | 0.2000 | 0.432 | −0.254 | no |

The same holds under both clocks. The graph's fade alone is 0.038 short: smoothstep at 0.2 / 0.3333 gives 0.648,
while a linear ease over the same 0.3333 s would give 0.600 and linear over 0.29 s about 0.69. **The ease of block
type 3 is the open quantity** (4409 named smoothstep [I]). One point cannot settle it: 4405 B's per-frame fades will.

**Finding [C]: the "From" idle is played from t 0, not continued.** In the controller's program at 4681, the
idle-A sample under the fade is at **t 0.200**, the state time, against the recorded held ~5.0-5.4. The controller's
`enterState` calls `GraphInstance::restart()`, not 4409's `restartFrom(previous)`, so a source with ContinueOffset 1
starts at 0 instead of taking over the previous state's clip time. For the lead: a one-line change in `enterState`
(4403's code), with its test. 4406 did not make it, because the change is outside this task's scope.
Correction 2026-11-16 00:16 (round 2, 15.8): fixed. `enterState` now calls `restartFrom`, and the From idle is
held by default (MC #2117). The P01 numbers are re-scored in 15.8.

### 15.5 D: transitions, clock and random parts

**Transitions** (`transitions.csv`; a warrior at dt 1/30 from the request, figure 0; both clocks alike). The
controller's sequences, with the new state's main-sample share per frame, are:
- **idle -> fidget** (roll forced): the chooser state 6 for one frame, then 3. The fidget's share is 0.028, 0.104,
  0.216, 0.352, 0.500, 0.648, 0.784, 0.896, 0.972, 1 at 1-10 frames of state time.
- **idle -> run** (`move`): the start-run state 4 on the request's frame. The run's share is 0.13, 0.42, 0.74, 0.97
  over 4 frames, then the run loop 17 at t 0, pure, after 0.15 s.
- **idle -> attack**: enter combat ready (8, 1600) for 1.0 s, then combat-ready idle 12 at 0 (pure), then the attack
  14 (1100) at 0 (pure), back through 20 to 12.
- **combat-ready / attack -> charge run** (`action charge`): 25 attack-to-run, then 17 at 0 (pure), 0.2 s later.

Against the recordings (7.3):
- P02's first frame of run, pure at t 0, and P04's charge run, pure at 0.0000, fit the controller's run-loop entries
  (17 at 0, pure).
- P02's figures already at 0.098-0.495 s 0.07 s after the order are the staggered starts that 4405 B measures as data
  (lead, 23:20); no offsets were guessed.
- The weight curves themselves need 4405 B's per-frame captures (one point exists, 15.4).

**K6.** Q1 and P01 fit both readings (15.3). The step reading is a comparison line only (MC #2089).

**Random parts** on the real omni machine (free mode, the files' values; `summary.json` random). Each is within the
99.9 % binomial interval:

| Decision point | Observed | Expected | ± (99.9 %) |
|---|---|---|---|
| `RandomRoll` 0.0007 (idle loop), 95,418 updates in state 1 | 70 fires | 66.8 | 26.9 |
| R1 fidget share (0.3), 70 decisions | 20 fidgets | 21.0 | 12.6 |
| R0 idle A share (0.6), 12,000 figures (1,000 warriors) | 7,065 on ec 1000 | 7,200 | 176.6 |

The live fidget count beside the expected one (X7): Q1 gives none (8.6); left to 4405 B's spaced singles.

### 15.6 E and 0041's regression floor

- **0041's `oracle.py --no-c` and `c5r_units` on this branch** (23:30-23:33, `render/evidence/0374/4406/run_0041_oracle.sh`,
  log `oracle0041_retired_hf.txt`, with the retired 070309 height field):
  - `c5r_units`: 9 of 9 passed, 0 skipped;
  - oracle exit 0; placement 1,083 of 1,135 heights within, 0 failed;
  - SHA-256 of `pose_rows.f32`, `pose_list.tsv`, `report_units.tsv`, `placement_report.tsv` and `pose.tsv` (without
    its timing column) all **equal to 4409's** (`render/evidence/0374/4409/before_after.txt`): the merged numbers
    are unchanged.
- **The same with the new default height field** (the format 3 bundle `..._20261115-225124_workstation`,
  `run_0041_oracle_newhf.sh`, log `oracle0041_new_hf.txt`, 23:36-23:37): oracle exit 0, placement 1,083 of 1,135 heights
  within, 0 failed, water level 10.1167316, and the four file hashes equal to 4409's again. Its height field differs
  from the retired one byte-wise (a re-recording) but gives an identical placement report. **The new default is
  usable** (MC #2108's check).
- **E through the controller:**
  - Single-source programs: 1,009 catalogue figures reproduce the fitted (clip, t) exactly (15.2). Their palettes are
    therefore 0041's own rows, bit-identical by 4409's test, and these equal the captured palettes as in 0041's run.
  - Blended programs against captured palettes (the P01 4681 cross-fade block, the aim layers): **not run in this
    round**. `unitscene` has no command that takes a pose program yet, and the P01 block's program differs from the
    recording in its From-clip time (15.4) and its weight. This is open for round 2, after the `restartFrom` fix and
    4405 B's blocks. Correction 2026-11-16 00:16: round 2 ran it (`unitscene program`, 15.8).

### 15.7 Not verified / open (4406)

Round 1's list, as of 2026-11-15 23:42. Updated 2026-11-16 00:20: items 2-4 below are done in round 2 (15.8), which
also holds what is still open.

- 4405 B's sequences (cross-fade curves, K6 at two frame caps, combat skips, staggered run starts): round 2.
- The ease of block type 3 (15.4) and the `restartFrom` gap (15.4).
- Airplanes on the ground (15.2) and the four VP ships' graph (15.2).
- E for blended programs (15.6).
- The aim members' pass is on the main sample only: the aim pose is not compared, because no capture of an aim blend
  is fitted.

### 15.8 Round 2 (2026-11-15 23:46 to 2026-11-16 00:25)

Written by the 4406 round 2 worker. The branch is `task-4406-oracle-run`. The build tree is
`C:/c5-64bit-work/render-build/74063e0d99/Release`. The inputs are those of round 1 (15): c5h-c's install with three
mods and the 0327 database copy. The clock is continuous, the default since MC #2089. Outputs are under
`F:/c5-64bit-work/0374/4406/r2_*`; the summaries (numbers only) are under `render/evidence/0374/4406/r2/`.

**1. `enterState` continues the previous clip times [C, test].** `enterState` now does the following:
- it collects the previous graph's active sources (`f.cur->active(before, ...)`) before `f.cur` switches;
- it calls `GraphInstance::restartFrom` with them, so a ContinueOffset 1 source takes over the clip time of the same
  ec;
- a start (no previous graph) restarts from 0 as before.

The test is `test_controller_start.cpp`, `entered_state_continues_previous_clip_time`. It runs a graph double that
honours ContinueOffset, with and without it.

**P01 4681 re-scored** (`r2_seq2/summary.json` `p01_4681`). The From idle A is entered at 5.434 (on 4680). At 4681 the
recording says the following:
- 7.3's scan gives 5.40 ± 0.06, held;
- 4407's fit `blends_a.json` gives t1 5.003 at w 0.686;
- 4407's fit `blends_b.json` gives 5.404 at w 0.681.

The fidget share is the same under every reading below (no reading changes the weight).

| Reading | From idle t at 4681 | Fidget t | Fidget share | w − 0.686 |
|---|---|---|---|---|
| default since MC #2117: From side held | **5.434** | 0.200 | 0.648 | −0.038 |
| `--run-from` (the graphs as read, Speed 1) | 5.634 | 0.200 | 0.648 | −0.038 |
| former X1 rule (`--fade`, linear 0.3 s) on the held default | 5.434 (twice) | 0.200 | 0.432 | −0.254 |

The files' ContinueOffset agrees with the recording at the hand-over: 5.434 against 7.3's 5.40, inside the scan's
spread. It does not agree after it: the graph runs the From idle on to 5.634, while the recording holds it. This went
to the lead as a PREMISE at 23:55, which made it premise change 4.

**MC #2117:** a From-side source with ContinueOffset 1 starts at the previous state's clip time and is held there for
the fade. It is data per graph role, default on: [L] from one point (P01 4681), [I] in general. It is to be confirmed or
rejected on 4405 B's transitions before the MERGE-REQUEST, and never tuned.

The implementation is `GraphOptions::hold_from_continue` (`graph.h`, default true):
- the rule applies to a ContinueOffset 1 source reached through the From input of an unmasked Blend **whose Param is a
  fade weight** (a hint-5 callback, or a BlockCurve of one), through Selection and Subgraph. *(Corrected 2026-11-25,
  ticket 0782, unit-server-scene.md 19.12 [C]: was "an unmasked Blend's From input", any weight. Under
  `mounted_running_loop.dge`'s path-7 turn blend that froze every mounted unit's run at its entry time for the whole
  walk. Restricting it to fades changes only that graph among the 112 reached graphs: 6 held sources; the 159
  fade-held sources stay held.)*
- that source is sampled at its start, the taken-over time, and the To side and the weight are untouched;
- a ContinueOffset source that is not on a From side runs on, as the idle-continue graphs need;
- the three-argument `bindGraph` uses the defaults;
- the driver's `--run-from` turns the rule off, as a comparison line;
- `ClipGraphFactory::options` carries it.

Tests:
- `test_graph_hold.cpp`: held against running, a lone continue source runs on, and the default is on;
- `test_graph.cpp`'s helper binds with the option off, so its as-read expectations stay unchanged.

The miss in the weight (−0.038, smoothstep of block type 3 over 0.3333 s) is unchanged and still open for 4405 B's
fades.

**2. Parked planes [I; the catalogue fits are the [L] support].** The lead's minor decision is that an air-domain
figure is created parked. In `Controller::create`, a member whose `MemberArt::domain` is `Air` and whose machine tests
path 4 = 1000 gets a start request of 1000. `Controller::settle` applies it after the default chooser's own write
(`airplane_state_01`: 6 -> 1 writes `S4=1400`). The machine then follows it at once, with no time, until no socket holds
(1 -> 8 -> 7, the idle).

Effects and limits:
- The missile machine never tests 1000 and is left alone.
- The unit input has no flying flag: flight is requested by `move` / attack (1400 / 1100 / 1180), as before.

The tests are in `test_controller_start.cpp`:
- `air_figure_created_parked`: state 7 at t 0, and `move` gives 1400;
- `land_figure_and_machine_without_landing_keep_the_default`.

The catalogue (`r2_cat`, 2026-11-16 00:00) gives:
- **all 17 airplane figures pass**: the idle clip at the fitted t, max |dt| 0.000000;
- 1,011 pass on a single source, 15 pass on the main sample, 62 not covered (the 58 that need history, plus the 4 ships'
  figures under `BadLogicFile`);
- 225 units Ok and 4 failed.

**3. The 4 VP ships** (destroyer, dreadnought, fleet destroyer, Yamato). Their first refusal is `BadLogicFile`
(`edited_aim_ms_idle_graph.dge: pose edge to node -1`, at bind time), not `TrackMaskNotBound`. Dated notes went into
6.1 and 13.3. They stay refused (D118).

**4. E for blended programs [C for the path; L for the numbers against captures].** The parts:
- `unitscene program` (additive) gives a pose program's palette rows through `poseFigureProgram`, the step behind
  `ModulePose::rowsProgram`. Masks are resolved from the model with `resolveTrackMasks`, as `ControllerPose` does.
- The driver now also writes `masks.csv`: the masks that `programs.csv`'s `mask` column indexes.
- `oracle_anim.py --steps palettes` (with `oracle_palettes.py`) takes the controller's program for each block, renders it
  unplaced, and solves the placement as `posefit.py` does (rotation, scale, translation by least squares). It scores the
  result with 0041's tolerances: rotation element < 1e-3, translation < 0.01.
- The run is `r2_pal2`, 25 driver runs, 56 s.

The path is checked against 4407's own fit: the blends_a program through C++ gives 0.001957 / 0.0230, which equals
4407's Python numbers.

| Block | Program | rot elem max | trans max | rot max (deg) | Within |
|---|---|---|---|---|---|
| P01 4681 id 9 (warrior) | controller default (held 5.434, fidget 0.2, w 0.648) | 0.00446 | 0.0732 | 0.287 | no |
| P01 4681 | controller `--run-from` (5.634) | 0.00368 | 0.0524 | 0.290 | no |
| P01 4681 | controller with the former X1 rule | 0.0369 | 0.597 | 2.34 | no |
| P01 4681 | 4407 `blends_a` fit (5.003, 0.1989, 0.686), reference | 0.00196 | 0.0230 | 0.123 | no |
| P01 4681 | 4407 `blends_b` fit (5.404, 0.1995, 0.681), reference from 4407's file | 0.0004 | 0.0036 | 0.025 | yes |
| U4353 2676 id 51 (mech) | **controller idle program: idle B at 4.8985 under the aim layer (state 1)** | **1e-6** | **5.6e-5** | 4.3e-5 | **yes** |
| U4353 2676 id 46 (mech) | controller idle program: idle B at 0.3205 under the aim layer | 0.00032 | **0.0120** | 0.023 | no (translation) |
| U4353 2676 ids 46 / 51 | controller idle B -> fidget B from `blends_a`'s t1, fidget at t2 1.0555 (fade over: fidget B under aim) | 1.25 | 4.39 | 79.8 | no |
| U4353 2676 ids 46 / 51 | 4407 `blends_a` fit (idle B x fidget B), reference | 0.258 | 1.48 | 16.3 | no |

Readings:
- **P01:** at 4681 the controller's palette is outside 0041's tolerances under every reading. The weight dominates:
  4407's `blends_b` fit, at nearly the held time (5.404) but with w 0.681, is within. The held default (5.434) is
  slightly worse in the palette than running on (5.634), at the same wrong weight. The block-type-3 ease is the open
  quantity, for 4405 B's fade curves.
- **The 4353 mech blocks are not cross-fades: they are idle B under the aim layer [L].** This corrects 7.3's "not
  explained" and 4407's two-clip fits. Evidence:
  - `mech_idleb_aim_fits.json` scans idle B's t under the controller's aim layer (aim centre at 0.0222, the masked blend
    of `AIM_IDLE...`, `TrackMaskAIM_LAYER` resolved from the model's bones);
  - block 51 is reproduced at t 4.8985 to 1e-6 (rotation element);
  - block 46 fits at 0.3205 within rotation (0.00032) but is 0.012 off in translation;
  - idle B without the aim layer is 1.25 deg away at its best t (4.3385).

  The controller's own idle program, at that start time, gives the same numbers (state 1). The 7.3 correction is for
  the lead (7.3 is not this task's section).
- **This is also the first capture check of a resolved aim mask [L]:** 13.3's binding-by-bone reading reproduces block 51
  exactly. 13.3's note that "a masked centre-aim pose equals the unmasked body pose below the catalogue's residual" does
  not hold for the mech: the aim layer moves it by about 1.25 deg (for the lead, 13.3).
- Block 46's 0.012 is open. It could be a fade still running (an idle transition) or the aim centre's own time.

**K6 action skip as data (lead 00:21, from 4405 B's Q4a and 4407's P04 [L]):** over two presented frames, the unit
just given a melee order advances 0.2 s while the defender and a bystander advance 0.4 s. This is now a rule held as
data, not a tolerance: `ActionSkip` (`clock.h`), `ControllerOptions::action_skip`, the driver's
`--action-skip none|order|next` (default none).

How it works:
- a unit given an `action` event takes no step on one frame: the order's frame (`OrderFrame`) or the frame after it
  (`NextFrame`);
- a skipped update leaves its clips, state time and machine as they were, so the request is seen on the next update;
- a frame with no step (dt 0) does not consume the rule.

The recordings cannot separate the two frames. **The reading named here is `OrderFrame` [I]**; both are to be scored on
4405 B's tables (not landed, so not scored yet). Every other skip stays reported per figure with its count. The test is
`test_action_skip.cpp`: none, order and next, with a bystander advancing and a dt-0 frame.

**5. 4405 B's tables:** at 00:10 they had not landed: `F:/c5-64bit-work/0374/4405/bones` and `captures` were still being
written, and the epic branch had no 4405 B merge. Not run; the lead resumes this task when they land.

**Checks (2026-11-16):**
- plain unitanim suites 15 of 15 passed, 0 skipped (with `test_action_skip`);
- MSVC ASan (`--asan --mem-cap-mb 4096 --test -R c5r_unitanim`, 00:24): 15 of 15 passed, 0 skipped, 0 ASan
  reports, peak 82 MB;
- `test_oracle_anim.py` 6 of 6 passed (two new: the palettes step's missing input is exit 3; the program text form);
- 0041's `oracle.py --no-c` on the round's head (`dfcf39eb`, default height field, 00:19-00:20,
  `r2/run_0041_oracle_r2.sh`, `r2/oracle0041_r2_hashes.txt`): exit 0. The SHA-256 of `pose_rows.f32`, `pose_list.tsv`,
  `report_units.tsv` and `placement_report.tsv` are all equal to 4409's, so `unitscene`'s existing outputs are
  unchanged by the new `program` command.

**Not verified / open (round 2):**
- the held From side on any transition other than P01 4681 (4405 B);
- the block-type-3 ease (P01's w);
- mech block 46's translation;
- an aim off centre;
- K6 at the two frame caps, and the staggered run starts (4405 B).

### 15.9 Round 3 (2026-11-16 00:52-01:05): 4405 B's tables

Written by the 4406 worker, round 3. It was granted by MC-B and briefed by the second lead-0374. Offline, on the epic
at c43a9569 (4405 B merged); the build tree is `C:/c5-64bit-work/render-build/74063e0d99/Release`.
- Inputs: `F:/c5-64bit-work/0374/4405/tables/table_4405.csv` (1,324 rows), `render/evidence/0374/4405/summary_4405.csv`,
  `runstart.csv`.
- Outputs: `F:/c5-64bit-work/0374/4406/r3_seq` (sequences) and `r3_tr` (transitions); summaries in
  `render/evidence/0374/4406/r3/`.
- Tolerances as in section 10: t 0.005 s, w 0.02. None was changed.

**1. C on 4405 B's multi-frame sequences.** `oracle_anim.py --steps sequences` now also reads `table_4405.csv`. It
reads one followed figure-mesh per key, with the horse mesh standing for its rider, and the models of the barbarian
and the archer. It does not read the singles SR30, SR60 and SA: they are one capture per process, so they have no
frame-to-frame clock to play. As in 15.3, a figure is played when its first captured frame is an idle loop (1000 /
2000). Every other start is named "not covered" with its ec: the scene starts the default state only.

| Seq | Pass | Fail | Not covered (rows: starting ec) |
|---|---|---|---|
| Q2a | 51 | 0 | 0 |
| Q2b | 51 | 0 | 0 |
| Q3a1 | 26 | 0 | 6 (1020 transition 3, run 1400 3) |
| Q3a2 | 20 | 0 | 48 (stop run 1440 / 2440, and the idles after them) |
| Q3b | 0 | 0 | 5 (run) |
| Q4a | 10 (horseman bystander) | 0 | 64 (shuffle 1450: 44; fortify idle 1520: 20) |
| Q4b | 20 (horseman bystander) | 0 | 96 (fortify idle 40, combat-ready 1620 / attack / charge / run: 56) |
| Q5 | 0 | 0 | 107 (fortify idle 40, shuffle 45, run start 22) |
| Q6 | 0 | 0 | 19 (combat-ready 1620) |
| Q7 | 24 | 0 | 0 |

Totals for 4405 B: **202 rows pass, 0 fail**, 345 not covered. By transition kind, covered means idle continuing,
over bystanders and the figures left after damage. Moves, attacks, stops and fortified starts are all not covered. They
need either:
- a start in a non-default state (fortify idle by the fortified flag, combat-ready); or
- the order's frame in the timeline: the runner records the Lua return time, not its frame number.

Neither is in the tables, and both are named for whoever extends the timeline. Q1 and P01 are unchanged (124 and 65).

**2. K6 at caps 30 and 60 [L for the steps; I for the rule].** The steps are counted from `summary_4405.csv`: the clip
step of each followed figure over each captured pair of two presented frames.
- 471 steps of 0.400 and 4 of 0.398 (475) are reproduced by the continuous clock, `t += min(dt, 0.2)` per presented
  frame. Every pair's frames are longer than 0.2 s, so both saturate. This covers every unit outside an action, in every
  sequence.
- The attacker in Q4a gives 9 steps of 0.200 / 0.201. These are reproduced by the action skip as data (`ActionSkip`, one
  skipped update of the unit given the order), with every other unit at 0.4.
- Combat steps not reproduced by any rule held now (reported with counts, not folded into a tolerance):
  - 0.2: Q4b 8 (6 barbarian, 2 warrior), Q5 2 (archer), Q6 1 (+ one 0.199);
  - 0.0: Q4b 4, Q5 4, Q6 1;
  - 1.415 (one Q4b barbarian: a clip change inside the pair);
  - 0.667 (one Q5 archer: a loop wrap).

  So a unit in combat loses one or two updates per pair beyond the order's one. A rule for it ("each combat request
  skips one update" is a reading) is open [I].
- The off-lattice times of SR30 / SR60 (8.9) come out of the continuous rule by construction (any dt).

**3. `ActionSkip`: which frame.** The tables cannot separate the two frames.
- Q4a's loss of 0.2 falls inside the pair 662 -> 664, so the skipped update is frame 663 or 664.
- The run record (`F:/c5-64bit-work/0374/4405/runs/units_0374/Q4a.json`) gives the Lua call (0.125 s, held in one frame)
  and the capture request right after its return, but no frame number for the order.
- If the order took effect on 662, the data needs `NextFrame` (663); if on 663, `OrderFrame`.
- The attackers start in shuffle, so the controller does not cover them (1).

**The data does not support either reading.** `OrderFrame` stays the named one [I]. Settling it needs the order's frame
number in the run record (a runner field, 4405's tool).

**4. Held From side (MC #2117), clip end and stop variants.**
- **Held From side:** 4405 B caught no two-clip cross-fade (8.11: every blend fit w 1). The covered rows of (1) are
  idle loops with no state change, so `hold_from_continue` on or off changes no scored row. **Not determinable from
  these captures** (confirmed); P01 4681 stays the only point.
- **Clip end -> next state at 0 on the end frame [C, run]:** `transitions` `run_to_stop` and `run_to_stop_b` (warrior,
  dt 1/30, move on 3, arrive on 30):
  - run 17 -> stop 9 on the arrive frame (30); the stop graph's own fade is over by 32 (stop main from there);
  - stop -> idle on frame 60 with the idle at **t 0.00000** on that frame, advancing from the next one: 8.10's rule. It
    comes from `enterState` after the step's advance (13.1).
- **Stop variant [C, run]:** with the stop's option 0 the run stops in 1440 and goes to **idle A (1000)**; with 1, 2440
  -> **idle B (2000)**. That is 8.10's 21 of 21. The omni machine does it: 17 -> 9 draws path 0, and 9 -> 1 tests
  `P0==0 EC1440 | P0==1 EC2440`, the idle graph selecting by path 0.
- **idle -> run / attack / fidget:** unchanged (states 4 / 8 / 6 -> 3 on the order frame).

**5. Type-3 ease.** No fade point in 4405 B's tables (8.11), so there is no new point. P01 4681 stays **0.648 against
0.686 (−0.038), outside 0.02**. Not loosened.

**6. X4 removal (premise change 5, #2141; the MC's answer pending).**
- `ActionBridge::remove_fallen` (default false, the 4404 behaviour, at round 3; **default true since 2026-11-16 01:03, MC #2147**, lead-0374; `--damage-death` gives the old reading) turns a damage fall into `Request::removes`.
- The controller then erases those figures at once: no 1200, no program; the living keep theirs and take requests.
- The driver flag is `--damage-remove`.
- The test is `test_bridge.cpp` `damage_removes_fallen_figures_with_the_option`.

Q7 scored both ways (12 figures, damage 50 -> 6 fall):
- the 24 captured rows (697: 12; 699 and 701: 6) are idle loops and pass under both (1);
- the difference is the 6 fallen figures on 699 and 701. Removed, they are absent as in the capture: 0 mismatches.
  Under the default they would be drawn in death A / B (1.0 s): **12 figure-frames drawn that the capture lacks**.

The data supports removal. **Its timing is a caveat [L]:** 697 is the first frame captured after the damage call and
still shows all 12, so the removal reaches the picture 1-3 frames after the call, not on the call's frame. "At once"
therefore means at the feed's damage event as U6 delivers it, not at the Lua call. This went to the lead in the
hand-back.

**7. Run-start phase (`runstart.csv`).** The controller does not keep it. Every figure enters the run loop at t 0 on
the same update, and the timeline has no per-figure run phase or entry delay. Carrying 4405 B's measured phases (no fit)
needs a timeline field for 0042's C1.

**Checks:**
- plain unitanim 15 of 15 passed, 0 skipped;
- MSVC ASan (01:00): 15 of 15 passed, 0 skipped, 0 reports, peak 82 MB;
- `test_oracle_anim.py`: 6 of 6 passed.

## 16. Not verified

Written 2026-11-16 01:03 by lead-0374 at the MERGE-REQUEST; replaces the placeholder "(all)". The open items of the
epic, each with where it is measured and who takes it.
**Corrected 2026-11-16 02:18 (ticket 0395, section 17):** fortify on / off, the embark model and the aim unit are now
captured and scored (17.2-17.4); the combat skips, the held From side, the type-3 ease and the 4 VP ships stay open
(17.5 says why); each bullet below carries its dated note.

- **The type-3 ease of the cross-fade** (13.2, 15.4, 15.9 item 5): P01 frame 4681's fidget share is 0.648 against 0.686
  recorded (-0.038, outside the 0.02 tolerance). 4405 B caught no switch mid-fade, so there is no second point. Not
  loosened; open until a capture of a fade in progress. *2026-11-16 (0395, 17.5): still open, no second point (a single
  capture lands 0.27-0.40 s after its request); quintic smootherstep is the one named ease that meets P01 [I, not
  applied].*
- **K6's per-unit skip in combat** (8.10, 15.9 item 2) [I]: the continuous clock reproduces all 475 bystander pair steps
  and `ActionSkip` the Q4a attacker's 9 steps of 0.2; 12 combat steps of 0.2 and 9 of 0.0 in Q4b, Q5 and Q6 are not
  reproduced ("each combat request skips one update" is a reading). Which frame the skip falls on (OrderFrame vs
  NextFrame) is not separable: the runner records no frame number for the order; OrderFrame stays the named reading [I].
  *2026-11-16 (0395, 17.2): still open; one more skip kind seen: after a move order out of fortify idle, 4 of 8 waiting
  figures lost exactly 0.200 s (one clamped update), per figure.*
- **X1, the From side held during a fade** (MC #2117): [L] at one point (P01 4681), [I] in general; on idle -> run,
  idle -> attack and run -> stop no capture caught a fade, and the setting changes no scored row (15.9 item 4).
  *2026-11-16 (0395, 17.1-17.2): still open; idle -> fortify is complete (w 1.000) 0.267 s after the switch, and the
  pre-triggered capture fell before the order.*
- **X4** (9, corrected 2026-11-16, MC #2147): one warrior, one damage step, captures 0.4 s apart; a death clip under
  0.4 s is not excluded; the defender's death in combat was not caught (Q6).
- **Not captured in 4405 B:** fortify on/off (a mission on a non-active player's unit does not start in the active
  turn), the embark model's fit (its model file not identified), a unit aiming off centre (left / right aim unscored).
  *Corrected 2026-11-16 (0395): all three captured and scored (17.2-17.4): fortify on 2 pass and the clip end 2 pass;
  fortify off 4 pass on the clip and 8 not covered (staggered run entry); the embark model is the owner's override (the
  Polynesian war canoe [C]), canoe 0 passes at 4 frames and canoe 1 fails at 4 (a per-figure arrive, open for 0042);
  the battleship's full left aim is reproduced through the resolved mask (selector 0, w 1, rotation element 0.0).*
- **Run-start phases** (4405 B's `runstart.csv`): staggered per figure in the EXE; the controller starts every figure's
  run at t 0. Kept as data for 0042's C1 (a timeline field), not modelled here.
- **345 of 547 rows of 4405 B's sequences not covered** by the offline oracle: figures that do not start on an idle loop
  need non-default starts or the order's frame in the timeline (15.9 item 1). 202 pass, 0 fail. *2026-11-16 (0395):
  unchanged; 0395's own rows are in 17.5.*
- **Refused by design:** work codes (`BadEvent`, no source table for which work clip, 14.1); the 4 VP ships
  (`BadLogicFile` and `TrackMaskNotBound`, D118, 6.1 and 13.3); parked aircraft start landed [I] (15.8).
  *2026-11-16 (0395): the 4 VP ships unchanged (no capture in the budget).*
- **The move's shuffle / charge choice** is 0042's (choreography). The mech block 46's 0.012 translation is open (15.8).
- **Q4b's dying barbarian blocks** (17-23 deg) are not explained (8.10).
- **Tests depending on a removed bundle:** 0041's placement-catalogue test and `oracle.py` default to a height-field
  bundle moved to `F:/c5-64bit-work/scene-library/retired/` by 0364; this epic ran them on the TINY_0009 bundle by
  `C5R_UNITS_HEIGHTFIELD` (fixing the default is 0364's).
  **Resolved 2026-11-16 01:23 (lead-0374):** main's a602ecd7 (0364, MC #2109) points the test's default at the
  format-3 TINY_0009 bundle; after merging main, render's suite passes 57/57 with 0 skipped and no
  `C5R_UNITS_HEIGHTFIELD` set.

## 17. Ticket 0395: fortify, embark, the aim unit, and the open items of 16 (2026-11-16)

Written 2026-11-16 01:31-02:20 EST by the 0395 implementer (account B, branch `task-0395-unit-controller-open-items`).
Live runs on pool instance c5h-c (claim 0395), 32-bit DLL `i0ship-26234371-ia` named on the command line (SHA-256
3C2F47DA...), CP/VP/EUI, stock `config.ini`, frame cap 30, Vibrant Textures present, RenderDoc 1.47 by the worktree's
0302+ wrapper, quick combat and quick movement off, save copy `C5H-0395-UNITS0301_0009` of `c5h-9028-tiny-t0008`
(b2ceb663...dcc29163, removed at the end). Scenario `tools/scenario/scenarios/units_0395.json`, driver
`render/evidence/0395/live.py` (runner.py's own pieces plus the steps the runner cannot express), fits by 4405's
`fastfit.py` (`render/evidence/0395/fit_0395.py`, `fit_embark.py`), table `render/evidence/0395/table_0395.py` (copy
`table_0395.csv`), scoring `oracle_anim.py --steps live0395` (new step; summary and rows in
`render/evidence/0395/oracle/`). Capture budget 5 (MC-B #2180, 4405's unused): **5 used**. Every process stopped
through the harness (no turn passed, no autosave), the D155 marker checked before every step (no box), c5h-c restored
by SHA-256 (`RESTORE OK`) and released 02:09. DIAGNOSTIC shots in Visual Checks (issue 0395): FORT
`vc-20261116-014645-workstation-cf11` / `-014704-...-1f2e`, AIM `vc-20261116-015931-workstation-3d6e`, FADE
`vc-20261116-020557-workstation-79d7`. Marks as at the top of this file.

### 17.1 What was captured [L]

| Capture | Stimulus (Lua answer to capture) | What the figures play |
|---|---|---|
| FORT 690 | W1 (warrior of the active player, placed with its moves, asleep) `fortify`; capture requested at once | W1: all 12 in fortify 1500 at **t 0.2668** (single clip, 0.062 deg on every figure; a two-clip fit against idle A / B gives w 1.000: no From left); W2 (placed in the same Lua chunk, not yet ordered) idle A / B at 1.4995 (11 of 12; one idle B at 0.6001); bystander horseman (player 1) idle 3.192 |
| FORT 783 | W2 `fortify`, capture 0.05 s later | W2: 12 in 1500 at **t 0.3999** (0.018 deg); W1: 12 in fortify idle 1520 at 2.6665 |
| FORT 951 | W1 `move` one plot (out of fortify idle), capture at once | W1: **4 figures in run** (t 0.24-0.31, single clip), **8 still in fortify idle**: 4 at 0.5773, 4 at 0.3773 (0.200 behind); W2 1520 at 1.3053 |
| AIM 856 | battleship (`ART_DEF_UNIT_BATTLESHIP`, 1 figure) on coast (33,10) facing NE, `range` on a barbarian warrior at (34,9) (2 plots, SE); capture 0.35 s after the answer | the battleship is **`battleship_aim_left` at t 0, rotation 0.0000 deg**; every other clip (idle, combat ready, attack, aim centre) 94.6 deg away, aim right 180 deg: the turrets are turned to full left aim. The hull yaw is that of the placement (not turned). The barbarian's 10 figures in shuffle (1450) at per-figure t |
| FADE 691 | capture requested 0.15 s **before** W3's `fortify` call | the captured frame preceded the order: W3 and W4 (one chunk) all idle, 22 of 24 figures at 1.4663-1.4665 (one idle B at 4.9322, one fidget A at 3.3664) |

Capture timing [L]: a single capture lands **0.27-0.40 s** after its request (FORT: the fortify clip at 0.267 and
0.400 s in the captured frame, requested 0.00 / 0.05 s after the Lua answer); the captured frame itself lasts 327-572 ms
(`frame_ms`). So no single requested after a stimulus can fall inside a fade of 0.15 s. Requested before the stimulus
(FADE), the concurrent Lua call took 0.325 s instead of 0.10 s and the frame was captured before the order: the
pre-trigger is not a reliable way into a fade either [L, one try].

Also from the runs [L]: a placed unit has no moves (place.lua's `FinishMoves`), so `CanRangeStrikeAt` is false through
`IsOutOfAttacks()` (DLL `CvUnit::isOutOfAttacks`: true when the unit cannot move) until its moves are given back, as
`lua/action.lua` does before the attack (`render/evidence/0395/aimcheck.lua` does it before its check). A ranged attack
with quick combat off moves the camera to the combat. The active player's warrior placed by `place_keep.lua` (InitUnit,
sleep, no `FinishMoves`, after the camera step) answered `moved=false can=true`, the fortify started at once and the
camera did not move: this is the way to fortify a unit in the placing turn that 8.12 asked for.

### 17.2 Fortify on and off [L for the captures; C for the DLL facts]

- **The DLL** (`CvUnitMission::ContinueMission`, `CvUnit::SetFortified`, read 2026-11-16): a FORTIFY / HEAL / ALERT /
  SKIP mission on a unit that has not moved and can fortify calls `triggerFortifyAnimation(true)`
  (`GameplayUnitFortify(unit, 1)`) **without setting `m_bFortified`**; any other mission sends
  `GameplayUnitFortify(unit, 0)` only when `m_bFortified` is set, which only the owner's turn start does
  (`CvPlayer.cpp`, units that did not move); `changeMoves` / `finishMoves` / `setXY` call `SetFortified(false)`, which
  sends the 0 only when the flag was set. So **a unit fortified by mission and moved in the same turn gets no
  `fortify 0`**: the EXE sees only the move [C]. In the turn-start case the 0 comes first, then the move.
- **Fortify on** (FORT 690, 783): `oracle_anim.py live0395` plays the warrior with `fortify 1` and the captured t: ec
  1500, t 0.2668 / 0.3999, the controller's share of 1500 **1.0000** against the capture's single clip: **2 pass**. The
  controller's idle -> 1500 fade is 0.15 s (graph; `render/evidence/0395/predict.out.txt`: w 0.126, 0.417, 0.741, 0.966
  at 1-4 frames of 1/30 s, From idle held); the capture shows no From left at 0.267 s (two-clip fit w 1.000,
  `blend_fort690.json`): **consistent, but no point inside the fade**.
- **Clip end -> fortify idle, and K6 between captures** (no controller run, the figures' own clocks): over 690 -> 783
  the bystander horseman's idle advanced 3.4055 s; W1 went 1500 0.2668 -> end (1.0 s) -> 1520 at 2.6665, i.e. 3.3997:
  the difference **0.0058 s** is the overflow dropped at the clip end (8.10's rule; it must lie in [0, one 1/30 s step]).
  Over 783 -> 951, W2 (1500 0.3999 -> 1520 1.3053, one 4.0 s loop) against the horseman's 5.9106: **0.0052**. Both
  **pass**: the continuous clock (`t += min(dt, 0.2)`) and the overflow drop hold across a 1.0 s clip end, also over
  stretches with a capture stall in them [L].
- **Fortify off by a move** (FORT 951): the controller gives every figure run (with the same 0.15 s fade) on the order;
  the EXE had 4 figures in run 0.24-0.31 s after the order and 8 still in fortify idle: the **staggered per-figure run
  entry** of 8.9, now also out of fortify idle. Scored: 4 `pass_ec`, 8 `not_covered` (0042's C1 owns the entry delays).
- **A new skip [L, cause open]:** of the 8 figures still in fortify idle at 951, 4 are on W1's clock carried from 783
  (0.5773 = 2.6665 + 5.9106 mod 4.0) and **4 are exactly 0.200 s behind** (0.3773): each lost one update clamped at
  0.2 s, per figure, not per unit. The update lost is one of 0.2 s or longer, so it was not a capped frame after the
  order; which frame it was (the capture stall after 783 or a long frame at the order) is not separable from singles.
  Recorded with K6's combat skips (16).

### 17.3 Embark [C for the model; L for the fits]

- **The model.** The embarked unit is drawn with the **owner's embarked graphic override**: the DLL's
  `CvPlayer::init` calls `SetEmbarkedGraphicOverride("ART_DEF_UNIT_U_POLYNESIAN_WAR_CANOE")` for a player whose trait has
  `EmbarkedAllWater` (Polynesia) and `"ART_DEF_UNIT_U_DANISH_LONGBOAT"` for `EmbarkedToLandFlatCost` (Denmark), and the
  EXE asks `ICvPlayer1::GetEmbarkedGraphicOverride` (`unit-feed-contract.md` row 156). The database row
  `ART_DEF_UNIT_U_POLYNESIAN_WAR_CANOE` has **2 members** of `U_Polynesian_War_Canoe.fxsxml` (PolynesiaModels, mesh
  `U_Polynesian_War_Canoe.gr2`, 26 clips): 4405 B's 7,722-index, 2-instance embark model (8.10 Q8, 0041 U02), which is
  why `transport.gr2` (8,910 indexes) did not match. What other players embark as (the override is empty) is not read
  here [I: `ART_DEF_UNIT_TRANSPORT` is the database's only other embark-looking row].
- **The fits** (4405 B's Q8 `bones.json`, no new capture; `fits/Q8` on F:): 8 of 8 blocks one clip, 0.000-0.055 deg.
  Canoe 0: stop run (1440) 0.2298 at 820 -> idle A (1000) 0.2000 at 823 -> 0.6000 at 825 -> 1.4000 at 829. Canoe 1:
  **run (1400) 0.0550 at 820** -> idle A 0.0000 at 823 -> 0.4000 -> 1.2000, i.e. one stalled frame (0.2 s) behind canoe 0
  from 823 on. The canoes play run, stop run and idle after the swap: the move goes on in the embarked model.
- **Scored** (`live0395`, the canoe art created, `move`, `arrive` placed so that canoe 0's stop t is met (the free start
  offset), then the recorded Present steps under the continuous clock): **canoe 0 passes at all 4 frames** (stop run ->
  idle A at 0 on the frame the 0.533 s clip ends, 8.10's rule, on a ship machine). **Canoe 1 fails at 4 frames**
  (`fail_stagger` at 820, then 0.2 s off): the controller's `arrive` is per unit; the EXE ends the run per figure (as it
  starts it, 8.9), and canoe 1 shows no stop run clip in these frames (run at 820, idle at 0 on 823 needs either no
  stop clip or a stop run shorter than 0.4 s of clip time) [L for the frames; the rule open, 0042]. The controller's
  `embark` event recreates the unit in the default state (13.1): whether the EXE restarts the canoe's figures at the
  swap or carries the move over is not in these frames (the swap fell before 820).

### 17.4 The aim unit [L]

AIM 856 against the controller (`live0395`, through `oracle_palettes.compare`: `unitscene program`, placement solved,
0041's tolerances rotation element 1e-3, translation 0.01): the battleship created idle, `steer * <w> <selector>` on
frame 2, 30 frames of 1/30 s:

| Steer (path 7 w, path 8 selector) | Controller program | rot elem max | trans max | Result |
|---|---|---|---|---|
| w 1, selector 0 | idle A under `Aim_Left` through the resolved `TrackMaskAIM_LAYER` (w 1) | **0.0** | **3.8e-5** | **pass** |
| w 1, selector 1 | `Aim_Center` | 1.403 | 16.88 | fail |
| w 1, selector 2 | `Aim_Right` | 1.732 | 19.11 | fail |
| w 0, any selector | the centre side | 1.403 | 16.88 | fail |
| reference | `Aim_Left` alone at 0 | 0.0 | 3.9e-5 | pass |

- **Off-centre aim reproduced through the mask** [L]: 13.3's binding by bone (the `AIM_*_DR` turret bones at weight 1,
  the rest the body) gives the captured palette exactly at selector 0 = left, weight 1. This is the scoring 13.3 and
  15.8 left open ("left and right aim unscored"); with 15.8's mech block 51 it is the second capture check of a
  resolved aim mask, and the first off centre.
- Geometry [I]: the hull kept its placement yaw (-30 deg in the fit, facing NE), and the turrets turned 94.6 deg, so
  they point within about 5 deg of the target's direction from the ship (ESE); "left" is the clip's name in the model's
  own frame.
- The steer values are the EXE's (paths 7 and 8: 0042's or the aim target's producer, section 1); here they are the free
  inputs of the score, like a start offset. Whether the aim was set at the attack order or already before it (the
  pre-attack shot `vc-20261116-015931-workstation-3d6e` is a PrintWindow picture, not fitted) is not determined.

### 17.5 The open items of 16, after this ticket

| 16 item | Now | Why |
|---|---|---|
| (1) K6 combat skips (12 x 0.2, 9 x 0.0 in Q4b, Q5, Q6) and OrderFrame | **open** | no combat capture was in the budget; one more skip kind found: per figure, 0.200 s, after a move order out of fortify (17.2). Which frame a skip falls on still needs the order's frame number in the run record |
| (2) X1, From held during a fade | **open** | no capture fell inside a fade: singles land 0.27-0.40 s after the request, the fades measured are 0.15 s (fortify: complete by 0.267 s, w 1.000); the pre-triggered capture fell before the order. P01 4681 stays the only point |
| (3) type-3 ease | **open, not loosened** | no second point (same reason). A note for the next point [I]: of the named eases only **quintic smootherstep** (6x^5 - 15x^4 + 10x^3) meets P01 (x = 0.2 / 0.3333 = 0.6 gives 0.6826 against 0.686 / 0.681); cubic smoothstep (the controller's) gives 0.648, cosine 0.655, linear 0.600. Not applied: one point cannot choose a curve, and the EXE's `granny_dag2_blockcurve` code is not read |
| (4) fortify on / off, embark, the aim unit | **captured and scored** | 17.2-17.4 |
| (5) the 4 VP-edited ships | **open, refused by design** | still `BadLogicFile` at bind (15.8) and no mask data (13.3); a live look at a VP destroyer needs a capture outside this budget |
| (6) 345 of 4405 B's rows not covered | **unchanged** | this ticket scored its own rows and changed no 4405 B row |

`live0395.csv` (39 rows): fortify on 2 pass; clip end 2 pass; after the move order 4 pass and 4 `skip_0.200`; fortify
off 4 `pass_ec` and 8 `not_covered`; embark 4 pass, 1 `fail_stagger`, 3 fail (canoe 1, per-figure arrive); aim 2 pass
and 5 comparison lines that fail as they should (the wrong selectors, w 0). Tolerances unchanged (t 0.005 s, w 0.02,
palettes 1e-3 / 0.01); no controller rule was changed by this ticket.

### 17.6 Tests and reproduction

- `render/tools/unitanim/test_oracle_anim.py` 9 of 9 passed, 0 skipped (2026-11-16 02:12, with `UNITANIM` set): new
  `test_live0395_missing_input_is_3` (exit 3 naming the missing table), `test_live0395_clock_rows_pass_and_fail` (a
  synthetic table: overflow in range passes, -0.094 fails, a figure 0.2 s off its carried clock is `skip_0.200`),
  `test_live0395_positive` (the step on this ticket's data: fortify 2 pass, clip end 2 pass, aim 2 pass, embark 4 pass).
  No native code changed (no sanitizer run needed).
- Reproduce: `python -I -X utf8 render/tools/unitanim/oracle_anim.py --exe <render build>/tools/unitanim/unitanim.exe
  --out F:/c5-64bit-work/0395/oracle/r1 --steps live0395 --clocks continuous` (render build `705314c9b3` Release,
  2026-11-16; unitscene built beside it). Inputs on F: (`F:/c5-64bit-work/0395/{bones,fits,tables}`; the 5 captures
  kept in `F:/c5-64bit-work/0395/captures`, 0.53 GB).
### 17.7 The Assyrian siege tower's missing charge attack (added to 0395 by the MC 2026-11-16 01:55) [C, no capture]

VP's `U_Assyrian_Siege_Tower.fxsxml` (Expansion2Models) names `U_Assyrian_Siege_Tower_Charge_Attack.gr2` for ec 1140,
and no install ships that file (6.1, 3104). The capture the MC asked for (what the EXE draws when that clip is
requested) was not taken, because in this game configuration the request cannot be made:
- the database the game runs (`F:/c5-64bit-work/0327/db/Civ5DebugDatabase.db`, CP/VP/EUI) gives
  `UNIT_ASSYRIAN_SIEGE_TOWER` **Combat 0, RangedCombat 0, Range 0, no CombatClass** (free promotions Sapper, Medic,
  Medic II: a support unit); the DLL's `CvUnit::IsCombatUnit()` is `m_iBaseCombat > 0`, so the unit never attacks, and
  1140 (AttackCharge) is the charge attack of a melee attack (Q4b's warrior: charge run 1120 -> charge attack 1140, 8.10);
- its member's `EnableActions` (`Idle Attack RunCharge AttackCity Bombard Death BombardDefend Run Fortify
  CombatReady`) has no `AttackCharge`, so the controller's bridge refuses a 1140 request for it as `BadEvent` (14.1)
  before any clip is looked up;
- the same `.fxsxml` also lists **1140 on `U_Assyrian_Siege_Tower_Stop_Run.gr2`** (`ec="1440, 1140"`): a clip that
  exists carries the code too. Which of two files with one code the EXE plays is not read [I]; if a mod ever gave the
  unit a melee attack, a capture of it would answer both questions.
So the missing file is unreachable in a VP game [C: database and DLL]; the controller's hold-current-state rule for a
missing clip (0375/4460) stays [I] and has no live case on this configuration. Spending a capture would need a
database change (a combat value for the unit), which this ticket does not make.

- c5h-c's pool health (not caused here, for its owner): `provision.py verify c5h-c` fails because the CP-mod DLL file
  is e589306e... (mtime 2026-11-15 20:44, as found at this ticket's backup) while the instance's `user.manifest.tsv`
  (mtime 2026-11-15 22:42) and `instance.json` keep 5fdf3432... (the kept copy in `run/dll-original`).

## 18. The drawn set: every unit advanced, outputs for the drawn units only (issue 4468, epic 0375, 2026-11-16)

Issue 4468 (W-4468, branch `task-4468-controller-drawn-set`, MC grant #2598 of `render/src/unitanim/`). Why: on the huge
save the controller cost +30.5 ms [30.2, 30.9] of server CPU per drawn frame (`unit-server-scene.md` 3.4, 4466's set 2),
81 % of the server's cycles, because `apply` settled, stepped and built a pose program for every held unit (896) while
the frame drew about 35.

**Settled 2026-11-16 by issue 0442 [L]: the EXE does not advance the animation of a unit it does not draw; the unit's
clock stands while it is out of view and goes on from there when it is drawn again.** The capture this paragraph names
below, run (`server/evidence/0442/s18_arm.py`, `s18.sh`, fits `render/evidence/0442/s18_fit.py`; data
`F:/c5-64bit-work/0442/rdc18`, `fit18/`): a copy of CAT_0009 (B2CEB663) on the product layout of 0442's build under
RenderDoc (the EXE draws its own units, as 4458's E-B), every plot within 12 of the capital revealed first; warriors A
(1039 at (45, 13)) and B (1040 at (52, 13)) created in one Lua call, facing 2, asleep (the EXE plays
`warrior_fortify_idle`, 4.0 s, on both). Three single captures, 0327's exact_t fits of every figure:

| Capture (frame) | Camera | A: figures at the unit's time | B |
|---|---|---|---|
| K0 (4075), 3 s after the creation | C2 (48, 13): both in view | 12 of 12 at 0.2233 s | 11 of 12 at 0.2233 s (one in the fidget clip) |
| K1 (4651) | C1 (42, 13): A in view, B 10 plots east of the centre | 11 of 12 at 2.8328 s | **no draw** (0 instances: not drawn) |
| K2 (5247), 1 s after the pan back | C2 | 11 of 12 at 1.9260 s (wrapped) | 10 of 12 at 3.3360 s |

- Created in one frame, A and B share one time (K0: equal to 1e-4 s, as 7.4's "one clock per unit"; units created
  together start together).
- At K2 they differ by 2.590 s (A ahead, modulo the 4.0 s loop; equivalently B ahead by 1.410). Advanced while hidden
  would give 0 (excluded: the fits agree to 1e-4 within each unit); a clock restarted at the reappearance would put B
  near the time since the pan back (about 1.5 s by A's rate below), not 3.336. **Frozen while hidden predicts A's
  advance over the hidden span:** B was out of view from the pan away to the pan back, 6.59 s of wall time; A's clock ran
  at 0.38 and 0.45 s per wall second between the captures (RenderDoc slows the frames; K6's 0.2 s per frame bound,
  7.2), which puts A's advance over that span at about 2.8 s [estimate, about +-0.3 s from the rate]: matching the 2.59
  s lag. So B's clock stood while B was not drawn [L, one pair; the length of the stop by estimate].
- Only culling was tested (a unit out of the camera's view). A unit on a plot the active team does not see (fog) was
  not: [I] the same.
- **Consequence for the controller:** *done 2026-11-17 by issue 0450, 18.1: the controller holds a unit's clock while
  it is not drawn.* (Was: "the server advances every held unit on every frame, so a unit that comes back into view shows
  another phase than the EXE's ... Making the server's rule the EXE's means: step only the drawn units' clocks (the
  events still applied to every unit)".) Whether the EXE also skips the machine updates and random draws of hidden
  units (or only the clip clocks) is not measured [I]; the controller skips all of them (18.1).

*Was (before 0442; the rule below replaced by 0450's, 18.1, 2026-11-17):* **What the EXE does with units it does not draw: not established [I].** Searched 2026-11-16: this file (7.2 K6, 7.4 X3,
13.1), `unit-playback.md`, `unit-draws.md`: every capture so far is of units on screen; no capture compares a unit that was
off screen (culled, or on a plot the active team does not see) with one that stayed visible. So whether the EXE keeps
advancing an off-screen unit's animation (clip clocks, machine, fidget rolls) or freezes it is open. **The rule written
here (MC grant #2598, issue 4468's scope) [C: code and tests]:** a unit's animation does not depend on whether it was
drawn: every held unit takes, on every frame, the frame's events (in order), its settle, its clock (`UnitClock::take`)
and one machine update per step exactly as before, including its random draws (`Rng` per unit); only the outputs are
limited to the drawn set. A unit that comes into view therefore shows the clip, time, state and blend it would show had
it been drawn on every frame. The capture that would settle the EXE's side [I]: two units of one type created on the
same frame, one kept on screen and one panned away for a known time, then both on screen: equal clip times (advanced) or
the second behind by the time off screen (frozen); one sequence of 8.5's kind, not run.

**The interface (`controller.h`) [C].** `Controller::apply(frame, const std::set<UnitKey>* drawn)`; `apply(frame)` is
`apply(frame, nullptr)` (every unit drawn, the behaviour before 4468). With a set:
- every unit: events and `settle`; *since 0450 (18.1) only a drawn unit takes* `clock.take` and `step` (graph
  `advance`, state time, fade time, machine update with its `ECComplete` walks and random draws). (Was: every unit took
  all of them, unchanged by the set.);
- a unit not in the set: no trigger walk (`step` skips `active()` and the trigger loop: triggers are an output; nothing
  reads them back), no pose program in `programs()`; its failures are still found on the frame they happen: a figure with
  no graph (`NoProgress`, as `collect`'s) and, **where the graph's program can fail**, the program is built and dropped so
  that its `BadLogicFile` lands on the same frame as when drawn (a failed unit stays failed until removed or created
  again, so the frame matters: a unit that failed while hidden must not be drawn later);
- `GraphInstance::programMayFail()` (graph.h, additive, default true): `bindGraph`'s graphs answer from their structure,
  following `build()`'s pose edges (every input a selector or weight could choose): true for a missing or out-of-range
  pose edge, the depth limit (64), a Selection whose default edge is out of range (a selector value may be out of range),
  a node that is not a pose node; false otherwise. On VP's files the only such graph seen live is
  `edited_aim_ms_idle_graph.dge` (the destroyer's and dreadnought's `AnimFailed` of `unit-server-scene.md` 3.4) [L by the
  LOUD lines, C by the rule];
- `statuses()` still lists every unit; `lastApply()` counts the frame's drawn units, the units stepped quietly and the
  programs built for hidden units;
- also for drawn units: no trigger walk for a member without trigger files (the walk's result was discarded) [C: no
  output changes].

**Tests [C] (`render/tests/unitanim/test_controller_drawn.cpp`, over the real `bindGraph` evaluator and hand-built
machines, no game file).** `drawn_set_programs_and_triggers_byte_equal`: two controllers on one 160-frame timeline (steps
of 1/60 to 0.3 s and 0, fortify on and off, damage, an attack, a remove + create, a `BadEvent` move, a unit type whose
fortify graph fails), A drawing every unit, B a schedule (hidden 48 frames then shown; shown, hidden 59, shown; hidden
until frame 120; a unit that fails while hidden): on every frame B's programs equal A's for B's drawn units bit for bit
(times and weights compared by their bits), B's triggers equal A's for those units, every status equal; 669 programs and
16 triggers compared, 300 hidden unit-frames. `shown_after_any_hidden_stretch` (hidden 1 to 98 frames, then the first
drawn frame equal). `program_may_fail_from_structure` (selection, fade and one-source graphs false; a selection without a
usable default and a blend with a missing To edge true, and they do fail; the base class true).
`start_work_bounded_by_the_drawn_set` (300 units created, 5 drawn: 15 programs on the first frame and 15 per frame after,
900 graph binds = one per figure). `drawn_set_frame_refusals` (a negative or non-finite step, a closed controller:
`BadEvent`, as `apply(frame)`). A mutation check (the hidden units' graph clocks not advanced) failed the first two cases
with 145 checks (2026-11-16 13:05, not committed). Server side: `unit-server-scene.md` 6.1.

**The graph factory's clip durations read ahead (4468, a669d00d) [C, L].** `ClipGraphFactory::bind` needs every clip's
duration of the member's ec map and read each clip file in full (`read_gr2`: the Oodle sections and the CRC) on the
thread that binds, cached per lower-case basename: at the controller's start with 853 held units this was the bulk of
the start (offline under `perf/sampler`: bind 31.6 % of the run's cycles, `read_gr2` 26.3 %; live 1.8-2.1 s of one
render-thread frame, `unit-server-scene.md` 3.4). Additive in `driver.h`: `ClipGraphFactory::readDurations(loader,
clips, out)` (static; the same read as bind's, `readOne`, so the duration and the refusal text are the same),
`seed(durations)` (takes the entries the cache lacks, keeps its own) and `lazyReads()` (clips bind still read itself).
The server reads them on the pre-check's thread for every art row the pre-check reached and seeds the runner's factory
before the scene is ready (`unit-server-scene.md` 6.1): live the start fell to 0.39-0.44 s with 26 clips still read on
the render thread. Test `render/tests/unitanim/test_clip_durations.cpp` (read ahead = read lazily, refusal texts
included; a seeded factory binds without a read; an unseeded clip is read and counted; no loader: the same refusal).

**Cost (oldpc, grade A, `unit-server-scene.md` 3.4 set B, 2026-11-16) [L].** On the huge save (853 held units, ~32
drawn) the server's CPU per drawn frame is 10.22 ms with the controller on (this rule) against 34.36 ms with every
unit's program built every frame (the team head) and 4.90 ms without the controller: the drawn set saves +24.14 ms
[+23.82, +24.45]; the controller still costs +5.33 ms [+5.04, +5.62] (was +30.53), by the profile ~3.6 ms its frame
(mostly the hidden units' steps and machine updates and the statuses) and ~1.7 ms the drawn figures' pose rows. *(2026-11-17, issue 0441: the frame's string work removed; on a new set the controller's cost fell from +5.48 to +3.13 ms per drawn frame, section 19.5.)*

### 18.1 The held clock (issue 0450, 2026-11-17)

Issue 0450 (account B, branch `task-0450-units-held-clock-when-culled`, started on main 89f8b4e07). The EXE's rule of 18
(settled by 0442's rdc18 capture) made the controller's rule.

**The rule [C: code and tests].** `Controller::apply(frame, drawn)`: every held unit takes the frame's events (in order)
and its `settle`; only a unit in the drawn set takes `UnitClock::take` and `step` (graph advance, state time, fade
time, one machine update per step with its `ECComplete` walks and random draws, the trigger walk). A unit not drawn
holds its clock: its state equals that of a unit for which the frame's dt was 0, and it goes on from there on the first
frame it is drawn again (that frame's own step only). Outputs as before: programs and triggers for the drawn units, a
program built and dropped for a hidden unit whose graph's program can fail (`programMayFail`), every status. `apply(frame)`
(no set: every unit drawn) is unchanged. `controller.h` comment and `step()`'s signature (the `shown` flag removed:
only drawn units are stepped). A unit created hidden takes its settle at the creation (a graphless chooser may enter a
state at once, with no clock: F's fortify state in the test below), its first clip time on its first drawn frame.

**Tests [C] (`render/tests/unitanim/test_controller_drawn.cpp`).**
- `drawn_set_programs_and_triggers_byte_equal`, **changed with the rule (reason: the EXE holds a hidden unit's clock,
  18)**: B (the schedule) is compared unit by unit with a reference R_k that draws every unit on every frame of the same
  timeline with dt 0 on the frames B did not draw unit k (events kept): B's programs and triggers of k on the frames it
  draws k, and k's status on every frame, byte-equal to R_k's (669 figure programs, 21 triggers, 340 hidden
  unit-frames); A (every unit drawn: 4468's comparison) differs from B on 212 drawn unit-frames (the hold is real). A
  unit ordered to fortify while hidden (F's failing graph) keeps its idle state until it is drawn and fails on that
  frame (75, A on 70); a unit created hidden with the order in the same frame enters the failing state in its settle
  and fails hidden on that frame (the programMayFail path, `quiet_programs` 1 on frame 100 only).
- `shown_after_any_hidden_stretch`: the first drawn frame equals the reference with dt 0 over the stretch (6 lengths),
  and differs from the every-unit-advanced controller from 17 hidden frames on.
- `held_clock_lags_by_the_hidden_time` (rdc18 as a test): two units created on one frame, one hidden for 30 frames of
  1/30 s: lag 0.00000 s on frame 10, 1.00000 s on frame 41 and 80 (no fidget roll).
- `start_work_bounded_by_the_drawn_set` now also counts graph advances: 300 units, 5 drawn, 10 frames: 150 advances
  (the drawn 15 figures only; before 0450 every figure advanced).
- Mutation (2026-11-17 01:20, not committed): the hold disabled (`if (!shown && false)`), the file fails 286 checks in
  four tests (the lag test reads 0.00000 s at frames 41 and 80).
- Suites: `c5r_unitanim|c5r_units` 29/29 plain (2026-11-17 01:17); ASan and the server unit suites: see the issue's
  acceptance (`render/evidence/0450/acceptance.txt`).

**The server's side live, DIAGNOSTIC [L] (cull arm; `render/evidence/0450/arm_0450.py --mode cull`, `live.sh`,
`cull_dumps.py`; records `arm_cull.json`, `cull-result.json`).** The product of this branch (server.exe 23c5273b, proxy
6d2faeea, composite 2643fbce, over the pin layout `x64-04b1211b-pfdee656`'s bin; build `build_0450.sh`) on a copy
`C5H-0450-CAT_0009` of 0442's CAT_0009 save (B2CEB663), pool instance c5h-g (F:, Goldberg, VT present; `provision.py
align548` and `verify` rc 0 first, MC #2549), state (ii) with `render_units_passes=1` and the TEST ONLY per-frame unit
dump (`render_test_units_dump_max=40000`), 2026-11-17 01:39-01:43 EST, restored by hash, verify rc 0. 0442's
`s18_make.lua` (warriors 1039 at (45, 13) and 1040 at (52, 13), asleep: `Warrior_Fortify_Idle`, 4.0 s), the camera on
C2 (48, 13), pan to C1 (42, 13) for 4.1 s of wall time, back. From the dumps (each drawn frame: the listed units' figure
clips and times):
- before: A and B listed with one clock (lag 0.0); B not listed on 131 frames (4.40 s); A advanced 4.39889 s over them;
- B's clock from its last listed frame (1130) to its first listed frame again (1262): **+0.03381 s = A's increment of
  frame 1262 alone** (held); the every-unit-advanced rule would give 0.39889 (4.39889 mod 4.0);
- after: B lags A by 0.36507 s mod 4.0 (= 4.39889 - 0.03381): the EXE's rule of 18 (B lags A by A's advance over the
  hidden span), at the server's own frame rate (~1 s of clock per wall second, no RenderDoc).
- **The fog filter too:** barbarian warriors 1041 at (45, 9) (its plot given one visibility count, drawn) and 1042 at
  (49, 9) (created in the fog) in one Lua call; 5.11 s later B's plot given a count (`fog_vis.lua`): the server did not
  list B until then (154 frames), and on B's first listed frame its clip time was 0.03395 s = that frame's step alone
  (A: 5.16665 s, lag 5.1327 s = A's advance since the creation less that step). A fogged unit is not in the server's
  drawn set (`srv_units_scene.cpp` builds it after the cull and the fog filter), so its clock is held as a culled
  unit's.

**The EXE's side in the fog, DIAGNOSTIC [L where stated] (fog arm; `arm_0450.py --mode fog` under RenderDoc, as 0442's
rdc18; `lua/fog_make.lua`, `lua/fog_vis.lua`).**
- **Fogging a drawn unit within a turn cannot be done [C, L]:** VP runs with `CORE_DELAYED_VISIBILITY = 1`
  (`(1) Community Patch/Database Changes/NewCustomModOptions.xml`), and `CvPlot::getVisibilityCount` returns the turn's
  maximum (`m_aiVisibilityCountThisTurnMax`) under it; `changeVisibilityCount` leaves the hide to `setTurnActive`
  (`flipVisibility`). Try 1 (2026-11-17 01:29-01:32) took B's count back with `Plot:ChangeVisibilityCount(-1)` and
  `Plot:UpdateFog(false)`: the plot stayed visible (count read 1); the arm refused. So in this game a unit drawn this
  turn stays drawn until the turn ends (or it is culled); a unit can only enter the view fogged (at the start of a turn,
  or created / moved into fog) and come out of it when a plot is revealed (immediate: `changeVisibilityCount` +1 takes
  the TO_VISIBLE path, `updateFog`, `updateCenterUnit`).
- **Barbarians take no fortify order outside their turn [C, L]:** `CvUnitMission::StartMission` returns at once for a
  player whose turn is not active (sequential turns); tries 2 and 3 (`actA=0 actB=0`: awake) found no other player
  whose turn was active at turn 9 of CAT_0009 (the owner search of try 3 fell back to the barbarians). The awake idle
  (`warrior_idlea` / `idleb`, 6.0 s, transitions 1.0 s, fidgets) desynchronises a unit's figures, so the exact_t fits
  give no single unit clock once a unit is a few seconds old.
- **Result, created in the fog and revealed (tries 2-4, 2026-11-17 01:35-01:56; captures `F:/c5-64bit-work/0450/rdcfog{b,c,d}`,
  fits `fitfog{b,c,d}/s18_fit.json` by 0442's `s18_fit.py`; records `render/evidence/0450/arm_fog.json` = try 2,
  `fog_fits.txt` all three) [L, qualitative, 3 of 3]:** barbarian warriors A (1039 at (44, 13), its plot given one
  count) and B (1040 at (48, 13), in the fog) created in one Lua call, the camera between them; K0 called 7.6-9.6 s after the
  creation (A drawn, B 0 instances: the EXE does not draw a fogged unit), B's plot revealed 8.5-10.6 s after the
  creation, K1 and K2 about 6 and 11 s after the reveal. In every run B in K1 is a *young* unit: 8 of its 12 figures on
  one idle time (2.4692 / 3.1952 / 4.3364 s, `warrior_idlea` / `idleb`), while A, of the same age had B's clock run in
  the fog, has 2 of 12 on one time in K1 (and 3-8 of 12 already in K0); B's shared time equals none of A's figure times.
  The awake idle's random transitions and fidgets scatter a unit's figures as its clock runs, so B's figures ran far
  less than A's: **the EXE does not advance a unit while it is fogged** (as culled, 18), as far as this measure shows.
  Not a measured lag (no shared clock in A to subtract); the case "drawn, then fogged" cannot occur within a turn in
  this game (above) and was not measured across a turn end.

**Cost.** Offline, workstation, an indication (COMMON 23; other teams' heavy jobs ran: `quietlock check`
listed 3 before every arm) [L]: 0441's bench `bench_frame` (base = main 89f8b4e07 built from a git archive, against this
branch), unitfeed's timeline of the format-6 huge bundle `C5H-0301-HUGE_0350_20261116-220048_workstation` (853 units
held, 8 frames; c5h-c's install, mods 1, 2, 3a, VP's catalogue database), then 1200 frames of 1/115 s, 32 drawn; six
rounds, the order turned each round (`render/evidence/0450/bench_0450.sh`, `bench-interleave.txt`, `bench_stats.py`,
`bench-stats.txt`; 2026-11-17 02:40-02:47 EST). **The per-frame outputs hash is equal in all 12 runs
(`c751b37f281d5564`, 1208 frames: with a fixed drawn set the drawn units' programs, triggers and pose rows do not
change)**; `apply` (the controller's frame + ControllerPose's set) median 1.48-2.71 ms base against 0.34-0.59 new,
paired new - base **-1.54 ms [-1.96, -1.13] (-77 %)**, means -1.70 [-2.05, -1.36]; `pose` -0.24 ms [-0.39, -0.09]
(not this change's work: the same rows for the same 32 drawn units; cache and machine load, [I]). What moved: the 821
held units not drawn no longer take `clock.take`, graph advances, machine updates (their `ECComplete` walks and rolls)
per frame. The live grade-A series on oldpc (0441's measure: `cost_4466.py`, arms u1 this branch / uo base 89f8b4e07 /
un no controller, 12 runs + warm-up, `render/evidence/0450/submit_0450.sh`) was submitted 2026-11-17 02:03 as job
`20261117-020342-3ac4` and is **held: no interactive session on oldpc** (Windows Update replaced its GPU driver 01:38-01:40
and restarted it 02:04, `quiet-testbed.md` 25; the headed series waits for a logon); not run at hand-back [I: the live
saving].

**Not verified / left.** Whether the EXE skips a hidden unit's machine updates and random draws (the controller does:
a fidget roll a hidden unit would have made is not made) [I]. The EXE's fog case as a measured lag (a fortified pair in the fog needs a player whose turn is active, or a turn end) [I beyond the qualitative result]. The server's comment in `animFrame`
(`server/src/units/srv_units_scene.cpp`, 4468's "every held unit's clock and machine still advance") corrected in this
issue (MC 2026-11-17, touches widened), and 4468's server test `CaseAnimDrawnSet` (`server/tests/units/test_scene_units.cpp`)
changed with the rule: a third host with the time held over the frames the product host hides 1002; from the shown frames
on the product's figure lines and picture equal it and differ from the every-unit host on all 13 frames (42/42 plain and
MSVC ASan).

## 19. The controller's remaining cost and the read-ahead (issue 0441, follow-up F3 of epic 0375, 2026-11-16)

Issue 0441 (account B, branch `task-0441-units-controller-remaining-cost`). Why: after 18 the controller still cost
+5.33 ms [+5.04, +5.62] of server CPU per drawn frame on the huge save (oldpc, grade A, section 18), and the clip
durations read ahead after the pre-check (18) delayed the scene's readiness by 5.7-7.3 s. Every change below keeps the
outputs byte-equal [C: tests and the bench's per-frame hashes]; none changes what the controller decides.

### 19.1 Where the controller's frame went (attribution) [L, workstation: indications]

Offline bench `render/tests/unitanim/bench_frame.cpp` (target `c5r_unitanim_bench_frame`, a tool, not a ctest): unitfeed's
`timeline.c5anim` of the huge bundle `F:/c5-64bit-work/scene-library/bundles/C5H-0301-HUGE_0350_20261116-192756_workstation`
(format 5; since 0403's merge, 2026-11-16 late, in `scene-library/retired/`: point unitfeed there or use a format-6
bundle) (853 creates, 8 frames; unitfeed 2026-11-16 21:13, c5h-a's install and mods 1, 2, 3a, VP's catalogue database
`F:/c5-64bit-work/0327/db/Civ5DebugDatabase.db`), then 1200 frames of 1/115 s; 32 units drawn (spread over the held units
in key order), 160 figure programs per frame (5.4 blends), 213 pose rows calls per frame. Per frame it times the server's
two parts: `apply` = `Controller::apply(frame, drawn)` + `ControllerPose::clearPrograms` / `setProgram` (AnimRunner), and
`pose` = `ControllerPose::rows` for every figure x every mesh that needs a pose (prepare's rule). `--dump` writes the
FNV-1a hash of every output of each frame (programs bit for bit, triggers, statuses with texts, rows bytes and rows
statuses). Under `perf/sampler` (RelWithDebInfo build of the bench, 2 ms) the frame's costs were string work per call,
not the animation:
- `FigHost::roll` built the forced-choice point name (`"roll:<machine file>:<socket>"`) and looked it up (a copy of the
  pair) on every random roll of every figure, although the free game queues no choice;
- the graph walk (`build`) looked up each node's inputs by name (`input(n, "From")`, ...), built `"Pose in (k)"` names
  with `std::to_string` for every Selection node it passed, looked sources up in a `std::map`, and copied clip names into
  `ActiveSource` and `PoseNode`; an `ECComplete` test walked the graph twice (`active()` then `complete()`), copying every
  active source's clip name;
- `step` allocated a per-unit vector of step counts; `collect` copied each program twice (into the unit's list, then
  into `programs_`); the trigger walk of the drawn figures copied clip names it never read;
- `settle` walked every figure of every unit each frame to find nothing to do;
- on the pose side, `checkProgram` built a message string per node per call, `ControllerPose::rows` resolved a masked
  program's track masks (bone name strings) once per mesh, and `setProgram` / `clearPrograms` freed and reallocated every
  entry each frame.

### 19.2 What changed [C: code and tests]

- **Graph walks** (`graph.cpp`): each node's named inputs (`Pose out`, `Offset`, `Selector`, `Param`, `From`, `To`,
  `Mask`, `Input`, the `Pose in (k)` list) and its source index are found once at bind time (input()'s rule: the first edge
  of the name). `build()` is a template walk over a sink: `ProgramSink` (program()), `ActiveSink` (active();
  `activeTimes()` without clip names), `EcSink` (complete() and the new `ecComplete(ec)`): the same nodes, order, float
  operations and failure texts. Additive in `graph.h`: `GraphInstance::ecComplete(ec, opts)` and `activeTimes(opts, out)`
  with defaults that reproduce the old calls (active() + complete(); active()).
- **Controller** (`controller.cpp`): `FigHost::ecComplete` asks `ecComplete` (one walk, no copy); `roll` / `option`
  build the point name only when a choice is queued (`ForcedChoices::empty()`, additive in `random.h`) or forced mode
  needs it for its refusal; `step` keeps one reused vector of step counts and fills it only when ActionSkip, skips or a
  hook can change them; the trigger walk uses `activeTimes`; `collect` moves programs; `settle` is skipped for a unit
  with nothing pending (`Unit::settle_pending`: set at creation and revival and when a figure's step chain ends in a state
  without a graph; cleared when settle leaves every figure with a graph).
- **Pose** (`controller_pose.cpp/.h`, `pose.cpp`): `ControllerPose` keeps its entries across frames (a frame counter;
  `program()` finds only entries set in the current frame; entries of figures not set lately are dropped when they
  outnumber the frame's), `setProgram` takes `const PoseProgram&` and assigns into the entry's storage, and a masked
  program's resolved copy is kept with the entry per (model file, skeleton) for the frame: one resolve per figure, not
  per mesh. `checkProgram` builds its node message only on a failure.
- **The read-ahead** (`srv_units_scene.cpp` `PrecheckRun::readDurations`, MC #3150 grant): the pre-check's own
  `ModulePose` has bound every shipped clip of every art row it checked (`bindAll`), reading each file and keeping
  animation 0's duration (`CachedClip::duration` = `bind_clip_all_groups`' `out.duration` = `fi.animations[0].duration`,
  the value `ClipGraphFactory::readOne` reads). Additive: `ClipCache::durations(out)` (bound without failure, positive
  and finite, by lower-case basename), `ClipGraphFactory::takeFromCache(cache, clips, out)` (those, as (duration, "")),
  `UnitRenderer::poseSource()`. The server takes them first and then reads only what the cache lacks (a clip not
  shipped, one that did not bind, one without a positive duration), so every refusal text is still `readOne`'s; a clip
  without a duration still fails its graph loud (EcNotBound, AnimFailed), never a default. The log line adds
  `N from the pre-check's clip cache, M read`.

### 19.3 Tests and byte-equality [C, L]

- `test_graph.cpp` `walks_without_copies_answer_alike`: 1200 `ecComplete` answers over a time sweep, three selector
  values and both hold options equal the default (`GraphInstance::ecComplete`: active + complete), 72 of them true;
  `activeTimes` equals `active` field for field but the clip name.
- `test_graph_masks.cpp` `entries_across_frames_equal_a_fresh_source`: 16 rows answers across six frames (two meshes on
  two skeletons in turn, a frame without the figure: refused by name, the model file replaced within a frame: refused
  again) equal a fresh `ControllerPose`'s bit for bit; 200 entries then 1: old entries not found, dropped.
- `test_clip_durations.cpp` `durations_from_the_clip_cache_equal_a_second_read`: on 0041's synthetic clips (two bound,
  two in two bases, one that does not bind, one missing, one unreadable, one never bound) the cache gives 4 and the rest
  is read (4 reads); all 8 entries equal a full second read by bits and text.
- The whole `c5r_units_|c5r_unitanim` suite (29 tests) plain, 0 skipped (2026-11-16 21:3x), and under MSVC ASan
  (29/29, 0 skipped, 0 ASan reports, peak 752 MB, 696 s; 2026-11-16 21:5x). The server's unit scene suites over this
  c5render (`build_units_tests.py`, `test_scene_units.exe`, `test_units_runner.py`, `run_units_seed_test.py`,
  `run_units_records_test.py`): all rc 0 (2026-11-16 22:05-22:09). `server/tools/unitfeed/test_unitfeed.py` fails 13 of
  its tests with this branch's unitfeed **and** with the one built before this issue's server change (the same 13: the
  synthetic fixture bundles are refused `MissingMember units/gen<N>.db: render_units is 1 and the bundle has no units_db
  member`, the scene library's newer bundle check): a failure on main, not of this issue [L, 2026-11-16 22:1x]. *(Corrected 2026-11-17 01:1x: stale since 0448 and 0403 changed the fixtures. On the merged state (main + this branch, f7a35da5b; render and unitfeed built from it, c5h-d's install, VP's catalogue database, UNITANIM_EXE and UNITSCENE_EXE of that build) `test_unitfeed.py`: 13 passed, 0 skipped, 0 failed [L].)*
- **The bench's outputs, base against this branch** (base = `render-base`, 249d68718 = main 27785c80 + the bench):
  identical per-frame hashes on all 1208 frames (`c751b37f281d5564`: 32 drawn, 1/115 s) and on all 6008 frames of a
  second set (120 drawn, 0.0123 s steps).
- **The durations on VP's database** (`bench_frame --durations`, 2026-11-16 21:3x, workstation, busy): the pre-check
  31,284 ms (233 entries, 231 art rows, 8676 clip names, 5402 clips, 1 without a duration: the Assyrian siege tower's
  unshipped charge clip); the full second read 8,356 ms; from the cache 5401 + 1 read in 37.6 ms; **0 of 5402 differ**.

### 19.4 Indications on the workstation [L: indications, D106]

Interleaved (6 rounds, order turned, other teams' jobs running; `C:/c5-64bit-work/0441/interleave-n2.txt`), the bench's
median ms per frame: `apply` base 5.18 / 4.71 / 4.50 / 6.08 / 5.51 / 6.49 against the branch (before the settle skip)
1.56 / 2.21 / 3.10 / 4.33 / 2.05 / 2.99, paired by round -2.7 ms (-50 %); `pose` 1.36 against 1.37 (no change resolved:
the pose rows are the curves' evaluation at moving clip times, `eval_curve`, `nlerp4`, `compose_transform`, which this
issue does not change). The read-ahead: 8.4 s -> 0.04 s after the pre-check (19.3).

**Profile before and after** (`perf/sampler`, 2 ms, RelWithDebInfo benches of base c2c73496c with the first bench and
of this branch at 0fd415ce8; 4000 frames after the timeline, 32 drawn, the same per-frame hashes `0131513fb9a063af` on
all 4008 frames; workstation 2026-11-16 22:12-22:15; reports `server/evidence/0441/offline/report-base.txt`, `report-n3.txt`, the sums `incl-profiles.txt`),
inclusive cycles of the whole run (setup included): all 110.5 G -> 85.4 G; `Controller::apply` 74.9 -> 49.1 G
(of it the start's graph binds in `settle` / `create`, ~5.5 G in both); `Controller::step` 54.5 -> 29.4 G;
`updateMachine` 37.7 -> 13.9 G, inside it `FigHost::ecComplete` 22.3 -> 6.7 G (two walks with copies -> one walk) and
`FigHost::roll` 9.8 -> 0.2 G (the point names); `ForcedChoices::take` 3.7 -> 0; the heap (`RtlAllocateHeap` +
`RtlFreeHeap`) 18.0 -> 6.0 G; the trigger walk 3.3 G (activeTimes; in the base inside `Graph::active` 26.3 G with the
ECComplete walks). `ControllerPose::rows` 16.8 / 19.2 G and `eval_curve` 7.5 / 9.4 G: the same work (the pose's
inputs and outputs are bit-equal), the difference is the machine's load between the two runs [I]. What is left of
`step` is its own per-figure loop (9.6 G self: the clock, the fade, the chain bookkeeping over ~4,000 figures held in
`std::map` nodes), `updateMachine`'s own tests (6.1 G self) and the one ECComplete walk (6.7 G).

### 19.5 The live series on oldpc (grade A) [L]

Oldpc job `20261116-221956-b3be` (`--quiet --headed`, 2026-11-16 23:02 - 2026-11-17 00:45 EST, exit 0), submitted by
`server/evidence/0441/submit_0441.sh` at 29e3a138f; 4466's `cost_4466.py` unchanged (`--issue 0441`), as 4468's set B.

```
Metric / tool:        as 4466's / 4468's sets (server CPU and GPU ms per drawn frame, server frames per s, idle-window EXE
                      frame time, memory after the load and at the end, AI turn wall and server CPU per turn, the
                      pre-check); server/evidence/0375/4466/cost_4466.py unchanged, --issue 0441
Builds:               u1, un: server.exe 6C2C9456 (this branch at 2b38bf18c: every change of 19.2); uo: 1480678C (main
                      c2c73496c, the commit this branch started from: 4468's controller and 0439's coverage), copied in for
                      its arms; x64 DLL 29F58E17 (pin 04b1211b8), proxy C14A678F and composite 078E0881 of this branch's
                      layout in every arm (C:/c5-64bit-work/0441/build_srv.sh, inputs listed in cost-inputs.sha256)
Arms:                 u1 = render_units=1, the controller on (the product); uo = u1 on the base server.exe; un = u1 with
                      render_units_anim=0 (no controller: the floor)
Game / config:        TBPP-HUGE_0350 (c499d3c6), product path (D33), timing launch, WaitForVSync 0, 2 AI turns per arm,
                      instance tb1
Series:               warm-up u1 + 6 rounds, the order turned each round (u1,un,uo / un,uo,u1 / uo,u1,un x 2), one game
                      at a time; quiet job: alone true, undisturbed false (foreign 0.182 cores mean, limit 0.1), CPU mean
                      47.7 %, free memory >= 6.4 GB
Result (paired by repetition, n 6, 95 % t interval; smallest detectable at 80 % power):
  server CPU per drawn frame   u1 10.30 ms (sd 0.22) / uo 12.64 (sd 0.24) / un 7.16 (sd 0.07)
                               uo - u1 +2.34 ms [+2.01, +2.67], det. 0.45: this issue's saving
                               u1 - un +3.13 ms [+2.93, +3.33], det. 0.27: what the controller still costs (uo - un
                               +5.48: the controller's cost before this issue on this set; -43 %)
  server frames drawn per s    u1 145.6 / uo 111.6 / un 242.7: uo - u1 -34.0 [-37.1, -30.9] (-23.3 %)
  server GPU per drawn frame   u1 1.103 ms / uo 1.202 / un 1.128: uo - u1 +0.100 [+0.080, +0.120] (the same GPU work
                               over fewer frames [I], as in 4468's sets)
  EXE frame time (idle)        u1 3.955 ms / uo 3.857 / un 4.018: uo - u1 -0.098 [-0.133, -0.063] (the EXE slightly
                               faster while the server draws fewer frames [I: contention], as in 4468's sets)
  AI turn wall (2 turns)       u1 26.41 s / uo 26.41 / un 26.70: uo - u1 -0.004 [-0.28, +0.27], not resolved
  server CPU per AI turn       u1 48.37 s / uo 46.92 / un 50.56: uo - u1 -1.45 [-2.05, -0.85] (the render thread draws
                               more frames during the turn: 146 against 112 per s)
  server private at the end    u1 1,860.5 MB / uo 1,815.2 / un 1,730.4: uo - u1 -45.2 MB [-65.3, -25.2]: this branch
                               holds 45 MB more after the controller's start (19.6); at the load (before the controller)
                               -3.3 [-28.9, +22.3], not resolved
  pre-check                    u1 13,815 ms / uo 13,672 / un 13,706: uo - u1 -143 [-448, +162], not resolved
  read-ahead after it          u1 52-60 ms (7 launches: 5401 durations from the pre-check's clip cache, 1 read) / uo
                               5,827-6,094 ms (6 launches: 5402 read), from each launch's render.log
                               (server/evidence/0441/readahead.py; readahead-20261116-221956-b3be.txt): the scene is
                               ready ~5.9 s sooner after a load
Machine load:         quiet proof above (quiet-summary-20261116-221956-b3be.json)
Profile:              19.4 (the bench under perf/sampler, base against this branch)
Why it moved:         19.1 / 19.4: the controller's frame no longer builds strings per random roll and per graph walk,
                      walks a graph once per ECComplete test with no copies, and skips settle for settled units; the
                      pose rows (curve evaluation at moving clip times) are unchanged
Correctness:          the bench's per-frame output hashes equal base on 1208 and 6008 frames (19.3); tests 19.3 plain
                      and ASan; in the series no LOUD line in any of the 19 launches, no dialog, the controller's first
                      frame 853 held / 29 drawn with the same 26 clips read on the render thread in u1 and uo
Trust grade:          A by PERFORMANCE.md 7 for the per-arm pairs (one series, one machine, six runs per arm, quiet);
                      the quiet proof says undisturbed false (foreign load over the limit), as 4466's and 4468's sets
Run records:          19 sealed c5-run/1 records in server/evidence/0441/runs/; summary
                      server/evidence/0441/cost-summary-20261116-221956-b3be.txt; job data
                      F:/c5-64bit-work/0441/oldpc-20261116-221956-b3be/
```
**The floor moved since 4468's set [I: across two sets].** In this set the no-controller floor is 7.16 ms (4468's set B: 4.90) and the
base product 12.64 ms (4468: 10.22), on the same save and machine: main between a669d00d and c2c73496c (0439 and the
server's other work since) costs ~2.3 ms per frame more in every arm. Numbers of two sets are not compared (PERFORMANCE.md
2.7); inside this set the controller's cost fell from +5.48 to +3.13 ms per drawn frame.

### 19.6 Not verified / left

- The pose rows' remaining cost (~1.7 ms on oldpc by 18's profile) is the animation itself; a cheaper curve evaluation
  is `models/`' and was not in this issue's files.
- **Memory: +45 MB [25, 65] at the end of the huge save's arms (19.5) [L; cause I].** Not seen before the controller
  starts (at the load: not resolved). The likely cause is 19.2's per-instance tables: every bound graph instance (one per
  figure and state entered, tens of thousands on this save) now also holds its nodes' named inputs, source indexes and
  node pointers (~70 bytes per node), beside the copy of the whole `DagGraph` it already held (`Graph::g_`, 4409). A
  follow-up would share the immutable part (the graph copy and these tables) between the instances of one graph file,
  which removes both; not done here (not measured which share each has). *(Measured and done by issue 0470,
  2026-11-17, 19.7: offline the tables are 9 % of an instance's 5.9 KB and the `DagGraph` copy 80 %, so the tables alone
  explain about 0.5 KB x the instances, not 45 MB unless the live save held ~86,000 instances; every bound graph now
  shares one shape per graph file and member, 32.2 MB -> 0.5 MB on the huge timeline.)*
- What remains of the controller's frame is per-figure work over ~4,000 figures (the clock, one graph advance, one
  machine update with its random draws, statuses): memory-bound traversal of `Unit` / `Fig` objects in a `std::map`.

### 19.7 Shared graph shapes (issue 0470, 2026-11-17)

Issue 0470 (account B, branch `task-0470-units-shared-graph-data`, from main 016bf28da: 0441 and 0450 merged). Why: 19.6's
+45 MB of server memory at the end of the huge arms with 0441.

**The census before [L, workstation, offline; byte counts C by construction].** Additive census: `GraphInstance::footprint`
(`GraphFootprint`: bytes as requested, vector capacity and string capacity beyond the small-string buffer, heap
overhead per block not counted; own / shared, by part), `Controller::graphCensus()`, `bench_frame --census` (with the
process's private bytes). On main + the census only (commit 0ef4c4fa0, outputs unchanged: the same per-frame hashes),
the huge timeline of 0450's bench (unitfeed of the format-6 bundle `C5H-0301-HUGE_0350_20261116-220048_workstation`,
`timeline.c5anim` sha256 4a182ae9..., c5h-c's install, mods 1, 2, 3a, VP's catalogue database
`F:/c5-64bit-work/0327/db/Civ5DebugDatabase.db`; 853 units held, 4,502 figures), 2026-11-17 03:17-03:39 EST:

| point | instances | member-states / files | bytes per instance (DagGraph copy / 0441 tables / sources / object, name, map) | all instances | private |
|---|---|---|---|---|---|
| after the timeline (8 frames) | 5,677 | 75 / 10 | 5,943 (4,756 / 522 / 262 / 404) | 32.18 MB | 114.6 MB |
| end, 32 drawn, 1200 frames of 1/115 s | 5,761 | 93 / 10 | 5,949 | 32.68 MB | 132.6 MB |
| end, 120 drawn, 6000 frames of 0.0123 s | 6,347 | 154 / 15 | 6,015 | 36.41 MB | 150.8 MB |
| end, all 853 drawn, 1800 frames of 1/30 s | 8,798 | 184 / 15 | 6,132 (4,920 / 540 / 260 / 413) | 51.45 MB | 207.6 MB |

So every figure holds one instance per state it entered (one per figure at the start: each figure's default state), each
with a full copy of its `.dge`'s `DagGraph` (every node with its type name, name, edge names, unread NodeData fields):
80 % of an instance. 0441's per-node tables are 9 %. The instances all come from 10-15 graph files and 75-184 (member,
state) pairs. [I] On the live huge save before 0450 every held unit advanced and entered states, so its instance count
was higher than this bench's (which steps only the drawn units); 0441's tables alone (~0.5 KB per instance) explain
45 MB only with ~86,000 instances, so the +45 MB of 19.5 is not fully explained by them (cause still open; see the live
series below).

**What changed [C: code and tests].** `graph.cpp`: a bound graph is an immutable `Shape` plus the instance's own state.
The shape holds what the walks read of the file, per node an `EvalNode` (type, LoopCount, Speed, Offset, ContinueOffset,
DefaultEdge, Scale and blocks, the callback hint and value, the scalar, the track mask's binding index and weights, 0441's
named inputs and source index), the root, the track mask names, the file name (failure texts) and the member's sources
(node, ec, clip, duration), and `programMayFail` (4468, computed on the shape by the same rule). An instance holds a
`shared_ptr` to its shape, its sources' start times, its clock and its options (64 B object + 4 B per source). Additive:
`GraphShare` and `bindGraph(file, ecs, name, opt, share, out)` (graph.h): every bind through one `GraphShare` whose shape
is equal to one bound before, every field above by bits (a key written from the shape; FNV-1a 64 buckets; a hit is an exact
comparison of the keys, never the file's address: a file replaced or freed cannot be confused with another), gets that
shape. Validation, statuses and texts are unchanged (every bind still checks the file). `ClipGraphFactory` (driver.h,
the product's factory in `AnimRunner` and the driver) binds through its own `GraphShare`. The three- and four-argument
`bindGraph` make an unshared shape (tests' factories). Unreachable change: a PoseAnimSource node without a source now
fails `BadLogicFile` "no bound source" where 4409's `std::map::at` threw (bindGraph fails EcNotBound first). bench_frame:
`--drawn K` above the held units ran past them (crash, exit without output); fixed to draw every unit.

**The census after [L, same inputs, 2026-11-17 03:31-03:34 EST, bench of d4440396b].** The same instances (5,677 / 5,761
/ 6,347 / 8,798), 75.8 B own each (64 object + 11.8 start times), shapes 63 / 79 / 121 / 150 (fewer than the member-states:
equal clip bindings share across members): all graph memory 32.18 -> 0.50 MB after the timeline, 51.45 -> 0.89 MB with
every unit drawn; private bytes 114.6 -> 78.2 MB after the timeline, 132.6 -> 95.9 (32 drawn), 150.8 -> 109.9 (120 drawn),
207.6 -> 148.4 MB (all drawn) (one run each, a busy machine: the private bytes are indications; the census bytes are
exact by construction).

**Byte-equality [C, L].** bench_frame `--dump` base (main + census, 0ef4c4fa0) against this branch (d4440396b): the
per-frame dump files identical, 19.3's two frame sets: 1208 frames, 32 drawn, 1/115 s, `c751b37f281d5564` (0450's
value); 6008 frames, 120 drawn, 0.0123 s, `af26782ad1a450f5`; and all 853 drawn, 1808 frames of 1/30 s, `8b4a0e8e8f29db4c`
in both (2026-11-17 03:39-03:44 EST; `F:/c5-64bit-work/0470/workstation/bench/dump-*.txt`, script `render/evidence/0470/bench_0470.sh`).
Tests (`render/tests/unitanim/test_graph.cpp`): `shared_shapes_answer_alike` (fade and mask graphs, held and as read: two
instances through one share, one from a second DagFile object of equal content, hold one shape; interleaved runs of
different seeds answer, point by point over 60 steps, program bits, active, activeTimes, complete, ecComplete, stateTime,
programMayFail, as unshared instances given the same runs: 480 points, the two shared instances apart on 238);
`shared_shapes_differ_by_content` (a speed, a block's duration and flip, an edge, a callback value, the file name, a clip,
a duration, a track mask name, a mask weight, a scalar each make a new shape answering as its own bind; a node's name, not
read, shares); `shared_bind_refusals_alike` (8 refusals: code and text equal, no instance, no shape and no bind added).
`test_controller_drawn.cpp` `shared_shapes_controller_byte_equal`: two controllers over the drawn-set timeline, one
binding through a share: programs, triggers, statuses byte-equal on every frame, every unit drawn (1485 programs, 31
triggers; 40 instances on 5 shapes against 40) and with the schedule (669, 21; 31 on 4); graph bytes 44,712 -> 7,688.
Mutation (not committed, 2026-11-17 03:2x): the share giving the first shape for every bind fails 222 checks.

**Cost offline [L: indications, workstation, one run each, other teams' jobs running].** `apply` median ms per frame
before / after: 32 drawn 2.04 / 0.97, 120 drawn 3.26 / 2.02, all 853 drawn 26.4 / 9.46; `pose` 2.32 / 1.43, 6.10 / 5.73,
42.0 / 32.5. [I] Why: the walks read 63-150 shapes of ~1.2 KB in contiguous `EvalNode` tables instead of thousands of
5-6 KB `DagNode` copies (each node with its strings and edge vectors), so the graph data of a frame's walks stays in the
caches; the binds no longer deep-copy a DagGraph (profile below).

**Interleaved offline rounds [L: indication, workstation, D106; 2026-11-17 04:46-04:48 EST].** `bench_0470.sh rounds 6`:
base (main + census, 0ef4c4fa0) against new (d4440396b), 32 drawn, 1200 frames of 1/115 s, the order turned each round,
`quietlock check` before each arm (3 other teams' heavy jobs listed every time); `render/evidence/0470/bench-interleave.txt`,
`bench-stats.txt` (`bench_stats.py`). Outputs hash `c751b37f281d5564` in all 12 runs. Paired new - base (n 6, 95 % t):
`apply` median -0.240 ms [-0.362, -0.118] (-36 %), mean -0.276 [-0.406, -0.147]; `pose` median -0.127 [-0.229, -0.024]
(-11 %); the process's private bytes at the end -37.05 MB [-37.43, -36.67] (132.6-133.7 -> 95.6-96.0 MB). Not profiled
(the change is made for memory; the time is a side effect); the grade-A numbers are the oldpc series'.

**Profile before and after [L, workstation, 2026-11-17 04:56-05:00 EST].** `render/evidence/0470/prof_0470.sh`:
`perf/sampler` (2 ms) over RelWithDebInfo benches of base (0ef4c4fa0, git archive) and new, every unit drawn (853) for 600
frames of 1/30 s after the timeline; the same per-frame outputs (hash `5d038a9f0a4c6ad4` over 608 frames in both);
inclusive cycles (`incl_0470.py`, `render/evidence/0470/prof-incl.txt`): all 119.0 -> 106.1 G; `Controller::apply`
29.3 -> 17.9 G; the graph walks `Graph::walk` 13.7 -> 3.4 G (`Graph::program` 7.0 -> 2.2, `activeTimes` 6.8 -> 1.2) for the
same walks over the same nodes in the same order; `Controller::step` 11.2 -> 5.2, `collect` 11.5 -> 6.9; the heap
(`RtlAllocateHeap` + `RtlFreeHeap`) 11.7 -> 9.3 G; `memmove` 9.1 -> 4.7 G; the new bind path (`bindShared`, `makeShape`,
`intern`) 0.16 G; `ControllerPose::rows` 38.6 / 38.2 G and `eval_curve` 9.7 / 10.5 G unchanged (the pose is not this
change's). Why it moved [I: the profile shows where, not the cache misses themselves]: the walks are the same work; what
changed is where their data lives: one ~1.2 KB `EvalNode` table per shape, read by thousands of figures, against a
5-6 KB `DagGraph` copy per instance whose `DagNode`s (strings, edge vectors, unread NodeData fields) are scattered over
the heap; the per-frame walks of ~4,500 figures then read a working set of ~0.1 MB instead of ~30-50 MB.

**The live series on oldpc (the acceptance's measure, grade A) [L].** Job `20261117-041041-c3b6` (`--quiet --headed`,
2026-11-17 05:20-07:05 EST, exit 0), submitted by `render/evidence/0470/submit_0470.sh` at 5151a5d50; 4466's
`cost_4466.py` unchanged (`--issue 0470`), as 0441's and 0450's sets.

```
Metric / tool:        as 4466's / 0441's sets (server CPU and GPU ms per drawn frame, server frames per s, idle-window EXE
                      frame time, memory after the load and at the end, AI turn wall and server CPU per turn, the pre-check);
                      server/evidence/0375/4466/cost_4466.py unchanged, --issue 0470
Builds:               u1, un: server.exe 534de5bd (this branch at 54c16bc6e: 0441 + 0450 + the shared shapes); uo: 24e0e1cb
                      (main c2c73496c, before 0441: 0441's own base, rebuilt from a git archive; 0441's build of the same
                      commit was 1480678C), copied in for its arms; x64 DLL 29F58E17 (pin 04b1211b8), proxy a99a5c84 and
                      composite 3359e381 of this branch in every arm (render/evidence/0470/build_0470.sh, cost-inputs.sha256)
Arms:                 u1 = render_units=1, the controller on (the product); uo = u1 on the base server.exe; un = u1 with
                      render_units_anim=0 (no controller: the floor)
Game / config:        TBPP-HUGE_0350 (c499d3c6), product path (D33), timing launch, WaitForVSync 0, 2 AI turns per arm,
                      instance tb1
Series:               warm-up u1 + 6 rounds, the order turned each round (u1,un,uo / un,uo,u1 / uo,u1,un x 2), one game
                      at a time; quiet job: alone true, undisturbed false (foreign 0.636 cores mean, limit 0.1: two
                      tvnserver.exe, a remote session open on oldpc), CPU mean 55.0 %
Result (n 6 per arm, 95 % t interval; uo - u1 and u1 - un paired by repetition; smallest detectable at 80 % power):
  server private at the end    u1 1,808.6 MB (sd 12.0) / uo 1,808.2 (sd 8.7) / un 1,770.2 (sd 7.3)
                               uo - u1 -0.4 MB [-11.7, +10.8], det. 15.3: not resolved, this branch equals main before
                               0441 within 15 MB (0441's set: uo - u1 -45.2 [-65.3, -25.2]); u1 - un +38.4 [+22.1, +54.7]:
                               what the controller still holds at the end (0441's set: +130.1)
  server private at the load   u1 1,361.1 / uo 1,426.9 / un 1,384.9: uo - u1 +65.8 [+16.2, +115.3] (u1 lower)
  server CPU per drawn frame   u1 10.09 ms (sd 0.18) / uo 13.50 (sd 0.16) / un 8.54 (sd 0.12)
                               uo - u1 +3.41 ms [+3.17, +3.64], det. 0.32; u1 - un +1.55 [+1.33, +1.76], det. 0.29: the
                               controller's cost (0441's set: +3.13; 0441 + 0450 + this issue against 0441 alone)
  server frames drawn per s    u1 154.8 / uo 103.4 / un 197.4
  server GPU per drawn frame   u1 1.214 / uo 1.121 / un 1.217 (the same GPU work over fewer frames in uo [I])
  EXE frame time (idle)        u1 4.350 / uo 4.236 / un 4.376: uo - u1 -0.114 [-0.181, -0.047] (as in 0441's sets [I:
                               contention while the server draws more frames])
  AI turn wall (2 turns)       u1 29.27 s / uo 28.53 / un 28.96: uo - u1 -0.74 [-1.63, +0.15], not resolved
  server CPU per AI turn       u1 54.62 s / uo 50.60: uo - u1 -4.02 [-5.37, -2.67] (more frames drawn during the turn)
  pre-check                    u1 12,679 ms / uo 13,413 / un 12,694: uo - u1 +734 [+506, +962]
Machine load:         quiet proof above (quiet-summary-20261117-041041-c3b6.json)
Profile:              19.7's (the bench under perf/sampler, base against this branch)
Why it moved:         19.7: one immutable shape per graph file and member instead of a DagGraph copy per instance; the
                      walks read compact shared tables
Correctness:          the bench's per-frame output hashes equal base on 1208, 6008 and 1808 frames (19.7); tests 19.7 plain
                      and ASan; no LOUD line but the TEST ONLY ones in the 19 launches; the controller's first frame 853
                      held / 29 drawn (R1-u1, 277.5 ms, 26 clip files read on the render thread)
Trust grade:          A by PERFORMANCE.md 7 for the per-arm pairs (one series, one machine, six runs per arm, quiet);
                      undisturbed false (foreign load over the limit), as 0441's and 0450's sets
Run records:          19 sealed c5-run/1 records in render/evidence/0470/runs/; summary
                      render/evidence/0470/cost-summary-20261117-041041-c3b6.txt; job data
                      F:/c5-64bit-work/0470/oldpc-20261117-041041-c3b6/
```
**Reading [I where marked].** The acceptance's two questions: the server's private bytes at the end of the huge arms are
those of main before 0441 (difference -0.4 MB, interval -11.7 to +10.8, resolvable 15.3 MB), and its CPU per drawn frame
is 10.09 ms against 13.50 for main before 0441 in the same set (0441's set read 10.30 against 12.64; two sets are not
compared, and the no-controller floor moved again, 7.16 -> 8.54 ms, between them). u1 here carries 0450 too (hidden units
neither step nor enter states, so fewer instances are bound), so the memory and the controller's cost (+3.13 -> +1.55 ms)
are the two issues together; the offline bench separates this issue's part (19.7: graph memory 32 -> 0.5 MB, apply -36 %).
The 45 MB of 0441 [I]: with the shapes shared nothing per instance is left but 76 B, and the end-of-arm memory is back at the
base's, so 0441's increase was graph-instance memory; whether its tables alone or more instances in 0441's arms made it is
not separated. The lower private bytes at the load (-66 MB [16, 115]) and the faster pre-check (-734 ms) are not this
issue's: the server's pre-check (`UnitRenderer::precheck`) binds no graph, and the controller starts after the load; u1 and
uo differ by everything merged between c2c73496c and 016bf28da (0441's read-ahead, 0450, other teams' server work), not
attributed here.

## 20. Workers slide instead of walking: orders posted with a move wait for the walk (ticket 5065, epic 0042, 2026-11-24)

W118 (account B), 2026-11-24 from 01:22 EST. The owner's beta 3 report (2026-11-23, main d56fbd94f, mode exe, route S):
workers seem to slide across tiles instead of playing a walk animation. Marks as in this file.

### 20.1 Cause

- **The DLL posts the build in the same call as the move [C, the DLL fork at the x64 pin].** The builder AI
  (`CvBuilderTaskingAI::ExecuteWorkerMove`, for automated workers and every AI worker) pushes `MISSION_MOVE_TO` and,
  when the worker stands on the target with moves left, `MISSION_BUILD` in the same call. The move mission ends with
  `PublishQueuedVisualizationMoves` (kind 12, `GameplayUnitMoved`), the build mission starts with
  `GameplayUnitWork(build)` (kind 18). So the server's feed carries kind 12 and kind 18 of the unit in one batch.
  (`MISSION_ROUTE_TO` posts `GameplayUnitWork(unit, 0)` before its moves, and a fortified unit's mission start posts kind
  17 before its move: neither is this case.)
- **The server applied kind 18 at once [C, `srv_units_anim.cpp` before 5065].** `AnimFeed::close` turned every op of
  the frame into its timeline event in feed order; nothing waited for the open walk (mode exe: the walk lasts until the
  EXE's arrival record, `unit-feed-contract.md` 4).
- **The worker machine leaves its run for any other request [C, `worker_state.fsmxml` through 4408's census].** The run
  loop (state 1) goes to stop-run on `P4 != 1400 && P4 != 1280`; the run start does not consume the request (no `S4=0`,
  unlike the omni machine). Path 4 is a level and the last write of a frame wins (1). So `move` (1400) and
  `action work` (19xx) in one frame never start the run, and a work a few frames later stops it.
- **Not the cause [C]:** the art rows. Both worker arts (`Worker_EURO`, `Worker_EURO_Modern`) bind 1400 (run) and 1440
  (stop run), and `ArtDefine_UnitMemberCombats` gives the worker member `Idle Death BombardDefend Run` (the bridge's word
  check passes). Quick movement (kind 13 per plot, no walk) and the AI-drain options do not enter this path.
- **Stock [I].** The EXE's queue holds later records naming a unit behind an earlier held one (`combat-choreography.md`
  1 item 3). That the move record is held for the walk, so that the work plays after it, is read from the behaviour (a
  worker runs, then digs), not from the EXE's code. A live stock pair was not run (20.4).

**Offline proof [C, 2026-11-24 01:30, `unitanim play` (a render build of 2026-11-23 23:13), the pure_vp install's
machines and graphs, the 0327 VP database copy, worker `ART_DEF_UNIT__WORKER`, 30 frames/s, a 1.2 s walk;
`server/evidence/0042/5065/w118/worker_walk.py`, output outside git]:** figure 0's state and clip per frame.

| Case | Events | During the walk | After the arrive |
|---|---|---|---|
| A | move, arrive 36 frames later | run start 3 frames, run 1400 33 frames | stop run 1440, idle |
| B | move + work BUILD_FARM in one frame | **no run**: enter work, rake 1920 for the whole walk | exit work, **idle** (the arrive's 1000 ends the work) |
| C | work 3 frames after the move | run 4 frames, stop run 1440, **idle 1000** for the rest | idle |
| D | working worker: work stop + move in one frame | runs | stop run, idle |
| E | move and arrive in one frame | no run (the server never sends this: such an arrive waits a frame, 4755) | idle |
| F | warrior: move + fortify 1 in one frame (a control) | **no run**: fortify | leave fortify, idle |

So with the server drawing units an automated or AI worker that reaches its tile in the same turn slides there playing
its tool clip (B) or idle (C), and then stands idle where stock shows it working.

### 20.2 The fix: a hold behind an open walk (lead-0042 #12560, #12565)

`AnimFeed::holdBehindWalks()` (`server/src/units/srv_units_anim.cpp`) is the first step of `AnimFeed::close`, before
any routing (the fights' hold `fightsFrame` included). Every op taken since the last close passes it in feed order. In
mode exe it follows each unit's walk as the op loop will see it: open at the close's start (`m_walks`), opened by a kind
12 of a held unit, closed by an arrive (not one whose move is in this close: the loop carries that one), a teleport or
rebase, a removal, a creation or a load. An op whose kind is in the **table** (`holdAfterWalk(kind, on)`) and whose
unit's walk is open waits in `m_afterWalkOps[unit]`; a unit a fight names is left to the fights' hold. `endWalk` gives
the unit's ops back (arrive, place, removal, creation; F9's rule ends a walk by an arrive). They go first at the next
close, in seq order, before the carried arrives, and pass `holdBehindWalks` again (a walk opened since holds them once
more). A unit with ops waiting, no open walk (its kind 12 opened none: no record facing, or not held) and no fight naming
it gets them back at the close's end (counted `afterWalkNoWalk`). `AnimFeed::walkOpen(owner, id)` is the shared test
(5066 builds on it). Mode own: no walk is open, nothing waits.

- **The table and the knob (rule A).** 5065 puts kinds 18 (work) and 17 (fortify) in it through `setOrdersAfterWalk`;
  server.cfg `render_units_orders_after_walk` (1 default | 0: the table empty, the rule before 5065 byte for byte),
  `UnitsOptions::ordersAfterWalk`, written into every bundle's `render_config`; sceneshot and unitfeed read the bundle's
  key with absent = 0 (a bundle recorded before 5065 replays as recorded). 5066 adds the mover's combat records to the
  table under its own knob (lead-0042 #12565).
- **Garrison and visibility stay out** (lead #12560). [I] candidates for a new ticket: a unit entering a city may get
  its garrison / visibility posted with the move, so the server would hide it at the walk's start where stock hides it
  at the arrival. Not counted.
- **Totals line:** ` after_walk=held/released/nowalk` after `deferred=`, only while the table holds a kind.

### 20.3 Tests

`server/tests/units/test_scene_units_afterwalk.inc`, cases `afterwalk_feed` and `afterwalk_worker_host`. The feed case
drives UnitsCore + AnimFeed in mode exe with the records as the EXE and the DLL send them (kind 12 + kind 18 in one
batch, the position records, the arrival): the work after the arrive; a work in a later batch; a second kind 12
continuing the walk; a fortify keeping feed order; a damage passing at once; a teleport, F9's rule and a removal ending
the walk; a kind 12 that opens no walk; off (the old events); mode own. The host case runs the product path
(SceneUnits, AnimRunner, the install's worker files) on and off and counts the clips the worker's figures play.

**Results [C, 2026-11-24 01:47-01:56, workstation, MC-B's slot #12578, `build_units_tests.py --out <dir>` (no LTO),
the pure_vp 4390913 install outside the pool, read only]:** `afterwalk_feed` PASS; `afterwalk_worker_host` PASS with
`on: walk frames 30 run 27 shovel 0, after: shovel 39, held 1 | off: run 0 shovel 26, after: shovel 0` (on: the run clip
from the fourth walk frame, the shovel once stopped, `after_walk=1/1/0`; off: the shovel through the walk, idle after);
`test_units_runner.py` 7 passed (every case of `test_scene_units.exe`). Every other builder of the changed files
(MC-B's slot #12592, 2026-11-24 01:55-02:45): `run_all.py` render-host 207, scenebundle-programs 4, scenebundle-py 28,
fow, overlay, shadow-ice, water and forest scenes PASS; `build_server.py` release, `build_units_extras_tests.py` (13/13),
`build_unitfeed.py`, `test_farmfields_build.py`, `build_trigger_scene.py` OK; `run_units_fights_test.py`,
`run_effect_routes_test.py` (`--render-build <run_all out>/rt/c5render`), `run_trigger_emitter_test.py` OK. Rule A
(offline equals live) not run yet: bundles recorded before 5065 carry no key and replay with 0.

### 20.4 Not verified

- The live pair against stock on one save (one and several tiles, by order and by automation) was not run.
- That the EXE holds the work record behind the move record is [I] (20.1).
- Garrison and visibility posted with a move (20.2).
