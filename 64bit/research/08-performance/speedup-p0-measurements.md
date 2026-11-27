# Speedup epic P0: hook raises, path-finder stamps, event coverage (ticket 4800, epic 0504)

impl-4800, 2026-11-17, workstation, account A. The counting measurements P0.1, P0.2 and P0.6 of
`speedup-epic-proposal.md` (section P0), asked for by the Architect's review
(`../07-architecture-decisions/architect-2026-11-17-speedup-epic-review.md` sections 4 and 5). Counts only: no timing
claim (D106); every millisecond figure below is an indication from a loaded workstation. Marks: `[C]` read in our source
or the DLL's, or in a file of recorded data; `[L]` seen in a run; `[I]` inferred.

**Headlines** (details and marks in the sections):

- **Raises per turn (headless, identical games):** M 151-160: **16,467**; M 151-200: 20,036; H 301-305: **160,126**;
  early game 1-50: 2,747. No raise has a listener in the server's own Lua state (handled 0). In the client, by the names
  CP+VP+EUI register listeners for, 0.1-1.4 % of the raises would be handled [I].
- **The client's "90,164 raises per M turn" was crossings, not raises:** the client measured about 90,000 `script`
  crossings per M turn; the counted raises at n + 3 crossings each give 91,608 (ratio 1.016 to the measured 90,150).
- **P2's value [I]:** about 0.78 s per client M turn and about 8.9 s per client H turn; P15 (the last crossing per raise)
  at most 0.17 s on M and 1.7 s on H. **Corrected 2026-11-17 (lead-0504): measured P2 about 0.025 s per client M turn
  [L, 4804]; a P15 stand-in -0.34 s [L, 4811]; the time is the EXE's Lua lock wait, not the crossings (see "What P0.1
  decides" and `speedup-p2-gameevents.md` sections 9.3 and 11).** 52 % of H's raises are `CanLoadAt` (testany, 4 args).
- **A10:** the 16-bit generation starts at 0 at each DLL load, so wraps depend on the session's length since the load,
  not on the game turn: the unit path finder wrapped after 19 turns from the M load (turn 169) and every 17 turns after,
  and every 3 turns on H (first at 303). **Aliased (stale) reads counted: 0** at the one compare site and at every read
  site, over 2 wraps on M (150-200) and 3 on H (300-309). Not counted as A10 but found: `CommonNeighborIsPassable`
  reads cache data the current search never refreshed (about 120 per M turn, an upper bound), and on H `PathValid`,
  `CanEndTurnAtNode` and `DestinationReached` read cleared nodes (stamp 0) a few thousand times per turn.
- **Coverage:** no World Congress session in M 151-160, H 301-305 or the early game 1-50 (M has one at 181-182, H at 306-307 and a special session at 311); no
  corporation in M (to 200) or the early game; H has 7 founded and 90-108 franchises; era changes and city-state level
  changes occur in every window.

## The counting build

