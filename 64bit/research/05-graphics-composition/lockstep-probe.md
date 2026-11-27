# Lockstep probe: from UpdateViews to the scene-clear list (issue 9039, 2026-11-12/13)

Decision D81 point 2 (the Architect's `research/07-architecture-decisions/architect-2026-11-12-renderer-scope.md`
section 3.2) makes lockstep between the EXE's frame and the server's picture permanent: the composite publishes the
camera at the earliest main-thread point after the EXE's `GameViewState::UpdateViews` (0x51DF70), the server draws
that frame, and the insertion after the scene-clear list takes the picture with the same frame number. This file
measures the one number that decides whether that costs waiting: the time per frame from UpdateViews to the
execution of the scene-clear list, idle and while panning, on the huge save, against the server's turnaround.

**Result (2026-11-13, huge save t350, D33 + server terrain, old pins; sections 2-4) [L]:**

| At the city view (plot 100, 30; ~69 lists per frame), run d33_r3 | p1 | p5 | p50 | p95 | p99 |
|---|---|---|---|---|---|
| UpdateViews -> scene-clear list executed, idle (composite + server) | 2.08 | 2.21 | 3.21 | 3.74 | 4.08 |
| the same, panning (~400 world units/s) | 1.79 | 1.99 | 3.01 | 3.80 | 4.19 |
| server turnaround: camera published -> its frame released, GPU complete | 1.39 | 1.52 | 2.09 | 2.83 | 3.35 |

(ms; the turnaround is from the windows after 01:29:42, when the server polled at full rate, section 3.2.)

- The floor of UpdateViews -> scene clear is **about 1.0 ms** in the emptiest view (open ocean, 5 lists) and about
  2 ms in the city view; the median is 1.4-3.4 ms and grows with what is on screen (section 2).
- The server's whole turnaround today is **2.1 ms median** (CPU 0.46 ms, GPU 0.78 ms per draw, the rest polling,
  GPU sync and the hand-over), not the 1-1.5 ms of the brief's premise; the GPU draw alone is the 0.775 ms of epic
  3200.
- **Lockstep therefore waits in a minority of frames, briefly**: with this server, at the city view, 8.5 % (idle) to
  11.3 % (panning) of frames would wait, typically 0.23-0.25 ms (median of the waits; p95 0.9 ms panning), 0.04-0.1 ms
  per frame on average [I: paired per frame from measured values, section 4]. In sparse views (UpdateViews -> scene
  clear 1.0-1.8 ms) most frames would wait, up to about 1 ms, unless the turnaround shrinks.
- What it needs: the turnaround made shorter and steadier rather than a longer EXE frame: wake the server by an event
  instead of 0.5 ms polling, keep the server out of Windows' timer throttling (section 3.2: for 5 minutes it polled at
  64 Hz, 16 ms turnaround), and replace the keyed mutex + gpu-sync hand-over (the Architect's 3.2 already names it).

## 1. Method

### 1.1 The UpdateViews timestamp: a hook in d3dx9_42.dll, filtered to UpdateViews' own call [C]

No existing hook of the composite DLL fires at UpdateViews (they are on dxgi/d3d11: Present, the contexts'
Clear/OMSet/Finish/Execute). The probe adds one, opt-in (`lockstep_probe=1` in `shell_composite.ini`, default 0):

- UpdateViews' mode-1 path computes `g_InvView[i] = inverse(g_View[i])` in a loop at 0x51E1B0 (dumpbin of the DX11
  EXE, 2026-11-12): each iteration passes `g_View[i]` (array at 0xDA1310) as the source, a stack local for the
  determinant and `g_InvView[i]` (array at 0xDA1410) as the destination to the D3DXMatrixInverse thunk: the call at
  0x51E1C3, return address **0x51E1C8** (instruction sequence replaced by this description, 2026-11-18, D83 audit). The loop runs after `GetViewProj` (0x51DFC9), the frustum
  planes (0x51DFD5) and the wrap copies (0x51E010..0x51E197), so at its first call view 0, the projection, the wrap
  views and `g_NumViews` are final. After it UpdateViews only calls 0x5D0370, the ortho block 0x51B450, 0x615950 and
  the view-block hand-off 0x617F30, then returns (0x51E2D4).
- 0x8A86B6 is the import thunk `jmp dword ptr [0x9D6B74]`; the import table names that slot
  `d3dx9_42.dll!D3DXMatrixInverse` (checked from the PE import directory by `shell/tests/test_lsprobe.py`).
