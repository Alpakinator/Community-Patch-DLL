# Speedups P3, tactical AI: A5, TA14, MB2 (ticket 4809): design note

Ticket `tracker/issues/4809-speedups-p3-tactical.md`, epic 0504. impl-4809, 2026-11-18/19. Line numbers are at pin batch
5's x64 branch `x64-batch5` `1fd51353e` (fork `CvGameCoreDLL_Expansion2/`; batch 4 `c29442e8c` + 4813's finder fix +
x64-ifver; lines of the files below are the batch-4 lines, neither commit touches them). Fork clone of this ticket
`C:/c5-64bit-work/4809/fork`; package branch **`perf/speed-p3`**, cut from `x64-batch5` on 2026-11-19 01:05
(`perf/evidence/0504/4809/recut_b5.sh`; it was first cut from `c29442e8c` with no commits, lead-0504's order after batch
5 appeared). One define `VP64_SPEED_P3` for the package; each item its own counter (section 5). Confidence marks as in
`research/README.md`. (Replaced 2026-11-19: the 2026-11-18 21:15 draft's TA14 notes, now section 1.)

## 1. TA14: the unread citadel scan

**What runs** `[C]`. The `CvTacticalPlot` constructor (`CvTacticalAI.cpp`:8534, the line at 8557) sets `bMightWantCitadel =
kPlayer.IsNicePlotForCitadel(pPlot)` for every tactical plot. `IsNicePlotForCitadel` (`CvPlayerAI.cpp`:3442) calls
`GetTopCitadelPlotsCached` (3414), which once per player per game turn (`m_iCurrentCitadelTargetsTurn`) runs
`GetBestCultureBombPlots(eGeneral, {}, bCheckDanger=false)` (3195) over every plot of the map and keeps the top 5 in
`m_vCurrentCitadelTargets`. Nothing reads the result: `isNicePlotForCitadel()` (`CvTacticalAI.h`:914) has no caller;
`GetTopCitadelPlotsCached`, `m_vCurrentCitadelTargets` and `m_iCurrentCitadelTargetsTurn` (`CvPlayerAI.h`:112-113,
reset at `CvPlayerAI.cpp`:97) have no other user and are not serialized. So the scan can affect the game only through
its side effects.

**Side effects, call by call** `[C, by reading]`:

1. *The closest-city map refresh.* For a candidate plot that passes the GP-improvement, `canBuild`, razing and
   `GetDefenseBuildValue / 10 > 0` tests, the scan calls `GetClosestCityByPlots` and `GetCityDistanceInPlots`
   (`CvPlayer.cpp`:41749-41770). For a major civ they read `CvGame::m_cityDistancePlots`, whose getters run `Update()`
   when the map is dirty (`CvDistanceMap.cpp`:123-168); for a minor civ they use the capital and `plotDistance` (no map).
   `CvDistanceMapByPlots::Update()` (226-266) reads only the alive players, their city lists (ids, plots) and
   `isMajorCiv`. Every change of those calls `SetClosestCityMapDirty()` **after** the change: `initCity`
   (`CvPlayer.cpp`:2923, after `addCity` and `init`), `deleteCity` (41719; `acquireCity` and city death go through it),
   loading (45957); a player's death follows the deletion of its last city, a rebirth an `initCity`. So outside the
   inside of `initCity` (between `addCity()` and its `SetClosestCityMapDirty()`), a read returns the current state
   whether or not the scan refreshed the map before. **Inside that window it does not**: if the map was dirty before
   `initCity` and the scan was the last reader, the scan's refresh leaves it clean with the old cities, and a read inside
   `CvCity::init` gets the old map; without the scan the same read finds it dirty and rebuilds it with the new city
   half made. That is the refresh the review asked to keep. **The guard** (exact): on the speed path, at the place where
   the scan ran (the first `IsNicePlotForCitadel` of the player in the turn), if the player is a major civ and the plots
   map is dirty, replay the scan's loop **up to its first call of `GetClosestCityByPlots`** (the same builds, the same
   plots in the same order, the same tests before the call) and make that call; if no plot reaches it, make none. When
   the map is clean the call is a pure read, so nothing is replayed (the common case: the map is dirty only after a city
   change and until its next reader).
2. *Lua hooks.* `canBuild` -> `canHaveImprovement` raises `GameEvents.PlotCanImprove` only with `MOD_EVENTS_PLOT`
   (`CvPlot.cpp`:2913). Our option file has `EVENTS_PLOT` = 0 (`(1) Community Patch/Database Changes/
   NewCustomModOptions.xml`:1046) `[C]`. The speed path runs the original scan whenever `MOD_EVENTS_PLOT` is on, so a mod
   set with the option on keeps the hook calls (and the two-thread unlock that every raise does).
3. *Reads.* `GetCultureBombBoost` (a field), `getImprovementType`, `GetImprovementInfo`, `canBuild` (const; its only
   static is the `IMPROVEMENT_FEITORIA` id, initialised once), `getOwningCity`, `IsRazing`, `GetDefenseBuildValue`
   (const), `RING` iteration, `isCity`, `getOwner`, `getTeam`, `IsStealBlockedByImprovement`, `IsChokePoint`,
   `getResourceType`, `IsImprovementEmbassy`, `GetPlayerThatBuiltImprovement`, `getYield`, the minor-civ queries
   (`IsFriends`, `IsProtectedByMajor`, `GetAlly`), `GetCivOpinion` (const) and `IsBadTheftTarget` (non-const by
   signature; reads approach, war state and minor friendship). The `mutable` members on these paths are `CvPlot`'s water
   and adjacency flags (`CvPlot.h`:977-980), memo bits of terrain facts. `[I]` none of them changes what a later call
   returns; the identity runs (section 6) are the check.
4. *An original defect, harmless to the removal:* `goodPlots.top()` after the last `pop()` reads an empty priority
   queue (`CvPlayerAI.cpp`:3433) when fewer candidates than 5 exist: a read of freed or unused memory, no write.

**The speed path** (`CvPlayerAI::IsNicePlotForCitadel`, `VP64_SPEED_P3`): with `MOD_EVENTS_PLOT` off, keep the once per
player per turn test on `m_iCurrentCitadelTargetsTurn` (set it as the original does), run the guard of item 1, return
false (the field is unread). Counters: `P3_TA14` scans skipped, `P3_TA14G` guard replays, `P3_TA14R` refreshes made.

## 2. MB2: the simulation's buffers kept across a player change

**What happens at the pin** `[C]`. The tactical simulation keeps three pools of preallocated slots: `gTactPosStorage`
(6000 `CvTacticalPosition`), `gSupportPosStorage` (60 `CvSupportPosition`), `gAssignmentStorage` (2000
`STacticalAssignment`) (`CvTacticalAI.cpp`:43-45). `FindBestUnitAssignments` (12190) starts every simulation with
`reset(eLastTactSimPlayer != ePlayer)` on each (12252-12254): a soft reset (same player) only sets the use count to 0
and keeps every slot's containers with their capacity; a **hard reset** (the player changed) also `wipe()`s every slot
of the pool (`swap` with an empty container: all buffers freed) and empties the tactical pool's spare buffer lists
(12921-12944). So the first simulation of every player in a turn starts on empty slots and allocates again what the
previous player's simulations had grown. MB2: on the speed path a hard reset does what a soft reset does (keeps the
buffers), unless the kept memory passes a cap; then it wipes as the pin does.

