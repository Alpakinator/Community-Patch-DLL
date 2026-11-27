# Speedups P0.3-P0.5: shares by caller, the early-game profile, the autosave and load splits (ticket 4802)

Epic 0504 (exact speedups, wave 1), ticket 4802, impl-4802, 2026-11-17 15:14-. Inputs for the value floor of the
Architect's review section 1 (`research/07-architecture-decisions/architect-2026-11-17-speedup-epic-review.md`): an item
enters wave 2 only if our own profile gives it **at least 0.3 % of an H or M turn**, or it is a pure deletion of dead work,
or it sits in a function another accepted item already rewrites. Item IDs are the review's
(`external-port-speedups-review.md`); the packages are the proposal's (`speedup-epic-proposal.md` section 3). 4800 keeps
P0.1, P0.2 and P0.6 in `speedup-p0-measurements.md`.

Every number here is from the shared workstation (5-15 heavy jobs of other owners registered at each `quietlock.py
check`): **indications, not results (D106)**. Shares are of sampled CPU cycles (perf/sampler, CPU mode: each walked
stack weighted by the cycles its thread used since the last tick); the "+-" is a binomial standard error from the number
of stack walks, the sampling noise only. Seconds are a share times a reference turn: **H 20.9 s** (0438's headless H,
workstation) and **M 1.424 s** (the M series, laptop, grade A), as in the proposal; the floor is then 0.063 s of H or
0.0043 s of M. Marks: `[C]` read in code, `[L]` measured here, `[I]` inferred.

Builds: the pin's release layout `x64-04b1211b-pfdee656` (x64 DLL `29F58E17...`, server `5B254430...`) for every new
run; 4668's H run used 0452's server `FC68B75B...` with the same DLL. The DLL's PDB beside it resolves inlined call
sites (below).

## Method: call sites through inlining `[L, tools C]`

The sampler's folded stacks name physical functions only. With LTO most AI helpers are inlined, so the frame above a
function is often not its source caller: `FindUnitsForHarassing`'s caller frame is `CvTacticalAI::ProcessDominanceZones`
in every run, which hides which `Plot*Moves` called it. `perf/evidence/0504/4802/callsite.py` reads the raw return
addresses (`samples.json`), finds the outermost frame of the target and symbolizes the caller's return address minus 1
with `llvm-symbolizer --inlining` against the DLL's PDB: the result is the inline chain with file and line of the call
(e.g. `CvTacticalAI::PlotGrabGoodyMoves (CvTacticalAI.cpp:1030) <- AssignGlobalMidPrioMoves (:811) <-
ProcessDominanceZones (:778)`). Lines are the pin's source (04b1211b8). Symbolizing a 21,000-walk profile takes about 8
minutes (llvm-symbolizer, one thread); the result is cached in `C:/c5-64bit-work/4802/symcache/`.

## P0.3: shares from 4668's folded stacks (no new run)

Inputs: `F:/c5-64bit-work/4668/runs/{m1,m2,m3,h1}/profile/` (M 151-160 three times: m1 and m2 at a 10 ms tick, m3 at 2
ms; H 301-305 once at 10 ms; `turn-time-study.md` "Where the server's time goes today (W6 phase 1)" on
`epic-0437-turn-study`). Commands (from the worktree root):

    python -I -X utf8 perf/evidence/0504/4802/shares.py F:/c5-64bit-work/4668/runs/{m1,m2,m3,h1}/profile \
        C:/c5-64bit-work/4802/runs/{e2,e3}/profile --src <the pin's source, git archive 04b1211b8>
    bash perf/evidence/0504/4802/run_callsites.sh      (callsite.py for every item and profile, a registered job)

Outputs: `perf/evidence/0504/4802/shares.txt` (inclusive shares, caller frames, OT5 literals by function body) and
`callsites-<run>.txt` (source call sites).

### A5: the barbarian-camp search without embarking `[L]`

`CvTacticalAI::PlotGrabGoodyMoves` calls `FindUnitsForHarassing(pPlot, iRange, -1, -1, DOMAIN_LAND, false, true, 3)` for
every barbarian camp target (`CvTacticalAI.cpp:1030`, range 11, or 6 at war with a major) [C]. Its 7th argument
`bAllowEmbarkation = true` sets `MOVEFLAG_NO_EMBARK` (`:4880-4881`: the name is inverted upstream) [C]; it is the only
`FindUnitsForHarassing` call that passes true [C]. Each candidate unit inside the reach set of the camp then runs
`TurnsToReachTarget` (`:4887`): a full path search per unit.

| Run | `FindUnitsForHarassing`, all callers | the camp search (`:1030`) | of it in `TurnsToReachTarget` | other callers |
|---|---|---|---|---|
| m1 | 7.62 +-0.54 % | **6.70 %** | 5.91 % | pillage `:1268` 0.70, blockade `:1315` 0.22 |
| m2 | 8.11 +-0.66 % | **7.87 %** | 6.59 % | pillage `:1281` 0.09 |
| m3 | 9.50 +-0.40 % | **8.88 %** | 7.48 % | blockade `:1315` 0.33, pillage `:1268`/`:1281` 0.21 |
| h1 | 1.67 +-0.09 % | **1.23 %** | 0.99 % | blockade `:1315` 0.41, pillage 0.03 |
| e2 (early, below) | 4.57 +-0.46 % | **4.57 %** | 3.93 % | none |
| e3 (early, below) | 3.63 +-0.24 % | **3.62 %** | 2.88 % | pillage 0.02 |

So the camp search is about **6.7-8.9 % of an M turn (0.10-0.13 s), 1.2 % of an H turn (0.26 s) and 3.6-4.6 % of the
early game**. The review's
reading (their 10 turns: 6.96 of 7.11 s of candidate search skipped, 0 of 2,231 searches found a path) fits a search that
mostly fails: camps on other landmasses, reachable only by embarking. **Floor: PASS** (M 6.7-8.9 %, H 1.2 %, early 3.6-4.6 %), on the
condition the review sets: only an exact form may enter (a landmass test that also covers unrevealed water and ice, so
that a path the finder would find is never skipped); P3 owns the file.

### DG8: `GetSimulatedDamageFromAttackOnUnit` `[L]`

| Run | inclusive | through `CvUnit::GetDanger` (`CvDangerPlotContents::GetDanger`, `CvDangerPlots.cpp:876` / `:800`) | inside `FindBestUnitAssignments` (DG8's scope) |
|---|---|---|---|
| m1 / m2 / m3 | 0.60 / 0.27 / 0.26 % | 0.55 / 0.27 / 0.22 % | m3 0.02 % |
| h1 | 0.93 +-0.07 % | 0.69 % (+ `GetUnitDangerForPlot` 0.17) | **0.23 %** |
| e3 | 0.90 +-0.12 % | 0.77 % | 0.00 % |

The function's time is mostly the danger evaluation of a unit's plot (`CvUnit::GetDanger` -> `CvDangerPlotContents::GetDanger`),
not the assignment search that DG8's memo lives in. **Floor: FAIL for DG8 as defined** (0.23 % of H, 0.02 % of M inside
`FindBestUnitAssignments`). The function's whole share (0.9 % of H and of the early game, 0.3-0.6 % of M) belongs to the
danger family (P10/P13: DG1, DG2), where a memo of the danger answer, not of this call, would act [I].

### OT5: `getInfoTypeForString` by call site, with the literals `[L; literals C]`

