# Save determinism: what differs between two saves of the same game

Task 1308, W6 of team 1300, 2026-11-10. This file covers why two headless runs of the same DLL on the same
fixed-seed game write different saves, and how that fits with epic 1000's "byte-identical" x86 saves. It is research
only; nothing here is fixed.

## Result

Two separate host processes that play the same fixed-seed game with the same DLL write saves that are never
byte-identical, even though every `stats.db` row is identical.

- **[L] Exact per-statement comparison.** Two saves of the same game written by the save-map DLL (below) were compared
  statement by statement. Of the 3,399,130 serialize statements, 417 differ, and they come from **13 writers**. They
  fall into three causes:
  1. **Two GUIDs made by `CoCreateGuid` per game** (2 writers, 44 bytes):
     - `CvGame::m_strGameId` (36-character string);
     - `CvMap::m_guid` (16 bytes).
  2. **Uninitialised memory written to the save** (6 writers, 1,177 bytes):
     - two fields of every unused notification slot;
     - the holy-city X/Y of pantheons;
     - team arrays at indices 64-79.
  3. **Great-person names, which differ between processes** (5 writers):
     - unit names (`m_strGreatName`, `m_strName`);
     - the game's born-name hash list;
     - great-work names and types.

  This is the only cause that is game content. It changes which great work a great person creates, but it does not
  reach any `stats.db` table.
- **[L] 1301's 43 A/A difference runs.** All 43 runs between `1301-aa-std200\A1` and `A2` map onto these three causes
  (table in section 3).
- **[L] What a byte-identical comparison needs.** Every difference is in game state that a save restores. So
  load-then-save of one save, in any number of processes, gives byte-identical bodies, provided the DLL version
  string is equal. That is what epic 1000 compared (section 5). A save from play is not a fixed point of
  load-then-save, though: loading and saving 1301's A1 at once gives 219 aligned difference runs against A1 itself.

## 1. How the saves were produced and compared

Common setup:

- **Host.** civ5-headless checkout `%USERPROFILE%\Documents\civ5-headless` at commit `00ce9f6`; host exe
  `out\Release\20261027-120431\civ5headless.exe`, from `crashes.log`.
- **Game dir.** The toolkit copy
  `...\verns_civ5_vp_toolkit\verns_civ5_vp_toolkit\headless_data\game\VP_MODPACK_5_4_8_NEUTRALS\Sid Meier's Civilization V`.
- **Database.** The host's pinned 5.4.8-neutrals database. Each run copies it into `<run>\db\`; the copies are
  byte-identical, `Civ5DebugDatabase.db` SHA-256 `284853f2...`.
- **Game.** Spec `%USERPROFILE%\Documents\civ5-headless\examples\specs\continents-standard-fixed-seed.json`
  (Continents, Standard 80x52, 8 majors, 16 city-states; player 0 Shoshone, human until autoplay), seeds
  `--seeds 1001 2002`, `--no-mods --detail summary`.
- **Base DLL.** Release-5.4.8 (7307aefdf), SHA-256 `9237D4CFF4C1B045D337EA98B5FDED4B72948150113249E72D65DAD577699B6D`.

