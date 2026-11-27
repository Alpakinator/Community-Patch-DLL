# Review of the pure 64-bit port's speedup lists against our architecture (ticket 9077)

Reviewed 2026-11-17 (13:30-16:00 EST) for one-off 9077 (the owner's order of 13:30): every entry of the two documents that
an author of the other, pure 64-bit port of Civ V / Vox Populi wrote for us, judged against our design. The documents are
in the owner's staging folder outside the repository (`EXACT_SPEEDUPS.md`: output-identical speedups, IDs LU, PF, DG, TA,
HL, MI, EC, CB, TR, DI, RE, PO, MB, BS, OT, plus "Built, kept OFF" and "Lessons"; `SPEEDUP_DEVIATIONS.md`: behaviour
changes A1-A16, B1-B4, "Tried and rejected / parked"). They stay out of the repository for good (the owner, decision D179, 2026-11-17 13:40): read them in place;
this file paraphrases them by entry ID. Their numbers are theirs: their saves (L290: 9 majors, 18 city-states, ~105
cities, ~1,050 units, ~25 extra mods; T130; Huge T250/T300), their machines, a VP 5.4.6 DLL built with MSVC, mostly
"contended, pre-PGO" per-function figures. Every figure of theirs below is marked [I] for us.

This file is the input for the performance team's epic proposal (9077's second step, which the MC reviews with the
Architect before anything enters the tracker). Nothing here is built or measured; no game ran for this review.

## 1. Facts that decide most verdicts

- **F1. A GameEvents hook raise crosses our IPC, several times.** Gameplay `GameEvents` listeners run in the EXE's Lua VM
  (decision D21 item 2; `research/03-dll-engine-boundary/proxy-design.md` section 9 MC-2 and 16.7). The DLL raises a hook
  through `CustomMods::eventTestAll` and friends (`CustomMods.cpp`:36-115 @04b1211b8 [C]): `CvLuaArgsHandle` makes the
  args object, `parseVariableArgs` pushes each argument, `LuaSupport::CallTestAll` calls, the handle's destructor destroys.
  In the server each of these is its own crossing (`server/src/remote/srv_remote_lua.cpp`, `RemoteArgs` /
  `RemoteScriptSystem`: generated remote calls, nothing batched [C]; `GetScriptSystem` alone is answered locally since
  2700). Measured: about 6 calls per hook on the huge save (proxy-design section 12 [L]); in a client M turn **about
  90,000 crossings of class `script` (mean 90,150 over turns 151-160), about 10 us each, 0.94 s per turn, against 2.14 s of
  the DLL's own work** (`turn-time-study.md` on branch `epic-0437-turn-study`, "Where U's time goes", 4665's budget runs
  2026-11-17 [L, one run on the workstation: indication]); these are **16,467 raises per M turn** at n + 3 = 5.56 crossings
  each, and 160,126 per H turn (`speedup-p0-measurements.md` P0.1, ticket 4800, headless counts [L]). (Corrected
  2026-11-17 by 4800: this line said "90,164 raises"; 90,164 was a crossing figure, not a raise count.) The other port
  answers these in-process; for us each one is worth several times more.
- **F2. In our mod set almost no hook has a listener.** Every measurement of ours runs CP + VP + EUI only
  (`port-comparison.md`, the study saves). At 04b1211b8 the Lua files of those three mods register listeners for 8 event
  names only: `TeamTechResearched`, `GetBombardRange` (CP `GlobalCityBombardRange.lua`), `PlayerDoTurn` (CP and EUI
  `DiploCorner.lua`), `PlayerCityFounded` (VP `CBP_IncaFunctions.lua`), `CityConstructed` (EUI `TechTree.lua`),
  `SetPopulation`, `CityBoughtPlot` (EUI `NotificationPanel.lua`), `UnitPromoted` (EUI `UnitFlagManager.lua`) [C: grep of
  `GameEvents.<name>.Add(` in the fork at 04b1211b8; dynamic registrations by other syntax not searched]. So nearly all of
  the 16,467 raises per M turn have no listener [I] (corrected 2026-11-17 by 4800: was "90,164 raises"). The other port's mod set had listeners on PlayerCanBuild,
  CityCanConstruct, PlayerCanConstruct/Train, CityCanTrain, UnitSetXY, EventCanTake, CityEventCanTake; none of those has
  one in ours, and `TheBalanceDemandsIt.lua` is not part of VP at the pin [C]. Several of those hooks are not even raised for us: `EVENTS_PLOT` (which gates `PlotCanImprove`, their largest at 254-514 k raises per turn, and `PlayerCanBuild`), `EVENTS_CAN_MOVE_INTO`, `EVENTS_AIRLIFT` and `EVENTS_SEALIFT` are 0 in CP and not set by VP [C, helper]. Which names make up our raises per turn: counted by 4800 (`speedup-p0-measurements.md` P0.1; corrected 2026-11-17, was "Which names make up our 90,164 raises per M turn was not counted").
- **F3. DLL base.** Ours is VP Release-5.4.8 plus upstream commits (x64 pin `04b1211b8` = Release-5.4.8-51, i0 pin
  `26234371e`); theirs was 5.4.6 (tag dated 2026-08-31). Where an entry's waste was checked at our pin the row says
  "present @pin [C]" with the file and line; "not read" means the row assumes it is still there.
- **F4. Build.** The x64 DLL is built with clang-cl and full LTO (ThinLTO rejected, D121); asserts are compiled into the
  release configuration (`VPRELEASE_ERRORMSG`, `research/06-tooling-and-workflows/x64-dll-build-configurations.md`:13
  [C]). No PGO yet: it is rank 2 of epic 0437's list (`turn-time-study-analysis.md` 9.8 on the epic branch).
- **F5. Where our turn time goes (epic 0437, 2026-11-16/17).** Headless 64-bit server turn: H (Huge, turn 300, ~175
  cities, ~1,600 units) 20.9 s (workstation, indication), M (Standard, turn 150) 1.42 s (laptop, grade A). Shares of
  the H turn: trade-path family 26 %, danger maps 11 % (their enemy reach 9 %), all `GetPlotsInReach` 25 %, single-unit
  paths 10 % (M 21-23 %), allocation 4.6 % (M 6-7 %), `MovementCost` 3.4 % (M 7-11 %) [L, sampled, indications]. In the
  client an AI turn is set by the EXE's frame pacing and the visual drain (M ~15 s, H ~89 s against 1.4 / 18.7 s
  headless); with proxy options 2+3 an M turn is 8.35 s = served Updates 3.86 s + end drain 4.14 s + tick 0.36 s, and
  option 4 (4675) cuts most of the drain, so **the served Updates (DLL work + GameEvents Lua + autosave) become the
  critical path** (d165-pacing-fix-design.md section 10 [L/I]). DLL CPU savings therefore count fully on H, and on M once
  the drain fixes are on.
- **F6. Scale of their figures.** Our H has ~1.5x the cities and units of their L290 and its headless turn (20.9 s, no
  PGO) is ~2.6x their pre-PGO L290 AI turn (~8 s). As a rough rule a pre-PGO per-turn saving of theirs is worth ~1.5-2x
  that many ms on our H [I]. "Value" tiers in the table: **high** >= ~0.4 s per H turn (~2 %), **medium** 0.1-0.4 s,
  **low** < 0.1 s, **bursty** = only on the turns the event fires.
- **F7. Database and text are local to the server.** The server keeps its own copy of the game database and its own
  localization (`server/src/remote/srv_remote_db.cpp` header, `srv_remote_main.cpp`:540-627 [C]); only DLL writes go to
  the engine. Their "fewer engine calls" items (CB8, DI3, OT8) are server CPU for us, not IPC.
