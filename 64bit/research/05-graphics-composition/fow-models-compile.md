# The frozen frames after a huge load: the fow-models compile is NOT the stall; the server's first GPU work after the field is (ticket 0407)

Written 2026-11-16 by impl-B-0407 (account B), branch `task-0407-fow-models-rsinit-off-thread`. Marks: `[C]` from the
code or an offline measurement, `[L]` measured live in the game, `[I]` inferred. Raw data: `F:/c5-64bit-work/0407/<run>/`
(per load: `out/` the composite, render, server and proxy logs and the lockstep CSV; `gpu/` nvidia-smi every 200 ms and
typeperf every 1 s; `stacks/` cdb stack samples and every thread's CPU seconds every 0.25 s; `analysis_0407.json`).
Drivers and tables: `shell/evidence/0407/` (`build_pp_0407.sh`, `live_0407.sh`, `run_0407.py`, `summary_0407.py`,
`vram_0407.py`, `acceptance.txt`). **Correction to `terrain-surface-verification.md` section 14, cause 3 (0404)**: its
"EXE stall ... spans the composite's fow-models first-mask set-up (d3dcompiler_47 load + D3DCompile)" is wrong; section 3
here is the cause. The MC applies the correction there at the merge (that file is not in this ticket's touches).

## 1. Result

- **The resample pass's programs are built in** [C, L]: `shell/composite/fow_models_resample.hlsl` (our own source, the
  HLSL 0394 kept in a C string) is compiled by `shell/build.cmd` with the Windows SDK's fxc (`vs_4_0` / `ps_4_0`, `/O1`,
  D3DCompile's default level, as 0394 measured it) into `%C5_SHELL_BUILD%\gen\FowRsVS.h` / `FowRsPS.h` (680 + 1,172
  bytes); `RsInit` makes the two shaders from those arrays. The composite no longer loads `d3dcompiler_47.dll`; its ready
  line says whether another module of the process has it: `d3dcompiler_47.dll not loaded` in every live load of the after
  arm. `RsInit` on the EXE's render thread: 0.28-0.58 ms live (9 loads), 0.57 ms on the stand-in. The whole first copy
  (slot open, local textures with the set-up, copy, move, swap) 12-28 ms on the render thread, now logged in the
  `first mask copied` line.
- **The compile was never the stall** [C, L]. 0394's `RsInit` (LoadLibraryW of `d3dcompiler_47.dll` + D3DCompile of
  both programs) costs **10.5-20.6 ms** (median 15.1; load 1.8-7.7 ms, compile 8.7-16.3 ms; 10 fresh processes, 64-bit
  Python on the System32 copy, workstation 2026-11-16 06:47, `shell/evidence/0407/compile_cost.py`). 0404 timed the span
  from `fow_models: section ... open` to `the resample pass is ready`; that span is mostly the wait for the server's first
  mask, which comes late *because of* the stall below, not the set-up itself.
- **Before/after on the ticket's metric** (section 2): with the stall absent, the longest gap in the first 20 s after the
  click is the click's own frame (0.53-0.83 s) in both arms; with it present, 3.1 s (several in a row). The stall's
  occurrence does not follow the arm (it came in 0404's four loads with the old build, in 4 of 9 after loads here and
  in 0 of 4 unperturbed before loads here; it needs a loaded board, 3.5).
- **What freezes the picture** (section 3) [L]: **the server's first GPU work after the field does not complete for
  8.7-23.4 s.** In every stalled load (4 here, 4 in 0404) the stall is exactly the span from the server's first copy into
  its new NT-shared fog-mask slot (`the copy into slot 1 has not signalled after 3 polls`) to that copy's completion
  (`first mask handed over`); in the loads without the stall the same copy completes in 0.1-0.9 s. Through the span one
  thread of the NVIDIA user-mode driver (`nvwgf2umx`) in the server process runs at ~95 % of a core, 60-75 % of it in
  kernel mode, the server's render thread waits on it (0.28 s of CPU in 16.7 s), and the EXE's main thread waits in steps
  of exactly 3.0 s between a Present and the next frame's camera publish. Section 4: what that GPU work is (not
  established; the evidence points at GPU memory pressure on a shared board).

