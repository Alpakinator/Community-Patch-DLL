# Idea 0594, stage 1: the x64 DLL headless on maps far beyond stock size (stage M0, the part that needs no EXE)

Written 2026-11-26 by impl-0594 (account A, from the queue) for the MC. Ticket `tracker/issues/0594-idea-massive-maps.md`
(D208, D275). Base: the Architect's note `architect-2026-11-19-massive-maps-and-civs.md` (section 5, stage M0; its walls
table 2.1). Marks: `[C]` read in the named source by this ticket, `[L]` a run of this ticket's, `[I]` inferred.
Nothing of the product was changed. Workstation runs, other teams' jobs beside them: every time here is an
indication, not a result (COMMON 23, D106).

## 0. Short answers

| Question (M0, note section 5) | Answer | Where |
|---|---|---|
| Does the x64 DLL run on 20,160 / 32,000 / 64,800 / 500,000 plots? | **20,160 and 31,866: yes**, 50 turns clean. **65,044: no**, a crash in turn 39 (the tactical AI's `short` plot index). **502,095: no** on the pin's DLL: past the same index wall, and turn 2 alone took over 14.5 minutes (VP's first barbarian camp spawn is cubic in plots) | 2, 3, 4 |
| ... with the two walls removed (a scratch DLL, two small changes) | **yes for both**: 65,044 and 502,095 plots played 50 turns clean; the 20,160-plot game is unchanged by the two changes | 5 |
| Memory | linear, about 176 MB + 7.9 KB per plot early (62 players): 4.1 GB at 502,095 plots | 2 |
| Seconds per turn | early turns (median of 1-50) 0.84 s (20,160), 1.57 s (31,866), 3.66 s (65,044), 36.1 s (502,095, scratch DLL): about plots^1.1; a late turn at 500,000 plots is 20-80 minutes by proportion [I] | 2, 5 |
| Save size | body about 1.25 KB per plot (637 MB at 502,095 plots at turn 0; 8.9 MB compressed) | 2 |
| Can 500,000 plots be played at all by the DLL? | **Yes, with two small DLL changes** (M2a): it loads in 10 minutes, holds about 4.1 GB, writes a 789 MB save body, and plays early turns at about 36 s. A late game at that size is not playable today by turn time [I] | 5, 7 |
| Next stages | M1 needs nothing from the DLL; M2 needs the two DLL changes (drafted as M2a); the EXE's load wall is read in M0b | 7, 8 |

## 1. What was run

- **Build** (COMMON 18): main's x64 pin, `deps/dll.lock` x64-vp55 a2627c8ac (VP 5.5), the shared read-only pin folders
  `C:/c5-64bit-work/keep/pins-5.5/src-x64-a2627c8ac` and `deps-x64-release` (x64 DLL sha256 2019c9d3...), laid out by
  `server/build_server.py --config release --no-proxy` from this branch (main at 941528a24) into
  `F:/c5-64bit-work/0594/layout/bin` (`evidence/0594/build_layout.sh`). The 22-civ build (`MAX_MAJOR_CIVS` 22).
- **Game data**: the release descriptor Release-5.5 (`C5_RELEASE=Release-5.5`): game folder
  `F:/c5-64bit-work/0764/take/testdata/game/Sid Meier's Civilization V`, database
  `C:/c5-64bit-work/keep/release-5.5-db/Civ5DebugDatabase.db` (run_server.py copies it per run).
- **Maps**: Tectonic v6 - VI of that game folder (sha256 346e9bf8..., the same bytes as VP 5.4.8's) copied by
  `tools/stressmap/tectonic_stress.py --factor F` (9073's tool: the size multiplier forced). Huge's base grid in the
  script is 150 x 94 and the ratio option 3 keeps it (`Tectonic v6 - VI.lua` lines 314-382 [C]), so the map is
  floor(150 F) x floor(94 F):

  | factor | map | plots | x Huge (10,240) | stands for |
  |---|---|---|---|---|
  | 1.20 | 180 x 112 | 20,160 | 1.97 | the largest map the stock EXE loads (9073) |
  | 1.51 | 226 x 141 | 31,866 | 3.11 | M1's target, about 32,000 |
  | 2.15 | 322 x 202 | 65,044 | 6.35 | M2's 360 x 180 = 64,800 |
  | 5.97 | 895 x 561 | 502,095 | 49.0 | M4's 1000 x 500 = 500,000 |

  The copies are third-party scripts and stay in `F:/c5-64bit-work/0594/maps/` (D83). sha256: 1.20 aff35324...,
  1.51 49f8afc2..., 2.15 97d27c42..., 5.97 48a7eac0....
- **Game** (one spec for every size, `evidence/0594/m0_run.py`): `WORLDSIZE_HUGE`, Standard speed, Prince, Ancient
  era, 21 AI majors + 41 city states (the 22-civ build's maximum with the top major slot free, as 9073's 42 + 20 on
  the 43 build), 9073's map options (`metrics-ledger.md` 8.4: shallow sea, abundant resources and islands, more land)
  with start placement 16=2, seeds map 9071 / sync 9072, max turns 3000, AI logging off (`--dll-log-flags 0`, a
  player's setting). Every player is an AI (run_server's direct mode has no human).
- **Measured** per run: `server/run_server.py`'s run folder (server.log, turn-times.tsv, server-report.txt), a save
  right after the new game (`--save-at-start M0-T000`) and at the end (`--save-out M0-END`), and the server's memory
  every 5 s by a sampler beside it (psutil: private commit, working set and their peaks; `<name>.mem.csv`).
- Players as played [L]: 21 majors and 41 city states alive at turn 0 and at turn 50 on every map that reached it;
  autoplay moves the observer to slot 21 (`ARM: ... active player 21`). Turn 50 on Standard is early: 76-78 cities and
  310-314 units in all on the two maps that reached it.
- Commands: section 9.

## 2. Results on the pin's DLL (2026-11-26 21:41-22:19 EST) [L]

| | 180 x 112 | 226 x 141 | 322 x 202 | 895 x 561 |
|---|---|---|---|---|
| plots | 20,160 | 31,866 | 65,044 | 502,095 |
| map generation (`init2`, the Lua script and the DLL's set-up) | 17.3 s | 33.0 s | 78.1 s | 607.3 s |
| turns played | 0-50, clean | 0-50, clean | **0-38, then a DLL crash in turn 39** | 0-1, turn 2 stopped by me after 14.5 min |
| s per turn, turns 0 / 1 / 2 / 3 | 1.81 / 0.67 / 0.61 / 0.62 | 2.88 / 1.41 / 1.57 / 0.99 | 5.33 / 2.67 / **9.07** / 3.32 | 66.4 / 29.7 / **> 872** |
| s per turn, mean of turns 26-50 (max) | 1.20 (3.72) | 1.91 (3.29) | 4.13 (6.57), turns 26-38 | - |
| s per turn, median of turns 1-50 | 0.84 | 1.57 | 3.66 (turns 1-38) | - |
| server private commit after turn 0 | 334 MB | 435 MB | 698 MB | 4,110 MB |
| server peak private commit (peak working set, the server's own report) | 347 MB (379) | 455 MB (515) | 803 MB (752) | 5,247 MB (4,771 by the sampler) |
| save at turn 0: uncompressed body / `.Civ5Save` | 35.7 MB / 0.64 MB | 50.4 / 0.88 | 91.9 / 1.51 | 637.3 / 8.88 |
| save at turn 50: body / file | 47.2 / 1.26 | 65.6 / 1.51 | - | - |
| time to write the turn-0 save | 0.66 s | 0.99 s | 2.37 s | 23.1 s |

Runs `F:/c5-64bit-work/0594/runs/S1.20`, `S1.51`, `S2.15`, `S5.97` (each with `<name>.mem.csv`, `<name>.summary.json`).
The 502,095-plot run was stopped with `quietlock.py stop` (my own registered job) because of section 4; its numbers
come from its server.log and the sampler (the server report is written only at the end).

**What scales how** [L for the points; I for the lines]:
- **Memory is linear in plots: about 176 MB + 7.9 KB per plot** (21 majors + 41 city states, early game). The slope is
  8.1 KB per plot between 20,160 and 65,044 plots and 7.8 KB between 65,044 and 502,095. The Architect's estimate
  (1.8 KB fixed + about 90 B per AI player per plot = 7.4 KB at 62 players, note 2.2) is confirmed in size. At
  1000 x 500 this is about 4.0 GB at the start, nothing for a 64-bit process; the late game adds the per-unit and
  per-city state (on the 16,920-plot Hormigas map with 42 majors the server went from 2.1 to 2.9 GB private by turn 178,
  `research/08-performance/giant-stress-game.md` 7, product path [L there]).
- **The save body is linear too: about 1.25 KB per plot** at turn 0 (slope 1,248 B between 65,044 and 502,095), 637 MB
  uncompressed at 502,095 plots (8.9 MB compressed: zlib packs the empty world well). The Architect's 700-800 B per
  plot (note 2.2) was low by about 60 %. On the product path today the save passes through the EXE (note 7); 637 MB would
  not fit there next to everything else.
- **Map generation is linear above about 65,000 plots** (log slope 1.29 from 20,160 to 65,044, 1.00 from 65,044 to
  502,095): 10 minutes for 502,095 plots with Tectonic, before the save.
- **Turn time grows faster than the plot count**: the median of turns 1-50 is 0.84 s, 1.57 s (1.9 times for 1.58 times
  the plots) and 3.66 s (4.4 times for 3.23 times). Turns 0 and 1 at 502,095 plots took 66 s and 30 s against 5.3 s and
  2.7 s at 65,044 (11-12 times for 7.7 times the plots).

## 3. Wall 1, measured: the tactical AI's `short` plot index crashes the DLL past 32,767 plots [L; cause C + I]

The 65,044-plot game crashed in turn 39 (2026-11-26 21:50:26 EST), 0xC000001D in the core thread, after the precondition
`getTerrainType() != NO_TERRAIN` failed in `CvPlot::seeFromLevel` (CvPlot.cpp:1555 at the pin; `crashlogs/CvAssert.log`).
The symbolised stack (`S2.15/server-symbolized.log`): `CvGlobals::getTerrainInfo` <- `CvPlot::seeFromLevel` <-
`CvPlot::canSeePlot` <- `CvTacticalPosition::getPlotsWithChangedVisibility` (CvTacticalAI.cpp:10310) <-
`CvTacticalPosition::addAssignment` <- `makeNextAssignments` <- `TacticalAIHelpers::FindBestAssignmentsForUnits` <-
`CvTacticalAI::ExecuteAttackWithUnits` (3390) <- `PlotBarbarianAttacks` (1150) <- `AssignBarbarianMoves` <-
`ProcessDominanceZones` (716) <- `CvPlayerAI::AI_unitUpdate` <- `CvGame::updateMoves`.

Cause [C, the pin's source; I that this assignment is the one]: `STacticalAssignment::iFromPlotIndex` and `iToPlotIndex`
are `short` (CvTacticalAI.h:538-539). `getPlotsWithChangedVisibility` takes `plotByIndexUnchecked(assignment.iToPlotIndex)`
(10298): a target plot with index 32,768 or more (row 102 and below on a 322-wide map) wraps to a negative index, the
unchecked look-up returns memory before the plot array, and its "terrain" is garbage. On 322 x 202 the barbarians' first
attack on such a plot came in turn 39. So the Architect's row "silently wrong above 32,768" is a crash, not silence. A
second limit sits beside it: the tactical plot look-ups are keyed by `unsigned short` with a guard `< USHRT_MAX`
(CvTacticalAI.h:1336, 1480; .cpp 10846-10870), and the root slot table is not built above 65,535 plots (h:1575); above
65,535 plots tactical plots are then "never found", which is wrong but does not crash [C, not run].

The 31,866-plot map (M1's target) stays below 32,768 and played 50 turns clean: **M1 needs no DLL type change** [L].

## 4. Wall 2, measured: VP's first barbarian camp spawn is cubic in the plot count [L; cause C]

Turn 2 is where VP places the first barbarian camps. At 65,044 plots it took 9.07 s against 2.7-3.9 s for its
neighbours; at 502,095 plots it had not ended after 14.5 minutes (single core at 100 %), and I stopped the run. Two
stack samples (`perf/sampler/sampler.py --pid`, 2026-11-26 22:12-22:14, 508 stacks): 96.4 % of the core thread's cycles
in an inlined `std::find` over 8-byte elements inside `CvPlayer::doTurnPostDiplomacy`, the rest in
`CvPlot::setImprovementType` / `setRevealedImprovementType` (camps being placed). The code [C, CvBarbarians.cpp at the
pin, `CvBarbarians::DoCamps`, called from `doTurnPostDiplomacy` for the barbarian player]:
- every unowned, unseen land plot is a candidate (`vPotentialPlots`), the coastal ones also go to `vPotentialCoastalPlots`
  (lines 782-793);
- the first pass keeps each candidate not too close to a capital or camp, and tests coastal membership with
  `std::find(vPotentialCoastalPlots...)` (880): candidates x coastal candidates;
- on the first spawn (elapsed turn 2 in VP, `BARBARIAN_INITIAL_SPAWN_TURN`, line 213) VP sets the number of camps to
  1 % of the valid plots (`BARBARIAN_CAMP_FIRST_TURN_PERCENT_OF_TARGET_TO_ADD` 10, `*= vValidPlots.size(); /= 1000`,
  lines 602 and 926-930), and after each camp placed the whole valid list is filtered again with the same `std::find`
  (994): camps x candidates x coastal, each term in proportion to the plots. On later turns the first pass alone is
  candidates x coastal whenever a camp is due.
- From the 65,044-plot turn (about 5.8 s above its neighbours) the cubic line gives about 45 minutes for turn 2 at
  502,095 plots [I].

The membership test is equivalent to `pLoopPlot->isCoastalLand()` (the coastal list is filled under that condition and
later rounds keep its valid subset) [C]: a one-line change per site removes both scans with no change to the game. The
scratch DLL of section 5 carries it; section 5 checks the "no change" on the 20,160-plot game.

## 5. Past the two walls: a scratch DLL (measurement only) [L]

To see what lies past the two walls, a scratch DLL: a `git clone --shared` of the local fork clone, local branch
`wide-0594` at the pin a2627c8ac with two local commits (never pushed, never merged; `evidence/0594/setup_fork.sh`,
`apply_wide.sh`, `apply_camps.sh`): e78164c68 the tactical AI's plot indices as int (`STacticalAssignment`'s from/to,
the two look-up tables' keys, their `< USHRT_MAX` guards, the root slot table's bound; `wide_plot_index.patch`) and
620deb5d0 the camp spawn's coastal test by `isCoastalLand()` (`camps_iscoastal.patch`). Built by `perf/speedkit/kitbuild.py
--config release` with the pin's three marker defines (`VP64_HIDDEN_EMBARK`, `VP64_SPEED_DANGER`, `VP64_SPEED_DANGER_DG7`,
as the pin's `build-summary.json`), 546 s, DLL sha256 efb70aaf..., in a copy of the layout's bin
(`F:/c5-64bit-work/0594/layout-wide/bin`; `kb_wide.sh`). Not a fix proposal: M2a makes the real change.

| run (scratch DLL) | plots | result | s per turn 0 / 1 / 2 / 3 | turns 26-50 mean (max) | private after turn 0 / peak | save body T0 / T50 |
|---|---|---|---|---|---|---|
| S1.20w, 2026-11-26 22:32 | 20,160 | 50 turns clean, **the same game as the pin's** | 2.42 / 1.78 / 1.33 / 0.51 (a busier machine) | 1.84 (7.78) | 333 / 348 MB | 35.7 / 47.2 MB |
| S2.15w, 22:34 | 65,044 | **50 turns clean, past the pin's crash in turn 39** | 4.88 / 2.90 / **1.92** / 3.62 | 4.01 (9.03) | 696 / 791 MB | 91.9 / 117.8 MB |
| S5.97w, 22:38-23:19 | 502,095 | **50 turns clean**; map generation 596.7 s; the whole run 41 min | 52.8 / 25.2 / **38.4** / 32.0 | 36.96 (46.77); median of turns 1-50 36.1 s | 4,106 MB, flat (4,149 at turn 49) / 5,883 MB in the final save | 637.3 / 788.7 MB (8.9 / 10.2 MB compressed); writing it 12.5 s / 38.9 s |

- **Identity** [L]: on 20,160 plots (no index past 32,767, so only the camp change can act) the scratch DLL's
  `final-summary.txt` at turn 50 is byte-equal to the pin's (every player's cities, population, techs, score, gold,
  units), and so is the turn-0 summary on 65,044 plots. The save files differ in the per-game GUID and the run
  folder's path, as any two new games do (runbook 3.11).
- **Turn 2 at 65,044 plots: 9.07 s on the pin's DLL, 1.92 s on the scratch DLL** (its neighbours 2.7-3.9 s in both): the
  camp spawn's scan was the whole spike [L, an indication on a shared machine].
- Both runs' `CvAssert.log` hold only two asserts of the headless host at every size (CvGame.cpp 5995 and 6089, the
  diplomacy response query without a localization database: `loc_attach=engine` attaches none in direct mode).
- **502,095 plots: turn 2 took 38 s on the scratch DLL** against more than 872 s on the pin's (section 4), and nothing
  else stopped the game: no exception, no `CvAssert.log` at all, Lua errors 0, 21 majors and 41 city states alive at
  turn 50 with 79 cities and 329 units [L]. No third wall showed up in 50 early turns: the `short` x, y, area and
  landmass fields (section 6) were not reached.
- **Turn time early in the game grows a little faster than the plots**: the mean of turns 1-50 is 3.53 s at 65,044
  plots and 36.0 s at 502,095 (10.2 times for 7.7 times the plots; log slope 1.13); from 20,160 to 65,044 the slope is
  about 1.1 as well (0.98 s to 3.15-3.53 s, two machine states) [L; the slope I].
- **Memory stays flat over 50 turns** (4,106 MB after turn 0, 4,149 MB at turn 49); the final save holds about 1.7 GB
  more for its buffers (the uncompressed body is 789 MB) [L].
- **What 500,000 plots would cost later in the game** [I]: early turns here are 37 times the 20,160-plot game's; the
  20,160-plot Marathon game of 9074 ran 34-134 s per turn at turns 400-2000 with 63 slots on this machine. The same ratio
  gives 20 to 80 minutes per late turn at 502,095 plots, before any pass that is worse than linear (section 4 is one;
  others may wait in later eras). So the DLL can play 500,000 plots, and a late game at that size is not playable
  without the AI-turn work.

## 6. The walls table of the Architect's note, as M0 leaves it

Corrections and additions to `architect-2026-11-19-massive-maps-and-civs.md` 2.1 and 2.2 (that note now points here):

| From plots | Wall | Status after M0 |
|---|---|---|
| 32,768 | Tactical AI plot index in `short` | **a crash, not silence**: 65,044 plots crashed in turn 39 [L] (section 3) |
| 65,536 | Tactical AI look-ups keyed by `unsigned short` | wrong look-ups, no crash expected [C]; widened in the scratch DLL |
| any size, cubic | VP's first barbarian camp spawn (`CvBarbarians::DoCamps`) | **new**: 9 s at 65,044, more than 14.5 min at 502,095 [L] (section 4); equivalent one-line fix [C] |
| about 74,000 | `setCustomWorldSize` finds no stock size | **not on the map-script path** [C]: it is called only through the engine interface `ICvPreGame::setCustomWorldSize` (CvDllPreGame.cpp:665), which the EXE uses for WorldBuilder maps; a map script keeps the world size the setup chose (here `WORLDSIZE_HUGE`) and gives width and height through `GetMapInitData`. A massive map made by a script does not meet it |
| none found | the DLL's other per-plot `short` fields (x, y, area, landmass) | x and y to 32,767 per axis, area and landmass ids to 32,767 areas: 895 x 561 with Tectonic stays far inside [I, section 5's run] |
| - | save body 700-800 B per plot (2.2) | **1.25 KB per plot** at turn 0 [L], 637 MB at 502,095 plots |
| - | DLL memory about 1.8 KB per plot + 90 B per AI player per plot (2.2) | **7.9 KB per plot at 62 players** [L], consistent |

## 7. What M0 means for the stages

- **M1 (about 32,000 plots, the product)**: the DLL side is not the limit. 31,866 plots played 50 turns clean at
  1.6 s per turn early and 455 MB of server memory [L]. What M1 must solve is all EXE-side (the load wall near 21,000
  plots, the farm graph past 16,384 plots, the renderer's 256-per-axis constants, the EXE's address space), as the
  Architect's note says. A map script of Tectonic's kind reaches it with one factor; no WorldBuilder limit is in the way.
- **M2 (360 x 180 headless)**: needs the tactical AI's plot indices as int (the crash) and the camp spawn's lookup (the
  cubic turn); both are small, local, harm nothing on standard maps and suit upstream as plain fixes (D104). Section 5
  says what else, if anything, showed up past them.
- **1000 x 500**: memory and map generation are no obstacle for the DLL (about 4 GB, 10 minutes). Turn time is: see
  section 5. The save (637 MB uncompressed at turn 0) cannot pass through the EXE; M3's "the save written by the
  server" is a precondition, not an option.
- **Turn time stays the practical ceiling**, as the note said; M0 adds that the curve is steeper than the plot count
  even early, and that single quadratic or cubic passes (like the camp spawn) decide whether a turn takes seconds or
  an hour. Every such pass is found only by running at size: M2's tickets should keep a 500,000-plot headless smoke.

## 8. Step tickets (drafts for the MC; ids to be given by the MC, COMMON rule 26)

Each is filed ready or blocked with a blocker (D274), priority 1 (D275), `answers: MC`, `trust: any`, kind task, parent
0594 (or the epic the MC opens for the idea). Hours [I].

**M0b. The EXE's load wall near 21,000 plots, read on build 26577625** (ready; machines: workstation; static first, a
Goldberg copy of the new build for one live check; no code write).
- title: "Idea 0594, step M0b: read the function behind the EXE's load wall at about 21,000 plots, and whether the
  thread's stack size can be raised without a code write"
- touches: `research/02-exe-internals/allocations-and-memory-map.md` (6.1), the study, the ticket, an evidence folder.
- acceptance: the faulting function of 9073's crash (old-build VA 0x926937, the stack `+0x8a351 +0x8a3b9 +0x1f2600
  +0x8a42c`) found on build 26577625 by the build table's means; the buffer named and its size per plot computed exactly;
  the thread's creator and its stack-size argument identified; whether an import-slot route (a `CreateThread` /
  `_beginthreadex` wrapper that raises the stack size for that one thread) moves the wall, checked live on a Goldberg
  copy with turn-0 saves of 21,879 and 31,866 plots made headless by `evidence/0594` (the 31,866-plot save is
  `F:/c5-64bit-work/0594/runs/S1.51/M0-T000.Civ5Save`); the next EXE wall after it named if the load goes further
  (D185: a crash root-caused, not closed on a rate). No `.text` write (D228); a code write, if it is the only route, is
  proposed for the owner's yes (D200), not made.
- size 6-12 h.

**M2a. The DLL's tactical AI plot indices as int, and the camp spawn's coastal lookup** (ready; machines: workstation;
headless only).
- title: "Idea 0594, step M2a: two DLL fixes for maps past 32,767 plots (tactical plot indices, barbarian camp spawn)"
- touches: the DLL fork (a branch of its own from the x64 pin; `CvTacticalAI.h/.cpp`, `CvBarbarians.cpp`), the study, the
  ticket; an upstream draft only after the owner's word (D95, D168).
- acceptance: the changes of `evidence/0594/wide_plot_index.patch` and `camps_iscoastal.patch` made properly (the
  look-up guards, the root slot table's bound, any other `short` or `unsigned short` that carries a plot index found
  by a search of the whole DLL, each named); the D10 save-identity test and the turn-time pair (`perf/speedkit/pair.py`,
  D178) show no change on standard maps (the camp change must be exactly equivalent); 65,044 plots plays 200 turns
  headless; a 500,000-plot headless smoke of 50 turns; numbers into the study.
- size 8-14 h.

**M2b. 360 x 180 and a map source of our own, headless** (blocked; blocker: "M2a merged on the fork's x64 branch").
- title: "Idea 0594, step M2b: a 360 x 180 game for 200 turns headless, on a map script of our own"
- touches: a map script under `tools/stressmap/` or a generator of our own (no third-party script committed, D83), the
  study, the ticket.
- acceptance: a map script that gives an exact width and height (360 x 180, 1000 x 500) with start plots for the player
  count; 360 x 180 plays 200 turns headless with turn times and memory per 50 turns recorded; the next wall named if one
  shows up (D185).
- size 10-20 h.

**M1. About 32,000 plots in the product** (blocked; blocker: "M0b's result, and the owner's choice of the target stage and his
yes for any EXE code write (D200)").
- as the Architect's note section 5, M1; the DLL side needs nothing (section 7).

**M3, M4** (blocked; blockers: "increment 3 and D144's true absence" for M3, "epic 0465's own height producer and M3" for
M4): epics of their own, not drafted here (the note's section 5).

## 9. Commands (this ticket, 2026-11-26)

All from the repository root of this branch, Git Bash; scripts in `research/07-architecture-decisions/evidence/0594/`.

```
bash research/07-architecture-decisions/evidence/0594/build_layout.sh            # server layout of the pin, 550 s under load
python -I tools/stressmap/tectonic_stress.py --game-dir "<Release-5.5 game dir>" --factor F --out F:/c5-64bit-work/0594/maps
bash research/07-architecture-decisions/evidence/0594/run_one.sh 1.20 50 3600   # one size: 75 s (1.20) to 17 min+ (5.97)
python -I -X utf8 research/07-architecture-decisions/evidence/0594/summarize.py F:/c5-64bit-work/0594/runs [names]
python -I perf/sampler/sampler.py --out <dir> --pid <server pid> --allow-foreign-pid --interval-ms 100 --depth 16 --duration 40
bash research/07-architecture-decisions/evidence/0594/setup_fork.sh             # scratch fork clone, branch wide-0594
bash research/07-architecture-decisions/evidence/0594/apply_wide.sh             # the int plot indices (local commit)
bash research/07-architecture-decisions/evidence/0594/apply_camps.sh            # the camp lookup (local commit)
bash research/07-architecture-decisions/evidence/0594/kb_wide.sh --config release   # kitbuild + layout-wide
LAYOUT=F:/c5-64bit-work/0594/layout-wide SUFFIX=w bash research/07-architecture-decisions/evidence/0594/run_one.sh 2.15 50 7200
```

Trap met: the sampler's first attach walked 2 stacks in 108 s (`--depth` default, symbol loading); a second attach with
`--depth 16 --interval-ms 100` walked 508 in 40 s.

## 10. Not verified

- Every time here is a workstation indication beside other teams' jobs (COMMON 23); the ratios between sizes are what
  the study uses.
- One seed, one map script (Tectonic), one player count (21 + 41), Standard speed, 50 turns: the late game at size is
  not measured (the 20,160-plot Marathon game of 9074 is the only long game at size).
- That turn 39's crash came from the assignment with the wrapped index is inferred from the stack and the source; the
  scratch DLL (indices as int) passed turn 39 and played to 50 on the same map and seeds, which supports it, but the
  two games had already diverged in what the wrapped indices did before the crash, so it is not a replay of the same
  turn. A debugger on the pin's DLL at that turn would confirm it [I, strong].
- No late game at any size past 20,160 plots; no other map script; the scratch DLL's two changes are not reviewed code.
- The 45-minute estimate for the camp turn at 502,095 plots is a cubic line through one point; the run was stopped at
  14.5 minutes.
- Nothing about the EXE: this stage ran without it.