- **F8. Saves.** A save moves in one transfer (D21 item 6, MC-6); the DLL's `FDataStream` writes are local. The
  autosave costs ~0.56 s of a client M turn (F1's source) [L, indication].
- **F9. UI Lua runs unmodified in the EXE at the EXE's own pace.** The AI runs in the server; the UI updates between
  served Updates and in the end drain (D165 options 2-4). There is no per-AI-step UI pass of ours to coalesce. Every UI
  Lua -> DLL call crosses the IPC (`kRt_LuaCall`, ~0.8 M per huge AI turn before 2800's cuts, proxy-design 16.7).
- **F10. Identity.** *(Correction 2026-11-17 15:05, MC from the Architect's review of 7018: D169's Extras are visuals only, so a
  game-changing DLL fix such as A9 or A10 is not an Extras bugfix; the route is a core defect fix in the fork on both pins,
  as bug 0116's was, with the upstream report part of the fix; the 'D169 bugfix' wording below and in the A9/A10 rows is
  replaced by this.)* An exact item must give IDENTICAL games (`stats.db` compare, D164: headless vs headless, windowed
  vs windowed). A behaviour change is a gameplay decision; for the DLL the route is a define on a fork branch from the
  x64 pin (AGENTS.md 3, D104: DLL-side changes go upstream, 32-bit unharmed), and a VP defect fix fits increment 4's
  flag-gated "bugfix" category (D169).

## 2. Verdicts

**DONE**: implemented or equivalent in our system (or already gone upstream at our pin). **IN WORK**: covered by a
running or finished epic-0437 ticket (prototype, not merged). **N/A**: not possible or not applicable in our architecture
or mod set. **MISSED**: would help us; where, exact or not, value, cost (S < 1 day, M 1-3 days, L more), risk. **OPEN**:
needs the named measurement first. "Subsumed by LU1" = with our hook gate (LU1) the raise costs nothing in our mod set,
so the item adds nothing beyond it.

**Counts (166 entry IDs, every one with a row):** MISSED 135 (about 15 of them negligible for us, <= 0.1 % of a turn), N/A 15 (most because no listener exists in our mod set, F2), IN WORK 5 (0437's 4673, 4674, 4677), OPEN 5, DONE 2 (A13's side effect is our bug 0116; A15 by design), not recommended 4. Plus 22 kept-OFF items, 7 lessons and 14 rejected items recorded as theirs (2.9, 2.10, 2.12).

### 2.1 Lua hooks and Lua <-> DLL calls (LU)

| ID | Item | Verdict | Reason and evidence | Where | Exact? | Value for us | Cost | Risk |
|---|---|---|---|---|---|---|---|---|
| LU1 | No-listener gate for hook raises | **MISSED (top)** | F1, F2: ~90,000 `script` crossings (16,467 raises at 5.56 crossings each; corrected 2026-11-17 by 4800, was "90,164 raises") per client M turn at ~10 us, nearly all without a listener. Our form differs: no DLL change; the server keeps the args locally and decides at `CallTestAll`/`CallHook`/`CallTestAny`/`CallAccumulator` whether the name has a listener in the EXE; if not, it answers the no-listener result (`srv_lua.cpp`:11 semantics) with zero crossings. Two parts: (a) local args batching (one crossing per raise), exact by construction; (b) a listener-presence mirror with a generation the EXE side bumps on any Add/Remove | server `remote/` + proxy | exact (a) by construction; (b) if the generation is complete | **high**: up to ~0.8-0.9 s per client M turn (~10 % of today's 8.35 s with options 2+3, more after option 4); H count not measured | M (a); M-L (b) | (b): how the EXE implements `GameEvents.X.Add/Remove` is not in the knowledge base; a missed registration would skip a real listener (game change): fail loud, verify mode, identity pair windowed vs windowed |
| LU2 | PlayerCanBuild answers kept per worker pass | N/A (subsumed by LU1) | No PlayerCanBuild listener in CP/VP/EUI (F2) [C] | - | - | none for our mod set | - | - |
| LU3 | PlayerCanBuild answered natively | N/A | Transcribes listeners of a mod we do not ship (`TheBalanceDemandsIt.lua`) [C: absent at pin] | - | - | none | - | - |
| LU4 | CityCanConstruct raise skipped for unnamed buildings | N/A (subsumed by LU1) | No CityCanConstruct listener in our set (F2) | - | - | none | - | - |
| LU5 | CityCanConstruct walks only relevant listeners | N/A | Listeners are walked by the EXE's GameEvents implementation, which we cannot change (no code bytes, CEG); and none exist in our set | - | - | none | - | - |
| LU6 | PlayerCanConstruct / PlayerCanTrain escape-set bypass | N/A (subsumed by LU1) | Audited listeners are from mods outside our set | - | - | none | - | - |
| LU7 | CityCanTrain escape-set bypass | N/A (subsumed by LU1) | Same | - | - | none | - | - |
| LU8 | UnitSetXY raise skipped when listeners return early | N/A (subsumed by LU1) | No UnitSetXY listener in our set | - | - | none | - | - |
| LU9 | EventCanTake escape-set bypass | N/A (subsumed by LU1) | No EventCanTake listener in our set | - | - | none | - | - |
| LU10 | CityEventCanTake answered natively | N/A (subsumed by LU1) | No CityEventCanTake listener in our set; the method (transcribe audited listeners) stays useful if a player's extra mods register some | - | - | none for our set | - | - |
| LU11 | EUI city screen asks the DLL only for shown rows | **MISSED** | EUI is in our set; every UI-Lua -> DLL call crosses (F9), so halving the city screen's `IsCanPurchase` / `cityCanProduce` calls (64 k -> 33 k, 32 k -> 16 k per their screen) is worth more to us; affects the human's city screen, not the AI turn | UI Lua: `(3a) VP - EUI Compatibility Files/LUA/CityView.lua` (VP repository; upstream change) | exact (same rows; the skipped calls are pure queries) | medium for responsiveness: tens of ms per city-screen update [I]; **OPEN**: time a city-screen refresh in the client with `proxy-lua-calls.tsv` | S-M | UI-only; their check compared 13 screens |
| LU12 | Instance-push key strings interned | MISSED (low) | Our proxy rebuilds instance tables in the EXE's VM and pushes "LuaTypes" / "__instances" as strings on every push (`proxy/src/proxy_lua.cpp`:611-681 [C]); same waste, other place | proxy | exact | low (their model ~0.02 s per early turn) | S | low |

### 2.2 Path finding (PF), tactical (TA), homeland (HL), military (MI)

Verdicts by the review's helper (same date, same rules). **Shares** "H x / M y" are inclusive shares of sampled cycles recomputed on 2026-11-17 from 0437's sampled runs of ticket 4668 (`F:/c5-64bit-work/4668/runs/h1/profile/folded.txt`, H turns 300-305; `.../m3/` or `m1`, M turns 150-160) [L: workstation, shared, indications]; under LTO most `static` helpers are inlined, so a function absent from the stacks is counted in its caller. 1 % = 0.21 s of H, 14 ms of M; "est." values are [I]. "fork" = the DLL fork behind a define on a branch from 04b1211b8; lines are `file:line@04b1211b8`. All items are DLL-internal and never cross the IPC: the event guards several of them mention (`EVENTS_PLOT`, which gates both `PlotCanImprove` and `PlayerCanBuild`; `EVENTS_CAN_MOVE_INTO`; `EVENTS_AIRLIFT`/`SEALIFT`) are 0 in CP (`NewCustomModOptions.xml`:821, 825, 855, 1046) and not set by VP [C]. "needs PFHI" items are exact only together with A10 (section 2.11).

| ID | Name (short) | Verdict | Reason and evidence | Where | Exact? | Value for us | Cost | Risk |
|---|---|---|---|---|---|---|---|---|
| PF1 | Per-search kept values, unit finder (7 parts) | MISSED (1 of 7 parts DONE) | DANGER_KEPT is upstream: commit 472e1163d (Release-5.4.7, in both pins) keeps end-turn danger per node per search, `CvAStar.cpp:1313-1316` [C]. The other six are absent: `PathValid` recomputes territory+terrain (`:1549`, `:1623`), `GetCostsForMove` re-reads unit/trait data per call (`CvUnitMovement.cpp:10-60`) [C]. Shares: enter checks under `PathValid` H 1.7 / M 1.4-6.1 %, `GetCostsForMove` H 2.0 / M 3.8-5.7 %, `PathEndTurnCost` H 1.6 / M 1.6-3.2 % [L]. | fork | yes (verify mode per part) | est. 1.5-3 % H (0.3-0.6 s), 3-6 % M | M | stale-value bugs if a kept input is missed; build a verify mode first |
| PF2 | Both canEnterTerrain answers from one evaluation | MISSED | Two calls per node fill still at `CvAStar.cpp:1039-1040` [C]. `canEnterTerrain` under `UpdateNodeCacheData` H 1.5 % / M 0.6-0.9 % [L]; one evaluation removes about half. | fork (CvUnit + CvAStar) | yes, by construction | est. 0.7 % H (0.15 s), 0.3-0.45 % M | S | low; air units keep two calls |
| PF3 | Extra-children call only when a lift is possible | MISSED | `UnitPathGetExtraChildren` is called for every added node (`CvAStar.cpp:1774`) and every expansion (`:585`) and only then tests the lift bits (`:2241-2260`) [C]. Its self time under `NodeAddedToPath` H 0.28 % / M 0.1-0.28 % [L]; the 3 % inclusive on H is real airlift work PF3 keeps. | fork | yes, by construction | <= 0.3 % | S | none |
| PF4 | Obsolete child decided before PathCost | MISSED | `LinkChild` still calls `udCost` before the obsolete test (`CvAStar.cpp:635`, test at `:650`) [C]. `PathCost` H 7.2 % / M 11.7 % inclusive [L]; part of it ends obsolete. | fork | yes, with the clean-danger-map guard | est. 1-2 % (overlaps PF6) | S | the guard is essential: `PathCost` can trigger the lazy danger rebuild |
| PF5 | Neighbour fills deferred to expansion | MISSED | `NodeAddedToPath` fills all six neighbours on every new add (`CvAStar.cpp:1761-1767`) [C]. Fills under `NodeAddedToPath` H 8.0 % / M 5.1-5.8 % [L]. | fork | only with A10 (needs PFHI) | est. 1.5-3 % H, 1-2 % M | M | identity depends on A10 |
| PF6 | Bound / dead neighbours skipped (two-layer) | MISSED | `CreateChildren` runs `PathValid`, `LinkChild`, `AddStopNodeIfRequired` for every neighbour (`CvAStar.cpp:553-577`) [C]. `PathValid` H 3.4 % / M 9.3 % inclusive [L]. Our CanMoveInto event is off [C]. | fork | yes (no PFHI) | est. 1-3 % | M | the stop-node condition is subtle; verify mode needed |
| PF7 | No neighbour fills for dead nodes in reach searches | MISSED | Same fill block as PF5, no dead-node test [C]. Reach searches are 24.9 % of H, fills 8 % [L]. | fork | only with A10 | est. 1-2 % H | S-M (with A10) | identity depends on A10 |
| PF8 | Closed neighbours skipped in simple step floods | MISSED | Generic `CreateChildren`, no closed test (`CvAStar.cpp:553`) [C]. `LandUnitSimpleValid` + `NavalUnitSimpleValid` self H 1.7 % / M 1.1 % [L]. TA3 removes the largest caller of these floods. | fork | yes, by construction | <= 1 % H (less after TA3) | S | low |
| PF9 | Route-search node memo per search | MISSED | `BuildRouteCost` ignores the parent and recomputes per edge (`CvAStar.cpp:2655-2745`) [C]. Self M 0.8 % / H 0.08 % [L]. | fork | yes | est. 0.5 % M, ~0 H | S | low |
| PF10 | Route node terms shared within one route step | MISSED (low) | Builds on PF9 across the searches of one `UpdateRoutePlots` step (`CvBuilderTaskingAI.cpp:1538`) [C]. After PF9 little is left in our profiles. | fork | yes (verification) | <= 0.3 % M [I] | M | invalidation within a step |
| PF11 | River extra children kept within one route step | MISSED (negligible) | River connections are on in VP (`CustomModOptionChanges.sql:39`) [C], but `AddCityConnectionRiverConnections` is 0.14 % H / M [L]. A cheaper fix: it copies the river's plot vector by value per hex side (`CvAStar.cpp:2197`) [C]. | fork | yes | ~0.1 % | S | none; skip unless done with PF12 |
| PF12 | Duplicate river children dropped (city connections) | MISSED | The same river ID on several hex sides is handed out once per side (`CvAStar.cpp:2188-2208`) [C]; each duplicate costs a `PathValid` + `LinkChild`. City-connection callbacks H 2.7 % self 1.1 %, `UpdateRouteInfo` H 2.65 % / M 0.47 % [L]. | fork | yes, by construction (destination hit count kept) | est. <= 1 % H | S | low |
| PF13 | Closed neighbours skipped in army searches | MISSED | Generic `CreateChildren` [C]. `ArmyStep*` callbacks self H 2.3 % / M 0.9 % [L]; their PF13+PF14 cut army search by 27 %. | fork | yes, by construction | with PF14 est. 0.8 % H (0.17 s), 0.3 % M | S | low |
| PF14 | Army step callbacks once per plot per search | MISSED | `ArmyStepValidLand/Water` and `ArmyStepCost` re-evaluated per edge (`CvAStar.cpp:3914-4047`) [C]. | fork | yes (verification) | see PF13 | S-M | low |
| PF15 | Army multi-path results filled in place | MISSED (low) | `GetMultiplePaths` copies the destination vector, builds each `SPath` by walking parents (`CvAStar.cpp:3390-3440`) [C]. Army part small; the same fill would apply to the trade-path searches (`GetMultiplePaths` self 4.4 % H) [L]. | fork | yes | army est. 0.2 % H; trade-path variant to measure | S | low |
| PF16 | City-connection floods on a thread pool | MISSED (larger, low priority) | Serial floods in `CvCityConnections::UpdateRouteInfo` (`CvCityConnections.cpp:287`) [C]; H 2.65 % / M 0.47 % [L]. A batch on private finders inside one call is legal in our process, but the DLL has no thread pool and the shared finder's counter must be advanced at each original call. | fork + server core budget | yes by design (their verify) | <= 2 % H bound on 4 cores | L | thread safety of every read; core contention with the server's other threads |
| PF17 | City path-length rebuild with parallel middles | MISSED (larger, low priority) | Serial `CvDistanceMapByPathLength::Update` (`CvDistanceMap.cpp:173`) [C]; H 0.7 % / M 1.9 % [L]. | fork | yes by design | <= 1.4 % M bound | L | as PF16 |
| TA1 | Branch and bound in FindUnitForThisMove | MISSED (negligible) | Loop still computes `TurnsToReachTarget` per candidate (`CvTacticalAI.cpp:4461`) [C], but the function is 0.10 % H / 0.15 % M [L]. | fork | only with A10 | ~0.1 % | M | PFHI; not worth it |
| TA2 | Dense distance-to-target tables | MISSED | `GetPlotDistanceToTarget` binary-searches a `ReachablePlots` index per query (`CvTacticalAI.cpp:5665-5677`, `CvAStar.cpp:4321`) [C]. Self H 0.13 % / M 0.68 % [L]. | fork | yes, by construction | est. 0.5 % M, 0.1 % H | S | none |
| TA3 | Zone cities per plot kept; floods as plain BFS | MISSED (high) | (D) `UpdatePlotDistanceToTarget` runs two A* step floods (`CvTacticalAI.cpp:5645-5662`) whose cost is the constant base cost (`PT_*_UNIT_SIMPLE` has no cost function, `CvAStar.cpp:3233-3240`), so BFS gives the same lengths [C]. Reach under `FindAndExecuteBestUnitAssignments` H 4.6 % / M 1.2-1.7 % [L]. (Z) lives in `CalculateMilitaryStrengths` (TA4). | fork | yes, if the BFS still advances the step finder's generation counter (else needs A10) | est. 3 % H (0.6 s), 1 % M | M | record order differs (only `iPathLength` is read); the counter detail |
| TA4 | Unit-major military strength pass | MISSED (high) | Zone x player x unit loop with `plotDistance` per pair (`CvTacticalAnalysisMap.cpp:780-880`) [C]. H 3.3 % (self 2.05) / M 2.3 % [L]; their pass 2.9x faster. | fork | yes (integer sums, their verify) | est. 2 % H (0.45 s), 1.5 % M | S-M | confirm every zone field is an integer sum |
| TA5 | Dominance zones with bitmaps | MISSED | `std::set` of non-city plots, `map` ID lookup, linear neighbour find (`CvTacticalAnalysisMap.cpp:659, 763, 532, 1233`) [C]. Create + neighbourhood H 1.8 % / M 0.8 % [L]. | fork | yes (same visit order) | est. 1.1 % H (0.23 s), 0.5 % M | S-M | low |
| TA6 | Zone cities tested by map rows | MISSED (with TA4) | Prerequisite TA4; same loop [C]. | fork | yes (distance bound, no y wrap) | inside TA4's figure | S | wrap handling |
| TA7 | Far plots skip the closest-city call | MISSED (with TA5) | `GetClosestCityByPlots` called even when the distance already decides (`CvTacticalAnalysisMap.cpp:677-683`) [C]. | fork | yes | inside TA5's figure | S | low |
| TA8 | Cover test asks only map neighbours | MISSED (negligible) | `hasCoverFromOtherUnits` scans every tactical plot (`CvTacticalAI.cpp:8767-8786`) [C]; inlined, too small to see [L]. | fork | yes | ~0 | S | none |
| TA9 | Tactical danger cache as open addressing | MISSED (low) | `CDangerCache` is a `tr1::unordered_map` (`CvTacticalAI.h:1102`) [C]; inlined, no separate share. | fork | yes | <= 0.5 % [I] | S | none |
| TA10 | Previous plot score once per unit | MISSED (low) | `GetPrevPlotScore` per scored plot (`CvTacticalAI.cpp:7810`, 8 call sites) [C]. | fork | yes | <= 0.2 % [I] | S | none |
| TA11 | Movement candidates reused on exact equality | MISSED (low) | Inside `getPreferredAssignmentsForUnit` (`CvTacticalAI.cpp:9023`), H 2.1 % / M 1.2 % inclusive [L]. | fork | yes (verification only) | est. <= 0.5 % | M | equality key must hold every input |
| TA12 | Safe-plot test stops at the first safe plot | MISSED | `gSafePlotCount` counted over every reachable plot with `GetPlotDanger` (`CvTacticalAI.cpp:10300-10306`, also `:11819`); readers only test `> 0` (`:7584, 7945, 8242, 12017`) [C]. | fork | yes | est. <= 0.5 % | S | low |
| TA13 | Plot -> position index table in the simulation | MISSED | `findTactPlot` binary-searches a sorted vector (`CvTacticalAI.cpp:10777-10789`) [C]. H 0.5 % / M 0.7 % [L]. | fork | yes | est. 0.4-0.6 % | S | positions are copy-on-write: stamp per position |
| TA14 | Unread citadel scan skipped | MISSED (measured, quick) | `bMightWantCitadel` is set in every `CvTacticalPlot` (`CvTacticalAI.cpp:8557`) but `isNicePlotForCitadel()` has no caller; the per-turn scan `GetBestCultureBombPlots` behind it (`CvPlayerAI.cpp:3414-3445`) [C] is M 1.6 % (~23 ms/turn) / H 0.1 %, and every sampled call comes from `addTacticalPlot` [L]. | fork | yes (EVENTS_PLOT off for us; keep the closest-city refresh guard) | 1.6 % M, 0.1 % H | S | low |
| TA15 | Move-plot reaches as an ordered batch (parallel) | MISSED (larger, low priority) | Reach from `updateMoveAndAttackPlotsForUnit` H 2.2 % / M 0.4 % [L]; prototype 4674's reach cache already counted tactical reuse (bound 0.21 s H). | fork + thread pool | only with A10 + ordered consumption | <= 1.7 % H bound on 4 cores | L | PFHI, thread safety (tactical globals, shared finders) |
| TA16 | City range strike skips empty plots | MISSED (negligible) | `canRangeStrikeAt` still runs range and line-of-sight before the unit test (`CvCity.cpp:33082-33112`) [C], but 0.02-0.03 % [L]. | fork | yes | ~0 | S | none; skip |
| TA17 | No Sleep(1) in the tactical searches | MISSED (quick, small) | `Sleep(1)` every 500 positions still at `CvTacticalAI.cpp:12429, 12868` [C]. Measured 1.0 % of a huge turn's wall time (2026-11-11, `turn-time-baselines.md` section 4) [L]; now 0.14 % H / 0.35 % M of cycles in kernel transitions [L]; the server's power-throttling opt-out keeps each sleep near 1 ms (`research/05-graphics-composition/lockstep-probe.md` 3.2.1). | fork | yes (a sleep is not game state) | <= 1 % H wall (0.2 s) | S | in the client the UI thread wants the lock: hand over only when a waiter exists |
| HL1 | Same-plot directive lookup | MISSED (low) | `IsBestDirectiveForPlot` takes the directive vector by value and scans all of it per directive (`CvHomelandAI.cpp:3168`) [C]; inlined into `GetWeightedDirectives`, self 0.54 % H / 0.05 % M [L]. An early score bound already exists upstream (`:3264`). | fork | yes | <= 0.5 % H | S | none |
| HL2 | Best non-GP directive score per plot | MISSED (negligible) | Quadratic scan exists (`CvBuilderTaskingAI.cpp:2165`) [C]; inlined into `GetImprovementDirectives`, self <= 0.05 % [L]. Their 63 ms came with more mods. | fork | yes | ~0 | S | skip |
| HL3 | Build-time bound before the expensive tests | MISSED (low) | `CanUnitPerformDirective` and `GetDanger` run before the build-time bound (`CvHomelandAI.cpp:3295-3313`) [C]. `GetWeightedDirectives` H 1.1 % / M 0.3 % [L]. Our `PlayerCanBuild` raise is gated off (`CvUnit.cpp:13397`, EVENTS_PLOT) [C], so their listener guard is moot. | fork | yes, with the clean-danger guard | est. 0.3-0.5 % H | S | lazy danger rebuild must not move |
| HL4 | Worker-only tests hoisted out of the directive loop | MISSED (low) | Per-worker tests sit inside the directive loop (`CvHomelandAI.cpp:3281-3313`) [C]; same family as HL3. | fork | yes | inside HL3's figure | M | low |
| HL5 | Same-plot buckets in FilterOnlyBestImprovements | MISSED (negligible) | Quadratic loop exists (`CvBuilderTaskingAI.cpp:2078-2135`) [C]; inlined, self <= 0.05 % [L]. | fork | yes | ~0 | S | skip |
| HL6 | Worker plot loops walk owned / revealed bitsets | MISSED (negligible) | Whole-map loop exists (`CvBuilderTaskingAI.cpp:2290`) [C]; prerequisite OT1 (not in this part); loop self <= 0.05 % [L]. | fork | yes | ~0 | S + OT1 | skip unless OT1 is taken anyway |
| HL7 | Adjacent-plot build lists kept within a worker pass | MISSED | `ScorePlotBuild`'s neighbour loop calls `CvPlayer::canBuild` per (neighbour, build) (`CvBuilderTaskingAI.cpp:3597`) [C]. `canBuild` under `ScorePlotBuild` M 0.9-1.4 % / H 0.05 %; `ScorePlotBuild` M 3.2 % [L]. | fork | yes (verification) | est. 1 % M | S-M | inputs fixed within a pass |
| HL8 | Plot theft likelihood kept within a worker pass | MISSED (low) | `GetPlotTheftLikelyhood` per `ScorePlotBuild` call (`CvBuilderTaskingAI.cpp:3424`) [C]; inlined. | fork | yes | <= 0.3 % M [I] | S | none |
| HL9 | Own-plot build scores on a thread pool | OPEN | Their own result has no verdict. Measure first: `ScorePlotBuild`'s share on the M series after HL7/HL8 (re-profile m-series, as 4668), then decide whether a pool is worth it. | - | their verify | <= 2.4 % M bound today | L | threads |
| HL10 | Garrison search stops at a 0-turn answer | MISSED | `GetBestUnitToReachTarget` searches every candidate (`CvHomelandAI.cpp:6544-6600`) [C]; H 2.2 % / M 0.8 % [L]. | fork | only with A10 | with HL11 est. 0.5-1 % H | S | PFHI; `SetMovesToTarget` is not written for skipped units (`:6588`): check its readers |
| HL11 | Turn-0 bound in GetBestUnitToReachTarget | MISSED (with HL10) | Same loop [C]. | fork | only with A10 | inside HL10's figure | S | as HL10 |
| HL12 | Worker danger reaches as an ordered batch | MISSED (larger) | `ExecuteWorkerMoves` asks one reach per worker (`CvHomelandAI.cpp:3415, 3433`) [C]; H 4.5 % / M 3.3 % [L]. The 4669 counting run found worker reaches never repeat a key in a turn, so a cache cannot help; a batch is the only route. | fork + thread pool | only with A10 + ordered consumption | <= 3.4 % H / 2.5 % M bound on 4 cores | L | PFHI, thread safety, core budget |
| HL13 | Human seat's automated workers join the batch | MISSED (with HL12) | The second loop (`CvHomelandAI.cpp:3420-3436`) runs only for a human player [C]; nothing in headless all-AI runs. | fork | as HL12 | only in games with a human seat; small | S after HL12 | as HL12 |
| HL14 | Settle-plot loop walks the revealed bitset | MISSED (negligible) | Whole-map loop in `GetBestSettlePlot` (`CvPlayer.cpp:47699`) [C]; its self time 0.10-0.17 % [L]. Prerequisite OT1. | fork | yes | ~0.1 % | S + OT1 | skip |
| HL15 | HaveGoodSettlePlot reads the found-value table | MISSED (negligible) | `CvPlayer.cpp:47651` [C]; 0.01-0.03 % [L]. | fork | yes | ~0 | S | skip |
| HL16 | Ring-plot site values once per plot | MISSED (negligible) | `updatePlotFoundValues` (`CvPlayer.cpp:51111`) 0.08-0.23 % [L]. | fork | yes | ~0.1 % M | S | skip |
| MI1 | Two army-path checks in one walk | MISSED (low) | Forward and reverse `ArmyPathIsGood` per path (`CvMilitaryAI.cpp:837, 848, 868, 879`) [C]; 0.18 % H / 0.03 % M [L]. | fork | yes | <= 0.1 % | S | none |
| MI2 | Target list built once per UpdateAttackTargets | MISSED (negligible) | List rebuilt per `GetArmyPathsFromCity` call (`CvMilitaryAI.cpp:963-980`) [C]; self 0.01 % [L]. | fork | yes | ~0 | S | skip |
| MI3 | Target approach answers kept in the muster loop | MISSED (low) | `SetBestTargetApproach` per target and reverse target (`CvMilitaryAI.cpp:834, 845`) [C]. | fork | yes (verification) | with MI4 | S | low |
| MI4 | Three approach walks fused | MISSED | Three `EvaluateTargetApproach` walks (`CvMilitaryAI.cpp:1061-1063`) [C]; H 1.05 % / M 0.37 % [L]. | fork | yes | est. 0.7 % H (0.15 s), 0.25 % M | S-M | low |
| MI5 | Operation precondition reach kept | OPEN | Waste exists (`CvAIOperation.cpp:3521`) [C], but 0.15 % H / 0.11 % M [L]; their gain (-7 %) was early game (turns 1-74), which our saves do not cover. Measure `PreconditionsAreMet`'s share on an early-game save (about turn 50). | fork | yes (verification, epochs) | unknown early; ~0.1 % at 150/300 | M | invalidation epochs |
| MI6 | AddUnit reuses the grab's travel time | MISSED (low) | `AddUnit` repeats the grab's `TurnsToReachTarget` (`CvArmyAI.cpp:614` vs `CvAIOperation.cpp:479`); max turns can differ, so the reuse needs their equality guard [C]. 0.33 % H / 0.17 % M [L]. | fork | only with A10 | est. 0.3 % H | S | PFHI |
| MI7 | Promotion cache walks only active promotions | MISSED (low) | `UpdateCache` loops every promotion for every terrain and feature (`CvPromotionClasses.cpp:3807-3850`) [C]; 0.04 % H / 0.22 % M [L] (their 372 ms came with more mods/promotions). | fork | yes | 0.2 % M | S | none |
| MI8 | Level-up instant-yield calls that apply nothing | MISSED (negligible) | `CvUnit::promote` 0.03 % [L]. | fork | yes | ~0 | S | skip |
| MI9 | AI_promote returns when no promotion is ready | MISSED (quick) | `CvPlayerAI.cpp:217` calls `AI_promote` for every unit every turn, which tests every promotion (`CvUnit.cpp:32019-32030`) with no ready gate [C]. `AI_promote` M 1.16 % / H 0.10 % [L]. | fork | yes, if `canPromote` is false whenever not ready (check) | est. up to 1 % M | S | the one check |
| MI10 | Promotion child index | MISSED (low) | Quadratic child scan inside `AI_promote` (`CvUnit.cpp:32053-32065`) [C]; inside MI9's share. | fork | yes | inside MI9's figure | S-M | low |
| MI11 | Expiring promotions from a short list | MISSED (negligible) | Full loop at `CvUnit.cpp:3184` [C]; `CvUnit::doTurn` 0.04-0.07 % [L]. | fork | yes | ~0 | S | skip |
| MI12 | City adjacent-unit defence term kept | MISSED (negligible) | City strength terms 0.02 % [L]. | fork | yes | ~0 | M | skip |
| MI13 | City great-general term kept | MISSED (negligible) | `GetAreaEffectModifier` 0.02-0.03 % [L]. | fork | yes | ~0 | M | skip |

### 2.3 Danger (DG)

| ID | Item | Verdict | Reason and evidence | Where | Exact? | Value for us | Cost | Risk |
|---|---|---|---|---|---|---|---|---|
| DG1 | City danger power table in `IsInDangerFromPlayers` | **MISSED** | 0437 found diplomacy's `IsInDangerFromPlayers` triggering danger rebuilds worth 5.1 points of the H turn (9.8 rank 5); the table removes the ring-4 scans inside one diplomacy re-evaluation | DLL fork, define | exact (no unit moves in the scope) | medium [I] | M | low; needs the scope's invalidation at war/peace and unit validation |
| DG2 | Same table over the diplomacy war-state block | MISSED (low) | Prerequisite DG1; ~100 scans per turn | DLL | exact | low | S after DG1 | low |
| DG3 | Static per-plot turn-damage table | MISSED (low) | Below their noise (<= 12.5 ms) | DLL | exact | low | S | low |
| DG4 | Ranged-attack tiles as a stamped array | MISSED | Set container replaced by a stamped array; 50.9 ms per their turn | DLL | exact | low-medium (~0.1 s H [I]) | S | low |
| DG5 | Reach answers kept within `DoUpdateWarStates` | IN WORK (overlap) | Narrow form of 4674's danger-reach cache (prototype b: ~1.25 s of H, identical games [L/I]); needs A10 (PFHI) in their port | DLL | exact with PFHI | covered by 4674 | - | - |
| DG6 | Enemy-unit reach shared across danger rebuilds | **IN WORK** (4674) | Same idea as our prototype (b) (`x64-proto-0437-reach`, `VP64_PROTO_REACH`): theirs keyed by unit state + a write epoch, ours by the full search data + plot stamps. Their quiet-laptop gain was only -1.8 %; their simpler reach memo lost on a quiet PGO machine (R3): our testbed pair S3 vs S3r is therefore decisive | DLL fork | exact by verification (both ports) | ours: attribution ~1.25 s of 20.9 s H; timing pending | - | invalidation completeness; verify mode exists in both |
| DG7 | Danger derive step shared across observers | MISSED (after 4674) | Builds on DG6: replays the attack-tile derivation per observer; -1.4 % H on their quiet laptop, PGO | DLL | exact by verification | medium (~0.3 s H [I]) | M | as DG6 |
| DG8 | Simulated attack damage kept within one assignment search | MISSED | Pure estimate memo inside `FindBestUnitAssignments`; 2.31 M calls verified, untimed | DLL | exact (static write scan) | low-medium, untimed: **OPEN** (share of `GetSimulatedDamageFromAttackOnUnit` in our H profile) | S | low; off with `MOD_EVENTS_CAN_MOVE_INTO` |

### 2.4 Economic, purchase and hurry (EC)

All DLL-internal, server CPU only (F7); none crosses the IPC except where noted.

| ID | Item | Verdict | Reason and evidence | Where | Exact? | Value for us | Cost | Risk |
|---|---|---|---|---|---|---|---|---|
| EC1 | `IsCanPurchase` skips unread work | MISSED (low) | Their quiet-laptop B/A 1.005 (no verdict) for EC1-EC3 together | DLL | exact | low | S | low |
| EC2 | Total building count kept per player | MISSED (low) | Needs a complete writer list (city list, buildings, puppets, a trait) | DLL | exact by writer list | low | S-M | medium (writer list) |
| EC3 | Sparse building-class requirement scans | MISSED (low) | Static info index | DLL | exact | low | S | low |
| EC4 | `GetBuildingYieldValue` sparse loops | **MISSED** | Static info indexes; their policy term 296.5 -> 10.0 ms and resource term 156 -> 6 ms over 3 turns | DLL | exact (info data immutable after load) | medium (~0.2-0.3 s per H turn [I]) | M (7 loops) | low |
| EC5 | Info-only zero flag per (building, yield) | MISSED (low) | Static flag | DLL | exact | low | S | low |
| EC6 | Maps read by reference | MISSED (low; half done upstream) | At the pin `GetYieldChangesFromAccomplishments` already returns a const reference, `GetTechEnhancedYields` still returns `std::map` by value (`CvBuildingClasses.h`:674-676 @pin [C]) | DLL | exact | low (their 43.5 -> 30.6 ms) | S | none |
| EC7 | Sparse prerequisite loop in `CheckBuildingBuildSanity` | MISSED (low) | Loop-invariant hoist + sparse visit | DLL | exact | low | S | low |
| EC8 | Victory type IDs pinned | MISSED (low) | Part of OT5 | DLL | exact | low | S | none |
| EC9 | Advisor recommender tables lazy and sparse | MISSED (low) | The first city-screen call's table build (their 272 ms) runs in the server; moving it after load helps the human's first city screen | DLL | exact | low (UI latency once per game) | S | low |
| EC10 | Follower counts once per `doInstantYield` | MISSED (low) | `CvPlayer::doInstantYield` @pin `CvPlayer.cpp`:25939 [C, function present; loop not read] | DLL | exact | low, untimed | S | low |

### 2.5 City production, buildings and city AI (CB)

| ID | Item | Verdict | Reason and evidence | Where | Exact? | Value for us | Cost | Risk |
|---|---|---|---|---|---|---|---|---|
| CB1 | `canConstruct` prerequisite-class index | MISSED (low) | Static index; only city-screen timings | DLL | exact | low | S | low |
| CB2 | Three sparse scans per `canConstruct` | MISSED (low) | Static indexes; no verdict in theirs | DLL | exact | low | S | low |
| CB3 | Building-class happiness index | MISSED (low) | Static index | DLL | exact | low | S | low |
| CB4 | Class happiness terms kept in `processBuilding` | MISSED (low) | ~6 ms | DLL | exact | low | S | low |
| CB5 | Zero-delta yield iterations skipped in `processBuilding` | MISSED (low) | 14.26 M no-op iterations | DLL | exact | low-medium | S | none |
| CB6 | All-zero rows skipped in `processBuilding` | MISSED | Prerequisite CB5; ~42 ms | DLL | exact | low-medium | S | none |
| CB7 | Irrelevant buildings skip the class-happiness update | **MISSED** | 456.7 -> 32.2 ms in their run; VP has many dummy buildings | DLL | exact (needs the input-writer check) | medium [I] | M | medium (writer list) |
| CB8 | No tooltip text without a sink | MISSED | Still present @pin: the OR branch of `CvCity::IsBuildingLocalResourceValid` formats a localized tooltip into a temporary on every missing resource whether or not the caller wants one (`CvCity.cpp`:9883-9897 @pin [C]); text is local to the server (F7), so CPU only | DLL | exact | low-medium (their -28/-35 ms per AI turn, quiet, PGO) | S | none |
| CB9 | Yield-rate tooltip strings built only when asked | MISSED (low) | `getYieldRateTimes100` 8-argument form @pin `CvCity.cpp`:23065 [C, present; body not read] | DLL | exact | low | S | none |
| CB10 | Flavor update applies only changed flavors | **MISSED** | 62.9 -> 8.6 ms per their turn | DLL | exact (integer) | medium (~0.1 s H [I]) | S-M | low |
| CB11 | City strategy branch by ordinal | MISSED (low) | 58 string compares per strategy @pin (`CvCityStrategyAI::DoTurn`, `CvCityStrategyAI.cpp`:1404 [C]) | DLL | exact | low (<= ~20 ms theirs) | S | none |
| CB12 | Event requirement lists | MISSED (low) | < 0.8 ms | DLL | exact | low | S | none |
| CB13 | Event-choice class loop skips zero rows | MISSED (low) | untimed | DLL | exact | low | S | none |
| CB14 | Light refills inside `SetWorkingPlot` | MISSED (low) | Exact only by a read census of the closing refill; untimed | DLL | exact by census | low, untimed | M | medium (census must be redone on 5.4.8) |
| CB15 | Dead happiness half after `UpdateReligion` | MISSED (low) | Dead store at two call sites | DLL | exact | low | S | low |
| CB16 | Corporation franchise yield walks present buildings | **MISSED** | Still present @pin: `UpdateYieldFromCorporationFranchises` loops every building class (`CvCity.cpp`:25798 [C]); their `Corporations_DoTurn` ~340 -> ~6 ms per pump | DLL | exact | medium-high once corporations exist (~0.5 s H late game [I]) | S | low (keep the class-cache side effect) |

### 2.6 Trade (TR)

| ID | Item | Verdict | Reason and evidence | Where | Exact? | Value for us | Cost | Risk |
|---|---|---|---|---|---|---|---|---|
| TR1 | Trade-path searches on a thread pool | **MISSED (larger)** | The trade-path family is 26 % of our H turn (F5); their batch keeps the serial order (private finders, ordered consumption, shared-finder counter accounted) and gave B/A 0.94 quiet. Our DLL is single-threaded by design; 0437 found little cross-player parallel headroom (rank 6), but this is parallelism inside one fill, which that count did not cover | DLL fork, define | exact by ordered consumption | high on H [I]: up to several % of the turn with 4 cores; less after 4673 | L | high: every read inside the search callbacks must be thread-safe (lazy caches, danger reads) |
| TR2 | Trade-path scoring rows on the pool | MISSED (larger) | Prerequisite TR1; B/A 0.96 | DLL | exact | medium-high on H [I] | M after TR1 | as TR1 |
| TR3 | Trade-path answers reused while their plots are unchanged | **IN WORK** (4673) | Same idea as our prototype (a) (`x64-proto-0437`: search reused while a fingerprint of every touched plot holds; identical games; ~0.8-0.9 s of H by attribution [L/I]). Their quiet result B/A 0.981 is consistent with ours being far below 0437's counted bound | DLL fork | exact | ours: ~4 % of H (attribution); timing pending | - | writer list |
| TR4 | Plot generations bumped only on search-relevant change | MISSED (on top of 4673) | Their +18-22 % more reuse on top of TR3; 4673 found 78 % of re-run searches see a real input change yet find the same paths, so a narrower "search-relevant" signature is the next lever | DLL fork (4673's branch) | exact if the signature is complete | medium [I] | M | medium (signature completeness) |
| TR5 | Destination sort once per published list | MISSED (low) | `GetMultiplePaths` sorts on every call | DLL | exact | low | S | none |
| TR6 | Base net gold once per route-priority step | MISSED (low) | ~14.9 ms | DLL | exact | low | S | low |
| TR7 | Furthest possible route from the cache row | **MISSED** | Still present @pin: `UpdateFurthestPossibleTradeRoute` loops every city of every player with `CanCreateTradeRoute` + `GetValidTradeRoutePathLength` (`CvTradeClasses.cpp`:5243-5290 [C]); their gate on a PlayerCanCreateTradeRoute listener (none in our set) | DLL | exact (with the listener fallback) | medium-high (their ~100-190 ms; ~0.2-0.4 s H [I]) | S-M | low |
| TR8 | Trade turn modifier walks only relevant buildings | **MISSED** | Still present @pin: every city x every building class (`CvTradeClasses.cpp`:5416-5430 [C]) | DLL | exact | medium (their ~82 ms) | S | none |
| TR9 | `GetAvailableTR` second pass replays the first | MISSED (low) | ~12.8 ms | DLL | exact (listener fallback) | low | S | low |

### 2.7 Diplomacy, city-states, espionage (DI); religion (RE); policy, tech and era (PO)

| ID | Item | Verdict | Reason and evidence | Where | Exact? | Value for us | Cost | Risk |
|---|---|---|---|---|---|---|---|---|
| DI1 | City-state bonus yield passes skipped when nothing changes | **MISSED** | Still present @pin: `CvMinorCivAI::DoSetBonus` runs `updateYield()` over the major's cities per yield type (`CvMinorCivAI.cpp`:11584-11660 [C]); their pumps 573/1077/85 -> 68/153/14 ms | DLL | exact (byte-image verify in theirs) | high, bursty (friendship / alliance changes; frequent with 20 city-states) | S-M | low |
| DI2 | Congress yield passes restricted | **MISSED** | Their Congress session 1,449 -> 91 ms | DLL | exact (falls back on a Lua dispatch) | high, bursty (one turn per session) | M | low |
| DI3 | Diplomacy response rows kept | MISSED (low) | Database is local (F7); CPU only | DLL | exact | low | S | low |
| DI4 | Espionage mission scores kept per call | MISSED (low) | ~14.3 ms | DLL | exact | low | S | low |
| DI5 | Sparse locked-class loop in city-state quests | MISSED (low) | ~13.8 ms | DLL | exact | low | S | none |
| RE1 | Religion spread skips connection tests for non-majority religions | MISSED (low) | ~10-12 ms per pump; keeps the trade-path cache triggers in order (important: those triggers are rebuild points of 4673's family) | DLL | exact by proof | low | S | low |
| RE2 | Both trade-pressure answers in one walk | MISSED (low) | ~0.5 ms | DLL | exact | low | S | none |
| RE3 | Desired faith great person asked once | MISSED (low) | Still present @pin: two back-to-back calls (`CvReligionClasses.cpp`:7217, 7219 [C]); the skipped call raises CityCanTrain/PlayerCanTrain, which cross our IPC until LU1 | DLL | exact | low | S | none |
| PO1 | Counts once per policy choice | MISSED (low) | untimed | DLL | exact | low | S | none |
| PO2 | Unit sanity values once per policy choice | MISSED | 6,439 -> 366 calls per decision; the exact alternative to B4 | DLL | exact | low-medium | S | low (settler/sea-worker kept, shared step finder) |
| PO3 | Era-change yield passes restricted | **MISSED** | Still present @pin: `CvTeam::SetCurrentEra`'s feature loop with a yield pass per changed feature (`CvTeam.cpp`:8629-8680 [C]); their era change 1,373 -> 63 ms | DLL | exact | high, bursty (each team's era change) | M | low |

### 2.8 Memory and buffers (MB), build settings (BS), other (OT)

| ID | Item | Verdict | Reason and evidence | Where | Exact? | Value for us | Cost | Risk |
|---|---|---|---|---|---|---|---|---|
| MB1 | Tactical simulation wipes only used positions | MISSED | 6,000 slots wiped per simulating player @pin (`CvTacticalAI.cpp`:43, `CvTactPosStorage::reset` :12921 [C]) | DLL | exact | low-medium | S | none |
| MB2 | Simulation vectors keep their buffers | **MISSED** | Allocation is 4.6 % of H, 6-7 % of M (0437 rank 7); their -2.1/-2.2 % on a quiet laptop | DLL | exact | medium (~0.3-0.4 s H [I]) | S | memory: buffers kept up to 32 MiB (server is 64-bit: fine) |
| MB3 | Flat unit lists in the simulation | MISSED (low) | Container swap | DLL | exact | low, untimed | M | low |
| MB4 | Pooled temporaries in tactical / homeland code | MISSED | Part of 0437 rank 7 (allocation pools) | DLL | exact | low-medium | S-M | low (no pointer-ordered iteration known to depend on it: check) |
| MB5 | Danger attacker vectors keep small buffers | MISSED (low) | untimed | DLL | exact | low | S | none |
| MB6 | Plot save fields in one block write | MISSED (low) | Writes are local (F8), so only CPU; part of the client's 0.56 s autosave per turn; **OPEN**: split the autosave's 0.56 s into DLL serialization, transfer and EXE write before choosing | DLL | exact (byte-identical saves) | low-medium | S | endianness only |
| BS1 | PGO + LTCG build | **MISSED (planned)** | Rank 2 of 0437 (no prototype needed). We already have full LTO, so only PGO's part applies: their loaded desktop gave LTCG 0.954 and PGO 0.806 of the plain build, i.e. ~15 % on top of whole-program optimisation [I]; with clang that is IR instrumentation and `-fprofile-use` on the LTO link. Their lessons: path instrumentation was too slow for late turns; a training set of late + early turns; retrain when < 90 % of profiled code still matches (~5 days of changes) | x64 DLL build (D107 configuration choice is the MC's) | exact by construction (identity test still decides: floating point) | **high**: ~10-15 % of every server turn [I] | M (+ upkeep at each pin move) | low-medium: training cost, a stale profile only loses speed |
| BS2 | Latched switch reads without init guards | N/A | We use compile-time defines, no run-time switches in hot code | - | - | - | - | - |
| OT1 | Owned / revealed / visible plot bitsets | MISSED (larger) | Base of HL6, HL14, OT2; setter hooks for every owner/reveal/visibility change | DLL | exact (shadow-mode checked in theirs) | medium together with HL6/HL14 (their batch -5.8 %) | M-L | medium (every setter) |
| OT2 | Delayed visibility walks the visible bitset | MISSED | Prerequisite OT1 | DLL | exact | low-medium | S after OT1 | low |
| OT3 | Visibility change skips the unit loop on quiet plots | MISSED | ~48 ms; 0437 counted 164,000 visibility changes per H turn (9.8) | DLL | exact | medium on H [I] | S | low |
| OT4 | Resource-count asserts in one map walk | MISSED | Asserts are compiled in (F4) and the per-resource map walk is present @pin (`CvPlayer::setTurnActive`, `CvPlayer.cpp`:33756 ff [C]); per major per turn | DLL | exact | low-medium on H (map 12,320 plots x resources x majors) | S | none |
| OT5 | 75 info-type string lookups pinned | MISSED (low) | 852 literal `getInfoTypeForString("...")` call sites in the DLL @pin [C: count]; only the hot ones matter: **OPEN** (their hot list of 75 against our H profile) | DLL | exact | low-medium | S-M | none (re-resolve after a database reload) |
| OT6 | Lake / river feature IDs pinned in `HasFeature` | MISSED (low) | 6.3 M calls in theirs | DLL | exact | low | S | none |
| OT7 | Great-work index | MISSED (low) | UI-driven (culture overview); the UI's calls cross, the DLL side is CPU | DLL | exact | low | M | low |
| OT8 | Table max-rowid answers kept during data load | MISSED (low) | Our server's database is local (F7); load time only; **OPEN**: `PrefetchGameData`'s share of the server's load | DLL | exact | low (load) | S | low |
| OT9 | Pool threads woken by count | N/A unless TR1 | Only matters with a thread pool | - | - | - | - | - |

### 2.9 "Built, kept OFF" (their exact items they did not enable)

Their negative or unproven results, recorded as theirs [I]; none is recommended for us unless named.

| ID | Their switch | Their result | Our verdict |
|---|---|---|---|
| K1 | trade-path input generation (skip a whole fill) | 8-10 % of fills on top of TR3 | IN WORK as a sub-idea of 4673; low |
| K2 | reach dead skip | superseded by PF7 | N/A |
| K3 | best-wonder memo | inside noise | not pursued |
| K4 | updateTimers skip | no gain above noise | not pursued |
| K5 | tactical presence view (copy-on-write) | B/A 0.997 | not pursued |
| K6 | harassment reach memo | B/A 0.989, invalidation never exercised | not pursued |
| K7 | harassment reach pre-test | no verify run | not pursued |
| K8 | settle value tested first | verify owed | not pursued |
| K9 | war-type predicate hoist | ~0.4 ms per 3 turns | not pursued |
| K10 | reinforcement zone reach memo | no win | not pursued |
| K11 | builder route memo | no separable saving | not pursued |
| K12 | hurry base-modifier const ref | inert | N/A |
| K13 | batched era-change writes | superseded by PO3 | N/A |
| K14 | danger reaches on a pool | no win, quiet | not pursued (supports 0437's rank 6 finding: little parallel headroom in danger) |
| K15 | parallel preferred assignments | 2.0-2.7 s per turn slower | not pursued |
| K16 | AI team wake | neutral | not pursued |
| K17 | army path batch / cross batch | no win / no verdict | not pursued |
| K18 | parallel wonder choice | no win | not pursued |
| K19 | reach by city distance | B/A 0.987, no verdict | not pursued |
| K20 | lift target memo, danger owner job, city build list off main | no gain, closed | not pursued |
| K21 | parallel homeland phase | slower | not pursued |
| K22 | unit build index | inert | N/A |

### 2.10 "Lessons" (theirs, [I] unless we confirmed them)

| ID | Their lesson | Our status |
|---|---|---|
| L1 | Small caches often lose; a key that misses an input returns wrong answers | Consistent with 0437: 4669 found only 1.9-4.6 % of reach calls repeat with nothing changed (a global epoch is useless), and 4673/4674 needed full input fingerprints and verify modes [L] |
| L2 | Prefetching on threads can cost more than it saves | Not tested here; noted for TR1 |
| L3 | Some pre-tests cost more than the work they skip | Noted |
| L4 | Parallelism is small in VP; only order-keeping batches won (2-6 % each) | Confirmed in kind by 0437 rank 6: counted <= 0.12 s of H for parallel first builds [L, counting runs 2026-11-17] |
| L5 | AVX2 slower and unportable; safebuffers useless; clang-cl 22 builds the DLL | We already build x64 with clang-cl (F4): DONE in kind; AVX2 not to try |
| L6 | Measure on a quiet machine with the final compiler settings | DONE: our rule (COMMON 23, D106, D178: quiet testbed pairs, grade A/B) |
| L7 | Verify modes (run the original, compare, seeded negative control) pay for themselves | DONE in kind: D30 verify mode for the proxy mirror; 4673/4674 verify modes; identity tests (D164). Adopt the seeded negative control for every DLL cache we build |

### 2.11 Behaviour-changing items (SPEEDUP_DEVIATIONS.md)

| ID | Item | Verdict | Reason and evidence | Where | Value for us | Cost | Risk |
|---|---|---|---|---|---|---|---|
| A1 | Candidate-move lists reused in the battle simulation | MISSED (behaviour change: the owner's decision) | Their largest AI item after PGO (-2.4 to -10.8 % of update on the human-seat late game, quiet, PGO); same winner in their whole-game check; changes war details by design | DLL fork, define, off by default | high [I] | M | high: changes AI fights; their first version crashed on a missing plot (keep the guard) |
| A2 | One diplomacy re-evaluation per war cascade | MISSED (behaviour change) | -0.4 s per turn median on their quiet laptop, almost all on cascade turns; small later drift | DLL fork, define | medium-high, bursty (multi-civ war declarations; frequent with 42 majors in the stress map) | M | medium |
| A3 | Scouts reuse a "nothing to explore" answer | not recommended | Small saving, visible war-timing drift (their own low priority) | - | low | - | - |
| A4 | Skip the single-unit opportunity attack with nothing near | MISSED (behaviour change, small) | 93-139 ms pre-PGO; 6 of 560 skipped calls acted in theirs (heal-in-place reproduced later) | DLL fork, define | low-medium | S | low-medium |
| A5 | No camp walk off a landmass without embarking | **OPEN** | 6.96 s of 7.11 s of candidate search over 10 of their turns skipped, 0 of 2,231 found a path; not exact only through unrevealed water or ice. Measure first: the share of `FindUnitsForHarassing`'s NO_EMBARK `TurnsToReachTarget` in our H profile; if large, look for an exact form (a landmass test that also checks the reveal/ice cases) | DLL fork, define | possibly high [I] | S-M | low if made exact |
| A6 | Barbarian-camp travel time kept 3 turns | not recommended | Untimed; stale answers possible | - | unknown | - | - |
| A7 | Worker directive answers kept within one worker pass | OPEN | Not exact by proof but 0 of 3.4 M mismatches; their function cost 487 ms; compare with HL1-HL4 (exact) first | DLL | medium [I] | M | medium |
| A8 | Rush-buy scan tests the cheap condition first | N/A (subsumed by LU1) | Its saving is fewer PlayerCanConstruct / CityCanConstruct raises; none has a listener in our set; decisions identical | - | none beyond LU1 | - | - |
| A9 | AI city specialization skips already-built wonders | **MISSED (VP defect fix)** | Still present @pin: `CvWonderProductionAI::ChooseWonder` keeps any building whose class carries the city's saved best-for-wonder flag, with no already-built test before `CheckBuildingBuildSanity` (`CvWonderProductionAI.cpp`:136-175 [C]); whether the sanity check rejects maxed wonders was not read. Their ChooseWonder 66 -> 8 ms; 14 of 20 picks were built wonders | DLL fork define, then upstream bug report (D104); increment 4 "bugfix" category (D169) | low-medium time; AI quality | S | changes AI production from the first affected turn: the owner's decision as a bugfix |
| A10 | Path-finder cache facts expire before every search (PFHI) | **MISSED (VP defect fix; prerequisite)** | The bug is present @pin: the node cache is tagged with a 16-bit generation that wraps to 1 (`CvAStar.h`:243, `CvAStarNode.h`:104, `CvAStar.cpp`:355-357 [C]); a node last stamped exactly 65,534 searches earlier is taken as fresh. **Not rare on H**: 4669 counted 72,692 danger-reach searches alone in 5 H turns, so a finder wraps every few turns [I from [L] counts]; the upstream end-turn danger cache (`472e1163d`) has the same exposure. Prerequisite for the PFHI items (PF5, PF7, TA1, TA15, HL10-HL12, MI6, and TA3 unless the BFS keeps advancing the counter). Also a candidate cause of headless-vs-windowed divergence (UI-triggered searches move the counter) [I] | DLL fork define; upstream bug report (D104) | its own cost untimed; enables ~3-6 % of H in the PFHI items [I] | S-M | changes (corrects) the game from the first stale read after a wrap: the owner's decision as a bugfix (D169); identity pairs must compare A10-on against A10-on |
| A11 | Unit-flag dim events: last one per unit per batch | **IN WORK** (4677) | 4677 coalesces dim-flag records per unit in the DLL at the source (queue B records, each worth a frame of the drain), stronger than their Lua-call coalescing. Their listener audit (VP-EUI `UnitFlagManager.lua` sets alpha from (owner, unit, isDim) only; base `UnitMemberOverlay.lua`) and their census (60.6 % superseded) are useful to 4677 | DLL fork (`x64-proto-0437-qmove`) | part of 4677's -1.1 s per M turn model | - | - |
| A12 | Unit position / teleport events: last one per unit per batch | **IN WORK** (4677) | 4677: one teleport per move segment in `CvUnit::move`; the EXE's `LocalMachineUnitPositionChanged` falls with it. Their finding that `HexFOWStateChanged` is only 0.3 % superseded agrees with 4672's rank 4 (FOW dispatch < 0.1 s) | DLL fork | as A11 | - | - |
| A13 | Civ list rebuilt once per AI phase | DONE (side effect) / OPEN (speed) | Their side-effect finding (EUI's civ-list rebuild calls `CalculateGoldRate()`, which stored a saved treasury member the AI read) is **already fixed in our fork**: bug 0116, one-off 9044, fork commit `84d4d4e9e` on both pins: `GetExpensePerTurnUnitMaintenance` computes on each read and the saved member is set at a fixed point in `DoGold` (`CvTreasury.cpp`:70-75, 593-600 @04b1211b8 [C]; EUI still calls it, `NotificationPanel.lua`:1525 [C]). The speed part: our UI Lua runs at the EXE's pace (F9); with options 2-4 the rebuild probably runs about once per AI phase. Measure: `UpdateCivListNow` calls and time per client AI phase with options 2+3+4 | (study) | low [I] | S (count) | none for identity (0116 removed the coupling) |
| A14 | Tech panel: no refresh while hidden | OPEN | `(1) Community Patch/Core Files/Overrides/TechPanel.lua` is in our set; the hidden-skip helps on every `SerialEventGameDataDirty` delivery; with options 2-4 deliveries per AI phase are fewer. Measure the panel's handler time per client turn | UI Lua (CP, upstream) | low-medium [I] | S | visual only |
| A15 | UI update scripts at most ~10 per second in the AI phase | DONE in kind | Our UI runs in the EXE at its own frame pace; D165 options 2-4 already decouple the AI phase from UI passes (d165-pacing-fix-design.md) | proxy (existing) | - | - | - |
| A16 | In-game editor overlay filled after the panel opens | N/A | The In-Game Editor mod is not in our set | - | - | - | - |
| B1 | CityCanConstruct answers kept per turn | N/A (subsumed by LU1) | No listener in our set | - | - | - | - |
| B2 | Prophet conversion answer kept 4 turns | not recommended | Ceiling 55-100 ms; no player-POV A/B in theirs | - | low | - | - |
| B3 | PlayerCanConstruct asked once per player in the rush-buy scan | N/A (subsumed by LU1) | No listener in our set | - | - | - | - |
| B4 | Policy AI skips unused unit sanity checks | not recommended | PO2 gives the saving exactly; B4 moves a tactical-map refresh | - | - | - | - |

### 2.12 "Tried and rejected / parked" (theirs, [I])

| ID | Their item | Their reason | Our status |
|---|---|---|---|
| R1 | Stale danger maps after war changes | Rejected: AI reacts a turn late, breaks AI combat | Agrees with our rule: only exact danger cuts (4674 is exact) |
| R2 | Danger maps ignore unseen units in unscouted cities | Parked: war waves moved, cause unresolved | Same |
| R3 | Path-free army recruiting; enemy-reach memo for danger | Play-identical but slower on a quiet PGO machine (+170 / +255 ms per turn pair) | **Warning for 4674**: the timing pair on a quiet testbed decides, not the attribution; repeat after PGO lands |
| R4 | Wider opportunity-attack skips | Changed decisions from T293 | A4's radius only |
| R5 | Builder re-plan cadence; workers keep jobs | AI weaker (games won 6-12 turns earlier) | Not for us (gameplay) |
| R6 | Civilian re-target, city re-plan cadence, nearest-4 harass | No gain (within noise); their estimates 5-10x too high | Noted: estimate with counts, as 0437 does |
| R7 | Lighter battle simulation, attack targets every 2nd turn | Not built: AI fights worse | Not for us |
| R8 | Worker list re-scored every 4 turns | 23 % stale, game diverged, no speedup | Not for us |
| R9 | Pruned prophet scoring | Cost moved to the next caller (trade-path fills) | Noted: trade-path rebuilds are triggered lazily by other readers (as 0437's 4669 found) |
| R10 | Rush-buy world-conquest stamp | Inside noise | - |
| R11 | Memo of identical path queries | 39 repeats per late turn, < 1 % | Agrees with 0437's counts (repeats need per-unit invalidation) |
| R12 | Simultaneous AI rounds | 1.5 s per late turn SLOWER; parallel part ~8 ms of 3.4 s | Agrees with 0437 rank 6 |
| R13 | Peaceful AIs plan in parallel | Not built: 3.7 % overlap possible | Agrees |
| R14 | Random numbers | VP mixes map seed, turn and call site, so skipped work rarely shifts later rolls | Useful for our identity debugging: look for the first differing decision input [I; `CvSeeder::fromRaw(...).mix(...)` seen at the pin, e.g. `CvWonderProductionAI.cpp`:186 [C]] |

## 3. Ranked MISSED items for the performance team's epic proposal

Ranked by expected value in our setting against cost and risk. Values are [I] unless marked; none is a timing claim
(D106): each needs its identity pair and a quiet testbed pair.

### 3.1 Quick independent items (exact, small, no prerequisite)

Exact, no prerequisite, cost S (S-M where marked); each can be its own small ticket on a fork branch (or server/proxy for
the first), with an identity pair and, for a default, a quiet testbed pair.

1. **LU1 part (a): GameEvents args kept in the server, one crossing per raise** (server `remote/` + proxy; M). Exact by
   construction; acts on the 0.94 s of GameEvents traffic in a client M turn (F1), which becomes critical path once the
   drain fixes are on. The only item here that cuts IPC rather than DLL CPU.
2. **TA14** unread citadel scan: 1.6 % of M [L], dead code at the pin [C]; the safest change in the list.
3. **DI1** city-state bonus passes: high on the turns friendships change (frequent with 20 city-states) [I]; S-M.
4. **CB16** corporation franchise yield: their ~340 -> ~6 ms per pump, late game [I].
5. **TR7** furthest possible trade route from the cache row: ~0.2-0.4 s of H [I]; S-M.
6. **MB2** simulation vectors keep their buffers: allocation is 4.6-7 % of our turn; theirs -2.1/-2.2 % quiet.
7. **PF2** one `canEnterTerrain` evaluation: ~0.7 % of H [L-based est.].
8. **MI9** ready gate in `AI_promote`: up to 1 % of M, after one check of `canPromote`.
9. **PF4** obsolete child before `PathCost` (keep the clean-danger guard): est. 1-2 %.
10. **TR8**, **CB10**, **CB8** (present at the pin), **OT3**, **OT4** (asserts compiled in), **MB1**, **DG4**, **TA2 + TA13**,
    **TA17** (`Sleep(1)` only when a waiter exists): each ~0.3-1 % or less.
11. Small and safe when touching the same files: EC6 (half done upstream), CB5/CB6, CB11, RE3, PF8, PF9, PF12, PF13, TA12,
    PF3, LU12 (proxy), EC1-EC3, EC5, EC7-EC10, CB1-CB4, CB9, CB12-CB13, CB15, TR5, TR6, TR9, DI3-DI5, RE1, RE2, PO1, PO2,
    DG3, MB5, OT6.

Not additive: PF2, PF4-PF7 and PF1 cut the same unit-finder callbacks; PF8 shrinks after TA3.

### 3.2 Larger pieces of work

1. **BS1 PGO** on the x64 DLL's LTO link: ~10-15 % of every server turn [I]; a build decision (D107 configuration, training set,
   retrain at each pin move). Lowest identity risk of the large items; also changes what the others are worth, so measure
   the others after it where possible (their R3 lesson).
2. **LU1 part (b): a listener-presence mirror** so a raise without a listener costs no crossing at all: needs a design read of
   how the EXE registers `GameEvents` listeners (not in the knowledge base) and a generation the EXE side bumps; fail loud,
   verify mode, windowed identity pair. With (a), up to most of the 0.8-0.9 s per client M turn.
3. **A10 as a VP defect fix** (16-bit path-finder stamp; wraps every few turns on H): the owner's decision as a bugfix (D169) and
   an upstream report; it unlocks the exact PFHI items: **PF5 + PF7** (neighbour fills, est. 1.5-3 % H), **HL10 + HL11**,
   **MI6**, and TA3 without the counter detail.
4. **Tactical batch**: **TA3** (BFS floods, est. 3 % H), **TA4 + TA6** (est. 2 % H), **TA5 + TA7** (est. 1.1 % H): exact,
   one area (`CvTacticalAI` / `CvTacticalAnalysisMap`), ~1 s of H together [I].
5. **PF1** six per-search kept values in the unit finder (est. 1.5-3 % H, 3-6 % M; the largest M item) and **PF6**
   (bound/dead neighbour skip): one path-finder batch with its own verify mode.
6. **Bursty yield passes**: **DI2** (Congress session 1,449 -> 91 ms theirs) and **PO3** (era change 1,373 -> 63 ms), with
   DI1 if not done above: they flatten the worst single turns.
7. **Static indexes and kept values with writer lists**: **EC4** (est. 0.2-0.3 s H), **CB7**, **DG1 + DG2**, **HL7**, **MI4 + MI3**.
8. **On top of 0437's prototypes**: **TR4** (narrower search signature for 4673's trade reuse) and **DG7** (danger derive
   step shared across observers, after 4674), each only after the prototype's quiet testbed pair.
9. **Parallel batches inside one call** (ordered consumption, private finders, a thread pool in the DLL): **TR1 + TR2**
   (trade-path searches and scoring; the family is 26 % of H), **HL12** (worker reaches; never repeat a key, so no cache can
   replace it; bound 3.4 % H), **PF16**, **TA15**, **PF17**: L cost and thread-safety risk; the other port's own lesson is
   2-6 % per working batch.
10. **OT1 + OT2** plot bitsets (with HL6, HL14): M-L for little measured value; only if another item needs them.

Behaviour changes, for the owner's decision rather than the epic's default: **A9** (VP defect: built wonders picked for city
specialization; fits D169's bugfix category), **A1** (largest AI item, changes fights), **A2** (war-cascade diplomacy,
bursty), **A4** (small). Not recommended: A3, A6, B2, B4.

Measurements to name in the proposal (the OPEN rows): GameEvents raises per client turn **by name** on M and H (LU1);
city-screen refresh time in the client (LU11); the share of `FindUnitsForHarassing`'s no-embark searches on H (A5); civ-list
rebuilds and tech-panel handler time per client AI phase with options 2+3+4 (A13, A14); the autosave's 0.56 s split (MB6);
`GetSimulatedDamageFromAttackOnUnit`'s share (DG8); the hot `getInfoTypeForString` sites (OT5); `PrefetchGameData`'s share of
the server's load (OT8); `ScorePlotBuild` after HL7/HL8 (HL9); an early-game save at ~turn 50 for MI5 and A7.

## 4. Findings for other work

- **For the hook path (F1, LU1):** the GameEvents traffic is ~4-6 crossings per raise with no batching
  (`RemoteArgs`, [C]) and the raises in our mod set are almost all without a listener [C/I]; the D165 note's "a
  GameEvents short-cut on the server is a separate study" is LU1. Part (a), local args with one crossing per raise, needs
  no knowledge of the EXE and is exact by construction.
- **A13 confirms our bug 0116:** the other port found independently that EUI's civ-list rebuild made AI decisions depend on how often the UI ran (`CalculateGoldRate` storing a saved treasury value the AI read); our fork fixed exactly that on 2026-11-13 in one-off 9044 (commit `84d4d4e9e`, both pins) [C]. Nothing to do; it is evidence that UI-Lua reads of the DLL are a real source of windowed-vs-headless divergence worth auditing further (D164: root cause nice to know).
- **For 0437 (A10):** the path-finder node cache's 16-bit generation counter wraps [C], every few turns per finder on H
  [I from 4669's counts]; searches made by UI actions in a windowed game shift the count, a candidate source of
  windowed-vs-headless divergence [I].
- **Already upstream (PF1's danger part):** fork commit `472e1163d` (Release-5.4.7, both pins) keeps the end-turn danger per
  node per search (`CvAStar.cpp`:950, 1313-1316, `CvAStarNode.h`:108-109 @04b1211b8) [C]; the other port's 5.4.6 figures
  include a waste we no longer have. Other upstream changes since 5.4.6 (A* child storage `a4678f41b`, tactical-sim memory
  `933890516`, `dc5e22917`, `4ac6f1185`) remove none of the PF/TA/HL/MI wastes [C, commit messages].
- **Dead code at the pin (TA14):** `CvTacticalPlot::isNicePlotForCitadel()` (`CvTacticalAI.h`:914) has no caller, so
  `CvPlayerAI::GetTopCitadelPlotsCached` (`CvPlayerAI.cpp`:3414) runs only to fill an unread field: 1.6 % of an M turn [C/L].
  A neutral upstream candidate (scrubbed, D95).
- **Where the H/M time sits (helper, from 4668's folded stacks, 2026-11-17) [L, indications]:** `CalculateMilitaryStrengths`
  H 3.3 % / M 2.3 %; distance-to-target floods H 4.6 % / M 1.2-1.7 %; neighbour fills (`UpdateNodeCacheData` under
  `NodeAddedToPath`) H 8.0 % / M 5.1-5.8 %; `AI_promote` M 1.2 %; worker reach H 4.5 % / M 3.3 %; `GetAllPlotsInReachThisTurn`
  H 11.4 % (8.45 danger). The folded stacks inline most `static` helpers (LTO): attribute by caller frame.
- **For 4674 and 4673 (R3, DG6, TR3, L1):** the other port built the same two caches; their quiet-machine gains were
  -1.8 % (reach share) and B/A 0.981 (trade reuse), and a simpler reach memo was slower on a quiet PGO build. Our
  attributions (~1.25 s and ~0.8-0.9 s of H) need the prepared testbed pairs before any default.
- **For PGO (BS1):** their numbers separate the whole-program part (0.954) from PGO (0.806 of the plain build): ~15 %
  from PGO on top of whole-program optimisation; training without path instrumentation; retrain below 90 % match.

## 5. Not verified

- Every value figure is the other port's or scaled from it [I]; their machines, saves, mod set and compiler differ.
- Rows saying "present @pin" were read at 04b1211b8; the other rows assume the waste is still there.
- F2's listener list is a grep for `GameEvents.<name>.Add(`; listeners registered another way, or by a player's extra
  mods, are not counted. The share of the raises with no listener was not counted by name (the raises by name: 4800, `speedup-p0-measurements.md` P0.1; corrected 2026-11-17, was "the 90,164 raises").
- How the EXE implements `GameEvents` registration (LU1 part (b)) is not in the knowledge base.
- The H-save count of hook raises per client turn is not measured.

Sources: the two staging documents (2026-11-17, not committed); `research/03-dll-engine-boundary/proxy-design.md`
(sections 9, 12, 16.7); `server/src/remote/srv_remote_lua.cpp`, `srv_remote_db.cpp`, `srv_remote_main.cpp`,
`proxy/src/proxy_lua.cpp`; on branch `epic-0437-turn-study` at `54e8bd002`: `research/08-performance/turn-time-study.md`,
`turn-time-study-analysis.md` (9.8), `d165-pacing-fix-design.md` (section 10), tickets 4673-4677; the DLL fork at
`04b1211b8` (files and lines in the rows); decisions D21, D30, D104, D106, D121, D164, D169, D178.