## 2. Before and after: the longest presented-frame gap after the click [L]

Setup, 2026-11-16 07:07-09:10 EST: workstation (functional runs on a loaded machine, 76-100 % CPU on 32 logical
processors, 10+ game clients and servers on the RTX 4080 until about 08:45, fewer after; no quiet lock: durations are indications,
COMMON 23), instance c5h-d (Goldberg, F:), save `C5H-VP8P-HUGE_0350` (c499d3c6...) copied as `C5H-0407P-HUGE_0350` (removed at
each restore), the D33 product path as 0404 (ServerRenderer = 1, the composite with its manifest, Overlay kept on, frame
cap 30), client 2554 x 1411, Vibrant Textures present. Builds from this branch (`build_pp_0407.sh`): server.exe
a317639d..., proxy 6ba22c5c..., x64 DLL 74588135... (the pin's release DLL); composite **after** 6f2f11b4... (this
branch), **before** 7a9e7d1f... (main a453879e, 0394's RsInit). Driver `run_0407.py` over 0404's `run_0404.py`: the load
screen closed through its button's handler the moment the button shows (the earliest click), no back-buffer dumps after
the click (control-arm runs, tag `nd`); prep and restore by hash around every load (`set-proxy --original`, provision
verify `ok`). Gaps from the composite's lockstep CSV (`present_qpc`, QPC / 1e7 = `perf_counter`).

| run | arm | longest gap, 20 s after the click (s) | at s after the click | prev. Present -> camera publish (s) | frames/s, 20 s | fow set-up | first copy, render thread (ms) | largest single bake (ms) | server quiet span (s) |
|---|---|---|---|---|---|---|---|---|---|
| b1nd | before | 0.663 | 0.07-0.73 | 0.06 | 20.1 | d3dcompiler_47 | - | 56 | 1.7 |
| a1nd | after | **3.135** | 11.5-14.6 | 3.009 | 5.1 | 0.276 ms built in | 12.2 | 19,792 | **20.1** |
| b2nd | before | 0.596 | 0.07-0.66 | 0.036 | 19.2 | d3dcompiler_47 | - | 99 | 1.0 |
| a2nd | after | 0.623 | 0.10-0.72 | 0.037 | 22.4 | 0.321 ms built in | 14.2 | 129 | 1.7 |
| b3nd | before | 0.719 | 0.11-0.83 | 0.044 | 20.3 | d3dcompiler_47 | - | 776 | 1.3 |
| a3nd | after | 0.598 | 0.07-0.67 | 0.045 | 21.4 | 0.401 ms built in | 16.4 | 47 | 1.0 |
| x1nd | after | **3.103** | 11.8-14.9 | 3.010 | 4.9 | 0.315 ms built in | 23.0 | 13,410 | **13.7** |
| x2nd | after | 0.829 | 0.07-0.90 | 0.045 | 19.7 | 0.377 ms built in | 14.8 | 98 | 1.1 |
| x3nd | after | **3.145** | 11.9-15.1 | 3.006 | 5.0 | 0.305 ms built in | 15.9 | 16,301 | **16.7** |
| x4nd | after | **3.116** | 3.1-6.2 | 3.010 | 11.0 | 0.576 ms built in | 28.2 | 8,173 | **8.7** |
| x5nd | after | 0.534 | -0.25-0.28 | 0.035 | 24.9 | 0.390 ms built in | 25.3 | 30 | 1.0 |
| x6nd | after | 0.746 | 2.28-3.02 | 0.641 | 23.8 | 0.312 ms built in | 11.7 | 109 | 1.0 |

| b4nd | before | 3.073 | 11.4-14.5 | 3.057 | 13.3 | d3dcompiler_47 | - | 3,169 | 3.9 (perturbed, 3.3) |
| b5nd | before | 0.412 | 0.05-0.46 | 0.026 | 23.6 | d3dcompiler_47 | - | 37 | 1.0 |
| m1nd | after, model_fow=0 | 0.452 | -0.25-0.20 | 0.031 | 25.4 | off | - | 77 | 1.0 |
| m2nd | after, model_fow=0 | 0.472 | 0.04-0.51 | 0.044 | 25.4 | off | - | 60 | 1.0 |
| m3nd | after, model_fow=0 | 0.491 | 0.06-0.55 | 0.041 | 25.4 | off | - | 91 | 1.1 |

(The same table with the GPU-memory columns: `shell/evidence/0407/acceptance.txt`; `summary_0407.py` and
`vram_0407.py` print it from the run folders.)

Reading: without the stall the longest gap is the frame of the click itself (the dismissal's Lua runs in it; kind `none`,
0.6 s from the camera publish to the insertion) in both arms. With the stall, the EXE runs at 5-11 frames/s with 4-5 gaps
of 3.0-3.15 s, each spent between a Present and the next frame's camera publish. The ticket's "after" condition (no
compile on the render thread) holds in every after load; the stall is independent of it. The ticket's arms cannot be
compared on the 20-s gap until the stall is fixed: it dominates whichever arm it falls in.

## 3. The stall is the server's first GPU work after the field

### 3.1 The span [L]

Every stalled load, 0404's included (seconds; 0404's wall times from `run.json` events, +-0.5 s):