**Why this is exact** `[C]`: the game's result depends only on the containers' contents, never their capacity, as long
as every slot is fully re-initialised before it is read. Slots are only handed out by `peekNext()` and then
- a tactical root by `initFromScratch` (10135-10170): sets every scalar and `clear()`s every container field
  (`childPositions`, `tactPlotLookup`, `tactPlots`, `availableUnits`, `notQuiteFinishedUnits`, `finishedUnits`,
  `assignedMoves`, `enemyPlots`, `freedPlots`, `unitDamageDealt`, `plotScores`): every field `wipe()` touches
  (`CvTacticalAI.h`:1271-1283, 1365-1371);
- a tactical child by `initFromParent` (10173-10218): every scalar from the parent, `childPositions.clear()`, and every
  copy-on-write field `inheritFrom` the parent (parent pointer set, `dirty` false: `read()` returns the parent's data
  and `write()` first copies it over the local buffer, `CvTacticalAI.h`:1125-1166; old local contents are never read);
- a support root by `initFromTacticalPosition` (11662-11716) and a child by `initFromParent` (11718-11757): the same
  pattern for its fields (`finalCombatPositionUnits` included);
- an assignment slot by `STacticalAssignment::init` (all eight `peekNext()` sites, 7819-10974) which sets every field
  and `clear()`s `unitDamage` and `unitHealing` (`CvTacticalAI.h`:588-589).
The soft reset already relies on exactly this (slots reused within a player's turn hold the previous simulation's
contents); a hard reset without `wipe()` leaves the same kind of contents, from another player. The spare lists hold
buffers whose contents the adopting field clears or never reads (the same argument; `adoptSpare` only moves a buffer
into a field whose capacity is 0, `CvTacticalAI.h`:1618-1625). The slot addresses (`aPositions + i`) are fixed for the
pool's life, so nothing that keys on a slot pointer moves.

**Pointer-order check** `[C]` (no iteration ordered by pointer value that MB2 could move): the open-position heap orders
by generation, last-round score and slot ID (`PrPositionSortHeapGeneration`, `CvTacticalAI.h`:1233-1255); completed
positions by total score, assignment count, ID (1257-1268); `gOverAllChoices`, `gPossibleMoves`,
`gPossibleRangedAttacks` (`OptionWithScore<STacticalAssignment*>`) by score only, stable (`CvGameCoreUtils.h`:448-452;
the pointers are slots of `gAssignmentStorage`, fixed); `SComboMove` by score (`CvTacticalAI.h`:708-722); the sets of
units and plots in `CvTacticalAI.cpp`:2591-2665 by unit id and plot index (`PrSortByUnitId`, `PrSortByPlotIndex`); the
one pointer-keyed container, `map<const CvTacticalPosition*, ...> nextAttackPosition` (12760, used by `find` at 11421),
keys on slot addresses, which MB2 does not move. No container's buffer address is a key or a sort criterion.

**The cap and the memory** : before skipping a wipe the speed path sums the kept capacity of the tactical pool (each
slot's vector fields: capacity x element size; `plotScores`, a `map`, as size x 48 B `[I]` node size; the spare lists'
buffers) and wipes as the pin does when the sum exceeds `VP64_SPEED_P3_MB2_CAP_MB` MiB (**default 16 since
fork `a6fc1778b`, 2026-11-19**; it was 32, the review's figure, until slice 3's measurements, 6.5). Each hard reset
writes one row to `%VP64_SPEED_DIR%\VP64P3Mb2.csv` (turn, player, kept bytes, kept or wiped). The D151 memory after 10
turns: 6.5. Counters: `P3_MB2` hard resets with buffers kept, `P3_MB2W` hard resets
wiped at the cap.

## 3. The statics list and the two-thread answers

