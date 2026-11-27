# Independent verification of epic 3300 (the renderer in the server, lockstep, depth, ring) - one-off 9055

Written 2026-11-13 by one-off 9055, which did not build any of epic 3300. It checks the claims of decision D99 and of the
as-built sections of `renderer-in-server-design.md` (1.1-4.13) by its own measurements where it could. Marks: `[C]` read in
code, `[L]` measured live by 9055, `[R]` 9055's own analysis of the team's raw records (code written from the CSV writer's
column list and the log formats, no team analysis code), `[I]` inferred.

**Set-up.** Workstation, instance c5h-b (Goldberg, `%USERPROFILE%/c5-instances`), DX11 EXE 04CFDC71..., CP/VP/EUI, window
2554 x 1411, RTX 4080. **Asset set (D94): Vibrant Textures present**, `install/Assets/DLC/VT` 55 files, 47,300,858 B, VT hash
**`45b3b43359279105fd9d0924e71192d413af38ad22c9d73fd00eacc2c73f0a45`** (9049's method, recomputed by 9055's own
`D:/c5-64bit-work/9055/scripts/vthash.py` from asset-pipeline.md 1.7's description). Builds from a **fresh clone of main at
478519e5** (`D:/c5-64bit-work/9055/clone`), 2026-11-13 17:33-17:35 EST: `server/build_server.py --src
D:/c5-64bit-work/9045/src-x64f --deps D:/c5-64bit-work/9045/bF/deps --config release --offline` -> server.exe
`4C5FB431...`, proxy `05BE7591...`; `shell/build.cmd` -> composite `72D749ED...`; x64 DLL `74588135...` (the D96 pin, layout
`x64-04b1211b-pa803776`, equal to 9045's deps build). Save copy `C5H-9055-HUGE_0350` (of the instance's
`C5H-VP8P-HUGE_0350`, SHA-256 C499D3C6..., turn 350; removed at each restore). Driver `shell/evidence/9055/run_9055.py`: it
imports 3311's driver only for the safety-critical steps (prep with SHA-256 backup, product layout, no-focus launch, guard
checks, memsampler, the AI-turn loop, harness stop, restore by hash); what it measures (D shots, B phases, C windows) and
every analysis script (`D:/c5-64bit-work/9055/scripts/`: `lk_analyze.py`, `ft_analyze.py`, `ft_stats.py`, `older_why.py`,
`e_attrib.py`, `e_turns.py`, `d_analyze.py`, `run_summary.py`) are its own. Raw logs, CSVs and shots stay on D: (D83).

## 0. Verdicts

| Claim (D99 / design 4.x) | Verdict | Where |
|---|---|---|
| F: render 77, frame 34, lockstep, feed 87, options 143, shell_unit 145 pass from a fresh clone | **confirmed by own run** (lockstep is now 28 tests, not 23: tests added by 3312/3313) | 1 |
| F: ASan once, 0 reports | **confirmed by own run with one flake**: render under MSVC ASan 76/77, 0 reports, the failing test passed 3/3 plain and 3/3 ASan on rerun (V3) | 1 |
| The render thread never calls into the DLL (D81, bug 0103) | **confirmed by code reading** | 2 |
| A: huge save, product path, 30 AI turns, 0 LOUD, no crash, no dialog, no shell_server.exe | **confirmed by own measurement** | 3 |
| Proxy shell product form: Terrain bit cleared within one 10-ms watcher poll | **confirmed by own measurement** (29.6 s -> 10.2 ms) | 3 |
| B: 99.997 / 99.999 % exact under the quiet lock, turnaround p50 0.77 ms, waits 0.03 % max 1.02 ms | **confirmed by own analysis of the raw records** [R]; own runs on D96: 99.92 % (idle/pan, no hold), 99.975 % (C windows under the hold), turnaround p50 0.93-0.95 ms | 4, 5.2 |
| B on D96 during AI turns 97.84 %, misses follow re-bakes | **confirmed by own measurement** (98.30 %, r = -0.53) | 4 |
| C: ring + fence costs nothing measurable over D33 passthrough | **confirmed by own measurement** on D96 (+0.032 [-0.056, +0.119] ms, grade A in one launch) | 5.2 |
| C: 0.55 ms faster than today's compositor; -2.15 / -2.24 ms against stock | **confirmed only by own analysis of the team's raw logs** [R] (no own launch of those arms) | 5.1 |
| C as a frame-rate statement | **incomplete**: every figure drops frames over 100 ms, which hides a 2-Hz ~150-ms stall of the product path (V1, not the renderer's) | 5.3 |
| D: the server's depth hides static-model pixels (~9-15 %) | **confirmed by own measurement** (10.7-12.7 % at three cameras) | 6 |
| D / 0134: part of the hidden building pixels are visible in stock | **confirmed by own measurement**, stronger and local: ~60 % at Thebes, ~23 % load view, ~4-11 % Akhetaten (V5) | 6 |
| E: server +3.11 MB/turn against +1.20 for render=0 | **confirmed by own measurement** (+3.15 [2.77, 3.52] against +1.33 [1.09, 1.58]) | 7.1 |
| 0137: the excess is the residency's arrays growing with re-bakes | **supported** (about 1 MB private per MB of arrays; re-bakes alone cost nothing) and **bounded** (2 slices per tile) | 7.2 |
| The as-built research matches the code | **confirmed, with stale rows** (V7) | 8 |

## 1. Build and tests from a fresh clone (F) [L, 2026-11-13 17:33-18:5x EST]

`git clone -b main` of the repository at 478519e5 into `D:/c5-64bit-work/9055/clone`; every build and suite through
`quietlock.py run` (no hold), script `D:/c5-64bit-work/9055/build_test.sh`, logs `D:/c5-64bit-work/9055/suite-*.log`.

| Suite | Command (from the clone) | Result |
|---|---|---|
| render | `server/tests/render/build_render_tests.py --out D:/c5-64bit-work/9055/rt`, `pytest server/tests/render` | **77 passed**, 0 skipped (244 s) |
| frame | `shell/frame/build.cmd D:\c5-64bit-work\9055\frame-build`, `pytest shell/frame/tests/test_ring.py` | **34 passed** (60 s) |
| lockstep | `pytest shell/tests/test_lockstep.py` (C5_SHELL_BUILD, C5_FRAME_BUILD set) | **28 passed** (174 s); the "23" of D99 is 3304's count, 3312 and 3313 added five |
| feed | `server/tests/feed/run_feed_test.py --out ...` | **87 checks passed**, 0 failed; the injected negative exits 1 as required |
| options | `ipc/build.py --target test_options`; x86 and x64 `test_options.exe docs/OPTIONS.md <scratch>` | **143 passed** in each bitness |
| shell_unit | `proxy/tools/test_proxy_rt.py --build D:/c5-64bit-work/9055/b1 --scratch ... --only-shell` | **shell_unit 145/145**; the loader checks 19/19 (bad hash, other proxy, missing composite, missing manifest refused) |
| render under MSVC ASan | `build_render_tests.py --asan` (cl 14.43), the suite under `shell/tools/memcap.py --mb 6144` with the ASan runtime on PATH and `ASAN_OPTIONS=log_path="..."` (18:43-18:48) | **76 passed, 1 failed, 0 ASan reports** (peak commit 1,182 MB); the failure: `test_pacing_falls_back_when_publishing_stops_and_returns_when_it_resumes` counted the note "pacing: legacy (the composite has never written the publish block)" not exactly once (the draw rates in all three phases passed). Rerun 19:11-19:13: **3/3 plain and 3/3 ASan pass** (`flaky-*.log`): a timing-dependent assertion, 1 failure in 7 runs (bug 0141's kind) |

Two environment traps of the ASan run, worth a line in `server/tests/render/conftest.py` [L]: without the folder of
`clang_rt.asan_dynamic-x86_64.dll` on PATH every test that starts `render_host_test.exe` fails with exit 3221225781
(0xC0000135, STATUS_DLL_NOT_FOUND; 70 of 77); and an unquoted `log_path=D:/...` breaks ASAN_OPTIONS at the drive colon
("AddressSanitizer: ERROR: expected '=' in ASAN_OPTIONS"), which fails every test the same way. The team's script
(`D:/c5-64bit-work/3300/w11/tests_asan.sh`) quotes the path and sets PATH; the conftest's docstring says neither. An own
canary (`D:/c5-64bit-work/9055/canary/canary.c`, one byte past a 16-byte block, `cl /fsanitize=address`, 14.43) reports
`heap-buffer-overflow` into the quoted log path, so the environment reports. (Both traps removed by bug 0149, section 12.)

## 2. The render thread never calls into the DLL (D81, bug 0103) [C, main 478519e5]

**Holds.** Every path the render thread can take, read from `server/src/render/srv_render_host.cpp` (`RunSeh` -> `RunCpp`
-> `CreateDevice` / `Loop` / `Rebuild` / `DrawFrame` / `SecondLine` / `LogMemory`) and the files it calls:
- **Includes.** `server/src/render/*` and `shell/frame/ring_sink.cpp` are compiled with `render_compile_args`
  (`server/build_server.py` RENDER_INCLUDES: `server/src/render`, `server/src`, `shell/common`, `shell/frame`,
  `render/include`, `options/include`; no fork path, none of the DLL's predefines). The only headers they include from
  outside those folders' own code are `feed/srv_feed.h` (no DLL type), `srv_power.h` (inline, `<windows.h>` only),
  `srv_options.h` -> `vp64_options.h` (the options library), `shell_shared.h`, `vp_frame.h`, `ring_common.h`, `frame_sink.h`
  and c5render's headers. A grep of `render/src`, `render/include`, `server/src/render`, `server/src/feed` and `shell/frame`
  for `ICvGame`, `ICvMap`, `ICvPlot`, `CvGameCore`, `LoadLibrary` and `GetProcAddress` finds nothing but
  `GetProcAddress(kernel32, "SetThreadDescription")` and the SEH filter's `GetModuleHandleExA(FROM_ADDRESS)` (it names the
  module of a faulting address; no call into it).
- **Calls out of the render units.** (1) `HostConfig::log` = `RenderLog` -> `SrvLog` (`srv_main.cpp`: `fprintf` into stdout
  and server.log under `s_csLog`; no DLL call); (2) `HostConfig::bindImports` = `BindRenderImports` (`__HrLoadAllImportsForDll`
  of d3d11, dxgi, D3DCOMPILER_47); (3) `SrvOptions()` (a const reference to the options the main thread read at start);
  (4) `SrvPowerOptOut`; (5) `SrvFeed::Drain` and `SrvFeed::CopyPlots` (`srv_feed.cpp`: an SRW-locked swap of the event
  vector and a copy of the published `PlotRecord` table; the feed has no callback or function pointer, grep). Nothing in the
  render units names `SrvRemote`, `SrvRt` or any `srv_remote_*` function.
- **The DLL side stays on the DLL's threads.** The writers (`server/src/remote/srv_remote_feed.cpp`: the ten `On*` overrides
  and `AtLeaveUpdate`) run inside the DLL's own calls on the service threads and return before any DLL call while
  `SrvFeed::Enabled()` is false (lines 341-479). The render thread only reads their results.
- **Residual coupling, not a call**: the render thread can block on `s_csLog` while a service thread logs, and on the
  feed's SRW lock while `AtLeaveUpdate` publishes a table (0.8-1.5 ms per turn on huge, design 2.2). Neither is a DLL
  object. The include barrier stops naming a DLL type, not a call through an `extern` declaration; the barrier plus this
  reading together are the evidence, and a future `extern` in a render unit would pass the build check (a note for review).

## 3. A: the product path on the D96 pins, 30 AI turns [L, 2026-11-13 18:01:59-18:24:07 EST]

Run `D:/c5-64bit-work/9055/live-product/out` (`run_9055.py run --arm product`, under `quietlock.py run`, no hold).
Product set-up by 3308's prep: `VP64Options.ini [Renderer] ServerRenderer = 1`, the composite and its manifest beside the
proxy, the shipped `shell_composite.ini` with the driver's test keys (`lockstep_csv=1`, `trace_frames=0`,
`tag_match=clear_bb` = the shipped default), D33 by darkred.py with Overlay on (D93), the server's own bake (Residency,
radius -1).
- **30 AI turns 350 -> 379**, every `turns.py end` / `wait` exit 0 (25-34 s per turn); guard check 0 at launch, in game and
  at the end; start-up watch ok; harness stop 0; restore by hash (backup restore ok, provision verify 0).