| load | server quiet span (no `terrain 1s:` line) | first fog-mask copy polled 3 times -> handed over | EXE frames in the span |
|---|---|---|---|
| 0404 run1 | 11.9 (04:54:35.4-47.3) | 35.36 -> 47.86 (12.5) | frozen 13.6 s (with a dump) |
| 0404 run3 | (field build, then) 09.64 -> 12.93 | 3.3 | frozen 3.2 s |
| 0404 run4 | 39.88 -> 41.87 (2.0) | 39.96 -> 42.04 (2.1) | frozen 1.9 s |
| a1nd | 20.1 (07:20:22.1-42.2) | 22.30 -> 45.71 (23.4) | 5 fps, 3.0-s gaps |
| x1nd | 13.7 (07:58:08.2-21.9) | 08.36 -> 22.68 (14.3) | 4.9 fps, 3.0-s gaps |
| x3nd | 16.7 (08:14:14.1-30.9) | 14.25 -> 30.85 (16.6) | 5.0 fps, 3.0-s gaps |
| x4nd | 8.7 (08:21:51.7-22:00.3) | 51.95 -> 22:00.98 (9.0) | 11 fps, 3.0-s gaps |

In the loads without the stall the first copy is handed over 0.1-0.9 s after the third poll (b2nd 0.08, a3nd 0.29,
x6nd 0.88). The server's residency reports the span as one bake call: `bake_cpu_max_ms` 19,792 / 13,410 / 16,301 / 8,173
(here) and 11,646 / 5,432 / 2,735 (0404 runs 1-3); a normal first bake is 30-780 ms. The bake does not itself take that
long: it is the render thread's D3D call that happens to be running when the driver blocks.

### 3.2 Who runs and who waits [L]