The rule (the Architect's review section 2): no state live across a hook raise and reachable from a ui-thread DLL call
unless the working object owns it or no raise can occur while it is live.

| Static or global | added by | live across a raise? reachable from the ui thread? | answer |
|---|---|---|---|
| (none in TA14's code) | TA14 | the guard reads `m_iCurrentCitadelTargetsTurn` (existing member) and the existing distance map | no new state |
| kept buffers of `gTactPosStorage`, `gSupportPosStorage`, `gAssignmentStorage` and the spare lists | MB2 (existing pools; MB2 keeps their capacity) | the buffers are existing globals' memory; between simulations they hold no state any later read uses (section 2: every slot is re-initialised before it is read). The pools are used only by `TacticalAIHelpers::FindBestUnitAssignments` and the support search, called only from `CvTacticalAI.cpp` (no other file calls them `[C, grep]`), i.e. from the AI's turn on the game-core thread. Whether a hook can raise inside a simulation is the pin's question, not MB2's: MB2 adds no state that lives from one simulation to the next | safe: MB2 adds capacity, not state |
| `static FILE*` of the MB2 row file, its row count | MB2 (statistics) | written only from the hard reset, inside `FindBestUnitAssignments` (core thread) | not reachable from a ui call |
| the counter rows (`P3_TA14`, `P3_TA14G`, `P3_TA14R`, `P3_MB2`, `P3_MB2W`) | both | `VP64SpeedVerify.h`'s logger, which takes its own SRW lock (the kit) | safe |

## 4. A5: the camp search without embarking (slice 2)

Not built in slice 1 (the brief). This section is the design and the proof as far as slice 1 read the code `[C, by
reading, 2026-11-19]`; slice 2 builds it and closes the items marked open.

**The call** (`CvTacticalAI.cpp`:1030): `FindUnitsForHarassing(pCamp, iRange, -1, -1, DOMAIN_LAND, false,
bAllowEmbarkation=true, 3)`; inside (4825-4890), for each of our units on a plot of the step finder's reach set of the
camp, after the cheap filters: flags `MOVEFLAG_AI_ABORT_IN_DANGER | MOVEFLAG_NO_EMBARK` (the inverted name), plus
`APPROX_TARGET_RING1 | APPROX_TARGET_NATIVE_DOMAIN` when an enemy unit stands in the camp and the unit cannot attack
with a move; then `TurnsToReachTarget(pCamp, iFlags, iRange)`, a full path search. A5 decides "no path" without the
search where the finder provably finds none.

**What the finder lets such a unit enter** (`CvAStar.cpp`, `PathValid` 1572-1700):
1. An unrevealed plot is refused at once (1585) unless the unit is human-controlled (`isHuman(ISHUMAN_AI_UNITS)`), the
   search has `MOVEFLAG_PRETEND_ALL_REVEALED`, or the unit's AI type is `UNITAI_EXPLORE`. So for an AI non-explorer,
   unrevealed water is closed: **the review's "unrevealed water" hole does not exist for those units**; for the three
   exceptions it does, and A5 never skips them.
2. On a revealed plot, with `MOVEFLAG_NO_EMBARK`, a step from a native-domain plot to a non-native one is refused
   (1668-1671, under `CanEverEmbark()`); a unit that can never embark cannot enter a non-native plot's terrain at all
   (`bCanEnterTerrainIntermediate`, 1654). A step from non-native to non-native is allowed ("move along if already on
   water"): so a unit that **starts** on a non-native plot (embarked, or cargo: `isNativeDomain` is false for cargo) is
   never skipped.
3. "Native" is `CvUnit::isNativeDomain` (`CvUnit.cpp`:14564-14610) for a land unit: not water; or water with an
   improvement that `IsAllowsWalkWater`; or **ice** (treated like land); or shallow water for `IsEmbarkDeepWater` units;
   or any water for `canMoveAllTerrain` units. These are the "ice and every other way": the test uses this function
   itself, plot by plot, so it follows whatever the unit's promotions make native.

**Correction 2026-11-19 (slice 2, 6.3)**: the finder also makes non-adjacent steps (airlift, sealift: `UnitPathGetExtraChildren`); the claim below that every step goes to an adjacent plot of `N(u)` holds only when no lift is possible for the team, and A5 now skips nothing otherwise.

**The exact test**: let `N(u)` be the plots revealed to the unit's team for which `u->isNativeDomain(plot)` holds. For an
AI-controlled non-explorer land unit standing on a native plot, every step of any path the finder accepts goes to a plot
of `N(u)` (items 1 and 2), so the path stays in the connected component of `N(u)` (the 6 hex neighbours, the map's wrap; "8-neighbour" until 2026-11-19, corrected: `NUM_DIRECTION_TYPES` is 6) that
holds the unit's plot. The search's destination test needs the camp (or, with `APPROX_TARGET_RING1`, the camp or a plot
next to it; with `APPROX_TARGET_NATIVE_DOMAIN` only native ones). **If no admissible destination lies in that
component, the search returns "no path", and skipping it is exact.** The other refusals of `PathValid`/`PathCost`
(territory, enemy cities, stacking, danger abort) only remove steps, so they cannot make a path the test misses.
Implementation (slice 2): one flood fill per call from the camp's admissible destinations over `N` for each distinct
native class among the candidates (base land; with `IsEmbarkDeepWater`; `canMoveAllTerrain` units are not skipped),
then a membership test per unit before `TurnsToReachTarget`. Open for slice 2: (a) `PathDestValid` (1117-1200) and the
approximate-destination code read once more for any destination the flood fill must include; (b) whether a unit's
native set can change during the call (a promotion taken mid-call: not on this path `[I]`); (c) the cost of the flood
fill against the searches it saves, measured.

**The finder history** (`speedup-p0-measurements.md` P0.2; `speedup-danger.md` 8.2b-8.2d): a skipped search no longer
advances the finder's generation counter and leaves no node data behind. On batch 4 alone that could change later
searches through the not-refreshed neighbour reads; pin batch 5 carries 4813's fix of both read sites and A10's 32-bit
generation id, so on `perf/speed-p3` (cut from `x64-batch5`) a skipped search changes no later answer, as the danger
package's skipped searches showed on the same base (8.6, 8.10). Identity on M, H and E is the check.

**Verify mode** (`VP64_SPEED_P3_VERIFY`): every skip also runs the original `TurnsToReachTarget` and checks
`VP64_SPEED_CHECK_K("P3_A5", ..., fast = unreachable, orig = (turns <= iRange), unit id, camp plot, flags, 0)`, and plays
the original. **Negative control** (`VP64_SPEED_P3_SPOIL=<n>`): the n-th unit the test finds reachable is skipped as if
unreachable (counted as a spoil); a verify run must count a difference when that unit's search finds a path
(`_SPOIL_EVERY` for the identity job's negative test). Coverage: the counters `P3_A5` (searches skipped) and `P3_A5T`
(tests made), and the M, H and early fixed-seed windows.

## 5. Counters and files

`VP64SpeedTotals.csv` (the kit's logger, `VP64SpeedVerify.h` taken from `perf/speed-danger-b5`): one row per counter
package above, `checks` = the count (fast = orig, so `differences` stays 0). `VP64P3Mb2.csv`: MB2's rows.
**Since fork `2e3c73c26` (round 4, 2026-11-19, 6.7) both exist only in a statistics build**: `VP64_SPEED_P3_STATS`, or
any of `_VERIFY`, `_SPOIL`, `_SPOIL_EVERY`, `_DIAG`, `_TA14_FORCE_DIRTY` (`VP64_SPEED_P3_STATS_ON` in
`VP64SpeedVerify.h`); the product form `VP64_SPEED_P3` alone writes no file and takes no lock. `VP64SpeedInfo.txt` names
`VP64_SPEED_P3`, `VP64_SPEED_P3_STATS` and `VP64_SPEED_P3_MB2_CAP_MB` with its origin ("source default" or "-D"): the
cap's default is now defined in `VP64SpeedVerify.h`, before the logger, so every translation unit sees one value.
(Replaced 2026-11-19: "the cap only when it is passed on the command line, not the source default", which held up to
`a6fc1778b`.)

## 6. Results

### 6.1 Slice 1: TA14 and MB2 on batch 5 (impl-4809, 2026-11-19, workstation shared: no timing claims)

Fork `perf/speed-p3` **`093213292`** = `x64-batch5` `1fd51353e` + one commit (TA14 and MB2 behind `VP64_SPEED_P3`;
files `CvPlayerAI.cpp`, `CvTacticalAI.cpp`, `CvTacticalAI.h`, `CvGame.h`, new `VP64SpeedVerify.h`); every block followed
by `#line N`, `linecheck.py` against `x64-batch5` OK (4 files). Scripts `perf/evidence/0504/4809/` (`recut_b5.sh`,
`run_s1.sh`); scratch `C:/c5-64bit-work/4809/`. The reference is 4807's batch-5 stand-in (`speedup-danger.md` 8.10):
`x64-batch5`'s release DLL **`72693B8E...1556`**, built by `defineoff.py` with the batch-4 pin's version string and epoch,
in the batch-4 pin layout's bin (server `fc73e60e`), copied to `C:/c5-64bit-work/4809/layout-ref`.

**Identity M and H** `[L, 2026-11-19 01:30-01:37 EST, headless]`: the fast on build (`kitbuild.py --define
VP64_SPEED_P3`) **`CDF8BF8F...FE5A`** against `72693B8E` (`idjob.py --windows M,H`): **M 150..160 IDENTICAL (15 tables,
32,815 rows), H 300..305 IDENTICAL (14 tables, 125,209 rows); `idjob: IDENTICAL`** (`id-on.txt`).

**Define off** `[C, 01:37-01:46 EST]`: `defineoff.py` on `perf/speed-p3` with no define (stubs of 4807's b5 slice,
`--deps`; the batch-4 pin's version string and epoch) built **`A61B65CC...E912`**; `dllcmp.py --allow-build-id` against
`72693B8E`: **IDENTICAL-EXCEPT-BUILD-ID** (only the PE time stamp, the two debug-directory time stamps and the PDB GUID
differ; `defineoff.txt`). (`defineoff.py`'s own verdict compares with the batch-4 pin of `deps/dll.lock` and is
DIFFERENT, by design: batch 5 carries the finder fix and the export.)

**Narrowing gate** `[C, 01:46-01:52 EST]` (`narrow.sh`, `-DVP64_SPEED_P3`): **794 sites, baseline 798, 0 new**, 4 gone
(`narrow.txt`).

**Coverage and memory** (`cov_s1.sh`: M and E again, each window its own `VP64_SPEED_DIR`, identity again IDENTICAL:
M 32,815 rows, **E 0..50 IDENTICAL (15 tables, 20,619 rows)**; files `id-M/`, `id-E/`, `id-H/`; the kit's totals stop one
turn before the window's end, as they always do):

| window | TA14 scans skipped | guard replays / refreshes | MB2 hard resets kept / wiped at the cap | largest kept buffers at a hard reset |
|---|---|---|---|---|
| M 150-160 | 203 | 0 / 0 | 227 / 0 | 23.2 MiB |
| H 300-305 | 143 | 0 / 0 | 161 / 10 | 38.8 MiB (cap 32 MiB binds) |
| E 0-50 | 365 | 0 / 0 | 374 / 0 | 12.3 MiB |

- TA14: in all 711 skipped scans the closest-city map was clean at the player's first tactical plot, so the guard's
  replay path never ran (not even in E's first 50 turns, with every city founded then): other readers rebuild the map
  between a city change and the tactical turn `[I]`. The guard is exact by construction (section 1) but **its replay path
  is not yet exercised by any window**: open (6.2).
- MB2: one hard reset per simulating player, 27-30 a turn on H; the tactical pool's buffers kept between two players
  reach 23 MiB on M and pass the cap on H (largest per turn 32.7-38.8 MiB, turns 300-305), where 10 of 171 hard resets
  fell back to the pin's wipe. So MB2 holds up to 32 MiB that the pin frees at each player change. The memory after
  10 turns (D151) is slice 3's.

**Verdict of slice 1**: TA14 and MB2 on pin batch 5 (`perf/speed-p3` `093213292`): define off IDENTICAL-EXCEPT-BUILD-ID
against the batch-5 release, define on IDENTICAL on M, H and E, narrowing gate 0 new.

### 6.2 Not verified after slice 1

- (Done in slice 2, 6.3: exercised with the map forced dirty, 368 of 368 equal.) TA14's guard replay path (no window had a dirty map at a scan): a verify mode for TA14 (run the original scan too and
  compare the map's dirty flag and contents after both) or a seeded window with a city founded right before a major's
  tactical turn; slice 2.
- (Done in slice 3, 6.5: release identity M/H/E IDENTICAL, D24 PASS; the rest there.) Identity with the **release** on
  build (slice 1 used the fast build; the kit's rule: the release build for timing arms and the one release
  confirmation); D24; the coverage windows; the memory after 10 turns; the oldpc dev pair (slice 3; the lead holds oldpc
  now).
- The reference is our own build of `x64-batch5`, not a lock-named batch-5 DLL: when `deps/dll.lock` names batch 5, run
  `defineoff.py` once against the lock.
- A5: not built (slice 2); section 4's open items (a)-(c).

### 6.3 Slice 2: A5 with its verify mode, the negative control and the TA14 guard exercise (impl-4809, 2026-11-19, workstation shared: no timing claims)

Fork `perf/speed-p3`: `e6a78f05f` (A5, `VP64_SPEED_P3_VERIFY`, `_SPOIL`/`_SPOIL_EVERY`, `_TA14_FORCE_DIRTY`),
`6d6ea4634` (`VP64_SPEED_P3_DIAG`), **`dd428c34d`** (the lift exclusion below). `linecheck.py` OK against `x64-batch5`
(defines `VP64_SPEED_P3`, `VP64_SPEED_P3_VERIFY`). Scripts `perf/evidence/0504/4809/run_s2.sh`, `diag_s2.sh`,
`run_s2b.sh`; evidence `perf/evidence/0504/4809/s2/`. Reference as in 6.1 (`72693B8E`, server `fc73e60e`). Fast builds.

**Round 1 (`e6a78f05f`)** `[L, 2026-11-19 02:59-03:53 EST, headless]`:
- A5 verify (`9C1C9369`): **M PASS** (1,658 tests, 1,209 skips, 0 differences, game IDENTICAL); **H FAIL**: 897 tests,
  850 skips, **3 differences** (turn 303, camp plot 3863: three units whose search found a path of 1-2 turns); **E**:
  1,210 tests, 0 skips (early units are all joined to their camps) so verifyrun counts no check there.
- On-mode identity (`E657D2ED`): M and E IDENTICAL, **H DIFFERENT** at turn 303 (`MilitarySummary.Danger`, row 113:
  127 against 129): the same wrong skips change the game.
- Diagnosis (`VP64_SPEED_P3_DIAG`, `FF2BEB87`, `s2/diag-H/VP64P3A5Diag.csv`): each of the three paths has **a jump**
  from a plot outside the fill to a plot next to the camp 9 plots away ((73,28) -> (82,25); (79,19), a city, ->
  (82,25)): the unit finder's **extra children**, `UnitPathGetExtraChildren` (`CvAStar.cpp`:2253-2381): airlift from a
  city of the unit's team that `CanAirlift()` (an airport) or a plot next to it, airlift from an improvement that
  `IsAllowsAirliftFrom`, sealift from a `CanSealift()` city, and whatever the airlift/sealift GameEvents allow. Section
  4's proof missed them: it read `PathValid`, which judges a step, not how the finder makes the steps. **Correction
  2026-11-19 (replaces section 4's claim that every step goes to an adjacent plot)**: A5 now skips nothing in a call
  where a lift is possible for the team (any city of the team with `CanAirlift()` or `CanSealift()`, any improvement type
  with `IsAllowsAirliftFrom`, or one of `EVENTS_AIRLIFT`, `EVENTS_SEALIFT`, `EVENTS_CITY_AIRLIFT`, `EVENTS_CITY_SEALIFT`
  on; in VP only `BUILDING_AIRPORT` has `Airlift`, no building has `Sealift`, no improvement allows airlift, and the four
  options are 0 `[C, 0438's database]`). Change of port (admirals) is for sea units only.
- **TA14 guard exercise** (`VP64_SPEED_P3_TA14_FORCE_DIRTY=1`: at every major civ's first scan of a turn the map is
  made dirty first; verify build `0702CE1F`): the guard's decision (replay reaches a `GetClosestCityByPlots`) against the
  scan's (the map rebuilt by the scan): **equal 368 of 368 times** (M 80, H 55, E 233 forced; 711 checks in all), verify
  games IDENTICAL on M, H and E. With the same force in the on build (`1D228F15`) M and E IDENTICAL (H differs only by
  A5's round-1 skips). So the replay path is exercised and agrees with the scan.
- **Negative control** (`VP64_SPEED_P3_SPOIL_EVERY=5`, verify `BD677B72`, M): **caught**: 89 spoilt skips, 36
  differences (first at turn 150), game IDENTICAL.

**Round 2 (`dd428c34d`, the lift exclusion)**: `[L, 2026-11-19 04:08-04:29 EST, headless]`: verify `453F49F6`, on `2707FF6F`.
- A5 verify (`verifyrun --package P3_A5 --expect clean`, `s2/vr3.txt`): **M PASS** (1,658 tests, **1,209 skips**, 0
  differences, game IDENTICAL); **H PASS** (374 tests, **354 skips**, 0 differences, game IDENTICAL; round 1 had 897 /
  850: the teams with an airport no longer skip); **E**: 1,210 tests, 0 skips, game IDENTICAL (verifyrun reports E as
  FAIL only because no check ran there: early units are all joined to their camps by land).
- **On-mode identity** (`idjob.py --windows M,H,E`, `s2/id3.txt`): **M 150..160 IDENTICAL (15 tables, 32,815 rows), H
  300..305 IDENTICAL (14, 125,209), E 0..50 IDENTICAL (15, 20,619)**.

**Verdict of slice 2**: with the lift exclusion, A5 is verify-clean on M and H (1,563 skipped searches, every one
checked against the search it replaces, 0 that found a path) and plays the reference game on M, H and E; the negative
control is caught; TA14's guard replay agrees with the scan in all 368 forced cases. Not shown: a skip in E (none
happen there), A5's speed after the lift exclusion (on H it now skips 354 searches in 5 turns instead of 850; the value
is slice 3's oldpc pair to say), the cost of the lift check (one pass over the team's cities and the improvement types
per call), the D24 save test, the memory after 10 turns, a release build.

### 6.4 Not verified after slice 2

- (Done in slice 3, 6.5, as far as one define allows: the package -0.39 % on H, A5's share inferred at about 0.03-0.05 s;
  the fill and lift-check cost described, not timed alone.) A5's value on H after the lift exclusion; the cost of the
  flood fill and the lift check (slice 3, oldpc).
- A narrower lift rule (exclude only units within airlift reach of an airport city: the unit's own plot or a plot next to
  it must be a lift-from plot, and the target side an airlift-to plot) would keep more H skips; not built, the
  conservative rule is exact by construction.
- (Done in slice 3, 6.5, 2026-11-19: release identity M/H/E IDENTICAL, D24 PASS; the coverage windows, memory after 10
  turns and the oldpc pairs there, with what stayed open in 6.6.) Release-build identity, D24, the coverage windows
  (CONG, CORP), memory after 10 turns, the oldpc dev pair (slice 3).
- `defineoff.py` against `deps/dll.lock` once it names batch 5 (the define-off check of slice 1 used our batch-5
  reference; A5's code added since is define-on only and `linecheck.py` passes).

### 6.5 Slice 3: release builds, D24, MB2's cap, memory after 10 turns, the oldpc dev pairs (impl-4809, 2026-11-19)

Fork `perf/speed-p3` **`dd428c34d`** (unchanged since slice 2). Script `perf/evidence/0504/4809/run_s3.sh` (steps
`onrel onrel16 id id16 x86 d24`); evidence `perf/evidence/0504/4809/s3/`; scratch `C:/c5-64bit-work/4809/`. Reference as
in 6.1 (`72693B8E`, the batch-4 pin layout's bin, server `fc73e60e`, `layout-ref`). `deps/dll.lock` on main still named
batch 4 when this slice ran (main `a1936044f`, 04:45 EST).

**Builds** (`kitbuild.py --config release`): **on, cap 32** (`--define VP64_SPEED_P3` on `dd428c34d`, whose default cap was 32) and **on, cap
16** (`--define VP64_SPEED_P3 --define VP64_SPEED_P3_MB2_CAP_MB=16`). The cap is a compile-time define
(`CvTacticalAI.cpp`, MB2's block: `#ifndef VP64_SPEED_P3_MB2_CAP_MB` / `#define ... 32`); `VP64SpeedInfo.txt` names it when passed with `-D`.
No run-time setting, no new state (the statics list of section 3 is unchanged).

**A5's own cost after the lift exclusion** `[C, by reading dd428c34d]`: per `FindUnitsForHarassing` call, at the first
eligible unit only (later units of the call reuse both): (1) the lift check `VP64P3A5LiftPossible`: the four lift options,
one pass over the improvement types (`IsAllowsAirliftFrom`) and one over the team's cities (`CanAirlift`/`CanSealift`),
stopping at the first hit; (2) when no lift is possible, the flood fill `VP64P3A5Fill`: a `char` per map plot set to 0 (a
`std::vector<char>` of `numPlots()`, allocated per call) and a fill over the 6 hex neighbours of the revealed,
embark-free component that holds the camp, each plot tested with `isRevealed` and `needsEmbarkation`; then per unit
one byte read. Against it each skip saves one `TurnsToReachTarget`, a full unit path search. The counts (the release
runs below, the same as slice 2's verify runs `s2/vr3-*`): M 150-159 **1,658 tests, 1,209 skips** (73 %); H 300-304 **374
tests, 354 skips** (95 % of the tests; slice 2's round 1, before the lift exclusion, tested 897 and skipped 850: the
calls of teams with an airport city now return at the lift check and are not counted as tests); E 0-49 1,210 tests, 0
skips (every early unit is joined to its camp, so E pays the fills and the lift checks for nothing). No in-build timer
was added: the workstation is shared and the oldpc pairs below measure the package as a whole.

**DLLs** `[C]` (`s3/dlls-SHA256.txt`): on, cap 32 **`C3AB351D...3E26`** (built 04:48-04:55 EST); on, cap 16 **`9337DA36...A9C3`**
(04:55-05:02), both of `dd428c34d`; **the branch head `a6fc1778b` (default cap 16), on: `2FF12C7F...FA25D`** (06:25-06:31,
`run_s3.sh head`).

**Identity, release builds** `[L, 2026-11-19 05:03-05:09 and 06:31-06:35 EST, headless, workstation]` (`idjob.py`, one
window per job with its own `VP64_SPEED_DIR`; `s3/id32-*/`, `s3/id16-*/`, `s3/idh-*/`): against `72693B8E` in
`layout-ref`:

| build | M 150..160 | H 300..305 | E 0..50 |
|---|---|---|---|
| on, cap 32 `C3AB351D` | IDENTICAL (15 tables, 32,815 rows) | IDENTICAL (14, 125,209) | IDENTICAL (15, 20,619) |
| on, cap 16 `9337DA36` | IDENTICAL (15, 32,815) | IDENTICAL (14, 125,209) | not run |
| **head `a6fc1778b`, on `2FF12C7F`** | IDENTICAL (15, 32,815) | IDENTICAL (14, 125,209) | IDENTICAL (15, 20,619) |

(The head's MB2 rows equal the cap-16 build's: M 221 kept / 6 wiped, H 143 / 28, E 374 / 0.)

The counters of the release runs equal slice 2's fast and verify runs (A5 above; TA14 203 / 143 / 365 scans skipped).

**MB2 at both caps** `[L, same runs; VP64P3Mb2.csv, one row per hard reset, kept bytes = the tactical pool's vector
capacities + map nodes at 48 B + spare lists, measured before the keep-or-wipe decision]`:

| window | cap | hard resets kept / wiped | kept bytes at a hard reset: median / p90 / max (MiB) |
|---|---|---|---|
| M 150-160 | 32 | 227 / 0 | 18.2 / 19.6 / 23.2 |
| M 150-160 | 16 | 221 / 6 | 13.9 / 15.8 / 18.2 |
| H 300-305 | 32 | 161 / 10 | 24.0 / 31.0 / 38.8 |
| H 300-305 | 16 | 143 / 28 | 8.2 / 23.8 / 34.4 |
| E 0-50 | 32 | 374 / 0 | 5.5 / 11.9 / 12.3 |

(At cap 16 the kept sizes are lower because each wipe starts the slots over; a size above the cap is the one measured
right before that wipe.)

**Coverage windows, release cap 32** `[L, 2026-11-19 05:27-05:32 EST, headless]` (`run_s3.sh cov`: `idjob.py --window`
on 4803's coverage saves `F:/c5-64bit-work/4803/coverage/SK4803-*`, read only; `s3/cov-*/`): all **IDENTICAL** against
`72693B8E`: CONGM 180..183 (World Congress; 14 tables, 16,612 rows), CONGH 305..308 (14, 82,168), SPECH 310..313
(special session; 14, 82,689), CORPM 240..245 (the first corporation; 14, 42,812). Counters (the kit's totals stop one
turn before the window's end): A5 skips / tests CONGM 495 / 624, CONGH 217 / 230, SPECH 195 / 215, CORPM 86 / 215; TA14
64 / 82 / 89 / 91 scans skipped; MB2 hard resets kept / wiped (all turns, `VP64P3Mb2.csv`) CONGM 85 / 0 (max 22.8 MiB),
CONGH 105 / 7 (max 39.6), SPECH 112 / 5 (max **41.4**, the largest seen), CORPM 109 / 0 (max 31.8, median 30.8: just
under the cap at every reset).

**D24** (`run_d10.py`, x86 `95FBCBA6` = 4807 b5's `i0-ship-batch5` release, copied; x64 cap-32 `C3AB351D` in
`layout-on` = `layout-ref` with the on DLL, `s3/layout-on-SHA256.txt`; order exception none, D18 on both)
`[L, 2026-11-19 05:09:33-05:25:22 EST, headless, wall 949 s]`: **PASS, all three parts on tiny, standard and huge**
(walls 255, 455, 940 s; `s3/d24-report.txt`, the user folder written as `%USERPROFILE%`).

**The oldpc D178 dev pairs** (`perf/evidence/0504/4809/pairs_s3.sh`; `perf/speedkit/pair.py`, quiet, 3 runs per arm,
every arm a copy of `layout-ref`'s bin with its DLL, server `fc73e60e`; staged on C:). `tbpair.py` takes two arms and
COMMON 23 compares arms only inside one interleaved set, so three jobs: **M** ref `72693B8E` vs on32 `C3AB351D`, M
151-165 (job `20261119-045554-bb81`); **H** ref vs on32, H 301-311 (`20261119-045645-47ad`); **Hc** on32 vs on16
`9337DA36`, H 301-311 (`20261119-050221-a80e`: the time and memory cost of the lower cap measured directly). Analysis:
`python -I perf/tbjobs/analyze.py <fetch dir> --ref <arm a>`; memory after 10 turns (D151) = `series.json`'s
`memAfter10MB` per run.

*Expected, written before the jobs ran* `[I]`: from the profile shares at the pin (ticket brief, 4802 and 0437): M: A5's
search is 6.7-8.9 % of an M turn and 73 % of the tests now skip, TA14 1.6 %, MB2 a part of allocation's 6-7 %: about
-5 to -10 % on M; H: A5 1.2 % (95 % skipped), TA14 0.1 %, MB2 a part of allocation's 4.6 %: about -1 to -3 % on H, so
small that 3 runs per arm may not resolve it (8.11's plain pair resolved 0.31 s on H with 6 runs). The cap pair: a few
more wipes per turn on H (28 instead of 10 in H 300-305's six turns, the identity runs above): expected below detection. Memory: MB2
keeps up to the cap between players while the pin frees and regrows; private bytes after 10 turns may move by less than
the kept size, since the pin's freed memory stays in the heap `[I]`. stats.db IDENTICAL in every pair.

**Results** `[L, oldpc, quiet; NVIDIA GeForce RTX 2080, driver 32.0.16.1088 of 2026-07-21 (D175)]` (evidence
`perf/evidence/0504/4809/s3/oldpc-<id>/`: `analysis.txt`, `series.json`, `turnbench.json`, `cmp-*`, `gpu.txt`,
`quiet-summary.json`, `mem-after-10.txt`; job manifests and process lists left out):

| pair (3 runs per arm) | arm a | arm b | difference b - a | private MB after 10 turns (D151), mean a / b | stats.db round 1 |
|---|---|---|---|---|---|
| **M** 151-165, job `bb81`, 2026-11-19 05:42:05-05:43:54 EST | ref 1.001 s/turn (sd 0.008) | on32 0.915 (sd 0.013) | **-0.086 s (-8.58 %)**, 95 % CI [-0.114, -0.058], significant (detectable about 0.025) | 213.6 / 230.8 (**+17.2 MB**); growth 0.74 / 1.42 MB per turn | IDENTICAL (15 tables, 52,706 rows) |

| H 301-311, job `47ad`, 05:43:55-06:03:25 EST | ref 15.521 (sd 0.667) | on32 15.723 (sd 0.697) | +0.202 s (+1.30 %), 95 % CI [-1.570, +1.973], **not significant** (detectable about 1.56): **void, a Defender scan** (below) | 322.7 / 329.9 (**+7.2 MB**) | IDENTICAL (14 tables, 270,241 rows) |

| **H2** H 301-311, **6 runs per arm**, job `2200`, 06:22:09-06:59:06 EST, quiet proof **undisturbed** (foreign cores 0.055) | ref 14.840 (sd 0.023) | on32 14.781 (sd 0.018) | **-0.059 s (-0.39 %)**, 95 % CI [-0.086, -0.032], significant (detectable about 0.033) | 325.1 / 332.7 (**+7.6 MB**); peak working set 259 / 266 MB | IDENTICAL (14 tables, 270,241 rows) |
| **Hc** H 301-311, job `a80e`, 06:03:28-06:22:07 EST, quiet proof **undisturbed** (foreign cores 0.09) | on32 14.961 (sd 0.057) | on16 14.919 (sd 0.037) | -0.043 s (-0.28 %), 95 % CI [-0.167, +0.082], **not significant** (detectable about 0.11) | 331.9 / 326.3 (**-5.6 MB**); peak working set 267 / 260 MB | IDENTICAL (14 tables, 270,241 rows) |

H's job `47ad` ran under a Windows Defender scan (`MsMpEng.exe` 142 CPU-s, foreign cores 0.205 against 0.10; it was busy
in every sample of rounds 1 and 2 and in 2 of 17 of round 3; `s3/oldpc-47ad/runs-defender.txt`): sd 0.67 s per arm
against 0.07 in 8.11's quiet H. Its only nearly clean round, 3, gave ref 14.934 against on32 14.918 s (-0.016 s; one
pair of runs, no claim). So H was repeated with 6 runs per arm (job `20261119-060415-2200`, row **H2**, the H result):
quiet and undisturbed, every round in favour of on32 (`s3/oldpc-2200/runs-defender.txt`); it meets the grade-A
conditions of `docs/briefs/PERFORMANCE.md` rule 7 as 8.11 of `speedup-danger.md` does (one interleaved series, one
machine, the same scenario, save and logging, six runs per arm, quiet, the same game).

M's quiet proof: alone, but **undisturbed = false** (foreign cores 0.118 against the limit 0.10; Defender's
`MsMpEng.exe` 7.6 CPU-s of 13.0 foreign): a dev pair, not grade A; the difference is 3.4 times its detectable size.

**MB2's cap: decided 16 MiB** (fork `a6fc1778b`, the default of `VP64_SPEED_P3_MB2_CAP_MB` changed from 32; the line
count is unchanged, `linecheck.py` OK against `x64-batch5` with `VP64_SPEED_P3,VP64_SPEED_P3_VERIFY,VP64_SPEED_P3_DIAG`).
The numbers: on H the lower cap wipes 28 of 171 hard resets instead of 10 and costs **no measurable time** (-0.043 s,
not significant, detectable 0.11 s, about 0.7 % of an H turn) while it holds **5.6 MB less** private memory after 10
turns (326.3 against 331.9 MB); on M it keeps 221 of 227 (6 wipes, none at 32), so M's share of MB2's gain is nearly
all kept (M's +17.2 MB at cap 32 comes from kept buffers of 18 MiB median, which at 16 drop to 14 MiB median `[I]` for
the memory; M at cap 16 was not timed); E never reaches 16 (max 12.3 MiB). Lower caps were not tried. The cap is a
compile-time define, so a build that wants another value passes `-DVP64_SPEED_P3_MB2_CAP_MB=N`; no run-time option, no
new state (section 3's statics list unchanged).

**What the M difference is made of** `[I]` (one define, so no arm per item; the pin's profile shares of 4802,
`speedup-p0-shares.md` A5, taken on the shared workstation, scaled to oldpc's 1.001 s M turn): the camp search's
`TurnsToReachTarget` is 5.9-7.5 % of an M turn (0.059-0.075 s) and A5 skips 73 % of the tested searches: **A5 about
0.04-0.055 s** less the fills and lift checks; TA14's scan 1.6 % (the ticket's brief; **about 0.016 s**, all of it
skipped); the rest, **about 0.015-0.03 s, MB2** (allocation was 6-7 % of an M turn at the pin, 0437, the ticket's
brief). The sum fits the measured 0.086 s.

**What the H difference is made of** `[I]` (the same method, H turn 14.84 s): the package gives **-0.059 s (-0.39 %)**,
below the -1 to -3 % expected. The camp search's `TurnsToReachTarget` is 0.99 % of an H turn at the pin (about 0.147 s
here). Before the lift exclusion A5 skipped 850 of 897 tested searches on H; now 354 (the calls of teams with an
airport city skip nothing): if the searches cost alike, **A5 is worth at most about 0.147 x 354 / 897 = 0.058 s**, less
the fills and lift checks, so roughly 0.03-0.05 s; **TA14 about 0.015 s** (0.1 %); **MB2 0 to about 0.015 s**: the cap
pair shows that wiping 18 more of 171 hard resets costs nothing measurable, so the allocations MB2 saves on H are a
small part of allocation's 4.6 %. A narrower lift rule (6.4) is where H's lost A5 value would come back.
**A5's value on H after the lift exclusion** (6.4's first item): about 40 % of what it was before the exclusion, by the
skip counts (354 of 850); measured only as part of the package's -0.059 s.

**Verdict of slice 3**: the package on pin batch 5 (fork `perf/speed-p3` `a6fc1778b`, release `2FF12C7F`) plays the
reference game (identity M, H, E and the four coverage windows with `C3AB351D` = the same code at cap 32; M, H, E with
`2FF12C7F`), passes D24's three parts, and on oldpc saves **8.6 % of an M turn** (dev pair, slightly disturbed) and
**0.39 % of an H turn** (6 runs per arm, quiet, significant), both at cap 32; memory after 10 turns **+17.2 MB on M and
+7.6 MB on H** at cap 32, about 5.6 MB less on H at the chosen cap 16 (M at 16: not measured).

### 6.6 Not verified after slice 3

- (Done in round 4, 6.7: fork `2e3c73c26`.) **Statistics always on in the on build**: `VP64_SPEED_P3` alone keeps the kit's counters (`VP64_SPEED_CHECK`, an SRW
  lock per check, `VP64SpeedTotals.csv` rewritten each turn) and MB2's row file (`VP64P3Mb2.csv`, one row per hard
  reset, `fflush` each), written to `%VP64_SPEED_DIR%` or else the process's working folder. Fine for this ticket (the
  define stays off in every product build), but a product form needs them behind a statistics define, as 4807 has
  `VP64_SPEED_DANGER_STATS`; their small cost is inside every on arm measured here (the timing is conservative by it).
- MB2's kept size and its cap count the tactical pool only (`CvTactPosStorage`: slot vectors, `plotScores` nodes at 48 B
  `[I]`, the spare lists); the support pool (60 slots) and the assignment pool (2000 slots, two small vectors each) keep
  their buffers too and are not counted `[I: small]`.
- A5 verify (skip against search) ran on M, H and E only (slice 2); the coverage windows above are identity runs.
- The time split between A5, TA14 and MB2: one define, so the pairs measure the package; the split above is from the
  pin's profile shares and the counters `[I]`.
- (Done in round 4, 6.7: the product form at cap 16 timed on M and H, 6 per arm.) Timing of the chosen cap: every timed on arm is the cap-32 build `C3AB351D`; the head `2FF12C7F` (cap 16) is the
  cap-16 build's code with another version string and was not timed; M at cap 16 was neither timed nor
  memory-measured. The M pair is a dev pair (3 per arm, slightly disturbed); H2 is the grade-A-conditions result.
- (Done in round 4, 6.7.) `VP64SpeedInfo.txt` does not name the cap when it comes from the source default (it prints the define only when
  defined before the logger's header); `VP64P3Mb2.csv`'s rows show which cap ran.
- The narrower lift rule (6.4, a note only); windowed identity; the client soak (not in this slice).
- (Done in round 4, 6.7.) Define off of the head `a6fc1778b`: not rebuilt; its only change since slice 1's define-off check is inside a
  `VP64_SPEED_P3` block (`linecheck.py` OK).
- (Done in round 4, 6.7, against main's lock.) The reference is our own build of `x64-batch5`; `defineoff.py` against `deps/dll.lock` when it names batch 5 or the
  0.1b pin (still batch 4 at the end of this slice).

### 6.7 Round 4: the product form, its define off against the batch-5 lock, identity and the oldpc pairs (impl-4809, 2026-11-19)

Fork `perf/speed-p3` **`2e3c73c26`** (pushed, fast-forward from `a6fc1778b`): one commit, three files
(`VP64SpeedVerify.h`, `CvPlayerAI.cpp`, `CvTacticalAI.cpp`): the counters (`VP64_P3_COUNT`, a no-op without statistics)
and MB2's row file behind `VP64_SPEED_P3_STATS_ON` (section 5), MB2's default cap 16 moved into `VP64SpeedVerify.h` and
named in `VP64SpeedInfo.txt` with its origin. Every block keeps its `#line N`; `linecheck.py` against `x64-batch5`
(defines `VP64_SPEED_P3,VP64_SPEED_P3_VERIFY,VP64_SPEED_P3_DIAG`): OK, 4 files. Scripts `perf/evidence/0504/4809/run_r4.sh`
(steps `on defoff id stats pairs`), `r4/defineoff_lock.py`, `r4/fetch_r4.sh`; evidence `perf/evidence/0504/4809/r4/`. The
lock's layout `%USERPROFILE%/c5-i0/dlls/x64-1fd51353-p1e09fc7` was not laid out at 19:10 or 19:20 EST, so identity and the
pairs use slice 3's reference **`72693B8E`** (`C:/c5-64bit-work/4809/layout-ref`, server `fc73e60e`), as the brief allows.

**The product-form release build** `[C]`: `kitbuild.py --config release --define VP64_SPEED_P3` (no statistics, cap 16 by
default): **`54E9AB9E...BE29`**, 23,685,632 B, 2026-11-19 19:19-19:26 EST (`r4/dlls-SHA256.txt`).

**Define off against main's `deps/dll.lock`** `[C, 2026-11-19 19:27:54-19:42:23 EST]` (x64-batch5 `1fd51353e`, release
DLL **`78355A75...EAFE6`**, ticket 0551's, version `Release-5.4.8-65-g1fd51353e Copy`, epoch 1795059558 = the commit time
of the lock's tools `1e09fc7c2`): this branch still carries the batch-4 lock and may not touch `deps/*.lock`, so
`r4/defineoff_lock.py` runs `defineoff.py` unchanged with three inputs replaced for the run: main's lock (saved by `git
show`), the pin DLL read from the folder 0551 lays out from (`F:/c5-64bit-work/0551/layouts/x64-1fd51353-p1e09fc7/bin`,
read only, SHA-256 checked against the lock) and the lock's describe passed literally to `build_as_pin.py`. With
`--deps` (4809's stubs) and `--control`: the define-off DLL of `2e3c73c26` **`4B1760E9...384C`** (23,681,536 B) against
`78355A75`: **IDENTICAL-EXCEPT-BUILD-ID** (the PE time stamp, the two debug-directory time stamps and the PDB GUID differ;
the checksum is equal); **control**: the pin's own copy built by the same steps is **`78355A75`, byte-identical to the
lock's release DLL**, so this repository's tools at the epic head (`tools/x64-dll/build.py` changed since `1e09fc7c2`)
still build the pin's bytes (`r4/defineoff.txt`, `defineoff.json`).

**Identity, the product form** `[L, 2026-11-19 19:42:23-19:47:15 EST, headless, workstation]` (`idjob.py`, one window
per job with its own `VP64_SPEED_DIR`, `r4/r4on-*/idjob.txt`): `54E9AB9E` against `72693B8E` in `layout-ref`: **M 150..160
IDENTICAL (15 tables, 32,815 rows), H 300..305 IDENTICAL (14, 125,209), E 0..50 IDENTICAL (15, 20,619)**, the row counts
of slice 3. **The product form wrote no file**: each window's `VP64_SPEED_DIR` stayed empty (no `VP64SpeedInfo.txt`,
`VP64SpeedTotals.csv` or `VP64P3Mb2.csv`), as the statistics gate intends.

**The statistics build's counters** `[L, 2026-11-19 19:47:15-19:51:03 EST, headless]` (fast, `VP64_SPEED_P3` +
`VP64_SPEED_P3_STATS`, **`E25783B2...F0C5`**; `idjob.py` M, `r4/r4stats-M/`): M 150..160 IDENTICAL (15 tables, 32,815
rows); `VP64SpeedTotals.csv` **equal to slice 3's head run** (`s3/idh-M`, release `2FF12C7F`, the same code at cap 16):
P3_A5T 1,658, P3_A5 1,209, P3_TA14 203, P3_MB2 201, P3_MB2W 4 (to turn 159); `VP64P3Mb2.csv` **byte-identical** to
slice 3's (227 rows: 221 kept, 6 wiped, max 18.2 MiB); `VP64SpeedInfo.txt` now reads `define VP64_SPEED_P3`, `define
VP64_SPEED_P3_MB2_CAP_MB=16 (source default)`, `define VP64_SPEED_P3_STATS`.

**The oldpc pairs** (`perf/speedkit/pair.py`, quiet, **6 runs per arm**, ref `72693B8E` vs on `54E9AB9E`, both in
`layout-ref`'s bin, server `fc73e60e`; `run_r4.sh pairs`) `[L, oldpc; NVIDIA GeForce RTX 2080, driver 32.0.16.1088 of
2026-07-21 (D175)]`:

| pair (6 runs per arm) | ref `72693B8E` | on `54E9AB9E` (product form, cap 16) | difference on - ref | private MB after 10 turns (D151), mean ref / on | stats.db round 1 |
|---|---|---|---|---|---|
| **M** 151-165, job `20261119-192656-1940`, 19:27:29-19:31:03 EST, quiet, **undisturbed** (foreign cores 0.062) | 0.998 s/turn (sd 0.025) | 0.898 (sd 0.004) | **-0.100 s (-10.04 %)**, 95 % CI [-0.127, -0.073], significant (detectable about 0.029) | 214.8 / 219.1 (**+4.3 MB**); growth 0.70 / 0.50 MB per turn | IDENTICAL (15 tables, 52,706 rows) |
| **H** 301-311, job `20261119-192727-443e`, 19:31:05-20:08:02 EST, quiet, **undisturbed** (foreign cores 0.057) | 14.836 s/turn (sd 0.054) | 14.809 (sd 0.029) | -0.027 s (-0.18 %), 95 % CI [-0.086, +0.033], **not significant** (detectable about 0.070) | 324.6 / 325.2 (**+0.6 MB**); growth 2.24 / 1.79 MB per turn; peak working set 259 / 260 MB | IDENTICAL (14 tables, 270,241 rows) |

Both series meet the grade-A conditions of `docs/briefs/PERFORMANCE.md` rule 7 (one interleaved series each, one
machine, the same scenario, save and logging, six runs per arm, quiet, undisturbed, the same game); evidence
`r4/oldpc-1940/`, `r4/oldpc-443e/` (`analysis.txt`, `series.json`, `turnbench.json`, `cmp-ref-on/`, `gpu.txt`,
`quiet-summary.json`, `mem-after-10.txt`; job manifests and process lists left out). Neither was disturbed by Defender
(`MsMpEng.exe` 7.2 and 54.6 CPU-s, foreign cores under the 0.10 limit), so neither was resubmitted.

**Why the numbers moved** `[L for the numbers, I for the split]`:
- **M -0.100 s (-10.0 %)**, both arms the same game: A5's skipped camp searches (1,209 of 1,658 tests in M 150-159), TA14's
  skipped scans (203) and MB2's kept buffers (221 of 227 hard resets kept at cap 16), split as in 6.5 (A5 about 0.04-0.055
  s, TA14 about 0.016 s, MB2 the rest). It is larger than slice 3's -0.086 s (cap 32 with the statistics on, 3 per arm,
  slightly disturbed): the product form takes no SRW lock and writes no row per check (6.6's first item said the
  statistics' cost was inside every measured on arm), within the two series' intervals `[I]`.
- **H -0.027 s, not resolved**: slice 3's H2 measured -0.059 s [-0.086, -0.032] for the package at cap 32 with
  statistics, and its cap pair Hc put cap 16 at -0.043 s against cap 32 (not significant, detectable 0.11). So H's effect
  is about 0.03-0.06 s (0.2-0.4 %), at or below this series' detectable 0.070 s; PERFORMANCE.md's pitfalls ask 12 runs per
  arm for a claim under 1 % on a logged-on testbed. **H at cap 16 stands as "no measurable change, 95 % CI
  [-0.086, +0.033] s, at most +0.22 %"** (lead-0504, thread 0504 #7210 answer, 2026-11-19: no 12-per-arm H series); **M
  -10.04 % is the package's grade-A result**.
- **Memory after 10 turns** (D151): **+4.3 MB on M, +0.6 MB on H** with the product form at cap 16, against slice 3's
  +17.2 MB on M and +7.6 MB on H at cap 32 with statistics. The cap explains M's drop (kept buffers of 13.9 MiB median at 16
  against 18.2 at 32, 6.5) only in part `[I]`: the statistics' own memory is small (one `VP64P3Mb2.csv` handle, the
  logger's few rows), so most of the rest is the heap's behaviour after the pin frees and regrows the pools, which these
  numbers do not separate. On H the cap wipes 28 of 171 hard resets (6.5), so little more is kept than the pin keeps.

**Verdict of round 4, item A**: the product form `2e3c73c26` (`VP64_SPEED_P3` alone: no file, no lock, MB2 cap 16) is
define-off IDENTICAL-EXCEPT-BUILD-ID against main's batch-5 lock `78355A75` (control byte-identical), plays the reference
game on M, H and E, its statistics build counts what slice 3 counted, and on oldpc saves **10.0 % of an M turn** (grade-A
conditions) with no measurable change on H (-0.18 %, not significant) and **+4.3 / +0.6 MB** after 10 turns on M / H.

**Not verified in round 4**: H's gain at a resolving series size (12 per arm; not to be run, the lead's word); M at cap 32 in the product form (not
needed: 16 is the default); D24 and the coverage windows on `2e3c73c26` itself (the change since `a6fc1778b` is inside
statistics gates only, `linecheck.py` OK, identity M/H/E IDENTICAL; slice 3's D24 ran `C3AB351D`, the same game code);
windowed identity and the client soak with the P3 package; the narrower lift rule (6.4); the reference for identity and
timing is our own build of x64-batch5 (`72693B8E`, batch-4 version string), not the lock's `78355A75` (whose layout was not
laid out); the define-off control shows the two are the same code.

