# Seconds per AI turn on the pins: baselines, profiles and the first hot-spot list (cycle 1, 2026-11-11)

Written 2026-11-11 by the performance standards team's cycle-1 lead (tracker 7000; tasks 7002 tool, 7003 sampler,
7004 hot spots). Every number here was measured with `perf/turnbench/turnbench.py` and `perf/sampler/sampler.py`
on this machine (i9-13900KS, 8 P-cores with HT + 16 E-cores = 32 logical, Windows 11 10.0.26100), headless, on the
builds of the current pins (COMMON rule 18):

**Old pins, not re-measured at the pin move of 2026-11-11** (note of one-off 9029, 2026-11-11). Every workstation number
in sections 1-8 below was taken on the pins ccfd6dd4d / c43ea1b9f named in this table. Since 14:10 EST that day the
pins are bbfacca1a / 76992d235 (upstream fix batch 1, which changes game results on huge from turn 359 and Standard
from turn 143). Do not compare a new-pin run with these numbers; the new pins' timing evidence is section 9 (quiet
testbed, huge 350-355, AI logging off). **The current baselines on the new pins are section 11** (perf 7010, 2026-11-12:
huge and Standard 0-50, AI logging on and off, quiet testbed). The workstation's grade-B numbers of sections 1-3 were
not re-measured on the new pins and will not be (7010 point 3, closed by the MC 2026-11-12: timing series run on the
quiet testbed, COMMON rule 23); they are old-pin history.