`stacks/threads.csv` (every thread's CPU seconds every 0.25 s, psutil) over x3nd's 16.7-s span:

- server thread 0x8720, an NVIDIA driver thread (`nvwgf2umx!...NVDEV_Thunk` at its root in the cdb samples): **16.4 s of
  CPU in 16.7 s, 10.9 s of it kernel mode**, ~95 % of a core from the third poll to the hand-over (0.0 before);
- server render thread `vp-render` (0x9078): 0.28 s: it waits on the driver; a cdb sample of x4nd's span found it inside
  `d3d11` -> `nvwgf2umx` (the unwind broke there; non-suspended sample);
- EXE main thread: 3.4 s of 16.7 s; each 3.0-s gap shows ~0.4 s of CPU: it waits. Its sample (x1nd, end of the span):
  `ntdll!NtWaitForSingleObject` called from the EXE's own code.

The F: drive (the instance's install and logs) was 97 % idle in the gaps (typeperf), so disk I/O is not the cause; the GPU
was 23-45 % busy (nvidia-smi) and the CPU 90-100 %.

### 3.3 The EXE's 3.0-s steps follow the server, and the fog-mask copy is not needed for the stall

- **A server that merely stops gives the same EXE steps** [L, by accident]. In b4nd (before arm, 08:43) the driver's
  stack sampler still suspended the server for its sample (cdb `-pv`, 08:43:45.2-52.3; changed to never suspend since):
  the server's render thread then wrote nothing for 3.9 s and the EXE showed a gap of **3.073 s** (Present -> camera
  publish 3.057 s) at 08:43:49.2-52.2, inside the suspension. So the EXE's 3.0-s steps are the EXE waiting on its server
  (a wait in the EXE's frame that gives up or is answered every ~3 s), not a board-wide GPU freeze. b4nd is perturbed and
  left out of the before/after counts.
- **Not board-wide** [L]: another team's client on the same machine (c5h-c, its own server; logs read only) presented
  normally (frames under 44 ms) through x4nd's stall (08:21:51-22:00).
- **Without fog models too** [L]: that client c5h-c had the same stall at its own first field at 08:14:11 (field OK
  08:14:11.57, its server quiet 08:14:11.7-18.8, 7.1 s, `bake_cpu_max_ms` 6,588) and its EXE showed frames of exactly
  3,009.9 and 3,020.2 ms (08:14:14.9, 17.9), with no fog-models section in its server's log (no fog-mask slots, no copy).
  So the first fog-mask copy is only the first thing our server polls; the GPU work it waits behind is the stall.
  (The trigger test with model_fow=0 on this ticket's build: section 3.4.)

### 3.4 Trigger test: model_fow=0 (no fog-mask slots, no copy)

Three after loads with `model_fow=0` in the composite's ini (`C5_0407_MODEL_FOW=0`; the server logged `consumer OFF`,
made no slots and copied nothing), 2026-11-16 08:53-09:10: no stall (longest gaps 0.45-0.49 s, largest bake 60-91 ms).
They ran when the machine had quietened (GPU 6.3-8.6 GB before the field, CPU 81-90 %), where no load of either arm
stalled (b5nd, the after loads x5nd/x6nd): **inconclusive on its own**. The evidence that the fog-mask copy is not needed
is c5h-c's stall (3.3).

### 3.5 The condition: a loaded board [I]

Over the 16 unperturbed loads: a stall only when the board's dedicated GPU memory was at least 9.9 GB of 16 GB before
the field (4 of 7 such loads: a1nd 9.92, x1nd 11.05, x3nd 12.63, x4nd 12.78 GB; without a stall b2nd 10.76, b3nd 10.51,
x2nd 10.17), never below (0 of 9; 5.43-9.20 GB); one-sided Fisher exact p = 0.019. CPU was 94-100 % in every stalled
load. During each stall the board's shared (system-memory) usage rose 0.06-0.33 GB, against 0.00-0.33 GB in the loads
without one (x6nd 0.33 without a stall). 0404's four stalled loads ran with 9-11 other heavy jobs (no GPU log then).

## 4. What the blocked GPU work is (not established)

[I] The GPU work queued by the server in its first frame after the field is the first use of everything the field made:
the height field's textures (8192 x 5376 samples), the residency's first array chunk and first bakes, the water and ice
meshes (ice: VB 2.2 MB, IB 2.6 MB), and the fog-mask slots (3 x 1024^2, 11 mips, NT-shared). The fog-mask copy is only
the first thing the server *polls*; its query signals only after all earlier work of the context. Kernel-mode time on the
driver's thread fits the video memory manager making the field's new allocations resident on a board that is nearly full
(3.5); the EXE then waits on its server (3.3). Naming the exact operation needs a GPU trace (WPR/GPUView: administrator
rights, ask the MC) or server-side timing around the first frame's uploads (render/ code, not this ticket's touches).

## 4a. Fix directions (for the MC's fix ticket)

1. **Measure first** (cheapest): log the time of each D3D call group in the server's first frame after the field
   (field textures' first use, residency array chunk, first bakes, water and ice uploads, fog-mask slots) with the
   board's dedicated usage (DXGI `QueryVideoMemoryInfo` budget and usage of the server's own adapter), and repeat this
   ticket's series on a loaded workstation (`shell/evidence/0407/live_0407.sh`; the stall came in 4 of 7 loads above
   9.9 GB).
2. **Take the first use off the player's frames**: touch the field's big allocations under the load screen (a draw or
   copy that makes them resident before the click), together with 0408's earlier tap start, so the paging happens while
   the screen is opaque.