- Fork branch **x64-count-0504** (`C:/c5-64bit-work/4800/fork`, a clone of the local fork clone), from x64-count-0437
  c32469ccb (= the x64 pin 04b1211b8 + 4669's shared counting code `VP64Count.h/.inl` and its three counters). Three
  more defines, each with its own `#if` at every hook: `VP64_COUNT_HOOKS`, `VP64_COUNT_ASTAR`, `VP64_COUNT_EVENTS`.
  4669's three defines are off in this build; its input-event hooks (`VP64_COUNT_ANY`) and its per-turn file
  `VP64CountTurns.csv` come with any of the six.
- The counters read and count only; the CSV files go into `%VP64_COUNT_DIR%`.

## P0.1 GameEvents raises

### Which raises reach the EXE in the client, and their crossings `[C, read 2026-11-17 at the pin 04b1211b8 and repository main dd292b14b]`

Every GameEvents raise of the DLL goes through one of five functions in `Lua/CvLuaSupport.cpp`:
`LuaSupport::CallHook`, `CallTestAll`, `CallTestAny`, `CallAccumulator(int&)`, `CallAccumulator(float&)`. The
`GAMEEVENTINVOKE_HOOK/TESTANY/TESTALL/VALUE` macros (`CustomMods.h`) and the direct `LuaSupport::Call*` call sites (20
DLL files) all end there; no DLL file calls `ICvEngineScriptSystem1::CallHook/CallTest*/CallAccumulator` another way (a
search of `->Call(Hook|TestAll|TestAny|Accumulator)(` outside `CvLuaSupport.cpp` finds none; `CvMapGenerator.cpp` and
`CvWorldBuilderMapLoader.cpp` call `CallFunction`/`CallCFunction` for map scripts only). The script-args objects are made
only by `CvLuaArgsHandle` (`Lua/CvLuaArgsHandle.cpp`: `CreateArgs()` or `CreateArgs(uiReserve)` in the constructors,
`DestroyArgs` in the destructor); no other DLL file calls `CreateArgs`/`DestroyArgs`.

What one raise costs in the client today (`server/src/remote/srv_remote_lua.cpp`, `srv_remote_objects.cpp`):

| Step | Where | Crossings |
|---|---|---|
| `gDLL->GetScriptSystem()` (constructor and destructor) | `srv_remote_objects.cpp` 93-115: answered locally after the first call (issue 2700) | 0 |
| `CreateArgs` / `CreateArgs(n)` | generated `RemoteGen_ICvEngineScriptSystem1` (not overridden by `RemoteScriptSystem`) | 1 |
| each `Push` (bool, int, string; `PushNULL`) | generated `RemoteGen_ICvEngineScriptSystemArgs1` (`RemoteArgs` overrides only `GetString`) | 1 each |
| `HasGameCoreLock` / `ReleaseGameCoreLock` / `GetGameCoreLock` around the call | `srv_remote_objects.cpp` line 5: server-local in remote mode | 0 |
| `CallHook` / `CallTestAll` / `CallTestAny` / `CallAccumulator` | generated, one round trip (the listeners run in the EXE inside it; their calls back into the DLL are `from_exe.lua` crossings, not counted here) | 1 |
| `DestroyArgs` | generated, special-cased (`EngineForget`, `gen_runtime.py` 913) | 1 |
| args getters after the call | none: no DLL file reads a script-args object after a raise (the `GetInt`/`GetBool` reads in `CvBeliefClasses.cpp` are database results) | 0 |

So a raise with n arguments makes **n + 3 crossings** (create, n pushes, the call, destroy), all of class `script` in the
0437 budget files (`proxy/include/vp_budget.h`: `ICvEngineScriptSystem*` interfaces and the `kRtE_Lua*` runtime
methods). A raise refused by `MOD_API_DISABLE_LUA_HOOKS` makes n + 2 (its args are still made, pushed and destroyed);
an args object made and destroyed without a raise makes n + 2. The `script` class also holds the DLL's own Lua chunks in
the engine VM (`kRtE_LuaGetGlobal`, `kRtE_LuaCall`, `kRtE_LuaRelease`, `kRtE_LuaRunChunk`), which are not raises.

### The client figure "90,164 raises per M turn" `[C, from the recorded files, 2026-11-17]`

The figure quoted in `external-port-speedups-review.md` (F1, LU1), `speedup-epic-proposal.md` (P2), the Architect's
review (section 4) and this ticket comes from 0437's budget run `perf/evidence/0437/4665/wsb-S4s-skip1_tick0/` (branch
`epic-0437-turn-study`, `turn-time-study.md` near line 2609). In that run's files:

- `budget-proxy.tsv`, turn 153: `turn wall 9.016403500 0 90164035` (the wall time in 100 ns ticks) and `in script
  0.712886900 83613 7128869` (83,613 DLL -> EXE crossings of class `script`, 0.71 s of EXE handler time).
- `budget.tsv` (the server's side), turn 153: `into_exe.script 0.811996900 167416` (twice the proxy's count: 167,416 /
  83,613 = 2.002).
- No count in either file equals 90,164. The proxy's `script` crossings per M turn 151-160 are 75,822 to 99,823, mean
  **90,150**; turn 153's wall time is 90,164,035 ticks.