Total: **M 0.49 / 0.91 / 0.75 %, H 0.38 +-0.04 %, early 0.41 / 0.54 %**. No single call site reaches 0.3 %; the 14
hottest by their largest share in any run (`ot5-sites.md`, made by `ot5_literals.py` from `callsites-*.json`; the
string is read from the call's own source line at the pin; "-" = not sampled in that run):

| call site (innermost function, file:line) | string | m1 | m2 | m3 | h1 | e3 |
|---|---|---|---|---|---|---|
| `CvCityCitizens::GetBaseValuePerGPP` CvCityCitizens.cpp:942 | `UNITCLASS_SCIENTIST` | 0.053 | 0.184 | 0.104 | - | - |
| `CvHomelandAI::IsValidExplorerEndTurnPlot` CvHomelandAI.cpp:6844 | `UNITCOMBAT_RECON` | - | 0.092 | 0.023 | 0.026 | 0.117 |
| `CvCityCitizens::GetSpecialistUpgradeThreshold` CvCityCitizens.cpp:3333 | `UNITCLASS_WRITER` | 0.056 | 0.105 | 0.023 | 0.039 | - |
| `CvGame::CanPlayerAttemptDominationVictory` CvGame.cpp:6504 | `VICTORY_DOMINATION` | - | 0.099 | - | - | 0.036 |
| `CvCityCitizens::GetSpecialistUpgradeThreshold` CvCityCitizens.cpp:3348 | `UNITCLASS_GREAT_DIPLOMAT` | 0.055 | 0.096 | 0.048 | 0.006 | 0.015 |
| `CvPlot::HasFeature` CvPlot.cpp:14993 (from `CvCity::CountFeature`) | `FEATURE_RIVER` | - | 0.090 | 0.044 | 0.051 | - |
| `CvCityCitizens::GetSpecialistUpgradeThreshold` CvCityCitizens.cpp:3356 | `UNITCLASS_MERCHANT` | 0.054 | - | 0.085 | 0.019 | - |
| `CvPlayer::HasResourceForNewUnit` CvPlayer.cpp:39973 | `RESOURCE_ALUMINUM` | - | 0.083 | - | - | - |
| `CvDiplomacyAI::GetDiplomaticVictoryProgress` CvDiplomacyAI.cpp:3149 | `VICTORY_DIPLOMATIC` | - | - | - | 0.083 | - |
| `CvCityConnections::UpdateRouteInfo` CvCityConnections.cpp:566 | `ERA_INDUSTRIAL` | - | 0.082 | - | - | 0.014 |
| `CvProcessProductionAI::CheckProcessBuildSanity` CvProcessProductionAI.cpp:205 | `AICITYSTRATEGY_NEED_IMPROVEMENT_FOOD` | - | 0.080 | - | - | - |
| `CvDiplomacyAI::GetScienceVictoryProgress` CvDiplomacyAI.cpp:3190 | `VICTORY_SPACE_RACE` | 0.054 | - | 0.074 | 0.006 | 0.029 |
| `SPrecomputedExpensiveNumbers::update` CvCityCitizens.cpp:3669 | `FLAVOR_ESPIONAGE` | - | - | 0.022 | - | 0.061 |
| `CvCityCitizens::GetBaseValuePerGPP` CvCityCitizens.cpp:958 | `UNITCLASS_ARTIST` | 0.058 | - | - | 0.013 | - |

A single sample at a 10 ms tick is about 0.05 % of an M window, so sites below 0.1 % are one or two samples each: the
list names the hot sites, not their order [I].

**Floor: PASS as one item** (the sum is 0.38-0.91 % of a turn: M about 0.007-0.013 s, H 0.08 s), **FAIL per site**.
So each later package pins the literals of the sites in its own files (the rule of the epic) and only the sum carries the
value; the biggest single owner is `CvCityCitizens.cpp` (P8's neighbour, not in any package's list today) with
`GetSpecialistUpgradeThreshold` and `GetBaseValuePerGPP` (M 0.28-0.39 % together by function, H 0.14 %). The re-resolve after a
database reload stays the item's one risk (review).

### CB16: corporation franchise yields `[L]`

| Run | `CvCity::UpdateYieldFromCorporationFranchises` | all corporation code (`CvPlayerCorporations::*`, `*Corporation*`) |
|---|---|---|
| M m1-m3, early e2/e3 | 0 | 0 (no corporation exists at M 150-160) |
| h1 | 0.007 +-0.006 % (from `CvCity::processBuilding`) | 0.17 +-0.03 % (`CvPlayerCorporations::DoTurn` from `setTurnActive` 0.13, `RecalculateNumFranchises`, `BuildFranchiseInCity`) |

**Corporations exist at H 300** [L: `CvPlayerCorporations::BuildFranchiseInCity` and `RecalculateNumFranchises` are on
h1's stacks, so franchises are being built]. Even so the function CB16 rewrites is 0.007 % of an H turn and all
corporation code 0.17 % (0.036 s). **Floor: FAIL** on H 301-305. Their 340 ms per turn does not appear here [I: a later
game with more franchises could differ; the H window is what we have].

### HL9: `ScorePlotBuild`, the base for a pool `[L]`

| Run | `CvBuilderTaskingAI::ScorePlotBuild` | from `AddImprovingPlotsDirective` (`CvBuilderTaskingAI.cpp:2595`) | of it in `CvPlayer::canBuild` (HL7) |
|---|---|---|---|
| m1 / m2 / m3 | 2.23 / 4.75 / 3.19 % | 1.48 / 3.01 / 2.48 % | 0.89 / 2.28 / 1.36 % |
| h1 | 0.91 +-0.07 % | 0.62 % | 0.05 % |
| e2 / e3 | 4.56 / 1.97 % | | |

`GetPlotTheftLikelyhood` (HL8) is inlined (no frame of its own) [L]. **Floor: the base PASSES** (M 2.2-4.8 %, about
0.03-0.07 s; H 0.9 %, 0.19 s), as the review's bound (2.4 % M) said. The review's order stands: HL7/HL8 first (exact;
HL7's `canBuild` is 0.9-2.3 % of M), then a re-profile decides whether the pool (threads, the review's L size) is worth it.

### MI5 and A7 at the scenario windows `[L]` (the early game: P0.4 below)

- MI5, `CvAIOperation*::PreconditionsAreMet` (all from `CvPlayer::addAIOperation`, `CvPlayer.cpp:42230`): M 0.98 / 0.36 /
  0.11 %, H 0.15 %.
- A7, `CvHomelandAI::GetWeightedDirectives` (the worker directive answers): M 0.00 / 0.29 / 0.33 %, H **1.06 +-0.07 %**;
  the whole worker pass `CvHomelandAI::ExecuteWorkerMoves` M 3.8-5.5 %, H 6.8 %.

## P0.4: the early game, a sampled profile of turns 0-75 `[L, 2026-11-17 15:28-15:33, workstation, shared]`

**Run.** `perf/evidence/0504/4802/profile_p0.py early` (driver `run_p0.sh early NAME`, a job registered with
`quietlock.py run`, owner impl-4802): `server/run_server.py perf/turnstudy/scenarios/M-standard-communitu.spec.json
--turns 78` (Standard Communitu_79a, 8 majors, 16 city states, seeds map 4382 sync 4383, Quick Combat and Quick
Movement on) on 0438's identity database (statistics logging on, `9335186b...`), `--dll-log-flags 0`, the toolkit game
folder, the pin's layout; the sampler attaches at server.log's `-- phase: turn loop` (boot, database and map generation
left out) and stops at the end of turn 75. **This is the M scenario's own game**: its `stats.db` over turns 0-75 is
IDENTICAL to 0438's making run (`F:/c5-64bit-work/0438/runs/M-make`, `perf/evidence/0438/idtrace.py 0 75`: 39,151
rows; `idtrace-e2-vs-make.json`), and e2 = e3 (statscmp, 41,575 rows, turns 0-78). Two runs: **e2** (10 ms tick, 2,046
walks, turns 0-75 in 27.2 s) and **e3** (2 ms tick, 6,099 walks, 30.2 s; the sampler's own cost is larger at 2 ms);
unsampled the same turns took 20.8 s in 0438's making run (0.27 s per turn). (A first run e1 lost its window's end:
the server writes its last turn's line only as it ends; the driver now plays two turns past the window.)

**Headline** (`early-top40.txt`, e3): the early game is **explorers and settlers**, not the tactical AI's search of
the mid game. Path search (`CvAStar::FindPathWithCurrentConfiguration`) 30 % (M 39-42 %, H 53 %), of it
`GetPlotsInReach` 23 %; `CvHomelandAI::PlotExplorerMoves` **14.7 %** (its `GetBestExploreTarget` 8.7 %); the settle-plot
search (`GetBestSettlePlot`, `updatePlotFoundValues`, `CvSiteEvaluatorForSettler::PlotFoundValue`) **7.0 %** (M 1.3-2.7 %);
dominance zones 16 %; danger maps 5.9 %; heap allocation and free 8.8 %; SQLite 5 % (the statistics log is on in the
identity runs; off for players).

**Package families early** (inclusive by physical frame; a function of a package's files inlined into another file's
frame counts there [I]):

| Package | e2 (10 ms) | e3 (2 ms) | M m3 | H h1 |
|---|---|---|---|---|
| P3 tactical AI | 31.2 | 29.2 +-0.6 | 31.8 | 35.1 |
| P4 tactical analysis map | 3.6 | 1.8 +-0.2 | 3.3 | 7.3 |
| P5/P6 path search (all callers) | 28.8 | 30.2 +-0.6 | 42.2 | 53.3 |
| P5 movement cost and validity (`CvUnitMovement`, `CvUnit::canEnter*`) | 12.0 | 6.4 +-0.3 | 14.3 | 8.4 |
| P7 homeland, builder, military, operations | 38.1 | 40.1 +-0.6 | 40.5 | 38.3 |
| P8 city and economy | 20.9 | 18.8 +-0.5 | 23.8 | 17.7 |
| P9 trade | 1.9 | 1.3 +-0.1 | 4.8 | 28.8 |
| P10 danger plots | 11.1 | 6.9 +-0.3 | 4.2 | 12.7 |
| P11 city-states, Congress, team | 4.4 | 6.8 +-0.3 | 6.4 | 9.9 |
| P12 diplomacy, espionage, religion, policy | 5.7 | 7.8 +-0.3 | 15.0 | 22.1 (10.7 of it is religion's city connections through the trade-path cache, 4668) |
| settle-plot search (no package) | 5.5 | 7.0 +-0.3 | 1.3 | 1.9 |
| heap allocation and free | 6.3 | 8.8 +-0.4 | 6.9 | 4.9 |

The early game changes the weights, not the order of the packages: P7 and P3 lead everywhere; P9 (trade) is small until
trade routes exist; the settle-plot search is the one family that matters only early (7 %; it is in no package: their
items for it were not in the review's exact list [I]).

**MI5 and A7 early:**

| Item | e2 | e3 | Verdict |
|---|---|---|---|
| MI5 `PreconditionsAreMet` | 0.11 +-0.07 % | 0.08 +-0.04 % | **FAIL**: below the floor early as at M (0.1-1.0 %, noisy) and H (0.15 %); their -7 % early-game gain does not show in our game [I: another mod set and map] |
| A7 `GetWeightedDirectives` | 0.19 +-0.10 % | 0.05 +-0.03 % | early **below** the floor; H 1.06 % **passes**. A7 is not exact by proof (review): it enters only after P7's exact HL1-HL4 and with its verify define |
| A7's worker pass (`ExecuteWorkerMoves`) | 1.03 % | 1.52 % | context |
| A5 camp search | 4.57 % | 3.62 % | above the floor early too |
| HL9 base `ScorePlotBuild` | 4.56 % | 1.97 % | above the floor early too |

## P0.5: the autosave and the server's load

### The autosave of a client M turn, split `[L, read from existing runs; I where marked]`

No new run: the product path already counts every call per method. The server's `proxy-calls.tsv` (header `method key
direction ...`, where `method` holds the hex key and `key` the name) gives per method the server's own seconds: for a
DLL -> EXE call (`ICvEngineUtility1::AutoSave`) the round trip the DLL's Update waited; for an EXE -> DLL call (the
`*::Write` calls the EXE makes back into the DLL while it saves) the handler time, i.e. the DLL writing into the
server's local stream. The proxy's `vpproxy-calls.tsv` (written at a normal proxy shutdown, beside `vpproxy.log`) gives
the EXE side's round trip of the same calls; the difference is the transfer. `perf/evidence/0504/4802/savesplit.py`
joins them (`savesplit.txt`).

Inputs: 4652's S4 runs of M with the budget switch (`F:/c5-64bit-work/4652/s4/new2-on-1/run` = b2 and `new-on-1` = b1,
2026-11-16 19:20-19:35, workstation, c5h-h, M 150-154, 5 autosaves each; their 0.437 s `save` row is the source of the
0.56 s order of magnitude); the transfer rate from two product-path runs of 2026-11-17 14:55-15:00 on c5h-c and c5h-f
(other teams' games, read only: 22 Write sets of 14.2 MB each, both files present).

| Per autosave, M (S4, workstation) | b2 | b1 | how |
|---|---|---|---|
| the whole `AutoSave` call | **0.455 s** | 0.545 s | server round trip, 5 calls |
| the DLL's serialization (8 Write methods, server handler time) | **0.112 s (25 %)** | 0.154 s (28 %) | per Write set (6 sets: one more than AutoSave calls, the EXE's own save at the load [I]); 0.134 / 0.185 s if all 6 sets belong to the 5 autosaves |
| - of it `ICvMap1::Write` (map and plots, 5.2 MB) | 0.047 s | 0.062 s | |
| - of it `ICvPlayer1::Write` (64 players, 15.0 MB) | 0.060 s | 0.085 s | |
| - of it `ICvTeam1::Write`, Game, PreGame | 0.006 s | 0.008 s | |
| transfer of the 21.6 MB through vpipc | **about 0.01-0.015 s (2-3 %)** [I] | | 1.44-2.31 GB/s measured on the c5h-c / c5h-f runs (proxy round trip minus server handler time over the Write calls) |
| the EXE's own work: header, its own sections, zlib, file write | **about 0.33 s (72 %)** | 0.38 s | AutoSave minus serialization minus transfer |

(2026-11-24, ticket 5032: how to take the EXE's part off the game thread without a code write is designed in
[saves-off-the-game-thread.md](saves-off-the-game-thread.md).)

The EXE's part is mostly compression: 4654's laptop profile of S4 put zlib at 67 % of the EXE's game-core thread (0.62
of 0.93 s per turn) [L, `turn-time-study.md` W4b], and the server's own save of H below is 60 % zlib, 13 % file write
[L]. **MB6's reach** is the plot fields of the map write: in the server's save of H, the stream writes below
`CvDllMap::Write` are 96 % of the map write (20.7 of 21.5 % of the save, run l1 below). So MB6 can remove at most most
of `ICvMap1::Write`: **0.047-0.062 s per client M autosave** = 0.56-0.74 % of an M turn with options 2+3 (8.35 s,
workstation, d165 section 10), 0.22-0.29 % of a stock-paced S4 M turn (21.4 s, laptop functional round), 0 of a headless
turn (headless hosts do not save) [I]. A block write keeps the bytes and removes the per-field call overhead, so the gain
is a part of that bound [I]. **Floor: OPEN** (the bound passes only on the client turn with the D165 options; the
lead decides which client turn is the reference; if it is taken, the package's identity test is byte-identical saves).

### `PrefetchGameData` in the server's load (OT8) `[L, 2026-11-17 15:39, run l1]`

**Run.** `profile_p0.py load --scenario H --save-at-start --interval-ms 2` (driver `run_p0.sh load l1`): the server loads
`TS0438-H-T300` (copied in by SHA-256), writes its own save of it (phase "save at start", for MB6 above), arms autoplay,
plays turn 300 and ends (rc 0). Sampled from server.exe's start to `-- phase: turn loop`: 4,154 walks, 23.1 G cycles.
`perf/evidence/0504/4802/loadsplit.py` splits it by the server's own phase frames (`l1-loadsplit.txt`); wall times from
server.log (sampled, so slowed by the sampler).

| Phase (server.log) | wall s | % of the window's cycles |
|---|---|---|
| boot | 2.00 | 13.9 |
| database (`CacheGameDatabaseData`) | **4.08** | **34.0** |
| lua | 0.04 | - |
| load (the save's body) | 1.29 | 10.6 |
| game started | 1.35 | 11.2 |
| t000 summary | 0.01 | - |
| save at start (H, the server's own writer) | 1.40 | 18.4 |
| arm autoplay | 0.84 | 6.8 |
| the whole load before the turn loop | **11.0** (9.6 without the extra save) | |

**`CvDllDatabaseUtility::PrefetchGameData` is 89 % of the database phase** (the rest `CvGlobals::cacheGlobals`), **30 %
of the window**, about 3.6 s of the H load's 9.6 s; 91 % of the phase is inside SQLite. Its time is spread over the
per-table `CacheResults` of the info classes: buildings 44 %, minor civs 17 %, `CvModEventCityChoiceInfo` 7 %, units 7 %,
policies 6 %, beliefs 5 %, promotions 4 %, traits 4 % (of PrefetchGameData). OT8 (keep the tables' max-rowid answers) acts on a part of this
that the stacks do not separate (the queries are inlined into each `CacheResults`) [I]. **Floor: not applicable** (load
time, no turn): 3.6 s per load is visible to the player at every load, so OT8 needs its own count of the max-rowid
queries before it is judged; it goes to the gate note as a load item, not a turn item.

The server's save of H: 1.40 s; zlib 60 %, the DLL's serialization 27 % (map 21.5 %, players 5.3 %, teams 0.3 %), file
write 13 % [L, cycles].

(2026-11-24, ticket 5036: the same pass timed per helper with timers, on quiet oldpc, is the section "The DLL's
database cache pass per helper" at the end of this file; OT8's max-rowid part is separated there.)

## Verdicts against the value floor

| Item | Share (M / H / early) | Seconds [I] | Verdict |
|---|---|---|---|
| A5 camp search without embarking | 6.7-8.9 % / 1.23 % / 3.6-4.6 % | M 0.10-0.13 s, H 0.26 s | **PASS** (exact form required; P3) |
| DG8 memo in the assignment search | M 0.02 % / H 0.23 % / 0 inside its scope | H 0.05 s | **FAIL** (the function's time is in the danger evaluation: P10/P13) |
| OT5 hot literals | 0.49-0.91 % / 0.38 % / 0.41-0.54 % (sum); every site < 0.3 % | M 0.01 s, H 0.08 s | **PASS as one item, FAIL per site**: each package pins its own sites' literals |
| CB16 corporation franchises | 0 / 0.007 % (all corporation code 0.17 %) / 0 | H 0.002 s | **FAIL** (corporations exist at H 300) |
| HL9 base (`ScorePlotBuild`) | 2.2-4.8 % / 0.91 % / 2.0-4.6 % | M 0.03-0.07 s, H 0.19 s | base **PASS**; the pool is decided by a re-profile after HL7/HL8 (review) |
| MI5 | 0.1-1.0 % / 0.15 % / 0.08-0.11 % | | **FAIL** |
| A7 | 0-0.33 % / 1.06 % / 0.05-0.19 % | H 0.22 s | **PASS on H**; enters only after HL1-HL4 with its verify define (not exact by proof) |
| MB6 plot fields in one block | client M autosave bound 0.047-0.062 s | 0.22-0.74 % of a client M turn; 0 headless | **OPEN** (which client turn is the reference). *Measured 2026-11-24 by 5038 (section "Save serialization in blocks", grade A, quiet oldpc): built with VX1 as one package, it takes 31 ms off every M save and 64-76 ms off every H save (the serialization -55 %); verdict there* |
| OT8 max-rowid answers | `PrefetchGameData` 30 % of the H load (3.6 s); 5036: `MaxRows` 0.54 s of the server's 1.33 s pass (quiet oldpc, 0.1b pin) | load, not turn | **n/a to the floor**; a load item for the gate note (5036's named list, row 1) |

## Not verified (4802)

- Every share is from a loaded, shared workstation (D106); H from one run (h1). The 4668 runs had 5-7 other heavy jobs,
  mine 12-15.
- The early profile is one game (the M scenario's own, seeds 4382/4383): another map or seed would weight explorers and
  settlers differently.
- The autosave split's transfer rate comes from two other games' runs on the same day, not from the M runs (4652's runs
  have no `vpproxy-calls.tsv`); which of the six Write sets of 4652's runs is the EXE's own save was not read.
- MB6's bound assumes the plot fields are most of `ICvMap1::Write` in the client too (measured on the server's own save of
  H, not on a client autosave of M).
- OT8's max-rowid part of `PrefetchGameData`: not separated. (Separated by 5036, next section.)

## The DLL's database cache pass per helper (ticket 5036; LD4, OT8, T6)

impl-5036, 2026-11-24. Measurement only, as the ticket's acceptance asks; no product change. The Architect's row LD4
(`research/07-architecture-decisions/architect-2026-11-21-full-exe-optimizations.md` 3.3, draft T6) names three
candidates: the statement key of `CvDatabaseUtility::SetFractionYields`, grouped reads for the helpers that do not
depend on row order, and the minor-civilization rows' cost; row OT8 of the first review
(`external-port-speedups-review.md`) is the tables' max-rowid answers.

### What the code does at the pin `[C, read at 1b87dd185]`

- `CvDllDatabaseUtility::PrefetchCollection<T>` (`CvDllDatabaseUtility.h`) steps `SELECT * FROM <table> WHERE ID > -1
  ORDER BY ID` and calls `T::CacheResults(kResults, kUtility)` per row; one `CvDatabaseUtility kUtility` per table keeps
  the prepared statements by a string key (`m_storedResults`) and finalizes them all when the table is done.
- The helpers (`CvDatabaseUtility.cpp`) `PopulateArrayByValue` (`SetYields`, `SetFlavors` go through it),
  `PopulateArrayByExistence` (bool and int forms), `PopulateVector`, `PopulateSetByExistence` and `SetFractionYields`
  each run one statement `... WHERE <filter column> = ?` bound to the entry's type: one execution per entry per call
  site. Each helper but `PopulateVector` and `PopulateSetByExistence` first calls `MaxRows(<type table>)` (to size
  its array), which prepares, steps and finalizes `SELECT max(rowid) from <table>` every time (no cache: OT8).
- `SetFractionYields` puts the filter value into its statement key (`"_SFY_" + table + column + value + denominator`),
  so every entry prepares its own copy of the same SQL, and every copy stays prepared until the table is done. Its
  three call sites are all in `CvBuildingEntry::CacheResults` (`Building_YieldChangesPerXBuilding`,
  `Building_YieldChangesPerXTiles`, `Building_YieldChangesFromXCityStateStrategicResource`).
- `CvMinorCivInfo::CacheResults` (`CvMinorCivAI.cpp`) runs per minor civilization five inline statements keyed by
  the minor's trait (four `MinorCivilizationTraits_*YieldBonuses`, `MinorCivilizations_QuestWeights` with three
  joins), three `MaxRows`, `SetFlavors` and the city names.
- `CacheGameDatabaseData` also maps every table's ID and Type (`setInfoTypeFromString` over `sqlite_master`), runs
  `LoadGlobalDefines`, `CvTypes::AcquireTypes`, `SetGlobalActionInfo`, `ClearCountCache`, `DB.Analyze()` (a fork
  addition: statistics for the indexes the mod adds), `GameDataPostCache` and `InitLuaFramework`. The server's
  `database` phase holds `PerformDatabasePostProcessing` (the remapper) and `CacheGameDatabaseData`.

### Method

- **Instrumented build.** Scratch fork branch `scratch-5036-dbprof` = 1b87dd185 + 818d723d6 (local clone only, never
  pushed or merged; the diff is `perf/evidence/5036/scratch-dbprof.diff`). Every timer is behind `VP64_DBPROF`
  (`CvGameCoreDLL_Expansion2/VP64DbProf.h`): a scope stack with inclusive and self time per scope (QueryPerformanceCounter),
  scopes on each part of `CacheGameDatabaseData`, each `PrefetchCollection` table, each helper, `MaxRows` (also by
  table), `PrepareResults`, and the minor-civ blocks; per statement (by SQL text) its prepares and prepare time, its
  uses and use time (helpers: from the cache hit or prepare to the helper's end, exact; inline statements in
  `CacheResults`: from `GetResults` to the next utility event, an upper bound `[I]`). After `SetGlobalActionInfo` and
  before `Analyze` (the pass's own conditions) it runs, outside the pass's scopes, the grouped form of every statement
  with exactly one `WHERE <col> = ?`: `SELECT <col>, <columns> ... WHERE <col> IS NOT NULL [and ...]`, every row
  stepped and every column read. The file goes to `C5_DBPROF_OUT`. Built with `perf/speedkit/kitbuild.py --config
  release` and the pin's marker defines (`VP64_HIDDEN_EMBARK VP64_SPEED_DANGER VP64_SPEED_DANGER_DG7`) plus
  `VP64_DBPROF`.
- **Arms.** `pin` = the 0.1b pin's release DLL (`C:/c5-64bit-work/keep/pins-0.1b/deps-x64-release`, SHA-256
  b84aa3e9...), `instr` = the scratch build; both in one server layout (server.exe built from this branch against the
  keep pin, `--no-proxy`). The pin arm gives the unperturbed numbers (server-report phases, the DLL's own
  `xml-perf.log` stopwatch per table); the instrumented arm gives the split, and the two arms' `PrefetchGameData`
  give the timers' cost.
- **Run.** `perf/evidence/5036/job_5036.py` on oldpc as a quiet testbed job: each run is server.exe loading
  `TS0438-M-T150` with 0438's statistics-on database (`9335186b...`), `turns = 150` (no turn played), `dll_logs=1`,
  AI logging off; rounds rotate the arm order; one warm-up round. Analysis: `perf/evidence/5036/analyze_5036.py`.
  Commands: `bash perf/evidence/5036/submit_5036.sh <layout bin> <instr DLL> 12 15`, then `tb.py --host oldpc wait /
  fetch`, then `python -I -X utf8 perf/evidence/5036/analyze_5036.py <fetched>/out --host-alias oldpc --md ... --json ...`.

### Results: quiet oldpc, 2026-11-24 17:39-17:42 EST `[L]`

Job `20261124-173903-c3e2` (quiet: alone, undisturbed, foreign cores 0.055), 12 rounds per arm after one warm-up, all
26 runs rc 0. Tables: `perf/evidence/5036/oldpc-c3e2-analysis.md` (and `.json`). Seconds per load, mean ± 95 % interval.

| Quantity | pin | instr |
|---|---|---|
| server `database` phase (remapper + `CacheGameDatabaseData`) | **1.325 ± 0.010** (1.306-1.353) | 1.430 ± 0.009 (of it the grouped estimate 0.047) |
| `PrefetchGameData` (the DLL's xml-perf.log) | **1.184 ± 0.009** | 1.232 ± 0.006 |
| of it `Buildings` / `MinorCivilizations` / `Policies` / `Units` / `Beliefs` / `UnitPromotions` | 0.445 / 0.195 / 0.103 / 0.102 / 0.081 / 0.073 | 0.465 / 0.196 / 0.109 / 0.106 / 0.086 / 0.076 |
| the remapper (`PerformDatabasePostProcessing`) | 0.015 | 0.015 |

The timers cost 0.048 s of `PrefetchGameData` (4 %) and 0.058 s of the phase; the instrumented figures below are
that much high at most, most of it on the 83,847 `MaxRows` scopes `[I]`.

**The premise of LD4 is out of date.** LD4 quotes 7.3 to 11.5 s for "ours"; the 11.5 s is the release boot of
2026-11-10 (`research/03-dll-engine-boundary/server-host.md` section 4, a pin before the upstream index and ANALYZE
work). At the 0.1b pin the server's whole pass is 1.33 s on quiet oldpc and 2.3 s on the busy workstation (one
functional load, an indication); 4802's sampled H load (batch 2 pin, workstation) had 4.08 s. The EXE's own 8.71 s
"Cache Game Database Data" of `load-sequence.md` is the stock 32-bit DLL inside the EXE at mod activation, not this
pass.

**Where the pass goes (instr, per load):**

| Part | calls | inclusive s | self s |
|---|---|---|---|
| `MaxRows` (prepare, step, finalize `SELECT max(rowid)`) | 83,847 | **0.532** | 0.532 |
| `PopulateArrayByValue` (of it via `SetYields` 36,396 calls 0.309, `SetFlavors` 1,424 calls 0.057) | 53,898 | 0.511 | 0.169 |
| `minor:QuestWeights` (3 `MaxRows`, the array, the statement) | 120 | **0.175** | 0.173 |
| `Initialize2DArray(tables)` (two `MaxRows` each) | 8,995 | 0.126 | 0.012 |
| `PopulateArrayByExistence(bool)` | 4,582 | 0.057 | 0.025 |
| `LoadGlobalDefines` (`CvGlobals::cacheGlobals`: 1,880 define lookups) | 1 | 0.042 | 0.042 |
| `infoTypeFromString` (every table's ID and Type) | 1 | 0.023 | 0.023 |
| `Analyze` | 1 | 0.022 | 0.022 |
| `SetFractionYields` | 852 | 0.021 | 0.003 |
| `PrepareResults` (all 1,513 prepares of 664 statements) | 1,513 | 0.019 | 0.019 |
| the four `MinorCivilizationTraits_*YieldBonuses` blocks | 4 x 120 | 0.015 | 0.015 |
| `PopulateArrayByExistence(int)`, `PopulateVector`, `PopulateSetByExistence` | 1,136 / 1,152 / 379 | 0.009 / 0.001 / 0.001 | |
| `ValidateGameDatabase`, `UpdatePlayableCivilizationCounts`, `AcquireTypes`, `SetGlobalActionInfo`, `ClearCountCache` | 1 each | < 0.001 each | |

Statements: 664 distinct SQL texts, 1,513 prepares, 91,330 executions per load; 660 have the one-filter shape.

`MaxRows` by table: `Yields` 49,322 calls 0.305 s, `BuildingClasses` 5,206 / 0.034, `Resources` 4,417 / 0.030,
`Features` 2,821 / 0.018, `Domains` 2,444 / 0.015, 35 more tables; 40 tables in all.

**The minor-civilization rows** `[L; schema C]`: `MinorCivilizations` is 0.195 s of the pin's pass (15 %); 0.173 s of
it is `MinorCivilizations_QuestWeights`, which has no index on `MinorCivTraitType` (`perf/evidence/5036/schema_5036.txt`:
6,080 rows; plan `SCAN MinorCivilizations_QuestWeights`), read in full once per minor civilization (120) although
the 120 minors have only **5 distinct traits**. The four yield-bonus blocks (same key) are 0.015 s; city names 0.001.

**`SetFractionYields`' key** `[L]`: 852 executions, 852 prepares of 3 SQL texts (284 per text, one per building),
0.0115 s of prepares beyond one per text, plus the finalize of 849 extra statements at the end of `Buildings`
(inside the table's self time, not separated). Its three tables hold 2 rows each. Small.

**Define lookups and read transactions** `[L]` (not in LD4's list; found here): `CvGlobals::cacheGlobals` looks up
1,880 defines by name with one prepared statement, each lookup its own autocommit read transaction (no other
statement of the connection is active then). On quiet oldpc that is 0.042 s (22 us a lookup); on the busy
workstation it was **1.0 s** in the functional load (530 us a lookup;
`perf/evidence/5036/workstation-smoke-analysis.md`, one run, an indication). `perf/evidence/5036/defines_probe.py`
(Python's SQLite 3.41.2, the same database read-only, workstation; `defines_probe_workstation.txt`): the 1,880
lookups take 0.12-0.48 s in autocommit and 0.005-0.011 s inside one transaction or while another statement of the
connection is stepping. That is also why the helpers are cheap: they run inside `PrefetchCollection`'s open `SELECT *`
statement, one read transaction per table `[I]`. The `Defines` lookup itself uses the primary-key index `[C: query
plan]`.

### The named list: changes and expected seconds (quiet oldpc, per load; the MC decides the build)

Expected savings are the instrumented cost minus the cost of the changed form where it was measured (grouped reads:
the grouped statement run once in the same process), so each is `[I]` built on `[L]` parts. They are disjoint and
add up. Every one is exact by construction (the same values cached), a plain DLL change, upstream-ready, and helps the
32-bit DLL too.

| # | Change | Cost now | Expected saving | Notes |
|---|---|---|---|---|
| 1 | **`MaxRows` answers kept for the pass (OT8)**: one `max(rowid)` per table per pass (a map filled on first use, cleared by `FlushGameDatabaseData`) | 0.537 s, 83,847 calls on 40 tables | **0.50-0.53 s** (38-40 % of the pass) | Exact if no table the pass sizes is written before `Analyze` `[I]: not checked in every CacheResults`; the timers inflate this row most. One function |
| 2 | **`MinorCivilizations_QuestWeights` read once per trait** (keyed by the trait, 5 reads instead of 120), or one grouped read, or an index on `MinorCivTraitType` (a database change) | 0.173 s | **0.166 s** (grouped read measured 0.007 s) | One block in `CvMinorCivInfo::CacheResults`; the four yield-bonus blocks with the same key add 0.014 s |
| 3 | **Grouped reads for the order-free helpers** (`PopulateArrayByValue` with `SetYields`/`SetFlavors`, `PopulateArrayByExistence(bool)`, `PopulateSetByExistence`, `SetFractionYields`): one read per (data table, filter column) per table pass, distributed by the filter value | 0.159 s (prepare + use of 440 statements, 59,711 executions) | **0.137 s** (grouped 0.023 s) | The helper's interface changes or a per-pass row cache sits behind it; `PopulateArrayByExistence(int)` and `PopulateVector` keep row order (0.002 s, not worth it) |
| 4 | Grouped reads for the inline statements of the `CacheResults` functions | at most 0.175 s (208 statements, 28,611 executions; the inline time is an upper bound) | at most **0.154 s** | Spread over 208 statements in many files (largest: `Buildings`' building-class yield changes 0.033 s, `Units`' unique-name count 0.021 s); much churn for little each |
| 5 | **Define lookups in one read transaction** (`cacheGlobals` between `BeginTransaction`/`EndTransaction`, or one read of `Defines` into a map) | 0.042 s (oldpc); 1.0 s on the busy workstation | **0.04 s** on oldpc; up to about 1 s where a read transaction is expensive `[I]` | One function. Running the whole pass inside one deferred transaction (before `Analyze`) would cover any other autocommit statement too `[I]` |
| 6 | **`SetFractionYields`' statement key without the filter value** | about 0.012 s prepares + the finalize of 849 statements | **about 0.012-0.02 s** | One line; also stops 849 statements being kept alive at once |

Rows 1, 2, 3, 5 and 6 together: about **0.86-0.89 s of the 1.33 s pass** on oldpc (the pass to about 0.45 s);
with row 4 about 1.0 s. What stays: the per-table `CacheResults` code and the main tables' rows (the table scopes'
self time, 0.27 s together), `Initialize2DArray`'s allocations, `Analyze` 0.02 s, `infoTypeFromString` 0.02 s.

### Not verified (5036)

- One save's database (0438's statistics-on M database, VP 5.4.8 neutral set); another mod set has other row counts.
  One host for the timing (quiet oldpc); the workstation numbers are one functional load on a busy machine.
- The grouped statements ran once, after the pass, with the database pages warm; a cold first read would cost more.
  Statements with an aggregate (`count(*)`) were grouped without `GROUP BY` (0.0003 s for `Unit_UniqueNames`), so their
  grouped cost is the scan, not the grouped answer; their per-entry side is measured exactly.
- No changed form was built or timed; the savings are cost-now minus the measured cost of the grouped form (rows 2, 3,
  4) or a bound (rows 1, 5, 6). The read-transaction explanation of the define lookups is from the Python probe, not
  from inside the DLL.
- The timers' own cost (0.048 s of `PrefetchGameData`) is not taken off the per-helper rows.

## Save serialization in blocks (ticket 5038; VX1, MB6, the Architect's draft T8)

impl-5038 (account B), 2026-11-24. A DLL change behind a define on the fork, measured on quiet oldpc on EXE build
4390913's inputs (the x64 server path does not involve the EXE: the server writes its own save). Marks: `[C]` read in the
code at the pin 1b87dd185, `[L]` measured here, `[I]` inferred.

### What the writer does at the pin `[C]`

- Every plain value of a save goes through `FDataStream::operator<<(const T&)` -> `Write(const T&)` (out of line, in the
  closed FireWorks library, here `tools/x64-dll/closedlib-stubs/FireWorks/FDataStream.cpp`) -> the stream's virtual
  `WriteIt(sizeof(T), &value)`; on a stream whose endianness is the machine's (every stream: `FDataStream`'s
  constructor sets little-endian and nothing changes it) that is exactly `sizeof(T)` raw bytes. The server's stream is
  `FMemoryStream` (`closedlib-stubs/FireWorks/FMemoryStream.cpp`): a bounds check, `memcpy`, three counters per call.
- The array writers (`CvSpanSerialization.h`, and through it `CvEnumMap` and `tr1::array`; `std::vector` in
  `FStlContainerSerialization.h`; C arrays in `FDataStream.h`) loop one element per call. `FDataStream` declares bulk
  overloads (`Write(int count, const INT32 values[])` and others) that no code uses and the library does not define.
- `CvPlot::Serialize` writes about 50 fields plus ten per team for 64 teams, one call each; `CvMap::Serialize` calls it
  for every plot. Enums go through their own operators, which write `static_cast<int>(value)` with `Write(const int&)`
  (the DLL's 20 in `CvGameCoreEnumSerialization.cpp`, the closed library's 59 in `CvEnumSerialization.cpp`; only
  `GameTypes` writes one byte).

### The change (fork branch `perf/speed-saveblk`, behind `VP64_SPEED_SAVEBLK`) `[C]`

Fork clone `C:/c5-64bit-work/5038/fork` (local; the MC takes it), cut from the x64 pin 1b87dd185 (`deps/dll.lock`,
x64-0.1b). Design in `CvGameCoreDLL_Expansion2/VP64SpeedSaveBlk.h`:

1. **Arrays of plain types in one write.** The white list (`VP64SpeedSaveBlkSink.h`): `bool`, `char`, `unsigned char`,
   `short`, `unsigned short`, `int`, `unsigned int`, `unsigned long`, `__int64`, `unsigned __int64`, `float`: the
   types whose `Write(const T&)` is `WriteIt(sizeof(T))` on a native stream. The span writers and the `std::vector`
   writer (not `std::vector<bool>`) send such an array as its count (vectors) and one `WriteIt`, on any native stream.
2. **A block in front of the stream.** `CvMap::Write`, `CvTeam::Write` and `CvPlayer::Write` write into a
   `VP64SaveBlkStream`, a 256 KB buffer that hands its bytes to the target stream in one `WriteIt` per filled buffer and
   at the end. While it is active (a per-thread sink, set and cleared on the Write call's stack), a plain value written
   to it is appended in place: by the save visitor (`CvStreamSaveVisitor` finds the block when it is built on it) and by
   `FDataStream::operator<<` itself (`FDataStream.h`). The plots go out inside the map's block: the "plot block".
3. **Int-copy enums.** The save visitor writes the 79 enums whose operator writes an int copy as that int.

Why the bytes stay the same: the same bytes in the same order (n raw values = the n writes); any type off the list, and a
stream whose endianness differs, take the original path; blocks do not nest (a second one stays off). `CvGame::Write`
(it raises the `GameSave` hook and embeds the saved-game database) and the supporting classes are not wrapped. The
two-thread hazard: the only state is the per-thread sink, owned by the Write call on the stack; another thread's writes
take the original path. With the define off the DLL is the pin's (`#line` after every block, 4807's `linecheck.py` OK
on 7 files; the byte check below).

`VP64_SPEED_SAVEBLK_STATS` (timers: QueryPerformanceCounter around each Map, Team and Player Write, to the last byte into
the target; a CSV per save, `VP64_SAVEBLK_STATS_OUT`), `VP64_SPEED_SAVEBLK_COUNT` (counters, functional builds only) and
`VP64_SPEED_SAVEBLK_SPOIL=<n>` (the n-th bulk array goes out with its first byte changed: the negative control) are
measurement defines, never shipped.

The design went through three forms in this ticket (fork commits): v1 32b57056f (arrays, the block, the visitor's
inline append and the plot's four `as<E>` enums), v2 2768fc221 (the per-thread sink: `operator<<` appends in place;
79 enums), v3 1548aecc5 (the block keeps its byte count itself, the sink points at it; counters out of the timers).

### Identity `[L]`

Every timed run writes two saves of the loaded game (`s0` at the start, `s1` after arming autoplay for 0 turns) and
hashes both bodies (the DLL's bytes, uncompressed) with the DLL version string cut out (D24's exception (a): the
package's describe differs from the pin's) and both pregames. Over every run of every arm, one value per case and save:
M T150 bodies `c8e77f70...` / `74cc3467...` (21,495,002 B with the pin's version string), H T300 `95dbb435...` /
`b031d8f9...` (46,276,918 B), 52 runs per case in set 1 (job `20261124-192711-458a`), and the same values in the two
functional smokes (`20261124-190040-7fee`, `20261124-191113-24de`).

### Timing set 1: the product builds against the pin, quiet oldpc, 2026-11-24 19:27-19:35 EST `[L]`

Job `20261124-192711-458a` (`perf/evidence/5038/job_5038.py`; quiet: alone, undisturbed, foreign cores 0.056, CPU mean
14.8 %), 12 rounds after one warm-up, arm order rotated, both cases each round, 104 runs rc 0. Arms in one server
layout (server.exe `4501f44d...`, this branch against the keep pin, `--no-proxy`): `pin` = the 0.1b pin's release DLL
`b84aa3e9...`; `on` = v3 release `1fe538ae...` (fork 1548aecc5, `VP64_SPEED_SAVEBLK` + the pin's marker defines);
`on-v1` `0545fbba...`, `on-v2` `403778ea...`. Inputs: `TS0438-M-T150` (`98fc454a...`), `TS0438-H-T300`
(`308a96bb...`), 0438's statistics-on database (`9335186b...`). The server's own phase time of each save: the DLL's
serialization into the server's `FMemoryStream`, the body and pregame files, the header, zlib and the `.Civ5Save` (the
arms differ only in the first). Mean ± 95 % interval, ms; tables `perf/evidence/5038/oldpc-458a/analysis.md`.

| Save | pin | on (v3) | on - pin | v1 | v2 |
|---|---|---|---|---|---|
| M, first save | 271.0 ± 2.4 | 240.1 ± 3.4 | **-30.9 (-34.9..-26.9), -11.4 %** | 246.2 ± 0.5 | 242.2 ± 0.9 |
| M, second save | 271.3 ± 2.9 | 239.5 ± 2.3 | **-31.8 (-35.3..-28.4), -11.7 %** | 247.3 ± 1.4 | 242.3 ± 1.1 |
| H, first save | 766.3 ± 3.9 | 698.7 ± 5.3 | **-67.6 (-73.8..-61.4), -8.8 %** | 708.3 ± 2.5 | 705.3 ± 2.5 |
| H, second save | 773.9 ± 9.7 | 698.1 ± 3.4 | **-75.8 (-85.9..-65.8), -9.8 %** | 714.5 ± 3.5 | 713.1 ± 4.7 |

v3 against v1: -6.1 / -7.8 ms on M, -9.6 / -16.4 ms on H (each interval excludes 0); v2 sits between them.

### D24 part 1 (writer identity) on oldpc `[L, 2026-11-24 19:40-19:43 EST]`

Job `20261124-193919-741b`: `perf/evidence/5038/d24p1_5038.py` runs `tools/testbed/evidence/0155/saveid.py` per x64 arm
(through `perf/evidence/5038/saveid_mods.py`, below): each input is loaded and saved before any turn by the 32-bit pin
`i0ship-f33fc46a` (`deps/i0.lock` release, `259F0D3C...`, c5h) and by the arm's x64 DLL (the server), and the two
resaves are compared by `tools/d10/savecmp.py` (header and pregame raw, body inflated, nothing masked but exception (a)).
**Product v3 (`1fe538ae...`): M T150, H T300 and the testbed's TB-HUGE_0350 IDENTICAL UNDER EXCEPTIONS** (exception (a)
only: `Release-5.4.8-51-gf33fc46a4b Clean` against `Release-5.4.8-84-g1548aecc5a+speed Copy`); the x64 pin as control:
the same three verdicts. Reports `perf/evidence/5038/oldpc-741b/`. Also on 4803's four coverage saves
(`perf/speedkit/README.md` item 5: `SK4803-M-T180` and `-M-T240`, a Congress and the first corporation; `SK4803-H-T305`
and `-H-T310`, a Congress and a special session), job `20261124-194859-4d7c` 19:49-19:52: all four IDENTICAL UNDER
EXCEPTIONS (`perf/evidence/5038/oldpc-4d7c/`). Parts 2 and 3 of D24 (play and cross-load) were not
run: the MC's answer #13299 (2026-11-24) took part 1 on oldpc as enough for the measurement and the go/no-go, and **the
full `tools/d10/run_d10.py` is owed** on the workstation before any product build of this change merges (not on
2026-11-24: beta 4's take from 22:00).

Negative result, tooling `[L]`: `saveid.py` resaves on the x86 side with `--no-mods` (it was written for games played
with the VP modpack as DLC), and c5h refuses every save made with CP v151 + VP v17 as mods ("the save's mods ... are not
the activated mods that affect saved games (none)", job `20261124-193517-95f6`, all three inputs, both arms).
`saveid_mods.py` runs `saveid.py` unchanged except for that command: `--mods-dir %TESTBED_MODS% --no-addins` and
`--mod` per mod of the release descriptor, the form `tools/testbed/jobs/longgames.py` uses for a save with mods.

### Timing set 2: the serialization itself, before and after, quiet oldpc, 2026-11-24 20:47-20:55 EST `[L]` (the acceptance series)

Job `20261124-204635-3ce7` (quiet: alone, undisturbed, foreign cores 0.05, CPU mean 14.6 %), 12 rounds after one warm-up,
104 runs rc 0, the layout of set 1. Arms: `pin` (`b84aa3e9...`), `on` (v3 product `1fe538ae...`), and the two timer
builds of fork 1548aecc5 (release): `st-off` = the pin's code with the timers (`VP64_SPEED_SAVEBLK_STATS` only,
`81923091...`), `st-on` = the package with the same timers (`VP64_SPEED_SAVEBLK` + `_STATS`, `ab0a2e31...`). The timers:
QueryPerformanceCounter at the start of `CvMap::Write`, each `CvTeam::Write` and each `CvPlayer::Write` and after its last
byte reached the server's stream; per save the map, the 64 teams and the 64 players summed (these three are 21.41 of the
21.50 MB body on M, 45.86 of 46.28 MB on H; `CvGame::Write` and the supporting classes are not timed). Their cost: st-off's
save phase is the pin's within noise (M 270.5 against 269.8 ms). Identity as in set 1 (one body and pregame value per
case and save over all 52 runs). Mean ± 95 % interval, ms; `perf/evidence/5038/oldpc-3ce7/analysis.md`.

| Per save (first / second) | before (st-off) | after (st-on) | after - before |
|---|---|---|---|
| **M, the DLL's serialization** | **56.6 ± 0.9 / 57.6 ± 2.2** | **25.3 ± 0.4 / 25.4 ± 0.9** | **-31.3 (-32.3..-30.4), -55 %** / -32.2 |
| - map with its plots (5.21 MB) | 17.9 / 18.0 | 7.9 / 7.6 | -10.0 / -10.4 (-56 %) |
| - 64 teams (1.30 MB) | 2.4 / 2.4 | 0.7 / 0.7 | -1.7 (-70 %) |
| - 64 players (14.90 MB) | 36.4 / 37.2 | 16.7 / 17.0 | -19.7 / -20.2 (-54 %) |
| M, the server's whole save phase | 270.5 / 270.7 | 240.8 / 240.2 | -29.8 / -30.5 (-11 %) |
| **H, the DLL's serialization** | **125.6 ± 1.5 / 127.7 ± 1.1** | **56.4 ± 2.2 / 59.5 ± 0.7** | **-69.2 (-71.8..-66.6), -55 %** / -68.1 |
| - map with its plots (15.34 MB) | 51.2 / 51.0 | 20.8 / 21.5 | -30.4 / -29.5 (-59 %) |
| - 64 teams (1.30 MB) | 6.1 / 6.7 | 0.7 / 0.8 | -5.3 / -6.0 (-88 %) |
| - 64 players (29.22 MB) | 68.4 / 69.9 | 34.8 / 37.3 | -33.5 / -32.7 (-49 %) |
| H, the server's whole save phase | 773.7 / 780.3 | 709.6 / 707.3 | -64.1 / -73.1 (-8 to -9 %) |

The product against the pin in the same set: M -32.1 (-33.3..-30.9) / -30.7 ms, H -64.8 (-71.0..-58.7) / -66.8 ms of the
save phase, the same as the timer builds' difference: the serialization is all that moved.

**Why it moved** `[L counts, I per-call cost]`: the counting build (below) shows what the old path did per M save: about
7.8 million `Write(const T&)` calls, each an out-of-line call and a virtual `WriteIt` into `FMemoryStream` (on H about
18 million). After: the values go into the block in place (5.9 M on M, 13.9 M on H), arrays go out whole (53,323 arrays
of 1.76 M elements on M, 117,718 of 3.81 M on H), and only about 116,000 (M) / 273,000 (H) values of types off the list
(strings through the closed library's operator, bit fields, GUIDs, `GameTypes`) still call the block's `WriteIt`; the
block reaches the server's stream in 166 (M) / 267 (H) writes of up to 256 KB. Removing about 7.7 M calls saved 31 ms
on M and 17.7 M calls saved 69 ms on H: about 4 ns per call on oldpc `[I]`, consistent with a call, a virtual call, a
bounds check and three counter updates.

### Negative control `[L, 2026-11-24 20:55-20:57 EST]`

`f-spoil` (fast, `VP64_SPEED_SAVEBLK` + `VP64_SPEED_SAVEBLK_SPOIL=1000`: the thousandth array of the process goes out
with its first byte XORed with 0x5A), D24 part 1 against the x86 pin, job `20261124-205458-1362`: **M, H and the huge
save all DIFFERENT** (M: one byte at body offset 562,732, `ff` / `a5`, besides exception (a)). The identity checks see a
single wrong byte. `perf/evidence/5038/oldpc-1362/`.

### Counts per save (the counting build) `[L, 2026-11-24 20:58 EST]`

`f3-count` (fast, `VP64_SPEED_SAVEBLK` + `_STATS` + `_COUNT`, fork 1548aecc5, `f6fcfcbc...`), job `20261124-205750-96f6`,
one run per case beside the pin (bodies identical to the pin's again). Map + teams + players of one save:

| | M T150 | H T300 |
|---|---|---|
| bytes | 21,414,143 | 45,860,226 |
| values appended in place (visitor and `operator<<`) | 5,949,147 | 13,934,695 |
| arrays in one write / their elements | 53,323 / 1,758,040 | 117,718 / 3,807,886 |
| values still through the block's `WriteIt` (types off the list) | 115,857 | 272,708 |
| writes into the server's stream (block spills) | 166 | 267 |
| old path's `WriteIt` calls (the sum of the three above rows) `[I]` | about 7.82 M | about 18.02 M |

The same counts as the v2 fast build of the second smoke (same paths). v1 (no `operator<<` path, four enums) left 2.87 M
of M's values on the block's `WriteIt` (smoke 1): the v2 widening is worth about 6 ms per M save in set 1 (v1 -> v3
-6.1 / -7.8 ms).

### Define off `[L, 2026-11-24 20:51-20:59 EST]`

`perf/evidence/5038/defoff.sh` (4808's form, runbook 1.8: a plain copy of fork 1548aecc5 built by `build_as_pin.py
--describe pin`, release, the pin's three marker defines, the pin build's `SOURCE_DATE_EPOCH` 1795214922, the keep
folder's stubs), then `dllcmp.py --allow-build-id` against the pin DLL `b84aa3e9...`: **IDENTICAL-EXCEPT-BUILD-ID** (DLL
`466e3c96...`, 23,725,056 B; only the PE and debug-directory time stamps and the PDB GUID differ). `perf/evidence/5038/defoff/`.

### Verdict against the value floor, and what stays open

- The gain is per save, not per turn: a headless host does not save (0). In the client every turn autosaves by default,
  and the EXE asks the server for the same Map, Team and Player writes (the server serializes into its own
  `FMemoryStream`, section "The autosave of a client M turn, split"), so the same 31 ms (M) and 64-76 ms (H) per
  autosave apply there `[I: the client path was not timed]`. Against the references of this file: **M 0.37 % of the client
  M turn with options 2+3** (8.35 s, workstation) and 0.15 % of a stock-paced S4 M turn (21.4 s); **H 0.40-0.54 % of a
  headless H turn on oldpc** (14.0-16.2 s per turn, 300-305, speedup-p1-pgo.md), less of a client H turn. **Above the
  0.3 % floor on H and on the D165-options M turn**; the 4802 bound (0.047-0.062 s on the workstation) holds: oldpc's
  pin serialization is 0.057 s per M save.
- The EXE's part of an autosave (header, zlib, file write; 72 % on M) is unchanged: that is T2 (`saves-off-the-game-thread.md`).
- Not in the package: `CvGame::Write` (it raises the `GameSave` hook and embeds the saved-game database) and the
  supporting classes, the pregame, and the loader (reads are unchanged; a load path was not touched).
- Statics and globals added (the two-thread rule): one `thread_local VP64SaveBlkSink` (and in the measurement builds a
  `thread_local` spoil counter and the timer rows' `std::vector` in `CvMap.cpp`). The sink is live only inside a
  Map/Team/Player Write on the thread that runs it and is cleared when it returns; no hook is raised inside those
  writes `[C: no GAMEEVENTINVOKE in CvMap/CvTeam/CvPlayer::Serialize's save branch]` `[I: not checked in every nested
  Serialize]`; a ui-thread write sees its own, empty sink and takes the original path.
- The package is x64-only as written (C++14: `thread_local`, `static_assert`, `<type_traits>`); an upstream form for the
  32-bit C++03 build needs `__declspec(thread)` and C++03 traits.
- The full `tools/d10/run_d10.py` (all three parts, D239's save compatibility): **run 2026-11-24 by ticket 0730, PASS**
  (next section). *Corrected 2026-11-25 by 0730; it said "owed before a product build merges (MC #13299)".*
- Not verified: the client autosave path (timed only through the server's own save), other saves than the seven used
  (M T150, H T300, TB-HUGE_0350 and 4803's four coverage saves), the 43-civ build, the laptop, and the workstation.

### The full D10 save test, on against off (ticket 0730) `[L, 2026-11-24 23:26 - 2026-11-25 00:19 EST, workstation]`

impl-0730 (account A). `tools/d10/run_d10.py` (all three parts of D24: writer identity, behaviour identity,
cross-load with round trips; D239), `--order-exception none --d18-x86 yes --d18-x64 yes --jobs 4` (the arguments of
`perf/speedkit/idjob.py`'s D24 window), run twice, one arm after the other through run_d10's own quiet-lock launch gate
(functional, no timing; MC #13476: on the workstation after beta 4's take 0725 had passed its HI journeys). Script
`perf/evidence/5038/run_d10_0730.sh`. Both arms use one server layout, 5038's `server-build/bin` (server.exe
`4501f44d...`, `CvGameDatabase_x64.dll` `9c37b39e...`, `CvLocalization_x64.dll` `35863ede...`, `lua51_x64.dll`
`30724d6d...`, `civ5save.exe` `409422ef...`), with only the DLL put in:

- **on**: 5038's v3 product DLL `1fe538ae...` (fork 1548aecc5, `VP64_SPEED_SAVEBLK` + the pin's marker defines; version
  string `Release-5.4.8-84-g1548aecc5a+speed Copy`);
- **off**: the 0.1b pin's release DLL `b84aa3e9...` (5038's define-off build of 1548aecc5 is identical to it except the
  build id, section "Define off");
- x86 arm of both: `deps/i0.lock`'s release, `i0ship-f33fc46a` (`259F0D3C...`, `Release-5.4.8-51-gf33fc46a4b Clean`),
  run_d10's default; the games are run_d10's own (Tiny 0-100 save at 25, Standard 0-200 save at 50, the huge reference
  save 350-400 save at 375).

| Arm | Verdict | Wall | Part 1 (writer) | Part 2 (behaviour rows, x86 = x64) | Part 3 (cross-load gates and round trips) |
|---|---|---|---|---|---|
| on | **PASS** (exit 0) | 23:26:28-23:52:40, 1,572 s | 11 of 11 inputs identical under exception (a) only | tiny 30,569 / standard 359,261 / huge 685,488, identical | every gate identical, every round trip identical, no exception |
| off | **PASS** (exit 0) | 23:52:41-00:19:09, 1,589 s | 11 of 11 identical under exception (a) only | tiny 30,569 / standard 359,261 / huge 685,488, identical | every gate identical, every round trip identical, no exception |

The on arm's part 2 row counts are those of the 0.1b pin's own save test (`deps/dll.lock` `verified`, take 0551:
30,569 / 359,261 / 685,488): the same games, row for row as many. Reports: `perf/evidence/5038/d10-0730/d10-on-report.txt`,
`d10-off-report.txt` (home folder written as `<home>`, `perf/evidence/5038/scrub_0730.py`).

**On against off, file by file** (`perf/evidence/5038/onoff_0730.sh`: every `.Civ5Save` of the on run's folder
against the same path in the off run's, `tools/d10/savecmp.py`): only the huge reference save is an input of both runs
(`inputs/D10-HUGE-input-T350`, IDENTICAL), so only its resaves compare across the arms. **The on DLL's resave of it
against the off DLL's: header identical, body (46 MB inflated) identical under exception (a) only.** The pregame differs
from its stored load path on (run folder `d10-on` against `d10-off`: one character longer, the rest shifted by one
byte); the x86 resave of the same file in the two runs (the same 32-bit DLL) differs in exactly the same pregame bytes,
so that is the path, not the DLL. Every other file differs, the x86-written ones included: each run plays its own new
games, and two runs of the one x86 DLL already differ (the x86 direct T25 save: 617 runs, 2,272 B of the 13.2 MB body,
from body offset 11,147: the games' GUID strings and other per-game bytes). That A/A difference makes those files no
on/off test. Detail: `perf/evidence/5038/d10-0730/onoff-all.txt`, `onoff-detail.txt`. For new games the on/off identity
is in the statistics instead: **the on arm's x64 direct plays against the off arm's (`tools/d10/statscmp.py`, every
table): tiny 30,569, standard 359,261, huge 685,488 rows, IDENTICAL**, and the x86 direct plays of the two runs likewise
(the control) `[L]` (`onoff-stats.txt`). Add 5038's identity sets on oldpc (one body value per case over 52 runs each).

**Result: VP64_SPEED_SAVEBLK passes the full D10 save test.** Its saves load and continue identically in the 32-bit
DLL and in itself, and the games it plays and the saves it writes are those of the pin. Part 1 was already shown on
oldpc by 5038 (seven saves); 0730 adds parts 2 and 3 and the round trips. Not verified here: the 43-civ build, the
client's autosave path (the server writes these saves), the laptop.

### The pin step (proposal to the MC; not done by 0730)

The run passed, so the four fork commits may go into the x64 pin. They are on fork branch `perf/speed-saveblk` in 5038's
local clone `C:/c5-64bit-work/5038/fork` only (not on the fork's remote; `perf/evidence/5038/fork-patches/` holds them
as patches): `32b57056f`, `872ec3c6e`, `2768fc221`, `1548aecc5`, on the pin base `1b87dd185` (`x64-0.1b`). What a pin
needs besides the commits `[C: tools/x64-dll/build.py, release-builds.md 5.3 and 5.5]`:

1. **A marker-define row.** `tools/x64-dll/build.py` `MARKER_DEFINES` turns a feature on only for a source that holds its
   marker header; there is no row for `VP64SpeedSaveBlk.h`, so a pin build of these commits gets the define **off** (the
   pin's code, by 5038's byte check). Row needed: `('VP64SpeedSaveBlk.h', ('VP64_SPEED_SAVEBLK',))`. Adding it changes
   the compile line of every source that holds the header, so a pin without the header keeps its hash.
2. **The package's `#line` lines.** 5038 put a `#line` after every block (23 `+#line` lines in `git diff
   1b87dd185 1548aecc5`) for its define-off byte identity. The release build's narrowing gate keys a site by its
   physical line, and `1b87dd185` exists to remove such lines (release-builds.md 5.3: the danger package's 66 lines made
   26 known sites look new). With the define on, nothing depends on them: one commit on top removing them, as
   `1b87dd185` and `a70ebe15c` did, then the gate's 794 / 0 new must hold again `[I: not built with the gate]`.
3. **Carry series rows.** The 0.1b pins are carry-series rows (`tools/release/series/carry.tsv`, release-builds.md 5.5):
   the five commits (four + the `#line` strip) as `x64` rows of class `port` after the danger package's rows. That
   needs the branch pushed to the fork (a new `perf/speed-saveblk` there; never master, i0-ship, x64 or the upstream
   branches) and a take of the validated tier on the new pin (its save test then reruns run_d10 on the built layout).
4. **The 32-bit side.** The package is x64-only (C++14 `thread_local`); the i0 pin is unchanged, so this is an x64
   row only and the x86 = x64 identity above is the gate that covers it.
