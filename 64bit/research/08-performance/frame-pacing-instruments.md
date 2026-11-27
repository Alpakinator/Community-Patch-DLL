# Frame-pacing instruments in the product (ticket 0688, epic 0686 P1, D241)

Written 2026-11-22 from 22:50 EST by ticket 0688's implementer (account B), from `main` 250dd9bab. The measures F1-F6
and the vocabulary (presented frame, world update, freeze, world age) are the Architect's:
[architect-2026-11-22-frame-pacing-epic.md](../07-architecture-decisions/architect-2026-11-22-frame-pacing-epic.md)
section 3; the gaps M1, M2, M4, M5 they close are in
[architect-2026-11-22-frame-pacing.md](../07-architecture-decisions/architect-2026-11-22-frame-pacing.md) section 3.
Marks: `[C]` read or written in the code here, `[L]` run on the workstation's stand-in or a build, `[I]` inferred.

## 1. What exists after 0688

| Where | Line / column | What | Default |
|---|---|---|---|
| Composite, per-frame record (`lockstep_csv=1`) | columns `wu`, `world_age_ms` (appended last) | 1 = this Present showed a world picture newer than the Present before; Present minus the shown picture's camera publish, ms (-1 unknown; `wu` -1 with `pacing=0`) | with the record |
| Composite log | `pacing 1s:` (per stats interval, 1 s) | presents, `ft_max`, `wu` (world updates), `wui_max`, `moving` (Presents whose camera moved), `freezes`, `freeze_ms`, `freeze_max`, `freezes50`, `unlogged`, `age_mean`, `age_max`, `self_us` and `self_ns_per_present` (the instruments' own main-thread time, their QPC reads included), `dropped`, `qpc_end`, `qpc_now`; `world=0` off the source path (F1 only) | `pacing=1` |
| Composite log | `pacing totals:` every 10 s (`why=periodic`), at the process's end (`why=exit`) and before the proxy's `Fatal` (`why=fatal`, ticket 0697, section 5a) | the run so far: presents, world updates, `moving_ms`, freezes (all, >= 50 ms, >= 100 ms), `freeze_ms`, `freeze_max`, `freeze_ms_per_min` (of camera movement), and for `ft` (F1), `wui` (F2), `age` (F4): `_n _mean _p50 _p99 _p999 _max _low1 _low01 _over2x _over50 _over100 _bins` | `pacing=1` |
| Composite log | `pacing freeze:` | one line per freeze of 50 ms or more (at most 32 a second, the rest counted as `unlogged`): `ms`, `frames` (Presents without a world update in it), `qpc0` / `qpc1` (the Presents of the world updates around it), `taken_after` | `pacing=1` |
| Composite log | `vidmem 10s: process=exe` | the EXE's private and working set, its video memory on the adapter (local and non-local usage, budget, reservation) | `pacing=1` |
| Server render.log | `pass 1s:` | passes, long passes (`long`, `long_unlogged`), `busy_max`, `busy_mean`, wake delay (`wake_n`, `wake_mean`, `wake_max`), run totals, `qpc_now`, then per stage `max/mean` ms | `render_pass_stats=1` |
| Server render.log | `pass long:` | one line per pass whose busy time is `render_pass_long_ms` (20) or more (at most 20 a second): `busy_ms`, `top` stage and `top_ms`, `frame` drawn, `wake_ms`, `qpc0` / `qpc1`, then every non-zero stage in ms | `render_pass_stats=1` |
| Server render.log | `vidmem 10s: process=server` | as the composite's, for server.exe (beside the older `mem periodic:` line) | `render_pass_stats=1` |
| `perf/pacing/pacing_report.py` | text and `--json` | F1-F6 of one run; each freeze joined to the server's long passes by QPC | - |

The keys: `shell_composite.ini` `pacing` (0 | 1, default 1); server `render_pass_stats` (0 | 1, default 1) and
`render_pass_long_ms` (0..10000, default 20). `[C]`

## 2. Definitions as coded (`shell/common/vp_pacing.h`) `[C]`

- **World update** (`WorldTracker::onPresent`): the Present shows a picture (lockstep kind `exact`, `older` or `nopub`)
  whose server frame id is greater than the newest shown so far. A newer picture that is not this Present's own camera
  (`older`) is still a world update; a picture shown again, or an older one, is not.
- **Freeze**: a run of one or more Presents without a world update, after a first picture, during which the camera moved
  (its position `camWorld[12..14]` differs from the Present before, at any Present of the run or at the world update
  that ends it). Its length is the time between the Presents of the two world updates around it (`qpc0`, `qpc1`): the
  time the world stood still. A stall under a still camera is not a freeze; it shows as a long world-update interval
  (`wui_max`).
- **World age**: Present QPC minus the shown picture's `camPublishQpc` (the composite's own publish time of that camera,
  carried in the ring slot record; the version-3 source looks it up in the composite's publish table).
- **Histogram** (`Hist`): 0.1 ms buckets below 100 ms, 1 ms below 1 s, 10 ms below 10 s, one overflow bucket; each bucket
  keeps its count and its sum. Percentiles are the upper edge of the bucket of the nearest-rank sample, capped at the
  maximum (at most 0.1 ms above the true value below 100 ms). "1 % low" = 1000 / the mean of the slowest 1 % (exact from
  the bucket sums; a partly used bucket by its own mean). Bins of the lines: < 8.4, < 16.8, < 25, < 33.4, < 50, < 100,
  < 200, < 500, >= 500 ms.
- **Pass** (server, `PassTimer`): one iteration of the render loop, from the end of one pacing wait to the end of the
  next. Stages: `polls` (sink tick, adapter check, composite pid, the scene's taps, the fatal steps), `feed` (the event
  feed's drain and its hand-on to the layers), `decide`, `slot` (the sink's begin, with 3302's busy retry), `predraw`
  (the tile source's `beforeDraw`: the pre-draw bakes and copies), `draw` (the scene's draw not claimed by a layer: the
  overlay and shadow binding, the arc, the extras, the path, the cloud planes), `fog` (the fog mask, `beforeWorld`),
  `terrain` (with the units' cast into the shadow map), `water` (with its reflection pass and the ice), `forest`,
  `models` (their frame set-up and the scene pass), `units` (their frame begin and scene pass), `handover` (the sink's
  end: hand-over, fence, flush), `idle` (the source's idle slice), `rec` (the recorder, tests), `log` (the 1 s lines and
  the memory lines), `wait` (the pacing wait; never part of the busy time), `other`. The scene's marks switch the
  calling thread's current pass (`CurrentPass`, thread-local); offline programs that compile the scene have no current
  pass, so the marks do nothing there.
- **Wake delay**: when the pacing wait ends on `<base>.Cam`, now minus the newest publish's `publishQpc`.

QueryPerformanceCounter is one clock for every process on the machine, so a composite freeze (`qpc0..qpc1`) and a server
pass (`qpc0..qpc1`, its busy time) are joined by overlap without any wall-clock conversion; `qpc_now` in the 1 s lines
anchors QPC to the lines' local times when a reader needs them.

## 3. Where the writes happen (MC #10822 condition 1) `[C]`

- Composite: the main thread (the EXE's render thread) only counts, in `PcPresent`, `PcWorld` and `PcSecond`, and hands
  the second's numbers and each logged freeze to the control thread through a 512-entry single-producer ring, and the
  run-long histograms every 10 s by one copy (about 100 KB) behind a flag. The control thread (`PcFlush`, every
  500 ms) formats and writes every new line and queries the video memory. The adapter (`IDXGIAdapter3`) is taken once
  per device on the main thread (QueryInterface and GetAdapter only) and handed over with its reference. The exit line
  is written in `DLL_PROCESS_DETACH` only when the log lock is free (as 0339's lines).
- Server: every line goes through `RLog`, which ticket 5054 queues to its log writer thread in server.exe (5054 on
  `main` at 7b4c110e7; merged into 0688's branch 2026-11-23 02:16, a51162d21, the one shared hunk in `DrawFrame`: the
  handover mark beside 5054's `end0`). Test programs without the writer (render_host_test.exe) still write on the
  calling thread, as 5054 describes. The volume: one `pass 1s:` a second, one `vidmem 10s:` per 10 s, at most 20
  `pass long:` a second, each only when a pass was already slow.

## 4. Cost of the instruments

MC #10822 condition 2 asks for `pacing` 0 against 1 and `render_pass_stats` 0 against 1, interleaved, by
`docs/briefs/PERFORMANCE.md`. MC #10859 (23:01): no quiet window tonight; the arms run on the workstation as an
INDICATION, and the decisive figure comes from oldpc or the laptop later.

**4.1 Per-operation cost on the workstation, an indication** (busy workstation, interleaved, not a timing claim; grade C).
`perf/pacing/bench_pacing.py --rounds 12`, 2026-11-22 23:03-23:04 EST, workstation (64 GB), branch
task-0688-pacing-instruments at adc62f149; the two builds alternate round by round. Load: CPU 66-68 % at the start, 59 %
at the end; free commit 74.6 GB; six other heavy jobs registered (0630's take suites, a render suite run, a game
instance, a server build, builders, an x64 server). Raw values: `server/evidence/0688/bench_pacing_2026-11-22-2303.json`.
Mean, sd, median, 95 % interval of the mean, n = 12 each, ns per operation:

| Operation (what the product does) | Composite build (MSVC x86 /O2) | Server build (clang-cl x64 /O2) |
|---|---|---|
| `present`: per Present, the histogram add, the tracker, the age and interval adds | 27.2, sd 4.6, median 29.9, 24.3..30.1 | 17.8, sd 3.7, median 16.7, 15.5..20.2 |
| `pass18`: per server pass, 18 stage switches with one QPC read each and the second's sums | 860, sd 119, median 925, 784..935 | 504, sd 75, median 518, 456..552 |
| `qpc`: one QueryPerformanceCounter | 40.8, sd 5.9, 37.0..44.5 | 26.9, sd 4.7, 23.9..29.8 |
| `copy_run`: the 100 KB copy of the run's histograms (main thread, every 10 s) | 4,368, sd 1,803 | 4,512, sd 1,285 |
| `summaries`: three histograms summarised (control thread, every 10 s) | 96,026, sd 26,956 | 150,027, sd 37,160 |

Why the numbers are this size `[I]`: a pass's cost is its 18 QPC reads (18 x 27 ns = 0.48 ms per 1,000 passes) plus the
array updates; a Present's cost is one bucket index and a few adds, with two more QPC reads for `self_us` in the product
(not in `present` above: about 80 ns on x86 by the `qpc` row). So per frame the composite's main thread pays about
0.1 us and the server's render thread about 0.5 us, 0.0006 % and 0.003 % of a 16.7 ms frame, under the 0.05 ms the
epic allows (proposal section 6). The per-10-s items cost 4.5 us on the main thread and 0.1-0.15 ms on the control
thread. The product's own measure of the first is `self_ns_per_present` in every `pacing 1s:` line.

**4.2 Per-operation cost on the quiet testbed (the decisive figure, grade A).** The same two builds (shipped, not
rebuilt), `bench_pacing.py --rounds 24 --exe-x86 ... --exe-x64 ...`, oldpc quiet job 20261122-232458-8cdd, 2026-11-22
23:25-23:28 EST, quiet proof alone=True undisturbed=True foreign_cores=0.062, free commit 7.0 GB, arms alternating round
by round. Raw values: `server/evidence/0688/bench_pacing_oldpc_20261122-232458-8cdd.json`. Mean, sd, 95 % interval,
n = 24, ns per operation:

| Operation | Composite build (MSVC x86) | Server build (clang-cl x64) |
|---|---|---|
| `present` | 21.06, sd 1.23, 20.54..21.58 | 14.03, sd 0.58, 13.78..14.27 |
| `pass18` | 506.9, sd 13.0, 501.5..512.4 | 254.3, sd 0.7, 254.0..254.5 |
| `qpc` | 22.94, sd 0.80, 22.60..23.28 | 12.55, sd 0.27, 12.43..12.66 |
| `copy_run` | 1,726, sd 31 | 1,735, sd 35 |
| `summaries` | 62,788, sd 1,055 | 90,829, sd 2,808 |

So on oldpc: the composite's main thread pays about 21 ns per Present for the instruments plus about 46 ns for the two
QPC reads of `self_us` (67 ns, 0.0004 % of a 16.7 ms frame); the server's render thread about 0.25 us per pass (18
switches; a pass with fewer stages pays less), 0.0015 % of 16.7 ms; every 10 s 1.7 us on the main thread and 63-91 us on
the composite's control thread or (server) none (the server keeps no run histogram). The per-pass figure is its QPC reads
(18 x 12.6 ns = 226 of the 254 ns), as the workstation's indication showed. Not measured as a whole-frame on/off
difference: at these sizes a frame-time arm cannot resolve it (a 16.7 ms frame's run-to-run spread is microseconds or
more); the on/off arms below check that nothing else changes.

**4.3 The interleaved on/off arms, an indication** (busy workstation, interleaved, not a timing claim; grade C; MC #10915
accepted them as indications, the decisive figure being 4.2). A first start at 23:04 was stopped at 23:07 for 0630's
barrier. The series ran again after MC-B's all-clear (branch at 020f349f9, main 11e67480b merged).

- Composite, `pacing` 0 against 1 (`test_cost_arms_pacing_on_off`, 6 rounds, arms alternating, order rotated per round;
  stand-in test_host + lockstep_server, camera panning, vsync off; metric: the mean of the composite's `stats` ft_mean
  after the first 2 lines per run), 2026-11-23 00:47:54-00:51:00 EST. Load at the start: CPU 88 %; four other heavy
  jobs registered (4808's PGO build and its x64 DLL release link, the MC's server build, 5054's builders). Raw values:
  `server/evidence/0688/composite_arms_2026-11-23-0048.json`. Frame time ms, n = 6 each: pacing=0 mean 3.259, sd 1.579,
  median 2.383, min 2.075, max 5.739, 95 % 1.601..4.916; pacing=1 mean 3.369, sd 1.493, median 2.491, min 2.279, max
  5.486, 95 % 1.803..4.936; paired difference (1 - 0) +0.111 ms, 95 % -1.952..+2.174: no difference this series can
  detect; the smallest it could have detected is about 2 ms, four orders of magnitude above 4.2's cost. The spread is the
  machine's load (two of each arm's six runs near 5 ms).
  `self_ns_per_present` in the pacing=1 runs: mean 1,968 ns, sd 503, median 2,038, min 1,046, max 2,416 (each run's mean
  over its seconds). That is 70-95x the 21-27 ns per Present of 4.2/4.1, plus the two timing reads' 46-80 ns `[I]`: the stand-in presents at about 400 Hz
  and the per-second sum includes any preemption of the main thread inside the timed region on a machine at 88 % CPU
  (one 1 ms preemption a second adds 2.5 us to every Present's mean); the quiet figure is 4.2's. The live measure is
  therefore an upper bound under load, not the instruments' own work.
- Server, `render_pass_stats` 0 against 1: 4.4.

**4.4 Server on/off arms, an indication** (busy workstation, interleaved, not a timing claim; grade C).
`test_cost_arms_pass_stats`, 6 rounds, arms alternating, order rotated per round; render_host_test.exe (the render
objects server.exe links, the render-host test build of 0688's branch at 020f349f9 with main 11e67480b), 60 cameras a
second published with `<base>.Cam`, 12 s a run; per run the means over its `host 1s:` lines after the first 2.
2026-11-23 01:47:34-01:50:23 EST; load at the start: CPU 98 %, seven other heavy jobs (0630's take suites, a shell build,
an x64 server, a bench, an m43 DLL build, a second shell build, builders). Raw values:
`server/evidence/0688/server_arms_2026-11-23-0147.json`. n = 6 each:

| Metric | render_pass_stats=0 | render_pass_stats=1 |
|---|---|---|
| render-thread CPU per draw, ms (rt_cpu_ms / draws) | mean 1.326, sd 0.452, median 1.390, 0.852..1.800 | mean 0.876, sd 0.215, median 0.875, 0.651..1.102 |
| rt_cpu_ms per second | mean 55.8, sd 28.9 | mean 34.7, sd 10.8 |
| submit_ms (draw + hand-over) | mean 3.37, sd 1.72 | mean 2.07, sd 1.62 |
| draws per second | mean 40.2, sd 15.4 (17.5..56.4) | mean 39.5, sd 9.5 (24.6..53.9) |

The paired difference of CPU per draw (1 - 0) is -0.450 ms, 95 % -0.820..-0.079: the arm WITH the instruments used
less CPU per draw. That is the machine's load, not the instruments (4.2 bounds their work at 0.25 us a pass, three
orders of magnitude below this difference; the draws a second, set by the fake composite's camera pace on a machine at
98 %, varied 3x within each arm). What the series shows: no cost of the instruments is visible against the load's
noise; it cannot show a cost below about 0.4 ms per draw.

## 5. Tests `[L]`

- `perf/pacing/tests/test_pacing_unit.py`: the header compiled four ways (MSVC x86 and x64 at /W4 /WX, clang-cl x64 at
  /W4 -Werror, MSVC x64 ASan with a caught canary), each running `pacing_unit.cpp`'s checks (buckets, percentiles,
  lows, bins, the tracker's freeze cases, the stage timer with nested scopes and marks). 2026-11-22: 4 passed.
- `perf/pacing/tests/test_pacing_report.py`: the reader on synthetic records (with and without the `wu` column), lines
  and a PresentMon CSV; the join (a freeze with a long `predraw` pass, one with none); the exit codes 2 and 3 with no
  output. 2026-11-22: 6 passed.
- `shell/tests/test_pacing.py`: the composite on the stand-in (lockstep_server stalled 1 s): panning gives exactly one
  freeze of about the stall, a still camera none, `pacing=0` no line and `wu=-1`; the reader finds the composite's freeze
  in the record with the same QPC bounds; `test_cost_arms_pacing_on_off` (1 round by default; the cost series with
  `C5_PACING_COST_ROUNDS`). 2026-11-22 23:10: with `test_config_defaults.py` 18 passed (156 s); `test_lockstep.py`,
  `test_lk_late.py`, `test_composite_log.py` 44 passed (298 s), shell build of 0688's branch.
- `server/evidence/0688/test_pass_lines_0688.py` (render_host_test.exe, the render-host fixtures): the `pass 1s:`,
  `pass long:` and `vidmem 10s:` lines with `render_pass_long_ms=0`, none with `render_pass_stats=0`, the keys' refusals
  (exit 2), and `test_cost_arms_pass_stats` (`C5_PASS_COST_ROUNDS`). Run by `server/evidence/0688/links_0688.sh`.
  2026-11-23 01:37: 8 passed (51 s).
- `server/evidence/0688/series_0688.sh` (with `links_0688.sh`), 2026-11-23 00:47-01:50 EST, branch 020f349f9 with main
  11e67480b: `run_all.py` render-host 189 passed, fow-scene 3, overlay-scene 3, shadow-ice-scene 6, water-scene 29,
  forest-scene 3, scenebundle-programs and scenebundle-py passed (8 suites OK; it built c5render, rt, fow, ovl, shadow,
  water, forest, sb and sceneshot); the links of build_units_tests, build_units_extras_tests, build_unitfeed and
  test_farmfields_build OK (with server/build_server.py of 2026-11-22: every R13 builder of runbook 2.10); the pass-line
  tests 8 passed; `scenebundle/test_host_unchanged.py` against a render-host build of main 11e67480b: 2 passed (the
  host's drawn frame byte-equal to main's, colour and depth, with the pass timer and the scene marks on).
- After the merge of main with 5054 (a51162d21), `server/evidence/0688/postmerge_0688.sh` 2026-11-23 02:17-02:36:
  server.exe OK (141 s), render-host 189 passed, the pass-line tests 8 passed.
- Rule A (scene-bundle offline equals live, MC #10916 in MC-B's slot): `run_all.py --only offline-equals-live` with
  `C5_SCENE_LIBRARY=C:/c5-64bit-work/scene-library/bundles` (scenelib check OK against the F: master, 16 bundles) and
  `C5_SCENE_ASSET_ROOT=C:/c5-64bit-work/scene-asset-root/mods548-vt`, 2026-11-23 01:51-02:14 EST, branch at b993935ac
  (main 11e67480b): PASS, 17 passed, 997 s. No picture change.
- Text readers of the changed files (git grep of the file names over the scripts): shell `test_farm_guard.py`
  `test_composite_wires_the_guard`, `test_ice_tap.py` `test_shipped_ini_and_default_hold_the_tap_on`, `test_lsprobe.py`
  `test_probe_site_matches_exe_on_disk`: 4 passed; `test_config_defaults.py` (the ini against the C++ defaults) above;
  `test_render_host.py` and `test_host_unchanged.py` above.

## 5a. The fatal end (ticket 0697, 2026-11-23) `[C]` code, `[L]` stand-in

- Gap (worker-0673l in 0691's rerun, lead-0673 #11179): the proxy's `Fatal` (`proxy_rt.cpp`) ends the EXE with
  TerminateProcess, so no DLL gets DLL_PROCESS_DETACH and the `pacing totals: why=exit` line never came after a hung or
  failed server (every `Fatal`: the hung route, CallFailed in a D33 session or with on_server_death=exit, strict 4/5).
- Fix (commit a9072235f): the composite exports `VPShell_FlushTotals(why, detail, timeoutMs)`: once per process;
  `g_pcStop` stops `PcPresent` / `PcWorld` / `PcSecond` counting, the ring is drained under `g_pcRingBusy` (which the
  control thread's `PcFlush` also takes, by an interlocked try), the log lock by a bounded `TryEnterCriticalSection`
  loop; then `pacing: the process ends (why=fatal): exit=N stage=S code=C: <Fatal's reason>; the ring drained` and
  `pacing totals: why=fatal ...` (the format of why=exit and why=periodic) from a copy of the run whose histogram counts
  are recomputed from the buckets (a copy torn by the main thread can never underflow the bins). Without the log lock
  in time nothing is written. The why=exit line at detach is unchanged (skipped only when the fatal line was written).
- The proxy: OnBoot finds the export BEFORE it passes the hung sink (`VPShell_SetHungSink`; a hung report that comes at
  once still finds it; one line either way); `Fatal` calls
  `ProxyShell::FlushCompositeTotals("fatal", detail, 1500)` after its FATAL line and before WriteStats / LogFlush /
  TerminateProcess: a thread of its own calls the export (composite bound = half the wait), WaitForSingleObject 1,500 ms
  (MC #11248: under 2,000 ms from Fatal to TerminateProcess); a failed CreateThread is logged and skipped at once; a
  timeout gives one `LOUD` line; a composite without the export one line, no wait; no composite: nothing. The report and
  the popup (ServerHungRoute, CallFailed) come before Fatal, unchanged.
- 5054's log writer is server.exe's; on the hung route the server is the hung party, so the EXE cannot drain it. The
  EXE side's logs are synchronous already (the composite's `Log` flushes each line, `Fatal` calls `LogFlush`).
- Once per process on both sides: the proxy's claim (a second `Fatal` skips the flush) and the composite's (a second
  call returns 0 at once and writes nothing). A second `Fatal` on another thread during the first one's wait goes straight
  on to its own TerminateProcess and may cut the first one's lines `[I]` (not tested; the waits are bounded either way).
  While `Fatal` waits (at most 1,500 ms) the process's other threads run on: the loader's orderly end overtook a 1,500-ms
  wait in the test below before the stand-in held its boot thread `[L]`.
- Stand-in results, 2026-11-23 06:12-06:28 EST, branch claude/lead-0673-holistic-d0fd9d at main aed29d9e8 + 0697 (the
  proxy by `build_proxy.py`, the pin 0.1b `src-x64-1b87dd185`; the shell by `shell\build.cmd`) `[L]`:
  - `test_lockstep.py::test_exact_hung_server_fatal_end_writes_the_totals` (test (c)'s stall, `lockstep_hung_ms=3000`,
    `test_host --fatal-sink`): host exit code 3; the composite's log ends `LOUD: ... HUNG` (06:27:28.254), then 51 ms later
    on the flush's thread `pacing: the process ends (why=fatal): exit=3 stage=server-hung code=no-frame: ...; the ring
    drained` and `pacing totals: why=fatal presents=149 world_updates=96 ... ft_n=148 ft_p50=15.1 ft_max=3008.900
    ft_over100=2 ...` (the hung frame is F1's 3,009-ms tail); the call returned 1 in 14.3 ms; the second call returned 0
    in 0.0 ms; no why=exit line. PASSED alone and in the `-k "exact_ or fatal_end"` run.
  - `test_proxy_rt.py` (the real proxy, the loader, a stand-in composite built with `/DSTANDIN_FATAL`, the hung route by
    the default on_server_death=exit): `shell-fatal` exit code 3, the export called once with `why=fatal detail=exit=3
    stage=- code=-: the server hung (on_server_death=exit)` (pack=0: no SetFail), returned 1 in 1.2 ms after the FATAL
    line; `shell-fatal-flushhang` (an export that never returns) exit code 3, one LOUD line, the wait 1,511.9 ms, nothing
    written; `shell-fatal-old` (no export) exit code 3, named at the load and at the Fatal, no wait. No server.exe left.
  - The stand-in's timing (06:09, `[L]`): the loader's boot-orderly ends about 1.3 s after the composite's load (loaded
    at 1.22 s, the detach at 2.56 s) and its ExitProcess ends the Fatal's thread, so the stand-in fires the sink after
    300 ms (`STANDIN_HUNG_AFTER_MS`) and holds OnBoot's thread 4 s in `VPShell_SetHungSink` (`STANDIN_HOLD_MS`).
  - The rest: `test_proxy_rt.py` in full 226/226 (226 s) and `--only-borrowed` 19/19; `test_proxy_session.py` 183/183
    (244 s); hutest PASS (55 checks); `test_build_settings.py` PASS; `test_lockstep.py -k "exact_ or fatal_end"` 13 of 14
    (`test_exact_killed_server_dead_route` failed under load, passed alone); the text readers of `test_farm_guard.py`
    (4), `test_lk_late.py`, `test_ice_tap.py` one, `test_composite_log.py`, `test_lk_late_unit`, `test_lsprobe` site:
    passed. `test_pacing.py::test_stall_while_panning_is_one_freeze` and
    `test_config_defaults.py::test_unreadable_ini_is_named_once_and_readable_again` failed with 0697 AND on a shell build
    of main aed29d9e8 alone (06:33, the same TimeoutError for the ini; the pacing case failed on main by its `age_p50`
    128 ms, with 0697 by a second freeze of 832 ms): not 0697's, open on main (another worker's live game ran beside).
    *(Root-caused 2026-11-23 by tickets 0701 and 0702: test faults under CPU load, section 5b.)*
- Commands: runbook 2.2 (`-k fatal_end`) and 2.6 (`--only-shell`, the stand-in's `STANDIN_*` variables).

## 5b. Two shell tests red under load (tickets 0701, 0702, 2026-11-23) `[C]` code, `[L]` stand-in

Both were TEST faults (fixed waits and thresholds that a loaded workstation breaks); the composite is right. Measured
by worker-0673s on a shell build of branch claude/lead-0673-holistic-d0fd9d at main e355ecfd7 (`shell\build.cmd`),
the workstation; "load" = 40 busy Python processes on its 32 logical processors (scratch `busy.py`, as 0699's load loop).

- **Not exact mode** `[C]`: `test_pacing.py` takes `test_lockstep.INI`, which pins `lockstep_mode=bounded` (set by 0691
  a81eda0be, before 0688's test merged at 550a228dd). The run shows the bounded path: 8 misses, give-up, `wait_off`
  frames with the stalled picture, recovery.
- **0701, `test_stall_while_panning_is_one_freeze`** `[L]`: quiet 3/3 passed (age_p50 2.0-2.1 ms, one long freeze of
  1,002-1,004 ms); under load 1 of 3 failed, two ways seen:
  - a second freeze of >= 800 ms BEFORE the stall: `pacing freeze: ms=1166.1 frames=4`, ending at the
    `lsprobe: hook ENABLED` commit (MinHook's enable suspends the process's threads; the host presented 4 frames in
    1.17 s). A true hitch of the host, not the stall's freeze; 0697's "second freeze of 832 ms" fits it `[I]`.
  - world ages of 100+ ms: the starved stand-in delivered a world picture only every 110-150 ms (dozens of 110-150 ms
    freezes a run), so `age_p50` was 94.1 ms in one loaded run (the limit was 100; main's 128 ms is the same `[I]`).
    The age is measured right: a 7-Hz world under a 400-px/s pan IS 100+ ms old.
  - Fix (test only): the stall's freeze is found by time. The test takes QueryPerformanceCounter just before it writes
    `stall.go` (the composite's `qpc0`/`qpc1` clock; Python's `perf_counter_ns` is the same clock, checked: 8 ticks
    apart); `stall_freeze()` wants exactly one freeze of >= 800 ms in [write, write + stall + 1 s], starting at most
    0.5 s after the write and ending at least 0.8 x the stall after it. Long freezes outside that window are allowed;
    `freeze_max >= the stall's freeze`; `age_p50 < the stall's freeze / 2` (an age not reset by world updates, or on
    another clock, still fails); the record's long freezes equal the composite's lines (QPC bounds). Negative control
    in the file, `test_stall_freeze_check_refuses_a_double_or_split_freeze` (no process): a second long freeze in the
    window, a stall split into two ~500-ms freezes, no long freeze covering the stall and a long freeze starting 0.7 s
    late all fail; the stall plus a 1.2-s freeze 2 s before it passes.
- **0702, `test_config_defaults.py::test_unreadable_ini_is_named_once_and_readable_again`** `[C]` + `[L]`: the
  composite's control thread (`ControlThread`, `shell_composite.cpp`) runs its ini watch (every 500 ms, `IniTime`
  against `last`) only after `DiscoverWithDummy`'s hook commit. Quiet the commit takes 0.1-0.5 s (once 9.5 s with other
  agents' jobs running); under load `hooks committed (dummy device): 46 enabled in 9122.1 ms` and `7918.4 ms`. The test's
  host lived a fixed 8 s from its own start and the test waited 10 s for `readable again`: the host ended (`frames=0`)
  before the watch ever ran. Quiet 3/3 passed, under load 2 of 3 failed with the TimeoutError. The composite is right
  by design: `last` is taken before the first read (issue 3007), so a change during the commit is applied after it,
  never lost.
  - Fix (test only): the host's life is a backstop (8 + 2 x `HOOK_COMMIT_WAIT_S` = 44 s, test_lockstep's 0494 constant);
    the test waits for `hooks committed (dummy device)` (18 s), then `readable again` (10 s), then ends the host by
    WM_CLOSE (conftest's `host_closer`, exit 0). Negative control: the same test without the ini touch (`set_ini`)
    times out at `readable again`.
- Proof (2026-11-23, each test alone, scratch driver `proof.py`): quiet 08:31-08:34 EST 10/10 and 10/10 (one quiet ini
  run had a 9,505-ms commit and passed in 16.9 s: the old test would have failed it). Under load 08:35-09:00 EST: 20/20
  and 20/20. In those 20 stall runs: 4 had a second freeze of >= 800 ms outside the stall's window (1,170-2,137 ms; the
  old test failed each), the stall's freeze 984-1,104 ms, age_p50 1.8-50.5 ms; the 40 dummy-device commits took
  7,077-15,043 ms (with the DLL loaded 1-1.7 s after the host's start, each ends past the old test's 8-s host `[I]`). Negative controls 09:00: the ini
  test without its touch failed with the TimeoutError at `readable again` (a scratch copy, not committed);
  `test_stall_freeze_check_refuses_a_double_or_split_freeze` passed. Both files as suites (`test_pacing.py` +
  `test_config_defaults.py`), quiet 09:01-09:04: 19 passed, twice. Results: `C:/c5-64bit-work/worker-0673s/proof-*/
  results.jsonl` (local).
- Side finding, ticket 0705, **FIXED 2026-11-23** `[C]` + `[L]`: the composite's publish table `g_lkPubTab` has 256
  entries (`LkPubQpc`, `f & 255`). The version-3 source (`handover=v3`, the stand-in `lockstep_server.exe` and
  `shell_server.exe`) looked its picture's publish QPC up in it again at EVERY Present, so a picture still shown more
  than 256 Presents after its publish got `srv_cam_pub_qpc=0` and `world_age_ms=-1` and dropped out of the age
  histogram; F4's p99/max understated long stalls (a quiet 1,002-ms stall's ages stopped at 570 ms). The ring source
  (`handover=ring`, the product path with server.exe) was never affected: it carries the publish QPC in the slot record
  the server wrote (`VPFrameSlot::camPublishQpc`, `ring_source.cpp` acquire), so the ring-path numbers of 0687/0689 stand.
  - Fix (`shell_composite.cpp`, the v3 source class): the lookup is made once, when a new picture is taken
    (`m_takenPub`, set beside `m_taken`), and kept with the picture; `acquire` hands `m_takenPub` out. The table is
    unchanged; one lookup per new picture instead of one per Present, so no per-frame cost is added.
  - Test: `test_pacing.py::test_stall_longer_than_256_presents_keeps_world_ages` (a 2-s stand-in stall while panning,
    host `--vsync 0`): more than 256 stalled Presents, each shown with `world_age_ms >= 0` and `srv_cam_pub_qpc > 0`,
    the ages reach (last stalled Present - the freeze's start), the totals' `age_max` too, and `presents - age_n`
    equals the record's rows without a picture (every shown frame in the histogram).
  - Numbers (workstation, quiet, 2026-11-23 09:19-09:21 EST; shell builds of 8e201197f without and with the fix):
    before: the 2,003-ms stall of 1,066 Presents lost 811 ages (2,749 of 3,560 shown rows aged), `age_max` 514.0 ms,
    `age_p99` 461 ms, `age_n` 3,003 of 3,815 Presents: the test FAILED. After, 3 of 3 passed: stalls of 2,002-2,003 ms
    with 1,071-1,079 Presents, 0 lost, `age_max` 2,002.1-2,002.7 ms, `age_p99` 1,940 ms, `age_n` = Presents - 1
    (the first Present, before any picture). `srv_cam_pub_qpc` equals the publish QPC of `taken_f` on every shown row.
  - Ring path unchanged `[L]` (ring_producer stand-in, `test_lockstep.py` `test_ring_handover_lockstep[fence|cpu]` and
    `test_ring_record_carries_the_unbaked_count[7]`, 09:22-09:30 EST, the same two builds; ring_producer has no stall
    switch, so these are runs without a stall): before 0 of 3,293 / 3,773 / 2,627 shown rows without an age, `age_p50`
    1.6-1.8 ms, `age_p99` 2.0-2.3 ms; after 0 of 3,815 / 4,146 / 2,702, `age_p50` 1.6-1.7 ms, `age_p99` 1.8-1.9 ms;
    in both, `srv_cam_pub_qpc` equals the publish QPC of `taken_f` on every shown row.
  - Suites with the fix: `test_pacing.py` + `test_lockstep.py` whole, 58 passed (436 s, quiet). The only builder of
    `shell_composite.cpp` is `shell/build.cmd` (runbook 1.5; scripts searched 2026-11-23).

## 6. Not verified

- D242 (2026-11-22; ticket 0691: the lockstep always exact, no older-world frames, only a dead or hung server ends
  the wait): once 0691 is in, a server stall no longer shows as a run of Presents without a world update (F3) but as
  long Presents (F1's tail, `ft_max`, `ft_over50`); F2 then equals F1 as in stock. The instruments need no change for
  that `[I]` (every shown picture is a world update; the tracker's freezes go to 0); a reader of a post-0691 run looks at
  F1's tail and the server's `pass long:` lines by time (the join by QPC works the same with a Present's interval).

- *(Corrected 2026-11-23: seen live in three workstation runs (c5h-c 04:39-04:43, EXE 4390913; c5h-new-a 06:13-06:24,
  EXE 26577625): `pass 1s:` once a second, `pass long:` with QPC bounds that join the CSV's frames, `vidmem 10s:` on both
  sides. The per-stage split those runs give, and the reader `server/evidence/0689/split0689.py`, are in
  [pan-coherence.md](../05-graphics-composition/pan-coherence.md) 5.8 (ticket 0689). Limit found there: `pass 1s:` gives
  per-second means and maxima, so per-pass stage percentiles need `render_pass_long_ms` lowered (e.g. 8: every slow
  pass itemised, at most 20 lines a second). Was: "No live game run yet: the lines on the product path (ring hand-over,
  the real EXE, server.exe) are not seen live.")*
- The instruments' own cost (section 4).
- Whether the per-frame record's `cam_x..z` moves in every kind of camera change the eye sees (a zoom changes `z`; a
  pure rotation does not exist in the game `[I]`).
