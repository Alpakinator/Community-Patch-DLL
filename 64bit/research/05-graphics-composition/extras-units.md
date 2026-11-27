# Unit extras: the units seam and the unit features of Extras B (epic 0455)

The extras of epic 0455 (Extras B: FixUnitPlacement, GarrisonVisible, ShipsMoored, FixAirBases (was AirBases, renamed 2026-11-20 by MC #7901); option ids 23-26) change
what the server's unit scene draws. This file holds the seam they share (section 4981) and, later, each feature's own
findings (4982-4986 add sections). The unit scene itself: `unit-server-scene.md`; the scene's extras module of epic 0454
(VisibleAreaOutline, roads): `extras.md`; the options and their rules: `docs/OPTIONS.md`, `options/include/vp64_options.h`.

## 4981: the units seam (issue 4981, W2 of team-0455, 2026-11-20) [C: the code and its tests; nothing run live]

Branch `w2-4981` (from `epic-0455-extras-b`). No feature: the seam only, with stand-in extras in its tests.

### 4981.0 In short

- One interface, `srvunits::UnitsExtras` (`server/src/units/srv_units_scene.h`), made through `MakeUnitsExtras()`, which
  returns the factory registered by `server/src/extras/units/srv_units_extras.cpp` and **null in a program that does not
  link that folder**: the scene is then exactly as before (no call, no line changes; the shared code on that path is
  `buildList` with the scene's root moved into `UnitsCore::sceneRoot` and the cull box `kCullRadius * 1.0f`, the same
  float; unitfeed on the huge bundle byte-identical, 4981.7.1).
- The unit host (`srv_units_scene.cpp`, server.exe's factory `MakeLive`) attaches it once:
  `attachExtras(MakeUnitsExtras(), SrvOptions(), SrvOptions().ServerUnits() == 1)`; a module with no extra in effect
  after `configure` is dropped (no per-unit call at all).
- Per drained batch: `onBatch(batch)` after `UnitsCore::onBatch` took it. Per built frame: `beginFrame(frame)` and then,
  for **every unit of the table** (listed by stock or not), `unit(u, io)`, before the cull and the fog filter.
- What an extra can do per unit (`UnitOverride`): show a unit stock hides (opaque), give it a root (x, y), turn it to a
  yaw, scale it (0 < s <= 4). Root and show act on the frame's `UnitScene`; turn and scale act on the prepared plan
  (`ApplyUnitXforms`, after `UnitRenderer::prepare`), so `render/` (0041's module and its `unit_scene.h` contract) is
  unchanged.
- E5: with `ServerUnits = 0` each unit extra that would be in effect writes one line `units extra <Name>: off:
  ServerUnits = 0` and does nothing. E6: an extra that fails is off for the session with one line, the others go on,
  and the scene draws stock for what it changed.
- Counts: the core's (unit-frames) and each extra's, appended to `units 1s` and `units totals` (4981.5).

### 4981.1 The contract (with lead-0042's three points, queue #7520, binding)

| Call | When | What it gets | What it may change |
|---|---|---|---|
| `configure(options, unitsAreOurs)` | once, at the host's start | the server's options (`SrvOptions()`), `ServerUnits = 1` | nothing in the scene; says per extra whether it runs |
| `onBatch(batch)` | every drained batch, after the unit scene took it (`UnitsCore::onBatch`'s end) | the batch | its own state |
| `beginFrame(frame)` | each build (`UnitsCore::buildList`), before the units | `UnitExtrasFrame`: the height grid, the unit and player tables, the feed's grid, the frame clock and id | its own state |
| `unit(u, io)` | each build, per table unit, before the cull and the fog filter | `UnitExtraUnit`: the record, the plot, whether stock lists it and whether stock hides it, **the scene's own root (mode exe) and heading (facing)**, `heldInFight` | `UnitOverride` io (show, root, heading, scale), in and out over the extras in order |
| `animating()` | each `SceneUnits::animating()` | | true: the next frame differs without a feed change |
| `countsText` | the `units 1s` and `units totals` lines | | its counts |

lead-0042's contract (epic 0042, increment 3, builds on `server/src/units/` right after this ticket):

1. **The hook runs after the scene set each unit's own root and heading** and sees them as inputs: `UnitExtraUnit::hasRoot`,
   `rootX`, `rootY` are the scene's root (mode exe: the newest position record, `UnitsCore::sceneRoot`, the same function
   the build uses), `plotX`, `plotY` the plot it draws at (a walking unit before its first position: the plot it left),
   `facing` the record's heading. 0042's per-frame facing (its 4772) replaces that input when it lands; the override
   then replaces the scene's root (`UnitInput::has_root`, `root_x`, `root_y`) and, after prepare, its yaw.
2. **0042's per-figure combat offsets (its 4776) are applied after the hook, in the unit's own frame.** The hook works on
   the unit's root, heading, scale and show only, never on per-figure positions. The places are marked in the code:
   after the override's root in `UnitsCore::buildList` (`// issue 4981: the extras' root, after the scene's own ...`) and
   at the end of each unit in `ApplyUnitXforms` (`// epic 0042 (4776): its per-figure combat offsets ...`).
3. **A unit held in a fight is never moved by an extra.** `UnitsCore::setHeldInFight(owner, id, on)` /
   `heldInFight(owner, id)` (default false; nothing sets it yet, 0042's visual queue 4774 will); the extra reads it as
   `UnitExtraUnit::heldInFight`, and the seam ignores every override of such a unit (show included), counted `held=`.
   Test `held_in_fight`.

### 4981.2 What hides a unit today, rule by rule

The server lists a unit when its visible flag is set or a fade-out is still running (`srv_units_core.cpp` `fadeState`
and the listing at the top of `UnitsCore::buildList`); the flag is the last `GameplayUnitVisibility` call
(`server/src/feed/srv_feed.cpp` 111-112: kind 24 `kUnitVisibility` sets or clears `kUnitFlagVisible`; a record is
inserted visible, 188; a call before the creation is ignored, 167). So every unit stock does not draw is hidden by **one
mechanism, the DLL's `Visibility(false)`**, sent by these sites (the DLL fork, branch `x64` at e9d0c4837, read
2026-11-20 [C]):

| Case | The call that hides it | Stock draws it? |
|---|---|---|
| a garrisoned unit, at a load | `CvUnit::setupGraphical`, `CvUnit.cpp` 1798-1801: `if(IsGarrisoned())` `Visibility(false)` + `Garrison(true)` | no [L, X0.5 in `extras.md`] |
| any unit that enters a city (a garrison, a civilian, a ship, an aircraft rebased there) | `CvUnit::setXY`, `CvUnit.cpp` 20527-20532 "if entering a city, hide the unit": `Visibility(false)` | no [L, `extras.md` X0.5: garrisons and ships in a city not drawn] |
| leaving a city | `CvUnit::setXY`, `CvUnit.cpp` 20470-20476: `Visibility(pNewPlot != NULL && !isInvisible(activeTeam))` (shown again) | yes |
| a unit not the centre of its plot (a stack; **every aircraft**, which is never chosen as centre unless selected) | `CvPlot::updateCenterUnit`, `CvPlot.cpp` 598-656: the selected unit, else the best defender if not air and not invisible, else the head unit if not garrisoned, not air, not invisible; the centre gets `Visibility(true, false, 0.01)`, every other unit `Visibility(false, false, 0.01)` | no |
| a based aircraft in a city; an aircraft carried by a carrier (cargo on the carrier's plot) | the two rows above (a city plot; not the centre unit) | no once the plot changes [L, `unit-server-scene.md` 12.3, 0442; corrected 2026-11-20 by 4980 and the 4985 re-scope: after a load both draw them until anything changes their plot; was: neither the EXE nor the server draws them] |
| an invisible unit (a submarine) of another team | `CvPlot.cpp` 549-581, 13058, 13154; `CvUnit.cpp` 20788-20808: `Visibility(isInvisibleVisible(activeTeam, ...))` | only when the active team sees its invisibility type |
| a city destroyed or captured | `CvCity.cpp` 2070-2083: the owner's units on the plot ungarrisoned and `Visibility(!isInvisible(activeTeam))` | yes |
| other teams' units on plots the active team does not see | none in the DLL at a load; the unit host's fog filter (`FilterByFog`, issue 4463) after the build | no [L, `unit-server-scene.md` 12.1] |

`UnitExtraUnit::hiding` is therefore "the visible flag is clear" (hidden, or fading out); `listed` is stock's own listing
this frame. The garrisoned, embarked and invisible flags (`UnitRecord::flags` bits 1, 0, 4) say which case it is; the
domain (`UnitRecord::domain`) tells an aircraft; the plot tells a city through the plot table (not in the seam's input:
an extra reads `SrvFeed::GetPlot...` or keeps its own from `onBatch`) [I: the order of `updateCenterUnit` and `setXY`'s
calls within one move was not established; the result per case is the [L] column].

### 4981.3 A shown unit goes through the same rules as any drawn unit

- **Residency:** only units of the scene's table are offered (`m_units`); an extra cannot add a unit the table does not
  hold.
- **Seed and art:** after a show the unit takes the same checks as a stock-listed one, in the same order: player record,
  colour, art style, unit type, plot, embarked row, the allocator's seed (`SeedMissing` when none); a failing check is
  counted `not_drawn` and named as before.
- **Fog:** the unit host's `FilterByFog` (and unitfeed's call of it) runs after the build: another team's shown unit on a
  plot the active team does not see is removed (test `show`, its fog part).
- **The seam's own refusals** (counted `show_refused=`): a dead unit (`kFlagDead`); an invisible unit (`kFlagInvisible`)
  whose owner's team is not the active team, or that has no player record (never revealed by an extra). A show of a unit
  stock lists (flag set) does nothing; a show of a unit fading out makes it opaque at once (1, 0, 0).
- **Root:** the module's placement puts the formation's slots around the given root and samples the heights there, as for
  mode exe (`UnitInput::has_root`); `root_z` is not read by the placement.

### 4981.4 Turn and scale after prepare (`ApplyUnitXforms`, `srv_units_core.cpp`)

`BuiltFrame::xforms` (by `UnitKeyOf`) holds each drawn unit's heading and scale; the host applies it to each wrap copy's
plan after `UnitRenderer::prepare` and `afterPrepare`, before the reflection and scene passes (both draw the same plan).
For a prepared unit (status Ok) with an entry: the pivot P = (placement root x, root y, the mean z of its living figures),
d = yaw - the placement's yaw (0 without a heading), L = s Rz(d); every bone row triple of its palette blocks (rows 2-97 of
each 98-row block: `[A | b]` -> `[L A | P + L (b - P)]`) and the world rows 0-2 of each material-87 cb0 are transformed:
the whole unit turns and scales rigidly about P. The colour and fade rows and every other unit's rows are untouched (test
`scale_heading`, exact against the formula to 1e-5).

Consequences [I, not measured; for the features to check in their pictures]: figures on sloped ground keep their heights
relative to P after a turn (no new height sample) and move up or down with a scale (s (z - Pz)); the shader's normals go
through the same rows, so a scale may change the lighting if the program does not normalise them (`unit-draws.md` names
the programs; not read for this); the dump (`render_test_units_dump`, `.c5units`) and unitfeed carry the root and the
show but not the turn or scale (they are not `UnitScene` fields). The cull box of a scaled unit grows with s (> 1).

Why not a `UnitScene` field: `render/include/c5r/units/unit_scene.h` is 0041's contract; a `scale` or `yaw` field there
needs `render/src/units/placement.cpp` and `draw.cpp` to read it (outside 4981's files). The plan transform gives the same
picture for a rigid turn and a uniform scale; a feature that needs the formation re-placed (slots and heights at the new
heading) asks for the field at its milestone.

### 4981.5 Lines and counts

- Attach (render log, once, `UnitsHost::attachExtras`; MC #7594, for the live smoke): `units: extras: module not linked
  (the scene as before)` when the factory is null, else `units: extras: module linked, <n> in effect: attached` or
  `units: extras: module linked, 0 in effect: dropped (the scene as before)` (n from `UnitsExtras::inEffect()`), after
  the extras' own configure lines.
- Start (render log): per extra, one of `units extra <Name>: in effect`, `units extra <Name>: off: ServerUnits = 0` (E5),
  `units <Key>: off: ...` (`Vp64Opt::DescribeExtra`'s reason; a stand-in without a row: `units <Name>: off: its own flag
  is 0 | its category switch is 0 | an extra it needs is off`), or `units extra <Name>: LOUD: failed: configure: ...`.
- E6: `units extra <Name>: LOUD: failed: <where>: <why>: off for the session (the scene draws stock for it)`, once per
  extra; `<where>` = `unit (<owner>, <id>) at frame <n>`, `beginFrame`, `onBatch`; causes: the extra returned false,
  threw, or gave a root or heading that is not finite or a scale outside (0, 4]. The unit keeps what the extras before it
  gave. A value outside the ranges that reached the core anyway is ignored for that unit (`bad=`, one LOUD line).
- `units 1s` and `units totals` end, when a module is attached, with
  ` | x shown=<n> show_refused=<n> root=<n> heading=<n> scaled=<n> held=<n> bad=<n>` (the core, unit-frames since the
  start) and per extra ` | extras <Name> on|off|failed ov=<n> shown=<n> scaled=<n> failures=<n>` (`ov`: root or heading
  changed by it; `shown`: show asked for a unit stock hides). Without a module the lines are as before.

### 4981.6 Files and the build

- `server/src/units/srv_units_scene.h` (the interface, the registry), `srv_units_core.h/.cpp` (`setExtras`, the per-unit
  hook in `buildList`, `sceneRoot`, `extraOverride`, `setHeldInFight`, `ApplyUnitXforms`, counters),
  `srv_units_scene.cpp` (registry, `attachExtras`, the lines, `animating`, the plan transform after prepare).
- `server/src/extras/units/srv_units_extras.{h,cpp}`: `srvextras::UnitExtra` (one feature: `name`, `id`, `state`,
  `configure`, `onBatch`, `beginFrame`, `unit`, `animating`), the features' list `UnitExtraMakers()` (empty; 4982-4986 add
  theirs), the module (E5, E6, counts), its registrar.
- **server.exe** links the folder: `server/build_server.py` `render_sources()` has the line (agreed by lead-0454, queue
  #7547). **sceneshot** does not yet: its line (`esrc += sorted((REPO / 'server' / 'src' / 'extras' / 'units').glob('*.cpp'))`)
  goes into 4938's extras block of `build_sceneshot.py`, added by lead-0454 when 4938 lands (its objects go to
  `odir/extras`; the stem `srv_units_extras` clashes with none of 4938's `srv_extras*`, `srv_extra_visible_outline*`).
  A program without the folder gets null from `MakeUnitsExtras()`; with it and no feature, a module with nothing on,
  which the host drops.

### 4981.7 Tests and results

`server/tests/extras/units/test_units_extras.cpp` (built by `build_units_extras_tests.py --out DIR --c5render RDIR`, the
render units of `test_scene_units.exe` + `server/src/extras/units/*.cpp`; no install, no device), stand-in extras with
no option row:

| Case | What it shows |
|---|---|
| `null_factory` | `MakeUnitsExtras()` null after `RegisterUnitsExtrasFactory(nullptr)` (a program without the folder); the features' module has nothing on (no feature yet); a core without extras, a core with an extra that changes nothing, and a core given a module with nothing on build **field-for-field equal** `UnitScene`s; the host keeps no module for null or nothing-on, and its totals line has no extras part |
| `override_root` | the extra sees the scene's own root, plot and facing as inputs and is called for every table unit; its root reaches `has_root`, `root_x`, `root_y`; counts `root=1`, `ov=1`; `onBatch` reaches it after the core took the batch |
| `show` | a hidden fighter (plot of a city) and an invisible unit of the active team listed opaque (1, 0, 0) with the allocator's seed; a dead unit and an invisible unit of another team refused (`show_refused=2`); a shown unit of another team removed by `FilterByFog` on an unexplored plot; a fade-out made opaque |
| `scale_heading` | the build's `xforms` entry; the `UnitScene` input unchanged; `ApplyUnitXforms` on a synthetic plan exact against P + s Rz(d) (w - P) for every bone triple and the material-87 world rows, the colour/fade rows and the other unit untouched, the placement's own yaw at scale 1 a no-op |
| `server_units_off` | E5: one `off: ServerUnits = 0` line for the extra that would run, the reason line for one that is off, nothing else; `in effect` with the units ours; the host drops the module |
| `failing_extra` | E6: an extra returning false, one throwing, one giving a scale 9: one LOUD line each (none more on the next frame), off for the session, the unit keeps the earlier extra's root, the good extra goes on; `countsText`; a throw in `onBatch` |
| `held_in_fight` | lead-0042's point 3: root, scale and show of held units ignored (`held=2`), the extra saw the flag; the override applies again once released |
| `host_lines` | `attachExtras` through `MakeSceneUnitsWith`; `units 1s` and `units totals` carry ` \| x shown=0 ... \| extras StandIn on ov=0 ...`; `animating()` from an extra; `feed()` reaches `onBatch` |

#### 4981.7.1 Results (2026-11-20 02:06-02:20 EST, workstation, clang-cl x64; c5render built from this branch after the merge of `epic-0455-extras-b` @b2378472d; every run through `quietlock.py run`) [C]

- `test_units_extras.exe`: **8 of 8 cases passed**.
- The units suite on this branch (`server/tests/units/build_units_tests.py` + `test_units_runner.py`, `C5R_GAME_DIR` =
  the huge bundle's own install, read only): **7 passed** (pytest), `test_scene_units.exe` **50 of 50 cases passed**.
- **unitfeed on the huge bundle** (`C5H-0301-HUGE_0350_20261119-214920_workstation`, `--db units/gen0000000001.db`, the
  bundle's install and MODS folder), built once from this branch and once from the team branch @b2378472d (a `git
  archive` of it, the same c5render): both `OK 8 frames ... 2933 unit events, 232 units listed in copy 0, seeds created
  853 ... statuses: none; 0 LOUD lines`, fog `48 unit-frames hidden`; **the 9 output files byte-identical** (`diff -rq`).
  unitfeed links `server/src/units/` (the restructured `buildList`, `sceneRoot`) but no extras module: the drawn list of a
  program without the module is unchanged.
- **sceneshot P1 on the huge bundle** (units on, `render_units_positions` own; 2026-11-20 02:21-02:22 EST, RTX 4080, the
  bundle's adapter): the player built from this branch **with `server/src/extras/units/` linked** (an untracked scratch
  copy of `build_sceneshot.py` with 4938's block's line for this folder, removed after the run; the module, with no
  feature, has nothing on and the host drops it: no `units extra` line in the log), `sceneshot --dumped --compare
  --tolerance 1:1 --no-pictures`: **`OK 8 shot(s), each byte-equal to the server's own dump`**, colour 0 of 3,603,694
  pixels and depth 0 values differing in each of frames 1196-1243 (tighter than the accepted 1:1).
  *(Corrected 2026-11-20 by 4983 [C, `server/tools/sceneshot/sceneshot.cpp` read and a scratch round where on equalled off
  byte for byte]: the player makes its unit scene with `srvunits::MakeSceneUnitsWith` directly, not through server.exe's
  `MakeLive`, so `attachExtras` was never called: the run above linked the folder but exercised no module (there was no
  module to drop, and no `units: extras:` line). The player attaches the module since 4983's sceneshot hunk (4983.6);
  was: "the module, with no feature, has nothing on and the host drops it".)*

### 4981.8 Not verified

- Nothing ran live, and server.exe was not built by 4981 (a full build needs the DLL fork's build_all; the seam's units and
  the module compile with the render units' own flags in the test programs above). With no feature the server's
  host drops the module at `attachExtras`, so its drawing equals the unitfeed and sceneshot results.
- The turn and scale's look (normals, slopes) and their cost per frame (one 3x4 product per bone row triple of a changed
  unit: 32 per block) are not measured.

## 4980: the scene kit

Written 2026-11-20 by W1 of team-0455 (account B, ticket 4980, branch `w1-4980`). Marks: [C] confirmed by code or a
file, [L] seen live, [I] inferred. Every picture here is DIAGNOSTIC, never ACCEPTANCE (D112); popups were closed by Lua
before each one (D114). Pictures, saves and bundles are on F: (D141), never in git (COMMON 27, D83).

### What the kit is

One staged save holds every case of the brief at its own plots; the stock pictures and the product bundles are both
taken from that save after a load, one camera per case, so stock and server show the same game state at the same
camera.

- **Staging.** A copy of the Goldberg Tiny save `C5H-3600-TINY_0008` of c5h-c (SHA-256 `b2ceb663...`, the same bytes as
  the library's `C5H-0167-TINY_0008`; Tiny 56 x 36, turn 8, active player 0 Polynesia) is loaded on the stock arm,
  the map dumped by Lua (`lua/info.lua`, `lua/mapdump.lua`), the plots of each case picked offline by `plan.py`
  (nearest the capital first; new cities on free land or hills with no foreign plot within 2 and at least distance 4
  from every city), the cases set up by `lua/stage.lua`, and the game saved by the game itself (the harness checklist's
  `save_game` step, `UI.SaveGame` on the main thread). The staged save is then loaded again: Lua plot-type and feature
  changes are drawn only after a load (`scene-catalogue.md` 2), and the bundles are recorded from a load anyway.
- **Stock pictures.** The stock EXE (build 4390913, stamp 0x546CD0A8) with a 32-bit pin DLL, no proxy, no server, no
  composite; per case: units of the active player that are awake put to sleep, `UI.LookAt` on the case plot, the zoom
  by wheel ticks, the interface hidden by Lua (P4), popups closed, the harness shot recorded in Visual Checks
  (`kit.py shots`).
- **Bundles.** The product path (`ServerRenderer = 1`, `ServerUnits = 1` in `VP64Options.ini`, the composite with its
  manifest, D33 with Overlay on) with the x64 DLL of fork branch `x64-render-events` (addd57b8b, the define on), the
  recorder through `server_cfg_extra` (`render_test_record_after=1800`, `_frames=8`, `_dump=all`), one launch per case,
  the case camera set beside the run by `kit.py camera` (the scenario runner's `camera` with the kit's quiet).
  `bundle_refs.py` then adds the case's named camera (`cameras/<case>.vpcam`, the span's one camera block;
  `cameras/<case>.json`, the camera runner's record) and the stock shot (`refs/<case>.png`) as members and runs the
  checker.
- **Kit quiet.** The scenario runner's `quiet.lua` puts every unit of the active player to sleep; that would end case
  7's intercept (activity 5 -> 2) during the product run. `lua/quiet_kit.lua` only puts AWAKE units to sleep and never
  touches cargo; the staging itself leaves every other unit asleep with its moves used.

Scripts: `server/evidence/0455/4980/` (`kit.py`, `plan.py`, `lua/`, `rec_kit.py`, `bundle_refs.py`, `feedkinds.py`,
`s01_fork.sh`, `s02_build.sh`, `s03_stock.sh`, `s04_record.sh`). Data: `F:/c5-64bit-work/0455/4980/` (`map/`, `plan/`,
`stage/`, `saves/`, `stock/`) and the bundles in `F:/c5-64bit-work/scene-library/staging-4980/` (moved to `bundles/` 2026-11-20 04:58).

### Builds [C, 2026-11-20 01:38-02:04 EST, `s02_build.sh all`]

| Piece | From | SHA-256 |
|---|---|---|
| x64 DLL, define on, release (LTO) | fork `x64-render-events` addd57b8b (= x64-batch5 1fd51353e + the render-messages commit), `tools/x64-dll/build.py --config release` | `3e731989...1d22`, `render_events: on` |
| server.exe, x86 proxy (release) | this repository at 12814b409 (archive), `server/build_server.py --deps` a copy of a release build_all of 1fd51353e | proxy `68aa33a5...` |
| composite | the same archive, `shell\build.cmd` | `ff6b97ff...` |
| recorder_test (checker), sceneshot (player) | the same archive | `a6e48897...`, `cb3646e3...` |
| layout | the pin layout `x64-1fd51353-p1e09fc7` bin with the server.exe and the define-on DLL above | |

The 32-bit DLL of the stock arm is `i0ship-e96457fd-ia` (i0 pin batch 4, SHA-256 `3eaa3290...3603`): the
instance-aware build of the current i0 pin 5d18d3d6c (`i0ship-5d18d3d6-ia`) was absent under `c5-i0/dlls` on
2026-11-20 (lead-0455 accepted the deviation). The stock pictures show one loaded game state; the batch-5 changes
(the path finder fix) play no part in a picture of a loaded save [I].

### The staged save [L, 2026-11-20 02:16-02:36 EST, c5h-c]

c5h-c (C:, Goldberg, build 4390913, mods548: CP, VP, EUI; Vibrant Textures present), claim w1-4980 for 0455. Source
`C5H-3600-TINY_0008` (SHA-256 `b2ceb6632d9766bd...`), copied to `C5H-4980-SRC_0008`, loaded at game turn 9 (the file
name's 0008 is the save's own label). Map dump `F:/c5-64bit-work/0455/4980/map/map.json` (56 x 36, 2,016 plots, capital
Honolulu (51,14)); plan `plan/plan.txt`, `plan.json` (with the Lua table), `cases.json` (the cameras). `stage.lua`
answered `OK` (`stage/stage.json`): 10 cities, 3 wonders, 21 units, damage on three cities. The game's own save
(`UI.SaveGame` on the main thread, 5.8 s) was kept as **`C5H-4980-KIT_0009`**, SHA-256
`74fc742879b40710eba0550ff24dd0e28c8666a0d38b7e56a585839f1b2944db`, 252,593 B (`F:/c5-64bit-work/0455/4980/saves/`).
After the load again, `verify.lua` read (`plan/verify_loaded.txt`):

| Case | Plots | Staged (unit ids) | State after the load |
|---|---|---|---|
| c1_mountain | mountains (49,13), (49,14), (48,14) | horseman 1051, spearman 1052, catapult 1053 | asleep, on the mountain plots |
| c2a_natural | Mount Kailash (38,12) (impassable); land (37,11), (38,11) | swordsman 1054, horseman 1055 | asleep, next to it |
| c2b_wonder | city W Samoa (54,15), pop 6; land (53,15), (55,15), (54,16) | Pyramid, Great Wall, Stonehenge; swordsman 1056, horseman 1057, catapult 1058 | wonders held; a settler 1049 and a great general 1050 were in that plot before the staging and stay in the city |
| c3a_garrison | city G1 Tonga (48,17) | spearman 1059 | garrisoned (`GetGarrisonedUnit` 1059) |
| c3b_garrison2 | city G2 Nuku Hiva (49,9) | swordsman 1060, archer 1061 | 1060 garrisoned, 1061 in the city |
| c4_port | city P Raiatea (54,19), coastal | trireme 1062, worker 1063 | the trireme counts as the garrison (`IsGarrisoned` true) |
| c5_air | city A Aotearoa (45,13) | fighter 1064, bomber 1065, jet fighter 1066 | based, asleep |
| c6_carrier | sea (1,12) next to city C Tahiti (1,13) | carrier 1067; fighters 1068, 1069 created on its plot | both cargo of 1067 (`IsCargo` true), activity 0 |
| c7_intercept | city I Hilo (46,8) | fighter 1070 | `MISSION_AIRPATROL` took: activity 5 (intercept) |
| c8a/b/c | cities S1 Te Wai Pounamu (42,14), S2 Rapa Nui (42,9), S3 Tuamotu (4,10) | damage 5, 12, 40 of 282 | as set |

### The stock pictures [L, 2026-11-20 02:37-02:40 EST, c5h-c]

Stock EXE with `i0ship-e96457fd-ia`, the kit save loaded, zoom level 15 (17 for c2a), 2554 x 1411 client area, the
interface hidden. Files `F:/c5-64bit-work/0455/4980/stock/stock_<case>.png` with their camera records `<case>.json`.

| Case | Visual Checks id | What stock draws [L, this one shot unless a mask pair is named] |
|---|---|---|
| c1_mountain | vc-20261120-023749-workstation-f039 | all three units on the mountain slopes; the catapults and spearmen stand on the rock faces, the horsemen's legs on the steep flank (X0.4's observation again) |
| c2a_natural | vc-20261120-023801-workstation-cc17 | Mount Kailash's model on its plot, the two units next to it below the model; no figure inside the model in view |
| c2b_wonder | vc-20261120-023815-workstation-4591 | the Great Wall's ring runs around the city's neighbour plots, the units of (53,15), (55,15), (54,16) stand inside the ring; Stonehenge's circle stands next to the swordsmen at the ring's west end; the Pyramid at the ring's east side. Figures stand in the city centre (a mounted great general with a banner and the settler): see the masks below |
| c3a_garrison | vc-20261120-023828-workstation-bcf1 | the city; no garrison figure noticed |
| c3b_garrison2 | vc-20261120-023842-workstation-e656 | the city; no figure of the two units noticed *(corrected 2026-11-20 by 4983 [L, this shot and the server's off-picture of the c3b bundle]: a formation of archers stands at the city centre, drawn by stock and the server alike: the non-garrisoned archer 1061 [I, by elimination]; the garrison 1060 is not drawn; extras-units.md 4983.7)* |
| c4_port | vc-20261120-023854-workstation-122a | the city with a group of worker figures standing among its buildings; no ship drawn in the city: see the masks below |
| c5_air | vc-20261120-023907-workstation-d944 | the city; no aircraft noticed in this shot centred on it |
| c6_carrier | vc-20261120-023920-workstation-50ca | the carrier with three plane figures beside and over it |
| c7_intercept | vc-20261120-023933-workstation-c64f | three plane figures at the city plot, partly hidden by the buildings, the same spots in the server's dumps 2 s apart: a parked formation, **no circling patrol** (corrected 2026-11-20 03:00: a first reading of this shot as "two planes circling over the city", sent to the lead at 02:44, was wrong); the based aircraft of city A (45,13), off centre at the picture's top, show partly among its buildings |
| c8a_smoke_lo | vc-20261120-023945-workstation-974f | raw damage 5: thin smoke trails over the buildings, no fire |
| c8b_smoke_mid | vc-20261120-023959-workstation-37d0 | raw damage 12: two to three small fires and smoke |
| c8c_smoke_hi | vc-20261120-024015-workstation-280a | raw damage 40: three to four fires and smoke over the buildings |

### Aircraft, civilians and ships in a city or on a carrier: per-unit masks [L, 2026-11-20 04:02-04:11 EST, c5h-c]

A third stock launch of `C5H-4980-KIT_0009` (`s05_mask.sh`, `kit.py mask`): at a case camera a shot, the listed units
removed by Lua (`Kill(false, -1)` on the save copy), the same shot again (camera unchanged in every pair). Differences
read on crops of each pair (`maskdiff.py` and the pair crops in `F:/c5-64bit-work/0455/4980/look/`).

| Pair | Units removed | Before / after | Crop pair | Result [L] |
|---|---|---|---|---|
| c5_air | based fighter 1064, bomber 1065, jet fighter 1066 in city A | vc-20261120-040312-workstation-511e / -040319-...-e53b | vc-20261120-042052-workstation-a4de | **stock draws them**: plane figures among the city's buildings, gone after |
| c7_intercept | fighter 1070 on intercept (activity 5) in city I | -040416-...-283f / -040423-...-acdd | -042053-...-198d | **stock draws it** as a three-plane formation parked at the city plot, partly in the buildings, gone after; no patrol animation |
| c6_carrier | fighters 1068, 1069, cargo of carrier 1067 | -040520-...-c396 / -040527-...-f571 | -042054-...-afbf | **stock draws them** as plane figures beside and over the carrier, gone after; the carrier stays |
| c4_port ship | trireme 1062 in city P | -040636-...-270f / -040642-...-42d4 | -042056-...-9daa | no visible change: stock draws no ship in a city (as X0.5) |
| c4_port worker | worker 1063 in city P | -040738-...-6213 / -040744-...-cbb8 | -042058-...-c36b | **stock draws it**: the worker figures among the buildings, gone after |
| c2b_wonder civ | settler 1049, great general 1050 in city W | -040842-...-80be / -040849-...-1f5b | -042101-...-437a | **stock draws them**: the general on horseback with the banner and the settler figures in the city centre, gone after |

- **Not every time [L].** The first stock session's shot centred on city A (02:39:07, vc-20261120-023907-workstation-d944)
  shows no aircraft at A, at the same camera and zoom as the mask's before shot (crop pair
  vc-20261120-042108-workstation-e7c1); 26 s later the c7 shot of the same session shows A's aircraft partly among the
  buildings at the picture's top (crop vc-20261120-042122-workstation-1d8b). Why the first shot had none is not
  established [I: the EXE's per-plot unit visibility, which the DLL's `updateCenterUnit` sets (0442, `unit-server-scene.md`
  12.3), may have changed between the two moments; not measured].
- **Loaded versus created live.** 0442 created its fighters during play (InitUnit and rebase in the running game) and
  pictured them in the same session; here every unit was created in one session, saved by the game, and pictured after
  a LOAD of that save. The server's dumps of the kit bundles draw the same figures at the same spots as stock (crops
  vc-20261120-042111-workstation-5b5a, -042113-...-51c8 (frames 2474 and 2415 of c7, 2 s apart: no movement),
  -042114-...-f916 (c6), -042116-...-ed1e (c5), -042117-...-10bc (c4 worker), -042119-...-f464 (c2b)), so the
  server's unit table lists them visible after a load [L]. No bundle holds an aircraft created or rebased after its load:
  every kit bundle's `feed.bin` has no kind 30 (rebase) and its kind-1000/1001 events are all the load's setup sends
  (turn -1) [C, `feedkinds.py`]. So 0442's "drawn by neither" stands for aircraft created live in the session [L, 0442]
  and does **not** hold for aircraft present at a load [L, here].
- **Intercept (MC #7602)** (a) the server draws the intercepting fighter like stock: parked, the same three planes at the
  same spots, no patrol [L]; there is no increment 3 gap here. (b) Whose aircraft: the kit has the active player's
  aircraft only; another player's aircraft on intercept was not staged: **not checked**. Since stock shows no patrol at
  all, (b) is about the parked formation only.
- **Garrisons** (c3a, c3b) were not masked; the single stock shots show no figure of the garrisoned or second unit
  [L, one shot each], as X0.5's masks of Giza and Akhetaten found for a garrison. *(Corrected 2026-11-20 by 4983: c3b's
  shot shows the second, non-garrisoned unit (the archers) at the city centre, and so does the server's picture; the
  garrisons themselves are not drawn in either case; 4983.7.)*

**Corrections proposed (the lead edits at the merge; not this ticket's files):**
1. `research/05-graphics-composition/extras.md` line 24 (X0.0 row 5a), cell 3: replace "**holds** (0442: fighters based
   in a city and fighters carried by a carrier are drawn by neither stock nor the server)" with "**fails** for a loaded
   save: stock draws based and carried aircraft (4980 masks, extras-units.md 4980); **holds** for aircraft created live
   in the session (0442) (corrected 2026-11-20 by 4980; was: holds, drawn by neither)", and the mark cell "[L] 0442" with
   "[L] 0442, 4980".
2. `extras.md` lines 167-170 (X0.5 "Based aircraft and cargo"): after "...drawn by neither stock nor the server
   (vc-20261116-211305-workstation-9d7c, vc-20261116-211303-workstation-6a4e; `unit-server-scene.md` 12.3)." insert
   "*(Corrected 2026-11-20, ticket 4980: that holds for aircraft created live in the session; after a load, stock draws
   based aircraft among a city's buildings, an intercepting fighter as its parked formation and carried aircraft beside
   the carrier, and so does the server (mask pairs vc-20261120-040312-workstation-511e/-040319-...-e53b,
   -040416-...-283f/-040423-...-acdd, -040520-...-c396/-040527-...-f571; extras-units.md 4980). Civilians in a city
   (worker, settler, great general) are drawn too (-040738/-040744, -040842/-040849).)*"
3. `research/05-graphics-composition/unit-server-scene.md` lines 424-425 (the italic 0442 note): append "*(2026-11-20,
   ticket 4980 [L]: for a LOADED save both stock and the server draw based, intercepting and carried aircraft; the
   0442 result is for aircraft created live; extras-units.md 4980.)*"
4. `unit-server-scene.md` line 2566-2571 (12.3 "Based aircraft and cargo"): append the same one-line note.
5. `unit-server-scene.md` line 2712 ("kind 30 ... drawn by neither arm when based or carried, 12.3"): add "(created
   live; after a load both draw them, 4980)".
6. The epic's premise P3 (`tracker/issues/0455-epic-extras-b.md` C1: "Stock draws no unit standing in a city (garrison,
   ship, civilian) and no based or carried aircraft") holds only for garrisons and ships after this; for 4983-4985 the
   lead decides how the extras treat aircraft and civilians that stock already draws.

### The bundles [L, 2026-11-20 02:45-03:58 EST, c5h-c; checks C, 04:14-04:40 EST, workstation]

Product path (`s04_record.sh` over `rec_kit.py`): the layout of `s02_build.sh` (server.exe and proxy `68aa33a5...` of
w1-4980 at 12814b409, composite `ff6b97ff...`, x64 DLL `3e731989...` of `x64-render-events` with the define, release),
`VP64Options.ini` ServerRenderer = 1, ServerUnits = 1, a per-case copy of `C5H-4980-KIT_0009`, recorder
`render_test_record_after=1800`, `_frames=8`, `_dump=all`, the case camera set by `kit.py camera` 87-108 s after the
launch, settle 110-120 s. Every bundle: format 9, 43 members (with the three added by `bundle_refs.py`), save SHA-256
`74fc7428...` turn 9, `render_units` 1, `render_units_positions` exe, the one note `gap: bake-cache tap left out
(render_tiles=bake)`; 8 frames, all dumped (2554 x 1411); **one camera in all 8 blocks**; checker `scenebundle: OK`
before and after the members were added; **P1 (`sceneshot --dumped --compare --tolerance 1:1`): "OK 8 shot(s), each
byte-equal to the server's own dump" for all nine**; `sceneshot --camera <case>` OK (one shot from the named camera).
Folder: `F:/c5-64bit-work/scene-library/bundles/` (in the library's `index.json` since 2026-11-20 04:58, 16 bundles; moved from `staging-4980/` by lead-0455, the old index kept as `index-before-4980.json`). [C, 2026-11-20 05:14-05:31, workstation] All nine replay with readers built from MAIN 9b0c1a6b2 (checker `recorder_test` 7d7eb9b4..., `sceneshot` 1ddd6c94...): checker OK 9/9, P1 byte-equal 8/8 for all nine, named camera OK 9/9 (MC #7778), so they need no Extras B build.

| Case (camera) | Bundle id | Frames | Bytes of members |
|---|---|---|---|
| c1_mountain | `C5H-4980-C1MOUNTAIN_0009_20261120-032355_workstation` | 1925..1976 | 367,296,056 |
| c2a_natural | `C5H-4980-C2ANATURAL_0009_20261120-030410_workstation` | 1951..2007 | 385,627,580 |
| c2b_wonder | `C5H-4980-C2BWONDER_0009_20261120-032950_workstation` | 2064..2120 | 375,450,186 |
| c3a_garrison | `C5H-4980-C3AGARRISON_0009_20261120-033534_workstation` | 2081..2132 | 366,974,327 |
| c3b_garrison2 | `C5H-4980-C3BGARRISON2_0009_20261120-034114_workstation` | 2330..2412 | 378,275,959 |
| c4_port | `C5H-4980-C4PORT_0009_20261120-034655_workstation` | 1990..2045 | 399,943,130 |
| c5_air | `C5H-4980-C5AIR_0009_20261120-035243_workstation` | 1996..2044 | 381,141,309 |
| c6_carrier | `C5H-4980-C6CARRIER_0009_20261120-025123_workstation` | 2110..2170 | 394,624,568 |
| c7_intercept | `C5H-4980-C7INTERCEPT_0009_20261120-024541_workstation` | 2415..2474 | 400,231,647 |

- **The extras' inputs in `feed.bin`** [C, `feedkinds.py`, every bundle alike, one save]: 35 kind-1000 events, all the
  load's setup sends (old == new, turn -1): sleep 2 x 21, hold 1 x 12, **intercept 5 x 1** (fighter 1070), mission 6 x 1;
  **2 kind-1001** events (fighters 1068 and 1069 -> transport 1067, owner 0); 9 kind-22 (garrison) events; no kind 30.
  The server counted 37 render-event calls (activity 35, carrier 2) in each run.
- **Each run's report ends `FAILED: the engine is gone: channel N serve() returned PeerDied`** [C, server.log]: the
  harness's safe stop ends the EXE with exit code 15 ("not a crash code; no exe-crash report"), as in
  `combat-on-the-product-path.md` (the harness's safe stop); with this release (LTO) DLL there is no database verdict to
  come first (4932's fast DLL had one). Not a crash.
- **Two takes rejected** (moved to `F:/c5-64bit-work/0455/4980/rejected/`): c1_mountain at 02:57 and c2b_wonder at 03:10,
  whose camera runner never set the camera because `pool.py list` (the runner's claim check) timed out after 60 s while
  the game loaded; recorded again in batch 2. `kit.py camera` no longer runs that check (rec_kit's prep checks the claim).
- **One transient P1 failure**: the first P1 of c3a (04:2x) failed `ReplayFailed` (the units pre-check: `.fxsxml not
  found` for VP's units; decals: `Rice_Random_D.dds` not in the install or the mods' folders) while another worker
  (w4-0042) had just claimed c5h-c and was preparing it; the bundles read the install and mods of c5h-c (`asset_set`,
  `render_decal_mods`). The rerun minutes later was byte-equal. A bundle of the library reads its instance's files at
  replay, so a replay during another ticket's prepare of that instance can fail [L, once].

### Verdicts

- **Case 1 (mountain)** [L, stock shot vc-20261120-023749-workstation-f039 (crop vc-20261120-042138-workstation-ea81) and the c1
  dump crop vc-20261120-042120-workstation-f9c1]: units on mountain plots stand on the slopes in stock and in the
  server alike; the input for X5 (4982) is in the c1 bundle.
- **Case 2 (wonders): verdict.** Natural wonder [L, one shot vc-20261120-023801-workstation-cc17]: the two units next to
  Mount Kailash stand outside its model; no figure inside the model in view. World wonders [L, one shot
  vc-20261120-023815-workstation-4591 and the c2b dump]: the Great Wall's model encloses the city's neighbour plots, so
  the units on those plots stand inside the ring of the wall (not inside the wall itself); Stonehenge's circle touches the
  swordsmen's formation at the west end. So stock puts figures next to and within wonder models, not inside solid parts,
  in this one staging; X0.4's open question is answered for these three wonders and Kailash only.
- **Case 7 (intercept)**: no patrol drawn by stock or the server (above).
- **Case 8 (smoke): verdict.** Stock draws the three levels as `cities-and-landmarks.md` 1.14 expects [L, one shot each]:
  raw damage 5 thin smoke trails, no fire (vc-20261120-023945-workstation-974f); 12 two to three small fires with smoke
  (-023959-...-37d0); 40 three to four fires with smoke (-024015-...-280a), on a city of 282 hit points: at 40 damage
  (14 %) the city already shows the top level, the premise of 4990 (a scale on raw damage).

### Not verified (4980)

- Garrisons (c3a, c3b) and the based aircraft's first-session absence were not masked or explained (above).
- Another player's aircraft on intercept (MC #7602 b): not staged.
- The bundles were recorded with the 32-bit stock arm's batch-4 DLL for staging and the batch-5 x64 DLL with the define
  for the bundles; the staged state was read back after the load on the stock arm only (`verify.lua`); on the product
  path the state is shown by the feed's events, not read by Lua.
- The stock shots carry no RenderDoc capture; `refs/<case>.png` is the stock shot of the first stock session.

## 4983: GarrisonVisible (issue 4983, W3 of team-0455, 2026-11-20) [C: code and unit tests; offline pictures; nothing live]

**Current state (2026-11-23, D243; section 4983.17):** the players' garrison option is `[Extras] PerimeterGarrisons`
(id 32, look (d), default 0, on the installer's visual-additions page); `GarrisonVisible` (look (a)) is a test value
(`Def::testOnly`: not in the default file, not on the page; read, logged and stepping aside while PerimeterGarrisons is in
effect). The text below describes GarrisonVisible as built on 2026-11-20 to 2026-11-23. *(2026-11-24: the D243 branch
had not reached main; re-merged onto main 49919ea97 on `task-4983-d243-remerge`, section 4983.18.)*

Option `[Extras] GarrisonVisible`, id 24 (MC #7491), visual addition, default 0. Branch `w3-4983` (from
`epic-0455-extras-b`). With `ServerUnits = 1`, `EnableVisualAdditions = 1` and `GarrisonVisible = 1` a city's military
garrison is drawn at the city through the units seam (4981); off, the scene is exactly as before (E2).

### 4983.1 Which units [C, code]

`srvextras::IsGarrisonUnit` (`server/src/extras/units/srv_unit_garrison.h`): the unit table's garrisoned flag
(`UnitRecord::flags` bit 1, written from the DLL's `IsGarrisoned` getter at every unit event, kind 22 among them),
**land domain only** (`UnitRecord::domain` 2, DOMAIN_LAND), not embarked, not dead. So:

- a ship that counts as a city's garrison (4980's c4: the trireme, `IsGarrisoned` true) is not shown: ships in a city are
  ShipsMoored's (4984);
- aircraft (domain 1) are never shown by it; a civilian is not a garrison [I: not read in the DLL; 4980's c4 worker and
  c2b settler and general were not the cities' garrisons] and stock already draws a worker, settler or great general standing in a city after a load (4980 masks): no second figure;
- a second military unit in the city that is not the garrison (4980's c3b archer 1061) is left as stock has it: stock
  and the server draw it at the city centre when the DLL makes it the plot's centre unit (4983.7).

Per unit (`ChooseGarrison`, pure): held in a fight (lead-0042's contract) or no plot centre for its plot: untouched;
**walking** (lead-0042 #7810, our #7812): the scene's root (mode exe) farther than `kGarrisonWalkShare` = 0.02 of the hex's
inner radius from its plot centre: shown if hidden, no root, no heading; otherwise shown if hidden and placed by the look.

### 4983.2 Foreign garrisons (E7) [C, the interface's Lua read 2026-11-20; no picture]

The game's own interface shows another player's garrison as its unit flag beside the city banner, and hides that flag
when the plot is not visible to the active team or the unit is invisible to it: base game `UnitFlagManager.lua`
(`GarrisonOtherOffset` for a garrison of another team; `UpdateVisibility`), and the VP EUI copy
(`(3a) VP - EUI Compatibility Files/LUA/UnitFlagManager.lua`: `flag.Anchor:SetHide(not plot:IsVisible(activeTeam, true)
or flag.m_IsInvisibleToActiveTeam)`, and the fog update `fogState ~= 2`). The seam already gives exactly that rule: the
unit host's fog filter (`FilterByFog`, 4463) keeps another team's unit only on a plot whose state is 0 (visible now), and
the seam never shows another team's invisible unit (4981.3). So GarrisonVisible needs no rule of its own for foreign
garrisons; test `garrison_scene` checks both plot states. Without fog states (`render_fow=0`, not the product's setting)
the filter is off and a foreign garrison would be shown anywhere.

### 4983.3 The looks (candidates for the owner)

`GarrisonLook` (the maker passes the default, `SouthEdge`):

| Look | Root | Heading | Why |
|---|---|---|---|
| (a) `SouthEdge` (default, C1's recommendation) | `kGarrisonEdgeShare` 0.6 x inner radius from the plot centre towards the first side on land (inner radius = half the distance of two neighbouring plot centres, 32 units on the nominal 64-unit lattice: 19.2 units) | facing out along that side: yaw = side angle + 90 degrees, so south = 0 (the module's yaw 150 - 60 d degrees with the front along local -y, `unit-formations.md` 11.2) | the default camera looks north, so the south side is in front of the city's buildings: the figures are not hidden by them; facing out of the city; the city centre stays free for a unit stock draws there (a civilian, 4980 c2b and c4; a second military unit, c3b) |
| (b) `Centre` | the scene's own (plot centre) | the unit's own facing | the simplest: the formation where the game would stand it; among the buildings, so partly hidden in the product, and mixed with any unit stock draws at the centre |
| (c) `SouthEdgeCompact` (added 2026-11-20 after round r2) | as (a) at `kGarrisonCompactShare` 0.85 x inner radius (27.2 units) | as (a) | (a)'s formation at stock scale reaches the centre unit's south rows (c3b); this one stands farther out and is drawn at `kGarrisonCompactScale` 0.8 (the seam's scale, about the root, after prepare): clear of the centre, mostly inside the plot |

**The side** (looks a and c): `kGarrisonSideDeg` in this order: south (-90 degrees), south-east (-60), south-west (-120),
east (0), west (180), north-east (60), north-west (120) (world angle from +x, y north). A side is taken when the
footprint is on land: the height field above its water level (`HeightGrid::waterZ`, the module's own floor for figures)
at the root and at the four points `kGarrisonFootShare` 0.5 x inner radius x the look's scale away along x and y
(`GarrisonFootOnLand`). Added after round r3 showed (a) and (c) standing on the beach and in the sea south of c3b's
coastal city. No side on land (a one-plot island): the centre look's placement for that unit (counted `noLandSide`).

### 4983.4 Inputs, E5, E6 [C, unit tests]

- Height field: `beginFrame` takes the frame's field; none this frame: nothing (the scene draws no unit then either). A
  field that gives no plot centre (`c5r::units::plotCentre` fails for (0, 0) or (1, 0)) is an input missing: the extra
  fails `beginFrame`, the module writes `units extra GarrisonVisible: LOUD: failed: beginFrame: the height field gives no
  plot centre: ...: off for the session (the scene draws stock for it)` once (E6), test `garrison_off_and_fail`.
- E5: with `ServerUnits = 0` the module's one line `units extra GarrisonVisible: off: ServerUnits = 0`, nothing drawn.
- The registration: `UnitExtraMakers()` gets `MakeGarrisonVisible(Vp64Opt::kGarrisonVisible)` together with the option
  rows (#7872: after Extras A's milestone 2a is on main); until then the extra is linked but not made.

### 4983.5 Tests [C]

`server/tests/extras/units/test_units_extras.cpp`: `garrison_choice` (the choice for land, sea, air, not garrisoned,
embarked, dead, held, no plot, walking at 0.0219 inner, at rest at 0.01875 inner; the inner radius of a field and its two
failures), `garrison_scene` (through `UnitsCore`: off and a module with the extra off field-for-field equal to no module;
on: only the land garrison shown, opaque, at the edge root, yaw 0, scale 1; the ship, the second unit, the embarked and
the dead not; the worker unchanged; another team's garrison removed by the fog filter on a seen-not-visible plot and kept
on a visible one; held in a fight; a frame without a field; the centre look; the compact look's scale),
`garrison_off_and_fail` (E5, E6, a row that is not an extra), `garrison_cost` (E8 indication, 4983.8); and, with the
option rows, `garrison_registered` (the module's real states and lines through row 24; in the scratch copy until the rows
land).

### 4983.6 The player: sceneshot never attached the unit extras [C]

`sceneshot` makes its unit scene with `srvunits::MakeSceneUnitsWith` (`sceneshot.cpp`, the units block of the replay), not
through server.exe's factory `MakeLive`, so `attachExtras` was never called: a first scratch round (r1, 2026-11-20
07:20-07:30 EST) drew GarrisonVisible on and off **byte for byte equal** (one SHA-256 for the three pictures of c3a). A
first hunk that set `ServerUnits = 1` in the player's options changed nothing for that reason. The agreed hunk (lead-0455
#7917 and Q3, MC #7930) attaches the module right after the unit scene is made, with the units ours (a bundle with
`render_units=1` was recorded with `ServerUnits = 1`): `unitsProbe->attachExtras(srvunits::MakeUnitsExtras(),
SrvOptions(), true);`. Its log lines appear as `render: units extra GarrisonVisible: in effect` and `render: units:
extras: module linked, 1 in effect: attached` (on), `render: units GarrisonVisible: off: ...` and `render: units: extras:
module linked, 0 in effect: dropped (the scene as before)` (off). The build line for `server/src/extras/units/` goes in its
own block after 4938's (lead-0454 #7921). Both land with the option rows (4983 issue log).

**A test that this line touches** (lead-0455's condition (b), grep of `server/tests` and `server/tools` 2026-11-20):
`server/tests/extras/test_visible_outline_scenes.py` `test_off_is_unchanged` asserts `'extras:' not in text` on the
player's output; on a bundle with units on (the huge one), the new `render: units: extras: module linked, 0 in effect:
dropped` line contains `extras:` and would fail it. `test_roads_scenes.py` and the C++ tests (`sceneshot_test.cpp`
`Has`/`Count`/`Last`, `test_extras.cpp`) do not compare whole logs or that substring. Fix proposed to lead-0454 with the
hunk: match the scene extras' own prefix (`'render: extras:'` or `'extras: '` at a line start) instead of any
`extras:`. *(Settled 2026-11-20 by lead-0454, #7973: `'extras: VisibleAreaOutline' not in text and 'LOUD: extras:' not in
text`; with it and the scratch player, `test_off_is_unchanged` and `test_on_draws_and_differs` on TINY and HUGE: 4 passed,
08:06-08:10 EST.)*

**Rule A for the two hunks (MC #7930) [C, 2026-11-20 08:25-08:54 EST, workstation, the bundles' adapter]:** `sceneshot
--dumped --compare --tolerance 1:1 --no-pictures`, every option at its default, on all **16** bundles of the library
(7 of epic 0301, 9 of 4980), with two players from the scratch source: (1) with both hunks and `server/src/extras/units/`
linked (GarrisonVisible registered, off): **16 of 16 "OK 8 shot(s), each byte-equal to the server's own dump"**, each log
with the two unit-extras lines (`units GarrisonVisible: off: ...`, `units: extras: module linked, 0 in effect: dropped`);
(2) with the attach hunk but without the folder (a scratch-only build switch): **16 of 16 byte-equal**, each with the one
line `units: extras: module not linked (the scene as before)`. Script `C:/c5-64bit-work/0455/4983/p1_both.sh` (not in
git), logs `C:/c5-64bit-work/0455/4983/p1/`.

### 4983.7 Pictures (DIAGNOSTIC, D112; offline, scratch player) [L for what the frames show, one camera per case]

Bundles c3a and c3b of 4980 (`F:/c5-64bit-work/scene-library/bundles/C5H-4980-C3*`), named cameras `c3a_garrison`,
`c3b_garrison2`, the scratch player (`C:/c5-64bit-work/0455/4983/ss`, source: `epic-0454-extras-a` @7e513c57e + this
branch's extra + the rows + the two player hunks; the look picked by a scratch-only environment variable in the maker,
never committed). Pictures in `F:/c5-64bit-work/0455/4983/r2/`, `r3/`.

- **The server's offline picture has no city buildings**: the bundles were recorded with `cities 0, landmarks 0` (the
  EXE draws the cities; the composite puts them together in the product). So an offline picture cannot show whether the
  city's buildings hide a garrison. A **mock composite** (`mock.py`: the pixels where the on-picture differs from the
  off-picture, pasted over the bundle's stock picture `refs/<case>.png` of the same named camera; no depth, so nothing
  hides a figure) shows where the figures stand relative to the buildings; it is not what the product draws.
- **c3a** (G1 Tonga, spearman 1059 its only unit): off = stock, no figure in the city. The edge look puts the spearmen in
  front (south) of the city on its grass, facing the camera; the centre look stands them on the east part of the building
  cluster (the stock city's buildings spread over the plot's west half here). The **other garrison in view** (c3b's
  city G2 at the picture's top, own) is drawn by every look as well, as it must be.
- **c3b** (G2 Nuku Hiva, swordsman 1060 garrisoned, archer 1061 in the city): **stock and the server already draw a
  formation of archers at the city centre** (the stock shot `refs/c3b_garrison2.png` and the server's off-picture, the
  same figures at the same spots) [L; that it is archer 1061 is by elimination, the only other unit in the city, I]: the
  DLL's `updateCenterUnit` makes the non-garrisoned unit the plot's centre unit and shows it (4981.2's row 4).
  *This corrects 4980's c3b reading ("no figure of the two units noticed").* So a garrison drawn at the centre (look b)
  stands in the same spot as that unit: two formations mixed. The edge look (a) overlaps the archers' south rows; the
  compact look (c) is meant to clear them.
- With every look the garrison is drawn opaque at once (the seam's show); the shadow under it is not drawn (the EXE's
  mask pass has no unit there, P2). *(Corrected 2026-11-22 by 4983.11: true only for bundles recorded without the
  server's unit shadows (format 9 and older). Since 4749 the server casts its own units' shadows (`render_units_shadow=1`,
  the default), and a garrison it shows casts one like any unit it draws.)*

**Round r4 (2026-11-20 08:12-08:22 EST; the land test in; the candidate set)** [L, offline]: for each case `--dumped
--compare` with the extra off is P1 byte-equal (8 of 8 frames), with it on exit 10 with every one of the 8 frames
differing by a formation's worth of pixels (c3a 18,636-18,669, c3b 15,199-15,337 of 3,603,694 in the frames read: the
same figures in every frame, so not a one-frame result, MC #7615). Pictures at the named cameras, differing pixels
against off (more than 8 in a channel): c3a (a) 18,368, (b) 19,105, (c) 11,867; c3b (a) 14,004, (b) 11,028, (c) 8,190.

| Case | Off (P1) | (a) edge | (b) centre | (c) compact edge |
|---|---|---|---|---|
| c3a | vc-20261120-082420-workstation-b52e | vc-20261120-082421-workstation-311d | vc-20261120-082422-workstation-8912 | vc-20261120-082429-workstation-f140 |
| c3b | vc-20261120-082431-workstation-4493 | vc-20261120-082434-workstation-cbc5 | vc-20261120-082436-workstation-974e | vc-20261120-082437-workstation-c7e5 |

Each record carries the stock shot as reference and a comparison sheet (`F:/c5-64bit-work/0455/4983/r4/<case>_sheet.png`:
off and the three looks, server pictures; `<case>_mock.png`: the stock shot and the three mock composites).

- c3a: the south side is land: (a) and (c) stand in front of the city facing the camera, (c) smaller and a little
  farther out; (b) on the east part of the building cluster.
- c3b: the south and south-east sides are sea, so (a) and (c) take the **south-west** side (on land, facing south-west);
  (a) still reaches into the archers drawn at the centre, (c) is clear of them but for its first row; (b) mixes the
  swordsmen and the archers in one crowd.
- **The worker's recommendation for the owner: (c)**, the compact edge: it never mixes with a unit stock draws at the centre,
  keeps the formation mostly inside the plot, stands on land, and faces out of the city; (a) is C1's recommendation and
  the code's default until the owner picks (the maker's one argument). What the city's buildings hide in the product is not
  seen offline (no cities in the bundles): a live picture pair per look is the lead's later step.

### 4983.8 Cost (E8) [indication only, PERFORMANCE.md; no lock held, nothing live]

- CPU: `garrison_cost` (UnitsCore::build of a 2000-unit table with 200 land garrisons, 201 builds per arm, alternated,
  clang-cl /O2, the workstation with other teams' work running): median without a module 1,502.9 / 735.6 / 1,267.7 us,
  with GarrisonVisible on 1,714.6 / 848.8 / 1,487.7 us in three runs (2026-11-20 between 07:05 and 08:15 EST, one per scratch rebuild). The on arm
  also lists 200 more units (the garrisons: 2000 against 1800), so the difference (+14 % to +17 % of the build) is the
  extra's choice plus the scene's work for the added units; the extra's own work per garrison is one plot centre and five
  height samples, per frame two plot centres. A single garrison per city on a real map is a few dozen units.
- GPU: each shown garrison is one more unit drawn, the same cost as any unit of the scene (the seam's scale and turn are
  one 3x4 product per bone row triple, 4981.4). The offline players' times (`sceneshot.json` `shot_ms` 4.1-4.7 s, the
  residency's settle 3.4-3.9 s of it) are workflow numbers and show no difference at that resolution.
- Memory: the extra holds a pointer, a float and its counters (under 100 bytes); no allocation per frame or unit.
- Not measured: frame time and memory on the product path with the flag on and off (a live arm: the lead's step).

### 4983.9 The scene test (E9) [C, file; run once with the scratch player]

`server/tests/extras/units/test_garrison_scenes.py` (pytest; `C5_SCENESHOT_EXE` = a player with the two hunks,
`C5_SCENE_LIBRARY`): on c3a and c3b, off is P1 with the module linked and dropped and GarrisonVisible's off line; on
starts the extra, no LOUD line, every frame differs (exit 10); the named camera's on-picture differs from the off-picture
in 2,000-60,000 pixels (one formation; r4 measured 8,190-19,105). Pictures are not committed. Run 2026-11-20
08:55-09:02 EST with the scratch player (look (a)): **6 passed** (404 s).

### 4983.10 Not verified

- Nothing live: the product path (the composite with the EXE's cities over or under the figures), the frame time and
  memory there, and the EXE's unit flag beside the figures are the lead's live step (START-REQUEST).
- A walking garrison (entering or leaving a city) and a fight at a city: unit-tested only (`garrison_choice`,
  `garrison_scene`); no bundle holds one.
- Foreign garrisons in a picture: the kit has the active player's cities only (E7 rests on the code and the Lua read).
- A one-plot island city (no side on land): unit-tested only.

### 4983.11 Round r5 on the format-10 kit bundles: the garrison casts the server's unit shadow (impl-4983, 2026-11-22)

Branch `task-4983-garrison-visible` from main 56ed455bb, account B. The kit bundles c3a and c3b were recorded again on
2026-11-22 at format 10 with the server's unit shadows (4749 W66, `offline-scene-harness.md` 41:
`C5H-4980-C3AGARRISON_0009_20261122-185217_workstation`, `C5H-4980-C3BGARRISON2_0009_20261122-185851_workstation`), so
4983.7's pictures (format 9, no unit shadows) were out of date. Driver `server/evidence/0455/4983/off_4983.sh` (build,
e9, p1, r5); players built from this branch: the committed one (`ss`, sceneshot SHA-256 7781db9c...) and a look player
(`ss-look`, 4693728e...) from a scratch `git archive` copy changed by `look_scratch.py` (the maker reads
`T4983_GARRISON_LOOK` = 0, 1, 2; never in the product). Library: the checked C: copy (`scenelib.py check` OK, 16 bundles),
asset root `C:/c5-64bit-work/scene-asset-root/mods548-vt`. Workstation busy with other teams' builds, suites and a game.

- **E2 / P1 [C, 21:12-21:20 EST]:** the committed player, `--dumped --compare --tolerance 1:1 --no-pictures`: c3a and c3b
  with every option at its default **8 of 8 frames byte-equal** each (colour and depth 0 of 3,603,694), and with
  `EnableVisualAdditions=1 GarrisonVisible=0` 8 of 8 byte-equal each. The attach line is now `units: extras: module
  linked, 1 in effect: attached` with GarrisonVisible off: the 1 is FixAirBases, default 1 since one-off 9081 (D235).
- **The E9 scene test was red on main [C, 21:00-21:12]:** `test_garrison_scenes.py` 2 passed, 4 failed, because it
  asserted `module linked, 0 in effect: dropped` (off) and `1 in effect: attached` (on); every picture and comparison
  held. 9081 updated the C++ unit tests for FixAirBases' default, not the two Python scene tests. Fixed here in
  `test_garrison_scenes.py` and `test_ships_moored_scenes.py` (same assertion): the module linked, any count, and the
  extra's own lines. Rerun with the committed player (21:32-21:43 EST, through `quietlock.py run`): **6 passed**
  (649 s). The ships test, changed the same way: **5 passed** (21:53-21:58 EST, 309 s, same player and settings).
- **Round r5 [L for what the frames show, 21:20-21:30 EST]:** per case the named camera off and with each look
  (`--option EnableVisualAdditions=1 --option GarrisonVisible=0|1`). Pixels differing from off (more than 8 in a
  channel): c3a (a) 26,653, (b) 25,306, (c) 17,040; c3b (a) 16,700, (b) 14,295, (c) 10,734 (r4 on format 9: 18,368 /
  19,105 / 11,867 and 14,004 / 11,028 / 8,190; the rise is the shadows below). The placements are r4's: c3a (a) and (c)
  south of Tonga facing the camera, (b) at the centre; c3b (a) and (c) on the south-west side, (a) into the archers'
  south rows, (c) touching them with its first row, (b) mixed with them. Visual Checks (DIAGNOSTIC, the bundle's stock
  shot as reference, the case's sheet as comparison): c3a off / a / b / c vc-20261122-213216-workstation-b14d,
  -213217-...-02c6, -213218-...-5a58, -213219-...-0bbf; c3b -213220-...-1398, -213221-...-d1c0, -213222-...-33af,
  -213223-...-1f8c. Files and sheets (`r5_sheet.py`) in `F:/c5-64bit-work/4983/r5/`.
- **A shown garrison now casts a shadow [C, `shadowcheck.py`, 2026-11-22]:** off against on at the same camera, the pixels
  whose depth is equal (no figure there) but whose colour differs: c3a (a) 7,962, (b) 6,153, (c) 4,787 pixels, **every
  one darker** (mean luminance -29.8, -24.8, -29.3); c3b (a) 3,356 (3,355 darker), (b) 3,755 (3,715), (c) 2,237 (all),
  mean -27.0, -28.9, -28.0; all inside the figures' area. So the server draws the shown garrison into its unit shadow map
  like any unit it draws (`render: units mode: the server's unit shadows are ready`), and C1's P2 ("added units get no EXE
  shadow") no longer leaves them without one where the server's unit shadows are on (the product default since 4749).
  Not seen live yet (4983.12).
- **Unit tests and the E8 CPU indication on main's code [C, 22:00-22:01 EST]:** `build_units_extras_tests.py` (this
  branch, c5render of the `ss` build), `test_units_extras.exe` **13 of 13**; `garrison_cost` (as 4983.8, no lock, the
  workstation busy): `UnitsCore::build` of 2,000 units with 200 land garrisons, median without a module 1,278.3 us (p10
  796.9, p90 1,632.5), with GarrisonVisible on 1,485.1 us (p10 891.6, p90 1,840.9), 201 builds per arm alternated: +16 %,
  inside 4983.8's +14 % to +17 % (the on arm also lists the 200 garrisons). An indication, not a claim (PERFORMANCE.md).
- **The recommendation stays (c)** for the reason of 4983.7 (it keeps clear of a unit stock draws at the centre and stays
  mostly inside the plot), with the note that at c3b its first row still touches the archers' south rows; (a) is the
  code's default until the owner picks a look.

### 4983.12 The live run: not done; the exact steps for the next implementer (written 2026-11-22 22:02 EST)

Not run on 2026-11-22: no 26577625 instance was free (MC-B #10785). What it is for: the three looks' pictures on the
product path for the owner's pick (with the EXE's cities and the interface over our figures, which offline cannot show:
the bundles record `cities 0`), the shadow of 4983.11 seen live, E1 (statscmp on/off from one save) and E8's live
indications (server private bytes per turn, seconds per turn). Everything below is in `server/evidence/0455/4983/`
(`smoke_4983.sh`, `arm_4983.py`, `off_4983.sh`, `look_scratch.py`); the driver's header lists every mode.

**Kept for it (do not delete before the run):** the layout `F:/c5-64bit-work/4983/live/layout` (server.exe SHA-256
583937d15d514adf..., built from the scratch look copy of this branch with `look_scratch.py`; x64 DLL b84aa3e90f51bbf5...
(0.1b pin); proxy a3b550144ec3618e...; composite 61343418a2a2e399...; `SHA256.txt`, `HEAD.txt`), the shell build
`F:/c5-64bit-work/4983/live/shell-build` (its `shell_composite.ini`), the scratch copy `C:/c5-64bit-work/4983/src-look`
(0.3 GB; only to rebuild). The kit save `F:/c5-64bit-work/0455/4980/saves/C5H-4980-KIT_0009.Civ5Save` (74fc7428...).
If the layout is gone: `bash server/evidence/0455/4983/off_4983.sh build` (makes `src-look`), then `smoke_4983.sh build`
(about 6 min: server 236 s, composite; no LTO link). Work folder `C5_SMOKE_WORK` must be NEW for each prep.

**Steps** (Git Bash, worktree root of a branch carrying this one, MC-B's grant and the claim first; owner `impl-4983` or
set `C5_SMOKE_OWNER`; the harness binaries built, runbook 0):
```
python -I tools/harness/pool.py claim --owner impl-4983 --name <inst> --ticket 4983 --hours 2 --address <queue address> --note "4983 live"
export C5_SMOKE_WORK=C:/c5-64bit-work/4983/smoke2          # new folder per prep
bash server/evidence/0455/4983/smoke_4983.sh dry           # rc 0
bash server/evidence/0455/4983/smoke_4983.sh check <inst>  # EXE build 26577625
bash server/evidence/0455/4983/smoke_4983.sh prep <inst>   # lay.py prep + helper on, XML backup + SQLITE_LOGGING 1, kit copy
bash server/evidence/0455/4983/smoke_4983.sh arm <inst> off   # then a, b, c (one launch each, about 4 min)
bash server/evidence/0455/4983/smoke_4983.sh e1 <inst> e1on   # acceptance.py autoplay T9 -> T19, about 5 min
bash server/evidence/0455/4983/smoke_4983.sh e1 <inst> e1off
bash server/evidence/0455/4983/smoke_4983.sh statscmp      # IDENTICAL is the pass (E1)
bash server/evidence/0455/4983/smoke_4983.sh end <inst>    # restore by hash, XML back, provision verify
python -I tools/harness/pool.py release <inst> --owner impl-4983
```
Each arm records 4 DIAGNOSTIC shots (Tonga c3a and Nuku Hiva c3b, interface hidden, then shown) into Visual Checks
and writes `lines.txt` (attach and in-effect lines, LOUD count, last `units 1s` line) under `F:/c5-64bit-work/4983/out1/<arm>/`.
Look b and c come from the launch's `--env T4983_GARRISON_LOOK=1|2` (the EXE's environment reaches server.exe: the
proxy's `CreateProcessA` passes NULL, so the child inherits it [C, `proxy/src/proxy_rt.cpp`]); look a is the code's
default. What to read: per arm `units extra GarrisonVisible: in effect` (a, b, c) or its off line, 0 LOUD, no box; the
listed count in `units 1s` higher by the kit's garrisons than off (the 0455 landing smoke: 34 -> 40); the e1 arms'
`acc/per-turn.csv` for server private bytes. A box: the driver exits 8 (runbook 3.8). The driver was checked by `dry`
and `bash -n` only; its prep, arm and e1 modes have not run yet [I]. *(Run 2026-11-22/23: 4983.13.)*

### 4983.13 The live run (impl-4983, 2026-11-22 23:40 - 2026-11-23 00:19 EST, c5h-new-b, MC-B's grant)

Instance c5h-new-b (C:, Goldberg, EXE build 26577625 checked by `smoke_4983.sh check`), claimed by impl-4983 under
MC-B's grant to 01:00; the layout of 4983.12 (server.exe 583937d1..., the scratch look copy), laid by `lay.py prep`
(backup by SHA-256) and `helper on`; CP's `NewCustomModOptions.xml` backed up by hash, `SQLITE_LOGGING` 0 -> 1; the kit
copy `C5H-4983-KIT_0009` (74fc7428...). Options per arm: `[Renderer] ServerUnits = 1`, `[Extras] EnableVisualAdditions
= 1`, `GarrisonVisible = 0|1`; looks b and c by `cli.py launch --env T4983_GARRISON_LOOK=1|2`. FixAirBases at its
default 1 in every arm. Pictures: the harness's PrintWindow capture, DIAGNOSTIC (D112: a test layout laid by lay.py and a
scratch server build; not the installer's), popups closed (D114), cameras by plot (Tonga (48,17), Nuku Hiva (49,9), zoom
min-2), first with the interface hidden by Lua, then shown.

**A driver fault on the first arm [L].** The off arm's in-game wait ended "not in game" (exit 3) at 23:48 although the
game had loaded: the scenario runner's claim check runs `pool.py list` with a 60 s timeout, and `pool.py list` took
**3 min 10 s** at 23:51 (user time 0.2 s: it waits on the machine's process list for every instance, with 0630's take
and other teams' games running). The game was left running; `arm_4983.py` and `smoke_4983.sh` now read the claim file
itself (`pool.read_claim`, `is_expired`), and the off arm was finished by hand with the same commands (shots, lines,
safe stop rc 0, front end restored, crash collect, lay collect). Arms a, b, c then ran through the driver.

**The four picture arms [L, 23:44-00:09].** Every arm: no box (marker after every step), 0 LOUD lines in render.log,
server.log and vpproxy.log, the safe stop rc 0, nothing from `crash.py collect`.

| Arm | Extra's lines (render.log) | Last `units 1s` | Visual Checks: hidden c3a / c3b; interface shown c3a / c3b |
|---|---|---|---|
| off | `units GarrisonVisible: off: GarrisonVisible = 0 (from the file)`; `module linked, 1 in effect: attached` (FixAirBases) | table 44, listed 35, refused 0 | vc-20261122-235256-workstation-dd2a / -235308-...-2971; -235324-...-a688 / -235336-...-d72d |
| a | `units extra GarrisonVisible: in effect`; `2 in effect: attached` | table 44, **listed 43**, refused 0 | vc-20261122-235815-workstation-fb14 / -235832-...-50d6; -235906-...-44d4 / -235919-...-3eb6 |
| b | as a | table 44, listed 43, refused 0 | vc-20261123-000343-workstation-a3b6 / -000356-...-7561; -000411-...-4634 / -000424-...-61f3 |
| c | as a | table 44, listed 43, refused 0 | vc-20261123-000807-workstation-263a / -000820-...-8ec0; -000835-...-8b75 / -000847-...-5350 |

Eight more units listed with the extra on (the kit's land garrisons in the table; which ones not checked by picture [I]).
The server's unit shadows were merged on every flagged frame in arm a (`unitshadow 1s: ready=1 ... merged_total=3702
flag_frames=3702 not_merged=0 mask_fails=0`), so the shown garrisons are in the shadow map live as offline (4983.11);
a pixel check of the shadow live was not made (the camera differs between launches by tens of pixels) [I].

What the live pictures show (sheets `F:/c5-64bit-work/4983/out1/live_c3a_sheet.png`, `live_c3b_sheet.png`, crops of
the hidden-interface shots; `live_sheet.py`; Visual Checks vc-20261123-002111-workstation-02f7 and
-002112-...-4464) [L, by eye]:
- **c3a Tonga** (one spearman garrison, the buildings on the plot's west half): (a) the formation stands on the plot's
  south-east grass in front of the buildings, its west rows over the eastern houses; (b) among the buildings at the
  centre, partly over them; (c) smaller and farther south, the city's houses drawn over its back row where they are in
  front (depth holds between the EXE's city and our figures).
- **c3b Nuku Hiva** (coastal, swordsman garrison and the archer stock draws at the centre, the buildings fill the plot):
  every look crowds: (a) the swordsmen in among the archers and houses on the south-west; (b) one mixed crowd with the
  archers; (c) the clearest of the three, still touching the archers' south rows and the houses.
- With the interface shown, the city banner and the unit flags sit above the figures; the cursor's plot tooltip (the
  harness leaves the cursor at the window's centre after the zoom) covers part of the top right in the off arm's shot
  and is not a popup the popup queue closes.
- The figures are drawn opaque from the first frame after the load; no flicker was looked for.

**E1 PASS [L, 00:09-00:18].** `smoke_4983.sh e1`: `perf/profilekit/acceptance.py run --config inc2 --no-ledger`, the kit
copy `C5H-4983-E1K_0009` autoplayed turn 9 -> 19, GarrisonVisible 1 (look a; 00:09-00:13, 260 s) then 0 (00:13-00:18,
261 s), both windowed on c5h-new-b (D164: like with like). `tools/d10/statscmp.py e1on/stats.db e1off/stats.db
--from-turn 9 --expect-turns 9 19`: **IDENTICAL, 15 tables, 2,509 rows**, every per-turn table with turns 10..19 on both
sides (`F:/c5-64bit-work/4983/out1/statscmp.txt`). Per-turn pictures at Tonga in `F:/c5-64bit-work/4983/out1/e1*/acc/shots/`.

**E8 live indications [L, one run per arm, no lock, workstation busy: not a claim].** From the acceptance runs'
`per-turn.csv` (the Lua channel sampled turns 9, 11 and 19; Tiny autoplay is about 2-3 s per turn): server.exe private
bytes on 1,087.6 / 1,108.4 / 1,123.0 MB, off 1,058.6 / 1,078.0 / 1,095.6 MB (+27 to +30 MB with the extra on: the
garrisons' figures and their clip state, within one-run noise not established); the EXE's private bytes the same
(1,318-1,356 against 1,321-1,349 MB); the ten turns took 27.6 s on and 29.4 s off (elapsed from the first sample). The
off arm's `units 1s` line has `frame_ms_max=3.3` (not compared per arm).

**Restore [L, 00:18-00:19].** `smoke_4983.sh end`: safe stop (nothing running), front end restored, the XML back by
hash (rc 0), `lay.py restore` "restore ok", saves back as snapped (rc 0), `provision.py verify --quick` rc 0; c5h-new-b
released 00:19 (queue note #10952). Left behind on purpose: the instance's `cache/stats.db` (a run output, written by
every arm once `SQLITE_LOGGING` was 1; the E1 driver moved the earlier one aside into `out1/e1on/stats-before/`).

Recommendation for the owner's pick, unchanged: **(c)**; Nuku Hiva shows that in a city whose buildings fill the plot
and where stock already draws a second unit, no look stays clear.

### 4983.14 Look (d), the perimeter (stage d, impl-4983, 2026-11-23)

The owner after the live sheets of 4983.13 (2026-11-23 00:3x): none of a/b/c is an improvement on the default; asked for
the figures [reworded for privacy] to stand as a ring along the edge of every non-water tile. Branch `task-4983-look-d` from main 9e0dd7109. Files by
MC #10989 (#10985).

**Why the seam needed more [C, code].** The seam (4981) moves a unit only as a whole: one root, a heading and a scale
(`UnitOverride`, applied rigidly after prepare in `ApplyUnitXforms`). A perimeter places each figure on its own spot.
The existing per-figure route is a placement source: 4776's `FightPlacement` moves figures after the module's own
placement with `c5r::units::applyFigureOffsets` (slot-relative offsets in the unit's frame, height sampled again at the
new spot, the member's ZOffset kept). Look (d) uses the same route.

**The design.**
- `render/src/extras/units/garrison_perimeter.{h,cpp}` (c5render, namespace `c5r::extras::units`): the plot lattice from
  the field (`latticeOf`: e = centre(1, 0) - centre(0, 0), a = centre(0, 1) - centre(0, 0); in world units the hexes are
  regular: e = (64, 0), a = (32, 64 x 0.866)); the six neighbours C + e, C + a, C + a - e, C - e, C - a, C + e - a, which
  are edges 0..5 (E, NE, NW, W, SW, SE); `landEdges` (bit k when neighbour k's centre is on the field and above the water
  level); `perimeterSpots` (a hex vertex is the centroid of the three plot centres meeting there; the ring is the plot's
  hexagon scaled by `kPerimeterShare` 0.85 about C; n spots spread evenly along the land edges' total length, each at the
  middle of its share; yaw = the outward angle of its edge + pi/2, the placement's convention); and `GarrisonPlacement`,
  a `PlacementSource` over another: a unit named for the frame is placed by the inner source, then every living figure
  moved to its spot by `applyFigureOffsets`; any failure leaves the inner placement (counted). The figure count is the
  unit's own (the living figures are spread, none added).
- `server/src/extras/units/srv_unit_garrison.{h,cpp}`: `GarrisonLook::Perimeter` (3). A placed garrison gets show, no
  root or heading (the plot centre), and `UnitOverride::perimeterEdges` = its plot's land mask; mask 0 (a one-plot
  island) leaves the centre placement (`noLandSide`). `wantsPlacement()` is true for this look only.
- `server/src/extras/units/srv_units_extras.{h,cpp}`: the module owns one `GarrisonPlacement`; `beginFrame` clears its
  list, `unit()` names each unit whose final override carries edges (never a unit held in a fight); `perimeterEdges`
  above bit 5 is an out-of-range value (E6); `wantsPlacement()` is any running extra's; `placementOver(inner)` returns the
  decorator over `inner`; the `units 1s` line gains ` | perimeter spread=.. failed=..` only while an extra uses it.
- Seam (`server/src/units/srv_units_scene.h`): `UnitOverride::perimeterEdges` (default 0) and two virtuals of
  `UnitsExtras` with defaults (`wantsPlacement` false, `placementOver` null). Host (`srv_units_scene.cpp` `openDraw`):
  when the attached module wants placement, the renderer's source is the decorator over the controller's fight placement
  (or over the module's own placement, made there, when the controller is off); otherwise the line is as before. An
  `attachExtras` after the renderer opened resets it so the next draw re-opens with the chain. `srv_units_core.cpp`
  needed no change (the module names the units itself).
- With no module, a module whose extras do not want placement, or no unit named in a frame, the placement is exactly the
  inner source's: rule A and the off and a/b/c arms are unchanged by construction [C]; checked by the P1 run below.
- Not in the product's choice: the look is the maker's default argument (still `SouthEdge`); (d) is drawn only by the
  scratch switch `T4983_GARRISON_LOOK=3` of `look_scratch.py` until the owner picks.

**Tests [C, 2026-11-23].** `render/tests/extras/units/garrison_perimeter_test.cpp` (`c5r_extras_units_garrison_perimeter`,
7 cases): the lattice and the six neighbours on even and odd rows against `plotCentre`; the land mask with two water
neighbours and with the water level above the land; six figures on six edges (each at the middle of its edge at 0.85 of
the way, facing its neighbour, inside the plot; at share 1 farther out); twelve figures on two edges (six each, none on a
water edge, equally spaced), three and one on one edge; the empty cases (no land edge, n 0, bits above 5) and the errors
(share 0 or above 1, a NaN, a degenerate lattice); the decorator (nothing set: exactly the inner placement; set: each
figure on its spot, facing out, z kept on flat land, the unit's frame unchanged; another unit untouched; mask 0 and
`clear()`), and its failures (no field, a plot outside the field, the inner source's failure passed through, no inner
source). `render/build.py --test -R garrison_perimeter`: 1 of 1 passed (first green run before 00:56 EST). The server-side cases (the extra's
mask, the module's list and counts) belong in `server/tests/extras/units/test_units_extras.cpp`, held by 4940 at this
stage: they are covered here by the offline pictures and the counts below, and wait for that file.

**Builds and offline pictures [C/L, 2026-11-23 00:56-01:36 EST, workstation, `server/evidence/0455/4983/d_4983.sh`].**
`build` (branch bd68b0df2): c5render and the perimeter test (passed), the committed sceneshot `--smoke` (8a3b9c9f...),
the look player from a scratch archive with `look_scratch.py` (1ec2a2f2...; `T4983_GARRISON_LOOK` 0..3), and
`test_units_extras.exe` **13 of 13** (the seam suite unchanged with the new fields). `sheets` at c3a and c3b with the look
player: **off, (a), (b), (c) byte-equal to round r5's pictures** (SHA-256 of all eight PNGs equal to
`F:/c5-64bit-work/4983/r5/`), so the change leaves every earlier arm as it was; (d) at Tonga: the twelve spearmen in a ring
on the plot's north, east and south edges, the west (the lake side) empty, every figure facing out (backs to the camera
on the north edge, faces on the south edge), each with its shadow; at Nuku Hiva the swordsmen on the west, north-west and
south-west edges only (the east edges face the sea), the archers stock draws at the centre left inside the ring. Pixels
differing from off (any channel above 8): c3a (a) 26,653 (b) 25,306 (c) 17,040 **(d) 23,728**; c3b 16,700 / 14,295 /
10,734 / **15,078**. Sheets (`d_sheet.py`) in `F:/c5-64bit-work/4983/d/`.

**No inland garrisoned city offline [L].** `d_4983.sh scan`: off against (d) at every named camera of the nine kit
bundles: only Tonga and Nuku Hiva change (c1_mountain, c2b_wonder, c3a, c3b, c4_port, c5_air, c7_intercept all show one
of the two; c2a_natural and c6_carrier none). The inland cities in view (A (45,13) and its neighbours in c5_air) have no
garrison in the kit. The 0301 bundles (`frames_4983.sh`, first frame): TINY none; HUGE (turn 350, another save) one city
with a garrison spread by (d). So the inland case is drawn live on the kit save with a garrison made by Lua (4983.15).

### 4983.15 Look (d) live, with an inland city (impl-4983, 2026-11-23 01:47-02:17 EST, c5h-new-b, MC #11037, MC-B #11043)

Instance c5h-new-b (C:, Goldberg, EXE build 26577625 checked), claimed 01:47, released 02:17 (queue note #11100). Layout:
server.exe built 01:39-01:43 from a scratch archive of af52b8851 with `look_scratch.py` (release, 0.1b pins, no LTO; tree
`F:/c5-64bit-work/4983/live-d`), laid by `lay.py prep` (backup by SHA-256) and `helper on`; CP's XML not touched this time
(`C5_4983_NO_STATS=1`, no E1 arm). Driver `smoke_4983.sh` with `C5_4983_INLAND=1`: after the load, `arm_4983.py garrison`
makes a spearman by Lua in the inland city at (45,13) on the kit copy (answer every arm: `OK spearman 1071 in Aotearoa
garrisoned=true city garrison=1071`; the 4985 notes call this city "A"), then shots at Tonga, Nuku Hiva and Aotearoa,
interface hidden, then shown. Five arms, one launch each (01:49-02:16), each exit 0, no box, the safe stop rc 0.

| Arm | Extra's lines | Last `units 1s` | Visual Checks: hidden c3a / c3b / inland; shown c3a / c3b / inland |
|---|---|---|---|
| off | `units GarrisonVisible: off: GarrisonVisible = 0 (from the file)` | table 45, listed 35 | vc-20261123-015403-workstation-69b2 / -015418-...-ae39 / -015431-...-1fbb; -015445-...-9dfe / -015457-...-8814 / -015509-...-6c24 |
| a | `in effect`; `2 in effect: attached` | table 45, listed 44 | -015829-...-9ac0 / -015842-...-3a91 / -015903-...-c513; -015919-...-24d1 / -015936-...-c6b7 / -015950-...-bf4f |
| b | as a | table 45, listed 44 | -020439-...-7b87 / -020455-...-f10d / -020511-...-07a5; -020528-...-9d7a / -020542-...-7635 / -020558-...-3170 |
| c | as a | table 45, listed 44 | -020933-...-73c0 / -020946-...-68b1 / -020957-...-8413; -021013-...-ef2c / -021025-...-c954 / -021038-...-fb74 |
| d | as a; **`perimeter spread=9102 failed=0`** | table 45, listed 44 | -021421-...-8735 / -021435-...-9bae / -021448-...-1def; -021503-...-f701 / -021516-...-53d4 / -021528-...-6b50 |

0 LOUD lines in render.log and server.log of every arm. The sheets (crops of the hidden-interface shots, `live_sheet.py`):
`F:/c5-64bit-work/4983/out-d/live_{c3a,c3b,inland}_sheet.png`, Visual Checks vc-20261123-021941-workstation-3c38,
-021949-...-0ea4, -021952-...-4fd7 [L, by eye]:
- **Aotearoa (inland)**: (d) puts the twelve spearmen in a full ring around the city's buildings and the based aircraft,
  facing out; (a) and (c) a block south of the buildings, (b) mixed into them.
- **Tonga**: (d) a ring on the north, east and south sides, none on the west (the lake); the buildings stand west of the
  plot centre, so the ring's west part, where it exists, would cross them.
- **Nuku Hiva**: (d) the swordsmen on the west side only (the east sides face the sea), the archers stock draws inside.
- Seen, not this extra's [L, cause not examined]: in arm b's inland shot the wheat field north-east of Aotearoa is not
  drawn while it is in the other four arms' shots (the same save and camera).

Not verified: the shadow of the ring live pixel by pixel (the server's unit shadows were on in every arm, as 4983.13); a
walking garrison or a fight with (d) on; the product build (the look is the scratch switch's).

### 4983.16 Every builder of the changed sources, and rule A (impl-4983, 2026-11-23, branch b73697ca2 = main merged)

`server/evidence/0455/4983/allb_4983.sh 1|2|3` (runbook 2.10's U6 plus the effects and units runners, the whole render
ctest and run_all's unit-carrying suites), each step through `quietlock.py run --who impl-4983`, `C5R_GAME_DIR` /
`C5R_EXE` / `C5_GAME_DIR` = the 4390913 install outside the pool, the workstation busy with other teams' builds and games
[C, logs in `C:/c5-64bit-work/4983/allb/`]:
- Phase 1 (to 02:41): `render/build.py --test` **109 of 109 ctest passed, 0 skipped** (with
  `c5r_extras_units_garrison_perimeter`); `server/build_server.py --config release` on the 0.1b pins: OK in 222 s.
- Phase 2 (02:41-03:00): `build_units_tests.py` OK and `test_units_runner.py` **7 passed** (213 s);
  `build_units_extras_tests.py` OK, `test_units_extras.exe` **13 of 13**; `build_unitfeed.py` OK; `build_trigger_scene.py`
  OK; `run_effect_routes_test.py`, `run_trigger_emitter_test.py`, `run_units_{fights,queue,records,seed}_test.py` each
  "OK (0 failing)"; `farmfields/test_farmfields_build.py` OK; `shadow/build_unit_shadow.py` OK. Every step exit 0.
- Phase 3 (03:00-04:01), `run_all.py --only render-host --only scenebundle-programs --only scenebundle-py --only
  offline-equals-live` (`C5_SCENE_ASSET_ROOT` the shared root, passed only to the last): builds c5render, rt, sb, sceneshot
  OK; **render-host 189 passed**, **scenebundle-programs 4 passed**, **scenebundle-py 23 passed**, **offline-equals-live
  17 passed** (rule A on every library bundle with this branch's player: the defaults draw exactly what the server
  dumped), 0 failed, 0 skipped: `run_all: OK: 4 suite(s) passed (plain)`.

### 4983.17 PerimeterGarrisons, the players' option (D243; impl-4983, 2026-11-23 15:45-, branch `task-4983-perimeter-garrisons-option`)

The owner (D243, 2026-11-23 12:1x): look (d) is its own visual addition "Perimeter Garrisons", default off, offered by the
installer's visual-additions page; looks (a)-(c) stay test values. Files by MC #11877 and #11915.

**The option [C, code and unit tests].** `[Extras] PerimeterGarrisons`, id 32 (MC #11877; the next free id is 33),
`kCatVisual`, no needsKey, 0 to 1, default 0, title "Perimeter Garrisons", help "... Needs ServerUnits = 1." The
installer's page is made from the table (`server.exe --write-options-schema`, one-off 9081), so the row sits under
`EnableVisualAdditions` with no installer code of its own; `/P64OPT=extras.perimetergarrisons=1` sets it silently.
- **GarrisonVisible becomes a test value**: a new last field `Def::testOnly` (1 only on row 24). `DefaultFileText` and
  `srv_write_options.cpp`'s schema skip such a row, so it is in neither the default file nor the installer's page; it is
  still parsed (no warning), logged (`DescribeExtra`) and documented (`docs/OPTIONS.md` says it is a test value).
- **The extra**: `MakePerimeterGarrisons(kPerimeterGarrisons)` is the same class as GarrisonVisible with look (d) and the
  name of its own row (`name()` = the row's key, so the module's lines say `units extra PerimeterGarrisons: ...`);
  registered last in `UnitExtraMakers()` (id order). GarrisonVisible steps aside while PerimeterGarrisons is in effect
  (configure sets `m_yield`; beginFrame drops its grid, no override, no placement wish; one note line
  `units extra GarrisonVisible: PerimeterGarrisons = 1 is in effect: this test look draws nothing (...)`), so the two looks
  never mix on one unit. Neither key changes the seam, the core or the decorator of stage (d).
- **Tests** (2026-11-23 16:17-16:18 EST, through `quietlock.py run`): options **951 passed, 0 failed** in the x86 and the
  x64 tree (the id pins, the row's states under its switch, the default file without GarrisonVisible and with
  PerimeterGarrisons last, `testOnly` only on row 24, GarrisonVisible still read); `test_units_extras.exe` **14 of 14**,
  new case `perimeter_garrisons`: with the key off (five option texts) the registered module draws the city fixture's
  frame field for field as a module of the five makers before this ticket, and asks for no decorator (rule A's arm in
  the unit test); with it on the garrison and the foreign garrison are shown at the plot centre (no root) and both named
  for the decorator (`pending() == 2`), the frame equal to GarrisonVisible forced to look (d); both keys on: (d) only,
  GarrisonVisible's slot with no override and no shown unit, its note once; GarrisonVisible alone: look (a) as before.
  `tools/installer/tests/test_options_schema.py` **22 OK** (the stand-in server of the real table: PerimeterGarrisons a
  checkbox under the visual switch; GarrisonVisible in neither page, schema nor default file).
- `server/tests/extras/units/test_garrison_scenes.py` runs its two "on" tests once per key (GarrisonVisible,
  PerimeterGarrisons) and checks both off lines in E2: **10 passed** (2026-11-23 20:45-21:01 EST, this branch's
  sceneshot player, the C: library and asset root): with PerimeterGarrisons = 1 the c3a and c3b frames differ from the
  recording and the case cameras differ from off by one formation's size, with no LOUD line.
- **Every builder of the changed shared sources** [L, 2026-11-23 16:10-20:44 EST, script
  `C:/c5-64bit-work/impl-4983/allb.sh a|r|b|c|d` outside git, each step through `quietlock.py run --who impl-4983`, paused by
  the day's holds]: `ipc/build.py --target test_options` both trees (951/951 each); c5render; `build_units_extras_tests.py`
  (14/14); `server/build_server.py --config release` on the 0.1b pins (206 s); `build_render_tests.py`;
  `farmfields/test_farmfields_build.py`; `forest/test_forest_build.py`; `fow/test_fow_build.py`;
  `overlay/test_scene_build.py`; `scenebundle/build_scenebundle_tests.py`; `shadow/build_scene_shadow.py`;
  `units/build_units_tests.py` + `test_units_runner.py` (7 passed); `build_sceneshot.py --smoke`; `build_unitfeed.py`;
  `proxy/build_proxy.py`; `test_options_boot.py` (13/13); `test_proxy_rt.py` (226/226 checks); `run_extras_test.py`
  (0 parts failed); `run_hidden_embark_test.py` (PASS); `shell/build.cmd` (BUILD OK); `tools/installer/tests` (98
  passed); `test_installer.py` (56 OK); `test_safety_static.py` (55 passed). Every step exit 0.

**Rule A, key off [L, 2026-11-23 17:14-18:13 EST, MC-B's slot #11979].** `C5_4983_ALLB=<own folder> bash
server/evidence/0455/4983/allb_4983.sh 3` on this branch (PerimeterGarrisons at its default 0, the module linked with six
makers): `run_all: OK: 4 suite(s) passed (plain)`: render-host **195 passed**, scenebundle-programs **4**,
scenebundle-py **27**, offline-equals-live **17 passed** (every library bundle drawn exactly as the server dumped it),
0 failed, 0 skipped.

**The test installer [L, 2026-11-23 16:41-17:00 EST].** A hotfix tier over `C:/c5-64bit-work/cand1/run` (read only;
own descriptor copy of cand5's beta3, own local fork clone, own results store with cand1's results imported), stages
survey to pack one by one with `--only`, `--base-unaccepted --no-export-preflight --not-for-art` (script
`C:/c5-64bit-work/impl-4983/take.sh`, outside git): the build stage adopted the DLLs (no DLL link) and built server,
proxy and composite here (x64-server 349 s); narrowing, census, model carried; purity 12 s; then `build_installer.py
--test-build`: `VP64-Patch-4983-perimeter-garrisons-UNTESTED-for-VP5.4.8-TEST.exe`, 10,060,067 B, SHA-256
`355c8012...91cf2` (local only, never to players). Its page (`payload.iss`) holds
`Opts.Add('Extras|PerimeterGarrisons|0|0|1|B|0|0|15|Perimeter Garrisons')` under the visual additions and no
GarrisonVisible row (27 options on the page).

**Installer-laid on c5h-new-mpk [L, 2026-11-23 17:12-17:13 EST].** Goldberg, modpack548, EXE build 26577625 (checked),
MC-B #11907: `server/evidence/0455/4983/inst_4983.sh install` (saves snapped, the player's DLL `262aee88...` by
`set-dll`, My Games and DLC listed, `run_installer.py ... --extra "/P64OPT=extras.enablevisualadditions=1;
extras.perimetergarrisons=1"`): exit 0; the log `options: /P64OPT [Extras] PerimeterGarrisons = 1 (default 0)`, `the
standard variant (22 major civilizations) of the patch fits it`, installed into `Assets\DLC\VP_MODPACK\Mods\(1) Community
Patch`; VP64Options.ini then holds the release set plus `EnableVisualAdditions = 1` and `PerimeterGarrisons = 1`, and no
GarrisonVisible line.

**The live smoke on that layout [L, 2026-11-23 19:56-20:09 EST, c5h-new-mpk; MC-B's holds moved it from 18:14].** The
mods-form kit save does not load in the modpack form, so `inst_4983.sh arm` loads a copy (`C5H-4983-MPK-T0020`) of the
instance's own autosave of turn 20 (front end with no mods, `g0626.py prepare ... --mods none`), launch without `--env`,
in game after 157 s. The active player of that save had founded no city at turn 21 (a settler, a warrior, a pathfinder):
the first `garrison --all-cities` answered `ERR no city`; the driver's Lua now lets the settler found one by `InitCity`
(setup only) and puts a spearman in every city without a garrison; `cont` resumed the same game: `OK FOUNDED 32 27 |
CITY Sukhothai 32 27 coastal=false made=1125 garrison=1125`.
- Logs (copies in `F:/c5-64bit-work/4983/out-pg/pg/run-logs/`): vpproxy.log and server.log `options: extras:
  PerimeterGarrisons: in effect (PerimeterGarrisons = 1 from the file; EnableVisualAdditions = 1 from the file)` and
  `GarrisonVisible: off: GarrisonVisible = 0 (default)`; server.log `render: units extra PerimeterGarrisons: in effect`,
  `render: units: extras: module linked, 2 in effect: attached` (with FixAirBases); render.log's last `units 1s`:
  `extras PerimeterGarrisons on ov=0 shown=221292 scaled=0 failures=0 | perimeter spread=4286 failed=0`, table 74,
  listed 70; unit shadows merged (`merged_total=11263 not_merged=0`). LOUD lines 0 in render.log, server.log and
  vpproxy.log; no box; the harness stop rc 0, front end restored, `crash.py collect` nothing.
- Pictures (DIAGNOSTIC, D112: a TEST build; popups closed, D114): Visual Checks vc-20261123-200359-workstation-8d1b
  (interface hidden) and vc-20261123-200503-workstation-06fe (shown), files `F:/c5-64bit-work/4983/out-pg/pg/`: the
  spearman's figures stand in a ring round the new city's buildings, facing out [L, by eye]; the other units of
  the player (warrior, pathfinder) as before.
- Removal (`inst_4983.sh remove`): installer exit 0; the DLC tree identical (11,623 rows), My Games identical outside the
  run outputs; `set-dll --previous`; `provision.py verify --quick` rc 0; the run's own save copy deleted by name.
  **One loss [L]:** the game rewrote the instance's `Saves/single/auto/AutoSave_0021 BC-3160` and `AutoSave_Post_0021
  BC-3160` at 20:03 (the autosave of the loaded turn). They were outputs of an earlier test run on this instance (not in
  its user manifest, not the owner's files); no copy existed to put back, because `saves_snap.py` snaps only
  `ModdedSaves`. A driver for the modpack form must snap `Saves/single` (with `auto/`) as well before a launch.
- What this smoke does not show: a coastal city (no coast on this save's one city), a foreign garrison, a fight, E1/E8
  for the key (the look's code path is stage (d)'s, measured in 4983.13 and 4983.15 for (a)).

### 4983.18 The re-merge onto main 49919ea97 (account B implementer, 2026-11-24 16:01-, branch `task-4983-d243-remerge`)

**Why.** The D243 branch (`task-4983-perimeter-garrisons-option` @ ef4a19823) was never merged into main: `git
merge-base --is-ancestor ef4a19823 origin/main` answered NOT an ancestor on 2026-11-24 16:01 (merge base 8417e801d),
though the ticket was marked done. Lesson: before a ticket is called merged, compare its branch head with main by
`git merge-base --is-ancestor <head> origin/main` (exit 0 = in main). [C]

**The merge [C].** `task-4983-d243-remerge` from ef4a19823, `git merge origin/main` (49919ea97: 0680's
`[Shell] WindowTitlePrefix` text option, one-off 9081's road order, 4984's `ShipsMoored` 0-2 by D250, 0711, 0712, ...):
three files conflicted, resolved as follows (merge commit e8c9a1500):
- `options/src/vp64_options.cpp`: GarrisonVisible's row keeps the branch's `NULL, 1` (no per-value categories,
  `testOnly` = 1); ShipsMoored's row is main's (0-2, D250 help lines); `DefaultFileText` is main's road loop (the road
  options last among the visual additions) with the branch's `d.testOnly` skip added. The WindowTitlePrefix row is in
  main's separate text table (`kTextDefs`), untouched.
- `options/tests/test_options.cpp`: `TestExtra`'s ten-id pin (PerimeterGarrisons id 32 tenth) with main's "no row has a
  category per value"; the rows table takes main's `max` column, PerimeterGarrisons max 1; `TestDefaultTextExtras`'s
  order is now `... AirInterceptPatrol < PerimeterGarrisons < RoadLayout < RoadJunction < RoadJitter` (9081 put the road
  options last; on the D243 branch PerimeterGarrisons was last). The first edit of that line went through a Python
  here-document and turned the literal's `\r\n` escapes into real line breaks (a compile error; runbook section 0's
  trap): fixed in 0f2d8dac6.
- `tools/installer/tests/test_options_schema.py`: both sides kept (the branch's PerimeterGarrisons checkbox and no
  GarrisonVisible; main's road order, RoadLayout radio and ShipsMoored's three values).
- `docs/OPTIONS.md` merged without a conflict but is kept as on main (the branch's three hunks taken out): its text for
  PerimeterGarrisons and GarrisonVisible is `server/evidence/0455/4983/OPTIONS-md-entry.txt` (part 1 replaces the
  GarrisonVisible section, part 2 inserted before `### [Extras] RoadLayout`, the page's order), for the MC to write at
  merge; `apply_entry.py` writes a copy carrying it.

**Tests and builders [L, 2026-11-24, workstation, each through `quietlock.py run --who impl-4983r`; script
`server/evidence/0455/4983/allb_4983_d243.sh o|1|2|3|4|a`, work folder `C:/c5-64bit-work/impl-4983r/allb`, game copy
`pure_vp` (4390913) outside the pool, the 0.1b pins].**
- Options (phase o, 16:21-16:23): `ipc/build.py --arch both --target test_options`; x86 **991 passed, 0 failed**, x64
  **991 passed, 0 failed** on the copy of docs/OPTIONS.md carrying the entry; `options/test_asan.py` canary caught, then
  990/1 against the repository's docs/OPTIONS.md (TestDocs: `PerimeterGarrisons = 0` is not in main's document, as
  expected while the entry is text), and the same ASan program on the copy **991 passed, 0 failed, no ASan report**;
  `test_options_schema.py` OK.
- Phase 1: render lib (`render/build.py`, 16:16-16:21), `server/build_server.py --config release` OK in 241 s.
- Phase 2 (16:26-17:05, every step exit 0): `build_units_extras_tests.py --test <t>` and run for all five unit-extras
  programs (they link `srv_units_extras.cpp`): `test_units_extras` **14 of 14** (perimeter_garrisons among them),
  `test_ships_moored` **8 of 8**, `test_fix_air_bases` 7 of 7, `test_fix_unit_placement` 10 of 10,
  `test_air_intercept_patrol` 11 of 11; `build_units_tests.py` + `test_units_runner.py` 7 passed; `build_unitfeed.py`;
  `build_sceneshot.py --smoke`; `farmfields`, `forest`, `fow`, `overlay/test_scene_build.py`, `scenebundle`,
  `shadow/build_scene_shadow.py`, `build_render_tests.py`; `run_extras_test.py` (0 parts failed);
  `run_hidden_embark_test.py` PASS; `proxy/build_proxy.py` (70 s); `test_options_boot.py` 13/13; `test_proxy_rt.py`
  235/235 checks; `shell/build.cmd` BUILD OK. With phase 1 and `ipc/build.py` that is runbook 2.10's 18 builders of
  `vp64_options.cpp` (R13 minus `test_render_host.py`, plus the proxy's three, extras, hidden embark, shell) and the
  three of `server/src/extras/units/`.
- Phase 3 (17:05-17:15): `test_garrison_scenes.py` with this branch's sceneshot, the C: library and the mods548-vt asset
  root: **10 passed** (E2 both keys off, the on tests once per key: GarrisonVisible and PerimeterGarrisons).
- Phase 4 (17:15-17:24): `tools/installer/tests` **98 passed**; `test_installer.py` 56 OK (no skip; the page clicks
  PerimeterGarrisons); `test_safety_static.py` 55 passed.
- ASan (17:24-17:46): `build_units_tests.py --asan` (its own ASan c5render) links; `test_units_runner.py` with that exe:
  6 passed, `test_every_case_passes` stopped by the runner's own 900 s subprocess timeout (no ASan report; the plain
  exe passes it in about 4 min), so the ASan scene-units cases are not verified [L]; `run_hidden_embark_test.py --asan`
  PASS; `build_sceneshot.py --asan --smoke` OK; `run_extras_test.py --asan` fails to compile on main's
  `server/tests/extras/test_extras.cpp` 672-673 (MSVC C3493, the lambda's W and H), as 4984.11 found: not 4983's file.
- Rule A not run (default 0; the key-off unit case equals the module before 4983; 17 of 17 on the D243 branch; main's
  merge changed no garrison source). No live run: the merge changed none of the code the 2026-11-23 smoke ran, and the
  page and parse changes are covered by the tests above. Then main fa399b71f merged (a44f0eef2, no conflict, no
  options or units file). Export check PASS; privacy grep of the own commits: no machine name, user path or private
  term; five lines with the owner's first name in 0f2d8dac6 are main's own comments re-added in a whole-file
  line-ending churn (e8c9a1500 committed test_options.cpp with CRLF because the broken literal held lone CRs;
  0f2d8dac6 put LF back), net diff against main 0 such lines: a squash merge drops them.

## 4985 re-scope: live basing against a reload (W4 of team-0455, 2026-11-20 05:00-07:00 EST, c5h-b)

The question (brief `server/evidence/0455/briefs/w-4985-rescope.md`, MC #7715): does stock draw an aircraft created,
rebased or carried LIVE as it draws the same state after a reload, and does the server draw the same?

**Setup.** Instance c5h-b (Goldberg, EXE build 4390913, stamp 0x546CD0A8 checked before each launch), windowed through
the no-focus launcher, 1080p-class client (2554 x 1411 shots). Stock arm: the pinned 32-bit DLL `i0ship-e96457fd-ia`
(the build 4980 used), no proxy, no server, mods cp, vp, eui. Save `C5H-4985-KIT_0009`, a copy of `C5H-4980-KIT_0009`
(SHA-256 `74fc7428...944db`, Tiny, turn 9; the kit's units, `extras-units.md` 4980). Driver
`server/evidence/0455/4985r/` (`live.py` over the 4980 kit's `kit.py`; `acts.sh`, `pairs.sh`, `readpairs.py`). Every
picture is DIAGNOSTIC (D112), interface hidden and popups closed (D114), Vibrant Textures present (D94).

**Live actions (by Lua, setup only; moves given back by `SetMoves` first):**

| Case | Action [L, 2026-11-20 05:21-05:31, the answers in `F:/c5-64bit-work/0455/4985r/s1/acts.jsonl`] |
|---|---|
| a | `InitUnit` UNIT_FIGHTER 1071 in Honolulu (51,14), a city with no unit in it |
| b | fighter 1064 (based in A (45,13) at the load) `MISSION_REBASE` to Tonga (48,17) (a city with a garrisoned spearman) |
| c2 | fighter 1068 (cargo of carrier 1067 at the load) `MISSION_REBASE` carrier -> Tahiti (1,13) |
| c1 | jet fighter 1066 (based in A at the load) `SetXY` into Tahiti, then `MISSION_REBASE` onto carrier 1067 at (1,12); `IsCargo` true |
| d | carrier 1067 (cargo 1066 and 1069) `MISSION_MOVE_TO` (1,12) -> (1,11); both stay cargo |

Rebase refusals on the way: from A onto the carrier `CanRebaseAt` false (out of range); from Tahiti onto the carrier with
two fighters aboard `CanRebaseAt` false (the carrier's places full; one place was free after c2). The jet fighter's
`SetXY` into Tahiti is a live teleport into a city as well.

**Readings.** After the actions the game's own save (`checklist.py` save_game step) kept as `C5H-4985-LIVE_0009`
(SHA-256 `37a66c8a566ce62e04a85bd795b32d7e0a8f76c7f31ab09277a5bed74eb83f68`, `F:/c5-64bit-work/0455/4985r/saves/`).
For each case a removal pair at a camera on the case's plot (zoom min-2): before 1, before 2 (3 s later), the case's unit
killed by Lua, after 1, after 2; a reading is "drawn" only when the figure is in both before shots and gone from both
after shots (MC #7615); the frame-to-frame floor is before 1 / before 2. Pairs in order a, b, c2, c1, d; c1 and d share
the carrier plot (see the cause below). Live = in the session that made the actions (05:36-05:42); reload = the game
stopped and `C5H-4985-LIVE_0009` loaded in a new process (05:50-05:58).

| Case | Stock live | Stock after reload | Visual Checks (before 1 / after 1; live, then reload) |
|---|---|---|---|
| a created in a city | **drawn**: a three-plane fighter formation over Honolulu, gone after | **drawn**, the same | vc-20261120-053616-workstation-bc19 / -053631-...-c36f; -055028-...-4796 / -055040-...-3eb2 |
| b rebased city -> city | **not drawn** at Tonga (no change above the floor) | **drawn**: the formation among Tonga's buildings, gone after | -053746-...-58ae / -053759-...-47a0; -055144-...-3dd6 / -055156-...-701d |
| c2 rebased carrier -> city | **not drawn** at Tahiti | **drawn**: the formation at Tahiti | -053910-...-ddc1 / -053924-...-d583; -055514-...-2e08 / -055527-...-742a |
| c1 rebased city -> carrier, then carried | **not drawn**: no aircraft on or by the moved carrier (before 1 is the first shot of that plot) | **drawn**: three jet figures on and beside the carrier, all gone after the jet fighter's kill | -054042-...-7fe3 / -054055-...-7b24; -055639-...-cb4b / -055652-...-bc0e |
| d cargo at the load, carrier moved | **not drawn** (the same before 1 as c1: no aircraft at the carrier at all) | **not separable**: no aircraft left after c1's kill (below) | -054209-...-7189 / -054226-...-e747; -055806-...-8723 / -055820-...-f25d |

**Server arm [L, 2026-11-20 06:04-06:33, c5h-b].** The product path (`srv.py` over `shell/evidence/3308/run_3308.py`:
proxy layout, composite, D33 with Overlay on, VP64Options.ini `ServerRenderer = 1`, `ServerUnits = 1`) with W1's 4980
builds used read-only (`server.exe` and the x64 DLL of 4980's `smoke/bin`, the 4980 proxy and composite); save
`C5H-4985-SRVK_0009` (a copy of `C5H-4985-KIT_0009`), the same actions (05:21-06:09 answers equal to the stock arm's,
`F:/c5-64bit-work/0455/4985r/s3srv/acts.jsonl`), the game's save `C5H-4985-SRVL_0009` (SHA-256
`62eb3ff7e755360f1abd6d8d67d4a5126388b5fbaf8014e74c450ce70f7a7ad7`), then that save loaded in a new process. The
runner's loaded-save check could not run on this launch path (no front-end load token with the pid); the save was named,
not checked by the front end (the launch log's `prepare.py --load` names it).

| Case | Server live | Server after reload | Visual Checks (before 1 / after 1; live, then reload) |
|---|---|---|---|
| a | **drawn** (the same formation as stock) | **drawn** | vc-20261120-061532-workstation-5b6b / -061546-...-a61d; -062719-...-dc48 / -062731-...-fe07 |
| b | **not drawn** | **drawn** | -061647-...-67c4 / -061659-...-67f6; -062834-...-f1ad / -062849-...-0507 |
| c2 | **not drawn** | **drawn** | -061759-...-3235 / -061812-...-fc0f; -062951-...-ad47 / -063005-...-339b |
| c1 | **not drawn** (no aircraft at the carrier) | **drawn** (three jet figures, as stock) | -061923-...-1bd3 / -061936-...-eaf4; -063107-...-e37d / -063121-...-9def |
| d | **not drawn** | not separable (as stock) | -062049-...-6b13 / -062103-...-057b; -063222-...-909a / -063236-...-d8d4 |

The server's `units 1s` counters (render.log of each run, copies in `C:/c5-64bit-work/0455/4985r/srv/run-live/` and
`run-reload/`) show the same rule from the unit table's side [L]: at the load table 44, listed 35; after (a) 45 / 36
(the created fighter listed); after (b) 45 / **33** (three fewer: the rebased fighter and, by the change of A's plot,
A's bomber and jet fighter); after (c2) 45 / **31** (the fighter leaving the carrier and the cargo 1069 left behind);
(c1) and (d) no change. After the reload of that state: table 45, listed **36** (the five hidden aircraft listed again).
In the reloaded game the kill of 1066 took table 42 -> 41 and listed 32 -> **30**: the kill hid 1069 too. So the server
draws exactly the aircraft stock draws, live and after a load: it follows the DLL's `Visibility` messages, and there is
no increment 3 gap in the server here; the inconsistency is stock's own.

**Stock is not consistent with itself [L].** An aircraft rebased live (b, c2, c1) is not drawn where it arrives; after a
save and reload of exactly that state it is drawn there. The cargo of a carrier that moved live (d) is not drawn either.
An aircraft created live by `InitUnit` in an empty city (a) is drawn live and after the reload.

**The cause, as far as shown here.** The DLL's `CvPlot::updateCenterUnit` (`unit-server-scene.md` 12.3, `extras-units.md`
4981.2 row "a unit not the centre of its plot": every aircraft is a non-centre unit and gets `Visibility(false)`) runs
whenever a plot's units change: a unit entering (`setXY`), a kill. [L] The kill of jet fighter 1066 on the reloaded
carrier plot took away every aircraft figure there, the fighter 1069 too (pair d after the reload has no aircraft in its
before shot); in the 4980 masks all of a plot's units were killed together, so this did not show. [I] After a load the
EXE creates each unit's figure visible and the DLL has not yet sent `Visibility(false)` for that plot, so every aircraft
is drawn until something changes its plot; a live rebase changes the target plot at once, so the arriving aircraft is
hidden. Case a fits if `initUnit`'s own `setXY` runs `updateCenterUnit` before the figure is created (`Created` is sent
at the end of `initUnit`, `unit-server-scene.md` 12.1) [I: not traced]. This also explains 4980's first-session absence
at city A [I]: any change on A's plot before that shot (not recorded) hides its aircraft. The server's counters confirm
the trigger without a picture [L]: the rebase out of A hid the two aircraft that stayed in A. Not run: a change of a
plot with loaded aircraft by a unit walking in or a selection only.

**For 4985 and lead-0042.** Stock draws a based or carried aircraft when the state comes from a load and stops drawing
it once anything changes its plot; it never draws one that arrived live by rebase or that sits on a carrier that moved.
The extra AirBases parked (option 26) is therefore a consistency fix in the sense of MC #7715 (the same state is drawn
two ways by stock), not only a visual addition; the server matches stock in every case, so the server has no gap of its
own to fix for increment 3.

**Corrections made in place (2026-11-20, this section):** 0442's "drawn by neither" (`unit-server-scene.md` 12.3)
holds for aircraft based or carried live by rebase or `SetXY`, and for aircraft on a plot changed after the load; it
does **not** hold for an aircraft created by `InitUnit` in a city with no other unit (drawn by both, case a).

### Not verified (4985 re-scope)

- **Each case was run once per arm, not on two loads of the same kind** (the lead's extra): one live session and one
  reload per arm. The grant ended at 07:00 and the server arm was taken over a second stock pair of sessions. The two
  arms agree case by case, which is a repeat of the live and the reload readings on a second process each, but not of
  stock itself.
- Case d after a reload is not separable from c1 (one kill on the plot hides both); whether the loaded cargo 1069 was
  drawn beside 1066's jets was not read (the jets' formation covers the carrier).
- Another player's aircraft, an aircraft entering a city with no other unit by rebase (Tahiti had none: c2 was still not
  drawn, so the empty city does not matter for a rebase), selection of an aircraft: not run.
- Case a's live drawing is a single creation in a city with no other unit; 0442's two fighters created in the capital
  were not drawn [L, 0442]; the difference (another unit on the plot, or the second creation changing the plot of the
  first) is not established [I]. *(2026-11-20, 4985 FixAirBases [C]: `initUnit`'s `setXY` and `updateCenterUnit` run
  before `setupGraphical` creates the figure, so their hides reach no figure; a second creation on the plot runs
  `updateCenterUnit` after the first figure exists and hides it. Section "4985 FixAirBases" 1.)*

## 4985 FixAirBases (W5 of team-0455, 2026-11-20; option id 26, bugfix, default 0 until the owner accepts) [C: the DLL source and the code with its tests; nothing run live]

Branch `w5-4985` (from `epic-0455-extras-b` @06e2a443a). Brief `server/evidence/0455/briefs/w5-4985.md`; MC #7901.

### 4985.1 Stock is not consistent with itself, and why [C, DLL fork branch `x64` at e9d0c4837, the working tree of `Community-Patch-DLL-x64`, read 2026-11-20 07:20 EST]

**Stock draws the same aircraft two ways.** After a load it draws every based and carried aircraft (4980's masks, the
re-scope's reload column [L]); after a live rebase, a carrier move or a kill on the aircraft's plot it draws none of the
aircraft on that plot (the re-scope's live column [L], W4 #7884). The option FixAirBases takes the **after-load**
behaviour. The cause is the DLL's `CvPlot::updateCenterUnit` (`CvPlot.cpp` 598-658) [C]:

- It returns at once while the graphics are not initialised (`if(!GC.IsGraphicsInitialized()) return;`, 600-603) and
  when the plot is not visible to the active team (605-609: `setCenterUnit(NULL)`, no visibility call).
- Otherwise it picks the centre unit: the selected unit, else the best defender if it is not `DOMAIN_AIR` and not
  invisible to the active team, else the head unit if it is not garrisoned, not air and not invisible (611-635). **An
  aircraft is never the centre unless it is selected.** Then every unit of the plot gets `GameplayUnitVisibility`: the
  centre `(true, false, 0.01)`, **every other unit `(false, false, 0.01)`** (637-657).
- It runs whenever a plot's units change: `CvUnit::setXY` calls it through `removeUnit` / `addUnit` with `bUpdate`
  (`CvUnit.cpp` 20463-20468, 20544) for the plot left and the plot entered, so a rebase, a move, a teleport, the cargo
  carried along (21016-21031: each cargo unit's own `setXY` with the carrier's `bUpdate`) and a kill (`setXY` to no plot)
  hide every aircraft on both plots; `initUnit` calls it once more after its `setXY` (`CvUnit.cpp` 607).
- `CvUnit::setXY` also hides any unit that **enters a city** (20527-20532: `Visibility(false)`), and shows a unit that
  leaves one (20470-20477), before `addUnit`'s `updateCenterUnit`; so the rebase into a city is hidden twice over.
- At a load, `CvUnit::setupGraphical` (`CvUnit.cpp` 1760-1804) creates each figure (`GameplayUnitCreated`) and hides
  only a garrisoned unit (1798-1802); `updateCenterUnit` has not run for any plot (the graphics were not initialised when
  the units were placed). So every aircraft is drawn until something changes its plot. In `initUnit` the `setXY` (599)
  and the `updateCenterUnit` (607) come before the figure exists (the EXE ignores a `Visibility` before the creation,
  `unit-server-scene.md` 12.1 [L]), which is why the re-scope's case a (a fighter created in an empty city) is drawn live.
- A rebase of a fighter (`SPECIALUNIT_FIGHTER`) seen by the human first sends `GameplayUnitRebased` (the EXE's flight
  visualisation, `CvUnit.cpp` 10219-10231) and then moves it with `bShow = false`; a cargo unit carried along moves with
  `bShow` false too, so its move reaches the EXE as `SetPosition` -> `GameplayUnitTeleported` (kind 13, `CvUnit.cpp`
  20972-20990, 29819-29827), not as a walk.

The server draws exactly what stock draws (it follows the DLL's `Visibility` messages, the re-scope's server arm [L]);
the inconsistency is stock's own, and it is fixed on the server's side only, display only (the DLL is untouched, E1).

### 4985.2 The extra (`server/src/extras/units/srv_unit_airbases.{h,cpp}`)

`MakeFixAirBases(id)` (the maker passes `Vp64Opt::kFixAirBases` once row 26 exists; until then it is not registered).
Per table unit, through the seam's show flag only (no root, no heading, no scale):

| Unit | What the extra does | Why |
|---|---|---|
| not domain air (`UnitRecord::domain` != 1) | nothing | land and sea units keep stock's stack rule (only the centre unit drawn) |
| an aircraft stock lists | nothing | |
| hidden (visible flag clear), alive, of the active team, its scene plot its record's plot, no scene root off its plot centre | **show** (opaque, the scene's own placement) | after a load stock draws every aircraft (4985.1) |
| dead or delayed death (`kFlagDead`, `kFlagDelayedDeath`; fading out by a kill) | nothing | |
| held in a fight (`heldInFight`) | nothing | lead-0042's contract |
| of another team, or with no player row | nothing | E7 (below) |
| its scene plot is not its record's plot, or its scene root is more than 0.02 inner radii from the plot centre (walking, GarrisonVisible's rule) | nothing | lead-0042 #7810; an aircraft never walks (rebase and cargo moves are teleports, 4985.1), so this is a guard only |

- **Every aircraft, not only those in a city or on a carrier:** an aircraft is always based in a city or carried (a
  carrier, or for a missile a submarine or cruiser); the feed has no city flag on a plot (`PlotRecord::flags` bit 4 is
  never set, `srv_feed.h`), and after a load stock draws every aircraft whatever its plot, so the rule needs none.
- **Placement = the after-load placement [C, 4980's c5 bundle `feed.bin`]:** after the load no aircraft has a position
  record (no kind-102/6 event for 1064-1070 in `C5H-4980-C5AIR_0009`'s feed; `feedair.py` in the scratch folder, not
  kept), so the scene places a loaded aircraft at its plot centre with the module's formation; a rebase (kind 30) or a
  teleport (13) clears a unit's position record (`srv_units_core.cpp` 982-994), so a shown aircraft gets the same input
  (test `air_live_sequence`: every field but the seed equal to the reload's). No root override and no hunk in
  `server/src/units/` or `render/src/units/`.
- **Foreign aircraft (E7):** not shown. After a load stock would also draw another team's aircraft on a plot the active
  team sees [I: the same rule; not staged, 4980 "Not verified"]; the brief's "when in doubt, only what the after-load
  state would show the active player" and "own or team aircraft" are kept by showing the active team's only. The
  seam's fog filter would remove another team's unit on a plot not visible anyway (4981.3).
- **Inputs:** the scene's unit and player tables (E6: either missing, the extra is off for the session with one line)
  and its height field (none this frame: nothing; one that gives no plot centres: E6). Feed kind 1001 (the DLL's carrier
  messages) only splits the counts into carried and based (no 1001 seen: every shown aircraft counts as based); kind 30
  is not read (its effect reaches the extra as the visible flag, and the core clears the rebased unit's root).
- Counts (`AirBaseCountsOf`, unit-frames): frames, aircraft, shown, shownCarried, shownBased, dead, held, foreign,
  noPlayer, noPlot, walking; carrierEvents, cargo. The module's own line part: ` | extras FixAirBases on ov=0 shown=<n>
  scaled=0 failures=0` (4981.5).
- **Not changed:** a selected aircraft is the centre unit and stock draws it already; the EXE's rebase flight
  (`GameplayUnitRebased`) is not drawn by the server either way (the server's unit scene has no rebase animation; with the
  flag the aircraft stands at its new base at once).

### 4985.3 Tests [C]

`server/tests/extras/units/test_fix_air_bases.cpp`, built by `build_units_extras_tests.py --test test_fix_air_bases`
(the option `--test` is this ticket's; default `test_units_extras`), synthetic tables on a flat 10 x 8 grid:

| Case | What it shows |
|---|---|
| `air_choice` | `ChooseAirBase` for each kind (shown; listed; not air (land, sea); dead; delayed death; held; another team; no player row; no plot; walking; an invisible aircraft of the active team shown); `AirBaseWalking` (its plot no root: no; another scene plot: yes; root 0.5 from the centre: no, 1.0: yes (0.02 x 32 = 0.64); root and no centre: yes); `AirTeamOf` |
| `air_scene_on` | the core with the extra on: the hidden own based fighter and the carried fighter listed opaque (1, 0, 0) at their plots with no root; the dead, delayed-death, foreign, no-record aircraft and the hidden warrior not listed; core `x shown=2 show_refused=0 root=0`; the extra's counts; the shown inputs equal the after-load table's (every aircraft visible) field for field |
| `air_flag_off` | E2: the flag 0 (one line `units FixAirBases: off: its own flag is 0`) and the real extra with no row (`off: not an extra`) give the same scene as no module, seeds included; the features' default module has nothing on; E5 `units extra FixAirBases: off: ServerUnits = 0`, one line |
| `air_inputs` | E6: no unit table, no player table, a height field with no plot centres: each one LOUD line, off for the session, the scene stock; no grid this frame: still on; the row check (made with another option's id: `configure: its option row is not FixAirBases`) |
| `air_held` | a held fighter not shown, the extra asked nothing (core `held=0`), shown again once released |
| `air_live_sequence` | the re-scope's live sequence on synthetic tables, stock and the flag on: after a load 6 listed by both; rebase A -> T (kind 30 and the hides of the rebased fighter and the two left in A): stock 3, on 6 (the rebased one at T, no root); the carrier's move with its cargo teleported (13) and hidden: stock 1, on 6; a kill on the carrier's plot (15 then 14): the dead one never shown, stock 1, on 5; the reload of that state: every unit's input equal to the flag-on live one but the seed |
| `air_cost` | E8 indication only (workstation, not a result): a 10,000-unit build without the module and with the extra on |

**The scene test (E9)**, `server/tests/extras/units/test_fix_air_bases_scenes.py` (pytest; sceneshot, the library and a
game copy by environment variables, as 0454's `test_visible_outline_scenes.py`), on 4980's c5_air and c6_carrier bundles
(recorded after a LOAD: every aircraft drawn by stock and the server):

- E2: `--option FixAirBases=0`: every frame byte-equal to the recording's dumps (tolerance 1:1);
- E9a: `--option EnableBugfixes=1 --option FixAirBases=1` on the bundle as recorded: the extra in effect and the frames
  still equal (after a load nothing hides an aircraft; the fix changes nothing there);
- E9b: a synthetic copy (`synhide.py`, in the same folder) whose `feed.bin` unit tables carry the aircraft (c5: 1064, 1065,
  1066; c6: 1068, 1069) with the visible flag clear, which is the state `updateCenterUnit` leaves after a live change of
  their plot: with the flag 0 the frames differ from the recording (exit 10, the aircraft are not drawn), with the flag 1
  they equal it again (exit 0). The copy keeps the bundle's dumps, the server's own frames of the after-load state, so
  E9b checks the shown aircraft against the after-load picture byte for byte: the same figures at the same spots.

Not a picture of a live rebase: in a synthetic copy the aircraft keep their after-load plots and seeds. The live-rebased
bundle (4985.4) is the later step.

Results: 4985.6.

### 4985.4 The bundle the live step must record (for E9 pictures and the scene test on a live-rebased state)

None of the library's bundles holds a live-rebased state (4980 "Aircraft ...": no kind 30 in any `feed.bin`). The live
step (MC #7901 condition 1, by START-REQUEST) records, on the product path with ServerUnits = 1 and `FixAirBases` = 0
(`rec_kit.py` over the 4980 kit, recorder `render_test_record_after=1800`, `_frames=8`, `_dump=all`):

1. `C5H-4985-LIVEREBASE`: the live session of the re-scope's save `C5H-4985-SRVK_0009` (a copy, never the original)
   after the actions b, c2, c1, d (`server/evidence/0455/4985r/` `live.py`), recorded **after** the actions in the same
   process, at the c5 camera (Tonga (48,17) and A (45,13) in view) and the c6 camera (the carrier at (1,11)): the feed
   then carries kinds 30, 13, 24 and 1001 of the live changes, and sceneshot with `--option EnableBugfixes=1 --option
   FixAirBases=1` must draw the aircraft that stock does not.
2. `C5H-4985-RELOAD`: `C5H-4985-SRVL_0009` (a copy) loaded in a new process, the same cameras: the after-load reference
   the on-picture of bundle 1 is compared with (the same aircraft at the same spots; seeds may differ, so the formation's
   figures may stand differently).

### 4985.5 docs/OPTIONS.md entry for id 26 (for the rows hunk; the lead lands it with W3's rows)

| `Extras` | `FixAirBases` | 0 (until the owner accepts; MC #7901) | 0 to 1 | A bugfix (`kCatBugfix`, no needed extra; named `AirBases`, a visual addition, until MC #7901 of 2026-11-20): stock draws a based or carried aircraft after a load but not after a live rebase, a carrier move or a kill on its plot (the DLL's `CvPlot::updateCenterUnit` hides every non-centre unit of a plot when its units change, and an aircraft is never the centre); with this flag the server draws the active team's aircraft in every case as stock draws them after a load, at the same place. Display only, the DLL is untouched. Needs `ServerUnits = 1` (its consumer's E5 line). Id 26 (MC #7491, #7901) | `server/src/extras/units/srv_unit_airbases.*` (ticket 4985) [C 2026-11-20, unit tests] |


### 4985.6 Results (2026-11-20, workstation, clang-cl x64; every run through `quietlock.py run`) [C]

- **Unit tests** (07:38-07:41 EST; c5render built from this branch @06e2a443a): `test_fix_air_bases.exe` **7 of 7 cases
  passed**; `test_units_extras.exe` (the seam suite, the new extra linked, not registered) **8 of 8**. The cost
  indication of `air_cost` (not a result, PERFORMANCE.md; two runs of the program, each 20 builds per arm): a
  10,000-unit build 6.86 / 6.34 ms (first run) and 5.97 / 7.05 ms (second run) without the module / with FixAirBases on
  and 3,334 more units listed; a third run after the merge of the team branch @b736f57c5 (08:55 EST): 9.69 / 14.43 ms.
  The runs disagree in sign and size, so this says nothing beyond "a few ms at 10,000 units" (the extra's own work per
  aircraft is a few compares and, for a shown one, one map lookup; the listed units' inputs cost what the same units
  cost after a load). E8 with PERFORMANCE.md's tools is not done.
- After that merge (main with 0598's link fix, 55fe87d38): `test_fix_air_bases.exe` 7 of 7 and `test_units_extras.exe`
  8 of 8 again (c5render rebuilt from the merged branch).
- **Scene test** (08:22-08:29 EST): `test_fix_air_bases_scenes.py` **6 passed** (pytest, 420 s): E2 on c5_air and
  c6_carrier, E9a on both, E9b on both synthetic copies (flag 0: exit 10, differs; flag 1: exit 0, equal to the
  after-load dumps at tolerance 1:1). `synhide.py` touched one UNITS record per aircraft (the first full table; the later
  diffs do not carry them). The player: an untracked scratch build (not committed) of epic 0454's tree @a0fc1becd (for
  `--option`) with this branch's changed files @442202156 laid over it (no file changed by both but research and
  tracker), W3's rows hunk for ids 23-26 (options files only, from `w3-4983` @3feba25b0), this extra registered in
  `UnitExtraMakers()` (the entry of 4985.2's hunk) and W3's two sceneshot lines (`attachExtras` after the probe check,
  #7951/#7956; the units module in 4938's extras block). Bundles from the library (read-only), replayed with c5h-c's
  install, EXE and MODS (read-only). A first build of that player failed to link (`keepTapCamera`, `applyFrameCamera`
  missing): `git archive` gives files their commit time, older than the objects of an earlier build in the same folder,
  so Ninja kept stale objects; a clean folder built (08:13-08:20).
- **Pictures** (08:31-08:38 EST, DIAGNOSTIC, the scratch player, F:/c5-64bit-work/0455/4985/pics/, not in git): the
  synthetic copies with the flag 0 differ from the recording in all 8 frames (c5: 9,940-9,967 pixels; c6: about 12,150),
  with the flag 1 every frame is byte-equal (`OK 8 shot(s), each byte-equal to the server's own dump`). Crops off | on:
  c5 frame 1996 vc-20261120-083849-workstation-e97d, frame 2044 (second frame) vc-20261120-083853-workstation-80dd; c6
  frame 2110 vc-20261120-083851-workstation-ceb0.
- **Shadows (open for the live step) [I].** In the flag-0 pictures the aircraft's shadows stay on the ground: the
  shadows come from the recording's shadow tap (the EXE's shadow pass of the after-load frames, where the aircraft were
  visible), not from the server's unit scene. Live, the EXE hides the aircraft after a plot change, so [I] it casts no
  shadow for them either, and FixAirBases would then draw the aircraft without a shadow (as 4983 notes for a garrison:
  the EXE's shadow is not drawn for a unit it hides). To read in the live step's pictures (4985.4); if so, a shadow for a
  shown unit is the seam's question, not this extra's.

### 4985.7 Not verified

- Nothing ran live and server.exe was not built: the extra is not registered until row 26 lands (W3's rows); the live
  step (MC #7901 condition 1: the deciding cases a second time per arm, then the flag on) is a later START-REQUEST.
- The synthetic copies keep the aircraft at their after-load plots and seeds; a live-rebased aircraft (new plot, kind
  30, cargo teleports) is covered by the unit test `air_live_sequence` on synthetic tables only, not by a bundle.
- Shadows of shown aircraft (above), the EXE's rebase flight, a missile on a submarine or cruiser, another team's
  aircraft (E7: not shown by choice) and an aircraft under a combat (0042's held state, set by nothing yet): not seen.
- The native suite was not run under MSVC ASan (`build_units_extras_tests.py` has no ASan variant).

## 4984 ShipsMoored (W6 of team-0455, 2026-11-20; option id 25, visual addition, default 0 by D207) [C: the DLL source and the code with its tests; offline pictures; live on the product path 2026-11-23, 4984.10 (corrected from "nothing live")]

Branch `w6-4984` (from `epic-0455-extras-b` @1287c9fd1). Brief `server/evidence/0455/briefs/w6-4984.md`. With
`ServerUnits = 1`, `EnableVisualAdditions = 1` and `ShipsMoored = 1`, a ship standing in a city is drawn on the city
plot's water side; off, the scene is exactly as before (E2). Premise [L] (4980 c4 mask pair -040636/-040642): stock draws
no ship in a city, and the server copies that.

### 4984.1 Which units, and which plots are ports [C, code; the DLL fork branch `x64` at e9d0c4837 read 2026-11-20 09:30 EST]

`srvextras::IsLiveShip` (`server/src/extras/units/srv_unit_moored.h`): `UnitRecord::domain` 0 (DOMAIN_SEA; the fork's
`CvEnums.h`: SEA 0, AIR 1, LAND 2), no dead or delayed-death flag. An embarked land unit is land domain and never taken
(GarrisonVisible does not take it either). A **port ship** is such a ship whose record plot's centre is land in the
height field (above `HeightGrid::waterZ`): a ship stands on a land plot only in a city or on a plot that acts as a canal.

- **No city flag in the feed.** `PlotRecord` flags bit 4 ("city on the plot") is never set (`srv_feed.h`: no pure getter
  gives a city's plot), and the city events carry plot -1. The seam's frame gives no plot table either, and an extra that
  called `SrvFeed::CopyPlots` itself would read the live feed, not a bundle's recorded tables (the player hands the scene
  its own `FeedSource`). So the extra decides on the height field alone.
- **Canals [C].** VP sets `GLOBAL_PASSABLE_FORTS = 1` (`(2) Vox Populi/Database Changes/CustomModOptionChanges.sql`), and
  the CP database gives `MakesPassable = 1` to `IMPROVEMENT_FORT` and `IMPROVEMENT_CITADEL`
  (`CoreImprovementChanges.sql`), VP to Portugal's `IMPROVEMENT_FEITORIA`. A ship on such a plot stands on land too and
  is moored by this extra as if the plot were a city [I: a canal ship is listed by stock, drawn at the plot centre on
  land; not staged; a listed ship on a land plot is drawn by the server at the plot centre on land, L on the synthetic
  copy of 4984.6]. Telling the two apart needs a city or improvement field in the seam's frame (a hunk in
  `server/src/units/`, agreed with lead-0042 first); not asked for this ticket.
- **How many ships in a city [C, `CvUnit::CountStackingUnitsAtPlot`, `CvPlot::getUnitLimit`].** Inside a city (and on a
  coastal canal plot) units of different domains do not count against each other; `CITY_UNIT_LIMIT` is 1
  (`CoreDefineChanges.sql`), so a city holds one naval combat unit beside its land garrison, plus non-combat ships
  (cargo ships, great admirals, work boats) and "stacking" units. So a row of two or three is the realistic most.
- **What stock does with each [C/I].** A ship that enters a city is hidden by `CvUnit::setXY`; at a load only a
  garrisoned unit is hidden (`setupGraphical`), so a second, non-garrison ship in a city after a load is listed and drawn
  at the plot centre, on land [I: by 4985.1's reading of the load path; no bundle holds two ships in a city]. The extra
  moors every port ship, hidden or listed: a hidden one is shown (the seam's show) and placed; a listed one is moved.

Per unit (`ChooseMoored`, pure): not a port ship: untouched; held in a fight: untouched; no water spot for it (4984.2):
untouched, as stock has it (a hidden ship stays hidden: shown at the plot centre it would stand on land, which round
r2syn showed for look (b), 4984.6; changed 2026-11-20 10:20, was "shown at the scene's own place"); **walking** (its
scene plot is not its record's plot, or its scene root is more than `kMooredWalkShare` = 0.02 inner radii from the plot
centre, the garrison's rule): shown if hidden, no root, no heading; otherwise shown if hidden and placed at its slot
(root, heading, the look's scale).

### 4984.2 The water side and the row [C, code and unit tests]

Per frame (`beginFrame`) the extra lists the port ships of the unit table, groups them by plot (key order inside a
plot) and plans each port's row once (`PlanMooredRow`): for the distances 0.6, 0.7, ... 1.6 inner radii from the plot
centre (the plot's edge is at 1.0, its south corner at 1.155) and, at each, the directions south, south-east,
south-west, east, west, north-east, north-west (the garrison's order: the default camera looks north), the first
direction and distance at which **every** ship of the row stands on water is taken: the nearest water, the earlier
direction on a tie. A ship's footprint is its root and the points `kMooredHalfLength` 0.5 inner along the hull and
`kMooredHalfBeam` 0.2 inner across it on either side (x the look's scale); each must be at or below the water level
(set from round r1, 4984.6: with 0.35 / 0.15 look (b) stood with its stern on the beach; a trireme at stock scale is about
one inner radius long [L, one picture]). The row runs along the shore (the direction turned by +90 degrees), member j at
(j - (n - 1) / 2) x gap: 0.5 inner side by side, 1.1 inner end to end (look b). The module's placement puts a figure at
max(terrain, water level), so a moored ship floats at the water level.

- A coastal city whose near sides are land takes the first side with water (test `moored_row`: the sea north only gives
  the north-east side at 1.3 inner); a one-plot island city takes the south side; a land-locked plot (no water within
  1.6 inner plus the footprint) has no row: its ships are left as stock has them (counted `noWaterSide`).
- **When the row does not fit** (two or more ships, added 2026-11-20 10:15 after round r2syn): each ship on its own, in
  key order, at the nearest water spot along the shore (at each distance and direction the offsets 0, +gap, -gap,
  +2 gap, ...) at least one gap from every ship placed before it; a ship with no such spot is left as stock has it. Test
  `moored_row`: a 16-unit inlet holds one ship bow to land, not a row of three (the first placed at the single ship's
  spot, the other two unplaced) and no ship along the shore (a 32-unit hull); a bay split by a pier holds two ships, each
  on water, one gap apart.
- **Outside the height field is water [C, `heightAtSample` returns 0 outside the samples].** A footprint that leaves the
  field (a port on the field's first or last row, the margins are inside the field: `mapH` = map height + 4) reads 0,
  at or below the water level, so it counts as water (test `moored_scene_on`, the all-land map). On a wrapping map x
  wraps (`wrapX`). Not seen on a real map [I].
- **The cull [C, `srv_units_core.h`].** The core culls by the unit's plot: a box of `kCullRadius` 64 units (2 inner) around
  the plot centre. A moored root is at most 1.6 inner out and the hull reaches 0.5 inner beyond it (2.1), so only the tip of
  a ship moored at the far end of the search could leave the box [I: not seen; the same rule for any unit an extra moves].

### 4984.3 The looks (candidates for the owner)

`MooredLook` (since D250, 2026-11-24, the option's value: `ShipsMoored = 1` is (d), `= 2` is (b); (a) and (c) are not
offered to players and stay reachable only through the maker's fixed-look form for tests; 4984.11. Was: "the maker's
second argument; the default `BowToLand`"):

| Look | Facing | Row | Why |
|---|---|---|---|
| (a) `BowToLand` (default, C1's recommendation) | the bow to the city, stern to the sea | side by side, 0.5 inner apart | C1's recommendation (the epic's question (2) to the owner): moored bow to the quay as galleys and small craft are; the narrowest row along the shore |
| (b) `AlongShore` | along the shore (the side's direction + 90 degrees) | end to end, 0.9 inner apart | alongside a quay; needs the least water (only half the beam reaches towards the city), so it stands nearest the city |
| (c) `OutToSea` | the bow out to sea | side by side | ready to sail; the same footprint as (a) |
| (d) `BowToLandSmall` | as (a) at scale 0.8 (`kMooredSmallScale`) | as (a), 0.8 x the gap | a ship at stock scale is about a plot long; smaller, it stays nearer its city and overlaps the neighbour plot less |

### 4984.4 Inputs, E5, E6 [C, unit tests]

- The unit table (E6: none, the extra fails `beginFrame`: `units extra ShipsMoored: LOUD: failed: beginFrame: the scene
  gives no unit table: off for the session (the scene draws stock for it)`), the height field (none this frame: nothing;
  one with no plot centres: E6, the same line with "the height field gives no plot centre"), the feed grid's width
  (`UnitExtrasFrame::width`; 0: no ports this frame). No player table, no feed kind.
- E5: with `ServerUnits = 0` the module's one line `units extra ShipsMoored: off: ServerUnits = 0`, nothing drawn.
- The registration (with the option rows, after Extras A's 2a is on main; not on this branch): include
  `#include "srv_unit_moored.h"` and the entry `[]() { return MakeShipsMoored(Vp64Opt::kShipsMoored); },` between
  GarrisonVisible's and FixAirBases's in `UnitExtraMakers()` (4984 issue log).

### 4984.5 Tests [C]

`server/tests/extras/units/test_ships_moored.cpp`, built by `build_units_extras_tests.py --test test_ships_moored`,
synthetic tables on a 10 x 8 lattice whose samples are land (2.0) or water (0, water level 1.0) by a rule per test:

| Case | What it shows |
|---|---|
| `moored_choice` | `ChooseMoored` for each kind (placed hidden and listed; at sea; land; embarked land; dead; delayed death; held; walking; no water spot, walking or not: never shown); `IsLiveShip`; each look's facing on the south side, its gap and scale |
| `moored_row` | `PlanMooredRow` on the south sea: (a) one ship at 1.2 inner south, yaw pi; three side by side at x -16, 0, +16; (b) two end to end at 0.9 inner, x -17.6, +17.6, yaw pi/2; (c) 1.2 inner, yaw 0; (d) scale 0.8 at 1.1 inner; a land-locked plot: none; a one-plot island: the south side at 1.2; the sea north only: north-east at 1.3, yaw -30 degrees; the inlet and the pier (4984.2); bad inputs; the footprint test |
| `moored_scene_on` | through `UnitsCore`: the hidden garrison trireme shown opaque and the listed cargo ship moved, a row of two at x -8 and +8, 1.2 inner south; both turned (xforms yaw pi); the ship at sea, the dead, delayed-death and land units as stock; core `x shown=1 root=2 heading=2`; module counts; a second frame the same; the small look's scale 0.8 in xforms; the all-land map: the hidden ship not drawn, the listed one at the plot centre, and the ship at (3, 0) moored off the field's edge (4984.2) |
| `moored_flag_off` | E2: the flag 0 (one line `units ShipsMoored: off: its own flag is 0`) and the real extra with no row (`off: not an extra`) give the same scene as no module; the features' default module has nothing on; E5 one line |
| `moored_inputs` | E6: no unit table, a field with no plot centres: one LOUD line each, off for the session, the scene stock; no grid this frame: still on; the row check (`configure: its option row is not ShipsMoored`) |
| `moored_held_walking` | held ships untouched (core `held=0`, the extra asked nothing), placed again once released; walking through the extra itself: no root (at rest), a root 0.5 from the centre (at rest), 1.0 (walking), another scene plot (walking); a ship at sea: nothing |
| `moored_cost` | E8 indication only: a 10,000-unit build on a 100 x 100 field with 400 ports of two ships each, without the module and with the extra on |

The scene test (E9), `server/tests/extras/units/test_ships_moored_scenes.py` (pytest; sceneshot and the library by
environment variables, as `test_garrison_scenes.py`): E2 on c4_port and c6_carrier (`ShipsMoored=0`: P1 byte-equal, the
module linked and dropped, the off line); on c4_port with the extra on: in effect, no LOUD line, exit 10; the named
camera's on picture differs from the off picture in `MIN_PIXELS`-`MAX_PIXELS` pixels (one ship); c6_carrier with the
extra on: in effect and still byte-equal (the carrier is at sea, no port ship).

**Results (2026-11-20, workstation, clang-cl x64; every run through `quietlock.py run`) [C]:** `test_ships_moored.exe`
**7 of 7** (10:15, after the fallback; built from this branch with the scratch player's c5render); `test_units_extras.exe`
(the seam suite, this extra linked, not registered) **12 of 12** (10:36); the scene test with the scratch player **5
passed** (229.7 s, 10:31-10:35); **rule A**: P1 (`--dumped --compare --tolerance 1:1 --no-pictures`, every option at its
default) of all **16** library bundles with the scratch player (GarrisonVisible, ShipsMoored and FixAirBases registered,
all off): **16 of 16 byte-equal**, each log with `units ShipsMoored: off: ...` and `units: extras: module linked, 0 in
effect: dropped` (10:36-10:46; `C:/c5-64bit-work/0455/4984/p1_all.log`, not in git).

### 4984.6 Pictures (DIAGNOSTIC, D112; offline, scratch player) [L for what the frames show, one camera]

4980's c4_port bundle (`C5H-4980-C4PORT_0009_20261120-034655_workstation`, city P Raiatea (54,19), trireme 1062 its
garrison, worker 1063), named camera `c4_port`; the scratch player `C:/c5-64bit-work/0455/4984/ss` (source
`epic-0454-extras-a` @50d986923 + this branch's `server/src/extras/units/` + W3's rows hunk + makers 24-26 + W3's two
sceneshot lines; the look by a scratch-only environment variable `C5_MOORED_LOOK`, never committed). Pictures in
`F:/c5-64bit-work/0455/4984/r1`, `r2`, `r2syn`, `r3`, `r3syn` (not in git). The bundle has no city buildings (`cities 0`),
so `<case>_mock.png` pastes the on-pixels on the stock shot (`refs/c4_port.png`; W3's `mock.py`; no depth).

- **Off** = P1 (`--dumped --compare --tolerance 1:1`, `ShipsMoored=0`): 8 of 8 frames byte-equal in every round, the
  module linked and dropped; no ship drawn, as stock (4980's mask pair).
- **On, one ship (r1 09:49-09:56, r2 10:00-10:06, r3 10:20-10:26):** each look draws the trireme in the bay north-east of
  the city's centre (the city plot's water is north and north-east here); `--dumped --compare` with the flag on differs
  in all 8 frames (r1 3,918-4,000 pixels, r2/r3 2,561-2,603 of 3,603,694: the same ship in every frame, MC #7615). At
  the named camera the on pictures differ from off in 3,863 (a), 3,694 (b), 4,181 (c), 2,507 (d) pixels (r2 = r3 byte
  for byte: one ship takes no fallback). r1 (footprint 0.35 / 0.15): look (b) had its stern on the beach; r2 with 0.5 /
  0.2: (b) lies along the bay's south shore at the city's edge, (a), (c), (d) a little farther out. The carrier bundle
  (no ship in a city) with the flag on: P1 8 of 8 byte-equal in every round.
- **Two ships (r2syn 10:08-10:12, r3syn 10:17-10:20)**: a synthetic copy (`synmove.py`, scratch: carrier 1067's record
  moved into the city's plot; DIAGNOSTIC, its dumps no longer match): **off**, the listed carrier is drawn by the server
  at the city plot's centre, on land, and the hidden trireme not at all [L, synthetic: what stock does with a listed ship
  in a city, 4984.1]. **On**: (a), (c), (d) moor the two side by side in the bay; the carrier at stock scale is far longer
  than the footprint (about two inner radii) and reaches the beach at its stern [L]: the footprint fits ships up to about
  a frigate. (b) end to end does not fit the bay: r2syn (before the fallback) drew both on land at the centre; r3syn (the
  one-by-one fallback) puts the trireme 1.6 inner north and the carrier on the bay's east shore, far apart.

| Round | Off | (a) BowToLand | (b) AlongShore | (c) OutToSea | (d) BowToLandSmall |
|---|---|---|---|---|---|
| r2 one ship (stock reference, mock and sheet attached) | vc-20261120-100804-workstation-32a8 | vc-20261120-100806-workstation-6ee4 | vc-20261120-100808-workstation-8e4f | vc-20261120-100809-workstation-5799 | vc-20261120-100810-workstation-7768 |
| r3syn two ships (synthetic) | vc-20261120-102712-workstation-013a | vc-20261120-102713-workstation-047f | vc-20261120-102714-workstation-ff83 | vc-20261120-102715-workstation-fdd5 | vc-20261120-102716-workstation-e5cf |

**The worker's recommendation for the owner: (a) BowToLand** (C1's recommendation and the default): the row fits small
bays, the ships stay near the city, and a row reads as a harbour. (c) is the same geometry with the bow out (the
sails face the default camera more, so the ship reads larger); (b) needs the most room along the shore and, with two
ships, often falls back to scattered spots; (d) is small beside the city's figures. What the city's buildings and the
EXE's flag do beside a moored ship is not seen offline (no cities in the bundles): a live picture pair is the lead's step.

### 4984.7 Cost (E8) [indication only, PERFORMANCE.md; no lock held, nothing live]

- CPU: `moored_cost` (a 10,000-unit build with 400 port plots of two ships each, one hidden; 20 builds per arm after a
  warm-up; clang-cl /O2; the workstation with other teams' work running), without the module / with ShipsMoored on, four
  runs 2026-11-20 09:45-10:20 EST: 6.94 / 4.96, 5.33 / 5.23, 3.81 / 5.55, 3.55 / 4.77 ms. The runs disagree in sign; the
  on arm also lists 400 more units. The extra's own work per frame: one pass over the table, a sort of the port ships, and
  per port a search of at most 11 distances x 7 directions x (row size) footprints of 5 height samples (fewer: the first
  fit ends it); per unit a binary search. Nothing is allocated per frame once the vectors have grown.
- GPU: a shown ship is one more unit drawn (as any unit); a moved one costs nothing more. The seam's turn is one 3x4
  product per bone row triple (4981.4).
- Memory: the vectors of port ships and the row (a few dozen bytes per port ship).
- Not measured: frame time and memory on the product path (a live arm: the lead's step).

### 4984.8 E7: foreign ships [C, code; no picture]

No rule of its own, as GarrisonVisible (4983.2): the unit host's fog filter keeps another team's unit only on a plot the
active team sees now, and the seam never shows another team's invisible unit (a submarine in a foreign city stays
hidden unless the active team sees its invisibility type, the DLL's own `Visibility` rule). The game's unit flags show
a foreign city's units the same way. The kit has the active player's cities only: not pictured.

### 4984.9 Not verified

- *(Done 2026-11-23, 4984.10: the product path with the EXE's city buildings, E1 and E8 indications live; the shadow
  and the flags in a city still not seen. Was: "Nothing live: the product path (the EXE's city buildings and the ship's
  flag beside a moored ship, the shadow: none for a shown ship, the EXE's at the plot centre for a moved listed one, P2),
  frame time and memory there: the lead's step.")*
- *(Answered 2026-11-23, 4984.10: stock draws a canal-fort ship at the plot centre on land; recommendation: keep "any
  land plot".)* **Stock's drawing of a ship on a canal fort, citadel or feitoria is not checked** (the extra moors it like a city's,
  4984.1; lead-0455's scope point, 2026-11-20 10:31, choice (2)). No city table reaches the units seam: the feed's plot
  city bit is never set, the city events carry plot -1, and the cities and landmarks scenes keep no city plot list
  (`server/src/render/cities/`, `landmarks/`, grep 2026-11-20 10:35); limiting the rule to cities needs a seam input
  (a hunk agreed with lead-0042). **For the live step:** stage a ship on an owned coastal fort used as a canal and take
  a stock picture and the flag on/off pair: if stock draws it at the plot centre, decide with the lead whether a canal
  ship stays moored (as now) or the seam gets a city input.
- A ship sailing into or out of a city
  (walking: unit-tested only), a ship in a fight, a foreign city's ships, a real city with two or more ships (only the
  synthetic copy), a city on a lake, a port on the field's edge (4984.2): not pictured.
- Ships larger than the footprint (a carrier, a battleship) may reach the beach (r3syn); the extra has no per-type size.
- The native suite was not run under MSVC ASan (`build_units_extras_tests.py` has no ASan variant).

### 4984.10 The live step (impl-4984, account B, 2026-11-23; branch `task-4984-ships-moored` from main a848a0023)

What it is for: the looks on the product path for the owner's pick (with the EXE's city buildings, the interface and the
ship's flag, which offline cannot show: the kit bundles record `cities 0`), a city with two ships, the canal-fort check of
4984.9 against what the game itself draws, E1 (statscmp on/off from one save) and E8's live indications. Same method as
GarrisonVisible's live run (4983.12, 4983.13); the files are in `server/evidence/0455/4984/`:
- `look_scratch.py` makes a SCRATCH copy (`git archive` of the branch) whose `MakeShipsMoored` reads
  `T4984_MOORED_LOOK` (0 a, 1 b, 2 c, 3 d; unset = the product's default, look (a)); never committed to the product.
- `smoke_4984.sh src|build|dry|check|prep|arm|e1|statscmp|end|stock` (the header lists every step): the server built
  from the scratch copy on the 0.1b pins (release, no LTO) plus this checkout's composite, laid by `lay.py prep` (backup
  by SHA-256) and `helper on`; CP's `NewCustomModOptions.xml` backed up by hash and `SQLITE_LOGGING` 1 for E1; the kit
  save copy `C5H-4984-KIT_0009` (74fc7428...). Per picture arm (off, a, b, c, d) one launch: shots at Raiatea (54,19)
  after the load (interface hidden), then the staging by Lua (`arm_4984.py stage`), then shots at Raiatea and at the
  fort, interface hidden and shown. `stock` runs after `end` (the layout restored, the patch off): the same staging and
  shots on the instance as provisioned, the reference for both the city and the fort.
- **The staging** (`arm_4984.py` `STAGE_LUA`): a second ship made by `InitUnit` in Raiatea beside its garrison trireme
  1062 (the first of `UNIT_WORKBOAT`, `UNIT_CARGO_SHIP`, `UNIT_GALLEASS` the database has); then on the first free land
  plot of (53,21), (54,20) (north of the city, two water neighbours each by the kit's map dump), the plot given to the
  active player if it is not theirs, `IMPROVEMENT_FORT` set and a trireme made on it. `CvLuaPlayer::lInitUnit` calls
  `CvPlayer::initUnit` with no `jumpToNearestValidPlot` [C, the x64 fork at the 0.1b pin, `CvLuaPlayer.cpp` 1679], so a
  ship made on a land plot stays there; the answer checks the trireme's plot and ends non-zero otherwise.

**The run [L, 2026-11-23 06:16-07:3x EST].** Instance c5h-new-b (C:, Goldberg, EXE build 26577625 checked), claimed
06:03 by impl-4984 under MC-B's grant #11231 after take 6's barrier (HOLD OFF 06:02). Layout built 06:05-06:15 from a
scratch copy of this branch at 450d0eb0a (= main 978ccc45d + the driver): server.exe 87b905800ee22b24..., x64 DLL
b84aa3e90f51bbf5... (0.1b pin), proxy 8875d3a9c9ab913c..., composite fcda5bca8c5a4076...; options per arm
`[Renderer] ServerUnits = 1`, `[Extras] EnableVisualAdditions = 1`, `ShipsMoored = 0|1`, FixAirBases at its default 1.
Pictures: the harness's PrintWindow capture, 2554 x 1411, DIAGNOSTIC (D112: a lay.py layout and a scratch server
build), popups closed (D114), camera on the plot at zoom min-2. Files `F:/c5-64bit-work/4984/out1/<arm>/` (not in git).

- **A harness stop fault [L].** The first series stopped after the off arm (06:26): `cli.py stop` raised
  `psutil.TimeoutExpired` in `launch._terminate_after_gates`: the EXE was still alive 15 s after `TerminateProcess` and
  15 s after the kill (the second `proc.wait(15)` is not caught), and ended by itself within the next minute (the next
  `stop` reported "exited by itself"). The saves had settled before the terminate (the harness's gates). `smoke_4984.sh`
  now waits 60 s and stops again once; arm b met the same fault (06:43) and passed on the retry. `end` restored the
  instance by hash after the first series (restore ok, provision verify 0); the rerun (arms a-d, E1, stock) used a fresh
  work folder. The off arm's pictures and lines are from the first series (same layout and save).
- **The staging answered the same in every arm:** `OK second UNIT_WORKBOAT 1071 at 54,19; sea units in the city 2; fort
  at 53,21 owner 0 improvement 14 (fort 14) water neighbours 2; trireme 1072 at 53,21`.
- **Every picture arm: no box, 0 LOUD lines** in render.log, server.log and vpproxy.log, nothing from `crash.py
  collect`. Extra's line: off `units ShipsMoored: off: ShipsMoored = 0 (from the file)`; a-d `units extra ShipsMoored:
  in effect`. Last `units 1s` after the staging: table 46 (the kit's 44 + 2 made), **listed 35 off, 36 on** in each of
  a-d: the garrison trireme 1062 is hidden by stock and shown by the extra; the work boat 1071 and the fort's trireme 1072
  are listed by stock already (drawn at their plot centres, on land) and moved by the extra. Arm a's counters: `x shown=4131
  root=10563 heading=10563`, `extras ShipsMoored on ov=10563 shown=4131 failures=0`.

| Arm | load port | staged port | staged fort | staged port, interface shown | staged fort, interface shown |
|---|---|---|---|---|---|
| off | vc-20261123-062211-workstation-ec3d | -062326-...-6df8 | -062352-...-5a24 | -062433-...-6723 | -062510-...-ef28 |
| a | vc-20261123-063434-workstation-d93d | -063506-...-ed52 | -063523-...-389d | -063542-...-27d8 | -063559-...-81c8 |
| b | vc-20261123-064023-workstation-dbc2 | -064101-...-04ea | -064123-...-6975 | -064147-...-688f | -064213-...-86aa |
| c | vc-20261123-064920-workstation-e877 | -065032-...-7fc8 | -065055-...-2f3c | -065122-...-b002 | -065147-...-857d |
| d | vc-20261123-065647-workstation-3583 | -065732-...-3015 | -065754-...-3b48 | -065816-...-7a1d | -065837-...-ea79 |

What the pictures show (crops around the plot, `live_sheet.py`; sheets `F:/c5-64bit-work/4984/out1/live_*_sheet.png`)
[L, by eye]:
- **Off (the server copying stock):** at the load no ship at Raiatea (the garrison trireme hidden; the worker's figures
  stand among the houses). After the staging the work boat is drawn at the city's centre among the houses (a ship on land)
  and the worker's figures are gone (the DLL's `updateCenterUnit` hides the non-centre units when the plot's units change,
  as for aircraft in 4985); at the fort the trireme stands on land inside the fort's walls.
- **(a) BowToLand:** at the load the trireme lies in the bay north-east of the houses, bow to the city; after the staging
  the trireme and the work boat lie side by side there, bows to the city, close to the houses; the fort's trireme lies on
  the water east of the fort, bow to the walls.
- **(b) AlongShore:** the row of two does not fit (4984.2's fallback): the ships stand apart and farther out in the bay
  (one near the bay's mouth); the fort's trireme lies alongside the fort's east wall, and the city's second ship appears
  at the far east of the fort view. Hull side on to the camera, the sails read largest.
- **(c) OutToSea:** as (a) with the bows out to the bay.
- **(d) BowToLandSmall:** as (a), smaller and a little nearer the houses.
- **Interface shown:** the city banner sits above the houses; in every arm the cursor's tooltip (a policy list the harness
  leaves at the window's centre after the zoom, as 4983.13) covers the bay and the ships, so the shown-interface shots do
  not show the ships' flags. At the fort (tooltips cover part of it too) the trireme's unit flag stays above the fort's
  centre while the moored ship lies on the water east of it (the EXE places the flag at the plot; OPTIONS.md says the
  flag stays at the city).

**The stock reference [L, 07:08-07:18].** After `end` (layout restored by hash: `lay.py restore` ok, XML back, saves
back, provision verify 0) one launch of c5h-new-b as provisioned (EXE 26577625, its own 32-bit DLL, no patch) with the
kit copy and the same staging (the same answer). Shots: load port vc-20261123-071240-workstation-f79c, staged port
-071319-...-3fde, staged fort -071337-...-b87b, shown interface -071359-...-b2d7 / -071427-...-3c56. **Stock draws
exactly what the server draws with ShipsMoored = 0** [L, by eye]: no ship at Raiatea after the load (the worker's
figures among the houses); after the staging the work boat at the city's centre among the houses, the worker's figures
gone; **the trireme on the canal fort stands on land inside the fort's walls, at the plot centre**. So 4984.9's question
is answered: stock has no water-side drawing for a canal ship either; with the extra on it is moored on the water beside
the fort like a city's ship. **Recommendation (for the MC, minor): keep "any land plot"** (a ship on land inside a fort is
stock's oddity, the moored ship reads better; telling forts from cities would need a seam input, 4984.1).

Sheets with the stock arm first, recorded in Visual Checks with the stock picture of the same camera as the reference
(`vc_sheets.sh`): load port vc-20261123-072022-workstation-09d3, staged port -072024-...-2538, staged fort
-072026-...-190c, staged port interface shown -072027-...-a78f, staged fort interface shown -072029-...-d84f.

**E1 PASS [L, 07:01-07:24].** `smoke_4984.sh e1`: `perf/profilekit/acceptance.py run --config inc2 --no-ledger`, the
kit copy `C5H-4984-E1K_0009` autoplayed turn 9 -> 19, ShipsMoored 1 (look a; 07:01-07:07) then 0 (07:19-07:24, after
a fresh prep: `e1off_4984.sh`), both windowed on c5h-new-b (D164). Both acceptance runs exited 1 only because their final
stop met the harness fault above, after the autoplay had reached turn 19 and the statistics log was written (each run's
`per-turn.csv` has turn 19; `acc/run.log` ends in the same `psutil.TimeoutExpired`). `tools/d10/statscmp.py e1on/stats.db
e1off/stats.db --from-turn 9 --expect-turns 9 19`: **IDENTICAL, 15 tables, 2,509 rows**, turns 10..19 in every per-turn
table (`F:/c5-64bit-work/4984/out1/statscmp.txt`).

**E8 live indications [L, one run per arm, no lock, workstation busy (take 6's and other teams' work): not a claim].**
From the acceptance runs' `per-turn.csv` (samples at turns 9, 11 or 12, 19): server.exe private bytes on 1,137.0 /
1,155.4 / 1,173.3 MB, off 1,122.2 / 1,138.8 / 1,155.6 MB (+15 to +18 MB with the extra on, within one-run noise not
established; the kit holds one ship in a city); the EXE's private bytes 1,319-1,355 on against 1,331-1,359 off; the ten
turns took 24.4 s on and 23.2 s off (elapsed from the first sample).

**Clean-up [L].** `end` after each series: the game stopped, front end restored, CP's XML back by hash, `lay.py restore`
"restore ok", the saves back as snapped (the kit copies removed), `provision.py verify --quick` 0; the stock run snapped
and restored the saves itself. No dialog box at any point (`dialogwatch.py marker` 0 at the end). c5h-new-b released
07:25 (desk 0136 #11365). Left behind on purpose: the instance's `cache/stats.db` (a run output, as 4983.13).

*(Superseded 2026-11-24 by the owner's pick, D250: value 1 = look (d), value 2 = look (b), default 0; 4984.11.)*
**Recommendation for the owner's pick, unchanged: (a) BowToLand** (the default): live, the row of two fits the bay close
to the houses and reads as a harbour; (b) falls back to scattered spots with two ships; (c) is (a) turned; (d) is small
beside the city. The default stays 0 (D207).

Not verified live: a ship sailing into or out of a city (walking), a ship in a fight, a foreign city's ships, a city on a
lake, the ships' unit flags in a city (the tooltip covered them), the server's shadow of a moored ship pixel by pixel;
ACCEPTANCE pictures on the installer-laid system (these are DIAGNOSTIC).

### 4984.11 The owner's pick in the product: the value is the look (D250; impl-4984, 2026-11-24, branch `task-4984-ships-moored-d250`)

D250 (the owner, 2026-11-24, after the sheets of 4984.10): [reworded for privacy] look (d) default off, and look (b) as an .ini option. So `[Extras]
ShipsMoored` is 0-2: **0 = off (default, D207), 1 = look (d) `BowToLandSmall`, 2 = look (b) `AlongShore`**; (a) and (c)
are not offered.

**Code [C, unit tests].**
- `options/src/vp64_options.cpp` row 25: range 0-2, help lines `0 = off (default).`, `1 = bow to the city, drawn a little
  smaller; several ships side by side.`, `2 = alongside the shore; several ships end to end.` (the installer's page reads
  them as three radio buttons). `options/include/vp64_options.h`'s comment on `kShipsMoored` (still "1 = ships in a city
  drawn moored") is outside 4984's touches (0042 holds the header): left for the MC.
- `srv_unit_moored.h`: `MakeShipsMoored(id)` (the product's maker in `UnitExtraMakers()`, unchanged entry) reads the look
  in `configure` from `Vp64Opt::Extra(o, id).value` through `MooredLookOfValue` (1 -> d, 2 -> b, anything else false:
  configure fails with "ShipsMoored = N names no look", which the range makes unreachable); `MakeShipsMoored(id, look)`
  keeps a fixed look for the tests (no default argument any more). The extra logs its look once after its first frame
  through `takeNote`: `units extra ShipsMoored: look (d) BowToLandSmall (ShipsMoored = 1)`. `MooredLookOf` gives a made
  extra's look (tests). The enum's numbers are not the option's values.
- `srv_units_extras.cpp`'s comment "Each look is its maker's default argument until the owner picks" is now stale for
  ShipsMoored (outside the touches; for the MC).

**Tests [L, 2026-11-24, workstation].** `test_ships_moored.exe` 8 of 8 (new case `moored_values`: the real extra on row 25
configured from option texts: no line and `= 0` off with one line and the scene as stock; `= 1` placed at
`PlanMooredRow(..., BowToLandSmall)`'s slots, yaw pi on the south side, scale 0.8, roots 0.4 inner apart; `= 2` yaw pi/2,
scale 1, 1.1 inner apart; switch 0 off; a fixed-look extra keeps its look; the product's maker list with `= 2` logs
look (b)); `test_units_extras.exe` 13 of 13; `test_options` x86 and x64 945 passed each on a COPY of docs/OPTIONS.md
carrying `server/evidence/0455/4984/OPTIONS-md-entry.txt` (the row test now checks each row's range and every value in
it); options ASan OK; the installer's `test_options_schema.py` 22 OK (`ShipsMoored = 0 (radio 0, 1, 2)` under
`EnableVisualAdditions`, the value lines' captions).
**Found on main [L]:** `test_options_schema.py`'s section pin `['Renderer', 'Performance', 'Proxy', 'Extras']` failed on
main since 0680's `[Shell] WindowTitlePrefix` (964849f24; the same failure on a `git archive` of main, 2026-11-24 15:0x);
the pin now carries `Shell`.

**Every builder [L, 2026-11-24 14:56-15:35 EST, workstation, `server/evidence/0455/4984/allb_4984_d250.sh o|1|2|3` and
`allb_4984_d250_asan.sh`, each through `quietlock.py run`].** Runbook 2.10's 18 builders of `options/src/vp64_options.cpp`
and the 3 of `server/src/extras/units/`: `server/build_server.py` release on the 0.1b pins (206 s; it builds the proxy, so
`proxy/build_proxy.py` with it), `build_units_extras_tests.py` (test_ships_moored 8/8, test_units_extras 13/13),
`build_units_tests.py` + `test_units_runner.py` 7 passed, `build_unitfeed.py`, `build_sceneshot.py --smoke`, farm fields,
forest, fow, overlay scene, scene bundle, scene shadow, `build_render_tests.py`, `run_extras_test.py` (0 parts failed),
`run_hidden_embark_test.py` PASS, `test_options_boot.py` 13/13 (ASan by default), `test_proxy_rt.py` 235/235,
`shell/build.cmd` BUILD OK, `render/build.py` (the c5render the others link); `test_ships_moored_scenes.py` 5 passed (the
E9 picture with `ShipsMoored = 1`, now look (d), still differs from off by one ship's pixels within 500-60,000);
`tools/harness/tests/test_safety_static.py` 55 passed. ASan: options `test_asan.py` OK, `build_units_tests.py --asan`,
`run_hidden_embark_test.py --asan` PASS, `build_sceneshot.py --asan --smoke` OK (the ASan route that compiles
`srv_unit_moored.cpp`; `build_units_extras_tests.py` has no ASan variant).
**Found on main [L]:** `run_extras_test.py --asan` fails to COMPILE under MSVC: `server/tests/extras/test_extras.cpp`
lines 672-673, `error C3493: 'W' cannot be implicitly captured because no default capture mode has been specified` (a
lambda `[rec]` reads the local constants `W` and `H`; clang-cl accepts it, MSVC does not). The file is as on main (last
change ca59a386d, 4940 part 2, 2026-11-22), so `run_extras_test.py --asan` has been red since then; not 4984's file
(outside its touches): for the MC (fix: capture `W, H` or make them `static const`/`constexpr`).
**Fixed by ticket 0726 (2026-11-24) [C, L].** The cause is MSVC `cl`'s lambda processor, not ASan: `cl` 14.43.34808
in `/std:c++14` without `/Zc:lambda` (or `/permissive-`) asks for a capture of a non-static local constant that a lambda
names, though the standard lets a lambda use such a constant uncaptured (it is not odr-used); clang-cl (the plain build of
`run_extras_test.py`) accepts it. A reduced repro (`[x]` lambda reading `const uint32_t W = 8`) fails C3493 under `cl`
with and without `/fsanitize=address`, and compiles with `/Zc:lambda` or with `static const` [C, 2026-11-24]. The
introducing commit is ca59a386d (4940 part 2, `ProduceDense`), as the blame of the `w = W; h = H;` lines shows. Fix:
`static const uint32_t W = 8, H = 8;` in `ProduceDense` (no capture needed for a static; capturing `W, H` instead would
trip clang-cl's `-Wunused-lambda-capture` under `/W4 /WX` [I, not tried]). What the test checks is unchanged. Results [L, 2026-11-24 18:0x, workstation]:
`run_extras_test.py` plain OK (unit tests, injected failure, road producer: 0 parts failed); `--asan` OK (canary caught,
`test_extras_asan.exe` and `test_roads_producer_asan.exe` exit 0 with no AddressSanitizer report). Trap for any test compiled by both: a lambda that names a
local constant must capture it or the constant must be `static`.

**Live [L, 2026-11-24 15:02-15:12 EST].** c5h-new-b (EXE 26577625, Goldberg, mods548) under MC-B's YES (desk 0136,
START-REQUEST #12979), claimed 14:59, released 15:12. Layout: `server/build_server.py --config release` of this branch
(d2adb9b18 + the driver commit 5f36f0ce4) on the 0.1b pins, the composite of this checkout, laid by `lay.py prep` and
`helper on` (`smoke_4984.sh layout|dry|check|prep|arm v1|arm v2|end`; the arms need no scratch copy and no `--env`). The
same kit copy and Lua staging as 4984.10 (the staging answered as then: work boat 1071 in Raiatea, fort at (53,21),
trireme 1072 on it).
- Arm v1 (`ShipsMoored = 1`): server.log `units extra ShipsMoored: in effect`, then `units extra ShipsMoored: look (d)
  BowToLandSmall (ShipsMoored = 1)`; vpproxy.log `extras: ShipsMoored: in effect (ShipsMoored = 1 from the file; ...)`.
- Arm v2 (`= 2`): `look (b) AlongShore (ShipsMoored = 2)`, `in effect (ShipsMoored = 2 from the file; ...)`.
- Both: 0 LOUD lines in render.log, server.log and vpproxy.log, no box (dialogwatch marker none), crash collect nothing;
  the last `units 1s` after the staging `table=46 listed=36` (as 4984.10's arms a-d).
- Pictures [by eye]: v1 = 4984.10's (d): the two ships small, side by side, bows to the houses in the bay north-east of the
  city, the fort's trireme on the water east of the fort; v2 = 4984.10's (b): the end-to-end row does not fit the bay, the
  fallback puts the ships apart (one near the bay's mouth), the fort's trireme alongside the fort's east wall.
  Per-value sheets beside the stock reference of 4984.10 (`value_sheet.py`, `vc_values.sh`): v1 vc-20261124-151315-workstation-36a5,
  v2 vc-20261124-151315-workstation-58a9; the single shots v1 vc-20261124-150532..150657, v2 vc-20261124-151015..151127.
  Files `F:/c5-64bit-work/4984/out-d250/` (not in git). DIAGNOSTIC (D112).
- End: game stopped, CP's XML back by hash, `lay.py restore` "restore ok", saves back as snapped, `provision.py verify
  --quick` 0.

**Rule A: no run, and why.** The default stays 0 and value 0 takes the same path as before (the module never calls the
extra's configure; the off line is unchanged), so a product build draws nothing new by default; no bundle in the library
sets `ShipsMoored`. Only the meaning of `ShipsMoored = 1` changed (look (a) -> (d)), which the E9 scene test
(`test_ships_moored_scenes.py`, run with `= 1` on the c4 port bundle) covers. MC-B agreed (desk 0136, 2026-11-24 14:5x);
the MC decides at merge.

Not verified: the installer-laid system (ACCEPTANCE pictures); the flags of the moored ships (4984.10); look (b)'s row on
a coast wide enough for two ships end to end (Raiatea's bay is not).

## 4982 FixUnitPlacement (W7 of team-0455, 2026-11-20; option id 23, bugfix, default 0 until the owner accepts)

Branch `w7-4982` (from `epic-0455-extras-b`). Phase 1 (the gate, brief `server/evidence/0455/briefs/w7-4982.md`): is any
drawn figure inside rock or a wonder model? Offline only.

### 4982.1 Phase 1: method

- **What the placement does** [C, `render/src/units/placement.cpp` `placeUnit`]: every living figure's model origin is
  its slot (the formation, turned by the facing) around the root, at `z = max(heightAtWorld(x, y), water) + ZOffset`,
  where `heightAtWorld` is the height of the one sample at the truncated column and row (no interpolation); the figure
  stands upright (yaw only, no tilt). So on a slope the origin is at a sample's height while the model's body spans
  several samples up and down the slope.
- **The measure** (scratch tool, own code, built against `c5render` of this branch): the bundle's unit feed through
  `unitfeed` (`.c5units` per frame), each frame prepared by the module's CPU path (`UnitRenderer::prepare`: art, pose,
  placement, palette); every vertex of every drawn figure posed as the vertex program does (the 28-byte stream's half
  positions, weights and bone indices against the block's bone rows; the 20-byte stream against cb0's world rows) and
  compared with the terrain under it (the bundle's height field, bilinear between samples). Per figure: the origin
  against the sample it took and against the surface at its (x, y); the share of its vertices below the surface and the
  deepest one (sunk), the lowest vertex's height above the surface (floating). Two frames per bundle (MC #7615).
- **Landmarks**: the 4980 bundles were recorded with `render_landmarks 0`, `render_cities 0` (the EXE draws them), so no
  wonder geometry is in a bundle; natural wonders on mountains are terrain (Fuji's file is an effect anchor only,
  `landmarks-and-models.md`); world wonders stand in the city plot (54,15) and the Great Wall around it. The figures'
  feet are put against the plots (the city plot or a neighbour) and against the stock picture of the same camera.

### 4982.2 Phase 1: results (2026-11-20 09:50-10:10 EST, workstation, offline) [C: computed by the module's own CPU path; L for the stock pictures named]

Inputs: `unitfeed` (built from this branch, `server/tools/unitfeed/build_unitfeed.py`) on the bundles
`C5H-4980-C1MOUNTAIN_0009_20261120-032355`, `C5H-4980-C2ANATURAL_0009_20261120-030410`,
`C5H-4980-C2BWONDER_0009_20261120-032950` (c5h-c's install and mods) and `C5H-0301-HUGE_0350_20261119-214920` (c5h-e's);
each frame's `.c5units` prepared by the scratch tool `figdepth` (own code, `UnitRenderer::prepare` of `c5render` built
from this branch, not committed) with the bundle's database and height field. Every frame of each bundle was measured;
two frames per case are shown (first and last of the span). **Check of the method:** on flat ground (origin slope < 0.05)
the lowest posed vertex of a figure lies at the surface (median -0.02 to -0.09 units over 296-640 instances per bundle),
as the feet should. Instances with >= 95 % of their vertices below the ground are props the pose keeps under it (VP's
swordsman `Shovel` mesh, 41 units down) and are left out.

Plot relief (the height field inside radius 28 of the plot centre, `plotrelief.py`): the c1 mountains (49,13), (49,14),
(48,14) relief 23.5 / 29.6 / 34.7 units, mean slope 0.80-0.93 (dz per unit run), max 2.1-3.0; Mount Kailash (38,12) 29.2;
the huge save's (87,41) (X0.4's conquistador) 29.7; hills of the huge view (106,21), (110,23), (106,32) 12-15; flat
plots 0-3.5. Columns: instances = drawn mesh instances (a horseman's figure is a rider and a horse); "worst share
below" = the largest share of one instance's vertices more than 0.25 units below the surface; "deepest" = the lowest
vertex against the surface under it; "deepest / height" = that depth over the instance's own height.

**c1_mountain** (units on the three mountain plots):

| frame | unit | type | plot | instances | with a vertex > 1 below | worst share below | deepest (units) | deepest / height | steepest slope | origin - surface |
|---|---|---|---|---|---|---|---|---|---|---|
| 1925 | (0, 1051) | HORSEMAN | (49,13) | 10 | 5 | 16% | -7.40 (horse) | 41% | 1.43 | -0.33 .. +0.91 |
| 1976 | (0, 1051) | HORSEMAN | (49,13) | 10 | 5 | 16% | -7.38 (horse) | 41% | 1.43 | -0.33 .. +0.91 |
| 1925 | (0, 1052) | SPEARMAN | (49,14) | 19 | 12 | 16% | -5.58 | 23% | 1.75 | -1.04 .. +0.50 |
| 1976 | (0, 1052) | SPEARMAN | (49,14) | 19 | 11 | 16% | -5.57 | 23% | 1.75 | -1.04 .. +0.50 |
| 1925 | (0, 1053) | CATAPULT | (48,14) | 6 | 3 | 34% | -7.97 (an operator) | 54% | 2.46 | -0.23 .. +2.57 |
| 1976 | (0, 1053) | CATAPULT | (48,14) | 6 | 3 | 34% | -7.97 (an operator) | 54% | 2.46 | -0.23 .. +2.57 |

**c2a_natural** (Mount Kailash (38,12) is terrain; the units on its neighbours):

| frame | unit | type | plot | instances | with a vertex > 1 below | worst share below | deepest | deepest / height | steepest slope | origin - surface |
|---|---|---|---|---|---|---|---|---|---|---|
| 1951 | (0, 1054) | SWORDSMAN | (37,11) | 12 | 8 | 5% | -2.15 | 12% | 1.18 | -0.82 .. +0.58 |
| 2007 | (0, 1054) | SWORDSMAN | (37,11) | 12 | 8 | 5% | -2.14 | 12% | 1.18 | -0.82 .. +0.58 |
| 1951 | (0, 1055) | HORSEMAN | (38,11) | 10 | 1 | 3% | -1.71 (horse) | 9% | 0.23 | -0.08 .. +0.17 |
| 2007 | (0, 1055) | HORSEMAN | (38,11) | 10 | 1 | 3% | -1.71 (horse) | 9% | 0.23 | -0.08 .. +0.17 |

**c2b_wonder** (flat ground; settler and general in the city plot (54,15), the others on its neighbours): every
instance's lowest vertex within 0.23 units of the surface, none more than 1 below, in frames 2064 and 2120 (settler
1049 -0.23, general 1050 +0.08, swordsman 1056 -0.03, horseman 1057 +0.08, catapult 1058 -0.17).

**Huge bundle** (frames 1196 and 1243): no drawn unit stands on a mountain plot in its view (the conquistador of X0.4 at
(87,41) is not in the drawn list). Units with a vertex more than 1 below: on hills (106,21), (110,23) the rifleman and
Great War infantry figures 2.1-2.8 units (10-22 % of their height), 3-4 % of the vertices; on gentle ground deeper
vertices of a gatling gun (-8.6), a worker (-20.4) and a machine gun's callout mesh (-4.2 in one frame only) [I: tool
and callout props of the pose, as the shovel, not the body]; an ironclad and a great admiral's ship 2.1-3.2 below the
sea bed under them (shallow water, ships are 4984's).

**The conquistador on (87,41)** as an indication [I: its seed and facing are not in the drawn frames]: a synthetic scene
(`synth.py`: player 2, facings 0-5 x seeds 11, 211, 4011, plot centre, default clip) measured the same way: in all 18
the horse's lowest vertex is 4.7-12.5 units inside the mountain (24-64 % of its height), 8-21 % of its vertices below,
which matches X0.4's [L] stock picture (horse's legs in the rock).

**Wonders [L, the 4980 stock shot of c2b `stock_c2b_wonder.png`, by eye on 3x crops, `F:/c5-64bit-work/0455/4982/look/`]:**
the catapult's operators of (54,16) stand on the Great Wall's line at the city's north; at the north-west tower one
operator's lower body is hidden by the tower (inside it or behind it: not decidable offline, the bundles hold no wonder
geometry and no stock depth). The swordsmen stand in front of Stonehenge's stones, not in them; the Pyramid and the
settler and general in the city centre are apart. (The Great Wall itself runs through Stonehenge's circle: wonder
against wonder, the city layout, not units.)

**A trap met on the way [L, 2026-11-20 09:52 EST]:** `unitfeed --mods-auto` on the huge bundle (c5h-e's install) left VP's models out (`statuses: AnimFailed 151; 11 LOUD lines`, `.fxsxml not found` for VP's destroyer, mamluk, dreadnought ...) although the files are in c5h-e's MODS folder; the same run with the three folders named (`--mod .../(1) Community Patch --mod .../(2) Vox Populi --mod .../(3a) VP - EUI Compatibility Files`) gave `statuses: none; 0 LOUD lines`. On the 4980 bundles (c5h-c) `--mods-auto` gave 0 LOUD lines. Name the mods when the bundle's instance is not the default one [I: `--mods-auto` takes `srvrender::ModFolders()`, not the bundle's `render_decal_mods`; not read further].

**Verdict (phase 1):** on mountain plots every unit has figures partly inside the rock: up to 34 % of an instance's
vertices and up to 54 % of its height (the downhill parts float: origins up to 2.6 units above the surface, the
truncated sample of a 2.5 slope); none is wholly inside (no instance with more than 34 % below; the conquistador
indication 21 %). On hills the same effect is small (2-3 units, about 10-20 % of a figure's height); on flat ground
none. The cause is the placement rule itself (one sample under the origin, an upright figure, no tilt), which the
server takes over from the EXE exactly, so stock shows the same (4980 c1 and X0.4 [L]). For wonders, no figure is
shown inside a wonder model; one catapult operator at the Great Wall's tower is undecided offline.

### 4982.3 Not verified (phase 1)

- Whether the catapult operator at the Great Wall's north-west tower (c2b) is inside the tower or behind it: no wonder
  geometry and no stock depth in the bundles. Out of 4982's scope since lead-0455's phase-2 word (2026-11-20 10:11,
  queue #8094: scope narrowed to terrain).
- The drawn terrain is the EXE's smoothed tessellation of the samples (terrain-surface.md: mip 1 at LodStep 2 at the
  default zoom, about 0.05-0.3 units off the samples on average); the measure uses the bilinear surface, so depths under
  about 0.5 units are within that difference. The depths reported (2-12 units) are not.
- The conquistador on (87,41) was measured on a synthetic scene (seed and facing assumed); the huge bundle's view does
  not hold it.
- Which deep vertices on gentle ground (the huge bundle's gatling gun, worker, machine-gun callout) are props of the pose
  rather than bodies was judged by mesh name, not checked in a picture.


### 4982.4 Phase 2: the rule, scored before any product code (W8 of team-0455, 2026-11-20 10:25-10:40 EST, workstation, offline) [C: computed by the module's own CPU path]

Scope (lead-0455 #8094, MC #8099): terrain only (mountains, hills). Own design (D83): nothing here comes from the EXE.

**Why a vertical rule alone cannot fix it.** The figures stand upright (no tilt). On a mountain plot the slope under one
figure is 1.4-2.5 and a horse's or a catapult's feet lie up to 5-6 units from its origin, so the ground under one figure
spans 7-12 units of height. Any rule that only chooses the figure's height trades feet inside the rock for feet in the
air: the "lowest foot on the surface" bound (posed vertices; not computable without the models) leaves no foot inside
but floats the downhill feet up to 23.6 units on c1 (stock: 15.7).

**Two parts, each scored alone and together:**

- **A, the formation moved to flatter ground on its plot** (the seam's root override, 4981.1; no hunk): candidate roots
  every 2 units within 10 units (0.31 of the hex's inner radius, 32.0 units, `plotCentre` of (0,0) and (1,0)) of the
  plot centre; the pick is the candidate with the least mean surface gradient (central differences +-0.5 units, bilinear
  surface) over the points of a 2-unit grid within 10 units of it; ties to the nearer candidate. Heights only: the extra
  has the height field, not the models or the formation. Other height-only criteria tried and dropped: surface range
  within 10 and 14, mean and worst local relief (range within 4) at 7 virtual slots (root + 6 at 5 or 7): worse on the
  synthetic conquistador (mean worst sink 7.6-10.4 against 4.9) and no better on c1.
- **B, each figure on the highest ground under its feet** (`max_r3`; needs a `render/src/units/placement.cpp` hunk, sent
  to lead-0455 2026-11-20 #8146, not written): a figure whose own sample is above the water stands on the highest bilinear
  surface among its origin, 12 points at 3 units and 6 at 1.5 units (instead of the one truncated sample under its
  origin). Also scored: radius 2, 4, 6, the mean of the origin and that highest point (`blend`), the bilinear surface at
  the origin.

**Measure:** `figrule` (scratch, own code; `figdepth` of 4982.1 extended): per drawn figure (instances grouped by the
nearest placed figure origin, props with >= 95 % below left out), the **foot** = vertices within max(0.5, 8 % of the
figure's height) of its lowest vertex; foot sink = the deepest foot vertex under the bilinear surface, foot float = the
highest foot vertex above it. A height rule is applied as a rigid vertical shift of the figure's posed vertices (the
placement's z is the figure's translation) [I: checked against the hunk's unit test once written]. A root is scored by
re-preparing the unit alone with `has_root` at the candidate. Cases: c1 frames 1925 and 1976 (horseman, spearman,
catapult on the three mountain plots), c2a frames 1951 and 2007 (Kailash's neighbours), the huge bundle's hills (106,21),
(110,23), (106,32) frames 1196 and 1243, the synthetic conquistador on (87,41) (6 facings x 3 seeds; an indication [I]).
"Worst" = over all frame-units; "mean" = the mean over frame-units of each unit's worst figure.

| case | rule | worst foot sink | mean worst sink | summed sink | worst foot float | mean worst float |
|---|---|---|---|---|---|---|
| c1 (6) | stock | 7.97 | 6.98 | 134.5 | 15.66 | 9.77 |
| c1 | B alone | 5.27 | 3.55 | 43.2 | 18.36 | 13.20 |
| c1 | A alone | 7.31 | 5.25 | 101.0 | 14.79 | 9.25 |
| c1 | **A + B** | **4.60** | **2.41** | **30.1** | **15.37** | **10.97** |
| c2a (4) | stock | 2.15 | 1.93 | 32.7 | 3.56 | 2.85 |
| c2a | B alone | 1.47 | 0.88 | 3.8 | 4.93 | 3.55 |
| c2a | A alone | 2.31 | 1.20 | 28.9 | 2.51 | 2.33 |
| c2a | **A + B** | **0.32** | **0.15** | **1.0** | **4.82** | **3.48** |
| hills (6) | stock | 8.59 | 4.38 | 93.5 | 3.65 | 2.11 |
| hills | B alone | 7.85 | 2.82 | 32.0 | 5.66 | 3.52 |
| hills | A alone | 7.67 | 4.14 | 94.6 | 3.30 | 2.00 |
| hills | **A + B** | **6.82** | **2.54** | **28.7** | **4.92** | **3.25** |
| synth (18) | stock | 12.54 | 8.61 | 522.6 | 15.73 | 10.74 |
| synth | B alone | 7.91 | 4.40 | 161.3 | 18.39 | 14.69 |
| synth | A alone | 7.31 | 4.90 | 329.1 | 11.48 | 8.18 |
| synth | **A + B** | **3.23** | **2.23** | **70.4** | **14.71** | **11.33** |

(The hills' worst 6.8-8.6 is the gatling gun's prop of 4982.2, not a body.) Per-figure rules alone on all figures of
the cases (stock -> `max_r3`): c1 figures with a foot more than 2 units in 28/38 -> 8/38; c2a 2/34 -> 0; hills 13/60 ->
4/60; synth 99/126 -> 32/126. A larger radius sinks less and floats more (`max_r4` with A: c1 mean worst sink 2.13,
mean worst float 11.48; `max_r6` alone: c1 mean dz +4.7 units, floats > 4 units on 36/38 figures). The limit of A: 6
units gives less (c1 mean worst 2.56 with B), 12 units no better than 10 (2.69). The best root from the posed figures
(not computable by an extra) reaches c1 mean worst sink 1.11 with B: the height-only pick leaves about half of that.

**Verdict:** A + B removes about three quarters of the sink on mountains (c1 summed foot sink -78 %, the synthetic
conquistador -87 %) and nearly all on Kailash's neighbours, and does not make the floating worse than stock in the worst
case (c1 15.4 against 15.7, synth 14.7 against 15.7); the mean worst float rises by 0.6-1.4 units. B alone floats
clearly more (c1 mean worst float +3.4); A alone is safe but weak. No vertical rule removes all sink without tilting the
figures (outside the seam and the placement's yaw-only figures).

Tools and data (scratch, not in git): `C:/c5-64bit-work/0455/4982/tool/figrule.cpp` (`build_rule.py`, `run_rule.sh
rules|roots`), `rulesum.py`, `rootsum.py`, `rootagg.py`; tables `fig/rule_rules_*.tsv`, `fig/rule_roots_*.tsv`,
`fig/rootagg.md`.

### 4982.5 Part A, the extra (`server/src/extras/units/srv_unit_placement_fix.{h,cpp}`, `MakeFixUnitPlacement(id)`) [C: code and unit tests; nothing run live, no picture yet]

- **Rule** as 4982.4 A: candidates every 2 units within 10 of the plot centre, cost = mean slope over the 2-unit grid within
  10 (81 points), least cost in hundredths then nearest; a move only when it saves >= 0.05 of slope and >= 10 % of the
  centre's; a candidate that stands by more points under the water (`g.waterZ`) than the centre is refused. The pick
  shares one 2-unit lattice of 41 x 41 slope samples between all 80 candidates (the same sums as `MeanSlope`, test
  `place_terrain`).
- **Which units:** domain land, not embarked, alive (no dead or delayed-death flag), drawn (listed by stock or shown by an
  earlier extra), not held in a fight, no root from an earlier extra, at rest (its scene plot is its record's plot and its
  scene root, if any, within 0.02 inner radii of that plot's centre, as FixAirBases's walking test). The root given is the
  scene's root (or the centre) plus the pick's offset.
- **Ease-in:** a unit that comes to rest (it walked or was hidden) gets the offset over 400 ms (smoothstep; `animating()`
  true meanwhile); a unit already at rest when first seen (a load, the extra's start) takes it at once; a unit that starts
  to walk snaps back to the scene's root (lead-0455 #8158: it merges into the move). A unit not offered for a frame is
  forgotten.
- **Known limit (lead-0042 #8163, 2026-11-20):** a moved unit that 0042's fight queue holds snaps back to the scene's root
  for the fight (the seam ignores every override of a held unit). lead-0042 wants the held path to keep the last override
  root; a hunk in the held path once 4726 is on 0042's team branch, posted to lead-0455 first. Not written.
- **Cache:** the pick per plot index, kept while a sparse checksum (every third sample of the 43 x 43 square the pick reads)
  is unchanged; the checksum is compared once per plot per frame; a new grid object or size drops every pick (test
  `place_cache`: a field flattened in place is picked again and the unit no longer moves).
- **E-rules:** E1 display only; E2 flag off = the scene before (`place_flag_off`, `SameScene`); E5 ServerUnits = 0: one line
  `units extra FixUnitPlacement: off: ServerUnits = 0`; E6 no unit table, or a field that gives no plot centres: off with
  one line (`place_inputs`); no field this frame: nothing to do.
- **Tests** (`server/tests/extras/units/test_fix_unit_placement.cpp`, built with `build_units_extras_tests.py --test
  test_fix_unit_placement`; 2026-11-20 10:47-10:54 EST, workstation, clang-cl x64, through `quietlock.py run`): 9 of 9 cases pass
  (`place_terrain`, `place_choice`, `place_scene_on`, `place_flag_off`, `place_inputs`, `place_held`, `place_settle`,
  `place_cache`, `place_cost`). The synthetic field is a ridge (falls 1 unit per unit of x up to x = 200, flat east of it).
- **E8 cost (indication only, PERFORMANCE.md; workstation busy, no lock held, one run each):** `place_cost`, 10 000 land
  units on 80 plots: the build 3.7-4.4 ms without the module, 5.1-10.6 ms with it (cached picks; two runs, noisy), the
  first frame with all 80 plots picked 13.3 ms (0.17 ms per pick; 42 ms before the shared lattice).
- **Registration (not committed; for the rows landing):** include `#include "srv_unit_placement_fix.h"` in
  `srv_units_extras.cpp` and the maker `[]() { return MakeFixUnitPlacement(Vp64Opt::kFixUnitPlacement); }` first in
  `UnitExtraMakers()` (id 23 is the lowest of 23-26; it sets a root, so it must run before an extra that reads `io.root`).
- **Not verified:** nothing run live; whether the move puts a formation on a neighbour's units at a shared border (offset
  at most 10 units, 0.31 inner radii, toward the plot's flatter side) is not checked. *(Corrected 2026-11-20 by W11: part B
  is applied and the bundles and pictures are in 4982.6-4982.9; this line said "nothing run on a bundle, no picture, part B
  not applied".)*

### 4982.6 Part B, each figure on the highest ground under its feet (W11 of team-0455, 2026-11-20) [C: code and unit tests]

The placement hunk W8 proposed (#8146), agreed by lead-0042 (#8163, #8184: the `placement.h` field after `waterLevel`, no
clash with 0042's lines at the file's end) and granted by the MC's touches (0455 v7/v8), with plumbing (a), per unit:

- `render/include/c5r/units/placement.h`: `PlacementInput::footRadius` (default 0). `render/src/units/placement.cpp`:
  `surfaceAt` (bilinear on the samples `heightAtWorld` reads) and `footBase` (the highest `surfaceAt` among the figure's
  origin, 12 points at r and 6 at r / 2, half a step turned); in the figure loop, when `footRadius > 0` and the figure's own
  sample is above the water, `base = max(footBase, waterLevel)`; otherwise the EXE's rule unchanged. A radius not finite,
  < 0 or > 16 is `BadLayout`.
- Seam: `UnitOverride::footRadius` (`server/src/units/srv_units_scene.h`, `kMaxUnitExtraFootRadius` 16); the module's
  range check (`srv_units_extras.cpp` `OutOfRange`: "a foot radius outside [0, 16]", the extra then off for the session)
  and the scene's (`srv_units_core.cpp` `extraOverride`, counted `x_bad`); a held unit's foot radius is ignored like every
  override; the module counts a changed radius in the extra's `ov=`; the scene counts unit-frames with a radius in a new
  `foot=` field of the `| x ...` part of `units 1s` / `units totals` (between `scaled=` and `held=`). `UnitsCore::buildList`
  copies it to `UnitInput::foot_radius` (`render/include/c5r/units/unit_scene.h`, not in the `.c5units` file), and
  `render/src/units/draw_steps.cpp` to `PlacementInput::footRadius`.
- The extra (`FootRadiusFor`): `kFootRadius` = 3 for a land unit at rest that it moves, or whose plot centre's mean slope
  (part A's cost) is at least `kFootRoughSlope` = 0.25 (a uniform slope that A cannot improve); 0 otherwise (flat plots
  keep the EXE's rule). It grows in with the move (radius x the ease-in share). Ships, aircraft, embarked, walking, dead,
  held units: 0 (the extra does not handle them). A ship's figures are also safe in the placement itself: their own sample
  is at or under the water level, so `footBase` is never read for them (W6's open point; test `place_foot`, water case).
- Tests: `test_fix_unit_placement` **10 of 10** (2026-11-20 11:44 EST, workstation, clang-cl x64, through `quietlock.py
  run`; the new case `place_foot`: radius 0 equals the EXE's formula exactly, radius 3 on the ridge gives each figure the
  ground 3 units uphill (`RidgeHeight(x - 3)`, within 0.02) with x, y, yaw unchanged, flat ground unchanged, water floor
  first, radius 16 accepted and -0.5 / 16.5 / NaN / inf refused; `FootRadiusFor`; `place_scene_on` checks radius 3 on the
  moved warrior, 0 on the flat one, `xFoot` 1 and `feet` 1); `test_units_extras` **12 of 12** (the `| x` line with
  `foot=0`).

### 4982.7 Re-score with the product's own pick and placement (W11, 2026-11-20 11:40-12:05 EST, workstation, offline) [C: computed by the module's own CPU path]

`figreal` (scratch, `F:/c5-64bit-work/0455/4982/w11/tool/`, made from W8's `figrule.cpp` by `make_figreal.py`, linked with
this branch's `server/src/extras/units/` and c5render): per unit on the named plots, `srvextras::FindFlatGround` and
`FootRadiusFor` (the product code), applied through the product's placement (`UnitInput` root and `foot_radius` ->
`placeUnit` with `footBase`), the posed figures scored as in 4982.4 (foot = vertices within max(0.5, 8 % of the height)
of the lowest; sink and float against the bilinear surface). Same cases and frames as 4982.4; "A" alone and "B" alone
are the product's pick or radius with the other part off.

| case | rule | worst foot sink | mean worst sink | summed sink | worst foot float | mean worst float | figures with a foot > 2 in |
|---|---|---|---|---|---|---|---|
| c1 (6) | stock | 7.97 | 6.98 | 134.5 | 15.66 | 9.77 | 28/38 |
| c1 | A | 7.31 | 5.25 | 101.0 | 14.79 | 9.25 | 22/38 |
| c1 | B | 5.27 | 3.55 | 43.3 | 18.36 | 13.20 | 8/38 |
| c1 | **A + B (the product)** | **4.60** | **2.41** | **30.1** | **15.37** | **10.97** | **6/38** |
| c2a (4) | stock | 2.15 | 1.93 | 32.7 | 3.56 | 2.85 | 2/34 |
| c2a | **A + B** | **1.71** | **1.01** | **4.4** | **4.82** | **3.48** | **0/34** |
| hills (6) | stock | 8.59 | 4.38 | 93.5 | 3.65 | 2.11 | 13/60 |
| hills | **A + B** | **6.82** | **2.54** | **28.6** | **4.92** | **3.25** | **4/60** |
| synth (18) | stock | 12.54 | 8.61 | 522.6 | 15.73 | 10.74 | 99/126 |
| synth | **A + B** | **3.23** | **2.23** | **70.4** | **14.71** | **11.33** | **17/126** |

The product reproduces 4982.4's A + B row on c1, the hills and the synthetic slope to the second decimal (the real picks
are the ones W8 scored as `grad_r10`, limit 10: c1 (49,13) (-8, 0), (49,14) (4, -8), (48,14) (-6, 8); hills (106,21)
and (106,32) (4, -8), (110,23) (-6, -8); synthetic (87,41) (-6, 8)). On c2a it differs: the spearman 1055 on (38,11) has a
mean slope of 0.046, so it is neither moved nor given a foot radius (`kFootRoughSlope` 0.25), and its 1.71-unit sink
(present in stock too, on ground the slope measure calls flat) stays; 4982.4 applied B to every figure (worst 0.32). The
moved unit 1054 goes from 2.15 to 0.32. Summed sink: c1 -78 %, c2a -87 %, hills -69 %, synthetic -87 %; the worst float
stays at or below stock on c1 and the synthetic slope and rises on c2a (+1.3) and the hills (+1.3).

### 4982.8 Pictures and the scene test (W11, 2026-11-20 12:15-12:42 EST, workstation, offline) [L for what the frames show; C for the test]

**Scratch player** (not in git): `C:/c5-64bit-work/0455/4982/w11/ss/bin/sceneshot.exe` from `origin/main` (with 0454's 2a)
merged with `w11-4982` @3cb8859cc (code merges clean; two document conflicts left as they were in the scratch copy), the
`options/` part of W3's `options-rows-23-26.patch`, the makers 23-26 registered in id order and 4983's two player lines
(`attachExtras`, the `build_sceneshot.py` units block). Scripts `F:/c5-64bit-work/0455/4982/w11/{player,pics,record}.sh`,
`triplet.py`. The player logs `units extra FixUnitPlacement: in effect` with the option on.

**Triplets** (MC #8099: the same units on the same plots, stock | ours off | ours on), 4980's c1 bundle
(`C5H-4980-C1MOUNTAIN_0009_20261120-032355_workstation`), the named camera `c1_mountain` and the recorded frames 1925 and
1976, stock = vc-20261120-023749-workstation-f039 (the same state and camera); pictures in `F:/c5-64bit-work/0455/4982/r1/`
(`t_cam.png` the whole changed region 635-1756 x 312-976, `t_f1925_zoom.png`, `t_f1976_zoom.png` the spearmen and
horsemen, 1020-1460 x 400-780; each crop alone as `_stock`, `_off`, `_on`). Recorded DIAGNOSTIC: vc-20261120-123952-workstation-b97d
(camera), vc-20261120-123953-workstation-1163 (frame 1925), vc-20261120-123955-workstation-f8a0 (frame 1976). What they show:
off is P1 and looks as stock (the spearmen's feet and the horses' lower legs hidden in the rock face); on, the spearmen stand higher up the slope with their feet visible and the horsemen moved up and
left with their legs clear of the rock; the moved units' pixels differ in 79,118 (frame 1925 and the camera) and 79,018
(frame 1976) of 3,603,694. A second group east of the mountain, by the city, also moves (land units at rest on rough ground
there; not identified per unit). The catapults on the left look unchanged at this camera. Stock's resource icons on the
right are not drawn by the server in any of the three (not this ticket).

**E9 scene test** `server/tests/extras/units/test_fix_unit_placement_scenes.py` (model `test_ships_moored_scenes.py`), run
with the scratch player through `quietlock.py run`: **5 passed** (341.6 s): E2 on c1 and c4_port with `FixUnitPlacement=0`
(every frame equal, module dropped, the off line); E9 on c1 with the option on (in effect, no LOUD line, frames differ,
exit 10); the c1 camera picture differs by 20,000-160,000 pixels (79,118); c4_port with the option on: in effect and every
frame equal (the city's trireme is a ship, the worker on flat ground: nothing moves).

### 4982.9 Rule A, the other tests, what is open (W11, 2026-11-20 12:42-12:58 EST, workstation) [C]

- **Rule A** (all library bundles P1 byte-equal with option 23 registered and off; the scratch player of 4982.8, every
  option at its default, `sceneshot --dumped --compare --tolerance 1:1 --no-pictures`, `player.sh p1`): **12 of 16 "OK 8
  shot(s), each byte-equal to the server's own dump"**, each with the two unit-extras lines: 0301's FOG, HUGE, the four
  SCNB, TINY; 4980's C1MOUNTAIN, C2ANATURAL, C2BWONDER, C3AGARRISON, C3BGARRISON2. **Stopped** at 12:58 by lead-0455 (MC
  #8300: one rule A run at a time). C4PORT gave exit 8 (`ReplayFailed`: the units pre-check found none of VP's unit models
  in the recorded mods folder, `c5h-c`'s MODS, at 12:56, while that instance's user folder was being written by another
  run); the same bundle was byte-equal with `FixUnitPlacement=0` in the scene test at 12:37, so this is taken as the
  folder's state then, not the hunk [I]; to re-run. **Open:** C4PORT, C5AIR, C6CARRIER, C7INTERCEPT, by `ONLY='C4PORT
  C5AIR C6CARRIER C7INTERCEPT' bash F:/c5-64bit-work/0455/4982/w11/player.sh p1` once the rule A slot is free (MC-B's
  order), with the player pointed at a private read-only copy of the MODS folder and the mods database (`--mods`) instead
  of `c5h-c`'s live folder (lead-0455, 2026-11-20 13:00; W12 of 4986 keeps one under F:/c5-64bit-work/0455/4986/).
- The other unit-extras tests on this branch after the seam change: `test_fix_air_bases` 7 of 7, `test_ships_moored` 7 of
  7 (12:44 EST).
- **E8 cost** (indication only): `place_cost` 2026-11-20 11:44, 10 000 land units on 80 plots: the build 6.9 ms without the
  module, 9.7 ms with it (cached picks), the first frame 14.0 ms; part B adds 18 surface samples per figure of a unit with
  a foot radius (not timed alone).
- **Not verified:** nothing live; the fight-hold snap (4982.5's known limit) and the foot radius of a held unit (ignored by
  the seam as every override); a unit on a plot whose mean slope is under 0.25 but with a local bump under one figure
  (c2a's spearman 1055 sinks 1.71 in stock and with the fix); which units form the second moved group by the city in
  4982.8; the worst float rises by 1.3 units on c2a and the hills (4982.7).

### 4982.10 On main: the merge, the registration, lead-0042's conditions (impl-4982 of account B, 2026-11-23) [C: code and unit tests]

Branch `task-4982-fix-unit-placement` from main 253c5a988 with W11's `w11-4982` @31cb57bae merged. Conflicts: the seam's
`UnitOverride` now carries 4983 (d)'s `perimeterEdges`, 4986's `lift` and this ticket's `footRadius` side by side;
`footRadius` is in the module's `OutOfRange`, its override count, the core's range check and its `any` test next to
`lift`; the research and README conflicts kept both sides (4982.6-4982.9 before the landing section).

- **Registration:** `MakeFixUnitPlacement(Vp64Opt::kFixUnitPlacement)` is first in `UnitExtraMakers()` (it sets a root, so
  it runs before an extra that reads `io.root`). It does not take the garrison extra's units: stock hides those, and 23,
  running first, skips a unit nobody draws yet (`NotDrawn`). The registration test (`test_units_extras`
  `CaseRegistration`) now checks five makers in id order and four lines of 23 (off by default; in effect with `= 1`;
  `EnableBugfixes = 0`; `ServerUnits = 0`), and the null-factory case seven attach lines with 23's off line.
  `docs/OPTIONS.md` 23: "Not yet in effect" removed; one sentence on fights added (below).
- **lead-0042's conditions for the `placement.cpp` hunk (#11560, relayed by MC-B #11561, 2026-11-23):**
  (1) a unit `FightPlacement` holds (any set entry: the whole fight and its settled tail) is placed by the inner source
  with `foot_radius` 0 (`render/src/unitanim/motion.cpp`, a copy of the `UnitInput`; the caller's input is unchanged).
  The reason: `applyFigureOffsets` moves each figure from `max(heightAtWorld, water)`, not from the foot base, and would
  carry the gap as a false ZOffset. Test `fight_placement_held_keeps_the_stock_floor` (`render/tests/units/
  test_fight_motion.cpp`): on the rising grid, foot radius 3 against 0 byte-equal at seven times from 0 to the end + 60 s
  (moves, end, settled tail); higher without a fight and again after `clear`. A live 5057-kind fight scene (kn-kill,
  knb-kill) with a foot radius is not run: the charge-scene runners carry no FixUnitPlacement option. (2) The same in the
  garrison perimeter decorator (`render/src/extras/units/garrison_perimeter.cpp`), test
  `decorator_drops_the_foot_radius_of_spread_units`. (3) **Known step:** when a fight releases a unit on a slope its
  figures step in one frame from the stock floor to the foot base (the size of 4982.2's sinks: 2-3 units on hills, up to
  about 8 on mountains [I]); accepted at option default 0 (lead-0042).
- **Scene test** (`test_fix_unit_placement_scenes.py`): the off and on cases no longer assert "0 in effect: dropped" and
  "1 in effect" (FixAirBases defaults to 1 since D235 and puts a second extra in effect); optional `C5_SCENE_MODS`
  replaces a bundle's recorded mods folder (W11's C4PORT `ReplayFailed`, 4982.9); `-SYNHIDE` bundles are skipped.
- **Scripts** (`server/evidence/0455/4982/`): `allb_4982.sh 1|2|3|4` (every builder, rule A, the five unit-extras scene
  tests; a copy of 4986's with phase 4 widened), `smoke_4982.sh` with `arm_4982.py` (the live arms off and on at the 4980
  kit's c1 mountains and c2b flat land, E1, the stock arm; a copy of 4986's).

### 4982.11 Rule A, the builders, the live check and E1 (impl-4982, 2026-11-23) [L, as named; C for the tests]

**Builders** (`allb_4982.sh`, head 1a39b4b22 plus scripts, workstation, through quietlock, owner impl-4982): phase 1
11:18-11:29 render ctest whole 109 of 109 (with `fight_placement_held_keeps_the_stock_floor` and
`decorator_drops_the_foot_radius_of_spread_units`; the two programs alone: 11 and 8 cases, 0 failed), `build_server.py`
on the 0.1b pins; phase 2 11:29-11:44 units (`test_units_runner` 7 passed), unit-extras tests `test_units_extras` 13/13,
`test_air_intercept_patrol` 11/11, `test_fix_air_bases` 7/7, `test_ships_moored` 7/7, `test_fix_unit_placement` 10/10,
unitfeed, trigger_scene, effect routes, trigger emitter, the four units runners, farm fields, unit shadow, sceneshot
`--smoke`: all OK; phase 5 11:44-11:58 `run_all --only fow-scene overlay-scene shadow-ice-scene water-scene forest-scene`
3/3/6/29/3 passed; the live layout's server and composite (`smoke_4982.sh build`, 11:20-11:24) OK; `test_options` x64
905 passed against this branch's `docs/OPTIONS.md` (12:00).

**Rule A** (MC-B's slot #11579, 11:58-12:31): `run_all --only render-host scenebundle-programs scenebundle-py
offline-equals-live`: 191, 4, 24 and **17 passed** (every library bundle P1 byte-equal with option 23 registered and
off). This closes 4982.9's four open bundles (C4PORT, C5AIR, C6CARRIER, C7INTERCEPT are in the 17).

**Scene tests** (phase 4, 12:31-12:48): 19 passed, 7 failed, all test-side: (a) the placement test replayed each bundle
with its recorded install, `c5h-a/install`, a pool instance claimed by another run: the player refuses it (exit 9,
"lies in pool instance c5h-a, which the pool shows claimed", ticket 0614's rule); the test now passes `--install` and
`--exe` from `C5_GAME_DIR` and `C5R_EXE` as `test_air_intercept_scenes.py` does. (b) `test_fix_air_bases_scenes.py`'s
two E9b cases: `synhide.py` copied the library bundle with `copytree` (copystat), the library copy's files are read-only,
and the rewrite of `feed.bin` failed (PermissionError); `synhide.py` now copies content only and clears read-only on a
re-run's delete. The two files' re-run (12:52-13:04): **11 passed** (placement 5: off byte-equal on c1 and c4, on moves and differs, the named-camera picture differs by the units within 20,000-160,000 pixels, flat ground unchanged; air bases 6). So all 26 unit-extras scene cases pass on this branch.

**Live** (c5h-new-b, Goldberg, EXE build 26577625, CP+VP+EUI 5.4.8, Vibrant Textures; layout of df53d59bd laid by
`lay.py` (server.exe `f67c51de...`, x64 DLL `b84aa3e9...` = the 0.1b pin, composite `c4f216f6...`); MC-B #11565/#11647;
12:22-12:48; restored by hash, released 12:5x):
- **E1** (the 4980 kit copy autoplayed turn 9 -> 19 by `perf/profilekit/acceptance.py run --config inc2 --no-ledger`,
  windowed, both arms from one save): `statscmp.py e1on e1off --from-turn 9 --expect-turns 9 19` **IDENTICAL, 15 tables,
  2,509 rows**. In the on run the extra worked: `extras FixUnitPlacement on ov=39412 failures=0`, the seam's counts
  `root=5071 foot=8350 held=0 bad=0` (off run: `FixUnitPlacement off ov=0`, `root=0 foot=0`); no LOUD line in either run.
- **E8 indications** [one run per arm, rule A running beside them: not a claim, PERFORMANCE.md]: server.exe private
  bytes 1,121.9 -> 1,150.1 MB on, 1,354.4 -> 1,140.7 MB off (turn 9 -> 19); the EXE's private bytes 1,320.7 -> 1,367.6
  on, 1,336.2 -> 1,368.3 off; ten turns 32.9 s on, 48.5 s off (off's turn 19 alone 29.0 s). The offline `place_cost`
  (4982.9) stays the per-frame indication.
- **Pictures** (DIAGNOSTIC, D112: a test layout, interface hidden unless named; files `F:/c5-64bit-work/4982/out1/<arm>/`,
  not in git). c1 (49,13) at min-2 and min, two frames 3 s apart each; c2b (54,15) at min-2; c1 with the interface:
  off vc-20261123-123628-...-bba8, -123635-...-2ace (near), -123645-...-c4fb, -123651-...-e2d2 (close), -123706-...-85e3,
  -123712-...-6f31 (c2b), -123725-...-d28f, -123732-...-9aa5 (interface); on vc-20261123-124046-...-a09a, -124052-...-aaff,
  -124103-...-5c8e, -124109-...-59f6, -124126-...-673c, -124132-...-6927, -124146-...-59a3, -124152-...-8145; stock (the
  instance with the layout restored, the game's own drawing) vc-20261123-124602-...-8e0b, -124608-...-c7ea, -124619-...-473c,
  -124629-...-a9f2, -124648-...-772c, -124655-...-f0ed, -124710-...-2c40, -124716-...-235e.
  By eye [L]: **off equals stock** in the figures' places on c1 (the catapults, the spearmen and the horsemen stand where
  stock puts them, the horsemen's legs on the steep flank as in 4980's shot); **on** moves the three formations on the
  mountain plots (the catapults and the horsemen a step up and west, the spearmen's block shifted) and the group of
  figures on the rough plot west of the city at the picture's right (its plot not identified); c2b's units on flat land stand where off has them. A pixel count is no help
  live: the whole frame differs between any two launches (30-46 % of pixels, water, clouds, the trees' placement and a
  sub-pixel camera difference), so no number is given for the units.
- **After two main merges** (3fd0a9bb1 with 5052 part D, d670e8b88 with 5055, both in `srv_units_scene.*`, no
  conflict): `allb_4982.sh` 1, 2, 5, 6 on the first (13:45-14:45: all OK, scenebundle-py now 25) and 1, 2 on the second
  (14:46-15:08: all OK, the same counts); rule A and the scene tests were not re-run after the merges.
- **Not verified live:** the fight-hold rule (no fight was staged), the release step of 4982.10 (3), a hill case close up
  (the kit's hills hold no unit at rest in the shots), a unit walking off a moved formation.

## 0455 landing: option rows 23-27, registrations 24-26, the player lines (W10 of team-0455, 2026-11-20) [C: code and its tests; offline replays; live smoke (a) by W13 at the section's end, L; corrected 2026-11-20 14:58 from "nothing live"]

Branch `w10-0455` from the team head 3a8a0f953 with main 0d07f94ed and the team head 3c500dea9 merged (two epic-log
conflicts and one research README conflict, both sides kept, the log in time order).

- **Option rows** (`options/`): W3's `server/evidence/0455/4983/options-rows-23-26.patch` applied clean to main's rows
  form, then: row **27 `AirInterceptPatrol`** (visual addition, default 0, MC #8114; `kCount` 28, next free id 28);
  row 23's help and `docs/OPTIONS.md` text **terrain only** (W8's text, 4982 log 10:55; MC #8099 dropped the wonder
  half); row 26's help, header comment and `docs/OPTIONS.md` text **corrected to the 4985 re-scope** (research 4985.5:
  the active team's aircraft drawn in every case as stock draws them after a load; the earlier text, "parked beside
  their city or on their carrier, intercepting ones in a loop", described 4985-4987 before the re-scope and now belongs
  to 27); `docs/OPTIONS.md` 25 is W6's text (4984 log, "For the landing"); 23 and 27 say "Not yet in effect in this
  version"; `research/07-architecture-decisions/options-file.md` section 2 rows 23-27 updated the same way.
  `TestExtra` pins seven extra rows; `test_options.exe` **724 passed, 0 failed** in the x86 and the x64 tree (11:19-11:20
  EST; 716 with rows 23-26 in W3's scratch copy).
- **Registrations** (`server/src/extras/units/srv_units_extras.cpp` `UnitExtraMakers()`, id order): GarrisonVisible (24,
  look `SouthEdge`, the maker's default), ShipsMoored (25, `BowToLand`), FixAirBases (26). Not registered: 23
  (`MakeFixUnitPlacement` merged, registered when 4982 is complete; *registered first in the list on 2026-11-23 by
  4982, section 4982.10*) and 27 (4986, W9). A look the owner picks changes the
  maker's default argument in its header, not the list.
- **Registration test** (`test_units_extras.cpp`): W3's `registration-test-delta.patch` ported and widened to the three
  registered features: the list (three makers, id order, each made with its row), per feature the switch-off, flag-off,
  on and E5 lines through the real state (one line per registered feature per configure), the attach line with every
  option at its default (`module linked, 0 in effect: dropped` plus the three off lines), GarrisonVisible's edge look.
- **Player** (`server/tools/sceneshot/`): sceneshot.cpp attaches the unit extras right after the unit scene's probe check
  (`unitsProbe->attachExtras(srvunits::MakeUnitsExtras(), SrvOptions(), true)`, W3's form of 07:38); build_sceneshot.py
  compiles `server/src/extras/units/*.cpp` in its own object folder `obj/extras_units` after 4938's extras block
  (lead-0454 #7921). A player built without that block gets a null factory and logs `units: extras: module not linked`.
- **Outline test** (`server/tests/extras/test_visible_outline_scenes.py`): `test_off_is_unchanged` asserts no
  `extras: VisibleAreaOutline` and no `LOUD: extras:` line (lead-0454's form #7973, MC #7977), so the unit extras' attach
  line of a units-on bundle (every library bundle has `render_units=1`) passes.

Results (2026-11-20, workstation, clang-cl x64; every run through `quietlock.py run`; builds on C:/c5-64bit-work/0455/landing/):

- `test_options.exe` 724 passed, 0 failed, x86 and x64 (11:19-11:20).
- `test_units_extras.exe` 13 of 13 (the seam suite with `garrison_registered` widened; 11:38), `test_ships_moored.exe`
  7 of 7, `test_fix_air_bases.exe` 7 of 7, `test_fix_unit_placement.exe` 9 of 9 (11:39-11:40).
- `server.exe` debug, no LTO, own `build_all` deps from a plain copy of the fork at the pin 1fd51353e, `--no-proxy`:
  `build_server: OK in 573 s`; starts to its usage line (exit 1 without `--config`), SHA-256 prefix 856eefcfe453ae42.
- Scene tests with the player that links the module (`C5_GAME_DIR` = c5h-c's install, EXE 04cfdc71...):
  `test_fix_air_bases_scenes.py` **6 passed**, `test_visible_outline_scenes.py` **9 passed** (both units-on library
  bundles TINY and HUGE, the extra off and on, the refusals), `test_garrison_scenes.py` 5 passed and 1 failed,
  `test_ships_moored_scenes.py` 4 passed and 1 failed: both failures exit 8 (`ReplayFailed`) on an asset the replay did
  not find although it is on disk (`Rice_Random_D.dds`; 62 `.fxsxml not found` rows of the unit pre-check), see below.
- **Rule A** (P1 = `--dumped --compare --tolerance 1:1 --no-pictures`, every option at its default, all 16 library
  bundles, install/EXE/mods of the free pool instance c5h-a for every bundle; 12:05-12:54): without the module **16 of
  16** byte-equal, each with `units: extras: module not linked`; with the module **15 of 16** byte-equal, each with
  `units: extras: module linked, 0 in effect: dropped` and no extra in effect; the 16th (C7INTERCEPT) exit 8 with 21
  `.fxsxml not found` rows of the unit pre-check (the files are on disk), and C3BGARRISON2 hit the same once and passed
  on its retry.
- **Asset-not-found flake [L 2026-11-20, cause I]:** five offline replays between 11:55 and 12:30 stopped with exit 8
  because `FileProvider::read` (`models/src/art_resolve.cpp` `resolve_fxsxml`) or the decal loader did not get a file
  that exists in the instance's mods folder, in both c5h-c's and c5h-a's folders, with both players, at random bundles.
  The MC measured F: reads at about 390 ms at that time (MC #8300: this rule A run and 0042's all-builders run reading
  the library from F: together), but the missing files themselves are on C: (the instances' folders). Likely cause [I]:
  the replays read the VP models from a pool instance's own MODS folder while a live run that had claimed that instance
  wrote its user folder (c5h-c was claimed by w21-0042 from about 11:15; W11 and W12 saw the same on c5h-c and c5h-a,
  queue #8298, #8312). Remedy: offline replays read MODS (and the mods database) from a private read-only copy taken
  once from an unclaimed instance, never from a pool instance's live folder.
  Nothing in the landing touches asset reading (the failures come before any extra runs).
- **Serial rerun** in MC-B's rule A slot (MC #8300; 13:02-13:07): C7INTERCEPT with the module (c5h-a's install and MODS) **byte-equal, 8 of 8** with `module linked, 0 in effect: dropped`; the garrison picture test on C3BGARRISON2 **1 passed**; the ships picture test **1 passed** (both with c5h-c's MODS, the bundles' own). So rule A: **16 of 16** with the module, **16 of 16** without; every scene test passes (garrison 6, ships 5, air bases 6, outline 9).

**Live smoke (a)** [L, 2026-11-20 14:28-14:49 EST, W13 of team-0455, instance c5h-a (old build 4390913, EXE stamp
0x546CD0A8 checked before every launch)]. The product path as W1's 4981 smoke (4981 issue log 03:34): `run_3308.py`
imported unchanged by `server/evidence/0455/landing/smoke_0455.py` (one prep, one run per arm, one restore; backup by
SHA-256, set-proxy, the composite with its manifest, D33 with Overlay on, `VP64Options.ini` `[Renderer] ServerRenderer = 1`,
`ServerUnits = 1`; the front end restored after every run). Build: branch `w13-0455` @4d4684ef6 = the team head 4b446e4bf
with main 2b50b1517 merged (0609's proxy setting), `server/build_server.py --config debug --deps` W10's `build_all` of the
pin 1fd51353e: server.exe SHA-256 c04680bba99041bb..., x86 proxy 211162da9038736c..., x64 DLL 6212200cc4932e85... (debug),
composite 9b3fc626d5c00288...; builds and logs in `C:/c5-64bit-work/0455/w13/` (run folder `smoke-142753/arm-*/out/`).
Save: a copy of `C5H-4980-KIT_0009` (SHA-256 74fc7428...), one AI turn per arm (turn 9 -> 10), no picture. Last `units 1s`
line of each arm (unit-frames since the start for the `x` and `extras` counters):

| Arm (`[Extras]` lines) | Attach line | Units | Extras' counters |
|---|---|---|---|
| off (none) | `units: extras: module linked, 0 in effect: dropped (the scene as before)` | table 43, listed 34, not_drawn 0, refused 0 | none (no module counters) |
| g24 (`EnableVisualAdditions = 1`, `GarrisonVisible = 1`) | `module linked, 1 in effect: attached`; `units extra GarrisonVisible: in effect` | table 43, listed **40**, not_drawn 0, refused 0 | `x shown=5685 show_refused=0 bad=0`; GarrisonVisible on ov=15797 shown=15769 failures=0 |
| s25 (`EnableVisualAdditions = 1`, `ShipsMoored = 1`) | `1 in effect: attached`; `units extra ShipsMoored: in effect` | table 43, listed **35**, not_drawn 0, refused 0 | `x shown=2136 show_refused=0 bad=0`; ShipsMoored on ov=2136 shown=2136 failures=0 |
| f26 (`EnableBugfixes = 1`, `FixAirBases = 1`) | `1 in effect: attached`; `units extra FixAirBases: in effect` | table 43, listed **36**, not_drawn 0, refused 0 | `x shown=0 bad=0`; FixAirBases on ov=0 shown=1706 failures=0 |

The extras that were off said why in one line each (`units GarrisonVisible: off: EnableVisualAdditions = 0 (default)
switches off every visual addition`, `units FixAirBases: off: FixAirBases = 0 (default)`, ...). Every arm: run rc 0, one
AI turn, the harness's stop ok, no box (`dialogwatch.py marker` rc 0 after each arm), no crash (no new dump in the
instance, `crashes.log` unchanged since 2026-11-15), no `LOUD` line in render.log. Each arm's `server-report.txt` says
`result: FAILED: database: the DLL imports database members the write recording does not cover`: the debug DLL's import
`?Bind@Results@Database@@QEAA_NHM@Z` (`upstream-fix-batch-2.md`, the lines on `Results::Bind(int, float)`: a debug build
keeps it, LTO drops it), the same in the off arm, so not an effect of the extras. Restore: the backup's 15 entries back
by hash (9 equal, 6 absent before and after), `set-proxy --original` rc 0, front end restored, `provision.py verify c5h-a`
ok, both save copies removed; c5h-a released 14:54. A lead-0590 driver wrote to c5h-a 14:07:43-14:15:59 under this claim
(lead-0590 #8399) and restored it by its own backup; nothing of this smoke was prepared or run then, and the instance
verified clean at 14:19 before this smoke's own backup [L].
What the extra units counts mean [I]: the off arm lists 34 of the table's 43 units (stock hides garrisons, ships in a city
and aircraft that are not their plot's centre unit); g24 lists 6 more (the kit's garrisons), s25 one more (a ship in a
city), f26 two more (aircraft stock leaves undrawn after a rebase in the kit's staging, 4985.1). Which units they are was not
checked by picture.

Not verified: no picture of the live arms (smoke only); frame time and memory per arm (one turn each, indication only:
server private bytes after the turn 997 / 1058 / 1038 / 1032 MB for off / g24 / s25 / f26, workstation, no lock);
release (LTO) build of the server and DLL; ASan of the unit extras not run.

## 4985 live smoke of FixAirBases on EXE build 26577625 (impl-4985, 2026-11-22; D235) [method C; results below]

The question (ticket 4985 acceptance (1), D235): with `FixAirBases = 1` in the instance's options file, are based and
carried aircraft drawn after a live rebase and a carrier's move as stock draws them after a load, with no error report,
no popup, no crash and no LOUD line of the extra; and does `FixAirBases = 0` behave as before (not drawn)? Only if it
passes does the default become 1 (the MC flips row 26, `docs/OPTIONS.md` and `docs/BETA-TESTERS.md` at the merge).

**Build** [C, 2026-11-22 00:29-00:33 EST]: `server/build_server.py --config release` from main 328fd71f3 on the shared
0.1b pins (`C:/c5-64bit-work/keep/pins-0.1b`, x64-0.1b 1b87dd185, `deps-x64-release`; no LTO link place taken),
`build_server: OK in 118 s`; the composite (`shell\build.cmd`) of the same tree. SHA-256 prefixes: server.exe
9286e3e5240ee381, x64 DLL b84aa3e90f51bbf5, x86 proxy 79ec71869e9da755, shell_composite.dll f6c73a9f43731e9d.

**Driver** `server/evidence/0455/4985/smoke_4985.sh` (modes build, dry, check, prep, arm, end) with `arm.py` (options,
shots, removal pairs, turns, log lines). The re-scope's live actions are reused unchanged (`server/evidence/0455/4985r/live.py
act`, steps a, b, c2, c1, d, state; save `live.py save`). The layout is the 26577625 route of 4914 (`lay.py prep` with
the composite, `lay.py helper on` = the installer's config.ini form, `ServerRenderer = 1`); the options file then gets
`[Renderer] ServerUnits = 1` and `[Extras] FixAirBases = 0|1` (EnableBugfixes stays at its default 1). Save: a copy of
the 4980 kit `C5H-4980-KIT_0009` (SHA-256 74fc7428...944db) put in as `C5H-4985-SMKK_0009`. Arms, one process each:

| Arm | FixAirBases | Loads | Steps |
|---|---|---|---|
| on | 1 | the kit copy | actions a, b, c2, c1, d; the game's save `C5H-4985-SMKL_0009`; shots at Tonga (b), A, Tahiti (c2) and the carrier, interface hidden, then two with the interface shown; two AI turns; shots again; removal pairs b, c2, carrier (c1) |
| off | 0 | the kit copy | the same (save `C5H-4985-SMKO_0009`) |
| reload | 0 | `C5H-4985-SMKO_0009` (the off arm's live state) | shots (the after-load reference), the same removal pairs |
| reon | 1 | `C5H-4985-SMKO_0009` | shots (on after a load) |

A reading is "drawn" as in the re-scope: the figure in both before shots of a removal pair and gone from both after
shots (MC #7615). Each arm ends with the extra's lines (`arm.py lines`: attach and in-effect lines, LOUD lines, the last
`units 1s` line, `server-report.txt`'s result, files under `<user>/VP64`), the dialog marker after every step (D155),
`crash.py collect`, the harness's safe stop and `frontend.py restore`. Every picture DIAGNOSTIC (D112): the layout is the
product's, laid by `lay.py`, not by the installer.

### 4985 rule A for the default 1 [C, 2026-11-22 00:37-00:47 EST, workstation, MC-B's slot (MC #9999 yes)]

Scene-bundle rule A before the flip: the row-26 default is changed by the MC at the merge, so the flipped default was
given to the player of main 328fd71f3 (`build_sceneshot.py --smoke`, OK) as `--option FixAirBases=1`, with the same
command and assertions as `test_offline_equals_live.py`: `server/evidence/0455/4985/rulea_4985.py` through `quietlock.py
run`, the checked C: copy of the library (`scenelib.py check`: OK, 16 bundles, synced 2026-11-20 14:10), asset root
`C:/c5-64bit-work/scene-asset-root/mods548-vt`, tolerance 1:1.

- **16 of 16 bundles pass**; each logs `units: extras: module linked, 1 in effect: attached` and `units extra
  FixAirBases: in effect` (every library bundle has units on); no LOUD line; 27-50 s per bundle.
- **All 128 dumped frames byte-equal** in colour and depth (`0 of 3603694 pixels differ`), so 0:0, not only the standing
  1:1.
- Why it holds [C, 4985.1]: every library bundle was recorded after a LOAD, where nothing has hidden an aircraft yet, so
  the extra finds nothing to show (E9a). **Gap note (for `research/06-tooling-and-workflows/offline-scene-harness.md`):** a
  bundle does not record the unit extras' state; the player applies its own options (`SrvOptions()`, the defaults plus
  `--option`). With the default 1, a bundle recorded live with `FixAirBases = 0` after a live rebase, a carrier's move or
  a kill on an aircraft's plot would replay with the aircraft drawn and differ from its dumps; the library holds no such
  bundle today. Remedy if one is recorded: a `render_config` key with the extra's effective state (as `render_roadlayout`
  for FixRoadLayout, ticket 4934), read by sceneshot unless `--option` gives the value. Not made here (outside this
  ticket's touches).

### 4985 live smoke results [L, 2026-11-22 00:51-01:26 EST, c5h-new-a, MC-B's GO of 00:51]

The window moved from 04:45 to 00:51 on MC-B's word (everything had to end by 01:38). EXE build 26577625 checked
before the prep; the layout and the save as in the method above. **Verdict: PASS** for FixAirBases = 1: with the flag
the aircraft stock hides after a live rebase, a carrier's move and a kill on their plot are drawn, as stock draws the
same state after a load; with the flag 0 the game draws as before; no box, no crash, no LOUD line.

| Arm (time) | Load | Units after the actions (`units 1s`) | Lines |
|---|---|---|---|
| on (00:52-01:08) | the kit copy, in game 131 s after the launch | table 45, **listed 36** (at the load 44 / 35) | `options: extras: FixAirBases: in effect`, `units extra FixAirBases: in effect`, `units: extras: module linked, 1 in effect: attached`; last line `extras FixAirBases on ov=0 shown=94483 scaled=0 failures=0` |
| off (01:09-01:21) | the kit copy | table 45, **listed 31** | `units FixAirBases: off: FixAirBases = 0 (from the file)`, `module linked, 0 in effect: dropped (the scene as before)` |
| reload (01:21-01:26) | `C5H-4985-SMKO_0009` = the off arm's state after the actions (a new process, flag 0) | table 45, **listed 36** | as off |

- The actions answered as in the re-scope (a: fighter 1071 created in Honolulu; b: 1064 to Tonga; c2: 1068 to Tahiti;
  c1: jet fighter 1066 by SetXY to Tahiti, then rebased onto carrier 1067, cargo; d: the carrier (1,12) -> (1,11)
  with 1066 and 1069 aboard), the same in both arms. **On = after a load (36 = 36); off = stock live (5 fewer).** The
  game's saves: `C5H-4985-SMKL_0009` (on, SHA-256 61884059...) and `C5H-4985-SMKO_0009` (off), kept in
  `C:/c5-64bit-work/4985/out1/saves/`.
- **Pictures** (DIAGNOSTIC, interface hidden, popups closed; Visual Checks): Tahiti (c2) on vc-20261122-010129-workstation-b37e
  (three fighters at the city), off -011657-...-bf74 (none), reload -012426-...-1ced (three fighters, the formation's
  figures at other spots: seeds); the carrier on -010143-...-6710 (the jet figures and 1069's three fighters on and beside
  the carrier), off -011711-...-a567 (the bare carrier), reload -012440-...-3748 (the same aircraft as on); Tonga (b) on
  -010107-...-528a, off -011627-...-1745, reload -012355-...-6725; A on -010118-...-cf59, off -011643-...-2049, reload
  -012406-...-cf37. With the interface shown (the full product layout as the player sees it, still DIAGNOSTIC: laid by
  lay.py, not the installer): on -010221-...-12a3 (Tonga) and -010232-...-c810 (carrier); reload -012509-...-1228 and
  -012528-...-6cb2.
- **Two AI turns with the flag on** (turn 9 -> 11, 01:02-01:03): the aircraft still drawn after them (Tahiti
  -010451-...-3c4c, carrier -010504-...-3e00, Tonga -010426-...-84d0, A -010438-...-536d); listed 38-41 later in the turns.
- **Removal pairs** (before 1, before 2, Lua kill, after 1, after 2): on b vc-20261122-010534-...-d04e / -010540-...-fd66 /
  -010550-...-4a14 / -010559-...-d802 (the fighter formation at Tonga, gone after: drawn); on c2 -010634-...-6858 ... -010654-...-5c73
  (drawn); on c1 -010730-...-0466 / -010736-...-38db / -010743-...-13ab / -010748-...-dac1: the jet figure on the deck goes
  with 1066's kill, 1069's three fighters stay (the kill changes the carrier's plot, stock would hide 1069 too; the flag
  shows it again: by design, 4985.1); off b -011749-...-24ef ... -011821-...-6287, c2 -011906-...-d525 ... -011930-...-aeba,
  c1 -012003-...-67f7 ... -012031-...-1681: no aircraft in any before shot (not drawn).
- **Clean:** the dialog marker after every step: no box in any arm; `crash.py collect` after each arm: nothing new; 0 LOUD
  lines in render.log, server.log and vpproxy.log of every run; `FixAirBases ... failures=0`. Every run's
  `server-report.txt` reads `result: FAILED: the engine is gone: channel N serve() returned PeerDied` [L]: the same line
  as the ten earlier runs of other tickets on this instance (the harness's stop ends the EXE; the server reports its
  peer gone), not a failure of this smoke [I].
- **Restore:** `smoke_4985.sh end` 01:25-01:26: the harness's stop, frontend restore, lay.py collect and restore (backup
  by hash: `restore ok`), the saves put back as snapped (exit 0), `provision.py verify --quick` 0; c5h-new-a released
  01:26.
- **Not run (time):** the reon arm (flag 1 on the loaded state: covered offline by E9a and by rule A above), the off arm's
  interface shots and turns (`C5_SMOKE_QUICK=1`), the reload arm's removal pairs (`C5_SMOKE_NOPAIRS=1`).
- **Seen, not this extra's [L, cause not examined]:** in several shots of all three arms (flag 1 and 0 alike) a city's
  model is missing at a city plot near the map's wrap seam (Tahiti at x 1, Tonga at x 48 on the Tiny map): e.g. Tonga in
  on -010534 and off -011749, Tahiti in on -010451 and off -011920, every city of the reload's carrier shot -012440;
  present in other shots of the same plot. One shot after a far camera jump (on -010743) has a grey checkerboard of
  unbaked tiles at its right edge. Both may be ticket 0659's case (a far camera jump on a wrapping map, the follow-up of
  0652) [I]; for the MC to route. *Corrected 2026-11-22 by ticket 0662 (`city-renderer.md` 18): neither is a presented
  frame or 0659's case. They are PrintWindow captures taken part-way through a frame (every EXE model missing at once, the
  grey 84,84,84 over the units, the camera still); the composite's counters show every presented frame complete.*
- **Shadows (4985.6, 4985.7) [L, by eye, one crop]:** the fighter formation the flag shows at Tonga (on pair b, before 1
  vc-20261122-010534-workstation-d04e) has **no shadow** on the ground, while the three aircraft at the bottom right of the
  same shot (a plot no action changed, drawn since the load) have theirs. As predicted in 4985.6: the EXE casts no shadow for a unit it hides, and a
  shadow for a shown unit is the seam's question, not this extra's. A visible difference to the after-load look, small at
  normal zoom; for the owner with the pictures.
## 4986 AirInterceptPatrol (W9 of team-0455, 2026-11-20; option id 27, visual addition, default 0)

Branch `w9-4986` (from `epic-0455-extras-b` @8ea070ea4). Brief `server/evidence/0455/briefs/w9-4986.md`; MC #8114. Premise
[L, 4980 c7]: stock and the server draw a fighter on intercept (kind 1000, activity 5) as its parked formation at its
base; no patrol. With the flag on, the active team's aircraft on intercept fly a slow holding ring around their base.

### 4986.0 Design note (2026-11-20 10:35 EST; posted to lead-0455 on thread 0455 before any product code)

**P8, the range source: the database base range [C].** The DLL's `CvUnit::GetAirInterceptRange()` is
`m_pUnitInfo->GetAirInterceptRange() + GetExtraAirInterceptRange()` (fork `x64` e9d0c4837, `CvUnit.cpp` 18708-18711); the
extra part comes only from promotions (`ChangeExtraAirInterceptRange(thisPromotion.GetAirInterceptRangeChange() *
iChange)`, 28225). In the database of the c7 bundle (`C5H-4980-C7INTERCEPT_0009_...`, member `units/gen0000000001.db`,
VP at this pin, read 2026-11-20 10:30) **no row of `UnitPromotions` has `AirInterceptRangeChange` <> 0**, so for VP the
unit row's `Units.AirInterceptRange` is exactly the game's value. Aircraft with a range: Triplane 6, SPAD 6, Fighter 8,
Zero 8, Jet Fighter 10 (bombers 0: they cannot intercept). The route needs no DLL change and no new kind; a mod that adds
such a promotion would make the drawn ring smaller than the real range, never larger (said in OPTIONS.md). A new kind on
`x64-render-events` (D104 identity) stays the fallback if the owner wants promoted ranges exact. The seam does not give an
extra the database today, so the route needs a small seam hunk (H1 below).

**What the seam gives (4981.1) and what it lacks.**
- Time-varying root and heading: **yes**. `beginFrame` carries the frame clock `tMs`; `unit()` may set `root` (x, y) and
  `heading` (yaw) every frame; `animating()` true keeps frames coming without a feed change. The base: the unit's scene
  plot (`plotX`, `plotY`; a carrier's cargo is on the carrier's plot), its centre by `c5r::units::plotCentre`.
- Height: **no**. The placement samples the ground at the root's slots (`root_z` is not read, 4981.3) and
  `ApplyUnitXforms` turns and scales about P with no translation (4981.4), so a unit cannot be lifted. Needed: **H2**.
- Flying pose: **no**. The art's flight clip is the 1400 (Run) clip: `fighter.fxsxml` has only Run 1400, Idle 1000,
  Attack_Air 1100, Bombardment 1180 and the deaths 1200 / 1220 / 2200 / 2220 [C, the install's UnitModels.fpk as
  extracted by 0327, read 2026-11-20]; `airplane_state_01` defaults to 1400 and lands on 1000 (unit-controller.md 6.2);
  since 4405 an air figure is created parked (1000). `posesample` on `fighter_run.gr2` (1 s loop, 16 tracks): every body
  bone stays at its rest translation (z within 6e-5) [C, 2026-11-20 10:31]: **the clip carries no altitude**; the EXE
  lifts a flying plane itself. So the flying look is the 1400 clip at a lifted root, and the clip needs a request the
  seam cannot make: **H3**.
- No take-off or landing clip exists for the fighters (the list above); the arcs are root motion only.

**The stock flight height: not in the knowledge base, not in the data [C, negative].** `ArtDefine_UnitMemberCombats`
`AttackAltitude` is set only for missiles and the atomic bomb (rocket 50, guided missile 90, atomic bomb 110, nuclear
missile 160); every fighter and bomber row has none, and no air member has a `ZOffset` but three members of a Smokey mod
(10, 12). *(Corrected 2026-11-20 10:50 by lead-0455 / lead-0042 #8163; was: "the height the EXE flies at is in its code,
unchecked; proposed H = 40 world units above the base's ground [I]".)* The EXE's combat record reads `AttackAltitude`
with a **default of 90** when the row has none [C, epic 0042's 4776 M1, `unit-combat-server.md` on `epic-0042-combat-
movement` "Aircraft out and back"], and the bombers' figures in a stock strike stood at **z 90** over a submarine at z 10.1
[L, 0477, `combat-choreography.md` 13.3]. Every interceptor member in VP's database (Fighter, Jet Fighter, Zero, WW1
fighter, SPAD, the steam fighter) has no `AttackAltitude`, so they would strike at 90. The patrol is flown at **world
z 90** (the look's `altitude`, absolute): **the strike height, not measured live for a patrol** (named in every picture).

**The loop.** Centre: the base plot's centre. Radius: `f x range x 64` world units (64 = the plot lattice's x spacing;
f = 0.5 by C1's recommendation: a fighter's 8 gives 256). Constant ground speed **v = 24 world units/s** (a fighter's
ring of radius 256 takes 67 s) [I, a look; nothing to match]; counter-clockwise seen from above. The formation turns
rigidly with yaw = the tangent's angle + pi/2 (the placement's yaw: facing 1, east, is 90 degrees). Phase per aircraft:
`2 pi x hash(owner, id) / 2^32`, so several patrols of one base spread round the ring; a patrol's planes are its own
formation (FighterWing), turned as one. Lift H by H2. Map wrap: the root's x is taken modulo the map's world width on a
wrapping map [I: whether the scene's wrap copies accept a root past the edge is to check].

**The arcs.** Kind 1000 transitions in `onBatch`: a[2] == 5, a[3] != 5 (the order given): take-off; a[3] == 5, a[2] != 5
(the order ends, by any cause): landing; a[2] == a[3] == 5 (a load's setup send): on the ring at once, no arc. Start
time: the next `beginFrame`'s `tMs`. Take-off: T = 6 s from the base centre to the ring point of (phase at t0 + T),
along a quadratic Bezier whose end tangent is the ring's (no kink), lift 0 -> H by smoothstep. Landing: T = 6 s from the
ring point at t1 back to the base centre, the start tangent the ring's, lift H -> 0, then nothing (stock's parked
formation). A unit that dies or leaves the table mid-arc is dropped. Held in a fight (0042): nothing (the seam ignores it).

**Which units (E7).** An aircraft (domain 1), alive, whose owner's team (`PlayerRecord::team`) is the active team (the
first player row's `activeTeam`; FixAirBases' `AirTeamOf`), with activity 5 (the extra's own map from kind 1000) and a
range > 0 (H1). It is shown (the seam's show) while on the ring or an arc: stock may hide it after a live plot change
(4985.1), and a patrol drawn must be visible. Off (flag 0): no module, or a module with nothing in effect: P1 byte-equal
(E2).

**The hunks asked of lead-0455 (and lead-0042 on thread 0042, MC #7660; none written before their word):**
- **H1** (`server/src/units/srv_units_core.{h,cpp}`, `srv_units_scene.h`): `Lookups` reads `Units.ID, AirInterceptRange`
  into `std::map<int32_t, int32_t> airInterceptRange` (optional: a failure leaves a `why`; the extra is then off, E6);
  `UnitExtrasFrame` gets `const std::map<int32_t, int32_t>* airInterceptRange` (null: none). No other path changes.
- **H2** (`srv_units_scene.h` `UnitOverride`, `srv_units_core.cpp` `ApplyUnitXforms` and the seam's range check): `float
  lift = 0` (world units, [0, 200]), applied with turn and scale as a translation of the result, `[L A | P + L (b - P) +
  (0, 0, lift)]` on the bone rows and the world rows; counted with `ov`. Default 0: every path as before.
- **H3** (`srv_units_scene.h`, `srv_units_core.cpp`, `srv_units_anim.cpp`): `UnitOverride::flying` (a level); the host
  keeps the last value per unit key and on a change gives the controller that unit's `move` (1400, the flight clip) or
  `arrive` (1000, parked) in the frame's close, as AnimFeed does for kind 12 and the arrival. The same lever 0042's air
  combat (4777: strike and rebase flights) needs; if 4777 has or plans one, 4986 reuses it. Without H3 the planes fly in
  their parked pose (a still propeller): acceptable for the first pictures, named in them.

**Candidates for the owner (offline pictures on the c7 bundle; stock / off / on, a frame sequence along the loop):**
(a) C1's recommendation: ring at half range at z 90; (b) ring at the full range at z 90 (the real reach; a fighter's ring
of 512 units leaves the screen at the default zoom); (c) half range, 40 above the base's ground (lower, closer to the
city's buildings). *(Heights corrected 2026-11-20 10:50 with the paragraph above; was H = 40 / 40 / 20.)*

**Lead's word (2026-11-20 10:39 #8140, 10:47 #8163, lead-0042 agreeing):** H1 and H2 written by 4986 (below); H3 only
after 0042's 4726 (W20, `srv_units_anim.cpp`) is on 0042's team branch, its exact hunk to the lead first; 4777 has no
flying pose outside fights. Until H3, pictures show the planes in their parked pose, named so.

### 4986.1 The extra (`server/src/extras/units/srv_unit_intercept.{h,cpp}`) [C: code and unit tests]

`MakeAirInterceptPatrol(id, look, ranges)`; not registered until row 27 lands with the lead's landing commit (the row
text, the maker entry and the OPTIONS.md text are in the issue log). Per table unit (`InterceptChoice`):

| Unit | What the extra does |
|---|---|
| not domain air | nothing |
| an aircraft with no patrol (no kind 1000 to 5, no landing under way) | nothing: stock's parked formation |
| dead or delayed death; held in a fight | nothing |
| another team's (E7), or no player row | nothing |
| its plot outside the grid; its unit type's range 0 or unknown | nothing |
| own or team (D207), on intercept or landing | root, heading, lift (and show, if stock hides it) |

- **Team (E7):** `AirTeamOf` (FixAirBases'): the owner's `PlayerRecord::team` equal to the row's `activeTeam` (one value
  in every row); so an ally on the active player's team is drawn flying (test `intercept_load_ring`, player 3 on team 0).
- **State per aircraft:** a map (owner, id) -> mode (ring, take-off, landing), the start time (the next frame's clock after
  the kind-1000 event), the last pose drawn. Kind 14 (destroyed) drops it; kind 32 (load begin) clears all (the load's
  setup sends kind 1000 again with old == new). `animating()` is true while any entry exists, so the scene keeps drawing
  frames without a feed change while a patrol is shown.
- **Height:** the look's `altitude` (world z 90) less the ground under the base gives the flight height H above the base;
  the lift given to the seam is `base ground + H - ground under the root` (so the plane flies level over hills), at least
  10 units while flying, at most the seam's 200. The ground outside the height grid reads 0 (`heightAtWorld`).
- **Ranges:** the frame's `airInterceptRange` (hunk H1) when it has rows, else the maker's table (tests); an empty table
  is an input missing: E6, one line `units extra AirInterceptPatrol: LOUD: failed: beginFrame: no intercept ranges
  (Units.AirInterceptRange was not read): off for the session`.
- **The ring step:** the distance of plot centres (0, 0) and (1, 0) of the frame's field (64 on the EXE's lattice).
- **The landing** ends at the base with the speed the Bezier gives there (about 2 |P2 - P1| / T: 88 units/s for a
  fighter's half-range ring), then the parked formation at once: a hard stop [I, a look to judge in the pictures].
- **Not done:** the map's x wrap (a ring that crosses the wrap seam is drawn past the edge; whether the scene's wrap copies
  draw it there is not checked); the flying clip (H3); the EXE's unit flag, selection ring and shadow stay at the base
  (P2); no shadow of the flying planes (the EXE's mask pass has them at the base).

### 4986.2 The seam hunks (agreed by lead-0455 and lead-0042, #8163) [C]

- **H1:** `Lookups::airInterceptRange` (`Units.ID, AirInterceptRange`; a NULL cell 0) read at the end of `Lookups::load`,
  optional (`airInterceptWhy`); `UnitExtrasFrame::airInterceptRange` points at the core's copy (`UnitsCore::buildList`).
  `srv_units_scene.h` includes `<map>`.
- **H2:** `UnitOverride::lift` (world units, [0, `kMaxUnitExtraLift` = 200]), `UnitXform::lift`; `UnitsCore::buildList`
  puts an xform for a unit with a lift; the core's range check and the module's `OutOfRange` refuse a lift that is not
  finite or outside [0, 200] (the core: `x_bad`; the module: the extra fails, E6); `ApplyUnitXforms` adds the lift to z of
  every bone row triple and every material-87 world row after the turn and the scale (`[L A | P + L (b - P) + (0, 0,
  lift)]`). A lift counts as `ov` in the module's counts. With no extra giving a lift every path is as before.
- **H3** (the flying clip) is granted (MC #8189) for after 0042's 4726 is on its team branch, the exact hunk agreed on
  thread 0042 first; not written.

### 4986.3 Tests [C]

`server/tests/extras/units/test_air_intercept_patrol.cpp`, built by `build_units_extras_tests.py --test
test_air_intercept_patrol`, synthetic tables on a flat 10 x 8 grid (city A at plot (3, 2)):

| Case | What it shows |
|---|---|
| `intercept_math` | the phase stable and spread (8,000 ids over 8 sectors, each 850-1,150); `RingAngle` (speed / radius per s, wrapped); `RingPose` positions and yaws (counter-clockwise; facing 4 west = -pi/2); take-off from the base (lift 0) to the ring point (its lift and yaw), lift monotone, end speed 24 units/s; landing from the ring point to the base; the three looks |
| `intercept_choice` | every kind of `ChooseIntercept` (a hidden own aircraft is flown too) |
| `intercept_load_ring` | after a load (kind 1000 with 5, 5): the own and the team fighter on rings of 256 round their bases, the asleep fighter, the bomber (range 0) and another team's fighter untouched; the heading tangent; the lift 90 in the frame's xforms; one second later 24 units further, same radius |
| `intercept_arcs` | the order given (0 -> 5): at the base with lift 0 at its first frame, off the base half way, at 6 s exactly the ring's own point and height (90 less the ground), 50 ms before within 1.5 units of it; the order ends (5 -> 0): the landing starts at the last pose, ends at the base with lift 0, then no override and `animating()` false |
| `intercept_off` | E2: the flag off and the real extra with no row give the scene of no module (roots, seeds, fades); E5 `off: ServerUnits = 0`; E6 no ranges: one LOUD line, the scene stock |
| `intercept_db_ranges` | H1: ranges from `Lookups::airInterceptRange` (a range of 4: ring 128) with no maker table |
| `intercept_held` | held in a fight: no override, flown again once released; kind 14 ends a patrol; kind 32 clears all |
| `intercept_fixed_ring` | look (d) (W12): a ring of 96 (1.5 plots) round the base with a range of 8 |
| `intercept_cost` | E8 indication only (below) |

And in the seam suite (`test_units_extras.cpp` `scale_heading`): a lift alone raises every bone and world row of its
unit by it and nothing else; a lift given by an extra reaches the frame's xforms through the core.

**Results (2026-11-20, workstation, clang-cl x64, c5render built from this branch; every run through `quietlock.py run`):**
`test_air_intercept_patrol` 8 of 8 (11:07 EST, @3152d1e75); `test_units_extras` 12 of 12 and `test_fix_air_bases` 7 of 7
with H1 and H2 (10:55, @12018c7f8 plus the `<map>` include). Cost indication (E8, not a result, PERFORMANCE.md): a
2,000-unit build with 200 own fighters on intercept, 41 builds per arm, median without a module / with the extra on:
998 / 1,269, 794 / 905, 674 / 841, 741 / 887, 681 / 892 us in five runs (10:59-11:07 EST, other teams' work running):
about +110 to +270 us for 200 flying units (per unit a map lookup, two plot centres, three height samples, a cosine and a
sine).

### 4986.4 The scratch player (W12, 2026-11-20)

Source `C:/c5-64bit-work/0455/4986/src` (not in git): `epic-0454-extras-a`'s folders the player reads (W9's
`mkscratch2.sh`) + this branch's changed files under them + W5's rows 23-26 patch + row 27 (`row27.py`) + W3's two player
hunks. The reduced source lacked two folders the build reads: `shell/composite` (`srv_cut_tap.h` includes
`shell/composite/cut_tap.h`) and `proxy/include` (`srv_liveterrain_icewaves.cpp` and `srv_buildid.cpp` include
`vp_livereload.h`, `vp_buildid.h`); both taken from `epic-0454-extras-a` (this branch changes neither). Built by
`build_sceneshot.py --out C:/c5-64bit-work/0455/4986/ss` through `quietlock.py run` (`ssbuild.sh`).
*(A reduced scratch source for a `build_sceneshot.py` player needs `options perf render server shell models deps proxy/include
tools/x64-dll tools/visualcheck`; leaving out `shell/composite` or `proxy/include` fails in "the common objects".)*

**An offline player must not read the game files of an instance a live run holds [L, 2026-11-20 11:46-11:58 EST].** The
player reads the mod activation from the instance's `cache/Civ5ModsDatabase.db` at each start (`render: decals: mods: N
activated`). With c5h-c claimed by another ticket's live game (`pool.py list`), the first run read 3 mods and was
byte-equal; the next read **0 activated** (the live game had the database open and was rewriting it) and failed `8
ReplayFailed: decals: ... texture 'Rice_Random_D.dds' is not in the install or the mods' folders` (a Vox Populi texture);
the run after read it again. Check `pool.py list` and take a free instance's files (here c5h-a, mods548, build 4390913)
for offline runs; an exit 8 with a missing mod texture means the mods were not read, not a fault of the extra.

### 4986.5 Pictures (DIAGNOSTIC, D112; offline, scratch player)

[L for what the frames show; 2026-11-20 11:59-12:33 EST, workstation, RTX 4080, the scratch player of 4986.4, c5h-c's
install with a private copy of its MODS and mods database] `pics.sh r1`: bundle `C5H-4980-C7INTERCEPT_0009` (8 dumped
frames 2415-2474, the named camera `c7_intercept`, the same view) and `C5H-4980-C5AIR_0009` (8 frames 1996-2044). Pictures
in `F:/c5-64bit-work/0455/4986/r1/` (contact sheets in `r1/sheets/`), not in git; each recorded in the Visual Checks feed
(`--issue 4986`). Every frame was looked at on the contact sheets (MC #7615: more than one frame per reading).

| Run | Result | Reading | Check |
|---|---|---|---|
| c7 off (`AirInterceptPatrol=0`) | 8 of 8 byte-equal | fighter 1070 parked at its city (46, 8) | vc-20261120-124458-workstation-54cb |
| c7 on, look a (half range) | exit 10, 8 differ | the parked formation leaves the city (only the EXE-side shadow stays there, P2) and flies at the picture's lower-left over the sea, moving along the ring from frame to frame | vc-...-124504-...-fe2c (2415), vc-...-124509-...-c48d (2474) |
| c7 on, look b (full range) | exit 10, 8 differ | the fighter leaves the city; its ring is **outside this camera in all 8 frames** | vc-...-124522-...-d98a |
| c7 on, look c (half range, lift 40) | exit 10, 8 differ | as a, lower, at the lower-left edge | vc-...-124526-...-239c |
| c7 named camera, look a | 1 shot | as look a frame 2415 | vc-...-124532-...-21cd |
| c5 on (no aircraft on intercept) | 8 of 8 byte-equal | nothing flies (E9b) | vc-...-124835-...-81b6 |

**Why the ring sits at the picture's edge** (a debug player, `ss2`: the scratch source with a stderr line per flying
aircraft and two scratch-only switches, ring radius and no turn; never product): the fighter is unit type 36 with
`AirInterceptRange` **8** in the bundle's database (read through H1), so look a's ring has radius 0.5 x 8 x 64 = **256**
(4 plots) round the base at world (1183.9, -554.2), height H 78 above the base (z 90 less its ground 12); look b's 512
(8 plots). This camera is close: a plot is about 250 px wide at 2554 x 1411 [I, from the city's first-ring border], so a
4-plot ring reaches the frame's edge and an 8-plot ring leaves it. With the radius forced to 0 the formation sits straight
above its city, raised by the lift (vc-...-124550-...-5511); forced to 128 (2 plots) it sits at its ring point south-east
of the city (vc-...-124556-...-e34e). So root, lift and ring are where the extra puts them; nothing is lost on the way
(first reading at 12:10 "no aircraft visible" was the look-b picture alone; corrected by the look-a sheet and the debug
pictures, 12:30).

**For the owner's choice of a look:** with VP's fighter range 8, half range is already 4 plots: on a close camera the patrol is
mostly off screen and the base looks empty (the stock parked formation is gone, the EXE's shadow and flag stay at the
city). A ring tied to a fixed small radius keeps the aircraft near its city in view: **look (d)**, added at 12:57 on the
lead's word (#8298): `PatrolLookD()`, `radiusPlots` 1.5 (96 units) at z 90, the range still deciding who flies (range 0:
parked). c7 on, look d (13:05 EST, the look-d player `ss2`): exit 10, 8 differ; **the formation flies just east of its city,
in view in all 8 frames, moving along the ring** (vc-20261120-130642-workstation-acab, sheet `r1/sheets/sheet8-c7-on-d.png`).
The four looks go to the owner as one batch (D207); no default is chosen here.

### 4986.6 Rule A and the scene test

**Rule A** (`rulea.sh`: `sceneshot --dumped --compare --tolerance 1:1 --no-pictures`, every option at its default, the
16 library bundles without `-SYNHIDE`; the scratch player `ss` with H1, H2 and the extra registered) [C, 2026-11-20
12:44-12:53 EST, workstation, c5h-c's install + the private mods copy]: **8 of 16 so far, each "OK 8 shot(s), each
byte-equal to the server's own dump"** with `units AirInterceptPatrol: off: EnableVisualAdditions = 0 (default)`: 0301
FOG, HUGE, SCNB (four), TINY; 4980 C1MOUNTAIN. Stopped at a bundle boundary at 12:53:49 under MC #8300 (one library
sweep at a time across teams, F: reads at ~390 ms); `rulea.sh` skips the bundles already byte-equal when it resumes.
**The other 8 are open** (the lead gives the slot).

**Scene test** `test_air_intercept_scenes.py` with the same player (`scenetest.sh`, 13:01-13:04 EST): **4 passed in
212.03 s** (off on c7 and c5 byte-equal; on flies c7's interceptor, every frame differs; on leaves c5's based aircraft,
every frame equal).

**Unit tests** with look (d) (`t1w.sh`, 12:57 EST, @e8c4eda92): `test_air_intercept_patrol: 9 of 9 cases passed` (the new
`intercept_fixed_ring`: a ring of 96 round the base with a range of 8).

### 4986.7 Not verified (state 2026-11-20 13:07 EST, W12)

*(Superseded 2026-11-23 by 4986.8-4986.9: rule A on every library bundle passed (17 tests), the arcs and the ring were
seen live with a define-on DLL, and the product's pin DLL sends no render messages; what is still open is listed in
`server/evidence/0455/4986/acceptance-4986.txt` "NOT DONE". The list below is W12's, kept as it was.)*

- Rule A on the last 8 of the 16 library bundles (4986.6; the first 8 byte-equal).
- The take-off and landing arcs in a picture: c7 starts with the fighter already on intercept (kind 1000 with 5, 5 at the
  load), so no bundle has an order given or ended; the arcs are checked by the unit tests only.
- What the player sees at the base with the flag on: the EXE's flag, selection ring and shadow stay at the city (P2) while
  the planes fly; offline pictures do not draw the EXE's part except the shadow mask.
- The flying clip (H3), the map's x wrap of a ring, another team's interceptor on a bundle, a carrier-based interceptor
  (the base follows the carrier's plot), and anything live.
- The height: world z 90 is the EXE's `AttackAltitude` default for a strike [C 4776 M1] and the bombers' figures in a
  stock strike [L 0477]; a stock patrol has no flight to compare with.

### 4986.8 On main: the range source decided, the promotion line, the registration (impl-4986, account B, 2026-11-23)

Branch `task-4986-airbases-intercept` from main 6a3d5b9fc with `w12-4986` (3d87eefe9) merged onto it at 07:40 EST (the
conflicts were additive: 4983's `UnitOverride::perimeterEdges` and H2's `lift` side by side, with both range checks in
`srv_units_extras.cpp`), then main e95d81514 merged.

- **The range source, decided by the MC (#11389, 2026-11-23 07:4x, via MC-B #11390; asked first as the ticket says,
  #11386):** the database base range `Units.AirInterceptRange` (hunk H1), no DLL change and no new render kind. Reason
  (4986.0): the DLL's `GetAirInterceptRange` is the row plus promotions' `AirInterceptRangeChange`, and VP has no such
  promotion. **The MC's addition, built:** at the database load the scene's `Lookups::load` also reads
  `UnitPromotions (Type, AirInterceptRangeChange)` and keeps the types with a value other than 0
  (`Lookups::airInterceptPromotions`; a missing column sets `airInterceptPromotionsWhy` and nothing is said: not
  checked). `UnitExtrasFrame::airInterceptPromotions` hands the list to the extras (null when not checked). The extra
  turns a non-empty list into one remark, `units extra AirInterceptPatrol: N promotion(s) change the intercept range
  (<types, at most 8, then "and K more">): the ring shows the unit row's base range (Units.AirInterceptRange) only, not a
  promoted aircraft's reach`, which the module logs once after a successful `beginFrame` through a new optional hook of
  the unit-extra interface, `UnitExtra::takeNote(line)` (default: nothing; `srv_units_extras.h`). Said only when the extra
  runs (flag on, ServerUnits 1): with the flag 0 the log is unchanged. The ring itself is unchanged by the list. Unit test
  `intercept_promotion_note` (three arms: a listed promotion: the line once over three frames, `promotionNotes` 1; none
  listed and an unread column: no line; the ring at the row's range in every arm).
- **Registered:** `UnitExtraMakers()` gains `MakeAirInterceptPatrol(Vp64Opt::kAirInterceptPatrol)` after FixAirBases,
  look (a) (half range, z 90) as the maker's default until the owner picks (D207); row 27 was on main since the 0455 landing
  (9e401a7b1), default 0 under `EnableVisualAdditions`. `test_units_extras`' `garrison_registered` case now expects four
  makers in id order and the four lines of id 27 (off by the category switch, off by its flag, in effect, off by
  ServerUnits 0). `docs/OPTIONS.md` row 27 rewritten (in effect; the range source and its limit; the parked pose; the
  flag and shadow at the base).
- **H3 (the flying clip) stays unwritten:** its condition (0042's 4726 on 0042's team branch, MC #8189) is not met (4726
  is in backlog on 2026-11-23); the planes fly in their parked pose, named in every picture and in OPTIONS.md.
- **The live step (method, written before the run):** `server/evidence/0455/4986/smoke_4986.sh` (after 4984.10's
  method): a scratch copy whose maker reads `T4986_PATROL_LOOK` (0 a, 1 b, 2 c, 3 d; `look_scratch.py`, never committed),
  the server built on the 0.1b pins, `lay.py prep` + `helper on` on c5h-new-b (MC-B #11401), the kit copy
  `C5H-4986-KIT_0009` (= `C5H-4980-KIT_0009`, 74fc7428...). Per arm (off, a, b, c, d) one launch: the ring at Hilo
  (46,8) (fighter 1070 on intercept since the load: on the ring at once) at zoom min-2 and min-8, two frames 3 s apart
  each; the take-off at Aotearoa (45,13): fighter 1064 put on intercept by Lua (`PushMission MISSION_AIRPATROL`, else
  `SetActivityType ACTIVITY_INTERCEPT`; the DLL sends kind 1000 5 <- old), then shots for 10 s after the order (the 6 s arc,
  then the ring); the landing at Hilo: 1070 woken (`SetActivityType ACTIVITY_AWAKE`), shots for 10 s; one shot with the
  interface shown. Expected: off = stock (parked formations, nothing moves); a-d: 1070 on its ring at once, 1064 rises
  from Aotearoa along an arc and joins its ring, 1070 descends to Hilo and parks; 0 LOUD; the extra's line `in effect`.
  Then E1 (`acceptance.py run --config inc2`, kit copy autoplayed 9 -> 19 with the flag 1 and 0, `statscmp`), `end`
  (restore by hash) and the stock reference on the restored instance.

### 4986.9 The live step (impl-4986, 2026-11-23, c5h-new-b, EXE build 26577625; MC-B #11401, terms MC #11423)

**On the 0.1b pins the extra draws nothing [L, 08:03-08:21 EST, `F:/c5-64bit-work/4986/out1/`].** Layout: server.exe
c217c2fc... built from a scratch look copy of this branch at 370e19ab8 on the 0.1b pins (release, no LTO), the pin's
x64 DLL b84aa3e9..., proxy d72290de..., composite ec33c4d5...; options `ServerUnits = 1`, `EnableVisualAdditions = 1`,
`AirInterceptPatrol = 0|1`. server.log: `render-events: the DLL has no DllGetRenderEventsVersion export (built without
VP64_RENDER_EVENTS): it sends no render messages`. The extra said `in effect` but received no feed kind 1000, so no
aircraft was known to be on intercept: `extras AirInterceptPatrol on ov=0` all session, arm a's pictures equal arm off's
(the interceptor parked at Hilo). **The render messages (fork branch `x64-render-events` addd57b8b, research
03-dll-engine-boundary/render-events.md 2) reach the product only with the next x64 pin batch; until then
AirInterceptPatrol changes nothing in the shipped system** (default 0, so nothing visible either). The 27 Visual Checks
entries of this first series (arms off and a) were amended one by one to say so. Consequences, by the MC's terms (#11423):
the extra now says once per session `units extra AirInterceptPatrol: no unit activity message (feed kind 1000) since the
load: the DLL sends no render messages (built without VP64_RENDER_EVENTS), so no aircraft is known to be on intercept and
none flies` after 5 s of frames with a unit table and no kind 1000 (not LOUD; unit test `intercept_no_activity_note`);
`docs/OPTIONS.md` says the line needs a DLL with the messages; **4986's acceptance stays open on the shipped-system
pictures until the next x64 pin batch carries `x64-render-events`**.

**The arms with a define-on DLL [L, 08:28-09:00 EST, `F:/c5-64bit-work/4986/out2/`].** A FAST define-on x64 DLL (the
0.1b pin x64-0.1b 1b87dd185 + `cherry-pick -x addd57b8b` = 9b2e7d018 on a local scratch branch `t4986-re` of an own
`--shared` clone, never pushed; `perf/speedkit/kitbuild.py --config fast` (no LTO) with the pin's marker defines
VP64_HIDDEN_EMBARK, VP64_SPEED_DANGER, VP64_SPEED_DANGER_DG7; `render_events: on`; 335 s; SHA-256 1eee1328...;
`C:/c5-64bit-work/4986/fork.sh`) swapped into the same layout (`live-re`), laid on the claimed instance only and
restored by hash. server.log: `render-events: the DLL sends render messages, version 1`; at the stop `41 calls: activity
39 (at setup 35), carrier 2`. Every picture DIAGNOSTIC (D112: a test layout, a scratch server build, a fast define-on
DLL), interface hidden, popups closed; the planes in their parked pose (no H3).

| Arm | Lines | Counters (last `units 1s`) | Pictures (Visual Checks) |
|---|---|---|---|
| a (look a, the code's default) | `units extra AirInterceptPatrol: in effect` | table 44, listed 35, `x root=6731 heading=6731`, `extras AirInterceptPatrol on ov=6858 failures=0` | ring Hilo near vc-20261123-083111-workstation-5682 / -083117-...-fb3f, far -083129-...-8081 / -083136-...-3477; take-off Aotearoa before -083156-...-c23f, +0.0 s -083159-...-dd97, +5.1 s -083212-...-78cb; landing Hilo before -083240-...-5877, +0.0 s -083251-...-5f26, +8.6 s -083256-...-9e63; interface shown -083314-...-992d |
| off | `units AirInterceptPatrol: off: AirInterceptPatrol = 0 (from the file)` | table 44, listed 34, `x root=0`, `ov=0` | ring near -083937-...-dfe5 / -084050-...-ea7b, far -084206-...-a84a / -084311-...-2654; take-off before -084442-...-a616, +0.0 s -084540-...-0a16; landing before -084755-...-e128, +0.0 s -084857-...-24ff; interface -085034-...-2d4e |

What the pictures show [L, by eye, every reading on two frames or more]:
- **Off:** fighter 1070 stands as its parked three-plane formation at Hilo (46,8), as stock draws it (4980 c7 [L]); the
  take-off and landing orders change nothing visible.
- **(a), the ring:** Hilo's parked formation is gone; the formation flies over the sea south-west of the city at about
  4 plots with its shadow on the water below (the server's unit shadow), and in the second far frame (7 s later) it has
  moved east along the ring. Look (a)'s ring fits the far camera (zoom min-8); at the near camera (min-2) it is at or past
  the frame's edge (as offline, 4986.5).
- **(a), the take-off:** after the order (`PushMission MISSION_AIRPATROL`, activity 2 -> 5) fighter 1064 leaves
  Aotearoa (45,13); at +5.1 s its formation flies over the lake north of the city with its shadow, near its ring.
- **(a), the landing:** at the order (`SetActivityType ACTIVITY_AWAKE`, activity 5 -> 0) 1070 is on its ring south-east
  of Hilo; at +8.6 s (after the 6 s arc) it is no longer over the sea: it has landed and the scene draws it as stock does.
- The shots took 3-13 s each on the busy machine (other teams' builds and take 7), so no frame caught an arc in its
  middle: the arcs' shape stays checked by the unit tests (`intercept_arcs`) only.
- Both arms: 0 LOUD lines in render.log; one LOUD line in server.log in both, `database: LOUD: the DLL imports
  ?Bind@Results@Database@@QEAA_NHM@Z, which the write recording does not cover`, from the fast define-on DLL (the pin DLL's
  arms in out1 do not have it) and unrelated to the extra [I: the fast configuration's inlining; not followed up].
  No box, nothing from `crash.py collect`.
- **Arm d's first launch failed** (`LaunchStartupTimeout` at 08:55, the EXE alive but not ready in time on the busy
  machine; no box, no crash dump, `end` stopped it and restored the instance by hash at 09:00). Looks d, b, c and the stock
  reference ran after E1 (below).

**Looks d, b, c and the stock reference [L, 09:08-09:28 EST, `F:/c5-64bit-work/4986/out4/`, the same define-on layout,
a fresh prep; stock after `end`: the instance as provisioned, its own 32-bit DLL, no patch].** Each arm `in effect`, 0
LOUD in render.log, `failures=0` (`ov` 5256 d, 5244 b, 5051 c); no box, nothing from `crash.py collect`. Visual Checks
(prefix `vc-20261123-`, suffix `-workstation-`):

| Arm | Ring Hilo near 1 / 2, far 1 / 2 | Take-off Aotearoa (s after the order) | Landing Hilo (s after the order) | Interface |
|---|---|---|---|---|
| d | 091142-a811 / 091148-5b8f, 091200-b62e / 091205-ba9b | before 091219-5994, +0.0 091224-6add, +4.9 091228-63bc | before 091247-aeba, +0.0 091252-5618, +5.3 091257-4e4c | 091316-5265 |
| b | 091635-ff92 / 091641-80c0, 091653-49fe / 091700-2317 | 091714-15bc, +0.0 091717-b761, +2.5 091719-ccc0, +5.3 091722-f590, +8.0 091725-6706 | 091740-2710, +0.0 091743-86f1, +3.0 091746-f1e5, +5.8 091749-43ee, +8.5 091751-3a99 | 091806-2948 |
| c | 092114-3a2c / 092120-b7f5, 092132-64f3 / 092138-86be | 092154-d1d6, +0.0 092157-02a2, +2.5 092159-a2c1, +5.1 092202-d749, +7.6 092204-3495 | 092220-44ce, +0.0 092223-b818, +3.2 092226-b624, +6.1 092231-0fa3 | 092245-d3b9 |
| stock | 092558-6109 / 092604-e992, 092617-3aa7 / 092623-0710 | 092636-c727, +0.0 092639-0842, +2.7 092642-053e, +5.5 092645-0873, +8.4 092648-3332 | 092702-a143, +0.0 092705-a340, +2.8 092708-f099, +5.8 092710-9c15, +8.0 092713-f659 | 092727-6a40 |

What the pictures show [L, by eye, two frames or more per reading]:
- **Stock (EXE build 26577625, patch off):** fighter 1070 parked at Hilo as a three-plane formation among the houses,
  the same picture as the server's off arm (crops of `stock_ring_hilo_near_1` and out2's `off_ring_hilo_near_1` side by
  side): the server copies stock, and stock has no patrol (4980's reading holds on the new EXE). In stock's landing frames
  at +5.8 s and +8.0 s the parked formation at Hilo is not drawn (one plane's shadow stays) after the Lua wake: stock's own
  behaviour after an activity change of a based aircraft [I: not followed up; FixAirBases' 4985.1 cause, the DLL's
  `updateCenterUnit` after a change on the plot, is the likely one].
- **(d) a fixed 1.5-plot ring:** the formation circles close to Hilo and stays in the near camera; at z 90 and this
  camera's tilt the planes appear well above-right of their shadows, which fall beside the city; it moves between the two
  frames. The look that keeps the patrol in view at the zoom players use most.
- **(b) the full range (8 plots):** the formation is at the far camera's edge over the fog (south), out of the near view;
  the base looks empty.
- **(c) half range, 40 above the base:** the ring of (a) flown low; the formation passes over the neighbouring land and
  cities west of Hilo, close to the ground.
- **The arcs, caught mid-way in b and c (shots 2.5-3 s apart in this window):** take-off at +2.5 s: the formation is
  between Aotearoa and its ring, climbing; landing at +3.0 s (b) and +3.2 s (c): the formation north-east / north of Hilo
  on its way in, lower; at +5.8 s (b) still in the last part of the arc next to the city; at +8.5 s (b) and +6.1 s (c)
  parked at Hilo as stock draws it. So start and end of both arcs and their middle are seen; their exact curve stays the
  unit tests' (`intercept_arcs`).
- One frame, c's landing at +6.1 s, shows Hilo's plot without its houses (dark ground) and two neighbouring tiles changed
  (a ruin's tile green, crops gone), while +0.0 s and +3.2 s show them as usual: a whole-picture change of the terrain and
  city layers in that frame, not of the aircraft [I: the extra only moves aircraft; the server's terrain or city layer
  updating at that moment; not followed up, no other frame of the five arms shows it].

**Recommendation for the owner's pick (D207), unchanged in substance from 4986.5:** **(d)** for the default when the flag is
turned on: the patrol stays in view near its base at the usual zoom; (a) and (b) put a VP fighter (range 8) 4 or 8 plots
away, mostly off a close camera, so the base looks empty (the EXE's flag stays there); (c) reads as low flying over
neighbouring cities. The maker's default stays (a) until the owner picks (one line in `srv_unit_intercept.h`).

**E1 PASS [L, 09:01-09:08 EST, `F:/c5-64bit-work/4986/out3/`, the same define-on layout, a fresh prep].**
`smoke_4986.sh e1`: `perf/profilekit/acceptance.py run --config inc2 --no-ledger`, the kit copy `C5H-4986-E1K_0009`
autoplayed turn 9 -> 19 with `AirInterceptPatrol = 1` (look a; 09:01-09:04, 195 s) and `= 0` (09:04-09:07, 199 s), both
windowed on c5h-new-b (D164), both exit 0 (no stop fault). In the on run the extra flew (`extras AirInterceptPatrol on
ov=9397 failures=0`; `render-events: 1426 calls: activity 1422 (at setup 35), carrier 4`). `tools/d10/statscmp.py
e1on/stats.db e1off/stats.db --from-turn 9 --expect-turns 9 19`: **IDENTICAL, 15 tables, 2,509 rows**, turns 10..19 in
every per-turn table. Display only, as designed: the extra reads the feed and never calls the DLL.

**E8 live indications [L, one run per arm, no lock, the workstation busy (take 7, other teams' builds and this ticket's
own server build): not a claim, PERFORMANCE.md].** From the acceptance runs' `per-turn.csv`: server.exe private bytes on
1,099.7 / 1,122.7 / 1,135.5 MB (turns 9 / 12 / 19), off 1,104.8 / 1,130.1 / 1,144.2 MB (turns 9 / 13 / 19): no growth
from the extra within one-run noise; the EXE's private bytes 1,323.8-1,369.8 on, 1,330.3-1,366.0 off; ten turns 18.1 s on,
17.2 s off (elapsed from the first sample). The offline cost (4986.3, `intercept_cost`) stays the extra's per-frame
indication.
