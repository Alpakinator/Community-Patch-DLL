# Inventory of the project's performance numbers (2026-11-11)

Compiled 2026-11-11 by implementer 7001 of the performance standards team (tracker 7000, decision D40) from the
knowledge base, the evidence folders and the tracker as they stood at main 7b195ab. Reading task only: nothing here
was measured again. Every row names the file the number comes from; where the number is the knowledge base's
restatement of an evidence file, both are named when they differ in detail. The machine is the same for every row
unless stated: Intel i9-13900KS (8 P-cores with HT + 16 E-cores, 32 logical), Windows 11 Home 10.0.26100, power plan
"Bitsum Highest Performance", RTX 4080, shared by several agent teams since 2026-11-10.

## Trust grades (defined here; the team's later files use the same scale)

For **timings** (seconds per turn, latency, frame time, load time, memory over time):

| Grade | Meaning |
|---|---|
| **A** | Interleaved runs, at least 6 per arm (or a micro-benchmark A/B of at least 6 passes), one build per arm, spread reported, machine load recorded during the series. Good enough to accept or reject a change. |
| **B** | Several runs per arm (2-5), or a single run on a machine stated to be quiet / with its load sampled, method stated (tool, window, definition of the metric). Good enough for a baseline to beat, not for a small difference. |
| **C** | A single run on a busy or unrecorded machine, runs made in parallel with each other, turn times from polling, or output not kept. An observation, not a measurement. |
| **D** | Projection, estimate or arithmetic on other numbers; nothing was timed. |

For **counts** (crossings, calls, rows, blocks): **A** = counted by an instrument over a stated window and reproduced
independently; **B** = counted by an instrument over a stated window once; **C** = counted from a partial instrument
(top-N methods, a proxy for frames); **D** = projected.

Confidence marks [C] / [L] / [I] are copied from the source where it gives one. "Logging" means the DLL's statistics
log (`stats.db`, `CustomModOptions.SQLITE_LOGGING`); "pragmas" means `PRAGMA synchronous = OFF` and
`PRAGMA journal_mode = PERSIST` on that database (decision D35, in both pins since 2026-11-10: `i0-ship` ccfd6dd4d,
`x64` c43ea1b9f). Builds before those pins have no pragmas unless stated.