- **0 LOUD lines** in server.log (246 lines), render.log (2,616), vpproxy.log (413) and shell_composite.log (2,355); no
  `EXCEPTION`, no `DISABLED`; no crash, no dialog (no step returned 8).
- **The EXE's children** (harness status in game): `server.exe` and `conhost.exe`; **no `shell_server.exe`**. server.log:
  "render thread started (handover ring)".
- **Proxy shell in product form** (vpproxy.log): "[Renderer] ServerRenderer = 1 (from the file)", the composite's SHA-256
  checked and loaded in 2.2 ms, the view mask "Terrain (0x1) cleared", the watcher re-checking every 10 ms; the EXE set the
  Terrain bit again at 73.78 s and the watcher cleared it with "the previous poll 10.2 ms before"; "first Update 29574 ms
  after the watcher cleared the Terrain bit (without the watcher it stays set that long)". So 3305's "34.6 s -> 10.4 ms"
  holds (29.6 s and 10.2 ms in this run).
- The render=0 control (section 7) was the same: 30 turns, all steps 0, 0 LOUD lines in server.log, vpproxy.log and
  shell_composite.log, stop 0, restored by hash.

## 4. B: lockstep

**4.1 The team's raw records, own analysis [R].** `lk_analyze.py` over 3307's `shell_composite.lockstep.csv` with its
`windows.json` (frames assigned to an arm by `pub_qpc` inside a phase; UpdateViews-published frames only):

| Records | Frames | Exact | Waits (share), max | Turnaround publish -> READY, exact frames: p50 / p95 / p99 / p99.9 / max (ms) | Split p50: publish -> wake / wake -> submit / submit -> READY |
|---|---|---|---|---|---|
| 3307 p1-ring, arm fence | 67,518 | 67,516 = **99.997 %** (2 older, age 1) | 23 (0.034 %), 1.02 ms | **0.767** / 0.930 / 1.044 / 1.242 / 3.52 | 0.015 / 0.749 / 0.0015 |
| 3307 p2-ring, arm fence | 68,101 | 68,100 = **99.9985 %** | 3, 1.02 ms | 0.776 / 0.928 / 1.032 / 1.193 / 2.42 | 0.015 / 0.759 / 0.0015 |
| 3307 p1-ring, arm cpu | 61,302 | 99.989 % | 1,333 (2.17 %), 1.40 ms | 1.568 / 1.817 / 1.955 / 2.200 / 3.44 | 0.015 / 0.624 / 0.941 |
| 3311 p1 whole file (load, D93 shots, 30 turns, 5 reloads) | 151,920 | 147,846 = 97.32 % (3,975 older, 383 of age 39-119) | 3,940 (2.6 %), 1.83 ms | 0.989 / 1.535 / 2.278 / 3.797 / 76.2 | 0.014 / 0.967 / 0.002 |
| 3313 rm1 whole file (load, bake-ahead, script) | 43,471 | 97.07 % | 1,360, 2.27 ms | 0.942 / 1.315 / 1.948 / 3.432 / 60.1 | |