3. **Decouple the EXE**: find what the EXE's frame waits on for exactly 3.0 s while its server does not progress
   (reproducible by suspending the server, 3.3); the composite should keep presenting with the last server frame.
4. If a trace shows the fog-mask slots themselves matter (NT-shared, 3 x 1024^2 x 11 mips): make them at the server's
   start instead of at the field.

## 5. Not verified

- The stall on a quiet player machine (no load stalled below 9.9 GB of board memory, 3.5; none was run alone).
- Which allocation or operation the driver's thread works on (section 4).
- Why the EXE's waits are exactly 3.0 s (a timeout of some wait in the EXE's frame; the EXE's code is not symbolised).
- The model_fow=0 arm on a loaded board (3.4).
- The stack samples' unwind inside the NVIDIA driver (non-suspended samples break there; suspending the server
  perturbs the stall, b4nd).
- The 64-bit measurement of the old compile stands in for the EXE's 32-bit SysWOW64 `d3dcompiler_47.dll`.

## 6. The "server's mask is stale" episodes in play: server render-thread stalls; the model fog now keeps its last mask (ticket 0415)

Written 2026-11-16 by impl-B-0415 (account B), branch `task-0415-stale-mask`. Marks as above. Drivers and analysis:
`shell/evidence/0415/` (`episodes_0415.py` reads a run's composite log, split by EXE process, and its render.log;
`run_0415.py` / `live_0415.sh` the live runs; `build_0415.sh`, `suites.sh`). Raw data: `F:/c5-64bit-work/0415/<run>/`.

### 6.1 What the line was [C]

0339's `fow_models.cpp` (`Run`) treated the server's fog-mask section as dead when its `heartbeat` (+1 every server
frame the fog was drawn in, `srv_fow_mask_pub.cpp` `frame()`, on the server's render thread) had not moved for
`fow_models_stale_ms` (2000) of wall time, measured at the EXE's map frames, or when the producer said STOPPED. Either
way it wrote `LOUD: the server's mask is stale (its heartbeat is still) (episode N)`, put the white view back into the
white handle's view-table entry (and FOWAnim to its start value), and swapped the mask in again only at the next newly
published mask. While the white view is bound the EXE's models on seen-not-visible plots are drawn fully lit (no fog
tint, 9.3: on the huge p13 camera 49 / 113 / 238 median / p95 / max off stock, against 0 / 61 / 231 with the mask).

### 6.2 Every episode is a stall of the server's render thread [L, from earlier runs' logs]

`episodes_0415.py` over the runs that met the line (the episode's start against render.log's `fog 1s:` lines, which the
render thread writes about once a second; a gap of more than 1.8 s = the render thread drew nothing):

| Run (ticket, machine, date 2026-11-16) | Episodes | White periods: n, total, longest | Server render-thread gap containing each episode | Decal line just before the gap |
|---|---|---|---|---|
| 0406 run 1 (workstation, 06:00-06:30, loaded) | 14 | 13, 123.1 s, 25.1 s | 14 of 14: 2.5-28.1 s (median 9.5 s) | 11 of 14 (`decals: generation N applied`, a re-bake, `set generation ... in use`) |
| 0406 run 2 (same builds and save, 06:33-06:58) | 0 | 0 | (2 gaps > 1.8 s, under 2 s of still heartbeat) | - |
| 0340 particles, step on the render thread (05:40-06:05) | 2 | 2, 3.8 s | 2.3, 7.8 s | 1 of 2 |
| 0340 particles off (06:30-06:56) | 0 | 0 | (1 gap) | - |
| 0340 particles, `std::async` step (07:07-07:34) | 2 | 2, 12.0 s | 13.6, 3.0 s | 1 of 2 |
| 0340 particles, own thread (08:00-08:28) | 3 | 2, 3.2 s | 23.0 (load), 4.8, 2.4 s | 3 of 3 |
| 0385 huge base (oldpc, 07:11-07:19, 5 loads) | 5 | 5, 13.3 s | one per load: 1.8-2.6 s right after the load's first map frames | - |
| 0385 huge worker (oldpc, 07:20-07:29) | 5 | 5, 13.3 s | one per load (3 of 5 matched to a gap) | - |