- The probe puts a MinHook inline hook on the export `D3DXMatrixInverse` inside `d3dx9_42.dll` (a Microsoft DLL, the
  same mechanism as the d3d11/dxgi hooks; the EXE image and its import table are never written, D4). The detour calls
  the original and takes `QueryPerformanceCounter` only when `pOut == &g_InvView[0]` and the return address is
  0x51E1C8 (both rebased on the module base). Every other caller (the EXE calls the function from many places and
  threads) costs one compare.
- Validated once before hooking, refused with a logged reason otherwise: the camera validation (TimeDateStamp
  0x546CD0A8 and the 11 patterns of `shell/common/exe_camera.h`), the 12 bytes at 0x51E1BC (`8D 86 &g_InvView[0] 50 E8`
  with the call landing on 0x8A86B6), the 6 bytes at 0x8A86B6 (`FF 25 &0x9D6B74`) and the slot holding the export's
  address (another hooker's IAT patch would be refused).

This hook is also a candidate for the lockstep design's publish point (section 4).

### 1.2 What is recorded per frame

All times are QPC on one clock (the server's too). Per Present of the target swap chain one row in
`shell_composite.lsprobe.csv` (written by the DLL's control thread from a ring of 16,384 rows):

| Column | Taken where |
|---|---|
| `t_uv` | the UpdateViews timestamp (1.1) that preceded the frame's scene-clear list |
| `t_first_exec` | the first `ExecuteCommandList` on the immediate context in the frame, before the original call |
| `t_sc_before` / `t_sc_after` | the `ExecuteCommandList` of the scene-clear list (tags `clear_bb` + `clear_dsv`, insertion-point-and-frame-structure.md 9.1), before / after the original call; the insertion (`tag_rule=after`) runs right after `t_sc_after` |
| `t_ins_end` | after the composite's own handling of that list (the insertion: AcquireSync, copy, draw) |
| `t_def_first` | the first deferred-context `ClearRenderTargetView` / `OMSetRenderTargets` since the previous Present (any thread) |
| `t_present`, `t_prev_ret` | the Present hook's entry; when the previous Present returned |
| `uv_before_sc`, `uv_after_sc` | UpdateViews calls between the previous Present and the scene clear, and between the scene clear and this Present |
| `srv_*`, `stamp_now` | for the server frame taken at this frame's insertion: its release time (`frameQpc`, GPU-complete with the server's default `--gpu-sync 1`), the camera block it was drawn with (`VPShellDepth::camFrameStamp`, server `--depth 1`) and that block's publish time (the camera read at Present) |
| `cam_x/y/z` | camera position (GVS camWorld row 3) read at this Present: pan speed |

A frame counts for the main metric when it executed the scene-clear list and exactly one UpdateViews came before it
in the frame (`uv_before_sc == 1`, `uv_after_sc == 0`); other frames are counted by kind.

**Main metric `uv_sc` = `t_sc_after - t_uv`**: the time the server has between the camera being final and the
point where its picture must be in the EXE's frame.

## 2. Run d33_r2: by scene size (2026-11-13 00:23-00:42) [L]

Setup: Goldberg instance c5h-e, DX11 EXE (TimeDateStamp 0x546CD0A8), DLL i2-shell-test b92373aeb (SHA-256 638B5E21...,
the old pins of epic 3200's live runs: D33 needs its view-mask marker; not the i0-ship pin of COMMON rule 18), D33
(`tap_live.py config --arm d33`, marker `viewmask.enabled` `clear=0x1`), save copy C5H-9039-HUGE_0350 (huge, turn 350),
window 2554 x 1411, RTX 4080; composite DLL and server of commit 0671c99 (`D:/c5-64bit-work/9039/shell-build`),
`insert=tagged tag_rule=after tag_match=clear_bb tap=1 bake_tap=1 lockstep_probe=1`, server `--scene terrain --fallback
constant --constant-rgba 0xFFFF00FF --depth 1`. Quiet-machine lock held 00:23-00:42 by 9039 (MC priority window);
total CPU 22.9-44.2 % per window (mean 28.3 %); a 9036 dump analysis (`probe1.py`, up to 462 % of one CPU) ran
00:24-00:38 and chrome 00:40-00:42 (the release's disturbance summary). Free commit 43.1 -> 41.7 GB. EXE private 1,622 MB
after load. Arms in 6 rounds x 4 windows of 30 s, order rotated: `t_*` = composite + server drawing every camera
block; `pt_*` = passthrough with `camera=0` (server HIDDEN, nothing drawn); `*_idle` / `*_pan` (posted RIGHT/LEFT holds
of 1.5 s, camera ~400 world units/s at zoom 14.3). 319,378 rows; **every in-game frame had exactly one UpdateViews,
before its scene-clear list** (frame kinds: 100 % `ok` in all 24 windows; no frame with UpdateViews after the scene
clear or two before it): the EXE's main thread does UpdateViews, then the frame's lists, then Present, strictly in
that order [L]. UpdateViews, ExecuteCommandList and Present ran on one thread (the main thread, 62084) [L].

**Defect of the series:** the pans drifted the view (each window started its pans with RIGHT) from the city view at
plot (100, 30) (~69 lists per frame) through mid-size views into open ocean (5 lists per frame: one per phase), so the
windows mix scenes and arm comparisons by window are confounded. The frames are therefore grouped by the frame's list
count (`shell/evidence/9039/ls_scene_9039.py`), which is what the time depends on:

| Mode, lists per frame | Frames | uv_sc min | p1 | p5 | p50 | p95 | p99 | UpdateViews -> first deferred record (p50) | -> first ExecuteCommandList (p50) | first Execute -> scene clear done (p50) | previous Present returned -> UpdateViews (p50) | frame time (p50) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| passthrough, 5-9 (ocean) | 47,843 | 0.994 | 1.038 | 1.068 | 1.826 | 2.354 | 2.725 | 1.429 | 1.807 | 0.018 | 0.355 | 2.387 |
| passthrough, 10-39 | 41,551 | 1.064 | 1.142 | 1.192 | 1.968 | 2.621 | 3.159 | 1.504 | 1.898 | 0.069 | 0.358 | 2.566 |
| passthrough, 40-59 | 37,020 | 1.184 | 1.531 | 1.624 | 2.417 | 3.000 | 3.438 | 1.880 | 2.318 | 0.095 | 0.364 | 3.054 |
| passthrough, 60+ (city) | 9,472 | 1.983 | 2.438 | 2.729 | 3.388 | 4.102 | 4.574 | 2.696 | 3.239 | 0.150 | 0.424 | 4.158 |
| composite + server, 5-9 | 52,047 | 1.021 | 1.072 | 1.111 | 1.433 | 2.346 | 2.759 | 1.126 | 1.417 | 0.016 | 0.324 | 2.251 |
| composite + server, 10-39 | 34,353 | 1.097 | 1.184 | 1.242 | 1.623 | 2.646 | 3.101 | 1.261 | 1.570 | 0.052 | 0.321 | 2.563 |
| composite + server, 40-59 | 27,255 | 1.324 | 1.598 | 1.673 | 2.314 | 3.018 | 3.383 | 1.819 | 2.234 | 0.076 | 0.343 | 3.410 |
| composite + server, 60+ | 10,453 | 1.970 | 2.403 | 2.564 | 3.407 | 4.077 | 4.440 | 2.709 | 3.259 | 0.147 | 0.421 | 4.784 |

(all in ms; `D:/c5-64bit-work/9039/d33_r2/scene.json`, per-window table `result.json`.)

Reading it [L numbers, I interpretation]:
- **The time is spent on the main thread after UpdateViews, before any list executes.** The scene-clear list executes
  0.02-0.15 ms after the frame's first ExecuteCommandList, although phases A-C (18-22 + 1 + 7-11 lists in city views,
  insertion-point-and-frame-structure.md 9.1) come before it; the first deferred-context record starts 1.1-2.7 ms
  after UpdateViews. So UpdateViews -> scene clear is "the rest of the main thread's update after the camera" (the
  per-system updates of GVS::Update and the render submission, camera-and-picking.md 4) plus the recording, not the
  GPU work of the offscreen passes.
- **The floor is about 1.0 ms**: 0.99 ms minimum and 1.04-1.07 ms at the 1st percentile in the emptiest scene; ~2.4 ms at
  the 1st percentile in the city view. It grows with what is on screen.
- The composite's own work does not shorten it: `sc_present` (scene clear -> Present) holds the insertion (0.3-0.75 ms
  in composite mode against 0.02-0.11 ms in passthrough), which lies after the scene clear.
- In the sparse buckets the composite mode's median is shorter than passthrough's (1.43 against 1.83 ms at 5-9 lists)
  while the minima agree (1.0 ms); not explained, and the buckets mix windows taken at different places and times
  [L numbers; cause open]. Run d33_r3 (section 3) repeats the series at one view.

## 3. Run d33_r3: one view, with the server's turnaround (2026-11-13 01:21-01:40) [L, grade A for the probe metric]

Setup as section 2, with two changes (commit f7bdbba): every window starts with `UI.LookAt` of plot (100, 30) and a
5-s settle (the city view, 38-69 lists per frame, 69 at rest); the server writes the depth block's `camFrameStamp`
(section 3.3), so every composite frame that takes a new server frame knows which camera it was drawn with. Lock held
01:21-01:40 (MC priority window); total CPU 23.7-35.6 % per window (mean 25.6 %); busy others from the release's
summary: chrome up to 61 % of one CPU, a 9037 testbed submit (13 %). Free commit 40.6 GB at start and end. EXE private
1,628 MB after load. 6 rounds x 4 arms x 30 s, rotated; 238,325 rows, 191,999 of them in the windows, **every one `ok`** (one UpdateViews, before
the scene clear). Raw: `D:/c5-64bit-work/9039/d33_r3/` (CSV, windows.json, result.json, scene.json, wait.json, server log).

### 3.1 UpdateViews -> scene clear executed, per arm (ms)

| Arm (6 windows each) | Frames | min | p1 | p5 | p50 | p95 | p99 | Window p50: mean, sd, 95 % CI | Window p1: mean, sd | Frame time p50 |
|---|---|---|---|---|---|---|---|---|---|---|
| composite + server, idle | 42,508 | 1.818 | 1.999 | 2.131 | 3.170 | 3.708 | 4.054 | 3.184, 0.076, [3.104, 3.264] | 2.060, 0.068 | 4.342 |
| composite + server, pan | 46,762 | 1.560 | 1.750 | 1.934 | 2.957 | 3.780 | 4.173 | 2.959, 0.110, [2.843, 3.074] | 1.769, 0.055 | 4.110 |
| passthrough (camera off, server idle), idle | 49,620 | 1.844 | 1.938 | 2.011 | 3.111 | 3.726 | 4.114 | 3.142, 0.092, [3.045, 3.238] | 1.961, 0.060 | 3.775 |
| passthrough, pan | 53,109 | 1.538 | 1.674 | 1.844 | 2.963 | 3.785 | 4.209 | 2.966, 0.055, [2.908, 3.024] | 1.685, 0.043 | 3.682 |

- **Panning shortens the median by about 0.2 ms and the 1st percentile by about 0.3 ms** (the view changes per frame:
  the list count falls to 38-50 in part of the pan frames, scene.json); it does not open a different regime. Per-window maxima are
  4.9-6.0 ms idle with the composite and up to 17-34 ms in some passthrough and pan windows: single frames.
- **The composite and the server do not change it**: composite+server against passthrough differ by +0.04 ms (idle)
  and -0.01 ms (pan) in the window medians, inside the windows' spread (sd 0.05-0.11 ms; the series could detect about
  0.12 ms). The composite's insertion and the server's GPU work lie after the scene clear or in another process.
- The breakdown (scene.json, 60+ lists): previous Present returned -> UpdateViews 0.40 ms; UpdateViews -> first
  deferred record 2.6 ms; -> first ExecuteCommandList 3.1 ms; first ExecuteCommandList -> scene clear executed 0.15 ms;
  scene clear -> Present 0.65 ms with the insertion (0.11 ms without).

### 3.2 The server's turnaround, and a 64 Hz polling state

Turnaround = the server's release of a frame (`frameQpc`, after its GPU work completed: `--gpu-sync 1`) minus the
publish of the camera it drew (the composite's read at a Present). Composite + server windows from 01:29:42 on
(5 idle, 4 pan windows; 60,548 frames with a new server frame):

| | min | p1 | p5 | p50 | p95 | p99 |
|---|---|---|---|---|---|---|
| turnaround, idle (ms) | 1.242 | 1.391 | 1.517 | 2.089 | 2.835 | 3.350 |
| turnaround, pan (ms) | 1.257 | 1.391 | 1.512 | 2.086 | 2.831 | 3.344 |

The inserted frame was drawn from the previous Present's camera in 99.4 % (idle) and 99.6 % (pan) of those frames
(stamp lag 1; lag 2 in 0.4 %, lag 4-7 in 0.6 % of idle frames, at the first seconds after a passthrough window where
the server had been HIDDEN). The parts [L, server `terrain 1s` lines of the same windows]: GPU per draw 0.776 ms
(median of 348 per-second means; 0.711-1.847), CPU in draw() 0.46 ms, 229 draws per second at 104 tiles; the rest
(~0.85 ms) is the 0-0.5 ms polling interval, the copy into the shared texture, the GPU-sync wait and the keyed mutex.

**From the server's start until 01:29:42 its loop ran at 64 Hz** (camera blocks per second 63-66 against the game's
~230 frames): the turnaround was 16-17 ms and the inserted frame 4-5 Presents old (the first three composite windows).
At 01:29:42 it switched to full rate by itself and stayed there. Run d33_r2 never showed it. The loop waits on a
high-resolution waitable timer (`CREATE_WAITABLE_TIMER_HIGH_RESOLUTION`, 0.5 ms; `shell_server.cpp` pacing) with
`timeBeginPeriod(1)`; 64 Hz is the 15.6 ms default timer tick, so the timer was coarsened. The server runs on a hidden
desktop; Windows 11 may ignore timer-resolution requests of processes without a visible window and apply power
throttling [I: cause not proven; the switch back without any action of ours fits a system-wide state; 2026-11-13,
bug 0118: the timer-resolution part is now measured and the opt-out is in both servers, section 3.2.1]. For lockstep
this matters more than the draw: the server must be woken by an event from the composite and opt out of power
throttling (`SetProcessInformation(ProcessPowerThrottling)`, both EXECUTION_SPEED and IGNORE_TIMER_RESOLUTION) [I].

#### 3.2.1 Bug 0118: the timer coarsening measured, and the opt-out (tracker 4000, 2026-11-13)

Measured 2026-11-13 03:45-04:00 EST on the workstation (AC power, quiet-machine lock free, other teams' builds
running) with `server/tests/test_power.cpp --rate` (built and run by `server/tests/run_power_test.py`): a windowless
console process, `timeBeginPeriod(1)`, then a wait loop for 6-8 s per run, one process per mode, two interleaved
rounds, per-second loop rates (the first second dropped) [C]:

| wait in the loop | mode: nothing set | mode: throttling requested (control 0x5, state 0x5) | mode: opt-out (control 0x5, state 0x0) |
|---|---|---|---|
| `Sleep(1)` | **670-696 Hz for about 3 s, then 64 Hz** in both rounds (n=10: median 64, max 696) | 64-65 Hz from the start (n=10) | **653-671 Hz throughout** (n=10: median 666) |
| 0.5 ms `CREATE_WAITABLE_TIMER_HIGH_RESOLUTION` + `WaitForSingleObject(timer, 5)` (shell_server's terrain pacing) | 807-903 Hz (n=14 / n=10 in two sessions, medians 832 / 837) | 804-878 Hz (medians 830 / 845) | 818-933 Hz (medians 839 / 846) |

So [C]: Windows 11 (26100) stops honouring `timeBeginPeriod(1)` of a process without a visible window about 3 s after
it asked, and every wait that rests on the system tick then runs at 64 Hz (15.6 ms) - the number of 3.2. The opt-out
`SetProcessInformation(ProcessPowerThrottling)` with ControlMask EXECUTION_SPEED | IGNORE_TIMER_RESOLUTION and
StateMask 0 keeps the 1 ms tick. The high-resolution waitable timer itself was not coarsened in any mode in these runs,
so which wait of shell_server's loop carried 9039's 64 Hz phase stays [I] (its loop also has keyed-mutex
`AcquireSync(…, 0)` + `Sleep(0)` and D3D waits, and the 64 Hz phase of 3.2 was not reproduced here).

Longer runs, 04:04-04:20 EST, same machine and lock state (other teams' builds running: not quiet), 120 s per run, two
interleaved rounds, first second dropped, 238 seconds per mode [C]:

| loop | nothing set | opt-out |
|---|---|---|
| `Sleep(1)` | medians 65 / 65 Hz; **234 of 238 seconds under 100 Hz, from second 4 on in both rounds** | medians 672 / 664 Hz; 0 seconds under 100 Hz (min 153 Hz, one second) |
| 0.5 ms high-resolution timer | medians 944 / 872 Hz; 5 seconds under 200 Hz (round 2: 99-129 Hz for 3 s at second 28-30, 75-112 Hz at 42-43) | medians 915 / 927 Hz; 1 second under 200 Hz (191 Hz) |

The `Sleep(1)` result is the same in every run: without the opt-out the tick is 15.6 ms after ~3 s. The dips of the
high-resolution loop are few and the machine was loaded, so they do not tell timer throttling from load (5 against 1
seconds; not a significant difference) [I]. Statistics as `docs/briefs/PERFORMANCE.md` asks are given for the
`Sleep(1)` case only; no speed claim is made for the high-resolution path or for shell_server's frame rate.

The change (tracker 4000, issue 0118): `server/src/srv_power.h` (header only) applies that opt-out to the process and
the execution-speed opt-out to the calling thread, then reads the process state back; `shell_server.exe` calls it
right after `timeBeginPeriod(1)` and logs the line, `server.exe` calls it at the start of `main` (stdout, then
`server.log`). The line reads `power throttling opt-out (bug 0118): process control 0x5 state 0x0 set, main thread
execution speed set; reads back control 0x5 state 0x0`. Tests: `shell/tests/test_power_optout.py` (the server on the
hidden desktop, its masks read from outside with `GetProcessInformation`): passes on the fixed build, fails on the
unfixed one with `control 0x0 state 0x0 (expected 0x5 0x0)` [C]; `server/tests/run_power_test.py` (the header's unit
test, and `--server-exe` for server.exe's start-up line): the unit test passes; server.exe built from the fix
(`build_server.py --config release --no-proxy`, the DLL pin 76992d235's deps) prints the line and passes, server.exe
built from the commit before the fix fails with `no power-throttling line` / `FAIL: server.exe start-up did not apply
the opt-out` [C]. Builds and scratch under `D:/c5-64bit-work/4000/0118/` (removed afterwards).

**The long live run (queue ticket 0101, account B, 2026-11-13 20:25-21:02 EST) [L]: no 64 Hz phase.** The D33 product path
(no `shell_server.exe`: the render thread inside `server.exe`, lockstep + ring + fence, `[Renderer] ServerRenderer = 1`), D96
DLL 74588135..., server.exe B20ACEFC... built from main 7b8f45f7, huge save copy, instance c5h-a on the hidden desktop, 55
AI turns (350 -> 404), 36 minutes of game. server.log has the opt-out line for the process (`[0.086] server: power
throttling opt-out (bug 0118): process control 0x5 state 0x0 set, ... reads back control 0x5 state 0x0`) and for the render
thread (`[1.423] render: render thread 36940: ...`, also in render.log). **Measure used:** server.exe has no timer loop of
its own and its render thread is woken by the composite's publish, so server.log gives no loop rate; the measure is the
lockstep records (`shell_composite.lockstep.csv`, 207,752 rows): per frame the server turnaround srv_ready - srv_cam_pub
and publish -> wake srv_wake - srv_cam_pub, grouped by wall second of the publish; a second whose median turnaround is over
10 ms counts as "under 100 Hz" (a 64 Hz phase shows as 16-17 ms, 3.2). Result over 182,844 frames with a server frame in
1,891 seconds (span 1,911 s): turnaround p50 1.174 / p95 1.877 / p99 3.843 / p99.9 7.562 / max 230.7 ms; publish -> wake
p50 0.017 / p95 0.037 / p99 2.409 / max 222.5 ms; **0 seconds with a median over 10 ms** (largest per-second median 8.71 ms,
median of the medians 1.24 ms), longest run 0 s (the criterion allows 5 s); 1 frame of 182,844 in the 15-18 ms band. The
isolated maxima (one frame each) are single late wakes, not a phase. Not shown by this run: render.log's `drawn=` per
second is not a timer rate in lockstep (it follows the EXE's frame rate: OK-state seconds p50 100 draws, 929 of 1,877
under 100; issue 0148's 2-Hz long frame is on this path [I: not separated here]), so it was not used. Checker `shell/evidence/0101/check_0101.py`
(its test `test_check_0101.py` covers a 6-s 64 Hz phase failing and a 5-s one passing), result
`shell/evidence/0101/acceptance.json`, logs `D:/c5-64bit-work/0101/live/out/`. Bug 0118's definition of done (a long live
run without a 64 Hz phase) is met for the product path; `shell_server.exe`, where 9039 saw the phase, is not on it.

### 3.3 Server defect fixed: the depth block never named its camera

`shell_shared.h` (epic 3200, issue 3208) documents `VPShellDepth::camFrameStamp`, `proj22`, `proj32`, `nearZ`, `farZ` as
"the camera the server drew this frame with"; `PublishDepthBlock` never wrote them (all 0 in run d33_r2) [C, L]. Fixed
in f7bdbba (written from the terrain state's last drawn stamp and camera); `test_depth_published` now checks the stamp
and the projection. The depth hand-off design of D81 relies on exactly this field.

## 4. What it means for lockstep (D81)

Model [I]: under lockstep the composite publishes the camera at UpdateViews (the probe's hook, or a point just after
it) and the insertion after the scene-clear list waits until the server's frame of that camera is released. The
server's work is the same as today's from publish to release, so a frame waits `max(0, turnaround - uv_sc)`. Paired per
frame with the measured values (each frame's uv_sc against the turnaround of the server frame it inserted;
`shell/evidence/9039/ls_wait_9039.py`, wait.json, windows from 01:29:42):

| City view | Frames that would wait | Wait when waiting: p50 | p95 | Mean cost per frame |
|---|---|---|---|---|
| idle | 8.5 % | 0.23 ms | (12.5 ms: the lag-4-7 frames after HIDDEN windows, which lockstep would not have) | 0.096 ms |
| pan | 11.3 % | 0.25 ms | 0.90 ms | 0.040 ms |

- **At the city view lockstep costs little with today's server**: one frame in ten waits, a quarter of a millisecond.
- **In sparse views it would wait in most frames**: UpdateViews -> scene clear is 1.0-1.8 ms (section 2) against a
  2.1 ms median turnaround (the ocean view draws fewer tiles; its turnaround was not measured because run d33_r2 lacked
  the camera stamp) [I].
- The bound should sit around the turnaround's tail (p99 3.35 ms) and a miss show the previous picture (the Architect's
  rule; bug 3014).
- The levers are on the server's side and are already named: event wake-up (removes up to 0.5 ms), no throttled
  timer (3.2), a texture ring or shared fence instead of keyed mutex + gpu-sync (Architect 3.2). With a turnaround near
  the bare draw (0.46 ms CPU + 0.78 ms GPU, partly overlapping), the floor of ~1.0 ms would rarely be undercut [I].
- **A publish point exists and is cheap**: the probe's hook fires once per frame on the main thread inside UpdateViews,
  after all views are final, in 100 % of map frames of both runs; the Present-time camera read could move there.

## 5. Not verified, and limits

- One view per run (city, plus the drift of d33_r2 through ocean); other saves, zooms and Strategic View not measured
  (UpdateViews' mode-2 path does not take the hooked call, so the probe records nothing there [C]).
- The stock configuration (the EXE's own terrain) was not run: the second hold was used for d33_r3. Lockstep concerns
  the D33 + server configuration.
- Old pins (DLL i2-shell-test b92373aeb), as epic 3200's live runs; not the i0-ship pin of COMMON rule 18.
- The model of section 4 assumes the server's turnaround does not depend on where the publish point is; a publish at
  UpdateViews comes ~0.4 ms + Present later than today's.
- The cause of the 64 Hz polling state (3.2) is inferred, not proven. (2026-11-13, bug 0118: the tick coarsening of
  windowless processes is measured and opted out of, 3.2.1; the 64 Hz phase of shell_server itself was not
  reproduced, and no long live run with the opt-out has been made.)
- One run each; within run d33_r3 the window statistics are grade A (6 interleaved windows per arm, lock held, load
  recorded), but there is one launch.
- Run d33_r1 (00:17) never started: the lock was held by 9038; the next attempt failed at launch because this
  worktree had no harness native build (`tools/harness/native/build.bat`).

Evidence: `shell/evidence/9039/` (ls_setup_9039.py, ls_live_9039.py, ls_analyze_9039.py, ls_scene_9039.py,
ls_wait_9039.py); raw data `D:/c5-64bit-work/9039/d33_r2/`, `d33_r3/` (not committed; CSVs of 63 MB and 50 MB kept for
review).