Every figure of 4.9 and 4.11 is reproduced (frame counts within 1-112 of theirs: phase-edge and `noins` counting). No row is
missing in any file (frame numbers without gaps or duplicates; the writer's ring `kLkRing` would drop rows silently on
overflow, `shell_composite.cpp` LkFlush). The 3313 whole-file share (97.07 %) is lower than the script's 99.994 % because it
includes the 16-s bake-ahead after the load; the team's figure is for the script's steps.

**A correction of reading, not of code.** The composite's `late` counter (`lockstep 1s`, "ended more than 0.5 ms after the
deadline", 3311's 263) counts waits that *started* after the deadline too: the EXE reached the insertion more than 3.4 ms
after UpdateViews (p50 3.7 ms), the wait returned at once, and the frame was taken older. A wait that overran
`max(deadline, wait start)` by more than 0.5 ms: **28 of 3,940** in 3311 p1 (max 0.95 ms), 1 of 1,704 and 0 of 1,932 in 3307's
two ring launches. So "never later than deadline + 0.5 ms" holds for the wait loop except under AI-turn load (0.7 %, all
within 1.1 ms). Of 3311 p1's 4,062 older/none frames: 2,528 waited to the deadline (the server late), 1,015 reached the
insertion after the deadline with the frame not ready, 519 were in give-up (`older_why.py`); the server was late in all.

**4.2 Own run [L, functional, no hold].** The product run of section 3, before the turns: two rounds of four 20-s phases
(city idle at the capital (101, 25); city pan: one `UI.LookAt` one plot further every 0.15 s, 10 plots out and back; sparse
idle and pan at the ocean plot (0, 10)); marks by QPC (`live-product/b_windows.json`):

| Phase | Frames | Exact | Waits, max | Turnaround p50 / p99 / p99.9 / max (ms) |
|---|---|---|---|---|
| city idle | 8,796 | 99.76 % (21 older, age 1-2) | 21, 0.98 ms | 0.920 / 1.251 / 1.460 / 3.74 |
| city pan | 8,596 | 99.92 % | 8, 1.23 ms | 0.979 / 1.708 / 1.940 / 3.55 |
| sparse idle | 12,549 | 99.98 % | 42, 1.22 ms | 0.913 / 1.201 / 1.474 / 2.88 |
| sparse pan | 10,570 | 100 % | 114, 0.44 ms | 0.920 / 1.537 / 1.786 / 2.00 |
| **all four** | 40,511 | **99.92 %** (31 older) | 185 (0.46 %), 1.23 ms; 0 overran by 0.5 ms | **0.927** / 1.567 / 1.815 / 3.74 |
| whole run (load, D shots, B, 30 AI turns) | 157,385 | **98.02 %** (3,031 older, age 1-4; 83 none) | 3,340, 2.08 ms; 59 overran (max 1.14 ms) | 0.970 / 1.875 / 3.781 / 99.5 |

Depth written in every frame except the 2,552 frames of 9055's own `depth_write=0` and passthrough shots (frames
4,503-5,196, 7,999-8,987, 12,298-13,166) and 85 at the start. Turnaround is ~0.15 ms longer than 3307's under the quiet lock
(wake -> submit 0.90 against 0.75 ms) and the idle exact share a little lower: no hold, the residency's background bakes
present (3307 ran before the Residency was in server.exe), other work on the machine [I]. **During the 30 AI turns** (the
composite's `lockstep 1s` lines per turn, as 3311 counted): 92,562 of 94,162 = **98.30 %** exact (per turn 97.1-99.5 %)
against 3311's 97.84 % on the same pins and game; re-bakes per turn median 346, max 768 (3311: 331.5, 809); per-turn exact
share against per-turn re-bakes r = -0.53 (3311: -0.67). So the misses during turns follow the re-bakes in both runs.

## 5. C: frame time

**5.1 The team's raw logs, own parsers [R].** Two independent metrics over 3307's launches: (a) the composite's 1-s `stats`
lines wholly inside each phase, frames-weighted `ft_mean`, lines with a frame over 100 ms left out (`ft_stats.py`, the
team's metric re-implemented from the log format); (b) **Present-to-Present intervals** from the per-frame records'
`present_qpc` (`ft_analyze.py`, a metric the team did not use; launches with `lockstep_csv=1` only). Window = mean of its
four phases; 6 rounds; differences paired per round against `d33pt` of the same launch, 95 % t interval.

| Launch: arm - d33pt (ms) | 3307's figure | (a) stats lines, own parser | (b) Present intervals |
|---|---|---|---|
| p1-ring: fence | -0.043 [-0.156, +0.069] | -0.043 [-0.156, +0.069] | -0.035 [-0.133, +0.064] |
| p2-ring: fence | +0.021 [-0.007, +0.049] | +0.021 [-0.007, +0.049] | **+0.026 [+0.005, +0.048]** |
| p1-ring: cpu | +0.215 [+0.097, +0.334] | +0.215 [+0.097, +0.334] | +0.210 [+0.102, +0.318] |
| p1-ring: fence_nols | +0.001 | +0.001 [-0.074, +0.077] | -0.006 [-0.078, +0.067] |
| p1-ss: today's compositor (3207's arm, ss_new) | +0.511 [+0.426, +0.595] | +0.511 [+0.426, +0.596] | no records |
| stock launches, absolute | 5.341 / 5.298 | 5.341 (sd 0.175) / 5.298 (sd 0.056) | no records |

Ring + fence against today's compositor, Welch over the two launches' paired differences: **-0.554 ms [-0.678, -0.430]**
(team -0.554 [-0.676, -0.432]); against stock (absolute, not interleaved): 3.101 - 5.341 = -2.24 ms (launch 1), 3.147 -
5.298 = -2.15 ms (launch 2). All of 4.9's C figures are reproduced from the raw logs. The one difference: by Present
intervals launch 2 shows a small cost of ring + fence over D33 passthrough, +0.026 ms [+0.005, +0.048], where the stats
lines' interval just includes 0; launch 1 shows none. "Costs nothing measurable" is right at the 0.1-ms level, not at 0.03 ms.

**5.2 Own series on the D96 pins and the product path [L, grade A within one launch].** 2026-11-13 18:54:05-19:10:38 EST,
one quiet-lock hold (`quietlock.py run --take --minutes 30`, nobody queued; release summary: 198 samples, total CPU mean
21.8 %, max 39.3 %; busy besides the game and server.exe: claude.exe up to 95 % of one CPU 19:01-19:10, chrome x5 up to 52 %
19:00-19:02, one powershell.exe sample; per-phase total CPU mean 23.5 %, max 35.3 %). One product launch (`live-c`, the
set-up of section 3); arms by composite ini keys, 6 rounds, order rotated: **fence** (ring + fence, lockstep, depth: the
product), **d33pt** (passthrough, `camera=0`: D33 without any server picture), **fence_nols** (`lockstep=0`); each window
four 8-s phases (city idle / pan, sparse idle / pan as in 4.2). Metric (b), Present intervals over 100 ms left out:

| Arm | Frame time, mean of 6 windows (sd) | against d33pt, paired (95 % CI; detectable) |
|---|---|---|
| d33pt | 3.217 ms (0.045) | - |
| fence | 3.248 ms (0.074) | **+0.032 [-0.056, +0.119]** (0.175) |
| fence_nols | 3.189 ms (0.062) | -0.028 [-0.103, +0.048] (0.151) |

Lockstep in the fence windows: 48,534 of 48,546 = 99.975 % exact, 223 waits, turnaround p50 0.946 / p99 1.584 / p99.9
1.819 ms. **Ring + fence with lockstep and depth costs nothing measurable over D33 passthrough on the new pins** (confirmed by
own measurement, detectable ~0.18 ms). Today's compositor and stock were not re-launched (they need 3307's marker route and
a version-3 server; the product path cannot host them): those two comparisons stand on 5.1 [R] only.