So the line is not a fault of the model fog: it fires whenever the server's render thread is held up for more than
2 s. During those stalls the rest of the server runs and the EXE keeps presenting at its normal rate (0406 run 1,
06:21:48-06:22:13: 24-100 frames a second in the composite's 1-s stats, every one an `older` lockstep frame: the last
server picture), so a player sees a still world picture and, before this ticket, the models of seen-not-visible plots
lit up for the length of the stall. The stalls come in AI turns right after the server applies a decal generation
(0406: `decals: generation 166 applied ... 54.2 ms` at 06:21:46.9, then the render thread's next steps 12.6 s, 4.6 s and
3.6 s apart: `applied by the reader`, `set generation 166 ... in use 7.8 ms`, the tiles' re-bake, the next fog line at
06:22:13.99; each step's own logged time is milliseconds), and at a load (0385: one per reload, while the server builds
the new field). [I] The steps' own times are short, so the wait is outside them: the same signature as section 3 (the
server's D3D calls blocking on a loaded board). The decal pass's vertex buffer is not re-made per generation (it grows
only, `render/src/decal/decal_pass.cpp` step 3) [C]; what blocks is not established (render/ code, not this ticket's).

### 6.3 The fix in fow_models [C, tests]

`shell/composite/fow_models.cpp` (contract in `fow_models.h`):
- **A still heartbeat is a "late" episode, the last mask stays bound.** The mask the models read is the composite's own
  copy (`g_src` / `g_local` on the EXE's device), valid whatever the server does; it is the fog the player had when the
  server's render thread stopped, and the window moves keep being made from it (0394). One line at the start (`the
  server's mask is late (its heartbeat still for N ms; late episode K): the last mask stays bound`), one at the end
  (`late episode K ended after N ms`), counted in the 10-s line and at the process's end (`late episodes K (... ms in
  all, longest ended ... ms)`). LOUD once per episode only past `fow_models_late_loud_ms` (new key, 30000; 0 = never).
- **STOPPED** (the producer released its slots: device lost, the server's fog released) still puts the white view back,
  LOUD, as before; a late episode running then ends there. A still heartbeat after STOPPED is not counted as late.
- **A new map load in the same process** (the camera block's `gridSeq`, as the taps use it; a reload steps it twice)
  puts the white view back at once (not LOUD: `a new map load (load A -> B; N in this process)`), and only a mask whose
  `builtQpc` is later than the moment the load was seen is copied and swapped in (`the first mask built after the map
  load (generation G, built X ms after the load was seen; K older masks passed over) is copied`). Before, the old map's
  mask stayed on the new map's models until the late line fired (about 2 s), or until the next copy when it did not.
- `fow_models_stale_ms` keeps its meaning (2000 ms of still heartbeat at map frames), so episode counts before and after
  the fix are the same measure.

Tests (`shell/tests/test_fow_models.py`, stand-in host with the real producer): new
`test_late_heartbeat_keeps_the_last_mask` (the producer process suspended 3.5 s with `fow_models_stale_ms=800`,
`fow_models_late_loud_ms=2000`: one late line, one LOUD, the end line with a length of 2-8 s, no RESTORED and one
SWAPPED until the producer's STOPPED, every read-back good) and `test_new_map_load_puts_back_until_a_mask_built_after_it`
(test mode `reload`: the test world's load count steps 3 s after the swap: put back, older masks passed over, swapped
again with a later mask, no LOUD); the existing STOPPED cases unchanged. Plain 19/19 (2026-11-16 10:03, 304 s); MSVC ASan 19/19 (10:11, 306 s), no report.

### 6.4 Live: what the player sees, before and after [L, 2026-11-16 10:12-11:04 EST]

Setup: workstation (functional runs on a loaded machine, 4-7 other heavy jobs registered: indications, COMMON 23),
instance c5h-d (Goldberg, F:), save `C5H-VP8P-HUGE_0350` (c499d3c6...) copied as `C5H-0415-HUGE_0350` (removed at each
restore), the D33 product path as 0406 (`run_3308.py` via `run_0415.py`: ServerRenderer = 1, the composite with its
manifest, Overlay on, FOWLevel 3, particles on), client 2554 x 1411, Vibrant Textures present. Builds
(`build_0415.sh`): server.exe 573a33b2..., proxy bef61702..., x64 DLL 74588135... (the pin's release DLL); composite
**before** 202ace04... (main 6e8cd850), **after** 678ee432... (this branch). Each run: the probe, 30 AI turns, one reload.

**The probe** (`run_0415.py`): after the load settles, `UI.LookAt` 3957's p13_model camera (103, 37), then the
composite's presented-frame dump (`shell_composite.shot`: the back buffer at Present, not PrintWindow) before, 4.5 s into
and 6 s after a 6-s `SuspendThread` of the server's render thread `vp-render` (its id from render.log), the rest of the
server running: the same thing a decal-apply stall does to the mask's heartbeat. Models box 0339's (827, 425)-(1727,
985) (`dumps_0415.py`):

| Arm | Composite's lines in the suspension | Box mean before -> during | Pixels > 24 off the before dump, during / after | Picture |
|---|---|---|---|---|
| before | 2.3 s in: `LOUD: ... stale ... (episode 1)`, `RESTORED the white view`; `SWAPPED` 0.03 s after the resume | 79.0 -> **84.4** | **8.6 %** / 0.15 % | citadel, both pyramids, the monument and its stones fully lit (`vc-20261116-101754-workstation-0d88`) |
| after | 2.0 s in: `late (... late episode 1): the last mask stays bound`; `late episode 1 ended after 5999 ms` | 78.7 -> 78.7 | **0.21 %** / 0.2 % (sheep idle animation, river flow) | unchanged (`vc-20261116-104308-workstation-43f4`) |

So the player saw, before this ticket, every white-handle model on seen-not-visible plots (buildings, wonders, cities,
improvements; 9.1's list) jump to fully lit for the length of each server stall and back afterwards; after it nothing
changes on screen during a stall (the world picture is the server's last frame either way, the camera at rest).

**The turns and the reload**:

| Run | rc | AI-turn episodes (30 turns) | Reload | Composite LOUD (fow_models) |
|---|---|---|---|---|
| b1 before 10:12-10:37 | 0 | **0** | 1 stale episode 10:36:19.95, white 3.9 s, behind the fading load screen (`vc-20261116-103834-workstation-9c87`) | 2 (probe, reload) |
| a1 after 10:37-11:04 | 0 | **0** | `a new map load (load 3 -> 5)` 11:02:59.47, white view put back; late episode 2 (6.1 s, no mask bound, not LOUD); `the first mask built after the map load (generation 50776, built 8051.5 ms after the load was seen; 1 older masks passed over)` 11:03:07.57, SWAPPED (`vc-20261116-110500-workstation-6e26`) | **0** |

No AI-turn episode came in either 30-turn run: the machine was quieter than in 0406's run 1 (06:00-06:30, 14 episodes)
and the server's render-thread gaps stayed short (b1: 3 gaps over 1.8 s, 2 of them the probe and the reload; a1: 5).
The episodes need the stall, and the stall needs the loaded board (section 3.5, 6.2).
(b1's composite also logged `decal_tap: LOUD: the server refused generation 239 (ackResult 5)` at 10:36:09.5, during the
reload: not the model fog; not followed here.)

### 6.5 Particles on / off: three interleaved pairs on the fixed build [L, 2026-11-16 11:04-13:00 EST]

The same setup and builds (composite **after**), one run at a time on c5h-d (COMMON 16: one heavy job of one agent),
interleaved on / off; particles off = `C5_RENDER_FOW_PARTICLES=0` (TEST ONLY, 0340's control switch). Episodes counted
are the AI turns' late episodes (the probe's and the reload's left out); every one sat in a server render-thread gap.

| Pair | Run (EST) | Particles | Turns | AI-turn late episodes (length) | White periods | fow_models LOUD |
|---|---|---|---|---|---|---|
| 1 | a1 10:37-11:04 | on | 30 | 0 | 0 (the reload's put-back only) | 0 |
| 1 | a2 11:04-11:32 | off | 30 | 3 (5.0, 2.8, 2.6 s) | 0 | 0 |
| 2 | a3 11:33-11:50 | on | 20 | 0 | 0 | 0 |
| 2 | a4 11:51-12:09 | off | 20 | 0 | 0 | 0 |
| 3 | a5b 12:19-12:39 | on | 20 | 1 (11.4 s) | 0 | 0 |
| 3 | a6 12:40-12:59 | off | 20 | 0 | 0 | 0 |

On: 1 episode in 70 turns; off: 3 in 70. **The particles do not raise the rate here** [L, small counts: no rate
difference can be claimed either way]; with 0340's runs (on 2, 2, 3 in 30 turns each; off 0 in one run, 6.2) the
particles are not shown to matter. What the counts follow is the machine's load (0406's 06:00 run 14 in 30 turns;
these runs 0-3) and the game's own events: every natural episode dumped here (a2 x3, a5b x1) had an EXE popup on screen
(the Alcatraz wonder splash, a leader scene) when it began [L, the dumps; `vc-20261116-113341/113343/113344-workstation-*`,
`vc-20261116-124003-workstation-780f`] [I: the EXE loading the splash's or the leader scene's large textures on a
nearly full board is one more GPU memory event of the kind of section 3].
The first attempt of pair 3 (a5, 12:10) lost the game at the load: the server's fog install failed `no overlay pair yet
... 13657 ms after the first frame of the map` right after the server's own 12-s post-field stall (section 3); the MC
numbered it **0429** (not the model fog; logs `F:/c5-64bit-work/0415/a5/out`). The pair was run again (a5b).

**Episodes before and after, counted** (the ticket's measure): before the fix every episode was a LOUD line and a white
period (0406 run 1: 14 LOUD, 123 s white; b1: 2 LOUD, 7.9 s white, the probe and the reload); after it, over a1-a6 and a5b (140
turns, 1 reload, 6 probes): 11 late episodes (6 probes, 1 reload, 4 in AI turns), **0 fow_models LOUD lines, 0 white
periods in play** (the only white
period is the new-map-load put-back of a1's reload, behind the load screen, 8.1 s).

### 6.6 Fix proposal for the server's stalls (for the MC; render/ and server/ code, not this ticket's)

The model fog no longer shows the stalls; the stalls themselves remain (the world picture freezes on the last server
frame for their length; the EXE keeps running). Proposal, in order:
1. **Measure**: time each step on the render thread around a decal generation (`applied`, `applied by the reader`,
   `set generation ... in use`, the re-bake of the raised tiles) and around an EXE popup, each with DXGI
   `QueryVideoMemoryInfo` (budget and usage) of the server's adapter: the logged own times are milliseconds while the gaps
   between them are seconds, so the wait is in D3D calls between them (section 3's driver-thread signature).
2. If the waits are video-memory residency: make the decal set's and the re-bake's GPU allocations once and reuse them
   (the decal pass's vertex buffer already only grows), and keep the server's working set inside the budget the DXGI
   query reports (evict the residency's far chunks first), so a board shared with the EXE does not page.
3. 0429's install clock (counts wall time across the server's own stall).
Side observation, not followed: b1's reload logged `decal_tap: LOUD: the server refused generation 239 (ackResult 5)` /
render.log `an image was refused ... load 3 ... the block 7992336, 239, 5, 18852` at 10:36:09.5 (the tap's image from load 3
read during load 5).

### 6.7 Not verified

- The fix against 0406's original load (14 episodes in 30 turns): no run here met a load that heavy (0-3 natural
  episodes a run); the probe (a 6-s suspension of the render thread) stands in for it and gives the same composite
  state (heartbeat still, rest of the server running).
- A late episode longer than `fow_models_late_loud_ms` live (the LOUD path is tested on the stand-in only); the longest
  live one here was 11.4 s.
- The new-map-load rule with a **different** save loaded in the same process (the reload here was the same save, so the
  old mask would have been right by chance); the rule does not depend on it (tested on the stand-in with the test
  world's load count).
- A heartbeat still while the EXE presents no map frames (a full-screen screen) is still counted from the last map frame
  where it moved, as before the fix; such an episode is now only a counted line.
- A producer suspended inside its seqlock write: the late clock runs (the heartbeat is read outside the seqlock), the
  copy waits; not provoked live.
