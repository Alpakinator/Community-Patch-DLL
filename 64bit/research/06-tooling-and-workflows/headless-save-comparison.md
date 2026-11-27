# Comparing saves from the headless host

How to compare `.Civ5Save` files written by the `civ5-headless` host (`c5h`), and what "byte-identical" means.
It reconciles two results of 2026-11-10 that looked contradictory: epic 1000 task 1008 ("x86 saves byte-identical")
and epic 1300 task 1307 ("two host processes never write byte-identical saves, not even base against base").
Both hold; they compared different things. Written by the librarian 2026-11-10 from the evidence files and the
sessions' transcripts. Bears on decision D10 and open question Q4.

## 1. The two methods

| | Epic 1000, task 1008 (round trip) | Epic 1300, task 1307 (independent plays) |
|---|---|---|
| Input | One save per turn, written by the official 5.4.8 DLL (SHA-256 `4E91C58A...`) as the final save of a new game: run `stock-new60` (60 turns) and run `stock-new150` (150 turns), each its own host process | None: each arm starts a new game |
| What each arm does | Loads that save with one DLL and saves at once, before any turn is played (`--lua-probe save-now.lua`: `UI.SaveGame("AfterLoad")`, `--turns T+1 --no-save-final`; `loadsave.sh`) | Plays a new game from turn 0 to turn 200 with one DLL and writes the final save (`tools/i0/ab.py`; also `--save-at 20`, `--save-at 160`) |
| Processes | One host process per DLL per save. Not one process loading both DLLs | One host process per arm |
| Game | Small Continental Drift, Quick, Ancient, Emperor, seeds map 1008 / sync 1808 (`cdrift-small-fixed.json`), `--no-mods`, toolkit `VP_MODPACK_5_4_8_NEUTRALS` game dir | Standard Continents 80x52, 8 majors, 16 city-states, seeds 1001/2002 (`continents-standard-fixed-seed.json`), `--no-mods`, toolkit game dir |
| Host | civ5-headless copy in the 1004/1008 worker's scratch (`w1004\c5h`, its own `civ5headless.toml`) | `%USERPROFILE%\Documents\civ5-headless` via `tools/i0/ab.py` |
| Compare | Whole file: header and pregame raw, body inflated (`savediff.py`); **no masking**. The DLL builds under test share one version string (`w1004-saveident`), so the version field does not differ; official vs clang build differ only in that string (`verdiff.py`) | Whole file aligned (`savecmp.py`, `savealign.py`); then, for the gate, only the bytes whose writer changed (`m_ReplayData`), the prefix and the embedded save DB, plus cross-loading and continued play |
| Result | Official vs official round trip byte-identical; Release-5.4.8 vs `x64` f3d2f5669 (x86 build) byte-identical at turns 60 and 150 [L] | A/A (base vs base) final saves at turn 200 differ in 43 aligned runs, and in length [L] |
| Evidence | `tools/x64-dll/evidence/2026-11-10-save-sorted-1008/` (`headless-check-1008.txt`) | `<pins_dir>\evidence\1307\` (`gate1-save-noise-aa-vs-ab-t200.txt`); tracker 1307 |

## 2. What differs between two independent plays (1307, A/A, turn 200) [L]

- The 16-byte game GUID, which the game does not seed, and a GUID string.
- Heap pointers and uninitialised bytes in the map and unit sections.
- `TXT_KEY_GREAT_PERSON_*` and "(Great Writer)" name strings (which great person got which name).
- A hash-ordered list before the embedded save database.
- Run lengths shift after the first length difference, so later aligned runs include large blocks; the full list
  goes into `research/03-dll-engine-boundary/save-determinism.md` (epic 1300, on `claude/jolly-kare-987ea6`).

In 1008's round trips none of this differed: the GUID and the names are read from the input save and written back,
and the other bytes came out the same for those two saves [L]. That the uninitialised bytes match after a load is an
observation on those saves, not a guarantee [I].

## 3. Which claim holds when

- **Same input state, serialised by two DLLs** (load one save, save at once, one process per DLL): byte-identical
  whole files are achievable and were measured (1008). This tests the **writers** only. It does not test that the
  two DLLs reach the same state when they play.
- **Two plays from a new game**, even with the same DLL and the same seeds: whole files are **not** byte-identical
  (1307). Game outcomes still are: the same A/A pair's `stats.db` is identical (1301: 15 tables, 356,105 rows, 200
  turns, with a run-uuid column excluded), so behaviour is deterministic but the save carries per-process noise.
- Therefore "saves are byte-identical" must always name the method. Without one, it is ambiguous.

## 4. A usable test for D10 (proposal, for the MC to adopt)

**Adopted 2026-11-10 as decision D24 and built as `tools/d10/run_d10.py` (issue 2403; findings in
`d10-save-identity-test.md`).** Changed from the proposal below: part 3's "identical `stats.db`" is gated as "the
other DLL's continuation of a save equals the owner's continuation of the same save"; comparing a continuation with
the direct play, or the two arms' continuations of their own different early saves, fails with one DLL alone
(d10-save-identity-test.md section 2) and is reported but does not gate. (Replaced: "proposal, for the MC to adopt"
as the status of this section.)

1. **Writer identity:** pick input saves (a fresh game at an early and a late turn, and the huge save). For each,
   load it with DLL A and with DLL B, each in its own process, and save before any turn. Compare the whole file
   (header raw, body inflated) with no masking. Pass = identical, except the DLL version string when the builds
   carry different ones. With D18 in only one build, the three hash-ordered containers may differ in order only.
2. **Behaviour identity:** play both DLLs from the same new game and seeds for N turns; pass = identical `stats.db`,
   and for each save, the cross-load continuation of step 3.
3. **Cross-load:** each DLL loads the other's save and continues; pass = identical `stats.db` and identical
   round-trip saves under step 1.

Comparing final saves of two independent plays byte for byte is not a valid test: it fails A/A.

Note: the turn-160 save of the 1307 Standard game crashes one turn after loading in the host, with the stock DLL
too (divide by zero, `CvReligionAI::ScoreBeliefForPlayer`, tracker 0078). Pick input saves that load and continue.

Sources: `tools/x64-dll/evidence/2026-11-10-save-sorted-1008/` (`loadsave.sh`, `save-now.lua`, `savediff.py`,
`verdiff.py`, `report.sh`, `headless-check-1008.txt`, `cdrift-small-fixed.json`); the epic 1000 worker transcript
`agent-ac53479488da101df` (runs `stock-new60`, `stock-new150`, 2026-11-10); tracker 1307 on `claude/jolly-kare-987ea6`;
`<pins_dir>\evidence\1307\gate1-save-noise-aa-vs-ab-t200.txt`; `../03-dll-engine-boundary/x64-dll-port.md`
section 4.1.