**5.3 A finding outside the claims: a long frame every 0.5 s on the product path [L, R].** With the 100-ms filter off, the
same windows give 4.06 / 4.10 / 3.99 ms: **every arm has ~270 Present intervals over 100 ms** (median ~150 ms) in its
6 x 32 s, i.e. ~1.4 per second, about a fifth of the wall time; they come **every 0.50 s** (median spacing between long
frames 0.5034 s, quartile 0.5025 s), in idle phases as in pans, in `d33pt` (no server picture) as in fence. The same 2-Hz
pattern is in the team's product-path records (3311 p1: 523 long frames in 237 s, median spacing 0.437 s; 3313 rm1: 299 in
210 s, 0.504 s) and in this verification's product run (341 in 240 s, 0.504 s), but **not** in 3307's marker-route launches
on the old pin (p2-ring: 7 in 237 s; 3307's idle windows 0-5 per arm). So the composite's 100-ms filter, which every C
figure uses, hides a stall that a player would see as a 2-Hz stutter.

**It is not the renderer** [L, three probe launches 19:14-19:37, product set-up, idle windows only (the capital 4 x 30 s,
then the ocean plot 2 x 30 s, no Lua call inside a window), `run_9055.py --idle-only`, `longframes.py`]:

| Launch | Server | Samplers beside the game | Long frames / s | median length | share of wall time | spacing median (q1) | stats lines with ft_max > 100 ms |
|---|---|---|---|---|---|---|---|
| live-idle-p | render=1 (product) | harness memory sampler 5 s + memsampler 2 s | 1.52 | 143 ms | 21.5 % | 0.5039 s (0.5024) | 164 of 173 |
| live-idle-c | render=0 (`server_config=`) | the same | 1.49 | 151 ms | 21.6 % | 0.5038 s (0.5023) | 161 of 173 |
| live-idle-c2 | render=0 | **none** (`--no-samplers`) | 1.39 | 154 ms | 20.4 % | 0.5036 s (0.5024) | 159 of 173 |

And in the team's raw records, by 10-s bins: 3307 p2-ring (old pin, **marker route**: the composite loaded by the 9040 test
switch, `server_config=`) 0-1 long frames per 10 s in every idle bin; 3306 r2 (old pin, marker route) 0-1 per 10 s before
its turns; **3308 r1 (old pin, the first product-path run, 12:28) ~20 per 10 s from the first in-game second**; 3311, 3313 and
this verification (D96 pins, product path) 10-20 per 10 s. So the stall came with the **product path** (3305's proxy shell
in product form and 3308's product prep), on both pins, with and without the renderer and without any sampler. The period,
500 ms plus ~4 ms, matches a `Sleep(500)` loop [I]. **Cause established by issue 0148 (2026-11-13, design 4.14) [C, L]:
the EXE watches `<user>\MODS` as a whole tree and stalls ~150 ms on a change under it; the composite's control thread
(`Sleep(500)`) appended its per-frame CSV beside the DLL, which on the product path is in `MODS\(1) Community Patch`; fixed by
writing outside MODS.** The candidates as 9055 listed them, read but not tested: the composite's control thread
(`shell_composite.cpp` ControlThread: ini poll, `LsFlush`/`LkFlush`, trigger files, `GetEnvironmentVariableW
("VPSHELL_SECTION")` added by 3308) runs on the marker route too, so a product-path-only cause is likelier (the proxy's
product-form shell code, the per-boot section names, the composite's load by the proxy instead of the helper). Not
measured: stock on the product path (`ServerRenderer = 0` loads no composite, so there is no frame record), and a
product-path launch with `lockstep_csv=0` (the stats lines above say the same with or without the CSV analysis).

## 6. D: depth against stock at the same camera, and bug 0134 [L shots, C analysis]

**Arms.** Joined = the product run of section 3 (D shots right after the load, before B and the turns): per camera
`depth_write=1`, `depth_write=0`, passthrough (the EXE alone over the clear grey (84, 84, 84)) and `depth_write=1` again.
Stock = `live-stock` (18:49-18:53, 3311's stock set-up: the same proxy route, pins and save copy, `ServerRenderer = 0`, no
composite, darkred.py `--arm stock`), two shots per camera. Cameras: the load view, `UI.LookAt` the capital Thebes (101, 25),
`UI.LookAt` Akhetaten (94, 16), default zoom. Camera bytes (gt_reader): the load view **identical** between the launches;
the two LookAt cameras equal in rotation and projection and **0.037 world units apart** in translation (~0.2 px at
0.15-0.24 world units per pixel; the reader's second, wrapped view differs by one map width and is not drawn). All 18 shots
are in the Visual Checks feed (issue 9055), the comparison figures below too (`vc-20261113-185513..185516`).

One joined shot is unusable: `akhetaten_dw0.png` is the clear grey over the whole window (no HUD, no model, no terrain), a
PrintWindow capture of a frame state between the clear and the EXE's lists [I, the same class as 3310's "server picture
alone" shot]; the composite log has nothing at that moment (18:06:0x). For Akhetaten the passthrough shot stands in as the
no-depth reference (the model pixels are by definition those of passthrough).

**Method** (`d_analyze.py`; simpler than 3310's, no erosion and no 3-shot stability test): model pixels = passthrough not
clear grey and equal (<= 24) to `depth_write=0`; static (buildings, D33's dark red) by colour; hidden = both `depth_write=1`
shots differ from `depth_write=0` by > 24; for hidden static pixels the share where stock is within 24 (40) of the joined
pixel, divided by the same share on plain ground = the estimated share stock hides too.

| Camera | Static px | hidden by the server | est. stock hides too (24 / 40) | => hidden static px visible in stock | Other model px hidden |
|---|---|---|---|---|---|
| load view | 150,903 | 18,684 (**12.4 %**) | 0.77 / 0.77 | **~23 %** | 5.0 % |
| Thebes (101, 25) | 559,406 | 59,790 (**10.7 %**) | 0.35 / 0.41 | **~60 %** | 7.0 % |
| Akhetaten (94, 16) | 193,855 | 24,642 (**12.7 %**) | 0.89 / 0.96 | **~4-11 %** | 22.7 % (coast: ships over the two water surfaces) |

- **The depth works**: the server's surface cuts 11-13 % of the static models' pixels at all three cameras (3310: 9.0 % at the
  load view with erosion and the stability test, which remove edge pixels; 3306: 10.7-15.5 %). The noise floor: the two
  joined `depth_write=1` shots differ by > 24 in 1.0-1.4 % of the picture (animated units), the two stock shots in 1.5-12 %
  (stock animates water, trees and units).
- **0134 confirmed, and it is local**: the share of server-hidden building pixels that stock shows is ~60 % at the capital,
  ~23 % at the load view and ~4-11 % at Akhetaten (a city on a hill). 3310's 30-48 % is an average over views; the effect is
  strongest in the big flat capital. Looked at (`D:/c5-64bit-work/9055/dana/capital_city_quad.png`, Visual Checks
  `vc-20261113-185516`, result fail, issue 0134): the hidden building pixels at Thebes are **strips along the lower
  (near-camera) edges of the buildings and the long run of the city wall**, a few to ~15 px high, while the bodies stay
  visible; stock shows those bases and the wall in full. That is the server's surface standing slightly **higher** than
  the EXE's under the city footprint, not a misregistration (cameras within 0.2 px, and a shift would cut on all sides) [I].
  It fits 3310's hypothesis (a), the EXE flattening or lowering the terrain under city footprints after or beside the
  height samples the tap copies; it does not fit (b) LOD alone, which would not prefer cities. Not separated here.
  *Settled 2026-11-13 by issue 0134 (renderer-in-server-design.md 4.16): the server stood higher everywhere, not by a city
  stamp: it drew HeightAtGrid's u16 / 255 * 0.2 where the EXE draws the high byte * 0.2 of its 8-bit height page.*
- COMMON 25 numbers, the whole picture joined `depth_write=1` against stock (HUD out; they measure D33's colouring and the
  systems D33 leaves out, not depth): median / p95 / max of the largest channel 47 / 138 / 255 (load), 46 / 150 / 255
  (Thebes), 37 / 127 / 251 (Akhetaten); stock against its own second shot 5 / 42, 0 / 7, 5 / 34. Difference images
  `D:/c5-64bit-work/9055/dana*/<camera>_absdiff_joined_stock_x2.png`.

## 7. E: memory per turn, and bug 0137's attribution

**7.1 Own runs [L].** perf/memsampler (`--server`, started by 3311's loop), private bytes at each autosave, least-squares
slope over turns 350-379 with a 95 % t interval (`run_summary.py`, own code; it reproduces 3311's +3.11 [2.74, 3.48] and
+1.20 [0.97, 1.44] from 3311's own `per-turn.csv` to the second decimal):

| | server.exe private, turn 350 -> 379 | server MB/turn, all 30 | second half (365-379) | EXE MB/turn, all 30 |
|---|---|---|---|---|
| product (render=1), 18:02-18:24 | 2,000.7 -> 2,100.3 MB | **+3.15 [2.77, 3.52]** | +1.65 [0.94, 2.35] | +1.90 [1.52, 2.29] |
| control (render=0, `server_config=` with the product run's lines), 18:25-18:43 | 246.9 -> 288.3 MB | **+1.33 [1.09, 1.58]** | +0.59 [-0.01, 1.18] | +1.33 [1.10, 1.56] |
| 3311 p1 / c1 (team, same pins) | | +3.11 / +1.20 | +1.65 / +0.58 | +1.42 / +1.53 |

So **E as measured by the team is confirmed by own measurement**: the server grows ~1.8 MB per turn more with the renderer
over the 30 turns and ~1.1 MB per turn more in the second half. The EXE grew more in this product run than in its control
(+1.90 against +1.33, intervals just apart) where 3311 saw +1.42 against +1.53: one run per arm, not a finding.

**7.2 0137's attribution: supported, and the growth is bounded [L, R].** Two independent checks on both product runs
(this one and 3311's p1, the same game on the same pins: the residency's arrays went 362.6 -> 405.2 MB in both, 8,704 ->
9,728 slices, four 256-slice chunks of 10.65 MB):
- *Per-turn regression* (`e_turns.py`, 29 turn intervals): server private growth per turn against the turn's re-bakes and
  the turn's array growth. This run: **about 1 MB of private per MB of arrays** (c = 1.056 [0.689, 1.423]); re-bakes
  themselves add nothing beyond that (b = -0.004 [-0.013, +0.005] MB per re-bake); intercept 3.40 [-0.45, 7.25]. 3311 p1:
  c = 0.682 [0.200, 1.164], b = -0.001 [-0.012, +0.010]. Re-bakes alone (without the array term): no positive slope in
  either run.
- *10-s steps* (`e_attrib.py`, render.log's `mem periodic` lines joined with the residency's counters, turns only): this
  run, the 4 ten-second intervals that hold an array step carry **+48.6 MB** of private for +42.6 MB of arrays; the other 78
  intervals +52.2 MB in all (0.67 MB each). 3311 p1: +30.7 MB in the 4 step intervals, +97.9 MB in the other 82.
- Arithmetic for this run: the product grew 99.6 MB over the 29 intervals, the control 41.5 MB; the excess 58 MB, of which
  the array steps account for ~45 MB (42.6 x 1.06). **About 75-85 % of the excess is the residency's array chunks**; the
  remaining ~13 MB (0.45 MB per turn) is inside the noise of one run per arm.
- **Why the arrays grow** [C + L]: a tile holds one slice (t0) or two (t0 and t8, `residency.cpp` install: `bytes = one *
  (slot[1] >= 0 ? 2 : 1)`); t8 is made when the tile's layer list has a layer whose set carries a spec map
  (`pipeline.cpp` 530-532, `needSecond`). With the resident count fixed at 4,922, **slices per resident tile rose from 1.744
  to 1.934 over the 30 turns** (this run; 3311: 1.720 -> 1.934): re-baked tiles gained a second slice. *(Corrected
  2026-11-13 by issue 0137, `renderer-in-server-design.md` 4.15 [C]: the t8 comes from `BakeRequest::areaChanged`, which the
  residency sets on every re-bake (the EXE's tile+0x300 rule), not from a changed layer list. Was: "[I: their layer lists
  gained a spec-mapped layer, ...]". 0137 now holds the t8 slice from the first bake.)* The ceiling is 2.0 slices
  per tile, i.e. **at most ~330 more slices (two chunks, <= 21 MB) on this map**, so this part of the growth stops by
  itself; a re-bake that keeps its layer set does not grow (b ~ 0 above). 0137's DoD ("a test that re-bakes the same tiles
  many times without growth") would pass on this evidence; the open question is only whether the second slice is wanted
  for those tiles, which is a correctness question of the bake, not a leak [I].
- *Not about video memory*: the arrays are `D3D11_USAGE_DEFAULT` texture arrays; on this machine their
  allocation shows up ~1:1 in the process's private bytes (the driver's backing) while render.log's video_local fell from
  1,415 MB after the load to 504-613 MB during the turns (the driver demotes idle textures, 3408).

## 8. The research against the code [C, main 478519e5]

Checked against the code: section 1 (thread priority `THREAD_PRIORITY_ABOVE_NORMAL` and `SrvPowerOptOut` at the thread's
start, `RunSeh` around `RunCpp`, the 100-ms event timeout and the 0.5-ms legacy timer, rebuild back-off doubling to 30 s
inside a 5-minute window, `kMaxCreateAttempts`), 1.1 (delay-load names bound with the table's spelling), 2.1 (SRW lock,
cap 65,536 with `kOverflow`, `PlotRecord` 20 bytes and `Event` 32 bytes by static_assert, every `On*` returning first while
the feed is off), 3.1 / `vp_frame.h` (bound 3,400 us, give-up 8, the bounded wait never waiting once the deadline has
passed), 4.8 / 4.12 (idle budget 0 while a camera is pending, the 2-ms wake while the source is busy, the pacing fallback
after more than 3 Presents without a publish), 4.13 (the shipped `shell_composite.ini` has `tap=1`, `handover=ring`,
`lockstep=1`, `depth_write=1`, `ring_sync=fence`, `ring_slots=4`). They match.

Stale, low (documentation only):
- Section 5's table still gives the wave-1 defaults: `render_ring_slots` 3 (code: `ringSlots = 4`,
  `srv_render_host.h`); the composite rows `lockstep` 0, `handover` v3, `depth_write` 0 are the code defaults, but the
  shipped ini (4.12, 4.13) has 1 / ring / 1 and the proxy writes `render_handover=ring` (4.8): the table does not say which
  default it means.
- Section 2 (design) gives the plot record as 16 bytes; 2.1 corrects it to 20 (fine, 2.1 says so); D99 and the epic's Result
  give "lockstep 23/23" (now 28 tests).
- 4.11's "server.exe private after load 1,956.4 MB" is `mem_after_load` 20 s after the load; its memsampler row for turn 350
  says 1,996.4 (the slope uses that row). Not a contradiction, two moments.
- 4.11 B-lite's "263 ended more than 0.5 ms after their deadline": see 4.1 above (waits that began after the deadline).

Issue 0150 (2026-11-13) fixed the first, the "23/23" and the last: design section 5 says code and product defaults, the
epic's Result (3300) says what 23/23 was; 4.11 and 4.8 carry the
correction of the `late` count; the composite now logs `late_start` and `overrun` instead of `late`.

## 9. Defects and findings, with severity

| # | What | Severity | Evidence | Owner (proposed) |
|---|---|---|---|---|
| V1 | **A ~150-ms long frame every 0.50 s on the product path** (~1.4 per second, ~21 % of wall time in idle), with render=1 and render=0 and without samplers; absent on the marker route; present since 3308's first product-path run on the old pin. Every C figure (team's and 9055's) filters it out (frames over 100 ms dropped) | **high** (a 2-Hz stutter a player sees; and frame-rate figures that hide it) | 5.3 | bug 0148: cause and fix in `renderer-in-server-design.md` 4.14 (the EXE's watch on the MODS tree; the composite's CSV and the run folder moved out) |
| V2 | The render suite's ASan environment is undocumented: without the runtime's folder on PATH every exe-starting test fails 0xC0000135, and an unquoted `log_path=D:/...` breaks ASAN_OPTIONS | low (tooling; cost 9055 ~15 min) | 1, 12 | fixed by 0149: `build_render_tests.py --asan` copies the runtime beside the binaries; the conftest writes a quoted log_path |
| V3 | `test_pacing_falls_back_when_publishing_stops_and_returns_when_it_resumes` fails 1 in 7 runs (here under ASan): the "never written the publish block" note is not logged exactly once. **Cause (corrected 2026-11-13 by bug 0149, from the failing run's log `D:/c5-64bit-work/9055/suite-render-asan3.log`) [C]:** the note was counted **0** times, not twice: `waits_legacy 1340 = waits_fallback 1340` and `waits_idle 0`, so the render thread never took a legacy wait before the first publish. `Loop` reaches its first pacing decision only after `scene.poll()` turned OK and the first `DrawFrame` returned; the test published as soon as it saw `-> OK` in the log (50-ms poll), and when the first draw took longer (ASan) the publish block's seq was already non-zero there, so event pacing began with no legacy phase. A test race, not a renderer defect; the test now waits for the note before publishing. (Replaced: "In `Loop` the idle-pacing branch resets `legacyNoted`, so a scene that drops out of state OK and back logs the note twice [I]": that reset exists and is meant (each switch between idle and legacy pacing is noted) but did not cause this failure.) | low (test flake) | 1, 12 | fixed by 0149 (test) |
| V4 | The composite's `late` counter (`lockstep 1s` "late") counts waits that began after the deadline; true overstays are 28 of 3,940 (3311 p1), not 263 | low (a misleading metric) | 4.1 | **fixed by issue 0150** (2026-11-13): two counters `late_start` and `overrun` (`wait1 - max(deadline, wait0)` > 0.5 ms), `shell/composite/lk_late.h`, design 3.1 Records |
| V5 | 0134 is concentrated at the capital: ~60 % of the hidden building pixels visible in stock at Thebes, ~23 % at the load view, ~4-11 % at Akhetaten; the cut lies along the buildings' lower edges and the city wall | medium (as 0134) | 6 | 0134 (adds the camera list and the picture) |
| V6 | 0137's growth is the arrays (~1 MB private per MB of array; re-bakes without a layer change cost nothing) and is bounded by 2 slices per tile (1.934 of 2.0 after 30 turns; <= ~21 MB more on this map) | informs 0137 (high -> could be lowered after one longer run shows the plateau) | 7.2 | 0137 |
| V7 | The design's section 5 table (`render_ring_slots` 3, composite defaults) and D99's "lockstep 23/23" are stale | low (docs) | 8 | **fixed by issue 0150** (2026-11-13): design section 5 corrected (code and product defaults); the epic's "lockstep 23/23" was 3304's ASan run, the suite has 29 tests after 0150 |

No defect was found in the render thread's isolation from the DLL (2), in A (3), or in the ring's lockstep behaviour (4).

## 10. Not verified

- **Today's compositor and stock frame time by own launch** (5.2): the product path cannot host 3207's version-3 arm or a
  composite-measured stock arm; these stand on the team's raw logs reanalysed (5.1) only.
- **The F determinism proof** (stats.db identical with render 0 and 1, Tiny 50 and huge 350-355): not rerun.
- **Reloads (0121)**: no reload in 9055's runs.
- **B under the quiet lock on the D96 pins with the B phases**: 4.2 ran without a hold; the hold of 5.2 covered the C
  windows (fence arm 99.975 % exact there).
- **Gray frames at radius -1 / 5 / 0 (3313)** and the D93 highlight checks (3311): not repeated.
- **The cause of 0134.** (The cause of V1 was found later by 0148: design 4.14.)

## 11. Files

Repository: `shell/evidence/9055/run_9055.py` (the driver). On D: (D83, not committed) `D:/c5-64bit-work/9055/`:
`scripts/` (every analysis named above, `vthash.py`, `record_vc.sh`, `amend_vc.sh`), `build_test.sh`, `suites2.sh`,
`asan2.sh`, `flaky.sh`, `live.sh`, `chain*.sh`; suite logs `suite-*.log`, `flaky-*.log`, `canary/`; live runs `live-product`,
`live-control`, `live-stock`, `live-c`, `live-idle-p`, `live-idle-c`, `live-idle-c2` (each `out/` with run.json, logs,
per-frame CSV, memsampler, shots, and the prep / restore records); analyses `bana/` (B, C), `dana/`, `dana-akh/` (D).
Visual Checks: issue 9055 (18 D shots, 7 end shots described, 3 comparison figures) and issue 0134 (the Thebes crop).

## 12. Bug 0149: the ASan environment and the pacing flake, fixed (2026-11-13)

Branch `task-0149-render-asan-env-pacing` (account B).
- **Runtime** [C]: `server/tests/render/build_render_tests.py --asan` copies `clang_rt.asan_dynamic-x86_64.dll` and its
  `.pdb` from the folder of the cl.exe that built the binaries (`VC/Tools/MSVC/14.43.34808/bin/HostX64/x64` here) into
  `DIR/bin-asan`; Windows finds a DLL beside the exe first, so pytest needs no PATH change. A missing runtime fails the build.
- **ASAN_OPTIONS** [C]: `server/tests/render/conftest.py` `pytest_configure`, for an ASan build (folder name `*-asan` or
  the runtime beside the binaries): stops the session with a UsageError when the runtime is neither beside the binaries
  nor on PATH; takes `C5_ASAN_LOG_DIR` (default `<build>/../asan-logs`, i.e. `DIR/asan-logs`) and sets it; writes
  `log_path="<dir>/asan"` (quoted) into `ASAN_OPTIONS`, replacing any log_path there (quoted, or an unquoted drive path
  `D:/...` up to the next colon) and keeping the other options; the session header prints the line.
- **Pacing test** [C]: the cause is in section 9 V3 (corrected); the test waits for the legacy note after `-> OK`
  before its first publish. Renderer code unchanged.
- **A second ASan start-up race, found by 0149's first run** [L, 2026-11-13 21:01-21:07]: with the runtime fix the ASan
  suite gave 76 passed, 1 failed, 0 ASan reports, no environment failure; the failure was
  `test_frames_per_camera_and_depth_stamp` (the first test of the session): `fake_consumer.exe` (ASan build) opened the
  texture only after the test's ~1 s of cameras (2..40 at 40 Hz) were written and took 1 frame of 8 (`camFrameStamp` 40).
  Fix [C]: the consumer flushes its `opened` line, and the two tests that start it (`test_frames_per_camera_and_depth_stamp`,
  `test_resize_request`) wait for that line (`consumer_opened`) before they write cameras.
  First attempt failed [L, 21:19-21:24, clone bbbe9e18]: both tests raised PermissionError reading the consumer's JSONL,
  because `_wfopen_s` opens the file without sharing; the consumer now opens it with `_wfsopen(..., _SH_DENYWR)` (readers
  allowed) and the wait ignores an OSError.
- **Acceptance run**: script `D:/c5-64bit-work/0149/run_0149.sh` (fresh `git clone -b task-0149-render-asan-env-pacing`
  into `D:/c5-64bit-work/0149/clone`; builds `rt/bin` and `rt/bin-asan`; the ASan suite under `memcap.py --mb 6144` with
  ASAN_OPTIONS and C5_ASAN_LOG_DIR unset and no MSVC folder on PATH; the pacing test 30 times plain and 10 times ASan;
  every step through `quietlock.py run`). Results [L, 2026-11-13, workstation, cl 14.43.34808]:

  | Run | Clone | What | Result |
  |---|---|---|---|
  | 1 (21:00-21:17) | d78066f4 | ASan suite, no PATH / ASAN_OPTIONS | 76 passed, 1 failed (the consumer race above), 0 reports, **0 environment failures** |
  | 1 | d78066f4 | pacing test, plain build | **30/30 passed** in a row (14.4-14.9 s each) |
  | 1 | d78066f4 | pacing test, ASan build | **10/10 passed** |
  | 2 (21:17-21:28) | bbbe9e18 | ASan and plain suites | 75 passed, 2 failed each (the exclusive-open trap above) |
  | 3 (21:28-21:40) | 74a99278 | ASan suite, no PATH / ASAN_OPTIONS | **77 passed** in 345 s, 0 reports (`r3/rt/asan-logs` empty), peak commit 1,195 MB |
  | 3 | 74a99278 | plain suite | **77 passed** in 253 s |

  Logs: `D:/c5-64bit-work/0149/` (run 1: `run.out`, `suite-render-asan.log`, `flaky-*.log`), `r2/`, `r3/` (`run.out`,
  `suite-render-asan.log`, `suite-render-plain.log`). The pacing series ran on run 1's build; nothing it uses changed
  after (later commits touch only `fake_consumer.cpp` and the two consumer tests).

## 13. Ticket 0333: the pacing counts and the watcher bound, counted instead of timed (2026-11-15)

Branch `task-0333-load-tolerant-timing-tests` (account B). Evidence: `server/evidence/0333/`.

**The fault [L].** Four tests asserted how many things happened in a fixed wall-clock time, and the test host
(`render_host_test.exe --seconds S`) counts that time from `HostStart`, so under MSVC ASan the device creation and the
first iterations eat into it, and other teams' load slows the rest. Measured with main's versions of the tests on the
same ASan build (control arm of the series below, 2026-11-15 11:20-11:32 EST, workstation, CPU 43-71 % busy):
`test_idle_pacing_off_keeps_the_old_poll` counted **22-603 legacy waits** in its 2 s run (median about 370; wants
> 500; 17 of 20 failed), `test_idle_pacing_before_a_camera_then_legacy_poll` counted **0-24 idle waits** in its 2 s idle
phase (wants >= 20; 19 of 20 failed), `test_pacing_stays_on_events_when_presents_stop_too` failed 6 of 20 (fewer than
10 wait timeouts by the end of a 4 s run). `test_tap_hash_mismatch_refused` failed once in a load suite (its 2 s run
ended before the tap was read: ack `(0, 0)`). Earlier sightings: 3903 (364, 470, 491 legacy waits), 3911 (idle 10, 19),
0166's 4112/4113 (251-372 legacy waits, decals on and off), 0188 (462). Plain builds count 1,271-1,369 legacy waits in
2 s (0166 15.5): the fault is the fixed time, not the renderer.

**The fix: counted, not timed [C].** The render loop ends every iteration in exactly one wait and counts it by kind
(`waits_event`, `waits_legacy`, `waits_idle`, `wait_timeouts`, `waits_fallback`; cumulative in each `host 1s:` line and in
the summary), so the tests now check the waits themselves and let each phase last until its count is reached:
- `render_host_test.exe --stop-event NAME` (test program only): the host stops when the test sets that named event;
  `--seconds` stays the upper bound. An event that does not exist refuses the start (exit 2, `--stop-event NAME: cannot
  open (error 2)`; negative test `test_stop_event_that_does_not_exist_is_refused`). `renderfx.Rig.stop_event()`,
  `Rig.wait_second_line(pred)` and `renderfx.second_lines()` are the helpers.
- `test_idle_pacing_off_keeps_the_old_poll`: runs until a per-second line counts more than 500 legacy waits (ends early on
  any idle, event or fallback wait); then no idle, event or fallback wait in the summary or any per-second line, and the
  legacy count rises between every two lines (an iteration lies between two lines and ends in a wait).
- `test_idle_pacing_before_a_camera_then_legacy_poll`: the idle phase lasts until a line counts 20 idle waits (no legacy
  wait by then), each camera 2..60 is written after the previous one was drawn (server block `lastDrawnFrameId`), the
  legacy phase lasts until 500 legacy waits; after the first line with a legacy wait the idle count never moves; `drawn
  >= 59`; `idle <= 400` kept (load only lowers it; a 0.5 ms idle wait would count about 1,700 a second in a plain build).
- `test_pacing_stays_on_events_when_presents_stop_too`: the silence lasts until two per-second lines written in it are
  10 wait timeouts apart; between them the legacy count does not move and no fallback wait is counted.
- `test_tap_hash_mismatch_refused`: runs until the refusal is logged and two more per-second lines follow.
- **The periods without a clock [C], the one server change:** the 0.5 ms legacy poll is now the constant
  `kLegacyPollHns = 5000` (100 ns units) in `srv_render_host.cpp`, used by the timer and printed by the pacing note
  ("camera block polled every 0.5 ms"), so a test reads the period the loop uses; the idle note already printed
  `render_idle_poll_ms`. The tests also require that the no-timer fallback ("no high-resolution waitable timer", 1 ms
  sleeps) was not taken. Behaviour unchanged.
- **The proxy's watcher (`proxy/tools/rt_tests/shell_unit.cpp`, `TestWatcher`) [C]:** the check was "worst set-back
  cleared within 15 ms" (0344: 25.9 / 19.2 ms with 9 games running). The rule it stands for is "cleared at the first
  watcher poll that begins after the set-back"; a poll counts itself (`g_watchPolls`) before it reads the mask, one poll at
  a time under the lock, so the test reads the count after the set-back (p0) and fails if it sees the count at p0 + 2 (a
  whole poll began after the set-back and ended) with the bits still set. The times are printed as information. The
  existing coarse check (at least 3 polls in 100 ms) stays.

**Acceptance series [L, 2026-11-15, `server/evidence/0333/`].** ASan build of `build_render_tests.py --asan` (cl
14.43.34808); per round the four changed tests (arm new) and main's versions of the same four (arm old, temporary copies,
not committed), alternating, while two looping ASan runs of `test_render_host.py` (47 tests, about 136 s each) and the
other teams' work ran (3 game instances, an x64 server, a parity host, an LTO link; `scripts/stress_ab.sh`):

| Series (EST) | Tests at | New (changed tests) | Old (main's tests, control) | shell_unit plain + ASan | Machine (5 s samples) |
|---|---|---|---|---|---|
| 1, 11:12-11:17 | e0d5fa63 | 3 pacing tests: **20 of 20** | - | - | CPU 45-65 % busy |
| 2, 11:20-11:32 | 8f64cfe8 | **20 of 20** rounds | 0 of 20 (idle-off 17, before-camera 19, presents-stop 6 failures) | - | CPU 43-71 %, median 53 % |
| 3, 11:43-11:57 | 45ac29c2 (final tests) | **20 of 20** rounds | 0 of 20 (idle-off 13, before-camera 18, presents-stop 1) | **20 of 20** | CPU 35-64 %, median 50 %; free commit >= 50.4 GB |

The new tests' `--stop-event` came 2.1-4.1 s after the start (median 3.6 s). Main's idle-off test counted 16-745 legacy
waits in its 2 s (series 3), its before-a-camera test 1-24 idle waits in the 2 s idle phase. The watcher's set-backs
took 4.7-9.9 ms (worst per run, series 3, information). The load suites: 14 of 14 passed in series 3; in series 1 one
failed (`test_tap_hash_mismatch_refused`, then changed here).

**Negative controls [L, 2026-11-15 11:35-11:40].** Each mutation built (plain), the sources restored by SHA-256 at once:
M1 legacy poll 20 ms (`kLegacyPollHns = 200000`): the idle-off and before-a-camera tests fail on the note's period; M3
event pacing drops to the poll after 5 timeouts without counting a fallback: the presents-stop test fails (legacy waits
moved in the silence); M2 `render_idle_poll_ms=0` idle-paces one iteration in 64: the idle-off test fails (idle waits
counted); M4 the watcher applies the mask only at every third poll: `shell_unit` fails 3 of 5 set-backs (`clear &&
!late`), where its times (mean 12.0, worst 15.4 ms) would have passed the old bound on most runs.

**Still timed (not changed here) [I].** `test_pacing_falls_back_when_publishing_stops_and_returns_when_it_resumes`
(`waits_fallback > 200`, draw rates within 0.75 of the cameras written) and `test_frames_per_camera_and_depth_stamp`
(0188: drawn 17 < 25 in 6 s) are of the same kind and can fail the same way; they did not fail in the 2 x 9 load-suite
runs of the series. `water/test_water_host.py` under ASan [L, 2026-11-15]: 2 of 7 failed in the branch's ASan suite run,
3 of 7 run alone, and **4 of 7 on main's sources and ASan build** (control, 12:38-12:42): the fog waits 10 s for the
feed's plot table after the first frame and draws no frame until then ("fog of war: no plot table from the feed 10000 ms
after the first frame of the map ... no frame is drawn", D118 / D125), and the test hosts' fixed 20-25 s runs leave too
little after the ASan start-up; not changed by 0333 (3911 saw two of them). (2026-11-15: made event-driven by ticket
0369, section 14.)

**Whole tree [L, 2026-11-15 12:22-12:35].** `server/tests/render` plain with every sub-suite built
(`scripts/whole_tree.sh`): 210 passed, 0 failed, 0 skipped (the three scene sub-suites need `C5R_EXE` in the
environment, else they fail at once with "C5R_EXE must name CivilizationV_DX11.exe"). ASan: the host suites pass, 0
reports.

## 14. Ticket 0369: the water host tests, event-driven instead of a fixed run (2026-11-15)

Branch `task-0369-water-host-load-tolerant` (account B). Evidence: `server/evidence/0369/`.

**The fault [C, L].** `server/tests/render/water/test_water_host.py` ran `render_host_test.exe` for a fixed 20 s
(`run_frames`) or 25 s (the module test) with the installed game (`C5_GAME_DIR`). With the installed game the fog of war
(`render_fow=1`, the default) starts its modules, and the test host feeds no plot table, so `FowScene::admit`
(`srv_fow_scene.cpp`) holds every frame for its install wait (`SceneOptions::fowWaitMs`, 10,000 ms after the first frame
of the map), then fails; without a fatal hook (`HostConfig::fatal`, test hosts only) `FowFatalStep` makes it draw on
without the fog. So the first frame the water scene decides comes 10 s plus the start-up after the start, and under MSVC
ASan the start-up (device, the water module, the fog modules) took the rest: in main's placeholder test
(2026-11-15 13:27, ASan) the fog's note came 19.8 s after the host's first log line and the host stopped at `--seconds`
0.2 s later with one frame drawn. The water scene's per-second `w_why` starts as `NO_TAP` (`srv_water_scene.h`) and
changes only in `WaterScene::decide`, so the tests that asserted on `w_why=OFF` / `PLACEHOLDER`, on the module's draws or
on the NO_TAP note failed whenever the run ended first; the stale test's `w_why=NO_TAP` check was met before any frame.

**The fix: event-driven [C].** No server change. Every host run in the file is started with `--stop-event` (0333's
`Rig.stop_event`) and `--seconds 600` as an upper bound only; the test waits (`wait_until`, at most 240 s per event,
failing at once if the host ended) for the line its assertions are about, then sets the event, and `stop` checks that
the event, not the bound, ended the run (`result: --stop-event set after`):
- `test_render_water_0_draws_no_water`: until a `water 1s:` line with `w_why=OFF` (a frame was decided);
- `test_render_water_placeholder`: until `w_why=PLACEHOLDER`;
- `test_stale_water_image_keeps_the_plane`: until the drawn frame's note `water scene: load seq 1: placeholder plane
  (NO_TAP`; it now also checks that note says `tap: accepted 0, refused 1, last result -3` (stronger than before);
- `test_module_draws_after_the_tap_and_the_plane_before`: each step waits for its note (the height tap, the NO_TAP
  plane, the water tap read), and after cameras 30-79 the run lasts until a `water 1s:` line with `w_draws > 0`;
- `test_water_keys_refused`: unchanged (the config is refused before any frame, exit 2).
The fog's 10 s wait is kept (the tests run the default configuration); it only makes each run about 10 s longer.

**Acceptance series [L, 2026-11-15 13:33-14:29 EST, `results/series-s1*`].** ASan build of `build_render_tests.py
--asan` (cl 14.43.34808) at `C:/c5-64bit-work/0369/rt/bin-asan`; 20 rounds of the changed file (arm new), the first 10
interleaved with main's version (arm old, a temporary uncommitted copy), on the workstation with the other teams' work
(3-8 registered heavy jobs per round: game instances c5h-a/b/c/e/f/h, two LTO relinks of 0358, shell tap suites of 0366
and 4304); CPU 31-100 % busy, median 55 % (673 samples of 5 s).

| Arm | Runs passed | Failures per test |
|---|---|---|
| new (changed file) | **20 of 20** (7 of 7 each) | none; 0 ASan reports |
| old (main's file, control) | 0 of 10 | module test 10, placeholder 7, render_water=0 4, stale 2 |

Time from the start to the stop event (arm new, n 20 each): render_water=0 17.7-21.4 s (median 19.3), placeholder
17.7-21.3 s (19.6), stale 23.0-28.7 s (26.2), module test 26.0-32.1 s (28.0): the old 20 s and 25 s bounds lay inside
these ranges, which is why main's version failed most rounds.

**Negative controls [L, 2026-11-15 14:30-14:42, `results/negative.txt`, `scripts/mutate.py`].** Three mutations of the
server's water code in one plain build, the sources restored by SHA-256 right after the build: N1 the module never
draws once the image is there (`decide` gives NO_FIELDS), N2 an image of another load is not refused stale (`CheckBlock`),
N3 `render_water=0` decides PLACEHOLDER. Result: the module test, the stale test and the render_water=0 test fail, each on
its own wait ("not logged (240 s passed)"); the placeholder test and the three key refusals pass. A failing run costs
the 240 s wait per test.

**Whole tree [L, 2026-11-15 14:48-15:15, `results/tree-*.txt`].** `server/tests/render` with every sub-suite built and
`C5R_EXE` set (`scripts/tree.sh`): plain 210 passed, 0 failed, 0 skipped (715 s); with the ASan render host build 210
passed, 0 skipped, 0 ASan reports (880 s; the sub-suites' own programs were their plain builds).
