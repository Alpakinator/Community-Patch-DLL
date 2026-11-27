# The server's unit scene: the unit feed in the server, the seed and the colour, the draws in the server's frame, the live comparison (epic 0375, U6 + U7)

Drafted 2026-11-15 22:40-23:11 EST by worker W-4451 (task 4451, branch `task-4451-c1-draft`) for lead-0375, as the C1
note of `docs/briefs/epic-0375-units-server-scene.md` (scope 0). **Status: C1 draft of 2026-11-15, awaiting the MC's
review answer.** Reviewed by lead-0375 2026-11-15 23:19-23:22 EST: the premise marks of 4459 filled in, section 4,
the failure table and decision 4 revised, decision 14 and the era-change event (Q6) added. The MC's answer (queue #2105, 2026-11-15 23:23 EST): **C1 accepted.** Premises and the four [C] findings accepted.
Decisions 1 (the three `build_server.py` changes), 2, 3 (a), 4 revised, 5, 6, 7 (c), 8, 9, 10, 11, 12, 13 and 14 (a) as
recommended. 7 (c) changes 0040's filter rule (keep the EXE's unit shadow-mask pass, drop only the pass-0 draws; the MC
logs it in 0040) and **holds until true absence (0328), which will need server-drawn unit shadows**. 14 (a): the hold's
length is measured live (4453) and every ambiguous case counted LOUD, never guessed. Q1 granted (the `pure-methods.tsv`
rows of section 13), Q2 granted (`srv_remote_main.cpp`: the database copy's path and `WriteTotals`' caller), Q3 after
0364's merge with decision 11. Q5: never `immutable=1` on a file the DLL is writing; read a consistent copy (SQLite's
backup API or the server's own in-memory database) and test a read during a write. Q6 granted in code (kinds 32
`kLoadBegin`, 33 `kEraChanged`); Q7 accepted (`a[3]` = 0, the value in the combat record); both go into
`unit-feed-contract.md` after 0374's merge, drafted in section 2.1 of this note meanwhile. DoD A-I as in section 15.

Marks: `[C]` read in this repository's code, the DLL fork's source or a dump as recorded in the files named; `[L]`
measured live or on captures by the issue named; `[I]` inferred or proposed. Code read 2026-11-15 on
`epic-0375-units-scene` (`48c05474`); the DLL fork read in the local checkout `Community-Patch-DLL-x64` (`e9d0c4837`),
to be re-read at the pinned commit by 4452 / 4459. Nothing here is product code; no EXE bytes or decompiler text (D83).

## 0. In short

- **Feed (sections 1-2).** The 18 unit calls and 3 new city calls of `unit-feed-contract.md` 2.1 (kinds 11-31) are
  taken in `RemoteEngineUtility` (`server/src/remote/srv_remote_objects.cpp`) by new `SrvRemoteFeed::OnUnit*` functions,
  as `GameplayCityCreated` -> `SrvRemoteFeed::OnCity` does today, before the call is forwarded. One new writer,
  `SrvFeed::PushUnit`, assigns the event's `seq` and updates the **unit table** (`UnitRecord`, contract 2.2) under the
  feed's one SRW lock, ~~so a drained batch and `SrvFeed::CopyUnits` never disagree~~ (*corrected 2026-11-16 by 4461,
  3.4: the drain and the copy are two acquisitions of that lock, and a push between them leaves the table ahead of the
  batch [L]*; *fixed 2026-11-16 by 4463 [C]: `Drain` copies the unit table in its own lock acquisition and `CopyUnits`
  returns that copy, so the copy and the batch agree again, 2.2*). Paths and combats go to per-batch
  side tables; a **player table** (record 6 PLAYERS, decision 2) is read at the snapshot point and at each turn change.
- **Scene (section 3).** `server/src/units/` owns the renderer's copy of the unit and player tables, the seed allocator,
  the visibility fades and residency, and builds a `c5r::units::UnitScene` per drawn frame for 0041's `UnitRenderer`
  (`prepare`, `draw`); a CPU-only `UnitRenderer` runs `precheck` on a worker thread at each new database.
- **Draws (section 7).** The scene pass goes into `Scene::draw` (`server/src/render/srv_render_scene.cpp`) after the
  water and the ice and **before** `m_fow.afterWorld` (stock draws models before the cloud planes, `fog-of-war.md` 5.4),
  into the sink's colour and depth; the composite's `depth_write=1` carries that depth to the EXE's models.
- **Departures from the brief** (each argued where it occurs): the build needs three changes in `build_server.py`, not
  one (14, decision 1); the EXE's own unit **shadows** are already in the tapped ShadowBlur (`terrain-surface.md` 11), so
  decision 7 gets an option (c): 0040's filter keeps the EXE's mask pass and the server draws only the scene pass and
  the reflection pass; the reflection and mask passes need two camera rules the knowledge base does not hold (R1, R2);
  `kUnitEmbark(on)` is a creation when the DLL's `setupGraphical` finds the unit embarked [C]; the combat event cannot
  carry the returned value in `a[3]` if it is pushed before forwarding (2.1); a load mark is needed for the allocator's
  reset (S5, Q6). Open questions for the lead and the MC: section 14, Q1-Q7.
- **The seed premises (4459, 2026-11-15, read [C]) change the allocator against the brief** (section 4): the EXE's free
  list puts a freed index at the head only when it is lower than the head, else right behind it (not "last freed
  first"); embarks and era changes are remove + create, so a seed can change in a unit's life and a creation of a
  known key is legitimate; a load frees every live record but keeps the table's size, so the allocator lives as long as
  the server process; and in the human's turn with Combat on a combat-held removal lets a later creation allocate
  first (S4 false in one case, decision 14, new). C1-C3 hold as the brief assumed [C].

## 1. What the code holds today [C, read 2026-11-15]

| Piece | Where | What it gives this epic |
|---|---|---|
| Feed queue and plot table | `server/src/feed/srv_feed.h`, `srv_feed.cpp` | `Event` (32 B), `Kind` (1-10, 100-102), `Push` (assigns `seq` under `g_lock`), `Drain`, `CopyPlots`; cap 65,536 events between drains, beyond it events are dropped and `Drain` adds `kOverflow` |
| Writer extras | `srv_feed_server.h` | `UpdatePlot` masks, `Counters2::perKind[128]`, `ResetForTest` |
| DLL-side capture | `server/src/remote/srv_remote_feed.h/.cpp` | `OnCity` (getters `ICvCity1::GetOwner`, `GetID`), `NoteException` (`SrvLog("feed: LOUD: ...")`, the first 5), `AtLeaveUpdate` (snapshot / refresh point); every DLL call there is on `proxy/model/pure-methods.tsv` (bug 0103); `WriteTotals(FILE*)` **has no caller** (grep of `server/`), so the feed's totals never reach `server-report.txt` |
| Overrides | `srv_remote_objects.cpp`, `RemoteEngineUtility` | the generated base `RemoteGen_ICvEngineUtility4`; `GameplayUnitMoved` is hand-written (crossing element-wise, n capped at 65,536); the city overrides call `SrvRemoteFeed::OnCity` then the base |
| Render loop | `server/src/render/srv_render_host.cpp` `Loop` | `h.feed->drain(batch)` once per iteration, then `Scene::plotsChanged`, `Scene::fowFeed`; the `*FatalStep` calls (D125 through `HostConfig::fatal` = `SrvRemote::ReportAndExit`, exit 5, `srv_remote_main.cpp:171`); `render_*` keys in `ParseHostConfig` (`g_water*`, `g_fow`, `g_shadow`, `g_ice`: "last parse wins") |
| Frame | `srv_render_scene.cpp` `Scene::draw` | clear colour (transparent), terrain (`m_r->draw`, clears depth), water + ice hook (`m_water.draw`, `WaterSceneAfterOcean` / `IceHook`), cloud planes (`m_fow.afterWorld`); the camera `SceneFrame::cam` (`VPShellCamera`: `view[0]`, `proj`, `camWorld`, `wrapPeriod`) |
| Shadow | `srv_shadow_tap.h` | a **copy** of the EXE's ShadowBlur (2048 x 2048 R16_UNORM at quality 3/2), ShadowMatrix0 per view, the strength; the EXE's casters (units included under D33) are already in it |
| Reflection | `srv_water_scene.h/.cpp` | `m_reflTex`: 1024 x 1024 RGBA8, `D3D11_USAGE_IMMUTABLE`, cleared to (0,0,0,0) once (stock's empty reflection); no render target |
| Module | `render/include/c5r/units/*.h` | `UnitScene` / `PlayerInput` / `UnitInput` / `FigureInput` (`unit_scene.h`), `UnitRenderer::open/prepare/draw/precheck`, `PoseSource`, `PlacementSource`, `PassSet` / `PassDesc` / `Cb1` / `makeCb1` (`draw.h`), `UnitCode` (13 codes, `status.h`), `.c5units` (`scene_file.h`, limits 64 players, 4,096 units, 64 figures) |
| Build | `server/build_server.py` | `render_sources()` = `server/src/render/*.cpp` + `ring_sink.cpp`, compiled `/std:c++14` with `RENDER_INCLUDES` (no `models/include`) into `obj/render` (object name = the source's stem); the link takes `c5render.lib` and `RENDER_LIBS` only |

## 2. The data path: from the DLL's calls to the feed

### 2.1 The overrides (21 calls, kinds 11-31)

Each new override in `RemoteEngineUtility` calls one `SrvRemoteFeed` function and then the generated base
(`RemoteGen_ICvEngineUtility4::<call>`), unchanged and in the same order; `GameplayUnitMoved` calls the feed before its
`SrvRt::Begin`. Every feed function returns at once unless `SrvFeed::UnitsEnabled()` (decision 8), runs inside
`try { ... } catch(...) { NoteException("<call>"); }` and makes only purity-list calls (Q1 lists the ones to add).

| Kind | Call (slot) | Feed function (new) | Pushed `a[2]`, `a[3]` | Read before forwarding |
|---|---|---|---|---|
| 11 | `GameplayUnitCreated(unit)` (146) | `OnUnit(kUnitCreated, u, type, 0)` | unit type, 0 | the whole `UnitRecord` (2.2) |
| 12 | `GameplayUnitMoved(unit, path)` (147) | `OnUnitMoved(u, kPlots)` | path index in the batch, length | path into the side table (2.3); record: plot, facing |
| 13 | `GameplayUnitTeleported(unit, plot)` (148) | `OnUnitPlot(13, u, plot, nullptr)` | target plot index, 0 | record: plot, facing |
| 14 | `GameplayUnitDestroyed(unit)` (149) | `OnUnit(14, ...)` | 0, 0 | record: flag dead; the record leaves the table (S1) |
| 15 | `GameplayUnitDestroyedInCombat(unit)` (150) | `OnUnit(15, ...)` | 0, 0 | flag dead (the record stays until 14, see 2.5) |
| 16 | `GameplayUnitSetDamage(unit, d, prev)` (151) | `OnUnit(16, u, d, prev)` | damage, previous | damage, maxHP |
| 17 | `GameplayUnitFortify(unit, on)` (152) | `OnUnit(17, u, on, 0)` | 0/1, 0 | flag fortified (no getter: from the call) |
| 18 | `GameplayUnitWork(unit, build)` (153) | `OnUnit(18, u, build, 0)` | build, 0 | none beyond the key |
| 19 | `GameplayUnitParadrop(unit)` (154) | `OnUnit(19, ...)` | 0, 0 | plot |
| 20 | `GameplayUnitActivate(unit)` (155) | `OnUnit(20, ...)` | 0, 0 | none |
| 21 | `GameplayUnitEmbark(unit, on)` (156) | `OnUnit(21, u, on, 0)` | 0/1, 0 | flag embarked; **a creation when the key is unknown** (2.5) |
| 22 | `GameplayUnitGarrison(unit, on)` (157) | `OnUnit(22, u, on, 0)` | 0/1, 0 | flag garrisoned |
| 23 | `GameplayUnitShouldDimFlag(unit, dim)` (158) | `OnUnit(23, u, dim, 0)` | 0/1, 0 | none |
| 24 | `GameplayUnitVisibility(unit, vis, check, blend)` (160) | `OnUnitVisibility(...)` | vis + 2 x check, blend as float bits | flag visible |
| 25 | `GameplayUnitCombat(info)` -> uint (161) | `OnCombat(25, info)` | combat index in the batch, 0 (see below) | the combat record (2.3); attacker / defender records |
| 26 | `GameplayCitySetDamage(city, d, prev)` (165) | `OnCityEvent(26, c, d, prev)` | damage, previous | city owner, id (plot -1, as kinds 4-6: no pure city plot getter) |
| 27 | `GameplayCityPopulationChanged(city, pop)` (166) | `OnCityEvent(27, c, pop, 0)` | population, 0 | as 26 |
| 28 | `GameplayCityCombat(info)` -> uint (167) | `OnCombat(28, info)` | combat index, the attacking city's plot + 1 (0 = not known; 0 always before ticket 5062, 2026-11-23: unit-combat-server.md 18.37.3) | as 25 |
| 29 | `GameplayUnitMissionEnd(unit)` (168) | `OnUnit(29, ...)` | 0, 0 | none |
| 30 | `GameplayUnitRebased(unit, from, to)` (169) | `OnUnitPlot(30, u, to, from)` | from plot, to plot | plot |
| 31 | `GameplayUnitResetAnimationState(unit)` (170) | `OnUnit(31, ...)` | 0, 0 | none |

Every unit event has `plot` = the unit's plot index, `a[0]` owner, `a[1]` unit id (contract 2.1). The city kinds
26-28 use `plot` -1 (refinement of contract 2.1, which proposed the city's plot: no pure getter gives it,
`srv_remote_feed.cpp` header comment [C]); `a[0]` owner, `a[1]` city id. **Combat return value:** contract 2.1 puts
"the returned value" in `a[3]`, but the event is pushed before the call is forwarded (so that its `seq` precedes the
EXE's reaction), when the value does not exist yet. Recommended: `a[3]` = 0 and the value not recorded (the renderer
does not use it); the MC's, as an N5 amendment (Q7).

**Settled by 4452 (2026-11-16, [C] code, `server/src/feed/srv_feed.h` `Kind` comments are the reference).** Every unit
event: `plot` the unit's plot (`GetPlot()->GetPlotIndex()`, -1 no plot), `a[0]` owner, `a[1]` id; `a[2..3]` as the table
above with these precisions: 11 `a[2]` = `GetUnitType` (read inside the feed, so nothing is called with units off);
12 `a[2]` = path index (-1 when the path's side table was full and the event dropped), `a[3]` = the length the DLL passed
(the side table keeps at most 65,536 plots: `PathEntry::length`); 13 `a[2]` = the call's target plot (-1 NULL);
14 reads **only `GetOwner` and `GetID`** (the call comes from `CvUnit::~CvUnit`, `CvUnit.cpp` 524-535 at the pin), `plot`
= the plot of the unit's record, -1 when the key is unknown; 24 `a[2]` = visible + 2 x check, `a[3]` = blend time's float
bits; 25 `plot` = the combat's target plot, `a[0..1]` = the attacker unit's owner and id (-1, -1 when a city attacks),
`a[2]` = combat index (-1 dropped), `a[3]` = 0; 28 as 25 but `a[0..1]` = the owner and id of the combat's city (the
attacker's city, else the defender's); 30 `a[2]` from plot, `a[3]` to plot. A NULL unit, city or combat argument
pushes the event with `a[0] = a[1] = -1`, changes no record, is counted and LOUD (first 5). Every push happens before
the base call is forwarded; the combat's returned value is stored after it (`SrvFeed::SetCombatResult`).

**Contract amendments drafted for `unit-feed-contract.md` 2.1 (0374's file: to be applied after 0374's merge; MC #2105 Q6,
Q7):**
- kind 32 `kLoadBegin`: pushed when the engine's `ICvPlayer1::SetupGraphical` starts a load (the first call, or a player
  id not above the previous call's; 64 calls 0..63 at every logged load, `unit-formations.md` 12.7.3 S2), before the call
  is forwarded and before any unit call of the load; `plot` -1, `turn` -1, `a[0]` = the new `unitTableGen`, `a[1]` = that
  call's player id. The unit table is emptied in the same lock (`SrvFeed::ResetUnitsAt`). Taken in the server hook
  `ServerHook_ICvPlayer3_SetupGraphical` (`server/src/remote/srv_remote_main.cpp`). [I] whether the DLL's own
  `GameplayUnitDestroyed` calls of `CvPlayer::reset` at an in-process reload come before this mark (they would find the
  previous game's keys in the table and remove them; the allocator side must know): to be seen live (4453/4458).
- kind 33 `kEraChanged`: `GameplayEraChanged(player, era)` (slot 144); `plot` -1, `a[0]` player, `a[1]` new era; the
  player row's era is set from `a[1]` at once (published with the batch).
- kinds 25 / 28 (Q7): `a[3]` = 0; the value the EXE returned is `CombatRecord::returned`, valid when flag bit 7
  (`kCombatReturnedKnown`) is set, i.e. when the batch had not been drained before the forwarded call returned;
  otherwise counted `combatResultsLate` (the renderer does not use the value).
- city kinds 26-28: `plot` -1 (not the city's plot: no pure getter gives it).

### 2.2 `SrvFeed::PushUnit` and the unit table

New in `srv_feed.h` / `srv_feed_server.h` (owned): `UnitRecord` exactly as contract 2.2 (32 B: `owner` i8, `domain` i8,
`facing` i8, `flags` u8, `id` i32, `unitType` i32, `plot` i32, `damage` i16, `maxHP` i16, `artStyle` i16, `era` i16,
`seq` u32, `reserved` u32 = 0: the contract's fields take 28 B, the last 4 are reserved so that the size stays 32), the
flag bits as `c5r::units::UnitFlag` (`unit_scene.h`); `PushUnit(Event, const UnitRecord*, op)` with op = upsert,
patch flags, remove; `CopyUnits(std::vector<UnitRecord>&, uint32_t& tableGen)` for the render thread, as `CopyPlots`.
`PushUnit` takes `g_lock` once, assigns `seq`, stamps `record.seq`, applies the op and appends the event. **The table a
`CopyUnits` returns is the state after exactly the events drained so far** (issue 4463, 2026-11-16 [C],
`srv_feed.cpp` `Drain` / `CopyUnits`; tests `unit-copy-at-drain` and `unit-concurrency` in `server/tests/feed`, scene
case `feed_out_of_order` over the live feed): `Drain` copies the unit table into a kept vector in the same lock
acquisition that takes the events, whenever the table changed since its last copy (a flag set by every table change and
by `ResetUnitsAt`), and `CopyUnits` returns that vector and its generation, not the live table; before the first `Drain`
with the units on it is empty with generation 0. A `PushUnit` between a `Drain` and the next `CopyUnits` is in the copy
of the following `Drain`. The recording feed and the file feed (`srv_scene_sources.h`) are unchanged: they call
`CopyUnits` as before. Cost: one copy of 32 B per unit per drain that follows a table change, beside the reader's own
copy (not measured). *Trace: 4452's "always the state after exactly the events drained" did not hold (two lock
acquisitions); 4461's correction (3.4) described the race; 4463 made the statement true.* `GetUnit` (the DLL side's
read of a held record) stays on the live table. (Plain `Push` returns no `seq`; a
separate `Push` and table update could be split by a `Drain`.) The table is keyed by `(owner, id)` (premise F3), at most
64 x the DLL's unit limit; it lives on the feed side, independent of the render thread (a renderer restart re-reads it).

Getters per field, all on the unit's `ICvUnit1*` inside the override (F2):

| Field | Getter | On `pure-methods.tsv` |
|---|---|---|
| `owner`, `id`, `unitType`, `domain` | `GetOwner`, `GetID`, `GetUnitType`, `GetDomainType` | yes (rows 107-115) |
| `facing` | `GetFacingDirection(bool)` (argument: [I] false; F2 reads what the EXE passes) | yes |
| `plot` | `GetPlot()` -> `ICvPlot1::GetPlotIndex()`, the wrapper released (`Destroy`) | yes; **not** `GetX`/`GetY`, which are not on the list; works before the first plot snapshot, when the grid width is still 0 (creations at load come before the first `Update`, 2.5) |
| `damage`, `maxHP` | `GetDamage`, `GetMaxHitPoints` | **no** (Q1) |
| flags embarked, garrisoned, invisible, delayed death | `IsEmbarked` (**no**, Q1), `IsGarrisoned`, `IsInvisible(activeTeam, false)` (active team from `ICvGame1::GetActiveTeam`), `IsDelayedDeathExported` | yes except `IsEmbarked` |
| flags fortified, visible, dead | the last `kUnitFortify`, the last `kUnitVisibility`, `kUnitDestroyed*` | no getter |
| `artStyle`, `era` | the owner's `ICvPlayer1::GetArtStyleType` (**no**, Q1), `GetCurrentEra` (yes), through `ICvGame1::GetPlayer` (yes) | partly |

**Settled by 4452 (2026-11-16, [C] code; `srv_feed.h` `UnitRecord`, `UnitFlagBits`, `PushUnit`):**
- Layout as above (32 B, `reserved` = 0); `seq` = the event's, 0 when the queue dropped the event (the table changes
  anyway; the reader re-copies at `kOverflow`). `damage`, `maxHP` clamped to int16. `artStyle`, `era` = the owner's
  `PlayerRecord` (the feed's kept row) when the record last changed; -1 when no row was readable.
- `facing`: `GetFacingDirection(false)`. *(2026-11-16, issue 4458 [L, C], 12.1: refreshed at every event this carries facing 7 (`NO_DIRECTION`, which follows `NUM_DIRECTION_TYPES` in the fork's `CvEnums.h`) for every unit a Lua `InitUnit` made without a facing, while the EXE keeps the facing of the creation; take it at the creation and at moves only. The `.c5units` writer refuses 7.)* **Rule (g), 4463 (2026-11-16) [C code;
  L for the creation's facing, 12.1; L for kind 13, 12.4; I for kinds 12 and 30]:** the record takes the getter's facing only at kinds 11, 21
  and 19 (a creation in the EXE: a record type 0x45 followed by its facing, `unit-formations.md` 12.7.1; embark and
  paradrop are remove + create there) and at kinds 12 and 30 (the moves); every other upsert, the teleport (13) and the
  combat's refresh of its units included, keeps the record's facing (`srv_feed.cpp` `FacingKind`). *(Corrected
  2026-11-16 by issue 0449, 12.4 [L]: kind 13 no longer takes the facing; was "at kinds 12, 13 and 30 (the moves)".)*
  The DLL's own facing changes only in
  `CvUnit::setXY` (the direction of the step, before it queues kind 12 or, with quick movement or an unseen move, sends
  kind 13 through `SetPosition`; `CvUnit.cpp` 20509-20516, 20978-20990, 29799-29826 at e9d0c4837) and by calls that
  send nothing (Lua `setFacingDirection`, the rotations, `convert`). Whether the EXE turns a unit at kind 13 (a teleport)
  or 30 (a rebase) is not measured [I]: the server takes the facing there because the DLL turned the unit in the same
  `setXY`. *(2026-11-16, issue 0442 [L], 12.3: at a Lua `SetXY` (kind 13, quick movement on) the EXE keeps the unit's
  heading while the server turns it to the step's direction: for kind 13 the rule takes a facing the EXE does not.
  Kind 30 not measured. Owner of the change: the feed, `srv_feed.cpp` `FacingKind`.)* *(2026-11-16, issue 0449 [L, C],
  12.4: changed; the EXE kept the heading at four kind-13 calls, three Lua `SetXY` and one `MISSION_MOVE_TO` with quick
  movement on, and the fixed server draws the same heading.)* Tests `unit-facing` (`server/tests/feed`, every kind) and `facing-4463` (`test_remote_feed`, through the DLL
  side's getters). **[C] `true` always returns `NO_DIRECTION`** (`CvUnit::getFacingDirection`,
  `CvUnit.cpp` 25338-25349 at the pin: "LOS property has been removed from units"), so `false` is the only meaningful
  argument (closes the open point of section 16).
- Flags (= 0041's `c5r::units::UnitFlag`, checked equal in `server/tests/feed`): embarked, garrisoned, delayed death from
  getters at every event; invisible = `IsInvisible(activeTeam, false)` only when the active team is known and >= 0 (a
  `NO_TEAM` index would read outside the DLL's arrays; counted `no active team`); fortified from kind 17, visible from
  kind 24 bit 0, dead from kind 15 (the call-derived bits are kept across getter refreshes).
- Table effect (`PushUnit`): only kind 11 and kind 21 (either direction) insert a key; ~~visible is set at insertion unless
  an event for the key before its creation said otherwise (4459 F7: a unit created in a city gets `Visibility(false)`
  before its `Created`): such events change no record, their call bits are kept aside (65,536 keys) and applied at the
  creation (`preCreationEvents`)~~ **visible is set at every insertion: rule (f), 4463 (2026-11-16) [C code; L for the
  rule, 12.1]: a `Visibility` before the creation is ignored, as the EXE ignores it.** Events before the creation change
  no record and are counted (`preCreationEvents`); only a `Fortify` or a `DestroyedInCombat` bit is still kept aside (65,536
  keys) and applied at the creation [I: not measured; no such call before a `Created` was seen]. Every unit event whose
  key the table does not hold and which does not create it (an upsert, flag patch or removal of an unknown key) carries
  `Event::flags` bit 0, `kEventNoRecord` (`srv_feed.h`), so a reader can ignore it as the EXE does: the scene sets no
  fade from such a `Visibility` (`UnitsCore::onBatch`), which matters for a unit created by `Embark(1)` (kind 21 erases
  no fade). Tests `unit-pre-creation-visibility` (12.1's two units), `kind-24` (`test_remote_feed`), scene case
  `visible_at_load`. Kind 14 removes (unknown key: counted `unitRemoveUnknown`, no change). A kind 11 for a
  held key is an update, counted `unitCreateKnown` (era change or KnownCreation: the allocator decides).
- Player rows: the owner's row is read through `SrvRemote::Context()->GetGame()->GetPlayer(owner)` only when the feed has
  none yet (the load's creations come before the first `AtLeaveUpdate`); after that the kept row is used (no DLL call
  per event), refreshed at every turn change and after a load, and its era set by kind 33. Refinement of 2.4's "the
  owner's row again at each creation": an era reached mid-turn arrives by kind 33, a colour does not change (C3).

### 2.3 Path and combat side tables

- **Paths** (kind 12): `SrvFeed::Batch` gains `std::vector<int32_t> pathPlots` and per path `(seq, first, length)`;
  `a[2]` is the path's index in the batch. Filled from the `CvPlotIndexVector` before `SrvRt::Begin`, at most 65,536
  entries as the crossing's own cap.
- **Combats** (kinds 25, 28): `Batch::combats`, a fixed `CombatRecord` per event (layout in 11) plus its damage members,
  read from `ICvCombatInfo1` (`GetUnit` / `GetCity` for `BATTLE_UNIT_ATTACKER` and the defender, `GetPlot`,
  `GetAttackerAdvances`, `GetVisualizeCombat`, `GetDefenderRetaliates`, `GetNuclearDamageLevel`, `GetAttackIsRanged`,
  `GetAttackIsBombingMission`, `GetAttackIsAirSweep`, `GetDefenderCaptured`, `GetDamageInflicted` and `GetFinalDamage`
  per side, `GetDamageMemberCount` and `GetDamageMember(i)`'s fields, `CvStructs.h` `CvCombatMemberEntry`) **before
  the base call**: the EXE's handler borrows the argument (`combat-on-the-product-path.md` 4, issue 0329). None of these
  getters is on the purity list (Q1). The attacker's and defender's unit records are refreshed at the same time.

**Settled by 4452 (2026-11-16, [C]).** Paths: `SrvFeed::PushUnitPath` puts the plots in `Batch::pathPlots` and a
`PathEntry{seq, first, length}` in `Batch::paths` in the same lock as the event; at most 65,536 plots per path; a side
table above 4,194,304 plots between two drains drops the event (`a[2]` -1 never reaches a batch; counted `sideTableFull`
and `droppedUnit`; `kOverflow` follows). Combats: `SrvFeed::PushCombat` (64 B `CombatRecord`, 28 B `CombatMember`,
layout in section 11), at most 65,536 combats and 1,048,576 members per batch, members capped at
`GetMaxDamageMemberCount` (32, `CvDLLUtilDefines.h` 42) and 256; the attacker's and defender's `UnitRecord`s are
upserted with the combat's seq. **`ICvCombatInfo1::GetDamageInflicted` is not pure and is never called** [C, DLL fork:
`CvCombatInfo::getDamageInflicted`, `CvGameCoreStructs.cpp` 345-388 at cited_at e9d0c4837 (the same function at the pin
04b1211b8), fires the Lua GameEvent `BattleDamageDelta` when the mod option `EVENTS_BATTLES_DAMAGE` (with
`EVENTS_BATTLES`) is on; VP ships both 0, `(1) Community Patch/Database Changes/NewCustomModOptions.xml` 846-848]: its
`pure-methods.tsv` row is `conditional`. The note's two `damageInflicted` words became `firstMember` and `reserved`; a
consumer takes the combat's damage from `finalDamage` and the unit's damage before it, or from the members.
*Corrected 2026-11-20 by issue 4726 (`unit-combat-server.md` 12): the feed now calls `GetDamageInflicted` only while the
DLL's CustomModOptions has that Lua event off (then it is a plain read) and puts it in `reserved` with flag bit 11; the
interceptor (`GetUnit(BATTLE_UNIT_INTERCEPTOR)`) is appended as a member flagged 0x8000 with flag bit 12. "From
`finalDamage` and the unit's damage before it" is wrong for the per-combat damage: 0477's fix changes only
`DamageInflicted` on the engine's copy.* The
combat's game flags (bits 8-10: `IsOption(GAMEOPTION_QUICK_COMBAT)`, the active player's `IsTurnActive`, read flag) are
read at the combat through the game context for decision 14.

### 2.4 The player table (record 6 PLAYERS, decision 2)

Per player 0-63 that `IsEverAlive`: `player`, `team` (`GetTeam`), `alive` (`IsAlive`), flags (bit 0 `IsMinorCiv`, bit 1
`IsBarbarian`), `colour` (`GetPlayerColor`, a `PlayerColorTypes` index), `era` (`GetCurrentEra`), `civilization`
(`GetCivilizationType`), `artStyle` (`GetArtStyleType`); plus the active team (`ICvGame1::GetActiveTeam`). All
`ICvPlayer1` methods of the fork's `CvDllInterfaces.h` [C]; on the purity list except `GetArtStyleType` and
`IsBarbarian` (Q1). **Refinement:** the brief's record lists player, colour, civilization, era, team, alive; art style,
the two flags and the active team are added because 0041's art rule takes a civilization or minor-civilization Type
(`unit-renderer.md` 4) and a city-state's `GetCivilizationType` may not name its `MinorCivilizations` row (premise A1).
**When read:** the DLL sends no call for a colour, era or team change, so the table is read in `AtLeaveUpdate` at the
snapshot point and at every turn change (the refresh rule the plot table uses), and the owner's row again at each
`kUnitCreated` / `kUnitEmbark(on)`; a row that changed is published with the next batch (`Batch::players`, a change
generation). An era reached in the middle of a turn shows at the owner's next creation or the next turn [I: whether the
EXE re-resolves existing units at an era change is premise A2].

**Settled by 4452 (2026-11-16, [C]; `srv_feed.h` `PlayerRecord`).** 32 B: `player` i8, `team` i8, `alive` u8, `flags` u8
(bit 0 `IsMinorCiv`, bit 1 `IsBarbarian`), `colour` i32, `civilization` i32, `era` i32, `artStyle` i32, `changeGen` u32
(the feed's counter of row changes), `activeTeam` i32, `state` u32 (union with the old `reserved`: bit 0 `IsHuman`,
bit 1 `IsTurnActive`, decision 14's input). Read in `SrvRemoteFeed::AtLeaveUpdate` for every `IsEverAlive` player 0..63
after each load and at every turn change; a row is published (`Batch::players`, one per player per batch, the newest)
only when a field other than `changeGen` changed. Record 6's file form stores this 32 B row whole (corrected
2026-11-16 by 4457, section 11.1; was: "16 B per player ... from this 32 B row").

**A1 settled by 4460 (2026-11-16, [C] the DLL fork at the pin 04b1211b8 and the code; corrects the row's `civilization`
for minors).** Every city-state's `GetCivilizationType` is `CIVILIZATION_MINOR`: `CvGame::InitPlayers` sets
`CvPreGame::setCivilization(player, MINOR_CIVILIZATION)` for every minor slot (`CvGame.cpp` 640, 813), and that row's
`ArtStyleSuffix` is NULL in the catalogue database (3.2), so the civilization cannot key a city-state's unit art. The
row therefore carries, **when flags bit 0 (minor) is set, the `MinorCivTypes` index in `civilization`** (the
`MinorCivilizations` row ID; -1 = `NO_MINORCIV`), read by `ICvPlayer1::GetMinorCivType` (`CvDllPlayer.cpp` 376-386 ->
`CvPlayer::GetMinorCivAI` (member read) -> `CvMinorCivAI::GetMinorCivType` (`CvMinorCivAI.cpp` 4723-4726) ->
`CvPreGame::minorCivType` (`CvPreGame.cpp` 1588-1593, a bounds-checked static read): pure; row added to
`proxy/model/pure-methods.tsv`, baseline regenerated by `derive_purity.py --make-baseline` (164 entries, 405 citations,
0 unresolved; at the pin `derive_purity.py --src <04b1211b8 copy> --dll <x64 DLL>`: 164 kept, 0 dropped,
`GetMinorCivType` kept). `GetMinorCivType` is asked of minors only and `GetCivilizationType` of the others only
(`server/tests/feed/test_remote_feed.cpp` player-rows). The size stays 32 B; `srv_feed.h` documents the layout. The
scene keys a city-state's art by its `MinorCivilizations.Type` (0041's resolver takes it, `art.cpp` reads
`MinorCivilizations.ArtStyleSuffix`); an index without a row is `PlayerArtUnknown` by name (test `minor_civ_art`).
*(Replaced: "flags ... `civilization` (`GetCivilizationType`)" for every player.)*

### 2.5 Loads, embarked creations, deaths and overflow [C, the DLL fork]

- At a load the EXE calls `ICvPlayer1::SetupGraphical` per player; the DLL's `CvPlayer::setupGraphical`
  (`CvPlayer.cpp:2306`) calls `setupGraphical` for each city, then each unit in `firstUnit`/`nextUnit` order.
  `CvUnit::setupGraphical` (`CvUnit.cpp:1760-1805`) calls **`GameplayUnitEmbark(unit, true)` instead of
  `GameplayUnitCreated`** for an embarked unit, then `ShouldDimFlag`, `Fortify`, `SetDamage` (if damaged), `Work` (if
  working), and `Visibility(false)` + `Garrison(true)` for a garrisoned one. So the table at load is built from events
  alone (no unit iteration by getters is needed), and **`kUnitEmbark(on)` for a key not in the table is a creation**
  for the table and for the allocator (S3 decides whether the EXE takes a record there).
- `GameplayUnitDestroyed` is called from `CvUnit::~CvUnit` (`CvUnit.cpp:524-535`) when `!gDLL->GetDone() &&
  GC.IsGraphicsInitialized()`; `GameplayUnitDestroyedInCombat` from `CvUnitCombat.cpp` (eight sites). A unit killed in
  combat therefore gets kind 15 and later, at its deletion, kind 14 [I: deletion follows in every case]. Which of the
  two frees the EXE's record is S1; the table removes the record at 14 only.
- A queue overflow (more than 65,536 events between two drains) drops events; the plot table recovers from a snapshot,
  the unit table does not need to (it is updated under the lock whether or not the event is kept), but **the allocator
  does**: a dropped unit event is counted (`Counters2::droppedUnit`, new) and puts the allocator out of step (10).
- The table resets at a new load; the allocator applies the EXE's clear and keeps its size (section 4, S5). The feed has no load mark before the load's creations (the plot
  snapshot comes at the first `Update`, after them): Q6.
- **Load mark (Q6, settled by 4452, 2026-11-16 [C]):** kind 32 is pushed in `ServerHook_ICvPlayer3_SetupGraphical`
  (`srv_remote_main.cpp`) at the first `SetupGraphical` of a load, before it is forwarded; the table is emptied and
  `unitTableGen` + 1 in the same lock; the player rows are re-read at the next `AtLeaveUpdate`. The same hook makes the
  render side's database copy (3.3).
- *(Corrected 2026-11-16 by 4458 [L], 12.1: the EXE ignores a Visibility call that comes before the unit's creation (two units of one frame drawn by the EXE, hidden by the table); the rule below that applies a pre-creation call's bit at the insertion is therefore not the EXE's.)* *(Applied
  2026-11-16 by 4463: every record is inserted visible, 2.2.)*
- **The visible flag at a load (settled by 4460, 2026-11-16).** [C, the DLL fork at 04b1211b8] `CvUnit::setupGraphical`
  (`CvUnit.cpp` 1760-1803) sends no `GameplayUnitVisibility` for a unit in the open: only `Visibility(false)` +
  `Garrison(true)` for a garrisoned unit, only `Embark(true)` for an embarked one; a load runs no `setXY`, so nothing
  else precedes the load's creations. In play the creation path can send visibility calls **before** `Created`:
  `initWithNameOffset` calls `setXY` (599; `Visibility(false)` when it leaves a city, 20489) and
  `plot()->updateCenterUnit()` (607) before `setupGraphical` (1251), and `CvPlot::updateCenterUnit` (`CvPlot.cpp`
  599-660) sends nothing on a plot that is not active-visible, else `Visibility(true, false, 0.01)` to the plot's centre
  unit and `Visibility(false, false, 0.01)` to every other unit on it. **Rule (in the feed's table since 4452,
  `srv_feed.cpp` `ApplyUnitLocked`; scene test `visible_at_load`, 4460; the first clause changed by 4463):** a record is
  inserted visible ~~unless a `Visibility` for its key came before its creation (then that call's bit)~~ (a `Visibility`
  before the creation is ignored, 2.2); later calls set or clear it; the scene
  lists a record with the flag (no fade at rest, (1, 0, 0)). So every unit a load creates outside a city is listed and
  drawn when in view, a garrisoned one is not, an embarked one is. [I] the EXE's side: its creation handler (12.7.1,
  0x5B03E0) is not described as starting a record hidden, and stock shows the human's units after a load although no
  visibility call reaches them. Named sources of difference for U7 (which measures the drawn set): (a) other players'
  units on plots revealed but not visible to the active team after a load: the DLL sends no call, so the server lists
  them (the cloud planes cover only unrevealed plots); whether the EXE hides them by a plot test of its own (the call's
  `checkPlot` argument suggests one exists) is not known *(answered 2026-11-16 by 4458 [L], 12.1: the EXE does not draw
  them; the unit host's fog filter since 4463, 3.2)*; (b) the non-centre units of a shared plot after a load get no
  `Visibility(false)` (in play `updateCenterUnit` sends one), so the server lists both units of the plot (P1).
- **Dropped unit events** are counted `Counters2::droppedUnit` and logged LOUD at the next `AtLeaveUpdate` (first 5:
  `feed: LOUD: <n> unit events dropped so far ... the render side's seed allocator is out of step`).
- **F8 evidence for the live run:** the report line `feed units per kind: ...; unit calls by thread: ui, core, aux,
  other; inside Update` (`server-report.txt`, written by `SrvRemoteFeed::WriteTotals`, which now has its caller in
  `SrvRemote::WriteReport`) counts which service threads make unit calls.

## 3. The server unit scene (`server/src/units/`, new)

### 3.1 What it owns

`srvunits::UnitScene` (sources `srv_units_*.cpp`: unique stems, because `build_server.py` names objects by stem):
the renderer's copy of the unit table (`CopyUnits` at start and after `kOverflow`, then patched from each batch's
events in `seq` order), the player table, the seed allocator (4), per unit the visibility fade state, the frame clock
(6), the drawing `c5r::units::UnitRenderer` on the server's device, the pre-check's state, the counters and statuses of
9, and the readiness flag (8). A member `Scene::m_units` (decision 1) with `Scene::unitFeed(const SrvFeed::Batch&)`
called in `Loop` right after `h.scene.fowFeed(batch)`, and the draw call in `Scene::draw` (7).

**As built (4454, 2026-11-16, branch `task-4454-scene`) [C, the code and `server/tests/units/`]:**
- Files: `server/src/units/srv_units_scene.h` (the interface `srvunits::SceneUnits` the terrain scene sees; it names
  nothing of 0041's module, so `srv_render_scene.h` can include it without `models/include`), `srv_units_core.{h,cpp}`
  (`UnitsCore`: tables, allocator feed, fades, cull, lookups, `UnitScene`, statuses; no device), `srv_units_scene.cpp`
  (`UnitsHost`: pre-check thread, drawing renderer, dump, lines). `Scene::m_units` is a `std::unique_ptr<SceneUnits>`
  made by `srvunits::MakeSceneUnits`, a factory that `srv_units_scene.cpp` registers at static initialisation and
  `srv_render_scene.cpp` holds: the other teams' test programs that compile `server/src/render/*.cpp` without
  `server/src/units/` (render host, shadow, water, overlay, fog, decal, ice tests) still link and get "no unit scene"
  (`render_units=1` is then refused by name), so none of their build scripts changed.
- The scene is made once, in `HostStart` before the render thread starts (`Scene::configureUnits`), then
  `SrvFeed::SetUnitsEnabled(true)`: every unit event of the process reaches the allocator; it lives as long as the
  `Scene`, across device rebuilds (`setDevice(nullptr)` drops only the drawing renderer).
- **The table is not patched from the events:** `UnitsCore::onBatch` takes `CopyUnits` after every batch that carries a
  unit kind (11-25, 29-33), a `kOverflow`, a new `unitTableGen`, and once at the start (with `CopyPlayers`); the feed's
  contract (`srv_feed.h` `PushUnit`: the table is the state after exactly the events drained) makes that exact, and a
  record with `seq` above the batch's last event is counted `FeedOutOfOrder` (the contract broken). Player rows are
  patched from `Batch::players`. The events (in `seq` order) drive the allocator (`SeedAllocator::Apply` per unit event,
  `NoteDropped` per `kOverflow`, `Advance` per drawn frame) and the fades. Cost: one copy of 32 B per unit per batch with
  unit events (not measured; DoD F 3).

### 3.2 `UnitScene` per drawn frame, field by field

| Field | From |
|---|---|
| `format`, `position_source` | 1; `"own"` until 0040's position records exist (decision 5), then the patch option `unit_positions` |
| `water_level` | the scene's kept height field (`Scene::m_grid`, `HeightGrid`), as 0041's oracle takes it from C5HF |
| `players[]` | one per owner of a unit in the list: `colour` = `PlayerColors.Type` of the player's `colour` index (5), `art_style` = the civilization or minor-civilization Type (A1), `era` = `Eras.Type` of the era index |
| `units[]` order | table order by `(owner, id)` [I: the EXE's draw order is not sorted, `unit-draws.md` 6; draw order only matters for overlapping alpha] |
| `player`, `id`, `unit_type` | `UnitRecord.owner`, `id`, `Units.Type` of `unitType` (database row order index) |
| `art_row` | `""` (the module resolves it from the owner's era and style); if A2 finds that the EXE keeps a unit's art from its creation, the row resolved at `kUnitCreated` with the era of that moment. **Issue 4469: an embarked unit (flag bit 0) gets the embarked row** (below), in the frame list and in the controller's input (`UnitsCore::inputOf`) |
| `plot_x`, `plot_y` | `plot % width`, `plot / width` (grid from the feed) |
| `has_root`, `root_*` | false in mode own (the module's plot centre, P6); mode exe: the newest `kKindUnitPosition` |
| `facing` | `UnitRecord.facing` |
| `formation_seed` | the allocator's index for the unit (4) |
| `damage`, `max_hp`, `alive` | `UnitRecord.damage`, `maxHP`; `alive` -1 |
| `flags` | `UnitRecord.flags` |
| `fade_from`, `fade_to`, `fade` | the unit's last `kUnitVisibility`: becoming visible (0, 1, p), becoming hidden (1, 0, p), p = clamp((t - t0) / blendTime, 0, 1) on the frame clock; at rest visible (1, 0, 0); blendTime 0 = at once [L for (0, 1, 1) and (1, 0, 0), `unit-renderer.md` 10.3; I for mid-fade and fading out] |
| `figures` | absent until 0374's merge (the module's default clip at t 0: frozen figures, test runs only); then the controller's output through `PoseSource` (6) |

**The embarked row (issue 4469, 2026-11-16) [C: the EXE's `GameplayUnitEmbark` handler (engine-utility slot 156,
0x408310) read for behaviour; L: 3.4's 4469 bullet].** For `Embark(true)` the handler chooses the art define of the new
record as follows (D83: behaviour only): (1) the owner's embarked graphic override (`ICvPlayer1::GetEmbarkedGraphicOverride`,
slot 51) when it is not empty and names an art define, in every era; else (2) **`ART_DEF_UNIT_TRANSPORT` when the unit's
team's era (`ICvTeam1::GetCurrentEra`) is at least `ICvGameContext1::GetLAST_EMBARK_ART_ERA`** (the define
`LAST_EMBARK_ART_ERA`, 4 = the Industrial era in VP); else (3) **`ART_DEF_UNIT_GALLEON` when the team can embark across
all water** (`ICvTeam1::CanEmbarkAllWaterPassage`: a technology with `EmbarkedAllWaterPassage`, VP `TECH_ASTRONOMY`,
Renaissance); else (4) **`ART_DEF_UNIT_GALLEY`**. All four rows are in VP's database (`GALLEY`, `GALLEON`, `TRANSPORT`
2 members each; no Units row uses them). The override is set by the DLL only in `CvPlayer::init` (`CvPlayer.cpp` 907-915
at the pin: a leader trait with `EmbarkedAllWater` -> `ART_DEF_UNIT_U_POLYNESIAN_WAR_CANOE`, else
`EmbarkedToLandFlatCost` -> `ART_DEF_UNIT_U_DANISH_LONGBOAT`), saved with the player, and Lua's
`SetEmbarkedGraphicOverride` can change it. *(Answers `unit-controller.md` 17.3's [I] "what other players embark as";
that file is not 4469's: named for the lead.)*
- **The server's rule (`srv_units_core` `EmbarkedRow`, `UnitsCore::embarkedRowOf`)**: the override from the database
  as the DLL's `init` gives it (Civilization_Leaders -> Leader_Traits -> Traits, `Lookups::loadEmbark`), for a major
  civilization; `TRANSPORT` from `LAST_EMBARK_ART_ERA` (Defines) on; `GALLEY` below the lowest era of an all-water
  technology (researching one moves the team's era to the technology's, so no team below it has the passage; the
  passage changes only with technologies, `CvTeam.cpp` 7923); **between the two (VP: the Renaissance era) the feed does
  not carry the team's all-water flag: the unit is not drawn, `PlayerArtUnknown` LOUD once per owner and reason** (no
  guessed boat, D118). A row the art resolver does not know: the era rule's row when the override was the missing one
  (as the EXE), else not drawn. Missing tables: `Lookups::load` still succeeds, `embarkWhy` names the first missing one
  and no embarked unit is drawn. Not modelled [I]: an override changed at run time by Lua (no VP Lua does it [I], not
  searched), the passage of a team whose era was forced down by Lua. **Named for the lead / the feed's owner:** a
  PlayerRecord bit for `CanEmbarkAllWaterPassage` (and the override itself) would close the Renaissance gap
  (`srv_feed.{h,cpp}`, not 4469's files).
- **The controller** (`srv_units_anim`, 4456): `inputOf` gives an embarked unit the same row, so a unit created
  embarked binds the embarked art; an `Embark` of a unit the controller holds is handed to it as a creation of a held
  key (`UnitsCore::onBatch`), which `srv_units_anim.cpp` applies as remove + create, so the unit binds its new art (the
  EXE's model swap, 12.7.3 S3); was the controller's own `embark` event, which kept the land art. An embarked unit
  without its row is not given to the controller either.
- Tests (`test_scene_units`): `embarked_row` (every branch of the rule and its refusals), `embarked_lookups` (the
  database read with each missing table or value refused by name; `load` good without them), `embarked_art` (the
  frame list's rows for an override, a TRANSPORT era, a land unit, a city-state in the Renaissance gap not drawn and
  counted once; `inputOf`; the disembark back to the land row; the controller's remove + create with the new row; an
  `Embark` of a unit not held stays a creation there); `anim_mapping` updated (the held unit's `Embark` is now
  `remove:1001,create:1001`).

**Which units are listed:** flag visible set, or a fade-out still running; not dead past its removal; and inside the
frame's view, culled by plot against the camera before `prepare` (the module poses every listed figure: 18.7 us per
figure-mesh, `unit-renderer.md` 11.8, so a late huge game cannot be prepared whole). Garrisoned, cargo, based aircraft
and invisible units are drawn exactly when the EXE draws them; U7 measures the drawn set and the rule is corrected then.
*(2026-11-16, issue 0442 [L], 12.3: fighters based in a city and fighters carried by a carrier are drawn by neither
the EXE nor the server (not listed: no visible flag); a unit moved into a city by `SetXY` is drawn by both.)*
*(2026-11-20, ticket 4980 [L]: for a LOADED save both stock and the server draw based, intercepting and carried aircraft;
the 0442 result is for aircraft created or based live; `extras-units.md` 4980.)*
**Fog rule (4463, 2026-11-16) [C code; L for the rule, 12.1; I for "seen" plots]:** a unit whose owner's team
(`PlayerRecord::team`) is not the active team (`activeTeam` of the rows) is listed only when its plot is visible to the
active team; the unit host filters the built lists by the fog scene's plot states (`SceneUnits::setPlotStates`,
`UnitsHost::filterByFog` in `srv_units_scene.cpp`, after `UnitsCore::build`, before the module and the controller see
the frame). A hidden unit leaves `listed` (`fog_hidden=` in the `units 1s` line, `fog hidden_units=` in the totals); a
culled one stays counted as culled. Without plot states (`render_fow=0`, a program that does not hand them in), without
an active team in the rows, or with states of another grid: no filter (the list as before), said once, counted
(`fog=none|no-team|grid`, `unfiltered_frames=`). 12.1 saw the EXE skip other players' units on plots the active team
does not see; whether it also skips them on plots it has seen but does not see now ("seen", fog state 1) is not
separately measured [I]: the filter hides them there too. **Not wired yet:** the terrain scene must call
`m_units->setPlotStates(...)` with `m_fow.states()` before `m_units->beginFrame` (`srv_render_scene.cpp`, outside
4463's files: named for the lead); until then the product lists as before (`fog=none`). Test: the module case
`precheck_pass_draw` (its `FogFilter` part: visible, seen, unexplored, the same generation not copied twice, a player
joining the active team, another grid, no states).

**As built (4454, 2026-11-16) [C; the marks of each rule's premise stay]:**
- Index -> Type through the database's `ID` columns (`Lookups`: `Units`, `PlayerColors`, `Civilizations`, `Eras`,
  `ArtStyleTypes`, read from the unit database copy). An index with no row is a named status, never a default (10).
- `art_style`: a major civilization's or the barbarians' `Civilizations.Type`; a city-state (`PlayerRecord.flags` bit 0)
  its `MinorCivilizations.Type` (*corrected 2026-11-16 by 4460: the record carries the minor civilization index, 2.4;
  it read "the `ArtStyleTypes.Type` of its `artStyle`", which the gap below refuted*). **Gap (closed by 4460) [C, the catalogue database `F:/c5-64bit-work/0327/db/
  Civ5DebugDatabase.db`, read 2026-11-16]:** an `ArtStyleType` does not fix the suffix: `ARTSTYLE_MIDDLE_EAST` spans
  `_AFRI`, `_MED` (civilizations) and `_AFRI`, `_ASIA`, `_MED` (city-states), `ARTSTYLE_EUROPEAN` and
  `ARTSTYLE_GRECO_ROMAN` span `_EURO` and `_MED`; the module's resolver refuses such a style (`BadInput`), so a
  city-state's units are refused (`UnitRefused`) until the player record carries the minor civilization (its
  `MinorCivilizations` row: Msoura `_AFRI`, Ragusa `_EURO` in the catalogue). `CIVILIZATION_MINOR` (ID 18) has
  `ArtStyleSuffix` NULL. Named for 4452 / the lead: the record needs the minor civilization type (A1).
- Fades: no `kUnitVisibility` since the unit's creation = (1, 0, 0) when the visible flag is set, else not listed;
  visible with blend b > 0 = (0, 1, p), p = clamp((t - t0) / b, 0, 1) on the frame clock (t0 = the drain time of the
  event), staying (0, 1, 1) after (stock's row of a unit that came into view, [L] 0041 10.3); **visible with blend 0 =
  (1, 0, 0) at once [I]**; hidden with b > 0 = (1, 0, p) while p < 1, then not listed; hidden with blend 0 = not listed.
  A creation, a removal and `kLoadBegin` reset the fade.
- Cull: the box plot centre (`c5r::units::plotCentre`) +- 64 world units in x and y, z 0..160, against the frame's
  `view[0] x proj` (outside one clip plane with all 8 corners = culled) [I: generous; P1's shared plots and the tallest
  units not measured]; on a wrapping map each copy k = -1, 0, +1 at x + k `wrapPeriod` is tested, and a unit is listed
  in every copy that shows it (drawn once per copy, 7). A camera that is not finite: no cull.
- Order: `(owner, id)`; `players[]` = the owners of the copy's units, ascending.
- **DoD B, the catalogue frames (4454, 2026-11-16) [C]:** `server/tests/units/oracle_check.py` built, for each of
  0041's 20 oracle scenes (`render/evidence/0041/4355/scenes/`), a synthetic feed (player records by the database IDs of
  the scene's Types, unit records, creations in seed order with fillers of player 63 for unused indices) and ran it
  through `UnitsCore` (database `F:/c5-64bit-work/0327/db/Civ5DebugDatabase.db`, height field of bundle
  `C5H-0301-TINY_0009_20261115-225124_workstation`, water level equal); the scene's `.c5units` with 0041's figure lines
  added (poses forced) then went through 0041's `unitscene prepare` beside 0041's own scenes (c5h-c's install, CP / VP /
  EUI mods, the same EXE). Result (`F:/c5-64bit-work/0375/4454/oracle/summary.json`): field by field 224 of 229 units
  equal (art_row "", the visible flag and the figures excepted by design); the module's status, blocks, draws and
  warnings equal for 220 units and the palette rows byte-equal for 220 of 224 compared, so 0041's merged numbers stand
  for those 220. The 9 others are named: 4 units of the two city-states (L15) are `BadInput` (the art style gap above);
  5 sea units (S01-S04) carry in 0041's scenes the seed of another unit of the same frame (4354's catalogue seeds by
  12.1's older rule), which no allocator sequence can give, so the synthetic feed leaves them without a seed
  (`SeedMissing`).

### 3.3 Calls into 0041's module, threads, database, files

- **Open**: `UnitRenderer::open(dev, files, db, exe, install)` on the render device at the first drawn frame after a
  database is ready; `files` = `gamefs::GameFiles` of the install with `ModFolders()` (`srv_residency_source.h`, every
  MODS folder [I: active or not]) as `extraLooseRoots`; `exe` = `HostConfig::exe` (`render_exe`).
- **Database**: the server's copy of the engine's database `<run>/db/Civ5DebugDatabase.<n>.db` (made by the
  SetGameDatabase hook in `srv_remote_main.cpp`, `proxy-design.md` 2180), which the DLL writes; `c5m::SqliteTables`
  opens files `immutable=1`, so the scene copies the file to `<run>/db/units-<n>.db` once per `<n>` and opens the copy.
  How the render side learns `<n>` and the path is Q2.
  **Settled by 4452 (2026-11-16, MC #2105 Q2/Q5, [C] code and test):** the render side never opens the file the DLL
  writes. At each load mark (2.5) the server copies `Civ5DebugDatabase.<n>.db` to `<run>/db/units-<n>.<gen>.db` with
  SQLite's online backup API on a second, read-only connection of the system's `winsqlite3.dll` (`CvGameDatabase_x64`
  exports no `sqlite3_*`), in steps of 256 pages (the source's read lock is held one step at a time; a commit by the
  DLL between steps restarts the backup; the DLL's connection waits at most a step, its busy handler retries 30 x 2 ms),
  checks the copy with `PRAGMA quick_check` on an `immutable=1` read-only open, and publishes it with
  `SrvFeed::SetUnitDatabase(path, gen)`. No new copy while `PRAGMA data_version` on the kept source connection is
  unchanged. Code: `server/src/feed/srv_feed_dbcopy.{h,cpp}`. Test (`server/tests/feed/test_feed.cpp` `db-copy`,
  2026-11-15): 12 copies taken while a writer thread committed (865 commits in one plain run, 429 under ASan; journal in
  memory and sync off as the server's copy), every copy one committed state (row invariant and count equal), 0 writer
  failures; refusals named for a missing source, a missing folder, an empty path, a non-database file. The copy's time on
  VP's database is not measured (logged per copy in `server-report.txt`: `units: game database copied ...`, since
  0631 take 2 with `longest step <ms>`).
  **The writer failures of `db-copy` (ticket 0631 take 2, 2026-11-24, [C] SQLite's backup order + [L] 520 test runs on
  the workstation):** `FAIL db-copy: g_writerFails.load() == 0 ...` with "writer failures N" was not a flake of one
  branch: it fails on `main` too and its cause is in `DbCopier::Copy`. The last `sqlite3_backup_step` commits the
  destination *before* it ends its read transaction on the source (SQLite's backup.c), so the source's SHARED lock is held
  through the copy's own commit. The copy was opened with SQLite's defaults (journal_mode DELETE, synchronous FULL): the
  commit creates, writes, flushes and deletes `<copy>-journal` and flushes the copy (FlushFileBuffers), which on the busy
  workstation took up to 760 ms. The writer's COMMIT needs the EXCLUSIVE lock; its busy budget (busy_timeout 60 ms, the
  DLL's 30 x 2 ms; about 110-140 ms of wall time with Windows' sleep granularity) ran out and the COMMIT returned
  SQLITE_BUSY: all 47 logged failures were `rc 5 (database is locked) after 109-141 ms`, and every failing run had a
  longest backup step of 126 ms or more (passing runs reached 230 ms: the step time also holds the copier's own waits
  for the writer's lock, so it overstates the hold). Interleaved repeat runs of the plain `test_feed.exe`
  (`server/evidence/0631/dbcopy_ab.py`, each variant with the same instrumentation, 2026-11-24 01:40-02:20 EST, all
  through quietlock with other teams' jobs on the machine; about 1 s a run): main 17 of 420 runs failing (4.0 %), main
  + 0631's first take 9 of 120 (5 vs 9 in the 120 rounds both ran; Fisher exact p 0.41, all runs p 0.14: no shown
  difference, and 0631 changes no copy or SQLite code), longest step median 67-77 ms. The MC's merge check (MC #12486:
  main 0 of 3, 0631 2 of 3, 9 binaries each) was a small sample of the same rate. **Fix:** the copy is written with
  `PRAGMA journal_mode = OFF` and `synchronous = OFF` (it is a scratch file, checked by `quick_check` and deleted on any
  failure, published only after the check): 0 of 400 runs failing against main's 17 of 420 in the same rounds (Fisher
  exact p 1.4e-5), longest step median 47-58 ms, max 165 ms. Product consequence [I]: the DLL's writes to its database during a copy (a load mark) met the
  same lock; no DLL write failure was seen in a game log, but none was looked for. The test now prints each writer
  failure's SQLite code and wait, and the longest backup step. Negative result: the failing runs were not tied to ASan
  (MC #12486's failures were in the plain and injection binaries; the ASan binaries passed all 6).
- **Pre-check**: per new database, on a worker thread with its own CPU-only `UnitRenderer` (dev null; the module is not
  thread-safe, `pose.h`), `precheck(out)` over every `Units.Type` x art style suffix x era (`unit-renderer.md` 10.6);
  the statuses are kept, the worker's model cache released. A status other than `Ok` takes D125's route (10); warnings
  (`TextureFallback`) are logged once each. The scene is not ready until the pre-check passed. Its time and peak memory
  on VP's whole database are not measured (decision 10).
- **Per frame**: `prepare(scene, &m_grid, plan)` then `draw(ctx, plan, passes)` (7). A unit whose `UnitStatus` is not
  `Ok` is not drawn (the module's rule) and is an in-play fault (10).
- **As built (4454, 2026-11-16) [C]:** the database comes from `SrvFeed::GetUnitDatabase` (4452's consistent copy,
  MC #2105 Q5), or the test key `render_test_units_db`; a new generation (polled every loop iteration) opens on the
  render thread an environment (`GameFiles` of the install with `srvrender::ModFolders()` as extra loose roots,
  `SqliteTables` of the copy), the `Lookups` and an `ArtResolver` (for the allocator's era-change test), and starts the
  pre-check on a `std::thread` with its own environment and a CPU-only `UnitRenderer` (joined in `poll()`). The drawing
  `UnitRenderer` opens at the first draw after the pre-check passed and a device exists. Times and memory on VP's whole
  database: not measured (DoD F 3). The pre-check over the 4352 fixture (warrior, trireme) on c5h-c's install passes
  in the test `precheck_pass_draw`.

### 3.4 Integrated and live (4460, 2026-11-16)

**Integration [C].** `server/build_server.py --src <git archive of the fork at 04b1211b8> --deps <9045's build_all of
04b1211b8> --config release --offline` (gated) links server.exe with the feed (4452), the allocator (4453) and the scene
(4454/4455) at the first try; `build_units_tests.py` prints `test doubles: none`. Mismatches found and fixed: (1) the
allocator's combat input was never wired (section 4, note of 4460); (2) the real allocator applies a drain's creations
at `Advance` (frame grouping, 4453), so a seed exists only after the next drawn frame: a test double had counted at
once (`feed_out_of_order` fixed), and the test-only table file now also rewrites when seeds arrive. `ResetUnitsAt`
pushes `kLoadBegin` and `UnitsCore::build` calls `Advance` before any `SeedOf` [C]. Suites (2026-11-16, logs in
`C:/c5-64bit-work/0375/4460/`, removed at hand-back; counts in the issue log): `test_scene_units` 28/28 plain and MSVC
ASan, its runner 6/6, seed allocator, feed, remote feed (stand-in DLL) plain + ASan, consumers 20/20, render host 47
plain + 47 ASan, water scene 19 + 19 ASan, render units + unitanim 24/24 plain + ASan; 0 ASan reports.
*Corrected 2026-11-16 (ticket 0443):* `c5r_units_test_model` skipped on the pool installs because VP's knight_turkey.gr2 is in the instance's MODS, not in the install, and the test read the install only. Since 0443 (`render/tests/units/units_mods.h`, `unit-renderer.md` 10.1's file table) it reads a pool instance's MODS folders (every folder, from `<install>/../instance.json`'s `user_dir`) and runs: 11 run, 0 skipped on all nine pool installs c5h-a..i [L, 2026-11-16]; the units + unitanim suites 29/29 plain on c5h-d and on the default copy and 29/29 under MSVC ASan on c5h-d, 0 skipped, 0 ASan reports (with VP's files found, `test_art_cache` took 191 s and `test_pose_catalogue` 321 s under ASan on the busy workstation). (Was: "skips on the pool installs (knight_turkey.gr2 absent; untouched test)".)

**Live on the product path [L]** (2026-11-16 02:16-02:29 EST, pool instance c5h-d (Goldberg, F:, Vibrant Textures
present), claimed by 0375; layout: the pin layout `x64-04b1211b-pfdee656`'s bin (DLL 29F58E17...) with server.exe of
this branch (run 3: FF368B69... at e64d6897, run 4: 17BDF4D1... at 1b4798b3, `dirty=0`), proxy 7A193D95... / 373DBBD2...,
composite 5F38A59D...; ServerRenderer = 1, D33 with Overlay on, `vpproxy.ini server_cfg_extra=` (TEST ONLY, issue 0307:
`render_units=1`, `render_test_units_table`, `render_test_units_dump` max 200); Tiny save copy `C5H-0375-4460-TINY_0018`
of `C5H-3000-TINY-T0018` (E337244C), turn 18, Spain, quick movement and quick combat on; driver
`server/evidence/0375/4460/live_4460.py`, outputs `F:/c5-64bit-work/0375/4460/live3`, `live4`):
- **Load and play:** in game 2 min after launch; 5 AI turns (4.7-6.1 s each); 0 LOUD lines in server.log and render.log
  (vpproxy.log: the 5 expected `TEST ONLY` lines of server_cfg_extra); no dialog at any step (D155 check after each);
  stopped by the harness's safe stop; instance restored by hash, `provision.py verify` rc 0.
- **The database copy and the pre-check on VP's database:** `units: game database copied ... units-1.1.db (6647 pages,
  0 busy steps, 236-240 ms)` 0.24 s after `kLoadBegin`; `pre-check of generation 1: 233 art rows, 0 failed, 2 warnings
  (ClipMissingInInstall 1, TextureFallback 1), 14324-14911 ms` on its worker thread; the scene ready (`ready=1`) about
  15 s after the load mark, units drawn every frame after it (`units 1s: ... listed=30 culled=22 not_drawn=0`). Before
  the ClipMissingInInstall rule the pre-check failed 1 of 233 on the catalogue copy of VP's database
  (`unit-renderer.md` 10.6), so every game would have ended; the live runs used the rule. **Item 5, the pre-check on the product's own copy**
  (`units-1.1.db` of a live run, 6,806,528 B, through `test_scene_units.exe precheck`, the scene's own environment and
  CPU-only renderer on c5h-d's install with the CP, VP and EUI mod folders): every reachable art row, 233 (215 unit
  types), status `Ok`, 0 failed, warnings ClipMissingInInstall 1 (the Assyrian siege tower), TextureFallback 1
  (Tadodaho's medicine man: `black_4x4.dds`); 15.1 s (environment 0.23 s), peak commit 388 MB, peak working set 491 MB
  (the process's peaks; the pre-check is its only work) [L, workstation: indications, D106]. Rows:
  `F:/c5-64bit-work/0375/4460/precheck/product.tsv`.
- **T1-T3, the table against the DLL** (`UNITS_LUA` of the driver: every unit of `Players[p]:Units()`, read-only):
  after the load 35 = 35 keys, after 5 AI turns 46 = 46, after the F3 reload 46 = 46; 0 field differences in unit type,
  plot, damage, max HP, domain, embarked and garrisoned; every record holds a seed (run 4: 35/35, 46/46, 46/46;
  run 3 read 0 seeds at T1 and T3 because the table file was written before the first drawn frame: fixed).
- **Count per kind against the EXE's Lua events** (one scripted sequence in the human's turn: a warrior created beside
  the capital, `SetXY` to the next plot, `ChangeDamage(20)`, `MISSION_FORTIFY`, `Kill`, a work boat created and killed;
  feed counts from the table file before and after, Lua listeners on 23 events), runs 3 and 4 identical: kind 11 2 =
  `SerialEventUnitCreated` 2; 13 3 = `SerialEventUnitTeleportedToHex` 3 (one per creation and one `SetXY`); 14 2 =
  `SerialEventUnitDestroyed` 2; 16 1 = `SerialEventUnitSetDamage` 1; 23 4 = `UnitShouldDimFlag` 4; 24 3 =
  `UnitVisibilityChanged` 3; kind 17 3 (one `Fortify(false)` per creation from `setupGraphical`, one from the mission)
  has no Lua event; kinds 12, 15, 18-22, 25-31 were 0 in the window and their Lua events too (no combat, embark,
  garrison or move in the sequence). Lua events without a kind: `SerialEventUnitFacingChanged` 2, `UnitActionChanged` 3,
  `SerialEventUnitInfoDirty` 7, `LocalMachineUnitPositionChanged` 1, `SerialEventCityInfoDirty` 4 (the EXE's own
  follow-ups). **No difference.**
- **F3 [L]:** the game saved as `C5H-0375-4460-F3` (UI.SaveGame on the main thread) and loaded in the same EXE process
  (`Events.PlayerChoseToLoadGame`): 46 keys before = 46 after, the same `(owner, id)` and unit types; the server's
  table generation 1 -> 2 (`kLoadBegin`), its 46 records equal to the DLL's after the load.
- **Feed totals, run 4** (`server-report.txt`): unit events 1020, 0 dropped, loads 2, table inserts 94, removes 2, 0
  unknown removals, 0 known creations, 30 pre-creation events (all kept); per kind 11=94 13=162 14=2 16=16 17=128 22=12
  23=536 24=47 26=16 27=5 32=2, the rest 0. **F8 [L]:** unit calls by thread: ui 271, core 726, aux 0, other 0; inside
  `Update` 698: the DLL makes unit calls from two threads (the UI thread at the load's `SetupGraphical`, the game-core
  thread during updates); the per-thread order question of F8 stays [I] (no reader on the product path).
- **The picture** (DIAGNOSTIC, Visual Checks `vc-20261116-022734-workstation-1d70`, run 4; run 3's
  `vc-20261116-021924-workstation-f930` was a flat 84,84,84 PrintWindow frame, amended `fail`): the server's units in
  its frame and the EXE's own units over them (0040's filter does not exist), poses the module's default clip at t 0.
  Observation [L, picture only]: figures of units on unrevealed plots show faintly through the cloud planes (the server
  lists every unit with the visible flag, 2.5 (a)); whether stock shows them is U7's.

**DoD F: parity, the huge run, the cost (4461, 2026-11-16, branch `task-4461-parity-cost` at d0559d8e).** The build of
all three: `server/evidence/0375/4461/build_4461.sh` (03:08-03:15 EST): server.exe **D9F5F17C...** (repo d0559d8e9,
`repo_dirty=0`, release, the fork at the pin 04b1211b8 by `git archive`, 9045's build_all deps), proxy 88860726..., vp64helper
7D251B12..., composite 302D135C..., in the pin layout `x64-04b1211b-pfdee656`'s bin (x64 DLL 29F58E17...). Run data on
`F:/c5-64bit-work/0375/4461/`; drivers in `server/evidence/0375/4461/`.
- **Method, F (1)** (`parity_4461.sh`, 0178's method as 0168/3957's `parity.sh`): pool instance c5h-e (F:, Goldberg, VT
  present), product path by 4460's prep (`live_4461.py prep --kind tiny`), Tiny copy `C5H-0375-4461-TINY_0018` of
  `C5H-3000-TINY-T0018` (E337244C), two arms of `proxy/tools/live/arm.py` (30 AI turns each with the harness checklist, the
  shipped composite ini), **u1** = vpproxy.ini `server_cfg_extra=` naming only `render_units=1`, **u0** = only
  `render_units=0`; `eq_stats.py` after the load and at the last turn, u1 against u0. Expected: identical, 0 differing
  keys (the unit scene reads the feed and pure getters only, 2.2-2.4, F2).
- **Method, F (3)** (`cost_4461.py`, a `--quiet --headed` oldpc job, decision 12): one instance (tb1, the machine's own
  EXE), product_path.py's prep and restore, a warm-up arm u1 and 12 arms interleaved u1,u0,u0,u1 x 3, each a fresh
  timing launch (no frame cap, WaitForVSync 0, config.ini LoggingEnabled 0, as 0095's fps_idle.py) of the huge copy
  `TBPP-HUGE_0350` (c499d3c6...): a 60 s settle (u1 also waits for `pre-check of generation 1` and `units 1s: ready=1`),
  a 45 s idle window (the Lua frame counter, fps_idle.py `--measure`, 3 blocks) with server.exe's CPU seconds (psutil),
  the 3D-engine utilization of server.exe and the EXE and server.exe's GPU memory (Windows counters, `gpu_sample.ps1`)
  and the server's frames drawn (render.log `terrain 1s: drawn=`) over the same seconds, memory after the load (psutil
  and the server's own `mem periodic` line: private, video_local), 10 AI turns (end turn to the next human turn, wall,
  and server.exe's CPU seconds), memory at the end, the safe stop. The unit scene's CPU and GPU time per frame are read
  as the u1 - u0 difference of the server's CPU ms and GPU ms per drawn frame (the product has no timer of its own around
  the unit pass: `terrain 1s` `cpu_ms` / `gpu_ms` time the terrain renderer's draw only, `srv_render_scene.cpp`); the
  feed's added time per AI turn as the u1 - u0 difference of the AI turn's wall time (the product path writes no
  `turn-times.tsv`, `srv_main.cpp`); the pre-check's peak memory as the u1 - u0 difference of server.exe's peak private
  bytes. Paired by repetition: mean, 95 % t interval, smallest detectable difference (80 % power).
- **F (1) result [L] (2026-11-16 03:24-03:38 EST, c5h-e):** u1 rc 0 and u0 rc 0, 30 AI turns each (turn 18 -> 48),
  checklist rc 0, stop rc 0, guard and start-up watch ok in both. **`eq_stats.py` u1 against u0: 376 keys after the load
  and 455 at turn 48, identical, 0 differing** (`F:/c5-64bit-work/0375/4461/parity/eq-load.txt`, `eq-end.txt`). The arms
  really differed: u1 `feed units: on; unit events 7042 (dropped 0 ...)`, per kind 11=114 13=1255 14=25 15=16 16=306
  17=714 18=61 21=1 22=15 23=4144 24=358 26=9 27=23 32=1, unit calls ui 117 / core 6892; the pre-check `233 art rows, 0
  failed, 2 warnings ..., 18019 ms`; the last `units 1s` line `ready=1 precheck=passed table=89 listed=81 culled=75
  drawn_frames=5301 ... refused=0 colour_unknown=0 out_of_step=0`; u0 `feed units: off; unit events 0`, no `units` line
  in render.log, 0 LOUD lines. Restore by hash ok, `provision.py verify` 0, the save copy removed.
- **Finding [L, C] (u1, server.log +167.3 s): one LOUD line `units: FeedOutOfOrder: table record (2, 1049) has seq 8199
  above the last drained 8198 (CopyUnits ahead of Drain)`.** `SrvFeed::Drain` (`srv_feed.cpp`) releases the feed's lock
  before `UnitsCore::onBatch` takes the table with a second, shared acquisition in `SrvFeed::CopyUnits`
  (`srv_units_core.cpp` `takeTable`); a `PushUnit` from the DLL's thread between the two leaves the table one event ahead
  of the batch, and the check after `takeTable` names it FeedOutOfOrder. The event comes in the next batch with a seq
  above the last taken and is applied, so nothing is lost or reordered: a benign race reported as a failure, 1 in 7,042
  unit events here. *Correction 2026-11-16 (4461) of 2.2 and section 0's "so a drained batch and `SrvFeed::CopyUnits`
  never disagree": they can, by one or more events pushed between the drain and the copy.* For the owner of the scene
  (not changed here): take the table under Drain's lock when the batch has unit events, or accept records whose seq lies
  above the batch and at most at the feed's current seq. **Fixed 2026-11-16 by 4463 [C] (the first way):** `Drain` copies
  the table in its own lock acquisition and `CopyUnits` returns that copy (2.2); `UnitsCore::onBatch` takes it as before,
  and its check now names only a real contract break (the line reads `... above the last drained N (the table copy is
  newer than the batch)`). Offline proof: the scene case `feed_out_of_order` over the live feed, one push between the
  `Drain` and the copy and then a writer thread with 40,000 pushes against the loop's drains, 0 FeedOutOfOrder; the feed
  test `unit-concurrency` checks every copy against the last drained seq. Not yet seen live (the rerun wave's).
- **F (2) result [L] (2026-11-16 03:39-04:07 EST, c5h-e, `huge_4461.sh` step F2 = 3308's run through
  `live_4461.py run-plain`):** copy `C5H-0375-4461-HUGE_0350` of the instance's `C5H-VP8P-HUGE_0350` (C499D3C6),
  `server_cfg_extra` naming only `render_units=1`, the product's DisableSystems list, AI logging as the instance has it.
  **30 of 30 AI turns (350 -> 380), rc 0, 0 EXE crashes, 0 server crashes** (server.exe ran to the harness's stop; its
  report ends with the stop's `PeerDied`, as every run), no dialog at the D155 check, guard check 0 at load and end,
  stop rc 0. AI turn wall 29.1-59.6 s (mean 44.1 s; the workstation busy with other teams' games: indication only).
  **`out_of_step=0`** in every `units 1s` line (the last: `table=896 listed=684 culled=666 not_drawn=0
  drawn_frames=45823 ... refused=0 colour_unknown=0 out_of_step=0`); **dropped unit events 0** (`feed units: on; unit
  events 197861 (dropped 0, side table full 0) ... table inserts 1863, updates 191042, removes 967, unknown removals 0,
  known creations 1, embark creations 34`; per kind 11=1830 13=55220 14=967 15=139 16=4991 17=7232 18=5386 20=11 21=847
  22=206 23=98110 24=21899 26=715 27=280 29=3 31=1 32=1 33=23; unit calls ui 2932 / core 193910, inside Update 192228).
  **LOUD lines: 21, not 0, none an R6 episode:** 20 `FeedOutOfOrder ... (CopyUnits ahead of Drain)` (the race above;
  the scene logs the first 20 and then every 100th, so 20-99 occurrences in 30 turns; 1 per ~2,000-10,000 unit events)
  and 1 `KnownCreation: kind 11 seq 236335 key (4, 15542): creation of a held key outside an era change: a second record
  (the EXE's)` at turn ~377 (04:04:24; the feed counts `known creations 1`; which game event made it is not known [I]:
  no event dump ran). vpproxy.log: the 2 expected TEST ONLY lines. **So DoD F (2)'s "0 LOUD lines other than counted R6
  episodes" fails on these two kinds; crashes, out-of-step and dropped events pass.** *(2026-11-16, 4463: the
  FeedOutOfOrder lines are the race fixed above; the KnownCreation's cause is the next item.)*
- **The KnownCreation (4, 15542): what made it (4463, 2026-11-16, offline; evidence lines in
  `server/evidence/0375/4463/knowncreation_evidence.txt`).** [C, the DLL fork at e9d0c4837] `GameplayUnitCreated` is
  sent only by `CvUnit::setupGraphical` (`CvUnit.cpp` 1760-1805). Every DLL site that calls `setupGraphical()` on a unit
  it just made (`CvUnit.cpp` 6226 gift, 14435 upgrade, 24596 conversion to the barbarians, `CvPlayer.cpp` 13012 goody
  hut upgrade, 36148 unit-class replacement, `CvBeliefClasses.cpp` 5061 and `CvTraitClasses.cpp` 8014 barbarian
  conversions) made it with `initUnit(..., bSetupGraphical = false)`, so the DLL itself never sends a second `Created`
  for one unit (`CvUnit.cpp` 24632's call has an unclosed comment in its argument list and was not followed; its owner
  is the barbarians, not player 4). The one other way is the EXE's: `ICvUnit1::SetupGraphical` (`CvDllUnit.cpp` 262-265),
  which the EXE calls in its `GameplayEraChanged` handler for each unit of the player whose art value differs at the
  new era (`unit-formations.md` 12.7.3, row "Era change"). [L, the run's own logs] `proxy-calls.tsv` of the run counts
  **20 `ICvUnit1::SetupGraphical` calls from the EXE** in the 30 turns, with 23 kind 33 events, while the feed counts 1
  known creation in 1,830 kind 11 events and 847 kind 21. So 19 of the 20 re-creations sent `Embark(1)` (an embarked
  unit, whose art changes with the era: kind 21 of a held key, an embark swap for the allocator, no LOUD line) and one
  sent `Created` for the land unit (4, 15542) [I: no per-call log of the keys]. The allocator names a held key's kind 11
  KnownCreation unless an era window of the owner is open and its art test (`UnitsCore::artChanges`: the module's art row
  at the old and the new era) listed the unit (`srv_units_seed.cpp` `Apply`). **Cause [I]:** an era change of player 4
  whose re-creation of (4, 15542) the server's art test did not expect (the EXE's art value differs where the art row
  does not, as `unit-formations.md` 12.7.10 (2) saw on the product path), or a non-setup event between the kind 33 and
  the re-creation that closed the window. **Not a feed rule** (the feed's record is right either way: the key is held,
  the record refreshed); the allocator's era model (4462's) decides the seed. A live run that settles it: an AI era
  change with land units whose art row does not change (Lua `Teams[t]:SetCurrentEra`), with the scene's feed recorded
  (`render_test_record`) or `render_test_units_table` and the reader of `unit-formations.md` 12.7 before and after. **The pre-check at that load:**
  database copy 276.6 ms (6647 pages), `pre-check of generation 1: 233 art rows, 0 failed, 2 warnings ..., 16398 ms`;
  memory 20 s after the load: server.exe private 1304.8 MB, working set 649.3 MB (psutil), the server's own line private
  1216.65 MB, video_local 793.85 MB; EXE private 1447.6 MB; at turn 380: server private 1780.6 MB, video_local 822.9 MB,
  EXE private 1627.9 MB (largest free block 1600 MB) [L, workstation: indications, D106; the pre-check's own peak memory
  and the per-arm difference are F (3)'s]. Data: `F:/c5-64bit-work/0375/4461/huge/out/` (`run.json`, logs).
- **F (3) result [L] (oldpc testbed job `20261116-031605-b381`, `--quiet --headed`, 2026-11-16 03:16-05:05 EST, exit
  0).** In PERFORMANCE.md section 6's form:

  ```
  Metric / tool:        idle-window EXE frame time (Lua frame counter, fps_idle.py --measure, 45 s, 3 blocks); server.exe
                        CPU ms and GPU ms per drawn frame over the same window (psutil CPU seconds; Windows "GPU Engine"
                        3D utilization x window / frames drawn; frames = render.log terrain 1s drawn=); memory (psutil
                        private / peak private; the server's own mem periodic video_local) at the window and after the
                        turns; AI turn wall (end turn -> next human turn) and server CPU s per turn over 10 AI turns;
                        server/evidence/0375/4461/cost_4461.py at 451c5c29 (gpu_sample.ps1, test_cost_4461.py 28/28)
  Builds:               one build in every arm: server.exe D9F5F17C (repo d0559d8e9, release, dirty 0), x64 DLL 29F58E17
                        (pin 04b1211b8, layout x64-04b1211b-pfdee656), proxy 88860726, composite 302D135C; arms differ
                        only by vpproxy.ini server_cfg_extra= (TEST ONLY): u1 render_units=1, u0 render_units=0
  Game / config:        TB-HUGE_0350 copy (c499d3c6), product path (D33 with Overlay on, ServerRenderer = 1, ring + fence,
                        lockstep, depth), timing launch (no frame cap), WaitForVSync 0, config.ini LoggingEnabled 0,
                        turns 350 -> 360; instance tb1 (oldpc's own EXE, VT present)
  Series:               warm-up u1 (measured, not counted) + 6 interleaved runs per arm (u1,u0,u0,u1 x 3), one game at a
                        time, 8.3 min per arm; quiet job: alone true, undisturbed false (foreign 0.171 cores mean against
                        the 0.1 limit: svchost 477 s, MsMpEng 250 s, audiodg 78 s of 6,552 s)
  Result (u1 - u0, paired by repetition, n 6, 95 % t interval; smallest detectable at 80 % power):
    server CPU per drawn frame   u1 18.65 ms (sd 0.30) / u0 3.41 (sd 0.20): +15.24 ms [+14.78, +15.70], det. 0.62
    server GPU per drawn frame   u1 1.413 ms (sd 0.007) / u0 1.060 (sd 0.009): +0.352 ms [+0.337, +0.367], det. 0.020
    server frames drawn per s    u1 61.4 (sd 0.8) / u0 265.3 (median 271.2): -204 [-218, -190] (-76.8 %)
    EXE frame time (idle)        u1 3.734 ms (sd 0.046) / u0 3.754 (sd 0.235): -0.019 [-0.273, +0.234], det. 0.345;
                                 without pair 4 (R4-u0 ran at 81.6 % machine CPU in its window, 236.6 fps: a disturbance)
                                 +0.078 ms [+0.016, +0.139] (+2.1 %), n 5
    EXE GPU 3D utilization       u1 32.5 % / u0 25.6 %: +6.8 points [+5.3, +8.4] at the same EXE frame rate [cause: I]
    AI turn wall                 u1 23.48 s (sd 0.49) / u0 23.16 (sd 0.60): +0.32 s [-0.63, +1.28], det. 1.29 (5.5 %):
                                 the feed's added time per AI turn is not measurable at this size (below ~1.3 s)
    server CPU per AI turn       u1 35.06 s / u0 27.51: +7.55 s [+6.34, +8.76] (+27.5 %; the render thread draws through
                                 the AI turn: 61 frames/s x 15 ms is about 0.9 s of CPU per second [I])
    pre-check (u1 only)          13,896 ms (sd 190; 13,694-14,110) in the live server, 233 art rows, 0 failed
    server private after load    u1 1,114 MB / u0 1,600: -486 MB; video_local 647 / 1,156: -508 MB. NOT a saving: at the
                                 window the terrain residency of u1 had baked far less (its render thread drew 61 frames/s
                                 against 271), so the load-time memory is confounded by the bake's progress [I]
    server private at the end    u1 1,716.8 MB / u0 1,666.4: +50.5 MB [+42.5, +58.4] (+3.0 %); peak private +52.0 MB
                                 [+42.3, +61.6]; video_local +27.4 MB [-8.1, +62.9] (not resolved)
    pre-check peak memory        not separable here: u1's peak private at the window, after the pre-check, was 1,127 MB
                                 (sd 7) and stayed below the process's later peak (1,761 MB at the end), so the pre-check
                                 (388 MB peak commit standalone, 4460 on the workstation) does not raise the server's peak
  Machine load:         window CPU mean 55-63 % per arm (R4-u0 81.6 %); job CPU mean 49.2 %; free commit 30.8 -> 25.7 GB
  Profile before/after: the next item (workstation sampler, attribution only): UnitRenderer::prepare 72.2 % of
                        server.exe's cycles with units on, of it ArtResolver::resolve 51.8 %; absent with units off
  Why it moved:         the unit art is resolved again from the files for every unit in every frame (next item);
                        the unit scene's per-frame CPU (+15.2 ms) makes the server's render thread the bottleneck:
                        the server draws 61 of the EXE's 269 frames per second, so the joined picture's world updates at
                        ~61 Hz while the EXE runs at ~269 fps; its GPU cost is small (+0.35 ms per frame)
  Correctness:          DoD F (1) parity identical (above); every u1 arm logged 5-13 FeedOutOfOrder lines (the race above)
                        and R1-u1 one `SeedMissing: unit (4, 17976) UNIT_SCIENTIST`, caused by that race: its FeedOutOfOrder
                        line for the same key came 5 ms before (the table held the creation one batch ahead of the
                        allocator, so the unit had no seed and was not drawn in that frame); u0 arms 0 LOUD lines
                        (2026-11-16, 4463: the race and so this SeedMissing are fixed, 2.2; not re-measured)
  Trust grade:          A by section 2.6 (6 interleaved per arm, spread and load reported) for the server's CPU and GPU
                        per frame and the end memory; the quiet proof says undisturbed false (foreign load over the limit)
  Run records:          13 sealed c5-run/1 records (series_id 20261116-031605-b381) in server/evidence/0375/4461/runs/;
                        summary server/evidence/0375/4461/cost-summary-20261116-031605-b381.txt; job data
                        F:/c5-64bit-work/0375/4461/oldpc-20261116-031605-b381/
  ```
  The window's units: `listed=739 culled=704` (35 drawn) at the save's camera in every u1 arm; `table` 853-855. The
  controller hook (4456) was not in this build (team branch d0559d8e); a second set after it is the lead's call.
- **Where the +15 ms go: the art is resolved again for every unit in every frame [L, C]** (`profile_4461.sh`, c5h-e,
  2026-11-16 05:09-05:22 EST; `perf/sampler/sampler.py --pid <server.exe> --allow-foreign-pid --duration 30
  --interval-ms 5` at DoD E's camera 1 (85, 31), the same build, the product's list; P1 `render_units=1`, P0
  `render_units=0`; an attribution, not a timing: the sampler slows its target by ~40 %, the workstation was shared;
  reports `server/evidence/0375/4461/profile/`). server.exe's sampled cycles: P1 92.5 G in 30 s, P0 17.6 G; the render
  thread 84.3 G against 9.9 G. Inclusive shares of P1: `Scene::draw` 88.4 %, `UnitsHost::beginFrame` 78.7 %,
  **`UnitRenderer::prepare` 72.2 %, of it `UnitRenderer::loadUnit` 53.2 % -> `ArtResolver::resolve` 51.8 % ->
  `c5m::resolve_fxsxml` 47.7 %** (`parse_fxsxml` 17.0 %, `parse_xml` 11.3 %, `GameFilesProvider::exists` 14.8 % and
  `::read` 9.4 % with `CreateFileW` / `ReadFile` / `CloseHandle` in the stacks, the heap 18-19 % in `operator new` and
  `RtlFreeHeap`), **`ModulePose::rows` 16.8 %** (`poseFigure` 8.3 %, `findAnimation` 7.6 %, `defaultClip` 7.1 %,
  `basename_lower` 10.1 % self+children), the cloud planes 4.4 %. The code [C]: `UnitRenderer::prepare`
  (`render/src/units/draw.cpp`) calls `loadUnit` for every unit of the frame's scene, and `loadUnit` calls
  `ArtResolver::resolve` (`art.cpp`), which runs `c5m::resolve_fxsxml` for every member: the unit's `.fxsxml` is found,
  opened, read and parsed from the game's files again in every frame (decision 10's residency keeps models, textures and
  clips, not the resolved art). So the cost is per drawn unit and member and per frame, not inherent to drawing: a
  cache of the resolved `UnitArt` per art row (and of the default clip per model) for the module's owner would remove
  about 52 % + most of 17 % of the server's CPU in P1 [I: not measured after such a change]. Not changed here (not this
  task's files). *Corrected 2026-11-16 by 4464: the art cache below removes the per-frame `.fxsxml` work; the pose's
  part stays.* (Replaced: nothing; "not changed here" was 4461's.)
- **The art cache per art row (4464, 2026-11-16, branch `task-4464-merge-cache`) [C: the code and its tests].** L0 and L1
  of a unit depend only on its art row (or on unit type, art style and era, which give the row) and on what
  `UnitRenderer::open()` loaded (tables, files, models, programs). So: `ArtResolver::resolve` keeps each row's result
  (status and art, a failure included; `resolve_shared` hands the cached art without a copy, `resolve_uncached` is the
  rule, `fxsxml_parses()` counts `resolve_fxsxml` calls); `UnitRenderer` keeps per art row the art, the member models
  and the programs' check (`loadRow`), and per (type, style, era) the row (`rowOf`); `UnitPlan::art` is a
  `shared_ptr<const UnitArt>` into that cache (its `unit_type` is "": the unit's type is the scene's). `open()` (a new
  database: the scene opens a new renderer per database generation, 3.3) drops both caches; `ArtResolver::clear_cache()`
  is there for a caller whose files change under the same tables (none today). `setArtCache(false)` gives the rule as
  before 4464 (the tests' reference and the "before" arm). Tests: `render/tests/units/test_art.cpp`
  (`cache_equals_rule_and_parses_once`: 8 rows incl. every failure, 50 frames, every field equal to the rule's, 8 parses
  in all, no file read after the first resolution; `cache_invalidated_by_open_and_clear`), `test_draw.cpp`
  (`art_cache_per_row`: 30 frames of the 11-unit fixture scene, plans equal to the uncached renderer's in every status,
  block, palette byte and draw; 5 parses in all against 17 per frame uncached; a reopen refills),
  `test_art_cache.cpp` (`catalogue_rows_cached_equal`: **3907's 229 catalogue art rows** on the catalogue's VP database
  (`F:/c5-64bit-work/0327/db/Civ5DebugDatabase.db`) and the `pure_vp` install (VP as DLC): all 229 `Ok`, every field of
  the cached art equal to the rule's, 327 parses for the first pass and none for the second). Suites: render units +
  unitanim 25/25 plain and MSVC ASan (0 reports), `test_scene_units` 36/36 plain and ASan.
- **Cost after the cache: an INDICATION, not a result [workstation, COMMON 23; 2026-11-16 06:12 EST; the machine loaded by
  this task's own ASan and server builds and other teams' jobs, and `quietlock.py check` was not run before it]:**
  `c5r_units_test_art_cache.exe art_cache_cost_indication` (release clang-cl, CPU only, no device): `prepare()` of a
  35-unit scene (the huge window drew 35) of the first 35 catalogue rows that draw (65 members), the module's own pose
  (default clip at t 0), a fixed placement (the module's placement needs the height field; it is not what the cache
  changes), 6 blocks x 10 frames, the three arms interleaved with the order turning per block. **Cache off (the rule
  before 4464): median 88.1 ms per frame (81.1-199.4), 65 `.fxsxml` parses per frame; cache on: 15.3 ms (11.7-50.2), 0
  parses per frame; cache on with a fixed pose: 0.46 ms (0.27-3.75).** So the parse is gone from the frame (asserted:
  the parse counter does not move after the first frame), and **what the unit pass still spends per frame is the pose**:
  `ModulePose::rows` once per figure **and per mesh** (`draw.cpp` asks the pose source per mesh; `poseFigure` samples the
  figure's clip for one mesh at a time), each call finding the default clip by name (`defaultClip` -> `findAnimation`
  over every animation with `ascii_lower` / `stem` string building, then `poseFigure` finds the clip again by name)
  before sampling; 4461's profile attributes to these lookups (`findAnimation` 7.6 %, `defaultClip` 7.1 %,
  `basename_lower` 10.1 %) more than to the sampling (`poseFigure` 8.3 %). Remedies for the owner of `pose.cpp` (0041 /
  0374), not done here: the default clip per pose model resolved once (cached with the model), the clip index instead
  of its name, one sampling per figure for all its meshes. The absolute numbers are larger than the live server's
  (u1 - u0 = 15.2 ms with 35 drawn units, F (3)) [I: the load, and VP's multi-member rows chosen here]; the F (3)
  testbed set on the cached build is the rerun wave's. *(2026-11-16, 4466: the three remedies are done, next item.)*
- **DoD C rerun on the fixed build (4465, 2026-11-16, branch `task-4465-rerun-c-e`).** Build: team branch 66891da6 (the
  fix wave 4462-4464 merged: allocator rules (a), (b), (d), (e) and the hold table, the feed's rules (f), (g), the race
  fix, the fog filter wired), `server/evidence/0375/4465/prep_build.sh`: server.exe **061E513E...** (repo_commit
  66891da69, `repo_dirty=0`, release, the fork at the pin 04b1211b8 by `git archive`, 0385's deps-release of the pin),
  proxy 468B4C74..., vp64helper 8722E053..., composite A32AF082..., in the pin layout `x64-04b1211b-pfdee656`'s bin (DLL
  29F58E17...). Method: 4458's (12.7.8 / 12.7.10 of `unit-formations.md`) through `server/evidence/0375/4465/c30.sh`: a
  copy `C5H-0375-4465-TINY_0018` of `C5H-3000-TINY-T0018` (E337244C), quick combat and quick movement off by `PreGame`,
  the feed recorded (`render_test_record`), the reader polled every 30 ms, 30 AI turns with a reader check after each
  (`live_4465.py turns`), then `Teams[t]:SetCurrentEra` for player 1 and then player 0 (eras 2, 4, 6) with a check
  after each (4463's named live check of the era re-creation, 3.4's KnownCreation item); the server's logs counted by
  `logcount.py` (LOUD lines by kind, FeedOutOfOrder, every KnownCreation line, the `units 1s` fields `fog=` and
  `fog_hidden=`). Expected: every unit equal at every check (4458's 6 carried differences of turns 29 and 35 gone by
  rule (d)), 0 FeedOutOfOrder, 0 KnownCreation, `fog=on` with `fog_hidden` > 0 while other players' units stand in the fog.
  **Result [L] (2026-11-16 07:39-08:09 EST, pool instance c5h-f (F:, Goldberg, VT present), claimed by W-4465):**
  - **30 AI turns (18 -> 48), a check after each: 30 checks, 1,949 unit comparisons, 1,949 equal, 0 differences**, no
    key on one side only, the Lua unit list equal to the reader's and to the server's table at every check (load 35/35;
    `server/evidence/0375/4465/checks_total_c30.txt`). The AI turns are deterministic (12.7.10): 4458's episodes came
    back (the EXE's free list held [65] at turn 35, [58, 71] at 38, [16, 59, 58] at 42, [32, 85] at 48) and the
    allocator now matches every one: rule (d) (a combat death's removal after the drain's creations) and the hold table
    hold live where 4458's build had 2 + 3 (+ 1 carried) differences. Feed: 7,128 unit events, 0 dropped, 114 creations,
    25 removals, 100 combats, 16 deaths in combat.
  - **FeedOutOfOrder 0** (4461's u1 arm of the same save and 30 turns: lines in every arm; 4458: 3): 4463's race fix
    holds live. **KnownCreation 0.** **The fog filter acts:** `fog=on` in all 1,495 `units 1s` lines after the scene was
    ready (`fog=none` in the 76 before), `fog_hidden` 1-7 (6 at the end: other players' units on plots team 0 does not
    see, no longer listed).
  - **Era changes (6, by Lua after the turns, players 1 and 0, eras 2, 4, 6): every check 89/89 equal**, and the EXE
    re-created no unit at any of them (kind 33 = 6; kind 11 = 114 = the table's inserts, known creations 0, embark
    creations 0): this save's units have no art that changes with the era, so 4463's case did not arise here.
  - **The era case on 4458's save (arm J2 of 12.2, 08:41-08:43 EST, the CAT_0009 copy after the P1 scene, the seam and
    the water units, 34 units):** player 0 to era 1 and 4: 34/34 equal; **to era 6: 4 differences**, the four embarked
    units re-created (1044 the embarked worker, 1045, 1049, 1050 embarked warriors; 1043, the embarked warrior at the
    centre of 1044's plot, kept its index 29 on both sides). Before: 1044 -> 30, 1045 -> 31, 1049 -> 32, 1050 -> 33 on
    both sides. After, the EXE (reader): **1044 -> 33, 1045 -> 32, 1049 -> 31, 1050 -> 30**; the allocator: 1044 -> 30,
    1045 -> 33, 1049 -> 32, 1050 -> 31. The EXE's result is exactly "the four records freed first (in walk order, onto
    the LIFO free list), then the four created in walk order"; the allocator kept 1044's index (rule (a): a `Created` of a
    held key inside the era window, `RecreateNow`; the feed counted known creations 2) and grouped the three
    `Embark(1)` swaps (pass 1: removals first), so only 1044 broke the EXE's order and shifted the other three. Player 1's
    era 6 afterwards: no change. No LOUD line; `known creations 2, embark creations 6` in the feed's totals. **This
    differs from 4458's [L] at era 6 (`unit-formations.md` 12.7.7 (2): "remove + create each, every index kept"), on
    which rule (a) rests:** with three units there (a land worker and two embarked units) the EXE kept every index; here
    (four embarked units, one a worker) it freed all four first. Which of the two the EXE does when, and so whether rule
    (a) must group a `Created` re-creation with the era's other re-creations, is [I]: a run with the feed recorded
    through the era step is needed (this run kept no feed; c30's recorder bundle stayed an empty `.partial` folder at
    the harness stop, because its frame span was placed after the run). For the owner of `srv_units_seed.cpp` (4462);
    `unit-formations.md` 12.7.7 and section 4's rule (a) need this note (not 4465's sections).
  - **New LOUD lines (the controller of 4456, in this build): units the server does not draw.** 2 `AnimFailed`: (25,
    1064) UNIT_WORKER `BadEvent: action work ... no work code for ENTITY_EVENT_IRRIGATE (build BUILD_FARM): the files have
    no work table` (a VP worker building a farm) and (2, 1093) UNIT_SETTLER `BadEvent: fortify: civilian_state_01.fsmxml
    never tests path 4 = 1000`; 1 `UnitRefused`: (1, 1073) UNIT_SHOSHONE_PATHFINDER `BadInput: no pose program for unit
    ... figure 5`; the last `units 1s` line counts `refused=696` (unit-frames). The EXE draws these units; the server
    does not (each "not drawn", logged once per type and code). Owners: the controller (0374 / 4456: the work table, the
    civilian fortify path, a figure without a program). The 1 further LOUD line is the TEST ONLY dump's cap.
  - Restore by SHA-256 ok, `provision.py verify` 0, the save copy removed. Counts: `logcount_c30.json`.
- **The pose layer's memo (4466, 2026-11-16, branch `task-4466-rerun-huge-cost` at 84f6c27f; the MC's grant #2395 of
  `render/src/units/pose.cpp` / `render/include/c5r/units/pose.h`) [C: the code and its tests].** `ModulePose` (the
  module's pose step; also under 0374's `ControllerPose`) now: (1) finds the default clips once per pose model
  (`PoseModel::idle` / `fortify`, filled by `buildPoseModel` with `findAnimation("ec:1000")` / `("ec:1520")`;
  `defaultClipIndex()`), not by name per mesh; (2) resolves the model and the clip binding through the last request's
  member / model pair (checked by the gr2 and `.fxsxml` names, so art reused at the same address is not mistaken) and a
  map (model, skeleton, clip name) -> the `ClipCache` entry looked up without building a string (a transparent
  comparator); a `ClipNotFound` is not kept and is asked again; (3) samples a skeleton once per figure: a ring of the last
  8 world poses keyed by (pose model, skeleton, bound clip, t's bits, loop rule) or by (pose model, skeleton, the whole
  pose program compared field by field, floats by bits), so the meshes of a figure after the first, and figures at the
  same clip time, only build their palette rows. `poseFigure` and `poseFigureProgram` keep their checks, codes, messages
  and float operations: their steps are shared functions (`checkMesh`, `sampleWorld`, `checkProgram`, `programWorld`,
  `paletteOf`), and the memo runs the same ones in the same order, so its rows are those functions' byte for byte. Not
  changed: `ClipCache` (its `hits` counter now counts fewer look-ups: the memo answers most), `bindAll`, `clipDuration`.
  New counters `worldHits()` / `worldSamples()`. **Tests:** `render/tests/units/test_pose.cpp` 11/11 (new
  `memo_rows_byte_equal`: 480 requests over 3 frames x 10 figures x 4 meshes, the default clip fortified and not, named
  clips by every look-up form, two models alternating, single-sample and blend programs with and without a mask, each
  memcmp-equal to `poseFigure` / `poseFigureProgram` for that mesh with their own cache and model, and the memo hit at
  least once per figure; `memo_errors_as_before`: every error path through the memo with the same code, message and
  identity rows as the per-mesh path: a mesh out of range, non-finite t / yaw / T, an unknown clip on a skinned mesh
  (twice) and on a mesh without bindings (Ok), a failed binding twice without a second read, a missing and a broken clip
  file, an unresolved mesh, no default clip, the fortified fallback, a member renamed in place, five bad programs; a good
  request byte-equal after the failures); `render/tests/units/test_pose_catalogue.cpp` (new, install-backed:
  `UnitRenderer::prepare` of **all 229 catalogue rows** (687 units over 3 frames, per-figure times, every third unit
  fortified) with every pose request also answered by the pre-4466 per-mesh path and by two programs per request:
  **14,715 answers, 0 differ**). **Cross-build check [C]:** the same test file built against the pre-4466 tree
  (ce89de9a) prints the same hash of the module's answers in request order, **FNV-1a 64 `2406d1c4d589bdca` both**
  (`server/evidence/0375/4466/catalogue-pose-old-tree.txt`, `-new-tree.txt`): the memo changes no row, code or
  message. Suites (`suites_4466.sh`): render units + unitanim 26/26 plain and MSVC ASan, `test_scene_units` 36/36 and
  its runner 7 plain and ASan (ASan line: see the F (3) item's correctness row). **Indication** (workstation, COMMON 23,
  not a result): the catalogue scene's pose requests 27 ms per frame against 63-67 ms by the per-mesh path (229 units,
  1,635 requests per frame); the old tree's own module 69-81 ms in the same file.
- **F (2) rerun (4466) [L] (2026-11-16 07:14-07:45 EST, pool c5h-g (F:, Goldberg, VT present), `huge_4466.sh` = 4461's F2
  step through `live_4466.py run-plain`).** Build `build_4466.sh` (07:05-07:13): server.exe **06BEC3C5** (repo 66891da69 =
  the team branch after the fix wave 4462-4464, `repo_dirty=0`, release; the art cache, no pose memo: the memo changes no
  row, previous item), proxy 468B4C74, composite ADEB3C11, the pin layout `x64-04b1211b-pfdee656` (x64 DLL 29F58E17).
  Copy `C5H-0375-4466-HUGE_0350` of the instance's `C5H-VP8P-HUGE_0350` (C499D3C6), `server_cfg_extra` naming only
  `render_units=1` (the controller at its default, `render_units_anim=1`, as the product runs it), the product's
  DisableSystems list. **30 of 30 AI turns (350 -> 380), rc 0, 0 EXE crashes, 0 server crashes** (no dump; the report
  ends with the harness stop's `PeerDied`), guard check 0 at load, in game and end, stop rc 0, no dialog, restore by hash
  ok and `provision.py verify` 0. **`out_of_step=0`** in every `units 1s` line; **dropped unit events 0**; the feed's
  counts equal 4461's run event for event (`unit events 197861 ... known creations 1, embark creations 34`, the same per
  kind): the AI plays the same game from this save. **`FeedOutOfOrder` 0** (4461: 20): 4463's race fix holds live.
  **LOUD lines: 28 in server.log, not 0** (`f2-loud-summary.txt` by `loud_4466.py`), none an R6 episode:
  - 1 `KnownCreation ... kind 11 seq 236334 key (4, 15542)` at 07:41:29, in the AI turn after the human turn 377: the
    same event as 4461's (seq 236335 there). **Cause [L] (`kc_4466.sh`, 08:06-08:23 EST, evidence
    `server/evidence/0375/4466/kc-evidence.txt`):** a copy of the run's own `AutoSave_Post_0377` (E6EC0139) on the same
    build, `render_units=1` with the TEST ONLY `render_test_units_anim` and `render_test_units_table` (the table file
    copied once a second), reproduces it in the first AI turn (`kind 11 seq 16354 key (4, 15542)`). In that turn the
    feed has **one era change (kind 33)**, and `proxy-calls.tsv` counts 1 `GameplayEraChanged` and **5
    `ICvUnit1::SetupGraphical`** from the EXE: between the table snapshots at seq 16162 and 17171 (which hold the
    creation) **player 4 (England) goes from era 5 to 6 (MODERN -> POSTMODERN)**, kind 33 from 0 to 1, and the unit's
    row changes era 5 -> 6, flags 9 -> 8 (embarked + visible -> visible; it disembarked earlier in the turn), seed 271 ->
    312. The unit is **`UNIT_GREAT_WAR_INFANTRY`**, whose `Units.UnitArtInfoEraVariation` is **0**: the module's art row
    is the same at both eras, so `UnitsCore::artChanges` says unchanged and the allocator's era walk (4462's S3 code)
    does not put the unit in the window's pending list; the EXE re-created it anyway (its own comparison, 0x591930 against
    the record, differs here [L that it re-creates; I why]). In `Apply` a creation of a held key of the era player that
    is not pending closes the window and is reported KnownCreation (`srv_units_seed.cpp` 574-595). **So the KnownCreation
    is the era rule's gap, not a feed or race fault:** rule (a) (re-create at once, not grouped) applies only to pending
    units, and the server's art test misses this EXE re-creation; the allocator then applies the creation as its message
    says (a second record for the key, the old index never freed [C, `Alloc`], where the EXE freed it and took it back), so
    the indices of later creations may differ from the EXE's until the next load [I: not compared here]. 4463's [I] (the EXE's
    era-change `SetupGraphical`) is now [L]; 4463's first explanation (the EXE's art value differs where the art row does
    not) holds. For the allocator's owner (not this task's file): inside an open era window, take a creation of any held
    key of the era player as a re-creation (remove + create at once) instead of requiring the art test's list, and keep
    KnownCreation for creations outside a window. The probe's AI turn stopped at a leader screen (Germany's demand):
    `run_3308`'s `run-plain` did not answer leader screens (rc 5 "channel silent", the harness stop refused it as it
    should); answered by `cli.py click` (the reply, then Back), stopped by the harness, restored by hash (ok, verify 0).
    (Corrected 2026-11-16, ticket 0444: run_3308 now arms the in-game diplomacy handler after each load and, before
    its stop and when its turn loop finds the channel silent, waits for the human turn with screens answered in game;
    section 3.5.)
  - 27 from the controller (4456's wiring, `render_units_anim=1`, not in 4461's build), LOUD once per unit type and code
    by design (section 10, D118: no default clip, the unit not drawn):
    `AnimFailed BadLogicFile` `edited_aim_ms_idle_graph.dge: pose edge to node -1` (destroyer, dreadnought: VP's edited
    modern-ship graph, refused at bind time, known: `unit-controller.md` 6.1 and 15.2);
    `AnimFailed BadEvent` work without a work table (worker `ENTITY_EVENT_RAILROAD`, archaeologist `..._MINE`, great
    general `..._BUILD` (citadel), workboat `..._BUILD` (fishing boats): known gap, `unit-controller.md` 6.5, 14);
    **`AnimFailed BadEvent` `fortify: civilian_state_01.fsmxml never tests path 4 = 1000`** for 14 civilian types
    (caravan, diplomat, musician, engineer, scientist, great diplomat, merchant, missionary, prophet, writer, Spanish
    inquisitor, artist, envoy, inquisitor; [C] `render/src/unitanim/controller.cpp` 452: the fortify mapping requests
    S4 = 1000, which the civilian machine never tests): **new, not in the controller's notes**; `AnimFailed BadEvent`
    `fortify on ... (UNIT_COLONIST): member ART_DEF_UNIT_MEMBER_COLONIST2 has no ArtDefine_UnitMemberCombats row` (VP's
    data, `bridge.cpp` 187);
    `UnitRefused BadInput: no pose program for unit ... figure N` for dreadnought, destroyer, caravan, worker,
    archaeologist (types the controller failed: the module asks `ControllerPose` for a program the failed unit does not
    have [I]) and **great war infantry (4, 15764) figure 9, a type with no AnimFailed line** (the controller's figure set
    of that unit lacks figure 9 that the placement drew. Likely cause [C for the rule, I for this unit]: with
    `ActionBridge::remove_fallen` (MC #2147) a Damage event removes the fallen figures from the controller, and a
    revival is not modelled (`bridge.cpp` 125, `unit-controller.md` 14), while the placement draws `livingFigures` of
    the current damage, so a unit that heals gets a figure back on the placement's side and none on the controller's).
  *(2026-11-16, issue 0439: each of these 27 lines' causes closed by an EXE rule, 3.6.)*
  The scene's counters at the end: `refused=86720` unit-frames not drawn (the largest AnimFailed episode count 342), so
  **with the controller on, these units are not drawn on the huge save** (sleeping or fortified civilians, working
  workers and boats, modern ships). DoD F (2)'s "0 LOUD lines other than counted R6 episodes" **fails on the controller's
  lines; it passes on the feed's (FeedOutOfOrder 0) and needs the KnownCreation's answer**. Memory [L, workstation,
  indication]: 20 s after the load server private 1358.7 MB (render.log 1358.1, video_local 686.2), EXE 1467.8 MB; at turn
  379 server 2084.2 MB (video_local 651.0), EXE 1618.4 MB; the pre-check `233 art rows, 0 failed, 2 warnings, 22621 ms`
  (the workstation loaded by this task's suites). AI turn wall 37-56 s (indication). Data:
  `F:/c5-64bit-work/4466/huge/out/`; drivers `server/evidence/0375/4466/` (`build_4466.sh`, `huge_4466.sh`,
  `live_4466.py`, `loud_4466.py`).
- **Where the server's CPU goes after the art cache and the pose memo: the controller [L, C]** (`profile_4466.sh` = 4461's
  method, c5h-g, 2026-11-16 07:51-08:06 EST; server.exe AA69333D (84f6c27f7: the art cache and the pose memo), the
  controller at its default; `perf/sampler/sampler.py --pid <server.exe> --allow-foreign-pid --duration 30
  --interval-ms 5` at DoD E's camera 1 (85, 31); P1 `render_units=1`, P0 `render_units=0`; an attribution, not a timing;
  reports `server/evidence/0375/4466/profile/`). server.exe's sampled cycles: **P1 94.5 G in 30 s, P0 22.2 G** (4461:
  92.5 G / 17.6 G): the total with units on did not fall, because what was removed is replaced by the controller.
  Inclusive shares of P1: `Scene::draw` 91.2 %, `UnitsHost::beginFrame` 88.1 %, **`UnitsHost::animFrame` 81.1 %**:
  `AnimRunner::apply` 71.3 % (`Controller::apply` 62.1 %: `Controller::collect` 33.2 % with the `PoseProgram`
  construction and destruction of every figure, `Controller::step` 26.4 %, `updateMachine` 3.1 %; 42.5 % under a symbol
  the sampler names `GraphInstance::restartFrom`, called recursively, which is the graph walk `Graph::build` behind
  `program()` / `active()` under a folded symbol [I: identical-code folding; `restartFrom` itself is called only at a
  state change, `controller.cpp` 299]) and `AnimRunner::fillFigures` 9.6 %; the heap 14-22 % inside these. **The
  drawing itself is small now: `UnitRenderer::prepare` 5.8 %** (`ControllerPose::rows` 4.3 %, `ModulePose::rowsProgram`
  4.1 %, `memoRows` 3.4 %); no `ArtResolver` and no `findAnimation` / `defaultClip` / `basename_lower` in the profile.
  The code [C]: `Controller::apply` and `Controller::collect` (`render/src/unitanim/controller.cpp` 603, 667) loop over
  every unit the controller holds, i.e. **every unit of the table (896 on this save), in every drawn frame**, and
  `AnimRunner::apply` (`srv_units_anim.cpp` 349-370) clears and refills every figure's program each frame, while the
  frame draws the listed units only (35 at this camera, 3.2's cull). So the remaining per-frame cost of the unit scene
  is the controller's per-unit work for units that are not drawn; remedies for the owners of `render/src/unitanim/`
  (0374) and `srv_units_anim.cpp` (4456), not this task's files: step and collect only the listed units (the others
  need only their clocks advanced and their events applied), reuse the programs' storage instead of clearing the map
  each frame. The testbed's numbers for this build are the F (3) item below. *(2026-11-16, issue 4468: done, the first remedy (programs
  for the drawn set only, every unit's clock and machine still advanced: 6.1, `unit-controller.md` 18); the numbers are
  4468's items below.)*
- **F (3) rerun, set 1 [L] (oldpc testbed job `20261116-073856-6e83`, `--quiet --headed`, 2026-11-16 07:39-09:38 EST, exit
  0; 4461's method, `cost_4466.py` = `cost_4461.py` plus the kinds below and the server.exe swap).** In PERFORMANCE.md
  section 6's form:

  ```
  Metric / tool:        as 4461's set (idle-window EXE frame time, server CPU ms and GPU ms per drawn frame, server
                        frames per s, memory after the load and at the end, AI turn wall and server CPU s per turn,
                        the pre-check's time); server/evidence/0375/4466/cost_4466.py at 84f6c27f (test_cost_4466.py 40/40)
  Builds:               x64 DLL 29F58E17 (pin 04b1211b8, layout x64-04b1211b-pfdee656), proxy B37A3C7E, composite
                        ADEB3C11 in every arm; server.exe AA69333D (84f6c27f7: art cache + pose memo) in u1 and u0,
                        06BEC3C5 (66891da69: art cache, no pose memo) in uo, copied into the instance's VP64 folder
                        before each uo arm and back after it (each arm's run record names the server.exe it ran)
  Arms:                 u1 = render_units=1, the controller at its default (render_units_anim=1): the product;
                        uo = u1 on 06BEC3C5 (the pose layer's before); u0 = render_units=0
  Game / config:        TB-HUGE_0350 copy (c499d3c6), product path (D33, ServerRenderer = 1), timing launch (no frame
                        cap), WaitForVSync 0, LoggingEnabled 0, turns 350 -> 355; instance tb1 (VT present)
  Series:               warm-up u1 + 6 rounds of the three, the order turned each round (u1,u0,uo / u0,uo,u1 /
                        uo,u1,u0 x 2), one game at a time, 6.3 min per arm; quiet job: alone true, undisturbed false
                        (foreign 0.162 cores mean)
  Result (paired by repetition, n 6, 95 % t interval; smallest detectable at 80 % power):
    server CPU per drawn frame   u1 35.66 ms (sd 0.46) / uo 37.04 (sd 1.02) / u0 3.32 (sd 0.07)
                                 u1 - u0 +32.34 ms [+31.91, +32.77], det. 0.58
                                 uo - u1 (the pose memo's saving) +1.38 ms [-0.09, +2.86], det. 2.00: not resolved at
                                 95 % (R1-uo ran at 63.3 % machine CPU, 38.96 ms, +4.13; the other five pairs +0.38 to +1.42)
    server frames drawn per s    u1 31.9 (sd 0.3) / uo 30.6 (sd 0.7) / u0 271.2 (sd 2.9): u1 - u0 -239.3 [-242.2, -236.4]
                                 (-88.2 %); uo - u1 -1.24 [-2.29, -0.18] (-3.9 %)
    server GPU per drawn frame   u1 1.501 ms / uo 1.513 / u0 0.990: u1 - u0 +0.511 ms [+0.500, +0.522], det. 0.015
    EXE frame time (idle)        u1 3.737 ms / uo 3.745 / u0 3.677: u1 - u0 +0.059 ms [+0.043, +0.076] (+1.6 %)
    EXE GPU 3D utilization       u1 35.8 % / u0 26.8 %: +9.0 points [+8.9, +9.1] at the same EXE frame rate
    AI turn wall (5 turns)       u1 22.92 s / uo 22.59 / u0 22.53: u1 - u0 +0.39 s [+0.11, +0.68], det. 0.39 (+1.7 %): the
                                 scene's added time per AI turn, now just measurable [I: the render thread's CPU
                                 competing with the DLL's turn on 4 cores]; uo - u0 +0.06 s [-0.27, +0.39]
    server CPU per AI turn       u1 36.26 s / u0 27.46: +8.79 s [+7.93, +9.66] (+32 %)
    pre-check                    u1 13,777 ms (sd 146), uo 13,851 (sd 197): 233 art rows, 0 failed
    server private after load    u1 1,177 MB / u0 1,603: -427 MB; video_local 631 / 1,160: -529 MB. NOT a saving: the
                                 bake is starved as in 4461's set (the render thread draws 32 frames/s against 271)
    server private at the end    u1 1,774.2 MB / u0 1,653.9: +120.3 MB [+110.2, +130.5] (+7.3 %); peak private +97.0 MB
                                 [+84.2, +109.9]; video_local -41.6 MB [-61.4, -21.8] (the bake again); uo - u1 -11.4 MB
                                 [-28.5, +5.6] (none from the memo)
  Machine load:         window CPU mean 55-63 % per arm (R1-uo 63.3 %)
  Why it moved:         against 4461's set (compared by explanation, sets are not compared across runs): the art
                        cache removed the per-frame .fxsxml parse (4461's profile: 52 % of the cycles) and the pose
                        memo the per-mesh look-ups and sampling (about 1.4 ms here), but the controller (4456, the
                        product default since then, absent from 4461's build) runs per held unit in every frame:
                        81 % of the server's cycles in the profile above (Controller::apply / collect / step over 896
                        units while 35 are drawn), so the server draws 32 frames per s, not 61 (4461) nor 271
  Correctness:          every u1 / uo arm logs 34-36 LOUD lines, all the controller's (AnimFailed / UnitRefused, as
                        F (2) above); u0 arms 0; no FeedOutOfOrder and no SeedMissing in any arm (4461: 5-13 per arm)
  Trust grade:          A by section 2.6 for the server's CPU, GPU and frame rate per arm pair; the quiet proof says
                        undisturbed false (foreign load over the limit)
  Run records:          19 sealed c5-run/1 records (series_id 20261116-073856-6e83) in server/evidence/0375/4466/runs/;
                        summary server/evidence/0375/4466/cost-summary-20261116-073856-6e83.txt; job data
                        F:/c5-64bit-work/4466/oldpc-20261116-073856-6e83/
  ```
  **So the MC's condition (4461's 271 -> 61 fps, +15.2 ms) is not met by removal: with the product's controller the
  server's frame rate on the huge save is 32 against 271 (+32.3 ms of CPU per frame).** Every ms is attributed: about
  1.4 ms (uo - u1) was the pose layer's per-mesh work, removed by the memo; of the 32.3 ms that remain the profile gives
  ~81 % to the controller's per-unit work over all held units and ~6 % to the drawing (`prepare`, of which the pose 4 %),
  the rest the scene's other per-frame parts (`beginFrame`'s table copy and cull, the heap); set 2 (next item) measures
  the controller's part with statistics.
- **F (3) rerun, set 2: the controller's own cost [L] (oldpc job `20261116-080135-5bdf`, `--quiet --headed`, 2026-11-16
  09:39-10:50 EST, exit 0; the same `cost_4466.py`, builds, game and configuration as set 1, 2 AI turns per arm, warm-up
  u1 + u1,un,un,u1 x 3; foreign 0.188 cores mean, undisturbed false; 13 sealed records in
  `server/evidence/0375/4466/runs/`, summary `cost-summary-20261116-080135-5bdf.txt`).** un = `render_units=1` with
  `render_units_anim=0` (no controller: the module's default clip; 4461's on arm, now with the art cache and the pose
  memo). Paired by repetition, n 6:

  ```
    server CPU per drawn frame   u1 35.55 ms (sd 0.26) / un 5.01 (sd 0.07): u1 - un +30.53 ms [+30.21, +30.85], det. 0.43
    server frames drawn per s    u1 31.8 / un 253.4 (sd 2.5): -221.5 [-224.2, -218.9]
    server GPU per drawn frame   u1 1.504 ms / un 1.101: +0.403 ms [+0.394, +0.412] (the controller's figures drawn
                                 posed: the un arm draws every figure at its default clip [I])
    EXE frame time (idle)        u1 - un -0.143 ms [-0.176, -0.110] (-3.6 %): the EXE runs faster when the server draws
                                 32 frames per s than 253 (less GPU and CPU contention [I])
    AI turn wall (2 turns)       u1 25.99 s / un 26.70: -0.72 s [-1.01, -0.42] (the same contention [I])
    server private at the end    u1 1,503.6 MB / un 1,708.1: -204.6 MB (the bake starved in u1, not a saving)
    pre-check                    u1 13,882 ms / un 13,842: +40 [-72, +151], not resolved
  ```
  **So, by explanation across the two sets (sets are not compared by number):** without the controller the unit scene
  now costs about **1.7 ms of server CPU per frame** (un 5.01 ms here, u0 3.32 ms in set 1; the server at ~253 frames per
  second against ~271), against 4461's +15.2 ms: **4461's regression (271 -> 61 fps) is gone**, by the art cache (the
  `.fxsxml` parse, ~52 % of 4461's cycles) and the pose memo (~1.4 ms, set 1's uo - u1). **What the product now spends
  is the controller: +30.5 ms [+30.2, +30.9] per frame on this save** (896 held units, 35 drawn), which holds the server
  at 32 frames per second; its cause [L, C] is the profile item above (the controller steps, settles and collects a pose
  program for every held unit in every drawn frame). Remedy for 0374 / 4456's owners (not this task's files): advance
  only the clocks of units that are not listed and build programs only for listed units; until then the product's
  default `render_units_anim=1` costs ~30 ms per frame on a late huge game. *(2026-11-16, issue 4468: built and measured, the
  items below.)*
- **The controller steps every unit, builds programs for the drawn set only; no stall at its start (issue 4468,
  2026-11-16, branch `task-4468-controller-drawn-set`, MC grant #2598) [C, L].** The change: 6.1's drawn-set note and
  `unit-controller.md` 18 (every held unit's events, clock and machine every frame; programs, figure lines and triggers
  for the frame's listed units only; byte-equal programs, statuses and pictures by test, plain and MSVC ASan).
  - **Where the start's time went [L, attribution].** Offline (`server/evidence/0375/4468/start_uf.sh`: this branch's
    unitfeed on the huge units-on bundle `C5H-0301-HUGE_0350_20261116-090943_workstation`, the catalogue database, c5h-a's
    install and the three mods; workstation, an indication) the controller's start with the table's 853 units cost
    ~0.75 s (`uf-start-on.txt` / `-off.txt`: the first frame line 7115 ms after the launch against 6358 ms without the
    controller; then 33-65 ms per frame with every unit). Under `perf/sampler` (2 ms, the same run) `ClipGraphFactory::bind`
    was 31.6 % of the process's cycles, `c5m::read_gr2` 26.3 % (the Oodle decompression and the CRC of every clip file):
    bind read each clip of each member in full, on the render thread, for its duration (`driver.cpp`). Live on 16cf5b58
    (the drawn set, no read-ahead; pool c5h-d, workstation, 13:32-13:51 EST, `huge_4468.sh c5h-d 3 1` with the old
    suffix, work `F:/c5-64bit-work/4468/huge/`): the controller's first frame 1845, 2043, 2053 and 1938 ms (R1-R3, P1),
    the unit frame 2.07-2.37 s, the model fog's first mask 0.1-0.2 s after it, no other slow unit frame. **The fix
    (a669d00d):** the pre-check's thread reads the duration of every clip of every art row it reached
    (`ClipGraphFactory::readDurations`, the same read as bind's) and the render thread seeds the factory before the
    scene is ready (`seed`); bind reads only what it lacks (`lazyReads`, in the first-frame line).
  - **Item 3 (b): the huge save with `render_units=1`, three launches on the final build [L]** (server.exe 7D455FE7 =
    a669d00d, x64 DLL 29F58E17, proxy 58FD046A, composite 33605D7D; pool c5h-d (F:, Goldberg, VT present), 13:52-14:12
    EST, `huge_4468.sh c5h-d 3 1 2 huge2`: prep with `render_units=1` only, each run a fresh launch of the copy
    `C5H-0375-4468-HUGE_0350` (C499D3C6) through the load, the unit scene ready, camera 1 of DoD E (85, 31), the
    harness's stop; restore by SHA-256 ok, `provision.py verify` 0; `start_check.py` per render.log):

    ```
    run  pre-check  read-ahead (5402 clips, 231 rows)  controller's first frame  clip reads there  unit frame  loop step after the start  D118 / fog timeout  LOUD
    R1   18,506 ms  6,736 ms                            393 ms (853 held, 29 drawn) 26                707 ms      1.1 s max                   none / none          4
    R2   19,699     7,310                               437                         26                768         1.1                         none / none          4
    R3   15,345     6,588                               389                         26                676         1.0                         none / none          4
    ```
    The four LOUD lines of every run are the known ones: `AnimFailed` of `UNIT_DESTROYER` and `UNIT_DREADNOUGHT`
    (`edited_aim_ms_idle_graph.dge: pose edge to node -1`) and their `UnitRefused` (no pose program) when the camera
    shows one; 0 crashes, no dialog. The model fog's consumer went WAITING -> ACTIVE (first mask handed over) 0.1-0.4 s
    after the unit frame in every run. The 26 clips still read on the render thread belong to rows the pre-check does
    not reach [I: embarked rows; not listed]. **The cost of the read-ahead:** 6.6-7.3 s on the pre-check's thread after
    the pre-check (workstation), which the scene's readiness now waits for (the units appear that much later after a
    load); a parallel thread or a cheaper duration read (the clip file's header only, `models/`) would remove it (not
    built: named for the lead). *(2026-11-16, issue 0441: removed another way: the pre-check's own clip cache already
    holds every duration it bound, so the read-ahead takes those and reads only what the cache lacks: on VP's database
    5401 of 5402 from the cache, 1 read, 38 ms against 8.4 s for the second read, 0 of 5402 different;
    `unit-controller.md` 19.2-19.3.)*
  - **Item 3 (a): F (3) with the controller on, set A (oldpc job `20261116-135024-ae75`, `--quiet --headed`, 2026-11-16
    13:50-15:17 EST, exit 0) [L]: five runs per arm, below grade A's six (PERFORMANCE.md 7); set B below is the grade
    A one.** `cost_4466.py` unchanged (`--issue 4468`), the game, save copy (TBPP-HUGE_0350, c499d3c6), product path,
    timing launch, WaitForVSync 0, 2 AI turns per arm, instance tb1, as 4466's sets. Arms: u1 = this branch 7D455FE7
    (a669d00d: the drawn set and the read-ahead), the product (`render_units=1`, the controller on); uo = the team head
    B8A41184 (eaacb90a: every unit's program built every frame, as 4466's set 2 measured it); un = 7D455FE7 with
    `render_units_anim=0` (no controller). Order u1,uo,un / uo,un,u1 / un,u1,uo / u1,uo,un / uo,un,u1, warm-up u1; proxy
    58FD046A... and composite 33605D7D in every arm (`cost-inputs.sha256`). Quiet proof: alone true, undisturbed false
    (foreign 0.17 cores mean). Paired by repetition, n 5, 95 % t interval:

    ```
    server CPU per drawn frame   u1 10.09 ms (sd 0.21) / uo 34.35 (sd 0.37) / un 4.83 (sd 0.10)
                                 uo - u1 +24.26 ms [+23.77, +24.75], det. 0.66 (the drawn set's saving)
                                 u1 - un  +5.26 ms [+4.97, +5.54], det. 0.38 (what the controller still costs)
    server frames drawn per s    u1 117.6 / uo 33.0 / un 254.3: uo - u1 -84.5 [-86.7, -82.4] (-71.9 %)
    server GPU per drawn frame   u1 1.123 ms / uo 1.501 / un 1.082: u1 - un +0.042 [+0.011, +0.073]
    EXE frame time (idle)        u1 3.806 ms / uo 3.751 / un 3.941: uo - u1 -0.055 [-0.073, -0.037] (the EXE a little
                                 faster while the server draws 33 frames per s than 118 [I: contention])
    AI turn wall (2 turns)       u1 26.10 s / uo 26.03 / un 26.16: no difference resolved (det. 0.49-0.52)
    server CPU per AI turn       u1 41.47 s / uo 40.89 / un 36.54: u1 - un +4.93 [+4.27, +5.60]
    server private at the end    u1 1,744.8 MB / uo 1,480.1 / un 1,706.7: u1 - un +38.1 [+28.6, +47.5]; uo - u1
                                 -264.7: not a saving of uo (its bake starves at 33 frames per s, as 4466's sets)
    pre-check                    u1 13,680 ms / uo 14,039 / un 13,615 (the read-ahead is after it, logged apart:
                                 5.7-6.3 s on oldpc in the six u1 launches)
    ```
    The controller's start on oldpc (the six u1 launches): its first frame 270-330 ms, the unit frame 506-578 ms, 26 clips
    read on the render thread; no D118 stop and no model-fog timeout in any of the 16 launches; LOUD lines only the known
    `AnimFailed` / `UnitRefused` of the destroyer and dreadnought (u1 and uo alike) and none in un. Summary
    `server/evidence/0375/4468/cost-summary-20261116-135024-ae75.txt`, 16 sealed records in `server/evidence/0375/4468/runs/`,
    job data `F:/c5-64bit-work/4468/oldpc-20261116-135024-ae75/`. *(A first submission, `20261116-133100-2484` on
    16cf5b58, was cancelled at 13:47 for the final build; its killed arm was repaired by jobs `20261116-134853-4f41` (stop)
    and `-134944-09e3` (restore from its backup, `provision_verify_rc` 0).)*
  - **Item 3 (a): F (3) with the controller on, set B, grade A [L] (oldpc job `20261116-151820-f8bb`, `--quiet --headed`,
    2026-11-16 15:18-16:58 EST, exit 0; series_id 20261116-151820-f8bb).** In PERFORMANCE.md section 6's form:

    ```
    Metric / tool:        as 4466's sets (server CPU and GPU ms per drawn frame, server frames per s, idle-window EXE frame
                          time, memory after the load and at the end, AI turn wall and server CPU per turn, the pre-check);
                          server/evidence/0375/4466/cost_4466.py unchanged, --issue 4468
    Builds:               u1, un: server.exe 7D455FE7 (a669d00d: the drawn set + the clip durations read ahead); uo: B8A41184
                          (eaacb90a, the team head: every unit's program every frame), copied in for its arms; x64 DLL
                          29F58E17 (pin 04b1211b8), proxy and composite of build_4468.sh's layout2 in every arm
    Arms:                 u1 = render_units=1, the controller on (the product); uo = u1 on the team head's server.exe;
                          un = u1 with render_units_anim=0 (no controller)
    Game / config:        TBPP-HUGE_0350 (c499d3c6), product path (D33), timing launch, WaitForVSync 0, 2 AI turns per arm,
                          instance tb1
    Series:               warm-up u1 + 6 rounds, the order turned each round (u1,un,uo / un,uo,u1 / uo,u1,un x 2), one game
                          at a time, ~5.3 min per arm; quiet job: alone true, undisturbed false (foreign 0.21 cores mean)
    Result (paired by repetition, n 6, 95 % t interval; smallest detectable at 80 % power):
      server CPU per drawn frame   u1 10.22 ms (sd 0.22) / uo 34.36 (sd 0.24) / un 4.90 (sd 0.11)
                                   uo - u1 +24.14 ms [+23.82, +24.45], det. 0.43: the drawn set removes 24.1 of the 29.5 ms
                                   the controller cost (uo - un)
                                   u1 - un  +5.33 ms [+5.04, +5.62], det. 0.40: what the controller still costs (4466's
                                   set 2: +30.53 [+30.21, +30.85])
      server frames drawn per s    u1 115.2 (sd 1.9) / uo 32.9 / un 251.6: uo - u1 -82.3 [-84.1, -80.6] (-71.4 %);
                                   u1 - un -136.4 [-138.7, -134.1]
      server GPU per drawn frame   u1 1.136 ms / uo 1.494 / un 1.088: u1 - un +0.049 [+0.023, +0.074]; uo's higher figure
                                   is the same GPU work spread over 33 frames per s [I]
      EXE frame time (idle)        u1 3.863 ms / uo 3.801 / un 3.985: uo - u1 -0.062 [-0.082, -0.042] (-1.6 %): the EXE
                                   slightly faster while the server draws 33 frames per s than 115 [I: contention]
      AI turn wall (2 turns)       u1 26.95 s (sd 1.09, one arm 28.94) / uo 26.19 / un 26.49: not resolved (det. 1.1-1.6)
      server CPU per AI turn       u1 42.75 s / uo 41.24 / un 37.38: u1 - un +5.37 s [+3.74, +7.00] (the render thread
                                   keeps drawing during the turn)
      server private at the end    u1 1,748.3 MB / uo 1,492.5 / un 1,708.1: u1 - un +40.2 MB [+22.9, +57.5]; uo - u1
                                   -255.7: uo's bake starves at 33 frames per s (4466's sets), not a saving
      pre-check                    u1 14,096 ms / uo 13,950 / un 13,837: not resolved (det. 0.6-1.2 s); the read-ahead after
                                   it 5.8-6.4 s in the seven u1 launches (logged apart)
    Machine load:         window CPU mean 47 % (quiet proof), per arm in the summary
    Profile:              the item below (4466's P1 against this build's P1)
    Why it moved:         the controller builds pose programs, triggers and figure lines for the ~32 drawn units only and
                          steps the other ~821 with their clocks and machines (unit-controller.md 18): animFrame ~80 M ->
                          ~8.3 M cycles per frame by the profiles; the 5.3 ms that remain are the item below
    Correctness:          every u1 and uo arm logs only the known AnimFailed / UnitRefused lines of the destroyer and
                          dreadnought, un none; no D118 stop and no model-fog timeout in any of the 19 launches; the
                          controller's first frame 265-527 ms and the unit frame 509-888 ms in the seven u1 launches;
                          equivalence by the tests of 6.1 and unit-controller.md 18 (plain and ASan)
    Trust grade:          A by PERFORMANCE.md 7 for the per-arm pairs (one series, one machine, six runs per arm, the quiet
                          lock held); the quiet proof says undisturbed false (foreign load over the limit), as 4466's sets
    Run records:          19 sealed c5-run/1 records in server/evidence/0375/4468/runs/ (with set A's 16); summary
                          server/evidence/0375/4468/cost-summary-20261116-151820-f8bb.txt; job data
                          F:/c5-64bit-work/4468/oldpc-20261116-151820-f8bb/
    ```
  - **Where the remaining ms go (u1 - un, +5.33 ms on oldpc, set B; +5.26 in set A) [L, attribution; the split by share is I].** Profile
    `profile_4466.sh`'s method through `huge_4468.sh`'s P1 (pool c5h-d, 2026-11-16 14:06-14:11 EST, server.exe 7D455FE7,
    `perf/sampler --pid <server.exe> --allow-foreign-pid --duration 30 --interval-ms 5` at camera 1 (85, 31); 32 drawn and
    821 held units not drawn; the server at the workstation's frame cap, ~30 frames per s, so 47 % of its sampled cycles
    are the loop's wait; reports `server/evidence/0375/4468/live-a669d00d/P1-sampler-report.txt`, the 16cf5b58 build's
    `live-16cf5b58/P1-sampler-report.txt`). Of server.exe's 42.3 G cycles in 30 s, inclusive: `UnitsHost::beginFrame`
    12.57 G, of which **`UnitsHost::animFrame` 7.46 G** (`Controller::apply` 6.72 G: `Controller::step` 4.38 G, within
    it `updateMachine` 2.11 G, `Controller::collect` 1.70 G; the feed's close, the drawn set, `setProgram` and
    `fillFigures` 0.74 G) and **`UnitRenderer::prepare` 4.64 G** (`ControllerPose::rows` 3.77 G: the 32 drawn units'
    programs evaluated at clip times that move every frame, `eval_curve` 1.44 G); the heap 2.2 G inside these. Against
    4466's P1 (`server/evidence/0375/4466/profile/P1-render_units1-report.txt`, AA69333D: `animFrame` 76.66 G in 30 s at
    32 frames per s, ~80 M cycles per frame) `animFrame` is now ~8.3 M cycles per frame (-90 %) [L, attribution: the
    sampler slows its target ~42 %; per frame by the frames drawn]. So the 5.3 ms split by these shares (un also prepares
    its 32 figures, at the module's default clip: its memo at t 0 is nearly free) [I]: **~3.6 ms the controller's frame**
    (~2.1 ms the 853 units' steps: the clock, the graph clocks and one machine update per figure, ~1.0 ms of it the
    machine updates; ~0.8 ms `collect`: the statuses of every unit and the hidden units' failure checks; ~0.4 ms the
    rest), **~1.7 ms the drawn figures' pose rows at moving clip times** (`ControllerPose::rows`, 0041's pose layer, not
    this issue's files), the GPU +0.04 ms. What could take more of it, not built (named for the lead): the per-unit
    `std::vector` of `Controller::step` and the statuses rebuilt every frame (the heap: 5 % of the cycles), the hidden
    units' machine updates where no socket of the state can fire before an event (only `CheckPathOption` tests), and a
    pose-row memo for moving times. *(2026-11-16, issue 0441: the frame's string work removed (the forced-choice point
    names per roll, the graph's input look-ups by name, two walks per ECComplete, program copies, per-mesh mask
    resolves), byte-equal; `unit-controller.md` 19 has what moved and the live series.)*
- **The era rule and the embarked art live (4469, 2026-11-16, branch `task-4469-era-embarked`) [L; pictures DIAGNOSTIC].**
  Pool instance c5h-e (F:, Goldberg, VT present), claimed by W-4469 11:07, released 12:12; drivers
  `server/evidence/0375/4469/` (`build_4469.sh`, `run.sh`, `live_4469.py` plan1 / plan2 / plan2b, `lua/`); save copies
  `C5H-0375-4469-CAT_0009-r1` / `-r2` of `REL548-TINY_0008` (b2ceb663); both restores by SHA-256 ok, `provision.py
  verify` 0, leftovers clean.
  - **r1, the evidence (11:19-11:37 EST, the team head 8c920dc0, server.exe 9E7D6829):** `unit-formations.md` 12.7.11:
    the feed recorded through three era changes; J2's era-6 case came back (4 differences on the team head); the rule
    in section 4 replays it 369 / 369. The plan's 2 AI turns (to push the recorder's buffer to the file) did not run:
    end turn blocked by production.
  - **r2, the fixed build (11:50-12:10 EST, d928cacb = the rule and the embarked row, server.exe A7988F7B, proxy
    AF82D4E8, composite 142EBAE7):** J2's state, Polynesia's eras 1, 4, 6, then a Carthage warrior embarked at (51, 10)
    (team 1 given `TECH_SAILING`) and Carthage's eras 4 and 3: **11 reader checks, 367 of 367 units equal** (eras 1, 4,
    6 of Polynesia 34 / 34 each, where the team head had 4 differences from era 6 on; Carthage's era re-creations
    35 / 35). `units 1s` at the end: `out_of_step=0 refused=0`; **1 LOUD line in the whole run**, the designed one:
    `PlayerArtUnknown: embarked unit (1, 1055) ... era 3: GALLEON or GALLEY by the team's all-water passage, which the
    feed does not carry: not drawn`. **The server's rows** (the newest `.c5units` dump at each picture,
    `server/evidence/0375/4469/r2/art_rows.jsonl`): Polynesia's embarked units `ART_DEF_UNIT_U_POLYNESIAN_WAR_CANOE`
    (the water warrior 1054 and the stack 1043-1045, 1050), Carthage's 1055 `ART_DEF_UNIT_GALLEY` at era 0,
    `ART_DEF_UNIT_TRANSPORT` at era 4, not listed at era 3. **The pictures** (the composite's back buffer, flags hidden,
    popups closed; Visual Checks `vc-20261116-121143-workstation-c80a` era 0 pass, `-121145-...-ca34` era 4 pass,
    `-121147-...-c556` era 3 info, `-121148-...-fa4c` the capital view pass; crops on F:
    `F:/c5-64bit-work/0375/4469/r2/out/pics/`): the EXE draws Carthage's unit as a galley at era 0, as two transports at
    era 4 and as a galley again at era 3 (no Astronomy), which is the embark handler's rule of 3.2 seen live; every
    embarked unit of Polynesia shows the war canoe only, **no land figures over it** (4465's fail: 12 standing warriors
    over the boat). The server's model lies under the EXE's in the joined frame (12.2 item 3 (4)), so the pictures show
    that nothing else is drawn there, not the server's model by itself. **Not the same view as 4465's water camera:**
    `UI.LookAt(51, 11)` after SETUP gave another pan and zoom than 4465's arm (the first water picture panned to the
    capital: SETUP's selection, E9), so no same-camera stock reference; the statistic of 12.2 was not rerun.
  - **Found on the way [L]:** a unit of a civilization the human has not met, created where the human sees it, brings up
    that leader's first-contact screen, which holds `CvGame::update` and so the Lua channel (r2's first Carthage step
    timed out; the harness stop then refused, rc 6); the harness's `key ESC` closed it, then `CLOSE_POPUPS` the "New
    Era" popup, and the run went on in the same game (`plan2b`). The barbarians or a met player avoid it.

### 3.5 The live drivers: pool owner from the caller, leader screens answered before the stop (ticket 0444, 2026-11-16)

Follow-up F6 of epic 0375 (about 17 minutes of 4466 and 4469). [C: the code and its offline tests; not run live.]

- **Pool owner.** Until 0444 the drivers `server/evidence/0375/44*/live_44*.py` wrote `C5_POOL_OWNER` (and
  `C5_JOB_OWNER`, in 4458 also `C5_QUIETLOCK_WHO`) = `0375` into their own environment, so a worker whose claim had
  another owner was refused by the harness's claim check; 4466 and 4468 worked round it by compiling 4461's `frames`
  again with the string `"0375"` replaced (`exec`), 4465 and 4469 by owners of their own (`W-4465`, `W-4469`). Now one
  helper, `server/evidence/0375/pool_owner.py`, gives every driver the caller's owner: `C5_POOL_OWNER` of the shell
  (COMMON 16) or `--owner NAME` on the command line (stripped before the driver parses its arguments, then set for
  the process and its children). `C5_JOB_OWNER` keeps the caller's value and defaults to the pool owner;
  `C5_QUIETLOCK_WHO` is never set by a driver. Without an owner a driver exits 2 before it touches an instance. The
  shell scripts of 4461 to 4469 already export their owners, so they run unchanged. Test:
  `python -I -m pytest server/evidence/0375/test_pool_owner.py` (10 tests: no driver names an owner, exit 2 without
  one, `--owner` reaches run_3308's `WHO`, 4466 / 4468 use 4461's `frames` as is).
- **Leader screens and the stop.** An AI leader screen holds `CvGame::update`, so the Lua channel is silent and the
  harness stop (`stop_readiness`) refuses; that is right, and it stays. In 4466's KnownCreation probe (2026-11-16
  08:07-08:19, c5h-g, the copy of `AutoSave_Post_0377`) Germany's demand came during the 20-s settle after the load,
  before `turns.py end` would have armed the in-game handler: the turn loop read "channel silent" (rc 5) and the stop
  refused after 300 s (`F:/c5-64bit-work/4466/kc/out/run.log`). `shell/evidence/3308/run_3308.py` now:
  1. arms the in-game diplomacy handler (`python -m tools.harness.diplo arm`, run-and-drive-the-game.md 12.2) right
     after each load (`res["diplo_arm"]`, also after each reload);
  2. before the stop (`stop_game`), and when its turn loop finds the channel silent (`human_turn`), checks the D155
     marker (`dialogwatch.py marker`; a box: nothing waited for, clicked or stopped, exit 8) and then runs
     `turns.py wait --instance NAME -1 --stall 20 --answer 8 --timeout 600`: FROM -1 means any active human turn; the
     wait re-arms the handler, which closes a screen through the screen's own Back / OnClose within about 2 s, and
     after 20 s of silence answers with the located refusing click (`input.answer_dialog`, dialogs.py: the refusing,
     neutral or only button, nothing clicked without confidence), at most 8 times. Both are the game's own UI; nothing is
     killed. The located click is never sent before the game was in game (front end, load screen);
  3. then the harness's own stop with the human-turn hook, as before; it still refuses what it cannot prove safe.
  The stops of `live_4456.py`, `live_4460.py` and `live_4461.py` (the same block) go through `R8.stop_game` too; the
  `stop` subcommands of 4458 and 4465 do not (run `turns.py wait ... -1 --answer 8` before them when a screen is up).
  Test: `python -I -m pytest shell/evidence/3308/test_run_3308.py` (12 tests: the order marker, wait, stop, leftovers;
  a box stops nothing; an unanswered screen still goes to the harness's stop; no click before in game; the argv parses
  in `turns.py`'s own CLI; `wait_turn` on a scripted silent channel answers once after the stall and returns the human
  turn, an active human turn or a screen-free AI turn needs no click). **Not verified live:** no run of the changed
  drivers in a game; no save shows a leader screen on demand (4466's `AutoSave_Post_0377` did once, by Germany's
  demand; whether a first-contact screen, 4469 r2, is closed by the handler or the located click was not tried; ESC
  closed it there).
### 3.6 Coverage of VP's art: the gaps of 4466's F (2) closed (issue 0439, 2026-11-16) [C: code and tests; L where marked]

Branch `task-0439-unit-vp-art-coverage` (account B). The 27 controller LOUD lines and 86,720 undrawn unit-frames of
4466's huge run (3.4) came from five gaps; each is closed by a rule taken from the EXE's behaviour, not by a default:

- **(a) Civilians' `fortify 0`.** `civilian_state_01` reads 1000 with a not-equal test (`P4!=1000` on its fidget and
  swap exits), which the controller's set of read values missed (method 1 only). The set now holds method 1 and method 6
  values, and a path-4 request whose value the figure's machine never reads is a **no-op** (not written, counted
  `ApplyCounts::unread_requests`, the `units 1s` field `anim_unread=`), no longer `BadEvent`: the EXE's fortify and work
  handlers post their action with no test of the unit's type or machine (`unit-controller.md` 5, 14.1). The setup's
  `fortify 0` therefore writes 1000 into an idle civilian, which already holds it. Test `civilian_fortify_off_at_setup`
  (both machine forms), `status_bad_event` (an unread 1180 on the omni machine stays Ok, counted per figure).
- **(b1) Work without a work table.** The EXE's `GameplayUnitWork` handler maps the build's **Type** to a tool (read
  2026-11-16, `unit-controller.md` 14.1 [C]): farm / terrace farm / polder 1920, mine / quarry 1940, road / railroad /
  scrub fallout / remove marsh / remove route 1910, remove forest / jungle 1930, everything else 1950; build -1 (the stop)
  posts idle, 1000. `ActionBridge` applies it (`workTool`), the server maps the stop to `action work -` (the `.c5anim`
  reader takes `-` as the stop) and an index with no Builds row to the hammer. A machine that does not read the tool's
  code (the great general's citadel, the work boat's fishing boats) takes a no-op. Tests `work_tool_by_build_type`,
  `work_table_replaces_the_rule`, `member_without_row_through_the_controller`, `anim_mapping` (server).
- **(b2) VP's modern ships.** 19 of VP's 21 `edited_aim_ms_*.dge` have a masked aim blend whose To has no edge; the
  evaluator now takes a Blend with one unconnected pose input as its connected input alone, so the destroyer,
  dreadnought, fleet destroyer and Yamato get the body's programs with no mask (`unit-controller.md` 15.2's added item;
  two combat graphs keep a connected mask, reached only through 0042's choreography). Test `program_may_fail_from_structure`.
- **(b3) The colonist.** VP's COLONIST2-4 members have no `ArtDefine_UnitMemberCombats` row; the bridge no longer checks
  `EnableActions` for fortify and work requests (the EXE posts them unchecked [C]) nor for a member with no row (what its
  word mask holds is not read [I]). Tests `member_without_combats_row_is_not_checked`,
  `member_without_row_through_the_controller`.
- **(b4) `UnitRefused` "no pose program".** Most were units the controller had failed (the module asked for programs a
  failed unit does not have). The remaining case, 4466's great war infantry figure 9, was a healed unit: the controller
  had removed its fallen figures (`remove_fallen`) and never gave them back while the placement draws `livingFigures` of
  the current damage. `Controller::revive` now gives back the figures the damage rule counts again, lowest index first,
  each in its member's default state (`units 1s` field `anim_revived=`). Test `damage_falls_revives_removed_figures`.
- **(c) The embarked art's inputs (MC #2908, #2941, #2942).** Kind 21 with a[2] = 1 carries in a[3] what the EXE's embark
  handler reads at the same call (`srv_feed.h` `EmbarkArtBits`; `unit-feed-contract.md` revision 2026-11-16): bit 0 read,
  bit 1 `ICvTeam1::CanEmbarkAllWaterPassage` of the owner's team, bit 2 an override, bits 8-31 the 24-bit FNV-1a hash of
  `ICvPlayer1::GetEmbarkedGraphicOverride` (two new pure rows; the server's `EmbarkArtOf` in `srv_remote_feed.cpp`).
  `UnitsCore` keeps each unit's bits from its last embark (cleared by a disembark, a creation, a removal, a load) and
  `EmbarkedRow(.., bits, ..)` applies the EXE's rule in full: the override when an `ArtDefine_UnitInfos` Type has its hash
  (a hash none has is an override that names no art define: the era rule, as the EXE; a hash two Types share: refused),
  TRANSPORT from `LAST_EMBARK_ART_ERA`, else GALLEON with the passage and GALLEY without. The Renaissance gap of 3.2 is
  closed; a recording without the bits (before 0439) keeps the older rule. Tests `embarked_bits` (server),
  `embark-art-bits` (`test_feed`), `kind-21-embark-art` (`test_remote_feed`, stand-in `ICvTeam1`).

**Suites (2026-11-16):** render `c5r_unitanim|c5r_units` 28/28 plain; `test_scene_units` 42/42; `test_feed` 295
checks, 0 failed; `test_remote_feed` 140 checks, 0 failed (each runner's negative test passes). After the merge of
`main` (19:46-20:12 EST, `suites_0439.sh`): the same plain, and under MSVC ASan render 28/28 with 0 ASan reports,
`test_scene_units` 42/42 (peak commit 1,195 MB) and its runner 7 passed, `test_feed` and `test_remote_feed` all checks
passed with the canary caught; 0 skipped anywhere.

**The coverage scene live, product against stock (2026-11-16, c5h-g, F:, Goldberg, VT present; DIAGNOSTIC pictures)
[L].** Drivers `server/evidence/0439/` (`build_0439.sh`, `cover.sh` -> `live_0439.py` over 0375's `live_4465.py`,
`cover_arm.py`, `lua/*.lua`, `cover_compare.py`). Build: server.exe f2c3f340 (repo 0d0f414c4, release, the x64 pin
04b1211b8 with 0325's deps-release), proxy 645262a8, composite 9ed3dff5, the pin layout `x64-04b1211b-pfdee656`'s bin;
stock = the 32-bit pin `i0ship-26234371-ia`. A copy `C5H-0439-CAT_0009` of `REL548-TINY_0008` (B2CEB663, turn 9,
Polynesia), the same Lua in both arms: Agriculture, Mining, The Wheel and Astronomy for team 0 (era 3, the
all-water passage); near the capital a worker on a build (the scan chose `BUILD_MINE` at 50,14), a colonist, a settler,
a great war infantry at damage 70, later healed; at sea a destroyer, a dreadnought, an embarked warrior with Polynesia's
own override (war canoe), one after `SetEmbarkedGraphicOverride("ART_DEF_UNIT_U_DANISH_LONGBOAT")` and one after the
override was cleared (GALLEON). Pictures per arm: land at damage 70 (L_A), 5 s later (L_A2), after the heal (L_H), the
background after the kill (L_B); water (W_A, W_A2, W_B); the product's from the composite's back buffer, stock's by
PrintWindow, both 2554 x 1411. Try 6 (product 19:31-19:37, stock 19:37-19:42 EST) is the result; tries 1-5 failed on
the driver (a Lua argument, queued tech popups, and the camera moved by a unit selection: E9's known trap; now every
awake unit is held by skip / sleep / fortify, no selection, the camera re-aimed before every picture and the head unit
read -1 before and after it, `lua/look.lua`).
- **Every scene unit is drawn by the product where stock draws it** (side by side at the same camera, and the gate of
  `cover_compare.py`: a figure stock draws whose pixels the product does not change against its own background, while
  the two backgrounds agree). The gate named 5 blobs of 262-603 px: by inspection 2 are fog cloud patches (land: the
  clouds move between the pictures) and 3 are the ships' **reflections in the water** (stock draws the galleons' and
  sails' reflections; the product runs with `render_units_passes` 0, so the server draws no units into its reflection
  target: 12.2, not a gap of the controller). No unit, figure group or boat is missing; the `extra` blobs (product only)
  are cloud patches too. The worker mines with the pickaxe in both, the colonist, the settler and the infantry stand,
  after the heal the infantry's full formation in both, the destroyer and the dreadnought, the war canoe, the Danish
  longboat and the two galleon models in both. Not compared: the animation phase per figure (no pixel statistic of the
  units is claimed; the controller's clocks start at the creation, the EXE's [I] at its own).
- The product's `units 1s` line at every picture: `refused=0`, `not_drawn=0`, `anim_unread=0`, `anim_revived=8` from
  the heal on; LOUD lines: the TEST ONLY dump's cap only. Also seen: the product has no trees (the forests of stock's
  pictures; not units, epic 0431's).
- Pictures in the Visual Checks feed (issue 0439, DIAGNOSTIC: the product arm runs with TEST ONLY keys); data
  `F:/c5-64bit-work/0439/{p,s,cmp}/`; earlier tries kept as `try1`-`try5` there.

**The recorded feed (2026-11-16) [L, C].** The controller's input of the product arm of try 4 (18:41-18:47 EST, the same
build and Lua as try 6 before the camera hold; `render_test_units_anim`, 4,661 frames; data `F:/c5-64bit-work/0439/try4/`) replayed offline by 0374's driver with this branch's controller (`replay.sh`, `replay_check.py`; c5h-g's install
and mods, the catalogue database): **0 failed units in 4,661 frames**; every scene unit Ok in every frame it was held;
the worker sampled the pickaxe 1940 (11,384 rows), the infantry had 6 figures at damage 70 and 14 after the heal (the
revival), the destroyer and the dreadnought 1 figure each with programs and no mask, the colonist 3 figures, the three
embarked warriors created with `ART_DEF_UNIT_U_POLYNESIAN_WAR_CANOE`, `ART_DEF_UNIT_U_DANISH_LONGBOAT` (the run-time
override, through the hash) and `ART_DEF_UNIT_GALLEON` (the passage bit). The product arm's `units 1s` lines: `refused=0`
at every picture, `anim_unread=0`, `anim_revived=8` after the heal; one LOUD line, the TEST ONLY dump's cap.

**The parity pair (4461's F (1) on this build; the MC's condition of #2918) [L] (2026-11-16 20:12-20:25 EST, c5h-g,
`parity_0439.sh`):** a copy `C5H-0439-TINY_0018` of `C5H-3000-TINY-T0018` (E337244C), two arms of
`proxy/tools/live/arm.py`, 30 AI turns each, u1 = `render_units=1`, u0 = `render_units=0`: both rc 0; **`eq_stats.py`
u1 against u0: 376 keys after the load and 455 at the last turn, identical, 0 differing** (u1's feed `unit events 7042`,
as 4461's); restore by hash ok, verify 0. Data `F:/c5-64bit-work/0439/parity/`.

**The huge run (4466's F (2) on this build) [L] (2026-11-16 18:53-19:17 EST, c5h-g after `provision.py align548` and
`verify` rc 0; `huge_0439.sh` -> `live_huge_0439.py` over 0375's `live_4461.py`).** The same build as the scene, a copy
`C5H-0439-HUGE_0350` of the instance's `C5H-VP8P-HUGE_0350` (C499D3C6), `server_cfg_extra` naming only
`render_units=1` (the controller at its default), the product's DisableSystems list. **30 of 30 AI turns (350 -> 380),
rc 0, 0 EXE crashes, 0 server crashes** (no dump; the EXE ended with the harness stop's exit code 15, "not a crash
code" in server.log), no dialog, restore by hash ok and `provision.py verify` 0. **LOUD lines: 0 in server.log, 0 in
render.log** (4466: 28), vpproxy.log the 2 expected TEST ONLY lines (`loud_4466.py`: `F:/c5-64bit-work/0439/huge-loud.txt`).
Over all 1,308 `units 1s` lines: **`refused` 0, `not_drawn` 0** (4466: 86,720 unit-frames refused), `out_of_step` 0,
`colour_unknown` 0; at the end `anim_unread=35` (requests no machine reads: no-ops) and `anim_revived=564` (figures
given back to healed units); the feed's counts equal 4466's event for event (`unit events 197861 ... known creations 1,
embark creations 34`), so the AI played the same game; 0 KnownCreation lines (4469's allocator rule). AI turn wall
26-48 s, server private about 1.9-2.04 GB, EXE 1.59-1.63 GB (workstation, indication only, COMMON 23). Data:
`F:/c5-64bit-work/0439/huge/out/`. Not shown here: that every unit the EXE draws is drawn by the server at the same
place on the huge save (no stock arm of the huge run; `refused=0 not_drawn=0` says the server drew every listed unit, and
3.4's fog filter and cull decide the listing as before).

## 4. The seed allocator

The EXE's rule, as 4459 read it on 2026-11-15 (`unit-formations.md` 12.7, which corrects 12.1's "reuse the last freed"
[C]), restated for the server. One table for the units of all owners, kept **for the server process's life** (not
reset at a load):

```
alloc(key):  i = free.empty() ? size++ : free.pop_head()        ; seed[key] = i; live.push_back(key)
free(key):   if key not in seed: no-op                          ; (the EXE ignores an unknown removal)
             i = seed.erase(key); live.remove(key)
             if free.empty() or i < free.head: free.push_head(i) else: free.insert_after_head(i)
load_clear:  for key in reversed(live): free(key)               ; size kept (12.7.3 S5)
group end:   pass 1: the drain's creations of units alone on their plot (and their units' queued events), feed order
             pass 2: creations after another unit's event on their plot, Embark creations of keys with no record
                     (none in a load's setup group: the save's units in feed order, S2)
             pass 3: removals of units whose kind 15 came (died in combat) and that no decision-14 hold took
at once:     era re-creation by Created, paradrop: free(key); alloc(key)  ; not grouped (issue 4462)
era window:  kind 33 .. the first event that is neither a re-creation of the era player's held key nor a setup call:
             Created of a held key -> at once; Embark of a held key -> the group's embark swap; no art test, nothing
             freed at its end (issue 4469, unit-formations.md 12.7.11 [L])
```

Which feed events drive it (12.7.3, 12.7.4):
- **creation**: `kUnitCreated`; `kUnitEmbark(on)` for a key not in the table (a unit set up embarked at a load, 2.5;
  also a unit created on water, 12.7.9);
- **removal**: `kUnitDestroyed` (kind 14; kind 15 only marks the unit dying in combat, S1, and since issue 4462 moves
  its removal to the group's end, pass 3);
- **remove + create**: `kUnitEmbark` in either direction for a known key (the EXE swaps the model by a removal and a
  creation, so **an embark changes the unit's seed**); an era change (`GameplayEraChanged`, not in N5 today: the EXE
  removes every unit of the player whose art define differs at the new era and the DLL re-creates it through
  `SetupGraphical`, so a `kUnitCreated` for a known key inside an era change is a remove + create, not a fault;
  **applied at once, one unit at a time**, issue 4462 from 4458's 12.7.7 (2) [L]; *corrected 2026-11-16 by issue 4469
  (unit-formations.md 12.7.11 [L, recorded feed]): at once for a `Created` (a land unit); an `Embark(true)` re-creation
  (an embarked unit: every one of the era player at every era change) is the frame group's embark swap: the EXE frees
  them together and creates them after, in walk order; and the window takes every re-creation of a held key of the era
  player whatever the art test says (4466's great war infantry, `Created`, art constant; Polynesia's embarked
  warriors, `Embark`, land art constant), so the scene's art test is no longer asked; was "each re-creation of an
  art-changing unit at once, units the DLL does not re-create freed at the window's end"*); a paradrop (kind 19: **a removal and
  a creation at once**, 12.7.7 (3) [L], issue 4462; *was "the EXE posts a removal; what re-creates the record is not
  traced, 4453", corrected 2026-11-16*; a paradrop of a key with no record outside a load is counted `UnknownRemoval`
  and creates the record [I]);
- **load**: `load_clear` at a load mark before the load's creations (Q6), then the save's units in `SetupGraphical`
  order (players 0..63, each its cities then its units in list order). The DLL's own `GameplayUnitDestroyed` calls
  from `CvPlayer::reset` at a load find no key and are no-ops, as in the EXE.
- **order**: the EXE's dispatch order, equal to the feed's except one case (S4): during the human's turn with Combat
  on, a unit killed in a visible combat has its removal held behind the combat until its simulation ends (up to 12 s,
  20 s with a projectile) while another unit's creation passes at once. During AI turns the barrier keeps feed order;
  with quick combat or an invisible combat nothing is held. Decision 14 (new) rules how the allocator follows it.
  *(Corrected 2026-11-16, issue 4462 from 4458's 12.7.8-12.7.10 [L]: not one case but four. Besides the human-turn
  hold, (1) inside a drain the creations come after the removals of other units (4453's frame order, 12.7.6 (3)); (2)
  a combat death's removal (kind 15 seen) comes 0.11-0.30 s after the DLL's, behind the drain's creations, **also in AI
  turns and for a combat not shown**, so "during AI turns the barrier keeps feed order" does not hold for deaths in
  combat; (3) a creation after another unit's event on its plot, and an Embark creation of a key with no record, come
  after the drain's other creations. The pseudocode's "group end" lines are the allocator's model of (1)-(3).)*

*(2026-11-16, issue 4453 [L]: two rules added to the pseudocode above from the live checks, `unit-formations.md`
12.7.6: (a) **frame order**: the events of one drain form a group; inside it a creation is applied after the removals
of other units, and a unit's own events stay behind its creation (`SetFrameGrouping`, default on; the scene must call
`Advance` before reading seeds). A drain is the server's stand-in for an EXE frame: an exact grouping wants a mark of
the DLL's update boundary in the feed (not in N5; for 4452 / the MC). The removal of the human's selected unit, a frame
later still in the EXE, is not in the feed: a named source of difference. (b) **the hold**: `HoldParams` melee 4.6 s,
ranged 3.7 s, band 0.4 s from the stock path, the product's to be measured; `NoteCombat` takes the kind 25 / 28
record. The implementation: `server/src/units/srv_units_seed.{h,cpp}`, tests `server/tests/units/`. *Hold values
superseded 2026-11-16 by issue 4462: melee 4.3 s band 0.6, ranged 3.9 s band 0.35, below.*)*

*(2026-11-16, issue 4460 [C, the code and `server/tests/units/` cases `combat_hold`, `combat_out_of_batch`]: the
integration found decision 14 (a) not wired: the scene never called `SeedAllocator::NoteCombat` nor `SetHoldParams`,
and kind 28 never reached the allocator, so no removal was ever held. Now `UnitsCore::onBatch` hands every kind 25 / 28
event with its `Batch::combats` record (index `a[2]`, the record's `seq` equal to the event's, else `FeedOutOfOrder`)
to `NoteCombat` as a `CombatNote`: the sides' unit keys (a city side: owner and id -1), visualize = flag bit 6, the
human's turn = bits 10 (read) and 9 (the active player's `IsTurnActive`), quick combat = bit 8, class ranged for
ranged, bombing and air sweep, else melee; unread game flags hold nothing (counted `combatGameUnread`). The hold table
is 4453's stock-path measurement, melee 4.6 s, ranged 3.7 s, band 0.4 s (`kHoldMeleeSec` ...), set at the scene's
construction; the product path's hold is not measured (12.7.6 (5)), and a creation within the band is counted
`CombatHoldAmbiguous`, never guessed. *Superseded 2026-11-16 by issue 4462: the product path was measured by 4458
(12.7.7 (1)); the table is now melee 4.3 s band 0.6, ranged 3.9 s band 0.35, below.*)*

*(2026-11-16, issue 4458 [L, product path], `unit-formations.md` 12.7.7: the server's own allocator against the reader,
on c5h-c with `render_units=1`: fresh loads, creations of three owners, deaths in and out of combat, an upgrade, an
embark above the free head, a unit created embarked, eight visible human-turn combats with a creation inside each
hold, a save, its reload and a second load: **equal on every unit at every check**. Three rules the code does not
follow yet, each [L] with its measurement there: (a) **an era re-creation is a removal and a creation applied at
once** (the EXE re-creates one unit at a time and each keeps its index; the allocator's frame grouping permuted three
seeds); (b) **a paradrop (kind 19) is a removal and a creation applied at once** (the allocator only frees: the unit
gets no seed, `SeedMissing`, and every later creation is out of step); (c) **the product's hold**: melee 3.76-4.80 s
(median 4.12), ranged 3.73-4.18 s (median 3.73), n = 3 each; a table covering both paths: melee 4.3 s band 0.6, ranged
3.9 s band 0.35 (the code's constants are `srv_units_core.cpp`'s, 4.6 / 3.7 / 0.4). The fixes are in
`srv_units_seed.cpp` (era window, kind 19) and `srv_units_core.cpp` (the table): not 4458's files, named for the lead.
Two more rules from 30 + 18 AI turns and a one-frame scene (`unit-formations.md` 12.7.8-12.7.10), each replayed on a
recorded feed by `server/evidence/0375/4458/sim_alloc.py` and equal to the reader at every check with it (899 of
899 and 34 of 34; the server's present rules give 882 and 25): (d) **a removal of a unit that died in combat
(kind 15 seen) is applied after the same drain's creations**, also for a combat not shown in an AI turn (the EXE
frees 0.11-0.30 s after the DLL; decision 14's AI-turn exemption does not hold); (e) **inside a drain, a creation
that follows an event of another unit on its plot, and an `Embark(1)` creation, come after the drain's other
creations** (stacked and embarked units). And for the table, not the allocator: (f) a Visibility before the unit's
creation is ignored by the EXE (12.1); (g) the facing is the creation's (12.1).)*

*(2026-11-16, issue 4462 [C: `server/src/units/srv_units_seed.{h,cpp}`, `srv_units_core.{h,cpp}` and
`server/tests/units/test_units_seed.cpp`; branch `task-4462-allocator-rules`]: rules (a)-(e) above are in the allocator,
each written from 4458's description (D83):*
- *(a), (b): `RecreateNow`: an era re-creation inside kind 33's window and a kind 19 free the key and allocate it before
  the next event, outside the frame group (a unit with its own creation pending in the group keeps its order behind
  it). `paradropFrees` now counts a remove + create.* *(Changed 2026-11-16 by issue 4469: (a) for a `Created` only, and
  without the art test (no pending list, `eraFreedOnly` stays 0); an `Embark` re-creation goes to the group as any
  embark swap; `eraRecreated` counts both; `SeedAllocator`'s art callback is kept in the constructor and not called.)*
- *(c) the hold table: `HoldParams::bandSec` is per class (was one value); `srv_units_core.h` `kHoldMeleeSec` 4.3,
  `kHoldMeleeBandSec` 0.6, `kHoldRangedSec` 3.9, `kHoldRangedBandSec` 0.35 (7 melee samples 3.76-4.94 s, 7 ranged
  3.61-4.18 s, both paths, 12.7.7 (1)); was 4.6 / 3.7 / 0.4. The replay's `H` line takes 3 or 4 numbers.*
- *(d): a kind 15 marks the key; its kind 14 (not held by decision 14) goes to pass 3 of the group's end (counter
  `combatLateFrees`); a load forgets the marks. The human-turn hold, where it applies, takes precedence (seconds,
  not the group's end).*
- *(e): per group, the units with an event (kinds 11-13, 16, 17, 21-24) on each plot; a `kUnitCreated` after another
  unit's event on its plot, and a `kUnitEmbark` creation of a key with no record (either direction [I]: 12.7.9 saw
  Embark(1) only), go to pass 2 (counter `lateCreates`). An Embark of a known key (remove + create) stays in pass 1,
  as in `sim_alloc.py --stack-late`. **The group of a load's first creation keeps feed order** (no pass 2): S2's
  load order [L] and 12.7.7's reload with an `Embark(true)` unit, equal in feed order [L]; without it the unit set up
  embarked would take the last index of the load (found by `test_scene_units`' `visible_at_load`). A load whose
  creations span two drains would apply (e) to the second [I: both recorded loads came in one drain].*
- *With grouping off (`SetFrameGrouping(false)`) (d) and (e) do not apply (strict feed order); (a), (b) apply.*

*Offline replay (2026-11-16 06:00 EST, `server/evidence/0375/4462/`): `feed_to_replay.py` turns 4458's recorded
feeds (`F:/c5-64bit-work/0375/4458/{c3,er}/out/rec/.partial/feed.bin`) and the reader's checks into the replay form of
`test_units_seed.exe --replay` (events as `UnitsCore::onBatch` hands them to the allocator; `X` lines carry the
reader's EXE indices per check); the C++ allocator gives **899 of 899** (c3, 17 checks, 18 AI turns) and **34 of 34**
(E-R, the final check ER2-seam), as `sim_alloc.py` with both rules. Control (`ablate.py`): (d) off gives 882 / 34,
(e) off 899 / 25, both off 882 / 25: exactly `sim_alloc.py`'s base numbers, so each rule is what makes its feed equal.
`run_units_seed_test.py` replays both inputs on every run (plain and ASan). Not covered: (a) and (b) have no recorded
feed (only c3 and E-R ran with 0364's recorder; the c1 run of the era and paradrop steps kept no feed): their tests rebuild 12.7.7 (2), (3)
from the reader's numbers; the c3 run carries no stacked creation (`lateCreates` 0) and E-R no combat death; a drain
that ends before the EXE's free (12.7.10's "not covered") stays [I].)*

*(2026-11-16, issue 4469 [C: `srv_units_seed.{h,cpp}`, `server/tests/units/test_units_seed.cpp`; L: unit-formations.md
12.7.11]: the era window's rule above. Tests: `era change (S3, kind 33; 4469 window)` (re-creations whatever the art
test, a unit not re-created keeps its record, the closed window gives `KnownCreation`, the art callback never called,
an `Embark` re-creation grouped and the window kept open after it), `era window negatives (4469)` (another player's
held key, a new unit of the era player, a setup call keeping the window, the next era change replacing it),
`era: embarked re-creations grouped (4469)` (12.7.11's five units through eras 1, 4, 6: 29 / 33 / 32 / 31 / 30, then
29..33, then 29 / 33 / ...), 4462's `era re-creation at once` with 4458's real kinds (the worker `Created`, the two
warriors `Embark`). **Recorded feed replayed** (`server/evidence/0375/4469/r1_replay.txt`, made by
`checks_to_timed.py` + 4462's `feed_to_replay.py`; `run_units_seed_test.py` replays it on every run, plain and ASan):
**369 of 369** (11 checks: the load, the P1 scene, the seam, the water units, the kill, eras 1, 4, 6, a player 1 unit,
a unit created embarked, its disembark); control `control_replay.py` (the team head's allocator built from git):
336 of 369; live on the team head 4 differences from era 6 on. 4462's 899 / 899 and 34 / 34 hold. The live check on
the fixed build: 3.4's 4469 bullet.)*

The index is the unit's `formation_seed` until its next remove + create. The allocator's inputs are all DRAIN events
(plus the load mark and the era-change event of Q6), so a recorded bundle reproduces every seed offline (decision 2).

Premises (marks by the lead from 4459's reads of 2026-11-15, `unit-formations.md` 12.7; the live checks are 12.7.5):

| # | Statement | Mark | What answers it |
|---|---|---|---|
| S1 | Which call frees the record, and when | **Kind 14** (`GameplayUnitDestroyed`, from `~CvUnit`) posts the removal; kind 15 posts a different record (0x47) and frees nothing. For a combat loss the free comes at the delayed death, after the combat played [C; L 2026-11-16 by 4453: the record stays through kind 15 and is freed 3.6-4.9 s after the combat, `unit-formations.md` 12.7.6 (5)] | 4453 live: the reader across a death in combat |
| S2 | Records before the save's units at a load, and their order | Players 0..63, cities then units in list order; embarked units through Embark; a fresh process gives 0..n-1 [C; **L 2026-11-16 by 4453**: 22 records = the save's 22 units in player-then-list order, 12.7.6 (1); the catalogue's "0-19" is not the save's count] | 4453 live: a fresh load with the reader |
| S3 | What else takes or frees a record | Upgrade and gift: create, then remove at the delayed death; capture: remove then create; embark (both ways) and era change: remove + create; paradrop: a removal *(corrected 2026-11-16 by 4458 [L], 12.7.7 (3): remove + create at once; implemented by 4462)*; teleport, rebase, move, cargo, invisible and garrisoned units: no change [C; **L 2026-11-16 by 4453** for embark (seed changes to the head), upgrade (one frame: the new unit takes the old index) and capture; era change: no re-creation in six `SetCurrentEra` steps (L negative); paradrop not reached; 12.7.6 (4)] | 4453 live: embark above the free head, an era change, a paradrop |
| S4 | No reordering of a creation against a removal | **False in two cases** [L 2026-11-16 by 4453, 12.7.6 (3), (5)]: the combat-held removal (melee 4.2-4.9 s, ranged 3.6-3.8 s on the stock path, 8 of 8 creations inside it passed it) and, more often, **one frame's creations come after the same frame's removals of other units** (6 probes; the human's selected unit's removal a frame later still). The allocator groups by drain (`SetFrameGrouping`) and models the hold (`HoldParams`) | 4453 live: a kill in a visible human-turn combat and a creation within 1 s; decision 14 |
| S5 | Second load / reload in one process | The EXE frees every live record (last created first, by the free rule) and **keeps the array size**: indices after a reload depend on the previous session [C; **L 2026-11-16 by 4453**: two reloads through `PlayerChoseToLoadGame`, every index as the clear predicts, sizes 22 and 52 kept, 12.7.6 (2)] | 4453 live: two loads in one process with the reader |
| S6 | The re-layout never passes a seed of its own | True: its three callers pass 0 or the record's own value [C] | none needed |

## 5. The colour rule

Row 0 of every block of a unit = the owner's `PlayerColors.SecondaryColor` -> `Colors`, each channel
`trunc(float32(v) x 255) / 255` *(corrected 2026-11-16, issue 4453 [L], `unit-draws.md` 5.1: `trunc(float32(v) x 255)
x float32(1/255)`, a float32 product; the division misses by one ulp for some values; 0041's `art.cpp` fixed by 4453
the same day)*, alpha 1 (`unit-renderer.md` 10.3 [L, 6 owners and the barbarians]; `unit-draws.md` 5
[C]); the module does this from the `PlayerColors.Type` (`ArtResolver`, `art.h`). The chain the scene follows: the
player record's colour index (`ICvPlayer1::GetPlayerColor`, a `PlayerColorTypes`, mirrored on the proxy route,
`policy-table.tsv`) -> the `PlayerColors` row whose `ID` is that index -> its `Type` -> `PlayerInput.colour`. An index
with no row is a fault (10), never a default colour. Premises (W-4459 writes them into `unit-draws.md` 5):

| # | Statement | Mark | What answers it |
|---|---|---|---|
| C1 | Row 0 comes from the player's colour, not from the civilization's `DefaultPlayerColor` | The player's pregame colour; a second player of one civilization gets the lowest unused colour row; the EXE called `GetDefaultPlayerColor` 0 times in `server/evidence/2500/P1` [C DLL, L call log; **L 2026-11-16 by 4453**: the second Greece shows `PLAYERCOLOR_BLACK` (ID 0), Egypt the setup's `PLAYERCOLOR_DARK_PINK`, bit-equal on every captured instance] (`unit-draws.md` 5.1) | 4453 live: Tiny with one civilization twice and one colour set by `PreGame.SetPlayerColor`, captured (DoD D) |
| C2 | City-states and the barbarians follow the same rule | Yes: their colour is always their database default, written into the pregame colour (it overwrites a setup value) [C, `CvGame.cpp` 638, 775, 833, 853; **L 2026-11-16 by 4453**: the city-state's `MinorCivilizations` default and the barbarians' row, bit-equal] | the DoD D capture |
| C3 | The colour does not change in a running game | True for VP: only Lua `Game.AddPlayer` / `ChangeMinorPlayer` or `setPlayerColor` change it, VP calls none, and no `Gameplay*` call announces it [C]; the player table's turn refresh (2.4) still follows one | none needed |

## 6. The controller hook (decision 6)

What 0374's brief fixes (`docs/briefs/epic-0374-units-controller.md`, scope 0, decisions 2 and 8): the controller takes
per unit a timeline of renderer-terms events mapped from the contract's kinds 11-31, the `UnitRecord` flags and the
position records, with a file form that a recorded feed fills unchanged; it takes the frame's time step from its caller
and applies the EXE's step rule itself; where the step comes from is U6's. What this scene gives it:

- **Events**: each batch's unit events in `seq` order, mapped by the table of 0374's section 3 (`unit-controller.md`
  on `epic-0374-unitanim`, a draft before 0374's C1 review: `create` from kind 11 and from the table at start,
  `move` from 12 + its path, `place` from 13 / 30, `destroyed` from 14 / 15, `damage`, `fortify`, `embark`, `garrison`,
  `visibility`, `reset`; combat kinds 25 / 28 produce nothing here, they are 0042's).
- **Clock**: per drawn frame one `frame` with `dt` = the time between the server's draws of two consecutive EXE frames
  (the camera's `presentCount` step times the EXE's frame interval is not known to the server, so `dt` = the difference
  of the server's QPC at the two draws), `exe_frame` = the composite's frame id; replaced by `kKindFrameMark` when 0040
  produces it. The step rule (K6 as revised by MC #2023 on 0374's thread: 1/30 s steps, at most 6 per frame) is the
  controller's, not the scene's.
- **Output**: the controller as the drawing renderer's `PoseSource` (`setPoseSource`); until 0374 merges, none.

Waits for 0374's C1 answer and merge: the event names and arguments, the `.c5anim` form, whether the scene calls the
controller once per drawn frame or per EXE frame, the pre-check extension (0374 decision 7). Child 4456.
*(Answered 2026-11-16 by 4456, section 6.1: once per drawn frame of the scene; the pre-check extension is not wired.)*

### 6.1 As built (4456, 2026-11-16 02:59-, branch `task-4456-controller`) [C: the code and its tests unless marked]

0374 merged (`unit-controller.md`, `render/src/unitanim/`, read only here). The hook is `server/src/units/
srv_units_anim.{h,cpp}`; server.exe and `server/tools/unitfeed` link the same code (D137 rule A).

- **`AnimFeed`** (the input). `UnitsCore::onBatch` hands every unit event it takes (seq order, after the allocator, after
  the batch's table copy) to `AnimFeed::take`; nothing is kept before the controller starts. At each drawn frame, after
  `UnitsCore::build` (the allocator's `Advance`, so creates carry their seeds), `close()` turns them into one
  `c5r::unitanim::TimelineFrame`, **the form unchanged** (0374's `TimelineEvent`s): `dt` = the step of the scene's frame
  clock between two drawn frames (decision 6; the first frame 0), `seq` = the composite's frame id (kept strictly
  increasing: a frame id that goes back, a new composite, gives last + 1, counted `seq_bumps`), `exe_frame` = the frame id,
  `present_time` = the frame clock in s. The mapping (`unit-controller.md` 3) with this scene's choices:
  - the table when the controller starts: `create` per unit (table order); kind 11 `create` with the fields of the
    table after its batch (a held key: `remove` + `create`, the era change of S3); 21 for a key not held: `create`;
  - 12 `move <facing> <path length>` and **`arrive <facing>` in the same frame [I]**: position mode own (decision 5)
    puts the unit at its destination at once, so a run with no arrival would run on the spot; the walk and its
    arrival are 0042's;
  - 13 `place` at a[2], 30 `place` at a[3] (the to plot), the facing from the record at the frame's end;
  - **14 `remove` [I]**: the scene stops listing the unit at once; the mapping's `destroyed 0` then a delayed `remove`
    needs 0042's queue;
  - 15 `destroyed 1`, 16 `damage a[2] a[3]`, 17 `fortify`, 21 `embark`, 22 `garrison`, 24 `visibility` (bit 0, the blend
    seconds), 31 `reset`, 19 `action paradrop`, 20 `action activate`, 18 `action work <Builds.Type>` (a[2] -1, the work
    stop, has no event in the form: counted `work_stops`; an index with no Builds row: `work_unknown`);
  - 32 (load) `remove` of every held unit; 23, 25, 28, 29, 33 nothing (combat is 0042's; 33 changes the player row);
  - kOverflow: at the next frame the held set follows the re-copied table (`remove` of units gone, `create` of units
    not held, counted `resyncs`);
  - a create that cannot be formed (its record gone by the frame's end, an unknown type, owner, owner's art or plot, a
    facing outside 0-5) is skipped and the unit is not held; its later events are dropped (`not_formed`, `not_held`);
  - the players: a player's `PlayerInput` is given to the controller when first needed and again when it changes (an era
    change); the `.c5anim` form has one `player` record per player, so a change is live only (`player_changes`, a gap
    of the form, named for 0374).
- **`AnimRunner`** (the controller). Opened at each new unit database on the render thread's environment: the unit string
  databases (`loadStringDatabases`), `loadBridgeTables` + `ActionBridge` (`remove_fallen` true, MC #2147), `Builds`
  (ID, Type), `ClipGraphFactory` (`hold_from_continue` true, MC #2117), `ResolverArtSource` over the scene's
  `ArtResolver`, `ControllerOptions` defaults (clock continuous, MC #2089; no cross-fade rules; seed 0; free). A second
  `ModulePose` (its own clip cache) serves `ControllerPose`, which the drawing renderer gets with `setPoseSource` at
  each open. Per frame: `addPlayers` (changed rows), `Controller::apply`, every program into `ControllerPose`; the
  single-sample programs are also the frame's figure lines (`fillFigures`: the dump and unitfeed's files; a figure that
  blends has none, `.c5units` cannot hold a blend). One controller frame per drawn frame (`beginFrame`), not per EXE
  frame: the scene draws once per EXE frame it receives.
  **The drawn set (issue 4468, 2026-11-16) [C: code and tests].** Since 4468 `animFrame` hands the controller the frame's
  drawn set: (player, id) of every unit listed in any wrap copy after the cull and the fog filter (`UnitsHost::m_drawnSet`,
  `AnimRunner::apply(f, players, core, drawn, why)`; the old `apply` without a set, which unitfeed still calls, steps and
  programs every unit). Every held unit still takes its events, clock and machine every frame; programs, figure lines and
  triggers exist for the drawn units only (`unit-controller.md` 18: the rule, its [I] for the EXE, the equivalence
  tests). `UnitsOptions::animAll` (tests only, no configuration key) restores the old behaviour for a comparison.
  Logged: `units: the controller's first frame F: N units held (E events), d drawn, q not drawn; the feed's close x ms,
  the controller y ms` once per start; `units: slow frame ...` for a unit frame (beginFrame + the scene pass) over 250 ms
  with its parts (build and fog, the controller, prepare with the cached art rows, the scene pass; the first 20, then
  counted); the `units 1s` line gained `anim_drawn= anim_quiet= anim_ms_max= frame_ms_max= slow_frames=` (maxima per
  line). Test `anim_drawn_set` (`test_scene_units`): two hosts on one feed, A `animAll`, B the product; warrior 1001 in
  view of camera 1, 1002 of camera 2 only, 1002 fortified while B does not draw it; on all 30 frames the figure lines and
  the pictures (the render target's bytes) of A and B are equal, B's line says `anim_drawn=1 anim_quiet=1`, A's
  `anim_drawn=2 anim_quiet=0`.
- **Statuses (section 10).** `AnimFailed`: a unit the controller gives a status other than Ok (no program, so the module
  refuses its figures, not drawn), counted once per episode, LOUD once per unit type and code. `AnimUnavailable`: the
  controller cannot open (string databases, bridge tables, Builds) or refuses a frame: D125's route (the scene failed,
  as `PrecheckFailed`). No default clip anywhere (D118).
- **Switch and test keys.** `render_units_anim` (default 1; 0 = no controller, the module's default clip at t 0, for
  decision 9's forced-pose comparison; needs `render_units=1`). TEST ONLY `render_test_units_anim=FILE`: the
  controller's input as the server applied it (`.c5anim`, scene `-`, seed 0, free), rewritten (tmp + rename) at the
  second line when frames were added; an unwritable file is LOUD once.
- **The run-time missing clip (MC ruling recorded by 4460).** Not reached by this mapping: the siege tower's charge
  attack (1140) is requested only by `action attack_charge`, which 0042's combat choreography issues. Graphs bind
  lazily (`enterState`), so before 4770 such a request failed the unit (`EcNotBound`, AnimFailed) instead of keeping its
  state. **Built by 4770** (epic 0042, 2026-11-20; the MC's ruling of 2026-11-16, answer to 4456's ASK 2): the revert in
  `enterState` (the figure keeps its state, graph, clip and clip time; one LOUD line per occurrence, counted; the flag
  telling a missing-in-install clip from other `EcNotBound` cases, from the art step's `FileRef::found`; a start with no
  state to keep fails as before, flagged), with its tests: `unit-controller.md` 13.4. The line reaches the server's log
  only once a sink is wired there (13.4 names the header fields for it).
- **The civilians' `fortify 0` is the EXE's too, not the server's mapping (issue 4470, 2026-11-16) [C].** 4466's
  `AnimFailed BadEvent fortify: civilian_state_01.fsmxml never tests path 4 = 1000` for 14 civilian types (3.4) comes
  from kind 17 with `a[2]` 0, which this mapping turns into `fortify 0` with no test of the unit's type.
  - The DLL: `CvUnit::setupGraphical` (`CvUnit.cpp` 1760-1781 at the pin 04b1211b8 and at e9d0c4837) calls
    `GameplayUnitCreated`, `GameplayUnitShouldDimFlag`, then **`GameplayUnitFortify(unit, IsFortified())` for every unit
    it sets up that is not embarked**, civilians included: at each creation, load and era re-creation a civilian gets
    `fortify 0`. The server pushes kind 17 and forwards the same call unchanged (2.1), so the EXE receives it as the
    controller does.
  - The EXE: its handler of slot 152 at **0x619610** (`proxy-design.md` 17.4's table, `research/03-dll-engine-boundary/`) was read on 2026-11-16 with a
    headless Ghidra pass, read-only, on a copy of the `civ5` project (`reverse-engineering-setup.md` 2; the listing kept
    in scratch, not committed, D83). With a unit argument and one check on an engine object (its virtual at +0xB4), it
    fills a message with the unit's owner and id and the action found by name in a string-keyed table (0x61AA50,
    -1 when absent): **`IDLE` for off, `FORTIFY` for on**, and posts it (0x87E7E0, 0x813D60). It tests neither the unit's
    type nor its domain nor its machine. So the EXE sends its own unit system the same request for civilians; the
    server's mapping matches it, and nothing in `srv_units_anim.cpp` changes.
  - What the EXE does with it [I]: `IDLE` is the `EnableActions` word Idle, code 1000 (`unit-controller.md` 6.5, by
    name); `civilian_state_01` has no socket that tests path 4 = 1000 and its default state writes `S4=1000` itself
    (`unit-controller.md` 6.2), so an idle civilian already holds 1000 and the request changes no state: the EXE keeps
    drawing the civilian. 0374's controller instead refuses a value its machine never tests (`render/src/unitanim/
    controller.cpp` 452, `BadEvent`), the unit is `AnimFailed` and the server does not draw it. Left as it is (4470's
    brief). For the controller's owner (0374): accept a path-4 request the machine never tests as written and without
    effect (what the EXE's machine does with it), at least for `fortify 0` = 1000, with a test; `unit-controller.md` 5
    carries the pointer.
- **Not wired:** the controller's pre-check extension (0374 decision 7: `Controller::precheck` over every reachable art
  row): `checkArt` refuses the siege tower's charge-attack graph (`EcNotBound`), so wiring it into D125's route needs
  the same missing-in-install flag first. Cost (CPU per frame with every held unit, the second clip cache) not measured
  (DoD F 3).
- **Tests** (`server/tests/units/test_scene_units.cpp`, 2026-11-16): `anim_mapping` (every kind above, each counted
  refusal, the seq bump, the overflow resync, the load, a player's era change, the record read back by 0374's reader,
  the writer's refusal of an event for a unit never created), `drain_clock`, `recording_port`, `cull_of`,
  `anim_controller` (12 warrior figures from the controller, clip times moved on by a 0.5 s step, drawn; a work event
  without a work table: BadEvent, AnimFailed 1, LOUD once, not drawn), `anim_off`, `anim_unavailable` (D125's route;
  the same database with the switch off passes), `anim_file` (read back by 0374's reader; a folder gone: LOUD once).
  36 of 36 plain and under MSVC ASan (0 reports); `test_units_runner.py` 7 passed.

## 7. Where the draws go in the server's frame

- **Scene pass**: in `Scene::draw` after the water block (`m_water.draw`, the ice through `IceHook`) and the ice
  fallback (`if(m_iceWanted && !m_iceDrawnFrame) IceHook(...)`), **before** `m_fow.afterWorld(ctx, rtv, dsv, w, h)`:
  stock draws units after its water (`unit-draws.md` 1: scene-pass draws 455-503 after the water's) and models before
  the cloud planes (`fog-of-war.md` 5.4), which the joined frame of today cannot do for the EXE's units. One
  `PassDesc{Scene, rtv, dsv, viewport (w, h), makeCb1(...)}` into the sink's colour and depth, depth LESS_EQUAL with
  write (the module's states, `unit-renderer.md` 10.4).
- **Depth with the EXE's models**: the sink's depth goes to the slot's R32F and the composite writes it into the EXE's
  D24S8 with depth write on before the EXE's later lists (`renderer-in-server-design.md` 4.3, `depth_write=1` in the
  shipped `shell_composite.ini`); the EXE's cities, landmarks and path models then pass or fail the depth test against
  the server's units, within the recorded 1-5 D24 steps. Named difference: an EXE model behind a unit's alpha edge
  fails the test and the edge shows the terrain blended, where stock blends over the model.
- **cb1 from the frame's camera**: r3 / r11 the eye (`f.cam.camWorld`), r4-r7 view-projection (`view[0]` x `proj`, in
  `unit-draws.md` 3's row form), r8.x the water level, r9 = the terrain's FogParams with w the ramp row, r10 = FogColor,
  both from `c5r::terrain::distanceFogBlock` at the same camera (premise R4: L01's r9.x -0.05 and r10 = 146/182/243 per
  255 agree with the terrain's block [C], not tested as equal).
- **Wrap copies**: the terrain draws each view's copy (`ShadowCopiesFromFrame`, `OverlayCopiesFromFrame`); a unit near
  the seam may be seen twice. Whether the EXE draws units once per view is premise R3; until then the scene pass runs
  once per wrap copy the camera shows, with Translation(-k W) as the overlay's (`overlay-system.md` 4) [I].
- **Visibility**: only listed units (3.2) are drawn.
- **As built (4454, 2026-11-16) [C]:** `Scene::draw` calls `m_units->drawScene(ctx, rtv, dsv, w, h, camera, &m_grid)`
  after the water block and the ice fallback and before `m_fow.afterWorld`. Per wrap copy with listed units: `prepare`,
  then one `PassDesc{Scene, rtv, dsv, viewport (0, 0, w, h, 0, 1), makeCb1(...)}`; cb1: r3 / r11 = `camWorld` row 3
  (the EXE's own position; the inverse of `view[0]` when `camWorld` is all 0), r4-r7 = rows of Translation(k W, 0, 0) x
  `view[0]` x `proj`, r8.x = `HeightGrid::waterZ`, r9 / r10 = `c5r::terrain::distanceFogBlock(DistanceFog(), eye)`,
  r0-r2 = `camWorld` rows 0-2 (not read). Tested on WARP with the 4352 fixture's warrior (`precheck_pass_draw`: drawn,
  culled when the camera turns away, drawn again after a device release).
- **R2 answered 2026-11-16 by 4455 [C, 3907's 24 exports with units, read in place; scripts and table in
  `F:/c5-64bit-work/0375/4454/r2/` (`cb1dump.py`, `r2check.py`, `cb1.json`)]:** the reflection pass's cb1 r4-r7 are
  **byte-equal to `ShadowMatrix1` (0xDA1890 + 0x40v)** as the same frame's WaterShader draw holds it in WaterFrame
  0x160, in 24 of 24 exports; the mask pass's r4-r7 are byte-equal to `ShadowMatrix0` (0xDA1790, WaterFrame 0x1A0) in 24
  of 24 (this also answers R1's matrix half: the mask's view-projection is the tapped ShadowMatrix0; the resample is
  still open); r9 and r10 are equal in all three passes of every export. Both matrices are orthographic (w column
  (0, 0, 0, 1)) with the same x / y extents (2 / 0.0023625 x 2 / 0.0033303 world units at camera height 473.7,
  proportional to the height: 0.00174945 at 639.7) and depth scale 1.66667e-5; their directions differ: ShadowMatrix0
  looks along (0.4330, -0.25, 0.8660) (the light), ShadowMatrix1 along (0.1228, 0.6964, 0.7071) at the catalogue's one
  camera pitch. *Corrected 2026-11-16 (4464, after 0382 on main, `water-system.md` 13.6 [C, checked against stock to
  1e-6]): the reflection camera **is** a rule of view 0's camera: 0xDA1890 is LookAtRH(c - 5000 D, c, (0,1,0)) x the
  shadow's ortho over view 0's ground footprint, D a constant direction, and the server computes it
  (`c5r::water::reflectionCamera`); no tap is needed, and the server's water binds it at WaterFrame 0x160.* (Replaced:
  "So the reflection camera is not a rule of the frame's camera ... it is the EXE's ShadowMatrix1, which no tap copies
  today; decision 7 (c) needs the shadow tap to carry ShadowMatrix1".)
- **The reflection pass since the merge with 0382 (lead-0375's fa253fef, 2026-11-16 05:39; tests 4464) [C]:** 0382's
  water scene is taken whole: t4 is always a 1024 x 1024 RGBA8 render target with D24S8 (`WaterScene::kReflectionTargetSize`),
  cleared to (0,0,0,0) / depth 1 in every drawn frame, and the water computes the reflection camera itself from view 0
  (`water-system.md` 13.6) and binds its view-projection at WaterFrame 0x160. The units draw into it through the water's
  **models hook** `WaterSceneReflectionModels(&water, fn, user)` (`srv_water_scene.h`): called in the same pass as 0382's
  ice hook and before it (stock's pass draws the model systems, view bits 0x40 / 0x20 / 0x2, before the ice behind 0x1),
  with the same target, the same reflection camera (`c5r::terrain::Camera` view / proj = 0xDA1990 / 0xDA1A90, 1024 x
  1024) and the same scene-camera z; **not** gated by `render_ice_reflection`. `Scene::draw` sets it for the frame while
  `render_units_passes=1` to a function that calls `SceneUnits::drawReflection(ctx, rtv, dsv, 1024, view x proj)` (the
  product as D3DXMatrixMultiply, = 0xDA1890) and clears it after `m_water.draw`. A failure of the hook is counted in
  `reflErrors`, LOUD once per new text (`the reflected models were not drawn: ...`), the target cleared and the ice not
  drawn that frame; a frame whose hooks all succeeded counts in `reflFrames`, w_refl_draws = the models' draws (the
  units report 0: they count their own) + the ice's. **So the ShadowMatrix1 tap of the old item (4) is not needed:**
  4455's `WaterSceneOptions::reflectionTarget`, `WaterScene::setShadowMatrix1` and `Scene::setShadowMatrix1` were dropped
  at the merge, and `ReflectionMatrixMissing` no longer arises on the product path [C: the code at fa253fef].
  Tests (`server/tests/render/water/water_scene_test.cpp`, 4464): `reflection_models` (once per drawn frame, models
  before the ice, after the clear, the ice drawing over the models' target, the same size / camera / z as the ice and
  the reflection camera of 13.6, not called when unset or set for another scene), `reflection_models_ice_off`
  (`render_ice_reflection=0`: models called, ice not, the frame counted), `reflection_models_bad_camera` (no ground
  footprint: not called, t4 empty), `reflection_models_fail` (counted, LOUD once per new text and again after a recovery,
  t4 cleared, ice not drawn, the water drawn). **Fixed by 4464 [C, the tests]:** with the models' hook alone
  (`render_ice_reflection=0`) the merged code returned before counting, so `w_refl_frames` stayed 0 and a repeated failure
  after a recovery was not LOUD again (the LOUD text was never reset).
- *Superseded 2026-11-16 by the merge above (kept for the record):* **The reflection pass as built (4455, 2026-11-16) [C, `server/tests/units/` cases `reflection_pass`,
  `reflection_matrix_missing`, `water_reflection_target`]:** with `render_units_passes=1` the water scene makes t4 a
  1024 x 1024 RGBA8 render target with its own D24S8 (`WaterSceneOptions::reflectionTarget`; off: the IMMUTABLE empty
  target as before, bit-equal), `WaterScene::reflectionTarget()` clears it to (0,0,0,0) and depth 1 per frame. In
  `Scene::draw`, after the terrain and before the water block: `SceneUnits::beginFrame` (the lists and one `prepare`
  per wrap copy, used by both passes), then `drawReflection` into that target with `PassKind::Reflection`, viewport
  1024 x 1024, cb1 as the scene pass's but r4-r7 = ShadowMatrix1 (moved by k W per wrap copy), then
  `WaterScene::setShadowMatrix1` (WaterFrame 0x160) with the same matrix. The matrix comes from
  `Scene::setShadowMatrix1`, which only the tests call: **no production source exists until a tap carries 0xDA1890**, so in
  a real game `render_units_passes=1` draws no reflection and counts `ReflectionMatrixMissing` (LOUD once). The unit
  passes unbind the hull, domain and geometry shaders before the module's draw (the module binds VS / PS only; the
  terrain's tessellation stages are still bound right after the terrain draw).
- **Shadow-mask and reflection passes** (decision 7, switch `render_units_passes`): the EXE's mask pass draws into
  ShadowRaw (back-buffer size, R16G16B16A16_FLOAT), and a resample by the EXE's own program makes ShadowBlur
  (`terrain-surface.md` 11 [C, L]); the server only receives ShadowBlur's copy. Drawing the server's mask "into the
  shadow tap's copy" would put an unblurred, unresampled mask into a resampled map: not stock's. Exact needs the mask
  pass's camera (R1) and the resample, or the EXE's mask itself. The reflection pass needs a render target (today's
  `m_reflTex` is IMMUTABLE: a new 1024 x 1024 RGBA8 target with D24S8, cleared to (0,0,0,0) per frame, in
  `srv_water_scene.*`, additive) and the reflection camera (R2), neither in the knowledge base (`unit-draws.md` 10:
  "the orthographic mask pass's light direction, and the ShadowShader's reduction (not opened here)"). Insertion
  points: the reflection pass before `m_water.draw` (its target is the water's t4); a server mask pass, if (a) is kept,
  before the terrain draw, into the scene's own ShadowRaw-sized target, then the resample, then combined with the
  tapped copy (the combination rule is itself unknown). Recommendation in 14, decision 7 (c). *(2026-11-16, 4464: the
  reflection half of this bullet is settled: the target and the camera came with 0382, and the units draw through the
  water's models hook, "since the merge with 0382" above; the mask half stands.)*

## 8. The switch and readiness (decision 8)

`render_units` = 0 (default) | 1, and `render_units_passes` = 0 | 1, parsed in `ParseHostConfig`
(`srv_render_host.cpp`) beside `render_shadow` as `g_units`, `g_unitPasses` ("last parse wins", refused values through
`bad(...)` as the others), and written into the recorder's render config map (`m["render_units"]`) as `render_shadow`
is. `render_units=1` requires the feed (`server.cfg render=1`). The host sets `SrvFeed::SetUnitsEnabled(true)`
(`srv_feed_server.h`, new) before its loop starts; with it off every `OnUnit*` returns at once, `PushUnit`, the side
tables and the player table do nothing, and the scene is not built: the shipped game carries neither cost nor risk,
and the parity pair (DoD F) covers getters and draws together. **Readiness** for 0040's mode word: `Scene::unitsReady()`
= switch on, pre-check passed, module open, table taken after the current load, the last `prepare` / `draw` returned a
frame-level Ok, and no D125 failure; shown in the scene's second line. ~~The server writes no mode word.~~ *(Corrected
2026-11-17, issue 4753: the server writes the mode word's unit bit from this readiness, 8.1.)* An allocator out
of step does not clear readiness (the units stay drawn, decision 4).

**As built (4454, 2026-11-16) [C]:** `render_units` and `render_units_passes` are parsed in `ParseHostConfig`
(`srv_render_host.cpp`) beside `render_shadow` (`g_units`, `g_unitPasses`; "last parse wins"): values other than 0 / 1
are refused (`render_units=2: 0 or 1`), `render_units_passes=1` without `render_units=1` is refused, and
`render_units=1` in a program that does not link `server/src/units/` is refused by name. Test keys:
`render_test_units_dump=DIR` (must exist; each drawn frame whose copy-0 `UnitScene` differs from the last written one
goes to `DIR/frame_<frame id>.c5units`, at most `render_test_units_dump_max`, default 1000, then LOUD once),
`render_test_units_db=FILE`. Both switches go into the recorder's render config (`render_units`,
`render_units_passes`). `SrvFeed::SetUnitsEnabled(true)` is called in `HostStart` after the unit scene is made; with
`render_units=0` no unit object exists, `Scene::unitFeed` and the draw do nothing. `Scene::unitsReady()` =
`SceneUnits::ready()`: pre-check passed, no D125 failure, the drawing renderer open on the current device, the table
taken, the last frame without `FrameFailed`. The scene's second line `units 1s: ...` goes to render.log every second,
`units totals: ...` once at the end (RNote: also server.log).

### 8.1 0040: the mode word (issue 4753, 2026-11-17) [C: the code and its tests; not run live]

The server's writer of the mode word's unit bit (epic 0040 state ii-d; `epic-0040-breakdown.md` 1.3, 1.5, O5, O6 and 11;
the block's layout and owners: `unit-draw-filter.md` 2.2). Branch `task-4753-mode-word`.

- **Where.** `server/src/units/srv_units_mode.{h,cpp}`: `srvunits::ModeWriter`. The scene (`srv_render_scene.*`) makes it
  with its unit scene (`configureUnits(true)` / `setUnits`) through `srvunits::MakeModeWriter()`, a factory that
  `srv_units_mode.cpp` registers at static initialisation (the pattern of `MakeSceneUnits`). The writer's out-of-line
  members are virtual, so `srv_render_scene.cpp` links into programs without `server/src/units/` (the render host test, the
  offline player, the scene tests): they get no writer, and they cannot have a unit scene either. A unit scene without a
  writer (no factory) is LOUD once (`LOUD: units: mode: this program does not link ...`) and writes nothing.
- **Block.** `VPSHELL_MODE(m_hdr)` of the section the scene maps (`Scene::attach`). The writer stores only the producer
  part, single stores in this order at a change: `writerPid` (when it is not already this process's pid), `word`,
  `changedQpc` (QPC, the heartbeat's clock), `changes` + 1. `readyLossTotal` + 1 at each loss. It keeps bits 1-3 of
  `word` as they are (their writers are 0037 / 0300, in this server) and writes bits 4-31 as 0. It never stores to the
  consumer part (`latchedWord` .. `passedSetTotal`) or `reserved`. A word that a dead server left (another `writerPid`) is
  not touched until this writer's first change, which is always a set (its own bit starts at 0).
- **The rule.** Bit 0 = 1 only while the unit scene is on (`render_units=1`), `Scene::unitsReady()` holds and the
  position source is exe (`server.cfg render_units_positions=exe`). Set only at a drawn frame (`Scene::draw` calls
  `unitsFrameDone()` after the units' scene pass). Cleared at once by a drawn frame or by any `Scene::poll()` (every loop
  iteration) whose readiness is false, and by a `kLoadBegin` in a drained batch (`Scene::unitFeed`, before the unit
  scene's own feed). `render_units=0`: no writer is made and nothing is written; a unit scene removed again turns the
  writer off.
- **Start-up transition (O6, allowed).** From a load's `kLoadBegin` (or, without one, the first drawn frame) to the first
  ready frame of that load the bit is 0 and the EXE draws its units. No bound applies. When the bit is set, the length
  is logged and counted: `units mode: start-up transition <n> ended: unit bit 1 after <ms> ms since the load's
  kLoadBegin (<ms> ms and <n> not-ready frames since the first drawn frame)`. The second number is O6's measure (first
  map frame to readiness). 4757 measures it live on the huge save.
- **A readiness lost in play (1.5, O5).** Only while the bit is 1. The bit is cleared at once, `readyLossTotal` + 1, and
  the line `LOUD: units: mode: the unit readiness was lost while the unit bit was set (between frames | at a drawn
  frame): bit 0, the EXE's units are drawn; the server ends unless it is back within 2000 ms (<n> so far)`.
  - Back within the bound (elapsed <= `render_units_ready_hold_ms`, at a ready drawn frame): the bit is set again, the loss
    is counted as recovered and LOUD (`... is back after <ms> ms (bound <ms> ms): unit bit 1 (<r> of <n> losses
    recovered)`). Device-loss recoveries (0405, 0445) that finish within 2 s go this way.
  - Not back (elapsed > bound, checked in every poll and frame): `Scene::unitsFailed(why)` turns true with `units: the unit
    readiness was lost in play for <ms> ms, beyond render_units_ready_hold_ms=<ms>: the server cannot draw the units
    (D118)`. The host's existing `UnitsFatalStep` (beside `IceFatalStep` in `Loop`, unchanged) takes it once:
    `HostConfig::fatal(5, why)`, D125's route. Nothing is set after that.
  - A load that begins during a loss ends the loss: counted (`loss_ended_by_load`), LOUD, never fatal.
- **The compositor cannot drop (issue 4761, 2026-11-17; D118, MC #4221 (a)) [C: code and hermetic tests; not run live].**
  The writer also reads the consumer word `filterState` (and `latchedWord`; it never writes either) in every `onPoll` and
  at the top of every `onFrame` (`ModeWriter::checkFilter`). While it holds the bit (phase live) and the compositor has
  latched it (`latchedWord & VPMODE_UNITS`), `filterState` state 2 (`VPFILTER_CANNOT_DROP`) is fatal at once, without
  waiting for `render_units_ready_hold_ms` (a filter that cannot drop does not recover): the bit is cleared, `fatal` and
  `filterFatal` + 1, phase failed, and `failed()` gives `units: the compositor's unit filter cannot drop the EXE's pass-0
  unit draws (<reason>; filterState 0x..) while it has latched the unit bit: units drawn twice (D118)`, which the host's
  `UnitsFatalStep` takes once (`HostConfig::fatal(5, why)`, unchanged). 0 (not reported: headless, no or an older
  compositor) and 1 never act, nor does any state while the bit is 0 or not latched. The word's layout and the writer's
  side: `unit-draw-filter.md` 3.3. The mode line ends `filter=0x<filterState> filter_fatal=<n>`. Tests (in
  `test_units_mode`): `filter_no_fatal`, `filter_fatal`, `filter_reasons`, `filter_not_held`, `filter_headless`,
  `scene_filter_fatal`; `producer_part_only` now has `filterState` read and untouched.
- **Keys** (`srv_render_host.cpp`, one small block per place; the MC added the file to 0040's touches, #3942):
  `render_units_ready_hold_ms` = 1..600000, default 2000 (O5: the compositor's hold has no time limit, so 2 s,
  `unit-draw-filter.md` 2.4) -> `SceneOptions::unitsReadyHoldMs`. `render_units_positions` = own (default) | exe ->
  `SceneOptions::unitsPositions`. This is 4755's key: 4753 only parses it, and 4755 builds the exe positions behind it. Until then
  the product keeps own, and the bit stays 0. Other values are refused through ParseHostConfig's `bad(...)`
  (`render_units_positions=EXE: own or exe`, `render_units_ready_hold_ms=0: 1..600000`). Neither key needs
  `render_units=1`. Both go into the recorder's render config map. `Scene::init` passes them to the writer at every
  device; the writer keeps its state across devices.
- **Lines.** The scene's `units 1s: ...` line (render.log) ends with `| mode bit=<0|1> phase=<starting|loading|live|lost|failed>
  src=<own|exe> hold_ms=.. losses=.. recovered=.. loss_ended_by_load=.. last_loss_ms=.. max_loss_ms=.. fatal=.. loads=..
  transitions=.. last_transition_ms=.. transition_frame_ms=.. transition_frames=.. not_exe_frames=.. changes=..`. The
  totals get a line of their own, `units mode totals: | mode ...` (RNote, also server.log). It is a separate line because
  the units' totals line is already near the log's 1,536-byte line body (`srv_render_common.cpp` `Emit`).
- **Tests.** `server/tests/units/test_units_mode.cpp`, its own exe, built by `server/evidence/0040/3/build_mode_test.py`
  (it links the objects of `build_units_tests.py`, which is unchanged). The cases: layout, bit_rule, producer_part_only,
  stale_writer, load, loss_recovers, loss_fatal, load_during_loss, options, line, scene_defaults, scene_units_off,
  scene_fatal (a fake `HostConfig::fatal`: one call, code 5, `units: ...`), scene_line, host_keys, no_writer_factory.
  Commands: `server/evidence/0040/3/run_4753.sh plain|asan|others` (gated through `quietlock.py run`). Results
  2026-11-17 07:36-07:51 EST on the workstation, commit 0d1b399c4 (`server/evidence/0040/3/results.txt`) [C]:
  test_units_mode 16 of 16, plain and under MSVC `/fsanitize=address` (the ASan binary imports
  `clang_rt.asan_dynamic-x86_64.dll`, no report). The existing suites still pass: test_scene_units 42 of 42 (plain and
  ASan); the render host suite (`server/tests/render/test_render_host.py`) 47 passed, and its `render_host_test.exe`
  links without `server/src/units/`, which is the point of the factory; the feed suite (`run_feed_test.py`) 298 checks,
  0 failed, and its negative build fails as it should.
  server.exe's own build was not run (no LTO link, the lead's rule). `build_server.render_sources()` globs
  `server/src/units/*.cpp`, so `srv_units_mode.cpp` is in it [C, `server/build_server.py` 104-116].
- **Not verified:** no live run; the writer has not run in server.exe with a game. The product default `own` never
  sets the bit, so a live check needs 4755, or a test run with `render_units_positions=exe` once 4755 lands.

## 9. Residency (decision 10)

Models, textures and clips are loaded at a unit type's first `prepare` in the session (the module's `ModelCache` and
`ModulePose` clip cache on the drawing renderer) and kept; `ModelCache::upload(dev)` makes their IMMUTABLE buffers.
The pre-check's renderer is CPU-only and released after it (3.3). No budget or eviction unless the huge run (DoD F 3)
calls for one (as built 2026-11-16: no eviction; the pre-check's renderer is destroyed with its thread's
environment when the check ends). Known sizes: 6.58 MB of decoded curves for the catalogue's 469 bindings (`unit-renderer.md` 11.8);
models and textures per type unmeasured. The per-frame CPU cost of `prepare` on a late huge game (the view cull of
3.2 bounds it) is the epic's main cost risk and is measured in DoD F 3. **Measured 2026-11-16 by 4461 (3.4, oldpc,
grade A by 2.6): +15.2 ms of server CPU and +0.35 ms of GPU per drawn frame on the huge save at turn 350 (739 listed,
704 culled, 35 drawn), which holds the server at ~61 frames per second against ~271 without units [L]. The cause is
outside residency as defined here: `ArtResolver::resolve` parses each unit's `.fxsxml` from the files again in every
frame (3.4, the sampler item) [L, C].**
*Rerun 2026-11-16 by 4466 (3.4, set 1, oldpc, grade A by 2.6), after the art cache (4464) and the pose memo (4466), with
the controller as the product runs it: +32.3 ms of server CPU and +0.51 ms of GPU per drawn frame, the server at ~32
frames per second against ~271; the cause again outside residency: the controller's per-frame work over every held
unit (3.4, profile item). Server private at the end +120 MB [+110, +131] against units off (4461: +50.5 MB without the
controller); video_local not resolved (the bake). Still no budget or eviction needed for memory [I].*
*2026-11-16, issue 4468: the controller's per-frame work is limited to the drawn set (3.4, 6.1); its clip durations are
read on the pre-check's thread for every reached art row (5402 clips of 231 rows on VP, 6.6-7.3 s on the workstation)
and kept for the session in the controller's graph factory (a duration per clip, no curves: small) [C, L].*

## 10. Failure statuses (decision 4, DoD G)

Logged with `RNote("LOUD: units: ...")` on the render side (`srv_render_common.cpp`: render.log and the host's log) or
`SrvLog("feed: LOUD: ...")` on the DLL side (`NoteException`'s pattern: first 5 per reason, then counted only),
counted in the scene's and the feed's counters, and reported in `Scene::totalsLine` (render.log at the end) and in
`server-report.txt` through `SrvRemote::Report` / `SrvRemoteFeed::WriteTotals` (which needs a caller, Q2). Never a
placeholder, default colour, seed of our choosing or silent gap.

| Status | When | Effect | Log, counter |
|---|---|---|---|
| `PrecheckFailed` | a pre-check entry not `Ok`, the module's `open` failing, or the database copy unreadable, at game start | D125's route: `UnitsFatalStep` beside `ShadowFatalStep` in `Loop` -> `HostConfig::fatal` (`SrvRemote::ReportAndExit`, exit 5) -> the proxy's server-death report, `vp64helper failed`, popup, end of game, config.ini restored by the helper (`proxy-design.md` 22.8); shown once live on the shipped pack layout | `LOUD: units: pre-check: <n> of <m> art rows failed: <first status>` |
| `UnitRefused` | `prepare` gives a unit a status other than Ok in play | that unit not drawn this frame; others drawn | LOUD once per (unit type, code), `unitsRefused` |
| `ColourUnknown` | a player's colour index has no `PlayerColors` row | that player's units not drawn (the module's `BadInput`) | LOUD once per player, `colourUnknown` |
| `FeedOutOfOrder` | a unit event's `seq` not above the last one taken, a path or combat index outside the batch's side table; a table record newer than the batch (since 4463 a real contract break only, 2.2) | the event is skipped | LOUD, `feedOutOfOrder` |
| `UnknownRemoval` | kind 14 for a key not in the table outside a load (at a load it is the expected no-op, S5) | none for the allocator (the EXE ignores it too); the table is unchanged | LOUD with key and seq, `unknownRemoval` (it shows a missed creation) |
| `KnownCreation` | kind 11 for a key already in the table outside an era change | mirrored as the EXE does it: a second record is allocated, the old index stays held (the EXE's creation handler does not look the key up, 12.7.3) | LOUD, `knownCreation`. Its one live case (4461's huge run) came from the EXE's own `ICvUnit1::SetupGraphical` at an era change [I], 3.4 |
| `CombatHoldAmbiguous` | a creation arrives within the uncertainty band of a combat-held removal's modelled end (decision 14) | the allocator applies the model's order | LOUD, `combatHoldAmbiguous` |
| `AllocatorOutOfStep` | a dropped unit event (2.5), a creation or removal the rules above cannot place, or (test runs) the reader of decision 3 disagreeing | units stay drawn with the allocator's seeds; the allocator keeps going (no reset: S5's clear carries state across loads) | LOUD once per episode, `outOfStep` episodes (an R6 count) |
| `UnitEventException` | an exception in an `OnUnit*` | the event lost (the game is not touched) | `NoteException`, `s_nExceptions` |

Each has a negative test (DoD H).

**As built (4454, 2026-11-16) [C, `server/tests/units/test_scene_units.cpp`]:** every status is a value of
`srvunits::Status` (`srv_units_core.h`), counted per occurrence (unit-frames for the per-unit ones) in
`CoreCounters::byStatus` and in the `units totals` line, and logged as `LOUD: units: <Status>: <text> (<n> so far)`
(once per key where the table says once: per player, per unit type, per (unit type, code), per unit; else the first 20
then every 100th). The allocator's four (`UnknownRemoval`, `KnownCreation`, `CombatHoldAmbiguous`,
`AllocatorOutOfStep`) arrive through `SeedAllocator`'s LogFn (`statusOfSeed`). `PrecheckFailed`'s line reads
`LOUD: units: PrecheckFailed: pre-check: <n> of <m> art rows failed: <type> (<row>, style '<s>', era '<e>'): <status>`;
`UnitsFatalStep` (beside `IceFatalStep` in `Loop`) then calls `HostConfig::fatal(5, "units: pre-check: ...")` once.
Statuses added by 4454 (inputs the scene cannot map, each: the unit not drawn, LOUD, counted):

| Status | When | Log |
|---|---|---|
| `PlayerUnknown` | a listed unit's owner has no player record | once per player |
| `UnitTypeUnknown` | `UnitRecord.unitType` has no `Units` row | once per type |
| `PlayerArtUnknown` | the player's civilization / art style / era index has no row; also an era change whose art rows cannot be compared (the allocator is told "unchanged") | once per player |
| `SeedMissing` | a listed unit the allocator holds no seed for | once per unit |
| `PlotUnknown` | the plot outside the feed's grid, or `plotCentre` refuses it | once per unit |
| `FrameFailed` | `prepare` / `draw` returned a frame-level error, or the frame's camera is not finite / its view not invertible | every time (first 20, then every 100th); readiness cleared |
| `ReflectionMatrixMissing` (4455) | `render_units_passes=1` and the frame has no ShadowMatrix1 (no tap carries it yet, 7) | LOUD once, counted per frame; no reflection drawn, the water's t4 stays empty |
| `AnimFailed` (4456) | the controller gives a unit a status other than Ok (`unit-controller.md` 5; e.g. `BadEvent` for work without a work table) | counted once per episode of the unit, LOUD once per (unit type, code); no program, so the module refuses its figures: not drawn (6.1) |
| `AnimUnavailable` (4456) | the controller cannot open on the database (string databases, bridge tables, Builds) or refuses a frame | D125's route as `PrecheckFailed` (the scene failed), LOUD; never figures of our choosing (6.1) |

**Live, 2026-11-16 (4460) [L]:** in two product-path runs with `render_units=1` (3.4) no status of this section was
reached (0 LOUD lines; `refused=0 colour_unknown=0 out_of_step=0` in every `units 1s` line). Added by 4460:
`FeedOutOfOrder` also for a kind 25 / 28 event whose combat index is outside the batch's combats or names another seq
(test `combat_out_of_batch`); the pre-check's `ClipMissingInInstall` is a warning, not a status of this table
(`unit-renderer.md` 10.6). **`render_units=0` [L]** (run 5, the same layout and save, `server_cfg_extra` naming only
`render_units=0`, 2 AI turns): `server-report.txt` `feed units: off; unit events 0 ... player rows ... reads 0 ... combat
game reads 0 ... database copies 0`, per kind all 0, `unit calls by thread: ui 0, core 0, aux 0, other 0; inside Update
0`; render.log has no `units` line (no unit scene, no unit draw); 0 LOUD lines.

**DoD G: `PrecheckFailed` by D125's route on the shipped pack layout [L]** (2026-11-16 02:51-02:53 EST, oldpc testbed
job `20261116-022516-711a`, instance tb2 (the machine's own EXE), `server/evidence/0375/4460/neg_units_job.py --fault
units --units-db bad` over `tools/testbed/jobs/release_d125.py` unchanged (0169 4313's pattern), submitted by
`server/evidence/0375/4460/submit_d125.sh`; the product build of 3.4 run 4 (server.exe 17BDF4D1..., proxy 373DBBD2...,
vp64helper 896968A0..., composite 5F38A59D..., pinned x64 DLL 29F58E17...); the pack `4460-d125` by `tools/release/pack.py`
(x86 = Release-5.4.8's DLL 262aee88..., purity 7b1a91fb...), pack=1, fallback_dll = the tag's DLL, config.ini in the A''
form by `vp64helper on`; save `TB-TINY_0008` (b2ceb663..., a copy); the fault: `vpproxy.ini server_cfg_extra=` (TEST ONLY)
with `render_units=1` and `render_test_units_db=` a file that is not a SQLite database]: server.log +3.053 s `LOUD: units:
PrecheckFailed: pre-check: the unit database ...: table Units (ID, Type): prepare: file is not a database ...`, +3.062 s
`units: the 64-bit path cannot go on: ... the server ends (exit code 5)`, +3.088 s `remote: exit code 5 (units:
pre-check: ...)`; the EXE ended by itself 56.1 s after the launch, no "Game Crash" box, one report folder with
`report.txt` (`server pid ...: exit code 5`, the last FAILED line naming `units: pre-check: ...`) and no `.dmp`;
`vp64helper failed` restored config.ini (SHA-256 = the player's), wrote `failed=1`, no side file, the popup on screen
(Visual Checks `vc-20261116-025206-oldpc-3858`, DIAGNOSTIC: the fault is a test key; pulled); 15 of 15 checks; the
instance restored by hash, provision verify rc 0, the save copy removed. Results `F:/c5-64bit-work/0375/4460/d125-bad`.
The failure comes before the map (the pre-check starts when the database is ready, before the first wanted frame), as
the ice tap's did (`terrain-surface.md`).

`FeedOutOfOrder` also covers a table record newer than the drained batch. *(2026-11-16, issue 4458 [L]: reached live, 3 times in 30 AI turns of the T0018 copy (`CopyUnits ahead of Drain`: the DLL's game-core thread pushes between the render thread's `Drain` and its `CopyUnits`), so 3.1's "exact" does not hold under load; no seed effect was seen; `unit-formations.md` 12.7.8.)* *(2026-11-16, issue 4463 [C]: fixed, `Drain` takes the copy `CopyUnits` returns, 2.2; the status stays for a real break, tested by `feed_out_of_order` part (3); not yet re-run live.)* Not a status (4463): a unit hidden by the fog filter (3.2) is counted, not LOUD (the EXE does not draw it either); a frame without the filter is said once per reason. `CombatHoldAmbiguous` is reached only by
4453's hold model; the scene's mapping and logging of it are tested. The live D125 run of `PrecheckFailed` on the
shipped pack layout (DoD G) ~~is not done~~ was done 2026-11-16 by 4460 (above).

## 11. The file form (decision 11)

Records in `feed.bin` (`offline-scene-harness.md` 2.5: `u32 type, u32 bytes, payload`, packed, no padding) after 0364's
version raise and the MC's grant, with `kFormatVersion` raised once more. The unit **events** need no new record: kinds
11-31 are in DRAIN records as they are.

| Type | Payload |
|---|---|
| 3 UNITS | `loopIter` u64, `caller` u32 (1 = `CopyUnits` at start / reset, 2 = after a batch with unit events), `tableGen` u32, `full` u32; full 1: `count` u32, `UnitRecord[count]` (32 B, sorted by owner, id); full 0: `nUpsert` u32, `UnitRecord[nUpsert]`, `nRemove` u32, `(i32 owner, i32 id)[nRemove]` against the previous UNITS record |
| 4 PATHS | `loopIter` u64, `nPaths` u32, per path `seq` u32, `length` u32, `i32 plot[length]` |
| 5 COMBATS | `loopIter` u64, `nCombats` u32, per combat 64 B (= `SrvFeed::CombatRecord`, 4452, 2026-11-16 [C]): `seq` u32, `kind` u16, `flags` u16 (bit 0 attacker advances, 1 defender retaliates, 2 ranged, 3 bombing, 4 air sweep, 5 defender captured, 6 visualize, 7 returned value known, 8 quick-combat option, 9 active player's turn active, 10 bits 8-9 read), `plot` i32, attacker and defender each `owner` i32, `unit` i32, `city` i32 (-1 = none), `nuclearLevel` i32, `firstMember` u32 (in the batch; the file form may write 0), `reserved` u32 0 (4726: with flags bit 11 the per-combat damage, two int16; bit 12: the members end with the interceptor's, member flag 0x8000; `unit-combat-server.md` 12.2), `finalDamage` i32 x 2, `returned` u32, `nMembers` u32 (corrected 2026-11-16 by 4452: was `damageInflicted` i32 x 2 at offsets 40-47 and `reserved` at 56; `GetDamageInflicted` is not pure, 2.3, and the returned value goes into the record, Q7); then `nMembers` x 28 B: `player` i32, `id` i32, `flags` u16, `damage` i16, `finalDamage` i16, `maxHP` i16, `x` i16, `y` i16, `combatType` i32, `era` i32 |
| 6 PLAYERS | (corrected 2026-11-16 by 4457, as built in 11.1: `loopIter` u64, `caller` u32, `count` u32, `SrvFeed::PlayerRecord[count]` whole, 32 B; was: `activeTeam` i32 and 16 B per player: `player` i8, `team` i8, `alive` u8, `flags` u8, `colour` i16, `era` i16, `civilization` i32, `artStyle` i32, written whole at start and when any row changes) |

The checker's rules of 2.5 apply (`loopIter` never goes back, sizes agree with counts; a diff before a full table, an
unknown caller, a removal of a key not held: `BadRecord`). The recording also needs `FeedSource` (`srv_scene_sources.h`,
0364's) to carry `copyUnits` and the players (Q3). **Until the grant** (4457): encoders and decoders in
`server/src/units/srv_units_records.*` with round-trip and refusal tests in `server/tests/units/`.
**`server/tools/unitfeed/`**: input a bundle (`feed.bin`, `scene.json`, the C5HF height field, the install and the
database copy named on the command line); output `.c5units` per drawn frame (`frame_<id>.c5units`, the scene built by
the same `srv_units` code as the server's, D137 rule A) and the controller's timeline (`.c5anim`, 0374's form) for the
span; exit codes: 0, 1 refusal of a record (named), 2 a scene the `.c5units` writer refuses.

### 11.1 As built (4457, 2026-11-16 01:14-01:50 EST, branch `task-4457-records`) [C: the code and its tests]

The MC granted decision 11 on queue #2174 (2026-11-16): 0364's bundle files with a raise to **bundle format version 4**.
What was built differs from the table above in these points (the table is the C1 proposal; this section is current):

- **Codec** `server/src/units/srv_units_records.{h,cpp}` (`srvunits::rec`): `Encoder` (addUnits, addPaths, addCombats,
  addPlayers) and a strict `Decoder`. The codec is **defined inline in the header** (`srv_units_records.cpp` holds only
  layout `static_assert`s): the bundle library `srv_scene_bundle.cpp` decodes the records, and every program that
  compiles `server/src/render/*.cpp` without `server/src/units/` (render host, fog, overlay, shadow, water, decal, ice
  tests, sceneshot: 10 build scripts not 0375's) must link with no new source. Every encoder call decodes its own record
  once (never writes what the decoder refuses).
- **Layouts** (all little-endian, packed, framed `u32 type, u32 bytes, payload`):
  - 3 UNITS: `loopIter` u64, `caller` u32 (1 the scene's first copy, 2 a later copy), `tableGen` u32, `full` u32; full 1:
    `count` u32, `UnitRecord[count]` (32 B, strictly increasing `(owner, id)`, owners 0..63); full 0: `nUpsert` u32,
    `UnitRecord[nUpsert]`, `nRemove` u32, `{i32 owner, i32 id}[nRemove]` (each strictly increasing) against the previous
    UNITS record of the **same `tableGen`**. Full is written the first time, at caller 1, at a new `tableGen`, and when a
    diff would not be smaller (as PLOTS). As proposed.
  - 4 PATHS: `loopIter` u64, `nPaths` u32, per path `seq` u32, `length` u32 (<= 65,536), `i32 plot[length]`; seqs
    strictly increasing; `PathEntry::first` is rebuilt as the running sum. As proposed.
  - 5 COMBATS: `loopIter` u64, `nCombats` u32, per combat the 64 B `SrvFeed::CombatRecord` **followed by its own
    `nMembers` x 28 B `CombatMember`** (refinement: the proposal put all members after all combats); `firstMember` is
    written as the running member count (the decoder checks it), `reserved` 0, `kind` 25 or 28, `flags` bits 0-10 only,
    `nMembers` <= 256, seqs strictly increasing.
  - 6 PLAYERS: `loopIter` u64, `caller` u32 (1 the scene's `CopyPlayers`, rows sorted; 2 the rows a drained batch
    carried, `Batch::players`), `count` u32 (<= 64), **`SrvFeed::PlayerRecord[count]` whole (32 B)** (corrected: the
    proposal's 16 B row dropped `state` (IsHuman, IsTurnActive: decision 14's input), `activeTeam` per row and
    `changeGen`; the lead's brief asked for the records whole, and the replay must give the scene the same rows).
  - **DRAIN (1) gains two fields** (feed.bin file form 2): after `dropped`, `unitTableGen` u32 (the `Batch` field the
    scene compares; it had no place in the form) and `drainMs` f64 (the recorder's `QpcToMs(Qpc())` when the drain
    returned; -1 unknown; not finite: `BadRecord`). Payload 52 + 32 n + 4 + 4 m bytes (was 40 + ...).
- **Placement in feed.bin** (refinement): records 4, 5 and 6-of-caller-2 follow their DRAIN at once (same `loopIter`, at
  most one of each, nothing between) and `ParseFeed` folds them into that DRAIN's `FeedRecord::batch`; records 3 and
  6-of-caller-1 (the scene's copies, taken inside `UnitsCore::onBatch`) follow the newest DRAIN of the same `loopIter`
  (the residency's and the fog's PLOTS reads may stand between: Loop calls `plotsChanged` and `fowFeed` before
  `unitFeed`) and are folded into its `unitCopies` / `playerCopies`. So `ParseFeed` still yields only DRAIN and PLOTS
  records and a reader without units (sceneshot) replays a version-4 feed unchanged. The file feed
  (`MakeFileFeed`) serves `copyUnits` / `copyPlayers` from the copies of the DRAIN its last `drain()` yielded and fails
  (LOUD, `out of order: copyUnits called, no recorded copy left after the DRAIN of loop iteration N`) when none is left;
  `FileFeedControl::unitCopiesLeft()` names copies the replay did not take.
- **Refusals** (each a negative test asserting exit code and output line, `bundle_format_test.cpp TestUnitRecords`):
  feed.bin of form 1 `BadVersion` ("file form version 1, this reader reads 2"); format_version 3 `OlderVersion`
  ("format_version 3 is older than 4"); a codec refusal maps Truncated / WrongSize to `WrongSize` and the rest to
  `BadRecord`, the why naming the codec's code (`UnitsDiffBeforeFull`, `LoopBack`, `BadRecord`, `OlderFormat`,
  `UnknownType`); PATHS not right after its DRAIN, of another loopIter, before any DRAIN, twice for one DRAIN, after the
  copies; a copy with no DRAIN of its loopIter; drainMs NaN; type 7.
- **`FeedSource`** (`srv_scene_sources.h`, Q3) gains `copyUnits(out, gen)` and `copyPlayers(out)` (defaults = the live
  `SrvFeed` calls, so sources written earlier keep today's behaviour); the recording feed hands them to
  `FeedRecordSink::onUnits` / `onPlayers` (defaults no-op); `SceneRecorder` writes them (`FeedEncoder::addUnits`,
  `addPlayers`), and `addDrain(..., drainMs)` writes the batch's PATHS, COMBATS, PLAYERS(2) after its DRAIN (a batch the
  codec refuses: the drain is not recorded, `error("feed: ...")`, the bundle incomplete). `srvunits::FeedSourcePort`
  (`server/src/units/srv_units_feedport.h`, header only) is the scene's `FeedPort` over a `FeedSource`.
- **Wired 2026-11-16 by 4456 [C, tests `recording_port`, `drain_clock`, recorder_test's lastDrainMs checks]** (the
  paragraph below is the state before): `SceneUnits::useFeedSource(FeedSource*)` routes the scene's `copyUnits` /
  `copyPlayers` through a source (the host's `StartRecorder` gives it `h.recFeed`; the unit database is still asked of
  the scene's own port); `SceneUnits::feed(batch, drainMs)` takes the drain clock, which the host reads from
  `SceneRecorder::lastDrainMs()` (MC grant to 0375 of `srv_scene_recorder.*`: the exact value `addDrain` wrote into
  feed.bin, -1 for a drain not recorded, then the scene reads its own clock); the frame clock was already the recorded
  one for the span's frames (`Scene::draw` gets `recFrame.nowMs`, `UnitsCameraOf` passes it). `CullOf(UnitsCamera)` in
  `srv_units_core` is the cull camera both `UnitsHost::beginFrame` and unitfeed use (the repeated lines are gone).
  unitfeed also runs the controller (6.1): figure lines in its files, `timeline.c5anim` (0374's form) beside them,
  `--anim-from FRAMEID` (start where the server started: its render.log line "units: the controller starts at frame
  N"), `--no-anim`; exit 5 a controller refusal. `replay.py --unitanim EXE` runs `unitanim play` on that timeline (step
  4, exit 30 + its code; the driver's 1, units failed, passes and is reported): the recorded game through 0374's driver
  with one command and no client (DoD I). `compare_anim.py figures DUMP PROGRAMS_CSV` (figure lines against the
  driver's programs) and `timelines A B [--skip-first-of-b]`; `compare_c5units.py --no-figures`. Tests: unitfeed pytest
  12 passed (2026-11-16, `test_controller_timeline`, `test_compare_anim`, the replay test with the driver: every figure
  line of the 3 frames equal to the driver's single-sample program).
- **Not wired yet (named for the lead / W-4460; done by 4456, above):** the host still gives the scene `LiveFeedPort()`
  (`srv_units_scene.cpp` `MakeSceneUnits`), so a live recording holds **no** UNITS / PLAYERS(1) records until
  `MakeSceneUnitsWith(o, FeedSourcePort(*h.feed), ...)` is used with `render_test_record` (unitfeed refuses such a bundle
  by name: "no UNITS record"). Two clocks are not the recorded ones: `UnitsHost::feed` passes its own `now()` to
  `onBatch`, not the drain's `drainMs`, and `beginFrame` uses `now()` when `SceneFrame::nowMs` is -1 (always, live), not
  frames.bin's `nowMs`. Mid-fade values (`fade`) and the hold's timing therefore differ from a replay by the few
  microseconds between the reads [I]; everything else is exact. The fix is one value passed in each place
  (`srv_units_scene.cpp`, `srv_render_scene.cpp`; not 4457's files).
- **`server/tools/unitfeed/`** (built by `build_unitfeed.py`): `unitfeed.exe --bundle DIR --db FILE --install DIR --out
  DIR [--exe] [--mod DIR]... [--mods-auto] [--first ID] [--last ID]` links `UnitsCore` and the seed allocator (no
  re-implementation; D137 rule A) and replays the bundle through `MakeFileFeed` + `FeedSourcePort`: `onBatch(batch,
  drainMs)` per DRAIN, `build(cull, grid, frames.bin nowMs)` per frame (frame k after every DRAIN of loopIter <= its
  own), writing `frame_<id>.c5units` of wrap copy 0 as the server's dump does. The cull camera is repeated from
  `UnitsHost::beginFrame` (8 lines: that file exposes no function; gap named). Exit codes 0 OK, 1 the bundle or a record
  refused (`scenebundle: REFUSED ...` or `unitfeed: REFUSED ...`), 2 the `.c5units` writer refused, 3 usage or an input
  outside the bundle, 4 out of step (a copy the recording did not take or the replay did not make).
  `compare_c5units.py A B` compares two frame sets field by field (DoD B's server dump against the replay);
  `replay.py` is the one command (unitfeed, then 0041's `unitscene scene --write` round trip and `unitscene prepare`).
  `render/tools/unitscene/oracle.py` is the catalogue's own oracle (it builds its scenes from 3907's index), so its two
  generic steps (`scene`, `prepare`) are what replay.py runs on the replay's scenes. The controller's timeline (`.c5anim`)
  is 0374's form, not merged: **issue 4456 carries it**.
- **Tests (2026-11-16):** `server/tests/units/run_units_records_test.py` 128 checks plain and under MSVC ASan (0
  reports, canary caught), 9 `--decode` negatives with exit codes 11-17, the inject negative.
  `server/tools/unitfeed/test_unitfeed.py` (pytest, `PYTEST_DISABLE_PLUGIN_AUTOLOAD=1`, `python -I`): 10 passed on the
  synthetic bundles of `server/tests/units/unitfeed_fixture.cpp` (database `F:/c5-64bit-work/0327/db/
  Civ5DebugDatabase.db`, install c5h-b's): the fades 0.4 and 0.8 from the recorded clocks (blend 0.5 s, drainMs 1000,
  frames at 1200 and 1400), the move, the damage, the removal, seeds 0 and 1, byte-equal output on a second run; the
  refusals nounits (1), noclock (1), extracopy (4), missingcopy (4), toomany (2, `TooLarge`), format_version 3 (1),
  a changed feed byte (1, `ShaMismatch`), usage and inputs (3); compare's equal / different / missing / malformed; the
  one-command replay with `unitscene` of render build `a5c1805383`: 3 of 3 scenes read back byte-equal, `prepare` 5 of 5
  units `Ok`. Scene bundle suite: see offline-scene-harness.md 20.
- **Format version 5 (issue 0448, 2026-11-16) [C: the code and its tests]:** 0440's bundle check (format 5) refuses a
  bundle with `render_units` "1" that has neither a `units_db` member nor a note starting `gap: units: no unit database`,
  and likewise for `units_history` / `gap: units: no controller state` (`MissingMember`). The fixture's format-4 bundles
  had none, so 13 of 13 tests failed on main from 0440's merge until 0448 (0440 did not run this suite). Since 0448
  `unitfeed_fixture.exe VARIANT DIR DB` copies the database `DB` whole into `units/gen0000000001.db` and writes the
  recorder's note `gap: units: no controller state: none recorded` (the controller starts at the span's first frame,
  unitfeed's default `--anim-from`, so no history exists). unitfeed itself still takes the database from `--db` and
  ignores both members. The older-version refusal test now derives the version from the bundle. 13 passed
  (`server/evidence/0448/acceptance.txt`). Any later format bump that adds a `render_units` member needs this fixture
  changed too: run `test_unitfeed.py` with every scene bundle format change.
- **DoD B's live comparison** (the server's `render_test_units_dump` against unitfeed on the same recorded frames)
  *(done 2026-11-16 by 4456: 11.2; the text below is the state before)* is not done: it needs the integrated server.exe (W-4460) and the port wiring above; then `compare_c5units.py DUMP_DIR
  REPLAY_DIR --only-common` (the server dumps only drawn frames whose scene changed).
- **`--mods-auto` takes the mods of the Documents folder in force, not the bundle's instance** [L, 2026-11-20 09:52 EST,
  ticket 4982, extras-units.md 4982.2]: on `C5H-0301-HUGE_0350_20261119-214920` (recorded on c5h-e), with no
  `C5_DOCUMENTS` set, `--install <c5h-e>/install --mods-auto` left VP's unit models out (`statuses: AnimFailed 151; 11
  LOUD lines`, `.fxsxml not found` for VP's destroyer, mamluk, dreadnought, armored car, Boer commando) although the
  files are in c5h-e's MODS folder. **Fix:** name the bundle instance's three mod folders, `--mod "<MODS>/(1) Community
  Patch" --mod "<MODS>/(2) Vox Populi" --mod "<MODS>/(3a) VP - EUI Compatibility Files"` (MODS = that instance's
  `Documents/My Games/Sid Meier's Civilization 5/MODS`, the bundle's `render_decal_mods`): `statuses: none; 0 LOUD
  lines`. Or set `C5_DOCUMENTS` to that instance's Documents, as 11.2's run 3 did [I: `--mods-auto` reads
  `srvrender::ModFolders()`, which follows the Documents folder; not read further]. On the 4980 bundles (c5h-c)
  `--mods-auto` gave 0 LOUD lines.

### 11.2 DoD B live: the server's dump against the replay of its recording (4456, 2026-11-16 03:54-04:51 EST) [L]

**Setup.** Pool instance c5h-b (Goldberg, C:, Vibrant Textures present), claimed by 0375; the product path as 4460's
(`server/evidence/0375/4456/live_4456.py`, prep/restore of `live_4460.py`: ServerRenderer = 1, D33 with Overlay on,
vpproxy.ini `server_cfg_extra=` TEST ONLY); pin layout `x64-04b1211b-pfdee656` bin (DLL 29F58E17...) with server.exe of
`task-4456-controller` (run 1 CC9AC859..., runs 2-3 E6BF45CC...), proxy of the same build, composite 7A59FFCE...;
save copy `C5H-0375-4456-TINY_0018` of `C5H-3000-TINY-T0018` (E337244C), turn 18, Spain, 3 AI turns (6.5-8.4 s each);
keys `render_units=1`, `render_test_units_dump` (max 20000), `render_test_units_table`, `render_test_units_anim`,
`render_test_record` (after 1, frames 1800 / 2400). Every run: no dialog at any step (D155 check after each), stopped
by the harness's safe stop, the instance restored by hash (`provision.py verify` rc 0); 0 LOUD lines of the unit scene
(no AnimFailed, no AnimUnavailable); the controller started at frame 831 / 684 / 686 with the table's 35 units; the
pre-check 233 art rows, 0 failed, 2 warnings, 21.7-28.2 s (workstation under load: indication). Numbers:
`server/evidence/0375/4456/summary.json`; data `F:/c5-64bit-work/0375/4456/live1..3`.

**The recorder's span ends at the first redraw.** Runs 1 and 2: the server drew a frame id a second time (a unit batch
between two EXE frames) during the first AI turn, the span ended there ("frame 1759 drawn after frame 1759 ... the span
ends there"), and with the dumps `first,last` / `every:200` only the first dump existed, so the bundles were INCOMPLETE
(refused by the checker; deleted). Run 3 with `render_test_record_dump=every:40`: span 1329 recorded frames (108..12376,
the 3 AI turns inside it), 34 dumps, complete, format version 4, 40 GB (overlay 28 GB: 1329 files, none deduplicated;
shadow 11 GB). The server draws about 3-4 frames per second while the recorder writes (shadow 8 MB and overlay 22 MB per
frame), so its controller frames there are long (dt 0.16-0.22 s).

**Result 1, the scene field by field (run 3).** `unitfeed --bundle live3/bundle --db <the server's units-1.1.db>
--install <c5h-b> --mods-auto (C5_DOCUMENTS = c5h-b's Documents) --anim-from 686`: OK, 1329 frames, 29 drains, 27 table
copies, 501 unit events, 0 statuses, 0 LOUD. `compare_c5units.py DUMP REPLAY --only-common`: 1272 common frames; **1271
frames EQUAL in every field of every unit, figure lines included (9058 unit rows, 77,972 figure lines: seeds, plots,
facings, damage, flags, fades, the controller's clips and times)**; the one difference is frame 12376's figure lines,
the frame the server redrew after the span ended (its file holds a later draw than the bundle's). `compare_anim.py
timelines SERVER.c5anim REPLAY.c5anim --skip-first-of-b`: 1271 common frames EQUAL (dt and every event, 127 events).
So the replay's controller from the recording equals the server's from the same start frame, step for step [L].
**Fades: equal only at rest here** (no unit in view mid-fade in the common frames: the 2 visibility events with a
0.5 s blend fell on culled units, 9 had 0.01 s); the batch clock is the recorded one by construction (recorder_test:
`lastDrainMs()` = every DRAIN's `drainMs`) [C]; a mid-fade row live is not verified.

**Result 2, the server's controller against 0374's driver (runs 1-3).** `unitanim play --timeline <the server's
render_test_units_anim file>` (the instance's install, the server's database copy, the four MODS folders) then
`compare_anim.py figures DUMP PROGRAMS TIMELINE`: run 1 6897 frames, 414,530 figure lines EQUAL; run 2 8950 frames,
538,498 EQUAL; run 3 1271 of 1272 (77,972), the last frame as above (the TEST ONLY timeline is rewritten once a second:
the dump of 04:39:34.876 is a draw after its last rewrite at 04:39:34.283). The driver: 0 failed units, 219 clips read;
the replay's own timeline through the driver equals the replay's figure lines (1272 frames, 78,032). Redraws: 22 (run
1), 97 (run 2), 2 (run 3) controller frames with a frame id already drawn: the timeline keeps them as frames of their
own (seq = the controller's frame count since run 2; run 1's build numbered seq by frame id with a bump).

**Not covered by these runs:** combat (quick combat on, no combat in 3 turns), embark, work (kind 18 would make the
worker AnimFailed: no work table, 6.1), a load inside the controller's life (F3), a mid-fade unit in view, the huge save.

**The library's units-on bundles (issue 4467, 2026-11-16 07:21-08:16 EST) [L].** The scene library's seven scenes
recorded at format version 4 with `render_units=1` on c5h-a (build of `epic-0375-units-scene` 66891da6, server.exe
e0916388...; drivers and the full record: `offline-scene-harness.md` 21, `server/evidence/0375/4467/`). Six of seven
made a bundle (`F:/c5-64bit-work/scene-library/staging-v4-units/`: Tiny, fog and the four full scenes, all on the Tiny
save at turn 8, Shoshone): feed.bin holds DRAIN, PLOTS, UNITS (one per drain) and PLAYERS of caller 1, no PATHS or
COMBATS (no AI turn inside an 8-frame span). `server/tools/unitfeed/replay.py` (one command, no client) on each: unitfeed
OK (8 frames, 11-17 drains, 138-144 unit events, 16 units listed, 0 statuses, 0 LOUD), `unitscene scene` 8 of 8 read
back and written equal, `unitscene prepare` 16 of 16 `Ok`, `unitanim play` Ok (22 units, 0 failed), `.c5units` and
`timeline.c5anim` written; `compare_c5units.py <the server's render_test_units_dump> <replay> --only-common
--no-figures`: **EQUAL in 8 of 8 frames of every bundle** (2 units in view, every field but the figure lines). The
figure lines are not compared: the server's controller starts 730-801 frames before a library span (pre-check 25-27 s
after the load), so its clip times hold history the bundle does not (11.2's run 3 started the span at frame 1 for that;
the controller's state at the span's first frame is not a bundle member). The live player cannot replay these bundles
(sceneshot does not link the unit scene, `offline-scene-harness.md` 21), so the library proper was recorded with units
off.

**Huge with `render_units=1`: the 64-bit path stopped by the EXE's model-fog timeout (4467, 2026-11-16 08:08-08:16
EST) [L].** `C5H-0301-HUGE_0350` (c499d3c6..., turn 350), the same build and keys on c5h-a: the pre-check 25.7 s (233
rows, 0 failed), `the controller starts at frame 139 with the table's 853 units` at 08:13:45.8, two LOUD `AnimFailed`
lines (`UNIT_DESTROYER` (0, 15514) and `UNIT_DREADNOUGHT` (0, 16807): `the controller: BadLogicFile:
edited_aim_ms_idle_graph.dge: pose edge to node -1: not drawn`, 5 so far by 08:13:48.5), then **the server's loop made
no step for 21.1 s** (the 1 s counters at 08:13:49.0 and next at 08:14:10.1; units `drawn_frames` 1 -> 2 across it; the
decal generation published 21,864.8 ms before the reader applied it). The EXE's model fog (`shell_composite.dll`)
waited for its first mask: `fog models: consumer WAITING` at 08:14:10.4, then `TIMEOUT: no mask from the server within
30000 ms of map frames (producer state 0, 0 published); unrevealed plots cannot be hidden, no frame is drawn (design
5.5, D118, D125)`, `fog: the 64-bit path cannot go on`, the server exited with code 5 and the EXE ended by itself
(dialog watcher: process ended, no box). No bundle (the recorder's span, at published frame 900, never began).
Lines: `server/evidence/0375/4467/huge_units_on_fail.txt`. The units-off huge bundle of the same series records
normally (`offline-scene-harness.md` 21). *Corrected 2026-11-16 by 4468 (was: "[I] The 21 s are the unit pass's first
frames over 733 listed units (models, textures and the controller's first poses loaded on the render thread); not
profiled here"):* `listed` is 3.2's count of candidates before the cull, so those frames drew 29 units (733 listed, 704
culled; unitfeed's replay of the second run's bundle at the same save and camera: 29 in wrap copy 0) [L]. The start's
own work is in the controller's first frame: 1.85 s live with 853 units (4468's R1, 3.4) and ~0.75 s offline, of which
the main part was `ClipGraphFactory::bind` reading every clip file of every held member in full on the render thread
for its duration [L: unitfeed under perf/sampler, 3.4's 4468 item]. The 21 s were not reproduced in 4468's runs (8 of
8 launches without a stall on the workstation, 3.4); the reading kept [I]: those clip reads on a cold file cache under the workstation's
disk load at 08:13 (the second run, 09:09, had the files cached). 4468 moved them to the pre-check's thread (6.1). 4461's huge run with
`render_units=1` (3.4, job 20261116-031605) ran before the controller (4456, 04:55) and before 0394's model fog reached this branch (main merged 05:40) [I: by the commit times]. **The same run again (09:09-09:17) recorded normally**: the
controller started at frame 142 with 853 units at 09:14:07.5, the unit pass drew frame 1 at 09:14:10.1 and 8 frames by
09:14:11.2, the model fog's consumer was WAITING at 09:14:10.3 and ACTIVE at 09:14:10.5 (first mask handed over), no
stall; bundle `C5H-0301-HUGE_0350_20261116-090943_workstation` (offline-scene-harness.md 21). So the failure is not
deterministic (1 of 2 runs) [L]; other teams' builds and games shared the workstation at both times (an indication,
not a cause). For DoD F(2) and the product: the EXE counts 30 s of map frames from its own start of waiting for the
first fog mask; a stall of the server's loop inside that window (here 21 s) ends the 64-bit path (D118), so a first-frame
load on the render thread counts against it.

**unitfeed has no fog filter (4467, 2026-11-16 09:21) [L for the count].** On the huge units-on bundle above, unitfeed's
replay lists 35 units in wrap copy 0 per frame and the server's `render_test_units_dump` 29; the 29 are equal in every
field (figures not compared), the other 6 (players 4 and 38) are only in the replay, and the server's `units 1s` line
says `fog_hidden=6`. 4463's filter for other teams' units on plots the active team does not see runs in `SceneUnits`
(`srv_units_scene.cpp`), which unitfeed does not link (it links `UnitsCore`). The six Tiny-save bundles are equal
(`fog_hidden` 0, or 1 in the river and city runs, whose hidden unit is not among the listed units of wrap copy 0 [I]). Proposed for lead-0375: run the
same filter in unitfeed from the bundle's fog member (or move it into `UnitsCore`), then the huge bundle is the test.
*(Done 2026-11-16 by 4470, next item.)*

**unitfeed applies the server's fog filter (issue 4470, 2026-11-16 11:05-11:50 EST, branch `task-4470-unitfeed-docs`)
[C: the code and its tests; replay results on the staged bundles].** One rule for both: 4463's filter moved unchanged
out of `UnitsHost::filterByFog` into **`srvunits::FilterByFog(BuiltFrame&, players, feedWidth, states, n, width,
height)`** (declared in `srv_units_scene.h`, defined in `srv_units_scene.cpp`, returns the state `on` / `none` /
`no-team` / `grid`, the reason line and the units hidden); `UnitsHost::filterByFog` calls it and keeps its counters and
once-per-reason lines. unitfeed (it links every `server/src/units/*.cpp`) calls the same function after each frame's
`UnitsCore::build`, before the controller's figures, as `beginFrame` does. Its plot states come from feed.bin's **fog
table reads** (PLOTS of caller 3, `kPlotsFog`: the fog scene's `FowPlotTable::copy` calls, recorded since format 2,
`offline-scene-harness.md` 2.11) through **`srvunits::FogStatesOfTable`** (`c5r::fow::stateFromFowBits` per plot, a table
not width x height keeps the last states: `FowScene::readTable`'s rule), so each replayed frame sees the states the live
fog scene held when the server called `setPlotStates` for it. A bundle without fog reads (render_fow=0) is replayed
unfiltered and says so once, as the server does. New: `--no-fog` (the list before 4470), per-frame `fog <state> hidden
<n>` and a closing `unitfeed: fog: <reads> fog table reads, <n> frames filtered, <n> frames not filtered, <n> unit-frames
hidden`. Not moved: `FowScene::readTable` computes its own states (`srv_fow_scene.cpp` is 0339's file); both call
`stateFromFowBits`, and `FogStatesOfTable` names the rule it repeats.

Results (unitfeed `--no-anim`, database `units-1.1.db` of the huge run's own run folder (SHA-256 dfa5d0de...), install
and the three MODS folders of c5h-a, each bundle replayed with and without `--no-fog` and the two compared by
`compare_c5units.py --no-figures`; drivers and the lines in `server/evidence/0375/4470/` (`tests.sh`, `results.txt`, `huge_nofog_vs_fog_compare_c5units.txt`)):
- **Huge (`C5H-0301-HUGE_0350_20261116-090943_workstation`): 10 fog reads, 8 of 8 frames filtered, 6 units hidden in
  each frame (48 unit-frames), 733 listed** against 739 unfiltered: the server's `units 1s` line of that run says
  `listed=733 ... fog_hidden=6 fog=on`. The 48 differences between the two replays are **exactly** the 48 lines of
  4467's `server/evidence/0375/4467/huge_units_compare_c5units.txt` (the same 6 units of players 4 and 38 in the same 8
  frames; the sets compared line by line), which were the only differences between the server's dump and the
  unfiltered replay. So the replay now equals the server's dump of that run in every unit field of every frame
  (figures not compared, as there) [C, by the two comparisons; the dump itself was deleted with 4467's work folder and
  is not compared directly].
- **The six Tiny-save bundles (Tiny, fog, coast, river, city, highlights): 5-6 fog reads each, 8 of 8 frames filtered,
  wrap copy 0 EQUAL with and without the filter** (8 frames, 16 units each), so they stay equal to the server's dumps as
  4467 found. The river and city runs hide 1 unit per frame (8 unit-frames, `listed` 16 -> 15): it stands in **wrap copy
  +1** (`(0, 1 in copies -1, +1)` -> `(0, 0)`), which is why the dumps (wrap copy 0) never showed it; 4467's [I] above is
  now read from the replay.
- Tests (2026-11-16): `test_scene_units` 37/37 plain and MSVC ASan (0 reports; new case `fog_function`: the function on
  hand-made frames, every state and reason, a unit off the grid, a unit with no player row, `FogStatesOfTable` on every
  bit pattern and its two refusals; 4463's `fog_filter` case unchanged through the host); `test_units_runner.py` 7/7 on
  each binary; unitfeed pytest 13 passed (new `test_fog_filter_lines`: no fog reads, unfiltered and said once; `--no-fog`
  byte-equal) with the plain and the ASan unitfeed.

## 12. U7: the comparison method (decision 9)

Pictures DIAGNOSTIC (ACCEPTANCE moves to 0040), each in Visual Checks with the stock reference; COMMON 25 statistics
(median, p95, maximum, difference image, also when equal).

1. **Tiny, per unit and pass.** A copy `C5H-0375-...` of a Tiny save on a pool instance, the product build with
   `render_units=1` (and `render_units_passes=1` for the passes), quick movement on, the game paused at each check.
   One RenderDoc capture of the product frame (the EXE's unit draws: palettes, ids, pixels by replay; the server's
   composited terrain and depth). For the same EXE frame the server writes, under a test key `render_test_units_dump`
   (new, `srv_render_host.cpp`), its pre-composite colour and R32F depth, its `UnitScene` as `.c5units` and its
   `FramePlan`'s palette, keyed by the composite's frame id (pairing with the capture by the camera block's
   `presentCount`, logged at capture time [I: the pairing is to be shown on the first capture]).
2. **Poses forced**: the capture's palettes are fitted (0041's `posefit`) and the server's frame is re-prepared offline
   from the dumped `.c5units` with those clips and t (a `figure` line per figure), so U6 is measured and not the
   controller.
3. **Measurements and tolerances (C1 values)**: drawn set: per frame the units the EXE draws (paired to `(player, id)`
   by plot and art row; shared plots by P1's rule) equal to the server's list, count and identity, exactly; placement:
   figure origins from the palettes within 0.002 world units, heights 0.001 (0041's); seed: the fitted seed (4153's
   method) equal to the allocator's index on every multi-figure unit, and the reader's index (decision 3) on every
   unit; colour: row 0 bit-equal after the rounding rule; pixels: per unit mask and pass, the module's draws against
   the captured draws replayed (0041's `draw_compare` method), floor = 0041's figure (199 / 228 units bit-equal in all
   three passes, median and p95 0); depth: the server's R32F against the capture's D24 at the unit's pixels within 5
   D24 steps; fog: cb1 r9 / r10 lanes against the capture's (R4; in the D33 product the EXE's DistanceFog key is off, so
   the reference for the fog values is a stock capture of the same camera).
4. **Huge save at frame level** (`C5H-VP8P-HUGE_0350` copy; RenderDoc cannot capture it, 0168 risk 8): three cameras,
   the server's dump with `render_units=1` against the harness picture with `render_units=0` (the EXE's units over the
   same server terrain), the same frame of a paused game: the drawn set per unit mask (exact), mask centroids per unit,
   pixel statistics reported (random idle parts differ: stock's own figures between two sessions beside them).
5. **With the controller** (after 0374's merge): the same Tiny frames with figures free, compared in distribution as
   0374's DoD D.

### 12.1 As run on Tiny (issue 4458, 2026-11-16 04:01-04:40 EST) [L; DIAGNOSTIC]

**The pairing of step 1 cannot be made in one process.** Under RenderDoc the composite hooks nothing
(`terrain-surface.md` S1's RenderDoc arm), so the server receives no camera and draws nothing: in the RenderDoc run
`units 1s: ready=0 ... drawn_frames=0` for the whole session, and no dump. **Pairing used instead (departure from
step 1):** the same scripted state in fresh processes of the same layout and save copy: E-B with RenderDoc (the
EXE's frame) and E-A / E-R without it (the server's TEST ONLY `render_test_units_dump` of the same state); the camera
set by `UI.LookAt` on a plot at the load's zoom, the units created by one Lua call each (`server/evidence/0375/4458/
lua/scene_e.lua`, `seam_e.lua`); the state checked equal across the processes by the reader (the same index for every
unit in all three runs) and the DLL's table. A dump is written per drawn frame whose copy-0 scene changed, so the dump
of a state is the newest by write time (not by name: `frame_752` sorts after `frame_5101`). Layout and save as
`unit-formations.md` 12.7.7; captures `F:/c5-64bit-work/0375/4458/captures/c5h-c_20261116-040120_frame{9042,12633,
15352}.rdc` (load camera, the P1 scene, the seam; 22-25 MB each; frame12633 kept, the others removed after their
tables), exported by `cli.py rdc-export --select all` and read by 3909's unit-draw extractor
(`F:/c5-64bit-work/0301/3909/scripts/units.py`, unchanged); comparison `render/evidence/0375/4458/drawn_set.py`,
tables `drawn_{load,p1,seam}.json` there. Instance origins are 3909's bone medians (a pose-dependent estimate, about
1-2 world units), mapped to plots by the catalogue map's relation x = 64 px + 32 (py odd) - 1760, y = 55.424 py -
997.632 (fitted exactly on 3909's figures.json; the same map). One to three instances per capture of the t27 buffer
`bc2a1a7b` have header rows that are not a team colour (alpha not 1): not unit figures, left out.

- **Drawn set, load camera:** the EXE draws 2 units (12 + 6 figures), the server lists the same 2: equal.
- **Drawn set, the P1 scene (10 units created in one call):** the EXE draws 10 units, the server lists 8. Equal: the
  lone units, the land stack (worker + warrior on one plot: the EXE draws only the warrior; the DLL sent the worker
  Visibility(0) after its creation, `CvPlot::updateCenterUnit`), the garrisoned warrior (hidden by both), the lone
  embarked warrior. **Different (2):** the worker created second in the city (its 8 figures drawn by the EXE) and the
  worker on the stacked embarked plot (its boat drawn): each got only **pre-creation** Visibility(0) calls
  (`unit-formations.md` 12.7.9), which the feed applies at the creation (2.5's rule, 4452 / 4460) and the EXE does not.
  **Rule [L]:** a Visibility call that comes before the unit's creation is ignored; every created record starts
  visible. Owner of the fix: the feed's `ApplyUnitLocked` (`srv_feed.cpp`), not 4458's file. *(Done 2026-11-16 by 4463,
  2.2: rule (f).)*
- **Drawn set, the seam (camera on (0, 14), W = 56):** the EXE draws each of the two units created on the seam's
  columns (x = 0 and x = 55) once (2 instances each, its whole figure set): **R3 [L]: one draw per unit per view near
  the seam**, the far column's unit beside the near one; the server lists 1049 in copy 0 (1050 belongs to copy -1,
  which the dump does not write). **Different (2):** the server lists two save units of other players ((3, 1012) at
  (4, 20), (63, 1038) at (7, 20)) that the EXE does not draw: 2.5's source (a) [L]: other players' units on plots the
  active team does not see get no Visibility call at a load; the server lists them, the EXE does not draw them. A rule
  needs the plot's visibility to the active team (the fog state the scene already reads, `fowFeed`): named for the
  scene's owner. *(Done 2026-11-16 by 4463 in the unit host, 3.2; the terrain scene's call that hands the states in is
  still to be wired.)*
- **Placement and P1 [L, bone-median estimate]:** every drawn unit's figure centroid lies within 1.6 world units of its
  plot centre, stacked or alone (the embarked stack's two boats 0.9 and 1.6 units from the centre, the lone boat 1.0,
  the warriors 0.1-1.1): **the EXE has no shared-plot root shift**; stacked units differ only in what is drawn.
  Decision 13: nothing to code in `placement.cpp` (P1 answered as a visibility rule, above).
- **Exact placement [L]** (`render/evidence/0375/4458/placement_inputs.py`: a figures file in 3909's form from the
  server's dump of the same state; 0327's `exact_t.py` on the P1 capture (66 of 80 mesh-instances fitted, every
  figure of the 6 multi-figure units); `catalogue_tsv.py --seed-overrides`; 4354's check `unitscene placement` with
  the height field the recorder took at this load; tables `placement_p1_*.tsv` there): **with the EXE's indices
  (the reader) all 66 figures within 0.0004 world units and 0.0019 degrees, heights within 0.0001** (tolerances
  0.002, 0.1146, 0.001), the land stack's centre warrior and the warrior created without a facing (DLL facing 7,
  placed at facing 2) included: the module's placement is the EXE's for these units, and the shared plot adds
  nothing. **With the server's seeds** 2 of 6 units within: the 4 others (whose seeds the frame order of
  `unit-formations.md` 12.7.9 changed) are off by up to 4.42 units and 0.76 in height: every placement difference
  of this frame is a seed difference. The embarked units (single figures, another art row) were not fitted.
- **Facing [L, estimate]:** a warrior created by Lua `InitUnit` without a facing (DLL facing 7 after the call,
  `unit-formations.md` 12.7.7 (4)) is drawn by the EXE at the heading of one created with facing 2 (bone-median
  headings 24-35 degrees for both; facing 2 = 30 degrees, 7 would be 57.3 by the module's rule): the EXE keeps the
  facing of the creation (`initUnit` has SOUTHEAST when it sends `Created`; Lua's `setFacingDirection` afterwards
  sends no call, `CvLuaPlayer.cpp` 1680-1691, `CvUnit.cpp` 25352 [C]). The feed refreshes `facing` by getter at every
  later event and so carries 7. **Rule:** take the facing at the creation and at moves only. Owner: the feed
  (`srv_remote_feed.cpp`). *(Done 2026-11-16 by 4463 in the feed's table, `srv_feed.cpp`, 2.2: rule (g).)*
- **Seeds:** the 10 creations of the one frame came out in another order in the EXE (`unit-formations.md` 12.7.9):
  6 differences, the same in all three runs.
- **R4 [L]:** cb1 of the EXE's unit draws on the product path (D33, the EXE's DistanceFog key off): r9 = (-0.05,
  -6.12663e-13, 0, 0.34269), r10 = (146, 182, 243) / 255 at camera height 424.264, in the scene and the reflection pass:
  the DistanceFog key does not reach the unit passes. The server's `distanceFogBlock(DistanceFog(), eye)` (terrain.cpp
  517) gives r9.x, r9.z and r10 equal and r9.y equal to the 6th digit by its formula; w (`fogZoomFraction`) 0.342697
  against the EXE's 0.342690 (7.5e-6; the EXE writes its camera controller's target zoom, terrain.h 165 [I]). Computed
  in Python from the formula, not by the server's code at this camera.
- **Not done (named):** pixels per COMMON 25 with poses forced from the capture's palettes (0041's `oracle.py` is bound
  to 3907's index; adapting it, with `exact_t.py` fits of these captures, is the next step); depth against the EXE's
  cities (under RenderDoc the composite does not hook, so no capture holds the server's depth beside the EXE's; it
  needs a colour and depth dump of the composite on the product path); fading units, based aircraft, cargo; the huge
  frame-level comparison (12 step 4, W-4461's). No picture was recorded (none validates anything here). *(2026-11-16,
  issue 0442: all of these done in 12.3: forced poses and depth offline on this capture, the city by pictures (the
  composite dump of depth was not needed), fading, based aircraft, cargo, the huge per-unit masks.)*

**Step 4 done (4461, 2026-11-16 04:07-04:56 EST, c5h-e, VT present; DIAGNOSTIC, not ACCEPTANCE) [L].** Build of 3.4's
DoD F block (server.exe D9F5F17C). Drivers `server/evidence/0375/4461/huge_4461.sh` (round 1), `huge2_4461.sh` (round
2), `live_4461.py frames`, `compare_e.py`; data `F:/c5-64bit-work/0375/4461/huge/` and `huge2/`.
- **Method as built.** The huge copy `C5H-0375-4461-HUGE_0350` (turn 350); three cameras picked from the game state:
  the plots with the most units visible to the active team within 3 plots, at least 8 plots apart: (85, 31) 19 units
  near, (110, 28) 17, (92, 35) 16 (`huge/cams.json`; `UI.LookAt(plot, 0)`, 8 s, popups closed, the active player's 72
  units asleep and the auto unit cycle off, E9). Pictures: the composite's back-buffer dump (2554 x 1411, the
  `shell_composite.shot` trigger as product_path.py; PrintWindow not used). The server's side cannot be pictured alone
  in the product, so the server's session runs in **state (ii)** (`[DisableSystems] Unit = 1` and `MovementPath = 1`,
  unit-state-ii.md: the EXE draws no unit models and the human's turn plays; config.ini restored by hash after):
  **T1** = `render_units=1` + `render_test_units_dump`, state (ii): only the server's units; **T0** = `render_units=0`
  with the product's list: only the EXE's units; both over the same server terrain.
- **Round 1 (a background from a third session) does not work [L]:** SB (`render_units=0`, state (ii)) differs from both
  unit sessions far beyond the units: **the EXE places its route splines (railroads), improvement buildings and
  resource animals anew in every session** (crop of camera 1: three different track layouts at the same plots), and
  water, cloud planes, smoke and animals move by themselves (SB's own two shots 3 s apart differ by a mean 0.4-8.3 per
  channel over the view). The masks of round 1 mixed those with the units (`huge/compare/`, kept for the record).
- **Round 2 (each session's own background):** the unit flags hidden first (`ContextPtr:SetHide(true)` in the
  `UnitFlagManager` Lua state), the three cameras shot, then **every unit killed** (853 in each session; the save copy is
  never saved again), 12 s, the cameras again (`cam<i>_bg.png` and a second shot 3 s later). Unit mask = max channel
  |shot - own background| > 24 outside the noise (pixels where either session's two background shots differ, dilated 2)
  and the fixed interface (top bar, city list, unit panel, notification column, minimap), opened 3 x 3; blobs joined by
  a 6 px dilation, at least 40 px; paired by overlap, else by the nearest centroid within 40 px.

| Camera | Server drawn set (dump `frame_*.c5units`, `units 1s`) | EXE blobs / server blobs | Paired, only server, only EXE | Paired blobs: IoU median (mean); centroid offset median (p95) px; area ratio median | Mask pixels EXE / server, pixel IoU | \|T1 - T0\| inside both masks: median, p95, max |
|---|---|---|---|---|---|---|
| 1 (85, 31) | 40 units, all with the visible flag; `listed=739 culled=699` | 127 / 133 | 104, 29, 23 | 0.753 (0.694); 2.0 (26.4); 0.965 | 259,959 / 216,249, 0.655 | 2, 58, 229 |
| 2 (110, 28) | 35; `culled=704` | 51 / 64 | 30, 34, 21 | 0.820 (0.653); 1.4 (49.5); 0.999 | 155,814 / 58,901, 0.265 | 5, 66, 205 |
| 3 (92, 35) | 39; `culled=700` | 45 / 61 | 38, 23, 7 | 0.690 (0.620); 3.8 (104.3); 0.889 | 156,605 / 123,273, 0.560 | 3, 55, 227 |

  With larger blobs only (`--min-area 200 --join 10`, `huge2/compare_a200/`): EXE / server blobs 44 / 43, 21 / 27,
  31 / 37 against the dump's 40, 35, 39 units; paired 37, 17, 27; IoU median 0.686, 0.793, 0.593.
- **Reading [L, pictures; I for causes]:** the paired blobs coincide to a few pixels (median offset 1.4-3.8 px at
  2554 x 1411, median IoU 0.69-0.82, median area ratio 0.89-1.00) and inside both masks the pixels differ little
  (median 2-5 of 255; p95 55-66 from the idle poses: the EXE's figures animate, the server's stand in the module's
  default clip at t 0). The unpaired blobs are of four kinds, each seen on the overlays (`huge2/compare/cam<i>_masks.png`):
  (a) **the EXE's unit shadows** (green beside yellow figures: T0 has the EXE's mask pass, T1 in state (ii) has no unit
  shadows at all; in the product decision 7 (c) keeps the EXE's), (b) **one tall EXE-only column over the city at
  camera 2** (green, about 150 x 400 px; it went with the kill pass, so it belongs to a unit: an effect the server does
  not draw [I]), (c) noise left in the water and cloud areas after the noise mask (camera 2 is mostly sea: pixel IoU
  0.265), (d) the city banner, which changes when its garrison dies. **No paired blob shows a unit in another place,
  and the server's drawn set per camera is the dump's 40 / 35 / 39 units; an exact per-unit equality of the drawn sets
  is not measurable this way** (a unit is not named by its blob; the EXE's per-unit set needs 0040's records or a
  capture, which the huge save does not allow: 0168 risk 8). Visual Checks (DIAGNOSTIC, info, T0 as the same-camera
  reference, the overlay as the comparison): `vc-20261116-045613-workstation-6517`, `vc-20261116-045615-workstation-b84d`,
  `vc-20261116-045618-workstation-35be`. T0 and T1: 0 LOUD lines in server.log; round 1's S1 had one FeedOutOfOrder
  (3.4's race).

### 12.2 Rerun on the fixed build: the drawn set with the fog filter, and the units in the water's reflection (issue 4465, 2026-11-16) [L; DIAGNOSTIC]

Build of 3.4's DoD C rerun bullet (server.exe 061E513E..., team branch 66891da6). Drivers `server/evidence/0375/4465/`:
`e.sh` runs three arms of one scripted state on a copy of `REL548-TINY_0008` (b2ceb663, 4458's CAT_0009), each
through `e_arm.py`: 4458's load camera (`UI.LookAt` (51, 14)), its P1 scene (`scene_e.lua`, every facing 2) and its
seam (`seam_e.lua`, camera (0, 14)), then the water camera (`lua/water_e.lua`: three naval units of player 0 (the
first of trireme, galleass, caravel, work boat, frigate the database has, in turn) and one embarked warrior, each
alone on a water plot visible to team 0 at distance 2-4 from the capital, facing 2, the camera on the plot nearest
their centroid), the active player's units asleep, the auto unit cycle off, popups closed (D114), the unit flags
hidden, 12 s; pictures A, A2 (5 s later) and B (the water units killed, 12 s later). **J1** = the product with
`render_units=1`, `render_units_passes=1` (the units drawn into t4 through the water's models hook, 7), **J0** = the
same prep relaunched with `render_units_passes=0`, **S** = stock (the 32-bit pin `i0ship-26234371-ia` DLL, config.ini
by `darkred.py --arm stock`, no proxy, no server; the instance's standard files backed up by SHA-256 and restored). In
the product arms a reader check after each step and the server's newest `.c5units` dump per state. Item 2 compares
that dump per plot with 4458's capture of the same state (`drawn_rerun.py`, 4458's `drawn_{load,p1,seam}.json`: the
EXE's instances per plot); item 3 compares the pictures (`e_compare.py`: figures, the 40 px band around them where the
reflection lands, open water; J1 and J0 against S with S's own A2 as the ceiling; the reflection's signal A - B per arm).

Runs (2026-11-16, pool instance c5h-f, F:, Goldberg, VT present): J1 08:11-08:18, J0 08:18-08:26, S 08:26-08:33 EST
(`e.sh`); **J2** 08:33-08:44 (`era.sh`: J1's settings again with `render_test_units_dump_max=1000000`, then the era steps
of 3.4). Every product arm's restore by SHA-256 ok and `provision.py verify` 0; the stock arm's standard files restored
by hash, verify clean; the save copies removed. In every arm the four water plots and the units' ids came out the same
(trireme 1051 (49, 11), galleass 1052 (51, 11), caravel 1053 (54, 14), embarked warrior 1054 (54, 12); camera (51, 11)).
**Found on the way [L]:** with the controller in the build (4456), the scene changes in every drawn frame, so
`render_test_units_dump` writes a file per frame and its default cap (J1 / J0: 400) is used up within the load; a state's
dump needs a cap above the run's frames (J2: 1,000,000, about 1 KB a frame).

**Item 2: the drawn set after a load, the fog filter and rule (f) [L].** Reader checks in J1, J0 and J2 (load camera,
P1 scene, seam, water, kill): **every unit equal at every check** (J1 + J0: 10 checks, 320 of 320; J2 before its era
steps the same): the P1 scene's ten creations in one DLL frame, where 4458's build had 6 seed differences
(`unit-formations.md` 12.7.9), are now equal (rule (e)). The server's listed set (J2's `.c5units` of each state,
`drawn_rerun.py`, tables `server/evidence/0375/4465/drawn_rerun_{load,p1,seam}.txt`) against 4458's capture of the same
state (the EXE's instances per plot, 12.1):

| State | EXE draws (4458's capture) | Server lists, 4458's build | Server lists, this build | Equal |
|---|---|---|---|---|
| load camera (51, 14) | 2 units | 2 | 2 (the same) | yes |
| P1 scene | 10 units | 8: not the second worker in the city (1042) nor the worker of the embarked stack (1044) | **10**: 1042 and 1044 listed (rule (f): their pre-creation Visibility(0) ignored) | **yes** |
| seam, camera (0, 14), copy 0 | 1049 (and 1050 in copy -1) | 3: 1049 and two other players' units on plots team 0 does not see, (3, 1012) at (4, 20), (63, 1038) at (7, 20) | **1: 1049** (the two hidden by the fog filter; `fog_hidden=2` in the `units 1s` line) | **yes** |

So 12.1's three drawn-set differences are gone on this build. The dump holds copy 0 only, as in 12.1 (the seam's 1050 of
copy -1 is not compared).

**Item 3: the units in the water's reflection (DoD E at a water camera, MC #2365) [L, pictures; DIAGNOSTIC].**
Pictures: the composite's back-buffer dump (J arms) and the harness shot (stock), 2554 x 1411, the same camera (the crops
line up to the pixel at the coasts). Statistic: max channel difference per pixel; cells median / p95 / max and the share
<= 16. Regions per water unit (a box around each; the figures = |S_A - S_B| > 24 outside stock's own A - A2 noise, which
takes in the figure and the strongest part of its reflection; the band = 2-40 px around them). Data
`server/evidence/0375/4465/e_compare_J1_J0_S.{txt,json}`, J2 in `e_compare_J1_J2_S.txt`; pictures on F:
(`F:/c5-64bit-work/0375/4465/ecmp/`).

| Unit (box) | Region, px | J1 (units in t4) vs S | J0 (no units in t4) vs S | J2 (J1 again) vs S | S 5 s later vs S (ceiling) |
|---|---|---|---|---|---|
| galleass (51, 11) | figures 3,909 | 8 / 46 / 136, **72.6 %** | 22 / 62 / 135, 40.9 % | 8 / 44 / 93, 73.6 % | 8 / 35 / 100, 82.0 % |
| | band 22,550 | 8 / 35 / 179, **83.3 %** | 9 / 59 / 187, 77.0 % | 8 / 45 / 189, 82.0 % | 8 / 36 / 174, 82.9 % |
| trireme (49, 11) | figures 2,658 | 10 / 71 / 183, **67.8 %** | 15 / 70 / 195, 53.3 % | 10 / 71 / 203, 68.4 % | 8 / 41 / 140, 77.2 % |
| | band 17,986 | 8 / 27 / 169, 83.4 % | 8 / 32 / 169, 82.8 % | 7 / 25 / 161, 86.3 % | 7 / 27 / 174, 85.4 % |
| caravel (54, 14) | figures 3,921 | 6 / 37 / 95, **78.5 %** | 7 / 57 / 115, 70.5 % | 7 / 30 / 84, 79.5 % | 5 / 27 / 129, 85.2 % |
| | band 19,252 | 5 / 37 / 147, 78.8 % | 5 / 43 / 142, 77.2 % | 5 / 35 / 142, 80.5 % | 2 / 30 / 147, 85.2 % |
| embarked warrior (54, 12) | figures 9,185 | 12 / 109 / 181, 54.3 % | 24 / 111 / 185, 42.8 % | 14 / 110 / 176, 52.5 % | 7 / 35 / 166, 82.4 % |
| | band 24,335 | 13 / 113 / 212, 55.4 % | 13 / 115 / 212, 56.1 % | 12 / 113 / 212, 57.5 % | 5 / 40 / 193, 80.4 % |
| pooled | figures 19,673 | 9 / 89 / 183, 64.6 % | 17 / 94 / 195, 49.3 % | 10 / 90 / 203, 64.2 % | 7 / 34 / 166, 82.2 % |
| | band 84,123 | 8 / 77 / 212, 74.2 % | 8 / 83 / 212, 72.3 % | 8 / 78 / 212, 75.5 % | 6 / 33 / 193, 83.2 % |

- **The units' reflection is drawn, where stock has it [L]:** for the three ships the reflection of the oars and sails
  (a pale band below the hull and the oar banks, towards the camera, as `water-system.md` 13.6's camera puts it) is in J1
  and J2 at the place it is in stock and absent in J0 (crops `galleass_crops_S_J1_J0.png`; difference images x4
  `*_diff_J1S_J0S_S2S_x4.png`). In the figure region the share within 16 rises from 41-71 % (J0) to 68-79 % (J1), the
  galleass's median from 22 to 8 (the ceiling's 8), and J2 repeats J1 within one or two points. `water refl 1s`:
  `w_refl_errors=0` and `w_refl_badcam=0` in every line of J1, J0 and J2 (328, 359, 441 lines; `w_refl_frames=0` only
  before the water was first drawn and around the stop), so the models hook never failed.
- **Named causes of what stays (J1 against the ceiling):** (1) **embarked units are drawn by the server with their land
  art** [L, picture; C]: the embarked warrior is 12 standing warriors over the EXE's boat (the catalogue's embarked units
  beside it too), in the scene and so in the reflection (crop `embarked_crops_S_J1_J0.png`); stock draws only the boat.
  `srv_units_core.cpp` sets `art_row` empty and `UnitRenderer::rowOf` (`render/src/units/draw.cpp`) resolves (type,
  style, era) only: the embarked flag never selects an embark model (the owner's `GetEmbarkedGraphicOverride`, else the
  default embark row, `unit-controller.md` 17.3; which row stock uses for players without an override is [I]:
  `ART_DEF_UNIT_TRANSPORT` is the database's only embark-looking row). The embarked box's 54 % and its band's 55 % are
  this; the reflection pass cannot be judged there. *(2026-11-16, issue 4469: fixed, 3.2's embarked row (the override,
  else TRANSPORT / GALLEON / GALLEY by the team's era and all-water passage; "the only embark-looking row" was wrong:
  `ART_DEF_UNIT_GALLEY` and `ART_DEF_UNIT_GALLEON` are the others); the live check at this camera is 3.4's 4469 bullet.)* (2) The idle poses' phase (the oar banks stand at another angle: the
  controller's clock against the EXE's; stock against itself 5 s later has the same kind of difference). (3) The water's
  animation phase in the band (the ceiling's own p95 30-40). (4) In the joined frame the EXE draws its units over the
  server's (4460's picture), so the figure pixels mostly compare the EXE with itself; what J1 changes is the
  reflection under and beside them. Forests are off under D33 (not in these boxes).
- **The EXE's reflection drawn set [L, 4458's captures read again]:** in 4458's three captures every unit the EXE draws in
  the scene pass it also draws into the reflection target (instances on unit plots, reflection / scene: load 18 / 18,
  P1 80 / 81, seam 84 / 85), except
  one instance of the embarked stack at (51, 13) (5 in the scene, 4 in the reflection and the mask). The server draws
  into t4 exactly the frame's listed units per wrap copy (`UnitsHost::drawReflection` uses the scene pass's plans [C]),
  so its reflection set is item 2's drawn set; the stack's fifth instance is not modelled [I: which figure it is].
- Visual Checks (DIAGNOSTIC, stock S_A as the same-camera reference): `vc-20261116-084019-workstation-3eec` (J1, info,
  comparison the galleass crops), `vc-20261116-084021-workstation-97cd` (J0, info, the difference images),
  `vc-20261116-084023-workstation-4746` (the embarked crop, **fail**: the embarked art).
- **Not done:** the reflection target's pixels unit by unit (no capture of the server's t4: under RenderDoc the server
  draws nothing, 12.1, and no test key dumps t4); the shadow-mask pass (decision 7 (c) keeps the EXE's); poses forced
  (the controller runs free here).

### 12.3 The remaining E items (issue 0442, 2026-11-16) [L; DIAGNOSTIC]

Branch `task-0442-units-doe-diagnostics` (account B), main 27785c80f. Build `server/evidence/0442/build_0442.sh`:
server.exe ec91e1cd, proxy 414b707c, composite 54602782 over the pin layout `x64-04b1211b-pfdee656`'s bin (repo
27785c80f, release, the x64 pin 04b1211b8). Stock = the 32-bit pin `i0ship-26234371-ia`. Pool instance c5h-g (F:,
Goldberg, VT present), `provision.py align548` and `verify` rc 0 first (MC #2549); every arm restored by SHA-256 and
verified 0. Drivers `server/evidence/0442/` (`live_0442.py` over 4465's `live_4465.py`, `scene_arm.py`, `tiny.sh`,
`huge_0442.py`, `huge.sh`, `s18_arm.py`, `s18.sh`, `chain.sh`, `lua/`), analysis `render/evidence/0442/`; data
`F:/c5-64bit-work/0442/`. Every picture is a diagnostic: the product arm runs in **state (ii)** (`[DisableSystems]
Unit = 1`, `MovementPath = 1`: the EXE draws no unit model, so every unit model is the server's, over the server's
terrain and under the EXE's cities) with TEST ONLY keys, so no picture here is what a player sees (D112).

**The Tiny arms (`tiny.sh`):** a copy `C5H-0442-CAT_0009` of `REL548-TINY_0008` (B2CEB663, turn 9, Polynesia),
**P** = the product with `render_units=1`, `render_units_passes=1`, the TEST ONLY unit dump, state (ii); **S** = stock;
the same Lua in both (`scene_arm.py`), the unit ids equal in both (1039-1054). Units held (0439's `look.lua`), the head
unit -1 before and after every picture, the unit flags hidden, popups closed (D114: `lua/popups.lua` dequeues every
popup context of `InGame.xml`; the first air try met a popup that none of them showed and was run again with the air
scene alone, no popup then). Pictures 2554 x 1411: P by the composite's back-buffer dump, S by PrintWindow. Runs
2026-11-16: P 20:49-20:55, S 20:55-21:02, the air scene again P2 21:03-21:07, S2 21:07-21:11 EST.

- **Depth against the EXE's city (4458's P1 scene at Honolulu (51, 14)) [L, pictures]:** the server's units and the
  EXE's city models occlude each other both ways as in stock: the workers created in the city stand among its
  buildings, the roofs in front of a figure cut its legs, a figure in front of a building covers it (crop
  `F:/c5-64bit-work/0442/cmp/p1_city2x.png`: stock, product, product's background). The composite's depth handoff
  (7, `depth_write=1`) therefore orders the server's units against the EXE's later lists in this view. The city's
  building set itself is placed anew by the EXE in each session (stock and product show different clusters at the
  same plot; 12.1's note on route splines and improvements), so its pixels are not comparable one to one, and forests
  are off under D33. Masks in the scene's box (800-1800 x 380-1050; `tiny_compare.py`, `cmp/tiny_compare.json`): units
  of P 70,848 px, of S 84,948 (S includes the EXE's unit shadows, which state (ii) does not draw), IoU 0.556;
  |P - S| inside both masks median 12, p95 73, max 229, 61 % within 16, against S's own picture 5 s later median 3, p95
  52 (the idle poses' phase and the city's other buildings). The drawn set: the server's dump of the state lists the
  10 units 4465 found equal to the EXE's (12.2), the seeds equal to 4458's fitted EXE seeds for all 8 units fitted.
  The depth in D24 steps: offline with poses forced (below).
- **Fading (`lua/fade_*.lua`): a worker moved by Lua `SetXY` into the capital and out again, a fast picture d = 0.1,
  0.25, 0.4 s after each call's return [L, pictures; C for the DLL's calls]:** the DLL sends, for a unit entering a
  city, `GameplayUnitVisibility(false)` with the default blend 0.5 s and then, in `addUnit` -> `CvPlot::updateCenterUnit`,
  `GameplayUnitVisibility(true, false, 0.01)` for the plot's centre unit (`CvUnit.cpp` 20540-20556, `CvPlot.cpp` 640-656
  at the x64 pin); leaving a city, `Visibility(true)` (0.5 s), then the new plot's centre call (0.01 s). So the last
  call wins in both directions and **both arms draw the worker in the city and outside it, at once**: no fade is seen
  (outside the city, the worker's share of its own mask: S 0.96-0.97 and P 0.94-0.95 from 0.1 s after the move out;
  0.01 in S and 0.00 in P after the move in, the worker then standing among the city's buildings in both; the server's
  dumps show `fade` (0, 1, p) for one frame and (0, 1, 1) the next, `render/evidence/0442/fade_dumps.py`,
  `cmp/fade_dumps_P.txt`).
- **Fading at a removal (scene `killfade`, runs P4 22:06-22:12, S4 22:12-22:17 EST) [L]:** a worker beside the capital
  killed at once by Lua `Kill(false, -1)` (`GameplayUnitDestroyed`, a removal outside combat), four rounds with one
  fast picture each 0.1, 0.25, 0.4, 0.7 s after the call's return (the picture itself: P's back-buffer dump written
  0.18-0.96 s after the return, S's PrintWindow 0.35-0.94 s): **both arms show the worker gone in every fast picture**
  (share of the worker's change left 0.001-0.005 in both; crops `F:/c5-64bit-work/0442/cmp_kf/`); the server's dumps
  list it until the call and not on the next frame (`cmp/killfade_dumps_P.txt`): a removal is not faded by either.
  Visual Checks `vc-20261116-221821-workstation-ac7b` (pass).
- **The 0.5 s fade with no later call is not pictured [L for the reason, I for the EXE's fade]:** the only DLL path
  found that sends `Visibility(false)` with the default 0.5 s and no later call is a settler founding a city
  (`CvUnit.cpp` 10865-10867: then `kill(true)`). For the active player the city view opens at once (the picture 0.1 s
  after the call shows the city screen), so nothing of the settler can be seen (try of 21:42 in P only, `F:/c5-64bit-work/0442/p3`); the
  server's dumps unlist the settler on the next frame (the removal resets the fade, 3.2). An AI's founding would show
  it but needs a met AI player in view (leader screen): not done. Whether the EXE fades a unit over the blend time at
  all is therefore not shown [I]; every case pictured here ends in a 0.01 s call or a removal, and both arms agree.
- **The teleport's heading [L]: the EXE keeps the unit's heading at a Lua `SetXY` (kind 13), the server turns it.**
  After the moves the stock worker stands as created (facing 2) both in the city and outside; the server's figures
  face the step's direction (the dump's facing 4 into the city, 1 out of it) because rule (g) (2.2) takes the getter's
  facing at kind 13 (strip `cmp/fade_strip.png`, top stock, bottom product). This answers 16's (4463) item for a
  teleport made by `SetXY` with quick movement on: **the EXE does not turn a unit at kind 13** [L]; whether it turns at
  kind 30 (a rebase) is not measured [I]. Named for the feed's owner: take the facing at kind 13 no more (rule (g)'s
  kinds 11, 21, 19, 12, 30 stay) — not this ticket's files. Visual Checks `vc-20261116-211302-workstation-ecd9`
  (**fail**: the heading). *(2026-11-16, issue 0449: taken; the case repeated on the fixed server matches stock, 12.4.)*
- **Based aircraft and cargo (`lua/air_cargo.lua`) [L]:** two fighters created in Honolulu (based in a city) and a
  carrier at (51, 12) with two fighters rebased onto it (`IsCargo` true, `GetTransportUnit` the carrier): **neither arm
  draws a model of a based or a carried aircraft**; the carrier is drawn by both (crops `cmp/air_carrier.png`,
  `cmp/air_city.png`: A, A2 and the background after the kill in each arm). The server's dump of the state lists the
  warrior 1002, the pathfinder 1003 and the carrier only: the four fighters are not listed (their visible flag is not
  set: the DLL's `updateCenterUnit` never makes an air unit the centre unit and sends the others `Visibility(false)`
  [C]). So the drawn set is equal for these cases. The carrier has no hull shadow in P (state (ii) draws no EXE unit
  shadow; decision 7 (c)). Visual Checks `vc-20261116-211303-workstation-6a4e`, `vc-20261116-211305-workstation-9d7c`.
  *(2026-11-20, ticket 4980 [L]: for a LOADED save both stock and the server draw based, intercepting and carried aircraft;
  the 0442 result is for aircraft created or based live; `extras-units.md` 4980.)* *(2026-11-20, 4985 re-scope [L],
  `extras-units.md` "4985 re-scope": the rule is the plot, not live against loaded: `updateCenterUnit` hides every
  aircraft on a plot whenever that plot's units change (an arrival by rebase or `SetXY`, a kill, a unit leaving), in
  stock and the server alike; after a load the aircraft show until then. A fighter created by `InitUnit` alone in a city
  was drawn by both; this 0442 run's two fighters per plot were not, cause of that difference not established.)*
  P1: `vc-20261116-211301-workstation-19de`.

**Pixels with poses forced, and depth, against 0041's floor (offline; `render/evidence/0442/forced_p1.py` through
`offline.sh forced`, 2026-11-16 22:29) [L for the inputs, C for the computation; DIAGNOSTIC].** Inputs: 4458's P1
capture (`c5h-c_20261116-040120_frame12633.rdc`, exported again by `cli.py rdc-export --select all`, 3909's unit-draw
extractor), 4458's exact fits of it (clip and t per mesh-instance) and placement report with the EXE's seeds (module
figure -> measured figure), and **this ticket's server dump of the same scripted state** (`P_P1_A.c5units`: the 10
units 4465 found equal, every seed equal to 4458's fitted EXE seed). The dump's figure lines (the controller's) were
replaced by the capture's fitted (clip, t) per figure, `unitscene prepare --rows` gave the module's palette, each
captured block was paired with the module block of its unit and index count with the nearest bone rows, and per unit
four replays into 2554 x 1411 with every other unit's blocks zeroed: `captured` (the captured draws, terrainreplay),
`floor` (the module's own draws with the captured palette: 0041's DoD C path), `ours` (the module's own draws with the
server's palette), `oursexe` (the captured draws with the server's palette, for its depth). Render build of this
branch (`C:/c5-64bit-work/render-build/d2c8690912`). The 6 units whose figures 4458 fitted (66 figures; the workers
and the embarked units were not fitted, so not forced):

| Unit | Blocks paired, max bone-row diff, row 0 | floor: equal / pixels, max | ours: equal / pixels, p95, max | depth: within 5 D24 steps / pixels, median, p95, max |
|---|---|---|---|---|
| 1002 warrior (save, fortified) | 12, 0.00052, equal | 15,860 / 15,893, 1 | 15,201 / 15,893, 0, 20 | 15,845 / 15,893, 0, 2, 40 |
| 1003 Shoshone pathfinder | 6, 0.00024, equal | 8,194 / 8,194, 0 | 8,024 / 8,194, 0, 14 | 8,166 / 8,194, 0, 2, 1,435 |
| 1040 warrior (land stack) | 12, 0.0015, equal | 14,641 / 14,649, 1 | 13,902 / 14,649, 1, 89 | 14,548 / 14,649, 1, 2, 312 |
| 1046 warrior | 12, 0.00037, equal | 12,859 / 12,865, 1 | 12,338 / 12,865, 0, 22 | 12,827 / 12,865, 0, 2, 30 |
| 1047 warrior | 12, 0.0058, equal | 12,391 / 12,416, 1 | 11,971 / 12,416, 0, 6 | 12,368 / 12,416, 0, 2, 33 |
| 1048 warrior | 12, 0.00047, equal | 11,343 / 11,361, 1 | 11,076 / 11,361, 0, 4 | 11,342 / 11,361, 0, 2, 19 |
| all | 66 figures | 75,288 / 75,378 (99.88 %), max 1 | 72,512 / 75,378 (96.2 %), median 0, max 89 | 75,096 / 75,378 (99.63 %), p95 2 |

- **The floor holds on this capture**: the module's own draws with the EXE's palette are bit-equal or one 8-bit step
  away on every pixel (0041's floor: 199 / 228 units bit-equal, 3901's warrior 14,711 / 14,724 equal, max 1).
- **With the server's palette** (its placement, seeds and colour, the module's pose of the forced clip and t) 96.2 %
  of the units' pixels are bit-equal, median 0, p95 0 (1 for 1040), the largest differences 4-89 on silhouette pixels;
  the bone rows differ by at most 0.0058 (1047) and row 0 (the team colour) is equal in every block. The coverage is
  the same: measured over the server palette's own coverage (`ours_on_own_cover`) the equal counts are the same and 1
  pixel more is covered for three units.
- **Depth**: the server's palette under the capture's camera puts the units' depth within 5 D24 steps of the captured
  draws' on 99.63 % of their pixels (median 0-1, p95 2 steps), the rest on silhouettes and thin parts (up to 1,435 steps
  on the pathfinder's spear tips: a sub-pixel shift of a thin part changes which surface the pixel sees). This is the
  units' own depth with the EXE's view and projection; the server's view-projection against the EXE's is 0169's 1-5
  steps (7). The live picture of the occlusion with the EXE's city is the P1 bullet above.
- Visual Checks `vc-20261116-223113-workstation-25d3` (1040: replay against ours, the difference image).
- Not forced: the three workers and the two embarked warriors (4458's capture has no exact fits of them); the
  reflection and mask passes (0041's C ran all three; here only the scene pass).

**The huge save's per-unit masks at three cameras (`huge_0442.py perunit`, `huge.sh`; try 2, 2026-11-16 21:44-22:06
EST, `F:/c5-64bit-work/0442/huge2/`) [L; DIAGNOSTIC].** A copy `C5H-0442-HUGE_0350` of `C5H-VP8P-HUGE_0350` (C499D3C6,
turn 350), 4461's three cameras ((85, 31), (110, 28), (92, 35)), two arms in one prep: **T0** = `render_units=0` (the
EXE's units over the server's terrain: stock's unit drawing), **T1** = `render_units=1` + the TEST ONLY dump in state
(ii) (only the server's units). At each camera: 0439's `look.lua` (units held, no selection, head -1: try 1 at 21:15
cleared the selection after each kill and the clear moved the camera at the first kill, E9's trap), 8 s, the
back-buffer dump `base` and `noise` 3 s later, then **every unit of every player within 7 plots that the active team
sees, killed one by one** in (owner, id) order (`Kill(false, -1)`, the head unit -1 after each), 2 s, a picture after
each (35, 36 and 30 units; the same lists in both arms). Unit j's mask = the change between pictures j-1 and j outside
the interface and both arms' noise, components near its plot's place on screen (a projective map plot -> picture fitted
to clear kills, residual median 14 px) (`render/evidence/0442/perunit_compare.py`, `cmp_huge/`).

| Camera | Units killed | Drawn by both / only the EXE / only the server / neither | Both: mask IoU median, centroid offset median, area T1 / T0 median | max channel \|T1 - T0\| inside both masks (pixels outside the noise): median, p95, max, share <= 16 |
|---|---|---|---|---|
| 1 (85, 31) | 35 | 17 / 4 / 0 / 14 | 0.62, 8.9 px, 0.64 | 0, 6, 127, 98.4 % |
| 2 (110, 28) | 36 | not separable (below) | | |
| 3 (92, 35) | 30 | 15 / 4 / 0 / 11 | 0.48, 12.5 px, 0.63 | 1, 7, 195, 97.6 % |

- **No unit is drawn by the server that the EXE does not draw** (only-server 0 at both cameras; the server's listed set
  at camera 1 has 29 of the 35 kill-list units, at camera 3 20 of 30, plus 3 and 1 units outside the 7-plot list), and every unit both
  draw sits at the same place (centroid offsets 1-36 px at 2554 x 1411, median 9-12). The masks of T0 include the EXE's
  unit shadows (T1 in state (ii) has none), so the server's masks are smaller (area ratio median 0.63-0.64; a mounted
  Mamluk 0.11-0.18: long horse shadows) and the IoU is low for that reason; inside both masks the pixels are equal to
  a median of 0-1 and p95 6-7 (outside the noise of a moving part, which the method leaves out).
- **Only the EXE (8 units)**: at camera 1 two workers on one plot (86, 29) inside a star fort, and two great war
  infantry of player 1 ((84, 33), (82, 34)); at camera 3 a caravan at the picture's edge (97, 36), a commando and a
  machine gun of player 2 ((94, 35), (94, 39)) and a machine gun of player 6 (97, 39). Each has a small T0 mask
  (263-2,576 px) and a T1 change under 150 px, **yet all 8 are in the server's listed set** of the camera (its dump
  before the first kill lists 29 of camera 1's 35 kill-list units and 20 of camera 3's 30, these 8 among them). So the
  server meant to draw them and its kill changed few pixels outside the noise. Readings [I, not settled here]: the two
  workers stand in a moving work animation inside the fort (most of their change falls in the noise mask), the others
  are small or at the picture's edge; a per-unit picture of the server alone (a TEST ONLY key drawing one unit) would
  settle it. The drawn set per unit is therefore equal for 32 of 40 visible units by this method and not refuted for
  the other 8 (the server lists them).
- **Camera 2 (mostly sea, the ships of 4461's camera) is not separable by this method**: the water moves under every
  ship (10 % of the picture changes between two pictures 3 s apart), so with the noise left out no kill leaves a clear
  component, and without it every unit's mask is water (IoU 0.15 against the water's own change). 4461's blob-level
  numbers for it stand.
- Visual Checks `vc-20261116-223415-workstation-ba74` (camera 1), `vc-20261116-223417-workstation-c374` (camera 3).

**unit-controller.md 18 [I] settled (capture `F:/c5-64bit-work/0442/rdc18`, 2026-11-16 21:38)**: the EXE stops the
clock of a unit it does not draw (culled) and goes on from there; the server advances every unit: a difference in
phase when a unit comes back into view. Details and the consequence for the controller there.

### 12.4 The teleport keeps the heading: the feed's rule changed and checked live (issue 0449, 2026-11-16) [L, C; DIAGNOSTIC]

Branch `task-0449-units-feed-heading-at-lua-setxy` (account B), main 6a60c2087. **The change [C]:** `srv_feed.cpp`
`FacingKind` no longer lists kind 13 (`kUnitTeleported`): at a teleport the record keeps its facing and takes the plot
and the other getter fields; kinds 11, 21, 19 (creations in the EXE), 12 and 30 still take the getter's facing (2.2
rule (g)). Through the table this also sets the controller's `Place` event's facing (`srv_units_anim.cpp`: the record's
facing at the frame's end), so the server's figures keep their heading at a teleport. No wire format change.
Tests: `server/tests/feed/test_feed.cpp` `unit-facing` (two teleports with getter facings 4 and 3 keep 2 and take plot
and damage; two rebases take 1 and 4; a teleport after a rebase keeps 4; the ten other kinds and the combat refresh keep
2 as before), `test_remote_feed.cpp` `facing-4463` (through the DLL-side getters: the teleport keeps 2 with plot 6, a
rebase takes 5). Runs 2026-11-16 22:46-22:49 EST (`C:/c5-64bit-work/0449/feedtests.sh`): feed plain PASS (negative
inject build exits 1), MSVC ASan 298 checks 0 failed, no report, canary caught; remote feed plain PASS (negative
inject exits 1), MSVC ASan 141 checks 0 failed, no report.

**The live check [L], drivers `server/evidence/0449/`** (`build_0449.sh`, `live_0449.py` over 0442's `live_0442.py`,
`scene_0449.py`, `run_0449.sh`, `head_compare.py`, `record_0449.sh`, `lua/`): build of 2026-11-16 22:50-22:55 EST,
server.exe 3ec6ef65 (repo 628c3ca38, release, x64 pin 04b1211b8), proxy c04eb6f0, composite dc13c8dd over the pin layout
`x64-04b1211b-pfdee656`'s bin; stock = the 32-bit pin `i0ship-26234371-ia`. Instance c5h-g (F:, Goldberg, VT): MC
#2549's `align548` rc 0 and `verify` rc 0 before, the product arm restored by SHA-256 (`backup_restore_ok` true,
`provision_verify_rc` 0), `verify` rc 0 at the end, claim released. Save: a copy of 0442's `REL548-TINY_0008` (B2CEB663,
turn 9, Polynesia), the save of the ecd9 case; quick movement **on** (the launch reports `quick_movement: true`; the
Lua `PreGame.GetQuickMovement()` true in both arms). P = the product with `render_units=1`, `render_units_passes=1`,
the TEST ONLY unit dump, state (ii) (`[DisableSystems] Unit = 1`, `MovementPath = 1`: every unit model is the server's);
S = stock. Runs P 22:56-23:01, S 23:01-23:05 EST; both arms exit 0, no dialog. One worker (id 1039 in both arms),
pictures 2554 x 1411 (P back-buffer dump, S PrintWindow), each 3 s after its step, after 0439's `look.lua` (camera on
Honolulu (51,14), no selection) and closed popups (D114):

| state | step (Lua) | the DLL's getter facing after | stock draws | server's dump facing | mask IoU P/S |
|---|---|---|---|---|---|
| H0 | `InitUnit(.., 2)` at (52,14) | 2 | 2 | 2 | 0.486 |
| H_IN | `SetXY` into Honolulu (51,14) | 4 [0442] | 2 | 2 | 0.532 |
| H_OUT | `SetXY` back to (52,14) (= ecd9's case) | 1 | 2 | 2 | 0.475 |
| H_ADJ | `SetXY` to (52,15), direction 0 | 0 | 2 | 2 | 0.544 |
| H_MOV | `MISSION_MOVE_TO` (53,15), direction 1, quick movement on | 1 | 2 | 2 | 0.656 |

"Stock draws" is read from the crops (`F:/c5-64bit-work/0449/cmp/head_strip.png`, top S, bottom P): the formation and
every figure face south-east in both arms at every step. The IoU is of the worker's masks (|X - H_B| > 24 against the
same session's background after the kill, in a 320 x 280 px box around the densest 32 px cell east of the capital;
`head_compare.json`); it is capped by the EXE's unit shadows, which only S draws (state (ii)), so H0 (equal by
construction) reads 0.49. **Control:** 0442's pictures of the same save and camera with the old rule (`P_F_VIS` /
`S_F_VIS`, the server at facing 1, stock at 2) read **0.142**. So the fixed server draws the EXE's heading at all four
kind-13 calls.

**Findings [L]:** (1) the EXE keeps a unit's heading at kind 13 whether the call comes from Lua `SetXY` or from a
mission move with quick movement on (the call is the same `GameplayUnitTeleported(unit, plot)`; `CvUnit::setXY` sends it
through `SetPosition` whenever `CvPreGame::quickMovement()` is set, `CvUnit.cpp` 20978-20990 at e9d0c4837). So with
quick movement on, a player's units do not turn when they move, in stock; the server now does the same. (2) The plot
(52,15) and (53,15) moves revealed plots east of the capital: a background taken after them differs from earlier
pictures by the fog's clouds, so masks of earlier states against H_B need a box away from the fog's edge (the method's
note in `head_compare.py`). Visual Checks `vc-20261116-230834-workstation-6e06` (the ecd9 case, pass) and
`vc-20261116-230835-workstation-c062` (all five states, pass); `ecd9` (0442, fail) stays as the record of the old rule.

**Not measured [I]:** kind 30 (a rebase: the aircraft concerned are drawn by neither arm when based or carried live, 12.3; after a load both draw them, 4980; so
its heading is not seen); kind 12 with quick movement off (the rule still takes the facing there, as before; the EXE's
turn along a path is unit-controller work); whether the EXE turns the unit later (a next turn, an attack) after a
teleport: not looked at beyond the 3 s pictures.

## 13. Premises

| # | Statement | Mark | What answers it |
|---|---|---|---|
| F2 | The unit, player and combat getters used here are pure (2.2-2.4) | **[C]** (4459, DLL fork at the x64 pin 04b1211b8, table in `tracker/issues/4459-premise-reads.md`): every getter is a plain read; the only side effect found is a log line on an invalid civilization type in `getCivilizationInfo`. Not yet in `pure-methods.tsv`: `GetDamage`, `GetMaxHitPoints`, `IsEmbarked`, `ICvPlayer1::GetArtStyleType`, `IsBarbarian`, the 15 `ICvCombatInfo1` getters (Q1) | rows added to `pure-methods.tsv` by its owner (Q1). **Done 2026-11-16 by 4452 (MC grant):** 20 `pure` rows and `GetDamageInflicted` `conditional` (2.3), evidence cited at e9d0c4837, baseline regenerated (`derive_purity.py --make-baseline`: 163 entries, 0 unresolved; at the pin 04b1211b8: 163 kept, 0 dropped) |
| F3 | `(owner, id)` is unique and stable over a unit's life and over a save and load | [C] read (4459): ids saved and restored verbatim in list order, from one game-wide counter (from 1001, saved), unique across players; **[L] 2026-11-16 by 4460** (3.4): 46 keys before a save = 46 after its load in the same EXE process, types equal, and the server's table equal to the DLL's after the load | done |
| F7 | A position record reaches the server after the DLL call that caused it | [L] for a walk (4755's r1, c5h-c, 2026-11-17 09:09-09:15: kind 12 seq 223, its first position seq 225, one EXE frame after `MoveToHexes` in Lua; 17.4). Was [I]; corrected 2026-11-17 09:29 by lead-0040. Not shown: AI walks, embark, walks over 20 plots (17.5) | 4755 (done); the rest with 4757 |
| F8 (new) | Unit creations and removals reach the EXE in feed `seq` order (pushes from several DLL threads cannot be overtaken between `PushUnit` and the forward) | [I] | 4452 read: which threads make unit calls and whether the game-core lock is held; 4453 live: the reader's order. **4452 (2026-11-16):** not settled by a read; the server now counts unit calls per service thread and inside `Update` (2.5, report line), so the first live run answers it [I until then]. **4460 live (2026-11-16, 3.4) [L]:** unit calls come from two threads, ui 271 (the load's `SetupGraphical`) and core 726 (698 inside `Update`), aux 0; whether a push and its forward can be overtaken across the two stays [I] (no reader on the product path; the load's calls and the turns' calls do not overlap in time) |
| S1-S6 | section 4 | S1, S2, S3, S5 [C] with [I] parts; **S4 false in one case** (combat hold); S6 [C] | section 4; live checks `unit-formations.md` 12.7.5 (4453) |
| C1-C3 | section 5 | [C] (C1 also [L] by call log) | section 5; the DoD D capture (4453) |
| P1 | Where the EXE places the roots of several units on one plot (civilian + military, a unit in a city, a stack of embarked units) | **[L] 2026-11-16 by 4458 (12.1): no shared-plot shift** (every drawn unit's figures centred on the plot within the 1-2 unit estimate); what differs is the drawn set: a land stack draws its centre unit only, a garrison is hidden, a second unit created in a city and a stacked embarked unit are drawn because their only Visibility(0) came before their creation | 4458 live (DoD E); a rule of up to about 2 h in `placement.cpp`, else named for 0042 (decision 13) |
| A1 (new) | A city-state's and the barbarians' `GetCivilizationType` names a row whose suffix is the one the EXE uses for their units' art | **False for city-states, true for the barbarians [C] (4460, 2026-11-16):** every city-state is `CIVILIZATION_MINOR` (`CvGame.cpp` 813; `ArtStyleSuffix` NULL), so the player row carries the `MinorCivTypes` index for a minor (2.4) and the scene keys its art by `MinorCivilizations.Type`; the barbarians' `Civilizations.Type` resolves (oracle, 3.2) | done; the oracle re-run on 0041's 20 scenes (3.2) |
| A2 (new) | An existing unit's art row follows the owner's current era (re-resolved at an era change), not the era at its creation | [I] | 4453 live: a unit with an era variation across an era change, captured |
| R1 (new) | The mask pass's view-projection and the resample to ShadowBlur can be built in the server from the tapped ShadowMatrix0 | matrix half **[C]** (4455, 2026-11-16, section 7): the mask pass's cb1 r4-r7 = ShadowMatrix0, 24 of 24; the resample [I] | a read of the shadow view setup (`terrain-surface.md` 11 names `0x615950`) and cb1 of the mask pass in 3909's exports; only needed for decision 7 (a) |
| R2 (new) | The reflection pass's view-projection follows from the frame's camera and the water level by a rule the KB can state | **False as stated [C] (4455, 2026-11-16, section 7):** it is the EXE's `ShadowMatrix1` (0xDA1890), byte-equal in 24 of 24 exports, an orthographic matrix not derived here from the camera; the server needs it tapped | a tap of ShadowMatrix1 (0169 / 0365 files) |
| R3 (new) | The EXE draws a unit once per view on a wrapping map near the seam | **[L] 2026-11-16 by 4458 (12.1):** once per unit, the far column's unit beside the near one | one capture at the seam (4458) |
| R4 (new) | cb1 r9 / r10 of the unit passes equal the terrain's DistanceFog block at the same camera; the product's value under D33 | **[L] 2026-11-16 by 4458 (12.1):** under D33 the EXE's unit passes keep the fog block (r9 -0.05, r10 146/182/243); equal to `distanceFogBlock`'s formula except w (7.5e-6) | 3909's exports against `distanceFogBlock`; one product capture (4458) |

## 14. Decisions 1-13

| # | Decision | Recommendation (brief) | Owner | Refinement from the code |
|---|---|---|---|---|
| 1 | Where the code lives | `server/src/units/`, render units, member of `Scene` | lead's | **Departs:** `build_server.py` needs three changes, not one line: the new sources among the render units with unique stems (or their own object folder), `models/include` in `RENDER_INCLUDES` (`c5r/units/draw.h` includes `c5models/gr2.h`), and `c5r_models.lib` linked beside `c5render.lib` (0041 builds it as a separate static library, `render/src/units/CMakeLists.txt`) |
| 2 | 6 PLAYERS | yes, the seed derived from events | MC's | Adds art style, minor / barbarian flags and the active team (2.4, A1); read at the snapshot point, every turn change and each creation (no DLL call announces a change) |
| 3 | Seed proof | (a) test-only reader + seed fits | MC's | none |
| 4 | Out of step in play | R6 fault, units stay drawn, reset at next load | MC's | **Revised by 4459:** a creation of a known key is legitimate inside an era change and mirrored as the EXE does it otherwise; a removal of an unknown key is the EXE's own no-op; `kUnitEmbark` is a remove + create; kind 15 then 14 is one removal (at 14); a dropped unit event is out of step. **No reset at the next load:** the EXE keeps its table's size across loads (S5), so the allocator carries its state and an out-of-step episode lasts until the server process ends ([I] a fresh server per EXE launch; counted per episode) |
| 5 | Position source | `own` until 0040 | MC's | none |
| 6 | Clock | the server's frame step; EXE frame number from 0040's marks later | MC's | the scene passes the raw step; 0374's revised K6 (1/30 s steps, cap 6) is applied by the controller (6) |
| 7 | Passes | (a) all three in the server, mask and reflection under `render_units_passes` | MC's | **Departs, new option (c) recommended:** 0040's filter drops only the EXE's pass-0 unit draws (VS `29a0b061`, reflection and scene; their reflection is not shown since the server's water binds its own t4) and keeps the mask pass (VS `8cb52b96` for material 90, and the mask programs of 89 and 87), whose output already reaches the server's terrain through 0169's shadow tap: stock's own unit shadows, exact by construction (named differences: figure poses follow the EXE's controller in random parts, and in mode `own` a walking unit's shadow follows the EXE's position, not the server's plot centre). The server draws the scene pass and, with R2 answered, the reflection pass. (a)'s mask path needs R1 and the resample and is kept for 0328. The sheet's 4.5 S5 ("dropping shadow and reflection draws has no visible effect") predates the shadow tap and no longer holds for the mask |
| 8 | Switch | `render_units`, default 0, everything behind it | MC's | the DLL side is gated by `SrvFeed::SetUnitsEnabled` set by the host; `render_units=1` needs `render=1` |
| 9 | U7 method | RenderDoc on Tiny with forced poses, huge at frame level; ACCEPTANCE to 0040 | MC's | the dump key `render_test_units_dump` and the frame pairing (12, step 1); fog against a stock capture (R4) |
| 10 | Residency | load at first creation, keep; pre-check without GPU buffers | lead's | the pre-check on a worker thread with its own CPU-only renderer; the database read from a copy (3.3) |
| 11 | Bundle records after 0364 | records 3-6 with one more raise, codecs first | MC's | the events need no record; `FeedSource` (0364's `srv_scene_sources.h`) needs `copyUnits` and the players (Q3) |
| 12 | Cost measurement | testbed, huge, 10 AI turns, on vs off | MC's | add the pre-check's time and peak memory at load, and `prepare`'s CPU time per frame against units in view |
| 13 | P1 | code it if up to about 2 h, else 0042 | lead's | none |
| 14 (new, lead) | The combat-held removal (S4) | (a) **Recommended:** the allocator models the hold: a kind 14 of a unit that took part in a visible combat (kind 25 with `GetVisualizeCombat`) in the human's turn with quick combat off is applied at the modelled end of that combat (its start = the kind 25 event's arrival time in the server, its length by the EXE's rule as 0042's choreography or a measured table gives it, capped at 12 s / 20 s with a projectile); creations that arrive before then allocate first; one inside a band around the end counts `CombatHoldAmbiguous`. 4453 measures the hold's length and the ambiguity live with the reader. (b) Feed order, every creation during a pending held removal counted as an R6 episode. (c) Wait for 0042's visual queue and ship (b) until then. | MC's | needs the human-turn flag and quick-combat setting in the player/game record (2.4); AI turns keep feed order (the barrier) |

**Open questions** (for the lead; Q1-Q4, Q6, Q7 go to the MC because they touch files or contracts outside 0375):
- **Q1** `proxy/model/pure-methods.tsv` rows for the getters F2 lists (not in 0375's `touches`).
- **Q2** `server/src/remote/srv_remote_main.cpp` (not in `touches`): one hand-over of the database copy's path and
  number to the render side, and a call of `SrvRemoteFeed::WriteTotals` in the report writer (no caller today).
- **Q3** `srv_scene_sources.h` `FeedSource` additions with decision 11's grant.
- **Q4** Decision 7 (c) changes 0040's filter rule: the MC carries it to 0040's brief.
- **Q5** The database copy (3.3): acceptable, or should the module's `SqliteTables` open read-only without
  `immutable=1` (a `models/` change, 0374's folder)?
- **Q6** Two feed events beyond N5's kinds 11-31, both N5 amendments: kind 32 `kLoadBegin`, pushed where the server
  enters the DLL's game load, before `SetupGraphical` (the allocator's `load_clear`, S5; may need a remote file outside
  `touches`), and kind 33 `kEraChanged` from the `GameplayEraChanged` override (player, era) so that the allocator can
  apply the EXE's era-change remove + create (S3). Which units' art changes at an era is the EXE's comparison of the
  unit's art define at the old and new era (`unit-formations.md` 12.7.3, [I] that the value is the era's art define):
  the scene resolves it through 0041's art resolver; 4453 confirms it live.
- **Q7** The combat return value in `a[3]` (2.1), an N5 amendment.

## 15. Definition of done A-I (final proposals)

- **A. Design (C1).** This file, its premises table (13) with marks, and the MC's review answer quoted in the status
  line and in 0375's log, before code is committed.
- **B. Feed and scene, exact.** A test per kind 11-31 on a stand-in DLL object (`server/tests/feed/`): event values,
  table effect, side-table entry; a live count per kind against the EXE's own Lua events over one scripted sequence on
  Tiny (4452); F2 answered per getter in the fork at the pin; F3 by ids before and after a save and load; the
  `UnitRecord` table equal to the DLL's units through the Lua channel (read-only) at five points of a 30-turn run;
  `UnitScene` built by the server for a recorded frame equal field by field to `unitfeed`'s from the same bundle (once
  decision 11 is granted; before that against the server's own `.c5units` dump); `.c5units` written by the server for
  3907's catalogue frames run through `oracle.py` with 0041's merged numbers unchanged.
- **C. The seed against stock.** S1-S6 marked; on a scripted Tiny sequence (creations, deaths in and out of combat, an
  upgrade, an embark of a unit whose index is above the free head, a unit created embarked, an era change, a paradrop,
  a kill in a visible human-turn combat with a creation inside the hold, units of three owners, a save, a reload and
  a second load in one process) and over 30 AI turns of a late game: the allocator's index equal to the EXE's record index for every unit at
  every check (the reader of decision 3, else the seed fits), and on every multi-figure unit of the RenderDoc captures
  the fitted seed equal to the allocator's; every difference with its cause.
- **D. The colour against stock.** C1-C3 marked; row 0 of every drawn unit bit-equal to the captured row 0 after the
  rounding rule for: a major civilization with its default colour, a player with a non-default colour set at setup, two
  players of one civilization if the setup allows, a city-state, the barbarians.
- **E. U7 (DIAGNOSTIC).** By 12: drawn set equal (count and identity per frame); placement within 0.002 / 0.001;
  pixels per COMMON 25 with 0041's figure as the floor; depth within 5 D24 steps of the capture's; P1 measured; R3, R4
  answered; with the controller free (after 0374) the same frames in distribution; huge: drawn set per unit mask and
  the difference per mask at three cameras.
- **F. Parity, nothing regressed, cost.** (1) Parity pair, 30 AI turns on a Tiny copy, `render_units=1` against 0:
  `eq_stats.py` keys after the load and at the last turn identical, 0 differing. (2) 30 AI turns on the huge save with
  `render_units=1`: 0 EXE and 0 server crashes, 0 LOUD lines other than counted R6 episodes, `outOfStep` 0, dropped unit
  events 0. (3) Testbed (decision 12): server private bytes and video memory after the load and at the end, the
  pre-check's time and peak memory, the scene's CPU and GPU time per frame, the feed's added time per AI turn, with and
  without the switch, statistics by `PERFORMANCE.md`.
- **G. Failure behaviour.** Every status of 10 reached, logged LOUD, counted, in the report; `PrecheckFailed` shown once
  live on the shipped pack layout by D125's route (as 0167 12.5 (b)).
- **H. Tests.** A negative test per status of 10 and per refusal of the record readers (11), asserting output and exit
  code; skipped counts as failed; `server/tests/units/`, `server/tests/feed/`, `render/tests/units/` plain and once
  under MSVC `/fsanitize=address` with 0 reports; `unitfeed`'s Python tests and the existing feed and render-host
  tests pass.
- **I. Research, tracker, offline loop.** This file listed in the research/05 README; `unit-formations.md` 12.1 and
  `unit-draws.md` 5 corrected in place with dated notes; the corrections owed to 0374's and 0364's files made by grant or
  listed in the MERGE-REQUEST (contract 2.1 city plot and `a[3]`, 2.2 `reserved`, 8a closed; `unit-renderer.md` 4;
  `unit-playback.md` 8); actual hours; captures and tables only on F:; a recorded game replays through `unitfeed` into
  `.c5units` and `.c5anim` with one command and no client (once decision 11 is granted; else a named follow-up with the
  round trip shown by tests).

## 16. Not verified

- Every [I] premise of 13; the live checks of S1-S5 and C1 (`unit-formations.md` 12.7.5, `unit-draws.md` 5.1).
- Sections 1-3 read the DLL fork at `e9d0c4837` of a local checkout; 4459's premise reads used the x64 pin 04b1211b8.
- How long the EXE holds a combat-killed unit's removal (decision 14) and how often a creation falls inside it.
- That `ModFolders()` (every MODS folder) gives the same files as the active mods; VP as a DLC modpack not checked.
- ~~The pre-check's time and memory on VP's whole database~~ measured 2026-11-16 by 4460 on the workstation (indication, D106): 14.3-14.9 s in the live server, 13.6 s and 387 MB peak commit in the pre-check mode on the catalogue copy (3.4); ~~the testbed numbers are DoD F 3's~~ on oldpc 13.9 s (sd 0.19, n 7, 4461, 3.4); its peak memory is not separable in the live server (it stays below the process's later peak). ~~`prepare`'s cost on a late huge game~~ measured by 4461 (3.4, 9): +15.2 ms server CPU and +0.35 ms GPU per frame; which part of the unit pass takes the CPU: the sampler item of 3.4. *(2026-11-16, 4466: after the art cache and the pose memo, with the controller: +32.3 ms CPU, +0.51 ms GPU, 32 frames/s, of it the controller +30.5 ms (set 2); without the controller ~1.7 ms, 253 frames/s, 3.4.)*
- (4461) ~~Which game event made the one `KnownCreation` of the huge run (key (4, 15542), turn ~377, 3.4)~~ *(4463,
  2026-11-16: the EXE's `ICvUnit1::SetupGraphical` at an era change, 20 calls in the run [L]; that this unit was one of
  them and why the allocator's era window did not take it [I], 3.4: a live run is named there)* *(4465, 2026-11-16 [L],
  3.4's rerun bullet: six era changes on the T0018 copy re-created nothing and gave 0 KnownCreation; on the CAT_0009 copy
  era 6 re-created four embarked units, the EXE freed all four before creating any, the allocator kept the one
  `Created` re-creation's index: 4 differences, no LOUD line. Open [I]: when the EXE keeps the index (4458's case) and
  when it frees the era's re-creations first; a run with the feed recorded through the era step)* *(2026-11-16, 4466 [L]:
  settled by a probe of the run's own autosave, 3.4: player 4's era change 5 -> 6, the unit a great war infantry whose
  art row does not vary by era, so the server's art test leaves it out of the window)*; the EXE's per-unit
  drawn set on the huge save (only blob level here, 12) *(2026-11-16, issue 0442, 12.3: per unit at cameras 1 and 3,
  32 of 40 visible units drawn by both, 8 by the EXE only by the masks though listed by the server, 0 by the server
  only; camera 2 not separable)*; the cause of the EXE's +6.8 points of GPU utilization with the
  server's units on (3.4).
- (4466) Why the EXE re-creates at an era change a unit whose art row does not vary by era (the great war infantry of
  3.4's probe; its record had been created embarked and the unit disembarked in the same turn) [I]; whether every such
  re-creation is of a held key of the era player, which the proposed allocator rule (3.4) assumes. *(2026-11-16, issue
  4469: the rule is in the allocator (section 4) and holds on the recorded feed of unit-formations.md 12.7.11; there
  every embarked unit is re-created at every era change whatever its art, the land workers only where their land row
  changes; a unit created embarked and then disembarked was not re-created (eras 7, 5, 6): why 4466's was stays [I].
  Open [I]: a land `Created` re-creation after embarked removals of the same drain (the order of the at-once creation
  against the group).)* That the great war
  infantry's missing figure 9 (3.4) is a healed unit's figure the controller removed (`remove_fallen`, no revival)
  [I] *(2026-11-16, 0439: the revival built, 3.6; the recorded feed's infantry 6 -> 14 figures at the heal [L])*. ~~The controller's fortify of civilians (`civilian_state_01.fsmxml never
  tests path 4 = 1000`, 14 types)~~ *(answered 2026-11-16 by 4470, 6.1 [C]: the EXE posts `IDLE` for every `fortify 0`,
  civilians included, so the server's mapping is the EXE's; what remains [I] is that the EXE's civilian machine ignores
  the value, and the controller's refusal is 0374's)* *(2026-11-16, 0439: civilian_state_01 reads 1000 by a not-equal
  test; an unread value is a no-op, 3.6)* and work without a work table: which state the EXE gives them (the
  fix belongs to 0374's mapping, 3.4) *(answered 2026-11-16 by 0439 [C]: the EXE's tool by the build's Type,
  `unit-controller.md` 14.1; built, 3.6)*. ~~The controller's per-frame cost with the step and collect limited to the listed units (3.4's
  profile; not built).~~ *(2026-11-16, issue 4468: built and measured, 3.4's 4468 items.)*
- ~~(4468) Whether the EXE advances the animation of a unit it does not draw (culled, or on a plot the active team does
  not see) or freezes it: no capture compares the two (`unit-controller.md` 18 [I]; the server advances every unit).~~
  *(2026-11-16, issue 0442 [L], 12.3 and `unit-controller.md` 18: a culled unit's clock stands while it is not drawn;
  the server's rule differs; the fog case not measured.)*
  The cause of 4467's 21 s stall: not reproduced in 8 launches; the cold clip reads under disk load stay the reading [I]
  (3.4's 4467 paragraph). Which rows the 26 clips the controller's start still reads on the render thread belong to [I:
  rows the pre-check does not reach, embarked rows]. The read-ahead (6.6-7.3 s on the workstation, 5.7-6.4 s on oldpc
  in 13 launches) delays the scene's readiness: a parallel or header-only read would take it off (not built).
- (4463) Whether the EXE turns a unit at a teleport (kind 13) or a rebase (30): the feed takes the facing there (2.2)
  [I]; *(2026-11-16, issue 0442 [L], 12.3: at kind 13 it does not; the server does: a difference, named for the feed;
  kind 30 open)*; a live check: `SetXY` of a unit to an adjacent plot in another direction with quick movement on, the EXE's drawn
  heading against the server's. *(2026-11-16, issue 0449 [L], 12.4: done, kind 13 settled: the EXE keeps the heading at
  a Lua `SetXY` and at a quick-movement mission move; the feed keeps it now and the server draws stock's heading. Kind 30
  stays open [I].)* Whether a `Fortify` or `DestroyedInCombat` before the creation is ignored by the EXE as a
  `Visibility` is (the feed still keeps those bits) [I]. Whether the EXE hides other players' units on plots the active
  team has seen but does not see now as on unseen ones (the fog filter does, 3.2) [I]. ~~The race fix and the fog filter
  live (the filter needs the terrain scene's `setPlotStates` call first, 3.2).~~ *Seen live 2026-11-16 by 4465 (3.4, 12.2):
  0 FeedOutOfOrder in 30 AI turns and in four product launches; `fog=on` with `fog_hidden` 1-7, and the seam's two
  other-player units no longer listed (the drawn set equal to the EXE's).*
- ~~(4465) The server's art for an embarked unit: it draws the land art row (12.2 item 3, [L, C]); which embark row stock
  uses for players without `GetEmbarkedGraphicOverride` [I].~~ *(2026-11-16, issue 4469: the EXE's rule read and the
  server's in 3.2 (the embarked row); open: the Renaissance gap (the team's all-water passage not in the feed: those
  units are not drawn, LOUD) and an override changed by Lua at run time.)* *(2026-11-16, 0439: both closed by kind 21's
  a[3] bits, 3.6; live: GALLEON from the passage bit and the Danish longboat after a Lua `SetEmbarkedGraphicOverride`
  [L].)* The server's reflection target (t4) per unit and pixel (no
  dump of t4 and no capture of the server). Units the controller fails live (`AnimFailed` for a VP worker's farm
  irrigation and a settler's fortify, `UnitRefused` for the Shoshone pathfinder's figure 5, 3.4): not drawn by the server.
  *(2026-11-16, 0439: the causes closed, 3.6.)*
- ~~The frame pairing between a RenderDoc capture and the server's dump (12, step 1)~~ answered 2026-11-16 by 4458 (12.1): not possible in one process (the server draws nothing under RenderDoc); two processes at one scripted state instead.
- ~~Whether `GetFacingDirection` takes true or false~~ settled 2026-11-16 by 4452: `true` always gives `NO_DIRECTION` [C], so `false` (2.2).
- ~~4452's live checks (F3; a count per kind against the EXE's Lua unit events)~~ done 2026-11-16 by 4460 on the product path (3.4): F3 equal, every kind with a Lua event equal over one sequence. Not covered by that sequence: kinds 12, 15, 18-22, 25-31 (no move, combat, embark, garrison, work in it); the `UnitRecord` table at five points of a 30-turn run (DoD B; 4460 compared three points of a 5-turn run).
- The EXE's side of the visible flag at a load (2.5): units of other players on revealed but not visible plots, and the non-centre unit of a shared plot (U7's drawn set). *(2026-11-16, 4458 [L], 12.1: other players' units on plots the team does not see are not drawn by the EXE after a load (2 at the seam camera); the non-centre unit of a land stack is hidden by both; a pre-creation Visibility(0) is ignored by the EXE.)* *(2026-11-16, 4463: both rules in the code, 2.2 and 3.2.)*
- At run time, a request for a clip no package ships (ClipMissingInInstall): the controller rule of `unit-controller.md` 5 is [I] and not implemented; *(2026-11-16, 4456 / MC: not reached by 0375's mapping; owed by 0042, 6.1)*.

## 17. 0040: position mode exe (issue 4755, 2026-11-17) [C: the code and its tests; L where marked]

Epic 0040 (state ii-d), child 4755, branch `task-4755-positions-exe` (account B, w-4755). Breakdown
`research/07-architecture-decisions/epic-0040-breakdown.md` 1.3, F4-F9 and O3 (a); contract `unit-feed-contract.md` 3.3, 4,
6; the bridge's records `research/03-dll-engine-boundary/proxy-design.md` 24.6. Under O3 (a) this is root positions and
move / arrive timing only: a unit walking a bent path keeps its record's (end) facing, and per-member path following stays
with 0042.

### 17.1 The switch

- `server.cfg render_units_positions = own (default) | exe`, parsed by 4753's block in `srv_render_host.cpp` (8.1); refused
  values by name (`render_units_positions=EXE: own or exe`). It reaches the unit scene as `UnitsOptions::positionsExe`
  (`srv_units_scene.h`): server.exe's factory `MakeLive` (`srv_units_scene.cpp`) sets it from
  `srvrender::RenderUnitsPositionsOption()`, the value `ParseHostConfig` read last. `SceneOpts` copies the same global into
  `SceneOptions::unitsPositions`, which the mode word's writer reads (8.1), so the scene and the writer get one parsed value.
  Chosen by lead-0040 (answer to queue #4007, 2026-11-17): no `srv_render_host.cpp` change (not in 0040's touches); the
  alternative was one line in `SceneOpts` (`o.unitsOpt.positionsExe = ...`). Test `positions_switch`: for no key, own, exe
  and own again, the scene made by the live factory (`MakeSceneUnitsLive`, the registered factory by name) and a writer
  given `SceneOptions::unitsPositions` as `SceneOpts` sets it agree; `EXE` and `1` are refused by name.
- Tests and unitfeed set `positionsExe` directly. unitfeed takes it from the bundle's `scene.json` `render_config`
  (`render_units_positions`, written by the recorder since 4753; a bundle without it is replayed own) or `--positions
  own|exe`.
- In mode own nothing of 0375's picture changes: kind-6 records are counted (`own=`) and not applied, and kind 12 still
  gives `move` and `arrive` in one frame (6.1).

### 17.2 The rule in mode exe

Code: `UnitsCore` (`srv_units_core.{h,cpp}`: `posRecord`, `posEvent`, `noteFrame`, `buildList`), `AnimFeed`
(`srv_units_anim.{h,cpp}`: `arrive`, `setPositionsExe`). The records are `SrvFeed::kUiMirror` events with `flags` 6
(`plot` = the player, `turn` 1 an arrival / 0 a position, `a[0]` the unit id, `a[1..3]` x, y, z as float bits) and 7 (the
frame mark: `a[0]` the bridge's drain number, kept as `exe_frame=`).
- **Root.** A unit's root is its newest kind-6 position (`UnitInput::has_root`, `root_x/y/z`; `UnitScene::position_source`
  `exe`); the cull takes the root, not the plot centre. An arrival record snaps the unit to its plot centre (`has_root`
  false) until the next position; live the EXE posts one more position after the arrival in the same frame, so a walk
  ends at the EXE's last position (17.4) [L]. A unit with no record stands at its plot. **A walking unit before its first position stands at the plot it
  left** (the table's plot before the batch with its kind 12): the DLL has already moved the record to the destination
  when it calls `GameplayUnitMoved`, so without this the unit would stand at the destination for the frames until the
  first position and then jump back [I: the jump's length is F7's delay].
- **Walk.** A kind 12 opens a walk (`walks=`; a kind 12 of a walking unit continues it, `moves=`) and gives the controller
  `move <facing> <path length>` only. The unit's arrival record closes it: `arrive <facing>` (the record's facing at the
  frame's end), never in the frame of its own `move`: an arrival taken in the same frame as the move waits for the next
  frame (`deferred=`).
- **Snaps without arrive.** Kind 13 (teleport) and 30 (rebase) clear the root and end a walk (`place_ends=`; the
  controller's `place` resets the unit); kind 11 (a creation, also an era re-creation) and 14 (removal) forget the unit;
  32 (a load) forgets every unit (`loads_cleared`, open walks `load=`).
- **F7.** A position whose seq is below its walking unit's last kind 12 is counted (`before_move=` / `f7=`), LOUD, and not
  applied. The drawn frames between a kind 12 and the walk's first position are kept (`first_max=`, and `first_over2=` for
  more than two); frames are the drawn frame ids (the composite's frame id, the EXE's Present count), so while the server
  draws only every n-th EXE frame (under the recorder about 1 in 9, 11.2) this is an upper bound; the count in EXE frames
  is measured in Lua (17.4).
- **F9.** Settled from the DLL [C, `Community-Patch-DLL-x64` at the pin 04b1211b8, `CvUnit.cpp` 20991-21002 and
  29812-29840]: a move the DLL shows (quick movement off and a plot the watching human sees) is queued
  (`QueueMoveForVisualization`) and published as one `GameplayUnitMoved` (feed kind 12) per at most 20 plots; with quick
  movement on (or a move the human does not see) the DLL calls `SetPosition` per plot, which is `GameplayUnitTeleported`
  (feed kind 13), never kind 12. **So a quick move opens no walk and needs no arrival**: it is a `place` per plot, and the
  EXE's position record after each teleport puts the root. F9's rule therefore covers only a walk whose arrival record
  never comes (a dropped record, a walk the EXE abandons): a walk with no position for `kWalkQuietFrames` = 30 drawn
  frame ids (`kWalkStartQuietFrames` = 120 before its first position) is ended: `arrive`, the unit at its plot, `quiet=`
  counted and logged (`units: positions: F9: the walk of unit (p, id) ... ended by the rule`, not LOUD). The count runs
  from the first frame noted after the newest position (or the kind 12), not from the frame before it was drained: a
  batch drained after a long draw (the recorder's frames take 10 EXE frames and more) is not old. Frame ids that go
  back (a new composite) restart the count. The bounds are [I]; a walk writes a position every EXE frame (52 in 4754's
  two-plot walk) [L, 4754].
- **Refused records** (LOUD, counted, not applied): a player outside 0..63, an arrival field other than 0 / 1, a
  non-finite x, y or z (`bad=`); a kind-6 / 7 seq not above the last one (`ooo=`). A record of a unit the table does not
  hold is counted (`unk=`), not LOUD.
- **Frame stamps.** `UnitsHost::beginFrame` calls `UnitsCore::noteFrame(frameId)` for every frame, also one it does not
  build (not ready), so the server's stamps equal unitfeed's, which builds every frame of `frames.bin`.
- **Lines.** The `units 1s` line gains `pos=<own|exe> positions= arrivals= walks= open= arrived= quiet_ends= place_ends=
  before_move= pos_bad= pos_unknown= exe_frame=`; the totals line gains `| pos <src> p= a= in= out= w= mv= arr= idle= quiet=
  place= rm= load= f7= unk= bad= ooo= own= hold= first_max= first_over2= marks= arrives= deferred=`. unitfeed prints one
  `unitfeed: move ...` line per walk, teleport and rebase (the ledger: seq and frame of the move, of its first position,
  the newest position before it, its end) and a `unitfeed: positions: ...` line.

### 17.3 Tests [C]

`server/tests/units/test_scene_units.cpp` (48 cases since 4755; was 42): `positions_switch`, `positions_root` (newest
position, arrival snap, no record at the plot, six refusals with their LOUD lines, mode own ignores the records),
`positions_walk` (held at the left plot, `move` alone, the root per position, `arrive` with the record's facing in a later
frame, the ledger, a move and its arrival in one batch: arrive deferred a frame, a second kind 12 continues the walk, mode
own still move + arrive), `positions_quiet_rule` (30 / 120 frames, frame ids going back, teleport = place without arrive,
removal, load), `positions_f7`, `positions_cull`. `server/tools/unitfeed/test_unitfeed.py` `test_positions_option` (the
source from scene.json / `--positions`, the fixture's kind 12 held at its plot in exe, the ledger and positions lines,
`--positions EXE` refused). Commands: `server/evidence/0040/5/run_4755.sh plain|asan|unitfeed|unitfeed-asan|others`.
Results: 17.5.

### 17.4 Live run r1 (acceptance 2 and 3) [L, c5h-c, 2026-11-17 09:09-09:15 EST; DIAGNOSTIC, no picture]

**Setup.** Driver `server/evidence/0040/5/live_4755.py`, run by `run_live_4755.sh c5h-c r1 3500`; 4754's product path
(0375's prep through 4460's `prep_or_restore`: ServerRenderer = 1, the composite and its manifest, D33 with Overlay on,
vpproxy.ini `server_cfg_extra=` TEST ONLY), the positions-only bridge by the TEST ONLY `VPPROXY_TEST_UI_POSITIONS=1`.
Pool instance c5h-c (Goldberg, C:, Vibrant Textures present; claim W-4755; import wrapper reinstalled, 0473), save copy
`C5H-0040-4755-TINY_0018` of `C5H-3000-TINY-T0018` (E337244C), turn 18, player 0. Build (`build_4755.sh`, own inputs:
the pin 04b1211b8 by git archive, a copy of 4662's deps-release) at 6a60f08ae: server.exe `49b9a4a4...`, proxy
`4eaf2b3c...`, composite `3114706b...`, in the pin layout `x64-04b1211b-pfdee656`'s bin (x64 DLL 29F58E17...). server.cfg
extra: `render_units=1`, `render_units_passes=1`, `render_units_positions=exe`, the dump, table and timeline keys, and the
recorder (`render_test_record_after=3500`, `frames=300`, `dump=every:40`; the bundle on F:, 8.9 GB). Evidence
`server/evidence/0040/5/r1/` (acceptance.txt, plan.json, the log lines); data `C:/c5-64bit-work/0040/4755/r1`,
`F:/c5-64bit-work/0040/4755/r1-bundle`. No dialog at any step; stopped by the harness's safe stop; restored by hash
(`provision.py verify` rc 0).
- **The switch.** `units: position source exe ...` at start; the mode word live: `mode bit=1 phase=live src=exe`
  (4753's writer on a live server for the first time; start-up transition 18,328 ms from the load's kLoadBegin). The
  controller started at frame 499 (35 units); the pre-check 233 rows, 0 failed, 16.2 s.
- **Stimuli** (in the recorder's span, frames 3734-5505): warrior 1052 created at (9, 24), selected, quick movement off,
  a two-plot walk to (8, 26); quick movement on and the move back; quick movement off.
- **Server counts** (the last `units 1s` line): `positions=54 arrivals=1 walks=1 open=0 arrived=1 quiet_ends=0
  place_ends=0 before_move=0 pos_bad=0 pos_unknown=0`. Lua in the same run: 54 `LocalMachineUnitPositionChanged`, 1
  `UnitMoveQueueChanged` (false).
- **The walk's roots** (the server's dump, every dumped frame where the unit's row changed): frame 3734 plot (9, 24), no
  root; from 4088 plot (8, 26) and root (-1189.7, 342.4) -> (-1200.2, 360.7) -> (-1211.1, 379.5) -> (-1223.8, 401.5) ->
  (-1234.6, 420.2) -> (-1245.4, 438.9) -> (-1248.0, 443.4) at 4124, held there. **The arrival does not leave the unit
  snapped:** the EXE posts one more `LocalMachineUnitPositionChanged` after its `UnitMoveQueueChanged` (the arrival seq 338,
  a position seq 339, the same frame) [L], so the root ends at that last position, (-1247.99, 443.43); the arrival's snap
  lasts until it. The quick move back: 4805 plot (9, 24), no root (the teleport's snap); 4810 root (-1183.995, 332.583), the
  point 4754 logged at this plot before its walk (`proxy-design.md` 24.6.1) [L].
- **F9 [L, C].** The quick move was two kind 13 (seq 353, 360) and no kind 12: no walk opened, no arrival needed, as the
  DLL read (17.2) says. The EXE posted both of its positions after the second teleport (ledger: 353 none, 360 two, the
  first 6 frame ids after it). No walk ended by the rule in this run.
- **F7 [L].** The walk: kind 12 seq 223, its first position seq 225, no position of the unit before it since its
  creation; 51 of its positions inside the walk, the 52nd after the arrival (above). In EXE frames (Lua: frames of
  `LocalMachineAppUpdate` from `SerialEventUnitMoveToHexes` to the unit's first position): **1**; after a teleport: 0. In
  drawn frame ids the first position came in the kind 12's own batch (+0).
- **D137 rule A [L].** `unitfeed --bundle r1-bundle` (the replay's source exe read from scene.json, `--anim-from` 499 ->
  the span's first frame 3734): OK, 300 frames, 19 drains, 9 table copies, 142 unit events, 0 statuses, 0 LOUD.
  `compare_c5units.py DUMP REPLAY --only-common --no-figures`: **EQUAL 300 frames, 1138 units**; the root fields on their
  own: 237 unit rows with a root, 0 differing. With the figure lines: 1024 differences (units 1002, 1003, 1052, (2, 1009)):
  the replay's controller starts at the span's first frame and the server's at 499, so their clip times differ; unitfeed
  does not read the bundle's `units_history.bin` (the history of 3038 frames is in the bundle) [C]. The controller's own
  equality was shown by 4456 (11.2) on a span from frame 1.

### 17.5 Results and not verified

Acceptance (1): the suites below; (2): r1's replay equal to the dump in every field but the figure lines, roots included,
and F7 held; (3): F9 settled from the DLL and the recorded quick move (17.2, 17.4).
- Suites [C] 2026-11-17 on the workstation (`server/evidence/0040/5/run_4755.sh`): test_scene_units 48 of 48 and
  test_units_mode 16 of 16, plain and under MSVC `/fsanitize=address` (0 reports), at 6a60f08ae; unitfeed's pytest 14
  passed; test_units_runner 6 passed, 1 failed: its `42 of 42` line (the case count is now 48; the line is lead-0040's to
  change, queue #4026); the render host suite 47 passed and the feed suite 298 checks, 0 failed (at ab5465d83; the later
  commits changed only `srv_units_core.*`, which neither links).
- Not verified: the F9 rule live (no walk lost its arrival in r1; the bounds 30 / 120 are [I]); a walk of more than 20
  plots (several kind 12) and a walk that embarks; AI walks (r1 had no AI turn); whether the EXE's position world point
  equals the server's own plot centre (`plotCentre`) to the last bit (the module draws the root as given; a check against
  a stock capture belongs to 4758); the model's lag behind the flag; the wrap seam with a root in another wrap copy;
  `positions_switch` covers the factory and a writer given the parsed value, not a whole `Scene::init`.

## 18. Trade units: facing and figures (ticket 0502, 2026-11-17)

Written by impl-B-0502 (account B, branch `task-0502-trade-unit-facing`). The defect: 4758's camera h3 of the huge t350
copy (`unit-draw-filter.md` 6.3), caravan (0, 17460) at (97, 38) drawn by stock, u0 and the EXE in u1 as one truck, by
the server differently (4758's reading: "facing right-up with a second truck below it"), and caravan (0, 17459) at
(97, 36) drawn only by the EXE. Marks as in this file; the EXE read in an `llvm-objdump` listing of the analysed copy
(`reverse-engineering-setup.md` 2, SHA-256 `b2ce96ae...c88e`) in the session's scratch folder (deleted at the end); only
behaviour is written here (D83). The DLL read at the x64 pin 4f4daac70.

### 18.1 Where a trade unit's route reaches the EXE, and what reads it [C]

- The DLL moves a trade unit along its route in `CvGameTrade::StepUnit` (`CvTradeClasses.cpp` 2153-2233) with
  `CvUnit::setXY(x, y, true, false, true, true)`: an ordinary move. `setXY` sets `m_eFacingDirection` to the step's
  direction (`CvUnit.cpp` 20523-20529) and sends `QueueMoveForVisualization` (kind 12 at the next publish, from
  `finishMoves` in `MoveUnit`) or, with quick movement or the plot unseen, `SetPosition` (kind 13). Trade units live on
  `TRADE_UNIT_MAP_LAYER` (`CreateTradeUnitForRoute`, 2236-2258), which the EXE is not told about.
- The route itself goes only through `ICvEngineUtility3::TradeVisuals_NewRoute` (slot 243, EXE 0x56A460),
  `UpdateRouteDirection` (244, 0x799EC0), `DestroyRoute` (245, 0x72EF30), and the two popup-route calls; at a load for
  every route (`CvTradeClasses.cpp` 2419-2443), at a new route (4560) and after every `MoveUnit` (the direction). Each
  posts a record into event manager B's queue: types **0x75** (route, player, type, n <= 256, n x (x, y); post 0x87DB00),
  **0x76** (route, forwards; 0x87DB60), 0x77 (route, player; 0x87DBC0).
- Their only readers are the EXE's trade-route overlay (`App/Graphics/TradeVisSystem.cpp`, the object made at
  0x6B3B28): it subscribes five handlers (0x53DC70 new route, 0x5C5E50 direction, 0x4B1260 destroy, 0x5B8000 / 0x5C92D0
  the popup route) and keeps a table of at most 256 routes (count 0x1A0C7C8, entries of 0x18 bytes from 0x1A0C7D0:
  route, player, plot count, type, forwards at +0x10, plots), which only the arrow builder (0x45D740, `TradeArrowShader`,
  the "Show Trade Routes" overlay and the popup route) and the draw call (0x42EA40) read. Nothing in the unit system
  reads it (references to the table: 0x42EAA1, 0x45D7E5, 0x4B1260, 0x5C5E50, 0x79E630, 0x8640B0, all in that system).
  **So a trade unit's route does not set its facing or its figures in the EXE.**
- The EXE's creation facing is the DLL's: `GameplayUnitCreated` (0x5579C0) reads `ICvUnit1::GetFacingDirection(false)`
  (vtable +0x40), maps -1 to 2 (south-east) and turns it into the yaw with 0x503880 for the 0x50 record
  (`unit-formations.md` 11.2, 12.7.1). The feed's record takes the same getter at kind 11 (2.2 rule (g)). In 0301's huge
  bundle (`C5H-0301-HUGE_0350_20261117-140850_workstation`, `feed.bin` UNITS record of loop 1933) both caravans hold
  facing **5** (north-west), damage 0, max HP 100, flags 0x8 (visible), art style 2, era 5 [C, the recorded table]; the
  same value reached the EXE at the load.
- Other posters of the facing record 0x50 [C]: `GameplayUnitEmbark` (0x40896C / 0x4089AC), a Lua event's `Call`
  (0x441EF0, registered at 0x548171), and a re-post (0x7947F0); the 0x503880 call at 0x633D98 posts nothing.

### 18.2 What stock draws for a caravan, and the server at the same camera [L; DIAGNOSTIC]

Runs 2026-11-17 17:52-18:12 EST on c5h-c (workstation, Goldberg, Vibrant Textures present; claim impl-B-0502), save
copy `C5H-0502-HUGE_0350` of `C5H-VP8P-HUGE_0350` (C499D3C6, turn 350, Egypt human), driver
`server/evidence/0502/cams_0502.py` over 4758's (`chain_0502.sh`); stock = the 32-bit pin `i0ship-39f17f75-ia`; u1 = the
product path with `[Renderer] ServerUnits = 1` built from this branch at 38373523c (server.exe d00fd9a2, proxy cc2195bd,
composite fa8ea535) on the pin layout `x64-4f4daac7-pf8e9f42`, with the TEST ONLY unit table and dump keys. Pictures
`F:/c5-64bit-work/0502/captures/<tag>/<arm>/`.

- **Lua in the game [L]** (the `probe` step, every arm): 17459 at (97, 36) and 17460 at (97, 38) are `UNIT_CARAVAN`,
  `IsTrade()` true, trade routes 28 and 50, `GetFacingDirection()` **5**, damage 0, not embarked or garrisoned; no other
  unit on either plot (player 5's caravan 13302 at (97, 34), facing 4). The server's table: facing 5, seeds 73 and 74.
- **Stock draws all three members [L]:** a modern caravan is `TheeInLineStag` (3 members: the box van, the sack truck,
  the plank trailer, each its own member model). At a camera centred on (97, 37) at the load zoom (`t1_97_37`) stock
  shows both caravans whole: three vehicles each, in a zig-zag line running back from the north-west-facing front
  vehicle. The zig-zag is the formation's random part: `TheeInLineStag` has `SpacingX` 22, `SpacingY` 23 and
  `UnitRandomness` **1** (install's `Assets/Units/UnitFormations.xml`), so each member moves by up to +-0.4 x 22 = 8.8 and
  +-9.2 units (`unit-formations.md` 3.4), against +-2.1 for a warrior: the caravans are by far the units whose look
  depends most on the seed.
- **The server draws them as stock does [L]:** u1 at `t1_97_37` (both caravans, all six vehicles), and at 4758's h3
  (`h3_92_35`, the load zoom, after h1 and h2 as in 4758, picture 18:10:53, and again 54 s later): the same three
  vehicles at the same places and headings as the EXE's own passthrough frame of the same session and as stock. **4758's
  picture does not come back on this build and these pins.** So the defect of 4758 is not a facing or a member count
  that never reaches the server: facing (5) and members (3) are the EXE's. 4758's "one truck" at h3 is the one vehicle
  of 17460 inside the picture's edge in stock; its "second truck" in the server's frame is a second member placed
  elsewhere.
- Reading of 4758's picture [I, to be settled by 18.3]: in 4758's session the server's seed for the caravans was not the
  EXE's art-record index, so their members' random offsets differed (up to about 18 units between two seeds), which
  moves and seems to turn the vehicles; 17459 "only by the EXE" at the picture's edge is the same effect. The seed is one
  allocation order over every unit of the load (`unit-formations.md` 12.7), so one creation placed differently shifts
  every later index; the caravans (player 0's newest ids) are created late in the load. *(Settled by 18.3 [L].)*

### 18.3 The cause: a load's setup spans several drains, the allocator exempted only the first [L, C]

- **Measured [L]** (plan v3 of `cams_0502.py`: 4758's three cameras, then the `seeds` step: the EXE's art-record index of
  every unit read by `server/tests/units/live/art_records.py` against the seed of the server's TEST ONLY table, both after
  the pictures; two fresh launches on c5h-c, 18:27-18:41 EST, the build of 18.2): **581 and 623 of 853 seeds differed**
  (`F:/c5-64bit-work/0502/captures/v3a|v3b/u1/seeds_u1_*.json`, `server/evidence/0502/seeds_report.py`). In both loads the
  **EXE allocated all 853 creations in feed order**: sorted by the EXE's index, the units' table seq rises everywhere
  but at 1 and 4 units whose seq a later event had raised (the driver's sleep missions, seq >= 13284)
  (`order_report.py`, `embark_order.py`). The server agreed up to index 272 (load a) and 230 (load b), then deferred
  embarked creations (`Embark(true)` of a key with no record, flags 0x09) and second creations on a shared plot to the
  end of their drain (seeds 754-778): rule (e) of `unit-formations.md` 12.7.9. Every later index shifted.
- **Why [C]:** the load's creations come in more than one drain. In 0301's huge bundle (`load_marks.py` on its
  `feed.bin`): kLoadBegin alone in drain 5, then 438 creations in drain 6 and 415 in drain 7 (0.6 s apart), then nothing
  until the kSnapshot of drain 8, 47 s later. The allocator's exemption for a load (`srv_units_seed.cpp`
  `loadSetupGroup`, issue 4462: "this group holds a load's first creation ... rule (e) not applied") covers only the
  group of the load's first creation, so in drain 7 rule (e) applied. Where the drain boundary falls depends on timing
  (272 and 230 in two loads), so whether a given unit is hit changes from load to load: in 4758's session it fell
  before player 0's caravans, in 18.2's runs after them. Tiny loads (22-38 units) fit in one drain, which is why
  4453 / 4458's load checks (12.7.6-12.7.7 of `unit-formations.md`) never met it.
- **The fix (ticket 0502, `server/src/units/srv_units_core.cpp` `LoadSetupGoesOn`, `UnitsCore::onBatch`):** the core
  turns the allocator's frame grouping off (`SeedAllocator::SetFrameGrouping(false)`: every creation allocated at once,
  in feed order, rule (e) off) right after kLoadBegin's own Apply, and on again before the first event that ends the
  setup. The setup is the DLL's `CvUnit::setupGraphical` calls (kinds 11, 16, 17, 18, 21, 22, 23, 24), player by player;
  it ends at the first unit event of another kind (12-15, 19, 20, 25, 28-31, 33), at an event of a player below the
  setup's highest so far, or at one more than 5 s (`kLoadSetupGapSec`) after the setup's previous event. render.log
  then says `units: load setup N ended at kind K of player P (seq S): M unit events in feed order, frame grouping on
  again (ticket 0502)`. Within one drain this gives the old order (the first group was already in feed order), so the
  Tiny loads and 12.7.7's reload are unchanged. Not changed: rule (e) outside a load, the combat hold, the era window.
- **Tests [C]** (`server/tests/units/test_scene_units.cpp`): `load_setup_rule` (the rule's start, the setup kinds, each
  ending, a lower player, the gap, a clock going back) and `load_setup_two_drains` (a setup of two drains whose second
  holds an `Embark(true)` creation and a second creation on a shared plot: seeds 0-5 in feed order; after the setup ends
  by a teleport, or by the gap, rule (e) applies again: an `Embark(true)` creation after a lone one is late). With the
  grouping left on at kLoadBegin (the old behaviour) `load_setup_two_drains` fails at `seeds == want` [C, checked
  2026-11-17 18:57 with a temporary build].
- **Facing and members:** nothing to change. The record's facing is the EXE's (18.1), the members are the formation's
  three (18.2); the visible defect was the members' random part from the wrong seed.

### 18.4 Acceptance of the fix [L, C; DIAGNOSTIC]

Build of this branch at a1c69973b (archive; the fix 118d12be2): server.exe 318d63bd, proxy 85cc4b25, composite
e23e67f6 on the pin layout `x64-4f4daac7-pf8e9f42`; c5h-c, the same save copy and driver (plan v4: 4758's h1, h2, h3,
then t1_97_37 and h3 again; u1 with the TEST ONLY table; u0 with `ServerUnits = 0`; 0442's kills at h3 and t1 in both;
stock references = plan v2's stock1 and stock2 of 18.2 on the 32-bit pin `i0ship-39f17f75-ia`). 2026-11-17 19:04-19:26
EST, every arm exit 0, restored by SHA-256. Compare roots `F:/c5-64bit-work/0502/cmp_v4` (and `cmp_v2`, before the fix).

- **Seeds: 853 of 853 equal** to the EXE's art-record index (art_records.py, after the pictures). render.log:
  `units: load setup 1 ended at kind 24 of player 0 (seq 13279): 2931 unit events in feed order, frame grouping on
  again (ticket 0502)` (a Visibility of player 0 after the last player's setup: the lower-player end).
- **Pictures:** the caravans at h3 and t1 as stock draws them (Visual Checks `vc-20261117-193739-workstation-050a`,
  `vc-20261117-193740-workstation-27b0`, pass).
- **compare_4309, drawn whole (u1):** h2, h3, t1, h3b pass (h3 99.7 % of 117,335 gated px within 16; the not-gated unit
  pixels 98.5 %); **h1 fails by one blob** of 257 px (35 %) at plot (85, 33), a stone wall the EXE draws which the
  server's trees (epic 0431, laid out at random per session) cover differently from the passthrough frame and from stock
  (crop `vc-20261117-193741-workstation-649f`); the camera itself is at 98.8 % and its unit pixels at 99.5 % (4758: 95.4 %
  and 95.9 %). Not a unit: u0 (the EXE's own units) failed a like blob at h2 ((108, 26), 61.6 %) and this ticket's u1
  before the fix one at t1 ((95, 37), 73.8 %). The drawn-whole gate does not mask the server's trees in front of EXE
  models; named for the owner of `tools/compare/model_checks.py` [I: the 0431 trees, not checked further here].
- **Per unit (0442's kills, `perunit_compare.py` T0 = u0, T1 = u1):** at h3 17 units drawn by both, 0 only by the
  server, 1 only by the EXE (player 6's great war infantry at (96, 39) at the picture's top edge, 199 px against the
  150 px threshold: not a trade unit), 23 by neither; caravan 17460 drawn by both, IoU 0.774, centroid 6.5 px, 94.25 %
  within 16 inside both masks (the EXE's own two pictures: 99.8 %); 17459 lies under the right interface strip (neither).
  t1: not separable (6 kills, too few for the plot-to-picture map).
- **Suites [C]** (`server/evidence/0502/run_0502.sh`, 2026-11-17): test_scene_units 50 of 50 and test_units_mode 22 of
  22, plain and under MSVC `/fsanitize=address` (0 reports); unitfeed's pytest 14 passed; test_units_runner 7 passed.

### 18.5 The scene library: the huge bundle recorded again (rule A) [L, C]

- **The standing check with the fix [L, 2026-11-17]:** `test_offline_equals_live.py` with this branch's sceneshot
  (`C:/c5-64bit-work/0502/ss`, built at a1c69973b) over `bundles/`: 7 passed, **the huge bundle
  `C5H-0301-HUGE_0350_20261117-140850_workstation` failed**: about 23,900 colour and depth values per dumped frame
  (largest channel difference 229), all eight frames. The merge base's sceneshot (8577644f2) passes it. The bundle's
  load came in two drains, so its live dumps were drawn with the old, wrong seeds of the second drain's units; the
  player replays the same feed with the fixed core. No input changed, so the format stays 8 (offline-scene-harness.md
  rule A's case "recorded again").
- **Recorded again** (`server/evidence/0502/record_huge_0502.sh c5h-c`: 0495's `live.sh` for huge with this ticket's
  build at cd3df31a3, server.exe c1ad8316, proxy 4cf5d325; 0403's driver unchanged; every renderer default on), 19:54-20:01
  EST into `F:/c5-64bit-work/scene-library/staging-0502/C5H-0301-HUGE_0350_20261117-195457_workstation` (663 MB):
  stamp and checker `scenebundle: OK`, `provision_verify_rc` 0, no box; render.log `units: load setup 1 ended at kind 24
  of player 0 (seq 13279): 2931 unit events in feed order`; forest 690 records, near pairs with 34 images.
- **Checks [L]:** the standing check over `staging-0502/` with this branch's player: 2 passed at tolerance 1:1 and at
  **0:0** (byte-equal, colour and depth); the merge base's player on it: fails (118,157 colour values in frame 1051), as
  expected. `swap_0502.py --dry` rc 0 (`server/evidence/0502/swap_dry.txt`).
- **The swap, for the MC at 0502's merge:** `python -I server/evidence/0502/swap_0502.py` moves the old huge bundle to
  `archive-0502-seeds-before/` (one `retired/RETIRED.log` line, never deleted) and the staged one into `bundles/`; it
  refuses and moves nothing unless `bundles/` holds exactly one huge bundle of format 8 and `staging-0502/` exactly one
  of format 8 with units, particles, trees and near pairs on and their members. After the swap `scenerun.py run` makes
  new baselines for it. Bundles of other saves are unchanged by the fix (their loads fit one drain: 7 passed).

## 19. The walk's facing in mode exe (ticket 4772, epic 0042 C1, 2026-11-20)

Written by W8 of team-0042 (account B, branch `w8-4772` from `epic-0042-combat-movement`), 2026-11-20 05:01- EST. Scope:
breakdown `epic-0042-breakdown.md` 1.2 item 1 and 5 row 3 (0040's O3 left it: "a unit walking a bent path faces its final
direction", 17 above). Mode own is unchanged. No hunk in `srv_units_scene.*` or `srv_units_core.*` (lead-0042 #7808).

### 19.0 In short

- In mode exe a walking unit (from its `move` to its `arrive` or `place`) is drawn facing the hex direction of its root's
  movement, measured per drawn frame from the EXE's position records; a change of direction is a `turn <d> <left|right>`
  event to the controller, and the formation is placed at the new direction (the placement of any facing). Before its
  first step the unit keeps the facing it had before the walk; the arrival gives the record's end facing, as before.
- The EXE's own turning rule for a walk is **not in the knowledge base and was not found** in a 45-minute static read of
  the old build (19.1). The rule built is the nearest direction (a 30-degree threshold), `[I]`. Live (19.4) the EXE's root
  path at a bend is a smooth curve (the heading turns from the first leg's direction to the second's over about 60 % of a
  two-plot walk) `[L]`; the server's facing switches once where it passes 30 degrees, and the figures face as stock's at
  3 points of the path by eye (DIAGNOSTIC); stock's members follow the path one by one, the server's formation is rigid.
- Behaviour change in an existing test: `positions_walk`'s 10-unit step east of a unit created facing west now carries
  `turn:1001` (lead-0042's yes, #7808).

### 19.1 The EXE: what is known and what is not [C for the reads named; I where marked]

Read 2026-11-20 05:05-05:45 EST in a read-only headless Ghidra pass over a scratch copy of the analysed project of the
**old build** (PE stamp 0x546CD0A8, Steam build 4390913; `reverse-engineering-setup.md` 2); the decompiler text stayed in
scratch (D83). Addresses are VAs of that build.
- **Where the rates are stored.** 0x4B8BF0 (caller 0x71CC60) reads `ArtDefine_UnitMemberCombats` rows in the order of the
  INSERT at 0xA169F0 into a member record: `MoveRadius` +0x150, `ShortMoveRadius` +0x154, `ChargeRadius` +0x158,
  `AttackRadius` +0x15C, `RangedAttackRadius` +0x160 (0 = `AttackRadius` x a constant), `MoveRate` +0x164,
  `ShortMoveRate` +0x168, `TurnRateMin` / `Max` +0x16C / +0x170, `TurnFacingRateMin` / `Max` +0x174 / +0x178 (a Max below
  its Min is raised to the Min), `RollRate*` +0x17C / +0x180, `PitchRate*` +0x184 / +0x188, `LOSRadiusScale` +0x18C, then
  the `Has*` flags as bytes +0x198.. and `OnlyTurnInMovementActions` +0x1A1 (byte). A value 0 or below takes a built-in
  default for most of the rates. The template reader 0x790F70 (`Civ5ArtDefines_UnitMemberTemplates.xml`) reads the same
  names with an `f` / `b` prefix (`fTurnRateMin`, `bOnlyTurnInMovementActions`, ...) into a template record of its own
  layout (+0x1AC `fTurnRateMin`, +0x1B4 `fTurnRateMax`, +0x1BC `fTurnFacingRateMin`; each with a "set" byte before it).
- **Who reads them: not found.** A scan of every function for memory operands with these displacements: +0x1A1 occurs
  only in 0x4B8BF0; +0x16C, +0x170, +0x174, +0x178 together only in 0x4B8BF0; with +0x164 as well in 18
  functions (16 others besides the two readers, among them CreateTerrain 0x709B50), not opened: large functions whose offsets
  belong to other structures is the likely reading `[I]` (D83 edit 2026-11-25, 0769: the operand scan's list of the 16
  functions by address was replaced by this count). So the runtime reaches the record through a copy or another base, and the
  consumer of `TurnRate*` / `TurnFacingRate*` is open.
- **Not the walk:** the five per-pass functions `combat-choreography.md` 11 names (0x6C8FD0, 0x5EFC50, 0x6902C0, 0x613940,
  0x51F3B0, all called from 0x5FE650) are the combat simulation's passes (0x6C8FD0 walks the 0x44 combat slots).
- **The position event's producer: not found.** `LocalMachineUnitPositionChanged`'s accessor 0x5DFAD0 (and 0x4AB410,
  0x7B9670) only builds the Lua event object (callback thread, `event_handle`); the C side that fires it is still the open
  item of `world-systems-and-stubs.md` 7.
- **What the records show [L 4755, 17.4]:** the seven roots the server dumped of 4755's two-plot walk (of its 52
  positions) lie on one straight line: the steps between them at 119.85-120.10 degrees, each root from the first at
  119.85-119.99 (north-west, direction 5; computed 2026-11-20 from 17.4's numbers): a path without a bend is straight.
  *Corrected 2026-11-20 by this ticket's live run (19.4) [L]: at a bend the root's path is a curve, the corner is cut
  (was here: "the root moves in straight legs between plot centres ... whether the corner is cut [I]").* What the rule
  cannot match is how the EXE turns the figures through the curve: over time at its `TurnRate`, members one by one `[I]`.

### 19.2 The rule as built [C: the code and its tests]

`server/src/units/srv_units_anim.{h,cpp}` only:
- **Directions.** World +x east, +y north; DirectionTypes d points along 60 - 60 d degrees (the placement's yaw 150 - 60 d
  is that + 90: the models face -y, `unit-formations.md` 11.2). `WalkDirection(dx, dy)`: the nearest direction, a tie
  (exactly between two) to the counter-clockwise one, -1 for a zero or non-finite vector. `WalkOffsetDeg(d, dx, dy)`: the
  signed angle from d's heading, in (-180, 180], positive counter-clockwise.
- **AnimFeed (the timeline side).** It keeps per held unit the facing the timeline gave it (create, arrive, place, turn)
  and per walk (mode exe: opened by `move`, a kind 12 of a walking unit continues it; ended by arrive, place, the unit's
  create or remove, a load) its last sample. In each `close`, after the frame's events, per open walk: the unit's newest
  position (`UnitsCore::rootOf`; none before the first position or after the arrival's snap: nothing); the first is the
  sample; a later one is taken when at least `kWalkMinStep` = 1 world unit from the sample (shorter steps accumulate),
  refused as a jump when longer than `kWalkMaxStep` = 256 (about 4.4 plots in one drawn frame: the wrap seam, a dropped
  stretch; the sample starts again there, counted `walkJumps`); a taken step whose heading leaves the facing by more than
  `kWalkTurnDeg` = 30 degrees gives `turn <WalkDirection> <right if the offset is negative (clockwise), else left>`
  (exactly 180: left). A walk that ends with no taken step is counted `walksNoHeading`.
- **WalkFacings (the drawn side).** `AnimRunner::apply` follows the same events (create, remove, move, turn, arrive,
  place) before the controller applies them; `fillFigures`, which the unit host and unitfeed already call on every wrap
  copy after `apply`, sets a walking unit's `UnitInput::facing` to the walk's facing. The module's placement then puts the
  slots at that direction (the formation "rotates with the heading": re-placed at each turn, not turned over time). Every
  other unit keeps the build's (record's) facing. Both sides follow one event list, so they agree by construction; the
  only gap is a facing the record changed at rest without a facing event (an attacker turned by a fight): before the walk's
  first step such a unit is drawn with the facing of its last create / arrive / place / turn `[I: rare, at most until the
  first position, 0-1 drawn frames, F7 17.4]`.
- **The controller.** `turn` requests nothing (`unit-controller.md` 14.1); the run continues. The mounted units' turn
  blend (paths 7 and 8, the 1460 / 1480 clips, `steer`) is not produced: open (19.5).
- **Quick movement.** The DLL moves a unit per plot with kind 13 (17.2 F9), so no walk opens and no turn is given; the
  unit keeps the record's facing, which the feed keeps at kind 13 (12.4): the EXE's own behaviour with quick movement on
  `[L 0449]`.
- **Lines.** `AnimCounters` gains `turns`, `turnsRight`, `walkSamples`, `walkJumps`, `walksNoHeading`; not yet in the
  `units 1s` line (the line is `srv_units_scene.cpp`'s).
- **The extras seam.** `UnitExtraUnit::facing` (`extras-units.md` 4981.1 point 1) stays the record's facing: the hook runs
  inside the build, before the controller's frame. An Extras B piece that needs the walk's heading there needs one
  `buildList` hunk in `srv_units_core.cpp`, to be agreed between the leads (lead-0042 to lead-0455, #7810).

### 19.3 Tests [C]

`server/tests/units/test_scene_units.cpp`, five new cases in one block at the end (55 cases; was 50):
- `walk_heading_math`: the six directions both ways, 29 degrees off keeps the direction, 31 gives the neighbour, the ties,
  zero and non-finite vectors, offsets 180 / -60 / 60 / -120.
- `walk_heading_bent`: a unit created facing east walks east, bends left to north-east and right to south-east: drawn 1
  (not the record's 0) from the move to the first bend, `turn 0 left`, `turn 2 right`, a 0.5-unit step accumulating, the
  arrival's `arrive 0` drawn 0; counters turns 2, right 1, samples 5; the frames through 0374's writer and reader give the
  turn records back (`0L 2R`).
- `walk_heading_negative`: a walk with a single position (no turn, the old facing to the arrival, `walksNoHeading` 1); an
  arrival record with no walk open (no arrive, no turn, `arrivalsIdle` 1); a 600-unit jump (no heading, `walkJumps` 1; the
  next step north turns to 5); a teleport during a walk (`place` with the kept facing ends it; later positions turn
  nothing); mode own (move + arrive in one frame, no turn, the record's facing drawn).
- `walk_heading_quick`: two kind-13 moves, each followed by positions in a new direction: no move, no turn, facing 4 kept.
- `walk_heading_host` (module): the unit host in mode exe through `drawScene` (build, the controller, `fillFigures`,
  prepare): the drawn list's facing 1, 1 (moved, before the first step), 1, 0 (left turn), 1 (right turn), 0 (the
  arrival's record), drawn (`covered() > 50`), no AnimFailed.
- `positions_walk`'s expectation changed (19.0).

Results 2026-11-20 at 31c3dd4d2, workstation, `C:/c5-64bit-work/0042/w8/run_suite.sh` (the install read only:
`F:/c5-instances/c5h-e/install`, old build): **plain (clang-cl) 55 of 55 passed; MSVC `/fsanitize=address` 55 of 55
passed, 0 reports.** unitfeed's pytest (`server/tools/unitfeed/test_unitfeed.py`, this branch's unitfeed, unitscene and
unitanim): 14 passed. After the merge of `epic-0042-combat-movement` @44bd5c108 (ee6c73084, 2026-11-20 07:10 EST): plain
55 of 55, ASan 55 of 55 with 0 reports, built with an untracked scratch double of `srvmodels::FrameEye` (never
committed): on that head the units test build does not link without it (0598's call in `srv_render_scene.cpp`; being
fixed on main by account A, `FrameEye` inline in `srv_models_scene.h`, MC #7945, lead-0042 07:30; the double goes when
that fix reaches the team branch).

### 19.4 The live check (MC-B's grant #7836: c5h-c, 06:30-07:30 EST) [L; DIAGNOSTIC]

**Build** (this branch at fdbbacaf8, 2026-11-20 05:46-06:02 EST, `C:/c5-64bit-work/0042/w8/build_server.sh` and
`build_layout.sh`): server.exe `e2f81d59` and proxy `e6d6e5e5` by `server/build_server.py --config release --offline`
with `--deps` of a release build_all of the pin 1fd51353e (a copy of 0600's, every step passed) and a copy of the
pin's source (fingerprint checked by the build), 304 s; composite `946f7431` (`shell/build.cmd`); the layout
`C:/c5-64bit-work/0042/w8/layout` = the pin layout `x64-1fd51353-p1e09fc7`'s bin (x64 DLL `78355a75`) with these three.
**Driver** `server/evidence/0042/4772/run_4772.py` + `chain_4772.sh` (4771's chain: prep, per arm options + D33
config.ini, launch, 0444's stop, restore by SHA-256), read-out `analyze_4772.py`. Save: a copy `C5H-4772-TINY_0018` of
`C5H-3000-TINY-T0018` (E337244C). Arms: u1 ServerUnits 1 (position mode exe) with the TEST ONLY dump, timeline and the
scene recorder (after 3500 frames, 200 frames, dump every 25), u0 ServerUnits 0 (the EXE draws the units: stock's
facing). Per arm: Quick Movement off (read back), a warrior at A where the walk A -> B -> C bends by 60 degrees and the
other two-plot route is water or a mountain (else two orders), the unit's EXE positions kept in Lua with the Lua clock,
the camera on B zoomed in, the order, pictures every 0.12 s for 7 s. Expected: in u1 the dump's facing is the pre-walk
facing, then d1 on leg 1 (a turn if it differs), d2 after the bend (one `turn`), the record's end facing after the
arrival; in u0 and u1 the formation faces d1 on leg 1 and d2 on leg 2.

**Runs** (instance c5h-c, claimed 06:31, released 06:57; no dialog, no crash, every run restored by SHA-256 with
`backup_restore_ok` true and `provision_verify_rc` 0): chain r1 06:32-06:43 (u1 rc 0; u0 rc 6: the scene's Lua failed,
`UnitAITypes` is not in the Lua channel's environment, from an edit made between the arms; nothing was walked), chain r2
06:45-06:51 (u0 again with r1's u1 scene, rc 0). Scene in both arms: warrior 1052 (Lua `InitUnit` without a facing; the
Lua getter said 7, the feed's record had 2 at the creation), A (6, 21) -> B (7, 20) direction 2 (south-east) -> C (8, 20)
direction 1 (east): a 60-degree bend to the left; the other route's plot (7, 21) water or a mountain; Quick Movement off
(read back `false`). Read-out `analyze_4772.py --tag r1 --tag-u0 r2`, `server/evidence/0042/4772/acceptance_4772.txt`.
- **The EXE's root path at a bend is a curve [L, both arms].** The unit's 52 `LocalMachineUnitPositionChanged` positions
  (one per EXE frame, 1.70 s in all, 119.3 world units) start at -60.0 degrees and turn smoothly to 0.0: the step
  headings run -60.0, -59.7, -59.0, ..., -31.2, -28.0, ..., -0.2 over the first 31 steps, then 0.0 for the remaining 20.
  So the EXE cuts the corner: the heading changes continuously over the first leg and reaches the second leg's direction
  about 60 % into the walk. (This corrects 19.1's reading of 4755's straight two-plot walk: the legs are straight only
  where the path does not bend.) With the 30-degree rule the server turns where the curve passes -30 degrees.
- **The server's record [L, u1].** Timeline (TEST ONLY `server.c5anim`): `move 1 2` (the record's end facing 1, two
  plots), one `turn 1 left`, `arrive 1`; no other turn. Dump (5,398 frames; the walk drawn in frames 4088-4125 while the
  recorder took about one EXE frame in nine): facing 2 at the plot it left (the pre-walk facing; without 4772 the record's
  1), 2 at roots (-1339.9, 159.2) to (-1302.5, 119.4), **1 from frame 4114** at (-1285.7, 113.6) (the step from the last
  sample heads -19 degrees: 41 off south-east), 1 to the end (-1248.0, 110.9).
- **The pictures [L; DIAGNOSTIC, `C:/c5-64bit-work/0042/w8/cmp/strip3-r1.png`, never in git].** At three points (leg 1,
  after the bend, the end: u0 pictures 003 / 008 / 013, u1 005 / 007 / 015, chosen by where each arm draws the unit; the
  server's picture lags the EXE's position by a few EXE frames under the recorder) the figures face the same way in both
  arms, by eye: south-east on leg 1, east after the bend and at the end. **Named difference [L]:** stock's members follow
  the path one by one (a strung-out column on leg 1 that re-forms at C after the root has arrived), while the server
  places the whole formation rigidly around the root at its facing; per-member path following is not in this ticket
  (17, O3). Two of stock's burst pictures (006, 022) show the figures' shadows but no figures and three are grey (PrintWindow
  artifacts, E9's list); the read-out's automatic P2 / P3 fell on two of them, so the points were chosen by hand.
- **D137 rule A on the recorded walk [L, C]** (`server/evidence/0042/4772/replay_4772.sh r1 1052`, 2026-11-20 06:58-07:0x
  EST): the u1 bundle `F:/c5-64bit-work/0042/w8/bundle-r1` (6.0 GB, 200 frames) replayed by this branch's unitfeed, plain
  and MSVC ASan: both `OK 200 frames ... 157 unit events`, 0 ASan reports; `compare_c5units.py` against the server's own
  dump (`--only-common --no-figures`: roots and facings): **EQUAL 200 frames, 1000 units** in both; the replay's timeline
  has the same `move 1 2`, `turn 1 left`, `arrive 1`. The one `AnimFailed` (unit (3, 1044) `UNIT_INCAN_SLINGER`,
  `hebrew_slinger.fxsxml` not found in this install) is the server's too and not this ticket's.

### 19.5 Not verified

- How the EXE turns the figures through the curve (over time at `TurnRate*`, member by member); the server switches the
  whole formation's facing in one frame where the root's heading passes 30 degrees off. The mounted turn blend (`steer`
  paths 7 and 8) is not produced.
- Per-member path following (stock's column on the walk) is not reproduced: named difference above.
- A walk of more than 20 plots (several kind 12), a walk that embarks, AI walks; a bend of 120 degrees; a walk with Quick
  Movement on was not run live in this ticket (12.4's run covered it before: no walk, the facing kept).
- *Note 2026-11-23 (worker-0673u, ticket 0685):* the stock-against-product measurement of the turn (every displayed
  frame, foot and mounted, 60 and 120 degree bends) is prepared, not yet run: `tools/capture/capture_job.py --cases walk`
  (the camera on the bend by the knowledge base's way, pan-coherence.md 5.4), analysis
  `server/evidence/0685/turn0685.py` (the formation's axis per frame against time, the turn's start, end, duration and
  shape, per-member lag), laptop runner `server/evidence/0673/acc0685_laptop.sh` (runbook 4.15). A 120 degree bend is
  never a shortest path on the hex grid (C is A's neighbour): it is ordered as MOVE_TO B plus MOVE_TO C appended, so
  whether the EXE draws it as one continuous walk is part of what the run shows.

### 19.6 Ticket 0685: turn the formation over time at the EXE's TurnRate (lead-0673, 2026-11-24) [I: the design; C: as built, 19.7]

*(Corrected 2026-11-24 by 19.8: stock turns a walking formation in about 2 frames, not at `TurnRate` over 171-233 ms;
this design's rate is too slow and the facing event is late; the premise below was taken from the axis reader.)*

The measured difference (laptop pass 3, `pan-coherence.md` 5.4, 2026-11-23 [L]): in a 60 degree bend stock's facing turn
lasts 171, 201 and 233 ms (3 walks), the server's 95 ms (1 walk: one drawn frame or two). By the EXE's figure turn rule
(`unit-combat-server.md` M5 [C]: 0x4ACAD0 turns a figure's heading towards the wanted one by (`TurnRateMin` +
(`TurnRateMax` - `TurnRateMin`) x a fraction) x 2 pi x frame time; 0.5 to 1.0 turns per second by default) a 60 degree
turn lasts 167-333 ms: stock's three walks are inside that band (0.71-0.97 turns per second). The root's curve (19.4) is
longer (about 1 s); the figures do not follow the root's heading continuously but turn at their rate towards a wanted
heading that changes in steps [I: from the durations].

The design, built on what exists:
1. **What turns.** The walk's facing rule of 19.2 is kept (the same `turn` events, the same 30-degree switch, the same
   discrete facing for the placement). New: per walking unit a drawn yaw that follows the facing's yaw over time instead
   of jumping: each drawn frame it moves towards the facing's yaw by 2 pi x rate x dt, the short way (exactly 180 degrees:
   the event's side), rate = `TurnRateMin` + 0.5 x (`TurnRateMax` - `TurnRateMin`) of the unit's first member, from the
   combat tables the runner already reads (`CombatInfo` `turn_rate_min` / `max`, 4774; the fraction 0.5 as 4735's turn,
   `unit-combat-server.md` 1979 [I]); without tables 0.75 turns per second. dt from the timeline's clock, clamped to 0.1 s.
   The same turn applies before the first step (the pre-walk facing to the first leg's) and at the arrival (to the
   record's end facing): the turning state outlives the walk until the drawn yaw reaches the facing.
2. **How it is drawn.** The placement still lays the slots at the discrete facing; the difference (drawn yaw - facing's
   yaw) is applied by 4981's rigid turn about the root (`ApplyUnitXforms`, every bone row and material-87 world row),
   merged into `BuiltFrame::xforms` before it runs; an extra's own heading composes (its yaw + the difference). So the
   formation turns as one body over 170-330 ms instead of in one frame. No change to the placement module, the scene
   file or the feed's records: unitfeed's `.c5units` facings and the offline comparisons stay as they are.
3. **Switch.** *As built (19.7): the test switch `C5_RENDER_UNITS_WALK_TURN=0` in the server's environment (as 0709's
   `C5_RENDER_MODELS_KEEP`), not a server.cfg key, so no host or option file changes; counters in `WalkFacings`, not in
   `AnimCounters` (was here: "server.cfg `render_units_walk_turn` ...; counters `walkTurns`, `walkTurnMsMax` in
   `AnimCounters`").*
4. **Not recreated** (named difference, 19.4): stock's members follow the path one by one and each figure turns with its
   own fraction; the mounted `steer` blend (paths 7, 8). The rigid turn removes the snap, which is what the report is about.

Files: `server/src/units/srv_units_anim.{h,cpp}` (WalkFacings: the drawn yaw, the rate, the counters),
`server/src/units/srv_units_scene.cpp` (one hunk: merge the walk turns into `m_built.xforms` before `ApplyUnitXforms`),
`server/tests/units/test_scene_units.cpp` (a 60 and a 120 degree bend at 60 fps: the yaw monotone from the first leg's to
the second's over 13-15 frames at 0.75 turns per second; the arrival's turn; quick movement and mode own unchanged; the
switch off gives 19.2's frames). Acceptance: the units tests plain and ASan; rule A (the scene library, merge-check
"scene-bundle rule A"); the laptop pass 3 again with more repeats (product turn inside stock's 171-233 ms band); the
reporter's eye check (D237).

### 19.7 As built (ticket 0685, lead-0673, 2026-11-24) [C: the code and its tests; L: 19.10]

`server/src/units/srv_units_anim.{h,cpp}` and one line in `server/src/units/srv_units_scene.cpp`:
- **WalkFacings** keeps per unit a drawn yaw. `follow` sets it at a create and a `place` (at once), and records a `turn`'s
  side (+1 left, -1 right; an `arrive` turns the short way). `advance(dt, rateOf)`, called by `AnimRunner::apply` right
  after `follow` with the frame's `TimelineFrame::dt`, moves every unit's drawn yaw towards `facingYaw(facing)` by
  2 pi x rate x dt (dt clamped to [0, `kWalkTurnMaxDt` 0.1 s]); exactly opposite (180 degrees) goes the turn event's side.
  `turnYaws()` holds the units still turning this frame; `turnCounters()` the turns started, the turned frames and the
  longest finished turn in ms. A unit created without a facing (-1, 6, 7) takes its first facing's yaw at once.
- **The rate** *(corrected 2026-11-25, 19.10: was `WalkTurnRate(TurnRateMin, TurnRateMax)` in turns per second, default
  0.75, 5-8 times too slow, 19.9)*: `WalkTurnRate(TurnFacingRateMin, TurnFacingRateMax)` = (Min + 0.5 (max(Max, Min) -
  Min)) / 2 pi turns per second, the two read as radians per second; `kWalkTurnRateDefault` 17.5 / 2 pi (about 2.785)
  for a result not above 0 or not finite. `AnimRunner` asks it once per unit (cache by `UnitKeyOf`,
  cleared at `open`) from the unit's first member's `CombatMotion` through `Controller::combatFigures` on the combat tables
  it reads at `open`; without the tables the default.
- **The draw**: `MergeWalkTurns(WalkFacings, BuiltFrame)` (and `AnimRunner::mergeWalkTurns`) puts each turning unit
  into `BuiltFrame::xforms` with `heading` and its drawn yaw; `UnitsHost::animFrame` calls it after `fillFigures`, and
  `ApplyUnitXforms` (4981) turns the prepared unit rigidly about its root after `prepare`. An extra's heading composes
  (its yaw + drawn yaw - `facingYaw(facing)`, scale and lift kept). A unit drawn at another facing than the turn's target
  (a fight's view, a record changed at rest) is left as placed. Each key once over the three wrap copies.
- **Switch**: `C5_RENDER_UNITS_WALK_TURN=0` in the server's environment (read at `AnimRunner::open`): nothing turns,
  19.2's frames. Default on.
- **Rule A** (offline-scene-harness.md 25): no new input. The turn reads the timeline's events and `dt`, both from the
  frames the bundle already records, and the player (`sceneshot`) runs the same `AnimRunner`, so it replays the turn. The
  test switch is not recorded: a bundle made with it set to 0 replays with the turn on.
- **Cost** [I]: `advance` walks every followed unit once per frame (a `std::remainder` per unit, a few thousand units on
  a huge map); a turning unit costs one `combatFigures` call in its life and one `UnitXform` per frame. Not measured.

Tests (`server/tests/units/test_scene_units.cpp`, 86 cases; was 83): `walk_turn_smooth` (60 degrees left at 60 fps and
0.75 turns per second: 4.5 degrees a frame, 13 turned frames, the 14th at the facing, 233 ms counted; 120 degrees right in
26 turned frames; the arrival turns the short way), `walk_turn_edges` (180 degrees by the event's side both ways, a
teleport sets the yaw, a 5 s step turns 27 degrees, remove forgets, a 1.1 turns per second rate from the function, a rate
below 0 gives the default, off: nothing turns and the facing still switches), `walk_turn_merge` (one entry for two wrap
copies, an extra's heading composes, a unit at another facing is left).

Results 2026-11-24, workstation, main 4aa0b8aa4 + this change, `bash server/evidence/0685/allb0685.sh 1|2|3|4` (runbook
2.5), `C:/c5-64bit-work/0673/allb0685`: server.exe `67ed2d2aeda56c0a...` (release, 0.1b pins, 264 s); render ctest 108 of
109 (`c5r_bakeresidency_test` under builder load; 3 of 3 alone passed, render code not changed); `test_scene_units` 7
pytest passed, run directly 86 of 86 in 5 of 6 full runs (the sixth `feed_out_of_order`'s `live_feed_no_race`, the load
flake of runbook 2.5; the walk and turn cases passed in every run); units extras 13 of 13; unitfeed, trigger scene,
effect routes, trigger emitter, units fights, queue, records and seed, farm fields and unit shadow built and passed; rule
A run_all: render-host 209, scenebundle-programs 4, scenebundle-py 29, offline-equals-live 17 passed, 0 failed, 0 skipped.
ASan (MSVC `/fsanitize=address`, its own ASan c5render): `test_units_runner` 7 passed in 835 s (every case passes), the
three new cases alone 0 reports. Not yet: the laptop pass 3 against stock with more repeats, the reporter's eye check (D237).

### 19.8 Laptop pass 3 against stock: the product's facing change is about 300 ms late (lead-0673, 2026-11-24) [L; I where marked]

*(This corrects 19.6's premise that stock turns a walking formation at `TurnRate`, 60 degrees in 171-233 ms: those
numbers were the noisy axis reader of pan-coherence.md 5.4 pass 3; by eye on full-size frames stock turns in about 2
frames.)*

- **What ran.** Laptop (TB-HUGE_0350 t350, EXE 4390913, 30 fps cap, 3825 x 2087 client), 2026-11-24 21:23-21:40 EST,
  `acc0685_laptop.sh` (runbook 4.15) with `REPEATS=3 EXTRA="--walk-crop 1600x1100 --walk-order path --walk-bends 60"`;
  jobs `20261124-212329-1ce0` stock and `20261124-213150-388d` product (`ship0685`: server `da1617d9...`, this change
  on the merged head). Spawned spearman / horseman 17542 from A 105,23 by B 104,23 to C 104,24 (leg 1 west, 180 degrees;
  leg 2 north-west, 120; the root's own heading turns 180 -> 120 over the curve), every walk ended on C facing 5, the
  game on screen (a stock frame checked; a probe and a read-only foreground check before: desktop in front).
- **turn0685.py's numbers do not decide it.** The axis reader gave stock foot 0-36 ms and product foot 733-752 ms
  windows, and "axis changed < 10 degrees" for all six mounted walks: the formation's principal axis does not follow the
  facing reliably at this crop. Read instead by eye on full-size frames (`out/raw/walk*_r1`, fetched with `tb.py fetch
  --include`), crops around the root every frame.
- **Stock [L, r1 by eye].** The whole formation goes from west (shield fronts / horses' flanks toward the camera) to
  north-west (backs, rumps) within about 2 frames (horseman: +540 -> +607 ms after the order; spearman: +742 -> +801).
  The moment matches 4772's 30 degree rule run on the EXE's own positions (walk.json, `LocalMachineUnitPositionChanged`):
  the root's heading passes 150 degrees at +403 ms of the mounted walk (+577 after the order) and +564 ms of the foot
  walk (+868; stock's figures turn about 100 ms before that, members following their own paths [I]).
- **Product [L, r1 by eye].** Spearman: the formation faces west until about +990 ms and is north-west at +1087
  (the 30 degree rule predicts the event at +915; then the 222 ms turn): about 300 ms after stock. Horseman: west through
  +795 ms, turned between +827 and +894, the arrival (+894): about 300 ms after stock, though the rule on the product's
  own EXE positions predicts the event at +551 ms. Why the mounted turn waits for the arrival is **not established** *(settled 2026-11-24 by 19.9: it does not wait; it
is the same slow turn, started from a 120 degree first turn)*:
  the server logs no turn events (candidates: the feed's walk samples behind the EXE's positions; `MergeWalkTurns`
  leaving the unit as placed; the turn event not given before the `arrive`).
- **What it means for 0685 [I].** Two differences, not one: (1) stock's turn is fast, about 60 degrees in under 70 ms,
  so 19.6's rate (`TurnRate*`, 0.5-1.0 turns per second, 167-333 ms for 60 degrees) is too slow; `TurnFacingRateMin/Max`
  (horseman 15 / 20) read as radians per second would give 52-70 ms, a fit not checked against the EXE; (2) the
  product's facing event comes late at least for the horseman. Before 0685 the product snapped at the same late moment
  (pass 3 of 2026-11-23's strips show the same west-facing spearmen through the bend), so (2) is older than this
  change. 0685 is not merged; not offered for the beta 4 cut (lead-0673 #13406).
- **Next.** A diagnostic line per walk turn and arrival (unit, facing, side, frame time) behind an environment switch,
  one product rerun of the r1 walks on the laptop, and the stock timing per frame on the full-size frames for 2-3 more
  walks; then the rate (TurnFacingRate, or the EXE's measured speed) and the event's timing fixed together.

### 19.9 The diagnostic rerun: the lateness is the rate, at the walk's first turn and at the bend (lead-0673, 2026-11-24) [L; I where marked]

*(This settles 19.8's open cause: the horseman's turn does not wait for the arrival; it is the same slow turn as the
spearman's, started from a 120 degree first turn.)*

- **The diagnostic** (commit f076bb244, off by default): `C5_RENDER_UNITS_WALK_LOG=1` in the server's environment (read at
  `AnimRunner::open`) logs `units: walk (0685): <kind> unit P:ID a b c dt | facing F side S walking W yaw Y goal G
  turning T | qpc_ms Q` for each move / turn / arrive / place / remove of a unit `WalkFacings` follows, and one
  `turning unit` line per frame while its drawn yaw turns (`WalkFacings::describe`; yaws in degrees, the placement's
  150 - 60 d). On the laptop pass it through the job: `acc0685_laptop.sh` `ARMS=product PENV="C5_RENDER_UNITS_WALK_LOG=1"`
  (tb.py `--env`; the server inherits it), runbook 4.15.
- **The run.** Laptop 2026-11-24 23:33-23:40 EST, job `20261124-233311-ea4b` (product only, `ship0685b`: server
  `b638a14e...`, proxy `9a893d0e...`, composite `18834f66...`, built from the merged head 6118509ad), the same walks
  (60 degree bends, 2 repeats: units 17542 foot, 17543 mounted, 17544 foot, 17545 mounted). 102 lines.
- **What the log shows [L].** Every walk the same way: at the `move` the unit stands at facing 2 (south-east, yaw 30);
  the first step (about 100 ms later) gives `turn 4 right` (west, yaw -90): 120 degrees, at 0.75 turns per second
  (spearman) 444 ms, at 0.625 (horseman) 533 ms; the bend's `turn 5 right` (north-west, yaw -150) comes 538-540 ms
  after the move for the spearmen (yaw then -98.8 / -99.0: the first turn just done) and 362-367 ms after for the
  horsemen (yaw -60.1 / -60.4: still 30 degrees short of west); the horseman then turns 90 degrees, 400 ms, ending
  about 870 ms after the move: the "turn at the arrival" of 19.8 (arrive 1,031-1,040 ms after the move). Through the
  first turn the formation sweeps south-east -> south-west -> west, facing the camera: the shields of 19.8.
- **Stock at the same moments [L, r1 by eye, 30 fps].** Before the order stock's spearmen also face south-east
  (+168..+304 ms); at +335 (the first move, +304) they face west: 120 degrees within 1-2 frames. At the bend about 60
  degrees within 2 frames (19.8). Stock turns about as fast as a 30 fps recording resolves, at the walk's start and at
  its bend; its timing of the facing (the 30 degree rule on the EXE's positions) is the product's.
- **What it means [I].** The product's lateness is 0685's rate alone, 5-8 times too slow, not the event timing and
  not the arrival. A rate that matches: at least about 2 turns per second for 60 degrees in 2 frames, about 4 for 120
  in 1 frame. `TurnFacingRateMin/Max` (EXE defaults 15 / 20, also for NULL rows, unit-combat-server.md M1) as radians
  per second gives 17.5 rad/s, about 2.8 turns per second: 60 degrees in 60 ms (2 frames), 120 in 120 ms (3.6 frames,
  slower than stock's 1-2). Which rate the EXE uses for a walking formation is not established (the consumer of
  `TurnFacingRate*` is open, 19.1); a 30 fps recording cannot tell 60 ms from 20 ms. Before 0685 the product snapped
  (19.2), which by these frames was nearer stock than 0685's turn is.

### 19.10 The rate fix and laptop pass 4: the turn and the walk's timing match stock (lead-0673, 2026-11-25) [C; L; I where marked]

- **The change** (commits 0147c1328 and 7fcbd2920 on `claude/lead-0673-holistic-d0fd9d`) [C]: `CombatMotion` gains
  `turn_facing_rate_min` / `_max` (defaults 15 / 20, the EXE's); `combat_tables.cpp` reads `TurnFacingRateMin` /
  `TurnFacingRateMax` of `ArtDefine_UnitMemberCombats` in their own query after 4777's (NULL or not above 0 takes the
  default, a Max below Min is raised to Min); `WalkTurnRate` takes them as radians per second (19.7). VP 5.4.8's
  database has NULL or 15 / 20 in every row, so every walking unit turns at 17.5 rad/s: 60 degrees in 60 ms, 120 in
  120 ms. Tests: `WalkTurnRate(15, 20)` 2.7852115, `(20, 15)` 3.1830989 (Max raised), `(0, 0)` the default; the
  mechanics cases keep pinning 0.75 turns per second by an explicit rate.
- **Correction (2026-11-25).** 0147c1328's commit message says the all-builders phases 1 and 2 passed; they did not:
  the units tests' TSV tables and older scene bundles have no `TurnFacingRate*` columns, the query failed and with it
  the whole combat-table load, and 12 fight cases of `test_units_runner` failed (`allb0685c` / `allb0685d` phase 2).
  7fcbd2920 treats a failed query as absent columns (the defaults, no error). Rebuilt 2026-11-25 04:18-04:50 EST,
  `bash server/evidence/0685/allb0685.sh 1|2` with `C5_0685_ALLB=C:/c5-64bit-work/0673/allb0685e`: render ctest
  passed (0 skipped), server release on the 0.1b pins (222 s, server.exe `1069cd62adea...`), `test_units_runner` 7
  passed (the fight cases included), units extras 14 of 14, unitfeed, trigger scene, effect routes, trigger emitter,
  units fights / queue / records / seed, farm fields, unit shadow OK; `shell/build.cmd` BUILD OK (composite
  `3893ddcb3664...`, proxy `b132c039fa29...`).
- **Laptop pass 4** [L], 2026-11-25, `ship0685c` (the build above), Huge save copy, Quick Movement off, the walk case
  with `--walk-crop 1600x1100 --walk-order path --walk-bends 60`, 3 repeats, foot and mounted: stock job
  `20261125-052406-85ba` (05:24-05:32 EST), product `20261125-053222-d822` (05:33-05:41, walk log on). Full-size frames
  fetched (about 2.9 GB an arm) to `C:/c5-64bit-work/0673/jobs_f/` (moved to `F:/c5-64bit-work/0673/jobs_f/` on 2026-11-25 for disk space).
- **The log** [L]: every product walk turns at about 1,000 degrees a second (for example yaw -25.7 to -79.1 in a 53.3 ms
  frame): the first 120 degree turn (facing 2 to west) ends about 120 ms after the first step, the bend's 60 degrees
  about 60 ms after its `turn`. Note: `turning unit` lines are written only on frames whose `apply` has unit events, so
  a turn's last frames may have no line and a unit can read `turning 1` until its next event; take a turn's end from the
  yaw and the rate, not from the next `turning 0`.
- **By eye** [L, every frame at the bend, foot r2]: no slow pivot is left; both arms change direction within a few
  frames. Stock's members string out along the path and turn one by one as each reaches the bend; the product's
  formation stays bunched and turns together (below).
- **The walk's timing** [L, `arrive0685.py`, share of pixels (grey, over 25) changed from the order's frame in a
  320 x 240 px box at A and at C, full-size frames]: leave A = 50 % of the plateau at A, reach C = 50 % at C, settle =
  50 % -> 90 % at C, ms after the order:

  | case | stock: leave A, A -> C, settle | product: leave A, A -> C, settle |
  |---|---|---|
  | foot r1 | 398, 1,118, 399 | 208, 1,000, 67 |
  | foot r2 | 201, 935, 534 | 402, 949, 118 |
  | foot r3 | 215, 899, 567 | 375, 933, 100 |
  | mounted r1 | 337, 629, 267 | 374, 616, 82 |
  | mounted r2 | 209, 634, 265 | 418, 570, 94 |
  | mounted r3 | 288, 614, 200 | 272, 583, 67 |

  A -> C: foot stock 899-1,118, product 933-1,000; mounted stock 614-634, product 570-616. The product is no longer
  late (19.8's 300 ms is gone with the rate). Leaving A varies from repeat to repeat in both arms (the order's latency).
- **What is left [I].** Stock's formation arrives spread out (settle 200-567 ms) and the product's bunched (67-118 ms):
  stock's members keep their own pace through the bend, the product's move as one block. This is the formation's
  member spacing on the move, not 0685's turn; not a ticket yet (a note for the D237 eye check). The EXE's own consumer
  of `TurnFacingRate*` is still not established (19.9); a 30 fps recording cannot tell 60 ms from a 1-frame turn.
- **The merged head and rule A [L] (2026-11-25).** Branch head 4bd9f58ff (main 2b4c93846 with 0743's melee reach, an
  additive change beside the rate in `combat.h`, `combat_tables.cpp` and `srv_units_anim`) rebuilt into `allb0685f`
  09:23-09:41: render ctest, server, units 7 passed, extras 14/14, every builder and the shell OK. Rule A
  (`allb0685.sh 3`, 10:54-11:41): render-host 231 passed, scenebundle-programs 4, scenebundle-py 33,
  offline-equals-live 17; 0 failed, 0 skipped.

### 19.11 The reporter's D237 eye check of 0685 (lead-0673, 2026-11-25) [L: the reporter's eyes; I where marked]

Main fed21d8bb's code (`allb0685f`, server.exe `2f618ff766eb...`), c5h-a (EXE 4390913) visible, guard and cloak off,
parked at 0,0, uncapped, the save copy HUGE_0350 at turn 350, autoplay off, `[Renderer]` ServerRenderer, ServerUnits,
ServerCities, ServerLandmarks, ServerFarmFields, ServerCombat = 1, ServerArc = ServerPath = 0 (runbook 4.20, `d237.sh`;
work folder `F:/c5-64bit-work/0673/eye0685`). Played 19:43-19:59 EST; the harness's stop, restore by hash, verify clean.
Verdict: **better than before, not yet stock.** Four observations:

1. **Units still snap their rotation noticeably on the plot just before the turn.** [I] The same cause as 4: the server's
   formation walks plot centre to plot centre and turns at the corner, now at about 1,000 deg/s (19.10), so the turn reads
   as a snap; stock's root path is a smooth curve through the bend (19.0, 19.4 [L]), so its heading changes over about
   60 % of a two-plot walk.
2. **Cavalry seem not to play the walk on straight moves either: the horses' legs are still and the model glides.**
   *(Explained 2026-11-25 by 19.12 [C]: the machine does enter the run, but the evaluator held the run clip at its
   entry time for the whole walk; not 0685.)* [I, was] Candidates: the mounted state machine (`1_omni_mounted_state_01`, `unit-controller.md`) not entering
   its run state (1400) for a server walk, or the run's blend weights; 19.4 and 19.8-19.10 judged facing only, never the clip.
3. **Armour (the landship) turning: the treads seem to come apart from the model for a moment.** Not explained here.
4. **Units do not follow stock's path.** Stock makes one big arc through a turn; the product goes from the centre of one
   plot to the centre of the next. On the landship the treads follow the bigger, smoother outer arc (as the reporter
   believes stock does for every unit) while the hull goes centre to centre. [I] That the treads follow the arc points to
   a part of the landship drawn on the EXE's curved path (the EXE's own records, 19.4) while the server places the
   formation by plot centres; which part, and whether the EXE still draws it, is not established.

What 0685 changed (the rate) holds; 1 and 4 are the path itself (the curve and members following it one by one, 19.0),
which 0685 did not take on. 2 and 3 are new. Follow-up tickets: the MC's (thread 0673, 2026-11-25).
*(Corrected 2026-11-25 by 19.13: the server's root already walks the EXE's curve; what goes "centre to centre" is the
formation's yaw, held on the discrete hex facing. Observation 2 is ticket 0782, 3 is 0784.)*
*(2026-11-25, 0784, 19.14: observation 3 and the treads of observation 4 have one cause: the landship's moving treads
are a walk-clip FX model effect the EXE draws at its own hidden figure, not a part of the server's model.)*

### 19.13 Ticket 0783: the walk's yaw along the EXE's curved root path (impl-0783, 2026-11-25) [C; L where marked; I where marked]

Written by impl-0783 (account B, branch `impl-0783` from main 899b3d1e8), 2026-11-25 20:50- EST, for lead-0673 (D264: a
beta 5 gate). Design posted before code (#15997), GO from lead-0673 (#16012) and the MC (#16046).

**Where the server's walk positions come from [C].** The product runs position mode exe (`render_units_positions=exe`,
written by the proxy with `ServerUnits = 1`: `proxy_shell.cpp` `kUnitLines`). `UnitsCore::buildList` puts each unit's
root at its newest EXE position record (`sceneRoot`, 17.2: `LocalMachineUnitPositionChanged` through the Lua bridge, one
per EXE frame) and `placeUnit` lays the formation's slots about that root (`render/src/units/placement.cpp` 230, the root
replaces the plot centre). So **the server's root already walks the EXE's curve**, not plot centres. The formation's
**yaw** was the discrete hex facing: 4772's 30-degree switch (19.2), turned in about 60 ms by 0685 (19.10). Through a
bend the whole formation therefore kept leg 1's direction while its root swung round the arc, then pivoted about the root
in about 2 frames where the root's heading passed 30 degrees: the owner's [reworded for privacy] observation of a snap before the turn (19.11 observation 1)
and of the body not following the curve (observation 4: a body pointing along the polyline's leg while it moves on the curve). A one-figure
unit (the landship) slid sideways along the arc with its hull on leg 1's direction, then pivoted.

**Stock's root path through a 60-degree bend [L].** Laptop pass 4's walks (19.10; the EXE's positions kept in Lua,
`walk.json`; 6 stock and 6 product walks, foot and mounted, A 105,23 -> B 104,23 -> C 104,24, leg 1 west, leg 2 north-west,
a right turn), read by `server/evidence/0783/rootpath0783.py` (`run_rootpath.sh`), 2026-11-25:

| quantity | stock (6 walks) | product arm (6 walks, the same EXE) |
|---|---|---|
| positions (EXE frames) | foot 34-35, mounted 24-25 | foot 32-34, mounted 20-24 |
| length | 119.3 world units = 1.86 plots (the centre-to-centre polyline: 2.00) | the same |
| nearest to B | 12.0-12.2 (0.188-0.190 plot) | 12.0-12.1 |
| largest distance off the A-B-C polyline | 9.5-10.2 (0.148-0.160 plot) | 9.7-10.3 |
| heading leaves leg 1 / reaches leg 2 | 9-13 % / 82-85 % of the walk's steps | 10-13 % / 83-85 % |

The step headings change smoothly (at most about 3 degrees per EXE frame on foot, 5 mounted). **The curve's shape [L, a
fit]** (`fitcurve0783.py`): a straight line from A to the point 3/4 plot before B, a quadratic Bezier with control point
B to the point 3/4 plot after B, then straight to C; the largest distance of any EXE position from it is 0.41 world units
(0.006 plot) in every walk (a cubic with its inner points at 7/8 plot and 3/4 of the way to B fits to 0.25). Foot and
mounted walk the same curve; only the speed differs. How the EXE builds it is not read from the code [I]; the 120-degree
bend has no stock reference yet (laptop pass, below).

**What the landship shows [C from impl-0784 #15998; I for the prediction].** The landship's moving treads are not the
server's: they are an FX model (`WW1_Tank_Treads.fxsxml`) the EXE starts from the Run clip's timed triggers at the bone
`FX_Treads` of its own hidden figure, so they ride the EXE's own figure through the bend, while the server's hull kept
leg 1's yaw: "the treads on the outer arc, the hull centre to centre". Both meshes of a server figure share its palette
block and pose (`draw.cpp` 379-429), so nothing of the server's model came apart. Prediction: with the yaw on the
root's tangent (below) the one-figure landship's hull lines up with the EXE's treads through the bend, up to the EXE
figure's own lag behind the root (a few world units, a few degrees) [I]; impl-0784 checks it live on this build.

**Does stock's drawn formation trail the root record? [L, offline; an indication] (lead-0673 #16106, answer #16136).**
`server/evidence/0783/lag0783.py` (`run_lag.sh`): per frame of pass 4's colour walk frames, the walker's foreground
centroid (turn0685.py's `centroid_track`) taken back to the ground through the camera's A/B/C affine, its arc length on the
EXE's root path against the newest root record's arc length at the frame's time, the rest offset at A subtracted.
Medians, world units / ms behind the record: stock foot 49.8 / 480 (r1), 21.6 / 230 (r2); stock mounted 13.7 / 86,
10.8 / 103; product (drawn at the newest record) foot 12.5 / 128 (r2; r1's rest centroid failed), mounted 14.8 / 124,
18.0 / 134. The metric carries about 100-130 ms of record-to-screen latency (the product shows it); against that,
stock's mounted formation trails no more than the product's, stock's foot formation 9-35 units more: the strung-out
column's centroid behind its leader, not one fixed delay. So a uniform time or distance trail on the server would make
mounted units worse (and delay the arrival, whose timing already matches, 19.10); none is built. Noise about 10 units.
impl-0784 saw the landship's EXE-drawn treads 25-30 units behind the product's hull for about 1 s (#16098): a one-figure
vehicle's EXE figure may trail more; measuring it per frame on a landship walk is 0784's.

**The EXE's per-figure positions are not readable today [C from the knowledge base] (answer #16044 to lead-0673's
#16003).** No feed record carries them; the walk's per-member mover is not located in the EXE (19.1); the only
per-figure reads that exist are diagnostic outside-process samplers of the combat simulation (`unit-combat-server.md`
18.15). Placing each server figure at the EXE's figure (lead-0673's option A, which would also give stock's strung-out
column) needs that structure found on both EXE builds, a proxy read and a new feed record: a later ticket.

**The fix as built [C: code and tests]** (`server/src/units/srv_units_anim.{h,cpp}` only):
- `WalkFacings::track(rootOf)`, called by `AnimRunner::apply` after `follow` and before `advance`, samples each walking
  unit's root (`UnitsCore::rootOf`: the root this frame's build draws) by 4772's rule (a step of `kWalkMinStep` 1 or more,
  a jump over `kWalkMaxStep` 256 starts the sample again); the step's heading + 90 degrees (the placement's yaw
  convention) is the walk's **tangent yaw**. A new walk (a `move` of a unit not walking) samples afresh; `arrive`,
  `place`, `create`, `remove` end it.
- `advance`: while a unit walks and has a tangent, its drawn yaw's goal is the tangent instead of its facing's yaw, at
  most 0685's rate a frame (`TurnFacingRate`, 17.5 rad/s): on a curve the yaw is on the tangent every frame; the turn from
  the standing facing to leg 1 is 0685's. After the arrival the goal is the record's end facing (= leg 2 on a hex path:
  no turn). `turnYaws()` now holds every unit drawn at a yaw other than its facing's (turning, or on its tangent);
  counters `tanSamples`, `tanFrames`; a tangent step within the frame's rate is not counted as a turn. The discrete
  facing, the `turn` / `arrive` events and the controller are unchanged.
- **The yaw is laid in the placement**, not turned after `prepare`: `MotionPlacement` (the runner's placement source,
  4724) turns the placed figures about the root by (drawn yaw - placed yaw) with `applyFigureOffsets` (each figure's height
  taken again at its new spot, as a fight's offsets; FixUnitPlacement's foot radius is not re-applied there), figure
  and unit yaw set to the drawn yaw (`walkYaw`, counter `AnimRunner::walkYawPlaced`). A unit a fight names or moves, or
  drawn at another facing than the yaw's, is left as placed. `MergeWalkTurns(.., placedYaw = true)` (the runner's own
  `mergeWalkTurns` when its placement source exists) then only composes an extra's own heading (ApplyUnitXforms turns by
  x.yaw - placement.yaw, the placement's yaw being the drawn one); 0685's xform route stays for a host without it.
- **Switch** (diagnosis only, no player key; the MC #16046): `C5_RENDER_UNITS_WALK_CURVE=0` in the server's environment
  (read at `AnimRunner::open`): no tangent, 0685's frames. The walk log (`C5_RENDER_UNITS_WALK_LOG=1`, 19.9) gains
  `tan <deg>`.
- **Rule A** [I until run]: no new input; the tangent comes from the roots the bundle's records give, and the player runs
  the same `AnimRunner`, so a replay draws the same yaw. Default pictures of walking units change (their yaw in bends and
  on every non-hex-aligned stretch of a curve).
- **Expected numbers** (`server/evidence/0783/simcurve0783.py`, the fitted curve sampled at the laptop's speeds): the
  largest yaw change per frame at 30 fps through a 60-degree bend 2.8 degrees (foot) and 4.0 (mounted); through a
  120-degree bend, if the EXE walks it as one curve of the same shape, 14.3 and 19.9 (10.2 mounted at 60 fps).

**Builds and tests [C]** (2026-11-25, workstation, `bash server/evidence/0783/allb0783.sh 1|2|4`, work folder
`F:/c5-64bit-work/0783/allb`, branch impl-0783 on main 899b3d1e8 + this change): phase 1 render (build) and server release
on the 0.1b pins OK (717 s; server.exe `b532b491...`); phase 2 every builder OK: units build, `test_units_runner` 7 passed,
the walk cases run directly 6 of 6 (`walk_curve` new; `walk_turn_merge`, `walk_heading_host` extended), units extras 14 of
14, unitfeed, trigger scene, effect routes, trigger emitter, units fights / queue / records / seed, farm fields, unit
shadow. Phase 4 (MSVC `/fsanitize=address`): the ASan units build failed on main's own `test_scene_units_walkopen.inc`
(`std::pair<float, float> prev(0, 0)`: C4244 under /WX, since 5066 of 2026-11-24); fixed here with float literals; then
the pytest wrapper's 900 s timeout ran out under load (`test_every_case_passes`), and the ASan exe run directly
(`asan_direct.sh`) passed **108 of 108, 0 AddressSanitizer reports**. Composite `83da6854...` and proxy `9957536c...`
(`ship0783.sh`), SHIP `F:/c5-64bit-work/0783/ship` for the laptop (runbook 4.15).

**On main's VP 5.5 pins, and rule A [C; L for rule A]** (2026-11-26): the branch merged main eaf2bcaaf (0765's pin move;
main had meanwhile taken the walkopen.inc line as 7a8483afd) at 1c69a2972, relinked on the shared
`C:/c5-64bit-work/keep/pins-5.5` (made for every branch here, `pins55.sh`, runbook 1.1): phase 1 server/proxy OK, phase 2
units 7 passed and every builder OK (01:35-02:13). Rule A on MC-B's go (#16635), `allb0783.sh 3`, 03:06-05:28,
`F:/c5-64bit-work/0783/allb/ra`: render-host 235 passed, scenebundle-py 34, offline-equals-live 18, 0 failed, 0 skipped;
scenebundle-programs: recorder_test 268/0 and sources_test 24/0 passed, `sceneshot_test` (1500 s) and
`bundle_format_test` (600 s) timed out under the night's load (several teams' suites at once): NOT A RESULT, no case
failed; their quiet rerun is W169's.

**Laptop pass [L; pictures DIAGNOSTIC]** (2026-11-26 07:14-07:36 EST, MC-B's grant #16152/#17018; runbook 4.15 with
`server/evidence/0783/acc0783_laptop.sh` / `purity_job.py`). tb1 is VP 5.4.8 (MC-B #16694), so the product ran a SHIP built
from the branch's pre-5.5 commit 75265921f (the same 0783 code) on `keep/pins-0.1b`: `F:/c5-64bit-work/0783/ship548`
(server.exe `f47c7912...`, x64 DLL `b84aa3e9...` = pins-0.1b's, proxy `08bbc410...`, composite `c0bbf30b...`), the
purity list for that DLL (`derive_purity.py`, 169 rows) through `VP_PROXY_PURITY`: vpproxy.log `purity: loaded`.
Stock job `20261126-071550-c44d`, product `20261126-072709-eaa5` (walk log on), TB-HUGE_0350, Quick Movement off,
foot and mounted, 60-degree bends (A 105,23 -> B 104,23 -> C 104,24), 3 repeats each, rc 0, fetched to
`F:/c5-64bit-work/0783/lap`. **A Windows Security Alert (Defender Firewall) stood over the walk crops in both arms**, so
no picture of this pass counts (MC-B #17057: the picture judgement is the owner's D237 check). The gate numbers come from
the server's walk log, which the dialog cannot touch (`run_walklog.sh` / `walklog0783.py`, per-line CSVs in
`F:/c5-64bit-work/0783/lap/csv`; one log line per drawn frame of a walking unit):

| case | lines | start turn: largest step (deg) | bend: largest yaw step per frame (deg) | root to stock's path, largest (world units / plot) |
|---|---|---|---|---|
| foot r1 | 52 | 60.1 | 3.1 | 0.06 / 0.001 |
| foot r2 | 54 | 34.5 | 3.2 | 0.02 / 0.000 |
| foot r3 | 54 | 33.6 | 3.1 | 0.02 / 0.000 |
| mounted r1 | 31 | 55.3 | 4.9 | 0.03 / 0.000 |
| mounted r2 | 30 | 61.8 | 5.3 | 0.05 / 0.001 |
| mounted r3 | 30 | 56.1 | 5.1 | 0.04 / 0.001 |

So through the bend the product's facing turns 3-5 degrees a frame (the ticket's limit 20; 0685 turned 33 a frame there)
and its root stays within 0.001 plot of stock's root path (the limit 0.1): both acceptance numbers met in all 6 walks. The
yaw runs the tangent from leg 1's -90 to leg 2's -150 smoothly (foot r1: -91.2, -92.5, ..., -149.8, -150.0 over lines 4-33);
the arrival changes nothing. The start turn (the standing facing 30 -> leg 1's -90, 120 degrees, before the bend) is still
0685's rate turn, up to 62 degrees in one (long) frame, as stock's 1-2-frame turn there (19.9). The stock arm's root paths
repeat pass 4's to the hundredth (1.86 plots, 0.188 plot from B, 0.152-0.158 off the polyline). Not run: the armour arm
(lead-0673 #16138; a picture check of the treads, blocked by the alert; the treads are dropped by 0784's fallback C, D268)
and the 120-degree bend (the walk case's `path` order cannot make one, 4.15's trap).

**Named difference left (not this ticket):** stock's members string out along the path at their own pace and re-form at
C (19.4, 19.10's settle 200-567 ms against the product's 67-118). Geometry alone is small: with the yaw on the tangent a
rigid member 15 world units from the root stands within about 1.5 units of the path a trailing member would take (the
corner's radius about 80 units). Per-member EXE positions (option A above) are the way to it.

**Not verified (0783):** the look by eye (the laptop's pictures were under the firewall alert; the owner's D237 hand check
decides); a 120-degree bend live (no stock reference can be ordered as one walk; on the fitted curve the yaw would change
at most 14-20 degrees a frame at 30 fps, `simcurve0783.py`); the landship's hull against the EXE-drawn treads (0784, now
dropped by fallback C); ASan units on the 5.5-merged head (phase 4 ran on the 0.1b-pin head, the same units code);
`sceneshot_test` and `bundle_format_test` in a quiet window (they timed out under load in rule A); a walk on hills (the
figures' heights are taken again at their turned spots, `applyFigureOffsets`, but FixUnitPlacement's foot radius is not
re-applied there); AI walks and walks of more than 20 plots.

### 19.14 The landship's treads are an EXE-drawn walk effect at the EXE's own figure (ticket 0784, impl-0784, 2026-11-25) [C; L where marked]

**The files [C]** (read at run time from the c5h-c test instance's install and its MODS folder, by bare name as the
game resolves them: `logicfiles.py`, `gr2read.py`; scripts outside git, `F:/c5-64bit-work/0784/scripts`; the database copy
`F:/c5-64bit-work/0327/db/Civ5DebugDatabase.db`):
- The landship is `UNIT_WWI_TANK` -> `ART_DEF_UNIT_WW1_TANK` (formation `TwoBigGuns`), 2 x member
  `ART_DEF_UNIT_MEMBER_WW1_TANK` (`WW1_Tank.fxsxml`, Scale 0.11, Expansion2Models.fpk). Its model `ww1_tank.gr2` has
  **one mesh** (the hull, tracks moulded in; skinned, `UnitShader_Skinned`) on a 12-bone skeleton `point_WORLD` ->
  `tank_bone_main` -> `tank_bone_body` -> {MG C/L/R, `AIM_DR`, `FX_Treads`, two sponson turrets and guns}. That is the
  part the server draws.
- **The moving treads are not in that model.** The member's trigger file `FX_Triggers_WW1_Tank.ftsxml` starts, in the
  Run clip (1400, 0.667 s, looping) and the charge clips (1120 run, 1140 attack), two Effect triggers on bone
  `FX_Treads` from time 0 for 0.7 s (1140: 1 s): `ART_DEF_VEFFECT_TANK_TREADS` (Transform translation y -1, scale 1.01)
  and `ART_DEF_VEFFECT_TANK_TREADS_02` (no offset, scale 1.01), both with `fBaseDuration` 0.02 and the one model effect
  `ART_DEF_MEFFECT_TANK_TREADS` (`Civ5ArtDefines_VisEffect*_Expansion.xml` and `_Inherited_Expansion.xml`, Expansion
  and Expansion2 `Effects` folders) = `WW1_Tank_Treads.fxsxml` (fScale 1.0, **fScrollTime 1.0**): `ww1_tank_treads.gr2`,
  one rigid mesh on one bone, material `FXShader`, textures `FX_UV_LeftToRight_01.dds`, `WW1_tank_treads.dds`,
  `white_alpha.dds`: a tread shell 1 % larger than the hull's tracks with a scrolling texture, the links that move while
  the tank drives. The same clips also start dust and dirt trails on `tank_bone_main` (`TANK_DUST_TRAIL_SM`,
  `WW1_TANK_DIRT_TRAIL_MD`, `TANK_DUST_TRAIL_FRONT`; particles).
- **Only the landship has it**: of every `.ftsxml` of the install and mods, the only Effect triggers naming a
  `*TREAD*` effect are these six (3 clips x 2). Other walk clips (1400-1480) that start a VisEffect with a model
  effect, drawn the same way: helicopter gunship blades (1400-1450), jet fighter and stealth bomber engine fire (1400),
  mobile SAM rockets (1400, 1420), the Carthaginian African forest elephant's spear (1400-1480), the Mayan atlatlist's
  and the Zulu javelineer's spears (1400-1450), the Huns horse archer's arrow in the hand (1450).

**Who draws them in the product [C for the design, I for this unit until the crops].** A walk clip's effects are not
a fight's: the server's trigger stream carries only fight effects (effects-formats.md 15.1, last row: "they stay the
EXE's (Particle stays on)"), and the EXE keeps animating its own (hidden) unit, so its Run clip's triggers start the
tread shell and the trails on the EXE's own figure's `FX_Treads` bone (as 16.4 of unit-combat-server.md says for an
unarmed fight: "the EXE's sounds and particles at the EXE's (hidden) figures' places"). So **the hull is where the
server's rigid formation puts it (the EXE's root, the slot at the server's facing) and the moving treads are where the
EXE's figure is (its own member path and turn)**. On a straight leg the two agree; in a bend stock's members follow the
path one by one and turn on their own (19.4, 19.10), the server's formation turns as one block about the root: the
shell leaves the hull for the bend, on the EXE figure's (outer, smoother) path. That is the owner's observation 3 and the
part of observation 4 [reworded for privacy] about the treads following the wider outer arc [L: the owner's eyes; the crops below].

**The live pass [L; read by eye, DIAGNOSTIC] (laptop, MC-B's grant #16026, 2026-11-25 21:19-21:32 EST).** Runbook 4.15
with the new `--walk-kinds armour` (capture_job.py, this ticket): `acc0685_laptop.sh` with `ARMS="stock product"
PENV="C5_RENDER_UNITS_WALK_LOG=1" SHIP=C:/c5-64bit-work/0673/ship0685c REPEATS=3 EXTRA="--walk-kinds armour --walk-crop
1600x1100 --walk-order path --walk-bends 60"`; stock job `20261125-211906-5e49`, product `20261125-212555-f54f` (the
reporter's `[Renderer]` set, ship0685c = 0685's rate fix, 19.10); TB-HUGE_0350 t350, EXE 4390913, 30 fps; a spawned
landship (17542-17544, `UNIT_WWI_TANK`) from A 105,23 by B 104,23 (leg 1 west) to C 104,24 (leg 2 north-west, a 60 degree
bend), 3 walks an arm, every walk ended on C, camera verified, restore by hash ok in both. Scale at this zoom: A -> B (64
world units) is 741 px across, B -> C 408 px up for 55.4 units, so 11.6 px a world unit across and 7.4 up the screen.
Read on walk r1's full-size frames (every frame, crops `F:/c5-64bit-work/0784/zoom/`, never in git; ms after the order,
turn0685's clock; stock's first move at +242, product's at -131 by the EXE's positions):
- **Stock: the treads part from the hull too, in the bend only, briefly.** f36-f47 (+709..+1076 ms, the trailing
  member as the formation turns at B): the hull keeps leg 1's heading while the tread shells already point further
  round, sticking out past the hull's rear by about 50-100 px (4-8 world units) at about 20 degrees on the screen (about
  30 in the world) (`stock_f41_crop.png`, `stock_f44_crop.png`); then they line up again. On the straight legs the shells
  sit on the hull. *(So stock's shell follows the bone's position but turns ahead of the hull: it takes the unit's
  heading through the curve, the hull turns later [I].)*
- **Product: larger, longer and different in kind.** From f76 (+486 ms) to about f105 (+1,521 ms, past the arrival at
  +1,269), on leg 1, through the bend and on leg 2: the tread shells lie behind and beside the server's hulls, up to
  about 300-350 px past the hull's rear (about 25-30 world units, half a plot) and up to about 15 degrees on the screen
  off the hull (`prod_f82_crop.png`, `prod_f88_crop.png`, sheets `prod_r1_full.png`, `prod_r1_late.png`): the EXE's
  figures, where the shells are drawn, trail the server's formation on the straight leg as well. Before the first step the
  product's hulls face the pre-walk facing (south-east) and sweep round (19.9), while the shells are not yet drawn.
- **Verdict for the owner [L].** Stock's own parting is about 0.3 s, a few world units, in the turn; the product's is
  about 1 s, up to half a plot, on the straight leg too: worse than stock, so not stock behaviour. Its cause is the gap
  between the EXE's hidden figures (the shells) and the server's formation (the hulls), which is 0783's subject.

**Fix (lead-0673 #16002, plan A).** Nothing in 0784's own code: when 0783 puts each server figure where the EXE's
figure is (per member, position and facing), the EXE's tread shell sits on the server's hull again, and the same holds
for the other walk props above. Plan B (the server draws walk-clip model effects at its own figures and the EXE's copy
is stopped for a server-drawn unit; how to stop it is not in the knowledge base) only if 0783 finds the EXE's
per-figure transforms unreadable or unusable.

**Not verified.** The 120 degree bend (the walk case's `path` order cannot make one, 4.15's trap; `waypoints` never ran);
walks r2 and r3 (fetched in part, not read); the walk log of the product arm (`out/server.log`, not fetched: the laptop's
fetch ran at about 0.3 MB/s); whether the EXE's hidden figures in the product stand where stock's visible figures stand
at the same moment (the shells' place against stock's hulls was not measured: needed to know whether plan A alone gives
stock's look); how the EXE orients a lasting bone effect (the shell turning ahead of the hull in stock) is [I]; the
other walk props listed above were not looked at live. Numbers are read by eye from crops, not measured by a tool.

#### 19.14.1 The fallbacks if 0783 does not put the hull under the treads (lead-0673's read-only scoping, 2026-11-25 22:5x) [C where cited; I]

Scoped from the knowledge base and the source, nothing built or run (full notes outside git: `C:/c5-64bit-work/0673/scope0784.md`).
Addresses are old-build (4390913) VAs already in the knowledge base.

- **Stopping the EXE's own walk effect without a code write (D228).** Route S suppresses nothing per effect: it turns
  Combat off, so the EXE's figures never play fight clips [C]; walk clips are the Unit system's, so that does not help.
  The designed trigger-count / per-trigger vtable writes (`combat-choreography.md` 9.1) were never built and "the path
  from a unit to its shared trigger systems" is not found [C]. Particle off kills every effect, the shell player's too [C].
  **The usable means exists in the product:** farm guard's area probe already swaps a listener node on the EXE's 4 MB "A"
  event manager (type 33) under its registration lock, its build-table rows `fg_a_*` carried to both builds
  (`research/02-exe-internals/farm-builder-guard.md` 1, 13.4) [C]. The EXE's walk effect is a type 0x28 record posted when
  an Effect trigger fires (`effects-formats.md` 8.1) [C]; a pass-through node on type 0x28 could drop only the listed
  effect names whose owner key lacks the player's VP64 tag and pass the rest [I]. Open, about 1 h each: the 0x28
  listener's calling convention and which record offset its argument points at; whether the EXE re-posts a lasting bone
  effect every frame.
- **Playing it at the server's figure (route S stream).** The shell's effect player already drew EXE model effects at the
  server's points and moved one by re-posting (`effects-formats.md` 15.7.8 r5) [C]; never seen live: an effect oriented
  by a bone, a player-posted scrolling FXShader model, the quaternion convention and the order of the definition's
  transform (the treads carry a y -1 offset) [I]. The controller already reports drawn units' walk triggers
  (`render/src/unitanim/controller.cpp` 815-843) and the emitter drops them (`noFight`, the EXE-class codes,
  `srv_trigger_emitter.cpp` 609-631); bone placement covers only figures in a fight (`srv_fight_geometry.cpp` 184-206) [C].
- **Options and estimates [I].** (C) the EXE's two tread effects dropped, none drawn: 6-9 agent-hours (the whole walk-prop
  list 8-12 h); the landship loses only its scrolling treads (the hull's moulded tracks stay, nothing parts from it);
  applied wider it would remove the gunship's rotor blades, jets' engine fire, the mobile SAM's rockets and hand-held
  spears and arrows, so never wider than the treads. (B) the server sends the walk clips' effects at its own figures and
  the EXE's copy is dropped: 15-24 h (treads only 12-18 h), two agents; one handle per unit, figure and trigger across the
  Run clip's loops, or the scroll restarts every 0.667 s. Recommended order if 0783 fails: C for the two tread names
  first, then B for the treads and the other props.

#### 19.14.2 Fallback C as built for beta 5 (ticket 0784, D268, impl-0784b, 2026-11-26) [C; L where marked]

The owner chose C for beta 5 (D268 [reworded for privacy]: C now); B (the server draws the walk effects) is ticket 0789 after beta 5 and
reuses the pass-through. The two open reads of 19.14.1 are answered [C, old build]: the type-0x28 listener is thiscall
with one argument, the record's payload (record + 0x10: key +0x40..+0x4C, name hash +0x50), `ret 4`, and it defers a busy
record by itself (never through the node); and **a lasting bone effect is re-posted every frame** while its trigger is
active (the clip's trigger walk fires every Effect trigger whose interval overlaps the frame), so dropping every tread
record means no tread instance is ever made. Built as farm guard's tread filter (`../02-exe-internals/farm-builder-guard.md`
15): the main Particle object's type-0x28 node of the A manager calls a pass-through that drops
`ART_DEF_VEFFECT_TANK_TREADS` and `_02` unless the shell's effect player posted them (key tag 'VP64') and passes every
other record; a data write (D228 base product), five patterns per build, both builds' tables complete. VP64Options.ini
`[Renderer] HideTreadEffect` (id 39, default 1, D255; acts only with `ServerUnits = 1`; `docs/OPTIONS.md`; a checkbox under
ServerUnits on the installer's page). What the player sees with it on: the landship's hull with its moulded tracks, no
scrolling tread shell and nothing parting from the hull in a turn; the dust and dirt trails of the same clips are
particles of other effects and stay; every other walk prop (gunship blades, jet fire, spears, the horse archer's arrow)
is untouched.

**The live check [L; read by eye, DIAGNOSTIC] (laptop, MC-B's grant #16464, 2026-11-26 02:38-02:47 EST).** tb1's VP is
5.4.8 (Community Patch modinfo v151, as c5h-c; job 20261126-023847-9bb6, `server/evidence/0784/vpver.py`), so the pass
ran on branch impl-0784b @4f3e317a0 built on the 0.1b pins (`F:/c5-64bit-work/0784b/s548/ship`, the purity file loaded
through `purity_wrap.py`: "purity: loaded ... 140 method keys, 29 Lua names"). Product arm alone, job
20261126-024105-3b4c (`lap0784b.sh product`): TB-HUGE_0350 t350, a spawned landship (17542, `UNIT_WWI_TANK`) from A 105,23
by B 104,23 to C 104,24 (60 degree bend), 2 walks, rc 0, restore and provision verify ok. `vpproxy.log`: the five
patterns match on 4390913; "treads (ticket 0784): on"; the main Particle object's node (the only node of type 0x28, list of
1) swapped; then 22,367 / 35,013 / 48,093 type-0x28 records through the filter at +121 / +151 / +181 s, **dropped 2 / 184 /
364, exactly half each name** (the two members' two triggers, every frame of the walks), every other record passed (47,729),
none of the effect player's, no fault; 0 LOUD lines in `vpproxy.log` and `server.log`. Frames (walk r1, every 4th frame of
f76-f120 through the bend, crops `F:/c5-64bit-work/0784b/lap/zoom_r1.png`, `sheet_r1_all.png`, never in git): **only the
two hulls with their moulded tracks; no tread shell on, behind or beside them on the straight legs or in the bend**; the
dust and dirt trails stay. Not verified live: walk r2's frames (fetched, not read), a gunship's blades or a spearman's
spear (no walk case for them; the counters show only the two tread names dropped, and the unit test passes every other
name), the 5.5 install (the laptop has 5.4.8).

### 19.12 Cavalry glide: the mounted run clip stood still at its entry time (ticket 0782, impl-0782, 2026-11-25) [C; L where marked]

Observation 2 of 19.11. In short: the mounted machine enters its run state for a server walk and samples the run clip
1400, but the clip's time never moves, so every horse holds one pose of its gallop while the formation slides. The cause
is the controller's graph evaluator, not 0685 and not the feed.

- **Offline, per figure and frame [C].** `server/evidence/0782/mounted_walk.py` writes `.c5anim` timelines of the events
  the server's AnimFeed gives in mode exe (19.9): `move 4 2` at frame 10, `turn 4 right` at 13 (the first step), for the
  bend `turn 5 right` at 21, `arrive` at 29 (about 630 ms, the mounted A -> C of 19.10). It plays them through `unitanim
  play` (the controller as AnimRunner opens it: `ClipGraphFactory` with its default options). Inputs: render build
  `C:/c5-64bit-work/0613/render-build` (2026-11-24 23:05; no change to `render/src/unitanim` since but combat), the pure_vp
  4390913 install, the 0327 VP database copy; work folder `F:/c5-64bit-work/0782/off1`. Horseman (`ART_DEF_UNIT_HORSEMAN`)
  and knight, 5 figures each, straight and 60-degree bend, every figure the same way: start run (state 4, idle 1000
  fading into 1400 over 0.18 s), then the run loop (state 17) with **1400 at one constant time for every frame**: figures
  0 and 3 at 0.2000 s, 1, 2 and 4 at 0.1333 s (13 frames each; 54 in a 2 s walk), then stop run (state 9, 2440 / 1440)
  at the arrive. The foot control (warrior, `1_omni_state_01`) advances its 1400 by 1/30 s a frame. The same timeline
  with `--run-from` (the hold off) advances the horseman's run 0.0333 s a frame and loops at 0.533 s (65 distinct times).
  Removing the turn events changes nothing: the clips do not read 0685's `WalkFacings`.
- **Why [C, the graph and the code].** `mounted_running_loop.dge` (the run loop's graph of the mounted machine):
  root <- Selection on path 8 over (0) Blend(From = the run selection, To = turn 1480, Param = path 7), (1) the run
  selection alone, (2) Blend(From = the run selection, To = turn 1460, Param = path 7); the run selection picks by path 1
  run 1400 / charge 1120 / shuffle 1450; every source loops with ContinueOffset 1. Paths 7 and 8 are written only by the
  EXE (unit-controller.md 6.3); the server sends no `steer`, so both are 0: input 0, weight 0, the run alone through the
  Blend's From input. `GraphOptions::hold_from_continue` (4406 r2, MC #2117, default on; unit-controller.md 15.8) holds
  every ContinueOffset 1 source reached through an unmasked Blend's From input at its start (the clip time taken over
  from the previous state). The rule was written for cross-fades (the From side of a transition fading out, [L] at
  one point, P01 4681), but applied to any Blend, so here it froze the loop's run for as long as the state lasts.
- **Stock [L, 4405's Q3b capture, `F:/c5-64bit-work/0374/4405/tables/Q3b.csv`].** 0.6 s into a horseman's move stock's
  rider and horse meshes fit the run 1400 at 0.131, 0.177 and 0.217 s (0.01-0.04 deg), stop run follows at the arrival; the
  reporter sees the gallop in stock (19.11). A single capture cannot show the clip advancing; the laptop pass below does.
- **Older than 0685 [C].** The hold rule dates from 2026-11-15 (4406 r2); the server's units have drawn every mounted
  walk this way since the controller drove them. `C5_RENDER_UNITS_WALK_TURN=0` gates only the rigid yaw (19.7) and
  cannot change a clip; the offline cases without turn events show the same frozen run. Not run live with the switch.
- **The census [C, `server/evidence/0782/hold_census.py`].** Over logicdump's Python dumps of 4402's reached set (112
  graphs, `F:/c5-64bit-work/0374/4402/reached/py`): 165 sources are held by the rule; 159 under a fade weight (a hint-5
  callback, or a BlockCurve of one), 6 under a path weight, all six in `mounted_running_loop.dge` (1400, 1120, 1450 on
  both turn blends). Over all 1,071 logic files of the install and MODS (`.../4402/all/py`): the same 6 plus one source each
  in `hate_hear_it_speech_graph_leader.dge` and `neutral_hear_it_speech_graph_leader.dge`, held under a BlockCurve
  whose input is not a fade. These change nothing: leader scenes are drawn by the EXE (renderer scope, D80), no unit art
  reaches them and the server's controller never binds them.
- **The fix [C: code and tests].** `render/src/unitanim/graph.cpp`: a Blend's From side is a held side only when its
  Param is a fade weight (`fadeWeight`: a hint-5 callback, or a BlockCurve whose input is one); under any other weight
  the From side runs at Speed. The branch for a Blend without a To edge (0439) is unchanged. So only
  `mounted_running_loop.dge` changes among the unit graphs: the mounted run loop runs, also inside a turn blend should a
  `steer` ever set paths 7 and 8. Tests: `render/tests/unitanim/test_graph_hold.cpp` `from_side_under_a_path_weight_runs`
  (path 7 at 0 and 0.5: the run at 0.2 + 0.3 s) and `from_side_under_a_curve_fade_still_held` (a BlockCurve fade holds).
  The offline cases again with the fixed build (`F:/c5-64bit-work/0782/allb/render`, 2026-11-25 21:07; work folder
  `.../off2`) [C]: the horseman's and knight's run loop advances 1/30 s a frame and wraps at 0.533 s (figure 0: 0.200,
  0.233, ... 0.500, 0.000, 0.033, ...; 65 distinct times over the 5 figures in the 13-frame walk, 253 in the 2 s walk),
  the states unchanged (start run, run loop, stop run at the arrive); the warrior's `programs.csv` is byte-equal to the
  0613 build's (`off1`). Not recreated: the EXE's paths 7 / 8 during a turn (the turn clips 1460 /
  1480 never play in the server; unit-controller.md 6.3, 0042).
- **Every builder [L, workstation, 2026-11-25, `bash server/evidence/0782/allb0782.sh 1|2|5`, work folder
  `F:/c5-64bit-work/0782/allb`, branch `impl-0782` on main 899b3d1e8].** Phase 1: render ctest 108 of 109 under load
  (`c5r_unitanim_test_graph_hold` and every unitanim test passed; `c5r_bakeresidency_test` failed, the load flake of
  19.7, and passed alone in phase 5, "tests passed, 0 skipped"); server release on the 0.1b pins OK (1,134 s; server.exe
  `f76962d8...`, its proxy `8b8b7437...`). Phase 2: units build + `test_units_runner` 7 passed, units extras 14 of 14,
  unitfeed, trigger scene, effect routes, trigger emitter, units fights / queue / records / seed, farm fields, unit
  shadow OK. Shell build (`server/evidence/0782/ship0782.sh`): BUILD OK (composite `8e06b2b2...`).
- **Laptop pass [L; by eye], 2026-11-25 22:34-22:50 EST (MC-B's grant #15977), runbook 4.15 `acc0685_laptop.sh`,
  TB-HUGE_0350, Quick Movement off, `--walk-crop 1600x1100 --walk-order path --walk-bends 60`, 2 repeats, foot and
  mounted: stock job `20261125-223439-9a12`, product `20261125-224318-74a8` (the SHIP above), both rc 0, cameras
  verified, restore by hash ok; full-size mounted frames fetched to `F:/c5-64bit-work/0782/lap/{stock,product}`; the
  product arm of pass 4 (`20261125-053222-d822`, before this fix) fetched for its mounted r2 to `.../lap/prefix`.
  Crops: `server/evidence/0782/legs0782.py` (fixed or root-following 1:1 crops of `out/raw`), sheets in
  `.../lap/zoom`. Read on the first leg (west, the horses side-on at A, about 450-600 ms after the order): **stock**
  (r2 f29-f35) and **the fixed product** (r2 f28-f32) show each horse in a different gallop pose from frame to frame;
  **the product before the fix** (pass 4 r2 f32-f35) shows each horse keep one pose while the formation slides. At
  this crop (a horse about 120 x 80 px) the reading is by eye and not certain; the walk is mostly behind the mine's
  towers and the city banner. A frame-to-frame template match of horse boxes (1 - NCC) did not separate the arms (seed
  placement dominated; tried and dropped). Not done: a numeric leg measure, the knight, the 120-degree bend.
  Caveat: this product run had no purity list loaded (`vpproxy.log`: "purity: not loaded (pack=0)"): the product path's
  `set-proxy` copies only `PROXY_VP64_FILES`, so `purity.tsv` in the shipped `bin/` never reaches `VP64\`. It does not
  touch the clip clock; MC-B asked for the product arm again with the list loaded.
- **The product arm again (MC-B #16201), 2026-11-26 02:02-02:09, job `20261126-020220-6e3b`** (same SHIP, rc 0, restore
  ok): purity still not loaded. The job environment does reach the EXE's proxy, but `VP_PROXY_PURITY` must be
  `<purity.tsv>|<x64 DLL>` (`proxy/src/proxy_purity.cpp` `LoadFromTestEnvironment`: the list is bound to that DLL's
  SHA-256); the file alone gives "VP_PROXY_PURITY is not ...". A read-only job (`server/evidence/0782/purity_check.py`,
  `20261126-020033-acd4`) confirmed an earlier job's shipped copy stays in `C:/c5-testbed/jobs/<job>/bin/pp/bin/` on the
  laptop (sha256 `d52b56e9...`). For a product-path laptop run with the list: `PENV="VP_PROXY_PURITY=<that
  folder>/purity.tsv|<that folder>/CvGameCore_Expansion2.dll"` (the same DLL bytes as the run's).
- **The product arm with the purity list [L], 2026-11-26 03:31-03:37, job `20261126-033127-46c2`** (MC-B #16714, same
  SHIP, REPEATS 1, rc 0, restore ok, cameras verified): `vpproxy.log` "purity: loaded (pack=0): ... 140 method keys, 29
  Lua names; bound to the x64 DLL b84aa3e90f51...; derived from 1b87dd185 (interim test load from VP_PROXY_PURITY)".
  Runbook 4.15 has the command. Its mounted walk (r1, f28-f34, the same 1:1 crop at A, `.../lap/zoom/tight_prod3_r1.png`)
  reads as the other fixed run: each horse in a different gallop pose from frame to frame [L, by eye].
- **Relink on the VP 5.5 pins [L], 2026-11-26 03:43-04:33 (MC-B #16784, `allb0782.sh 7` with `PINS_SRC` / `PINS_DEPS`
  = `C:/c5-64bit-work/keep/pins-5.5/{src-x64-a2627c8ac,deps-x64-release}`):** branch at 3b18647e5 (main 38b221c7d merged,
  clean; main brought no `render/src/unitanim` change, so no second rule A by the relink policy): server release OK
  (1,676 s, server.exe `a690f7f3ae2a673e...`, not dirty), units build + `test_units_runner` 7 passed, units fights /
  queue / records / seed and the trigger emitter OK.
- **Rule A [L], MC-B's go #16296, 2026-11-25 23:47 - 2026-11-26 01:41, `allb0782.sh 3`, `F:/c5-64bit-work/0782/allb/ra`:**
  render-host 235 passed, scenebundle-py 34 passed, offline-equals-live 18 passed (37 min), 0 failed, 0 skipped, no
  0xC0000142; scenebundle-programs: bundle_format 1,260, recorder 268, sources 24 passed, `sceneshot_test.exe` timed out
  (1,500 s in the suite; 3,600 s alone, `allb0782.sh 6`, 01:43-02:43, its work folder still advancing): not a result,
  the night's known load flake (MC-B #16545, ticket 0800). offline-equals-live passing means no library bundle's
  replay changed beyond its tolerance [I: no bundle holds a walking mounted unit at a compared frame; not checked per
  bundle].

### 19.15 The reporter's D237 eye check of the five beta 5 gates: passed (lead-0673, 2026-11-26) [L: the reporter's eyes]

Main 869437685 (beta 5 plan: 0788 4e532763a, 0784 128acec38, 0782 f91052950, 0783 6293f9f40, 0789 138e034cf), server.exe
`b2ac7395ef60...`, shell composite `f943f5639682...`, every new key at its shipped default (`WalkCurve` on,
`[Renderer] HideTreadEffect = 1`, `[Renderer] ModelStore = 1`), with the 19.11 `[Renderer]` set. c5h-55-c (mods55, EXE
26577625) visible, guard and cloak off, parked at 0,0; the save copy `C5H-0673-D237-VP55-HUGE_0350-B` (0771's
VP55-HUGE_0350-B, sha256 `87fbdfec...`) at turn 350, autoplay off (`d237.sh` with `STOCK_INST = PROD_INST = c5h-55-c`,
work folder `F:/c5-64bit-work/0673/eye-d237`). In game 12:58 EST, played until about 13:02; the harness's stop, restore
by hash, verify clean, released 13:04 (MC-B's quiet window #17531).

The reporter was given five things to look for, one per 19.11 observation plus the flashes, and confirmed all five:

| What to look for | Ticket | Result |
|---|---|---|
| Units turn smoothly through a bend, no snap on the plot before the turn (19.11 obs. 1) | 0783 (with 0685) | correct |
| Cavalry's legs move on every move, straight or not (obs. 2) | 0782 | correct |
| The big arc through a bend as stock, not centre to centre (obs. 4) | 0783 | correct |
| The landship's treads stay on the hull; under fallback C they no longer scroll (obs. 3) | 0784 | correct |
| No late-game model flashing | 0788, 0789 | correct |

The reporter's words: "I've confirmed all of these look correct now, great work!" This closes 19.11's four observations
for beta 5 (D264's gates 0782, 0783, 0784; D266/D269's 0788, 0789). Still open: the treads do not scroll under C (ticket
0795, fallback B, after beta 5); 0790's picture scores on the C5 candidate.

## 20. What the units frame costs, and buildList's per-unit work (ticket 5056, epic 0042, 2026-11-23)

Ticket 5056 (W77, branch `w77-5056`; MC #10982 and #11002). Filed from 0689 step 1 (the laptop's `units 1s` line on
TB-HUGE_0350 t350: `frame_ms_max` 7.80 ms p50, `anim_ms_max` 3.40, 733 listed and 704 culled) as "the module animates
every unit and draws about 30".

### 20.0 In short

- **The premise was partly done and partly against stock [C].** The controller already steps only the drawn units
  (4468, 0450: `Controller::apply(frame, drawn)`, the drawn set built after the cull and the fog filter), so "733
  animated" was an inference from `listed`/`culled` [I in 0689]; the `units 1s` line's `anim_drawn` / `anim_quiet`
  show the split. **An off-screen unit's clock stays HELD**, because the EXE holds it (0442's rdc18 capture and 0450's
  cull and fog arms [L], `unit-controller.md` 18, 18.1): the ticket's "advance by time and land on the right pose on
  entry" would make a unit entering the view differ from stock, and the MC kept the held clock (#10982). Fights already
  step their named units drawn or not (`srv_units_anim.cpp` `AnimRunner::apply`, 4774 / 4729).
- `frame_ms_max` and `anim_ms_max` are **per-second maxima**, not per-frame values.
- What still scales with every held unit: `UnitsCore::buildList` (the fades, the per-unit checks and the cull of all
  853) and the controller's per-unit loop (events, settle, a status per unit). The largest part, `prepare` (pose rows
  of the drawn figures), scales with the drawn units.
- **Done (design A):** `buildList` takes each owner's `PlayerArt` once per call and builds a note's key only when a
  note is written. Byte-identical by construction and by rule A; build + cull 0.53 -> 0.28 ms per frame on the
  workstation (20.3).

### 20.1 The split [L, workstation, an indication (COMMON 23), 2026-11-23 00:4x]

sceneshot of main ad17fa7cb with a timer patch that is never merged (`server/evidence/0042/5056/diag5056.diff`, v2 adds
the cached art rows: `diag5056-v2.diff`; it prints one `diag5056` line per `drawScene` to stderr under
`C5_DIAG_5056=1`) on `C5H-0301-HUGE_0350_20261122-124422_workstation` (the laptop's save): the 899-frame
`units_history.bin` replay plus 8 shots, 887 frames after the first 20; 853 held, 733 listed, 704 culled, 29 drawn,
824 controller-quiet; every shot byte-equal to its dump. Per-frame ms, p50 / p90 / p99 (`stage1-split-workstation.txt`):

| Part | p50 | p90 | p99 |
|---|---|---|---|
| units frame (`frame_ms`'s per-frame value) | 1.88 | 2.39 | 4.24 |
| `buildList` (fades, checks, cull) | 0.53 | 0.59 | 0.72 |
| fog filter | 0.003 | 0.004 | 0.006 |
| controller (`anim_ms`) | 0.50 | 0.78 | 1.40 |
| of it: feed close / `Controller::apply` / pose set / `fillFigures` | 0.07 / 0.28 / 0.01 / 0.13 | | |
| `prepare` (pose rows of the 29 drawn) | 0.76 | 0.89 | 2.36 |
| scene pass | 0.08 | 0.15 | 0.46 |

Not in `frame_ms`: the reflection and the mask passes. `buildList` sampled (`perf/sampler`, 2 ms;
`stage1-buildlist-profile.txt`): `playerArt` 34.8 % (two strings copied and three lookups per listed unit), the loop
itself 32.3 %, `boxInView` 14.2 %, `~PlayerInput` 4.1 %, `plotCentre` 4.0 %, the note key's `to_string` 3.1 %.
The controller's first frame (853 creates) costs 305 ms and the first `prepare` 210 ms (11 art rows); after that no
art row is loaded on this bundle (the cached rows stay 11 in every run): its `prepare` spikes of 3-9 ms fall on
different frames in each run, so they are not row loads [I: the machine's other jobs].

### 20.2 The change [C: code and test]

`server/src/units/srv_units_core.cpp` `UnitsCore::buildList` only (no header change): a 64-slot `PlayerArt` cache
local to the call, filled on an owner's first use (`m_players` and `m_look` do not change inside the call; an owner
outside 0..63 goes to `playerArt` each time, as before); used by the per-unit checks and by each wrap copy's players
list. The note key (`"owner,id"`) is a lambda called only by the three notes that use it. Same check order, same
notes and once-keys, same counters, same `UnitScene` contents. Test `build_art_once_per_owner`
(`server/tests/units/test_scene_units.cpp`): seven units of four owners (a valid one in view, in the wrap copy and on
an unknown plot; an unknown colour; an unknown era; no player record), two frames, the lists, counters and once-only
notes as before; a player record changed between two calls is seen by the next (the cache lives one call). Mutation
(not committed): the cache made `static` fails the case.

### 20.3 Numbers [L, workstation, an indication]

`bench5056.sh` (6 interleaved rounds, order turned each round, base = the same tree with the base `srv_units_core.cpp`,
both with the v2 timer patch), `bench_stats.py` (`bench-stats.txt`, `bench-rounds.txt`), 2026-11-23 01:38-01:58 with
other teams' heavy jobs running (`quietlock check` listed builds and a release link). `buildList` per-frame p50: base
0.74 pooled (0.55-0.92 per round), new 0.29 (0.26-0.59): **paired new - base -0.33 ms [-0.58, -0.28]** (bootstrap of the
round medians). In the quieter rounds (2, 4, 6) base 0.55-0.63, new 0.26-0.29, matching 20.1's quiet base 0.53. The
whole units frame per round: base 2.09-5.91 ms, new 1.48-4.66; the base arm ran during heavier load in most rounds, and
the unchanged controller also differs between arms, so the frame's difference (-1.27 ms [-3.40, -0.68]) is mostly load;
**the change's own saving is buildList's ~0.3 ms of ~1.9 ms (about 15 %)** [I: from the quiet rounds]. Why it moved:
733 `PlayerArt` copies (two `std::string` each plus three map finds) and 733 key strings per frame became 1-4 and 0.
Not measured live; on the laptop's slower CPU the same work is expected to scale up [I].

### 20.4 Rule A [L]

The branch's sceneshot over the C: library (`scenelib check` OK, 16 bundles): `scenerun run --no-baseline-write`, 13
bundles' dumped frames byte-equal (or within the library's tolerance) to the server's dumps and 6 cameras drawn; the
3 bundles whose recorded install was a claimed pool instance at the time (C5AIR, C6CARRIER, C7INTERCEPT) again with
`--asset-root mods548-vt`: 8 of 8 byte-equal each (`ruleA-scenerun.txt`). The history replay (899 frames of recorded
camera moves) goes through `buildList` before every compared frame.

### 20.5 Not verified / left

- No live measurement (laptop or oldpc) of the saving.
- B (cull before the type, seed and embark lookups) and C (a hidden unit's per-frame visit in the controller) not done:
  B changes which failing culled units count as not drawn; C could change random rolls of a hidden unit's settle, its
  parity with the EXE unknown (MC #11002: not now).
- `prepare` (40 % of the frame, drawn units) and the 0689 laptop's remaining draw time are outside this ticket.

