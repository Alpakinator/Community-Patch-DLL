# Turn-time and memory study (epic 0437, D163): premises and analysis plan

Started 2026-11-16 17:25 EST by lead-0437 (account A). Design: the Architect's proposal
`research/07-architecture-decisions/architect-2026-11-16-turn-time-study-epic.md` (the owner's words in its section 1).
This file holds the epic's findings until task 0438 merges and `research/08-performance/turn-time-study.md` joins the
epic's touches; then the two are merged into that file and this one keeps a pointer. Marks: `[C]` read in code or
records, `[L]` verified live, `[I]` inferred.

**Status of this plan (2026-11-16 20:06):** reviewed by the performance standards team (ticket 7014, queue entry
2955, 2026-11-16); the review's required changes are adopted in section 9, which **overrides the sections above where
they differ**. Timed runs may start from this version.

## 1. The questions and the order they are answered in

| Q | Question | Package | Publication |
|---|---|---|---|
| Q1 | How do S1-S4 compare on scenarios H and M: seconds per turn, memory per turn? | W3 (tool W2) | report v1 |
| Q2 | Where does a turn's time go in S2 and S4 (DLL, calls each way, transport, EXE alone, save)? | W5 (tools W4) | report v2 |
| Q3 | What can the server buy with memory (caches, tables, idle cores, build)? | W6 | report v3 |
| Q4 | What can be done about the shell, sized by Q2's measured share? | W7 | report v3 |

Q2's result sets Q4's size (Architect 4.6): if the rows "EXE alone", "calls into the EXE" and "calls from the EXE"
together are under 25 % of an S4 turn on both scenarios, Q4 shrinks to one page.

## 2. Arms

| Arm | Host | DLL | Notes |
|---|---|---|---|
| S1t | `c5h` (32-bit headless) with `loc_db`, `game_dir`, `--checkpoint-every=1000`, AI logging off (`--host-arg=--no-game-logging`) | the official Vox Populi 5.4.8 release DLL ("the tag's DLL", `262aee88...`, Release-5.4.8, what a player installs; 0438) | D163's "stock DLL". c5h's per-call counting overhead (~3 % of a huge turn, PERFORMANCE.md 4) stated beside every S1 number |
| S1i | as S1t | `i0-ship` 26234371e (`i0ship-26234371`, `f92881a3...`) | **added by P1 (0438's finding):** the tag's DLL plays a different game from the first turn after the load; i0-ship plays the same game as S3 on both saves |
| S2t | the stock EXE, the tag's DLL installed as a player has it, no proxy | the tag's DLL | needs a way to arm autoplay without the DLL's Lua channel (0438: the turn could not be ended); W0 decides how, or whether S2t runs on M only |
| S2i | the stock EXE with `i0ship-26234371-ia` (`3c2f47da...`), no proxy | i0-ship, instance-aware | the same game as S4 (0438, one turn on both saves) |
| S3 | `server.exe` direct mode (`server/run_server.py --dll-log-flags 0`) | x64 pin 04b1211b8, layout `x64-04b1211b-pfdee656` (DLL 29F58E17, server 5B254430) | the same game as S1i |
| S4 | the product: EXE + proxy + `server.exe` remote mode, every shipped part on | as S3; proxy 75C40CEC | the release candidate current at W3's start, named by hashes; the same game as S2i |

Grade A pairs (one game): **S1i-S3** (bitness, headless) and **S2i-S4** (bitness, client). Pairs across a client and a
headless host are always different games (0438: they diverge at the first turn after the load) and are labelled so;
the "player's stock" pairs S1t-S3 and S2t-S4 are labelled "different game" as well. Per-turn means over 25 turns still
compare the same scenario's workload; they are reported with that label and never as grade A.

Every number names its build by SHA-256 prefix (PERFORMANCE.md 2.1). A newer product state on `main` at W3's start is a
labelled extra arm (Architect 4.1), never a replacement.

## 3. Scenarios

H = Huge Continental Drift, turns 300-325; M = Standard Communitu_79a, turns 150-175; default players, Standard speed,
Emperor (D163, Architect 4.2). The saves come from task 0438 (account B, handed back 2026-11-16 17:28; its section
"Scenarios" in `turn-time-study.md`): `TS0438-H-T300.Civ5Save` (sha256 `308a96bb...`, map as generated 140 x 88, 11 of
12 majors alive) and `TS0438-M-T150.Civ5Save` (`98fc454a...`), in `F:/c5-64bit-work/0438/saves/`, made on the client's
database (fingerprint `02b5faa4...`), Quick Combat and Quick Movement on in all setups. Headless S3 on the workstation
(indication): H 20.9 s/turn (17-31 s), M 1.62 s/turn; one client turn (indication): H 85-92 s, M 17-18 s. Game options
that change what the EXE does (Quick Combat, Quick Movement, UI mod set cp+vp+eui, autosave interval, frame cap, window
size) are written into a scenario settings file next to the save and are identical in S2 and S4. Before any laptop run
the laptop's client database is compared with 0438's `dbinfo.py` fingerprint.

## 4. Metrics (the definitions; ids proposed for `perf/ledger/registry.json`, added by W2 for 7000's review)

| Proposed id | Unit | Definition | Source |
|---|---|---|---|
| `turn.study_s_per_turn@1` | s | mean over the 25 played turns of the per-turn time; a turn is the interval between two consecutive turn completions: the host's own per-turn line in S1 (`turn-times.jsonl`) and S3 (`turn-times.tsv`); in S2 and S4 the difference of the file times of consecutive per-turn autosaves (validated by P2), and in S4 also the server's own per-turn line (remote mode, W2) as a cross-check | run folder |
| `turn.study_turn_s@1` | s | the same per turn (a vector of 25), for the per-turn lines | run folder |
| `turn.study_boot_load_s@1` | s | process start to the first played turn's start; reported apart, never in a turn | run folder |
| `turn.study_cpu_s_per_turn@1` | s | CPU seconds of the host process (S1, S3), the EXE (S2), the EXE and the server apart (S4), per turn | process times at turn boundaries |
| `turn.study_clock_ghz@1` | GHz | effective clock per turn (cycles / CPU seconds, `perf/cputime`), so that the mobile chip's clock drop with busy cores is visible beside wall time | run folder |
| `mem.study_private_mb@1`, `mem.study_ws_mb@1` | MB | private bytes and working set at each turn boundary; S4: EXE, server and their sum | `perf/memsampler` |
| `mem.study_free_va_mb@1`, `mem.study_largest_free_mb@1` | MB | 32-bit processes only (S1, S2 and S4's EXE): free address space and largest free block | `perf/memsampler` |
| `mem.study_growth_mb_per_turn@1` | MB/turn | slope of private bytes over the 25 turns, with its 95 % interval | `perf/memsampler` |
| `turn.end_turn_to_control_s@1` | s | S2 and S4 with a human seat: from the end-turn command to the moment the game hands control back (first `ActivePlayerTurnStart` of the human seat), 25 times per run | W2 |

Q2's budget ids (`budget.*`) are defined in W4's plan addendum and come to 7000 for review before W5's first run.

## 5. Series design (Q1)

- **Where.** The laptop (`quiet@laptop`), through the testbed queue (`tools/testbed/tb.py --host laptop`). Headless
  arms and client arms in one interleaved series per scenario, one process at a time, arm order rotated every round
  (PERFORMANCE.md 2.3). One series per scenario; arms are compared only inside their own series (COMMON 23).
- **Runs.** At least 6 rounds per series (every arm once per round); 12 where a difference under 1 % matters
  (PERFORMANCE.md 4, logged-on machine). P4 can raise the client arms' count (below).
- **Each run.** Load the scenario save, play 25 AI turns (autoplay; the AI plays every seat), stop safely after the
  newest autosave settles. The client arms: 1080p window (D158), harness instance, crash-box watcher (D155), assert
  policy `hold`, frame cap as written in the scenario settings, statistics logging and AI logging as section 6.
- **Load records per run.** total CPU and the busiest other processes, free commit at start and end, effective clock,
  the laptop's power state (AC, power plan as found, never changed), the quiet place's holder.
- **Analysis.** Per run: the 25-turn mean (the observation). Per arm: mean, sd, median, min, max, 95 % interval
  (t distribution, n = runs). Per pair of arms: the difference of means with its Welch 95 % interval and the smallest
  difference the series could detect (80 % power); also the ratio with its interval (delta method). Per turn: the
  median over runs and the inter-quartile range, drawn as lines. Boot and load reported apart.
- **Same game.** Every run keeps its statistics database (or a separate untimed identity run, section 6). Turns are
  paired across arms only up to the first statscmp difference; past it a comparison is labelled "different games" and
  is not grade A (PERFORMANCE.md 2.7). The headless arms are deterministic from one save (`[L]`, 5 of 5 games); the
  client arms are not after a load (`[L]` 2026-11-15) — P4 measures how far.
- **End turn to control** (S2 and S4 only): one run per round with a human seat on autoplay off, 25 end-turn presses
  through the harness, the time to control back each; 6 rounds.

## 6. Settings that are fixed for the whole study

| Setting | Value | Why |
|---|---|---|
| AI logging (`LoggingEnabled`, host `dll_log_flags`) | off in every arm | the player's workload; on changes the game (PERFORMANCE.md 4) |
| Statistics logging (`SQLITE_LOGGING`) | **decided by P1**: on in every arm only if the official DLL carries D35's pragmas or its cost is shown negligible on S1; otherwise off in timed runs and identity taken from separate untimed runs with logging on | identity needs the statistics tables; the official 5.4.8 DLL predates D35 `[I]` |
| Autosave | every turn in S2 and S4 (it is the turn clock); its cost is the budget row "Save" and the factor "autosave never" | |
| Quick Combat / Quick Movement, UI mod set, frame cap | as the scenario settings file says; varied only in factor experiments | |
| Window | 1080p windowed on the laptop (D158); the laptop's scaling fault (0177) noted | |
| Asset set | the laptop's test instances (Vibrant Textures, D94); S2 and S4 from the same kind of instance | |

## 7. Premises (W0, ticket 4650): each answered before the first timed run

| P | Question | How | Result |
|---|---|---|---|
| P1 | Which file is the "official VP 5.4.8 release DLL"; does it play the same game as the x64 pin; does it carry D35's pragmas; what does statistics logging cost it; how is S2t armed for autoplay? | 0438: the tag's DLL `262aee88...` differs from S3 at the first turn on both saves; i0-ship = S3 `[L 2026-11-16]`. Left for W0: the pragmas, the logging cost, S2t's autoplay | answered, 7.1 (P1a-c) |
| P2 | Is the autosave file time a sound per-turn clock: resolution, offset against a direct clock, and the save's own duration? | S4 run with the server's per-turn line (W2) beside the autosave times; S2 against a Lua-side turn-start time if the channel gives one | answered, 7.1; pass rule R5 applied in 9.2 |
| P3 | Can a client series run unattended on the laptop: headed testbed job, 1080p, crash-box watcher, safe stop, instance restored by hash; quiet place; how long one H and one M client run takes | W2's first milestone, from `tools/testbed/jobs/product_path.py` / `headed_probe.py` | answered by 4651 (`turn-time-study.md` "The series tool and premise P3"): yes, `tb.py submit --headed --quiet` |
| P4 | How many turns do the arms stay one game from the scenario save, and is one client arm the same game from run to run? | 0438: S2i = S4 and S1i = S3 at T+1, clients differ from headless at T+1 `[L, one turn]`. Left for W0: 3 untimed 25-turn runs per client arm on M (and 2 on H), statscmp per turn between runs and between S2i and S4 | answered, 7.1: client arms one game run to run and S2i = S4 (M 25 turns, H 10 turns) |
| P5 | Wall time per run and per round on each scenario, so the series fit the laptop's time | from P3/P4's runs | answered, 7.1 and 4651's dry round |

### 7.1 Premise findings (W0, ticket 4650, impl-4650, 2026-11-16 17:44-20:00 EST, workstation: times are indications)

All runs on the workstation, 2026-11-16 17:50-20:00 EST, shared with 5-11 heavy jobs of other teams (games, builds,
suites): every time below is an **indication, not a result** (D106). Builds: tag DLL `262aee88...` (Release-5.4.8);
S2i `i0ship-26234371-ia` `3c2f47da...` in c5h-a; S4 the layout `x64-04b1211b-pfdee656` by `set-proxy` in c5h-b
(proxy `75c40cec...`, server `5b254430...`, x64 DLL `29f58e17...`); EXE `04cfdc71...`. Saves `TS0438-M-T150`
(`98fc454a...`) and `TS0438-H-T300` (`308a96bb...`). Client runs: `config.ini LoggingEnabled = 0` (it was 1 in both
instances: 0438's client checks ran with AI logging on), statistics logging on (CP option, as 0438), frame cap lifted,
no pictures, mods cp, vp, eui. Driver `perf/turnstudy/premises/clientrun.py`, analysis `analyze.py`, run folders
`F:/c5-64bit-work/4650/runs/<run>/` (stats.db kept there), comparisons `F:/c5-64bit-work/4650/checks/`, text copies in
`perf/evidence/4650/`.

**P1 (a) pragmas [C].** The tag Release-5.4.8 (commit 7307aefdf) does **not** carry D35's pragmas: it opens
`stats.db` in `SqliteLogger::EnsureOpen` (`CvGameCoreDLL_Expansion2/SqliteLogger.cpp`, lines 67-95 at the tag) with
`m_kConnection.Open(...)` followed directly by `EnsureDictionaryTable()`; no PRAGMA is executed on the connection (the
only `PRAGMA` in the file is `table_info`, line 157). The pins add `PRAGMA synchronous = OFF` and `PRAGMA journal_mode =
PERSIST` at lines 162-163 of the same function (i0-ship 26234371e and x64 04b1211b8; commit ccfd6dd4d "Do not wait
for the disk on every statistics database commit"). Read with `git grep` in the read-only fork checkout.

**P1 (b) what logging costs the tag's DLL [L, indication].** c5h, tag DLL, `--no-game-logging`, identity database
(SQLITE_LOGGING 1) against the plain client database (0):
- **M cannot be played from the save in c5h with the tag's DLL**: 4 of 4 runs (on, off, off, on; 17:50-17:52)
  crashed in turn 152 with 0xC0000094 (integer divide by zero) at DLL+0x9F9479, logging on or off: bug 0078's
  signature (`CvReligionAI` divides by the world's EstimatedNumCities, which a load leaves 0 in a pregame that never
  cached a world size; fixed in the pins by 27176dbfe; `research/04-memory-investigation/leaks-and-growth.md`) [I: the
  tag build is not symbolised]. The client does not crash there (P1c played 152): the EXE's front end calls
  `setWorldSize` at boot (`research/sources/civ5-headless/engine-contract.md`), c5h's load path does not [I]. So **S1t
  on M is not runnable for 25 turns** as c5h stands; S1t on H played 300 -> 310 twice without a crash (25 turns not run).
- Turns played: M 150 and 151 on: 3.17 / 2.09 and 2.36 / 2.05 s; off: 2.16 / 1.77 and 2.25 / 1.55 s. H 300 -> 310,
  one run each: **on 32.09 s/turn, off 28.31 s/turn (+3.8 s, +13 %)**; per-turn differences 0.2-8.6 s, the last four
  turns' largest; 11 heavy jobs registered at the on run's start, 7 at the off run's, so the difference is confounded
  with load. Not negligible on this evidence: by the plan's section 6 rule, statistics logging stays **off in timed
  runs** and identity comes from untimed runs with it on. A quiet laptop pair would size it properly (not run).
- Evidence: `perf/evidence/4650/headless/s1-*/` (turn-times, events, the host log's phase/turn/crash lines),
  `perf/evidence/4650/p1b.log`, `p1b-h.log`; run folders `C:/c5-64bit-work/4650/runs/` (moved to F: at the hand-back).

**P1 (c) S2t's autoplay [L].** Works: a test-only mod `4650 Autoplay Arm (test only)` that `clientrun.py addin` writes
into the test instance's MODS folder (one `InGameUIAddin` Lua file, `AffectsSavedGames` 0, no database action) and the
harness front end activates with cp, vp, eui. 5 s after `Events.SequenceGameInitComplete` it does what the load
screen's Continue button does (`Events.LoadScreenClose()`, `Game.SetPausePlayer(-1)`, `UI.SetDontShowPopups(false)`)
and `Game.SetAIAutoPlay(N, 0)`. Run `p1c-tag-addin-M` (c5h-a, tag DLL by `set-dll`, 18:03-18:16): turns 150, 151, 152
played (autosaves at 18:06:14.6, 18:06:32.8, 18:06:47.3: 18.2 and 14.5 s), autoplay handed back at 153 (diagnostic
picture `F:/c5-64bit-work/4650/p1c-after3.png`: "Turn: 153", CHOOSE PRODUCTION); safe stop by the harness's no-channel
path, the mod removed by hash afterwards. Whether the add-in arm plays the same game as the channel arm: run
`S2i-M-addin` (i0-ship, 25 turns, 19:45-19:56) against the channel runs: **identical at every turn 150..175**
(statscmp "15 tables, 90457 rows compared: IDENTICAL" against S2i-M-1), so the add-in arm does not change the game
against the channel arm [L, one run]. The mod is a file in the instance's MODS
(not the owner's install), made at run time; the run's autosaves list no extra mod in the save (AffectsSavedGames 0) [I: not
read back].

**P2 the autosave clock [L].**
- **What the file is.** In autoplay the game writes `AutoSave_Post_<N>.Civ5Save` once per turn, at the **start** of
  turn N: the file is created empty 1.1 s (S2i) / 0.25 s (S4) after player 0's `GameEvents.PlayerDoTurn` of turn N and
  filled in one step 0.5-2.3 s later. Autoplay's arm itself writes `AutoSave_Post_<T>` (the human seat's turn ends),
  and a plain `AutoSave_<T>` is written once after the load (the engine's initial autosave, inside the first turn:
  0.9 s M S2i, 0.45 s M S4, 2.5 s H S2i, 1.0 s H S4). **No Post save is written for the turn at which autoplay hands
  back** (the seat is human again): N autoplay turns give N-1 autosave intervals; arm N+1 turns to time N.
- **Which Lua event.** `GameEvents.PlayerDoTurn` fires per player per turn (M 625 events in 25 turns, H 319 in 10);
  player 0's (the observer seat, first in the turn) is the turn boundary. `Events.ActivePlayerTurnStart` fired once per
  run (at the hand-back), `Events.ActivePlayerTurnEnd` never: neither is a turn clock in autoplay.
- **Agreement** (interval between consecutive autosaves' last-write times minus the interval between player 0's
  PlayerDoTurn events, per turn):

  | Run | turns | mean | sd | max abs | save (listing) mean / max | save end after the event |
  |---|---|---|---|---|---|---|
  | S2i-M-1 | 23 | +0.008 s | 0.37 | 0.98 | 1.05 / 2.06 s | 2.18 s |
  | S2i-M-2 | 23 | +0.011 | 0.17 | 0.35 | 0.98 / 1.21 | 2.03 |
  | S2i-M-3 | 23 | +0.008 | 0.24 | 0.44 | 1.01 / 1.15 | 2.25 |
  | S4-M-1 | 23 | +0.005 | 0.12 | 0.35 | 0.54 / 0.61 | 0.76 |
  | S4-M-2 | 23 | +0.006 | 0.09 | 0.21 | 0.51 / 0.60 | 0.74 |
  | S2i-H-1 | 8 | -0.012 | 1.25 | 2.44 | 2.31 / 2.73 | 4.52 |
  | S4-H-1 | 8 | +0.038 | 0.25 | 0.42 | 1.13 / 1.16 | 1.69 |

  Per turn the autosave clock is within 0.1-0.4 s (sd) of the direct clock on M (turns of 12-24 s, so 0.5-2.3 %) and
  0.25-1.25 s on H (58-144 s turns); over a run the mean interval differs from the direct clock's by the change of
  the save's lag between the first and the last save divided by the number of intervals (here under 0.04 s per turn).
  The save is **inside** the turn it starts: about 1.0 s of a 16 s M turn in S2i (6 %), 0.5 s in S4 (3 %); 2.3 s and
  1.1 s on H. S4's saves are about half as long as S2i's on both scenarios [L, the file sizes are equal].
- **Resolution.** NTFS times at 100 ns (every mtime and ctime a multiple of 100 ns); the 25 ms listing's largest gap
  0.07-0.29 s; `first_seen - ctime` 0.015-0.018 s for fresh files. A file that replaces an older one of the same name
  keeps the old creation time (NTFS), so the save's duration is taken from the listing (created empty -> last size
  change) where the name existed before; where both exist they agree within 0.04 s (S2i-M-1: 1.01 by NTFS, 1.05 by
  listing; S2i-H-1: 2.315 and 2.313).
- **The direct clock.** `os.clock()` in the InGame state (wall time, 1 ms) at each event, tied to `time.time()` by the
  channel round trip with the shortest time (16-45 ms) at the start; the tie drifted -38 to +6 ms over a run (10-20
  min), checked again at the end.

**P4 one game [L].** statscmp per turn (`analyze.py ident`, every table with a Turn column, rowid order, RunId and
the GUID left out as statscmp does) and the verdict of record by 0438's `idtrace.py` (trim LO..HI, then
`tools/d10/statscmp.py --from-turn LO`):
- (a) S2i on M, 3 runs of 25 turns: **identical to turn 175** (every turn 150..175, all three pairs; statscmp "15
  tables, 90457 rows compared: IDENTICAL").
- (b) S2i against S4 on M (3 x 2 pairs) and S4 run against run: **identical to turn 175**, every pair.
- (c) H, S2i against S4, 10 turns: **identical to turn 310** (statscmp "14 tables, 231095 rows compared: IDENTICAL").
- So the client arms are one game run to run and S2i = S4 over the study's window, with this procedure (scenario
  save, harness load, channel arm, AI logging off, no pictures). The client arms need no extra runs for identity.
  This replaces the inference "in a client, decisions differ from play to play after a load" (Architect 4.3, from the
  9071 pairs, which differed in pictures and were not compared) [I: what made those plays differ is not traced].
- Not covered: whether acceptance.py's per-turn pictures (popup closing, camera moves) change the game; AI logging on
  (0438's checks) against off; the laptop.

**P5 running time [L, indications on the loaded workstation].**

| Arm | Scenario | Boot+load | Per turn | One run (25 turns + stop) | Source |
|---|---|---|---|---|---|
| S1t | M | 5.0-6.2 s | 1.6-3.2 s (2 turns) | crashes at turn 152 | this ticket |
| S1t | H | 6.4-6.5 s | 28.3-32.1 s | ~12-14 min | this ticket |
| S1i | M / H | as S1t | 1.19 / 25.4 s (one turn, 0438) | ~1 / ~11 min | 0438 |
| S3 | M / H | - | 1.62 / 20.9 s (0438) | ~1 / ~9 min | 0438 |
| S2t | M | 151 s launch to the first autosave | 14.5-18.2 s (2 turns) | ~10.5 min [I: as S2i] | P1c |
| S2i | M | launch 34-39 s, channel at 150-177 s, armed at 163-190 s | 16.2-16.9 s (24 intervals each) | 10.3-10.5 min (616-628 s) | 3 runs |
| S4 | M | launch 30-47 s, channel at 166-174 s, armed at 179-187 s | 15.7-16.9 s | 10.2-10.5 min (613-633 s) | 2 runs |
| S2i | H | launch 35 s, channel 222 s, armed 238 s | 95.6 s (69-144, 9 intervals) | ~45 min for 25 turns [I] | 1 run (10 turns: 20.2 min) |
| S4 | H | launch 32 s, channel 211 s, armed 226 s | 66.3 s (58-98) | ~32 min for 25 turns [I] | 1 run (10 turns: 15.4 min) |

The stop takes 28-41 s (the harness's safe stop), the driver's own steps (settings, front end, save copy) about 10 s.
**One round** (S1t, S1i, S2t, S2i, S3, S4), sequential, on this machine: **M about 35 min** (3 client runs x 10.5 min
+ 3 headless runs x ~1 min, S1t not runnable), **H about 2 h 30 min** (S2t ~45, S2i ~45, S4 ~32, S1t ~13, S1i ~11,
S3 ~9 min). Client H turns varied 58-144 s with the machine's load; 0438 measured 85-92 s for one client turn. The
lead converts to the laptop.

**Not verified.** Nothing on the laptop. One run per H arm. S2t on H (the add-in was run on M only, 3 turns). S1t past
turn 310 on H. The statistics logging cost on a quiet machine. Whether the tag's DLL in the client plays the same game
as itself run to run. P2's events in S2t (no channel to read the Lua clock).

## 8. What counts as a finding

- A difference between two arms (a pre-declared comparison, 9.4 R13): its 95 % interval excludes 0; otherwise "not
  resolved by this series", given with its interval and the detectable difference. No practical-relevance floor.
  (Corrected 2026-11-16 20:06 by the review's R15; was: "excludes 0 and is above the series' detectable difference".)
- A budget row's share (Q2): per-run shares, mean and 95 % interval across runs. Q4 shrinks to one page only if the
  upper bound of "EXE alone" + "calls into the EXE" + "calls from the EXE" is under 25 % of an S4 turn on both
  scenarios; a row is "large" when its lower bound is >= 25 %; between the two "undecided", both bounds shown (R16).
- A factor experiment: screening at 3 runs per arm on M, on and off interleaved in one laptop series (grade B); a
  factor goes to grade A on both scenarios when the far end of its 95 % interval exceeds 5 % in absolute value (R17);
  factor runs record identity, and a factor that changes the game is labelled; only grade A numbers enter the table.
- A server opportunity (Q3): seconds saved per turn with interval, MB spent, statscmp identical over the whole run,
  build cost and risk; a prototype that changes the game is reported as "changes the game" and not ranked.
- Every number in the report carries its grade (A-D of `inventory.md`), its machine, its builds and its run_ids.

## 9. Review and the revised plan (binding; overrides sections 2-8 where they differ)

The performance standards team's review: ticket 7014 (account B), 2026-11-16 17:40-17:53 EST, full text
`perf/evidence/7014/review.md` on branch `task-7014-review-0437-plan@fb904f63`, answer on queue thread 0437 entry 2955.
Verdict: timed runs may start once the required changes R1-R20 are written into the plan. They are adopted below by
lead-0437 on 2026-11-16 20:06, with the premise results of section 7.1 and P3 (ticket 4651) applied.

### 9.1 Arms and grade A pairs

- **R1, applied.** P4 found case (a): S2i run against S2i run, and S2i against S4, identical to turn 175 on M (25
  turns, 3 x 2 pairs) and to turn 310 on H (10 turns) `[L, 2026-11-16, workstation]`. So **S2i-S4 is grade A as
  written**, on the condition that every timed client run's own `stats.db` confirms it (R12). If a timed run diverges,
  R1(b) applies (within-arm against between-arm first difference; if between-arm is no earlier: grade A labelled
  "workload, different games", 12 runs per arm, per-turn pairing only before the first difference) or R1(c) ("different
  game"). The owner's D164 (MC 18:45): identity is required only like with like; headless against windowed divergence is not a
  defect.
- **R2.** S1i-S3 is labelled **"host and bitness"**. Both arms record engine calls per turn (`turn.engine_calls@1`, as
  `perf/tbjobs/series.py --count-arm`), and the report gives c5h's estimated overhead (calls x 0.2-0.6 us) beside the
  difference, replacing the old "about 3 %".
- **S1t on M is not runnable:** the tag's DLL in c5h crashes at turn 152 with an integer divide by zero, 4 of 4 runs
  (P1b) and in 4651's dry round, bug 0078's signature. S1t runs on H only. S2t is armed by 4650's test-only add-in (P1c;
  it does not change the game: identical to channel-armed runs over 150-175).
- Tag arms (S1t, S2t) are descriptive ("player's stock, different game, logging off"): 3 runs each per scenario where
  runnable, inside the rounds, no grade A claim.

### 9.2 The turn clock and the window

- **R3, one window in every arm.** From a save at turn T every arm plays T..T+26; the timed window is **the 25 turns
  T+1..T+25** (M 151-175, H 301-325). The post-load turn T is dropped in every arm (P3's first client turn: 28.9 s
  against 16-18 s after it; the headless hosts' first line is a partial turn). A client is armed for 27 autoplay turns
  (N autoplay turns give N-1 intervals, P2), a headless host plays to T+27; the turn after the window is untimed and
  only closes it.
- **R5, the pass rule, applied to P2's data.** Rule: mean (autosave interval minus direct interval) within +-0.2 % of the
  mean turn, over >= 10 M turns and >= 5 H turns, and no single turn off by more than one save duration. Result: the
  mean passes in all four cases (M S2i +0.008 s of 16.5 s; M S4 +0.005 s; H S2i -0.012 s of 95.6 s; H S4 +0.038 s of
  66 s); the per-turn bound passes in M S2i (0.98 s against a 1.0 s save), M S4 and H S4, and **fails in H S2i (2.44 s
  against a 2.3 s save)**. So the **per-turn clock in S2i and S4 is the direct in-game clock**: `os.clock()` at player
  0's `GameEvents.PlayerDoTurn` in the InGame Lua state, tied to wall time through the Lua channel at the start and the
  end only (tie drift at most 38 ms per run, P2); the `AutoSave_Post_N` times stay as the cross-check. **S2t** has no
  channel to read the handler back, so it uses the autosave clock (its mean passed), labelled. **S4's server line in
  remote mode (`turn-times.tsv`, `turn.server_remote_s@1`) exists only in the W5 budget runs, not in the Q1 timed
  series:** server.exe writes it only with `VP64_TURN_BUDGET=1`, and the Q1 series runs with the switch off. It is the
  turn budget's wall (9.7), never paired turn by turn with `turn.doturn_s` (another event on another clock: the end of
  the Update in which the game turn changed, QueryPerformanceCounter; against player 0's PlayerDoTurn, `os.clock()`).
  Writing the line with the switch off would change the product's behaviour when off and needs the MC (condition of
  #2923). (Corrected 2026-11-16 by ticket 4658 from 7015's B1; was: "S4's server line in remote mode (4652) is a third
  cross-check.")
- **The client clock's ends (7015 P3).** `os.clock()` in the EXE's Lua is the C runtime's `clock()`: wall time since
  the process started, 1 ms resolution (7.1). Row N of `turn.doturn_s@1` runs from player 0's first PlayerDoTurn of
  turn N to that of N+1; the rows are `os.clock()` differences and need no tie; the tie (offset = `time.time()` -
  `os.clock()` of the shortest of 6 channel round trips, before the window and after it) places the window's ends on
  wall time for `load.process_to_window_s@1` and the cross-checks. (Ids corrected 2026-11-17 by ticket 4659: 4658's
  `@2` of these two entries were folded back into `@1` under 7016's in-place ruling, 9.5.)
- Each run records `w32tm /query /status` before and after (no administrator rights), since file times are system
  time and the direct clocks are not.
- **R4.** Every client mean in the four-way table is shown with the save's duration beside it (P2: M S2i 1.0 s, S4 0.5
  s; H S2i 2.3 s, S4 1.1 s); the headless hosts write no per-turn save. Grade A pairs are unaffected (both sides save).

### 9.3 Rounds, order and exclusions

- **R6.** S1i-S3: 6 rounds. S2i-S4: **12 rounds** on both scenarios. A series is extended past its planned rounds only
  by precision: when the 95 % half-width of a primary comparison's mean is above 1 % of the mean; never by
  significance.
- **R7.** Rounds are balanced by Williams squares over the four i-arms (each arm follows each other arm equally often;
  12 rounds = 3 squares), tag arms placed in fixed positions in the rounds that carry them; a fixed idle gap of 60 s
  before every run in every arm; each run's start records the effective clock (R18) and the temperature if readable
  without administrator rights.
- **Laptop time (P5 and 4651's dry round, converted):** M: S1i 47 s, S3 39 s, S2i 11.4 min, S4 11.7 min per run on the
  laptop `[L, one run each]`; 12 rounds about 5.5 h, plus 3 S2t runs (about 35 min) and the logging treatment series
  (R12, about 15 min). H `[I]`: S1i/S3 about 12 min each, S2i about 50 min, S4 about 37 min, so an i-round about 2 h;
  12 rounds about 24 h, plus S1t and S2t 3 runs each (about 3 h). **Order: M first (report v1 can show M while H runs),
  then H.**
- **R8, exclusions (before runs).** A run is excluded only for a recorded cause, never for its time: a crash or an
  assert box; the quiet place lost or a disturbance above the runner's threshold (foreign cores > 0.25 mean); a power
  state change; an identity failure from outside the game (a stale `stats.db`: every run moves the instance's
  `stats.db` aside first). Excluded runs are listed with their cause and their round is re-run at the end.
- **R9, what the EXE does is fixed in the scenario settings** and identical in S2i, S4 (and S2t): the camera left where
  the load puts it, no camera command; the human seat moved to the observer by `SetAIAutoPlay` in every arm; the game
  window in the foreground and not minimised (the laptop needs no hiding, D63); the per-turn sampler at one fixed rate
  in every arm, the headless ones included. **The Lua channel is not polled during the window**: turn progress is read
  from the `AutoSave_Post_N` files; the channel is used before T+1 (arm, tie, handler install) and after the window
  (read-out, tie, stop). 4651's 1 s channel poll is removed.

### 9.4 Statistics and findings

- **R10.** The detectable difference is (t(0.975, v) + t(0.80, v)) x SE with Welch's v, not 2.8 SE.
- **Ratios** by a Welch interval on the logarithms of the run means, transformed back (the review's recommendation).
- **R11.** Memory growth: the slope per run, then the mean and 95 % interval across runs.
- **R12, statistics logging.** **On** in every timed run of S1i, S2i, S3 and S4 (these builds carry D35's pragmas,
  P1a), so each client run's identity is read from that run. Its cost is measured on S1i on M as a treatment series
  (`identity.treatment = statistics logging`, on against off, 6 runs each, laptop, grade A by PERFORMANCE.md 2.7's
  exception), and the report gives the player's workload (VP's default: logging off) as the measured mean minus that
  cost, with its interval. The tag arms run with logging **off** (no pragmas in the tag, P1a; P1b's indication +13 % on
  H) and are labelled.
- **R13, the primary comparisons (findings):** seconds per turn S1i-S3 and S2i-S4 on H and on M (four), and for memory
  the peak private bytes and the growth slope of the same pairs. Everything else (tag arms, headless against client,
  S4's EXE against its server) is description with intervals and claims no finding.
- **R14.** Every timed run writes a `c5-run/1` record into the epic's folder (`F:/c5-64bit-work/0437/runs/`), one
  `series_id` per scenario series; the logging series sets `identity.treatment`; P2, P4 and identity-only runs carry
  `trust.not_for_timing = true`. The report names run_ids.
- **R15-R17** are written into section 8.

### 9.5 Metric ids (R18-R20)

- **R18.** The clock is `machine.processor_performance_pct@1` (4651's entry, accepted by 7015 P2): `\Processor
  Information(_Total)\% Processor Performance`, the mean since the previous turn boundary, series, unit percent,
  context; each run's `run.json` names the rated MHz (`rated_mhz`, 7016 C11), so the effective clock is % x the rated
  MHz, a derived column labelled "% Processor Performance x rated MHz (CallNtPowerInformation)", never a registry
  metric (7016: no GHz unit). It can exceed 100 under turbo
  (laptop: one busy core 133 %, four 122 %, 9.6). The per-run mean is analysis and gets no id: 4651's
  `turn.study_clock_pct@1` is withdrawn (removed by 4653), as is `turn.study_clock_ghz`. Before the first timed run W2
  checked on the laptop that it moves (done, 9.6). (Corrected 2026-11-16 by ticket 4658 from 7015's P2; was: "The clock
  is `machine.effective_clock_ghz@1` ... x the base clock": GHz is not a registry unit.)
- **R19, with 7015's P1-P7 and 7016's C1-C11 (tickets 4658, 4659; the registry entries are in
  `perf/ledger/registry.json`; the list for the performance team's check is `perf/evidence/0437/4658/registry-diff.md`).**
  No `study_` ids. 7016's ruling: an entry that is not on `main` may be edited in place on the branch, with its pin,
  until the review is met, and is immutable from the merge on; so 4658's `@2` versions of entries not on `main` were
  folded back into their `@1` (ticket 4659), and a new version exists only where the `@1` is on `main`. The runner
  (`perf/turnstudy/runner/runrecord.py`) names every id with its version; a test checks that each is registered with its
  kind (`runrecord.id_faults`).
  - S1 and S3 per turn: `turn.ai_s@1`, `turn.ai_mean_s@1` (the hosts' own lines, the same method).
  - S2i and S4: `turn.doturn_s@1` / `turn.doturn_mean_s@1` (P3, C4: `os.clock()` in the EXE is the MSVC C runtime's
    `clock()`, wall time since the process started, not CPU time as on POSIX; row N = hook[N+1] - hook[N]; how the ends
    are tied; 9.2).
  - S2t: `turn.autosave_s@1` / `turn.autosave_mean_s@1` (C5: row N = mtime(`AutoSave_Post_{N+1}`) - mtime(`Post_N`);
    the per-turn rows are description only, the mean is the claim).
  - S4's server line: `turn.server_remote_s@1` (B1, P4, C6: the end of the Update served by server.exe in which
    `GetGameTurn()` changed, QPC on the core thread, row N = boundary N to N+1, `(first)` and `(end)` not turns; switch
    on only; the budget's wall; 9.2, 9.7).
  - CPU (P5): `turn.cpu_s@2` / `turn.cpu_mean_s@2` for the headless hosts (their `@1`, perf/cputime's 25 ms instants,
    is on `main`), because the runner reads process times when it first sees the host's turn line in a 0.1 s poll (7016
    C9, a note: per-turn rows can move by up to about 0.1 s, the window mean by at most about 0.004 s; a 25 ms poll or a
    late-boundary flag is recommended before per-turn CPU rows are used for anything but description);
    `turn.exe_cpu_s@1` and `turn.server_cpu_s@1` for the clients (C7: read when the runner first sees
    `AutoSave_Post_N`, folders polled every 0.5 s, or at a `poll:S` variant's Lua poll of `Game.GetGameTurn()`; the EXE
    then server.exe in the same boundary; per-turn rows not paired with `turn.doturn_s@1`, the window means differ by the
    change in offset between the events / 25).
  - Memory: `exe.private_mb@1`, `exe.ws_mb@1`, `exe.free_mb@1`, `exe.largest_free_mb@1`, `server.private_mb@1`,
    `server.ws_mb@1` reused (the runner reads PrivateUsage and WorkingSetSize of GetProcessMemoryInfo through psutil,
    the counters `tools/harness/memory.py` reads, equal to 0.1 MB in 7016's check, and free space through memory.py);
    `host.private_mb@1`, `host.ws_mb@1`. Slopes (P1, C1): `exe.private_slope@3`, `server.private_slope@3` (OLS over the
    26 boundary rows T..T+25 of the runner's sampler; `@1` is memsampler's, `@2` the acceptance driver's) and, for R13's
    S1i-S3 pair, `host.private_slope@1` by the same method; S4's sum growth, which the analysis quotes as the player's
    memory growth, is `exe_server.private_slope@1`; per R11 no per-run interval is registered. S4's sum (P6, C8):
    `exe_server.private_mb@1`, EXE and server from the same tick of the fixed-rate sampler (one tick reads every process
    under one lock); a boundary whose two values come from different ticks has no row and is left out of the sum's
    slope. The headless hosts' peak (C3): `host.peak_private_mb@2`, the runner's maximum over its 2 s ticks and the
    boundaries (`@1` is turnbench's maximum of 0.5 s samples: a sampled maximum reads lower when samples are further
    apart). 7016's recommended exact peak (psutil `peak_pagefile`, PeakPagefileUsage, at the last boundary) is not taken:
    the M timed series has no such column and is rewritten without a rerun; it would be a later `@3` with a sampler
    change.
  - Boot and load: `load.boot_load_s@2` (headless, C2: the runner's Popen to its first sight of the host's first turn
    line, less that line's seconds; `@1`, turnbench's process wall time less the turns, includes the shutdown and is
    never compared with it); clients `load.process_to_window_s@1` (P7: process creation to player 0's first PlayerDoTurn
    of T+1).
  - The rated MHz (C11): each run's `run.json` carries `rated_mhz` (the run's machine's CallNtPowerInformation MaxMhz;
    `series_job.py`), attached to the record as `turnstudy-run.json`, so the derived clock can be reproduced from the
    record alone.
  - The four-way table labels headless against client "two clocks, related by P2".
  - **The records written before these ids** (the M timed series `0437-M-timed-1`, job `20261116-223507-e8da`, from
    `fb0ecaef9`, and earlier rounds) are rewritten without a rerun: `python -I perf/turnstudy/runner/runrecord.py
    --rewrite <fetched job folder>` keeps each record's run_id, machine, build, identity, trust and outcome, recomputes
    the metrics and series from the run folder under the ids above, and supersedes the old record
    (`provenance.supersedes`, the old folder moved to `<run>/ledger/superseded/`). On a copy of round 1 every value
    and cell stayed the same under the new ids (the old `exe.private_slope@1` values were already this method's), the
    host peaks and boot times moved to their `@2` ids unchanged, and S4 gained `exe_server.private_slope@1`
    (`perf/evidence/0437/4659/`, 2026-11-17). Until its records are rewritten, no number of a series is cited under the
    ids 7016 named (`exe.private_slope`, `server.private_slope`, `load.boot_load_s`, `host.peak_private_mb`).
  (Rewritten 2026-11-17 by ticket 4659 from 7016's C1-C11 and its in-place ruling; was 4658's list with
  `turn.doturn_s@2`, `turn.server_remote_s@2`, `turn.exe_cpu_s@2`, `turn.server_cpu_s@2`, `exe_server.private_mb@2`,
  `load.boot_load_s@1` and `host.peak_private_mb@1`, and the note that the runner still wrote 4653's versions. That list
  had replaced 7014's R19 list on 2026-11-16, ticket 4658.)
- **R20.** End turn to control is measured by an in-game event stamp, so it is `turn.end_to_control_s@2`; its human
  seat's policy for blocking prompts is written before its runs (W3) and comes to 7000 with W5's budget ids.
- **The budget ids** (7015's B1-B9, applied by ticket 4658): `turn.server_remote_s@1` as the wall (4658's `@2`, folded back by 4659), the series rows
  `budget.game_logic_s@1`, `budget.into_exe_s@1` with its seven classes `budget.into_exe.<c>_s@1`,
  `budget.from_exe_s@1` with its four classes `budget.from_exe.<c>_s@1`, `budget.save_s@1`, `budget.transport_s@1`,
  `budget.dll_idle_s@1`; the diagnostics `budget.dll_idle_proxy_s@1`, `budget.overlap_s@1`, `budget.messages@1`,
  `budget.hidden_msgs@1` and the context `budget.updates@1`; the per-run scalars `budget.<row>_mean_s@1` for the six
  main rows. Not registered: `budget.wall_s` (B1), `budget.exe_alone_s` and `budget.exe_alone_proxy_s` (renamed, B4),
  `budget.cost_us_per_msg` (a table column only, B7). Their definitions and the series' design are in 9.7. (Replaces
  2026-11-16: "The budget ids (`budget.*@1`) of 4652's addendum go to 7000 now, before W5's first run": 7015 reviewed
  them and required B1-B9.)

### 9.6 Updates after wave 2 (lead-0437, 2026-11-16 23:10)

- **R12 measured** (4653, job `20261116-205826-5ab6`, laptop, grade A treatment, `turn-time-study.md` "Series of the
  study"): statistics logging costs S1i on M **+0.064 s/turn (+4.1 %, 95 % interval +0.056 to +0.072)**. The player's
  workload is reported as the measured mean minus this cost for the headless arms; the cost on clients and on H is not
  measured and is stated as such.
- **R8 extended: "the game window lost the foreground" is an exclusion cause** (4653's proposal, adopted). With the window
  in the foreground, client turns 151-152 were 26-33 % shorter than with another window in front `[L, indication]`, so
  P3, P5, 4651's dry round and 4654's profile jobs (all background-window times) are not comparable with the timed
  series; their shares and identity results stand, their seconds do not. The runner records foreground state at every
  boundary.
- **9.2 corrected (7015's B1):** the server's per-turn line in remote mode exists only with `VP64_TURN_BUDGET=1`, so it is
  not a cross-check in the Q1 timed series (switch off); it is the budget series' wall (`turn.server_remote_s@1`).
- **R18:** the effective clock is recorded as `machine.processor_performance_pct@1` (one busy core 133 %, four 122 % on
  the laptop); the request for a GHz unit in `perf/ledger/record.py` was answered "no unit; derived column" (7016:
  GHz = % x the rated MHz in analysis, no registry id; corrected 2026-11-17 by ticket 4659, was "until
  `perf/ledger/record.py` has a GHz unit (asked of the MC)"); `w32tm` cannot run on the laptop (the time
  service is stopped), so file times and direct clocks are compared only through the channel ties.
- **P4's headless side (4655):** the headless hosts differ from the clients because they skip `RecalculateLandmasses()`
  after a load; with a probe that recomputes the water flags they play the client's game. The Q1 series runs the hosts
  as they are (the defect is the hosts', not the study's), so S1i-S3 and S2i-S4 stay the grade A pairs and
  headless-against-client stays "different games".
- **The M timed series** is job `20261116-223507-e8da` (series_id `0437-M-timed-1`), 12 Williams rounds of S1i, S2i, S3,
  S4 (S4 = the increment 1 RC as a player has it, checked byte for byte) with S2t in rounds 4, 8 and 12, then count runs;
  round 1 clean (all runs exit 0, 25 of 25 window turns, foreground held, S2i = S4 and S1i = S3 identical).
- **W5's budget ids** are reviewed by 7015 (queue entry 3097, `perf/evidence/7015/review.md` on
  `task-7015-budget-ids-review@b9b456f6`): B1-B9 to be written before W5's first budget run, including a switch-off arm
  to measure the instrument's own cost (B3).

### 9.7 The turn budget series (W5): 7015's B1-B9 (written by ticket 4658, 2026-11-16)

The performance standards team's review of the budget ids: ticket 7015, `perf/evidence/7015/review.md`, 2026-11-16
20:10-20:25 EST. Verdict: W5's first budget run may start once B1-B9 are written; B3 is a measurement inside that
series. **No budget number is quoted to the owner or to the MC as a result until the registry entries are accepted** (review
of the registry diff: ticket 7016, from `perf/evidence/0437/4658/registry-diff.md`). Accepted as written by 7015 and
not repeated here: the switch, the accounting (one state word, priority core > ui > aux, rows partitioning each turn in
integer QPC ticks, overlap beside), QueryPerformanceCounter as the clock, the turn boundary, the labelled turns, the
save's turn attribution, the reader's exit codes, the proxy-side value as a cross-check and `overlap` as a diagnostic
(the mechanism: `turn-time-study.md` "The turn budget (W4a)").

**Status (ticket 4670, 2026-11-17 10:10 EST, from the tracker): the condition "no budget number before 7016's review" is
met.** 7016 is `done` (merged on `main` at `abb41c2b3`, 2026-11-16 23:42; verdict "change": C1-C11, the GHz unit
rejected; MC review #3294); 7017 is `done` (2026-11-17 02:33-02:45: the change record accurate, C1-C11 and B1, B2,
B4-B9 done, C3 and C9 done differently and accepted; one required change, B3-a, a budget build with timers); B3-a is
done by ticket 4664 (the budget build bb, below) and its measurement is the oldpc set `0437-M-budget-oldpc-1` (job
`20261117-083839-4101`). So budget numbers of that set may be quoted, each with the instrument's measured cost beside
it (B3) and its grade; results in `turn-time-study.md`, "The M budget series on oldpc (W5)", "Results (ticket 4670)".
B3 and B3-a measured there (2026-11-17, oldpc, grade A): the instrument's cost on - off **+0.114 s per turn [+0.036,
+0.192], 1.05 % of the budget's 10.813 s wall** (the low end of B3's 0.11-0.35 s estimate); off - RC +0.019 s [-0.045,
+0.083], no difference resolved (detectable 0.089 s); all 18 runs the same game.

- **Scope.** Every budget id is per turn of S4 (configuration `inc1-thin`), remote mode, `VP64_TURN_BUDGET=1` only.
  Source: server.exe's `budget.tsv` and the proxy's `budget-proxy.tsv` -> `perf/turnstudy/budget/budget.py` ->
  `budget-table.csv` / `.json`; in a run record the series file `series-budget.csv` (the W5 runner writes it from
  `budget.series_values`, whose keys are the registry ids) and the per-run scalars in `run.json` metrics.
- **B1, the wall.** The budget's wall is `turn.server_remote_s@1` (4658's `@2`, folded back by 4659): server.exe's own per-turn line in remote mode
  (`turn-times.tsv`), the interval between two turn boundaries (the end of the Update served by server.exe in which the
  game turn changed) on the core thread, QueryPerformanceCounter. `budget.wall_s` is not registered (one method, one
  id). The rows `game_logic`, `into_exe`, `from_exe`, `save`, `transport`, `dll_idle` sum to `turn.server_remote_s@1`
  of the same turn exactly (`budget.py` exit 3 otherwise); `budget.py` checks the line against `budget.tsv`'s wall to
  1.5 ms (exit 5). The line exists only with the switch on (9.2).
- **B2, turn label, window, per-run values.**
  - Row N runs from the boundary at which game turn N begins to the one at which N+1 begins. In 4652's run from save
    T150, turn 150 is `first` and 151-154 are full turns.
  - The means use **exactly the window T+1..T+25** (M 151-175, H 301-325). The window is named in the run record
    (`scenario.turns.from/to`, as 4653's Q1 records do) and applied by the reader: `budget.py RUN_DIR --run-record
    run.json` (or `--window 151 175`). A window turn missing from the table or labelled (`load`, `first`, `end`) is
    refused (exit 6); a complete turn outside the window (a client armed for 27 autoplay turns also completes T+26) is
    in the table and never in a mean. Without a window the reader gives no mean. (Replaces 4652's reader, which averaged
    every unlabelled turn.)
  - Per-run scalars, the mean over the window: `budget.game_logic_mean_s@1`, `budget.into_exe_mean_s@1`,
    `budget.from_exe_mean_s@1`, `budget.save_mean_s@1`, `budget.transport_mean_s@1`, `budget.dll_idle_mean_s@1`
    (`budget-table.json` `window_summary.run_scalars`). The class rows stay series only.
  - **Shares** are analysis and get no id: a row's share is sum(row) / sum(wall) over the window (= the ratio of the
    means), never the mean of per-turn ratios; its interval comes across runs (`budget.py --across TABLE...`: mean and
    95 % Student-t interval of the runs' shares and scalars, at least 2 runs with the same window, exit 7 otherwise).
    A budget share always uses its own wall as the denominator (B8).
- **B3, the instrument's own cost, measured in W5's first series.** The study's micro-benchmarks put the timers in the
  range of the small rows: server.exe 45-110 ns per frame change, two per call it serves or makes; the proxy 280-320 ns
  per top-level call it makes and 120-160 ns per call it serves or makes nested; so **per call, both sides together,
  0.21-0.54 us** (a call is two messages). 4652's S4 run has 1.08-1.28 M messages (0.54-0.64 M calls) per full M turn,
  so 0.11-0.35 s per turn: 1-3 % of a 10.5-12.6 s turn, but 7-28 % of `game_logic` (1.27-1.67 s) and 19-71 % of
  `transport` (0.49-0.57 s) `[I]` (uncontended micro-benchmarks times call counts; 4652's "0.25-0.55 us per crossing"
  mixed the per-call and the per-message readings, 7015 B3). So the cost is measured, not estimated:
  - **The budget build (7017 B3-a, ticket 4664, 2026-11-17).** The RC has no budget timers (`S4`: proxy `75C40CEC`,
    server `5B254430`), so the two arms cannot be "the same RC build" (corrected 2026-11-17 by ticket 4664 from 7017
    section 3; was: "`S4:budget_on` and `S4:budget_off`, the same RC build"). The budget build is **bb, "RC + 4652"**:
    arm `S4b` in `arms.json`, the RC's layout (x64 DLL `29F58E17`, libraries, `manifest.json`, `purity.tsv`, the RC's
    `vpproxy.ini`, the tag's DLL `262aee88` as the fallback, pack=1) with bb's **server.exe `14D22095...`**, **proxy
    `09D09D7C...`** and **vp64helper.exe `EBFB6AF0...`**; source commit **`a4bc303f6`** (branch `task-4664-budget-src`:
    the RC's tools commit `fdee65668` plus the `server/` and `proxy/` parts of 4652's `d838981bd`, `926f16593`,
    `34f343fe7`, `4d921fc6a`; one conflict, in `srv_remote_main.cpp`, resolved by taking only 4652's five `SrvBudget`
    lines), built 2026-11-17 05:28-05:30 EST by `server/build_server.py --config release --offline --repo-commit
    a4bc303f6` from a git archive of it (build id `repo_commit=a4bc303f6... (archive)`, DLL pin `04b1211b8`); layout
    `F:/c5-64bit-work/4664/bb-layout/VP64`, proxy and helper `F:/c5-64bit-work/4664/bb-proxy`, build log and outputs
    `F:/c5-64bit-work/4664/bb-build` [C: hashes 2026-11-17]. **What separates it from the RC:** 4652's timers (off
    unless the environment sets the switch: one bool test per hook) and the build id string [C from the source diff:
    `git diff fdee65668 a4bc303f6` is 12 files, all 4652's]. Not verified: that bb with the switch off plays the RC's
    game (the series' identity check does it) and that it runs at all in a client (no live run of bb yet).
  - **Not b2.** 4652's b2 (server `4AF51826`, proxy `B73E1D25`, helper `5DFFEF62`, `F:/c5-64bit-work/4652/layout-b2`)
    was built from repo commit `4d921fc6a` (clean; `F:/c5-64bit-work/4652/b2.log`), the epic branch at the time, which is
    **not** RC + 4652: `git diff fdee65668 4d921fc6a` has 78 files under `server/src`, `proxy/src` and `proxy/include`,
    33 of them in remote mode and the proxy (the crash filter, D118's fatal report, the leader scene, the save variant,
    the remote feed, besides the budget) [C, 2026-11-17]. So b2-off minus the RC would mix those changes with the hooks'
    cost. b2 stays the named fallback (an RC-form layout of it is at `F:/c5-64bit-work/4664/b2-layout/VP64`) if bb fails
    a check; then the RC arm measures the whole difference and the report says so.
  - **Arms:** `S4b:budget_on` and `S4b:budget_off` (bb, the same build in the same instance (oldpc's `tb1`; was "laptop", corrected 2026-11-17 by 4666) from the same save,
    differing only in `VP64_TURN_BUDGET=1` in the EXE's environment: the harness's `extra_env`; server.exe inherits it;
    the variants are W5's, 7017 section 4) **and `S4`, the RC, as the third arm** (7017 B3-a option (a)): 6 runs, so
    "budget-off = RC" is measured (S4b:budget_off minus S4, Welch, as the cost pair) instead of inferred, and a budget
    share may be set beside Q1's S4 seconds only with that difference beside it.
  - **Series:** at least 6 on, 6 off and 6 RC (`S4`) runs, interleaved (rounds in Williams order over the three arms,
    each arm first equally often; ticket 4664 replaced "rounds alternate the order on/off, off/on" when the RC arm was
    added), **on oldpc as a quiet testbed job of its own** (slots 2, `--expect-min`), **one `series_id`**
    (`0437-M-budget-oldpc-1`; H gets its own), the 60 s idle gap and R8's exclusions (the game window losing the
    foreground included). *Corrected 2026-11-17 08:40 by ticket 4666 (the MC's answer C on thread 0437, 07:57: the
    laptop runs the H series until about 2026-11-18 10:21): this set is its own set on oldpc and is never compared run
    against run with the laptop's Q1 numbers (D106: arms only inside one set); its S4 arm is the RC reference for the
    cost pair inside the set. Submitted as job `20261117-083839-4101` (`turn-time-study.md`, "The M budget series on
    oldpc (W5)"). Replaced: "on the quiet laptop with the quiet lock held, one `series_id` (`0437-M-budget-1`; H gets
    its own)".* The other way
    7015 allows, placing on-runs in fixed positions of the Q1 M series so that its S4 runs are the off arm, is not
    taken for M: job `20261116-223507-e8da` is already running without them.
  - **The cost:** on minus off of `turn.doturn_mean_s` (player 0's PlayerDoTurn clock, which exists in both arms; the
    version the runner writes, the same in both arms) with Welch's 95 % interval and the detectable difference (R10).
  - **Same game:** every run's identity is read from its own `stats.db` (R12): on-runs against off-runs, and both
    against the RC runs (ticket 4664, 7017 B3-a item 4), over the whole window (`tools/d10/statscmp.py --from-turn 150 --expect-turns 150 175`, as 4652's parity pair), not only over
    150-154 as 4652 showed for the switch on.
  - **Grade:** the team's ruling for this study: the on/off pair differs only in an environment switch and is the same
    build; when each run's own `stats.db` shows the same game it is **grade A as a treatment pair**, in the manner of
    PERFORMANCE.md 2.7's exception. `perf/ledger/record.py` accepts only `ai_logging` and `stats_logging` as
    `identity.treatment` (adding `turn_budget` is on 7000's backlog), so until then the runs carry the `series_id` and
    the arm names, no `identity.treatment`, and the comparison is labelled **"treatment: turn budget switch, ruling of
    7015"**.
  - **Reporting:** every row and share is given with the measured instrument cost beside it. R16's bound (the size of
    Q4) is widened by the cost divided by the wall time, since the cost cannot be assigned to rows exactly (most of it
    lands in `game_logic` and `into_exe`, where the hooks sit, and the part between the two sides' clock reads in
    `transport`, B6) `[I]`.
  - The on-runs of this series are also W5's budget runs: their `budget-table.json` files are the input of
    `--across`.
- **B4, the remainder row.** It is `budget.dll_idle_s@1` (7015's recommended form (a); `budget.tsv` keeps the row name
  `exe_alone`, the reader renames it) and `budget.dll_idle_proxy_s@1` for the proxy's cross-check (a diagnostic).
  - It measures that no service thread of server.exe is busy: in `inc1-thin` the EXE on its own (the report may label
    it "EXE alone" there); in the MC's extra M arm with increment 2 (#3090) server.exe works on other threads during it
    and the EXE may wait for the server's frame, so that arm reads it as "DLL idle" only.
  - It holds the EXE's frames between Updates, frame-pacing waits included. `budget.updates@1` (count, series, from the
    `turn wall` line) is registered beside it: 4652's full turns had exactly 25 Updates each with the frame cap lifted.
  - **The frame-cap state is fixed in the scenario settings as the RC ships it, identical in `S4:budget_on`,
    `S4:budget_off`, the Q1 S4 runs and S2i**: the runner's `frame_cap` setting is the same in all of them (the Q1 M
    series runs with `launch: timing`: the harness adds no cap of its own; the game's own settings come from the
    instance's `GraphicsSettingsDX11.ini`, which the run record hashes). Before the first budget run W5 records whether
    the game itself caps the frame rate in the series' instance (its VSync / frame-limit keys): 7015 marked it not
    verified. 4652's numbers (cap lifted on the workstation) do not carry over. *Done 2026-11-17 by ticket 4666 for
    oldpc's `tb1` (the series' machine; was "the laptop instance"): `WaitForVSync = 1`, windowed 1920 x 1080, timing
    launch with the window on screen, so the game is vsync-bound at 60 fps there (`turn-time-study.md`, "The M budget
    series on oldpc (W5)", step 2).*
  - The row is never described as the EXE's work without the EXE's CPU time of the same turn (`turn.exe_cpu_s`) beside
    it, because it includes waits.
- **B5, the class table is part of each class id.** Every `budget.into_exe.<c>_s@1` and `budget.from_exe.<c>_s@1`
  names its method list from `proxy/include/vp_budget.h` (`ClassOfMethod`, `ClassOfRuntime`) at commit `926f16593`
  and one line on what the class holds (for example `script` = the `ICvEngineScriptSystem*` interfaces with Args1 and
  `kRtE_Lua*`: GameEvents and the DLL's Lua in the engine). A change of the table that moves a method is a new version
  of every class id it moves, and of the parent row if a method changes parent (for example `kOutSave` against
  `engine`).
- **B6, transport.** `budget.transport_s@1` states: (1) the turn's total is measured: (the proxy's round trip -
  server.exe's handler time) over the EXE -> DLL calls + (server.exe's round trip - the proxy's handler time) over the
  DLL -> EXE calls; messages that crossed while a higher-priority thread's work was charged (`hidden`) are in no row,
  because their transit overlaps the charged row; (2) the split per row is apportioned at the turn's mean cost per
  message, so a row whose messages are larger or smaller than average (for example `save`) is approximate; (3) the
  part of the instrument's own cost that lies between the two sides' clock reads is inside it `[I]`; (4) the cost is
  clamped at 0, and each row's share at the row's length. `budget.messages@1` and `budget.hidden_msgs@1` (counts,
  series) are registered as diagnostics.
- **B7, units.** The cost per message stays a `budget-table.csv` column (`cost_us_per_msg`), a diagnostic, not a
  ledger metric, until 7000 adds `us` to `record.UNITS` (7000's backlog).
- **B8, what the budget may be compared with.**
  - Within S4: only S4 switch-on runs of one `series_id` on one machine.
  - Against S2: S2's budget (Architect 4.4, ticket 4654) uses other methods (core-thread CPU as "game logic", wall minus
    CPU as "EXE alone"); its rows get their own ids, never `budget.*`; a stacked bar of S2 beside S4 is labelled "two
    methods".
  - 4652's headless proxy route (c5h with the proxy) is not `inc1-thin`: c5h stands in for the EXE; its rows carry
    their own configuration label and are never set beside S4's.
  - Against the client turn clock: the budget's wall (QPC at the end of an Update) and `turn.doturn_s` (`os.clock()` at
    player 0's PlayerDoTurn) are two events on two clocks. Per-turn rows of the two are not paired; over the same
    25-turn window their means differ by the change of the offset between the two events divided by 25. A budget share
    always uses its own wall as the denominator.
- **B9, registry fields.** Every `budget.*` entry: family D; kind series (the six `_mean_s` scalars per B2); timing
  true for the `_s` rows, false for the counts; better lower for the rows, none for the diagnostics
  (`dll_idle_proxy_s`, `overlap_s`, `updates`, `messages`, `hidden_msgs`); the source names `budget.tsv`,
  `budget-proxy.tsv` -> `budget.py` -> `budget-table.csv` and the run record's `series-budget.csv`; the definition
  says "S4, remote mode, `VP64_TURN_BUDGET=1` only". `game_logic` notes the previous turn's flush (`info flush_prev`,
  0.1-0.3 ms per turn in 4652's run). `perf/turnstudy/budget/tests/test_budget.py` checks these fields for every id the
  reader writes, and that `budget.wall_s`, `budget.exe_alone_s`, `budget.exe_alone_proxy_s` and
  `budget.cost_us_per_msg` are not registered.
- **Recommended by 7015 and done:** the consistency check per turn, 0 <= (server idle - proxy idle, both before
  transport) <= direct transport, as the column `remainder_check` (a diagnostic, no exit code); on 4652's b2 files
  (`perf/evidence/4652/s4/*-new2-on-1.*`) it holds in 151-154 (0.377-0.480 s against 0.522-0.584 s) `[C, 2026-11-16,
  ticket 4658]`, and the reader reproduces 4652's means over 151-154 (wall 11.738 s, DLL idle 8.031 s). **Recommended,
  not done:** splitting `from_exe` into calls the EXE starts and callbacks nested inside a DLL -> EXE call (needs the
  timers to tell them apart: a change to server.exe), and the core thread's CPU time beside `game_logic`.

### 9.8 W6 phase 1: the server's memory-for-speed candidates ranked (ticket 4668, impl-4668, 2026-11-17)

The candidate classes of the proposal's section 4.5, ranked by an **upper bound** on what each could save per S3 turn:
the measured share of the turn it can touch (the S3 profiles of `turn-time-study.md`, "Where the server's time goes
today (W6 phase 1)"; workstation, shared: indications) times the S3 turn time. **No row is a timing claim** (D106): a
bound is what the row could save if everything it touches disappeared; the real saving is the counted fraction of it.

- Turn times used: M **1.424 s** (S3 mean of the M timed series, laptop, grade A); H **20.9 s** (0438's headless S3 run
  300 -> 325 on the workstation, the pin server: an indication; replace by the H series' S3 mean when it exists, every H
  bound scales with it).
- Shares: M the range of the three runs m1-m3, H the one run h1. Families overlap; the bounds are **not additive**.
- Map sizes for the memory column: H 140 x 88 = 12,320 plots (the script scales Huge), about 175 cities of which 155
  are majors', about 1,600 units at turn 307 (`F:/c5-64bit-work/4668/runs/h1/final-summary.txt`); M 79 x 53 = 4,187
  plots.

**The ranking after the counting runs (ticket 4669, impl-4669, 2026-11-17; current).** The three designs below were
built on the fork branch `x64-count-0437` (head `c32469ccb`, from 04b1211b8; counting DLL SHA-256
`ED0BB1A72C343CE12E3B7C468615DFE7A69674FA9EC1CE75DABCD33ADBE18144`, one build with the three defines
`VP64_COUNT_TRADEPATH`, `VP64_COUNT_REACH`, `VP64_COUNT_PARALLEL`) and run on M 151-160 and H 301-305; both runs'
`stats.db` are IDENTICAL to the arm's games (method and tables: `turn-time-study.md`, "What the server asks again: the
counting runs"). A counted bound is the time the counted repeats took, as a share of the counting run's turn, times the
S3 turn above; it is still an upper bound (a cache's own cost is not in it), and still an indication (workstation).

| Rank | Candidate | Counted bound, s per S3 turn, M / H | What the count says | Memory (counted keys and values) |
|---|---|---|---|---|
| 1 | **Trade-path cache with exact (narrower) invalidation** (was rank 2, <= 5.7) | **0.052 / 4.34** (cities whose paths equal their previous rebuild; 0.037 / 2.13 if the cached info with its danger and yield scores must also be equal) | 86-89 % of origin-city rebuilds find the same paths again; on H 384 of 439 rebuilds follow an invalidation in the same turn: every `setAtWar` change (`CvPlayerManager::Refresh(true)`), every finished worker build (`CvUnit::build`) invalidate every player, a city plot coming into view invalidates both teams. **Prototype (a), 4673: the searches reused while every input they read holds: game identical, about 0.8-0.9 s of the H turn (family share 26.1 -> 22.4 %, attribution), see below** | info 18,072 destinations x 28 B = 0.5 MB on H (exists today); with the paths kept 830,760 nodes x 10 B = 8.3 MB |
| 2 | **Profile-guided optimisation** (unchanged, not counted) | 0.07-0.21 / 1.0-3.1 [I] | - | none |
| 3 | **Reachable sets reused with a per-unit invalidation**, above all the danger maps' enemy reach (was ranks 3 and 5: <= 5.2 and <= 2.65) | **0.032 / 1.94**, of it the danger maps' enemy reach 0.022 / **1.72**, tactical 0.007 / 0.21; **prototype (b) (4674): the danger reach about 0.016 / 1.25 kept, identical games** | same key and result as earlier in the turn: 39 % / 69.5 % of calls (danger: 94 % of `UpdateDangerSingleUnit`'s calls on H); with nothing at all changed in between (a global epoch) only 4.6 % / 1.9 % of calls -> 0.006 / 0.043 s: a cache needs an invalidation by unit and region, not by turn or by any event. All danger maps together are 11.0 % of the H turn (<= 2.3 s) | 70-96 MB on H per turn (6,315 keys, 4.4 M plots x 16 B); 5-8 MB on M |
| 4 | **Single-unit paths** (unchanged, not counted) | <= 0.33 / <= 2.15 | - | tens of MB [I] |
| 5 | **Allocation** pools (unchanged) | <= 0.10 / <= 0.96 | - | tens of MB [I] |
| 6 | **Idle cores at the turn's start** (was rank 1, <= 6.2) | **0.001 / 0.12** (first builds with no input event since the turn start, 4 cores); 0.001 / 0.24 counting only map inputs; 0.03 / 0.95 even if every first build qualified | only a player's first build of the turn can move to its start, and first builds are 2.8 % (trade) and 3.2 % (danger) of the H turn; the rest of the 25.0 % and 11.0 % are rebuilds after same-turn invalidations and dirty flags; 45 of 55 first trade and 149 of 160 first danger builds on H follow an input event | as before (<= 50 MB [I]) |
| 7-10 | step-cost table, plotDistance table, the player's own turn (unchanged) | as in the table below | - | - |

**The two prototypes for W6 phase 2** (4669's recommendation to the lead): **(a) the trade-path invalidation made
exact**: keep each city's paths and rebuild only the players and cities a change can reach (a war only for the teams
whose territory danger or passage it changes, a worker build only for paths within the changed plot's trade range, a
city coming into view only for the two teams), checked by the identity test; reason: the largest counted bound (H 4.34 s,
21 % of the counting turn), almost no memory (<= 10 MB), all in one class (`CvGameTrade`), and the counter already lists
which invalidations repeat. **(b) A per-turn cache of the danger maps' enemy reach** keyed by unit and every
`SPathFinderUserData` field, dropped for a unit when a unit moves, appears or dies, or a plot changes, within that
unit's reach plus one (or its ZOC neighbourhood), checked by the identity test; reason: the second counted bound (H
1.72 s; 94 % of those calls repeat an earlier result), a single caller (`CvDangerPlots::UpdateDangerSingleUnit`), 70 MB
at most. PGO (rank 2) needs no prototype of this kind and carries the least identity risk; it is a build decision for
the MC (D107). Idle cores (now rank 6) should not be prototyped.

**Prototype (a) built (ticket 4673, impl-4673, 2026-11-17; fork branch `x64-proto-0437`, evidence only; details in
`turn-time-study.md`, "W6 phase 2: prototype (a)").** Narrowing the invalidations themselves would change the game: a
rebuild re-reads the scoring's inputs (approaches, visibility, units, dominance zones), which change without any
invalidation (4669: only 49 % equal scored info on H). So the prototype keeps every rebuild and reuses each origin city's
`GetMultiplePaths` search while a fingerprint of everything that search read is unchanged (the search-level inputs and a
hash of every touched plot's properties, each only where the callbacks read it). Results `[L, workstation, shared]`:
every run of three builds (b1 wide, b2 and b3 narrower; b3 = DLL `163DEFCF...`, fork `5a6c1a25d`) IDENTICAL to the
arm's games on M 150-160 and H 300-305; the verify mode found no false reuse (b3: H 9,166 of 9,166); b3 reuses 75.7 % (M)
/ 64.7 % (H) of the searches. **The saving is far below the bound**: the trade-path family's sampled share on H fell from
26.1 % (pin) to 22.4 % (b3), about 0.8-0.9 s of a 21-25 s turn (bound 4.34 s), an attribution, not a timing; memory
+35 MB (the store 29 MB). Most searches that still run see a real input change on a touched plot (war changes at enemy
cities, worker builds, borders) yet find the same paths (78 %), which an input-exact check cannot prove. Timing: no
quiet-machine comparison (COMMON 23); a D178-sized testbed pair (S3 vs S3p with b3, M and H, 3 x 15 turns, dry runs
ok) is prepared, not submitted. **Rank 1's counted bound stands as a bound; the exact-input design delivers about a
fifth of it**; more would need an argument about which changes cannot alter a search's result (for example a cost
increase off every path of its result tree, or a war change at a city no path passes through), with its own identity
risk.

**Prototype (b) built and run (ticket 4674, impl-4674, 2026-11-17; `turn-time-study.md`, "W6 phase 2: prototype (b)").**
Fork branch `x64-proto-0437-reach` `0d15296d0`, define `VP64_PROTO_REACH` (evidence only), DLL `F09FAD8B...`. A cache of
each enemy unit's reach for the game turn, keyed by the unit and the full search data, checked against a snapshot of the
unit, team and player epochs, and stamps on the plots the search closed plus one or two rings (plus every city's two
rings for searches that reach an airlift city). **Every run's game is IDENTICAL** to the references (M 150-160, H
300-305; verify mode: 0 wrong hits in build 3). On H 82.7 % of the 72,692 calls hit; the danger reach falls from 9.1 % to
3.1 % of the run's turn (sampled family 9.4 % -> 3.8 %), **about 1.25 s of a 20.9 s S3 turn** [I: attribution on a
shared workstation, no timing pair yet]; M about 0.016 s. Memory about 5 MB on H (not 70-96 MB). The airlift searches
(an enemy unit in or next to an airport city) were 9.9 of the 12.8 s of H's danger reach: the first design, which left
them uncached, kept only a quarter of it. A testbed pair S3 against S3r (H, oldpc, 6 Williams rounds) is prepared, not
submitted.

The profile-only ranking of 4668 follows (09:19, before the counting runs; the bounds of rows 1, 2, 3 and 5 are
corrected in place, each with the old value).

| Rank | Candidate (class) | Share it touches, M / H | Bound, s per turn, M / H | Memory | Identity risk | Cost to build properly |
|---|---|---|---|---|---|---|
| 1 | **Idle cores**: each major's trade-path rebuild and danger map built in parallel at the start of the game turn (per-player work that reads the map) | trade-path cache or danger maps: 8.4-9.2 % / **39.8 %** | **counted (4669): <= 0.001 / <= 0.12** on 4 cores (only first builds can move, and few see no input event); was: 4 cores <= 0.10 / <= 6.2 (share x 3/4), unlimited cores <= 0.13 / 8.3 | one path finder per thread (the A* node grid, about 12,320 x 2 layers x ~100 B, 2.5 MB on H) plus scratch: <= 50 MB [I] | **high**: the parallel result must equal the serial one, so nothing a player does between the turn's start and its own build may change another player's inputs (the premise counted by design 3); and every read of the build must be thread-safe (the step finder is one global object, `GC.GetStepFinder()`) | large: the DLL is single-threaded by design; not additive with ranks 2 and 5, which remove the same work |
| 2 | **Trade-path cache kept across turns** with exact invalidation (cache) | 4.2-5.0 % / **27.2 %** | **counted (4669): <= 0.052 / <= 4.34**; was <= 0.07 / <= 5.7 | the cache exists today (an `STradePathInfo` per origin and destination); an invalidation index of each path's plots about 155 x 175 x 2 x 30 plots x 4 B = 6.5 MB on H: <= 15 MB [I] | **high**: the paths read revealed plots, routes, features, improvements (and their revealed state), owners, war states, every team's approach (`aiTeamDanger`), era and techs (ocean, mountains), cities' passing-route yields, rivers; a missed invalidation changes AI decisions | large (P-7101 estimated 10-15 % of a huge turn; D49 approved only its counting step) |
| 3 | **Reachable sets reused** while their inputs hold (`CvPathFinder::GetPlotsInReach`, incl. the danger maps' enemy reach) (cache) | 14.5-17.0 % / **24.9 %** | **counted (4669): <= 0.032 / <= 1.94** with an exact invalidation, <= 0.006 / <= 0.043 with a global epoch; was <= 0.24 / <= 5.2 | a set per (unit, flags): about 1,600 units x a few flag sets x ~100 plots x 16 B = about 10 MB on H; with every observing player's view of enemy units <= 100 MB [I] | **high**: reach depends on every unit's position (ZOC, stacking), visibility, borders and danger flags, which change with every move inside a turn | large |
| 4 | **Profile-guided optimisation** of the x64 DLL (build) | the whole DLL (84-93 % of the samples end in it) | 5-15 % [I: typical PGO gains on branchy C++, not measured here]: 0.07-0.21 / 1.0-3.1 | none | low: same source; the identity test still decides (floating point must not change) | medium: an instrumented build, a training run on the study's saves, the profile in the release link; which configuration carries it is the MC's (D107) |
| 5 | **Danger maps** rebuilt only when their inputs change (cache) | 3.6-4.3 % / 12.7 % (9.0 points inside rank 3) | **counted (4669): every danger map 2.7 % / 11.0 % of the counting turn, <= 0.04 / <= 2.3; its enemy reach repeats <= 0.022 / <= 1.72 (in rank 3)**; was <= 0.06 / <= 2.65 (its own part: 3.7 % = 0.77 s) | the maps exist (one per player); little more [I] | medium-high: rebuilt every turn and on the dirty flag (war states); `IsInDangerFromPlayers` from diplomacy triggers rebuilds (5.1 points on H) | medium |
| 6 | **Single-unit paths reused** inside one decision or turn slice (`CvPathFinder::GetPath`, incl. `CvUnit::TurnsToReachTarget`, which calls `ComputePath(..., bCacheResult = false)` and so bypasses the one-path-per-unit cache `HaveCachedPathTo`) (cache) | **21.3-23.5 %** / 10.3 % | **<= 0.33** / <= 2.15 | small: paths per (unit, target, flags), <= tens of MB [I] | high: unit positions change inside a turn | medium-large |
| 7 | **Allocation**: pools or arenas on the hot paths (allocation) | heap self 6.0-7.3 % / 4.6 % | <= 0.10 / <= 0.96 (a pool removes part of it [I]) | tens of MB of pooled slack [I] | low, unless some code orders by pointer value (to check) | medium |
| 8 | **Static step-cost table**: the terrain, feature, route and river part of `CvUnitMovement::MovementCost` per plot, direction and movement class, built at load and invalidated on route, feature and improvement changes (table) | 6.5-10.8 % / 3.4 % | <= 0.15 / <= 0.71 | 12,320 x 6 x ~16 classes x 4 B = about 5 MB on H [I] | medium: every unit modifier must stay outside the table | medium |
| 9 | **plotDistance table** for all plot pairs (table) | self 2.0-2.7 % / 2.2 % | <= 0.04 / <= 0.46, **likely <= 0**: `plotDistance` is a few integer operations; a read from a 152 MB table misses the caches [I] | H 151.8 M pairs = **152 MB** at one byte (the proposal's 105 MB was for 128 x 80; the script makes 140 x 88); M 17.5 MB | none (a pure function) | small; recommendation: drop |
| 10 | **The player's own turn**: prepare the next AI turn while the player thinks | not measurable in S3 (no human seat) | 0 in S3 | - | high (the player's moves invalidate) | design note only; belongs to S4 with a player |

Read with it: the M bounds are all under 0.33 s (23 % of a 1.4 s turn) and an M turn in the client is paced by the EXE
(the 15.3 s client turn of the M series), so on M none of the rows changes what the player waits; on H the trade-path
cache, the reachable sets and the danger maps together touch 55.7 % of the server's turn (union of the three families
in h1). Every row stays evidence for the report (section 4.5's hard rule): nothing merges into the product DLL.

#### Counting-run designs for the top three (for the lead to take to the MC; no DLL code written)

All three counters go on one fork branch, **`x64-count-0437`**, cut from the pin `04b1211b8` (never the fork's
`master` or an upstream-tracking branch), each behind its own compile-time define, off by default (nothing compiles in
without the define), reading game state only and writing a CSV into the server's run folder (`run_dir`). The run, for
each: S3 headless from the study's save, **M 151-160 (10 turns)**, and H 301-305 because the shares are on H; release
configuration; `dll_log_flags 0`; statistics logging on as the arm. **The run counts only if its `stats.db` is
IDENTICAL to the series' S3 over the window** (`perf/evidence/0438/idtrace.py`, as 4668's sampled runs were checked):
a counter that changes the game voids the run. The QPC time recorded per call is only for turning counts into seconds
(it is an indication on the workstation; the bound uses its share of the counting run's turn times the series' turn).

1. **Trade-path cache (rank 2), define `VP64_COUNT_TRADEPATH`.**
   *Question*: how often does a major rebuild its trade paths with the same result it built before?
   *Where*: `CvGameTrade::UpdateTradePathCache` (`CvTradeClasses.cpp:363`), after `SetTradePathsCacheUpdateCounter`:
   for each origin city a hash of its land and water maps (destination plot index and the seven `STradePathInfo`
   fields), compared with the hash the same city had at the player's previous rebuild; the reason of the rebuild (first
   of the turn, or after `InvalidateTradePathCache(PlayerTypes)`, `InvalidateTradePathTeamCache` or
   `InvalidateTradePathCache()` in the same turn, each logged with its caller); and per turn the counts of the input
   events: `CvPlot::setRouteType`, `setImprovementType`, `setFeatureType`, `setOwner`, `setRevealed`, `setRevealedImprovementType`, `setRevealedRouteType` (per team; all
   in `CvPlot.cpp` at 04b1211b8),
   `CvTeam::declareWar` / `makePeace`, city founding, capture and razing.
   *Writes*: `TradePathCount.csv`, one line per rebuild (turn, player, reason, cities, cities with an equal result, new,
   gone, ms) and one per input event.
   *Into a bound*: per turn, the summed ms of the cities whose result was equal, as a share of the counting run's turn,
   times the series' S3 turn; that is the most an exact invalidation can save (equal results are a superset of
   unchanged inputs; the event counts show how far apart the two are).

2. **Reachable sets (rank 3), define `VP64_COUNT_REACH`.**
   *Question*: how often is the same reachable set asked again with nothing changed?
   *Where*: `CvPathFinder::GetPlotsInReach` (both overloads, `CvAStar.cpp:3333` and `3382`) and
   `TacticalAIHelpers::GetAllPlotsInReachThisTurn` (`CvTacticalAI.cpp:5559`). Key: start plot plus every field of
   `SPathFinderUserData` (`ePath`, `iFlags`, `ePlayer`, `eEnemy`, `iUnitID`, `iMaxTurns`, `iMaxNormalizedDistance`,
   `iMinMovesLeft`, `iStartMoves`; its `operator==` compares only five of them, so it is not the key) plus a world epoch
   that the counter increments on any unit position change, unit creation or death, plot owner, route, improvement or
   feature change, a team's visibility change, and war or peace. A call whose key and epoch equal an earlier call of the
   turn is a repeat; its result is hashed and compared with the earlier one (an unequal repeat means the epoch misses an
   input: the counter lists them). Split by caller class: danger (`CvDangerPlots::UpdateDangerSingleUnit`), worker moves,
   explorer moves, tactical, city connections.
   *Writes*: `ReachCount.csv` per turn and caller class: calls, repeats, repeats with an equal result, ms of all calls and
   of the equal repeats.
   *Into a bound*: the ms of equal repeats as a share of the counting run's turn, times the series' turn.

3. **Idle cores' premise (rank 1), define `VP64_COUNT_PARALLEL`.**
   *Question*: could each major's trade-path rebuild and danger map have been built at the start of the game turn, before
   any player moved, with the same result?
   *Where*: a mark at the start of `CvGame::doTurn` (the game turn) and at each player's first `UpdateTradePathCache`
   rebuild and `CvDangerPlots::UpdateDanger` of the turn; between them, the input events of design 1 plus, for the danger
   map, the moves, creation and death of units the player can see. A build with no input event since the game turn's
   start is one a parallel build at turn start would have made identically [I: event completeness is checked by design
   1's equal-result hashes for the trade paths].
   *Writes*: `ParallelPremise.csv`, per turn and player: the two builds' ms and the number of input events before each.
   *Into a bound*: the ms of the builds with no event, as a share of the turn, times (1 - 1/cores) and the series' turn.
   No shadow build is made (a second build at turn start would use the shared step finder and could change later
   results).

**Built and run (ticket 4669, 2026-11-17)**: all three as designed, with these differences: the input events are counted
per turn and type in `VP64CountTurns.csv` (one line per event would have been millions of lines: 164,000 visibility
changes per H turn), not in `TradePathCount.csv`; war and peace are counted at `CvTeam::setAtWar` on a change (it covers
`declareWar`, `makePeace` and the vassal and defensive-pact paths); the reach key also holds the finder object and every
`SPathFinderUserData` field incl. `eRoute`, `eBuild`, `eRoutePurpose`, `bUseRivers` and the ZOC list; callers are
classified from symbolised call stacks, not from markers in the callers; the parallel counter also records every later
build of the turn, which showed that only first builds could move. Results in the ranking above and in
`turn-time-study.md`.

**Not verified (9.8):** every bound (shares on a shared machine; H's turn time is an indication on another server
build); the PGO range (literature, not measured); every memory figure marked [I]; whether a pool would change any
pointer-ordered iteration.

## 10. Findings

(none yet)