| pair | command (per arm; the arms run at the same time in separate processes) | saves |
|---|---|---|
| 1301 A/A (the 43 runs) | `tools\i0\ab.py run --dll <base> --dll <base> --name A1 --name A2 --label 1301-aa-std200 --turns 200 --seeds 1001 2002` | `<pins_dir>\runs\1301-aa-std200\A{1,2}\saves\<final save>.Civ5Save`, the end-of-run final save (the save's file name replaced by a placeholder, 2026-11-18, export ruling J6) |
| 1308 A/A, autosaves | the same with `--c5h-arg=--autosave --c5h-arg=10` and a units final extra, label `1308-aa-std200-saves` | `...\1308-aa-std200-saves\A{1,2}\saves\auto\*` every 10 turns, plus the final save |
| 1308 save-map A/A | `c5h run <spec> --turns 200 --seeds 1001 2002 --dll <save-map DLL> --save-at 100`, with `VP_SAVEMAP=<dir>` (`evidence\1308\run_savemap_aa.ps1`) | `...\1308-savemap-aa-std200\M{1,2}\saves\`, plus `M{1,2}-map\savemap-00{0,1}.txt` |

All three pairs give identical `stats.db` (356,105 rows, `tools\i0\ab.py diff`). The save-map DLL also matches
base on `stats.db` (356,105 rows identical against `1301-aa-std200\A1`), so its instrumentation does not change
behaviour.

**Save layout and comparison scripts** (in `<pins_dir>\evidence\1307\` and `...\1308\`):

- `savecmp.py` splits a save as follows (layout per civ5-headless `docs\saves.md` section 2):
  - the uncompressed prefix: header, pregame and marker;
  - the inflated body: the 64 KiB pieces, joined and inflated;
  - `Game::Write` embeds the save database as `u32 size` followed by the SQLite file. The script cuts it out. "preDB"
    is the body before it, which is all of `CvGame::Serialize`. "postDB" is everything after it: the end of
    `CvGame::Write`, then `CvMap::Write`, teams 0-63, players 0-63, and `WriteSupportingClassData`.
- `savealign.py runs` aligns two bodies greedily, separately for preDB and postDB. Equal stretches are skipped. At
  a difference it resyncs on the first 32-byte anchor of A after the difference that has at least 6 distinct byte
  values, searched in B within 16 KiB (steps of one byte up to 4 KiB, then 4 KiB steps up to 2 MiB). It reports
  each difference run as (offset, length in A, length in B, the 24 bytes before it).

  The resync is heuristic. A run's length is the distance to the next good anchor, so one run can cover many small
  differences: the 8192- and 36864-byte runs are like that. Once it falls out of sync it ends in one long tail run
  (runs 42 and 43 below). **The "43 runs" are a property of this aligner, not 43 independent causes.**
- **The save map** is the exact method:
  - **DLL.** The throwaway local branch `i0-savemap-1308` (e9122636c on 7307aefdf, made by
    `evidence\1308\savemap_patch.py`, never pushed). DLL `<pins_dir>\dlls\savemap1308-e9122636\`,
    SHA-256 `B324048E5DA9753025CBC643516161BF93656690CAEC200A5E8A7BC3AE0416A9`.
  - **What it records.** Every statement that starts with `visitor` in a `.cpp` (3,020 statements) becomes
    `SAVEMAP_MARK(visitor), <statement>;`. That is one expression statement, so unbraced `if` bodies keep their
    meaning. While saving, each mark appends `body offset, file, line` to `%VP_SAVEMAP%\savemap-NNN.txt`. Marks
    also name each CvPlayer, CvTeam, CvUnit, CvCity and CvPlot object, plus the sections (CvGame::Write, the
    embedded database, CvMap::Write, supporting data). The save bytes are unchanged.
  - **Line numbers.** CvPlayer/Team/Unit/City/Plot.cpp lines after the object mark are one higher than in
    Release-5.4.8. The scripts subtract 1, so every file:line in this document is a Release-5.4.8 line.
  - **Comparison.** `exactcmp.py A MAP_A B MAP_B` checks that both maps have the same sequence of statements. This
    held for M1 and M2: 3,399,130 statements each, identical. It then compares the bytes each statement wrote.
  - **Mapping 1301's runs.** `attribute_runs.py translate` aligns 1301's A1 to M1 (same game, same turn) and names
    the M1 statement that wrote each of 1301's difference runs.

## 2. The 13 writers that differ (exact, M1 vs M2 final saves, turn 200)

Source: `<pins_dir>\evidence\1308\exact-savemap-aa-t200.txt`. File:line is the writing statement in
Release-5.4.8.

| # | writer (statement) | member | statements / bytes differing | cause | varies | cheapest way to make it deterministic (not implemented) |
|---|---|---|---|---|---|---|
| G1 | CvGame.cpp:10975 `visitor(game.m_strGameId)` | 36-char game UUID | 1 / 28 | `CoCreateGuid` in `CvGame::init` (CvGame.cpp:205-215) [C] | every new game, so within a process too [C]; kept by save/load [C] | derive it from the map and sync seeds, or write a fixed value when an option is set |
| G2 | CvMap.cpp:1789 `visitor(map.m_guid)` | map GUID, 16 B | 1 / 16 | `CoCreateGuid(&m_guid)` in map init (CvMap.cpp:883) [C] | every map init [C] | same: derive it from the seeds |
| U1 | CvNotifications.cpp:36 `m_iExtraGameData` | per notification slot | 156 / 555 | `Notification::Clear()` (CvNotifications.cpp:64-77) sets every field except `m_ePlayerID` and `m_iExtraGameData`. All 150 slots per player (`MaxNotifications`, `m_aNotifications[150]`, CvNotifications.h:39/115) are saved by `visitor(notifications.m_aNotifications)` (:123). Unused slots carry heap garbage from `FNEW(CvNotifications)` (CvPlayer.cpp:858). [C] | garbage depends on heap history: process to process [L]; within one process [I] | add `m_ePlayerID = NO_PLAYER; m_iExtraGameData = -1;` to `Clear()` |
| U2 | CvNotifications.cpp:40 `m_ePlayerID` | same | 162 / 566 | same [C] | same | same |
| U3 | CvReligionClasses.cpp:207 `m_iHolyCityX` | `CvReligion` | 4 / 16 | the constructor with a holy city (CvReligionClasses.cpp:185-199) leaves `m_iHolyCityX/Y` unset when `pHolyCity` is NULL, as it is for pantheons. The default constructor sets -1. [C] | stack garbage: process to process [L] | initialise both to -1 in that constructor's initialiser list |
| U4 | CvReligionClasses.cpp:208 `m_iHolyCityY` | same | 4 / 8 | same [C] | same | same |
| U5 | CvTeam.cpp:9227 `m_aiTechShareCount`; CvTeam.cpp:9239/9240 and 9306 `m_abAggressorPacifier`, `m_abPermanentWarPeace` | `Firaxis::Array<T, REALLY_MAX_TEAMS>` (80) | 2 / 32 here (1301 shows 9239 and 9306) | the reset loops (CvTeam.cpp:218-230) run to `MAX_TEAMS` = 64, but the arrays have `REALLY_MAX_TEAMS` = 80 entries and are saved whole. Teams come from `FNEW(CvTeam[REALLY_MAX_TEAMS])` (CvTeam.cpp:58). Only elements 64-79 differ, measured: elements 64-79 of :9240 and 66-73 of :9227. [C][L] | heap garbage: process to process [L] | loop to `REALLY_MAX_TEAMS` in reset, or value-initialise the arrays |
| N1 | CvUnit.cpp:29001 `m_strGreatName` | named units | 18 / 25 | great-person names differ, section 4 | process to process [L] | section 4 |
| N2 | CvUnit.cpp:29002 `m_strName` | same | 18 / 25 | same | same | same |
| N3 | CvGame.cpp:10950 `m_aszGreatPeopleBorn` | `std::vector<size_t>`: name hashes in birth order (`addGreatPersonBornName`, CvGame.cpp:8392; `stringHash`, CvGame.cpp:86) | 1 / 211 | same; also the count (1301: 149 vs 150, run 1) | same | same |
| N4 | CvCultureClasses.cpp:53 `m_szGreatPersonName` | `CvGreatWork` | 25 / 732 (24 change length) | a great work carries its creator's name [C] | same | same |
| N5 | CvCultureClasses.cpp:54 `m_eType` | `CvGreatWork` | 25 / 32 | the work's type follows the name: `SetGreatWork(GetGreatWorks(index))` (CvUnit.cpp:637). **Game content.** [C] | same | same |

The other 3,398,713 statements are byte-identical, including:

- all city, plot and unit-ID state;
- the random generators;
- the replay data;
- the embedded save database (its content).

**When each cause appears** [L] (base A/A, autosaves every 10 turns; `evidence\1308\aa-autosave-run-counts.txt`):

- Turn 0 (`AutoSave_Initial`) already differs, in 9 aligned runs: the GUIDs, notification and team garbage.
- The preDB difference runs grow from 1 to 2 at turn 30 and to 16 by turn 200.
- Great-person names first diverge at turn 62 in the trace runs (section 4).
- Aligned runs per save: 9 at turn 0, 23 at turn 100, 42 at turn 200.

## 3. The 43 A/A difference runs of 1301 (`1301-aa-std200` A1 vs A2, final saves, turn 200)

**Method.** `attribute_runs.py translate` (output `<pins_dir>\evidence\1308\attr-1301-aa-43runs.txt`):

- Offsets are into the inflated body: preDB offsets are body offsets; postDB offsets start after the embedded
  database, and postDB @0 = body 101,300.
- Lengths are A1 / A2.
- "Writer" is the M1 statement at the run's start; for equal-length runs, the writers of every differing byte are
  listed.
- [L] for the mapping, [C] for the cause.

| runs | where (offset, length A1/A2) | writer and object | cause (section 2) |
|---|---|---|---|
| 1 | preDB 5481, 1/1 | CvGame.cpp:10950: the count of `m_aszGreatPeopleBorn` (0x95 = 149 vs 0x96 = 150) | N3 |
| 2 | preDB 5589, 492/496 | CvGame.cpp:10950: the hash entries | N3 |
| 3 | preDB 70942, 8192/6844 | starts at CvReligionClasses.cpp:208 (pantheon holy city); the aligner span runs on into the great works | U3/U4 + N4/N5 |
| 4-9 | preDB 79173-79695, lengths 0/1304, 50/0, 0/55, 92/88, 33/33, 36/33 | CvCultureClasses.cpp:53-57, `CvGreatWork` names, types, turn and era fields shifted by the different name lengths | N4/N5 |
| 10 | preDB 98188, 36/36 | CvGame.cpp:10975 `m_strGameId` | G1 |
| 11 | postDB 30, 16/16 | CvMap.cpp:1789 `m_guid` | G2 |
| 12 | postDB 5179013, 1595/1595 (12 bytes) | CvTeam.cpp:9239 team 1, elements 64-79 | U5 |
| 13 | postDB 5197600, 16/16 (12 bytes) | CvTeam.cpp:9306 team 1, elements 64-79 | U5 |
| 14, 17, 26, 32, 35, 39, 40, 41 | postDB 6622421 (P0, 1,090 bytes), 7140698 (P1), 9072156 (P4), 9832060 (P5), 10689691 (P6), 11754891 (P7), 12778364 (P11), 13051934 (P13) | CvNotifications.cpp:36/40, notification slots of players 0, 1, 4, 5, 6, 7, 11, 13 | U1/U2 |
| 15, 16, 18-23, 28-31, 33, 34, 36-38 | postDB units of players 0-6, 1-4 bytes each | CvUnit.cpp:29001/29002, named units (IDs 6688, 6836, 4390, 5904, 5912, 3099, 6477, 3355, 6869, 3058, 5599, 5104, 6678, 5879, 6279, 6966) | N1/N2 |
| 24, 25, 27 | postDB 8829881 (0/8376), 8829964 (20480/12104), 9395716 (35/95) | CvUnit.cpp:29001 (unit 4713, P3; unit 6624, P4): a name length change, then an aligner span over the following units | N1/N2 |
| 42 | postDB 13318873, 602112/59996 | starts at CvNotifications.cpp:36 of player 15; the aligner loses sync and the run covers the rest of players 15-16 | U1/U2 [I for the bytes inside the span] |
| 43 | postDB 13921567, 319488/861604 | starts at CvDiplomacyAI.cpp:414 of player 16 (where the resync failed), then the tail of the body | U1/U2 [I] |

[I] for runs 42 and 43, the bytes inside the tail spans: the exact M1/M2 comparison of the same game and turn finds
no differing writer beyond the 13 above, DiplomacyAI included.

## 4. Great-person names

[L] Trace build (throwaway `i0-gpname-1308`, `evidence\1308\gpname_patch.py`, runs `1308-gptrace-std100` G1/G2):

- **What matches.** Unit IDs, owners, positions, cities and turn order are identical between the runs (units dump at
  turn 200 of `1308-aa-std200-saves`: 505 units, 62 cities; only 11 unit names differ).
- **Where it diverges.** The first divergence is at turn 62, `CvUnit::initWithNameOffset`, for great scientist unit
  1493 of player 1:
  - same era (1) and the same born list (3 entries);
  - `vfPossibleUnits` has 7 entries in one run and 6 in the other, so the roll `urandLimitExclusive(size,
    CvSeeder(iID))` (CvUnit.cpp:634) picks a different name;
  - every later great person of the same class then sees a different born list.
- **What the database says.** It has 1 ancient and 6 classical scientist names, and S__010 is already born, so 6 is
  the expected count.

### What the two traced runs show

- **[L] Loading is identical.** Trace with `NAMESLOAD` (commit 5368261bb, runs `1308-gptrace3-std2`): in both processes
  all 14 unit types with unique names load complete. The row count equals `count(*)` and no name is empty; for
  example UNIT_SCIENTIST has 136 of 136. All are loaded on the same thread. The database copies are byte-identical.
- **[L] The candidate lists differ later.** Trace with `CAND` lines (commit 9c73edd36, runs `1308-gptrace2-std63`):
  - At turn 52, engineer unit 1345 of player 1, one process had 4 candidates and the other had 5.
  - The extra candidate, index 38, is `TXT_KEY_GREAT_PERSON_E__020`. In the first process the debug copy of it
    (`CvString strDbg = GetUnitNames(38)`) printed as an empty string and was counted as born. In the second it
    printed the name and was not born.
  - Index 54, `S__010`, read empty in one process and intact in the other at turns 53 and 62.
- **[L] The first divergence turn varies.** In trace 1 it was turn 62; in trace 2 it was turn 52.
- [C] The DLL has no threads of its own: no `_beginthread`, `CreateThread`, `std::thread` or `concurrency::` in
  CvGameCoreDLL_Expansion2.
- [C] `CvUnit::setName` passes a copy to `gDLL->stripSpecialCharacters`, which is a no-op stub in the host
  (`HHStubs.gen.h:134`).

### Root cause

**Not found** (budget). [I] Some unit-info unique-name strings (`CvUnitEntry::m_paszUnitNames`, `FNEW(CvString[n])`
at CvUnitClasses.cpp:507), or the born check that reads them, change after load in a process-dependent way. That
points to memory corruption or a host-side race, not to game logic. The traces support only "reads empty and counts
as born"; they do not show how.

### Next step and cheapest fix

- **Next step:** a throwaway build that checksums every `m_paszUnitNames` array and `m_aszGreatPeopleBorn` at each turn
  start and at each great-person `INIT`, to find the first turn and event at which a name changes. Then a hardware
  data breakpoint on that string under a debugger.
- **Cheapest determinism fix:** none can be named until that is known. A workaround (hash the name index instead of
  the string) would hide the symptom, not the cause.
- **Varies:** process to process [L]. Within one process [I].
- **Impact:**
  - the great-work type follows the name (N5), so this is game content;
  - it reached no `stats.db` table in any pair here;
  - it does not change unit IDs, positions or cities, checked with the units dump.

## 5. Epic 1000's "byte-identical" saves (issue 1008)

Read-only from `main:tools/x64-dll/evidence/2026-11-10-save-sorted-1008/`.

**How they were made** (`loadsave.sh`, `save-now.lua`, `report.sh`, `headless-check-1008.txt`):

- A save was loaded in the headless host and saved again at once, before any turn was played (`--lua-probe
  save-now.lua`, which calls `UI.SaveGame("AfterLoad")`, with `--turns <save turn + 1> --no-save-final`). One
  process per DLL. The saves were Continental Drift, Small, Quick, seeds 1008/1808 (`cdrift-small-fixed.json`),
  turns 60 and 150.
- `savediff.py` compares the uncompressed prefix and the whole inflated body byte for byte. Its pairs:
  - **the same save and the same official DLL in two processes:** body equal;
  - **DLL builds from different source** (stock, pins): body equal at turns 60 and 150;
  - **the official DLL against a source build** (`verdiff.py`): equal after skipping the DLL version string at
    body offset 0x14. All source builds used one fixed version string, `w1004-saveident`
    (`update_commit_id.bat`).
- `attribute.py` explains the sorted-write builds' differences: every differing byte lies in a re-ordered
  `[count][records]` container with 8- or 16-byte records. That is hash-order container output, not noise.

**Reconciling the two results.** Both results hold; they test different things.

- 1008 compared **load-then-save outputs of one input save**. Every difference cause in section 2 is state the
  save restores:
  - the GUIDs are read back;
  - the garbage notification, team and religion bytes are read back verbatim;
  - the names are read back.

  So two processes that load the same save write the same body [L for 1008's pairs; C for the causes above].
- This task compared **two games played from turn 0** in two processes. These differ in the GUIDs, in uninitialised
  memory and in great-person names.
- [L] Load-then-save is not a fixed point of a played save. 1301's A1 loaded and saved at once
  (`<pins_dir>\runs\1308-resave\A1-map`) differs from A1 itself in 219 aligned runs, prefix (pregame)
  included. A load recomputes some state; one known case is the espionage lists, which grow by 22 per load
  (civ5-headless `docs\saves.md` section 6).
- **For gate D10** (x64 DLL against x86 DLL saves): 1008's method is the right one, with one input save, both DLLs,
  load then save, and a fixed version string. Two DLLs playing the same game from turn 0 can only match byte for
  byte after:
  - fixing causes G1/G2 (a seeded GUID or a test option);
  - fixing U1-U5;
  - fixing the great-person cause;
  - masking the DLL version string.

## 6. Reproduce

```powershell
$e = "<pins_dir>\evidence\1308"
# exact per-statement comparison (needs both save maps)
python $e\exactcmp.py "<M1 save>" <M1 map> "<M2 save>" <M2 map>
# 1301's runs onto the save-mapped pair
python $e\attribute_runs.py translate "<1301 A1 final>" "<1301 A2 final>" "<M1 final>" "<M2 final>" <M1 savemap-001.txt>
# a new save map: apply the patch to a Release-5.4.8 worktree, build release, run with $env:VP_SAVEMAP = <dir>
python $e\savemap_patch.py <worktree>
```