So **the figure counts crossings of class `script`, not raises** (either the crossing mean of a slightly different turn
set, or turn 153's wall ticks read as a count; both put it at about 90,000 crossings per M turn). The counted raises below
reproduce the measured crossings within 1.6 %. Corrected in place on 2026-11-17 with a dated note:
`external-port-speedups-review.md` (F1, F2, LU1, "Not verified") and `speedup-epic-proposal.md` (P2). Not edited: the
Architect's review (section 4) and the tickets, which quote it; this file is current.

### Counts `[L, 2026-11-17 15:57-16:09 EST, workstation (shared; quietlock.py check before each run in C:/c5-64bit-work/4800/load-*.txt), counting DLL d06fb61d..., headless server.exe direct mode, statistics on, dll_log_flags 0; each run IDENTICAL to its reference: section "Runs and identity"]`

Per turn, full turns of the window (the turn of the load is partial and left out). Handled: a listener in the server's
own Lua state (`srv_lua.cpp` `LuaDispatch`), not the EXE's: there is none, so 0 everywhere. Client crossings (model) =
CreateArgs + pushes + calls + DestroyArgs from the counters; every args object made was passed to a raise (unused 0) and
no raise was refused by `MOD_API_DISABLE_LUA_HOOKS` (disabled 0) in any run.

| Window | raises / turn | range | args / raise | crossings (model) / turn | per raise | testall | testany | hook | accum | inside `CvGame::doTurn` | names (name/kind pairs) |
|---|---|---|---|---|---|---|---|---|---|---|---|
| M 151-160 (c-m1) | **16,467** | 14,178-17,644 | 2.56 | 91,608 | 5.56 | 15,326 | 45 | 1,032 | 64 | 18.7 % | 73 |
| M 151-200 (c-m2) | 20,036 | | 2.58 | 111,802 | 5.58 | 18,629 | 40 | 1,305 | 61 | 16.3 % | 79 |
| H 301-305 (c-h1) | **160,126** | 105,143-199,265 | 3.32 | 1,012,331 | 6.32 | 72,862 | 83,476 | 3,725 | 63 | 3.5 % | 80 |
| early 1-50 (c-e1) | 2,747 | | 2.49 | 15,073 | 5.49 | 2,369 | 3 | 312 | 63 | 8.9 % | 57 |

Top names, mean raises per turn (all `testall` unless marked; full lists in `perf/evidence/0504/4800/reduced/`):

- M 151-160: PlayerCanConstruct 5,686; CanHavePromotion 1,642; PlayerCanTrain 1,450; CityCanTrain 1,091;
  CityCanConstruct 916; UnitSetXY (hook) 672; CanStartMission 656; CanDeclareWar 651; CanHaveUpgrade 645;
  CityEventChoiceCanTake 575; PlayerCanDisbandUnit 456; CityStrategyCanActivate 334; CityCanAcquirePlot 304.
- H 301-305: **CanLoadAt (testany) 83,387** (52 %: raised in `CvUnit::canLoad` for a special unit with city load, air
  units, tested on a non-city plot); PlayerCanConstruct 25,069; CityEventChoiceCanTake 14,009; CanHavePromotion 7,730;
  PlayerCanTrain 5,631; UnitSetXY (hook) 3,049; CityCanTrain 3,038; CanHaveUpgrade 2,483; PlayerCanEverResearch 2,152;
  PlayerCanResearch 2,150; CanStartMission 2,108.
- Early 1-50: PlayerCanConstruct 516; CanDeclareWar 414; CanHavePromotion 363; CanStartMission 198; PlayerCanTrain 168.

The client comparison: 4665's windowed run started from the same save (TS0438-M-T150 by hash, the RC's layout, statistics
on) but is not the same game after the load (windowed and headless games differ, D164); its measured `script` crossings
per turn 151-160 mean 90,150 against the model's 91,608 from the headless counts (ratio 1.016) [L for both counts; the
equality of the two games' raise mix is assumed, I].

