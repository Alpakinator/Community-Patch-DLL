# The D10 save-identity test (decision D24) as a tool

`python tools/d10/run_d10.py --out DIR` runs the three-part test of decision D24 offline, with the 32-bit headless host
(c5h) for the x86 DLL and the server for the x64 DLL, and exits non-zero on any failure. Usage, defaults and the exact
rules: `tools/d10/README.md`. Evidence: `tools/d10/evidence/2403/`. Issue 2403 (team 2400, worker B), written
2026-11-10. This file records what the tool established and the design decisions behind it; it adopts
`headless-save-comparison.md` section 4 with the changes in section 2 below.

## 1. What the tool compares

- **Part 1, writer identity** (`tools/d10/savecmp.py`): for every input save, the x86 resave against the x64 resave,
  header raw, pregame raw, inflated body, nothing masked. Exception (a) `dll-version-string`: the u32-length string at
  inflated-body offset 0x14 (`CURRENT_GAMECORE_VERSION`, written by `CvGame::Write` after the save version and the
  data hash) and the header's `dllVersion` field (always `1.0.0` in every save seen so far [L]), cut out of both files
  only when they differ. Exception (b) `d18-container-order`: when exactly one writer has D18, each remaining differing
  run must lie inside the records of one **anchored** container (below) with the same count, the same multiset of whole
  records and strictly ascending keys on the D18 side. D18 is detected from the version string's commit: `git
  merge-base --is-ancestor e9d0c4837 <commit>` in the fork (e9d0c4837 is the D18 commit; it is an ancestor of x64
  29f15ac38 and its successors, not of i0-ship [C, 2026-11-10]). **Corrected 2026-11-10 22:55 EST (one-off 9013):**
  i0-ship now carries D18 as the cherry-pick 40ce81aee (decision D32), which the ancestry check does not see: the
  auto detection reports the i0-ship builds from 40ce81aee on as "no D18", so their runs pass `--d18-x86 yes`
  (with `--order-exception none` the detection decides nothing; it only labels the report). **Corrected 2026-11-10 (one-off 9011):** the containers were
  found by searching up to 200 KB back from each difference for any ascending-key stretch of 4, 8 or 16-byte records,
  and named by record size; that accepted 18 of 18 synthetic reorderings outside every real container (section 4a).
  They are now located from the save's structure before anything is compared (`savecmp.anchors`) [C source, L]:
  - `CvGame::m_AdvisorMessagesViewed`: a walk of `CvGame::Serialize` (`CvGame.cpp:10835-10887` at 29f15ac38, the same
    lines at e6f904ad6): 33 int fields, 6 bools, one `PlayerTypes`, 5 bools = 147 bytes after the version string, then
    `[u32 count][count x u32]`; `m_eHandicap` and `m_ePausePlayer` follow (checked: pause player at body 212 for a
    33-character version string, as section 3 found).
  - `CvExplorationPlotSet::m_aValues`, three per player (`m_vPlotsToExploreLand/Coast/Ocean`): `CvPlayer::Serialize`
    writes `CvPlayerPolicies` then `CvEconomicAI` (`CvPlayer.cpp:45839-45840`); the policies end with
    `m_aiPolicyModifiers`, a `vector<int>` of `NUM_POLICY_MODIFIER_TYPE` = 60; `CvEconomicAI::Serialize`
    (`CvEconomicAI.cpp:345-369`) writes N bools, N `m_paiTurnStrategyAdopted` ints (-1 or a turn), the three sets
    (`[u32 count][count x (int plot, 3 x int)]`), 10 ints and `m_RequestedSavings` (12-byte records, type a small
    enum); `CvCitySpecializationAI` starts with 4 bools. N (economic strategies) is the unique value for which exactly
    64 such blocks parse; the blocks in body order are players 0-63. Measured: N = 37 on the tiny, standard and huge
    saves (VP 5.4.8, also with CP v151 + VP v17) [L].
  - `CvCity::m_miInstantYieldsTotal`, one per city: `CvCity::Serialize` (`CvCity.cpp:32424-32434`) writes
    `m_aiEconomicValue` (63 ints), two `std::map` (8-byte records), the instant-yields map, four yield vectors, a
    resource vector of fractions, a yield vector and a resource vector; the city blocks are those whose
    (yield, resource) counts occur exactly `CvGame::m_iNumCities` times (the 10th int of `CvGame::Serialize`).
    Measured: 17 yields, 57 resources; 12 / 26 / 124 city blocks on tiny T25 / T100 / huge T350, equal to
    `m_iNumCities` [L]. Chance matches with other counts exist (two on the huge save, (2,2) and (5,5)) and are dropped.
  Reports name containers by location ("player 2 CvEconomicAI::m_vPlotsToExploreCoast", "city block 7
  CvCity::m_miInstantYieldsTotal") with body ranges. `--d18-side neither` (MC decision 2026-11-10, recorded with D18 in
  the decision log): when neither writer has D18 the anchored containers compare as multisets (any order); used only
  for the round trip of a pre-D18 x86 save (part 3). Bytes after a final zlib block are compared raw; the game ends
  its body stream with a sync flush (`00 00 ff ff`), not a final block, in every c5h and server save checked [L], so a
  real save carries none. `tools/d10/tests/` (unittest, about one minute) holds the verification's negative cases;
  `selftest.py` now permutes an anchored container.
- **Part 2, behaviour identity** (`tools/d10/statscmp.py`): the two direct plays' `stats.db`, every table and column in
  rowid order. Skipped and listed in every report: `Mem*`, `run_dictionary`, `sqlite_*`, `RunId`,
  `uuid_dictionary.uuid_hex`. `MostEndangeredCity` and `IsReligionOwner` are compared. Since 9011 cells compare with their storage type
  (INTEGER 5 is not REAL 5.0), and a comparison is accepted only if the seven per-turn tables (`WorldStateLog`,
  `civ_turn_era`, `MapPlotsState`, `MapUnitsState`, `MilitarySummary`, `BuildingYields`, `BuildingsOverview`; each has
  rows for every turn of the D10 games [L]) are present and have rows for every turn of the run's range on both sides.
  A host run whose `CvAssert.log` holds a `SqliteLogger:` assert is failed ("stats.db lost rows", section 4b.2).
- **Part 3, cross-load**: gates (i) for each arm's early save, the other DLL's continuation against the owner's
  continuation, whole `stats.db`; (ii) the round trips. **Corrected 2026-11-10 (9011):** the round trips were the
  part 1 comparison of the same input (one hop). Now, for every input one arm W wrote, W resaves its own part 1 resave
  (W->W->W) and the other DLL's part 1 resave (W->O->W), both loaded from one slot path, and the two are compared (W with
  D18: nothing excepted; W without: `--d18-side neither`). Equal hops and an equal load path are required because the
  pregame stores the absolute path of the loaded file and a load-save is not a fixed point (a second load-save inserted
  39,464 bytes at body 2.52 MB on tiny T25, both DLLs) [L]. Section 2 gives the comparisons that gate only when an A/A
  control passes. `--order-exception none` turns every order exception off (the gate for two builds that both carry
  D18, MC 2026-11-10 after D32); `auto` (default) already uses none between two D18 builds.
- **Verdict "identical content"**: header, pregame and inflated body identical but the zlib stream differs. Measured
  [L] for the server-written early saves (Standard x64-T50: 476,691 vs 476,692 B, first differing file byte 473,794,
  the c5h resave shorter; Huge x64-T375 likewise). D24 compares the inflated body, so this passes; the encoder
  difference itself is not explained here (server-host.md section 9 has the server's zlib 1.2.3 flush rule) [I].

## 2. Two comparisons of the brief fail with one DLL alone (A/A), so they cannot gate [L]

Measured 2026-11-10 with the same-source control builds (section 3): the x86 and x64 builds of one commit.

| Comparison asked for | A/A control (one DLL on both sides) | Control result (Tiny / Standard / Huge) |
|---|---|---|
| x86's continuation of x64's early save vs x64's continuation of x86's early save | x86 continuing x86's early save vs x86 continuing x64's early save | DIFFERENT in 13/15, 14/15, 13/14 tables |
| each continuation vs the direct plays over Turn >= early | each DLL continuing its own early save vs its own direct play | DIFFERENT in 13/15, 14/15, 13/14 tables (both DLLs) |

Causes [L, partly I]:
- **A continuation is not the direct play.** Tiny, x86, continuation of its own turn-25 save vs its own direct play,
  rows with Turn >= 26: every table with per-turn rows differs; `BuildingYields` rows come in another order from turn
  26 (same building set, other iteration order after the load), and the multisets first differ at turn 37
  (`BuildingYields`), 38 (`BuildingsOverview`), 46 (`BuildingInstantYields`). Rows of the save turn itself (Turn 25)
  are written by the direct play before the save point and not by the continuation. The likely cause is state the
  DLL rebuilds after a load instead of reading it from the save, which steers the AI differently [I: which state is
  not established; leaks-and-growth.md, bug 0078, documents one such field].
- **The two arms' early saves are not the same state.** c5h's `--save-at T` saves "at the autosave point of turn T"
  (civ5-headless `host/driver/HHMain.cpp`) in a running game, the server's early save is the end of a separate run to
  turn T; the c5h saves carry pregame active player 4 (Tiny) / 8 (Standard, Huge) where the server's carry 0 (server
  reports, "loaded game at turn 25, active player 4"). Continuations of the two differ from turn T on.

What does hold [L]: **both DLLs continuing the same save give identical statistics**, on all six saves (Tiny 25,
Standard 50, Huge 375, each arm's), 27,044 to 357,049 rows each. The tool therefore gates part 3 on "other DLL vs
owner" for each save (D24: "each DLL loads the other's save and continues identically") and still runs the two
comparisons above, reporting them as gating only if their control is identical (so they become gates automatically
if load-continue ever equals direct play). (Proposal for the coordinator: confirm this reading of D24.)

## 3. Control run: same-source builds (2026-11-10 13:15-13:44 EST) [L]

Builds of worker A (issue 2402; `tools/d10/evidence/2402/README.md`): x86 `d0d4d405...3ebea2` and x64
`8a52094a...b2015d` from fork 3cfa98347 + the 32-bit replay count fix, server.exe `06058acb...dc1111`. Both write
`Release-5.4.8-17-g3cfa98347 Dirty`, both have D18, so neither exception applies. Evidence:
`tools/d10/evidence/2403/control-same-source-tiny/`, `control-same-source-standard-huge/`.

| Game | Part 1 | Part 2 | Part 3 gates |
|---|---|---|---|
| Tiny 0-100, early 25 | 3 of 4 inputs identical; **x86-T25 differs in 1 byte** | identical, 15 tables, 30,553 rows | both identical (27,044 / 27,556 rows); round trip x86-T25 differs (same byte) |
| Standard 0-200, early 50 | 3 of 4 identical (x64-T50 identical content); **x86-T50 differs in 1 byte** | identical, 15 tables, 348,086 rows | both identical (334,989 / 347,818 rows); round trip x86-T50 differs |
| Huge 350-400, early 375 | input-T350 identical, x64-T375 identical content; **x86-T375 differs in 1 byte** | identical, 14 tables, 682,731 rows | both identical (354,621 / 357,049 rows); round trip x86-T375 differs |

**The one byte: `CvGame::m_ePausePlayer`** (inflated-body offset 212 for these version-string lengths;
`CvGame::Serialize` right after `m_eHandicap`) [C by field walk, L]. Every resave is written while the game is paused
by the host's load sequence. c5h pauses the pregame's active player (`host/setup/HHNewGame.cpp:923`, as the EXE's
`LoadScreen.lua`, engine-contract finding 10); the server pauses player 0 (`server/src/srv_main.cpp:575`,
`SetPausePlayer((PlayerTypes)0)`). The inputs whose active player is not 0 are exactly c5h's mid-run saves (active
player 4 or 8), so x86 writes 4/8 and x64 writes 0 there; everywhere else both write 0. A host difference, not a DLL
one: **the server should pause the loaded game's active player** (a one-line server change; `server/` is not this
issue's to edit, reported to the team-2400 coordinator 2026-11-10).

Also found here [L]: c5h writes the header's DLC list from its game folder (civ5-headless
`host/setup/HHSaveGame.cpp` `SaveDlcList`), the server keeps the loaded save's list, so a c5h resave of a
server-written new-game save (14 entries: no `DLC_SP_Maps`, no Vibrant Textures) lists 18 entries on the toolkit
folder. The tool gives every x86 run that loads a save a copy of the toolkit folder without the DLC folders whose
packages the save does not list (x64 new-game saves: without `DLC_SP_Maps` and `VT`; the huge save: without
`DLC_SP_Maps`, `VP_MODPACK_5_4_8_NEUTRALS`, `VT`). With that the headers are identical on all inputs above. This
generalises server-host.md 12.2 row 4.

Run times (wall, all games in parallel, `--jobs 10`, this machine shared with builds): Tiny alone 150 s (13 host
processes); Standard and Huge together 1,528 s (25.5 min): Standard x86 0-200 798 s, x64 704 s; Huge x86 350-400
1,031 s, x64 947 s; the longest chain is Huge (x64 early 350-375 458 s, then the continuations 375-400, 349-534 s).
Disk: about 0.9 GB for Standard + Huge after the automatic cleanup (1.15 GB of server database copies deleted).

## 4. Pinned builds (increment 0 ship x86 vs x64 29f15ac38)

Run 2026-11-10 15:43:56-16:11:29 EST (1,653 s wall, all three games in parallel, `--jobs` default) by the team-2400
coordinator, `python tools/d10/run_d10.py --out %USERPROFILE%/AppData/Local/Temp/claude/e2400/d10-pinned`, exit 0 [L].
Builds: x86 `i0-ship` e6f904ad6 (SHA-256 `2c216fc9...f8cc`, version string `Release-5.4.8-11-ge6f904ad6 Clean`, no D18),
x64 DLL `x64` 29f15ac38 (`e3f5a4de...b67f`, `Release-5.4.8-18-g29f15ac38 Copy`, D18) with server.exe `21b0fbc5...b291`
(server/ of the epic 2400 branch, with the two section-12 changes of `server-host.md` and the pause-player change).
Report and compare outputs: `tools/d10/evidence/2405/pinned/`.

| Game | Part 1 writer | Part 2 behaviour | Part 3 cross-load (gates) |
|---|---|---|---|
| Tiny 0-100, saves 25 and 100 | pass: 4 inputs identical under exceptions (a) and (b) | pass: 15 tables, 30,553 rows | pass: other-DLL vs owner continuations identical (27,044 and 27,556 rows); 4 round trips identical under exceptions |
| Standard 0-200, saves 50 and 200 | pass: 4 inputs | pass: 15 tables, 348,086 rows | pass: 334,989 and 342,572 rows; 4 round trips |
| Huge copy 350-400, input 350 and saves 375 | pass: 3 inputs | pass: 14 tables, 682,731 rows | pass: 354,621 and 357,049 rows; round trips |

- Both exceptions were used on every input, and only those: (a) the version string (the builds differ), (b) the D18
  order. Every byte range under (b) was attributed to `CvExplorationPlotSet::m_aValues` records (count unchanged, same
  16-byte records, the x64 side sorted by key); the report lists each container with its body ranges.
- Part 2 skipped only `sqlite_sequence` and the column `uuid_dictionary.uuid_hex` (a per-run id), as listed in the
  report. `ReligionBeliefYields.IsReligionOwner` and `MilitarySummary.MostEndangeredCity`, excluded by earlier tools,
  were compared and are identical in these runs.
- The non-gating comparisons of section 2 differ as in the control run (their single-DLL controls differ too).
- The report's `locks:` line shows `deps/i0.lock` as it was when the run started (the old b9311e2ee pin, re-pinned to
  e6f904ad6 two minutes later); the DLL hash line is the authority for what ran.
- Corrects the statements that x86 and x64 agree only "with throwaway builds" (`x64-dll-port.md` 9.6-9.7,
  `server-host.md` 10-11): with the pinned builds they agree on Tiny 100, Standard 200 and the huge save 350-400.

## 4a. Independent verification, 2026-11-10 17:44 EST (MC, from `tools/d10/evidence/verification-2400/`) [L]

The full test was re-run from main e2e7ab7 on the pinned builds with a fresh copy of the huge save: PASS on all three
games and parts. Real differences are caught end to end (the stock build and i0-bugfixes against i0-ship fail from turn
34). **History (2026-11-10, one-off 9011): every limit below is fixed in the tool (sections 1 and 4b); each negative case
is a test in `tools/d10/tests/` that fails on the tool of main 88485af and passes now.** The limits as found:

- Exception (b) is too wide: `savecmp.find_d18_container` accepts any stretch of ascending-key records within 200 KB
  before a difference, so 18 of 18 synthetic reorderings outside the real container passed. Container names in the
  reports are assigned by record size, not by location.
- `statscmp` passes two databases with all tables and zero rows.
- Bytes after the end of the compressed stream in the last chunk are not compared.
- The part 3 "round trips" are one hop (the part 1 comparison of the same input), not there and back.
- The same DLL run twice gives saves that differ in 1-3 KB (GUID strings and two buffer areas) [L]; one part 3 gate
  compared 342,572 rows in one run and 347,818 in another, identical both times.

## 4b. One-off 9011: the stricter tool on the real builds, noise, option A, speed (2026-11-10, 17:45-20:53 EST)

Issue `tracker/issues/9011-d10-tool-holes-and-speed-baseline.md`, branch `worktree-agent-a33fdcdcdbec2bcb1`. Evidence:
`tools/d10/evidence/9011/` (reports, test outputs, speed). Builds as section 4 unless named.

### 4b.1 Full re-runs on the pinned builds [L]

**Run 2, alone, final tool: PASS, exit 0** (2026-11-10 19:07:04-19:34:15, 1,632 s wall; nothing else of this session
ran; other agents' processes on the machine not controlled). `run_d10.py --out .../o9011/full-pinned2`, builds and locks
as section 4 (x86 `2c216fc9...f8cc`, x64 DLL `e3f5a4de...b67f`). Report: `tools/d10/evidence/9011/full-pinned-run2-alone-report.txt`.

| Game | Part 1 (exception (b), by member) | Part 2 | Part 3 gates | Real round trips |
|---|---|---|---|---|
| tiny 0-100 | 4 inputs pass; 12 `m_aValues` containers each (3.0-5.0 KB) | 15 tables, 30,553 rows | 27,044 / 27,556 rows identical | x64 2/2 byte-identical; x86 2/2 under `neither` (12 containers, 1,053 / 2,340 B) |
| standard 0-200 | 4 pass; T50: 24 `m_aValues`; T200: 23 `m_aValues` + 16 `m_miInstantYieldsTotal` | 15 tables, 348,086 rows | 334,989 / 347,818 rows identical | x64 2/2 byte-identical; x86 2/2 under `neither` (24 / 22 containers) |
| huge 350-400 | 3 pass; 16-17 `m_aValues` + 87-89 `m_miInstantYieldsTotal` (9.8 KB) | 14 tables, 682,731 rows | 354,621 / 357,049 rows identical | x64 byte-identical; x86 under `neither` (16 containers) |

Every per-turn table covered every turn of every comparison. No SqliteLogger failure in any run.

**Run 1, side by side (failed, kept as evidence)**: 18:15-18:50 together with the option-A run and two LTO builds:
huge part 1 failed on a defect of the first version of the anchors (65 economic blocks, chance city matches; fixed,
the same pairs then pass) and the standard gate failed on the server's lost statistics rows (4b.2). Report:
`full-pinned-run1-sidebyside-report.txt` (the tool before commit 51c232c).

**So the real builds pass the stricter test**, with one condition: the x86 round trip needs `--d18-side neither`,
because i0-ship (without D18) reorders `m_aValues` records after a load. With the sorted writes on the x86 side
(option A, 4b.4; decision D32) no order exception is needed anywhere. (Superseded 2026-11-10 by section 4c: the pins now carry
D32 and pass with no order exception.)

### 4b.2 What the stricter tool sees on real data [L]

- Exception (b) on real data, now by location: tiny and standard use only `m_aValues` (12 containers, players 0-3, on
  tiny T25/T100); the huge save uses both members: 16-17 `m_aValues` containers and **87-89
  `m_miInstantYieldsTotal` containers** per pair (input-T350, x86-T375, x64-T375; about 9.8 KB of reordered bytes).
  `m_AdvisorMessagesViewed` has count 0 in every save (autoplay), as before.
- **Real round trips** (W->O->W against W->W->W, one slot path): x64 saves come back **byte-identical** (whole file).
  i0-ship x86 saves come back identical except the record order inside the anchored `m_aValues` containers
  (tiny T25: 12 containers, 1,053 B; T100: 12, 2,340 B): VC9's `tr1::unordered_map` iteration order follows insertion
  history and i0-ship lacks D18, so neither side is sorted. The MC allowed `--d18-side neither` for exactly this
  (2026-11-10; the test fails without it). Without the new mode the stricter test fails on the real builds there.
- **A load-save is not a fixed point**: a second load-save of tiny T25 inserts 39,464 bytes at body 2.52 MB and changes
  scattered fields (both DLLs), and the pregame stores the absolute path of the loaded file (8,062+: `...\inputs\
  D10-TINY-x64-T100.Civ5Save`). Round trips therefore compare equal hops loaded from one path.
  *Explained 2026-11-11 (one-off 9020):* 39,424 of the 39,464 bytes are the espionage AI's per-civ vectors, which every
  5.4.8 DLL appends after their defaults on load (upstream 30a2349ed fixes it); with that fix a second hop adds 40
  bytes. Still not a fixed point. `../03-dll-engine-boundary/upstream-fix-batch-1.md` section 3.
- **The x64 server lost statistics rows under load** [L]: in the two full runs made side by side with two LTO builds
  (18:15-18:51, one link died of out-of-memory), the standard gate "x64 continues x86-T50 vs x86 continues x86-T50"
  failed: the server's `CvAssert.log` had `SqliteLogger: failed to execute insert` (`SqliteLogger.cpp:503` at
  29f15ac38) and its `stats.db` lacked the `MilitarySummary` rows of Vatican City and Hanoi at turn 185 (other run:
  America at 176, plus one `BuildingInstantYields` row). The other tables were identical and the x86 continuation was
  identical across runs (334,989 rows), so the game agreed and a write was lost. The SQLite error code is not in any
  log (the logger asserts without `sqlite3_errcode`; the server's database layer logs nothing) [not established]. Bug
  0084 (MC). The tool now fails a host run whose `CvAssert.log` has a `SqliteLogger:` assert, with that reason.

### 4b.3 Run-to-run noise, named [C source + L]

The same DLL playing the same spec twice (tiny, `o9011` runs `tiny-main` and `tiny-new`, 2026-11-10; verification's
standard 50 for the 12.9K stride) writes saves that differ in these fields only; the statistics of the two plays are
identical (tiny, all four play runs, 30,553 / 27,556 / 27,044 rows):

| Body place (tiny T100; std 50) | Field (source) | Cause | Section in save-determinism.md |
|---|---|---|---|
| 507-byte stride, one per pantheon (tiny T100 12,965+; std 50 12,912+) | `CvReligion::m_iHolyCityX/Y` (`CvReligionClasses.cpp:185-208`) | uninitialised in the pantheon constructor: stack garbage (x64 values such as `0x65BBB370`, `0x25B`) | U3/U4 |
| 36 chars (tiny 20,229; std 21,141) | `CvGame::m_strGameId` (`CvGame.cpp:10975`) | `CoCreateGuid` | G1 |
| 16 B right after the embedded save database (tiny 23,371; std 24,283) | `CvMap::m_guid` (`CvMap.cpp:1789`) | `CoCreateGuid` | G2 |
| 2-27 B near tiny 2.58-2.61 MB (std x86 5.24-5.30 MB) | `CvTeam` arrays of 80, elements 64-79 [I by position and pattern] | heap garbage, reset loops stop at 64 | U5 |
| 1-4 B in a 41-byte stride near tiny 3.94 MB (std 6.60 MB) | `CvNotifications` unused slots `m_iExtraGameData`, `m_ePlayerID` | heap garbage (the x86 values are code bytes) | U1/U2 |
| 1 B at tiny x64 T100 body 100,881 (77.5 KB into the plot data) | a `CvPlot` field, not named [I] | not established | - |

Why none of them can move a gate: part 1 and the round trips compare resaves of **one** file, and every field above is
read back verbatim on load (save-determinism.md section 5), so both resaves carry the same bytes; measured on every
pair of 9011 (no unattributed difference). The part 3 gates compare two continuations of **one** file. What differs
between runs is the input file itself (the x64 early save is regenerated by every run), so a gate's row count can
change from run to run: "x86 continues x64-T50" gave 342,572 rows in run 2405 and 347,818 in the control run, the
verification and both 9011 runs; each run compares its own input, whose SHA-256 the report prints. Which input
difference moved the 2405 game is not established [I: the garbage fields are not read by game logic per
save-determinism.md; the great-person name divergence N1-N5 is process-dependent within a continuation].

### 4b.4 Option A: i0-ship with the sorted writes (scratch preview of decision D32) [L]

Throwaway `git worktree` of the fork at e6f904ad6 under the scratch folder, `git cherry-pick --no-commit e9d0c4837`:
**applies cleanly** (auto-merge of `FirePlace/include/FireWorks/FStlContainerSerialization.h`, no conflict). Release
build (clang-cl, `build_vp_clang.py`), SHA-256 `a85e67cb...8a5e`, version string `Release-5.4.8-11-ge6f904ad6 Dirty`.
Nothing committed or pushed; worktree removed.

- `run_d10.py --x86-dll <it> --d18-x86 yes` against the pinned x64 (18:15-18:51, side by side with the pinned run):
  **part 1 identical apart from the version string on all 11 inputs (tiny 4, standard 4, huge 3), no order exception
  used**; **all 10 real round trips byte-identical (whole file), both directions**; parts 2 and the gates pass except
  the standard gate hit by the server's lost rows (4b.2).
- Same DLL, same input, two processes, one load path: byte-identical (tiny T100, standard T200, huge T375; i0-ship too).
- An x86 resave is a **fixed point in the D18 containers**: hop 1 against hop 2, 219/219, 257/257, 317/317 anchored
  containers byte-identical (38, 87, 140 non-empty). i0-ship: 207/219 and 236/257 (12 and 21 `m_aValues` containers
  reorder on the second hop). The rest of the body still changes between hops (4b.2).

### 4b.5 Speed of the 32-bit build (part B) [L]

Standard Continents fixed seed (`continents-standard-fixed-seed.json`), turns 0-200, c5h `--no-mods`, toolkit game
folder; one run at a time, arms in a fixed rotation, two rounds, 2026-11-10 19:34-20:33 (x86) and 20:33-20:49 (x64);
nothing else of this session running; other agents' processes not controlled: total CPU sampled every 30 s by
`Get-Counter`, means 11-29 % per run, peaks up to 74 % (one sample) on this 32-thread machine. Script and raw data:
`tools/d10/evidence/9011/speed.py`, `speed.json`, `speed-x64.json`. "Off" = a copy of the pinned database
(`bd9b8aa1...`) with `CustomModOptions.SQLITE_LOGGING = 0`, passed with `--db` (no `stats.db` is written; checked).
Builds made in the scratch worktree from the fork's Release-5.4.8 7307aefdf with this session's clang toolchain:
`base9011` as is (`0fbe26b0...1ef7`), `baseP9011` = the same plus exactly the two pragma lines of the baseline tree
(`verification-2400/baseline-dll-source-dirty-diff.txt`; `71db6753...5f7b`). `official` = 4e91c58a (the toolkit's DLL,
built from the pragma tree), `ship` = i0-ship e6f904ad6 (`2c216fc9...`), x64 = the pinned server.

| Arm | Logging | s/turn 1-200, run 1 / run 2 | mean | s/turn 2-33 mean |
|---|---|---|---|---|
| official 4e91c58a (pragmas) | on | 1.192 / 1.393 | 1.292 | 0.555 |
| base9011 (5.4.8, no pragmas) | on | 1.560 / 1.725 | **1.643** | 0.676 |
| baseP9011 (5.4.8 + the two pragmas) | on | 1.130 / 1.273 | **1.201** | 0.470 |
| ship i0-ship (no pragmas) | on | 1.538 / 1.574 | 1.556 | 0.714 |
| base9011 | **off** | 1.176 / 1.282 | 1.229 | 0.472 |
| ship | **off** | 1.147 / 1.280 | 1.214 | 0.541 |
| x64 server 29f15ac38 (no pragmas) | on | 1.353 / 1.361 | 1.357 | 0.579 |
| x64 server | **off** | 0.851 / 0.831 | **0.841** | 0.283 |

- **Confirmed: the two pragmas are the whole gap.** The same source with only the two lines added goes from 1.643 to
  1.201 s/turn (-27 %) and lands at or below `official` (1.292); with logging off, base (1.229) and ship (1.214) equal
  baseP and official within the run-to-run spread (about 0.1-0.2 s/turn between the two rounds of one arm). The
  difference is the cost of SQLite's default `synchronous = FULL` / `journal_mode = DELETE` on every statistics commit
  (flush to disk, journal created and deleted), about 0.35-0.45 s per turn on Standard.
- **A player without statistics logging sees no gap**: `SQLITE_LOGGING` is 0 in VP 5.4.8's
  `NewCustomModOptions.xml`; only the toolkit's `autoplay` mod sets it to 1 (`Mods/autoplay/game_options.sql`). With
  it off, i0-ship plays as fast as the baseline (1.214 vs 1.229 s/turn).
- **Why the x64 server was "at par"**: it pays the same logging cost (1.357 on vs 0.841 off, 0.52 s/turn; its SQLite
  is 3.7.17 with the closed library's defaults, `tools/x64-dll/cvgamedatabase/build_db.py`), but its compute is about
  30 % faster than the 32-bit build (0.841 against 1.21-1.23 off), so with the logging cost on top it lands where the
  32-bit build with the pragmas lands. It is not at par: without logging it is the fastest arm.
- No `SqliteLogger:` assert in any of the 16 timed runs (`CvAssert.log` of each). The lost rows of 4b.2 happened only
  under the side-by-side load; the error code is not logged (4b.2). The x64 database layer's busy handler gives up after
  30 attempts of 2 ms (`tools/x64-dll/cvgamedatabase/src/Database.cpp:31-39`), so a lock held for about 60 ms on the
  statistics database fails the insert [I: candidate cause of bug 0084, not verified].
- Corrects tracker 2404's speed line (ship 1.671 against "stock" 1.335): the 25 % is the statistics database's disk
  syncs, present in every build without the pragmas (our builds of unmodified 5.4.8, i0-ship and the x64 server), not a
  slowdown of the fork's code.

**Recommendation** (no DLL change shipped by 9011): put the two pragmas into `SqliteLogger::EnsureOpen` on the fork's
`x64` branch and the 32-bit ship branch (the baseline tree's exact lines, which already carry the reason in a comment).
They change no game state and no save byte (`stats.db` is not part of the save), only durability of the statistics file
on an OS crash; every D10 and timing run uses statistics logging, so test time drops by about a quarter on x86 and
about 0.5 s/turn on x64 (Standard). Players with logging off (the default) are unaffected either way. Whether the
pragmas also remove the lost-row failure of bug 0084 is open (fewer journal files to collide with) [I].

## 4c. One-off 9013: D32 and D35 on the pins, no order exception (2026-11-10, 20:54-22:59 EST)

Issue `tracker/issues/9013-sorted-writes-into-i0-ship.md`. Evidence: `tools/d10/evidence/9013/` (fork steps
`fork-branches.txt`, builds `builds.txt`, one report per run, machine samples `machine-*.txt`).

**Fork [C]**: `i0-ship` e6f904ad6 -> 40ce81aee (e9d0c4837 cherry-picked, `git range-diff` '=': patch and message
identical; auto-merge of `FStlContainerSerialization.h`, no hand edit) -> ccfd6dd4d (D35, added on the MC's message
after the owner approved tracker 0085: the two pragma lines and the owner's comment in `SqliteLogger::EnsureOpen`, applied from
`tools/d10/evidence/verification-2400/baseline-dll-source-dirty-diff.txt`; the file is then blob 6cd0ee301, the same
as the owner's local tree). `x64` 29f15ac38 -> e716240a7 (merge of 40ce81aee; tree equal to 29f15ac38) -> c43ea1b9f (merge of
ccfd6dd4d; only the six pragma lines). Both merges: `git show --remerge-diff` empty. All four pushes fast-forward.
Pins: `deps/i0.lock` ccfd6dd4d, `deps/dll.lock` c43ea1b9f. Builds: `<pins_dir>/dlls/i0ship-40ce81ae`,
`x64-e716240a` (step 1), `i0ship-ccfd6dd4` (`3e1a7fc7...00b6`), `x64-c43ea1b9` (DLL `7ea44fc3...6706`, server.exe
`9bbce03e...03be`) (final).

**Strict runs** (`run_d10.py --order-exception none --d18-x86 yes`, all three games, `--jobs` default 12), one at a
time, nothing else of this session running [L]:

| Run | Builds | Result | Tiny 0-100 | Standard 0-200 | Huge 350-400 |
|---|---|---|---|---|---|
| 1, 21:12-21:41 (1,728 s) | i0ship-40ce81ae vs x64-e716240a (sorted writes alone) | **PASS** | 4/4 inputs, 4/4 round trips | 4/4, 4/4 | 3/3, 2/2 |
| 2, 21:57-22:18 (1,251 s) | i0ship-ccfd6dd4 vs x64-c43ea1b9 (plus the pragmas; the pins) | **PASS** | 4/4, 4/4 | 4/4, 4/4 | 3/3, 2/2 |

In both runs: part 1 identical on all 11 inputs with only exception (a), the version string; all 10 round trips
byte-identical (whole file), both directions, no exception; part 2 and the part 3 gates identical with the same row
counts as every earlier run (tiny 30,553 / 27,044 / 27,556; standard 348,086 / 334,989 / 347,818; huge 682,731 /
354,621 / 357,049), so neither D32 nor D35 changes behaviour. No host run had a `SqliteLogger:` assert (the tool
fails such a run; 63 `CvAssert.log` of run 2 searched, none). Other load on the machine (samples once a minute): a
`CivilizationV_DX11` of another team in most samples of both runs; during run 2 a C++ build of another session
(`cl` x16, `lld-link`) from 21:57 to 22:06; total CPU mean 32 % / 38 %, peaks 58 % / 98 %.

**Seconds per turn, Standard, from the host lines of the reports** (12 host processes at once on one disk, so the
logging cost is larger than in the one-at-a-time timing of 4b.5; same `--jobs` in every run):

| Run | x86 direct 0-200 | x64 direct 0-200 | x86 cont. of x86-T50 | x64 cont. of x64-T50 | Tiny x86 / x64 direct | Huge x86 / x64 direct |
|---|---|---|---|---|---|---|
| 9011 run 2 (e6f904ad6 / 29f15ac38, no pragmas) | 4.77 | 4.23 | - | - | 1.31 / 0.96 | 22.1 / 20.3 |
| 9013 run 1 (sorted writes, no pragmas) | 4.58 | 4.20 | 3.88 | 5.40 | 1.04 / 0.85 | 22.6 / 21.4 |
| 9013 run 2 (sorted writes + pragmas) | **1.70** | **1.31** | 1.85 | 1.52 | 0.59 / 0.33 | 14.7 / 12.3 |
| 9013 old-pin comparison (x86 e6f904ad6 no pragmas, x64 c43ea1b9f pragmas) | 2.82 | **1.08** | - | - | - | - |

The pragmas cut the parallel test's Standard time by about 63 % (x86) and 69 % (x64) and its wall time from 1,728 to
1,251 s; within one run the build without them is 2.6 times slower than the one with them (old-pin comparison). Part
of run 2's speed may come from a quieter disk; the other session's build ran during run 2, not run 1 [I].

**Comparisons in the mode that normalises container order** (`--order-exception auto`, x64 = the final c43ea1b9f) [L]:
- **Stock 5.4.8** (x86 = the toolkit's `4e91c58a`, 5.4.8 from the owner's tree, no D18, no D25), 22:19-22:31: part 1 identical
  under (a) and (b) on 10 inputs (tiny 4, standard 4, huge input-T350 and x64-T375): **our saves differ from stock's
  only in the version string and the order inside the anchored D18 containers**. The x86 side's T375 input is missing
  because stock crashed (below). Part 2 differs from turn 34 (tiny) and 38 (standard), as tracker 2404 measured (D25).
  Every stock run that loads a save and plays on crashed: tiny and standard continuations with `0xC0000094` (integer
  divide by zero) at turns 90-96, the signature of bug 0078 [I: not symbolised]; the huge direct play 350->400 with
  `0xC000001D` after turn 354, the trap of bug 0080 (as measured in tracker 0080) [I]. The round trips of the stock
  x86 saves through our x64 pass under `--d18-side neither` (tiny, standard). Report: `vs-stock548-report.txt`.
- **Old x86 pin** (i0-ship e6f904ad6, no D18, no pragmas) against the final x64, 22:31-22:53: **PASS** on all three
  games with exceptions (a) and (b); x86 round trips under `neither`, x64 round trips byte-identical. Report:
  `vs-oldpin-e6f904ad6-report.txt`. The old x64 pin was not run against the new x86: its source equals e716240a7's
  (run 1) [C].

## 4d. One-off 9029: the pins of upstream fix batch 1, the new references (2026-11-11, 14:22-14:40 EST) [L]

Pins since 2026-11-11 14:10 EST: `deps/i0.lock` bbfacca1a (`i0ship-bbfacca1`, `f7c64840...01ac`), `deps/dll.lock`
76992d235 (`x64-76992d23`, DLL `fa75f483...8cec`, server.exe `a1d7bdd6...803b`; that server.exe was replaced the same
day by `3a35330a...` with the corrected bug 0094 fix, which changes only what happens after a game has ended). What the batch changes:
`../03-dll-engine-boundary/upstream-fix-batch-1.md`. Command: `python tools/d10/run_d10.py --out D:/c5-64bit-work/9029/d10
--x86-dll <pins_dir>/dlls/i0ship-bbfacca1/CvGameCore_Expansion2.dll --srv-bin <pins_dir>/dlls/x64-76992d23/bin`
through `quietlock.py run` (one registered job, no hold), all defaults: order exception `auto` (D18 detected on both by
ancestry or cherry-pick), **DLL AI logging off in both arms** (D49; the report now says so in its own line, defect D3
of 9028), `--jobs` 12. Wall **1,092 s** (tiny 189 s, standard 559 s, huge 1,077 s). **PASS**: part 1 identical on every
input with only the version-string exception, every round trip byte-identical, part 2 and the part 3 gates identical.
Report: `tools/d10/evidence/9029/d10-report.txt` (and `.json`).

**The references on the new pins** (statistics rows of the identical pairs; they replace the old pins' counts of 4c and
of 9028's run for any comparison from now on):

| Game | x86 direct = x64 direct | x64 continues x86's early save = x86 continues it | x86 continues x64's early save = x64 continues it |
|---|---|---|---|
| tiny 0-100 (early T25) | 30,666 (15 tables) | 27,008 | 26,644 |
| standard 0-200 (early T50) | **351,478** | **327,041** | **341,206** |
| huge 350-400 (early T375) | **683,123** (14 tables) | **357,675** | **356,423** |

The old pins with the same tool and settings (9028, 2026-11-11 12:29-12:49): tiny the same; standard 351,917 /
330,542 / 344,330; huge 679,543 / 353,928 / 353,676. Tiny is unchanged because no fix of the batch changes it
(batch file, section 2). The new counts equal one-off 9020's run of the same commits (04:07-04:29, `d10-new-report.txt`)
row for row although the x64 DLL is a different build (9029's `fa75f483...`, 9020's `d6e4f881...`): the result does not
depend on the build of a commit [L, two builds]. (The row counts of 4c were taken with AI logging on; the counts with it
off are 9028's and these.)

## 4e. Issue 4254 (epic 0334): the 43-civilisation builds, a 30-major game, and the headers since 0303 (2026-11-15) [L]

**The game `large30`** (tool change on `task-4254-43civ-parity`, `tools/d10/run_d10.py`). The three default games seat
at most 22 majors, so they never use slots 22-42 of a 43 build (`../03-dll-engine-boundary/player-limits.md` 5.2).
- New game, not in the default set: `server/tests/specs/continents-large-30majors-fixed-seed.json` (Continents, Large,
  30 majors, 16 city states, seeds 43017/43018), early save at turn 25, final turn 100, played like `tiny` (no mods,
  the toolkit game folder).
- New option `--major-civs N` (default 22). The game is refused (argparse exit 2, nothing created) when N is not above
  30, and when `--x86-dll` or `--srv-bin` is the 22 default of the locks.
- The x64 arm reads the short spec. c5h's own resolver (`civ5headless/spec.py`) has `MAX_MAJOR_CIVS = 22` as a
  constant and refuses 22 majors or more ("majors must be 1..21"). So run_d10 resolves the spec itself with that
  module, its constant set to N for the call, and gives c5h the resolved file
  (`<out>/inputs/large30-x86-spec-resolved.json`: 43 slots, 1 taken, 29 computer, 13 closed). c5h passes a resolved
  spec through unchanged (`run.py` `resolve_spec`), and its host reads the DLL's count at run time (`HHNewGame.cpp`).
- ALL_GAMES and the default run are unchanged; `tools/d10/tests` 51 passed after the change.

**Result on the 43 builds** [L, 2026-11-15 08:36:50-08:45:24, 514 s, `--jobs 8`, workstation shared with other teams]:
- Builds: x86 = 4252's plain 32-bit 43 DLL `i0ship-26234371-m43` (`6e2a7be1...`, "Release-5.4.8-39-g26234371e+m43
  Copy"); x64 = the 43 layout `x64-04b1211b-m43-p11b2c60/bin` (DLL `3e862705...`, server.exe `db26a0eb...`, built clean
  at repository 11b2c602). Order exception `none` (D18 on both), AI logging off.
- **PASS in all three parts.** Part 1: the four inputs identical with only the version-string exception. Part 2: x86
  direct = x64 direct, turns 0-100, **164,110 rows** in 15 tables. Part 3: the gates are identical, **146,025** rows (x64
  continues x86's T25 = x86 continues it) and **144,085** (x86 continues x64's T25 = x64 continues it), and the four
  round trips are byte-identical.
- The A/A controls differ from turn 25, as they do on the 22 games (section 2).
- Report: `../03-dll-engine-boundary/evidence/0334/4254/d10-run2/report.txt`; command:
  `evidence/0334/4254/d10_large30.sh`.
- **Again on release-43, the build that ships** (MC #1702/#1707). Layout `x64-04b1211b-m43-p9aa30f1`: DLL
  `cf981126...`, server.exe `d529d5d0...`, built at team head 9aa30f19; the same 32-bit DLL.
  [L, 2026-11-15 10:11:49-10:18:55, 426 s]: **PASS in all three parts** with exactly the same row counts
  (`evidence/0334/4254/d10-p9aa30f1/report.txt`).

**The save headers since issue 0303** [C source; L].
- Since 0303 (2026-11-14 20:48, `1bb44085`), `server/run_server.py` writes the release descriptor's mods (CP v151,
  VP v17) into the header of a new game's save by default (`--save-mods descriptor`).
- c5h loads a save that lists mods with `--mods-dir a.mods_dir`. run_d10 makes `a.mods_dir` (a git archive of the
  release tag) only inside the block for the `huge` game.
- **So a run without `huge` fails** wherever c5h loads an x64 save. c5h is called with `--mods-dir None` and refuses:
  "mod d1b6328c... v151 is not below ...\None". This was the first 4254 run, 08:29-08:36: parts 1 and 3 failed, part 2
  passed (`evidence/0334/4254/d10-run1/`).
- A full run is not affected: 0351's PASS of 2026-11-15 06:58-07:32 had all three games. In such a run the x86 arm
  continued the x64 arm's saves with CP and VP as mods from that archive, not with the toolkit's VP modpack that both
  direct plays use.
- **Fix** (`a841b235`): run_d10 passes `--save-mods none` for its new games. The x64 saves of the no-mods games then
  list no mods, like the x86 arm's own saves, and c5h loads them `--no-mods`.
- Harmless for the 22 games [I]: part 1 compares resaves, which keep the loaded save's list; the huge game loads the owner's
  save and is unchanged. Not rerun on tiny and standard with the fix.
- Correction of a first note to the lead (thread 0334, 08:4x), which said every game failed since 0303: that was
  wrong, as the MC's #1641 showed.

## 5. Not verified

- That c5h's load sequence (pause the active player, select a unit) is the EXE's: taken from civ5-headless's
  reconstruction (engine-contract finding 10), not traced.
- Which non-serialized state makes a continuation differ from direct play (section 2).
- ~~Exception (b) on real data~~ (2026-11-10, 9011: exercised on every real pair, both members, section 4b.2).
- ~~The container rule is structural, not a parse~~ (2026-11-10, 9011): the containers are now located from the save's
  structure (section 1). Still not a full walk of the save from its first byte: the economic and city anchors are
  structural signatures checked against independent counts (64 players, `m_iNumCities`), and `m_AdvisorMessagesViewed`
  is reached by a walk of `CvGame::Serialize` only. Its 4-byte rule is still covered by no real save (always empty in
  autoplay games).
- The anchor constants (`NUM_POLICY_MODIFIER_TYPE` = 60, `MAX_CIV_PLAYERS` = 63, the `CvGame` field list) are those of
  29f15ac38 and e6f904ad6; a DLL that changes these serializers needs the constants updated (the anchors then fail
  safe: no exception, the differences are reported).
- The one noisy byte in the plot data (4b.3) is not named; why the x64 early save of run 2405 led to a different
  continuation (342,572 rows) is not established.
- Why the server's statistics insert failed under load (4b.2, bug 0084).

- Whether the pragmas (D35) remove bug 0084's lost rows: no run of 9013 lost rows, with or without them, but no run of
  9013 was made under the side-by-side load that produced 0084 (2026-11-10, section 4c).
- The two stock crash signatures of section 4c (0xC0000094, 0xC000001D) are matched to bugs 0078 and 0080 by exit code
  and turn only, not symbolised.
- `run_d10.py`'s D18 auto detection does not see a cherry-picked D18 commit (section 1); the tool's defaults
  (`DEFAULT_X86_DLL`, `DEFAULT_SRV_BIN`, the README) still name the e6f904ad6 / 29f15ac38 builds (2026-11-10, reported
  to the MC; tools/d10 code not changed by 9013). Corrected 2026-11-13 (issue 0133): the two defaults are read from the
  `release` key of `deps/i0.lock` and `deps/dll.lock`, so they follow every re-pin (`tools/d10/tests/test_pin_defaults.py`).
- The x64 dependency builds are not bit-reproducible: `civ5save.exe`, `CvGameDatabase_x64.dll`, `CvLocalization_x64.dll`
  and `lua51_x64.dll` had other SHA-256 in the two 9013 builds from unchanged sources (`tools/d10/evidence/9013/builds.txt`);
  the cause is not examined.
