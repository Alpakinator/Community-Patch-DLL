# The giant stress game: Play The World Extended (map pack and v3) with Vox Populi, and its stock calibration (one-off 9069, D145)

Written 2026-11-15 by one-off 9069 for epic 0330 (the metrics ledger; the Architect's proposal
`research/07-architecture-decisions/architect-2026-11-15-metrics-ledger-epic.md` section 9, scenario P7). The owner chose
the map (D145 addendum): Play The World Extended (Brave New World Edition) (v 3), in the owner's MODS folder, read only.
Tools: `tools/stressmap/` (README there). Evidence: `F:/c5-64bit-work/0330/9069/`.

## 1. Result

- **Both maps run on stock Vox Populi on a Goldberg instance, fully automated, with no file of either mod changed**
  (`tools/stressmap/`): Hormigas Enhanced Map Pack (43 Civs) v1 (the owner's later choice, evaluated first, works:
  section 3a) and Play The World Extended BNW v3 (the fallback: section 3). 180 x 94 = 16,920 plots, Huge, 21 AI
  majors with true start plots + the observer, 32 city states (pack) or 16 (PTWE), every victory type off, turn limit
  2000. 22 majors cannot run with VP's autoplay observer (section 3, point 3).
- **Stock calibration** (section 4): after the load the EXE commits about 2,550 MB with about 1,060 MB of address space
  free; over 100 turns of the pack game committed grows 1.34 +- 0.06 MB per turn and free space falls 0.94-1.33 MB per
  turn (largest free block 1,003 -> 797 MB).
- **Forecast** (section 5): stock runs out of address space at about turn 450-700, well before the limit, so the game
  should carry a "CTD" marker; but a stock seed is many hours of machine time, and only a played-out run verifies it.

## 2. The map [C, read from the files 2026-11-15 with `tools/stressmap/civ5map.py`]

- The mod is four WorldBuilder **scenario** maps (`.Civ5Map`, version 12, scenario flag set) and nothing else: no
  Lua, no SQL, no XML. Each 408,046-408,049 bytes. 180 x 94 = 16,920 plots, world wrap on, **no world size in the
  file** (empty string; the game picks WORLDSIZE_HUGE from the dimensions [L]).
- **The "EPIC" map is the QUICK map**: `Play The World Extended BNW EPIC.Civ5Map` and `... QUICK.Civ5Map` are the same
  bytes (sha256 ff8a8451..., game speed GAMESPEED_QUICK inside). v3 has no Epic map. STANDARD (a5818886...) and
  MARATHON (91fbdda6...) carry their own speeds. The modinfo's md5 values match the files (the tool checks them).
- **Players:** 22 majors with true start locations, each on its own team, all playable, HANDICAP_PRINCE, ERA_ANCIENT:
  England (12,72), France, Germany, Rome, Spain, Greece, Sweden, Russia, Ottoman, Arabia, Egypt, India, China, Japan,
  America, Brazil, Inca, Shoshone, Zulu, Morocco, Mongolia, Indonesia (slot 21). 16 city states, all BNW ones that VP
  keeps (Bucharest, Budapest, Almaty, Lhasa, Singapore, Sydney, Jerusalem, Quebec City, Yerevan, Hong Kong, Zanzibar,
  Colombo, Cahokia, La Venta, Buenos Aires, Cape Town), **without start plots** (-1, -1): the game places them, each
  with a city on turn 0 [L].
- Every civilisation, city state, resource, feature and natural wonder name in the file exists in Vox Populi: the game
  loads the map with no database or Lua error [L]. 22 majors is exactly the DLL's `MAX_MAJOR_CIVS`.
- Player record layout (436 bytes) and header layout: in the docstring of `tools/stressmap/civ5map.py`.

## 3. What it takes to play it with Vox Populi: the recipe (`tools/stressmap/ptwe.py`)

No byte of the mod is changed. Three things are needed, all re-applied by the script from the original:

1. **The map file goes into the instance's `Maps` folder** (`ptwe.py apply -i <instance> [--speeds STANDARD]`), copied
   unchanged and checked by sha256; `ptwe.py remove` takes it out again by hash. The game lists the user's `Maps`
   folder without activating a mod, so the run keeps the usual exclusive mod set cp, vp, eui and the saves name no
   extra mod.
2. **A scenario start in the harness front end.** The vp-dll-dev skill's patched `MainMenu.lua` (used by
   `tools/harness/frontend.py`) can only start map scripts. `ptwe.py prepare` builds, in the instance's run folder, a
   copy of the skill's file with one added function, `c5StartWBScenario` (our own Lua, in `ptwe.py`), and hands that
   folder to `frontend.prepare(..., assets_dir=...)`. It does what the setup screen's scenario path does
   (`PreGame.SetMapScript(<.Civ5Map>)`, `SetLoadWBScenario(true)`, `SetOverrideScenarioHandicap(true)`,
   `UI.ResetScenarioPlayerSlots()`, the map's speed, `UI.MoveScenarioPlayerToSlot(<first playable>, 0)`), turns every
   victory type off and sets the turn limit. Two traps found on the first start [L, 2026-11-15 01:28]:
   - `Events.SerialEventStartGame()` swaps the UI and runs `MainMenu.lua` again in a fresh Lua state, so the start ran
     twice, 4 s apart (the game came up anyway). The function now records the launch's load token in
     `Modding.SetSystemProperty("c5WBScenarioStarted", ...)` and starts once.
   - After `PreGame.ResetSlots()` on the Huge size, slots 1-11 are SS_COMPUTER and 12-21 SS_OBSERVER, and
     `PreGame.GetCivilization(slot)` reads -1 for every scenario slot. The map's players came alive in all 22 slots
     regardless (22 majors alive at turn 0) [L].
3. **A free major slot for the observer.** VP's `CvGame::setAIAutoPlay` (CvGame.cpp, `setAIAutoPlay(int, PlayerTypes)`)
   moves the human into an observer slot: an SS_OBSERVER slot with an unassigned or reserved claim, else an SS_CLOSED
   slot below `MAX_MAJOR_CIVS` (22), searched from the top. With 22 majors there is none, and the function silently
   sets the autoplay counter back to 0 [C from the source; L: `Game.SetAIAutoPlay(30, 0)` then
   `Game.GetAIAutoPlay()` read 0 on the first start]. So one civilisation gives up its slot: `--close`
   (default Indonesia, the map's slot 21): **21 AI majors and the observer**.
4. **The scenario load cannot give that slot: load the map as a plain WorldBuilder map** (`scenario=0`, the default of
   `prepare` and `calibrate.py`). `CvWorldBuilderMapLoader::SetupPlayers` (the DLL, `CvWorldBuilderMapLoader.cpp`) sets
   every scenario player below the scenario's player count to SS_COMPUTER whatever the front end set, and takes the
   victory types, game options and max turns from the map (`SetupGame`: `setVictoryValid(type,
   m_kVictoryTypes.Contains(type))`, `setMaxTurns(GetMaxTurns())`) [C]. Live, 2026-11-15 01:39: with slot 21 set
   SS_CLOSED before the start, Indonesia was alive at turn 0 with the other 21, all five victory types valid, max
   turns 0, autoplay 0 [L]. The scenario's player count is one byte (the game description after the speed string:
   `uiStartingTurn, uiMaxTurns, uiTargetScore, iStartYear, byPlayerCount 22, byCityStateCount 16, byTeamCount 38`;
   `CvWorldBuilderMap.h` `GameDescription`), but the 38 player records follow one another, so lowering it would
   shift every later table [I]: not done. A plain load keeps the front end's slots: `c5StartWBScenario` with
   `scenario=0` sets the map's civilisations in the map's order (minus the closed ones) into slots 0-20, slot 21
   closed, 16 city states (the map's count), every victory type off and max turns 2000, all honoured [L, 01:46:
   21 majors, 16 city states, `Game.IsVictoryValid` false for all five, `Game.GetMaxTurns()` 2000, autoplay armed].
5. **True start locations on a plain load: moved at turn 0** (`calibrate.py --tsl`). A plain load places the civs
   itself (England at (84,49), not the map's (12,72)) [L]. Before any city exists, `--tsl` moves each major's
   starting units onto its start plot from the map file (`unit:SetXY`, `player:SetStartingPlot`); slot s is the s-th
   remaining map player. [L, 01:55: all 21 moved; at turn 4 every capital stood on its plot or within 2 plots of it,
   e.g. England (12,72), Rome (20,58), Japan (97,58), Inca (156,23).] City states are placed by the game in both
   cases (the map has no plots for them).

What a run is: `ptwe.py apply` once per instance, then `calibrate.py <instance> --out ... --turns N [--tsl]`. Load
121-156 s on c5h-e (F:, the workstation under other teams' load; timing indicative only, D106).

## 3a. Hormigas Enhanced Map Pack (43 Civs) (v 1): evaluated first on the owner's order (D145 addendum 3), works

The owner's later choice (MC, 2026-11-15 ~02:00): the same author's map pack, in the same MODS folder, read only. Script
`tools/stressmap/mappack.py` (check, apply, prepare, remove); `calibrate.py --pack`.

**What it is [C, read 2026-11-15].** 18 files: four `.Civ5Map` (its `PlayTheWorldExtended.Civ5Map` is the same 180 x 94
map, 412,893 bytes, also a scenario file with 22 players, header flags 0x5), database tables of true start plots
(`XML/CivilizationsStartPos.xml`: `Civilizations_GiantStartPosition`, 43 civilisations; `XML/MinorCivStartPos.xml`:
`MinorCiv_GiantStartPosition`, about 55 rows, some under names that no longer exist), `SQL/CityStates.sql` (deletes 23
city states, renumbers the IDs), `XML/AddMinorCivs.xml` (13 city states), its own setup screen (a "Custom" entry point:
`GameSetup/MapPackSetup.lua`, loops to `GameDefines.MAX_MAJOR_CIVS`), an override `Override/MapGenerator.lua`
(imported into the VFS; a copy of the stock file whose `AddGoodies`, the one function the WorldBuilder loader calls,
first calls `InitializeMapPack`) and an in-game add-in `LUA/MapPackInGame.lua` (kills the city states without a start
plot at the start). How it works: the setup screen's Start button loads the map **plain** (`SetLoadWBScenario(false)`)
and leaves its choices in the mod's user data (`Modding.OpenUserData`: `MapID`, `NumPlayers`, `NumMinorCivs`,
`Handicap`, `ToRemove`, `LoadingMapPack = "true"`); during the load the overridden `AddGoodies` reads them and sets
every major's and city state's starting plot from the tables (`player:SetStartingPlot`).

**Automated without its screen [L, 2026-11-15 02:09-02:13, c5h-e, stock VP, DLL `i0ship-26234371-ia`].** The harness
activates cp, vp, eui and the pack (the pack last, so that its `MapGenerator.lua` wins over Community Patch's
override), and `c5StartMapPack` (in `ptwe.py`'s generated front end) writes the same user data and PreGame values as
the Start button (map path from `Modding.GetEvaluatedFilePath`, Huge, Standard, Prince, the 21 civilisations of the
PTWE plan in slots 0-20, slot 21 closed for the observer, 41 city states = VP's `MAX_MINOR_CIVS`, victories off, max
turns 2000). Result: the load ran the pack's hook (`WorldBuilderMapLoader: MapPack highjacking - Setting Map`, "Map ID
: 1", a "Civtype" line for each of the 21 civs); at turn 4 every capital stood on its table plot or within 2 plots
(England (12,72), Rome (20,58), Inca (156,23)); 41 city states were drawn from the 45 left in the database, 9 had no
start plot and were removed, **32 city states** alive at turn 0 (twice PTWE v3's 16); victories off and max turns 2000
held; autoplay armed (21 majors + observer, the same slot rule as section 3 point 3). Load 110 s.
- Harmless noise, all from the pack: `Database.log` "Invalid Reference on MinorCiv_GiantStartPosition.Type ... does
  not exist in Civilizations" (its minor tables declare a reference to `Civilizations`), and the Europe tables' civs
  that do not exist; `Lua.log` "MapPackFunctions.lua:90: attempt to index global 'Events'" a dozen times in the menu,
  when VP's map-script previews include the overridden `MapGenerator.lua` (not in the load). None stopped the game.
- 22 majors are not possible with VP's autoplay observer (section 3 point 3); 43 majors stay out of scope (they need
  the 43-civilisation DLL variant). More city states with a start plot would need choosing which city states the game
  draws; the engine draws them at random from the database, so the count varies a little per game [I].

## 4. The calibration runs

All on 2026-11-15, the workstation (memory counts here, timing does not, D106), instance c5h-e (F:), stock route: the
stock EXE with the pinned instance-aware 32-bit DLL `i0ship-26234371-ia` (sha256 3c2f47da...), mods cp, vp, eui (and
the pack for `pack-*`), AI logging as the instance has it, frame cap lifted during autoplay (as `turns.wait_turn` does
for AI turns). Huge, Standard speed, Prince, 21 AI majors + observer, every victory type off, max turns 2000. Driver
`tools/stressmap/calibrate.py`; one memory sample of the EXE (`tools/harness/memory.py`) at the first poll after
`Game.GetGameTurn()` changes, i.e. during the AI part of a turn, so single rows scatter by about +-30 MB committed; only
fits are used (`tools/stressmap/forecast.py`, ordinary least squares). Folders under `F:/c5-64bit-work/0330/9069/`:
`run.json`, `per-turn.csv`, `memory.csv` (every 5 s), `forecast.json`, `run.log`.

| run | map / start | turns | t0 committed / free / largest free (MB) | last turn committed / free / largest free | cities at end |
|---|---|---|---|---|---|
| `stock-cal4` | PTWE v3 plain, game's own start plots | 0-30 | 2551.6 / 1052.1 / 994.8 | 2639.9 / 928.7 / 862.1 | 39 |
| `stock-tsl150` | PTWE v3 plain, true start plots moved at turn 0 | 0-60 (cut, see 3a) | 2565.5 / 1056.6 / 990.4 | 2698.1 / 879.5 / 729.5 | 60 |
| `pack-cal1` | map pack, its start plots, 32 city states | 0-100 | 2544.8 / 1064.9 / 1003.4 | 2706.6 / 913.3 / 796.8 | 120 (529 units) |

Slopes (MB per turn, +- one standard error):

| run, window | free | committed | largest free block |
|---|---|---|---|
| `stock-cal4` 0-30 | -1.86 +- 0.36 | +1.63 +- 0.27 | -2.01 |
| `stock-cal4` 15-30 | -0.28 +- 0.57 | +0.34 +- 0.44 | -0.50 |
| `stock-tsl150` 0-60 | -1.46 +- 0.11 | +1.55 +- 0.09 | -3.18 |
| `stock-tsl150` 30-60 | -1.46 +- 0.17 | +2.03 +- 0.22 | -4.73 |
| `pack-cal1` 0-100 | -0.94 +- 0.06 | +1.34 +- 0.06 | -1.84 |
| `pack-cal1` 50-100 | -1.33 +- 0.18 | +1.36 +- 0.21 | -1.40 |
| `pack-cal1` 75-100 | -2.03 +- 0.63 | +1.49 +- 0.74 | -0.95 |

`pack-cal1` was started for 30 turns and extended to 100 at turn 4 (`Game.SetAIAutoPlay(96, 0)` over the Lua channel;
`run.json` still says 30 wanted). Turn-0 saves (each the game right after the load, before any turn):
`F:/c5-64bit-work/0330/9069/PACK-STOCK-T000.Civ5Save` (sha256 5c21a29f...; needs the pack activated: AffectsSavedGames),
`PTWE-STOCK-TSL-T000.Civ5Save` (c086ec66..., start plots already moved), `PTWE-STOCK-PLAIN-T000.Civ5Save` (207cb5eb...).

Other facts [L]: right after the load the EXE commits 2,496-2,647 MB with 974-1,131 MB free (the September giant game,
GameId 62 on the old DLL, started at 1,945 MB committed and 1,659 MB free: about 600 MB less headroom here; not
explained by this ticket [I: settings or assets of the instance]). Reserved stays at 470-550 MB. The free space below
2 GB is 37-49 MB from turn 0, and its largest block 6-15 MB: the low half is full from the start, so the game lives on
the one large block above 2 GB, which is what the largest-free column follows. Turn times on this busy workstation:
10-12 s per turn up to turn 60 (indicative only, D106). Autosaves (`AutoSave_Post_N`) are written every turn during
autoplay, so `perf/memsampler` can follow such a run too.

## 5. Forecast: stock dies, but late: about turn 450-700 [I, from the fits above and two reference slopes]

The death line (`forecast.py`): the EXE runs out when its free address space is about 70 MB (GameId 62 died three
turns after 70.5 MB free, largest block 4.0 MB; `research/04-memory-investigation/ownership-and-ceiling.md` 1).
Early-game slopes understate later growth (more cities, units and AI state); the September giant game grew 2.476 MB
per turn committed over turns 20-317 on the old DLL (`leaks-and-growth.md` 2), and D78's acceptance run 1.63 MB per
turn for the stock EXE late in a huge game (Architect's proposal 9.5, G2).

| assumption from turn 100 of `pack-cal1` (913 MB free) | MB per turn | death turn |
|---|---|---|
| measured, turns 50-100 (free) | 1.33 | ~ 730 |
| measured, turns 75-100 (free) | 2.03 | ~ 515 |
| D78's late stock EXE growth | 1.63 | ~ 620 |
| September giant game, old DLL | 2.48 | ~ 440 |

So the forecast lies **well before the turn limit of 2000** (the proposal's acceptance condition for a candidate) and
stock should die, at about turn **450-700**; the 30 turns asked for cannot narrow this (the 15-30 window's slope was
0.3 +- 0.6 MB per turn). The cost follows from it: at 10-16 s per turn up to turn 100 and turns getting longer, a stock
seed is many hours of machine time [I], and the patched arm 100
turns more. A stock death earlier than the fit (fragmentation: GameId 62 died about 29 turns before its forecast) is
likely. The largest free block above 2 GB shrinks in steps (1,003 to 797 MB in 100 turns); the low 2 GB are full from
turn 0. Not verified: the forecast itself (only a played-out stock run verifies it), and whether the AI keeps the game
growing after the tech tree is complete.

**Correction check, 2026-11-15 06:35 EST (worker-4203b, issue 4203, MC entry 1542) [I, fits; L, the runs].** The P7
stock seeds of the profile kit (`metrics-ledger.md` 4.3; same recipe, new games from turn 0, pin `i0ship-26234371-ia`,
workstation) gave this section's forecast a first test. Seed 1's fit over **turns 51-101** said death by free space at
**about turn 290** (free -3.74 +- 0.19 MB per turn from 792.6 MB free at turn 100), far earlier than 450-700. The same
seed's fit over **turns 101-188** says **about turn 583** by free space (-1.53 +- 0.06 MB per turn, 692 MB free at turn
188) and about 663 by committed (+1.16 MB per turn): the turn 51-101 slope did not hold, and the forecast is back inside
450-700. Seed 2 (another city-state draw) over turns 44-87: about turn 893 by free space. **Update 09:00 EST:** seed 1 at turn 297 (436.8 MB free, largest block 262.6 MB): turns 188-297 give free -2.00 +- 0.06 MB per turn, death about turn 492 by free space and 498 by committed (+1.68 MB per turn); seed 2 at turn 261, turns 101-261: about turn 555 (committed 647). Both still inside 450-700; turns now take about 2.5 minutes each on seed 1. **The 450-700 heading
stands** until a seed ends; the outcomes (turn, how it ended, largest free block at the last sample) are added here
when known. Trace: wave 1's issue log and `metrics-ledger.md` 4.3 had stated "death about turn 290" from the 51-101 fit;
corrected there the same day.

## 6. Recommendation and what is next

- **Use the map pack (`calibrate.py --pack`)**: the same map with the author's own start plots for every civ, 32 city
  states instead of 16, set up without its screen, and no change to any file of the mod. PTWE v3 stays as the fallback
  (`ptwe.py`, plain load, `--tsl`); it needs the turn-0 move for true start plots and has 16 city states.
- The long stock arm (proposal 9.3, three seeds to the death) starts from `calibrate.py --pack --turns 2000` (or a
  turn-0 save); the driver ends a run as a crash when the EXE is gone (exit 6, last sample kept). Each seed is a new
  game (city-state draw and AI choices differ).
- Not done here: the patched arm (G3: the product path on a 16,920-plot map), the ledger's own forecast metric
  (epic 0330 W3), 22 majors (impossible with VP's autoplay observer), 43 majors (needs the 43-civ DLL variant).

## 7. The 43-civilisation build on the map pack (issue 4255, epic 0334, 2026-11-15)

Written 2026-11-15 by the 4255 worker. Details, commands and evidence: `research/03-dll-engine-boundary/player-limits.md`
section 8.4; scripts in `research/03-dll-engine-boundary/evidence/0334/4255/`. Workstation indications only (COMMON 23).

- **The plan now follows the DLL** (section 6's "43 majors need the 43-civ DLL variant" is done): `mappack.py prepare`
  and `calibrate.py --pack` read `GameDefines.MAX_MAJOR_CIVS` / `MAX_MINOR_CIVS` in the front end and seat the pack's
  start-table civilisations up to `MAX_MAJOR_CIVS - 1`, the top major slot closed for the observer, and ask for
  `MAX_MINOR_CIVS` city states. 22 build: 21 + observer + 41 asked (33 alive); 43 build: **42 + observer + 20 asked
  (16-17 alive)**, since the DLL draws the city states and the pack removes those without a start plot (the EXE's
  `PreGame` has no `SetMinorCivType`). Section 3a's "43 majors stay out of scope" is replaced by this.
- **Both arms ran 178-180 AI turns clean** on the Hormigas map (180 x 94, Huge, Standard, Prince, victories off):
  the stock 32-bit EXE with the 32-bit 43 DLL (`i0ship-26234371-m43-ia`) and the D33 product path with the x64 43 DLL.
  Turn time rose from about 14 s (turns 1-20) to 43-47 s (turns 100-180) in both arms.
- **Stock arm, the EXE's address space (32-bit, 43 build):** private 2,249 MB at turn 0 -> 2,551 MB at turn 180; free
  1,021 -> 646 MB; largest free block 966 -> 501 MB; -2.07 MB free per turn over turns 100-180. Against 9069's 22
  build (`pack-cal1`, 21 + 32 city states: 1,065 MB free at turn 0, 913 MB at turn 100) the 43 build has about the same
  room at turn 0 and less by turn 100 (774 MB). A straight line to 9069's death line (~70 MB free) gives about turn
  460 [I; section 5's caveats apply, and growth speeds up later, so earlier is likely].
- **Product path (x64 server), old tools line (8.4.6 there):** the EXE through the proxy 1,527 -> 1,762 MB private,
  1,833 -> 1,551 MB free at turn 178; the server 2,112 -> 2,904 MB private (0.5 MB per turn after turn 20). The
  rerun on the team head's layout (the result, 8.4.10 there, 180 turns clean): the EXE 1,518 -> 1,749 MB private and
  1,847 -> 1,613 MB free at turn 180 against 646 MB free on stock; the server 2,256 -> 2,790 MB private.
