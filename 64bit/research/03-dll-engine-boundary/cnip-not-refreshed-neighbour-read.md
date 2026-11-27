# CNIP: the unit path finder reads node cache data the current search has not refreshed (ticket 4813, epic 0504)

impl-4813, 2026-11-18, workstation (shared: functional runs, no timing), account A. The MC accepted the root cause
(#5399, 2026-11-18) that ticket 4807's trace found (`../08-performance/speedup-danger.md` 8.2c, 8.2d): a unit path
search's answer depends on which searches ran before it, through two reads of node cache data the current search has
not refreshed. This file is the defect, the fix on fork branches from both batch-4 pins, the trace with the fix,
history independence, and (slice 2) the stock-result change, parity and the upstream draft. Marks: `[C]` read in the
source, `[L]` seen in a run, `[I]` inferred. 4807's traces and P0.2's counts (`../08-performance/speedup-p0-measurements.md`
"P0.2") are not repeated here.

**Headlines** (2026-11-18, slice 1; filled as results land):

- The defect is the same text at the x64 batch-4 pin `c29442e8c`, the i0-ship batch-4 pin `e96457fd5` and upstream
  master `3a511df0f` (2026-11-17) [C]; upstream has no issue or pull request about it (searched 2026-11-18) [C].
- The fix: one commit, the same text and patch-id (`3e180c82`), on fork `fix-cnip-x64` `909f73bd1` and
  `fix-cnip-i0ship` `02749f1b9`: `UpdateNodeCacheData` on each `CommonNeighborIsPassable` candidate, and on each unit
  path extra child before `PathValid` [C]. x64 release DLL `af148133...` [C].
- With the fix, 4807's trace reads **0 not-refreshed node data at every site** on H 300-305, M 150-160, E 0-50 and the
  D24 part 3 continuation (50-199), and the danger package's `on` build plays **the same game as its verify build**
  there (D24 continuation: 355,062 rows IDENTICAL, all 249,178 searches equal): a search's answer no longer depends on
  the finder's history [L].
- The product form plays exactly 4807's tested scratch form (all 65,800 searches of H 300-305 equal) [L].
- Cost in calls: H about 2,300 CNIP and 431,000 extra-children refresh calls per turn, of which about 3,000 do work;
  M about 400 and 4 [L]. Time on `oldpc` (D178 pair, H 301-311, 3 per arm): no cost detectable; the window's -1.10 %
  is the other game the fix plays from turn 302, not a speedup; turn 301, the same game in both arms, -0.14 % [L].
- It changes stock's games (a behaviour change like A9's): D10 tiny from turn 43, standard from 50, huge from 355;
  windows M 157, H 302, E 49 [L]. **x64 = x86 parity: the D24 save test passes on the two fixed DLLs** (30,569 /
  359,261 / 685,488 rows) [L]. Pinning it replaces every D24 and window reference of batch 4.
- Upstream draft for the owner (a pull request text, nothing sent): `upstream-drafts/cnip-not-refreshed-neighbour-read.md`.

## 1. The defect `[C]`

Read at the three trees (`perf/evidence/0504/4813/defect_sites.sh`, output `defect_sites.txt`): `CommonNeighborIsPassable`,
`NodeAddedToPath`, `PathCost`, `UpdateNodeCacheData` are the same text at both pins and upstream master;
`CvAStar::CreateChildren` differs at master only by `!extraChildren.empty()` for `size() > 0`, `DestinationReached`
only by the order of the distance check (master tests the ring distance first, the pins after `CanEndTurnAtNode`; the
same answer), `UnitPathGetExtraChildren` only by `int iCityLoop = 0`. The x64 and i0-ship pins differ in `CvAStar.cpp`
only in the save stream's count type (lines 4135, 4151). Line numbers below are the x64 pin's (master: +45 to +123).

**The node cache.** `UpdateNodeCacheData(node, pUnit, finder)` (`CvAStar.cpp` 940) fills `node->m_kCostCacheData` for
the searching unit from the live plot (revealed, visible, enterable terrain and territory, enemy city or unit, stacking,
airlift and port flags, ...) and stamps it with the search's generation id; for a node already stamped with the current
id it returns at once. Nothing else clears the data between searches except `CvAStarNode::clear()` from the next
search's `Reset`, which reaches only the previous search's open and closed nodes: a node filled only as a neighbour
keeps the data of whatever search (and unit) filled it last. `CvTwoLayerPathFinder::NodeAddedToPath` (1699) refreshes
the added node's neighbours and the extra children `GetExtraChildren` returns **at that moment**, only for
`ASNC_INITIALADD` and `ASNC_NEWADD` (1755-1786).

