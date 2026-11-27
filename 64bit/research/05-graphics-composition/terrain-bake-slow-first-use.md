# Slow single bakes at a load: the pipeline baker's steps and its warm-up (ticket 0417, 2026-11-16)

Ticket 0417 follows 0409 (`terrain-bake-residency-first-draw.md` 4): in 3 of 5 huge product-path loads one of the first
~30 bakes of the residency's catch-up took 1.7-9.7 s of CPU and held the load's first server frame 2.6-10.8 s; 0404's
logs had such bakes too (11.6 s, 2.7 s). The residency timed only the baker's call. This file holds what the pipeline
baker's steps showed, the warm-up that was tried, and the second stall found on the way (the render thread's log writes).

## 0. Result (2026-11-16)

- **The slow single bakes are a wait in the driver's `Flush`, under GPU memory pressure, not a first use and not disk
  reads [L].** Two slow bakes reproduced in 10 huge loads on c5h-e (F:), 5,657 ms and 3,065 ms: every pipeline step
  under 6 ms except the GPU timer's end (`ctx->Flush()` in the server's `GpuTiming::Deferred`): 5,647 and 3,061 ms. The
  files they read: 3 decal textures in 2 ms, and none. During the second, the board's dedicated memory was 8.54 GiB at the
  field and rose 8.58 -> 9.22 GiB, its shared (system) memory 523 -> 604 MiB, F: read 252 KB, CPU 82-95 %; the eight
  loads without a slow bake had 5.73-7.76 GiB at the field. The thread's own CPU in the bake was 15.6 ms in one (it
  waited) and 3,031 ms in the other (busy in the driver, kernel time [I]). This is 0407's mechanism
  (`fow-models-compile.md` 3.5: the server's first GPU work after the field waits 8.7-23.4 s in the NVIDIA driver when
  the board holds >= 9.9 GB), met at the first draw's bakes instead of the first frame: the bake that happens to flush
  when the video memory manager pages in the field's new allocations takes the wait. Section 5.3.
- **The warm-up does not prevent it** (both slow bakes were in warm-up loads) and was taken off by default
  (`render_bake_warm` = 0; the MC, 2026-11-16: no warm-up; the fix direction is 0407's: make the field's allocations
  resident under the load screen, its own ticket). It loads all 74 layer textures at init (183-374 ms), which adds to the
  board's memory; whether that made the two slow loads more likely cannot be told from 2 of 5 against 0 of 5 [I].
- **Second stall: the render thread's log lines waited on server.exe's stdout [L].** Every RNote line goes to render.log
  (0.1 ms) and then through `SrvLog` to stdout and server.log. Under the proxy server.exe runs with `CREATE_NO_WINDOW`:
  its stdout is a console without a window, and each write waited 57-2,512 ms for the console host on the loaded machine
  (server.log 0.1 ms, the lock 0.0 ms); the residency's field line took 2.5 s between the field and the first draw.
  Fixed: `SrvLog` (server/src/srv_main.cpp) skips a stdout that is a console without a window (said once in server.log);
  pipes, files and consoles with a window are written as before (`server/tests/run_stdout_console_test.py`).
- **On c5h-b (C:, NVMe) no slow bake in 5 loads**; first uses cost 5-30 ms per bake there and on F:.
- Instrumentation kept: the pipeline's per-bake step times and thread CPU (`lastSteps()`), `pipeline: slow bake` and
  `pipeline: first use` lines with the files read (render.log only), the residency's slow-bake line with thread CPU and
  page faults, the first draw's split line, and slow log-write lines in render.log and server.log.
- **On a smaller board with one game, nothing pages (0428, 2026-11-18) [L].** The laptop's Quadro P3000 (6 GiB) ran 5 huge
  product-path loads with no slow bake, no wait at the first GPU work and a peak of 2.05 GiB. So the paging needs a board
  that other programs fill, as on the workstation. The "resident under the load screen" fix was not made (section 6).

## 1. What the pipeline does on first use [C]

`c5r::bake::PipelineBaker` (`render/src/bake/pipeline.cpp`) makes several things on their first use inside a bake, not
at `init`:

| first use | made in | what it costs |
|---|---|---|
| a layer texture (diffuse, height, spec of the layer's TerrainDef set) | `BlendTextureCache::bind` (`render/src/bakeblend/blendpass.cpp`) | `gamefs` read of the DDS (loose file or pack), parse, `CreateTexture2D` + view; kept per name |
| a style's inputs (Sunlight / Shadowlight cubes, ColorKey LUT) | `loadStyleInputs` (`render/src/bakepost/style.cpp`) | three file reads, cube and 3D texture creation; kept per style |
| a height page (1024 x 1024 samples, 11 mips) | `ExePages::page` (pipeline.cpp, 3406's `buildHeightPage`) | CPU build of the page and its mips, `CreateTexture2D` with init data; kept while the grid stays |
| a decal texture | `DecalPass` (`render/src/decal/decal_pass.cpp`) | as a layer texture; kept per name |
| a pass's first draw on the device (blend, NormalCalculate, macro shadow, CacheBake with one or two targets, CompressBC, decal) | the first bake that uses it | whatever the driver defers from `Create*Shader` to the first draw [I] |

The shaders themselves are created at `init` (`BlendPass::init`, `BakePost::init`, `DecalPass::init`).

## 2. The step times and the log lines (0417) [C]

Every bake's CPU time is now split into steps (`PipelineBaker::lastSteps()`, `PipelineStepTimes`, steady clock): prep
(checks, layer list, window, style, slope classes, blend vertices and indices), page, own block, textures (with the
count loaded), blend pass, normals, style (loaded or not), constants, post passes (with decal textures loaded), copy
into the array slices, the GPU timer's end (Deferred in the server: a flush and a non-blocking collect). With
`GpuTiming::Deferred` nothing waits for the GPU, so a GPU stall shows as a slow D3D11 call inside the step that made it.

`PipelineConfig::log` (the server gives `RLog`: render.log only, since build 197dea5e; before that the residency's log,
render.log and server.log, whose server.log part could take seconds, 5.2):
- `pipeline: slow bake: tile (x, y) size S, style s, N layers, t8 0|1, decals D, total T ms: prep a, page b (built), own c,
  textures d (k loaded), blend e, normals f, style g (loaded), constants h, post i (k decal textures loaded), copy j,
  gpu timer k; thread CPU t; files n, K KB, read r ms, create c ms: name K KB r+c ms, ...` (the layer and decal texture
  files the bake read, the slowest 6) for a bake of `slowBakeMs` (500) or more, the first 32 of the process;
- `pipeline: first use: ...` (same form) for the first `firstUseLogs` (64) bakes that loaded a texture or a style or built
  a height page;
- `pipeline: warm-up at init done in T ms: K layer textures in a ms, 5 styles in b ms, 11 throwaway bakes in c ms, GPU
  wait d ms` (section 3).

Counters `slowBakes()`, `firstUseBakes()`.

## 3. The warm-up (0417) [C]

`PipelineBaker::warm(tables, sizeClass)`, run at the end of `init` when `PipelineConfig::warm = WarmUp::AtInit`:
1. every layer texture of the tables (each layer id's set: diffuse, height map, spec map), eight layers per `bind`; a
   name the install lacks is recorded as missing (the EXE's handle 0), as a bake would;
2. the five styles' cubes and LUT;
3. 11 throwaway bakes on a 3 x 3 map of its own (materials 0 and 1, so the middle tile has two layers) at `sizeClass`:
   each style with t8 off and on, then one with a quad decal over the tile (the decal pass's default texture, no file);
   the map is dropped after them (a bake before the next `setMap` fails NotFound) and the height pages of that map are
   forgotten;
4. a wait (event query, at most 10 s) until the GPU has finished them.
A failing step is recorded (`warmReport()`, logged) and the next step runs; the warm-up never fails `init`.

The server can run it at the residency's init, at server start (on the product path, minutes before the first load's
field), with the install's TerrainDef tables and the residency's size class: option `render_bake_warm` (server.cfg; 0
default since build 0dbc3634, as before 0417; 1 = the warm-up, a test arm; on the product path through vpproxy.ini
`server_cfg_extra=`, issue 0307, TEST ONLY). Builds 1fff44f2-2c0d009a had 1 as the default (series 1-3). Height pages
depend on the map and are still built by their first bake. Off by default because it does not prevent the slow bakes
(section 0) and costs board memory (74 layer textures).
*Beside it since 2026-11-17 (issue 0371): `render_bake_near` (server.cfg; parsed and recorded with `render_bake_warm`;
1 = default on branch `task-0371-near-rgba8-caches-part2`: the EXE's near-camera RGBA8 pairs, their state-5 bakes and the
draw from them; 0 = every tile from its BC1 pair, the picture before 0371). What it does and its checks:
terrain-bake-passes.md 3.5.4.*

Unit-test figures (2026-11-16, Steam install on C: with Vibrant Textures, RTX 4080, the loaded workstation, plain
Release; indications, not timing results): warm-up 389 ms (74 layer texture names in 219 ms, 5 styles 56 ms, 11 bakes
114 ms, GPU wait 0.2 ms). Without the warm-up the first bake of a two-layer tile took 59.9 ms: textures 19.4 ms (4
loaded), page 5.5 ms (built), style 2.3 ms (loaded), blend 0.4, post 2.0, GPU timer 28.3 ms (Blocking in the test).

## 4. Tests [C]

`render/tests/bake/bake_test.cpp` (c5r_bake_test):

| test | what it shows |
|---|---|
| `game_step_times_and_first_use_lines` | the first bake of a tile makes its first uses (>= 2 textures, the page, the style), its steps add up to 50-100 % of its total, one first-use line; the same tile and a tile of the same page and layers make none and log nothing; slowBakeMs 0: 33 bakes counted, 32 lines (the cap), firstUseLogs 0 logs none; a failed bake (tile outside the map) keeps its steps up to the failure and its line names `failed: NotFound` |
| `game_warm_at_init_takes_the_first_uses_out_of_the_bakes` | init with `WarmUp::AtInit`: every texture name of the tables bound, 5 styles, 11 bakes, a decal drawn, one report line, nothing counted; a bake before `setMap` fails NotFound; then 30 bakes (3 tiles x 5 styles x t8 0/1) load no texture and no style, build the one height page once, and equal a baker without the warm-up byte for byte (t0 and t8); the baker without it counts >= 5 first uses |
| `game_negative_warm_up` | warm before init NotFound and at size class 100 BadLayout (nothing loaded); no tables: styles only, again: none; a missing texture name recorded as missing, no failure; a layer without a set: BadLayout returned, styles loaded, no bake, no map; `WarmUp::AtInit` at size class 100: init Ok, report BadLayout, the log line, the baker bakes |

`render/tests/bakeresidency/residency_test.cpp`: `slow_bake_is_logged_and_counted` also checks the thread CPU (the stand-in's
520 ms busy wait shows >= 100 ms) and the page faults in the slow-bake line; `catch_up_first_draw_bakes_every_visible_tile`
checks the split line.

`server/tests/render/test_residency_source.py`: `render_bake_warm` other than 0 / 1 refused (exit 2);
`test_pipeline_bakes_a_real_tap[default|1]`: by default no warm-up and texture or style loads in the first-use lines;
with 1 the warm-up line and page-only first-use lines.

Results on the final tree (fda19ebf, 2026-11-16 10:49): c5r_bake_test + c5r_bakeresidency_test plain passed, 0 skipped;
MSVC ASan passed, 0 skipped, 0 ASan reports, peak 1,302 MB; server render tests (test_residency_source.py) 34 passed;
run_stdout_console_test.py 4/4 on the final server.exe and 3/4 on the pin's (x64-04b1211b-pfdee656: the hidden arm has
no skip line), as it should. The GPU and disk loggers: `render/evidence/0417/gpu_log_0417.py`.

`server/tests/run_stdout_console_test.py` (server.exe as run_preinit_report_test's "lua" case, four arms): stdout a file
or a pipe gets the lines, a console without a window (`CREATE_NO_WINDOW`, no handles, as the proxy starts it) is skipped
with one line in server.log, a console with a window (on a hidden desktop) is written; exit 1 and the exit line in
server.log in every arm.

## 5. Live: huge product-path loads [L, 2026-11-16]

**Setup.** As 0409 (`terrain-bake-residency-first-draw.md` 4) with the driver copied as
`render/evidence/0417/run_0417.py` (`--warm 0|1`), on instance **c5h-b** (Goldberg, on C:, the NVMe SSD; 0409 ran on
c5h-f on F:, the USB SSD), save `C5H-VP8P-HUGE_0350` copied as `C5H-0417P-HUGE_0350`, the D33 product path
(ServerRenderer = 1, composite with manifest, Overlay on), x64 DLL 74588135 (pin release). The workstation was loaded
(13 registered heavy jobs, 7 other game instances): durations are indications, not timing results (COMMON 23).

### 5.1 Series 1 (build 1fff44f2: step times, first-use and slow-bake lines, warm-up)

Two loads before the series was stopped for more instrumentation (server.exe 5a52e854..., proxy 5274c0ec...,
composite b9df4295...). w1's raw folder was overwritten by series 2 (the lines below were read before that); c1 is in
`F:/c5-64bit-work/0417/s1-c1`.

| load | warm-up at init | first draw (77 bakes) | residency slow bakes in it | first server frame after the click |
|---|---|---|---|---|
| w1 (warm 1) | 316.7 ms: 74 layer textures 251.1 ms, 5 styles 8.6 ms, 11 bakes 56.4 ms, GPU wait 0.6 ms | 3,138 ms | tile (105, 31) 746.1 ms (bake 52) | +17.9 s |
| c1 (warm 0) | none | 550 ms | none | +7.4 s |

- **w1's slow bake was not the pipeline's work.** The pipeline's own line for that bake: `tile (105, 31) size 256,
  style 3, 3 layers, t8 1, decals 13, total 7.7 ms` (prep 1.7, textures 0, post 4.5, 3 decal textures loaded), logged at
  08:37:20.074; the residency's slow-bake line (746.1 ms for the same call) at 08:37:20.813. The 738 ms went between the
  pipeline's last step and the residency's clock: the pipeline's own log write (RNote: render.log, then server.log and
  server.exe's stdout under one lock), the server's decal wrapper after the bake (`decalTileBaked`), or the thread not
  running. Three more slow bakes of 506-605 ms followed in the bake-ahead, each right after a first-use line of 5-6 ms.
- **c1's first uses were cheap on this instance**: 64 bakes with a first use, 5-30 ms each; layer textures 9-14 ms for
  2-6 loads, height pages 6-10 ms, a style 2 ms, the first decal bake 19 decal textures in 7-26 ms.

So the warm-up takes the texture and style loads out of the bakes (w1: every first-use line had `textures 0.0 (0
loaded)`, style 0), but on this instance those cost milliseconds, and the slow bake of w1 came from elsewhere. Series 2
adds the thread's CPU time and the page faults to the residency's slow-bake line, a split line for the first draw
(`residency: first draw of load N split: baker calls W ms (thread CPU C ms, F page faults), the rest of the bakes, the
rest of the draw`), the pipeline's thread CPU per bake, and a `render log: the line before took N ms to write (waiting
for the log lock, render.log, server.log and stdout)` line for any log write of 50 ms or more.

### 5.2 Series 2 (build f04a54c7, c5h-b, 08:57-09:19 EST)

server.exe 77aeadce..., proxy 3023ed5f..., composite d6b98e3b.... Stopped after four loads (ABBA w1 c1 c2 w2; w3
aborted by the stop file) to move to an F: instance; c5h-b restored (backup ok, verify ok) and released. Raw data
`F:/c5-64bit-work/0417/b-*`; table by `render/evidence/0417/table_0417.py`.

| load | warm-up | first draw | its split: baker calls / thread CPU / page faults | slow bakes | log writes >= 50 ms (max) | first server frame |
|---|---|---|---|---|---|---|
| w1 | 239 ms | 458 ms | 440 / 313 ms / 2,850 | 0 | 20 (311 ms) | +9.5 s |
| c1 | - | 537 ms | 522 / 344 ms / 17,882 | 0 | 2 (89 ms) | +6.7 s |
| c2 | - | 537 ms | 524 / 313 ms / 18,465 | 0 | 2 (92 ms) | +6.2 s |
| w2 | 324 ms | 797 ms | 779 / 438 ms / 7,473 | 0 | 32, the cap (2,512 ms) | +19.8 s |

With s1-c1 (550 ms, no slow bake): **no slow bake in 5 loads on c5h-b** (on C:), against 3 of 5 on c5h-f (on F:) in 0409.
The first draw's time is the baker's calls (the rest of the bakes 2-3 ms, the rest of the draw 10-16 ms); the thread ran
for 60-70 % of it (scheduler granularity 15.6 ms). The warm-up cuts the page faults of the first draw from 18,000 to
3,000-7,500 (the layer textures' memory is touched at init instead) but on this instance the draws were not shorter in a
way these four loads can show (458 and 797 ms against 537 and 537 ms).

**The render thread's log writes are slow during a load [L].** Every line the render thread logs with RNote goes to
render.log (0.0-0.1 ms in every case) and then, through the host's log function, to server.log and server.exe's stdout
under the server's log lock (`SrvLog`, server/src/srv_main.cpp). That second part took 57-311 ms per line (20 times) in
w1 and up to **2,512 ms** in w2: the residency's own field line (`residency: field 128x84 tiles ...`, written in
`onField` between the field and the first draw) took 2.5 s, the tile-styles line after it 0.7 s, the water tap's line
2.0 s, all on the render thread on the way to the first frame. Series 1's w1 "slow bake" of 746 ms was such a write: the
pipeline's first-use line inside the bake (its own steps 7.7 ms) went through the same path. Since build 197dea5e the
pipeline's lines go to render.log only (`RLog`). Which part of `SrvLog` waits (its lock, the stdout write to the hidden
console that `CREATE_NO_WINDOW` gives server.exe, or server.log) is timed from build 197dea5e+ by a `log: the line
before took N ms (waiting for the lock, stdout, server.log; thread)` line in server.log for every write of 50 ms or more.
*Changed 2026-11-22 (ticket 5054, W75): server.exe no longer writes a log line on the producing thread. RLog, RNote and
SrvLog queue the line and one log writer thread writes render.log, server.log and stdout; the two "the line before took"
lines are no longer written while that writer runs. The writer writes `log writer: a write of N lines took M ms` instead
(the disk's wait). Design and tests:
[unit-combat-server.md](unit-combat-server.md) 18.27 (c) and 18.27.1.*

### 5.3 Series 3 (build 2c0d009a, c5h-e on F:, 09:27-10:15 EST)

server.exe 7c9aceac..., proxy f5468420..., composite 3d590d76...; 10 loads in ABBA order (c1 w1 w2 c2 c3 w3 w4 c4 c5
w5); GPU memory and disk logged from w2 on (`render/evidence/0417/gpu_log_0417.py`, 0407's commands: nvidia-smi every
200 ms, typeperf every 1 s; `F:/c5-64bit-work/0417/gpu3/`); instance restored (backup ok, verify ok). Tables:
`render/evidence/0417/table_0417.py` and `gpu_0417.py` (output in `render/evidence/0417/series3.txt`).

| load | warm-up | board dedicated at the field | first draw | split: baker / thread CPU / page faults | slow bake in it | first server frame |
|---|---|---|---|---|---|---|
| c1 | - | (not logged) | 508 ms | 491 / 359 / 17,950 | - | +7.1 s |
| w1 | 374 ms | (not logged) | **6,143 ms** | 6,115 / 344 / 16,382 | (105, 28) **5,657 ms**: gpu timer 5,647, thread CPU 15.6 | +13.9 s |
| w2 | 307 ms | 8.54 GiB | **3,419 ms** | 3,397 / 3,359 / 14,521 | (109, 29) **3,065 ms**: gpu timer 3,061, thread CPU 3,031 | +10.5 s |
| c2 | - | 6.19 GiB | 339 ms | 329 / 313 / 18,607 | - | +6.3 s |
| c3 | - | 5.81 GiB | 339 ms | 330 / 313 / 18,151 | - | +5.4 s |
| w3 | 184 ms | 5.73 GiB | 274 ms | 262 / 250 / 4,790 | - | +5.6 s |
| w4 | 198 ms | 6.71 GiB | 318 ms | 304 / 297 / 13,840 | - | +7.1 s |
| c4 | - | 6.04 GiB | 341 ms | 329 / 297 / 18,617 | - | +5.7 s |
| c5 | - | 6.68 GiB | 421 ms | 409 / 328 / 22,961 | - | +6.3 s |
| w5 | 357 ms | 7.76 GiB | 819 ms | 800 / 750 / 13,913 | - | +6.7 s |

- Both slow bakes: every step under 6 ms but the GPU timer's end (`ctx->Flush()`, Deferred). w1's bake read 3 decal
  textures (`landmark_decal_afr_d/_s/_h.dds`, 150 KB) in 2.0 ms; w2's read none. w2's window (typeperf, the adapter with
  the larger dedicated usage): dedicated 8.58 -> 9.22 GiB, shared 523 -> 604 MiB, F: reads 252 KB (read latency at most
  1.8 ms), CPU 82-95 %. Not the disk.
- The first draw's time is the baker's calls (the rest 1.4-14.1 ms, the rest of the draw 7-17 ms).
- The two slow loads were the first two warm-up loads, early in the series while the machine was busier (w2's board at
  8.54 GiB against 5.73-7.76 GiB later). The warm-up's own cost on the board (74 layer textures) is real but these
  numbers cannot separate it from the time of the run [I]. The warm-up cut the first draw's page faults from
  17,950-22,961 to 4,790-16,382 and the first-use lines in the first draw from 13-18 to 6-15.
- SrvLog's slow lines (>= 50 ms) in the ten server.logs: 7, 55.7-139.1 ms (median 68.1), all in the stdout part
  (server.log at most 0.1 ms; one waited 94.9 ms for the lock, held by another thread's slow stdout write). The machine
  was less loaded than in series 2.

Over the 17 loads with the step timer (series 1-3): 2 slow bakes in the first draw, both in the driver's Flush, both on
F: with the board above 8.5 GiB (one not logged); none on C: (5 loads). With 0409's 3 of 5 on c5h-f (F:), the F:
instances are the ones where it was seen; the disk itself is not what the bakes wait for (no file reads in them), so the
link to F: is the instances' load, not their drive [I].

### 5.4 Series 4: the final build (fda19ebf, c5h-e, 10:23-10:38 EST)

server.exe dcbc8461..., proxy 1595c450..., composite 06edf96b... (main merged at fda19ebf, so the load path carries
main's changes of 2026-11-16 too); `render_bake_warm=0` (the default); c5h-e aligned (`provision.py align548`, MC #2549)
and verified before, restored after (backup ok, verify ok), released.

| load | board dedicated at the field | first draw | split: baker / thread CPU / page faults | slow bakes | server.log: skip line / slow lines | render.log slow writes | first server frame |
|---|---|---|---|---|---|---|---|
| v1 | 7.44 GiB | 233 ms | 221 / 203 / 2,139 | 0 | 1 / 0 | 0 | +0.48 s |
| v2 | 7.63 GiB | 220 ms | 213 / 172 / 2,235 | 0 | 1 / 0 | 0 | +0.49 s |
| v3 | 7.27 GiB | 216 ms | 206 / 219 / 2,431 | 0 | 1 / 0 | 0 | +0.46 s |

`log: stdout is a console without a window (CREATE_NO_WINDOW): lines go to server.log only (issue 0417)` once per launch,
no `log: the line before took` line in server.log and no `render log: the line before took` line in render.log in the
three launches (series 2: 2-32 per launch, up to 2,512 ms; series 3: 0-1 render.log lines per launch, the machine less
loaded). The first server frame now comes 0.46-0.49 s after the click (0409: 6.4-18.8 s; series 3: 5.4-13.9 s): the field
is ready earlier with ticket 0408 (merged to main 09:32, before this branch's merge; not this ticket's), so this series does not
measure this ticket's effect on the first frame [I]. The first draw's page faults fell from ~18,000 to ~2,200 for the
same reason [I] (the field's memory is touched before the first draw).

## 6. The smaller board: the laptop's Quadro P3000 (ticket 0428, 2026-11-17)

Ticket 0428 asked whether the GPU-memory paging of section 0 and 0407 (`fow-models-compile.md` 3.5: a stall only with
the 16 GB board at 9.9 GB or more) also hits a player with a smaller board, on a huge map, with one game running. If it
did, the fix was to make the field's GPU allocations resident under the load screen. If (1) showed no paging, the ticket
was to stop at (1).

### 6.1 Setup [C]

- **Board.** The laptop's NVIDIA Quadro P3000 has **6,144 MiB** of dedicated memory (nvidia-smi, 2026-11-17 [L]).
  *Correction:* `06-tooling-and-workflows/quiet-testbed.md` 21.4 says "4 GB". That figure is WMI's `AdapterRAM`
  (4,293,918,720), a 32-bit field that saturates at 4 GiB; nvidia-smi's `memory.total` is 6144 MiB. Drivers (D175):
  NVIDIA 573.22 = 32.0.15.7322, Intel HD Graphics 630 27.20.100.9320 (the panel's adapter). The server picks the Quadro
  ("largest dedicated video memory").
- **Job.** `render/evidence/0428/loads_0428.py` is a testbed job (`--headed --quiet`) on the laptop's instance `tb1`.
  It uses `product_path.py`'s prep, stop and restore unchanged: the D33 product path with Overlay on, ServerRenderer = 1,
  the composite and its manifest, and the save copy `TBPP-HUGE_0350` of `TB-HUGE_0350` (c499d3c6..., turn 350, the
  workstation's `C5H-VP8P-HUGE_0350`). For each load: launch, `frontend.py ingame` polled until the load screen's button
  shows, then `frontend.py dismiss` at once, then a wait for the residency's `bake-ahead complete`, then the logs, then
  the harness's stop. 0407's loggers run beside the loads: nvidia-smi every 200 ms and typeperf every 1 s (`GPU Adapter
  Memory(*)` dedicated and shared usage per adapter, CPU, commit). The poll is coarse: the Lua channel is silent while
  the game loads, so each `ingame` poll then takes 10.3 s, and the click comes up to about 10 s after the button shows.
- **Builds.** The pin `x64-4f4daac7-pf8e9f42` (server.exe `ed829bda...`, proxy `4815ed81...`, x64 DLL `2d737bd5...`;
  COMMON 18) and the composite `ee3fdfbf...` built from main 1d502008e (`render/evidence/0428/build_pp_0428.sh`).
- **Table.** `render/evidence/0428/analyse_0428.py <fetched job>`. Raw data: `F:/c5-64bit-work/0428/laptop-<job>/`.

### 6.2 Five loads without paging [L, jobs `20261117-202351-7fdf` (load 1, 20:24-20:28 EST) and `20261117-203951-fd34` (loads 2-5, 2026-11-18 00:56-01:13 EST)]

The crashed load of 6.3 is not counted. Times are relative to the click (the dismissal's return). The table comes from
`render/evidence/0428/series1.txt` (`analyse_0428.py`).

| load | field | first draw (under the load screen) | its bakes' ms (baker, thread CPU, page faults) | slow bakes (>= 500 ms) | first GPU work's wait (fog-mask copy) | server's longest quiet span, field +60 s | EXE's longest frame gap, 60 s | Quadro dedicated at the field / max (GiB of 6) | shared at the field + rise (GiB) | bake-ahead |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | -5.7 s | -0.4 s | 260.0 (245.7, 250.0, 5,146) | 0 | none | 1.03 s | 0.43 s | 0.87 / 2.05 | 0.06 + 0.14 | 35.5 s |
| 2 | -6.5 s | -0.9 s | 261.9 (252.2, 250.0, 2,098) | 0 | none | 1.02 s | 0.58 s | 0.90 / 2.05 | 0.06 + 0.04 | 41.7 s |
| 3 | -6.0 s | -0.7 s | 259.3 (249.6, 218.8, 2,097) | 0 | none | 1.03 s | 0.43 s | 0.84 / 2.05 | 0.06 + 0.04 | 40.1 s |
| 4 | -6.3 s | -0.7 s | 247.5 (237.6, 234.4, 2,095) | 0 | none | 1.27 s | 0.40 s | 0.89 / 2.05 | 0.07 + 0.13 | 39.7 s |
| 5 | -5.7 s | -0.3 s | 239.3 (227.5, 187.5, 2,212) | 0 | none | 1.06 s | 0.22 s | 0.88 / 2.05 | 0.06 + 0.14 | 39.2 s |

- In every load the field came 5.7-6.5 s before the click, and the first draw came under the load screen, 0.3-0.9 s
  before the click, with 239-262 ms of bakes. The 0408 order (field before the click) holds on the laptop too.
- No load had a slow bake (no residency or pipeline slow-bake line). The fog-mask slots' first copy was handed over in
  the same frame (no "has not signalled" poll). The server's 1 s lines kept their period (the longest gap 1.27 s, against
  0407's 8.7-20.1 s stalls). The EXE's longest frame gap was 0.22-0.58 s.
- The board: the EXE and the server together peaked at **2.05 GiB of 6 GiB** dedicated in every load (0.84-0.90 GiB at
  the field). Shared (system) memory rose by 0.04-0.14 GiB. Every load reached the same final size: 4,922 tiles, 410.1 MB
  of caches, 415.9 MB of arrays.
- **Answer to 0428 (1): no paging on the smaller board** [L, 5 of 5 loads]. A huge product-path game alone takes about
  a third of a 6 GiB board. 0407's stall needs the board nearly full, which on the workstation came from 10+ other
  clients' memory. So the ticket stops at (1), as its acceptance says: the field's allocations were not moved to the
  load screen, and no server or residency code changed.
- Not measured [I]: a 4 GB board or smaller. The laptop has none. Going by these numbers (2.05 GiB at the peak, one
  game) a 4 GB board would hold the huge map without paging, but a 2 GB board would not. A board that other programs
  fill (a browser, a second game) is the workstation's case of 0407.
- Of 6 loads started, 1 crashed in the EXE before the field (6.3, bug 0515): 1 crash in 6 loads on these pins and
  this instance.

### 6.3 Load 2: an EXE crash in the load, before the field (the 0410 family) [L]

Load 2 of the same job (EXE pid 12200, launched 20:28:41) crashed at **20:31:02.039**. The exception was 0xC0000005 in
`CivilizationV_DX11.exe` itself: RVA 0x136580, **VA 0x536580** (module base 0x00E40000), a read of 0x51B82000 (a page
start) on the EXE's main thread. The crash records are: the proxy's `exe-crash.txt` and the LOUD line in `vpproxy.log`
(0419), Goldberg's `crashes.txt` (@0x00F76580), and WER APPCRASH with exception offset 0x00136580. WER's "Close
program" box stayed open; the dialog watcher marked it at 20:31:12, kind `unknown`, and the job ended with exit 8.
**0x536580 is 0x40 into 0x536540**, the farm-field layout graph's endpoint remove-by-index
(`02-exe-internals/crashes-huge-load.md` section 2). [I] That it lies inside that function's body; it was not
disassembled here. The crash came in the load gap after `StartupGame`, the gap 0410's crash (run5nd) also came in
(`vpproxy.log` 67.7 s: the view-mask watcher saw GameViewState's bits again; the crash at 77.1 s). At that moment the
server had no field (terrain HIDDEN, no terrain tap READY; only the cut tap's generation 1 applied, at 66.1 s), so 0428's
GPU path was not running yet. The server logged the peer as hung at 82.1 s. The box was closed on the MC's word
(#4721) by recovery job `20261117-203535-8cfa` (`render/evidence/0428/recover_0428.py`), which first copied the evidence
off the instance, then closed the box, stopped the game and restored the instance by hash (dismiss, stop and restore all
rc 0). Evidence: `F:/c5-64bit-work/0428/laptop-8cfa/out/crash/`. **Root cause (COMMON 30, D185):** bug 0515,
`02-exe-internals/crashes-huge-load.md` section 8. The candidate cause is a double free: the node delete frees a closed
loop's endpoint twice, and the free list then names that entry for good. The code chain is [C] link by link; that the farm
layout deletes such a loop in a load is [I], and a dump or a live trace of the next crash would confirm it. Rate there:
product 3 of 57 loads, stock 0 of 24.

## 7. Decal textures read off the render thread (ticket 0642, 2026-11-23)

Ticket 0642 (epic 0686 P5 b, D241: no file read on the render thread). Found by 0641 (`city-renderer.md` 16.1): on a
loaded machine one bake of tile (104, 34) read 21 decal textures for 4,835 ms on the server's render thread.

### 7.1 Where the reads were [C]

- `c5r::decal::DecalPass::draw` (`render/src/decal/decal_pass.cpp`, step 2) read, parsed and created each decal texture
  on its first use, inside the bake, on the caller's thread, and kept it per lower-case name (section 1's table). Nothing
  read the names ahead of the bakes.
- 0641's raw render.log (`F:/c5-64bit-work/0641/raw/render.log` lines 1800-1974): the field at 11:38:56.797; the
  bake-ahead waited 2,000 ms for the load's first decal set (0365) and resumed without it; the set (generation 1, 13,322
  decals, **245 names**) arrived at 11:39:03.112 and raised 2,951 tiles; the first bake with decals after it read 21
  of the names: `post 4845.2 (21 decal textures loaded) ... read 4835.2 ms` (riverbank_d_endcap1.dds alone 1,215 ms,
  683 KB). The next re-bakes read more names (540, 677, 229 ms). A set's names are all known when the set is applied;
  every later bake only needs some of them.
- The layer textures (`BlendTextureCache::bind`, `render/src/bakeblend/blendpass.cpp`) and a style's inputs
  (`render/src/bakepost/style.cpp`) are first-use reads in a bake too (same log: 1-19 ms per bake on F: before the load
  peaked). Not changed by 0642; measured in 7.4 (the MC's word, #11075: a follow-up ticket if any is above 100 ms on a
  loaded disk).
- The 779.5 ms render.log write on the same thread (0641) is ticket 5054's: since 5054 (merged 2026-11-23, `7b4c110e7`)
  render.log, server.log and the TEST files are written by a log thread; a line keeps the render thread's time stamp
  (`srv_render_common.cpp` EmitBody).

### 7.2 The loader thread (DecalPass) [C]

`c5r/decal/decal.h`: `startLoader()`, `stopLoader()`, `request(names)`, `adopt()`, `ready(names)`, `loaderPending()`,
`loaderCounters()`, `loaderLoads()`. One thread per pass reads each requested name through `gamefs::GameFiles` (const
reads: packs are file mappings, loose files `CreateFileW` per read; no shared mutable state, so two threads may read),
parses it and creates the texture and its two views with the same code as the draw (`createFromDds`), on the
free-threaded device. On a device created `D3D11_CREATE_DEVICE_SINGLETHREADED` it keeps the parsed file and the texture is
created at `adopt()`, on the caller's thread. The draw adopts what the loader finished, waits for a name in flight
(`waits`, `waitMs`), and reads a name itself only when it was never requested or failed on the loader (`syncLoads`; the
failure then shows as before 0642: NotFound, `missingTextures`). `stopLoader` drops queued names (a later draw reads
them); `init` and the destructor stop it. The server's device is created without SINGLETHREADED
(`srv_render_host.cpp` 908-909).

### 7.3 The residency source's modes (`srv_residency_source.h`, `ResidencySourceConfig::decalLoad`) [C]

`C5_RENDER_DECAL_LOADER` (environment, read at construction, as `C5_RENDER_HELD_BAKE`) unless the config names a mode:
- **2, hold (the product's default).** `setDecals` resolves and checks the names as before, then requests every name from
  the loader. When all are in, the set is used at once, as before 0642. Otherwise the set is held: the set in force stays,
  every tile keeps its last bake, and the `decalChanges` that follow it are queued; the first `beforeDraw`, `idle`,
  `pending` or `settle` after the loader has every name uses the set and then applies the queued changes, whose raised
  tiles are re-baked with it (P13 kept). A newer set replaces a held one (`setsHeldReplaced`). `pending()` is true while a
  set is held, so the host polls again soon. The bake-ahead's wait for the load's first set (0365) ends when the set is
  used, or at its 2,000 ms bound as before. Lines: `decals: set generation G (load L) held: N of its M names are still
  loading off the render thread`, then `decals: set generation G used after T ms held: ... (loader so far: ...)`.
- **1, wait.** `setDecals` waits (at most 60 s) for the loader, then uses the set: the order of sets and frames of mode 0,
  without reads in the bakes. The offline player (`server/tools/sceneshot/sceneshot.cpp`) runs it, so a replay keeps its
  recording's order (rule A unchanged).
- **0, sync.** No loader: the reads in the bakes (before 0642).
- The stand-in baker and `render_decals=0` have no decal pass: nothing is loaded or held.
Counters (`DecalBakeCounters`): `setsHeld`, `setsHeldReplaced`, `setsAppliedAfterHold`, `heldMsMax`, `heldMsSum`,
`setsWaited`, `waitedMsMax`, `heldChangeBatches`; the pass's `DecalLoaderCounters` through `decalLoaderCounters()`. They are
not on the per-second terrain line (it is cut at about 1,550 characters).

What a player sees: while a set is held, the decals of the generation appear a little later (the loader's read time of
the new names; 0 when every name was read before), instead of the whole world standing still for that time.

### 7.4 Tests and measurements

Offline (2026-11-23, workstation, `[L]`):
- `render/tests/decal/decal_test.cpp`: `game_loader_draws_bit_for_bit_what_the_draws_own_reads_draw` (six synthetic
  A8R8G8B8 files and five of the install's decal textures, riverbank_d_endcap1, mine_generic_decal_s/_h, grey,
  riverbank_d: no loader, loader before the draw, loader during the draw, loader on a SINGLETHREADED device: the targets
  equal byte for byte; the loader arms read nothing in the draw) and `game_loader_fallbacks_and_negatives` (before init
  and without files NotFound, a second start, a name queued once in any case, a missing name failed on the loader and
  NotFound in the draw, a name never requested read by the draw, stop with 200 names queued, the destructor with the
  loader running). c5r_decal_test: 15 run, 0 failed checks.
- `server/tests/render/test_decal_wiring.py::test_decal_loader_modes_draw_the_same_frame` (render_host_test.exe, the
  pipeline baker on the install `C5R_GAME_DIR` = pure_vp 4390913 and 3401's Tiny tap `C5R_TINY_TAP`, one 4 x 4 tile decal
  with the road textures around a land spot near the map's centre, uv 0..1, the camera on it, the 20th published frame
  dumped): modes 0, 1 and 2 give the same frame by SHA-256 (`e6504cf7...`), `render_decals=0` another (the decal is in
  the frame); mode 0's first-use lines show the bakes loading the decal textures, modes 1 and 2 none. In mode 2 the set
  arrived with the field, was held 9.2 ms (`2 of its 2 names are still loading`) and used before the first bake that
  needed it. The rest of test_decal_wiring.py and test_residency_source.py: 58 passed with the change.

### 7.5 Before and after on oldpc (job `20261123-022757-1456`, 2026-11-23 02:28-03:37 EST) [L]

Setup: `render/evidence/0642/job_0642.py` (driver; `analyse` makes `summary_0642.txt`), oldpc's quiet place with a
window (`--headed --quiet`; the quiet proof says alone, `undisturbed=False`, foreign cores 0.107), instance tb1 on E:
(SATA SSD), EXE build 4390913 of the host, the D33 product path of `product_path.py`, save TB-HUGE_0350 (huge, turn 350),
load + 3 AI turns per run. Arms: `before` = server.exe `09c2d96e...` built from main `c008b85c1` (5054's log thread in
it), `after` = `a83600ec...` from the branch at `fa0e1435c`; the same composite `9a47dc08...`, the DLL pin `1b87dd185`
(keep/pins-0.1b deps). Conditions: `quiet`; `load` = every .fpk and .dds of the instance opened once without buffering
(2,347 files, 0.3-0.6 s) and, for the whole run, a writer (64 MB blocks with fsync over a 2 GB file in the job's work
folder, 87-94 GB written per run) and a random 1 MB reader on the same disk. ABBA, 2 runs per arm and condition. Every
product-path run ended with exit 5 because of 200 pre-existing LOUD lines in server.log (`remote: LOUD: stale Unit
instance ... (ticket 0576)`), the same in both arms; 3 turns played in every run.

Per run (decal = the pipeline's `first use`/`slow bake` lines, the first 64/32 of a run; stall = the gap between two
`host 1s:` lines minus 1 s, from the field on):

| run | decal textures read in bakes | their post step, max / sum ms | decal bakes > 100 ms | render-thread stall max ms | submit_max ms | layer textures step max ms (bakes > 100 ms) | style max ms | sets held, longest hold ms | loader: textures, MB, reads ms, longest ms |
|---|---|---|---|---|---|---|---|---|---|
| 01 before quiet | 61 | 18.9 / 56.0 | 0 | 198 | 45.0 | 40.9 (0) | 3.5 | - | - |
| 06 before quiet | 54 | 3.5 / 20.0 | 0 | 150 | 39.0 | 9.8 (0) | 1.1 | - | - |
| 02 after quiet | 0 | - | 0 | 210 | 41.8 | 12.8 (0) | 2.7 | 8, 96.4 | 243, 19.9, 88.8, 3.5 |
| 05 after quiet | 0 | - | 0 | 80 | 40.6 | 14.3 (0) | 1.2 | 8, 27.1 | 243, 19.9, 16.0, 0.8 |
| 03 before load | 54 | 179.4 / 322.1 | 1 | 303 | 272.9 | 375.9 (5) | 21.5 | - | - |
| 08 before load | 59 | 176.3 / 466.4 | 2 | 94 | 107.8 | 223.8 (2) | 96.6 | - | - |
| 04 after load | 0 | - | 0 | 99 | 97.7 | 288.2 (3) | 24.7 | 8, 627.9 | 243, 19.9, 993.1, 60.2 |
| 07 after load | 0 | - | 0 | 175 | 94.9 | 237.7 (2) | 25.4 | 9, 741.7 | 243, 19.9, 1056.5, 113.5 |

What it shows:
- **The decal reads are off the render thread** (counted, not sampled): before, 54-61 decal textures were read inside
  the bakes per run, their bakes' post step up to 18.9 ms quiet and 176-179 ms under the load (1-2 bakes above 100 ms
  per run, none above 1 s on this SSD); after, none in any bake (the loader read all 243 names, 19.9 MB, in 16-89 ms
  quiet and 993-1,057 ms under the load, longest file 113.5 ms, on its own thread; no draw waited, none read a name
  itself). The cost moved to when a generation's decals appear: a set waited 27-96 ms quiet and 628-742 ms under the
  load for its names, while the tiles kept their bakes.
- **The render thread's longest stall and submit did not change measurably** with n = 2 per cell (stall max 150-198 vs
  80-210 ms quiet, 94-303 vs 99-175 ms under the load): other work dominates them here. Under the load the layer
  textures (`BlendTextureCache::bind`, 224-376 ms, 2-5 bakes above 100 ms per run) and once a style (96.6 ms) are now the
  largest file reads on the render thread: above the MC's 100 ms line, so a follow-up was proposed (the same loader
  route for the layer textures and the style inputs) and filed by the MC as ticket 0698 (2026-11-23, MC #11209).
- ASan (MSVC): c5r_decal_test 15 run, 0 reports; the modes' frame test under the ASan render host, the same hash in all
  three modes, no report.
- oldpc's E: (SATA SSD) under this load gave 176-179 ms for a decal bake, not 0641's 4.8 s on the workstation's loaded
  USB F:; the elimination does not depend on the disk.
- Indication, not a PERFORMANCE.md grade-A timing claim: 2 runs per cell; the claim made is the count (decal reads in
  bakes 54-61 -> 0), which is exact.

## 8. Layer textures and style inputs read off the render thread (ticket 0698, 2026-11-23)

Ticket 0698 (follow-up of 7.5, MC #11209; D241: no file read on the render thread). 0642's oldpc job
`20261123-022757-1456` showed, under its disk load, the layer textures (`BlendTextureCache::bind`) at 224-376 ms in a
bake (2-5 bakes above 100 ms per run, both arms) and one style's inputs (`loadStyleInputs`) at 96.6 ms; quiet 9.8-40.9 ms
`[L]` (7.5's table). Both were first-use reads inside a bake on the server's render thread (section 1's table).

### 8.1 The loader (`PipelineConfig::loadAhead = LoadAhead::Thread`) [C]

`render/src/bake/pipeline.cpp`, on 0642's pattern (7.2), one thread per `PipelineBaker`:
- **Requests.** `init` (after the warm-up, when that runs) starts the thread and queues the five styles not yet loaded;
  `setMap` queues every layer texture name of the map's tables (each layer id's set: diffuse, height map, spec map unless
  empty or `NULL`), skipping names in the cache or already queued, so a later field with the same tables queues nothing.
  The server's tables are the install's TerrainDef.csv, the same for every map, so the first field queues them all and
  later loads none. The warm-up's own `setMap` queues nothing.
- **The thread.** One job at a time: a layer texture is checked with `GameFiles::exists` (a name the install lacks is
  reported as missing, no read), read, parsed and created as `bind` does (`gamefs::parseDds`, `gamefs::createTexture`);
  a style's three files are read (`readStyleFiles`) and its views made (`createStyleInputs`), the two halves of
  `loadStyleInputs` (post.h; the checks in the same order, so a failure reads the same). On a device created
  `D3D11_CREATE_DEVICE_SINGLETHREADED` the thread only reads and the caller makes the textures at adoption.
- **The bakes.** Every bake first adopts what is finished (an atomic count, cheap when nothing is) into
  `BlendTextureCache` (`provide`, `provideMissing`: blend.h) and the style slots. For each name of the tile's layer list
  not in the cache, a name the loader has queued is moved to the front of its queue, and the bake waits for it (and for a
  name in flight); a name never requested, or failed on the loader, is left to `bind`, which reads it as before 0698 (the
  failure then reads as before: NotFound, `missingTextures`). The style step does the same for the tile's style. So a
  bake's output does not depend on the loader: it equals the bake without the loader, bit for bit (8.3).
- **Counting.** `PipelineStepTimes::texturesLoaded` now counts `bind`'s own reads (`loads()`), never the loader's; a
  bake that waited has `loaderWaits` / `loaderWaitMs` (inside its textures and style steps), counts as a first use (so it
  is logged) and its line ends `; loader waits n (w ms)`. `PipelineLoaderCounters` (`loaderCounters()`): requested,
  layer textures, missing, styles, failed, made at adoption, dropped (made after a bake had read the name itself), bytes,
  read ms (sum, longest), create ms, bakes that waited (ms, longest), names moved to the front, fallback binds (bakes
  whose layer textures were not all from the loader). `loaderLoads()`: the loader's files in the order taken.
- **Lines** (the pipeline's log; render.log in the server): `pipeline: loader: started (free-threaded device: read and
  made on the loader thread); N styles queued, the layer textures at setMap (ticket 0698)`; `pipeline: loader: map set: N
  layer texture names queued (K in the cache)`; `pipeline: loader: queue empty T ms after its first request: <counters>`
  (adopted by a bake, so written from the caller's thread).
- **Stop.** `stopLoader()` (also `init` and the destructor) joins the thread and drops queued and unadopted names (a
  later bake reads them itself). `waitLoader(ms)` adopts until nothing is pending (tests, tools).

### 8.2 The server (`srv_residency_source.cpp`) [C]

`ResidencySourceConfig::layerLoad` (-1 = the environment variable `C5_RENDER_LAYER_LOADER`, 0 or 1, else 1): 1 (the
product's default, approved by the MC #11327) gives the pipeline `LoadAhead::Thread`; 0 is the behaviour before 0698.
One line in server.log and render.log at init: `residency: layer textures and style inputs: read off the render thread by
the pipeline's loader (ticket 0698, C5_RENDER_LAYER_LOADER=1) ...`, or `... read by the bakes on the render thread
(C5_RENDER_LAYER_LOADER=0, before ticket 0698)`, or a LOUD line when mode 1's loader did not start. The residency
initialises at server start, so the five styles are read minutes before the first load's field; the layer textures from
the field's `onField` -> `setMap`, while the bake-ahead waits up to 2,000 ms for the first decal set (0365). The offline
player (sceneshot) runs the same default: the bakes wait, so a replay's frames do not change (rule A, 8.4).

### 8.3 Tests (2026-11-23, workstation, pure_vp 4390913 with Vibrant Textures, RTX 4080, plain Release) [L]

`render/tests/bake/bake_test.cpp`:
- `game_loader_bakes_bit_for_bit_what_the_bakes_own_reads_bake`: 3 tiles x 5 styles x t8 0/1 of the two-material
  grid, 60 BC1 textures, compared byte for byte with a baker without the loader (which read 4 layer textures and loaded 5
  styles in its bakes). Arm 0, the loader given time first: 0 reads, 0 style loads, 0 waits in the bakes; the loader took
  73 layer textures + 1 missing (`generic_gibraltar_h.dds`, as 0417's note) and 5 styles, **87.2 MB of files**, reads
  140.6 ms in all (longest 10.6 ms), create 69.6 ms. Arm 1, the bakes at once after setMap: 0 reads, 0 style loads, one
  bake waited 49.2 ms for 4 names (moved to the queue's front); the loader's reads 404.8 ms in all (it ran beside the
  bakes). Arm 2, a SINGLETHREADED device: 78 textures made at adoption, 0 reads in the bakes. All three arms equal the
  reference; one "queue empty" line each; a second setMap with the same tables queues nothing.
- `game_loader_fallbacks_and_negatives`: a loader stopped before the map leaves the reads to the bakes (same bytes); a
  name the install lacks is recorded by the loader (handle 0, no read in the bake, same bytes as the plain baker); a
  layer without a set: setMap refuses BadLayout with and without the loader and queues nothing; a bake before setMap
  NotFound; a second init restarts the loader without requeueing the styles in; the destructor with a full queue; init
  without game files BadLayout and no thread; `readStyleFiles`/`createStyleInputs`: style 7 and no device BadLayout, a
  missing Shadowlight file NotFound naming it, a truncated Sunlight file reported before the missing Shadowlight file.
- c5r_bake_test (25 tests) passed, 0 skipped; c5r_bakeblend, bakegrid, bakeheight, bakelayers, bakepost,
  bakeresidency passed (ctest -R bake, 7/7).

Memory: the loader holds every layer texture of the tables (87.2 MB of DDS files on this install) from the first field
on. Without it a load holds the ones its bakes used: on 0642's huge before-run (oldpc job 1456, run 01) the logged
bakes alone read 51 layer texture files before the 64-line cap, so the difference on a huge map is at most the
remaining ~22 names. [L for the 87.2 MB and the 51; the bound is I]

### 8.4 Before and after on oldpc (job `20261123-080458-fbb6`, 2026-11-23 08:05-08:53 EST) [L]

Setup as 7.5 (0642's method; driver `render/evidence/0698/job_0698.py`, which imports 0642's conditions and analysis):
oldpc's quiet place with a window (`--headed --quiet`; quiet proof alone, foreign cores 0.11 against the 0.1 limit,
svchost and Defender the largest), instance tb1 on E: (SATA SSD), EXE build 4390913 of the host, the D33 product path,
save TB-HUGE_0350 (huge, turn 350), load + 3 AI turns per run. Arms: `before` = server.exe `e911324a...` from main
`1312e2329` (0642's decal loader in it), `after` = `07bb21f0...` from the branch (C5_RENDER_LAYER_LOADER default 1); the
same composite `5662568e...`, the DLL pin `1b87dd185` (keep/pins-0.1b). Conditions `quiet` and `load` (every .fpk and
.dds of the instance, 2,347 files, opened once without buffering, then for the whole run a 64 MB fsync writer and a random
1 MB reader on one 2 GB file on the same disk: 99 GB written per run). ABBA, 2 runs per arm and condition. Every run
ended with product_path exit 5 (the pre-existing LOUD lines of ticket 0576 in server.log, both arms); 3 turns in every
run. Summary: `render/evidence/0698/oldpc-fbb6/summary_0698.txt` / `.json`.

| run | layer files read in bakes (bakes) | styles loaded in bakes | textures step max ms, reading bakes / all logged | style load max ms | bakes > 100 ms in those steps | longest bake CPU of the run, ms | loader waits | first-use lines (cap 64) | loader: drain ms, reads ms in all, longest file ms |
|---|---|---|---|---|---|---|---|---|---|
| 01 before quiet | >= 53 (20) | 5 | 14.6 / 14.6 | 3.3 | 0 | 16.7 | - | 64 (capped) | - |
| 06 before quiet | >= 53 (20) | 5 | 16.6 / 16.6 | 1.1 | 0 | 18.6 | - | 64 (capped) | - |
| 02 after quiet | 0 | 0 | - / 0.1 | - | 0 | 11.1 | 0 | 54 | 1,397, 53.0, 3.9 |
| 05 after quiet | 0 | 0 | - / 0.1 | - | 0 | 9.2 | 0 | 54 | 568, 53.5, 3.9 |
| 03 before load | >= 53 (20) | 5 | 207.2 / 207.2 | 33.7 | 3 | 209.6 | - | 64 (capped) | - |
| 08 before load | >= 53 (20) | 5 | 203.5 / 203.5 | 13.4 | 4 | 205.9 | - | 64 (capped) | - |
| 04 after load | 0 | 0 | - / 0.1 | - | 0 | 18.8 | 0 | 54 | 2,397, 865.6, 116.5 |
| 07 after load | 0 | 0 | - / 0.2 | - | 0 | 18.2 | 0 | 54 | 2,384, 697.2, 102.2 |

(The longest bake CPU is the residency's running `bake_cpu_max_ms` of the terrain 1 s line from the field on; the
before runs' counts are lower bounds because their first-use lines reached the 64-line cap, which the after runs did
not: there every first-use bake was logged, so their 0 is exact.)

What it shows:
- **The layer texture and style reads are off the render thread** (counted, exact for the after arm): before, at least
  53 layer texture files in 20 bakes and all 5 styles were read inside bakes per run; after, none, and no bake waited:
  the loader had every name before the first bake needed it (the styles from init at server start, the 73 + 1 layer
  names from the field's setMap during the bake-ahead's wait for the first decal set). The loader read 87.2 MB in
  53 ms quiet and 697-866 ms under the load (longest file 102-117 ms) on its own thread.
- **The longest bake of a run fell from 205.9-209.6 ms to 18.2-18.8 ms under the load** (quiet 16.7-18.6 -> 9.2-11.1 ms):
  before, the textures step alone was 203-207 ms in the worst bake and 3-4 bakes per run spent more than 100 ms in the
  textures or style step; after, the textures step of every logged bake was at most 0.2 ms. Why: the work left the bake
  (the reads and the texture creation), nothing else in the bake changed.
- **The render thread's longest stall and submit did not improve measurably** with n = 2 per cell (stall max 96-189 vs
  90-158 ms quiet, 79-91 vs 91-98 ms under the load; submit_max under the load 110-260 vs 239-253 ms): other work of the
  frame dominates them on this machine, as in 7.5. The submit numbers under the load are noisy in both arms.
- The drain time (the first request to the "queue empty" line) includes the wait until a bake adopts the last name: it
  is not the loader's own read time.
- Indication, not a PERFORMANCE.md grade-A timing claim (2 runs per cell); the claims made are the counts (layer reads in
  bakes >= 53 -> 0, style loads 5 -> 0) and the per-run longest bake (an order of magnitude, all four load runs).

### 8.5 Live smoke, builders and ASan (2026-11-23) [L]

- **Live smoke** (MC-B #11478; c5h-c, EXE 4390913, hidden, 10:02-10:09 EST; `render/evidence/0698/live_0698.sh`): a
  Huge t350 load on the product path with the branch's server and the default `C5_RENDER_LAYER_LOADER=1`. The styles
  were read at the server's start (adopted at the field's setMap, 130 s later: the first "queue empty" line's time is
  that wait, not reading); at the field the loader queued 74 names and had them all 2,477 ms later (73 textures + 1
  missing, 87.2 MB, reads 205.5 ms in all, longest file 13.8 ms on C:); the bake-ahead completed 29.4 s after the field
  (4,922 tiles, 7,529 bakes, 0 failed) with 0 bakes waiting and 0 layer or style reads in the 54 first-use lines; LOUD 0
  in render.log and server.log, no box, the picture textured as usual by eye (DIAGNOSTIC).
- **Builders:** every builder of the changed render and server files linked and its tests passed (list in
  `render/evidence/0698/acceptance.txt`); render ctest 109/109.
- **ASan:** c5r_bake_test 0 reports (peak 1,454 MB); the ASan render host with the real-tap test's three arms and
  0642's decal-loader frame test: 4 passed, no ASan log.
- **Rule A** (MC-B's slot, 10:10-10:28 EST): offline-equals-live on all 16 bundles of the C: library, sceneshot from the
  branch with the loader on by default: 17 passed, 0 failed, 0 skipped. So the replayed frames equal the recorded ones
  with the loader, as 8.1 says they must.
- Open: none for 0698. Seen on the way (not 0698's): the whole ASan render tree does not build on main since 0692
  (`render/tests/fow/particles_test.cpp` line 763, C4456 under /WX), reported to MC-B (#11465).

## 9. The height pages built at the field (ticket 0714, frame pacing B2, 2026-11-24)

Ticket 0714 is B2 of the Architect's frame-jobs advice (`07-architecture-decisions/architect-2026-11-23-frame-jobs.md`
6, 8): the first uses a bake still meets in play, moved to the load as a warm-up, no threading, the load-time cost stated.

### 9.1 What was left [L]

After 0642 (decal textures, section 7) and 0698 (layer textures and styles, section 8) the only first use left in a
load's bakes is the **height page**. 0703's two laptop jobs on the huge save turn 350 (`20261123-180912-1f69`,
`20261123-181645-6b71`, render.log read with `server/evidence/0714/firstuse0714.py`, 2026-11-24): 54 `pipeline: first
use` lines in each load, **54 of 54 a page build** (`page 3.7-12.8 (built)`, totals 6.5-23.6 ms), no texture, style,
decal texture or loader wait among them; **38 of them after the first drawn frame**, every one over 5 ms (max 22.9 / 18.2
ms), spread over the bake-ahead (the field to `bake-ahead complete` 48.6 / 55.9 s). The huge grid (128 x 84 tiles,
8,192 x 5,376 samples) has (8,192 + 896) / 896 = 10 by (5,376 + 896) / 896 = 7 = 70 pages [C: heightpage.h's rule]; the
land tiles use 54 of them [L: 54 page-building bakes]. A page is a function of the grid alone (section 1;
`c5r/bake/heightpage.h`), 1,398,101 bytes (A8, 11 mips), so the 54 are 75.5 MB of video memory, which the bake-ahead
(radius -1, D88) built anyway within a minute of the field.

### 9.2 The change [C]

- `PipelineBaker::warmPages(tiles, count, report)` (`render/include/c5r/bake/pipeline.h`, `render/src/bake/pipeline.cpp`):
  for the map set by `setMap`, each tile's page through the same source and calls as `bakeBody`'s page step
  (`heightPageForTile`, `tileWindow`, `HeightPageSource::page`), once per distinct page; tiles outside the map are
  skipped and counted; `PageWarmReport` (tiles, skipped, pages, built, ms, thread CPU); `pagesBuilt()`. No log line of its
  own. Checks: before init or setMap NotFound, null tiles with a count BadLayout, the page source's errors (the first is
  returned, the other pages are still built).
- `ResidencySource::onField` (`server/src/render/srv_residency_source.{h,cpp}`): after the residency's compare and the
  baker's `heightsChanged`, **a new load's field** (a load sequence other than the last warmed one, or a field of another
  size) builds the page of every land tile (`tileClass >= 2`) on the render thread before `onField` returns, so before
  the first draw. Line: `residency: height pages of load seq N built at the field (ticket 0714): P pages for T land tiles,
  B built, in X ms (thread CPU Y ms; Z MB of video memory the bakes would have built later)`, LOUD on a failure (the
  bakes then build the rest as before). The same load's field again (a live terrain edit applied whole, 4909's
  fallback) keeps the lazy build of the pages it drops, and so does `onFieldRegion` (a live edit's region): those are
  turn events, not first uses of a load.
- Switch `C5_RENDER_PAGE_WARM` (environment, read at construction, as `C5_RENDER_LAYER_LOADER`;
  `ResidencySourceConfig::pageWarm` overrides): **1, the default**; 0 = the pages in the bakes (before 0714). Init line
  `residency: height pages: built at the field (ticket 0714, C5_RENDER_PAGE_WARM=1) ...` or `... built by their first
  bake (C5_RENDER_PAGE_WARM=0, before ticket 0714)`.
- The picture does not change by construction: the page a bake reads is the same object either way, built from the same
  grid by the same code; only the moment of its build moves. The offline players (sceneshot) run the same `onField`.

### 9.3 Tests [L: workstation, 2026-11-24, pure_vp 4390913 with Vibrant Textures, RTX 4080, plain Release]

- `render/tests/bake/bake_test.cpp` `game_warm_pages_takes_the_page_builds_out_of_the_bakes`: a 30 x 20 plot grid (6
  pages, two materials); warmPages over all 600 tiles + 2 outside: 2 skipped, 6 pages, 6 built, **41.2 ms** (thread CPU
  31.2 ms; about 6.9 ms a page); again: 0 built; then 56 bakes on every page, both sides of the wrap seam, the first and
  last rows: none builds a page, no `(built)` in any first-use line, every t0 / t8 byte-equal to a baker that builds its
  pages in its bakes (which built all 6 there); heightsChanged (all): the next bake builds its page again, a second
  warmPages the other 5; an in-place edit of a 40 x 40 sample box between the warm-up and the bake: the box's page is
  built from the edited grid and the bake equals the plain baker's. `game_negative_warm_pages`: before init and before
  setMap NotFound (report zeroed but `tiles`), null tiles BadLayout, no tiles Ok and nothing built, only outside tiles all
  skipped, no report pointer, a failed setMap drops the map (NotFound), a caller's own page source (`heightPages`)
  counts 6 pages and `pagesBuilt` 0. c5r_bake_test 27 run, 0 failed checks, 0 skipped; c5r_bakeheight_test and
  c5r_bakeresidency_test passed.
- `server/tests/render/test_residency_source.py` `test_pipeline_bakes_a_real_tap` (3401's Tiny tap, the rig of
  render_host_test.exe): arms default (page warm on: the field line names P pages for the land tiles, all built, no
  `(built)` in any bake's first-use line), `pages0` (C5_RENDER_PAGE_WARM=0: the init line says so, the bakes build the
  pages), `1` (0417's warm-up arm, now with C5_RENDER_PAGE_WARM=0 so that its check, first uses = page builds, keeps its
  lines), `loader0`. `test_page_warm_draws_the_same_frame`: the 20th published frame on a land spot near the map's centre
  after the bake-ahead, SHA-256 equal with C5_RENDER_PAGE_WARM=1 and 0 (`620fbbad...` both, 2026-11-24 01:01). On the
  Tiny tap the field line read `12 pages for 1352 land tiles, 12 built, in 66.4-76.2 ms (thread CPU 62.5-78.1 ms; 16.8 MB
  ...)` in three runs (the rig's loaded workstation: an indication of the cost, about 6 ms a page). The rig's tests
  (test_residency_source.py, test_bake_fit.py, test_bake_yield.py): 58 passed, 0 skipped.
- Rule A (MC-B's slot, 2026-11-24 04:48-04:58): offline-equals-live on the 16 bundles of the C: library with sceneshot
  from the branch (the warm-up on by default): 17 passed, 0 failed, 0 skipped. ASan (MSVC): c5r_bake_test 0 reports
  (peak 1,422 MB); the ASan render host's frame-identity test and the four real-tap arms: 5 passed, no ASan log.
  Every builder of the changed files linked and its tests passed after main's merge
  (`server/evidence/0714/acceptance.txt`).

### 9.4 Workstation: two 10-minute pan and zoom sessions [L: 2026-11-24 01:13-01:43 EST, c5h-new-a]

Setup: EXE build 26577625 (instance c5h-new-a, C:, Goldberg, hidden through cli.py, D155 watcher), the branch's
server.exe `43aa1b33...`, proxy `3eeaeeb1...`, composite `a9572bfd...` (branch at `d3c5268e4`, before main's 0720),
laid by `lay.py prep`, the reporter's `[Renderer]` set, `lockstep_mode=exact` from the load; the save copy
`C5H-0714-HUGE_0350` (= the laptop's and oldpc's `TB-HUGE_0350`, SHA-256 `c499d3c6...`, huge, turn 350) put in and taken
out by hash (`saves_snap.py`). Per arm one launch with `C5_RENDER_PAGE_WARM` in the launch's environment, the earliest
dismiss, autoplay off, then `server/evidence/0714/pan0714.py` for 10 minutes (seed 714: arrow-key holds of 0.3-2.5 s in
four directions, drags, wheel zoom out 12-30 ticks and back, zoom in 4-8 ticks and out; about 26 s a round), the safe
stop. Driver `server/evidence/0714/ws0714.sh` (around `live0714.sh`); reader `firstuse0714.py`; summary
`server/evidence/0714/ws-20261124/firstuse.txt` / `.json`. The machine ran other teams' builds and suites (9 registered
heavy jobs at 01:12): the times are indications, the counts are exact.

| arm | page warm at the field | first-use lines in the session | of them after the first map frame | over 5 ms after it (max) | play after the first map frame | field -> first drawn frame | field -> bake-ahead complete |
|---|---|---|---|---|---|---|---|
| s1, C5_RENDER_PAGE_WARM=1 | 54 pages for 4,922 land tiles, 54 built, 355.6 ms (thread CPU 359.4 ms), 75.5 MB | **0** | **0** | **0** | 10.74 min | 6,532 ms | 68,781 ms |
| s0, C5_RENDER_PAGE_WARM=0 (before 0714) | - | 54 (all page builds) | 38 | 27 (10.1 ms) | 10.87 min | 4,252 ms | 43,787 ms |

- **The acceptance holds on the workstation**: with the default no `pipeline: first use` line at all in a 10.7-minute pan
  and zoom session (none over 5 ms after the first map frame), against 38 after it (27 over 5 ms) without. No slow bake
  in either; LOUD 0 in render.log and server.log; the composite's 2 / 4 LOUD lines are the load's `held_skip` frames at
  the fog's install (frames 44-47, both arms: not 0714's). No box. The near pairs' up-res and the zoom-outs met no first
  use: the pages were the only one left, as 9.1 says.
- The warm-up's cost is the line's 355.6 ms on the render thread inside `onField`, 6.6 ms a page (0.36 s of the load).
  The two arms' other load times are one run each on a loaded machine (6.5 against 4.3 s to the first drawn frame, 69
  against 44 s to the bake-ahead's end): their difference is larger than the warm-up and is not attributed to it; the
  load-time comparison is the laptop series (9.5; oldpc's slot fell away on 2026-11-24, its commit floor).

### 9.5 Laptop: 12 interleaved huge loads and a 10-minute session (load time before and after) [L: job `20261124-042751-b139`, 2026-11-24 04:28-05:25 EST]

Setup: the laptop (Quadro P3000 6 GiB, NVIDIA 573.22; EXE build 4390913 of the host; instance tb1), a quiet testbed job
(`--quiet --headed`; the quiet proof: alone, foreign cores 0.114 against the 0.1 limit, MsMpEng.exe 142 s, audiodg 88 s:
`undisturbed=False`), `server/evidence/0714/session0714.py` (render/evidence/0428/loads_0428.py's prep, per-load steps,
stop and restore, imported unchanged), D33 product path, save `TBPP-HUGE_0350` (copy of `TB-HUGE_0350`, c499d3c6...,
huge, turn 350). Builds: server.exe `e8093f3c...`, x64 DLL `b84aa3e9...`, proxy `c77766f0...`, composite `f8a33c71...`
(branch after main `15bf0cb94`, 0720 in it). Order w0, w1, w1, w0, w0, w1, w1, w0, w0, w1, w1, w0 (`C5_RENDER_PAGE_WARM`
0 / 1 in the game's environment, which server.exe inherits), each load: launch, the earliest dismiss (the Lua channel's
`ingame` poll, about 10 s a poll while the game loads), the wait for `bake-ahead complete`, logs, stop; then s1 (the
default) with `pan0714.py` for 10 minutes from the dismiss. Readers: `loadstats0714.py` (per arm: n, mean, sd, median,
min, max, 95 % interval; w1 - w0 by Welch with the detectable difference), `firstuse0714.py`. Summaries:
`server/evidence/0714/laptop-b139/loadstats.txt` / `.json`, `firstuse.txt` / `.json`; raw data
`F:/c5-64bit-work/0714/laptop-b139/`. Every load rc 0, no slow bake in any, the session rc 0 (10.48 min of play after the
first map frame), LOUD 0 in its render.log and server.log, restore rc 0.

| per load, 6 an arm | w0 (before 0714) | w1 (the default) | w1 - w0 (95 % interval; detectable) |
|---|---|---|---|
| the warm-up's own time (its line) | - | **216.7 ms** (sd 10.7, 203.4-229.1) | +216.7 ms on the render thread at the field |
| field -> first drawn frame | 5,201 ms (sd 403, 4,808-5,870) | 4,762 ms (sd 111, 4,583-4,863) | -439 ms ([-878, -0.1]; ~478) |
| field -> first draw | 4,813 ms (sd 896) | 3,788 ms (sd 523) | -1,025 ms ([-2,001, -48]; ~1,186) |
| field -> bake-ahead complete | 29,466 ms (sd 477) | 28,359 ms (sd 1,318) | -1,107 ms ([-2,507, 293]; ~1,602) |
| first-use lines after the first map frame (all over 5 ms) | 38-42 (mean 39.3) | **0** in every load | -39.3 |
| the session s1: first-use lines in 10.48 min | | **0** (54 pages in 227.9 ms at the field) | |

- **The acceptance holds on the laptop too**: no first use at all after the first map frame in the 10-minute session, and
  none in any of the 6 w1 loads' bake-ahead; 38-42 per load without the warm-up, every one over 5 ms.
- **Load time**: the cost is the warm-up's 216.7 ms of render-thread time inside `onField` (counted per load, exact). On
  this path the field comes seconds before the click (0428: 5.7-6.5 s), so it is spent under the load screen: the
  measured time from the field to the first drawn frame **did not grow** (w1 - w0 = -0.44 s, interval [-0.88, -0.0001] s;
  the series could detect about 0.48 s). The negative sign is not claimed as a gain: the first drawn frame waits for
  the click, whose moment follows the 10-s `ingame` poll, and the bimodal w0 values (first draw before or after the click:
  3.7 or 5.1-5.8 s) show that; the visible set's bakes before the first draw were alike in both arms (229-323 ms, the
  first-draw lines), so the warm-up did not shorten them measurably [I]. Grade: 6 interleaved runs an arm on the quiet
  laptop with the disturbance named (foreign cores 0.114, above the 0.1 limit), the PERFORMANCE.md A conditions but for
  that disturbance; no speed claim is made, only the cost (216.7 ms) and "no increase detectable at 0.48 s".
- Memory: the 54 pages (75.5 MB of video memory) are built about 25-30 s earlier than the bake-ahead would have built
  them; at the bake-ahead's end both arms hold the same pages.