**Current or history (COMMON rule 18, added 2026-11-11 by the team lead).** Every number below that was taken with
statistics logging on and a build without the D35 pragmas (all of section 1 except the rows naming ccfd6dd4d /
c43ea1b9f, and section 9's per-turn disk-sync costs of 0.35-0.5 s) is **history**: it describes builds nobody runs
any more, not the current pins, and is not to be quoted as a property of them. The current baselines, measured on
the pins with `perf/turnbench`, are in `turn-time-baselines.md` of this section. A second caution for every
headless number here and there: both headless hosts switch the DLL's own AI logging on by default (flags 15), which
a player with `LoggingEnabled = 0` does not run, and on the pins that work is a large share of a Standard turn
(`turn-time-baselines.md` section 3).

## 1. Seconds per AI turn

### 1.1 Headless, Standard Continents 80x52, 8 majors, fixed seeds 1001/2002, turns 0-200

| Metric | Value | Date | Build | Game / config | Method | Source | Grade |
|---|---|---|---|---|---|---|---|
| x86 Release-5.4.8 built here, no pragmas, logging on | 1.643 s/turn (runs 1.560 / 1.725); turns 2-33 0.676 | 2026-11-10 19:34-20:33 | base9011 `0fbe26b0...` (fork 7307aefdf) | c5h `--no-mods`, toolkit folder, logging on | c5h, 2 runs, arms in fixed rotation, one run at a time, CPU sampled every 30 s (means 11-29 %, one peak 74 %) | `research/06-tooling-and-workflows/d10-save-identity-test.md` 4b.5; `tools/d10/evidence/9011/speed.txt`, `speed.json` [L] | B |
| x86 same + the two pragmas | **1.201** (1.130 / 1.273); 2-33 0.470 | same | baseP9011 `71db6753...` | same | same | same | B |
| x86 official toolkit DLL (built from the pragma tree) | 1.292 (1.192 / 1.393); 2-33 0.555 | same | 4e91c58a | same | same | same | B |
| x86 i0-ship, no pragmas, logging on | 1.556 (1.538 / 1.574); 2-33 0.714 | same | i0-ship e6f904ad6 `2c216fc9...` | same | same | same | B |
| x86 Release-5.4.8, logging **off** | 1.229 (1.176 / 1.282) | same | base9011 | database copy with `SQLITE_LOGGING = 0` (`--db`) | same | same | B |
| x86 i0-ship, logging **off** | 1.214 (1.147 / 1.280) | same | e6f904ad6 | same | same | same | B |
| x64 server, no pragmas, logging on | 1.357 (1.353 / 1.361); 2-33 0.579 | 2026-11-10 20:33-20:49 | x64 29f15ac38 (pinned server) | server headless, same spec | 2 runs, same method | same; `speed-x64.txt` [L] | B |
| x64 server, logging **off** | **0.841** (0.851 / 0.831); 2-33 0.283 | same | same | logging off | same | same | B |
| x64 server (epic 1500), release | 1.505 (200 turns 301.1 s, 5,008 Updates; turns 1-100 0.6-1.3 s, 100-200 1.3-2.4 s) | 2026-11-10 | x64 of epic 1500 (pre-pins) | logging on, no pragmas | 1 run, load not recorded; `turn-times.tsv` | `research/03-dll-engine-boundary/server-host.md` 1, 4; `server/evidence/1503/std200/server-report.txt` [L] | C |
| x86 c5h reference (epic 1505) | 1.476 (320.7 s process wall); `--no-profiles` 1.493 | 2026-11-10 | 4e91c58a (pragma tree) | `--no-mods --fingerprint`, logging on | 1 run each, load not recorded | `server/evidence/1505/README.md` [L] | C |
| x86 i0-ship vs official, one at a time (MC verification of 2400) | 1.671 vs 1.335 | 2026-11-10 ~17:44 | e6f904ad6 vs 4e91c58a | logging on | 1 run each, method in `tools/d10/evidence/verification-2400/speed.py` | `tracker/issues/9011-*.md` scope B; `tracker/issues/2404-*.md` log [L] | C |
| Inside the D10 test, 12 host processes at once: x86 / x64 direct 0-200 | no pragmas: 4.77 / 4.23 (9011 run 2), 4.58 / 4.20 (9013 run 1); with pragmas **1.70 / 1.31** (9013 run 2); old x86 pin (no pragmas) vs new x64 (pragmas) 2.82 / 1.08 | 2026-11-10 19:07-22:53 | e6f904ad6 / 29f15ac38; 40ce81aee / e716240a7; ccfd6dd4d / c43ea1b9f | logging on, `--jobs` 12 | host lines of the reports, 1 run per row, CPU sampled once a minute (means 32 % / 38 %, peaks 58 % / 98 %, another team's game and a C++ build running) | `d10-save-identity-test.md` 4c; `tools/d10/evidence/9013/machine-*.txt` [L] | C |
| Continuations inside the D10 test (12 at once) | x86 cont. of x86-T50 3.88 -> 1.85; x64 cont. of x64-T50 5.40 -> 1.52 (no pragmas -> pragmas) | 2026-11-10 | as above | as above | as above | `d10-save-identity-test.md` 4c | C |
| 2404 side observation, four arms at once | stock 1.468 vs ship 4.46 | 2026-11-10 ~15:00 | 4e91c58a vs e6f904ad6 | parallel, loaded | concurrent | `tracker/issues/2404-*.md` (corrected there by 9011) | C, superseded |

### 1.2 Headless, Tiny Continents (seeds 30017/40017)

| Metric | Value | Date | Build | Config | Method | Source | Grade |
|---|---|---|---|---|---|---|---|
| x64 server release, 50 turns | 0.265 s/turn (13.2 s) | 2026-11-10 | epic 1500 | logging on | 1 run | `server-host.md` 1 [L] | C |
| x64 server release, 50 turns, with real localization | 0.23 s/turn | 2026-11-10 | epic 1700 | logging on | 1 run | `research/03-dll-engine-boundary/x64-dll-port.md` 11.4 [L] | C |
| x64 server debug | 0.55 s/turn | 2026-11-10 | epic 1500 debug | | 1 run | `server-host.md` 1 [L] | C |
| x86 c5h, 50 turns | 0.222 / 0.210 (A/A pair launched together); `--no-profiles` 0.258; peak WS 279 MB | 2026-11-10 | 4e91c58a | `--no-mods` | 2 runs in parallel | `server/evidence/1505/README.md` [L] | C |
| Inside the D10 test (12 at once), x86 / x64 direct 0-100 | 1.31 / 0.96 (9011 r2), 1.04 / 0.85 (9013 r1), 0.59 / 0.33 (9013 r2, pragmas) | 2026-11-10 | as 1.1 | parallel | 1 run each | `d10-save-identity-test.md` 4c | C |
| 2404 side observation, four arms at once, Tiny 100 | stock 0.265, bugfixes 1.591, pre 1.619, ship 1.623 | 2026-11-10 | | parallel, loaded | concurrent | `tracker/issues/2404-*.md` | C, superseded |
| Through the proxy (32-bit host -> proxy -> x64 server) vs the direct server, turns 1-49 | 0.240 vs 0.212 (ratio 1.13); map generation 4.15 s vs 1.45 s | 2026-11-10 11:57-12:22 | proxy `1dbeaba6`, server `ad182e3f`, x64 DLL e9d0c4837 | logging on | 1 run, machine ~50 % loaded; definition fixed by 9008 | `research/03-dll-engine-boundary/proxy-design.md` 12 [L] | C |
| civ5-headless records: host vs real game, VP 5.4.8 | 0.18 vs 2.29-2.37 s/turn (Tiny, 150 turns); Standard 0.30 vs 3.84 (60 turns); VP fork 0.36 vs 2.52; stock BNW 0.06 vs 1.38 | 2026-10-26 | VP 5.4.8 release / fork / stock | real game beside 8 host processes | 1 run per row | `research/sources/civ5-headless/verification.md` "Runs on record" | C |
| civ5-headless host alone | VP 0.43 s/turn (450 MB), stock 0.08 (121 MB) | 2026-10-25 | | up to 8 host processes | | same | C |

### 1.3 Huge: the save `VP8P-HUGE_0350` (128x80, turn 350, CP + VP (+ EUI in the real game))

| Metric | Value | Date | Build | Config | Method | Source | Grade |
|---|---|---|---|---|---|---|---|
| x64 server headless 350 -> 360 | **9.0 s/turn** (90.1 s), peak WS 323 MB | 2026-11-10 09:11-09:15 | epic 2000 THROWAWAY x64 (crash fixes), release | logging on, mods CP 151 + VP 17 | 1 run, "quiet pair, one process at a time" | `server-host.md` 10 [L] | B |
| x86 c5h same save 350 -> 360 | **10.4 s/turn** (104.2 s), 340 MB | same | x86 build of the same source | same | same | same | B |
| x64 / x86 350 -> 400 in parallel (`fixes`, `fixkey`) | 10.9 / 13.3 and 10.8 / 12.6 s/turn; 366 / 445 and 396 / 376 MB | 2026-11-10 08:53-09:09 | throwaway 31dc8e774 / 1c0f10f1d | parallel with each other and a DLL build | 1 run each | same | C |
| x86 c5h 350 -> 361, alone | 15.8 s/turn, 445 MB | 2026-11-10 | `i0-bugfixes`, release | | 1 run | `server-host.md` 10 (one-off 9005) | C |
| Inside the D10 test (12 at once), x86 / x64 direct 350-400 | 22.1 / 20.3 (9011 r2), 22.6 / 21.4 (9013 r1), **14.7 / 12.3** (9013 r2, pragmas) | 2026-11-10 | as 1.1 | parallel | 1 run each | `d10-save-identity-test.md` 4c | C |
| Through the proxy vs the direct server, 351-359 | 9.09 vs 8.27 s/turn (ratio 1.10, +0.8 s/turn; ~0.2-0.25 s of it transit [I]) | 2026-11-10 11:57-12:22 | as 1.2 proxy row, x64 DLL `6a44abb7` throwaway | logging on | 1 run, ~50 % loaded | `proxy-design.md` 12 [L] | C |
| Real EXE via proxy vs 32-bit DLL, AI turns 350->351, 351->352, 352->353 | proxy 33.4, 28.9, 26.7 (mean 29.7) vs 32-bit 37.2, 33.0, 30.9 (mean 33.7) s | 2026-11-10 14:43-15:46 | epic 2500 builds (not tied to a commit) | Goldberg c5h-a, CP + VP + EUI, leader screens answered 2 s after the message | 1 run each, harness polling 1-3 s, busy machine | `proxy-design.md` 15.5 [L] | C |
| Real EXE, 32-bit DLL, AI turn 350->351 with the 1116 handler | 37.2-37.9 s alone; 42.4-43.9 s with two instances (+14-18 %) | 2026-11-10 | instance-aware DLL | Goldberg c5h-a / c5h-b | 1 sample each | `research/06-tooling-and-workflows/headless-and-parallel-testing.md` (measured costs per instance) [L] | C |
| Real EXE, AI turn with leader screens, no handler | 336 s (90 s stall per screen) | 2026-11-10 | | | 1 run | same | C |
| Real EXE in process (boundary benchmark), f1 350->351 / f2 351->352 | wall 127.12 / 58.3 s, of which stalled on leader screens 89.1 / 19.9 s; **core thread busy 25.15 / 26.81 s**; 39 / 36 Updates | 2026-11-09 03:00-03:45 | instrumented release DLL | CP + VP + EUI, uncapped ~155 fps | 1 run, screens answered by hand | `research/03-dll-engine-boundary/interfaces-and-traffic.md` 8; `research/sources/boundary-benchmark/REPORT.md` [L] | C |

## 2. Boot, database cache, load and save

| Metric | Value | Date | Build | Config | Method | Source | Grade |
|---|---|---|---|---|---|---|---|
| `CacheGameDatabaseData`, x64 release | 11.5 s (Standard new game; boot 16.6 s incl. map generation 4.75 s); 10.0 s (huge save load); 11.7 s (prefetch 11.45 / 10.6 s) | 2026-11-10 | epic 1500 / 2003 / x64 e9d0c4837 | VP 5.4.8 pinned database `bd9b8aa1...` | 1 run each, phase timers | `server-host.md` 1, 4, 10; `x64-dll-port.md` 10.5, 10.6; `server/evidence/1503/std200/server-report.txt` [L] | B (consistent across 4 runs) |
| `CacheGameDatabaseData`, x86 release with the closed library | 11.9 s | 2026-11-07 log | VP 5.4.8 x86 | c5h, same database | 1 run (`Headless.log`) | `x64-dll-port.md` 10.5 [L] | C |
| `CacheGameDatabaseData`, x64 debug | 17-21 s (17.0 s, prefetch 16.7 s: UnitPromotions 4.0, Buildings 3.9, MinorCivilizations 2.7, Units 2.1) | 2026-11-10 | debug `/Od` | | 1-2 runs | `server-host.md` 1; `x64-dll-port.md` 10.5 [L] | C |
| c5h database cache phase per run | about 16 s | 2026-11-10 | 4e91c58a | | 5 runs | `server/evidence/1505/README.md` | C |
| Huge save load in the x64 server (after the cache) | 0.35 s (pregame 13,798 B, body 37,694,481 B) | 2026-11-10 | epic 2003 | headless | 1 run | `server-host.md` 10 [L] | C |
| Real EXE, process start -> `WaitForNetPlayers`, huge save | 141.34-156.89 s over 6 configurations (B4 142.77 / 148.24, A5 146.10, Combat 156.89, b4unit 147.45, B4 no-validate 141.34); B4 + Landmark off 117.42 | 2026-11-09 / 2026-11-10 01:44-02:07 | Release 5.4.8 / experiment DLL 5.4.8 + memory diagnostics | mods CP + VP + EUI, `[DisableSystems]` presets | n = 1 per configuration, run-to-run noise 2-4 s per phase; unsampled runs for timing | `research/02-exe-internals/load-sequence.md` 1; `research/sources/shell-gates/LOADTIME.md` [L] | B |
| Load owners (B4, 142.8 s) | EXE attributed 59.5 s; EXE before/around the load 37.0 s (DxDiag twice 16.0, DirectShow wait 10.0, save read 11.0); LandmarkSystem view build 27.1 s (gap 19.9 + tail 7.2); **DLL 17.4 s** (ValidateGameDatabase 10.1, PrefetchGameData 3.5, `CvGame::Read` 3.3); Lua ~1.8 s; terrain bake 7.7-9.7 s | same | same | same | logs + `cdb -pv` sampled runs for owners | same [L] | B |
| `ValidateGameDatabase = 0` saving | ~11.7 s measured (Validate FK 10.1-13.4 s per launch) | 2026-11-10 | | | 1 A/B | `load-sequence.md` 5 [L] | C |
| `LoadGameState_TOTAL` | 38.96 s (B4), 41.22 s (A5); 39-44 s | 2026-11-09 | | | log line | `load-sequence.md` 1-2 [L] | B |
| Real EXE via proxy vs 32-bit DLL, launch -> Lua channel answering, huge | 2 min 45 s vs 2 min 18 s | 2026-11-10 | epic 2500 builds | c5h-a | 1 run each, busy machine | `proxy-design.md` 15.5 [L] | C |
| Real EXE load phase, same save, across arms | 92 s to 303 s | 2026-11-10 18:22-21:41 | epic 2700 b7 / b11 | c5h-a, busy machine | many arms | `proxy-design.md` 16.5 | C (shows the load noise) |
| Goldberg instance: launch -> in game | 2.6-2.9 min (mod activation 91-115 s, load 60-66 s; two loads in parallel 60-66 s each, alone 63 s); window 18-38 s after the launch gate | 2026-11-10 | | c5h-a / c5h-b | few runs | `headless-and-parallel-testing.md` [L] | C |
| Save write time | **no number anywhere** | | | | | | - |

## 3. IPC: latency per call and per crossing

| Metric | Value | Date | Build | Config | Method | Source | Grade |
|---|---|---|---|---|---|---|---|
| Micro-benchmark, shared section, spin, x86 client -> x64 server | median 0.21-0.23 us unpinned (p99 0.31-0.34, p99.9 0.45); pinned 0.18-0.20; HT siblings 0.092; same-process fn pointer 0.04 (2.6 ns without timer) | 2026-11-08 23:17-23:32 | `research/sources/boundary-benchmark` bench | Civ V not running | 1 M calls x 2 passes, rdtsc | `research/03-dll-engine-boundary/ipc-design-and-measurements.md` 1 [L] | B |
| Micro-benchmark, events | median 13.2 us unpinned (p99 35-36, p99.9 185); pinned p99 ~0.6 ms | same | same | same | same | same | B |
| Micro-benchmark, hybrid with 200 us gaps | median 7.4 us (one event wake), p99 136-138 us | same | same | same | 100,000 calls x 2 | same | B |
| Micro-benchmark, batch under one spin flag | 7 ns per call (N = 100), 4 ns (N = 1000); events 96 / 11 ns | same | same | same | same | same | B |
| Server bitness | no measurable difference (srv32 rows) | same | | | | same | B |
| `vpipc` empty call, after task 1209, x86 client | 0.249 us pinned / 0.268 unpinned (before 1209: 0.410 / 0.388); micro 0.203-0.210 / 0.198-0.212 | 2026-11-10 09:15 | library 5a84a05 -> team 4af16c6 | load 10-14 % | **7-pass A/B**, rdtsc as the micro-benchmark | `ipc-design-and-measurements.md` 7.4 [L]; `ipc/results/bench-1209-attrib-*` | A |
| `vpipc` calls by type, after 1209 | empty 0.21-0.31 us median (p99 0.34-0.39, p99.9 0.42-0.61); 3 x i32 0.20-0.27; 64 B string in 0.35-0.37; 1 KB string out 0.43-0.51; one nested callback 0.39-0.63 | 2026-11-10 09:19-09:29 | 4af16c6 | load recorded per series (`-runs.csv`, LOADED marks) | 2 passes | `ipc/results/bench-2026-11-10-0919.md` [L] | B |
| 1209 attribution of the per-call overhead | activeDepth store -65 ns pinned, TLS + locked ops -54, clock reads -19 / -11; all four -110 ns | 2026-11-10 07:55-09:34 | experimental build | load 8-16 % | median of 5 run medians | `ipc-design-and-measurements.md` 7.4 [L] | B |
| Posts (12-byte), per post | after 1209: 0.051-0.070 us (100 + 1 call), 0.034-0.035 us (1000 + 1); before 1206 0.145-0.165 | 2026-11-10 04:10-09:29 | | | 2 passes | `ipc/results/bench-posts-*.md` [L] | B |
| Large payloads | 1 MB 5.8-8.0 GB/s; 50 MB in 1.24-1.36, out 1.59-1.86 GB/s (~6x slower per byte, spill buffers freed above 4 MiB [I]) | 2026-11-10 | after 1209 | lower load | 2 passes | `ipc-design-and-measurements.md` 7.4 [L] | B |
| Wake after idle | first call after 60 s idle 34.6 us, second 0.92 us; first call after a soak gap 21-36 us; idle server CPU 0.078 % of a core | 2026-11-10 | | | spin-budget series, one pass | `ipc/results/spin-budget-2026-11-10-0643.md` [L] | B |
| Spin budget defaults | `replySpinUs` 100, `idleSpinUs` 50, `flushIntervalUs` 1000; a call finding the server asleep pays one wake (8-10 us at 1 ms gaps, 14-20 us at 10 ms) | 2026-11-10 06:43 | | unpinned | 7 x 7 x 5 grid, one pass | same | B |
| Soak, 15 min (final code) | ui p50 0.33-0.35 us, p99 0.66-0.72; core p50 0.34-0.35, p99 0.60-0.81; max 0.1-0.6 ms (2-12 ms in two minutes); client 194 % / server 102 % of a core while active | 2026-11-10 09:39-09:54 | 4af16c6 | load 10.5 % at start | 1 soak (+1 earlier on 573637b) | `ipc/results/soak-2026-11-10-0939.md` [L] | B |
| Robustness timings | server crash -> `PeerDied` 2.5-3.1 ms; restart to verified calls median 123-125 ms (max 222-249); hung detection 1.2-1.4 s | 2026-11-10 04:16 | 3eab756 | Civ V running | scenario harness | `ipc-design-and-measurements.md` 7.5 [L] | B |
| Real per-call cost through proxy and server, by type | scalar args 0.45-0.50 us transit (`Push(int)` 1.00-1.05 round trip vs 0.53-0.57 handler); `CallTestAll` 0.57-0.63; `GetScriptSystem` 0.60-0.75; `setDirty` 0.74-0.86; DLL object argument 0.8-1.1; `getSelectionPlot`, `isCitySelected` 1.4-1.9; Lua thunk 1.25 (Tiny) to 2.8 us (huge) | 2026-11-10 11:57-12:22 | 2300 builds (after the NDEBUG fix; before it 6-7 us per call) | headless host through the proxy, ~50 % load | whole-run counters, millions of calls | `proxy-design.md` 12 [L] | B |
| Projection's assumption per crossing | 0.44 us sync, 0.40 nested, 0.06 post, 15 us wake per frame | 2026-11-10 | | | model | `proxy-design.md` 7 | D |

## 4. Crossings per frame and per AI turn

| Metric | Value | Date | Build | Config | Method | Source | Grade |
|---|---|---|---|---|---|---|---|
| In process, engine -> DLL per idle frame (huge, ~155 fps) | 968 (531 lifetime, 233 static handle, 202 getter); pan/zoom 1,607; slot poll ~830 of 968; ~158 lock pairs per frame (317 lock calls, nearly all re-entries) | 2026-11-09 | instrumented release DLL | huge save, CP + VP + EUI | counters; frames from 9.47 `GetGameTurn` calls per frame (no Present count) | `interfaces-and-traffic.md` 2, 4, 5 [L] | C (frame proxy) |
| In process, all crossings per idle frame (naive mode i) | 1,286 (idle), 1,930 (pan/zoom); worst 16.7 ms slot p99 12-15 k, max 24 k | same | same | same | `projection.txt` | `ipc-design-and-measurements.md` 2 | C |
| In process, per AI turn | 4.8 M (main 3.33 M + core 1.49 M) f1; 4.2 M f2; core thread engine -> DLL 169-174 k; fire-and-forget 25-37 k notifications + ~5 k commands | same | same | same | counters | `interfaces-and-traffic.md` 4, 8; `ipc-design-and-measurements.md` 2 [L] | B |
| Headless through proxy, calls per turn | Tiny 12,984 (DLL->engine 12,151); huge 365,285 (362,496), of which GameEvents ~300 k (`GetScriptSystem` 115 k, `Push(int)` 96 k); boot + map generation Tiny 1.54 M | 2026-11-10 | 2300 builds | headless host | server counters | `proxy-design.md` 12 [L] | B |
| Real EXE, proxy without local answers, idle frame | Tiny 589 (338 fps window), huge 633 (174 fps); during AI turns 1,694 per frame | 2026-11-10 14:43-15:46 | epic 2500 | c5h-a | proxy snapshots, 60 s windows | `proxy-design.md` 15.4 [L] | B |
| Real EXE, idle Tiny frame, local answers off / **on** | 590.1-591.1 / **30.7-33.7** (575-593 answered locally); during AI turns 689-704 / 107-127; verification on c5h-b: 590.2 / 30.9-31.5, AI turns 694-696 / 118-121 | 2026-11-10 18:22-22:39 | b7 20542a2, b11 5fa3f5b, pin 29f15ac38 | c5h-a, c5h-b | proxy snapshots, 3 + 3 arms, reproduced by the MC | `proxy-design.md` 16.5, 16a [L] | A |
| Real EXE, idle huge frame, off / on | 630 / 58.8 (16 lock syncs, 22 plot reads, 6.3 mirror misses); during AI turns 1,776 / 963 | 2026-11-10 20:13- | b11 | c5h-a | 1 run each | `proxy-design.md` 16.5, 16.5b [L] | B |
| Real EXE, crossings per huge AI turn | 2.29 M (epic 2500 P3), off 2.00 M, **on 1.41 M**: UI Lua thunks ~0.8 M, GameEvents 0.18 M, plot reads 0.12 M, slot-poll residue 0.12 M | 2026-11-10 | b11 | 6 AI turns 351-355 | server per-turn counters | `proxy-design.md` 16.5b, 16.7; `tracker/issues/2800-*.md` scope [L] | B |
| Real EXE, lock counts huge run | off: 3,676,150 ui lock calls (324,629 first-level); on: 242 + 461,205 lock-want (353,461 first-level) | 2026-11-10 | b11 | | counters | `proxy-design.md` 16.5b [L] | B |
| Standard game crossings per AI turn, real EXE | **none** | | | | | | - |

## 5. IPC time per frame and per AI turn (derived)

| Metric | Value | Basis | Source | Grade |
|---|---|---|---|---|
| Per idle frame, naive spin / event | 270 us / 17.0 ms (mode i); worst slot 2.6-5.0 ms spin, 160-310 ms event | 2026-11-08 micro x 2026-11-09 counts | `ipc-design-and-measurements.md` 2, 3 | D |
| Per huge AI turn, naive spin / event | 0.87-1.01 s / 55-64 s | same | same | D |
| Per huge AI turn, projection with hybrid UI Lua | 0.24-0.30 s (all Lua proxied 0.53-0.63 s) | model | `proxy-design.md` 7 | D, superseded |
| Per huge AI turn, from measured crossings | **0.6-1.4 s** (1.41 M x 0.45-1.0 us) against AI turns of 27-33 s | 2026-11-10 counts x per-type costs | `proxy-design.md` 16.7 | D (current) |
| Per idle frame, proxy without local answers | ~0.3-0.6 ms (589-633 x 0.45-1.0 us) | same | `proxy-design.md` 15.4 | D |
| UI thread 1.5-1.66 M sync calls per turn after 1209 | ~0.37-0.42 s (was ~0.65-0.75 s before 1209) | micro x counts | `ipc-design-and-measurements.md` 7.4 | D |
| Posts per AI turn after 1209 | ~0.9-1.3 ms (25-37 k posts) | same | same | D |

## 6. Frame rate and frame time

| Metric | Value | Date | Build | Config | Method | Source | Grade |
|---|---|---|---|---|---|---|---|
| Real EXE, **Tiny idle**, proxy with local answers / without / 32-bit DLL | **399.8 / 348.7 / 425.1 fps** mean (median 399.2 / 348.1 / 426.5; sd 46.2 / 18.2 / 21.1); paired on - B: -99, -8, +23, -8, -31, -29 (mean -25, -6 %); off 18 % below B in every pair | 2026-11-10 18:22-19:20 (b7) and 21:00-21:41 (b11) | proxy b7 20542a2 and b11 5fa3f5b (**mixed builds**); B = i0-ship e6f904ad6 + instance-aware instruments | c5h-a, Tiny turn-8 save, nothing open | Lua frame counter (`Events.LocalMachineAppUpdate`) over 45 s, **6 interleaved per arm**, busy shared machine (load not sampled; load phase 92-303 s) | `proxy-design.md` 16.5, 16.5b; `server/evidence/2700/fps-tiny-all.txt` [L] | B |
| Real EXE, **huge idle**, on / B / off | 221.0 / 207.0 / 184.8 fps | 2026-11-10 | b11 | c5h-a | 1 window each | `proxy-design.md` 16.5b [L] | C |
| Real EXE, huge, frames during AI turns, off / on | 45.9 / 54.1 fps | 2026-11-10 | b11 | | proxy snapshots over the checklist | same | C |
| Real EXE (epic 2500), proxy vs 32-bit DLL | huge idle 171 vs 183 fps; Tiny idle 311 vs 395 fps | 2026-11-10 | epic 2500 | c5h-a | 1 run, **no saved output** (MC correction 16:58) | `proxy-design.md` 15.5 | C, superseded by 16.5b |
| Real EXE (epic 2500), frame time from proxy snapshots | Tiny idle 2.96 / p50 2.93 / p99 4.38 ms; huge idle 5.74 / 5.68 / 9.34 ms; huge AI turns 19.1 / 6.9 / 128 ms (52 fps) | 2026-11-10 | epic 2500 | c5h-a | 60 s windows, 1 run | `proxy-design.md` 15.4 [L] | C |
| In process, uncapped, huge (boundary benchmark) | ~155 fps idle (6.4 ms frame); 140.6 tech tree | 2026-11-09 | instrumented release DLL | | 15-30 s windows | `interfaces-and-traffic.md` 2-3 [L] | C |
| Two cloaked instances idle at once | 113-155 fps each | 2026-11-10 | | Goldberg | 1 run | `headless-and-parallel-testing.md` [L] | C |
| Compositor prototype in the real EXE (B4, huge, exec 34) | passthrough 3.561 ms (284 fps) vs composite 3.565 ms (283 fps); present 3.605 ms | 2026-11-09 21:25-22:20 | `shell_composite.dll` + `shell_server.exe` prototype | B4 preset, Release-5.4.8 experiment DLL | 60 s per mode, **3 interleaved rounds** | `research/05-graphics-composition/shared-texture-and-compositor.md` 5; `research/sources/shell-gates/g3_ab.txt` [L] | B |
| Compositor stand-in, uncapped 2560x1440 | no DLL 0.0927 ms, passthrough 0.0927, composite 0.0873-0.0877 (mode differences < 0.01 ms, spread up to 0.03) | 2026-11-09 | prototype | test host | 3 rounds x 20 s per mode | same 4; `research/sources/shell-gates/ab_table.md` [L] | B |

## 7. Server render time and composite latency (prototype only)

| Metric | Value | Date | Config | Method | Source | Grade |
|---|---|---|---|---|---|---|
| Composite CPU per frame (live) | 0.163 ms (exec 34), 0.217 (present); stand-in ~5 us uncapped, 0.118-0.150 ms at 165 Hz | 2026-11-09 | prototype | 3 rounds | `shared-texture-and-compositor.md` 4-5 [L] | B |
| `AcquireSync` CPU per new server frame | 0.13 ms stand-in, 0.25 at 165 Hz, **0.61 ms live** (0.85 present) | same | | | same | B |
| GPU copy / draw | 0.27 / 0.017 ms live; stand-in 14 us draw, 70-75 us with the 14.7 MB copy | same | | GPU timestamps (not a cost measure at display rate) | same | C |
| Latency server frame -> client Present (CPU) | live 2.50 / p99 6.48 ms (exec 34), 1.72 / 4.80 (present 1); stand-in 60 Hz 4.03 / 7.79, free-running 0.27-0.33 / 0.63-0.80 | same | | | same | B |
| Server render time per frame (the real server's renderer) | **none** | | | | | - |

## 8. Memory

| Metric | Value | Date | Build | Config | Method | Source | Grade |
|---|---|---|---|---|---|---|---|
| 32-bit EXE ceiling | dies at 4,025 MB of 4,095.9 MB address space; 510.6 MB reserved-never-committed; sub-2GB half ~1.6 GB committed from turn 0 | 2026-09 | VP DLL of the time | 20,340-plot map | crash runs, vmmap | `research/04-memory-investigation/README.md`, `ownership-and-ceiling.md` [L] | B |
| Ownership of committed memory | EXE and others ~78 % (fixed from turn 0), DLL 5-9 %, Lua ~4 %, video driver 9-12 % | 2026-09 | | | instruments | same | B |
| Growth after the CitySites fix, 20,340-plot map | **2.476 MB/turn committed** (heap busy 1.148, free list 0.723, overhead 0.085, non-heap 0.520); +8,239 blocks/turn | 2026-09-10 | a26506ae8 era | GameId 62, t20-317 | 1 crash run | `research/04-memory-investigation/leaks-and-growth.md` 2 [L] | B |
| Leak fix | heap growth 4.05 -> 0.63 MB/turn (huge map) | 2026-09-07 | a26506ae8 | | | `leaks-and-growth.md` 1 | B |
| Headless growth, Standard / Huge, before the growth fixes | committed 0.354 / 0.378 MB/turn; DLL live blocks 1,498.7 / 2,610.0 per turn | 2026-11-10 | Release-5.4.8 + instruments | c5h, seeds 1001/2002, t20-200 slopes | 1 run per arm | `leaks-and-growth.md` 2 "Headless baselines", 4 [L] | B |
| Headless growth on i0-ship | committed **0.254 / 0.323 MB/turn**; DLL live blocks 372.9 (-75.1 %) / 775.3 (-70.3 %) per turn; heap busy 0.046 / 0.101 MB/turn | 2026-11-10 15:05-15:29 | instrumented i0-ship (throwaway 9168772a0) | as above | 1 run per arm | `leaks-and-growth.md` 4; `tools/d10/evidence/2404/measure-{std,huge}200-growth.txt` [L] | B |
| Real EXE growth over 4 AI turns (huge 350-354) | committed 2407 -> 2501 MB fixed DLL, 2435 -> 2510 base; +-28 MB run noise, cannot resolve 0.04-0.08 MB/turn | 2026-11-10 | instance-aware 440878983 | c5h-b | 1 run per arm | `leaks-and-growth.md` 4 (live check 1305) [L] | C |
| Real EXE, proxy vs 32-bit DLL, huge | EXE private bytes idle after load **1,812 vs 2,078 MB**; after 3 AI turns 2,030 vs 2,184; peak 2,061 vs 2,293; largest free block minimum **1,194 vs 886 MB** | 2026-11-10 | epic 2500 builds | c5h-a | 5 s samples, 1 run each, same tool both sides | `proxy-design.md` 15.5 [L] | C |
| Real EXE, Tiny t18 idle, proxy vs 32-bit | 1,676 MB (+158 MB server) vs 1,800 MB | 2026-11-10 | epic 2500 | | different tool, **output not kept** | `proxy-design.md` 15.5 (MC correction) | C |
| Server private bytes behind the proxy, huge | idle 235, after 3 turns 280 MB, peak WS 319 MB | 2026-11-10 | epic 2500 | | **output not kept** | same | C |
| x64 server headless, peak working set | Tiny 166 MB, Standard 200 turns 249 MB (32-bit host 279 / 368 MB); huge 323-396 MB (32-bit 340-445 MB) | 2026-11-10 | epic 1500 / 2003 | headless | 1 run each | `server-host.md` 1, 10 [L] | C |
| Goldberg instance in game, huge | private 2.14-2.29 GB, WS 1.79-2.01 GB, committed 2.43-2.62 GB, largest free 0.89-1.15 GB; ~1.5-1.6 GB VRAM; system commit +~5 GB for two; disk 8.2 GB | 2026-11-10 | | c5h-a / c5h-b | 2 instances | `headless-and-parallel-testing.md` [L] | C |
| Real EXE load, memory | B4 peaks at 1,601 MB private at 151 s, settles 1,537-1,540; A5 peaks 1,917, settles 1,865 | 2026-11-09 | | huge | ~6 s samples, 1 run each | `load-sequence.md` 2 [L] | C |
| civ5-headless host peaks | VP Tiny ~350 MB, Standard to t281 442 MB; real game 1.5-1.7 GB; host 288 MB vs real game 1,570 MB | 2026-10 | | | | `research/03-dll-engine-boundary/headless-host.md`; `research/sources/civ5-headless/verification.md` | C |
| vpipc soak | private bytes flat (+88 KB client, +32 KB server), handles constant | 2026-11-10 | 4af16c6 | 15 min | 2 soaks | `ipc-design-and-measurements.md` 7.4 [L] | B |
| Compositor soak, real EXE | game private 1,663 -> 1,734 MB over two AI turns (B4 without compositor +81 MB over three); server 87 MB | 2026-11-09 | prototype | B4, huge | 15.5 min | `shared-texture-and-compositor.md` 5 [L] | C |
| Server per-turn growth (x64 server, any game) | **none** | | | | | | - |

## 9. Statistics logging (SQLite) cost

| Metric | Value | Date | Build | Method | Source | Grade |
|---|---|---|---|---|---|---|
| x86 logging cost, default pragmas (`synchronous = FULL`, `journal_mode = DELETE`), Standard | ~0.35-0.45 s/turn (1.643 -> 1.201 with the pragmas, -27 %; logging off 1.21-1.23) | 2026-11-10 | base9011 / baseP9011 / ship | 2 runs per arm, one at a time | `d10-save-identity-test.md` 4b.5 [L] | B |
| x64 logging cost, closed-library defaults (SQLite 3.7.17) | 0.52 s/turn (1.357 on, 0.841 off) | 2026-11-10 | 29f15ac38 | same | same | B |
| With 12 hosts on one disk | the pragmas cut Standard s/turn by ~63 % (x86 4.58 -> 1.70) and ~69 % (x64 4.20 -> 1.31); D10 wall 1,728 -> 1,251 s | 2026-11-10 | 9013 pins | 1 run per arm, parallel | `d10-save-identity-test.md` 4c [L] | C |
| Lost statistics rows under load (bug 0084) | 1-2 tables of one turn lost in 2 of 2 side-by-side runs; 0 in 16 one-at-a-time runs and in 9013's 2 runs alone; busy handler gives up after 30 x 2 ms [I] | 2026-11-10 | 29f15ac38 | | `tracker/issues/0084-*.md` | C |
| Players with logging off (VP default) | no gap: `SQLITE_LOGGING` = 0 in VP 5.4.8's `NewCustomModOptions.xml`; only the toolkit's `autoplay` mod sets 1 | 2026-11-10 | | source read [C] + runs | `d10-save-identity-test.md` 4b.5 | B |

## 10. Build and test-run times

| Metric | Value | Date | Method | Source | Grade |
|---|---|---|---|---|---|
| x64 `build_all.py --config release` | 538 s (stubs 8, CvGameDatabase 27, DLL 479 of which full-LTO link 383, host 11, smoke 12) | 2026-11-10 04:40-04:49 | 1 run | `x64-dll-port.md` 10.6 [L] | C |
| `server/build_server.py --config release --offline` | build_all 476 s (DLL 422 s), server step OK, total reported 496 s | 2026-11-10 10:33-10:41 | 1 run, loaded | `server-host.md` 11.4 [L] | C |
| Server build from scratch | debug ~2 min, release ~9 min (full-LTO link); server step alone 5-21 s | - | README statement | `server/README.md` | C |
| D10 full test (`run_d10.py`, 3 games, `--jobs` 12) | 1,653 s (pins of 2400), 1,728 s (no pragmas), 1,251 s (pragmas); control run Standard + Huge 1,528 s | 2026-11-10 | 1 run each | `d10-save-identity-test.md` 3, 4, 4c [L] | C |
| `tools/d10/tests/` | about 1 min, 40 tests | 2026-11-10 | | `tracker/issues/9011-*.md` | C |
| Instance create / verify | create 98.4-120.4 s; verify 67.9-71.6 s; quick verify 17.0 s | 2026-11-10 | | `headless-and-parallel-testing.md` [L] | C |

## 11. Contradictions and superseded numbers (which is current)

1. **"x64 at par with 32-bit" (`server-host.md` section 1: Standard 1.505 vs 1.48 s/turn; the 1505 reference
   run's 1.476).** Not a like-for-like comparison: the 32-bit reference DLL 4e91c58a carries the statistics pragmas,
   the x64 server did not. **Current: `d10-save-identity-test.md` 4b.5** (9011): with logging off the x64 server is
   ~30 % faster than every 32-bit build (0.841 vs 1.21-1.23 s/turn); with logging on and no pragmas it pays 0.52
   s/turn more, which landed it at the pragma build's level. `server-host.md` section 1's row is not corrected in
   place; the knowledge-base owner of that file should add the note.
2. **"Stock is 3-6x faster than our builds"** (2404 side observation) is a parallel-load artefact plus the pragmas;
   corrected in `tracker/issues/2404-*.md` by the MC and by 9011. **Current: 4b.5.**
3. **Official 4e91c58a on Standard** measured three times: 1.476 (1505, single run), 1.335 (MC verification, single
   run), 1.292 (9011, 2 runs, 1.192 / 1.393). Same build; the spread (0.18 s/turn) is machine load. 9011's figure is
   the best-documented.
4. **Huge x64 s/turn**: 9.0 (quiet, one process), 8.27 (direct, ~50 % loaded, 2300), 10.8-10.9 (parallel pairs),
   12.3-21.4 (inside D10, 12 at once). No contradiction in kind, but every figure is on a different build and load;
   none is on the current pins alone.
5. **IPC per huge AI turn**: projected 0.24-0.30 s (`proxy-design.md` 7, every lifetime call local, 0.42-0.47 M UI
   Lua calls) vs 0.6-1.4 s derived from the measured 1.41 M crossings (16.7). **Current: 16.7.** The study's "0.25 s
   per turn" and REPORT.md's 0.32-0.35 s (UI-thread mode ii) are older projections of the same thing.
6. **Per-call cost through the proxy**: "0.45-0.5 us transit" as a general figure was replaced by the per-type
   figures of `proxy-design.md` 12 (9008 correction); the projection assumed 0.44 us flat.
7. **Crossings per idle frame**: 1,286 (in-process benchmark, mode i: inbound + lock calls + Lua, frames from a
   `GetGameTurn` proxy) vs 589-633 (proxy snapshots of the real EXE, epic 2500, frames from the slot-poll marker).
   Different definitions and frame counters, not a change in the EXE. The study's "about 1,300 crossings per frame
   are the slot poll" conflates the two (`interfaces-and-traffic.md` contradictions).
8. **Frame rate proxy vs 32-bit DLL**: epic 2500's 171 vs 183 (huge) and 311 vs 395 (Tiny) have no saved output;
   **current: `proxy-design.md` 16.5b** (Tiny 6 interleaved per arm: on 399.8, off 348.7, B 425.1; huge one each:
   on 221.0, B 207.0, off 184.8). Epic 2800 is re-measuring on one build.
9. **Tiny x64 s/turn**: 0.265 (epic 1500) vs 0.23 (epic 1700, real localization). Single runs, load unknown; not a
   regression or a fix.
10. **"Lock pairs per frame" 317** (1300 brief, handoff 2.4) is 317 lock calls = ~158 pairs, nearly all re-entries
    (`interfaces-and-traffic.md` 5).

## 12. Gaps: charter metrics with no trustworthy (A or B) number

| Charter metric | Best existing | Missing |
|---|---|---|
| Seconds per AI turn, **huge save, x64 server** | B: 9.0 s/turn, one quiet run, throwaway build, no pragmas, 10 turns | Any series on the current pin c43ea1b9f (pragmas in), 350-355, interleaved, with spread and load |
| Seconds per AI turn, **huge save, 32-bit DLL** | B: 10.4 s/turn, one quiet run, x86 of a throwaway source | Same on i0-ship ccfd6dd4d |
| Seconds per AI turn, **Standard, x64 and x86** | B: 9011 (2 runs per arm, pre-pragma pins 29f15ac38 / e6f904ad6, plus baseP9011) | Current pins one at a time (only 12-way parallel numbers exist, grade C); >= 6 interleaved runs; turns 0-50 as the charter asks (9011 has turns 2-33 and 1-200) |
| Where the AI-turn time goes | Call counts only (`server/evidence/1503/std200/server-report.txt`: `IsGameCoreThread` 8.9 M, `GetScriptSystem` 6.0 M, lock pairs 1.87 M per 200 turns) | **No sampling profile of an AI turn exists** in either build |
| Crossings per frame / per AI turn through the proxy | A (Tiny idle frame), B (huge AI turn 1.41 M) | Standard game; per-turn crossings on the current pins; a crossing count from the 32-bit DLL's side for the same steps |
| Time per crossing | A for the transport (0.25-0.27 us empty call); B for real calls by type (loaded machine) | Per-type cost on a quiet machine and inside the real EXE (only the headless host measured it) |
| Frame time via proxy vs 32-bit DLL | B for Tiny idle (mixed builds b7/b11, load not sampled); C for huge | Huge idle and frames during AI turns, >= 6 interleaved on one build, with load (epic 2800's definition of done) |
| Load time | B for the 32-bit EXE (n = 1 per configuration, 141-157 s); C for via-proxy (2:45 vs 2:18, one run) | Interleaved load series via proxy vs 32-bit DLL; server-side load phases (cache 10-12 s, read 0.35 s) inside the real EXE |
| **Save time** | **none** | Any measurement of save write time, x64 or x86, headless or real EXE |
| Memory, 32-bit EXE address space | C (one run per side, 5 s samples) | Repeated runs; address space after many turns via proxy |
| Memory, server private bytes | C, output not kept | Any kept series |
| **Per-turn growth** | B for the 32-bit DLL headless (one run per arm) | Server (x64) per-turn growth: **none**; EXE per-turn growth via proxy: none (4 turns cannot resolve it) |
| **Server render time per frame** | none (only the prototype `shell_server.exe`) | Everything; waits for the renderer epics (2900) |
| Composite latency | B on the prototype only (2.5 ms live, 3 rounds) | The real server's renderer and compositor |
| Statistics logging under parallel load with the pragmas | none | Whether bug 0084's lost rows still happen (tracker 0084) |

## 13. What the numbers already say

1. **The statistics log's disk syncs, not the fork's code, were the 32-bit "slowdown".** Default SQLite settings cost
   0.35-0.45 s per Standard turn on x86 (1.643 -> 1.201 s/turn with `synchronous = OFF` and `journal_mode = PERSIST`)
   and 0.52 s on x64; in the 12-way D10 test the pragmas cut Standard turns by 63-69 %. Players with logging off (the
   VP default) never paid it. Both pins carry the pragmas since D35. Any timing taken with logging on and an older
   build mostly measures disk syncs. Sources: `d10-save-identity-test.md` 4b.5, 4c; decision D35. [B]
2. **x64 compute is about 30 % faster than 32-bit on Standard** with logging off (0.841 vs 1.21-1.23 s/turn) and
   13 % faster on the huge save in one quiet pair (9.0 vs 10.4 s/turn). Sources: 4b.5; `server-host.md` 10. [B]
3. **Machine load moves per-turn times by 2.5-3.5x.** The same i0-ship build ran 1.556 s/turn alone and 4.58-4.77 in
   the 12-way test; the official DLL 1.29-1.48 alone vs "3-6x faster than ours" in a four-arm parallel run. Numbers
   from parallel runs are not speed measurements. Sources: 4b.5, 4c; `tracker/issues/2404-*.md`. [B/C]
4. **A huge AI turn in the real EXE crosses the boundary 1.41 M times with local answers on** (2.00 M off), mostly
   UI Lua thunks (0.8 M), which at the measured 0.45-1.0 us per real call is 0.6-1.4 s of IPC against 27-33 s turns
   (about 2-5 %). The next cuts are the UI-Lua mirror, GameEvents batching, a plot mirror and fewer lock sessions
   (epic 2800). Source: `proxy-design.md` 16.5b, 16.7. [B counts, D time]
5. **The transport is at the micro-benchmark's floor**: empty call 0.25-0.27 us (micro 0.20-0.21), p99.9 0.42-0.61
   us; events cost 13 us per call and are ruled out; a real call through proxy and server costs 0.45-1.9 us by type,
   so marshalling, staging and handle work, not the transport, now dominate per-call cost. Sources:
   `ipc-design-and-measurements.md` 1, 7.4; `proxy-design.md` 12. [A/B]
6. **Idle frames through the proxy are about 6 % below the 32-bit DLL on Tiny** (399.8 vs 425.1 fps, 5 of 6 pairs
   below, within the on-arm's spread); without local answers 18 % below in every pair. An idle frame now crosses 31
   (Tiny) to 58 (huge) times, 16 of them lock syncs. Source: `proxy-design.md` 16.5b; `server/evidence/2700/fps-tiny-all.txt`. [B]
7. **Moving game state out frees the 32-bit EXE**: 230-270 MB less private memory and ~300 MB more largest free
   block on the huge save (one run). After the growth fixes the 32-bit DLL grows 0.25 (Standard) to 0.32 (Huge) MB
   committed per turn headless, with block growth cut 70-75 %. Sources: `proxy-design.md` 15.5;
   `leaks-and-growth.md` 4. [C / B]
8. **Load time is the EXE's, not the DLL's**: 141-157 s process start to `WaitForNetPlayers` on the huge save, of
   which the DLL is 17.4 s (ValidateGameDatabase 10.1 s, switchable) and LandmarkSystem's view build 27 s;
   `CacheGameDatabaseData` costs 10-12 s in release in both bitnesses, and the server reads the huge save in 0.35 s.
   Sources: `load-sequence.md` 1; `server-host.md` 4, 10; `x64-dll-port.md` 10.5. [B]
9. **The real game spends far more wall time per AI turn than its compute**: in process the huge turn's core thread
   was busy 25-27 s of 58-127 s wall (leader screens 20-89 s); the headless hosts play the same save at 9-10 s per
   turn; civ5-headless measured the real game at 13x the host per turn (VP, Tiny). Where the core thread's extra
   time goes in the real EXE (the 25-27 s against 9-10 s headless) is not measured. Sources:
   `interfaces-and-traffic.md` 8; `server-host.md` 10; `research/sources/civ5-headless/verification.md`. [C]
10. **Compositing is not a frame-time cost**: +0.004 ms frame time, 0.16-0.22 ms composite CPU, `AcquireSync` 0.6 ms
    per server frame on the render thread, 2.5 ms latency (prototype, live, 3 interleaved rounds). Source:
    `shared-texture-and-compositor.md` 5. [B]

Sources: every file named in the tables above; the read covered `research/` (all sections and `sources/`),
`server/evidence/` (1503, 1505, 2700 READMEs and reports), `ipc/README.md` and `ipc/results/`,
`tools/d10/evidence/` (9011, 9013, 2404), `tools/drift/README.md`, `proxy/README.md`, `server/README.md`, and the
tracker issues 0084, 2404, 2800, 9011, 9013, 9014 (2700's numbers through `proxy-design.md` 16, which its team wrote). 9014 (instance cost survey) is in progress and had not
written its per-instance cost into research at the time of reading.