**Site 1, `CvTwoLayerPathFinder::CommonNeighborIsPassable` (CNIP, 2980).** `PathCost(parent, to)` computes `bIsPathDest
= finder->IsPathDest(to)` (1409) before `LinkChild` calls `NodeAddedToPath(to)`. `IsPathDest` is
`DestinationReached` (`CvAStar.h` 128); with `MOVEFLAG_APPROX_TARGET_RING2` it reads `to`'s own data
(`bIsNonNativeDomain` with `APPROX_TARGET_NATIVE_DOMAIN`, `CanEndTurnAtNode`: fresh, `to` is a neighbour of the
expanded node) and, for `to` at ring distance 1 or 2 of the target, `CommonNeighborIsPassable(to, target)` (1082),
which reads `bCanEnterTerrainIntermediate` and `bCanEnterTerritoryIntermediate` of `to`'s neighbours next to the target.
Those are two steps from the expanded node and are current only if this search has already added one of their
neighbours. With stale data `bIsPathDest` can be wrong in either direction; when it is wrongly false and the unit has no
moves left at `to`, `PathCost` adds the end-turn cost (1494) and `to` sinks in the open list (4807's 181:1613: 3 turns
instead of 0); when wrongly true, the attack-cost branch (1528) can apply. The main loop's own `IsPathDest(m_pBest)`
(406) reads fresh data (the popped node was added, so its neighbours were refreshed), so the stale answer changes the
path through the costs, not through the final test.

Callers of `IsPathDest` in a unit path search: `PathCost` 1409 (**stale**), the main loop 406 (fresh), `CreateChildren`
569 for direct neighbours after a valid link (fresh) and 618 for every extra child (stale reads possible, but the count
it feeds, `m_iDestHitCount`, is used only when not in approximate mode, 412, so a RING2 search never uses it).
`CvStepFinder` has its own `CommonNeighborIsPassable` (3137) and reads no node cache.

**Every caller that reaches CNIP** (`perf/evidence/0504/4813/ring2_callers.py`, output `ring2_callers.txt`): only a
`PT_UNIT_MOVEMENT` search (`CvTwoLayerPathFinder`, the only path type it configures, 3104) with
`MOVEFLAG_APPROX_TARGET_RING2` (0x10000) and a valid destination:

| Caller (x64 pin) | Flags it sets | In 4807's traces as |
|---|---|---|
| `CvAIOperation::GrabUnitsFromTheReserves` (`CvAIOperation.cpp` 478), `CvArmyAI::UpdateCheckpointTurnsAndRemoveBadUnits` (`CvArmyAI.cpp` 414), `CvArmyAI::AddUnit` (613), through `CvUnit::TurnsToReachTarget` (`CvUnit.cpp` 34079) | `RING2 \| IGNORE_STACKING_SELF \| IGNORE_ZOC` | 0x50010 (the split searches of D24 and 9 of 10 on H) |
| `CvTacticalAI::PositionUnitsAroundTarget` (`CvTacticalAI.cpp` 3392) | `NO_STOPNODES \| RING2` (+ the caller's) | 0x830200 (+ `PRETEND_ALL_REVEALED`, `NO_EMBARK`) |
| `CvTacticalAI::MoveToEmptySpaceNearTarget` (5107), `CvTacticalAI::ExecuteSpotterMove` (3242, RING2 beyond distance 2 else RING1), `CvTacticalAI::FindUnitsWithinStrikingDistance` (4681, `RING2 \| IGNORE_DANGER`), `CvTacticalAI::ExecuteMoveToPlot` (3968, 3990: passes RING2 on) | `RING2` with the caller's flags | 0x10200 (`RING2 \| NO_EMBARK`) among them `[I: which caller made each 0x10200 search was not traced]` |
| `CvHomelandAI::ReviewUnassignedUnits` (`CvHomelandAI.cpp` 2533), `CvHomelandAI::ExecuteExplorerMoves` (2890), `CvHomelandAI::MoveCivilianToSafety` (5950, + `PRETEND_ALL_REVEALED`) | `RING2 \| APPROX_TARGET_NATIVE_DOMAIN` | 0x230000 (`MoveCivilianToSafety`) |
| `CvUnit::DoGroupMovement` (`CvUnit.cpp` 15175): a `MISSION_MOVE_TO` with `DESTINATION \| ABORT_IF_NEW_ENEMY_REVEALED \| RING2`; `CvUnit::UnitPathTo` (30545) passes it on | as named | not seen in the traced windows |

**Site 2, the unit path finder's extra children (`CvAStar::CreateChildren` 582-622).** For each extra child that
`GetExtraChildren(node)` returns at expansion (`UnitPathGetExtraChildren` 2241: airlift targets of an airlift city or
improvement, admiral port changes), `CreateChildren` calls `PathValid(node, child)` and `LinkChild` -> `PathCost` with no
refresh. `NodeAddedToPath` refreshed only the extra children found when `node` was added; the set at expansion can be
larger: an airlift needs the node's moves to be full or zero (2258), and an open node's moves change when a cheaper path
reaches it (`ASNC_OPENADD_UP`, which does not refresh). (The airlift- and port-checked flags only turn true within a
search, so they can only shrink the set.) The children are then judged on another search's leftovers: in 4807's H trace
(8.2d, 310:6522) every not-refreshed read at `PathValid`, `PathCost`, `CanEndTurnAtNode`, `DestinationReached` and
`UnitPathGetExtraChildren` was of such a child or what it led to. Only the unit path finder has node cache data for its
extra children: the city-connection and rebase finders' validity functions read none `[C: no m_kCostCacheData read in
RebaseValid, CityConnection*Valid]`. `CvAStar::VerifyPath` (820) also calls `PathValid` on unrefreshed nodes but has no
caller.

**Upstream** `[C, 2026-11-18, before 13:02 EST]`: `gh search issues` and `gh search prs` in LoneGazebo/Community-Patch-DLL for
"CommonNeighborIsPassable", "UpdateNodeCacheData", "pathfinder airlift", "pathfinder nondeterministic", "pathfinder
stale cache", "path finder cache neighbor", "APPROX_TARGET_RING2", "pathfinder history", "airlift path": nothing about
it (only #6799, closed, `EVENTS_CAN_MOVE_INTO`; #13423, merged, the node memory leak; #13518, open, build-route memo).
Open pull requests touching `CvAStar*`: #13518 (build-route callbacks; names `RebaseGetExtraChildren` only as context)
and #13424 (tactical-zone cache, `CvAStarNode.h` `ReachablePlots`): neither touches the two sites. Upstream commits to
`CvAStar.cpp` since the pins' base: #13520, #13582, #13428, #13423 (none changes the sites, above).

## 2. The fix `[C]`

One commit, "Path finder: update the node cache of a common neighbour and of extra children before reading it",
`git patch-id --stable` `3e180c82` on both branches (`perf/evidence/0504/4813/commit_fix.sh`, log `commit_fix.log`):

| Branch (fork) | Head | Base (pin) | describe |
|---|---|---|---|
| `fix-cnip-x64` | `909f73bd1fd520b0aa4c4ac6266fbe722e415ec4` | x64 batch 4 `c29442e8c` | Release-5.4.8-64-g909f73bd1 |
| `fix-cnip-i0ship` | `02749f1b99e3653e4c1fbe23efe4c10401b291b2` (cherry-pick, no `-x`, clean) | i0-ship batch 4 `e96457fd5` | Release-5.4.8-50-g02749f1b9 |

The diff (19 lines added, 1 changed, `CvAStar.cpp` and `CvAStar.h`):
1. `CommonNeighborIsPassable`: `UpdateNodeCacheData(check, scratch->pUnit, this)` on each candidate before reading its
   flags (4807's tested `28c5598fc` without the define).
2. `CreateChildren`, extra children: when the finder's extra-children function is `UnitPathGetExtraChildren`, the unit
   (`GET_PLAYER(m_sData.ePlayer).getUnit(m_sData.iUnitID)`, the same unit `UnitPathInitialize` puts in the scratch
   buffer) is looked up once per expansion that has extra children, and `UpdateNodeCacheData(child, pUnit, this)` runs
   before `PathValid` (a no-op with a null unit, i.e. for every other finder). The same refresh at the same place as
   4807's tested `d313b0b18`; the product form needs no helper because the unit comes from the search data instead of
   the scratch buffer (whose struct is defined later in the file).
3. `CvAStar.h`: the declaration of `UnitPathGetExtraChildren` takes `const CvAStar*` like its definition (it declared a
   never-defined overload with `CvAStar*`); this lets `CreateChildren` compare the function pointer without a cast.

Why it is exact for the search's own unit: `UpdateNodeCacheData` is a no-op for a current node, and a later refresh in
the same search computes the same values (the game does not change during a search), so the only change is that these
reads see this unit's current data instead of leftovers. It changes stock's answers wherever a search read leftovers
(4807: from D24 turn 50 and H turn 302), so it is a pin-batch change with A9's kind of statement (section 5).

Smaller equivalents considered (not taken): refreshing extra children in `NodeAddedToPath` also on `ASNC_OPENADD_UP`
covers only the moves case argued above, rests on an `[I]` about when the set can grow, and refreshes children that may
never be expanded; a refresh at the top of `PathValid` would be one line but runs for every child of every expansion
(millions of calls per H turn, each a call and a compare) and needs a `const_cast`; clearing nodes in `Reset` does not
reach neighbour-filled nodes (8.2c).

## 3. Builds

`perf/evidence/0504/4813/builds.sh` (log `builds.log`), workstation, this repository's tools at `task-4813-cnip`, stubs
= 4807's batch-4 stubs (`C:/c5-64bit-work/4807/defineoff-b4/build/stubs`, built from the pin's plain copy, 8.5):

| Build | Source | Config | DLL SHA-256 | Notes |
|---|---|---|---|---|
| x64 release, `fix-cnip-x64` | plain copy of `909f73bd1` | release (LTO), `build_as_pin.py -- --config release --shorten`, version "Release-5.4.8-64-g909f73bd1 Copy" | `af148133f3b4b87588ce0143d193b03f2732e5e28723a8207b4f3a2d953db8e3` (23,681,536 B) | 2026-11-18 13:13:52-13:23:32 EST, link 434 s; narrowing 794 / baseline 798 (0 new, 4 gone); 0 unresolved; repeatability check OK; `C:/c5-64bit-work/4813/rel-x64/dll` |
| x86 release, `fix-cnip-i0ship` | plain copy of `02749f1b9` | `build_vp_clang.py --config release` under `quietlock.py run`, version "Release-5.4.8-50-g02749f1b9 Copy" | (running) | `C:/c5-64bit-work/4813/rel-i0` |
| trace `fixverify` | `scratch-4813-trace` `d0710f7e9` | fast; `DANGER`, `DANGER_DG7`, `TRACE_FINDER`, `DANGER_VERIFY`, `DANGER_DG7_VERIFY` | `4b9313650e492cb4f0407b05725a8075758c639bdb90dad08c3cf99fc1d2801e` | 13:07-13:10 |
| trace `fixon` | the same | fast; `DANGER`, `DANGER_DG7`, `TRACE_FINDER` | `5b0352374957c0d705afb401226b196a80c81f7d6f53866fe69c203a2b43e7cd` | 13:10-13:13 |
| count | `scratch-4813-count` `5e8205102` (= `fix-cnip-x64` + `VP64_COUNT_CNIP_REFRESH`) | fast | `84c8b0201ced5bae4fca34b4a3ce361904d4ac18ae603e3c0d110c249a3a09ae` | 13:13-13:17 |

## 4. The trace with the fix and history independence `[L, 2026-11-18 13:10-13:24 EST, workstation (shared): functional runs]`

Fork scratch `scratch-4813-trace` (`scratch_branch.sh`) = `perf/speed-danger-b4` `aeae7c8e5` (the danger package on
batch 4, 4807's 8.5) + the fix (`437157894`, a clean cherry-pick of `909f73bd1`) + 4807's `VP64_TRACE_FINDER`
(`d0710f7e9` = `3c34a91e4`; one conflict, in `CommonNeighborIsPassable`, resolved with the trace read after the
refresh). Arms: `fixverify` (both verify defines: every cached search still runs, so the finder's history is stock's)
and `fixon` (the package on: the cached searches are skipped). `perf/evidence/0504/4813/trace_runs.py`, each window in the
batch-4 pin's layout (`x64-c29442e8-p58724e0`) with the DLL swapped; H, M, E exactly as `idjob.py` plays them; D24C =
the D24 standard part 3 continuation of 8.2b/8.2c (`D10-STANDARD-x64-T50` from 4807's a9 D24 run, to turn 200,
`run_server.py` defaults + `--dll-log-flags 0`). Runs `F:/c5-64bit-work/4813/trace/`; reports
`perf/evidence/0504/4813/trace/` (`sitesum-*`, `align-*`, `statscmp-*`).

**Not-refreshed reads with the fix: 0 at every site, in every window, in both arms** (`sitesum.py`: pv_node,
pv_parent, pc_node, pc_parent, cetan, cnip, dr_native, extra, stop_next all 0):

| Window | turns traced | searches outside / inside `Reach` | not-refreshed reads (fixverify / fixon) |
|---|---|---|---|
| H 300..305 | 6 | 39,605 / 26,195 | 0 / 0 |
| M 150..160 | 11 | 22,524 / 4,046 | 0 / 0 |
| E 0..50 | 51 | 17,221 / 3,387 | 0 / 0 |
| D24C 50..199 | 150 | 205,944 / 43,234 | 0 / 0 |

(Without the fix the batch-4 H window read such data 93,451 times in 2,775 searches over 300-310, 8.2d; the D24
continuation 9,407 times at CNIP, 8.2c.)

**History independence: fixverify against fixon IDENTICAL in every window**, and every search answers the same:

| Window | stats.db fixverify vs fixon | searches (`align.py --all`) |
|---|---|---|
| **D24C (the D24 part 3 continuation)** | **IDENTICAL**, 15 tables, 355,062 rows | **249,178, no difference** |
| H 300..306 | IDENTICAL, 14 tables, 133,662 rows | 65,800, no difference |
| M 150..161 | IDENTICAL, 15 tables, 34,581 rows | 26,570, no difference |
| E 0..51 | IDENTICAL, 15 tables, 21,035 rows | 20,608, no difference |

So with the fix the package's skipped searches change nothing a later search sees on these trajectories; without it
the batch-3 `on` game left stock at D24C turn 182 (8.2b) and 10 searches split on H (8.2d).

**The product form equals 4807's tested form** `[L]`: `align.py --all` of 4807's `fix2verify` trace on H (scratch
`d313b0b18`: `VP64_TRACE_FIX_CNIP` + `VP64_TRACE_FIX_EXTRA`, the extra-children refresh through a helper reading the
scratch buffer's unit, played to 311) against this `fixverify` (played to 306): **all 65,800 searches of 300..305 equal,
0 result differences** (`trace/align-H-4807fix2verify-vs-4813fixverify.txt`; the files part only where 4807's run goes on
to turn 306).

**The fix's cost in calls** `[L, 13:17-13:24 EST]` (`count_and_windows.sh count`, `count_run.py`; fork
`scratch-4813-count` `5e8205102`, which counts every call of the two new refreshes and those that found the node not
current, all finders; fast build; the stock trajectory with the fix, no package; per-turn files
`perf/evidence/0504/4813/count/{M,H}-VP64CountCnip.csv`; the window's last turn is not written, the process ends before
the flush):

| Window | CNIP refresh calls / turn | of which did work | extra-children refresh calls / turn | of which did work |
|---|---|---|---|---|
| M 151..159 (mean of 9) | 409 | 59 | 4 | 0 |
| H 301..305 (mean of 5) | 2,277 | 283 | **430,981** | 2,689 |

The extra-children site is called for every extra child of every expansion (the airlift targets of an airlift city or
improvement: on late H about 40 per unit search), almost always a no-op (the child was refreshed when its parent was
added). A call that does nothing is a null check and one compare; one that works is one `UpdateNodeCacheData` (plot
lookups, a few unit tests). Both counts are over the ticket's 1,000 per H turn, so the D178 dev pair on `oldpc` is due
(slice 2). A cheaper form if the pair shows a cost: test `check->m_kCostCacheData.iGenerationID !=
GetCurrentGenerationID()` inline before the call (the same behaviour, no call for current nodes) `[I]`.

## 5. The stock-result change

The fix changes stock's own answers in every search that read leftovers, so it changes games (a behaviour change, as
A9's): every reference game of the batch-4 pins is replaced when it is pinned.

**The window games** `[L, 2026-11-18 13:25-13:31 EST]` (`count_and_windows.sh windows` = `perf/speedkit/idjob.py --dll
<x64 release fix af148133...> --windows M,H,E`, against the batch-4 pin's DLL `2B56A1CD...` in its layout; report
`perf/evidence/0504/4813/idjob-rel-vs-pin.txt`; runs `F:/c5-64bit-work/4813/id-rel/`):

| Window | Result | First differing turn and table | Tables that differ |
|---|---|---|---|
| M 150..160 | DIFFERENT | **157**, `HandicapYields` (rows 226 / 220; `Civ` at row 145: pin 'The Ottomans', fix 'Byzantium') | 10 of 15: `HandicapYields`, `MilitarySummary`, `MapUnitsState`, `BuildingInstantYields`, `InstantYields`, `TechChoices`, `BuildingYields`, `BuildingsOverview`, `ReligionBeliefYields`, `WorldStateLog` |
| H 300..305 | DIFFERENT | **302**, `MapUnitsState` (rows 7,308 / 7,306; `plotX` at row 1,907: pin 119, fix 120) | 11 of 14: `MapUnitsState`, `BuildingInstantYields`, `HandicapYields`, `InstantYields`, `MilitarySummary`, `TechChoices`, `BuildingYields`, `BuildingsOverview`, `ReligionBeliefYields`, `civ_turn_era`, `WorldStateLog` |
| E 0..50 | DIFFERENT | **49**, `MapUnitsState` (`unitCurrHP` at row 2,355: pin 49, fix 54) | 1 of 15: `MapUnitsState` |

H from 302 is what 4807 saw with both scratch refreshes (8.2d). **The release fix plays the trace arm's games**: its
M, H and E `stats.db` are IDENTICAL to the `fixverify` trace build's (34,581 / 133,662 / 21,035 rows), so section 4's
zero counts are counts of the product build's own games `[L]`.

**The D10 games** `[L, 2026-11-18 13:53 EST]` (`parity_d10.sh statscmp`: `tools/d10/statscmp.py`, whole `stats.db`,
D24's skips only, each direct play of the fixed save test of section 6 against the same arm's direct play of batch 4;
batch 4's plays are 4805's run_d10 of batch 3 + A9, `F:/c5-64bit-work/4805/d10-a9`, which 0517 found IDENTICAL row for
row to the batch-4 take's own save test, `upstream-fix-batch-4.md` 9.6; reports `perf/evidence/0504/4813/statscmp/`,
first turns by `d10_first.py`, `statscmp/summary.txt`):

| D10 game | fixed rows (run_d10, x86 direct = x64 direct) | batch-4 rows | x86 / x64 against batch 4 | First differing turn (table) | Tables that differ |
|---|---|---|---|---|---|
| tiny 0-100 | 30,569 (15 tables) | 31,226 | DIFFERENT / DIFFERENT | **43** (`MapUnitsState`) | 14 of 15 |
| standard 0-200 | 359,261 (15) | 349,074 | DIFFERENT / DIFFERENT | **50** (`MapUnitsState`; `HandicapYields`, `InstantYields`, `TechChoices` 57) | 14 of 15 |
| huge 350-400 | 685,488 (14) | 683,261 | DIFFERENT / DIFFERENT | **355** (`BuildingInstantYields`, `HandicapYields`, `InstantYields`, `MapUnitsState`) | 13 of 14 |

Standard's turn 50 is the turn where 8.2c saw the fix change stock's first search answer on the D24 continuation from
the same game's turn-50 save (turn 50, search 20) `[L]`. So, in the form of `upstream-fix-batch-4.md`: **the fix changes
every D10 game and every window game; when it is pinned, every D24 reference (31,226 / 349,074 / 683,261 rows) and
every window reference of the batch-4 pins is replaced** (as A9's did for batch 4).

## 6. Parity, cost, upstream draft

**x64 = x86 parity: the D24 save test PASSES on the two fixed DLLs** `[L, 2026-11-18 13:30:44-13:52:35 EST, wall
1,311 s, workstation]` (`perf/evidence/0504/4813/parity_d10.sh` = `tools/d10/run_d10.py --x86-dll <x86 fix 690ce2b5...>
--srv-bin <the batch-4 pin layout bin with only the DLL and PDB of the x64 fix af148133...> --order-exception none
--d18-x86 yes --d18-x64 yes --jobs 8`, AI logging off in both arms, D49; layout hashes
`layout-x64-fix-cnip-SHA256.txt`; report `perf/evidence/0504/4813/d10-report.txt`, user paths as `%USERPROFILE%`; run
folder `F:/c5-64bit-work/4813/d10`): tiny 0-100, standard 0-200 and huge 350-400 (the release descriptor's huge save
`Release-5.4.8-HUGE_0350`, `c499d3c6...`) each pass part 1 (writer identity; only the version-string exception,
versions "Release-5.4.8-50-g02749f1b9 Copy" and "Release-5.4.8-64-g909f73bd1 Copy"), part 2 (**x86 direct = x64 direct:
30,569 / 359,261 / 685,488 rows**) and part 3 (cross-load). Every host run reached its turn with 0 Lua errors.

**Cost: no time cost detectable** `[L, 2026-11-18 13:29-13:47 EST, oldpc, quiet: alone, undisturbed, foreign cores
0.032; RTX 2080, driver 32.0.16.1088 of 2026-07-21]`. The calls counted in section 4 are over the ticket's 1,000 per H
turn, so the D178 dev pair ran: `perf/speedkit/pair.py --a pin=<the batch-4 pin's DLL 2B56A1CD...> --b fix=<af148133...>
--scenario H --runs 3 --turns 11` (both arms in the pin's layout, server `fc73e60e`, AI logging off; job
`20261118-132817-824d`, `submit_pair.log`; fetched to `perf/evidence/0504/4813/oldpc-20261118-132817-824d/`,
`perf/tbjobs/analyze.py <out> --ref pin`):

| Measure | pin (3 runs) | fix (3 runs) | fix - pin |
|---|---|---|---|
| s/turn, H 301-311 | 14.916 (sd 0.028) | 14.751 (sd 0.004) | -0.165 s (-1.10 %), 95 % CI [-0.235, -0.094] |
| turn 301 only (the same game in both arms) | 14.398 (14.401, 14.427, 14.365) | 14.378 (14.346, 14.404, 14.385) | -0.020 s (-0.14 %), within the runs' spread (about +-0.5 %) |
| peak working set | 261 MB | 259 MB | |
| private MB after 10 turns (D151) | 321.4 (319.5, 323.0, 321.7) | 326.3 (324.6, 327.8, 326.4) | +4.9 MB |

Why the window numbers moved: **the fix plays another game from turn 302** (section 5; round 1's `stats.db` pin vs fix
DIFFERENT on `oldpc` too), so turns 302-311 time different AI work: the per-turn means part where the games part (307:
14.83 pin / 13.93 fix, 309: 14.12 / 12.18, 310: 15.72 / 16.62). The -1.10 % is therefore **not a speedup of the fix**
and is not claimed as one; the one turn that is the same game in both arms (301, every search of the fix's extra
refreshes included) shows no difference beyond about 0.5 %. Likewise the +4.9 MB after 10 turns is the other game's
state, not the fix (which allocates nothing) `[I]`. Consistent with the counts: about 3,000 refreshes that do work and
430,000 that return at once per H turn, a few milliseconds of a 14.4 s turn `[I]`.

**Upstream draft**: `upstream-drafts/cnip-not-refreshed-neighbour-read.md` (nothing sent, D95, D130), its "pending"
sentences to be settled by the results above and the pair.

## 6a. Fork branches

Pushed 2026-11-18 13:31 EST to the fork on GitHub, new branches only, after checking that none carries the three
forbidden commits (`perf/evidence/0504/4813/push_fork.sh`, `push_fork.log`): `fix-cnip-x64` `909f73bd1`,
`fix-cnip-i0ship` `02749f1b9` (the fix), `scratch-4813-trace` `d0710f7e9` and `scratch-4813-count` `5e8205102` (never
product builds). Local clone `C:/c5-64bit-work/4813/fork` (its worktrees removed at hand-back; build trees
`C:/c5-64bit-work/4813/{rel-x64,rel-i0,b-*,layout}` kept until the lead merges).

## 7. Not verified

- AI quality: the fix makes ring-2 targets and extra children judged with the moving unit's own current data, which
  looks like the code's intent `[I]`; no play-strength measure was made.
- Other games and longer runs with the fix: only H 300-305, M 150-160, E 0-50, the D24 continuation 50-199 and (section 6)
  the D10 games. A site that reads not-refreshed data only in situations these windows lack (another extra-children
  kind, `VerifyPath`, which has no caller) would not show.
- The caller of each traced flag combination other than 0x50010 (`TurnsToReachTarget` from the army and operation code,
  4807's 8.2c `[I]`) was not confirmed by a stack.
- The i0-ship (32-bit) build's own counts and trace: the x86 DLL ran only in the save test (section 6).
- A game with a window (the client path): headless only. The fix touches no engine interface.
- The count build's last turn of each window is not written (the process exits before the flush); the means are over
  the turns written.