| Arm | Build | SHA-256 of the DLL |
|---|---|---|
| x64 | server + DLL `<pins_dir>/dlls/x64-c43ea1b9/bin` (`x64` c43ea1b9f, D35 pragmas in) | `7ea44fc3...6706` |
| x86 | `<pins_dir>/dlls/i0ship-ccfd6dd4/CvGameCore_Expansion2.dll` (`i0-ship` ccfd6dd4d, D35 pragmas in) in civ5-headless `c5h` (the workstation's `civ5headless.toml` with `db` and `loc_db` of the pinned set, `--game-dir` from turnbench: the corrected configuration; added 2026-11-12, 7010 point 4) | `3e1a7fc7...00b6` |
| x64 server.exe | `9bbce03e...` (the old pin folder's server, before bug 0094's fixes) | |

**Machine configuration change** at ~02:25 EST on 2026-11-11: a page file on D: (32-64 GB) was added, commit limit
42.8 -> 74.6 GB (`machine-commit.md`). Every series in this file ran before it. The turn loop is CPU-bound and the
hosts used 0.2-0.35 GB, far from the old limit, so the change should not move CPU time [I]; no before/after pair was
run in cycle 1 (the next lead's first series is the check).

Games: **huge** = a copy of `VP8P-HUGE_0350` (SHA-256 `c499d3c6...5188c3`), turns 350 -> 355; **Standard** =
civ5-headless `continents-standard-fixed-seed.json`, new game, turns 0 -> 50. Statistics logging on (the pinned
database, `SQLITE_LOGGING` 1, with D35's pragmas). Metric: mean of the host's own per-turn times over the turns
played (boot and load excluded). Raw data: `D:/c5-64bit-work/7000/tb-*/turnbench.json` (scratch, not committed;
the numbers below are the record).

## 1. Baselines (both hosts' default: the DLL's AI logging ON, flags 15)

| Game | Arm | n | mean s/turn | sd | 95 % CI | min-max | load-adjusted at 25 % total CPU | boot+load s | peak WS / private MB |
|---|---|---|---|---|---|---|---|---|---|
| huge 350-355 | x64 | 6 | **8.259** | 0.541 | 7.69-8.83 | 7.53-8.83 | 7.61 | 13.8 | 284 / 339 |
| huge 350-355 | x86 | 6 | **10.119** | 1.024 | 9.05-11.19 | 8.39-11.51 | 9.26 | 19.1 | 327 / 285 |
| Standard 0-50 | x64 | 6 | **0.302** | 0.034 | 0.267-0.338 | 0.271-0.348 | 0.299 | 13.2 | 174 / 232 |
| Standard 0-50 | x86 | 6 | **0.548** | 0.069 | 0.475-0.620 | 0.427-0.622 | 0.510 | 19.7 | 188 / 181 |

- Series: huge 2026-11-11 00:41:42-00:56:57, Standard 00:21:48-00:31:08, interleaved (rounds of both arms, order
  shuffled), one process at a time, quiet-machine lock held [L].
- **x86 is slower than x64**: huge +1.86 s/turn (+22.5 %, 95 % CI +0.74..+2.98); Standard +0.246 s/turn (+81 %,
  CI +0.17..+0.32); load-adjusted +21.7 % and +70.8 %. Per-turn means (huge) x64 10.16, 8.36, 7.71, 7.51, 7.55;
  x86 11.94, 11.36, 9.69, 8.91, 8.69. Standard has one heavy turn: turn 20 took 2.24 s (x64) and 4.89 s (x86), 7-9x
  its neighbours, in every run [L, not explained].
- **Grade B, not A**: the lock was breached during both series (other teams started three real game EXEs, 18
  headless host processes and C++ builds during the huge hold; recorded by `perf/quietlock`'s monitor; owners named
  by the MC, issue 7000). Three Standard runs overlapped another breach (00:27:35-00:28:49, one-off 9016's test
  suite); their values and the load samples showed no effect, so they were kept.
- **Machine load, not the build, is the main source of spread** [L]: the correlation between a run's mean total CPU
  (all 32 logical CPUs, sampled every 10 s) and its s/turn is 0.97 (x64) and 0.98 (x86) on huge, 0.88 and 0.81 on
  Standard. Each +10 % of total CPU (about three busy logical CPUs elsewhere) adds 0.69-0.84 s/turn on huge (about
  9 %) and 0.03-0.05 on Standard; after the linear load fit the residual spread is only 0.13-0.18 s/turn (huge,
  1.6-1.8 %) and 0.016-0.041 (Standard). The host is single-threaded, so it is not starved of cores; the likely cause
  is the CPU's turbo and core placement (fewer boost bins with more active cores; the thread landing on an E-core)
  [I, not measured]. Consequence: a comparison of two builds is only as good as the load balance between their runs;
  turnbench prints the load fit and a load-adjusted comparison next to the plain one.
- Older numbers (9.0 / 10.4 s/turn huge, 1.2-1.6 s/turn Standard 0-200) were other builds, other turn ranges and,
  for Standard, mostly the statistics database's disk syncs: history (inventory.md).

## 2. Where the time goes (sampling profiles of the turn loop)

One sampled run per arm, CPU profile (each stack weighted by the cycles the thread used), stacks under
`CvGame::update` / `doTurn` / `updateMoves`; the sampler slowed the target by 15-26 %. Percentages are of the turn
loop's cycles; "inclusive" counts a function once per stack. Profiles:
`D:/c5-64bit-work/7000/tb-*/profile-*/report.txt` and `folded.txt`.

**Huge 350-355 (x64 / x86 inclusive)**: unit AI `AI_unitUpdate` 54 / 54 %; pathfinding
`CvAStar::FindPathWithCurrentConfiguration` 41 / 35 % (heap operations on the open list alone 4-5 % self:
`_Pop_heap_hole_by_index` / `_Adjust_heap`); tactical `ProcessDominanceZones` 27 / 29 % with
`FindAndExecuteBestUnitAssignments` 19 / 20 %; homeland 27 / 25 %; **trade-path cache `UpdateTradePathCache` 21 / 21
%** (via `HavePotentialTradePath`, and religion's `IsCityConnectedToCity` 9 / 10 %); operations `CvAIOperation::Move`
13 / 14 %; `GetBestSettlePlot` 6 / 11 % (the logging path, section 3). Self leaders: A* heap, `TradePathWaterValid`
and `TradePathLandValid` (2-4 % each), `plotDistance` 3.5 %, `CvPlayerAI::getPlayer` 3 %, heap allocation 3 % +
free 1.4 %, `ZwDelayExecution` 3 % (below).

**Standard 0-50, AI logging on (x64 / x86 inclusive)**: `GetBestSettlePlot` **50 / 60 %**,
`std::ostream::operator<<` **38 / 42 %**, `CvPolicyAI::WeightPolicyAttributes` 38 / 49 %, `CheckUnitBuildSanity`
40 / 51 %; locale and critical-section functions of the C++ stream library lead the self list (x64:
`__lc_locale_name_func` 6 %, `locale::facet::_Incref` 6 %, `RtlLeaveCriticalSection` 6 %, `RtlEnterCriticalSection`
4 %). That is string formatting, not game logic (section 3).

**Standard 0-50, AI logging OFF (x64)**: `setTurnActive` 55 %, unit AI 40 %, pathfinding 25 %, homeland 22 % (of which
explorer moves 14 %, `GetBestExploreTarget` 10 %), dominance zones 17 %, settle-plot values 7 %, unit-assignment
search 10 %; heap allocation and free 9 % of the loop (spread over many callers; the largest are the tactical
assignment search 1.5 % and `CvBasePosition::wipe` 0.9 %); `ZwLockFile` 1 % (the statistics database).

## 3. Hot spot 1: the DLL's AI logging in every headless run [C source, L profile]

Both headless hosts call `SetLogging(true)`, `SetAILogging(true)`, `SetAIPerfLogging(true)`,
`SetBuilderAILogging(true)` and `SetPlayerAndCityAILogSplit(true)` at boot (server `srv_main.cpp`: `dll_log_flags`
default 15, "as the 32-bit reference"; c5h `HHMain.cpp`: game logging on by default because DLLs before the logging
fixes gave different AI results without it). A player with `LoggingEnabled = 0` in `config.ini` (the game's
default) runs none of this. With the flags on:

- `CvPlayer::GetBestSettlePlot` (`CvPlayer.cpp:47669` at c43ea1b9f) takes `bLogging = GC.getLogging() &&
  GC.getAILogging()`; then it (a) recomputes `ignorePlots` over every plot of the map, (b) for every candidate plot
  calls `CvSiteEvaluatorForSettler::PlotFoundValue` with a debug string instead of the cached
  `pPlot->getFoundValue(eOwner)`, and (c) writes a CSV line per plot into a `std::stringstream`, then opens
  `CitySites_<civ>_<turn>.txt` through the log manager.
- It is called from `CvUnitProductionAI::CheckUnitBuildSanity` (`CvUnitProductionAI.cpp:1101`, settlers) which
  `CvPolicyAI::WeightPolicyAttributes` (`CvPolicyAI.cpp:3798`) calls for every unit class while weighing every
  policy, and from city production choices, so the full-map evaluation and dump run many times per player turn.
- Measured A/B (same builds, AI logging on vs off, Standard 0-50): see the table below. The sampled runs alone gave
  x64 0.380 vs 0.158 s/turn.

**A/B, Standard 0-50, AI logging on vs off** (2026-11-11 01:08-01:24, 6 interleaved runs per arm, four arms
in shuffled rounds; `turnbench --arm x64off=... --ai-logging-off-arms x64off,x86off`) [L]:

| Arm | mean s/turn | sd | 95 % CI | load-adjusted at 25 % CPU |
|---|---|---|---|---|
| x64, AI logging on | 0.319 | 0.028 | 0.290-0.349 | 0.315 |
| x64, AI logging off | **0.164** | 0.022 | 0.141-0.186 | 0.156 |
| x86, AI logging on | 0.530 | 0.035 | 0.494-0.566 | 0.525 |
| x86, AI logging off | **0.214** | 0.023 | 0.190-0.237 | 0.207 |

- Switching the DLL's AI logging off cuts a Standard turn by **49 %** on x64 (-0.156 s/turn, 95 % CI -0.189..-0.123)
  and by **60 %** on x86 (-0.316 s/turn). The profile of the off arm shows `GetBestSettlePlot` falling from 50 % to
  8 % inclusive and the stream library leaving the top of the list (section 2).
- **Behaviour is identical** [L]: the `stats.db` of the on and off runs are identical in all 15 tables (19,016 rows,
  turns 0-50) for x64 and for x86 (`tools/d10/statscmp.py`, run 1 of each arm), as are two runs of one arm (A/A).
  So on the pins the logging path changes only time, not AI decisions, at least to Standard turn 50.
- **x64 against x86 for the player's workload** (logging off): 0.164 vs 0.214 s/turn, x86 +30 %. The larger gap with
  logging on (+66 %) is mostly the logging path, which costs the 32-bit build more (its MSVCP90 stream and locale
  code takes a critical section per character class lookup) [I from the profiles].
- Disturbance: epic 2800 launched a real EXE on c5h-a at 01:10:15 (to ~01:20) under this hold (reported by 2800);
  rounds 1-4 overlap it. The effect is far larger than the load spread (load fit residual sd 0.014-0.027 s/turn),
  so the conclusions stand; the absolute values are grade B.

**A/B, huge 350-355, x64, AI logging on vs off** (2026-11-11 01:39-01:48, 6 interleaved runs per arm, a quiet
hold: total CPU mean 17.4 %, max 36.9 %, no other team's game or host started, only a `du` of team 3100 and this
team's own gate demonstration before the series; grade **A**) [L]:

| Arm | mean s/turn | sd | 95 % CI | min-max | boot+load s |
|---|---|---|---|---|---|
| x64, AI logging on | 7.498 | 0.374 | 7.105-7.891 | 7.127-8.063 | 12.0 |
| x64, AI logging off | **6.713** | 0.224 | 6.478-6.949 | 6.382-7.077 | 11.3 |

- Off minus on: **-0.785 s/turn (-10.5 %)**, 95 % CI -1.195..-0.374; load-adjusted -8.9 %. On the huge save the
  logging path is a much smaller share than on Standard because the late game's turn is dominated by unit AI and
  pathfinding (section 2), and the settle-plot search is called less once the map is full [I: call counts not
  measured].
- The same build measured 8.259 s/turn in the busy 00:41 series and 7.498 here: the 10 % difference is the machine
  load (mean total CPU 32.8 % there, 18.6 % here), consistent with the load fit of section 1.

**The D10 save test with AI logging off (decision D49's acceptance)** [L]: `perf/turnbench/d10speed.py`, run_d10
`--games tiny --allow-subset`, arms `--ai-logging off` / `on`, 6 interleaved rounds, 2026-11-11 01:58-02:20, one
run_d10 at a time (its own 12 jobs inside), lock held. **All 12 runs PASS** (6 with logging off in both arms).
Wall time (5 s resolution): off 160, 110, 105, 105, 90, 95 s; on 105, 110, 105, 110, 115, 115 s. Round 1 overlapped
two disturbances (one-off 9019's ASan run 01:57:48-02:02:45, started before the hold; team 3000's test suite
`test_host.exe` / `shell_server.exe` 01:59-02:00, started under the hold); without round 1: off 101 s (sd 8.2), on
111 s (sd 4.2), about -9 %, not significant at n = 5. Inside the runs the hosts' turns are faster (x86 direct Tiny
0-100: 0.26 -> 0.147 s/turn, wall 45 -> 34 s), but the Tiny game's wall time is ruled by process boots (~11-16 s
each) and the test's dependency chain, so the gain hardly shows; Standard and huge, with longer games, gain more
[I: not measured as D10 runs in this cycle]. A full three-game run_d10 with logging off: section 8 (part 2 passed on all three games; parts 1 and 3 could not run).

## 4. Not a hot spot after all: `Sleep(1)` inside the tactical search [C source, L profile]

`TacticalAIHelpers::FindBestUnitAssignments` (`CvTacticalAI.cpp:12423-12429` and `12853-12859`) releases the game-core
lock and calls `Sleep(1)` every 500 positions examined ("be a good citizen and let the UI run"). Both hosts hold the
lock during the turn, so it runs headless too. In the CPU profile it costs ~3 % (the cycles of entering and leaving
the kernel), but a `Sleep(1)` waits for the next timer tick: up to 15.6 ms at the default timer resolution, so its
wall-clock cost could be much larger than its CPU cost. Measured with the sampler's wall-clock mode (`--wall`,
every thread every tick, weight 1; huge 350-355, x64, AI logging on, 2026-11-11 01:37): stacks ending in
`SleepEx` / `ZwDelayExecution` are **1.0 %** of the turn loop's wall-clock samples (2,293 samples) [L]. So the sleeps
return after about a millisecond in this process (the timer resolution in effect is fine-grained) and the yield
costs ~1 % of a huge turn: not worth a DLL change now. (Negative result; the CPU profile's 3 % is the cost of the
kernel transitions.)

## 5. Hot spot 3: the trade-path cache is rebuilt every turn for every major [C source, L profile]

`CvGameTrade::UpdateTradePathCache` (`CvTradeClasses.cpp:363`) runs once per player per turn (counter per turn) and,
for every city of the player, two multi-destination path searches (`GetMultiplePaths`, water and land) to every
city on the map within trade range, then fills the cache. On the huge save this is 21 % of the turn loop in both
builds. The result depends on plots, borders, war state and routes, most of which do not change between two turns;
an incremental invalidation would keep the cache identical with much less work, but proving identity is the cost
(epic proposal, `perf/TEAM-STATE.md`).

## 6. Other observations

- **Boot**: `CacheGameDatabaseData` is ~80 % of the server's boot CPU; inside it SQLite B-tree inserts (temporary
  B-trees for sorted or indexed reads) are 41 %, `PopulateArrayByValue/Existence` 34 % [L, Tiny smoke profile
  2026-11-11]. Boot+load is 11.6-15.8 s (x64) and 16.5-21.7 s (x86) in the series above.
- **x64 is faster than x86 at equal load** (huge -18 %, Standard -41 % with AI logging on); the Standard gap is
  mostly the C++ stream library's cost in the logging path (MSVCP90 locale locks against MSVCP140) [I from the
  profiles; the AI-logging-off comparison in section 3 settles it].
- Memory: peak private bytes 339 MB (x64) and 285-320 MB (x86) on huge 350-355; 232 / 181 MB on Standard 0-50.

## 7. Tool facts learned (negative results included)

- Kernel sampling (WPR / xperf, ETW profile source) needs administrator rights on this machine; not used. `cdb -pv`
  stack samples take 1.2-1.7 s each (LOADTIME.md), too slow for a profile. The user-mode sampler (`SuspendThread` +
  `GetThreadContext` / `Wow64GetThreadContext` + dbghelp `StackWalk64` from the SDK's Debuggers folder) walks a
  stack in 1-3 ms, needs no rights for a process it started, and resolves the DLL's symbols from the PDB next to
  the image (the search path must name the image folders: `SymSetSearchPathW`; without it only exports resolve).
- Weighting samples by `QueryThreadCycleTime` deltas is required: an unweighted sampler counted idle thread-pool
  threads (whose cycle counters move slightly) as 74 % of all samples.
- `psutil.process_iter` over ~550 processes takes ~3.6 s here (7005), too slow for a 5 s load monitor.
- **A hold began while jobs that had passed the gate were still running (2026-11-11, fixed by 7008).** Four series
  that night: run_d10 started 02:21 ran inside 2800's hold to 02:36; a verifier's AddressSanitizer run from 01:57
  inside a hold taken 01:58; another verifier's builds 02:21-02:24 and a fuzz run to 03:08 inside a hold taken 02:55.
  Cause [C, code]: `quietlock.py run` and `gate.require()` check the lock once, at the start, and `take` knew nothing
  of running work. Fix: a launch that passes `gate.require()` (and `quietlock.py run`) writes
  `D:/c5-64bit-work/locks/quiet-machine.jobs/<pid>-<create time ms>.json` (who, what, class, pid, start, command) and
  removes it at exit; a killed job's entry is stale (pid gone or create time differs by > 1 s) and the next reader
  removes it. `take` waits up to 20 min (`--job-wait`) for other owners' `heavy` jobs, naming each; `--overlap` takes
  at once and writes them into the state and the history line (`"disturbed": true`). Premises checked [C, code]: the
  lock folder holds `quiet-machine.lock`, `.state.json` (only during a hold), `.history.jsonl`,
  `.<stamp>.load.jsonl` and `launches.jsonl`; every gated launch point is a Python process that waits for its work
  (`subprocess.run`/`call`) except the harness, whose game outlives `launch()`: the harness registers the game's pid,
  not its own. The harness's hermetic fixture (`C5_QUIETLOCK_DIR`) redirects the registry too. Unit tests only; no
  live hold has used it yet [I for the live behaviour]. Details: `perf/README.md`, "Running jobs".
- **The lock's files and the compatibility rule (7009, 2026-11-11) [C, code and unit tests; no live hold yet].**
  Files in `D:/c5-64bit-work/locks/`: `quiet-machine.lock` (one line, `<who> <what> start <YYYY-MM-DD HH:MM> expected
  end <YYYY-MM-DD HH:MM>`; any file counts as held; the gate reads only the first word); `quiet-machine.state.json`
  (during a hold: `who, what, start, expected_end, line, stamp, load_file, holder_pid, monitor_pid, monitor_ctime`;
  7008 added `overlap_jobs, idle_jobs, disturbed`; 7009 added `planned_end` (ISO, = `expected_end`) and
  `planned_minutes` (int)); `quiet-machine.history.jsonl` (one line per release: `who, what, start, end, load_file,
  disturbed, overlap_jobs, disturbance`; 7009 added `planned_end`; override lines `{"kind": "override", ...}` have no
  `end`); `quiet-machine.jobs/<pid>-<ctime ms>.json` (7008); `launches.jsonl` (7007 launch records; 7009 adds
  `{"kind": "heavy-refused", "free_commit_gb", "floor_gb", "largest": [...]}`); and, new in 7009,
  `quiet-machine.queue.json` (`{"version": 1, "waiters": [{"who", "what", "joined", "turn_since"}]}`, in order),
  `quiet-machine.queue.log.jsonl` (join, update, leave, move with `by`, skip, took) and the transient
  `quiet-machine.queue.mutex`. **Rule for every change to these files:** code from older worktrees runs against the
  same folder for hours after a merge, so fields are only added, never renamed, removed or retyped; anything older code
  must not act on goes into a new file it never reads. What the old take reads from the history is only `who` and `end`
  (the 10-minute rule): a new line kind must not carry both. Shown by `perf/quietlock/tests/test_compat_old_code.py`,
  which runs commit 483d8f8's code (verbatim in `tests/fixtures/old_483d8f8/`) on folders written by the new code and
  back. Known gap until every worktree has merged 7009: an old `take` does not see the queue and takes a free lock over
  waiting teams.
- **Python 3.8 on the test machines (7009) [C, static check; not run on a 3.8 interpreter].** The testbed's system
  Python is Anaconda 3.8.5 (psutil 5.7.2); `gate.py` failed there at import with `TypeError: unsupported operand
  type(s) for |: 'type' and 'NoneType'` (a `str | None` annotation in a signature, evaluated at definition time
  without `from __future__ import annotations`), and the launch went on unregistered
  (`research/06-tooling-and-workflows/quiet-testbed.md`). `quietlock.py` had the future import but called
  `str.removesuffix` (3.9). Constraints for any file there: `from __future__ import annotations` where `X | None` or
  `list[int]` appear in signatures or module/class-level annotations; no `X | None` or `list[...]` outside
  annotations; no `match`, parenthesised context managers, `removeprefix/removesuffix`, `dict | dict`,
  `functools.cache`, `zip(strict=)`, `zoneinfo`. `ast.parse(src, feature_version=(3, 8))` on Python 3.11 catches the
  grammar (match, parenthesised `with`) but not the run-time annotation or library cases; `tests/test_py38.py` adds an
  AST scan for those. It cannot see `|` between two dict variables or calls on objects of unknown type.
- **The heavy-start memory floor (7009) [C, code and unit tests].** `gate.require()` and `quietlock.py run` refuse below
  8 GB free commit (`GlobalMemoryStatusEx.ullAvailPageFile`, 7007's reading). To name the three largest processes it
  scans every process with `psutil.process_iter(['pid', 'name', 'memory_info'])` (private bytes); this runs only on a
  refusal. Game and server starts keep 7007's floors (6 GB, 2 GB): the harness and `server/run_server.py` pass
  `require(..., memory=False)` and apply their own floor next (MC correction 2026-11-11 16:26; the first 7009 version
  made both need 8 GB in effect, which would refuse small parallel headless validation runs for no gain).

## 8. The full D10 run with AI logging off (2026-11-11 02:21-02:35) [L]

`tools/d10/run_d10.py` (all three games, `--ai-logging off` by default, the pinned builds named), started through
`quietlock.py run` at 02:21:00 while the lock was free. Epic 2800 took the lock at 02:21 ("c5h-a frame-rate series",
to 02:51); from then on the launch gate in `run_server.py` refused **16 x64 server starts** (exit 3, "the
quiet-machine lock is held"), so parts 1 and 3 FAILED for lack of runs, not for a difference. **Part 2 (behaviour
identity, x86 against x64 direct plays) PASSED on all three games with AI logging off** (tiny 0-100, Standard 0-200,
huge 350-400). Not a valid acceptance run of the whole test; the six tiny runs of section 3 are. The run also showed a
gap, fixed the same night: run_d10 started its x86 `c5h` runs without the gate, so those kept loading the machine
during 2800's hold; `Hosts._go` now calls the gate before every host start (refused runs fail with the gate's
reason). Report: `D:/c5-64bit-work/7000/d10-full-ailog-off/report.txt` (scratch). Rerun the full test next cycle.

## 9. Pin move 2026-11-11: old against new pins on the quiet testbed (one-off 9029) [L]

Quiet testbed job `20261111-145527-5e8d` (oldpc, 2026-11-11 14:55-15:13 EST; quiet record: alone, undisturbed, foreign
cores 0.02, machine CPU mean 13.1 %, free commit 34.7 GB throughout), `tools/testbed/jobs/headless_series.py`, huge save
`TB-HUGE_0350` (`c499d3c6...`), turns 350 -> 355, statistics logging on (pinned database), **DLL AI logging off in every
arm**, 6 runs per arm, one process at a time, arm order rotated every round; metric as section 1. Grade A.

| Arm | Build | mean s/turn | sd | 95 % CI | min-max | boot+load s | peak WS MB |
|---|---|---|---|---|---|---|---|
| x64-off | new pin `x64-76992d23` (DLL `fa75f483...`, server.exe `a1d7bdd6`: the first 0094 fix, replaced the same day by `3a35330a`; added 2026-11-12) | **5.905** | 0.011 | 5.893-5.917 | 5.893-5.922 | 8.78 | 263 |
| x64old-off | old pin `x64-c43ea1b9` (`7ea44fc3...`; that folder's server.exe [I: `9bbce03e`, not in the series record]) | 5.925 | 0.019 | 5.905-5.945 | 5.897-5.944 | 8.70 | 280 |
| x86-off | new pin `i0ship-bbfacca1` (`f7c64840...`) | **6.996** | 0.023 | 6.971-7.020 | 6.967-7.026 | 13.54 | 326 |
| x86old-off | old pin `i0ship-ccfd6dd4` (`3e1a7fc7...`) | 6.869 | 0.022 | 6.846-6.892 | 6.840-6.904 | 13.45 | 327 |

- **x64, new minus old: -0.021 s/turn (-0.4 %), 95 % CI -0.042..+0.000**, not significant (smallest detectable
  difference about 0.025 s/turn). Peak working set -17 MB.
- **x86, new minus old: +0.127 s/turn (+1.8 %), 95 % CI about +0.098..+0.156** (Welch, from the table), significant.
  It is the same on every turn (per-turn means new/old 8.52/8.40, 7.18/7.06, 6.77/6.62, 6.20/6.09, 6.30/6.17), so it is
  a cost per turn, not one turn. The two builds play the same game in these turns (results differ from turn 359,
  `../03-dll-engine-boundary/upstream-fix-batch-1.md` section 2), the configuration is the same for both arms (one job),
  so neither the game nor the configuration explains it.
  **Why: see section 10 (perf 7010, 2026-11-12): commit 17 alone, through the c5h test host's per-call accounting
  and a machine-dependent remainder; the product is not affected.** (Replaced: "Why: not established".) The text of
  2026-11-11 follows. One sampled run per arm on the workstation (`turnbench --profile-only`, 350 -> 353,
  15:14-15:17, attribution only; reports and the inclusive-share diff in `tools/testbed/evidence/9029/profiles/`) shows
  `CvPlayerTrade::UpdateTradeStats` at 0.51 % inclusive on the new build against 0.04 % (commit 6cc672569 rebuilds the
  cached trade-route counts when routes change; about +0.03 s/turn of the +0.127), and shifts of 2-3 points between AI
  functions (explorer and path finding up, trade path cache down) that one sampled run cannot separate from sampling
  error. The x64 build of the same source shows no slowdown, which argues for an x86-only cause (code layout of the
  32-bit LTO build, or c5h's per-call engine bookkeeping) over more AI work [I]. Open for perf issue 7010: three or more
  profiled runs per arm, both bitnesses.
- x86 against x64 on the new pins: +1.091 s/turn (+18.5 %, 95 % CI +1.066..+1.116).

**c5h configuration of both x86 arms** (corrected 2026-11-11 by 9029 after 9034's finding): civ5-headless with
`loc_db = E:/c5-testbed/base/civ5-headless/pinned/vp-modpack-5.4.8-cdrift/Localization-Merged.db`, `game_dir =
E:/c5-testbed/base/toolkit-game/Sid Meier's Civilization V`, database `.../vp-modpack-5.4.8-cdrift/Civ5DebugDatabase.db`,
CP + VP mods, no add-ins (recorded in `series.json` `meta.c5h_config`). **Every earlier x86 testbed series (9016's sets
A-C) ran without `loc_db` and `game_dir`** (c5h then took the logged-on account's localization database and discovered
its own game folder); those numbers are not comparable with these. `--c5h-legacy-config` reproduces the old form.

**Not covered on the new pins** (perf issue 7010): Standard 0-50, every AI-logging-on arm, the workstation's grade B
series of sections 1-3 (no workstation baseline was taken at the pin move, MC 2026-11-11). Evidence:
`tools/testbed/evidence/9029/series/` (`turnbench.txt`, `turnbench.json`, `series.json`, `quiet-summary.json`).

## 10. Why the new x86 pin is slower: commit 17 and the test host's engine-call accounting (perf 7010, 2026-11-12)

**The code [C].** Upstream commit 6cc672569 (our 17th of batch 1, fork bbfacca1a, "cached trade route counts")
rewrites `CvPlayerTrade::UpdateTradeStats` (`CvTradeClasses.cpp`): it now calls `gDLL->IsGameCoreThread()` on **every
call, cache hits included**, to choose the game core's copy or the UI's copy of the counts (and `HasGameCoreLock` /
`TryGameCoreLock` on a UI thread); `ResetTradeStats` is also called from `ClearTradeRoute` now, so the counts are
rebuilt more often. Every reader of the route counts (`GetNumberOfTradeRoutesFromCity`, `...CityStateTradeRoutes`,
`GetNumberOfTradeRoutesCity`, ...; called from yield and AI valuation code) pays the engine call.

**What an engine call costs in each host [C, source].**
- c5h (the 32-bit headless TEST host, `~/Documents/civ5-headless/host/engine/HHRuntime.cpp`, `Hit()`): every engine
  method, implemented or stub, enters a critical section, builds a `std::string` from the method name (the names are
  over 15 characters, so a heap allocation and free) and looks it up in a `std::map<std::string, CallEntry>` (string
  compares over the common prefix `ICvEngineUtility1::`) to count it. `HHEngineUtility::IsGameCoreThread` is
  `HH_IMPL_CALL(...); return m_bInGameCoreUpdate;` (`HHEngineServices.cpp:357`).
- The x64 server (`server/src/srv_ifaces.cpp:69-73, 383`): one `InterlockedIncrement` on a fixed array slot and a
  thread-local read.
- Through the proxy (the product route) `IsGameCoreThread` is **server_local** (`proxy/model/policy-table.tsv` row
  440, `../03-dll-engine-boundary/proxy-design.md` section 5): the server answers it itself and it **never crosses**
  to the EXE. On the stock route the 32-bit DLL asks the EXE's own engine utility. So the extra calls of commit 17 cost
  the product nanoseconds each; only c5h pays a map lookup per call.

**Profile share of c5h's accounting on the old pin [L, a re-read of this file's section-2 profile]**: in the sampled
x86 run of huge 350-355 (old pin ccfd6dd4d, AI logging on, workstation 2026-11-11 00:4x,
`D:/c5-64bit-work/7000/tb-huge-2026-11-11/profile-x86/folded.txt`), stacks through c5h's `Hit` are **3.13 %** of the
turn loop's cycles, 1.76 % under `IsGameCoreThread` and 0.57 % under `HasGameCoreLock` (Standard 0-50: 1.52 %). In
9029's sampled pair (section 9) `std::_Tree<...CallEntry...>::_Lbound` is 0.9 % self and `memcmp` under it a further
0.8 % on the new build.

**Call counts [L, the hosts' own counters].** c5h's `Logs/Headless.log` (huge 350 -> 400, AI logging on, one-off 9020's
runs `D:/c5-64bit-work/9020/cmp1b/runs/{pin,head}` and `cmp-s16/runs/s16`): `IsGameCoreThread` **33.24 M** on the old
pin, **33.53 M** on s16 (every batch commit but 17), **43.87 M** on head (all 17): commit 17 adds about 10.3 M calls
over those 50 turns (the games differ from turn 359, so the per-turn split is approximate). Tiny continuation 25-100:
87,862 -> 331,167. The server counts the same calls (`server-report.txt`, "ICvEngineUtility1::IsGameCoreThread").

**Series A, the bisect on oldpc [L]** (quiet job `20261112-213817-b7ba`, 2026-11-12 21:55-22:25 EST, nobody logged on,
`alone`/`undisturbed` true, foreign 0.029 cores, machine CPU 13.7 %; `perf/tbjobs/series.py`, runner of 9033.2;
huge `TB-HUGE_0350` 350 -> 355, statistics logging on, DLL AI logging off, c5h with `loc_db`/`game_dir` unless noted,
6 rotated rounds, one process at a time; builds: old pin `i0ship-ccfd6dd4` `3e1a7fc7`, 9020's cumulative bisect builds
`s09-051c5503` (`edddb055`, commits 1-9) and `s16-a5f53048` (`1bd6d2a3`, commits 1-16), new pin `i0ship-bbfacca1`
`f7c64840`; evidence `perf/evidence/7010/A/`). Grade A.

| Arm | mean s/turn | sd | cv | 95 % CI | minus old pin (95 % CI) |
|---|---|---|---|---|---|
| old pin (ccfd6dd4d) | 6.897 | 0.008 | 0.12 % | 6.888-6.905 | - |
| s09 (batch commits 1-9) | 6.898 | 0.016 | 0.23 % | 6.881-6.914 | +0.001 (-0.016..+0.019), not significant |
| s16 (commits 1-16) | 6.914 | 0.013 | 0.19 % | 6.900-6.927 | +0.017 (+0.003..+0.032), +0.25 % |
| new pin (all 17) | **7.014** | 0.007 | 0.10 % | 7.006-7.022 | **+0.118 (+0.107..+0.128), +1.70 %** |
| new pin, c5h legacy config (no `loc_db`/`game_dir`) | 7.014 | 0.013 | 0.18 % | 7.001-7.027 | +0.118; **= new pin to the millisecond** |

- **Commit 17 alone** (s16 -> new) costs **+0.100 s/turn (+1.45 %)** on oldpc; commits 10-16 about +0.25 %; commits 1-9
  nothing measurable. 9029's +1.8 % (2026-11-11, another set) is reproduced as +1.70 %.
- **Engine calls in turns 350-354** (c5h's counters, round 1; identical in every build that shares the code path):
  `IsGameCoreThread` **3,243,227** (old, s09, s16) -> **4,182,040** (new): **+938,813 (+29 %), ~0.19 M per turn**;
  `HasGameCoreLock` +3, `GetGameCoreLock`/`ReleaseGameCoreLock` unchanged, `TryGameCoreLock` 0 (no UI thread headless).
  Per turn (count runs to 351 ... 355, differences): +237 k, +171 k, +206 k, +156 k extra calls for turns 351-354
  against +0.094, +0.122, +0.101, +0.126 s: **0.40, 0.71, 0.49, 0.81 us per extra call**, not one constant [L]. Mean
  ~0.63 us per extra call on oldpc.
- **The same pair on the laptop** (series D, quiet job `20261112-212915-eade`, 21:29-21:45, the owner logged on as always
  there, foreign 0.036 cores, `undisturbed`; evidence `perf/evidence/7010/D/`): new 8.207 vs old 8.166 s/turn: **+0.042
  (+0.51 %, 95 % CI +0.013..+0.070)**, i.e. ~0.2 us per extra call. x64 new pin 7.035 (cv 0.46 %).
- **c5h's legacy configuration does not move turn time** (point 4 of 7010; new pin with and without `loc_db`/
  `game_dir`: 7.014 both, difference 0.000 +- 0.014). So the older x86 testbed series (9016 sets A-C, 9029's capacity
  and the laptop capacity job) are comparable in time with corrected ones of the same build and machine state; their
  `stats.db` text columns are not (TXT_KEY names).

**Explanation, as far as it goes [C for the mechanism, L for the numbers, I where marked]:** the slowdown is commit 17
and nothing else of the batch; with it c5h counts ~0.19 M more engine calls per huge turn, each through a critical
section, a heap-allocated `std::string` and a string-map lookup. On the laptop the time per extra call (~0.2 us) is what
the old pin's profile share of `Hit` gives (~0.18 us per call, section above); on oldpc it is about three times that and
varies 2x between turns. So on the laptop the extra engine-call accounting explains the whole difference; on oldpc it
explains about a third, and the remaining ~1 % is machine-dependent [I: both CPUs are Skylake-family, oldpc 8 MB L3
against 6 MB; the likely candidates are code and data placement (the 32-bit LTO build's layout moved with commit 17)
and the Skylake jump-alignment microcode penalty, whose mitigation state differs by microcode/OS (Windows 10 on oldpc,
Windows 11 on the laptop); not measured]. The extra rebuilds of the route counts (`ResetTradeStats` from
`ClearTradeRoute`) are the same work in both bitnesses and cannot be large: x64 shows nothing (-0.4 %, n.s., 9029).

**Why x64 does not show it [C, source; L, 9029's x64 pair -0.021 s/turn n.s.]:** the server counts an engine call
with one `InterlockedIncrement` (a few ns): 0.19 M extra calls are ~1-4 ms per turn, under the 0.025 s/turn the x64
series could detect.

**What it means for the product [C]:** nothing measurable. The extra `IsGameCoreThread` calls never cross the
EXE-server boundary (server_local, above); in the stock game the EXE answers them itself. The +1.7 % is a cost of
the 32-bit TEST host c5h, and every x86 c5h number carries c5h's per-call accounting (3.1 % of a huge turn on the old
pin, more on the new one): **an x86 (c5h) against x64 (server) comparison overstates the server's lead by about that
much** (PERFORMANCE.md section 4).

## 11. Baselines on the new pins, AI logging on and off (perf 7010, 2026-11-12) [L]

The current baselines. Machine `oldpc` (quiet testbed, i7-6700K, Windows 10), runner 9033.2, nobody logged on;
`perf/tbjobs/series.py` (one process at a time, 6 rounds, arm order rotated); statistics logging on (pinned database
`bd9b8aa1`); builds: x64 = `<pins_dir>/dlls/x64-76992d23/bin` (DLL `fa75f483`, **server.exe `3a35330a`**, the
corrected 0094 server), x86 = `i0ship-bbfacca1` (`f7c64840`) in c5h with `loc_db`/`game_dir` (the corrected
configuration). "on" = DLL AI logging flags 15 (both hosts' default), "off" = as a player. Grade A.

**Huge 350 -> 355** (quiet job `20261112-213830-45e9`, 22:25-22:43 EST; `alone`, `undisturbed`, foreign 0.031 cores,
machine CPU 13.7 %; evidence `perf/evidence/7010/B/`):

| Arm | mean s/turn (turns 350-354) | sd | cv | 95 % CI | boot+load s | peak WS MB | mean of turns 350-353 (same game in every arm) |
|---|---|---|---|---|---|---|---|
| x64 off | **5.915** | 0.010 | 0.17 % | 5.905-5.926 | 8.74 | 261 | 6.050 |
| x64 on | 6.513 | 0.007 | 0.10 % | 6.506-6.520 | 8.91 | 265 | 6.688 |
| x86 off | **7.016** | 0.011 | 0.15 % | 7.005-7.027 | 13.29 | 332 | 7.191 |
| x86 on | 8.022 | 0.008 | 0.10 % | 8.014-8.031 | 13.56 | 334 | 8.252 |

- **Logging on is a different game from turn 354's play on, in both bitnesses** (bug 0103): statscmp of round 1's
  stats.db, on against off: `MapUnitsState` differs at turn 355 (4 unit ids, 1 position), x64 and x86 alike; every other
  table identical to 355. The engine-call counts differ too (`IsGameCoreThread` 4,182,074 on vs 4,182,040 off). x64 off
  = x86 off: 14 tables, 62,453 rows identical (D24 holds on the new pins).
- **AI logging cost on the same game (turns 350-353)**: x64 +0.638 s/turn (**+10.5 %**, 95 % CI +0.625..+0.652); x86
  +1.060 (**+14.7 %**, +1.048..+1.072). Over 350-354 (turn 354 a different game): +10.1 % / +14.3 %.
- **x86 against x64, the player's workload (off)**: +1.101 s/turn (**+18.6 %**, +1.087..+1.114); about 3 points of it are
  c5h's per-call accounting (section 10), not the DLL.
- Against 9029's set of 2026-11-11 (another set, a runner restart between; for orientation only): x64 off 5.905 then,
  5.915 now; x86 off 6.996 then, 7.016 now.

**Standard new game 0 -> 50** (civ5-headless spec Continents, WORLDSIZE_STANDARD, map seed 1001, sync seed 2002, standard
speed, ancient era, Emperor, default players: the turnbench/d10 Standard game; the server through `server/run_server.py`,
c5h with `--no-mods --db <pinned>`, the commands of `tools/testbed/jobs/longgames.py`; boot+load includes map
generation; quiet job `20261112-213910-c3f5`, 22:43-22:53 EST, foreign 0.029 cores, `undisturbed`; evidence
`perf/evidence/7010/C/`):

| Arm | mean s/turn (turns 0-49) | sd | cv | 95 % CI | boot+load s | peak WS MB |
|---|---|---|---|---|---|---|
| x64 off | **0.129** | 0.001 | 0.50 % | 0.128-0.130 | 10.13 | 172 |
| x64 on | 0.281 | 0.001 | 0.19 % | 0.281-0.282 | 10.12 | 172 |
| x86 off | **0.170** | 0.000 | 0.22 % | 0.169-0.170 | 13.57 | 205 |
| x86 on | 0.414 | 0.001 | 0.14 % | 0.413-0.414 | 13.58 | 214 |

- **On and off play the same game to turn 50** in both bitnesses (statscmp, 15 tables, 19,016 rows identical), so the
  whole range is a valid on-against-off comparison: AI logging costs x64 +0.152 s/turn (**+118 %**, i.e. 54 % of an
  on-turn), x86 +0.244 (**+144 %**, 59 % of an on-turn), as on the old pins (49 / 60 %, section 3).
- x86 off against x64 off: +0.040 s/turn (**+31 %**, +0.040..+0.041). x64 off = x86 off except
  `ReligionBeliefYields` from turn 29 (the known `IsReligionOwner` column, `m_iHolyCityX/Y` of a pantheon; issues 0077,
  1803).
- Old pins on the workstation (section 3, grade B, another machine): 0.164 / 0.214 s/turn off. Not comparable (another
  machine and other code); the testbed figures above are the baseline.

- **The Standard turn-20 spike of section 1 is the AI logging path** [L, per-turn means of series C]: with logging on,
  x64 turn 20 takes 2.28 s (turn 19 1.55 s, median turn 0.196 s); with logging off no turn of 1-49 is above 0.23 s (x64,
  slowest turns 45-47, median 0.136; x86 0.285 / 0.179), turn 0 0.31 / 0.37 s. So it is a cost of the settle-plot
  logging (section 3), not of the game; nothing to fix for the player. (Closes cycle 1's open question.)

**What is not measured on the new pins**: the workstation (closed with reason, 7010 point 3).

Sources: `perf/` tools and their outputs named above; DLL source at the fork commit c43ea1b9f (read with `git show`);
`server/src/srv_main.cpp`; `~/Documents/civ5-headless/host/driver/HHMain.cpp` (read in place).