Listeners in the client: of the names raised, those for which CP+VP+EUI register a `GameEvents` listener
(`external-port-speedups-review.md` F2: TeamTechResearched, PlayerDoTurn, PlayerCityFounded, CityConstructed,
SetPopulation, CityBoughtPlot, UnitPromoted; GetBombardRange is not raised) make **0.51 % of M's raises (83 per turn), 0.13 %
of H's (210 per turn) and 1.4 % of the early game's (38 per turn)** [I: by name from F2's grep; the live EXE count per name,
the ticket's optional item, was not taken].

### What P0.1 decides `[I]`

**Correction (impl-4804, 2026-11-17 18:50):** the per-crossing cost is not uniform. On the client route a push costs
about 0.8 us, `CreateArgs` 2 us, `DestroyArgs` 1.2 us and the `Call*` itself 25-48 us (server clock, M 151-160, two
windowed runs; indication, workstation). P2's measured saving was about 0.025 s per client M turn, not 0.78 s; the time
is in the `Call*` (P15's part). Current figures: `speedup-p2-gameevents.md` section 9.3. The two P2 bullets below are
the original estimate, kept as a trace.

- Cost per crossing: 0.94 s per client M turn over about 90,150 crossings = **10.4 us** (the server's own `into_exe.script`
  row: 9.37 s over 901,496 crossings in 151-160, also 10.4 us) [C from 4665's files; one run, workstation].
- **P2** (one crossing per raise) removes n + 2 crossings per raise: 4.56 x 16,467 = 75,100 per M turn = **0.78 s per
  client M turn**; 5.32 x 160,126 = 852,000 per H turn = **8.9 s per client H turn**. The mean cost per crossing is
  assumed for every kind of crossing (a push may cost less than a call); no client H turn was measured.
- **P15** (no crossing for a name without a listener) can then remove at most the one crossing left: 16,467 x 10.4 us =
  0.17 s per M turn, 160,126 x 10.4 us = 1.7 s per H turn; nearly all raises qualify (above). On H its value is no longer
  small; most of it is one name (`CanLoadAt`), so a narrower form (that one name) would hold most of it.

## P0.2 Path-finder generation wraps and stale reads

### The stamp and every place it is compared or read `[C, CvAStar.cpp at the pin 04b1211b8]`

- `CvAStar::m_iCurrentGenerationID` (unsigned short, starts 0) is incremented once per search in
  `FindPathWithCurrentConfiguration` and set to 1 when it reaches 0xFFFF: the IDs run 1..0xFFFE, a wrap every 65,534
  searches of one finder. It is not saved and starts at 0 at every DLL load.
- Each node's `m_kCostCacheData.iGenerationID` (`CvAStarNode.h`) is written in one place, `UpdateNodeCacheData`
  (`//done!` line), and copied whole to a stop node in `CvTwoLayerPathFinder::AddStopNodeIfRequired`
  (`pStopNode->m_kCostCacheData = current->m_kCostCacheData`).
- `CvAStar::Reset()` (each search) clears the nodes of the previous search's open and closed lists
  (`CvAStarNode::clear()`: stamp 0). Nodes whose cache was filled as a neighbour (`UpdateNodeCacheData` on the six
  neighbours and the extra children of each newly added node) but that never entered a list keep their stamp: these are
  the nodes a later search can find with an equal stamp.
- **The one compare that treats an equal stamp as fresh**: `UpdateNodeCacheData`, `if
  (kToNodeCacheData.iGenerationID==finder->GetCurrentGenerationID()) return;`. Counted there as fresh, aliased (equal
  16-bit stamp, written by an earlier generation: the defect A10) or unequal.
- **The reads of a node's cache data**, counted by the class of the node at the read (fresh; aliased; written in an
  earlier generation of the same wrap cycle; of an earlier wrap cycle; cleared; no shadow): `PathCost` (node, parent),
  `PathValid` (node, parent), `CvTwoLayerPathFinder::CanEndTurnAtNode`, `CommonNeighborIsPassable` (each neighbour of a at
  distance 1 from b: the loop stops at the first passable one, so this is an upper bound of its reads),
  `AddStopNodeIfRequired` (next), `UnitPathGetExtraChildren` (node), `CvPathFinder::DestinationReached` (the node at the
  tested plot, read only with an approximate-target flag and `MOVEFLAG_APPROX_TARGET_NATIVE_DOMAIN`). Not counted
  separately: the reads inside `NeedToCheckStacking` and `PathEndTurnCost` (they read the node their callers' sites
  count), and `CvAStar::CreateChildren`'s airlift/port flags of `m_pBest` (a node taken from the open list, refreshed when it
  was added).
- The counter keeps a 32-bit shadow generation per node slot (the main layer and the two-layer finder's partial-move
  layer), written at the stamp write and copied at the stop-node copy. The step finder (`stepfinder`, trade and simple
  step paths) increments its generation too but never calls `UpdateNodeCacheData`: its wraps touch no cache read.

### Counts `[L, 2026-11-17 15:57-16:22 EST, the runs of "Runs and identity", counting DLL d06fb61d...]`

The generation counter starts at 0 when the DLL loads (here: the server's load of the save, or the new game), so **when
the first wrap comes depends on how many searches the session has made since the load, not on the game turn**: a player
who loads a save and plays for hours meets wraps every few turns late in the game; the identity windows meet them only if
they run long enough after the load.

| Run | unit path finder searches / turn | its wraps (turn) | step finder searches / turn | its wraps (turn) | compare: fresh / aliased / unequal | aliased reads at any read site |
|---|---|---|---|---|---|---|
| M 151-160 (c-m1) | 3,030 | none (gen 33,691 at 160) | 2,459 | none | 9,367,387 / **0** / 3,306,452 | **0** |
| M 151-200 (c-m2) | 3,512 | 169, 186 | 3,264 | 173, 192 | 81,932,019 / **0** / 26,017,389 | **0** |
| H 301-305 (c-h1) | 20,993 | 303 | 15,275 | 304 | 56,903,422 / **0** / 20,752,830 | **0** |
| H 301-309 (c-h2) | 20,499 | 303, 306, 309 | 15,486 | 304, 308 | 99,509,567 / **0** / 36,025,456 | **0** |
| early 1-50 (c-e1) | 635 | none (gen 31,935 at 50) | 300 | none | 4,972,605 / **0** / 1,939,286 | **0** |

The interface finder (`iface pf`, used off the game-core thread) made 2,382 (M) and 4,583 (H) searches during the load and
none in any window. No generation mismatch and no node without a shadow slot in any run (the counter's own checks).

So **the A10 alias did not occur once** in 5 wraps of the unit path finder and 4 of the step finder, over about 243 million
compares (the two long runs) [L]. An alias needs a node stamped in one cycle, then left untouched (not stamped again, not cleared by a
`Reset`) for exactly 65,534 searches, and first touched again by the search whose ID equals its stamp; nodes near the
action are restamped many times per cycle and far ones are rarely touched at the right search [I]. A10 is therefore a
real but rare defect at these session lengths: a fix (clearing the stamps at the wrap, or a 32-bit stamp) would change
none of these windows' games [I: no aliased read means no different decision], and its urgency is low.

Reads of cache data **not refreshed by the current search** (not A10; a separate finding) `[L]`:

| Site | M 151-200: earlier generation, same cycle / cleared (stamp 0), per turn | H 301-309: same, per turn |
|---|---|---|
| `CommonNeighborIsPassable` (upper bound: every candidate the loop may read) | 92 / 27 | 299 / 113 |
| `PathValid` node | 0 / 0 | 550 / 4,117 |
| `CanEndTurnAtNode` | 0 / 0 | 469 / 1,656 |
| `DestinationReached` (native-domain test) | 0 / 0 | 2 / 146 |
| `UnitPathGetExtraChildren` | 0 / 0 | 53 / 13 |
| `PathCost` node | 0 / 0 | 64 / 14 |
| `PathValid` parent, `PathCost` parent | 0 / 0 | 11 / 2 |

A read of an earlier generation's data uses what another unit's search computed for that plot; a read of a cleared node
uses zeros (every flag false). They are the same in every run of a given game (the identity checks hold), so they are
part of the game as stock plays it; whether they change decisions, and which caller reaches these nodes without the
neighbour refresh (the destination's ring in approximate-target searches, the partial-move layer's neighbours, extra
children), was not traced [I]. Reported for P5/P14's owners; not a part of A10.

## P0.6 Event coverage

Counted per turn: World Congress sessions (`CvLeague::StartSession`, `StartSpecialSession`, `FinishSession` with the
number of enact and repeal proposals on the table at the finish), era changes (`CvTeam::SetCurrentEra` when the era
changes, with the team's players), city-state friendship level changes (`CvMinorCivAI::DoFriendshipChangeEffects`: the
level 0 below the friends threshold, 1 friends, 2 at or above the allies threshold, from the old and new friendship with
the DLL's own threshold getters), and at each turn start the corporations founded, franchises, allied city-states and
friend pairs (a read of every player).

### Counts `[L, 2026-11-17, the runs of "Runs and identity"]`

| Window | Congress sessions (proposals voted) | era changes: major teams / city-state teams | city-state level changes (up / down) | corporations founded / franchises (at the window's end) | allied city-states |
|---|---|---|---|---|---|
| M 151-160 | **none** | 1 / 0 (team 4 to era 3 at 157) | 24 (13 / 11) | **0 / 0** | 13 |
| M 151-200 | 1: started 181, finished 182 (3 enact proposals) | 8 / 17 | 109 (67 / 42) | 0 / 0 | 16 |
| H 301-305 | **none** | 2 / 20 (team 5 to era 6 at 302, which the city-state teams follow the same turn; team 10 to era 5 at 303) | 127 (111 / 16) | 7 / 90 -> 108 | 18-19 |
| H 301-309 | 1: started 306, finished 307 (3 enact proposals); a special session starts at 311 | 2 / 20 | 323 (287 / 36) | 7 / 119 | 18 |
| early 1-50 | **none** | 2 / 0 (teams 0 and 4 to era 1 at 43 and 49) | 23 (14 / 9) | 0 / 0 | 1 |

No corporation was founded inside any window (H's 7 exist from before 300; franchises grow by 0-9 per turn). Team ids
22 and above are the city states' (22 major civilizations in this build).

### What P0.6 decides `[I]`

- **World Congress:** none of the three identity windows (M 151-160, H 301-305, early 1-50) holds a session. A package that
  touches League code (P11 and P8's bursty passes) needs a window with one: M 180-183 (from the M save, 30 turns of play,
  about 2 minutes headless) or H 305-308 (from the H save, about 5 minutes headless) both cover a session with 3
  proposals voted; an extra save at M 180 or H 305 shortens those runs. A special session (H 311) is outside both.
- **Corporations:** none founded in any window, and none exist in M or the early game: CB16 and other corporation code
  run only in H (7 corporations, franchises growing every turn). A founding needs a later M save or another game.
- **Era changes and city-state levels** occur in every window, several per turn on H.

## Runs and identity `[L, 2026-11-17 15:57-16:22 EST, workstation]`

Evidence: `perf/evidence/0504/4800/` (scripts, each run's counter CSVs and records, `reduced/`, `identity.txt`, the build log). Every run: `perf/evidence/0504/4800/run_count.sh` (through `quietlock.py run`, owner 0504, the machine's load before and
after in `C:/c5-64bit-work/4800/load-<run>.txt`), headless `server.exe` direct mode under `jobcap.py`, 0438's identity
database (`stats-on`, `9335186b...`), the toolkit game folder, `dll_log_flags 0`. Counting DLL
**d06fb61df16c18f16dbed9869239e2ec51f15553855b6ca89165936310e8f2c4** (23,674,880 B; x64-count-0504 fe0f2346b, release,
`build_all.py --unlocked --config release`, CL=`-DVP64_COUNT_HOOKS -DVP64_COUNT_ASTAR -DVP64_COUNT_EVENTS`, version
"Release-5.4.8-57-gfe0f2346b7 Clean", narrowing gate 794 / baseline 798, 0 new; `VP64CountInfo.txt` of every run lists
exactly the three defines). Identity with 0438's `idtrace.py` (branch `epic-0437-turn-study`, `perf/evidence/0438/`:
trims both stats.db to the window, then `tools/d10/statscmp.py`).

| Run | server | from | played to | reference | window compared | result |
|---|---|---|---|---|---|---|
| c-m1 | pin layout `x64-04b1211b-pfdee656` | TS0438-M-T150 | 162 | 0437's series r01-p4-S3 | 150-160, 32,631 rows | IDENTICAL |
| c-m2 | pin layout | TS0438-M-T150 | 202 | ref-m2 (the pin's own DLL, same script) | 150-200, 214,989 rows | IDENTICAL |
| c-m2 | | | | r01-p4-S3 (ends at 177) | 150-175, 90,501 rows | IDENTICAL |
| c-h1 | 0452's `layout-new` (as 4669) | TS0438-H-T300 | 307 | 0452's s3new-H10 | 300-305, 125,210 rows | IDENTICAL |
| c-h2 | 0452's `layout-new` | TS0438-H-T300 | 312 | s3new-H10 | 300-309, 215,895 rows | IDENTICAL |
| c-e1 | pin layout | `server/specs/continents-standard-fixed-seed.json` (map seed 1001, sync seed 2002), new game via `run_server.py` | 52 | ref-e1 (the pin's own DLL, same script) | 0-50, 20,626 rows | IDENTICAL |

c-h2 against s3new-H10 over 300-310 differs only in turn 310, which the reference's run ended inside (its rows of turn
310 are partial: BuildingYields 1,502 rows against 14,164); so the long H window is 300-309. No run is void.

Wall times (indications, counters on, shared workstation): M about 2.2-3.1 s per turn (4669's counting build 1.6-2 s),
H 33-47 s per turn, the early game under 0.4 s per turn.

## Not verified

- The client's listener count per name (the ticket's optional read through the harness Lua channel): not taken; the
  handled shares above come from F2's grep of the mods' Lua.
- P2's and P15's seconds apply one M run's mean cost per crossing to headless raise counts; the H client turn was not run.
- Why the not-refreshed reads of P0.2 happen (which callers reach those nodes) was not traced.
- The raise mix of the windowed M game is assumed equal to the headless one's (the crossing totals agree within 1.6 %).
- `CommonNeighborIsPassable`'s counts are an upper bound (the loop stops at the first passable neighbour).
