# The world against the interface during pans and zooms (epic 0673, D237)

Written 2026-11-22 by lead-0673 (account A, branch `claude/lead-0673-holistic-d0fd9d`). Origin: The reporter's playtest of the
second beta candidate (installer 0.1b2, main 33c3fcb44, the reporter's options ServerRenderer, ServerUnits, ServerCities,
ServerLandmarks, ServerFarmFields, ServerCombat = 1, ServerArc, ServerPath = 0) and four tickets with the reporter's words:
0674 gray flashes on zoom and pan, 0675 banners/flags/resource icons jitter during pans, 0676 parallax between layers
(city-view citizen icons, resources among trees), 0677 units drift against the terrain and snap back. The reporter's videos and
stills stay local (`%USERPROFILE%/Documents/c5-64bit-patch-staging/bug_report_additions`), never in git. Scripts:
`server/evidence/0673/`.

## 1. Tooling (runbook 4.12, 4.13)

- **Frame by frame through a screen recording** (`vidframes.py`, OpenCV; there is no ffmpeg on the workstation; run with
  plain `python`, since `-I` hides the user site where cv2 lives): `info`, `extract` (one PNG per decoded frame plus
  `frames.csv` with the mean absolute difference to the previous frame, which finds the moments of change), `sheet` (a
  labelled contact sheet), `track` (per region the shift against the previous frame by phase correlation: a terrain
  region against an interface region shows which layer moves when). The reporter's recordings are about 24 fps (rec1 24.7, rec2
  23.3; Medal TV, 1920x1080), so one recorded frame spans about 1-2 presented frames at 30-60 fps.
- **The product exactly as a player has it on a Goldberg instance** (`drv.sh INST install|opts|csv|csvoff|bound|remove`):
  the release installer run silently (`run_installer.py`), the player's VP 5.4.8 DLL set first (`provision.py set-dll`,
  the installer refuses any other DLL, exit 7 "not one of a Vox Populi release"), tree lists before and after, removal
  and `set-dll --previous` at the end. The 0.1b2 installer's defaults equal the reporter's option set [L, 2026-11-22 16:10].
- **Live A/B of the composite's wait bound** (`ab_bound.sh INST TAG`, `ARMS="3400 8000 ..."`): the composite re-reads
  `shell_composite.ini` when it changes, so `lockstep_bound_us` can be switched on a running game; each arm runs the same
  keyboard pans (`live1.sh pans`, `cli.py key --hold <ms>`) and cuts its rows out of `shell_composite.lockstep.csv`
  (`lockstep_csv=1`); `lkcsv.py` prints kinds, ages, the server's ready-minus-publish latency and the present interval.
- **Back-buffer dumps are not a per-frame series and they disturb timing**: one dump per 500 ms control-thread pass,
  about 100 ms each (0665's finding); with dumps on, the present rate fell to 6-18 per second in pans [L]. Timing arms
  run with `NODUMP=1`.

## 2. 0675 and 0676: the composite takes the previous server picture in about half the frames [L]

**Cause.** With lockstep (`renderer-in-server-design.md` 3-5) the composite waits at its insertion point for the server
frame drawn with this EXE frame's camera, at most `lockstep_bound_us` after the camera's publish at UpdateViews, and
otherwise inserts the newest older frame (kind `older`, age 1). The bound is 3,400 us, chosen when the server drew bare
terrain (turnaround p99 3.35 ms, 9039; kept by 3304 with p99.9 1.24 ms). With units and models on, the server's frame
(`Scene::draw` plus the sink's end, `submit_ms`) costs 2.7-3.9 ms, so the frame is ready after the deadline in a large
share of frames (first seen by 4879 on 2026-11-18, `city-renderer.md` 11.12 (b): 45-54 %, not fixed then). In an
`older` frame the whole world (terrain, trees, server units, tapped models) shows the camera of the previous EXE frame,
while the EXE's interface drawn after the insertion (city banners, unit flags, the city view's citizen icons, which are
`WorldAnchor` controls placed by `CityView.lua` 30 units above the plot) shows this frame's camera. During a pan the
interface therefore leads the world by one frame's camera step in some frames and not in others: the micro-jumps of
0675, the "big jump as a pan begins" (the first moving frame is often `older`), and (proposed, NOT confirmed) the icons sliding against their tile
in 0676. *(Corrected 2026-11-22 18:21: The reporter's check with 8,000 us: "The parallax issue did not seem fixed"; 0676 stays open
with a cause of its own to find; the reporter compares against stock VP first. 0675 was accepted as better.)*

**Not the cause [C, code read 2026-11-22]:** the tapped models (resources, improvements, cities) are drawn with the
model tap of EXE frame N-1 almost always (`models: ... pair exact=224 older=12088 max_lag=1`), but the server writes
camera N into every tapped record before drawing (`render/src/landmarks/inputs.cpp` `applyFrameCamera`, 4882), so only
their content is a frame old, not their place; inside one server frame every layer uses one camera
(`srv_render_scene.cpp` `Scene::draw`).

**Measurements [L, 2026-11-22, c5h-new-b, EXE 26577625, the 0.1b2 installer's layout, machine under load from account
B; not a quiet timing].**

| run | save | cap | bound us | frames | older | older while the camera moves | ready - publish p50 / p90 / p99 / max ms | present interval p50 / p90 / p99 ms |
|---|---|---|---|---|---|---|---|---|
| p1 (dumps on) | Tiny C5H-0167-TINY_0008 | 30 | 3400 | 14,166 | 5,200 (36.7 %) | 55 / 170 | 3.08 / 3.87 / 4.67 / 168 | 33.4 / 33.7 / 170 |
| ab1 arm 1 | new Standard game, turn 0 | 30 | 3400 | 2,016 | 934 (46 %) | 141 / 291 | 2.68 / 3.79 / 6.08 / 105 | 33.3 / 33.6 / 33.8 |
| ab1 arm 2 | same | 30 | **8000** | 2,487 | **1** | **0 / 293** | 3.55 / 5.04 / 5.77 / 8.3 | 33.3 / 33.7 / 34.5 |
| ab1 arm 3 | same | 30 | 3400 | 2,378 | 1,285 (54 %) | 166 / 290 | 3.20 / 5.14 / 5.80 / 111 | 33.3 / 33.7 / 36.4 |
| ab2 arm 1 | Tiny | none | **8000** | 21,244 | **4** | - | 1.86 / 2.73 / 3.69 / 8.5 | 3.07 / 4.16 / 5.85 |
| ab2 arm 2 | Tiny | none | 3400 | 21,114 | 3,278 (15.5 %) | - | 2.59 / 4.11 / 4.61 / 22 | 3.95 / 6.60 / 9.80 |
| ab2 arm 3 | Tiny | none | **8000** | 25,292 | **18** | - | 2.21 / 3.51 / 4.39 / 35.5 | 3.50 / 5.61 / 8.74 |
| ab2 arm 4 | Tiny | none | 3400 | 24,741 | 3,917 (15.8 %) | - | 2.32 / 3.89 / 4.36 / 13.4 | 3.55 / 5.80 / 8.63 |

The 100-170 ms maxima at 3,400 us are give-up episodes (8 misses in a row, then no wait until a frame is ready without
one); at 8,000 us none occurred. The frame interval shows no cost of the longer bound in these arms (alternating arms,
under load: an observation of no regression, not a performance claim under `docs/briefs/PERFORMANCE.md`; no speed is
claimed, and a quiet-machine series would be needed for any statement about the wait's cost).

**Fix (accepted for 0675 by the reporter's D237 check on 2026-11-22: "better than it was before"; the reporter takes the next steps for 0675 and 0676 to the Architect):** `lockstep_bound_us` 8000 in `shell/composite/shell_composite.ini`, the DLL's
built-in default and `VPFRAME_BOUND_US_DEFAULT` (`shell/common/vp_frame.h`; one truth, ticket 0490). The wait costs
only when the server is late, which with the bound at 3,400 us already shows as a one-frame lag; the bound's maximum
stays 10,000 us.

## 3. 0674: the "gray" flashes are the fog of war placed through the previous frame's window [I, from the reporter's video]

The reporter (2026-11-22): "these aren't the old renderer gray tile issue these look more like the clouds fog of war". rec1
frame 571 (0:22.37, a zoom-out step between 570 and 572): clouds cover everything except a rectangle the size of the
previous frame's view, with the revealed land inside it; the next frame is correct. This fits the mechanism of
`fog-after-camera-jump.md` 2 in a small form: the mask's plots come from camera N but are placed through the overlay
pair's window of frame N-1, and every reader samples the mask through that window; when the view of N reaches outside
the N-1 window (a zoom-out step, a fast pan step past the window's margin) the part outside reads no mask. Not yet
reproduced live. *(Corrected 2026-11-22 by ticket 0674: "the fix is 0666's route (a)" read so; the fix as built is
route (c) of the epic's design, 3.1 below, and covers 0666 too.)*

### 3.1 The fix as built: the mask's window from camera N, the overlay moved into it (ticket 0674, route (c)) [C]

Built 2026-11-22 by worker-0673b on `claude/lead-0673-holistic-d0fd9d` (main 3f2add0b7 + the lead's tracker commits).
Not verified live (no game was launched for this ticket).

- **When.** `FowScene::beforeWorld` (`server/src/render/srv_fow_scene.cpp`, `chooseWindow`) computes, per view of camera
  N (the camera block's view x projection), the ground footprint (`c5r::fow::footprintFromCamera`: the four frustum
  corner rays cut with z = 0 and z = 127.5, the box `plotRangeFromCamera` already took its plot range from). If any
  view's footprint is not inside the pair's window of that view (`footprintInWindow`, 0.01 world units of tolerance;
  a view the pair does not have counts as outside), the frame takes **camera windows**; otherwise the pair's windows,
  exactly as before 0674.
- **The window.** `c5r::fow::windowFromCamera` (`render/src/fow/mask.cpp`): the EXE's rule per axis
  [floor64(min) - 64, ceil64(max)] of view 0's footprint, as an OverlayProj in the EXE's form (diag(2/W, 2/H, -0.5, 1),
  last row (tx, ty, 0.5, 1)). It reproduces stock's two captured Tiny windows exactly [C, `mask_test`
  `window_from_camera_stock_cameras`]: cam1 footprint x -1530.35..-772.13, y 40.33..586.04 -> [-1600, -768] x [-64, 640];
  cam2 x -1449.18..-982.80, y 217.74..582.30 -> [-1536, -960] x [128, 640]. Each further view's window is view 0's moved
  by that camera view's footprint shift against view 0 (snapped to k x the wrap period within 0.5 units: the EXE's wrap
  OverlayProj is view 0's moved by 3583, overlay-system.md 4).
- **The mask.** Built in those windows (plots from camera N as before); the window is part of the rebuild key (the
  built window is compared byte for byte, so a camera window that changes inside one pair generation rebuilds). The
  window is published with the mask to the EXE's model draws (`handOver`: the built window, not the pair's), and the
  composite moves it from there to the EXE frame's window (fog-of-war.md 9.5). The clouds, particles, ice and server
  models read the mask through the same windows (`m_lastIn`, `op.overlay.proj`).
- **The overlay.** Terrain t1/t4, water and river t9/t10 and the trees share one matrix for the overlay and the mask
  (the EXE's shader bytecode). On a camera-window frame `Scene::draw` (`server/src/render/srv_render_scene.cpp`) sets
  `op.overlay.proj` to the camera windows' wrap copies (`OverlayCopiesFromFrame`) and `op.overlay.srv` to the overlay
  of frame N-1 **resampled** into view 0's new window by `c5r::fow::OverlayResampler` (`render/src/fow/resample.cpp`):
  one full-screen triangle, uv' = a uv + b from the two windows (`uvMapBetweenWindows`), trilinear over the source's
  mips, (0, 0, 0, 0) where uv' lies outside [0, 1] (the ground the N-1 overlay never held), then `GenerateMips`, into a
  scratch texture of the source's size and mip count (R8G8B8A8_UNORM; the live overlay is 2048 x 2048 x 12). Own HLSL
  compiled at the first use by d3dcompiler (as the terrain's own programs), nothing of the EXE. A failed resample binds
  no overlay for that frame (the mask still placed), counted and LOUD once per reason. The water's overlay is now set
  after the fog block so it gets the same window. `Scene::lastOverlay()` stays the received pair (the recorder writes
  it); `Scene::boundOverlay()` is what the draw bound.
- **Key** `render_fow_mask_window` (server.cfg / render_config; `srv_render_options.cpp` `ParseRenderFowMaskWindow`,
  declared in `srv_render_host.h`): `camera` (default, the fix) | `pair` (the picture before 0674). `ParseHostConfig`
  calls the parser (one line in `srv_render_host.cpp`) and the recorder writes the key into a bundle's render_config
  (one line), so the offline player replays what was recorded; a bundle without the key replays with `camera`.
- **Counters.** render.log `fog 1s:` gains `camwin=<frames of the second with a camera window>` and
  `ovl_moved=<overlays moved this second> ovl_move_fail=<failed moves in total>`; at the end one line
  `mask window totals (0674): render_fow_mask_window camera, camera window frames N, footprint errors N, overlays moved
  into the camera window N, move failures N, camera window frames without an overlay N` (a line of its own: appended to
  the terrain totals line it cut off the decal totals at the host's line length, which 10 render-host tests parse; seen
  2026-11-22 in the first suite run). The first camera-window frame of a scene also writes one note,
  `fog: the mask's window made from the camera at frame N (... the pair's window [..] x [..]; camera window [..] x [..] ...)`,
  so an offline replay says whether any of its frames took one.

**Tests [L, 2026-11-22, this workstation, RTX 4080; the stand-in cases on WARP].** `render/tests/fow/mask_test.cpp`
(c5r_fow_mask, 17 cases, 4 new, all pass): `window_from_camera_stock_cameras` (the two windows, footprints inside, cam1 with
clip x/y x 0.8 outside cam1's window and inside its own, 64-unit multiples, the tolerance), `uv_map_between_windows`,
`overlay_resample_world_points` (a 256-texel 8 x 8 block source: every block centre the same colour at the same world
point after a x1.5 zoom-out step and after a 64-unit pan step, every texel a texel or more outside the source's window
exactly (0, 0, 0, 0), every texel two inside opaque, the mips made; the refusals), `overlay_resample_gpu_time`.
`server/tests/render/fow/test_fow_scene.cpp` (31 cases, all pass; 5 new):
`zoom_out_step_leaves_no_pixel_without_mask` (the real FOW mask with the EXE's programs on the Tiny map, every plot
seen, pair = cam1's window, camera N = cam1 x 0.8: an 8 x 8 grid of screen points over the whole screen, edges
included, projected to z = 0: with `camera` 0 of 64 outside the bound window and 0 on a zero mask texel; with `pair`
11 of 64 outside), `far_jump_mask_not_empty` (cam1 moved 1,500 units: 944,388 non-zero mask texels with `camera`, 0
with `pair`: 0666's empty mask reproduced and removed), `at_rest_unchanged` (both key values: no camera window, the
mask built with the pair's window, the bound overlay the pair's texture and matrices), `camera_window_moves_overlay`
(64 of 64 block centres equal at the same world point, 16 of 16 strip points transparent, the recorder's overlay the
pair's, a failed move: no overlay, the fog still on, one LOUD line), `camera_window_handover` (the published slot's
OverlayProj is the camera window). The stand-in rig's pair window was [-32, 32]^2, which its own camera's view
(x -43..47, y -32..32) leaves; it is now the EXE's window for that camera, [-128, 64]^2, so the 26 older cases still
run at rest.

**Suites and builders [L, 2026-11-22 19:25-21:00, EXE 4390913 copy outside the pool].** render ctest whole 108 of 108
(`render/build.py --test`, 0 skipped). `run_all.py` (runbook 2.3): fow-scene 3, overlay-scene 3, render-host 181,
cut-tap 1, decal-tap 13, ice-reader 11, overlay-reader 36, shadow-reader 40, shadow-ice-scene 6, water-scene 29,
forest-scene 3, scenebundle-programs 4, scenebundle-py 22: all pass, 0 skipped (render-host first failed 10 decal tests
on the totals line, fixed as above). Builders linked (runbook 2.10, R13 + U6 + c5render): `build_server.py` (release,
the 0.1b pins), `build_units_tests.py` (test_scene_units 79 of 79), `build_units_extras_tests.py` (13 of 13),
`run_extras_test.py` (OK), `build_unitfeed.py`, `test_farmfields_build.py` (16 of 16 cases), `build_trigger_scene.py`,
`build_sceneshot.py`, `test_fow_build.py`, and run_all's rt, cut, decal, ice, ovr-reader, shadow-reader, shadow, water,
forest, fow, ovl and sb builds.

**Rule A [L, 2026-11-22 19:49-21:00].** The C: scene library synced from the F: master at 19:49 (16 bundles, 1306
files, 8.84 GB; it holds 4749's format-10 bundles and the kit bundles re-recorded 18:31-18:59, 4749's code a30005c33
being in this branch's base). `run_all.py --only offline-equals-live` with the new default: 17 passed, 0 failed (863 s).
A second pass with this branch's sceneshot over every bundle (`--dumped --compare --tolerance 1:1`): 128 of 128 frames
colour and depth EQUAL, and no replayed frame took a camera window (no bundle printed the first-camera-window note):
the bundles' frames are drawn at rest, where the change does nothing. No frame differs; no re-record is needed. The
change is not covered by any bundle (no recorded pan or zoom step leaves the pair's window); only the stand-in cases
above exercise it.

**Resample cost [L, indication under docs/briefs/PERFORMANCE.md, no speed claim].** `overlay_resample_gpu_time`,
timestamp queries around `resample()` (the draw and GenerateMips), 2048 x 2048 x 12 mips, 200 samples after 5 warm-up,
2026-11-22 19:2x, RTX 4080, the workstation under other agents' load: zoom-out step x1.5 median 0.0327 ms (p10 0.0323,
p90 0.0331, p99 0.0354, max 0.359); pan step median 0.0325 ms (p90 0.0329, max 0.0360). It runs only on camera-window
frames (none at rest).

**Not done / not verified.** *(Corrected 2026-11-22 22:00: the live run is 3.2; was here: "No live run (no game was
launched for 0674)".)* The resample's GPU cost is a module figure, not a live frame time. The
composite's handling of a published camera window (fog-of-war.md 9.5: shift or resample to the EXE frame's window) is
unchanged and was not exercised with one (`shell/tests/test_fow_models.py` was not run: it needs the shell build, and
neither `srv_fow_mask_pub.cpp` nor the composite changed).

### 3.2 The live check (2026-11-22 20:50-21:50, c5h-new-b, EXE 26577625) [L]

**Build and lay-out.** This branch's 470981026: server.exe c7d7206e (`build_server.py`, 136 s) and composite 86b99858,
laid with `lay.py prep` + `helper on` on c5h-new-b, the reporter's option set (setopts.py; section 2), `lockstep_csv=1`.
Arms by `server_cfg_extra render_fow_mask_window=pair|camera` (server.cfg and render.log confirm the value per launch),
order pair, camera, pair2, camera2. Driver and read-out: `server/evidence/0673/live-0674/` (drive0674.py, anal0674.py,
live0674.sh, series.sh, sheet0674.py, reveal.lua, reveal_r.lua, lookcap.lua); outputs
`C:/c5-64bit-work/0673/live-0674/out/<arm>/` (not in git). Wheel zoom by `tools/hi/actions.py` `wheel` (z 424 -> 572
per tick); `cli.py wheel` did nothing in either direction (not root-caused; [I] client against screen coordinates of
WM_MOUSEWHEEL with the window parked at -4680).

**Counters** (`camwin` / `ovl_moved` / `ovl_move_fail` summed over each series, frames in the series):

| Series | pair | camera |
|---|---|---|
| zoom, 6 x 3 wheel ticks, z 424 <-> 1043 | 0 / 0 / 0, 4447 | 140 / 140 / 0, 2372 |
| keyboard pan, 8 keys | 0 / 0 / 0, 4203 | 16 / 16 / 0, 2569 |
| zoom with the whole map revealed, z 261 <-> 864 | 0 / 0 / 0, 3685 | 136 / 136 / 0, 2642 |
| minimap, 3 far jumps | 0 / 0 / 0 | 8 / 8 / 0 |

Whole camera launch 559 / 559 / 0, second camera launch 199 / 199 / 0; no LOUD line. First camera-window note at frame
4398: pair window [1024, 1856] x [-576, 64] -> camera window [960, 1920] x [-640, 64]. **So the route engages on zoom
steps, pan steps past the margin and far jumps, and every overlay move succeeded.** The `mask window totals (0674)` line
was in no run: the harness stop ends the server before its end-of-run totals (as every other totals line) [L].

**Pictures.** 130 back-buffer dumps in `pair`, 138 in `camera`, at three reveal states (the save's own 28 plots, the whole
map, radius 6 around the capital). **No dump in either arm shows the flash** (clouds over everything but a rectangle).
The dumps are one per 500 ms control pass (section 1), so a one-frame flash is caught only by chance; the dumps do not
decide the fix. Acceptance stays the reporter's eye check (D237).

**Cost.** The fog module's own per-second figures (render.log `fog 1s:` `cpu=` / `gpu=`, mean over the seconds with a
fog): pair 0.372 / 0.104 ms, camera 0.422 / 0.162 ms (first pair); pair2 0.430 / 0.141 ms, camera2 0.448 / 0.135 ms.
The uncapped frame rates differed far more (camera launches 117-141 fps, pair launches 154-293 fps) even in the pan
series with 16 camera-window frames out of 2569, so that difference is not the change [I: other agents' load on the
workstation, which varied over the hour]. No speed claim either way (docs/briefs/PERFORMANCE.md): a quiet A/B is open.

**The reporter's eye check (D237) [L, 2026-11-22 22:13-22:19, c5h-new-b].** The same build and lay-out
(`live0674.sh c5h-new-b prep`, `ini lockstep_csv=0`, `arm camera`), C5H-0167-TINY_0008 at turn 9 with AI autoplay off,
uncapped (`--timing`), the camera on the capital (51, 14), the client shown for the check only (`cli.py guard off`,
`cloak off`, `park 0,0`; 2560 x 1440). The reporter zoomed, panned, dragged and jumped by the minimap by hand: **"No flash
seen".** Then the harness stop (exit 0), `lay.py restore` ok (verify ok), `leftovers` clean. During the check a 64 x 64
square cycling colours at the bottom right of the screen was another agent's test window (`tools/capture/tests/
test_dupcap.py`, topmost, over the client's minimap), not the client: tests that show a window wait while a check runs
(the window tests now run only with `DUPCAP_TEST_SHOW_WINDOW=1`; `DUPCAP_TEST_WINDOW=x,y` moves the window).

**A stall seen in passing.** 21:00:20: one render.log write on the render thread took 7,427 ms; the world froze about
7 s (1,777 older frames). That is ticket 5054's cause (render.log writes on the render thread), not 0674's.

### 3.3 Beta 4: the reporter saw the flash again; not reproduced (ticket 0736, lead-0673, 2026-11-25 00:3x-01:1x) [L; C where marked]

The reporter, after playing beta 4 (0.1b4-std-only, cut dc259f0b1, server `6c75d7fa...`, proxy `48524398...`, composite
`fc01cafa...`) on the workstation: the flashing fog on panning / zooming "seems to have come back"; later the reporter
could not reproduce it either, and 0736 was parked waiting for a video (MC, 01:0x). What was established:
- **The reporter's own session [L, read only]** (`Documents/My Games/.../Logs/VP64/run/20261124-235235-46664-1/render.log`,
  2,495 s of `fog 1s:` lines): `render_fow_mask_window=camera`; 3,907 camera-window frames in 259 seconds, every overlay
  moved (`ovl_moved` 3,907, `ovl_move_fail` 0). The view straddled the map's wrap seam (the first camera-window note:
  the pair's window [-256, 576] x [1216, 1856], world x below 0). The composite (`shell_composite.log`, `fow_models: 10
  s shifted` totals at 00:55): 238 frames of effective lag (the EXE's own model draws reading the mask through another
  window than their frame's after the composite's move), nearly all of kind `views` (the server's mask and the EXE's
  frame have a different number of views or the views do not move together: `ShiftFor` returns SH_VIEWS and the mask
  is used as built, `shell/composite/fow_models.cpp`), in bursts of up to 29 frames; 5 `too far`, one at 8,716 texels;
  the fill strip read on screen in 3,531 frames. Whether any of that is what the reporter saw is **not known**.
- **The code [C]** (a review of c23de660f..dc259f0b1): route (c) of 3.1 is intact: `chooseWindow` unchanged and called,
  the overlay moved and bound, `handOver` publishing the built window, the key's parser and default unchanged; no
  commit in the range touches the fog's window, the overlay move or the publication; the fog's own files changed only in
  `admit()` / `readTable` (0691 hold bits, 5052 plot table). The candidates the ticket named (0529 R2, 0691, 0703 / 0704 /
  0707, 0711-0714, 0713) have no path to the fog window found in their diffs.
- **Every displayed frame on the laptop [L]** (TB-HUGE_0350 t350, 30 fps, beta 4's exact binaries in
  `C:/c5-64bit-work/0673/ship-b4`, product arm; `server/evidence/0736/lap0736.sh`, runbook 4.15): key pans (150-1200 ms)
  and drags (300 / 1000 ms) x 3, job `20261125-003902-b4db`, 57 recordings, 193 camera-window frames; capture_job's new
  `zoom` case (wheel steps of 3 ticks x 4 out and back, the capital's view) x 3, job `20261125-004844-4cd3`, 83 camera-window
  frames: **no fog flash in any frame** (`server/evidence/0736/flash0736.py`: a frame unlike both neighbours while they
  are alike; baseline on 0685's walks: top score 1.5). At the wrap seam: job `20261125-005802-e02e` stayed on the capital
  (`UI.LookAt(plot, 0)` did not move the camera; `--look-at` now uses the one-argument form), 24 `views` lag frames,
  no flash; job `20261125-010549-f2b8` (`--look-at 1,26 --reveal`) moved the camera but every frame shows black
  unexplored ground (the reveal did not show there), 18 `views` lag frames (one of 14,564 texels): **the seam case is
  not checked visually.**
- **A one-frame camera jump [L, once in 57 recordings]:** `drag1000r_r2` frame 50 of job `20261125-003902-b4db` shows
  the whole picture, the EXE's city banners included, at another camera position for one frame between two equal ones
  (flash0736 score 21.6): the EXE's own frame, not the fog. Too rare to chase from one sighting; strips with flash0736.py
  `--sheet` from that job's fetch (`C:/c5-64bit-work/0673/j0736/b4-keys/flash/`).
- **Open (if the reporter's video comes):** the wrap-seam case with revealed land on screen (a save whose revealed land
  reaches the seam, or a reveal that shows), the reporter's frame rate (about 165 fps on the RTX 4080, vsync), and
  whether the flash is on the server's clouds or on the EXE's model draws (the `views` frames above).

## 4. 0677: units and flags move with the camera while the terrain stays still [open]

rec2 2:29 (frames 3478-3485 at 23.3 fps): the terrain, water and clouds are identical for about five recorded frames
while the units and their flags move about 170 px with the camera; then the terrain jumps to the new place. No units are
left at the old place in the frozen frames. Inside one server frame all layers share one camera [C], and the composite
can only insert a whole older server frame, so the units seen moving were not the server's units of that frame [I].
Keyboard pans on a new Standard game at turn 0 with the reporter's options did not reproduce it (ages 1-2 only, units ready,
the unit filter dropped every EXE unit draw: `passed_set=0`). Next: The reporter's own session logs and the way the reporter panned.

**The reporter's further reports (2026-11-22).** It happens on left-button drags (click the ground, move the mouse); on
brief arrow-key holds the resource icons move against the terrain (ticket 0684), and the unit flags and city banners do
the same (21:3x, with the 8,000 us bound, where nearly every frame is exact).

**Camera at Present against the published camera [L, 2026-11-22 21:2x-21:4x, c5h-new-b, 3.2's camera launch,
`lockstep_check=1`].** Drags at the capital and arrow-key pans: over 41,000 compared frames `cmp_diff=0` and the CSV's
`cmp` column all 0 (the only differing row the first compare, at the moment the check was switched on: every field).
Drags: every frame exact (age 0); keys: 2 frames of age 1. `unitfilter 1s`: `passed_set=0`, `mismatch=0`; units ready
throughout (`not_ready_frames` +0). In consecutive drag dumps units and flags stay on their tiles: **no drift reproduced
by the automation.** So the EXE's camera does not change between UpdateViews (where it is published) and Present
**[L]**; this refutes the reading "the EXE draws its interface with a camera that changed after the publish". Still open
**[I]**: the EXE may compute the world-anchored interface's screen positions from the previous frame's camera (before
UpdateViews moves it), which a camera compare cannot see; whether stock shows the same lag against its own world is the
stock-against-product comparison of 0684. The automation pans and drags uncapped at 117-300 fps, where one frame of
camera movement is small; the reporter plays at a capped rate.
*(Answered 2026-11-22 23:15 by section 5 on the laptop: stock shows no lag of the interface against its world, while the
product's world trails the EXE's interface by one displayed frame because the composite inserts the older server
frame, ages 1-3.)* *(2026-11-23, section 6.4: the reporter's own session logs show the same mechanism, more often at
the reporter's 165 Hz: 24-98 % older frames while the camera moved.)*

## 5. Stock against product, same inputs, every displayed frame (laptop)

Written 2026-11-22 by worker-0673f under lead-0673 (branch `claude/lead-0673-holistic-d0fd9d`), for 0684 (arrow
keys), 0677 (left-button drags) and 0685 (walks on a bent path).

### 5.1 Method [C: the scripts; L: the runs]

- **Machine and arms.** The laptop (`game-instance@laptop`, MC-B #10780, `--headed`, its window visible on the
  owner's logged-on screen, D63), instance `tb1` with the laptop's own Steam EXE, the TB-HUGE_0350 save copy
  (`%TESTBED_SAVES%`, SHA-256 `c499d3c6...`, turn 350, active player 0, AI autoplay already 0), the same graphics
  settings in both arms (the instance's own), client 3824 x 2088 physical showing a 1530 x 835 back buffer (250 %
  scaling), window parked at 0,0, the game capped at 30 fps in both arms (frame interval p50 33.3 ms in every
  recording). **Stock**: the instance's 32-bit DLL (the i0-ship instance-aware pin of the testbed bases, `bases.json`
  `dll-pin-ia` 3c2f47da...; its hash per job in `capture.json` `before.proxy`), no proxy or server. **Product**:
  `product_path.py`'s prep imported unchanged (proxy 02cf8f13..., server.exe c7d7206e... with the four x64 libraries of
  `C:/c5-64bit-work/0673/live-0674/build/bin` (x64 DLL b84aa3e9...), composite 86b99858..., the repository's
  `shell_composite.ini` with `lockstep_bound_us=8000`, `lockstep_csv=1`), D33 with Overlay on, then
  `VP64Options.ini` `[Renderer]` ServerRenderer, ServerUnits, ServerCities, ServerLandmarks, ServerFarmFields,
  ServerCombat = 1, ServerArc, ServerPath = 0 (the reporter's set). Both arms restored by hash and `provision.py
  verify` at the end (every job exit 0).
- **Inputs, the same Lua and posted input in both arms** (`tools/capture/capture_job.py`): in the InGame state AI
  autoplay off (it was 0), `PreGame.SetQuickMovement(false)` (it was **on** in the save), selection cleared,
  `UI.LookAt(the active player's capital, 101,25, 0)` before every recording. **That call did not move the camera**
  [L]. The view in every recording is the load's: the city Asyut with the selected machine gun. The capital is Thebes,
  and plots three or more hexes from Asyut never came into the crop. The pans return the camera to its start
  (LEFT then RIGHT of equal length, drags in pairs), so the base frames of all 16 key and drag recordings of an arm still
  lie within 0.3 px of each other (one drag case 5.3 px, in both arms), and the two arms' base frames show the same view.
  *(Corrected 2026-11-23 00:03; was: "UI.LookAt(capital Asyut 101,25, 0) before every recording".)* The zoom is the
  load's default (no Lua sets it). Keys: `tools/hi/actions.hold` LEFT N ms, 1 s, RIGHT N ms, 1 s, N = 150, 300, 600, 1200 (posted key messages
  with system-rate repeats, the focus guard reports the key held). Drags: `actions.drag` from 40 % to 65 % of the client
  width at 55 % height (and back), over 300 and 1000 ms. Walks: a spawned spearman and horseman, `MISSION_MOVE_TO`
  two plots with a 60 degree bend (5.4).
- **Recording.** `dupcap.exe` (tools/capture, b30c083b...) per case: a 1400 x 900 physical crop centred on the
  client, start file after `READY`, the inputs 0.3 s after "recording"; every input time is `perf_counter`, the same
  QPC clock as dupcap's `qpc` column. The job reduces each frame by 2.5 to grey (560 x 360, about back-buffer px) for
  the fetch; the full-size PNGs stay in the job's `out/raw` on the laptop.
- **Analysis** (`server/evidence/0673/`, plain `python` with OpenCV): `capcmp.py track` takes the terrain's step
  between consecutive displayed frames as the median of phase correlations of 192 px patches. A single whole-frame
  correlation was biased by a big still interface element (a synthetic test: a 6 px/frame pan read as +0.8 px). Each
  element (the city banner 195,90,170,32, flags at 222,50 / 452,108 / 367,222 in the reduced frame) is followed by its
  base-frame template within 40 px of where the terrain's step puts it. `capbatch.py` gives per recording the frame shift k that best
  matches the element's steps to the terrain's over the pan's first 8 moving frames (k = +1: the terrain moves one
  displayed frame after the element), the mean |step difference| at k = 0 and +1, and the **freezes**: displayed frames
  in which the terrain stands still between moving frames. `caplock.py` joins the product's `shell_composite.lockstep.csv`
  rows to the displayed frames by QPC (present in (qpc[i-1], qpc[i]]). Synthetic check: an icon drawn one frame late
  reads k = +1 and lag 1.09 frames; drawn in step, k = 0.

### 5.2 Results: keys (0684) and drags (0677) [L, 2026-11-22 22:57-23:33 EST, laptop host clock]

Jobs: stock `20261122-225731-0750` (run 1) and `231134-884b` (runs 2, 3); product `230441-8a8b` (run 1) and
`232043-c899` (runs 2, 3); all exit 0. Reduced px (x 2.5 on the laptop's screen). k = the frame shift that best
matches the banner's steps to the terrain's over the pan's first 8 moving frames (+1: the terrain one displayed frame
later); d0 / d1 = the mean |banner step - terrain step| at k = 0 / +1; freezes = displayed frames with the terrain
still between moving frames (`capbatch.py`; per run, in run order).

| case | stock k | stock d0 / d1 px | stock freezes | product k | product d0 / d1 px | product freezes |
|---|---|---|---|---|---|---|
| keys 150 | 0, 0, 0 | 0.38-0.51 / 3.0-3.6 | 0, 0, 0 | +1, +1, +1 | 3.1-6.4 / 1.3-5.6 | 2, 1, 4 |
| keys 300 | 0, 0, 0 | 0.62-0.63 / 2.2-3.1 | 1, 0, 0 | +1, +1, +1 | 3.8-6.6 / **0.5-3.6** | 8, 7, 8 |
| keys 600 | 0, 0, 0 | 0.62-0.64 / 2.2-2.4 | 0, 1, 1 | -1, +1, +1 | 8.7-10.4 / 8.0-9.2 | 11, 13, 14 |
| keys 1200 | 0, 0, 0 | 0.63-0.68 / 2.3-2.8 | 0, 0, 0 | +1, +1, -1 | 11.5-11.7 / 9.5-10.3 | 13, 24, 24 |
| drag 300 right | 0, 0, 0 | 0.97-1.30 / 8-22 | 0, 0, 0 | 0, 0, 0 | 11.4-16.7 / 16.5-33.4 | 3, 2, 2 |
| drag 300 left | 0, 0, 0 | 0.82-0.88 / 22 | 0, 0, 0 | -, 0, 0 | 11.3 / 22.5 | 3, 3, 2 |
| drag 1000 right | 0, 0, 0 | 0.36-0.46 / 4.5-7.2 | 0, 0, 0 | 0, +1, 0 | 5.6-7.5 / 4.1-7.3 | 3, 3, 5 |
| drag 1000 left | 0, 0, 0 | 0.28-0.30 / 5.5-6.6 | 0, 0, 0 | +1, 0, +2 | 4.3-7.0 / 1.7-7.3 | 3, 5, 6 |

- **Stock: the interface moves with its world in the same displayed frame**, in every one of 24 recordings (k = 0,
  0.3-1.3 px mean difference, which is perspective: the banner and flags stand above the ground). Its world never
  stands still while the interface moves (0-1 freezes per recording).
- **Product: the world trails the EXE's interface by one displayed frame** at the start of the key pans (k = +1 in 10
  of 12 key recordings; d0 against stock's 0.4-0.7 px is 3-12 px), and **freezes** for single frames while the
  interface keeps moving (1-24 per key recording, more the longer the hold; 2-6 per drag), then catches up with a
  double step. The drags are fast (6-11 moving frames for 300 ms), so their k is less clear (0, +1 or +2), but in
  every drag recording d0 is 4-17 px against stock's 0.3-1.3 px. Example keys 300, per-frame steps terrain / banner: stock 5.8 / 6.0,
  10.7 / 11.0, 14.4 / 15.0, 18.1 / 19.0; product 0.0 / 6.0, 6.2 / 10.0, 10.2 / 16.0, 15.2 / 18.0, 18.3 / 22.0; later in
  the same pan 24.5, **0.0**, 17.9, **0.1**, 12.2, 13.3, 10.7, **0.0**. **By how much:** one camera step per displayed
  frame while moving (6-30 reduced px at these pan speeds, 15-75 screen px on the laptop), and a whole step in a
  freeze; the element-minus-terrain offset over the tracked frames reached 15-36 px in the product against 1-14 px in
  stock (stock's is perspective drift over the pan).
- **Cause [L, the joined lockstep rows]:** in the product job, 5,195 composite frames: exact 682 (13 %), **older
  4,470 (86 %)**; ages 0: 725, 1: 4,403, 2: 33, 3+: 34. In the four key recordings 92-96 % of the presented frames are
  `older`, age 1, and every freeze is a frame whose age stepped up (1 -> 2, 2 -> 3: the same server picture shown
  again). Server ready - publish p50 14.7 ms, p90 25.6 ms, p99 46.3 ms, max 889 ms, so most frames miss the 8,000 us
  bound. The CSV's srv_cpu p50 is 1.09 ms and srv_gpu p50 0.57 ms, but these cover the terrain pass alone (units,
  models, trees, water, fog and ice are not in them) [C, the Architect's
  `architect-2026-11-22-wait-or-shorten.md`]. So on the laptop the product
  shows the whole server world (terrain, server units, models) one EXE frame late against the EXE's interface: the
  mechanism of section 2, which 8,000 us removed on the workstation (ready - publish p50 1.9-3.6 ms there).
  Publish to ready is server CPU time only; no cross-adapter copy lies in that interval [C, the same file]. The stage
  that holds the time is in 5.5. *(Corrected 2026-11-22: this bullet first said "the server's own cost is small
  (srv_cpu ..., srv_gpu ...)" and named "the cross-adapter path" as a candidate inside publish-to-ready. Both were
  wrong, as the Architect's file shows.)*
- **0677's "units drift, then snap back".** In these frames the product's server units belong to the world picture,
  so they trail and freeze with the terrain while their EXE flags move with the interface [I from the mechanism; no
  unit was tracked in the key and drag crops].

### 5.5 Where the laptop's publish-to-ready goes (ticket 0689 step 1, no build) [L, product job `230441-8a8b`]

Read on 2026-11-22 with `server/evidence/0673/lat0689.py` (render.log's `host 1s:` and `units 1s:` lines of the
seconds that drew, 176 and 165 of them, and the lockstep CSV's per-frame columns, 5,195 rows; ms as p50 / p90 / p99 /
max). This follows the reading the Architect's file proposes (`research/07-architecture-decisions/architect-2026-11-22-wait-or-shorten.md` 3, on main).

| Stage | p50 | p90 | p99 | max |
|---|---|---|---|---|
| wake - publish (CSV) | 0.04 | 5.73 | 23.32 | 429 |
| **submit - wake (CSV): the server's pass from its wake to `Signal`/`Flush`** | **13.48** | 23.12 | 38.16 | 891 |
| ready - submit (CSV, `ring_sync=fence`) | 0.00 | 0.01 | 0.03 | 0.09 |
| ready - publish of the frame taken (lkcsv.py) | 14.69 | 25.61 | 46.33 | 889 |
| insertion - publish (CSV) | 8.53 | 22.73 | 66.11 | 1467 |
| host `submit_ms` per second (`Scene::draw` to the sink's end, every layer) | **12.19** | 20.56 | 25.22 | 163 |
| host `submit_max_ms` per second | 18.22 | 33.81 | 130.3 | 877 |
| host `submit_n` per second (server frames) | 31 | 39 | 47 | 48 |
| host `rt_cpu_ms` per second (the render thread's CPU) | 508 | 969 | 1004 | 1016 |
| units `frame_ms_max` per second | 7.80 | 14.76 | 43.67 | 696 |
| units `anim_ms_max` per second | 3.40 | 6.26 | 9.52 | 386 |
| units listed / culled per second's last frame | 733 / 704 | 735 / 707 | | |

- **The stage that holds the time is the draw** (`Scene::draw` of every layer, wake to submit): 12-13.5 ms at the
  median, against about 0 ms for the wake and 0 ms from submit to ready. On the workstation the whole interval was
  1.9-3.6 ms (section 2). Within the draw, the units' frame reaches 7.8 ms in the median second (its per-second
  maximum, so an upper share), with 3.4 ms of it animation. The units module lists 733 units and culls 704 each frame,
  so it does the animation and listing work for all of them while it draws about 30 [I, from the counters: the
  Architect's first suspect, "the units' CPU work at turn 350", fits]. The terrain pass is 1.09 ms CPU (5.2). The
  remaining 3-4 ms of the median draw (trees, water, models, fog, the depth copy) were not split; that needs
  per-layer timings, which no existing line has. *(Continued 2026-11-23 by 5.8: the workstation's split with 0688's
  per-stage lines, this run's CSV split by camera move (moving server frames ready - publish p50 21.3 ms), and what the
  laptop run must record for the laptop's stage split.)*
- The insertion comes 8.53 ms after the publish at the median, so the composite usually waits out the whole 8,000 us
  bound and then takes the older frame.
- **Vsync and the cap:** the instance's `GraphicsSettingsDX11.ini` has `WaitForVSync = 1` (60 Hz panel), and the
  harness launched both arms with its default frame cap, "capped at 30 fps (declared by default)" (issue 0157). The
  present interval is 33.36 ms p50 (p90 34.9) in the product and 33.3 ms in stock. So the laptop's frames had about
  20 ms of slack at 30 fps [I: the EXE's own busy time per frame was not measured].

### 5.4 Walks on a bent path (0685): recorded in both arms, but no walking unit was ever in the crop [L, 2026-11-22 22:57 to 2026-11-23 00:43]

- **What ran.** Spawned (`InitUnit`) spearmen and horsemen, then existing units of the active player, ordered by
  `MISSION_MOVE_TO` two plots with a 60 degree bend. Jobs: stock `225731-0750`, `231134-884b`, `235319-e271`,
  `001631-982f`, `002826-d0e9`; product `230441-8a8b`, `232043-c899`, `235930-5309`, `002229-c094`, `003507-25ba`. The
  game side matched in both arms every time: the same unit ids, plots, moves left and arrival plots
  (e.g. spearman 17542 from 106,26 by 105,25 to 104,25, moves 120 -> 0; horseman 17543 the same, 240 -> 120).
- **Why there are no walk numbers.** `UI.LookAt` over the Lua channel does not move the camera (5.1), so every
  recording shows the load's view of Asyut (110,26). Walks near the capital (101,25) or 4-6 plots west of Asyut fell
  outside the crop: 2000 x 1000 physical shifted west shows sea there, and EUI's unit list covers the lower left. Within 3
  plots of Asyut no plot pair qualified (water, hills, jungle owned by another player, the one machine gun in the city).
  No frame showed a walking unit (heatmaps of |last - first| and per-frame motion on the reduced frames).
- **What would get them** (not done): a camera move that works over the channel, tried in both arms the same way:
  `UI.SelectUnit` with the "centre on selection" key, or a click on the minimap at a computed point, or key pans of
  a measured length; or a save where a free bent path lies within 2 plots of the view's centre. Then crop around the
  unit (`--walk-at`) and fetch full-size frames of that region for the facing (`--max-mb` and `--include
  "raw/walk*"`), since the reduced frames show a unit about 10 px tall.
- **Why the camera did not move, and the walk case as rewritten** (worker-0673u, 2026-11-23, written 09:38 EST; code
  `[C]`, tests on stand-ins and synthetic walks only, **no live run yet**). The knowledge base already had the cause
  `[I for this save, L elsewhere]`: `UI.ClearSelectionList()` does not leave nothing selected while a unit needs orders;
  the game selects the next one at once and pans to it, also with the auto unit cycle off (overlay-system.md 9.2 [L,
  c5h-b Tiny]); the fix used there and in every E9 camera since (4309's rule) is every unit of the active player asleep
  first (`PushMission(MISSION_SLEEP, -1, -1, 0, 0, 1)`), then the clear. capture_job's setup and its per-recording
  LookAt both cleared the selection first, so the machine gun at Asyut was selected again and the camera went back to
  it; both also passed `UI.LookAt(plot, 0)`, which did not move the camera on the seam save where `UI.LookAt(plot)` did
  (terrain-decals.md, drivers' note). The keys/drags cases keep their old view (their recordings are compared with
  it). `tools/capture/capture_job.py`'s `walk` case now, the same in both arms: once, the auto unit cycle off
  (`OptionsManager.SetAutoUnitCycle_Cached(false)`, put back at the end), the player's units asleep, the clear, and a
  listener keeping the walker's `LocalMachineUnitPositionChanged` positions with `os.clock()` (4772's); plots A-B-C
  near Asyut per bend (60: B->C one direction step on; 120: two steps, so C is A's neighbour and no pathfinder takes the
  bend: MOVE_TO B, then MOVE_TO C appended, `PushMission(..., 0, 1, 1)`, as a shift-queued move; the 60 degree walk is
  one MOVE_TO C when the other route's middle plot is costly); a spawned spearman / horseman at A with 3 moves (it
  arrives with moves left, so it blocks the auto end turn); `UI.LookAt(B)` with one argument, 3 wheel ticks in,
  LookAt again; the check by the EXE's camera read (`tools/hi/uimap.camera_pixel`, gt_reader, read only; build 4390913
  needs no table): B within 12 % of the client's centre, else `UI.SelectUnit` + the centre-on-selection key `C`
  (`CONTROL_CENTERONSELECTION`, `CIV5Controls.xml` HotKey `KB_C`, `tools/hi/actions.key`); the crop from A, B, C's
  client px with one plot spacing of margin; a Lua clock sync, the order, 6 s, the positions, a second sync; colour
  frames and `walk.json` per case. Not chosen: a minimap click (its point depends on the minimap's size and the map's
  wrap; nothing in the tools computes it) and key pans of a measured length (the pan speed depends on the frame rate).
  Analysis `server/evidence/0685/turn0685.py` (per frame the root from the EXE's positions, the formation's principal
  axis, members, a snap measure; per case the facing's and the root's turn: start, end, duration, step or ramp,
  per-member lag), validated on synthetic walks (`test_turn0685.py`: a one-frame switch reads `step`, under 40 ms; a
  500 ms turn reads `ramp`, 400 +/- 90 ms; 120 degree bends; without the camera read). Runbook 4.15 has the commands
  and `server/evidence/0673/acc0685_laptop.sh` runs both arms after 0691's.

**The laptop runs of 2026-11-23 (11:41-12:34 EST, laptop, TB-HUGE_0350 t350, EXE 4390913, product build `acc-0691b`,
30 fps cap) [L].** Three passes; only the third is usable:
- Pass 1 (jobs `114005-3b87` stock, `114041-cce3` product): VOID. The laptop's queue ran both arms at once (tb1 and
  tb2, both windows at 0,0), and `--walk-crop auto` gave 2342 x 2056 at 3 zoom ticks on the 3825 x 2087 client, which
  filled dupcap's 1,500 MB buffer after 81 frames (~3 s, rc 4 in every case).
- Pass 2 (`115725-c418`, `120737-96a7`; serial, fixed crop 1700 x 1200): complete recordings (180 frames, 6 s), but
  the units walked one straight leg: the appended second `MOVE_TO` (`--walk-order waypoints`) never ran; after 6 s the
  unit stood on B with moves left (`where_after` `104,23 moves=180`, of 240) [L; cause in the DLL's mission queue not
  established: `CvUnitMission::PushMission` appends with bAppend, the head's completion does not start it here].
- Pass 3 (`121846-1cea` stock, `122629-e054` product; `--walk-order path --walk-bends 60`: one MOVE_TO C two plots from
  A, the pathfinder bends at the middle plot): every walk ended on C (`104,24`) in both arms; foot 2, mounted 2 each.
  `turn0685.py jobs` (`F:/c5-64bit-work/0673/cap/turn0685c.tsv`, strips `strips0685c/`): facing turn read in 3 stock
  walks, **171, 201, 233 ms** (foot r2, mounted r2, mounted r1), and in 1 product walk, **95 ms** (foot r1); the other
  four read "the axis changed 3.5-9.9 degrees between the legs: read the strip"; `root not read (no crossing)` in all
  eight. By eye on the mounted r1 strips (row 3, the turn toward C): stock rotates the horses over about 4-6 frames
  (133-200 ms at the cap), product in about 1-2 frames [I: one pair read by eye; the measured contrast is 3 stock
  against 1 product reading]. This agrees with the reporter's report (a snap at the bend in the product, a smooth turn
  in stock) but does not yet prove it; the next step is a fix design in the units' animation code and a repeat of
  pass 3 (more repeats) for the numbers.
  *(Corrected 2026-11-24 by unit-server-scene.md 19.8: by eye on full-size frames stock turns the formation in about 2
  frames, under 70 ms, and the product's facing change comes about 300 ms after stock's; the 171-233 ms stock readings
  above are the axis reader's noise, which 19.6 took as stock's turn time.)*

### 5.3 Not verified

- One machine (the laptop), one save and camera, the load's zoom, the 30 fps cap. A frame-lag result needs the
  server's ready - publish above the bound, so a faster GPU may show less of it (the workstation's ab1/ab2 arms in
  section 2). *(Corrected 2026-11-23 by section 6: the reporter's machine and rate are now known from the session's
  logs: the workstation's RTX 4080, vsync on, about 165 presents a second, so the server's budget there is about 6 ms,
  not 33 ms, and older frames are more frequent than on the laptop; was "The reporter's machine and frame rate are not
  known.")*
- Three runs per arm for keys and drags; the spread between runs is in the table, no confidence interval is claimed.
- No resource icon was tracked: none was in the crop at this camera (the city banner and three unit flags were).
- Wheel zoom was not recorded.

### 5.6 After 0691 (exact lockstep), same inputs: NOT RUN YET (laptop display idle-off) [L: the probes; C: the build]

*(Superseded 2026-11-23 11:4x: the run was made, with the later build `acc-0691b`; results in 5.9. This section keeps the
2026-11-22 baseline table that 5.9 compares with.)*

Written 2026-11-23 02:3x by worker-0673i under lead-0673 (branch `claude/lead-0673-holistic-d0fd9d`).

- **Why no numbers.** Inside MC-B's laptop grant (#11055, start moved to 01:5x by #11079) 13 one-minute capture
  probes (`capture_job.py --probe`) ran from 01:58 to 02:29 EST and every one exited 3: `DuplicateOutput 0x80070005
  E_ACCESSDENIED`, the trap of runbook 4.15 (the console display idle-off; session 1 Active, the monitor reported
  `Active` by WMI all the same). Nobody was at the laptop to wake the display, so the product arm was not submitted, no
  game started and nothing on the laptop changed. Jobs `015855-9f85`, `020019-4112`, `020323-2c5f`, `020423-ba34`,
  `020725-70ac`, `020815-fc4e`, `020909-b5a7`, `021117-0483`, `021417-20d0`, `021720-1d9b`, `022018-780e`,
  `022318-a715`, `022619-9819` (three of them extra, from a job-id slip in the probe loop: the ids end in hex).
- *(Corrected 2026-11-23 03:2x by 5.7: NOT ready to run unchanged. With `lockstep_mode=exact` in the ini at the load,
  the first map frame deadlocks and the hung route ends the game after 30 s (5.7 Result 1); the product arm below loads
  with the repository ini, so it would end the same way until that is fixed. Was: "What is ready to run unchanged".)*
- **What was staged for the run.** The build of 0141a06c6 (0691's code a81eda0be + main, which brought 0681's
  `proxy_rt.cpp` and 5054's log thread): server.exe `f1652316b7603dbd...`, the x86 proxy `cafc7843cee0bd5b...`, the
  composite `42007145b8de68d8...`, the x64 DLL `b84aa3e9...` (the pin, unchanged), staged for the job in
  `C:/c5-64bit-work/0673/acc-0691/ship/` (`bin/`, `proxy/`, `composite/`). No job change was needed: `product_path.py`
  copies the repository's `shell_composite.ini` (`lockstep_mode=exact`, `lockstep_hung_ms=30000`) and sets
  `lockstep_csv=1`. The run: `SHIP=C:/c5-64bit-work/0673/acc-0691/ship sh server/evidence/0673/acc0691_laptop.sh` (a
  probe every 3 min for 30 min, then at once the product arm: keys 150/300/600/1200 and drags 300/1000 ms, 3 repeats,
  TB-HUGE_0350, the reporter's `[Renderer]` set, the 30 fps cap as in 5.1). Then fetch with `--max-mb 0.5` and
  `python -X utf8 server/evidence/0673/caplock.py --job <fetched job>` (F5 per recording), `capbatch.py` (k, d0,
  freezes) as in 5.2.
- **The workstation checks of that build passed** (2026-11-23 01:52-02:06) [L stand-in]: `test_lockstep.py -k exact_`
  10/10 (test (d): `SendMessage` answered at 7.02 s = the frame's end, the hung mark at 4.52 s), the other 37 cases of
  the file passed (bounded mode), `test_proxy_rt.py` 219/219 and `--only-borrowed` 19/19,
  `test_served_exception_text.py` PASS (3 scenarios), `test_vp64helper.py` 225/225.
- **The baseline the run will be compared with: last night's product F5** (`caplock.py --job` on jobs `230441-8a8b` and
  `232043-c899`, the bounded lockstep at 8,000 us; F5 = the share of displayed frames in a camera move whose newest
  composite present is `exact`; stock is 100 % by construction, it has no older picture to show) [L]:

| case | F5 per run (%) | F5 all runs | taken frame's ready - its publish, p50 per run (ms) |
|---|---|---|---|
| keys 150 | 6.8, 5.1, 14.3 | 11 / 125 = 8.8 % | 25.9, 27.2, 22.5 |
| keys 300 | 7.4, 7.7, 11.1 | 14 / 160 = 8.8 % | 32.6, 27.6, 24.1 |
| keys 600 | 15.2, 12.2, 25.3 | 40 / 228 = 17.5 % | 24.0, 23.9, 21.2 |
| keys 1200 | 14.8, 19.8, 23.6 | 64 / 329 = 19.5 % | 26.5, 22.7, 21.7 |
| drag 300 right | 14.3, 33.3, 37.5 | 6 / 21 = 28.6 % | 20.2, 20.9, 19.0 |
| drag 300 left | 33.3, 28.6, 37.5 | 10 / 31 = 32.3 % | 21.9, 19.9, 24.8 |
| drag 1000 right | 7.4, 16.1, 16.1 | 12 / 89 = 13.5 % | 25.2, 17.8, 21.8 |
| drag 1000 left | 12.1, 18.5, 14.3 | 13 / 88 = 14.8 % | 20.7, 24.6, 19.4 |
| **all** | | **170 / 1071 = 15.9 %** | |

  Displayed frame interval p50 33.2-33.4 ms in every recording (the cap). In camera moves the server's ready came
  18-33 ms after the publish of the frame taken (p50), longer than the 14.7 ms over all frames of 5.2: so with an
  exact wait at 30 fps the laptop's frame may run past 33 ms while panning [I, from these rows; to measure].
- **Not verified**: everything the acceptance asks (F5 in camera moves with `exact`, freezes, k and d0, the frame rate,
  `hung_ends` = 0 on the laptop), and the owner's D237 check.

### 5.7 INTERIM: 0691 on the workstation (hidden instance), CSV-only [L, 2026-11-23 02:49-03:19 EST, c5h-c, EXE 4390913]

Written 2026-11-23 03:2x by worker-0673k under lead-0673 (branch `claude/lead-0673-holistic-d0fd9d`), on the MC's
#11113 (an interim check; the laptop caplock run of 5.6 and the owner's D237 check stay the acceptance). MC-B's grant
#11122 (c5h-c, 02:37-03:45).

**Build and lay-out.** The build of 5.6 (0141a06c6): server.exe `f1652316b7603dbd...`, the x86 proxy
`cafc7843cee0bd5b...`, the composite `42007145b8de68d8...`, the x64 DLL `b84aa3e9...`; laid by
`server/evidence/0691/live0691.sh c5h-c prep` (`lay.py prep` from `C:/c5-64bit-work/0673/acc-0691/build` and
`shell-build/bin/shell_composite.dll`, the repository's `shell/composite/shell_composite.ini` at HEAD with
`lockstep_csv=1`; the laid hashes checked), `helper on`, the reporter's `[Renderer]` set (ServerRenderer, ServerUnits,
ServerCities, ServerLandmarks, ServerFarmFields, ServerCombat = 1, ServerArc, ServerPath = 0). c5h-c is EXE build
4390913 (TimeDateStamp 0x546CD0A8, `exe_build.describe`); the build's EXE table holds both builds. Save: a copy of
C5H-0167-TINY_0008 (SHA-256 `b2ceb663...`, put in and taken out by hash with `saves_snap.py`), turn 9, AI autoplay set
to 0 after the load. Hidden all the time: the harness's cloaked, parked launch, focus guard on (no `guard off`).

**Method.** `drive0691.py`: 0684's inputs (`tools/hi/actions.hold` LEFT then RIGHT, 150/300/600/1200 ms, 1 s apart;
`actions.drag` from 40 % to 65 % of the client width at 55 % height and back, 300 and 1000 ms, 1.5 s apart; 3 repeats),
each input's start and end as QPC in `marks.tsv`. `an0691.py` cuts the launch's `shell_composite.lockstep.csv` (moved out
of Logs after each stop, one header per launch) per input from its start to its end + 1 s. **F5csv** = the share of
composite presents IN A CAMERA MOVE (camera moved > 0.5 world units since the previous present) that are kind 0 (the
server picture of that frame's own camera). This is caplock.py's F5 with the composite's presents in place of the
displayed frames: no capture runs on a hidden window. Waits = wait1 - wait0 of every present in the windows; fps =
1000 / mean present interval. Per launch from the composite log's `lockstep 1s:` lines, summed per mode: `giveups`,
`hung_ends`, `dead_ends`, `wait_off`, LOUD lines. Results per series: `server/evidence/0691/results-interim.tsv`.

**Launches** (the arm's mode was switched live in the laid ini, which the composite reads again each second):

| launch | cap | mode at the load | arms, in order | outcome |
|---|---|---|---|---|
| L1 02:49 | uncapped (`--timing`) | exact | (none) | **deadlock at the first map frame, the game ended, exit 3 (below)** |
| L2 02:58 | uncapped | bounded | bounded, exact | the bounded arm VOID: a stray copy of L1's script drove the same game at the same time (doubled inputs) |
| L3 03:05 | capped (harness default, 30 fps) | bounded | exact, bounded | clean |
| L4 03:12 | uncapped | bounded | bounded, exact | clean |

**Result 1: with `exact` at the load the first map frame deadlocks** [L, L1]. The composite's first in-game frame with
a publish (frame 23 inserting `pub_f` 24) waited 30,009 ms (`kind` 2 none). The server held frame 24: "fog: frames held
from frame 24: no plot table from the feed", "residency: held frame 24: the visible set's preparation waits for the plot
table's tile styles of load 3 (no table yet)", "live terrain: waiting for the bake's plot table" (02:52:14), and the fog
install "waiting on 1 map frame(s) presented by the EXE ... the wait ends after 10000 ms once the EXE has presented 30"
(02:52:24). The overlay and shadow taps' first copies are published at Present 24, after the insertion, and the water
and decal taps' acknowledgements came only after the wait ended. So the server waits for EXE frames and the EXE's frame
waits for the server: at 30.0 s the no-progress limit ended the wait ("LOUD: lockstep: the server is HUNG: no frame
delivered for 30.0 s, heartbeat alive (heartbeat age 0.1 s); frame 24 not drawn", `hung_ends` 1), the proxy took the
hung route ("FATAL: the server hung in a D33 session (no play, D125): the process ends with exit code 3"). No box (the
dialog watcher saw none; `pack=0`, so no report). In a player's session this would be the D118/D240 failure at every
load. **Root cause [L, logs; mechanism I]:** 0691's design point 2 assumed no frame to wait for until the server is
READY; the ring is READY long before the server can draw a map frame, and the server's first map frame depends on
inputs that only later EXE frames deliver. The stand-in server of tests a-g draws at once, so they could not see it.
Not fixed here (lead-0673's call); fixed offline later the same day by the server's hold report (2026-11-23,
renderer-in-server-design.md 3.2.1; live 04:42 on c5h-c: frames 24-25 skipped, exact from frame 26, no hung end). Not tried on EXE 26577625 [I: nothing in the mechanism is build-specific].
Evidence (not in git): `C:/c5-64bit-work/0673/live-0691/L1-run/` (render.log, server.log), `L1-exact-uncapped.*`.

**Result 2: loaded bounded and switched to exact in game, the exact wait works** [L, L2-L4]. In every exact arm
F5csv = 100 %, every camera-move frame age 0, and in the seconds the composite ran exact: `giveups` 0, `hung_ends` 0,
`dead_ends` 0, `wait_off` 0, `older` 0 (bounded seconds of the same launches: `giveups` 3 / 1 / 24, `older` 291 / 18 /
1,697). No LOUD line in L2-L4. ms; F5csv as % (camera-move presents):

| series | L4 exact, uncapped: F5 / wait p50, p90, p99, max / fps | L4 bounded (8,000 us), uncapped | L2 exact, uncapped | L3 exact, 30 fps cap | L3 bounded, 30 fps cap |
|---|---|---|---|---|---|
| keys 150 | 100 (219) / 0.55, 1.31, 3.79, 20.1 / 136 | 68.4 (193) / 3.04, 4.32, 5.48, 5.89 / 100 | 100 (211) / 0.66, 3.19, 4.12, 5.41 / 138 | 100 (128) / 0.93, 1.80, 18.4, **364** / 28.7 | 100 (130) / 0.84, 1.47, 2.03, 2.13 / 30.0 |
| keys 300 | 100 (319) / 0.71, 1.43, 4.08, 6.93 / 134 | 83.0 (300) / 2.59, 3.99, 4.89, 5.48 / 104 | 100 (343) / 0.72, 1.36, 3.74, 4.48 / 151 | 100 (168) / 0.86, 1.67, 4.59, 9.35 / 30.0 | 100 (170) / 0.78, 1.41, 1.99, 2.83 / 30.0 |
| keys 600 | 100 (586) / 0.67, 1.30, 4.02, 5.87 / 137 | 86.4 (450) / 2.87, 3.96, 4.78, 5.86 / 102 | 100 (588) / 0.77, 2.93, 4.00, 7.85 / 139 | 100 (229) / 0.82, 1.51, 3.84, 5.52 / 30.0 | 100 (234) / 0.94, 1.47, 2.18, 2.81 / 30.0 |
| keys 1200 | 100 (914) / 0.57, 1.45, 4.32, 13.3 / 137 | 76.8 (668) / 2.92, 4.02, 4.73, 6.60 / 99 | 100 (874) / 0.74, 3.29, 4.09, 5.43 / 141 | 100 (302) / 0.84, 1.44, 2.59, 4.12 / 30.0 | 100 (304) / 0.82, 1.48, 2.05, 2.45 / 30.0 |
| drag 300 right | 100 (36) / 0.43, 1.42, 7.50, 10.2 / 131 | 72.2 (36) / 2.15, 3.98, 5.08, 6.10 / 102 | 100 (35) / 0.74, 1.52, 3.82, 4.47 / 149 | 100 (28) / 0.68, 1.18, 1.78, 1.97 / 30.0 | 100 (39) / 0.80, 1.35, 1.71, 2.09 / 30.0 |
| drag 300 left | 100 (36) / 0.47, 1.33, 7.59, 35.5 / 132 | 91.7 (36) / 0.78, 3.78, 5.06, 6.82 / 105 | 100 (37) / 0.63, 2.25, 3.71, 5.10 / 140 | 100 (29) / 0.83, 1.33, 1.90, 6.74 / 30.0 | 100 (30) / 0.78, 1.27, 1.72, 2.39 / 30.0 |
| drag 1000 right | 100 (120) / 0.53, 2.00, 7.79, 38.6 / 123 | 87.4 (119) / 0.88, 3.86, 4.43, 5.71 / 110 | 100 (120) / 0.68, 3.14, 4.60, 26.3 / 135 | 100 (96) / 0.78, 1.35, 1.74, 2.44 / 30.0 | 100 (99) / 0.80, 1.54, 2.04, 2.59 / 30.0 |
| drag 1000 left | 100 (119) / 0.46, 1.78, 11.0, 49.8 / 127 | 78.0 (118) / 1.81, 3.93, 5.16, 6.22 / 105 | 100 (119) / 0.63, 1.35, 3.90, 6.83 / 152 | 100 (95) / 0.76, 1.48, 1.70, 1.76 / 30.0 | 100 (95) / 0.88, 1.57, 2.11, 3.62 / 30.0 |
| **all** | **100 (2,349)** / 0.54, 1.43, 5.03, 49.8 / 132 | **80.1 (1,920)** / 2.44, 3.98, 5.02, 6.82 / 103 | **100 (2,327)** / 0.71, 2.42, 4.05, 26.3 / 143 | **100 (1,075)** / 0.81, 1.45, 4.12, 364 / 29.8 | **100 (1,101)** / 0.83, 1.46, 2.11, 3.62 / 30.0 |

- Ages in camera moves, L4 bounded: 0: 1,538, 1: 356, 2: 23, 3: 3 (the older picture shown 1-3 frames late); every
  exact arm: age 0 only. The void L2 bounded arm read 97.8 % (3,211) with the doubled inputs.
- Present interval (all series): L4 exact p50 7.08 / p99 16.0 / max 115.7 ms; L4 bounded 9.03 / 20.9 / 88.3; L2 exact
  6.16 / 19.6 / 42.0; L3 both arms p50 33.3-33.3, p99 34.6-35.9 ms (the cap holds), L3 exact max 372 ms (the 364-ms wait).
- The taken frame's server ready - its publish, p50 3.8-4.4 ms and p90 4.3-4.9 ms in the exact arms (8.3 ms p90 in L4's
  bounded arm): this Tiny save on the workstation is far faster than the laptop's TB-HUGE_0350 (14.7 ms p50, 5.2), so
  bounded mode already gets 80-98 % uncapped and 100 % at 30 fps here; the laptop decides.
- **No speed claim** (docs/briefs/PERFORMANCE.md): uncapped, L4's bounded arm ran at 103 fps and its exact arm at 132,
  but L2's exact arm ran at 143 and the void bounded one at 148: the order (bounded first, just after the load and the
  units' 28 s pre-check) and the other agents' load on the workstation move the rate more than the mode [I]. With the
  30 fps cap neither mode costs a frame (29.8 vs 30.0, the one 364-ms wait).
- **The 364-ms wait** (L3, the first second of the exact arm, frame 859, 03:08:34, exact): one server frame took that
  long. Not root-caused [I: near the end of the units' 28-s pre-check, which ended at 03:08:35.8, about 1.5 s later].
  No hung end, no box; with bounded mode that frame would have shown older pictures instead.

**What this does not cover.** No displayed-frame capture (the window is hidden: F5csv counts composite presents, not
what reached the screen; k, d0 and freezes of 5.2 need the laptop); not the laptop and not EXE 26577625; one Tiny save
at turn 9 (no big save with many units: no 26577625 instance was free); IsHungAppWindow / "Not Responding" not read
(hidden window; no box seen); the owner's D237 check. The acceptance stays 5.6's laptop run and the owner's check, and
both need Result 1 fixed first (the laptop job's product arm loads with the repository ini, `exact`).

**Closing state.** Harness stop exit 0 for L2-L4 (L1's game had ended itself), `lay.py restore` ok (with `provision.py
verify --quick`), `leftovers` clean, the save copy taken out by hash, a `VP64/reports` folder of 2026-11-21 that was on
the instance before (not this run's) moved aside for `lay.py prep` (it refuses a user `VP64`) and put back by hash,
`verify --quick` ok, released 03:21. Scripts: `server/evidence/0691/` (live0691.sh, drive0691.py, an0691.py,
results-interim.tsv); runbook 4.16. Outputs: `C:/c5-64bit-work/0673/live-0691/` (not in git).

**After the hold fix (c5bffa406), the same instance, exact AT THE LOAD [L, 2026-11-23 04:4x, c5h-c, EXE 4390913, Tiny
save copy, 30 fps cap].** Frames 24 (reasons 0x11: no plot table + install) and 25 (0x18: no overlay pair + install)
were reported held and skipped (no world, as at a load before), 794 ms in all; the fog installed at frame 26; then
2,298 of 2,298 frames exact, hung_ends 0, held_skip 0 in all 79 later seconds; the pan series F5csv 100 % (351/351),
waits p50/p90/p99/max 0.77/3.46/10.0/357 ms, 29.6 fps. **The held frame's wait for the camera read (accepted, MC
#11226):** a held frame is skipped only once the server has read its camera and reported the hold; during the load the
server's pass was busy, so frame 24 waited 1,126 ms and frame 25 558 ms before their holds were seen. Load only, not a
deadlock, under the hung limit; a "sticky" hold (skip every frame at or after a held one) would remove it but is not the
per-frame report the design asks for. Details: renderer-in-server-design.md 3.2.1.

**Repeat on EXE 26577625 [L, 2026-11-23 06:11-06:25 EST, c5h-new-a, hidden, guard on, `--timing` uncapped, Tiny
C5H-4773-TINY_0018, exact AT THE LOAD, builds of fog-of-war.md 12.6, workstation at 100 % CPU].** Two launches: frames
25-28 (L1) and 25-27 (L2) held and skipped, all reasons 0x11 (fog-no-table + fog-install), the first after 1,206 / 1,337
ms until the hold was seen, the run 249 / 167 ms; fog installed at frame 29 / 28; then held_skip 0, hung_ends 0,
giveups 0, older 0 in all 145 / 163 seconds (27,046 / 24,705 exact frames); one key + drag series each (`REPEATS=1`):
F5csv 100 % (1,192 / 1,192 and 986 / 986 moving presents), waits p50 / p99 / max 2.01 / 5.14 / 244 ms (L1; the 244 ms
wait is in the first, 150 ms key series) at 186 fps. LOUD lines: the held skips only. Logs `C:/c5-64bit-work/0673/live-0692/`.

### 5.8 0689: the server's pass split by stage with 0688's instruments (workstation), the laptop split by camera move, and what the laptop run must record [L: logs; C: code read; I: marked]

Written 2026-11-23 07:0x EST by worker-0673q under lead-0673 (branch `claude/lead-0673-holistic-d0fd9d`). Read only:
no build, no game. Reader: `server/evidence/0689/split0689.py` (runbook 4.18): render.log's `pass 1s:`, `pass long:`
and `host 1s:` lines, the lockstep CSV, the marks. Raw outputs not in git.

**What 0688's lines give.** `pass 1s:` carries per stage the second's MEAN per pass and its MAXIMUM; per-pass stage
values exist only in `pass long:` lines (busy >= `render_pass_long_ms`, 20 ms). So per stage below: ms per drawn frame
(sum of mean x passes / frames drawn), its share of the pass's busy time, and the per-second maximum's p90 / max; the
per-frame percentiles of the whole come from the CSV (one row per server frame here: every run is `exact`, D242). The
script also prints each stage's per-second mean p50 / p90 / p99. Order in a pass [C, `Loop` and `DrawFrame` in
`server/src/render/srv_render_host.cpp`]: polls, feed, decide, then slot, predraw, the scene's layers (draw, fog,
terrain, water, forest, models, units), handover; then idle, rec, log; then the wait. Everything up to `handover` lies
between the wake and ready; `idle` and `log` run after the frame is ready and reach the NEXT frame only as wake delay,
when its camera comes during them. `draw` holds the fog's particle call (`afterWorld`, fog-of-war.md 12.6).

**Runs.**

| run | instance, EXE, time (EST) | server | save, pace | 0688 lines |
|---|---|---|---|---|
| L1 | c5h-new-a, 26577625, 2026-11-23 06:13-06:17 | of `aed29d9e8` (0688, 0691 + hold report, 0692 cuts, 5056 stage 1), 4 particle step threads | Tiny `C5H-4773-TINY_0018` t18, uncapped (`--timing`), hidden | yes |
| L2 | the same, 06:21-06:24 | the same, `C5_RENDER_PARTICLE_THREADS=1` | the same | yes |
| H1 | c5h-c, 4390913, 04:39-04:43 | the hold-fix run's (5.7, branch at `c5bffa406`: 0688, the 0692 cuts and 5056 stage 1 are its ancestors) | Tiny `C5H-0167-TINY_0008` t9, 30 fps cap; one key and drag series, no wide hold | yes |
| live-0691 L2-L4 (5.7) | c5h-c, 4390913, 02:58-03:19 | of `0141a06c6` | Tiny t9 | **no** (before 0688: `host 1s` and the CSV only) |
| laptop `230441-8a8b` (5.5) | laptop, 2026-11-22 23:0x | main before 0688, bounded 8,000 us | TB-HUGE_0350 t350, 30 fps cap + vsync | **no** (CSV and `host 1s` only) |

Load: L1 and L2 ran on a workstation at 100 % CPU with a second game instance (fog-of-war.md 12.6); H1's load was not
recorded (other teams' jobs ran, 5.7). Every workstation number here is an INDICATION (grade C, COMMON 23), not a timing
claim. Camera classes: a present "moved" when the camera moved more than 0.5 world units since the present before;
seconds are `moving` when 25 % or more of their presents moved, `rest` when none did (city view at the load's zoom),
`wide hold` = the 60 s still view zoomed out (`wide0692.py`); seconds partly moving are in the script's output only.

**Per frame (CSV), ms p50 / p90 / p99 / max.**

| run, class | server frames | wake - publish | submit - wake (the drawing pass) | ready - publish | the EXE's own work (insertion - publish) | composite wait p50 / p90 / p99 (presents that waited) | present interval p50 / p99 | ready - publish over 6.06 / 8 / 16.7 / 33.3 ms |
|---|---|---|---|---|---|---|---|---|
| L1 moving | 1,192 | 0.02 / 0.95 / 2.89 / 9.7 | 2.02 / 2.79 / 10.9 / 246 | 2.05 / 4.12 / 12.4 / 246 | 2.14 / 4.81 / 8.97 / 12.7 | 0.28 / 3.66 / 10.9 (522) | 3.65 / 15.2 | 5.5 / 4.8 / 0.34 / 0.08 % |
| L1 rest | 7,500 | 0.02 / 0.02 / 1.93 / 15.0 | 1.94 / 4.16 / 5.39 / 15.6 | 1.96 / 4.24 / 6.35 / 17.3 | 2.02 / 3.05 / 5.87 / 20.9 | 0.27 / 2.30 / 7.88 (3,729) | 3.30 / 12.0 | 1.1 / 0.67 / 0.01 / 0 % |
| L1 wide hold | 9,508 | 0.02 / 0.02 / 0.03 / 2.7 | 4.66 / 5.37 / 6.70 / 61.2 | 4.68 / 5.39 / 6.73 / 61.2 | 2.57 / 3.43 / 6.57 / 20.9 | 2.10 / 2.59 / 4.08 (9,355) | 6.06 / 12.6 | 1.7 / 0.50 / 0.08 / 0.03 % |
| L2 moving | 986 | 0.02 / 1.97 / 3.27 / 15.2 | 2.39 / 3.12 / 12.1 / 26.5 | 2.43 / 5.23 / 14.4 / 31.3 | 2.74 / 5.54 / 10.8 / 28.0 | 1.04 / 8.01 / 16.6 (297) | 4.52 / 21.6 | 7.6 / 6.8 / 0.61 / 0 % |
| L2 rest | 8,500 | 0.02 / 0.02 / 0.05 / 18.7 | 2.37 / 3.32 / 7.42 / 58.6 | 2.39 / 3.35 / 7.50 / 59.3 | 2.58 / 3.72 / 6.77 / 32.9 | 0.25 / 3.41 / 7.65 (3,151) | 4.13 / 15.2 | 6.4 / 0.71 / 0.06 / 0.01 % |
| L2 wide hold | 7,031 | 0.02 / 0.02 / 0.03 / 0.5 | 6.38 / 7.17 / 9.24 / 23.6 | 6.40 / 7.20 / 9.27 / 23.6 | 2.97 / 3.82 / 9.60 / 54.7 | 3.50 / 4.42 / 5.92 (6,894) | 8.09 / 16.7 | 77.1 / 3.9 / 0.06 / 0 % |
| H1 moving (30 fps) | 351 | 0.03 / 0.04 / 4.93 / 15.1 | 4.10 / 5.13 / 14.1 / 361 | 4.17 / 6.42 / 14.5 / 361 | 5.00 / 8.35 / 10.6 / 13.9 | 0.95 / 4.96 / 10.0 (104) | 33.36 / 35.4 | 12.0 / 5.7 / 0.57 / 0.28 % |
| H1 rest (30 fps) | 479 | 0.03 / 0.04 / 0.05 / 4.0 | 3.97 / 4.73 / 8.19 / 26.9 | 4.02 / 4.79 / 8.22 / 27.0 | 4.67 / 6.17 / 8.36 / 15.8 | 0.67 / 1.41 / 9.48 (139) | 33.30 / 35.0 | 2.5 / 1.0 / 0.21 / 0 % |
| laptop moving (bounded; each server frame once) | 515 | 0.04 / 10.5 / 25.3 / 31.9 | 20.2 / 32.4 / 53.1 / 121 | 21.3 / 37.2 / 56.8 / 125 | 13.6 / 24.7 / 42.3 / 1,467 | (the wait ends at 8 ms) | 33.34 / 54.4 | 100 / 100 / 67.8 / 13.8 % |
| laptop rest | 4,125 | 0.04 / 5.09 / 20.0 / 429 | 13.0 / 21.5 / 34.3 / 891 | 14.1 / 24.0 / 40.1 / 889 | 8.20 / 21.7 / 67.6 / 1,078 | - | 33.36 / 73.9 | 99.7 / 98.5 / 34.8 / 2.5 % |

Ready - submit is 0.00-0.01 ms p99 in every run (`ring_sync=fence`). On the laptop each server frame is counted at the
first present that showed it and against its own camera's publish (`srv_cam_pub_qpc`); its "moving" is the camera of
that present [I: in a bounded run the EXE's camera may differ from the server frame's]. Over all frames this agrees with
5.5 (ready - publish p50 14.69 by lkcsv.py).

**Per stage (`pass 1s`), ms per drawn frame (share of the pass's busy time).**

| stage | L1 moving (6 s) | L1 rest (15 s) | L1 wide hold (59 s) | L2 moving (7 s) | L2 rest (22 s) | L2 wide hold (59 s) | H1 moving (17 s) | H1 rest (6 s) |
|---|---|---|---|---|---|---|---|---|
| **busy per drawn frame** | **2.67** | **2.68** | **4.73** | **3.30** | **3.36** | **6.46** | **6.82** | **3.40** |
| polls | 0.13 (5 %) | 0.12 (4 %) | 0.19 (4 %) | 0.11 (3 %) | 0.18 (5 %) | 0.21 (3 %) | 0.57 (8 %) | 0.33 (10 %) |
| predraw (pre-draw bakes, copies) | 0.48 (18 %) | 0.25 (9 %) | 0.37 (8 %) | 0.56 (17 %) | 0.27 (8 %) | 0.39 (6 %) | 0.43 (6 %) | 0.32 (10 %) |
| draw (with the particle call) | 0.43 (16 %) | 0.75 (28 %) | 1.65 (35 %) | 0.48 (14 %) | 0.93 (28 %) | 3.03 (47 %) | 0.55 (8 %) | 0.49 (14 %) |
| fog (the mask) | 0.04 (2 %) | 0.05 (2 %) | 0.07 (1 %) | 0.06 (2 %) | 0.06 (2 %) | 0.07 (1 %) | 0.09 (1 %) | 0.10 (3 %) |
| terrain | 0.68 (25 %) | 0.70 (26 %) | 1.16 (24 %) | 0.85 (26 %) | 0.96 (29 %) | 1.32 (20 %) | 1.29 (19 %) | 0.91 (27 %) |
| water | 0.09 (3 %) | 0.10 (4 %) | 0.16 (3 %) | 0.12 (4 %) | 0.13 (4 %) | 0.17 (3 %) | 0.28 (4 %) | 0.24 (7 %) |
| forest | 0.29 (11 %) | 0.38 (14 %) | 0.70 (15 %) | 0.34 (10 %) | 0.40 (12 %) | 0.76 (12 %) | 0.53 (8 %) | 0.51 (15 %) |
| models | 0.06 (2 %) | 0.06 (2 %) | 0.10 (2 %) | 0.02 (1 %) | 0.09 (3 %) | 0.12 (2 %) | 0.10 (1 %) | 0.07 (2 %) |
| units | 0.01 | 0.01 | 0.02 | 0.01 | 0.02 | 0.02 | 0.64 (9 %: one 338-ms pass) | 0.02 |
| handover | 0.18 (7 %) | 0.19 (7 %) | 0.30 (6 %) | 0.19 (6 %) | 0.24 (7 %) | 0.28 (4 %) | 0.46 (7 %) | 0.34 (10 %) |
| idle (after ready) | 0.27 (10 %) | 0.03 (1 %) | 0.00 | 0.55 (17 %) | 0.03 (1 %) | 0.01 | 1.81 (27 %) | 0.00 |
| per-second max p90 / max: predraw | 10.3 / 10.3 | 0.5 / 8.8 | 0.6 / 0.7 | 11.9 / 11.9 | 0.5 / 8.3 | 0.6 / 0.6 | 5.3 / 5.6 | 0.5 / 0.5 |
| ... draw | 2.3 / 2.3 | 2.4 / 5.5 | 2.2 / 14.5 | 8.9 / 8.9 | 7.2 / 12.6 | 11.2 / 20.3 | 1.1 / 11.5 | 0.7 / 0.7 |
| ... handover | 1.3 / 1.3 | 0.6 / 0.6 | **16.0 / 56.7** | 0.6 / 0.6 | 0.8 / 45.7 | 0.6 / 0.8 | 4.8 / 11.1 | 2.2 / 2.2 |
| ... idle | 7.6 / 7.6 | 0.1 / 6.8 | 0.0 / 0.3 | 16.7 / 16.7 | 0.0 / 7.8 | 0.0 / 0.0 | 10.9 / 24.3 | 0.0 / 0.0 |

The feed, decide, slot, rec, log and other stages are each under 0.06 ms per frame (slot's per-second max reaches
5.7-10.3 ms in a few seconds: 3302's busy retry for a free slot).

**Reading.**
- **At the median no single stage holds the time on the workstation now.** The ready-to-publish time IS the drawing
  pass: the wake costs 0.02-0.03 ms at p50 and ready - submit 0.00-0.01 ms. The pass is 1.9-2.4 ms in the city view
  and 4.7-6.4 ms in the wide view: terrain 20-29 %, draw (with the particle call) 14-47 %, forest 10-15 %, predraw
  6-18 %, handover 4-10 %, polls 3-10 %, units 0.3-0.5 % (Tiny saves at turns 9-18: few units, unlike the laptop's
  733). The particle cut is the one stage that moved between L1 and L2: the wide hold 4.73 -> 6.46 ms per frame with one
  step thread, `draw` 1.65 -> 3.03 ms (fog-of-war.md 12.6).
- **The tail in camera moves is `predraw`.** 4.8 % (L1), 6.8 % (L2) and 5.7 % (H1) of the moving frames have ready -
  publish over 8 ms. The script's attribution (the frame's long pass if it has one, else the stage with the largest
  per-second maximum in its second: an indication) names predraw for 52 of L1's 57 and 58 of L2's 67, the idle slice or
  the wake for 11 of H1's 20. predraw = the tile source's `beforeDraw`, the pre-draw bakes and copies of tiles entering the
  view: per-second maxima 5-12 ms in moving seconds, long passes of 20-34 ms with predraw on top (8-23 ms) at L1's first
  camera move (88 ms after the first key, 22.7 ms) and during the wheel zoom-out in L1 and L2 (8-12 ms, each with an
  idle slice of 3.8-7.8 ms in the same pass). *(2026-11-23, ticket 0703, terrain-bake-residency.md 8.1 and 8.4: in L1 /
  L2 those moving predraw maxima are the near pairs' up-res, 12-94 state-5 bakes a second, with 0 BC1 bakes before the
  draw; on the zoom-out the wake's tail is the previous pass's predraw, not an idle bake; the idle-bake part below holds
  for key pans and the rest.)* **The second part of the tail is the idle bake holding the
  wake** [C, `render/src/bakeresidency/residency.cpp`, the idle loop]: an idle slice checks for a waiting camera only
  before each `bakeOne`, so a camera that comes during a bake waits for its end; idle tops of 18-36 ms (long passes in
  L2 and H1) and wake - publish up to 15-19 ms (`wake_ms` 18.74 at L2 06:22:49.188).
- **Once per load: the units' first drawn frame**, one pass with `units` on top: 226 ms (L1 06:14:47.271, frame 5398),
  266 ms (L2 06:22:06.093), 338 ms (H1 04:42:55.089) (times of the line, written at the pass's end). Each pass starts
  within 3 ms of the units' pre-check's end (`units: pre-check of generation 1 ... 21244 / 21488 / 23773 ms`) and holds
  the opening of the units' drawing renderer (`units: the drawing renderer is open on the render device` comes 151-240
  ms into the pass) and their first frame [L, the log's times]. Under D242 each is a Present of the same length: the
  364-ms wait of 5.7 L3 and the 244-ms wait of the 26577625 repeat are this pass [L]. 0688 has one `units` mark, so the
  opening and the first frame are not split from each other [I: the opening on the render thread is the larger part by
  the line's time]. It is a load cost (once, about 22 s after the load), not a pan cost; moving the opening off the
  render thread would remove it.
- **The `handover` passes of 16-57 ms (worker-0673p's six, fog-of-war.md 12.6) are contention, not code** [L the
  numbers; I the cause]. Six in L1's wide hold (23.4, 23.9, 56.7, 16.0, 38.0, 32.3 ms; frames 16729, 17146, 17736,
  17904, 20040, 20210) and five in L2 (16.2, 40.2, 16.7 + 24.4 on consecutive frames, 45.7 ms). `handover` is the
  ring sink's end (`shell/frame/ring_sink.cpp` `RingSink::end`) [C]: `CopyResource` of the depth into the slot,
  `ID3D11DeviceContext4::Signal` on the fence, `Flush`; CPU-cheap, 0.18-0.30 ms per frame otherwise. In each of L1's six
  every other stage is normal (draw 1.6-1.8, terrain 1.1-1.5, forest 0.6-0.8 ms), the frames before and after are
  normal (submit - wake 4.3-5.6 ms), the terrain's GPU timer of the frames around it is unchanged (0.13-0.18 ms), the
  EXE's own work is unchanged (insertion - publish 2.1-4.2 ms), and there is no video-memory pressure (`vidmem 10s`:
  server 1.06 GB and EXE 0.77 GB local against a 15.3 GB budget). So the time is a block inside the driver's submission
  (the `Flush` or the `Signal` waiting in the kernel), not the code's own work; preemption spread evenly over the pass
  would put all six into a 6.5 % stage with p of about 1e-7. The machine was at 100 % CPU with a second game instance and
  other teams' GPU jobs on the same RTX 4080 (12.6): contention. Under D242 each is a hitch of the same length (the
  composite waited 17.8-58.8 ms; presents of 21-62 ms). A quiet machine should show none; the laptop run checks it.
- **L2's `draw` long passes** (19.9 and 20.3 ms, wide hold, one step thread) are the particle call's join (12.1); with
  four threads `draw`'s per-second maximum stays at 2.2 ms (p90). `polls` reaches 12.6-21.7 ms in a few moving seconds
  (the scene's taps; not split by 0688) [I].
- **Against the frame interval.** At the 30 fps cap (H1) ready - publish is 4.2 / 6.4 / 14.5 ms (p50 / p90 / p99) in
  moves, under 33.3 ms in 99.7 % of moving frames; the one frame over is the units' first frame. Uncapped (L1) the EXE's
  own work before the insertion is 2.0-2.6 ms at the median against the server's 2.0 (city) to 4.7 ms (wide): the
  composite waits in 44 % of moving, 50 % of city-rest and 98 % of wide-hold presents, so uncapped in the wide view the
  server's pass sets the frame rate (159 presents a second, interval p50 6.06 ms). Against the reporter's 165 Hz
  budget (6.06 ms, section 6): 5.5 % of L1's moving frames are over it, and 77 % of L2's wide-hold frames: the particle
  cut (4 step threads) is what keeps the wide view under the reporter's refresh.
- **The laptop, split by camera move for the first time** (step 1's CSV, bounded run): moving server frames ready -
  publish 21.3 / 37.2 / 56.8 ms (submit - wake 20.2 / 32.4 / 53.1), at rest 14.1 / 24.0 / 40.1. 13.8 % of the moving
  server frames take longer than the 30 fps interval (33.3 ms) and 67.8 % longer than 16.7 ms. Under D242's exact wait
  each frame over 33.3 ms costs at least one displayed frame while panning [I: how much depends on the EXE's own work,
  13.6 ms p50 in moves, that runs beside it]. The laptop's moving pass is 8-10x the workstation's at the median; its
  stage split is the open part of 0689.
- **Which part a fix would remove** (0689's acceptance, workstation): the wake: nothing at the median; its tail is the
  idle bake holding the next camera (a bake that yields mid-way, or no idle bake for a short time after the camera
  moved) [I]. The hand-over: nothing in the code (contention). The cross-adapter copy: none in this interval [C, the
  Architect's file 3]. The composite's wait: a consequence of the drawing pass, not a part. The draw: the camera moves'
  tail is predraw's bakes (the Architect's "prepare after publish" moves it out of the publish-to-ready path only if the
  bakes are for tiles the next camera needs, which a pan does not know in advance [I]); the median is terrain + draw +
  forest, which only a faster draw removes.

**What the laptop run (0691's laptop acceptance, 5.6) must record for 0689.**
- A server of `aed29d9e8` or later (0688's lines on by default, `render_pass_stats=1`); keep `render.log` whole (the
  `pass 1s:`, `pass long:`, `host 1s:`, `units 1s:`, `fog 1s` and `vidmem 10s:` lines), `server.log`, the composite's log
  (`pacing 1s:` / `pacing totals:`) and `shell_composite.lockstep.csv` (`lockstep_csv=1`, set by `product_path.py`), and
  the inputs' QPC windows if the job writes them (else the reader takes the whole run, the load included).
- **One test-only server.cfg line, `render_pass_long_ms=8`** (by `server_cfg_extra`, runbook 3.3 `lay.py cfgextra` /
  `set_ini.py`; unset before the restore): every pass over 8 ms then writes a `pass long:` line with all its stages (at
  most 20 a second, the rest counted in `long_unlogged`), which gives per-pass p50 / p90 / p99 per stage for the laptop's
  13-20 ms passes directly. Without it only passes of 20 ms or more are itemised and the median pass is seen only as
  per-second means.
- The same save, cap and inputs as 5.2 / 5.6 (TB-HUGE_0350 t350, keys and drags, the 30 fps cap with vsync on) and, if
  the grant allows, one minute still at the load's view (the rest class).
- Reading: `python -I -X utf8 server/evidence/0689/split0689.py --log render.log --csv shell_composite.lockstep.csv
  [--marks M --tag T] --json OUT`. It answers: the stage of the laptop's 13-20 ms (units against terrain against draw and
  particles against predraw), the wake delay, the count of `handover` long passes on a machine with nothing else on its
  GPU (the contention check of this section), and the units' first-frame pass on the huge save.

**Not verified.** Per-pass stage percentiles (0688 gives per-second means and maxima; the long-pass lines only from 20
ms); the moving classes rest on 6-17 seconds per run; Tiny saves only (the units' cost is not the huge save's);
contention as the cause of the handover blocks is an inference from the numbers (a quiet run decides); the "second's
largest maximum" attribution may name another pass of the same second; the laptop's stage split *(done 2026-11-23:
5.9)*.

### 5.9 0691's laptop acceptance: exact lockstep at stock parity; the laptop's 0689 split and 0692 figure [L, 2026-11-23 11:27-11:39 EST, laptop host clock]

Run by lead-0673 inside MC-B's grant (#11458, #11568) after the reporter woke the laptop's display (11:26). Probe job
`112638-c586` recorded at once; product job **`20261123-112740-fd80`** (11:27:40-11:39:02, rc 0). Build `acc-0691b` (the
build of `ec19daa90`: 0691 with its load-hold fix, 0692's cuts, 0697) from `C:/c5-64bit-work/0673/acc-0691b/ship`;
EXE build 4390913 on the laptop (`boot: exe build` line); TB-HUGE_0350 t350, tb1, the reporter's `[Renderer]` set, the
30 fps cap with vsync (as 5.1-5.6), `shell_composite.ini` as in the repository (`lockstep_mode=exact`,
`lockstep_hung_ms=30000`), test-only `render_pass_long_ms=8`. Inputs: keys 150/300/600/1200 ms and drags 300/1000 ms
left and right, 3 repeats each (24 recordings), then the `wide` case (30 wheel ticks out, 60 s still, 11:37:20-11:38:20,
not recorded). Fetch `F:/c5-64bit-work/0673/cap/acc0691b-20261123-112740-fd80` (logs only; frames stay on the laptop);
tables `f5-acc0691b.tsv`, `batch-acc0691b.tsv`, `split0689.json` and `marks.tsv` (written from `capture.json`'s marks)
beside it. Commands: runbook 4.15's 0691 block.

**0691 / 0684 / 0677: the interface and the world are in step in every displayed frame, as in stock.**

| | moving frames | F5 (exact share in camera moves) | freezes | recordings with banner lag k != 0 | fps |
|---|---|---|---|---|---|
| stock (`batch1.tsv`, 2026-11-22) | 1,237 | 100 % (no older picture exists) | 3 | 0 / 24 | 30 |
| product, bounded 8,000 us (5.6's table; `batch1` + `batch-prod23`) | 1,022 | 15.9 % | 169 | 15 / 23 | 30 |
| **product, exact (this run)** | **1,216** | **100 %** (1,015 / 1,015 presents, `caplock.py`) | **4** | **0 / 22** | 28.3-30.0 |

- Per recording F5 is 100.0 in all 24; `ages_moving={0: n}` throughout. The last `lockstep 1s` line: `mode=exact
  hung_ends=0 dead_ends=0`. Over the whole run the CSV's exact share is 0.9998 (`pacing.txt` F5): the rest are the load's
  frames before the first server picture.
- `capbatch.py`: banner and flag lag k = 0 in every recording that found the element, d0 (offset at lag 0) 0.3-1.9 px.
  Freezes 4 against stock's 3 (same inputs, 1,216 against 1,237 moving frames).
- The frame rate holds the cap: displayed interval p50 33.2-33.4 ms in every recording; fps per recording 28.3-30.0
  (`caplock.py`, the lowest in the first repeat of a series: keys0600_r1 28.3, keys0150_r1 28.5). Composite wait p50
  5.8-10.1 ms per recording, max 66.6 ms (keys0600_r1).
- **The load passed without a stall**: one held frame (frame 40, reasons 0x11 fog-no-table + install, skipped after
  0.00 ms; LOUD `held_skip` line), `held_faults=0`, no hung mark. (Against 5.7's 30 s deadlock before the hold fix.)
- `pacing.txt`: F2 p50 33.3 / p99 60.3 ms, max 891 ms (the load's first frames); F3 freezes 0; world updates 26.0 a
  second over the whole run (the load included).

**0689 on the laptop: the stage split, by camera move** (`split0689.py --marks marks.tsv --tag acc`; window 11:31:1x -
11:39:0x; 11,127 presents, every one exact).

| class | ready - publish p50 / p90 / p99 (ms) | over 16.7 / 33.3 ms | busy per drawn frame | terrain | polls | predraw | draw | forest | water | models | handover | units |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| moving (86 s) | 20.8 / 29.1 / 41.4 | 67.6 / 4.5 % | 20.3 ms | 7.67 (37.8 %) | 3.07 | 3.17 | 2.20 | 1.45 | 0.61 | 0.58 | 0.48 | 0.14 |
| still, city view (198 s) | 14.5 / 22.0 / 31.9 | 33.0 / 0.6 % | 15.7 ms | 5.86 (37.2 %) | 1.92 | 1.93 | 2.15 | 1.41 | 0.54 | 0.48 | 0.40 | 0.13 |
| wide hold, zoomed out (58 s) | 21.3 / 33.2 / 41.7 | 97.2 / 9.9 % | 23.2 ms | 8.55 (36.9 %) | 4.43 | 1.29 | 3.84 | 1.97 | 0.99 | 1.20 | 0.36 | 0.23 |

(Stage columns: ms per drawn frame, per-second means; fog 0.10-0.11 ms in every class.)
- **Terrain is the laptop's largest stage in every class (37-38 %)** and the top stage of 7,207 of the 8,487 itemised
  passes over 8 ms; then idle 634 (the idle bakes, load and rest), polls 324, predraw 189, forest 82, draw 49, units 1,
  **handover 0**. The workstation's handover blocks (5.8) do not occur on a machine with nothing else on its GPU: this
  supports contention as their cause [L on the laptop; the workstation side stays I].
- Wake delay is nothing at the median (0.03 ms) in every class; its moving tail (per-second max 96.6 ms, one second)
  is the idle bake / predraw case of 5.8. Predraw is 3.17 ms per frame in moves against 1.93 still: the pan's bakes
  (0703's subject).
- Under exact lockstep the moving server frame (20.8 ms p50) runs beside the EXE's own work (insertion - publish 14.2
  ms p50 in moves): at the 30 fps cap only 4.5 % of moving frames exceed 33.3 ms, and the displayed rate stays at
  28.3-30 fps. Uncapped, the laptop's moving frame rate is bounded by the server's pass at about 48 fps (1000 / 20.8)
  [I: not run uncapped on the laptop].
- Units' first frame on the huge save: `units 1s` frame_ms_max 676.7 ms, anim_ms_max 382.7 ms (once, at the load;
  0704's subject); p50 8.4 ms afterwards.

**0692 on the laptop (the wide hold, 60 s, `an0692.py --from 11:37:20 --to 11:38:20`):** sprites 5,897 p50 (max
5,908), particle CPU **1.69 ms** p50 / 2.14 p90 / 2.29 max, particle GPU 0.18 ms; 101,083 particles alive, 0 lost. The
fog stage (mask + overlay) is 0.10 ms. In the reporter's workstation logs before the cuts the particles took 5-9 ms
(section 6); on the laptop after the cuts they are 7 % of the wide hold's 23.2 ms pass, behind terrain
(8.55), polls (4.43) and draw (3.84).

**Not verified.** The laptop's turn and walk comparison (0685, section 5.4); an uncapped laptop run under exact
lockstep; EXE build 26577625 on the laptop (this run is 4390913). *(The reporter's D237 check: done, 5.10.)*

### 5.10 The reporter's D237 side-by-side check (2026-11-23 12:18-12:3x EST, workstation) [L: the reporter's eyes]

Two Goldberg clients on the reporter's screen at 0,0, 2560 x 1440, focus guard and cloak off (D237 acceptance only;
MC-B #11595): **product** on c5h-a (build `acc-0691b` = `ec19daa90`, the laptop run's build; the reporter's
`[Renderer]` set, `lockstep_mode=exact`, `lockstep_csv=1`) and **stock** on c5h-b (VP's own 32-bit DLL, nothing laid);
both EXE 4390913, mods548, save `C5H-VP8P-HUGE_0350` (same SHA-256 on both), uncapped (`--timing`), launched by
`server/evidence/0673/d237/d237.sh` (roles swapped from the plan because c5h-b held an older run's `VP64/reports` folder,
which lay.py refuses as not a clean start). The reporter panned and zoomed both by hand and compared. Their findings
(paraphrased):
- **The interface's lock to the terrain is fine now; no divergence seen** (0691's exact lockstep; 0684 and 0677 as
  experienced: accepted by the reporter).
- **New: farm buildings flash during mouse-drag pans** (rarely on zoom, sometimes). Other improvements such as pastures
  may be affected too (hard to tell); the farm buildings certainly are. Not seen in the laptop's recordings (5.9: no
  farm in those crops was checked) [L: the reporter; cause: 5.11 (ticket 0709), offline, not yet checked per frame].
- **Smoothness is still far below stock: occasional severe stutter on camera movement** (uncapped on the reporter's
  165 Hz screen). The laptop's split (5.9) puts the server's moving pass at 20.8 ms p50 there, terrain 37 % of it;
  on the workstation the per-frame causes are in 5.8 and section 6.
- **Measured in the product's own logs of this session** [L] (`C:/c5-64bit-work/0673/acc-0691b/pp-d237/d237`:
  `render.log` of run `20261123-122331-50900-1`, the composite's `shell_composite.log` and `shell_composite.lockstep.csv`
  copied from c5h-a's `Logs`; `pacing_report.py`, `split0689.py` without marks): presents 15,546, F5 0.9999, interval
  p50 24.5 ms (~41 fps), p99 78 ms, 1,413 frames over 50 ms, 24 over 100 ms; camera moving (2,179 presents): interval
  p50 42.6 / p90 72.1 / p99 94.7 ms, the server's drawing pass 23.0 / 32.0 / 61.4 ms, the EXE's own work (insertion -
  publish) 35.2 / 63.0 / 76.9 ms; still: interval 23.0 ms p50. Stage split while moving, per drawn frame: **terrain 9.06
  ms (35.8 %)**, polls 3.55, draw 3.03, idle 2.39, predraw 2.01, models 1.56, forest 1.51, water 1.05; terrain is the top
  stage of 4,403 of 4,629 itemised passes. **So the stutter is the same terrain stage as on the laptop (7.67 ms there,
  5.9), at the same cost on an RTX 4080** [I: a cost that does not shrink with a far faster GPU points at CPU-side or
  per-draw work in the terrain stage; 0707's subject, now not laptop-only]. Caveat: the workstation was shared (other
  agents' games and builds ran; not quiet), which also slows the EXE's own work, and stock was not measured (no CSV
  without the patch); the reporter's "far less smooth than stock" stands as their observation.
  *(2026-11-23 19:4x: the quiet pair of 5.15 measures both on a quiet workstation: most of this session's stutter was
  the shared machine; the product stays about 2.2x stock's frame time.)*

### 5.15 The quiet stock/product pair: the shared machine made most of 5.10's stutter; the product's frame is about 2.2x stock's (lead-0673, 2026-11-23 19:43-19:54 EST) [L: two launches; one launch per arm: an indication, not a grade-A claim]

**Setup** (MC #11654 order 1; `server/evidence/0673/quietpair/`, runbook 5.7 `quietpair.sh`): the workstation inside
a held quiet hour (MC-B #12082/#12098: every account-B game, build and suite stopped; the owner closed their own game
first; at 19:43 no `CivilizationV_DX11.exe` or `server.exe` ran). EXE 4390913, save C5H-VP8P-HUGE_0350 (huge, T350,
the save of 5.10), uncapped (`launch --timing`), hidden, one game at a time: stock on c5h-b (19:43-19:48), then the
product on c5h-a (19:49-19:54; build `acc-0691b`, server `7f67b8c8...`, composite `a424edcf...`, exact lockstep, the
reporter's `[Renderer]` set: ServerRenderer, Units, Cities, Landmarks, FarmFields, Combat = 1). The same scripted key
and drag pans (`drive0691.py`, 3 repeats) in both; the frame time from the game's own per-frame Lua hook
(`ft_hook.lua`, `Events.LocalMachineAppUpdate` + `os.clock`, 1 ms bins) in both arms.

| over the drive (about 72 s) | stock | product |
|---|---|---|
| EXE frames | 12,252 | 5,609 |
| frames per second | 171.0 | 76.8 |
| mean / p50 / p90 / p99 / p99.9 / max (ms) | 5.85 / 6 / 8 / 11 / 18 / 34 | 13.01 / 13 / 16 / 21 / 33 / 60 |
| over 16.7 / 33.3 / 50 ms | 0.2 % / 0 / 0 | 8.1 % / 0.1 % / 0.02 % |

**Against 5.10** (the reporter's session, same save, shared machine): the product's moving present interval was p50
42.6 / p99 94.7 ms there; here, quiet, the product's moving interval (composite CSV, 1,748 presents) is **p50 13.45 /
p90 17.45 / p99 22.28 / max 58.5 ms**. So most of the severe stutter of 5.10 was the shared machine [L for both
numbers; I: that the load is the only difference]. The product still takes about 2.2x stock's frame time (p50 13 vs
6 ms, p99 21 vs 11 ms) [L].

**Where the product's frame goes, quiet** (`split0689.py`, moving seconds, per drawn frame): the server's pass busy
11.0 ms: terrain 3.80 (34.5 %; per 5.12 mostly the units' `beginFrame` inside it), idle 1.65, draw 1.59, polls 1.07,
predraw 0.89, forest 0.79, models 0.40, water 0.34, handover 0.28; ready - publish p50 10.40 / p99 16.88 ms; the EXE's
own work (insertion - publish) 8.37 / 18.20 ms; the composite's wait 3.13 / 9.98 ms. Under exact lockstep the frame
is the EXE's work plus the server's draw of that frame (only partly overlapped): 8.4 + 10.4 > 13.5. F5 (exact share)
0.9998, 0 freezes. **One 852 ms `units` pass** at 19:52:05 (frame 336, about 10 s into play): the units' first frame
after the load (ticket 0704's subject), the product's longest frame-time outlier.

**Reading** [I]: the gap to stock is now the per-frame cost itself, not the machine: the server's draw (about 10-11
ms a frame, a third of it the terrain stage's units setup, 0707) is added to the EXE's own work on every frame. 0707
(the units' frame setup), 0703 and 0704 are the tickets on it; a fuller overlap of the EXE's frame and the server's draw
is a system question (the lockstep design, D242), for the MC and the Architect.

**Not verified**: one launch per arm (PERFORMANCE.md grade A needs 6 interleaved runs per arm, and timing claims
belong on the testbed); the hook measures the EXE's frame, which under lockstep includes the wait for the server, so
it is the player's frame time but not a split; stock's EXE work per frame on the same view is not itemised; the
owner's eye on the quiet product was not asked.

### 5.11 0709: farm buildings flash on drag pans: the model tap is one Present old under exact lockstep (offline, worker-0673v, 2026-11-23 12:49-12:58 EST) [C: code at `ec19daa90`; L: the D237 logs of 5.10; I: marked]

No game, no build; the logs of 5.10 and the code of `acc-0691b` (`ec19daa90`; the files below are unchanged at
`5043721fb`).

**The path** [C]. With `ServerFarmFields = 1` the farm buildings are the EXE's own landmark instances: the composite's
builder hook posts the farm end (`shell/composite/farm_guard.cpp:1009` `PostFarmEnd`, `../02-exe-internals/farm-builder-guard.md`
13), the EXE's farm manager places the instance at the plot centre (`cities-and-landmarks.md` 2.6.1), and with
`ServerLandmarks = 1` the EXE's draw of it is tapped and dropped by the compositor (`shell/composite/unit_filter.cpp:1874`,
flag `VPMTAP_DRAW_DROPPED`) like every other landmark (`city-renderer.md` 10.3-10.5). The server's frame for Present P
pairs a tap in `ModelsScene::beginFrame` (`server/src/render/landmarks/srv_models_scene.cpp:206-267`); the wait for tap P
is 0 by default (`srv_models_scene.h:66-75`, `render_models_pair_wait_ms`, 4882: under lockstep tap P is published at
Present P, after the composite took the server's frame P), so it takes **the newest older tap, P-1**. The layer keeps
only that tap's DROPPED records (`srv_model_layer_c5r.cpp:279-330`) and draws them at frame P's camera
(`applyFrameCamera`, `render/src/landmarks/inputs.cpp:443`). So the positions are P's, but **the set of instances is the
one the EXE culled with camera P-1**. Nothing else drops a building per frame: no residency or LOD of the server's own
for tapped models, no generation counter per frame; the EXE's builder is not called again during pans
(`farm-builder-calls.log` last written 12:25, 1,200 lines, all at the load).

**What the D237 logs show** [L] (run `20261123-122331-50900-1`, workstation c5h-a, EXE 4390913, huge T350, uncapped):
- `models:` line at 12:33:00: `drawn=15524`, **`pair exact=209 older=15315`**, `waited=0`, `max_lag=1`, `none=0`,
  `unfitConsts` never logged, `draw_errors=0`. Every drawn frame but 209 (camera still) used the previous Present's tap.
- The shift that drawing at the frame's camera avoids (`win_max_px`, the largest instance-origin move between the tap's
  camera and the frame's in the line's second), per second from 12:25:40 to 12:33 with the camera read from
  `shell_composite.lockstep.csv` (scratch parser, not committed): **pan seconds** (camera moving in >= 5 presents, no
  zoom; n = 70): median 153 px, p90 373, max 688, at a median 20 presents per second; **zoom seconds** (n = 48): median
  298, p90 758, max 1,125; **still seconds** (n = 301): median 0. The lockstep CSV: 15,500 presents `exact`, 0 `older`.
- The compositor (`modelfilter totals` 12:32:59): `dropped_landmarks=7939140`, `passed_set_landmarks=5425`,
  `not_in_set=7274`; per second the passes and `not_in_set` occur only in the seconds where new model keys join the
  identity set (`set_gen` 0 -> 33, the last at 12:28:15; the server's `unknown` rises by 12-290 in those seconds,
  e.g. 12:25:44 +290, 12:27:56 +119), all on camera moves into new areas; from 12:28:16 to 12:33 none, while the
  reporter's pans went on (12:31:21-12:31:42 pans and zooms with 125-835 px shifts).

**Causes** (2026-11-23 14:00, 5.13 measured them live: cause 1 confirmed as the mechanism, only in frames moving more than about 300 px; cause 2 not seen; nothing inside the view):
1. **[I, the leading-edge hole of R1, `city-renderer.md` 2 R1 and `landmark-renderer.md` 7.7]** In frame P a building
   that entered the EXE's culled set only at camera P is missing (tap P-1 does not hold it); it appears in P+1. The EXE
   submits a landmark while its origin lies in the view's ground box widened by 64 world units (`landmark-renderer.md`
   7.7.1 [C]; the "21.6 px" of 7.7 was the closest culled instance of one frame pair, corrected 2026-11-23); the pans here move 150-370 px per frame (p50-p90
   of the per-second maxima) at ~20 fps, so a band at the leading edge up to a few hundred px wide shows terrain and
   fields (the server's, at camera P) without their buildings each frame. Farm buildings are the most numerous tapped
   model on this save (373 farms, white barns on drawn fields), which would make them the visible case; pastures, mines
   and every other tapped landmark share the path [I]. Zoom-out opens all four edges at once but is smoothed, hence
   "rarely" [I]. Not stock's behaviour: the EXE draws its own cull with its own camera.
2. **[C by the code, rare]** When a new key joins the identity set the compositor drops its draws from frame P on, but the
   server draws tap P-1, where those draws were passed (no DROPPED flag): the new key's instances are missing in that one
   frame. 33 set generations in this session, the last at 12:28:15, so it does not explain the later flashes.
3. ~~Possible farm-specific aggravation: a tighter cull bound for a farm instance placed without curves~~ **ruled out
   [C, 2026-11-23, `landmark-renderer.md` 7.7.1]**: the cull tests only the instance's origin against the view's box,
   the same rule for every landmark; a farm building's origin is its plot centre (2.6.1 of `cities-and-landmarks.md`).

**The decisive check** (no new code): a short live drag on the huge save with the recorder keys
(`offline-scene-harness.md` 1061: `render_test_record=<F: folder>`, `render_test_record_after=<frame>`,
`render_test_record_frames=60`) through `capture_job.py --server-cfg-extra` or `live0691.sh` (runbook 4.15 / 4.16): 60
consecutive `modeltap/frame<N>.mtap` and their cameras. Offline, per frame P: the instances of tap P inside the view at
camera P that tap P-1 lacks (= the holes shown), by model file (`modern_farm*.gr2` against others) and by distance from
the leading edge; `render/evidence/0561/4873/r1.py` does this projection for capture exports and needs a reader for
`.mtap` (`render/src/landmarks/tap.cpp` `readTap`). Holes in the leading band only = cause 1; holes inside the view =
something else (cause 3 or an unread path). The farm part of cause 3 shows as farm instances culled nearer than 21.6 px.

**Fix sites** [C for the files, I for the choice]: the pairing and the kept set in `server/src/render/landmarks/
srv_models_scene.{h,cpp}` and `srv_model_layer_c5r.cpp` (e.g. draw tap P-1 plus the instances of older taps that are
inside camera P's view and not in P-1, keyed by key + world rows, dropped on a plot change; it fills only content seen
before), `render/src/landmarks/inputs.{h,cpp}` / `tap.cpp` if the record form changes, the scene bundle's model member
(rule A: `server/src/render/srv_scene_bundle.cpp`, `srv_scene_recorder.cpp`) when the drawn set changes, and the tests
`server/tests/render/cities/models_test.cpp` (`scene_pairing`). A first entry into view can only be covered by a tap of
frame P (ruled out under lockstep, `city-renderer.md` 11.11), by a wider EXE cull (`landmark-renderer.md` 7.7.1: the
box can be widened for the landmark vis system alone by a listener-node pass-through on the views list, a data write
of the base product's kind; the 64.0 is a shared `.rdata` literal and not a route), or by the server placing the farm buildings itself from its own records (ticket
0653's list, `render/src/farmfields/buildings.cpp`, `server/src/render/srv_farmfields.{h,cpp}`; the parked 0654
route), which covers farms only. Cause 2: the same two server files (draw a passed record of tap P-1 whose key is in
the set the compositor applies at P).

### 5.13 0709's decisive recorder check: the missing instances are leading-edge entries from beyond the EXE's cull, and only in frames that move more than about 300 px (worker-0673x, 2026-11-23 13:41-14:00 EST) [L: the run; C: the bundle's taps read by the scripts; I: marked]

**Run** [L]. Instance c5h-c (EXE 4390913, mods548, Goldberg, hidden, focus guard on), MC-B's grant #11698 after the MC's
cand4 BARRIER END (13:40); claimed 13:14, released 13:53. Product build `acc-0691b` (server.exe `7f67b8c8...`, composite
`a424edcf...`; exact lockstep) laid by `lay.py prep`, helper on, the reporter's `[Renderer]` set (ServerRenderer, Units,
Cities, Landmarks, FarmFields, Combat = 1; Arc, Path = 0), `lockstep_csv=1`, the recorder keys as a TEST-ONLY
`server_cfg_extra` (`render_test_record=F:/c5-64bit-work/0673/rec0709/r1`, `_after=3000`, `_frames=600`,
`_dump=first,last`), unset before the restore. Save C5H-VP8P-HUGE_0350 (huge, T350), launch `--timing` 13:41:44, in game
13:46, autoplay off, then 240 s of continuous left-button drags (`server/evidence/0709/drive0709.py`: 40 % <-> 65 % of the
2554 px client at 55 % height, 300 ms and 1000 ms, 0.25 s apart, 28 cycles), the harness's safe stop 13:51:59 (exit 0),
`lay.py collect` + `restore` by hash rc 0, `provision.py verify --quick` rc 0, leftovers clean. Commands: runbook 4.20.

**The span** [L, C]. Published frames 2978-3359: 382 frames (frames.bin index 0-381), 374 `modeltap/` members (a member
is written only when the tap's content changes), every member pair one Present per frame (taps' presentCount 2977-3358,
no gap). The span did not end before the stop (600 frames asked): the bundle is INCOMPLETE (`.partial`, no scene.json;
12 GB on F:, not in the library); its taps are whole (every member passes `readTap`'s checks, `server/evidence/0709/mtap.py`).
**The recorder slowed the game**: from about 19 published frames per second during the drags before the span
(323 -> 2647 in 122 s) to 4-5 per second inside it (render.log `host 1s`), because the overlay animates (a unit selected [I])
and each frame writes its 22.4 MB overlay to F: (`overlay/`: 121 files about 45 s into the span). Under exact lockstep the EXE waits for
it, so **the camera moves 4-5 times farther per frame than the same drag at normal speed**: the 117 moving pairs have
|shift| p10 103, p50 155, p90 586, max 634 px (median screen move of the instances both taps hold; all horizontal);
256 pairs are still (the idle seconds after the drive). That is what makes this span decisive for fast frames: the
D237 session's pans (5.11) had per-second maxima p50 153, p90 373, max 688 px.

**Holes** (`holes.py`: instances of tap P inside the viewport at camera P, dropped by the compositor, not held by tap
P-1; non-skinned, the scene target, origin-projected with each draw's own VS cb1 rows) [C on the taps]:

| \|shift\| per frame | pairs | pairs with holes | holes | entering instances (inside at P, off screen at P-1) covered by tap P-1 |
|---|---|---|---|---|
| 0-2 px (still) | 256 | 0 | 0 | - |
| 2-150 px | 55 | 0 | 0 | 1,295, off screen at P-1 by up to 165 px |
| 150-300 px | 31 | 0 | 0 | 866, up to 328 px |
| 300-450 px | 14 | 3 | 18 | 1,157, up to 509 px |
| 450-634 px | 17 | 7 | 346 | 1,636, up to 752 px |

- **All 364 holes are leading-edge entries** (cause 1 of 5.11): every one was off screen at camera P-1 (178-479 px
  beyond the edge, p50 357), and every one lies within 281 px of the leading edge at camera P (p50 89, p90 208), never
  deeper than the shift (no hole deeper than |shift| + 50 px). **No hole inside the view** (0 instances visible at P-1
  and missing), so nothing else drops buildings per frame in this span.
- **Cause 2 not seen**: 0 holes whose key was passed in tap P-1 (one set generation change in the span, 25 -> 26 at
  index 92, with 1 passed draw in that frame).
- **First entries against re-entries**: all 364 are re-entries (the span starts after 120 s of drags over the same
  strip, so every instance had been in an earlier tap of the span). The EXE keeps nothing across frames for them: a
  re-entry is missed exactly like a first entry.
- **Farm against other**: 2 farm-building holes (`modern_farm_diff.dds`, 321 px and more off screen at P-1) of 364; the
  rest city blocks (`afri_mod_city_diff.dds` 310), customs houses 22, bridges 6, landmarks 6, wonders 7, other 11.
  This view held only 8-10 farm buildings on screen per frame (60 farm entries covered, 2 missed: 3 % against 7 % for
  the others), so the run does not single out farms; nothing points to a farm-specific tighter cull (cause 3 of 5.11)
  [I, two cases].
- **One direction only**: every hole is in a frame whose content moved right (the camera left, the left edge leading);
  the 57 frames moving the other way had 0 holes. The right side of this view is nearly empty of models (10-42 visible
  instances within 300 px of the right edge against 55-215 at the left) [C], so the asymmetry is the map, not the rule [I].
- **Where the boundary lies**: no hole below 300 px per frame; between 300 and 450 px, 3 of 14 frames; above 450 px,
  7 of 17 frames with 65-86 holes each (about 15 % of the 450-600 visible instances), in a band up to 281 px wide.
  Covered entries reach 752 px off screen and holes start at 178 px: the EXE's lead is not a pixel constant
  (`landmark-renderer.md` 7.7.1, a world box). Checked against 7.7.1's rule with a box made from tap P-1's VS cb1
  rows (four frustum side edges cut at z = 0 and z = 127.5, plus 64 units; `entering.py`): 4,933 of 4,954 covered
  entries inside it, but only 196 of 364 holes outside it; the holes lie 11-110 world units beyond that box (p50 65),
  covered entries up to 77. So the rule's shape holds, but the box the EXE uses is not exactly reproduced from the
  draw camera's rows (the snapshot's frustum, or its frame, may differ) [I; not resolved here].

**Verdict on (a)**: **confirmed as the mechanism** [L, C] (the fix: 5.14, 2026-11-23): the instances missing from the server's frame are exactly
instances that enter the EXE's cull at camera P, at the leading edge, and only in frames whose camera moves farther than
the EXE's lead (about 300 px per frame on this save and zoom). **But at normal frame rates it is rare on these drags**:
the same 300 ms drag at 19 fps moves about 105 px per frame, where this span shows 0 holes in 2,161 entries; holes need
pans that reach 300+ px in one frame, which the D237 session had in its faster seconds (5.11: p90 373, max 688 px per
second's maximum) [I: the share of the reporter's frames above 300 px is not counted]. Hypothesis (b) (cause 2) did not
occur in the span. Whether the reporter's farm flashes are all of this kind is not proved by this run (few farms in view).

**Data** (outside git): the bundle `F:/c5-64bit-work/0673/rec0709/r1/.partial/` (12 GB; deleted 2026-11-24 after 0709 closed, with 5.14.1's `keep1` and `replay`; 5.14.1's `keep0` bundle, with the 6 first entries, is kept on F: as input for the widening ticket's offline check), the
run's work folder `C:/c5-64bit-work/0673/rec0709/` (`rec0709.log`, `marks-r1.tsv`, `r1-shell_composite.lockstep.csv`,
`holes-r1.json` / `.txt`, `entering-r1.txt`, `pp/rec0709/` the collected logs, run `20261123-134352-42116-1`).
Header oddity [C on the data, cause not read]: 14 members (2 every ~30 frames from index 188) carry `setGeneration` 0
and `latchedBits` 0 while holding dropped draws as usual.

**Scripts** (`server/evidence/0709/`): `mtap.py` (the `.mtap` reader, readTap's checks and hash; checked on the
library's huge bundle `C5H-0301-HUGE_0350_20261122-124422_workstation`: 8 members read, 0 holes at a still camera, 186
visible instances each; and on a synthetic pair made from its member 0 with the camera moved 100 px and the instances
within 300 px of the left edge renamed: shift [100, 0], 81 holes, 42 off screen at P-1 = those within 100 px),
`holes.py`, `entering.py`, `drive0709.py`, `rec0709.sh`.

### 5.14 0709 fix: the server keeps the instances the EXE culled at the drawn tap's camera (impl-0709, 2026-11-23 14:02-16:45 EST) [C: code, unit tests, offline replay of 5.13's taps; L: the live A/B of 5.14.1 (2026-11-23)]

**The rule** (`server/src/render/landmarks/srv_models_scene.h` `CulledKeep`, used by `srv_model_layer_c5r.cpp`). The
layer remembers, per instance (its part, material and the bits of its origin, the identity 5.13's `holes.py` matched
with), the newest tap the scene pass drew it from (its item, world rows and VS / PS constants as tapped). Frame P,
drawn from tap C (= P-1 under exact lockstep) at camera P, also draws each remembered instance that
- (a) tap C does not hold at all (no record, dropped or passed: a passed one is the EXE's to draw),
- (b) a drawn tap L held with C - 120 <= L < C,
- (c) lies outside camera C's ground box (7.7.1's box rebuilt from tap C's main VS cb1 rows: the four frustum side edges
  cut at z = 0 and 127.5, NOT widened by 64),
- (d) lies farther outside camera C's box than camera L's box by more than 1 world unit (the camera moved away from it
  since it was last drawn),
- (e) lies inside camera P's box widened by 64 (the EXE's rule: tap P will hold it),
and nothing while camera P equals camera C (still camera, or exact pairing). Each kept instance is one record of its own
(instance count 1, its four rows, its tapped constants), drawn after the tap's records at camera P through the same
`applyFrameCamera` (wrap copy by its own tapped rows), scene pass only. Distances take the nearest wrap copy of the box
when the frame has a wrap period. Sightings are taken only from taps the scene pass actually draws (once per tap), so a
replay of the same frames in order keeps the same instances; a tap with an older Present (a load, another bundle) and
`clearScene` forget everything; at most 65,536 sightings (the oldest go first).

**Why (c) uses the box itself and (d) guards removals** [C on 5.13's taps, `C:/c5-64bit-work/0709/impl/keep_sim.py`,
outside git]. The box rebuilt from the tapped rows does not reproduce the EXE's (5.13): holes lie 11.4-110 world units
outside the rebuilt box (so 168 of 364 lie inside it widened by 64), and covered entries reach 76.7 units outside it.
With (c) at +64 only 196 of the 364 holes would be kept; at +32, 328; at 0, all 364. An instance the game removes while
it lies in the 0-64 band at the leading edge would pass (c) at 0, but in that case the camera is coming nearer (that is
what brings it into view at P), so its distance outside the box shrank since L and (d) refuses it; one that vanishes at
a still camera is refused by (d) and by the still-camera rule. What remains possible: an instance removed while it was
out of the EXE's box (far away), whose spot the camera brings back into view within 120 Presents, is drawn in that one
entry frame; at frame P+1 its tap's camera holds it in view, (c) fails and it is gone. So a wrong keep lasts one frame at
most, the same size of fault as the hole it replaces, and needs a removal during the pan [I].

**Why 120 Presents** [C on 5.13's taps; I for the live rate]. The age of the sighting each hole needed (tap C minus L):
p50 6, p90 9, max 16 Presents in 5.13's span (4-5 published frames per second, drags of 0.3 s and 1 s each way); at
the normal 19-20 per second the same drags are 4-5 times as many Presents, so up to about 80. 120 is 2 s at 60 fps
and 6 s at 20 fps. With 8 the replay misses 14 of the 364 holes, with 16 or more none. Memory: 5.13's span held at most
1,043 sightings; each refers to the scene copy of the tap it was last drawn from (a few hundred KB per tap), so up
to 120 tap copies can stay referenced during a long one-way pan [I, not measured live].

**Offline replay of 5.13's taps** (`keep_sim.py 120 0`, the rule above in Python, members of the `.partial` bundle in
order, each pair one Present apart; the bundle's taps are what the server drew): 373 pairs, **364 holes, all 364 kept**;
5,271 kept instance-frames in all, 4,248 of them instances tap P holds (correct fills, mostly just off screen inside
the EXE's box), 1,023 that tap P lacks and that are off screen at camera P (drawn, invisible; the rebuilt box's error),
**0 kept instances on screen that tap P lacks** (no wrong building drawn in any frame). Holes the fixed pairing
leaves: 0 of 364. Not replayed through `sceneshot`: the bundle is partial (no `scene.json`).

**Switch and log**: `ModelsOptions::keepCulled` (on) and `keepMaxAgePresents` (120); the test switch is the
environment variable `C5_RENDER_MODELS_KEEP=0` (read at configure, logged "models: the keep ... is OFF by
C5_RENDER_MODELS_KEEP"); no config key (that would need `srv_render_host.cpp`). The `models:` line gains
`keep_on= keep_frames= keep_inst= keep_max= keep_entries=` (scene passes that drew kept instances, the instances, the
most in one pass, the sightings remembered).

**Not done**: the reflection pass keeps nothing (a reflected building at the leading edge can still miss one frame);
skinned parts (material 6) are not kept (their origin is not in their rows); a first entry (never drawn before) cannot
be kept (5.11: only tap P, a wider EXE cull or the server's own placement can cover it); cause 2 of 5.11 (a key joining
the identity set at P) is not covered (it did not fall out of this rule: the passed record is in tap C).

**Tests and builds** [C, 2026-11-23 14:20-16:41, workstation, game copy 4390913 outside the pool; the worktree also held
lead-0673's 93f4f0751]: `models_test` new cases `keep_ground_box` (the box against the screen's ground corners, wrap
copies, a singular matrix), `keep_rule` (re-entry kept with its payload; not kept: frame box + 64 passed, trailing edge,
removed inside tap C's box, held by tap C, still camera, sighting older than 120 or not older than C, removed in the band
while the camera came nearer or stood; 120 / 8 Presents age, switch off, older Present forgets, memory bound, wrap
copy), `scene_keep_switch` (options and `C5_RENDER_MODELS_KEEP` reach the layer, the counters and the line): test_models.py
31/31 plain and ASan (`reader_torn` and `feed_model_load_begin` each failed once under load and passed on every rerun:
timing cases, untouched). All builders (runbook 2.10, `allb0709.sh`): run_all render-host 193, scenebundle-programs 4,
scenebundle-py 26, overlay-scene, shadow-ice-scene, water-scene 29, forest-scene PASS; fow-scene failed only its case
list (lead-0673's new case, fixed in d3a4787d4); build_server (0.1b pins, 158 s), units, units extras, unitfeed,
farmfields, sceneshot (--smoke) OK. **Rule A**: offline-equals-live 17 of 17 library bundles PASS (standing tolerance
1:1, depth byte-equal). ASan run_all: fow-scene PASS; render-host 192/193 then 193/193 on a rerun; scenebundle-py 25/26
then 24/26, each time a different `test_recorder_host.py` case refused with "1 dumped frame(s) of 2" (the recorder's
frame count under ASan on a busy machine; those cases run without models, so the keep's code is never reached) [I:
not compared with main under ASan].

**Offline and live alike** [C, code read]: the offline player hands each frame's `modeltap/` member to the module with
`loadImage` (`srv_scene_sources.cpp` ~1006) and keeps the module across frames, so a replay of a moving bundle's frames
from its first frame in order makes the same sightings and keeps the same instances as the live frames did; a replay
that starts in the middle of a moving span lacks the earlier sightings and draws less (still bundles: nothing kept).
`holes.py` reads only the taps, so it counts the same holes with or without the fix: the fix shows in the frames, not
in the taps.

**The live check to run** (lead-0673): 5.13's recorder run (runbook 4.20) on the same save and drive with this build,
with `render_test_record_frames` small enough that the span closes (about 150 at the recorder's 4-5 frames per second;
5.13's 600 never closed), twice: keep on, and `C5_RENDER_MODELS_KEEP=0` in the harness launch's environment (runbook
3.14's pattern; server.log shows "the keep ... is OFF"). Read: the `models:` lines' `keep_inst` / `keep_max` in the
drag seconds (on: > 0; off: 0); `keep_sim.py` on the "on" bundle (holes kept, 0 on-screen instances tap P lacks); a
`sceneshot` replay of each closed bundle from frame 0 in order, comparing the leading-edge band of the 300+ px frames
(on: the buildings present; off: missing as in 5.13). Then the owner's eye on fast drag pans, and runbook 4.14's
every-displayed-frame record if a flash is still seen.

#### 5.14.1 The live A/B and its replay (lead-0673, 2026-11-23 16:57-17:40 EST) [L: two launches; C: their bundles replayed]

**Runs** [L]. c5h-a (EXE 4390913, MC-B #11955), impl-0709's server build of aae608ee7 (`server.exe` `eec53dde...`,
composite `a424edcf...`, exact lockstep), the reporter's `[Renderer]` set, save C5H-VP8P-HUGE_0350, hidden, guard on;
`rec0709.sh` (runbook 4.20) with `FRAMES=150`, `AFTER=3000`: `keep1` (keep on, launch 16:59, 244 s of drags) and
`keep0` (`C5_RENDER_MODELS_KEEP=0`, launch 17:10, 208 s; server.log "... is OFF by C5_RENDER_MODELS_KEEP"). Both spans
closed (150 frames each, 0 record errors, 4.6 GB each on F:); safe stops exit 0, restore by hash rc 0, verify rc 0,
released 17:20. The workstation ran 5-6 games meanwhile: no frame rate here is a timing.

**Live counters** (`render.log` `models:` lines, cumulative) [L]. keep1 at the end: 983 of about 2,840 drawn frames
kept at least one instance, 15,992 kept draws in all, `keep_max` 257 in one frame, `keep_entries` about 560-660;
keep0: `keep_on=0`, all keep counters 0. The kept draws are mostly off screen (below); their CPU and GPU cost is
not measured.

**Holes in the taps** (`holes.py`) [C]. keep0: 103 holes in 6 pairs, all re-entries, all at 300+ px a frame
(afri_mod_city 62, anc_customs_house 22, landmark_afr 10, bridges 5, other 4; no farm in this span); `keep_sim.py 120`
keeps 103 of 103, 0 on-screen instances that tap P lacks. keep1 (another stretch of the drive): 8 holes, 2 re-entries
(both kept by the rule) and 6 FIRST entries (3 farm, 3 landmark_afr), which the fix cannot cover by design: an instance
the server never drew has nothing to keep. First entries remain for the EXE-side widening (the open point of 5.13).

**Replay on/off** [C] (`sceneshot` of aae608ee7, impl-0709's build, `--span 2997:3146` of the keep0 bundle from its
first frame in order, asset root mods548-vt; once with the default, once with `C5_RENDER_MODELS_KEEP=0`;
`server/evidence/0709/cmp_onoff.py`): exactly 6 of 150 frames are not byte-equal, frames 3018, 3047, 3075, 3076, 3106
and 3135, which are the six hole pairs' P frames; every changed pixel lies in a band at the leading edge (x 0-286 of
2554 for these leftward-entering pans; 222 to 32,096 pixels per frame). In frame 3047 (69 holes) the keep-off picture
lacks a whole city's buildings at the left edge (only their shadows lie on the ground); the keep-on picture draws them
(diagnostic crop, not kept in git). The other 144 frames are byte-equal on/off, so the kept off-screen draws change
no pixel.

**Offline equals live** (`--dumped --compare --tolerance 1:1`) [C]. keep0: both dumps byte-equal (with the default
keep on offline, as the keep-off live run had nothing to keep in those two still frames). keep1: both dumps differ
in colour by 24-27 pixels (green channel +1 to +4 over a small world-fixed patch, depth EQUAL), and the same with the
keep off offline, so the keep is NOT the cause; an unexplained residual of this bundle, open (not 0709's).

**The owner's eye check** (2026-11-23 21:06-21:13 EST, c5h-a, EXE 4390913, build impl-0709 allb/srv = aae608ee7
(server.exe sha256 eec53dde4f953778...) with the acc-0691b shell, the huge save copy C5H-VP8P-HUGE_0350 at T350,
uncapped, focus guard and cloak off, the reporter's [Renderer] set; B's heavy builds were running, so smoothness was
not judged) [L]. The reporter panned by hand (drags and zooms) and saw no flashing of farm buildings: "looked fixed
on the flashing front". The render.log `models:` counters at the end: keep_frames=2150 keep_inst=112731 keep_max=960
keep_entries=1722 over 18,445 drawn frames, so the keep was active throughout. The reporter's earlier "no flashes in
general" in beta 3 (which does not carry 0709) was ordinary play, not the fast drag pans of the D237 report. The
reporter also saw cities that looked suspiciously empty, in beta 3 and in this run: not 0709's (models are only kept,
never dropped, by the keep); reported to the MC as a new symptom.

**Not verified**: first entries (above, the EXE-side widening); the reflection pass; the kept draws' cost.

### 5.12 0707: the terrain stage split [C: code read at 5043721fb; L: the live logs of 5.9 / 5.10 and offline runs; I: marked]

Written 2026-11-23 13:5x EST by worker-0673w under lead-0673 (branch `claude/lead-0673-holistic-d0fd9d`). No product
file changed, no game launched. Evidence: `server/evidence/0707/` (the diagnostic timer patch `diag0707.diff`, never
merged; `bench0707.sh`, `diag0707_stats.py`, `terrain_join.py`, `terrain_fit.py`; the summaries
`split-offline-workstation.txt` and `split-live-logs.txt`). Commands: runbook 4.19.

**What the `terrain` mark covers [C].** `server/src/render/srv_render_scene.cpp` `Scene::draw`, from
`PassMark(kStTerrain)` (line 1238) to `PassMark(kStUnits)` (line 1256):
1. `unitShadowStep` (line 2816): with `render_units_shadow=1` (the default, `srv_render_host.cpp` 225) and a shadow triple
   whose slot says the EXE dropped its unit mask, it copies the fog's plot states and plot table to the units
   (`setPlotStates`, `HandPlotTable`: copies only on a new generation / read count), **begins the units' frame
   (`UnitsHost::beginFrame`, `server/src/units/srv_units_scene.cpp` 1224: `buildList` + fog filter, the controller's
   `animFrame`, `prepare` of each wrap copy's drawn figures)** and merges the units' cast into the tapped shadow map
   (`UnitShadow::apply`, `srv_unit_shadow.cpp` 309: one `CopyResource`, the mask pass, the resample). Both live runs had it
   on every drawn frame (`unitshadow 1s: ready=1 bit=1 merged=27..30`). Without it the frame is begun later, under the
   `units` mark (as offline: a version-9 `shadow.bin` never carries the dropped-mask flag, so sceneshot runs no merge).
2. `ClearRenderTargetView` of the colour.
3. `c5r::terrain::Renderer::draw` (`render/src/terrain/terrain.cpp` `drawPass`, line 1556): the input checks; the EXE's
   LOD (`exeFrameLod`: a sphere test and projected size per 64-sample EXE tile, over all of them); two constant-buffer
   uploads (TerrainFrame, fog) and the pipeline state; then **for each of the 3 wrap copies, every renderer tile of the
   map** (one per plot: 128 x 84 = 10,752 on the huge save, 32,256 visits a frame): an 8-corner box against 6 clip
   planes (128 multiply-adds), and for a tile not culled (and not open sea): 5 by-name `put`s into the TerrainTile
   buffer (a `strcmp` walk each), a `Map(WRITE_DISCARD)`/`Unmap`, `VSSetShaderResources`, `PSSetShader` (every tile),
   `PSSetShaderResources` (11 slots, every tile), one tessellated `DrawIndexed`; then the unbinds and the GPU timestamp
   queries. `terrain 1s`'s `cpu_ms` is this function alone (`steady_clock` around it); `gpu_ms` its timestamps.

**Live split, per drawn frame (`terrain_join.py`: `pass 1s` terrain x passes / drawn, minus `terrain 1s` cpu_ms, minus
`unitshadow 1s` cpu_ms_mean) [L].**

| run | terrain stage | the terrain draw (CPU) | its GPU | unit shadow apply | rest = the units' frame begin, plot copies, clear |
|---|---|---|---|---|---|
| laptop, job `20261123-112740-fd80` (5.9), all drawn seconds (423) | 6.37 mean / 6.03 p50 | 1.60 / 1.54 | 0.64 / 0.63 | 0.15 | **4.63 / 4.25** (73 %) |
| the same, top quarter by terrain (106 s) | 9.46 / 9.31 | 1.93 / 1.87 | 0.65 | 0.16 | **7.38 / 7.28** (78 %) |
| workstation D237 (5.10, shared, uncapped), 449 s | 8.84 / 8.47 | 1.37 / 1.32 | 0.74 / 0.64 | 0.23 | **7.24 / 6.88** (82 %) |

In the seconds before the units were ready (laptop 11:31:47, workstation 12:25:17) the stage equals the terrain draw's
CPU (0.72 against 1.47 ms; 0.93 against 0.87 ms); from the first second with units listed the rest jumps to 3-4 ms [L].
**So most of the `terrain` stage is the units' frame, begun there because the units cast into the shadow map; the
terrain draw is 20-25 % of it and its GPU time is under 1 ms on both machines (asynchronous, not on the render
thread's path).** This explains why an RTX 4080 does not shrink it (5.10). The `units` stage (0.14 ms laptop) holds
only the units' scene pass.

The terrain draw's CPU against tiles drawn (`terrain_fit.py`, least squares over the seconds) [L]: laptop **1.46 ms
fixed + 1.2 us per tile drawn**; workstation 0.84 ms + 1.7 us per tile (76 tiles city view, 350-540 zoomed out). The
fixed part is the work over every tile of the map.

**Offline split (sceneshot + `diag0707.diff`, HUGE_0350 bundle, 2554 x 1411, 76 tiles and 29 unit figures drawn in each
of the 8 frames; each frame drawn 30 more times after its shot, every shot still byte-equal to its dump)** [L,
workstation 2026-11-23 13:49-13:56 EST, 4-5 heavy jobs of other owners registered: NOT quiet, an indication; 4 runs,
960 draws; per-run medians in brackets]:

| part | p50 ms | p90 | per-run medians |
|---|---|---|---|
| the terrain draw (`drawPass`, CPU) | 0.90 | 1.21 | 0.72-1.03 |
| of it: the tile loop (cull of 32,256 boxes + 76 tiles submitted) | 0.70 | 0.95 | 0.56-0.79 |
| of it: the same cull alone (diagnostic replay, no submission) | 0.61 | 0.86 | 0.58-0.84 |
| of it: `exeFrameLod` | 0.11 | 0.18 | 0.10-0.18 |
| of it: checks, constants, state, unbinds | 0.05 | 0.09 | |
| the terrain draw's GPU (`C5_DIAG_0707_GPU=1`, 2 runs) | 0.16 / 0.51 (run medians) | | |
| the units' frame begin (`beginFrame`, under `units` offline) | **1.85** | 2.84 | 1.40-2.25 |
| of it: `buildList` + fog filter / controller / `prepare` | 0.30 / 0.75 / 0.78 | 0.49 / 1.16 / 1.20 | |

API counts per frame (every draw): 81 constant-buffer maps (2 + 1 per copy with an overlay + 1 per tile), 76
`PSSetShader`, 152 SRV-binding calls, 76 `DrawIndexed`, 1 hull-shader switch. Per tile submitted: about 0.1 ms / 76 =
1.3 us offline (the player's device has `PREVENT_INTERNAL_THREADING`, so the driver's work is inline), 1.2-1.7 us live.

**Reading.**
- **The largest part is the units' frame begin: about 70-80 % of the live stage** (laptop 4.3 ms p50 of 6.0; 7.3 of 9.3
  in the busiest quarter). Offline it is 1.85 ms on the workstation (5056's 20.1 measured 1.55 ms before its cut); the
  live 4-7 ms is 2.5-3.5x that, while the terrain draw is 1.6-1.8x its offline figure (laptop CPU slower, workstation
  shared and loaded; the live trigger stream and feed events are not in the offline replay) [I: the live beginFrame was
  not timed by part; `units 1s` gives only per-second maxima].
- **Of the terrain draw itself, the fixed cost dominates: the box cull of all 32,256 tile copies (0.6 ms of 0.9 offline,
  1.2-1.5 ms live on the laptop)**; submission is 1.2-1.7 us per drawn tile (0.1 ms at 76 tiles, 0.6-0.9 ms at
  350-540), `exeFrameLod` 0.1-0.2 ms. The 2 wrap copies that are almost wholly off screen cost two thirds of the cull.
- **GPU: 0.16-0.96 ms per frame** (offline and live): not the cost.

**Candidate cuts that keep the picture byte-identical (rule A) [I: not built].**
1. **Units' frame begin (the largest part).** (a) Instrument first: a `PassMark(kStUnits)` around `beginFrame` (and
   back to `kStTerrain`) inside `unitShadowStep`, so `pass 1s` shows it under `units`; no picture change.
   File: `server/src/render/srv_render_scene.cpp`. (b) Then cut inside it on the units' side: `prepare` and the
   controller are 40 % each offline (5056 B/C were declined by the MC; `prepare`'s per-figure pose rows are the next
   profile, `perf/sampler`); files `server/src/units/srv_units_scene.cpp`, `render/src/units/draw.cpp`. (c) A larger
   option for the Architect: begin the units' frame on a worker from the pass's start (its inputs: the frame's camera,
   grid, clock and the fog's plot states), joined in `unitShadowStep`; it overlaps 1-2 ms of predraw / fog / binding.
2. **A coarse cull before the per-tile cull** (`render/src/terrain/terrain.cpp` only, with its test in
   `render/tests/terrain/terrain_test.cpp`): blocks of 8 x 8 tiles with their z range kept where `zmaxAll` is
   (`setHeightField`, `updateHeightRegion`); a block whose 8 corners lie outside one clip plane by a margin culls its 64
   tiles without visiting them (the plane test is linear, so every tile box inside is outside the same plane; the margin
   keeps float rounding from flipping a tile on the edge, and a block within the margin falls back to the per-tile test).
   Same tiles drawn, same order, same counters (`tilesCulled` counted per block at copy 0). Saves most of 0.6 ms
   offline / about 1.2 ms on the laptop per frame.
3. **Per-tile submission** (same file): TerrainTile member offsets found once (not 5 `strcmp` walks per tile),
   `PSSetShader` only when the pass changes, SRV slots bound only when they change. Same GPU state for every draw.
   0.1 ms at 76 tiles, up to about 0.5 ms at 500 tiles zoomed out [I].

**How to verify.** Rule A on every library bundle (runbook 2.4, `scenerun` 4.4; byte-equal dumps); the terrain module's
tests (runbook 1.6, `render/tests/terrain`); for 2, a unit test that compares the drawn tile list and `tilesCulled`
with and without the coarse cull over random cameras including the wrap copies. Numbers: `bench0707.sh` before/after
on the HUGE_0350 bundle (`dp_loop`, `dp_total`), then PERFORMANCE.md statistics (6+ interleaved runs) on oldpc and
the laptop with the live `terrain 1s` cpu_ms and `pass 1s` terrain lines (runbook 4.15 / 4.18).

**Not verified.** The live beginFrame's parts (needs the timer in server.exe or cut 1a) *(done 2026-11-23: 5.12b)*; the
wide view offline (the bundle's 8 frames are all the city view, 76 tiles); a quiet machine (every number here is an
indication, grade C).

*Done 2026-11-23 (ticket 0707, account B): cut 1a (the instrument), cut 2 and cut 3 (terrain-surface.md 15); the units'
part of cut 1b written and measured but NOT merged (5.12b: no live saving).*

### 5.12b 0707: the units' frame begin measured live by part, and the cuts made in it [L: laptop runs; C: code; I: marked]

Written 2026-11-23 17:3x EST by the 0707 implementer (account B, branches `task-0707-terrain-stage-cost`,
`task-0707-units-begin-2`). Readers: `server/evidence/0707/split0707.py` (a render.log window: stages per drawn frame, the
terrain draw's CPU, the begin's parts).

**The instrument (cut 1a) [C].** `server/src/render/srv_render_scene.cpp`: `unitShadowStep` (the fog's plot states and
plot table handed to the units, `UnitsHost::beginFrame`, the units' cast into the shadow map) now runs under the pass's
`units` mark; `terrain` holds the clear and the terrain draw only (as offline, where the begin was already under `units`).
`server/src/units/srv_units_scene.cpp`: the `units 1s` line ends with `begun=N begin_ms=B/A/P/T`, the MEANS over the
second's begun frames of beginFrame's parts: B = build + fog filter, A = the controller (with route S's poll), P =
`prepare` (the drawn units' pose rows and draws), T = the whole begin (`m_beginMs`, the timers 4468 put there).

**Live on the laptop** [L, TB-HUGE_0350 t350, tb1, 30 fps cap, laptop clock; job `20261123-162525-97e6` 16:28-16:31 and
`20261123-171658-0e61` 17:20-17:23, both of the instrumented build; one run each; Desktop Duplication refused (display
idle-off), so only the load's city view and the `wide` case ran]:

| window | units drawn / held | units stage | begin total | build + fog | controller | prepare | terrain stage (= terrain draw) |
|---|---|---|---|---|---|---|---|
| city view, still (16:29:35-49) | 29 / 853 | 2.37 ms | 2.25 | 0.31 | 0.80 | 1.14 | 0.35 |
| wide hold, 352 tiles (16:29:53-16:30:51) | 51 / 853 | 5.07 | 4.75 | 0.46 | 1.60 | 2.69 | 0.67 |
| wide hold (17:21:31-17:22:29) | 51 / 853 | 4.70 | 4.41 | 0.43 | 1.41 | 2.58 | 0.65 |
| city view after the zoom back (16:30:55-16:31:20) | 29 / 853 | 2.13 | 2.01 | 0.44 | 0.82 | 0.74 | 0.41 |

So the begin is about 1 ms fixed over all 853 held units (the controller's per-unit loop and build) plus about 50 us per
drawn unit on the laptop (prepare: pose sampling and the draw lists). It is the `units` stage now: 5.12's "rest" of the
laptop's `terrain` stage (4.6 ms mean) was this begin, the plot copies and the cast; in the wide view the base build's
`terrain` stage was 5.92 ms (job `20261123-171019-18fc`, same set) against 0.65 + 4.70 after.

**Offline profile (sceneshot HUGE_0350, the 899-frame history + 8 shots, `perf/sampler` 2 ms, workstation, an
indication; one-time first-frame stacks removed: `Controller::create`, `ModelCache::build`, `RawFile::load`)** [L,
2026-11-23 16:12; `F:/c5-64bit-work/0707/prof1`, readers `fold.py` / `children.py` / `callers.py` in `F:/c5-64bit-work/0707/scripts`, not in
git]: of beginFrame, the controller 41 % (`Controller::apply` 31 %: `collect` 16 %, of which about 9.6 % building and
freeing the pose programs of units not drawn whose graph may fail; `settle` 8.5 %), `prepare` 32 % (pose rows 27 %:
`eval_curve` 14 % self), `buildList` 20 % (9.5 % in frees of the frame's rebuilt scene), `fillFigures` 6 %. Heap
allocation and free together: 31 % self.

**The cuts tried (U1, U2, U4; MC #11963, #11978, #12004)** [C: code and tests]. They live on the branch
`task-0707-units-begin-2` and are NOT on main: the MC (#12104, 18:3x) would land them only if the oldpc offline series showed
a gain beyond the run-to-run spread; the terrain cut and the instrument landed alone:
- **U1** `render/include/c5r/unitanim/graph.h` `GraphInstance::programStatus` (additive; default: program() into a
  dropped scratch), `render/src/unitanim/graph.cpp` (bindGraph's graphs: the same walk with a sink that keeps nothing),
  `controller.cpp` `collect`: a unit not drawn whose graph's program may fail gets its status checked without a
  `PoseProgram` built and freed. The walk's control flow never reads a sink's answer, so the failures and their texts are
  the same; tests: `test_graph.cpp` `answers()` compares programStatus with program at every point of every run,
  `test_controller_drawn.cpp` a failing selection (code and text equal).
- **U2** `render/src/units/draw.cpp` `prepare`: the per-unit `std::map`s (figure -> input, (member, mesh) -> draw) and the
  figure -> member vector became kept vectors (`units.h`), the same lookups; `render/src/units/pose.cpp` `memoRows`: the
  world transforms of a memo miss are built in a kept vector swapped into the memo slot, the palette's skin and rows
  vectors are kept (`pose.h`); every one is written whole before it is read.
- **U4** `models/src/anim_curves.cpp` `eval_curve`: the padded knot vector (n + degree + 1 floats) was copied into a
  stack array on every call before a binary search and a degree-3 de Boor that read at most 2k + 2 of them; it is now
  read through an index (T[0..k] = 0, T[k+1..n] = K[j-1], T[>n] = K[n-1]): the same values, comparisons and float
  operations. Test: `models/tests/test_anim.cpp` `test_eval_curve_equals_the_copy_form` (the copy form kept as the oracle;
  random curves of degree 0-3, dimension 3/4/9, repeated knots, 1-600 knots including the old heap path above 256, t
  below 0, on knots and past the end: every value bit-equal).
- **U3** (buildList's per-frame scene rebuild, `fillFigures`): approved, waits for W99 (5057) and W102 (5062) to merge.
- **Live, laptop, base against the combined branch (terrain cut + instrument + U1/U2/U4)** [L, one run per arm in one
  set, laptop clock: jobs `20261123-175605-74e3` (main 364413ddd) and `20261123-180157-12a4` (9f6fbe480), the `wide` case,
  `split0707.py`]: wide hold (352 tiles, 51 units drawn) busy per drawn frame 17.00 -> 15.64 ms (-1.36 ms, -8 %): terrain
  + units 6.42 -> 5.48, forest 1.50 -> 0.78; the begin 4.51 ms (build 0.49, controller 1.53, prepare 2.50); city view
  (29 units): the begin 2.75 ms (0.30 / 1.15 / 1.29). **Against the two runs of the terrain-only build (begin 4.41 and
  4.75 ms in the wide hold, 2.01-2.25 in the city view) U1, U2 and U4 show no live saving outside the run-to-run spread**
  [L; grade C]: the offline profile's heap and `eval_curve` shares did not turn into laptop milliseconds (either the
  sampler's suspension inflated the heap's share [I], or the laptop's begin is held by the pose arithmetic itself). 
- **Offline, oldpc, quiet (grade A)** [L, job `20261123-213529-1eab`, 2026-11-23 21:36:39-21:49:58 EST, alone,
  undisturbed, foreign cores 0.028; the terrain branch f598bf1ce against the same + U1/U2/U4 (1c2033b0b), each with
  `bench0707b.diff`; HUGE_0350 city view (29 units drawn), 10 interleaved rounds, 2,320 draws per arm;
  `server/evidence/0707/oldpc-1eab-stats.txt`]: the `units` stage (offline: the begin + the scene pass) 1.222 -> 1.164 ms
  p50, paired **-0.058 ms [-0.065, -0.052] (-4.8 %)**, sd of the round differences 0.012; the scene draw -0.067 ms
  [-0.082, -0.053] (-1.8 %); terrain, forest, draw, water, models unchanged within 0.003 ms. A gain beyond the spread
  offline, about a twentieth of the units' stage: the begin's cost is the pose arithmetic, not the allocations the
  profile showed.
- **Decision (MC #12301, 2026-11-23 21:5x): U1/U2/U4 stay UNMERGED** on `task-0707-units-begin-2` (pushed; one commit,
  1c2033b0b, on top of the terrain branch): 0.058 ms is not worth a second byte-identical risk and a builder pass, and
  ticket 0711 (the units' begin on a worker, research/07 architect-2026-11-23-frame-jobs.md) restructures the begin.
  0711 or a later J step may take them from there (tests included).

**The begin on a worker thread (for the Architect; MC's request, not built) [I].** What it would save: the whole begin
leaves the render thread's pass when it overlaps the pass's earlier work (polls, predraw, the draw's overlay and shadow
binding, the fog): 2.0-2.3 ms a frame in the city view and 4.4-4.8 ms in the wide view on the laptop (above), as long as
the earlier stages are at least as long (laptop wide hold: polls 1.8 + slot 0.7 + predraw 0.9 + draw 3.6 = 7 ms before
`terrain`). Its inputs are known at the pass's start: the frame's camera (`UnitsCameraOf(f)`), the height grid, the frame
clock, the fog's plot states and plot table (copied, generation-checked, today in `unitShadowStep`). Risks: (1) the units
module is not thread-safe (`pose.h`; the pre-check already runs on its own thread with its own CPU-only UnitRenderer):
`prepare` touches the renderer's caches and `open()` the device; the worker would need the CPU part (build, controller,
prepare) split from anything that touches the device context, and the render thread joining before `drawMask` /
`drawPass`; (2) route S's trigger poll and the effects it emits (`pollRouteS`, `say` lines) would run off the render
thread; their order against the render thread's own lines changes the logs, not the picture, but the fight clock reads
the frame's time, which must stay the frame's; (3) the fog's plot states must be the frame's (today copied right before
the begin; with the overlap, copied at the pass's start: the same frame's states, the fog's admit runs in `decide`
before); (4) determinism: the controller's random rolls are per unit and per frame and do not depend on the thread, so
the picture stays exact if the begin for frame N always finishes before frame N's units are drawn (a join, never a
skip) [I]; (5) a hidden cost: the join waits when the earlier stages are shorter than the begin (city view uncapped on
a fast machine). Verification: rule A on every bundle (byte-equal), the units suites, and a live laptop pair.

### 5.16 0711 (J1): the frame-jobs pool, and what the units' begin shares with the pass [C: code read at `2de26b08a`; L: marked]

*(Renumbered 2026-11-24 from 5.15: lead-0673's quiet-pair section, merged first, holds 5.15; code comments of 0711 say 5.16.)*

Written 2026-11-23 18:5x-19:3x EST by the 0711 implementer (account B, branch `task-0711-frame-jobs`). Design: the
Architect's advice `research/07-architecture-decisions/architect-2026-11-23-frame-jobs.md` sections 4-5 (rules R1-R9).

**The pool** (`server/src/render/srv_frame_jobs.{h,cpp}`, standard library and `windows.h` only, so every builder that
globs `server/src/render/*.cpp` compiles it without a list change). One job slot (`FrameJob`) per module, forked and
joined on the render thread; `FrameJobs` holds the workers. How each rule is held:

| Rule | Held by | Proven by (`server/tests/render/frame_jobs/`) |
|---|---|---|
| R1 | the closure's captures are the job's inputs; the pool reads a clock only for `job_ms` / `join_wait_ms` | determinism series (camera and clock by value) |
| R2 | `FrameJobOwner::mayCall`: false while a job owns the module, unless the caller IS the thread running that job (worker, or the render thread running it inline); counted, LOUD on stderr and kept for the host's log; with `FrameJobsSetOwnerChecksFatal(true)` the process ends with exit 0xF1 by `TerminateProcess` (no crash box, D155) | `TestOwnerChecks`, children `owner-fatal`, `owner-counted` |
| R3 | `join` waits on a condition variable with no timeout; `~FrameJob`, `~FrameJobs` and `FrameJobJoinGuard` join a forked job; a second fork of an un-joined job joins it first (LOUD, counted as a misuse); a join on another thread than the fork is a misuse (fatal with checks on) | `TestAlwaysJoined`, `TestThrow`, child `join-thread` |
| R4 | a job is claimed once under the pool's mutex: a worker pops it, or the join finds it still queued and runs it itself; 0 workers = every job inline at its join | `TestDeterminism` counts (jobs 0: all inline; jobs 1: worker + inline = forks) |
| R5 | one closure, one thread; the pool never splits a job | determinism series (a memo with eviction, order-dependent) |
| R6 | `FrameJob::log` collects; `FrameJobResult::lines` hands them over at the join, in order, then a LOUD line if the job threw | `TestLines`, `TestThrow` |
| R7 | each worker starts with the MXCSR of the thread that called `start`; each fork stores the forking thread's MXCSR and the worker runs the job under it, then restores its own | `TestMxcsr` (FTZ, DAZ, round-to-zero set after the pool started); the determinism series under FTZ/DAZ with a sensitivity control (the same series without FTZ/DAZ differs) |
| R8 | `Options::jitter` (to be fed by `render_test_frame_jobs_jitter`): a 0-5 ms sleep on the render thread after each fork is published and in each worker before its claim, so both the worker path and the claim-at-join path are taken | determinism series jobs 2 + jitter: same hashes, both paths counted |
| R9 | the host's (integration) | - |

The key: `render_frame_jobs = auto | 0 | N` (`ParseFrameJobsKey`): auto = 0 with 2 logical cores or fewer, else 2; N 0..8;
anything else refused with a reason (empty, sign, spaces, upper case, above 8).

**The suite's result** [L, 2026-11-23 19:58-20:01 EST, workstation, not quiet; `bash server/evidence/0711/suites_0711.sh
C:/c5-64bit-work/0711/fj`, output in `server/evidence/0711/acceptance_0711.txt`]: plain (clang-cl /W3 /WX /O2) and ASan
(MSVC /fsanitize=address) both build and pass, 5 of 5 pytest cases each (the in-process program with about 4,390
checks, the three negative children, the usage errors). How often a worker claims (per-frame hashes equal in every arm):

| Arm (400 frames; jitter 150) | plain: worker / inline | ASan: worker / inline |
|---|---|---|
| jobs 1, 300 us of render-thread work between fork and join | 391 / 9 | 316 / 84 |
| jobs 2, the same | 391 / 9 | 328 / 72 |
| jobs 2 + jitter (0-5 ms sleeps) | 135 / 15 | 132 / 18 |

Without the 300 us window (fork then join at once) the joining thread won every claim (0 / 400 on the first plain run):
a worker's wake from its condition variable takes longer than an empty window. So the window the integration forks over
(1.7-2.5 ms on the laptop's wide hold, the Architect's 3.1 [I]) is what lets the worker take the begin; on a loaded machine
some begins still run inline at the join (R4), at today's serial cost. The `inline` count of `units 1s` will show it.

**What the begin shares with the window between the fork and the join** (Architect section 9's "read first"):

1. **The fog's plot states and plot table are final before `decide`** [C]. `FowScene::m_states`, `m_fogGen`, `m_w/m_h`
   and the plot-table copy `m_rec` are written only in `readTable` (`srv_fow_scene.cpp` 316-370), called only from
   `init` (278) and `onBatch` (423, the feed drain). `beforeWorld` (695, inside the window) writes none of them. So
   `setPlotStates` and `HandPlotTable` moved from `unitShadowStep` (2864-2866, or 1269-1271) to the serial head after
   `decide` copy the same values. (That the feed drain precedes `decide` is the Architect's 3.1 [C by the Architect].)
2. **The game file system is not shared** [C]. The units open their own `c5r::gamefs::GameFiles` in their `Env`
   (`srv_units_core.h` 415-420, `LiveEnvOpener` `srv_units_scene.cpp` 1528); the path models (`Scene::pathInit`) and the
   bake use other instances. And `GameFiles::read` is const and stateless: an FPK read copies from a read-only file
   mapping (`fpk.cpp` 179-186), a loose read opens the file per call; no `mutable` member in `render/src/gamefs` [C].
3. **The feed is not read by the begin** [C]: `UnitsCore::build` (`srv_units_core.cpp` 1261) reads no `FeedPort`; the
   port is read in the batch path (`takeTable` 603, 650) under `poll`, before the fork. The units' TEST files go to their
   own worker (`testPost`) already.
4. **Mutable statics reachable from the begin** [C: grep of `render/src/units`, `render/src/unitanim`, `models/src`,
   `server/src/units` for function and file statics]: `models/src/gr2_file.cpp` 71-84 `crc32`'s table is filled behind a
   plain `static bool init` (a data race on the first two concurrent calls; both write the same values, so it is benign
   on x64 but formally undefined; the landmarks' open thread `srv_models_scene.cpp` 586 can already race it today). A
   one-line fix (a function-local `static const` table filled by a lambda, a thread-safe magic static) belongs to the
   owner of `models/src` [I: not filed yet; named to the MC]. `controller.cpp` 141-160 (the missing-clip registry) is
   under a mutex; `art_db.cpp` 52 and `LiveFeedPort` are magic statics, thread-safe. No `thread_local` (the Architect's
   grep, confirmed).
5. **Log lines from inside the begin** [C]: `say` (animFrame's first-frame lines, `filterByFog`'s once lines, `fail`),
   the core's `m_log` (`note`, `frameFailed`, `afterPrepare`'s warnings, `srv_units_core.cpp` 570-590, 1674-1693) and the
   controller's missing-clip sink (`missingClipLoud`, which falls back to stderr). They go to `RNote`, which is
   thread-safe, but R6 wants them in order at the join: the integration routes the host's `say` and the core's `m_log`
   into the job's `log` while the job runs, and the missing-clip sink likewise.
6. **Render-thread calls into the units module in the window** [C: every `m_units->` in `srv_render_scene.cpp`]:
   none between `decide` and `unitShadowStep` (the calls are `poll` 846 before, the begin and `drawMask` in
   `unitShadowStep`, `beginFrame` 1272, `drawReflection` 1341, `drawScene` 1392, the second and totals lines 1517 / 1572,
   `ready()` in `unitsFrameDone` 2803 at the end). These are the owner-check points.
7. **Where the join must be**: `unitShadowStep` returns early (no begin) on quality 0 or no shadow triple; the begin
   then runs at 1264-1273. Both places join; the fork does not depend on which one will.

**Integration** (named to the MC on the thread, #12116, before any code): `srv_units_scene.{h,cpp}`,
`srv_render_scene.{h,cpp}`, `srv_render_host.cpp`, `srv_scene_bundle.cpp`, `server/tools/sceneshot/sceneshot.cpp`,
`server/tools/scenerun/scenerun.py`. It waits for 0704, 5062 and 0707's terrain cut (they hold those files).

#### 5.16.1 The integration as built (ticket 0711, implementer a0c94989, branch `task-0711-integration-units-begin`, 2026-11-24) [C: the code; L: marked]

Files changed: `server/src/units/srv_units_scene.{h,cpp}`, `server/src/render/srv_render_scene.{h,cpp}`,
`server/src/render/srv_render_host.{h,cpp}` (the `.h` holds `HostConfig`'s two new fields: one more file than the
approved list, named to the MC), `server/tools/sceneshot/sceneshot.cpp`. Not changed after all: `srv_scene_bundle.cpp`
(the recorder's `render_config` is written by `RecorderRenderConfig` in `srv_render_host.cpp`), `scenerun.py` (it starts
sceneshot, which reads the keys itself and inherits the TEST variables), `build_sceneshot.py`, `unitfeed.cpp`
(`beginFrame` keeps its shape).

- **The split** (`UnitsHost`): `beginFrame(cam, grid)` = `beginHead` + `beginBody(nullptr)`. Head (render thread): the
  frame flags, the ONE clock value `t` (R1), `noteFrame`, the readiness returns, `openDraw` (0704's take of the worker's
  open), route S's `setGrid` + `pollRouteS` (moved before the build: it reads only the shell's answer and the build reads
  nothing it writes; only the log lines' order changes), the camera and the grid stored by value. Body (the job): build,
  fog filter, `animFrame`, the three `prepare`s, `afterPrepare`, `ApplyUnitXforms`, the timers, `m_frameBegun`, in
  beginFrame's order (R5). `begin_ms` part A (the controller) no longer holds route S's poll (a read of the section, a few us).
- **The fork** (`Scene::forkFrameJobs(f)`): the fog's plot states and plot table handed to the units (final before
  `decide`, item 1 above), `beginHead`, then `beginBody` forked as the job `units begin` owning the units module
  (`FrameJobOwner "units"`). Called by the host right after `decide` returned true (not with a recorder: the recorder's
  `beforeDraw` reads the unit scene after the slot, so a recording host forks at the top of `Scene::draw`, as the players
  do). The head counts to the `units` stage.
- **The join** (`Scene::joinUnitsJob`): in `unitShadowStep` before the mask pass (where the begin was), else at the later
  begin place before the reflection pass; a guard at every return of `Scene::draw`; the host after `DrawFrame` on every
  path (a busy slot, a lost device, an error); `releaseDevice`, `setUnits`, `configureUnits(false)` and the destructor.
  At the join the body's lines are written in order (R6: the host's `say`, the core's log and the extras' log route
  through `say`, which collects while the body runs on that thread), then the owner checks' LOUD lines. A job that threw
  rethrows on the render thread at the normal join places (as an exception of the begin did before), not in the guard.
- **R2 owner checks**: every `SceneUnits` entry point of `UnitsHost` calls `m_owner.mayCall("UnitsHost::<fn>")`; fatal
  (exit 0xF1, no box) with `render_test_frame_jobs_jitter=1` (`FrameJobsSetOwnerChecksFatal(true)` at the pool's start).
- **R6 and IPC, a deviation named to the MC**: the controller's frame writes route S's trigger records into the trigger
  section (`SectionSink::record` -> `VPTrigWrite`, "the server's one writer thread only") from inside `animFrame`, so with
  workers they are written by the worker. They are never written by two threads at once (the module is owned; the pool's
  mutex orders the hand-over), in today's order, and before the frame's slot is READY (the host marks it after the join),
  so 4722's R1 and the section's one-writer rule hold; only the writing thread changes. Buffering them to the join would
  need a change in `server/src/effects/` (lead-0042's) and `name()` returns an index at once. The controller's
  missing-clip sink is not set by the server (stderr fallback), unchanged. **Accepted by the MC (#12834, 2026-11-24
  12:5x)**, with a check: the ordering argument in full is (a) the trigger stream (`UnitsHost::m_trig`) is reached only
  through the unit host (`triggerStream()` has no caller in server.exe; unitfeed is one thread); (b) its writers in the
  host are `beginHead` (route S's poll, render thread), `beginBody` (the controller's frame: records, names; the job),
  `poll` (the stream made or dropped at a new database) and `feed`; (c) while the job runs the render thread calls no
  host entry point (R2's owner checks on every one), so the job is the only writer; (d) the fork publishes the job under
  the pool's mutex after the head's last write, and the join takes the mutex after the job's last write, so each
  hand-over is a happens-before edge (no torn or reordered record); (e) the shell reads the frame's records only after
  the host marks the frame's slot READY, which is after the join. The check: `FrameJobsOneWriter` (srv_frame_jobs.h)
  around those four writer paths; a claim while another thread holds it is counted and LOUD, fatal with the owner
  checks fatal (the jitter key, the tests); `frame_jobs_test.exe` `TestOneWriter` (a pool hand-over: no violation; two
  threads at once: a violation) and child `writer-fatal` (exit 0xF1).
- **R9**: the host stamps `SceneFrame::nowMs = QpcToMs(pass start)` after `decide` when it is negative (live without a
  recorder); a recording host keeps the recorder's value. The units, the water, the fog, the arc and the path read it.
- **Keys**: `render_frame_jobs = auto | 0 | N` (`ParseHostConfig`, absent = auto, resolved to a number) and
  `render_test_frame_jobs_jitter = 0 | 1` into `HostConfig` -> `SceneOptions::frameJobs / frameJobsJitter`; the pool is
  made once per `Scene` in `init`. The recorder writes both (resolved) into every bundle's `render_config`; sceneshot
  replays them, a bundle without them with 0; the TEST variables `C5_SCENESHOT_FRAME_JOBS` (0..8) and
  `C5_SCENESHOT_FRAME_JOBS_JITTER` (0/1) replace the bundle's (rule A's three runs; line `sceneshot: render_config: frame jobs N, jitter J`).
- **Numbers**: `units 1s` ends its begin part with `jobs=N job_ms=M join_wait_ms=mean/max inline=K` (the second's
  begins run as jobs; job_ms on whichever thread ran it; inline = no worker had claimed it at the join, R4);
  `units totals` gains `| jobs N inline=K`. `pass 1s` gains `| draw split max/mean ms: draw_head=.. draw_mid=.. draw_end=..`
  (the `draw` stage's three stretches: before the units' mark; after the water up to the forest mark (arc, extras, the
  path's depth); the rest (cloud planes, path, and what the host counts to `draw` around the scene's call)). Reader:
  `server/evidence/0711/split0711.py` (split0707's table plus the job numbers and the draw split).
- **What changes live with 0 workers**: the head runs before the predraw, the body at the join (the old place): the
  same calls, values and order but for route S's poll (above); the clock is the pass start (R9).
- **A known live-only difference**: a frame whose slot is busy after `decide` (3302's retry to the deadline) used to skip
  the begin; now its begin has run (forked) and the camera is not drawn. Before 0711 a frame whose terrain draw failed
  already had its begin run without a picture; the controller's clock is the frame clock, so the next drawn frame
  advances from there [I].

Tests and series: the commands are in `server/evidence/0711/allb_0711.sh` (phases srv, fj, 2, 2a, 3, 3a, 5, rule A ra0 /
ra1 / raj) and `server/evidence/0711/laptop_series0711.sh`; the results follow in 5.16.2.

#### 5.16.2 Results: rule A, the builders, the laptop series [L: the runs named; I: marked]

**Rule A** [L, 2026-11-24, workstation, `run_all.py --only offline-equals-live`, library C: copy, asset root mods548-vt,
sceneshot of 5b40375d5's scene code]: 17/17 bundles passed with `C5_SCENESHOT_FRAME_JOBS=0` (13:42-13:56), `=1`
(13:56-14:10) and `=1` + `C5_SCENESHOT_FRAME_JOBS_JITTER=1` (14:10-14:23). pytest does not show sceneshot's lines, so the
paths were shown on one bundle (C5H-4980-C3AGARRISON) with a6890c539's sceneshot at tolerance 0:0: jobs 0: 8 forks, 8
inline; jobs 1: 8 worker runs; jobs 2 + jitter: 7 worker, 1 inline; every shot byte-equal, no owner violation.

**Builders** [L, 2026-11-24 12:46-14:36, workstation, `allb_0711.sh`]: server.exe release on the 0.1b pins; the pool suite
6/6 plain and ASan (TestOneWriter, TestPriority added); U6: units 7/7 (test_units_runner), units ASan 85/85 cases
(`test_scene_units.exe` alone, 19 min under load: the pytest wrapper's 900 s timeout fired first, a time-out and not a
failure), units extras 13/13, unitfeed, trigger_scene, farm fields, unit shadow, sceneshot `--smoke`; run_all render-host
211, scenebundle-programs (24 cases, 4 programs), scenebundle-py 29, fow / overlay / shadow-ice / water / forest scene
suites. ASan run_all of render-host and the five scene suites: see the acceptance file.

**The laptop series, first set** [L, laptop host clock, 2026-11-24 13:50-14:48, build 5b40375d5 (workers at NORMAL
priority), TB-HUGE_0350 t350, the `wide` case (60 s hold zoomed out) and the city view after the zoom back (hold end +5 to
+25 s), `render_pass_long_ms=8`, `render_frame_jobs` 0 / 2 ABBA, 6 jobs an arm, jobs 20261124-132016-ad96 ..
20261124-135728-1242; `series0711.py`, `C:/c5-64bit-work/0711i/series1.txt` (not in git)]. Per drawn frame, paired
differences of neighbouring jobs (arm 2 - arm 0), mean and 95 % t interval over 6 pairs:

| window | busy arm 0 | busy arm 2 | busy diff | units stage arm 0 / 2 | units diff | begin (job_ms) 0 / 2 | join wait (arm 2) |
|---|---|---|---|---|---|---|---|
| wide hold (352 tiles, 51 units drawn) | 32.35 | 31.09 | **-1.26 [-2.15, -0.36]** | 7.42 / 6.06 | -1.36 [-1.53, -1.18] | 6.71 / 7.28 | 5.34 |
| city view after the zoom back (29 units) | 16.91 | 15.02 | -1.88 [-4.52, +0.76] | 3.61 / 2.51 | -1.10 [-1.24, -0.97] | 3.30 / 5.14 | 2.18 |

Inline share in arm 2: 0-0.3 % (the worker took almost every begin). The ticket's bars (wide -1.5 ms or more, city -2.0 or
more, intervals excluding 0) are NOT met by this set. Three findings:

- **(a) The window before the join is small** [L]. The draw stage (4.0-4.2 ms per frame here) is nearly all its END
  stretch (`draw_end`: the cloud planes, with the fog particles, and the path); `draw_head` is 0.02 ms and `draw_mid` 0.00.
  So what runs beside the job is the predraw (1.4-1.8 ms in the wide hold) and the fog's mask: the job (7.3 ms) waits
  5.3 ms at the join. The Architect's 1.7-2.5 ms estimate of the window (section 3.1 of the advice) holds; the begin is
  longer here than in 0707's runs (6.4-6.9 ms against 4.4-4.75), so the saving is the window, not the begin. J2 (0713:
  the polls not needed by `decide` or the units after the fork; polls are 4.6-4.9 ms per frame on this run) is the lever.
- **(b) The begin runs slower on a worker** [L]: job_ms 7.28 against 6.71 inline (wide), 5.14 against 3.30 (city). The
  workers ran at NORMAL priority while the render thread runs ABOVE_NORMAL, on a 4-core / 8-thread i7-7700HQ shared with
  the game's threads [I: the cause]. a6890c539 starts each worker at the starter's priority (TestPriority); the second
  set measures it.
- **(c) The laptop ran 1.5-2x slower than in 0707's runs** [L]: busy 31-33 ms per frame in the wide hold at the 30 fps cap
  (0707: 16.6-17.0), polls 4.6-4.9 ms (0707: 1.8), idle 7.7-8.3 ms. The testbed runner's memory growth (ticket 0723: 0.4
  GB above the game floor) is a candidate cause [I]; the MC restarts the runner before the second set, whose jobs-0 arm
  says whether busy moved. *(2026-11-24 15:5x, 5.16.3: it did not move; the runner is not the cause.)*

#### 5.16.3 The laptop series, second set: the final build, after the runner restart [L: the runs named; I: marked]

Written 2026-11-24 16:0x by the 0711 implementer (research-only follow-up on branch `task-0711-series2`, MC #12985).
Build a6890c539 = main's 0711 code (workers at the render thread's ABOVE_NORMAL priority, `FrameJobsOneWriter`), server.exe
sha256 b2522fa1...; the laptop's testbed runner restarted 14:52 on runner 0723.1 (MC #12977). The same set-up as 5.16.2
(TB-HUGE_0350, `wide` case, city view after the zoom back, `render_frame_jobs` 0 / 2 ABBA, 6 jobs an arm), jobs
20261124-145449-c1b1 .. 20261124-150314-14f3, 14:55-15:53 laptop clock; `series0711.py`, `C:/c5-64bit-work/0711i/series2.txt`
(not in git). Paired differences (arm 2 - arm 0) over 6 pairs, mean and 95 % t interval:

| window | busy arm 0 | busy arm 2 | busy diff | units stage arm 0 / 2 | units diff | begin (job_ms) 0 / 2 | join wait (arm 2) |
|---|---|---|---|---|---|---|---|
| wide hold | 32.89 | 31.12 | **-1.77 [-2.44, -1.10]** | 7.58 / 6.07 | -1.51 [-1.90, -1.11] | 6.92 / 7.27 | 5.36 |
| city view after the zoom back | 16.53 | 15.38 | -1.15 [-2.20, -0.10] | 3.38 / 2.57 | -0.81 [-1.38, -0.23] | 3.07 / 5.73 | 2.24 |

- **Against the bars**: wide hold -1.5 ms or more: met by the mean (-1.77), the interval's near end is -1.10 (not wholly
  beyond -1.5); city view -2.0 ms or more: not met. Both intervals exclude 0 now.
- **(c) settled**: the jobs-0 arm's wide busy is 32.89 against 32.35 in the first set (city 16.53 against 16.91): the
  runner's restart did not make the laptop faster, so its memory growth is not why this laptop runs 1.5-2x slower than in
  0707's runs [L]. What did change since 0707's runs is not known (main moved by a day of merges; the save and the case are
  the same) [I].
- **(b) not closed by the priority**: on the worker the begin still takes longer than inline (wide 7.27 against 6.92; city
  5.73 against 3.07, nearly twice) [L]. With the render thread waiting at the join for most of the job, a slower worker
  costs the frame directly. Candidates [I, not measured]: the worker starts cold (caches, the pose memo's pages last
  touched by the render thread), a hyperthread sibling shared with a busy game thread on the 4-core i7-7700HQ, the wake;
  `job_ms` counts from the claim, so the wake is not in it. A test for J2: pin the worker to a core away from the
  render thread's sibling, or have the render thread run the job itself when the window is short (the city view).
- **For J2 (0713)**: the window before the join stays the predraw (1.4-1.8 ms wide) plus a 0.02 ms draw head; the polls
  (4.6-4.9 ms a frame on this laptop) are the room J2 moves after the fork.

### 5.17 0713: the prepare phase (the drain and the polls before the camera) [C: the code; L: marked]

Written 2026-11-24 by the 0713 implementer (account B, branch `task-0713-prepare-phase`). Design: the Architect's
frame-overlap advice, `research/07-architecture-decisions/architect-2026-11-23-frame-overlap.md` section 6 (design 5),
which replaced the frame-jobs note's J2.

**The contract** [C] (`shell/common/vp_frame.h`): `VPFramePublish::tapPresent` (int64 at offset 176, taken from
`reserved`; layout, size 256 and `VPFRAME_VERSION` unchanged, so a proxy or server before 0713 reads the block as before
and a composite before 0713 leaves the field 0) and the named auto-reset event `<base>.Tap`, created or opened by both
sides. The composite (`shell/composite/shell_composite.cpp`, `LkTapsDone`) opens the event with `<base>.Frame`
(`EnsureFrameSection`; a failure is one log line, no fault) and, in `OnPresent` right after `UnitFilter_OnPresent` (the
last of the Present hook's taps), stores the Present's number (`g_camPresents`, the camera block's `presentCount` of that
Present) with one interlocked 64-bit write, then sets the event. Main thread, about 1 us; it runs in both server arms.

**The server** [C] (`server/src/render/srv_render_host.cpp`, `Loop`; key `render_prepare = 1 | 0`, default 1,
`HostConfig::prepare`):
- The wait is on the stop, `<base>.Cam` and `<base>.Tap` (in that order, so when both are set the camera's pass comes
  first), and on `<base>.Tap` only in event pacing (before the first publish the slow waits stay as they were).
- A pass woken by the tap with no camera pending is a **prepare pass**: it reads `tapPresent`, then drains the feed and
  polls the taps and the fatal steps (`TakeInputs`), all counted to the new pass stage `prepare` (`shell/common/vp_pacing.h`,
  between `feed` and `decide`; readers that parse `pass 1s:` by name see one more name). It does not decide or draw. A tap
  pass whose number was already taken (a camera pass polled after the tap was written) does nothing (`prepare_nothing`).
- A pass woken by the camera whose `tapPresent` equals the prepared number goes **straight to decide** (`cam_hits`). Any
  other camera pass (no prepare in time, a newer tap, a composite without the event) drains and polls first as before
  0713 and notes the number it read (`cam_polls`). The 100 ms timeout passes and the legacy poll are unchanged.
- **A frame about to be held** (`decide` false with `Scene::holdFrameId()` set, 0691) in a pass that took no inputs drains
  and polls once more and decides again (`hold_repolls`).
- **The idle bakes**: the residency's yield check (`CameraWaiting`) also answers yes while a tap is pending (a
  `tapPresent` newer than the prepared number; counted `tap_yields`, apart from the camera's yields), and a pass starts
  its idle slice with budget 0 while the camera or the tap is pending. So the prepare runs before any idle bake, and no
  bake starts while the tap or the camera waits.
- Off (every pass as before) with `render_prepare=0`, with a recorder (`render_test_record`: its iteration is drain +
  draw), or when the event cannot be opened (LOUD line). The offline players (sceneshot, the scene bundle's programs) do
  not run `Loop` and are unchanged; no `Scene` code changed.
- A new composite pid resets the prepared number (-1), as it resets the frame ids.
- Lines: `prepare 1s: on= tap_event= tap_present= prepared= prepare_passes= prepare_nothing= cam_hits= cam_polls=
  hold_repolls= tap_yields=` (running totals) beside `pass 1s:`, and the same counts at the end of the host's summary.
  Reader: `server/evidence/0713/series0713.py` (per window: ready - publish p50/p90 from the lockstep CSV, the polls, feed
  and prepare stages per drawn frame, the hit share, the models line's `older` pairs, held rows).

**What changes in which inputs a frame sees** [C; I where marked]: the taps a frame sees are those of the Present before
its camera, as before (the composite writes them in that Present's hook, before the next `UpdateViews`). The feed is
drained up to the time between that Present's last tap and the camera earlier, so an event pushed in between is drawn one
frame later than before. Two taps finish on worker threads after the hook (the height-field copy, the cut tap's worker):
a result that lands between the prepare and the camera is now taken at the next frame's prepare [I: both carry
load-time data; a frame held for the field is covered by the hold re-poll]. The model tap is paired in the draw
(`ModelsScene::beginFrame`), not in the polls, so its pairing (`pair exact/older`, 5.11) does not move.

**Tests** [L, 2026-11-24, workstation]: `server/tests/render/test_render_host.py` (render_host_test.exe against the fake
composite of `renderfx.py`, which gains `FrameSection.tap_present`): `test_prepare_phase_cameras_go_straight_to_decide`
(100 frames with the tap event: prepare passes and camera hits >= 60, hits >= 3x polls, every camera drawn, a `prepare`
stage in `pass 1s`), `..._off_polls_at_every_camera` (`render_prepare=0`: no counts, no line, no stage),
`..._a_newer_tap_polls_at_the_camera` (a number without the event: 0 hits, every camera polls),
`..._without_a_tap_number_is_todays_loop` (a composite before 0713), and `render_prepare` 2 / on refused in
`test_config_refused`. `shell/tests/test_lockstep.py::test_tap_event_after_the_last_tap_of_each_present` (test_host.exe
with the composite and the lockstep_server stand-in): a waiter on `<base>.Tap` sees `tapPresent` grow by one per Present.

#### 5.17.1 Results [L: the runs named]

**The laptop series** [L, laptop host clock, 2026-11-24 15:52-16:59 EST, one build of `task-0713-prepare-phase` @281c96d08
(server.exe sha256 c08e7ed0..., proxy 224de397..., composite 780372cd...), TB-HUGE_0350 t350, `capture_job.py --arm product
--cases drag,wide` (four recorded drags 300 / 1000 ms right and left, then the wide hold of 60 s zoomed out and the city
view after the zoom back, hold end +5 to +25 s), lockstep exact, the 30 fps cap, `render_pass_long_ms=8`, `render_prepare`
0 / 1 ABBA, 12 `--quiet` jobs 20261124-155232-42e5 .. 20261124-160424-b7b5 (every one `alone=True undisturbed=False`,
foreign cores 0.22-0.41: the testbed's runner and the capture beside the game); `laptop_series0713.sh`, `series0713.py`,
`caplock.py`; tables in `server/evidence/0713/laptop_series0713.txt`]. **A Windows Settings window ('Power & sleep') was
open over the game in all 12 jobs** [L, found 2026-11-24 17:3x after lead-0673's report #13179: every one of the 48 recorded
drag crops has the same statistics, mean 253.98 sd 12.38 (`capture.json` `reduced`), and a fetched frame shows the Settings
page]. The game still took the input (its camera moved in every drag window and the wide hold zoomed out), and the window was
there in both arms for the whole interleaved series, so the paired differences below compare like with like; the absolute
values may differ from an uncovered desktop [I: a partly covered window can lose independent flip and be composed by DWM]. Per window, arm means over 6 jobs and the paired
difference of neighbouring jobs (arm 1 - arm 0), mean and 95 % t interval over 6 pairs [L, the covered desktop]; the absolute values [I] for an uncovered desktop (measured uncovered on the workstation 2026-11-24: 5.17.2); stages in ms per drawn frame:

| window | ready - publish p50 0 / 1 | diff | p90 diff | composite wait p50 diff | wake delay 0 / 1 | polls 0 / 1 | prepare (1) | hit share |
|---|---|---|---|---|---|---|---|---|
| city view (at the cap: 30.4 / 30.2 fps) | 12.59 / 11.47 | **-1.11 [-2.11, -0.11]** | -0.59 [-2.26, +1.09] | -0.92 [-1.27, -0.58] | 0.05 / 0.06 | 1.79 / 0.02 | 1.13 | 1.00 |
| wide hold (server-bound: 28.6 / 28.8 fps) | 24.48 / 23.20 | -1.28 [-3.02, +0.47] | **-2.28 [-4.47, -0.10]** | -2.30 [-3.60, -1.01] | 8.65 / 2.26 | 5.41 / 0.58 | 4.47 | 0.88 |
| the four drags (14.3 / 14.3 fps) | 22.98 / 22.24 | -0.74 [-2.03, +0.55] | -1.13 [-5.93, +3.67] | -0.90 [-1.58, -0.23] | 8.97 / 6.04 | 1.61 / 0.57 | 1.32 | - |

- **The acceptance's bar** [L]: the drain and the polls are a stage of their own (`prepare`) outside ready - publish in
  every window. At the cap (the city view) ready - publish p50 falls by 1.11 ms: by the `prepare` stage's own mean (1.13
  ms) within 0.02, but by 0.68 ms less than arm 0's polls stage (1.79 ms). The same work costs 0.66 ms less as a prepare
  than at the camera [I: at the camera it runs beside the EXE's main thread recording the frame, at the tap the EXE is in
  Present and the cap's sleep]. So "falls by the polls' stage mean within 0.3 ms" holds against the moved work's own cost
  and is NOT met against arm 0's polls stage. Not at the cap (the wide hold, the drags) the window between the tap and the
  camera (the Present call and the head, under 1 ms here [I]) is shorter than the prepare (4.47 ms in the wide hold): 12 % of
  cameras still poll first, and in the others the camera waits for the end of the running prepare, so p50 does not move
  significantly; p90 falls by 2.28 ms in the wide hold.
- **The wake delay** (publish to the camera's pass) falls by 6.39 ms [-7.53, -5.24] in the wide hold [L]: in arm 0 the
  camera comes while an idle bake slice runs (a bake started before the camera is finished first: idle 4.65 ms per frame);
  in arm 1 the tap pending ends the slice at the Present and the prepare runs there (idle 2.68 ms per frame, `tap_yields`
  1,244-1,757 per job) [I: the mechanism, from the counts]. That is most of the composite wait's -2.30 ms.
- **Busy rises in the drags** (+4.46 ms [2.37, 6.56] per drawn frame) by the idle stage (+4.09): each tap pass also runs an
  idle slice, so more bakes run between frames at 14 fps; they yield to the tap and the camera, and ready - publish does
  not rise [L].
- **F5: NOT MEASURED** (corrected 2026-11-24 17:3x; was "unchanged, 100 % in both arms"): `caplock.py` joins the recorded
  frames to the presents, and the recorded crop was the Settings window, not the game. The CSV's own counts: every present
  of both arms is `exact` (kind 0) but the load's 2 `none`, 37 `noins` and 2-3 `held` per job; `older` 0. **Held frames**: one hold episode per job in both arms (at the load; held rows 0 in every window), `hold_repolls` 28-31 per job
  of arm 1, all at the load. **Older model taps** (5.11's `pair older`, per window): wide 1,766 / 1,758 (diff -8 [-76,
  +60]), city 626 / 631 (+4.8 [-4.9, +14.5]): unchanged.
- Counts per arm-1 job (`prepare 1s` totals at the end): prepare passes 3,364-4,092, camera hits 3,370-4,103, camera polls
  439-726, nothing to prepare 186-272.

**Rule A** [L, 2026-11-24 17:06-17:20, workstation, MC-B's slot, `run_all.py --only offline-equals-live`, the C: library copy (16 bundles, 8.84 GB, matching the F: master), asset root mods548-vt]: 17/17 passed byte-equal (the offline players do not run the host loop; no `Scene` code changed). **Builders** [L, 2026-11-24 15:45-17:5x, workstation, not quiet, `allb_0713.sh`]: server.exe on the 0.1b pins, the proxy, the shell (composite) and shell/frame plain and ASan, the render tests, units, units extras, unitfeed, farm fields, unit shadow, sceneshot --smoke; run_all render-host 223/223, scenebundle-programs 4/4, scenebundle-py 30/30, fow / overlay / shadow-ice / water / forest scene suites; the pacing unit 10/10 and its x86 ASan build; test_pacing.py and test_lockstep.py (exact and held cases, plain and ASan, the new tap case plain and ASan); two load-sensitive cases failed once each and passed alone (listed in `server/evidence/0713/acceptance_0713.txt`). ASan render-host: 222 of 223; the new `..._a_newer_tap_polls_at_the_camera` saw 1 camera hit in 60 on the slow ASan host (a legitimate hit: the pass of camera F ran after F's tap number was written); the case now allows hits up to 10 % of the polls and passed 3 of 3 runs against the same ASan build.

**Not measured**: an uncapped laptop; the stock EXE's same stretches. (The quiet workstation arm, listed here until
2026-11-24 20:30, is 5.17.2.)

#### 5.17.2 The quiet workstation arm [L: the runs named; I: marked]

**The series** [L, workstation clock, 2026-11-24 19:31-20:25 EST, MC-B's quiet window 19:30-20:30 with account B on full
hold (MC #13185), impl-0713q]: one build of main a6c266f72 (0713 merged as d967c2735) on the 0.1b pins (server.exe sha256
75c48730..., proxy afaf0a50..., composite 62d52728...), pool instance c5h-new-a (EXE build 26577625, mods548, adapter RTX
4080), the save copy `C5H-0713Q-HUGE_0350` of the instance's `C5H-VP8P-HUGE_0350` (SHA-256 c499d3c6..., the laptop's
TB-HUGE_0350, t350), the reporter's `[Renderer]` set, lockstep exact, the harness's 30 fps cap (no `--timing`, as the
laptop's capture_job launches), `render_pass_long_ms=8`, `render_prepare` 0 / 1 ABBA over 11 launches (pairs 1-2 .. 9-10;
launch 11, arm 1, unpaired; launch 12 not started for the window's end). Per launch the laptop's inputs (`ws_drive0713.py`:
the four drags 300 / 1000 ms right and left after a LookAt of the capital, the wheel -30, 3 s, the wide hold of 60 s, the
wheel back, the city view hold end +5 to +25 s), the quiet lock taken for each launch (`quietlock run --take`): all 11
holds `disturbed: false`, CPU mean 16.5-22.3 % (the game itself). Scripts `server/evidence/0713/ws_series0713.sh`,
`ws_drive0713.py`, `mk_jobs0713.py` (lays the cut files out as the laptop's fetched jobs), then `series0713.py`
unchanged; tables in `server/evidence/0713/ws_series0713.txt`. **The window**: the no-focus launcher's hidden window
(rect -4680,0 to -2120,1440) lies wholly outside the virtual screen (-1920,0, 5560 x 1920) in all 11 launches: on no
monitor, so no window covers it, and the desktop's foreground window (the owner's editor) is never over it [L, the drive's
window line]. This is an uncovered window, not a visible one: whether DWM gives an off-screen window the same
presentation path as a visible uncovered one is not known [I]. Per window, arm means (n = 5 / 6) and the paired
difference (arm 1 - arm 0), mean and 95 % t interval over 5 pairs, ms per drawn frame [L]:

| window | ready - publish p50 0 / 1 | diff | p90 diff | composite wait p50 0 / 1, diff | wake delay 0 / 1 | polls 0 / 1 | prepare (1) | hit share | fps 0 / 1 |
|---|---|---|---|---|---|---|---|---|---|
| city view (at the cap) | 4.83 / 4.39 | **-0.51 [-0.61, -0.40]** | **-0.84 [-0.98, -0.71]** | 1.50 / 0.82, **-0.73 [-1.08, -0.37]** | 0.03 / 0.04 | 0.55 / 0.005 | 0.57 | 1.00 | 30.6 / 30.5 |
| wide hold (at the cap here) | 10.86 / 10.71 | -0.25 [-0.84, +0.33] | -0.29 [-0.69, +0.10] | 3.51 / 3.96, **+0.44 [+0.10, +0.78]** | 0.03 / 1.32 (**+1.30 [1.12, 1.47]**) | 2.13 / 0.005 | 2.19 | 1.00 | 30.5 / 30.2 (-0.40 [-0.68, -0.12]) |
| the four drags | 6.01 / 5.13 | **-0.93 [-1.71, -0.15]** | **-1.06 [-2.04, -0.09]** | 2.33 / 1.38, **-0.97 [-1.56, -0.38]** | 0.57 / 0.28 | 0.78 / 0.08 | 0.71 | - | 22.8 / 21.8 |

- **The city view meets the acceptance's bar here** [L]: ready - publish p50 falls by 0.51 ms against arm 0's polls stage
  of 0.55 ms (within 0.04; the bar is 0.3), and the prepare's own mean is 0.57 ms. On the laptop it fell 0.68 ms short of
  the polls stage (5.17.1), so the laptop's shortfall is not the prepare's design [I: the laptop's covered window, or its
  slower CPU, made the polls dearer at the camera than at the tap]. The composite's wait p50 falls 0.73 ms.
- **The wide hold does not gain here** [L]: the prepare (2.19 ms) is longer than the stretch between the tap and the
  camera, so every camera is a hit (1.00) but waits for the running prepare: the wake delay rises 1.30 ms, ready - publish
  does not move significantly, the composite's wait rises 0.44 ms and the frame rate drops 0.40 fps (30.5 to 30.2). The
  difference, about 0.9 ms, is that stretch [I: prepare minus the wake's rise]. The laptop's wide-hold gain (wake -6.39
  ms, wait -2.30) came from the tap ending an idle bake slice that the camera otherwise waited for; here arm 0 has no idle
  bake in the hold (idle 0.004 ms per frame against the laptop's 4.65) and the wake delay is already 0.03 ms, so that
  gain cannot appear [L: the idle stage; I: the cause]. On this machine the wide hold is at the cap (10.9 ms ready -
  publish), on the laptop it was server-bound (24.5 ms).
- **The drags gain** [L]: ready - publish p50 -0.93 ms, p90 -1.06 ms, the wait -0.97 ms; the drags' idle stage varies in
  both arms (0.01-7.6 ms per frame by launch: bakes in the moves), so busy has a wide interval (-0.96 [-7.33, +5.40]).
- **Lockstep** [L]: every present exact (kind 0: 4,540-4,705 per launch) but the load's 2 `none`, 41 `noins` and 3-5
  `held`; `older` 0 in all 11. Held rows 0 in every window. Older model taps unchanged: wide 1,834 / 1,837 (+7 [-10,
  +24]), city 630 / 626 (-6 [-42, +30]). Counts per arm-1 launch (`prepare 1s` at the end): prepare passes 4,154-4,344,
  camera hits 4,155-4,346, camera polls 366-442, nothing to prepare 347-424, hold re-polls 18-26, tap yields 163-198.
- **What this replaces** [L]: these are the uncovered-window absolute values that 5.17.1 marked [I] for the laptop; they
  are another machine's (RTX 4080 workstation against the laptop), so they replace the [I] as the measured uncovered case,
  not as the laptop's own numbers. A trap of the run: a `shell_composite.lockstep.csv` left in c5h-new-a's Logs by an
  earlier run (18:40) made the first start refuse every launch (runbook 4.21's trap); it was moved aside as
  `shell_composite.lockstep.before-0713q-193056.csv` and the series started again at 19:31 in a new work folder.

### 5.18 0738: the first 250-500 ms of a fast drag from rest, stock against beta 4 on the laptop [L: the runs; I: marked]

Written 2026-11-25 02:2x EST by the 0738 implementer (account B, branch `worktree-agent-a4914da2236fd996a`). The owner's
report (2026-11-25 00:3x, beta 4 beside stock, no-animations mode): in the first quarter to half second of a fast
left-button drag pan the stock client has a higher frame rate and looks smoother; after that both are smooth.

**Method** [C: the scripts; L: the runs]. `tools/capture/capture_job.py` gained (this ticket) the case `fastdrag`
(`--fast-drags N`, directions alternating; per drag `--fast-rest` 3 s still, then a recording: 0.5 s still, a left drag
from 35 % to 75 % of the client width at 55 % height over 1,000 ms with a posted move every 8 ms (127 moves, p50 gap
8.8-9.2 ms measured), 0.7 s after), `--uncapped` (`cli.py launch --timing`) and `--quick on` (PreGame quick movement
AND quick combat on: the owner's no-animations mode). Every move's QPC time is in the recording's marks
(`t_first_move`; the button goes down 30 ms later). Read with `server/evidence/0738/drag0738.py` (runbook 4.15): per
drag the terrain's step per displayed frame (capcmp's patch phase correlation on the reduced frames), the onset (first
displayed frame after the button with a step over 0.5 px), and from the onset W1 [0, 250), W2 [250, 500), W3 [500,
1000) ms and REST [-500, 0) before the input: new pictures per second, repeated refresh slots (`held`, against 60 Hz),
the longest gap between new pictures, identical pictures presented again (`still`); for the product the composite's
lockstep rows in each window (present interval, E = ins - pub, the wait, the server's pass, ready - publish) and the
`pass long:` lines (TEST ONLY `render_pass_long_ms=8`). Laptop (i7-7700HQ, the 60 Hz panel on the Intel HD 630 output
as dupcap lists it, 3840 x 2160 at 250 %), tb1, EXE build 4390913, TB-HUGE_0350 turn 350 (the load's view of Asyut), the instance's
`GraphicsSettingsDX11.ini` (WaitForVSync = 1), uncapped, quick modes on. Product: beta 4's binaries as installer
0.1b4-std-only ships them (cand6: server.exe 6c75d7fa..., x64 DLL 4c5da20e..., proxy 48524398..., composite
fc01cafa..., the repository's `shell_composite.ini` f0edb1ec... = cand6's, `lockstep_mode=exact`), the owner's
`[Renderer]` set (capture_job's default). Stock: the instance's VP 32-bit DLL. Series 2026-11-25 01:50-02:17 EST,
interleaved stock, product, stock, product, one headed job at a time, 8 drags a job: 16 drags stock, 15 product (one
void, below). Jobs, the void ones and why: `server/evidence/0738/jobs0738.txt`; tables `series0738.txt` /
`series0738.tsv`. Not quiet-locked (the testbed's game-instance place; the laptop ran nothing else in the series).

**Result 1: at the laptop's 60 Hz with vsync, the first 250-500 ms are the same in both arms** [L]. Both arms present
at the 30-fps vsync cadence throughout (stock's own frame on this save is over 16.7 ms, so vsync holds it at 33.3 ms;
REST 29.84 / 29.83 fps). Means over drags, product - stock with the Welch 95 % interval and the smallest difference the
series could detect (mdd):

| window | stock fps | product fps | diff [95 %] (mdd) | stock / product held slots | diff [95 %] | longest gap ms stock / product |
|---|---|---|---|---|---|---|
| W1 0-250 ms | 31.50 | 30.93 | -0.57 [-2.56, +1.43] (2.8) | 7.06 / 7.27 | +0.20 [-0.25, +0.66] | 37.5 / 40.0 |
| W2 250-500 ms | 29.25 | 29.87 | +0.62 [-1.36, +2.59] (2.8) | 7.62 / 7.67 | +0.04 [-0.37, +0.45] | 38.2 / 37.3 |
| W3 500-1000 ms | 29.62 | 29.87 | +0.24 [-0.90, +1.38] (1.6) | 15.38 / 15.27 | -0.11 [-0.75, +0.54] | 40.0 / 39.5 |
| latency, button to onset | 358.9 ms | 369.0 ms | +10.1 [-51.7, +71.9] (87) | | | |

(The 7 held slots in 250 ms are the 30-fps cadence itself: every second 60 Hz slot.) So on this machine the owner's
difference is **not reproduced**: a 30-fps ceiling in both arms hides any gap between a 60-plus and a 30-plus frame
rate. The onset is 280-510 ms after the button in both arms (the EXE's own drag start; the same in stock) [L].

**Result 2: the one product-only pan-start hitch here is the first drag after the load** [L]. Product job 2's first drag:
onset 707 ms after the button (stock's first drags: 427 and 513 ms), W1 20 fps with an 82.5-ms gap, E (the EXE's side,
publish to insertion) 38.6 ms mean, 65 ms max, against 19.6 in later drags; in the 500 ms before it the server's passes
held 162 ms of idle bakes and 95 ms of `units` (long passes topped by idle 11, units 5). Product job 4's first drag:
the harness's 125 posted moves took 3.1 s instead of 1.15 s (the laptop's 4 cores saturated), and the camera moved only
after the recording. Both come about 30 s after the game is in (the job's 20-s settle, the set-up, 3 s still): the
post-load server work (idle bakes, the units' first frames; 0704's subject, 5.8 / 5.15) [L: the stages; I: that it is
the whole cause]. Stock's first drags show nothing of the kind.

**Result 3: what moves inside the product when a pan starts** (14 drags, the first drags left out; ms) [L]:

| | present interval | E (EXE side to insertion) | lockstep wait | server pass (submit - wake) | ready - publish | predraw per long pass | units per long pass |
|---|---|---|---|---|---|---|---|
| REST (500 ms before) | 33.32 | 14.19 | 5.44 | 18.34 | 19.30 | 1.86 | 7.00 |
| W1 | 34.26 | 19.62 | 3.36 | 19.33 | 19.97 | 5.64 | 2.94 |
| W2 | 33.36 | 18.90 | 3.49 | 19.64 | 20.17 | 6.02 | 3.11 |
| W3 | 33.65 | 19.06 | 3.25 | 18.99 | 19.76 | 5.88 | 3.20 |

Paired W1 - REST over the 14 drags: E **+5.43 ms [+4.05, +6.81]**, server pass +0.99 [-0.06, +2.03], wait -2.08
[-2.73, -1.42], present interval +0.94 [-0.37, +2.26]. So when the camera starts moving the EXE's own side of the
product frame grows by about 5.4 ms and stays there for the whole pan (W1 = W2 = W3 within 0.7 ms): a pan cost, not a
start-only cost; the server's pass hardly grows, its predraw (the bakes of tiles entering the view) triples per pass
while its `units` stage shrinks. On the laptop that fits under the 33.3-ms vsync slot (the wait shrinks to absorb it).
Whether stock's EXE grows the same way when panning is NOT measured (stock has no per-present record) [open].

**Reading for the owner's case** [I] (CORRECTED 2026-11-25 by 5.19, measured on the workstation: no drop at the pan's
start; the product is about 115 against stock's 170 new pictures per second in every window and at rest; the guess
below is kept as a trace). On the owner's workstation (RTX 4080, about 165 Hz with vsync, 5.10) stock runs
at the refresh (6 ms frames, 5.15) and the product at about 13 ms (5.15; beta 4 is faster, not measured). A pan that
adds about 5 ms to the product's EXE side at once (Result 3) takes the product from about 75 to about 55 fps in its
first frames, before anything else adapts, while stock stays at 165; that would be seen as "lower fps in the first
fraction of a second" only if the product's frame then recovers, which Result 3 does not show on the laptop (E stays
up). Candidates the laptop could not decide: (a) the EXE-side growth on moving (the model capture's per-present copy and
Place, 0715 / 0719, grow with the instances entering the view; or the EXE's own world work, which stock pays too);
(b) predraw's bakes for the entering tiles (5.6 against 1.9 ms per pass), which at 165 Hz exceed the frame budget on
their own; (c) the lockstep wait (D242) is NOT the cause here: it falls at pan start; (d) the camera window of 0674 engages
on 2-7 frames per drag (`fog 1s` camwin, every overlay moved, 0 failures) at fog cpu 0.34-0.70 ms a frame, and the
`fog` stage per long pass is 0.11 ms at rest against 0.13-0.17 moving: not a cost that matters [L for c and d].

**Not verified / next.** The owner's refresh rate: the laptop's 60 Hz panel and stock's own 30-fps cadence on this
save cannot show a difference above 30 fps. The decisive arm is the same fastdrag series **with vsync off** on the laptop
(`WaitForVSync = 0` in tb1's `GraphicsSettingsDX11.ini`, backed up and restored by SHA-256, both arms) or a quiet
workstation window with 0715's EXE-side probe in both arms (dupcap cannot record a hidden window). Stock's EXE-side
growth on moving. One save and view; a busy laptop is not a quiet-locked place (grade B, not A: two launches per arm).

**Traps found** [L]: (1) the laptop's runner started a second headed job while the first ran, twice (a submit-all
script): two games at 0,0, every recording void; `laptop0738.sh` now submits a job only after the one before has ended.
(2) TaskStop of a background Git Bash call does not end the script it started (runbook 4.16's trap): the stopped script
went on submitting; end it by its pid (`/proc/<pid>/cmdline` in Git Bash lists it). (3) A drag that starts at 15 % of the
client width on TB-HUGE_0350 puts the button down on EUI's left panel, not the ground: the camera never moves; the
default span is 35-75 %. (4) A stock 32-bit game started beside another game in tb2 crashed at its load with VP's 'Game
Crash' box, 0xC0000005 at file offset 0x4CC892 (VA 0x8CD492, RenderBatch::Reset on a NULL batch, addresses.md), Sub2G
free 33 MB: the 32-bit address space nearly exhausted on the huge save [L: the box's text; I: that the NULL batch is a
failed allocation]; closed by `cli.py dismiss --fatal` (owner's close, COMMON 29) through `fatal0738.py`, then repaired.

### 5.19 0748: the same fast drags on the workstation, uncapped, vsync off: no pan-start effect; the product is about 54 fps (32 %) below stock in EVERY window, at rest too [L: the runs; I: marked]

Written 2026-11-25 06:4x EST by the 0748 implementer (impl-0748, account B, branch `worktree-agent-a3b6522f44e764a9e`).
This corrects 5.18's "Reading for the owner's case" [I], which guessed a drop of the product's frame rate when a pan
starts. No such drop happens at about 115 fps.

**Method** [C: the scripts; L: the runs]. This is 0738's drag on a HIDDEN workstation instance. dupcap cannot record a
hidden window, so a camera sampler stands in for it. Scripts are in `server/evidence/0748/` (runbook 4.15, the
workstation variant). `cam0748.py` opens the game process for reading only, through `shell/tools/gt_reader.py` and the
build's local EXE table; no address is printed or stored (D202). It reads view 0's view matrix every 0.53 ms (p50; p99
1.2-1.3 ms) and writes one row with a QPC stamp for each change. The EXE writes the view once per frame while the camera
moves, so one change is one EXE frame with a new camera, i.e. one new picture. In the stock arm the EXE draws it. In the
product arm, under exact lockstep, the composite presents the server's picture of that camera. Check [L]: in the product,
camera changes and the lockstep CSV's presents agree within 3 % per window (W1: 115.6 against 112.7 per second). The
controller block (GVS+0xA8..) changes at another moment of the same frame and is not counted.

`ws0748.py` runs capture_job's set-up Lua in the InGame state (autoplay off, quick movement AND quick combat on,
ClearSelectionList, LookAt) and the same drag as 0738: 35 % to 75 % of the client width at 55 % height over 1,000 ms, 3 s
still before each drag, directions alternating. The posted moves come every 5.4 ms (p50; 200 steps). 0738 used 8 ms, and
at 170 fps that would have capped stock's new pictures at about 115 per second. `an0748.py` makes the tables.
`ws0748.sh` (claim / begin / run / series / finish) did the following:
- a copy of C5H-VP8P-HUGE_0350 under its own name (removed at the end);
- `GraphicsSettingsDX11.ini` set to `WaitForVSync = 0`, backed up and restored by SHA-256;
- `launch --timing`, uncapped, hidden;
- for the product arm: `server/evidence/0600/lay.py` prep from beta 4's cand6 layout (server 6c75d7fa, x64 DLL 4c5da20e,
  proxy 48524398, composite fc01cafa, ini f0edb1ec with `lockstep_mode=exact`), helper on, the owner's options copy
  (`C:/c5-64bit-work/0742/owner-VP64Options.ini`), `lockstep_csv=1`, the TEST ONLY `render_pass_long_ms=8`, and beta 4's
  pack `purity.tsv` beside the x64 DLL. Every product arm logs `purity: loaded (pack=0) ... 140 method keys, 29 Lua names;
  bound to the x64 DLL 4c5da20e`. pack=1 refuses on this instance, because its 32-bit DLL is not the manifest's.

Every launch was restored by hash (`restore ok`, `provision verify` clean). The setup:
- the instance and EXE: c5h-new-b, EXE 26577625, mods548;
- the client: 2554 x 1411, the owner's 2560 x 1440 desktop windowed;
- the turn and the view: T350, the load's view at the capital (101, 25);
- the stock arm: the instance's own VP 32-bit DLL;
- the series: 2026-11-25 05:25-06:39 EST, launches interleaved s1, p2, s3, p4, s5, p6, 10 drags each.

s1 is void: the set-up Lua ran in the wrong state (no quick modes), and a write bug left the sampler without a file;
both are fixed. Valid: stock s3 + s5 (18 later drags), product p2 + p4 + p6 (27 later drags), and the first drag of each
launch reported apart (2 + 3). The owner's Steam game was not running. Each arm ran under its own quietlock take:
- p2's hold was clean;
- s3, p4, s5 and p6 were taken `--overlap`, because account A's heavy jobs (release tests, builds) never let the lock
  become free inside MC-B's window. Those holds are recorded DISTURBED.

So this is a workstation indication (COMMON 23), not grade A. The product launches spread: per-launch W1 of 104 (p2,
the clean hold), 112 (p4) and 130 fps (p6). The busier holds were not the slower ones, so the load is not the main
spread [I].

**Result 1: the difference is a constant frame rate gap, not a pan-start effect** [L]. Later drags, per arm: mean, then
product - stock with the Welch 95 % interval and the smallest detectable difference:

| window | stock fps | product fps | diff [95 %] (mdd) | longest gap ms stock / product | diff [95 %] | mean gap ms | step CV stock / product |
|---|---|---|---|---|---|---|---|
| W1 0-250 ms | 169.1 | 115.6 | **-53.6 [-62.0, -45.1]** (12.0) | 10.1 / 15.5 | +5.4 [+4.1, +6.6] | 5.96 / 9.01 | 0.27 / 0.35 |
| W2 250-500 ms | 166.9 | 116.9 | **-50.0 [-59.7, -40.4]** (13.7) | 10.2 / 15.7 | +5.5 [+4.2, +6.8] | 6.05 / 8.88 | 0.26 / 0.35 |
| W3 500-1000 ms | 170.2 | 112.7 | **-57.6 [-65.4, -49.8]** (11.1) | 10.8 / 16.4 | +5.6 [+4.4, +6.8] | 5.89 / 9.06 | 0.25 / 0.35 |
| latency, button to onset | 282.3 ms | 295.4 ms | +13.1 [-20.6, +46.8] (48) | | | | |

At rest (REST, the 500 ms before the input) the product presents 115.3 per second, the same as moving. The paired
window - REST differences in the product, over 27 drags, are all zero within their intervals:
- E: W1 +0.20 [-0.24, +0.64] ms, W3 +0.42 [-0.01, +0.85];
- the lockstep wait: W1 -0.06 [-0.23, +0.11];
- the server's pass: W1 -0.21 [-0.49, +0.08];
- the present interval: W1 +0.08 [-0.37, +0.53].

So at the workstation's frame rate a fast drag costs the product nothing measurable at its start or later. Stock shows
170 new pictures per second and the product 115, in W1 as in W3. The longest gap between new pictures is 5.4 ms longer in
the product in every window. The camera's step per picture is less even in the product (CV 0.35 against 0.27): with input
every 5.4 ms and frames every 9 ms, a frame carries one or two moves.

The onset is 280-300 ms after the button in both arms; it is the EXE's own drag start. First drags after the load:
- stock: W1 176, W3 169 fps (n = 2);
- product: W1 108, W3 95 fps (n = 3);
- none has 0738's 707-ms onset or a gap over 18 ms. At 30 s after the load on this machine, the post-load hitch of 5.18
  Result 2 did not appear (ticket 0749 covers it).

**Result 2: where the product's 9-ms frame goes** [L]. These are the lockstep CSV's means in the moving windows (W1 /
W3; rest within 0.4 ms):
- present interval 9.02 / 9.06 ms;
- E (the EXE's side, publish to insertion) 5.55 / 5.77;
- the lockstep wait 1.55 / 1.50;
- the server's pass (submit - wake) 6.38 / 6.42;
- ready - publish 6.55 / 6.59.

`split0715.py` (runbook 4.22) over each whole product run, moving presents, p50:
- head 1.1-1.5 ms;
- E 4.1-5.8;
- wait 1.2-1.5;
- tail 0.3-0.4;
- S (the server's frame) 5.6-7.1;
- interval 7.4-9.5;
- the frame waits for the server in 76-80 % of presents.

So under exact lockstep the product's frame is head + max(E, S) + tail (frame-cost-exe-side.md), and S is the longer
branch. That gives about 1.4 + 6.5 + 0.4, about 8-9 ms, against stock's whole 5.9-ms frame. The longest gaps (15-16 ms)
come from server passes over 8 ms: 2-6 `pass long:` lines per 250-ms window. In 92 of 107 such passes in W1 the top
stage is `units` (the units' frame begin, 5.12 / 5.12b / 5.16); then come `draw`, `models` and `predraw`. The units
stage is the top at rest as often as moving: 145 of 146 in REST.

**Reading for the owner's report** [I]. [reworded for privacy] The report that stock feels smoother and faster in the first fraction of a second of a pan is not a
start-only cost on this machine: the gap is the same in W1, W2 and W3. It is most likely seen at the start because the
start is the eye's moment of reference. There the change from a still picture to 170 against 115 new pictures per second
(5.9 against 9-ms steps, with 15-ms holes) is most visible. Once both move, the picture is a blur in both. Not measured:
the owner's own sight of it, and whether a 1000 Hz mouse makes the product's uneven steps (CV 0.35) look worse.

**Fix proposal to the MC** [I; no build or test made here]. Nothing is pan-start-specific to fix. The product's frame
has to come down from about 9 to 6 ms, and the route is the existing frame-pacing work:
- (a) Shorten S. The server's pass is the critical branch in 76-80 % of frames. Its long tails are the `units` stage,
  the units' frame begin:
  - `server/src/units/srv_units_scene.cpp`;
  - `render/src/units/draw.cpp` and `pose.cpp`;
  - the frame-jobs pool of 0711 in `server/src/render/srv_render_host.cpp`: the begin on a worker, faster, at a higher
    priority (5.16's open bar);
  - 0707's cuts U1/U2/U4 (5.12b).
- (b) The head, 1.1-1.5 ms, the previous Present hook's taps and publication:
  - `shell/composite/shell_composite.cpp` `OnPresent`;
  - the model capture in `shell/composite/unit_filter.cpp` (0715 / 0719).
- (c) More overlap of E and S under D242: an earlier camera publish or a pipelined server frame. This is the
  Architect's frame-overlap note `research/07-architecture-decisions/architect-2026-11-23-frame-overlap.md`, a system
  question for the MC.

A grade-A confirmation would be the same series in a held, quiet hour with no `--overlap`: 3 launches per arm,
interleaved, with `ws0748.sh series`.

**Traps found** [L]:
- (1) `tools/harness/lua.py` without `--state InGame` runs where `PreGame` is nil: capture_job's set-up Lua fails at
  its first unguarded PreGame call and nothing after it runs.
- (2) A `quietlock.py run --take` refuses after its 20-minute job wait when other owners keep starting heavy jobs one
  after another. That happened twice here (05:38-05:58 and 06:00-06:20), even inside MC-B's window, because account A's
  jobs kept starting. Ask for a window on both accounts, or take `--overlap` and report the holds as disturbed.
- (3) The GVS camera block and the view matrix change at different moments of one frame: a sampler that counts either
  change counts each frame twice.

### 5.20 0753 (J3): record ahead, the draw stage's end recorded into a command list while the units' job runs [C: the code; L: marked; I: marked]

Written 2026-11-25 from 14:05 EST by the 0753 implementer (account A, branch `task-0753-record-ahead`). Design: the
Architect's route (a), `research/07-architecture-decisions/architect-2026-11-25-frame-gap-route.md` section 5 (rules
A1-A6).

#### 5.20.1 Step 0: the join wait before any build [L: 0748's and 0754's logs; no run of this ticket]

Reader: `server/evidence/0753/step0_0753.py RENDER_LOG ...` (the `units 1s` job numbers weighted by the second's jobs, and
for the `pass long:` lines each stage's mean in the long passes against its mean per drawn frame). Stage split per drawn
frame: `server/evidence/0711/split0711.py`.

**Workstation**, beta 4's cand6 binaries, EXE 26577625, uncapped, vsync off, huge save, 0748's launches p2 / p4 / p6
(`C:/c5-64bit-work/0748/ws/p*/render.log`, 2026-11-25 05:25-06:39 EST; indication grade, COMMON 23):

| launch | drawn frames | units job_ms | mean join wait | inline | predraw | draw (end) | units stage | busy per frame |
|---|---|---|---|---|---|---|---|---|
| p2 | 11,538 | 3.18 | **2.44** ms | 0 | 0.72 | 1.57 (1.56) | 2.58 | 9.34 |
| p4 | 11,224 | 2.85 | **2.17** | 0 | 0.65 | 1.56 (1.55) | 2.32 | 8.73 |
| p6 | 12,767 | 2.40 | **1.83** | 0 | 0.55 | 1.48 (1.47) | 1.94 | 7.45 |

M2's held hour (0754, `frame-cost-exe-side.md` 6.2): join wait 2.17 ms moving / 2.10 at rest. **Laptop**, beta 4, M2's
jobs of 2026-11-25 09:14-10:02 (`shell/evidence/0754/laptop-an.txt`): TB-HUGE_0350 job 5.2-5.3 ms, join wait 3.53-3.57;
TS0438-H-T300 job 8.5-9.0, join wait 6.44-6.77. Both bars of the ticket (workstation >= 1.0 ms, laptop >= 2.0 ms) are met,
so no extra laptop run was made for step 0.

**The long passes** (`pass long:`, 8 ms; the logger keeps at most 20 a second) are not outliers on the workstation: the
mean pass is 7.5-9.3 ms busy, and `busy` counts the post-hand-over idle bake slice, which is not in S. Against the mean
per drawn frame, a long pass is long by +0.4-0.7 ms in `units` (the job is longer and the wait with it: seconds with long
passes have job_ms 2.50 against 2.10 and join wait 1.92 against 1.56 in p6) and +0.5-1.7 ms in `idle`; every other stage
adds 0.0-0.4 [L]. No ticket of their own: the units' wait is what J3 hides, and the idle slice is outside S.

The record-ahead gain is bounded by the recorded stages' CPU cost: the draw stage's end (`draw_end`: the cloud planes with
the particles, and the path) is 1.47-1.56 ms a frame on the workstation and 1.92-1.98 on the laptop, below the join waits
above, so step 1 alone leaves about 0.3-0.9 ms (workstation) and 1.6-4.8 ms (laptop) of wait [I: arithmetic, the recording
costing what the inline calls cost].

#### 5.20.2 Step 1 as built [C: the code at the branch]

- **Where**: `Scene::draw` records right after the draw head's mark, before `unitShadowStep` (whose start joins the units'
  job; without the shadow step the join is before the reflection pass, after the terrain draw, so the window is longer).
  `Scene::recordDrawEnd` records `FowScene::afterWorld` and `Scene::pathDraw` into a deferred context made at `init`
  (`CreateDeferredContext`), `FinishCommandList(FALSE)`; `Scene::drawEnd` at the stage's old place (after the units'
  scene pass and the models' `frameDone`) runs `ExecuteCommandList(list, FALSE)`, or the two calls inline when nothing
  was recorded. Only while the units' job is forked (`m_unitsForked && m_unitsJob.forked()`); otherwise counted as
  `not_forked` and drawn inline (nothing to hide).
- **A1**: the list holds the calls the two stages make today, in their order; nothing else moves. The path's depth copy
  (`pathSaveDepth`) is split: `pathDepthPrepare` (the decision and the copy's texture, CPU) runs at the recording, the
  `CopyResource` stays at its place on the immediate context.
- **A2, the recorded stages' CPU inputs** [C: `srv_fow_scene.cpp`, `srv_render_scene.cpp` at the branch; grep of every
  `m_fow.` call in `srv_render_scene.cpp`]:

| Input | Read by | Written by | Final before the recording? |
|---|---|---|---|
| `m_state`, `m_maskOk`, `m_mod` | afterWorld | `init`, `fail`, `drawWithout`, `admit` (in decide), `beforeWorld` | yes: beforeWorld runs before the record point |
| `m_terrain` (mask, fowAnim), `m_lastIn` (views, plot states), `m_cloudWorld` | afterWorld, drawParticles | `beforeWorld` (831, 832, 867), `releaseDevice` | yes |
| `m_partOn`, `m_partReset`, `m_partW/H`, `m_partSeed`, the particle sim | drawParticles | `init`, `setParticleState` (the player, before draw), drawParticles itself; the step's own worker, joined inside `frame()` | yes |
| the scratch depth (`m_scratchTex`) | afterWorld | `scratchDepth` (the stage itself) | the copy is a command in the list, executed at its place |
| `m_opt.path`, `m_pathR`, `m_pathM`, `m_grid` | pathDraw | init, the load | yes |
| `m_pathChanged`, `m_pathNodes`, `m_pathBits`, `m_pathMouseX/Y`, `m_pathShown` | pathDraw | the feed's path records (the drain, before decide) | yes |
| `m_fow.states()`, `tableWidth/Height` | pathDraw (fog per node) | `readTable` (init, onBatch: the drain) | yes (as 5.16 item 1) |
| `f.nowMs`, `f.cam` | pathDraw | the host (R9), by value | yes |
| `m_pathDepthSaved`, `m_pathDepth` | pathDraw | `pathDepthPrepare`, now at the recording | yes (moved) |
| `m_pathLastMs`, `m_pathClock`, `m_pathInst` | pathDraw | pathDraw itself | - |

  Between the record point and the stage's place no call writes any of these: the only `m_fow` calls there are
  `terrainInputs()` (const, the forest's), and the path's members are written only by the feed and pathDraw. Neither stage
  reads the units' module, so the units' job (R2's owner checks unchanged and fatal under the jitter key) and the recording
  share nothing.
- **A3**: a deferred context starts each list in the default state. The cloud planes set viewport, rasterizer, targets,
  buffers, topology, the null HS/DS/GS, layout, shaders, constant buffers, blend and depth state, samplers and views
  (`render/src/fow/cloud.cpp` 457-555); the particles the same (`particles.cpp` 1027-1066); the path all but HS/DS/GS
  (`render/src/pathmodels/draw.cpp` 277-305), which are null by default in the list and null after the clouds inline.
  `ExecuteCommandList(.., FALSE)` leaves the immediate context in the default state: after the list the host only unbinds
  the targets and the sink copies; every stage of the next pass binds its own (rule A and the render-host suite with
  `render_test_record_ahead_always=1` are the check).
- **A4**: `render_record_ahead = auto | 0 | 1` (`HostConfig::recordAhead`, `SceneOptions::recordAhead`; absent = auto in
  server.exe). auto = on when the frame-jobs pool has workers and `D3D11_FEATURE_THREADING` reports `DriverCommandLists`;
  1 = on whatever the driver says (the runtime then replays the calls at the execute; the TEST arm and machines without
  driver lists); 0 = off. A render setting, not a VP64Options entry (D255). The init line: `record ahead: on|off
  (render_record_ahead=..., ticket 0753): driver command lists yes|no, frame-jobs workers N`.
- **A5**: no query on the deferred context: `FowScene::setRecording` makes the particle draw's timing skip, and
  `FowScene::listTiming` brackets the executed list on the immediate context instead (the fog line's `gpu=` then covers
  the whole list and says `gpu_scope=list`). A list `FinishCommandList` refuses is drawn inline at its place (LOUD, counted
  `finish_failed`); its CPU part then runs twice (the particles step once more, the path clock adds 0). A list whose place
  the draw does not reach (a failed terrain draw, an exception) is released and counted `dropped`.
- **A6**: sceneshot reads `render_record_ahead` from the bundle's `render_config` (absent = 0; the recorder writes the key
  as given) and the TEST variable `C5_SCENESHOT_RECORD_AHEAD` (auto / 0 / 1) replaces it; it prints `sceneshot: record
  ahead: on|off, driver lists N: lists ..., not forked ...` at the end.
- **Lines**: `record ahead 1s: lists= not_forked= record_ms_mean= record_ms_max= exec_ms_mean= exec_ms_max= | total ...`
  and `record ahead totals:` at the end. The recording counts to the `draw` stage (a `StageScope`), and is kept out of
  `draw_mid`, so `draw_end` stays the end's whole cost wherever it ran.
- **TEST keys for the acceptance**: `render_test_d3d_debug=1` (the device with `D3D11_CREATE_DEVICE_DEBUG`; the info
  queue's corruption, error and warning messages as `LOUD: d3d debug:` lines after each pass, `d3d debug totals:` at the
  end; the layer is `d3d11_3SDKLayers.dll`, present on the workstation) and `render_test_record_ahead_always=1` (record on
  every draw, also without a job: the render-host suite has no units). `server/tests/render/renderfx.py` adds
  `C5_RENDER_TEST_KEYS="k=v;k=v"` to every Rig config; `server/evidence/0753/debug_scan_0753.py DIR` reads the logs.

#### 5.20.3 Step 2 (the arc to the models' scene, and the terrain draw): what the code read found before any build [C: the files named]

Not built. The Architect's step 2 records the stretch from the arc to the models' scene as a second list and the terrain
draw as a third. Three things in the code make the second list depend on the third, and the third on more than
`unitShadowStep`'s Options:
- **The trees' Screen Overlay pass reads the terrain draw's LOD of the same frame.** `SceneForest::drawStage` calls
  `terrain.drawScreenOverlay` (`server/src/forest/srv_forest_scene.cpp` 920), which is `Renderer::drawPass(..,
  screenOverlay=true)`: "the Screen Overlay pass takes the state draw() left (no list, no update)" (`render/src/terrain/
  terrain.cpp` 1785-1790: `lodNext = p.lod`, committed at the end of this frame's `draw`). Recorded before the terrain
  draw, the trees would take the previous frame's LOD level: a different picture whenever the LOD moves (a zoom). So the
  forest can be recorded only after the terrain has been recorded, in order (A2).
- **The terrain draw reads its GPU timestamps** (`drawPass`: `p.pollQueries(ctx, false)` = `GetData`, then `Begin` /
  `End` on `ctx`, terrain.cpp 1757-1767): A5 needs the same split as the fog's (`setRecording`, `listTiming`) inside
  `render/src/terrain`, a module outside this ticket's touches. Its Options take the merged unit shadow map
  (`op.shadow.srv`, set by `unitShadowStep` after the join); the Screen Overlay pass binds it too (t3).
- **The models' scene and their reflection pass**: `m_models->beginFrame` runs after the terrain draw today and
  `drawReflection` (inside the water's draw) before `drawScene`; recording `drawScene` ahead inverts that CPU order. The
  module's layers (`server/src/render/landmarks/srv_models_scene.h` `ModelLayer::draw`, cities and landmarks) were not
  read for shared per-frame state; 0709's keep rule (5.14) keeps state across drawn frames.

So step 2 is one ordered recording of terrain -> (water stays inline: the units' reflection needs the job) -> arc,
extras, the path's depth copy, forest, models' scene, with the terrain module's timing split and a prediction of the
merged shadow map whose miss re-runs CPU state (the terrain's LOD update twice). Its gain bound by stage means: the
terrain 0.34-0.43 ms, forest 0.27-0.36, models 0.40-0.53 on the workstation (5.20.1's launches); 0.64-0.69, 0.62-0.67,
0.70-0.99 on the laptop (M2) [L stage means; I the gain].

#### 5.20.4 Step 1's functional results [L: 2026-11-25 15:35-19:02 EST, workstation, loaded by the VP 5.5 autoplays]

Record: `server/evidence/0753/acceptance_0753.txt` (build hashes, every line below). Scripts: `allb_0753.sh`,
`block_0753.sh`, `show_0753.sh`, `debug_control_0753.sh`.
- **Rule A** (C5_SCENESHOT_FRAME_JOBS=1, 16 bundles + 1 case): record ahead on 17/17, on + jitter 17/17, off 17/17, none
  skipped. On single bundles at tolerance 0:0 (GARRISON, FOG): with the key on every frame (8 of 8) is recorded and
  executed, byte-equal to the live dump in every arm; the jitter arm took the worker and the inline path. sceneshot's
  device (with `PREVENT_INTERNAL_THREADING_OPTIMIZATIONS`) reports driver command lists on the RTX 4080 [L].
- **The debug layer**: the render-host suite (243 cases) with record ahead on and recorded on every draw: 7,610 lists, no
  message from record ahead. The suite's only messages, WARNING #408/#410 (the bake pipeline's GPU timer ring drops a
  pending set, `render/src/bake/pipeline.cpp` 416-421) and ERROR #381 (a negative test's missing shared texture), come
  the same with record ahead off (control run) [L]. They are not record ahead's; they are pre-existing and would fail a
  strict "no message" bar for any change, so the bar here is "no message that the off arm lacks".
- **Live** (one hidden launch, c5h-new-a, EXE 26577625, the huge save, capped 30 fps, key 1; not a timing run): about 30
  lists a second, none failed or dropped; recording 1.5 ms (city view) to 2.8 ms (wide hold), the execute 0.03 ms
  (driver command lists: the immediate context's cost of the end falls from about 1.5-2.8 ms to 0.03 ms). The units'
  mean join wait was 0.31 ms in the city view, 0.60 in the drags and 1.34 in the wide hold [L, one launch, loaded
  machine: an indication]. Before (0748's launches, uncapped): 1.8-2.4 ms. So step 1 alone takes the city view to the
  bar (0.3 ms) on the workstation and leaves the wide hold above it: the wide hold's job (4.9 ms) is longer than predraw
  plus the end (about 3.5 ms).

#### 5.20.5 Step 2 as built (2026-11-25 19:1x) [C: the code at 338568597]

- **Three lists, recorded at the one record point in the stages' order** (`Scene::recordAhead`): the terrain list (the
  target's clear and `Renderer::draw`), the middle list (`arcDraw`, `extrasDraw`, `pathSaveDepth` with its decision and
  copy, the forest's `drawStage`), then step 1's end list. Each executes at its stage's place (`ExecuteCommandList(..,
  FALSE)`); the units' own passes (the shadow mask, the reflection inside the water, the scene pass), the models, the
  water and the ice stay inline. The CPU order of the recorded stages among themselves is today's, so the trees' Screen
  Overlay reads the terrain draw's LOD of the same frame (5.20.3) and the end's path draw the middle's depth decision.
- **The Options rule** (`raPredictShadow`): the terrain and the trees are recorded with the shadow map the shadow step
  will hand them, found without its side effects by its own conditions in its order: no unit shadow wanted -> the
  tapped map; the step's `UnitShadow` not made yet (it is made inside the step) -> not predictable, step 2 not recorded
  this draw (`no_prediction`); else `UnitShadow::mergedSrv()` (the merged map `apply()` returns again while the tap's
  description stays; a new accessor in `srv_unit_shadow.h`). After the step `raCheckPrediction` compares; another map
  drops both lists, LOUD and counted (`mispredicted`), and the stages draw inline. Their CPU part then runs twice:
  `exeLodUpdate` is idempotent for one camera (a second call with the same minimum takes no step: `d = 0`), the arc's
  and the path's clocks add 0, the forest's counters count twice.
- **A5 in the terrain module**: `c5r::terrain::Options::gpuTiming` (default true); false = `drawPass` issues and reads
  no query. `Renderer::gpuTimingBegin` / `gpuTimingEnd` (new, `render/src/terrain/terrain.cpp`) take the same ring's
  timestamps around the list's execute on the immediate context. The Screen Overlay pass never timed itself.
- **A failed terrain draw while recording** keeps its partial list (what the failed draw issued), records nothing after
  it, and the draw returns at the terrain's place as inline. A `FinishCommandList` failure of a list draws that stretch
  inline (LOUD, `finish_failed`). An exception while recording discards the partial recording and rethrows.
- **A2 for step 2** [C]: the terrain's inputs are final at the record point (the tile caches after the predraw's bakes,
  the overlay, fog and fog-of-war Options, the field and LOD state); between the record point and the terrain's place
  only the shadow step runs, which changes `op.shadow.srv` (predicted). The middle's: the arc's and the extras' feed
  records (the drain), the fog's plot states (final before decide), the forest's records and the terrain renderer's
  static state and LOD (recorded after the terrain). No recorded stage reads the units' module, the models or the water.
- **TEST key** `render_test_record_ahead_end_only=1` (and sceneshot's `C5_SCENESHOT_RECORD_AHEAD_END_ONLY=1`): step 1
  alone, for the A/B of the two steps. The `record ahead 1s:` line adds `terrain_lists`, `mid_lists`, `no_prediction`,
  `mispredicted`, `end_only`; record_ms is every list of a draw.
- Not recorded: the models' scene pass (its `beginFrame` / `drawReflection` / `drawScene` order and per-frame state were
  not audited, 5.20.3) and everything after the units' join that needs the units.

#### 5.20.6 Steps 1 + 2 on the laptop: one functional smoke [L: 2026-11-25 19:20-19:26 EST, job 20261125-192003-574a]

Not a timing run (one non-quiet job, MC-B #15275). The laptop (Quadro P3000) **reports driver command lists**
(`record ahead: on ... driver command lists yes`), which the Architect's note left open. Layout: beta 4's cand6 `bin/`
with the branch's server.exe (sha256 949794b8..., steps 1+2), the branch's proxy (c58b5488...) and composite
(5e529d8e...), beta 4's pack `purity.tsv` (`purity: loaded (pack=0) ... bound to the x64 DLL 4c5da20e`), TB-HUGE_0350,
capture_job's drags and wide hold, `render_record_ahead=1`. 4,887 end lists, 4,886 terrain and middle lists, 1 draw
without a prediction (the first, before the shadow step made its map), 0 mispredicted, 0 failed, 0 dropped.
`series0753.py` (one launch, arm 1 only):

| window | ready - publish p50 | busy | mean join wait | job_ms | recording (all lists) | execute | units stage |
|---|---|---|---|---|---|---|---|
| wide hold | 17.57 | 21.79 | 0.027 | 9.99 | 9.84 | 0.18 | 0.99 |
| city view | 8.11 | 9.90 | 0.002 | 4.58 | 4.96 | 0.13 | 0.50 |
| drags | 16.43 | 17.87 | 0.388 | 8.55 | 6.76 | 0.23 | 1.33 |

The join wait is gone in the holds: the recording (5-10 ms) is as long as the job. The ABBA series is the measure of
what the frame gains (the recording's own cost on a deferred context against the inline calls is not known from one
arm) [I].

#### 5.20.7 Steps 1 + 2: rule A, the suites, the debug layer, the workstation smoke [L: 2026-11-25 20:05-23:07 EST]

Record: `server/evidence/0753/acceptance_0753.txt`, block 2 (server.exe 36fdbc12..., composite 20e81dd4...).
- **Rule A** with the 17-bundle library (the 0742 overtaking bundle joined it at 20:05): 18/18 with steps 1+2 on, 18/18
  on + jitter, 18/18 off. At 0:0 on GARRISON, FOG_0008 and HUGE_0350 every arm is byte-equal (12 runs, 8 of 8 frames);
  with the key on the terrain and middle lists run on 7 of 8 frames (the first frame has no prediction: the shadow step
  makes its `UnitShadow` in it), 0 mispredicted; the end-only arm records the end only [L].
- **The other suites**: run_all render-host 245, scenebundle-programs 4, scenebundle-py 34, the fow, overlay,
  shadow-ice, water and forest scene suites 3 / 3 / 6 / 29 / 3: all passed, none skipped [L].
- **The debug layer** with steps 1+2 recorded on every draw: 7,356 terrain and middle lists, no new message kind; the
  same pre-existing bake timer-ring warnings (#408/#410, in another bake test this run: which test shows them varies) and
  the negative test's #381 [L].
- **Live** (c5h-new-b, one launch, loaded machine, capped 30, key 1; an indication): mean join wait 0.03 ms (city view),
  0.14 (wide hold), 0.09 (drags), against 0.31 / 1.34 / 0.60 with step 1 alone (5.20.4, another launch) [L]. Recording
  all three lists 3.0 (city) to 5.7 ms (wide), the executes 0.07-0.13 ms. ready - publish in this launch is not
  comparable with step 1's launch (another hour, another load): the ABBA series is the measure.

#### 5.20.8 Steps 1 + 2: the ABBA timing series on both machines: the join wait is gone, the frame gains less than the wait [L: 2026-11-26 06:00-07:10 EST]

Written by impl-0753b (account B). Record: `server/evidence/0753/acceptance_0753.txt` (timing series section: builds,
load, every number below); tables `series_{ws,laptop}_20261126.txt` (`series0753.py`) and `stages_{ws,laptop}_20261126.txt`
(`stages0753.py`, new: every `pass 1s` stage per drawn frame by window and arm). One build per machine, the arm only the
TEST line `render_record_ahead=0|1`; VP 5.4.8 pins (MC-B: no relink for the series). **Workstation**: 5 ABBA pairs in
MC-B's quiet window (06:18-07:09, quietlock held per launch, total CPU mean 27-38 %, one launch 50 %), c5h-new-a, EXE
26577625, the pins-0.1b x64 DLL **without a purity list** (every call crosses; both arms), the huge save, frame cap 30,
0713's drive. **Laptop**: 6 ABBA pairs, quiet jobs (grade A), beta 4's bin/ with the branch's server.exe and the pack's
`purity.tsv` (`purity: loaded` in all 12), TB-HUGE_0350, capture_job's drags and holds, frame cap 30; a Windows Defender
Firewall alert was on screen and foreground through the whole series (the same for every job). Record ahead in the 11
arm-1 runs: 0 finish_failed, 0 dropped, 0 mispredicted.

| machine | window | units join wait off -> on | ready - publish p50 diff [95 %] | busy diff [95 %] | units job_ms diff |
|---|---|---|---|---|---|
| workstation | wide hold | 3.74 -> **0.18** ms | **-2.60** [-3.03, -2.18] (off 12.19) | -2.74 [-3.23, -2.25] | -0.09 [-0.36, 0.19] |
| workstation | city view | 1.29 -> **0.04** | **-0.77** [-1.05, -0.49] (off 4.80) | -0.86 [-1.06, -0.66] | +0.01 |
| workstation | drags | 1.55 -> 0.06 | -0.86 [-1.46, -0.25] (off 5.72) | +0.08 [-1.36, 1.52] | -0.14 |
| laptop | wide hold | 6.26 -> 0.03 | **-2.21** [-3.03, -1.39] (off 19.55) | -2.21 [-2.59, -1.82] | **+1.47** [1.10, 1.84] |
| laptop | city view | 2.58 -> **0.005** | -0.66 [-1.35, +0.02] (off 8.10) | -0.20 [-1.18, 0.78] | +0.74 [0.05, 1.42] |
| laptop | drags | 4.59 -> 0.45 | -1.15 [-2.36, +0.06] (off 18.94) | -1.02 [-2.93, 0.89] | +0.66 [-0.03, 1.36] |

- **The join-wait bar is met**: at or under 0.3 ms in the city view on both machines (0.036 / 0.005) and in the
  workstation's wide hold (0.18); the laptop's wide hold 0.03 [L].
- **The ready - publish bar is not met anywhere**: the frame falls by less than the off arm's join wait (workstation wide
  2.60 of 3.74, city 0.77 of 1.29; laptop wide 2.21 of 6.26), and on the laptop the city view and the drags do not
  separate from 0. Real gains with intervals excluding 0: the workstation in all three windows, the laptop's wide hold [L].
- **Why, from the stages** [L: stages0753; I: the attribution]: the `units` stage falls by about the join wait
  (workstation wide -3.45, laptop wide -5.92), but the three recorded stages (terrain, forest, draw: recorded plus
  executed) cost more than the same calls on the immediate context: workstation wide 3.87 -> 4.56 ms (x1.18), laptop wide
  6.43 -> 9.82 (x1.53), laptop city 3.05 -> 4.71 (x1.54); the handover adds 0.02 (workstation) and 0.16-0.31 ms
  (laptop). On the laptop the units job itself runs 0.7-1.5 ms (8-20 %) longer while the render thread records beside
  it (2 frame-jobs workers; the workstation's job is unchanged), so in the wide hold the recording (9.9 ms) and the job
  (10.0) end together. The frame's gain is then about join wait + inline cost of the recorded stages - recording cost -
  executes - remaining wait: workstation wide 3.74 + 3.87 - 4.56 - 0.08 - 0.18 = 2.79 against 2.74 measured.
- So the Architect's bar (gain = the off arm's wait) holds only where recording costs what the inline calls cost; it
  costs 18 % more on the RTX 4080 workstation and about 50 % more on the laptop's Quadro P3000 (both report driver
  command lists). Not measured: step 1 alone (the end list, TEST key `render_test_record_ahead_end_only=1`) against
  both steps on the laptop, which would say whether the terrain list (the stage with the largest recording overhead:
  laptop wide 1.17 -> 2.16 ms) costs more than it hides there [I].
- Not run, owed (MC-B): 0748's `ws0748.sh series` again and 5043's laptop idle window.

#### 5.20.9 The merge request phase: main's VP 5.5 pins and 0788 / 0789 merged, every builder again [L: 2026-11-26 from 11:46 EST, workstation]

Written by impl-0753c (account B, MC-B #17392). main bdfc22dbb (the VP 5.5 pins of 0765; 0788's held draws, 0789's model
store, 0782, 0783, 0784, 5074) merged into a1b00d9f1 as 3a219d76d: only the ticket conflicted. 0789's hunks in
`srv_render_host.cpp` (the store's keys and `render_models_store` in the bundle's render_config), `srv_render_scene.cpp`
(the store's log lines) and `sceneshot.cpp` (the store's key from the bundle) merged without a conflict and touch nothing
record ahead records: the models' scene, where the store and 0788's held records are drawn, stays inline (5.20.5,
"Not recorded") [C: the merged files]. Builds on `C:/c5-64bit-work/keep/pins-5.5` (`allb_0753.sh`, the pins now its
default; runner `server/evidence/0753/mr_0753.sh`, outputs `F:/c5-64bit-work/0753/mr55/`): server.exe sha256 b7804f4c...
with the proxy 8d57f0c7... (`build_server: OK in 116 s`), the units builders and their runs (units 7 passed, unit extras
14 of 14, unitfeed, trigger scene, farm fields, unit shadow) and sceneshot 615ba679... with `--smoke`: all OK
(11:51-12:01) [L]. The suites (`run_all.py --only render-host --only scenebundle-programs --only scenebundle-py`, 12:01-12:53,
the workstation loaded by four other owners' builds and suites): render-host **247 passed** (821 s); scenebundle-py 34
passed, 1 failed: `test_recorder_host.py::test_bundle_from_the_host`, the host's first draw came 8.5 s after its start and
the 5 s run ended with `draws 0` ("the span never started"), a load failure: alone at 12:55 (`rerun_0753.sh py ...`) it
passed in 7 s [L]; scenebundle-programs: bundle_format_test 1260, recorder_test 268 and sources_test 24 passed,
`sceneshot_test` ran out its 1,500 s timeout (in block 2, 2026-11-25, the whole suite took 849 s) [L]; it is rerun alone
with a 3,600 s timeout after the workstation's quiet hour (`rerun_0753.sh sceneshot_test`), with the ASan render-host
run (`mr_0753.sh asan`) and rule A. *(13:08: that rerun was refused by the harness's permission classifier; it waits for
MC-B, no other form tried.)*
- **Rule A on the merged head** (MC-B's go, 13:11-14:01, `mr_0753.sh raon raj raoff`, the C: library's 17 bundles + 1
  case): 18/18 with steps 1+2 on, 18/18 on + jitter, 18/18 off, none skipped. At 0:0 on GARRISON, FOG_0008 and HUGE_0350
  (`show_0753.sh`, 14:03-14:22) all 12 runs are byte-equal 8 of 8 in colour and depth; the on arms run the terrain and
  middle lists on 7 of 8 frames (the first has no prediction), end-only the end list alone, 0 mispredicted, 0 failed,
  0 dropped: the same as 5.20.7 on the 5.4.8 tree, so 0788's held draws and 0789's store (drawn inline in the models'
  scene) do not meet the recorded stages [L].
- **ASan** (`run_all.py --asan --only render-host`, 14:22-14:48): 247 passed, none skipped, no ASan report [L].

## 6. The reporter's own session logs of 2026-11-22 (ticket 0687, P0 of epic 0686) [L: the logs; C: the code read]

Written 2026-11-23 01:10 EST by worker-0673h under lead-0673 (branch `claude/lead-0673-holistic-d0fd9d`). This is the
result of P0 of `research/07-architecture-decisions/architect-2026-11-22-frame-pacing-epic.md` section 4; it sits here,
beside the laptop's numbers it is compared with, because the Architect's two files have no results section of their own
(both now point here). Read only, no build, no game: the patch's logs the reporter's Steam install wrote into
`%USERPROFILE%\Documents\My Games\Sid Meier's Civilization 5\Logs\` (the composite's `shell_composite*.log`, one per EXE
process, joined to a run by the EXE's pid) and `...\Logs\VP64\run\<run>\` (`render.log`, `server.log`,
`vpproxy-snapshots.tsv`). Working copies outside git in `C:/c5-64bit-work/0673/0687/`; nothing from them is in git.
Reader: `server/evidence/0687/p0read.py DIR [--runs 20261122] [--csv OUT]` (per run and per second; the per-second CSV
has 38 columns, the fog, terrain and forest costs among them).

### 6.1 What these logs can and cannot say

- **Per second only.** `lockstep_csv=0` and `lockstep_check=0` in both builds, so there are no per-frame rows: **no
  ages** (age 1 against 2+), no per-frame wait, no camera compare. "Older" here is the `lockstep 1s:` count; a
  freeze shorter than a second shows only as `submit_max_ms` (the longest server pass of that second) or the EXE's
  `ft_max`.
- **The machine:** the workstation (`adapter 0: NVIDIA GeForce RTX 4080`, render.log), the reporter's
  `GraphicsSettingsDX11.ini` `WaitForVSync = 1`, `FOWLevel = 3`, windowed, back buffer 2554 x 1411; the EXE presents
  165-167 times a second at ft_mean 6.03-6.06 ms with ft_p99 about 7 ms when nothing else holds it, so a 165 Hz
  display with vsync [I: the panel's rate is not logged]. **The server's budget per frame is therefore about 6 ms, not
  the 33 ms of the laptop's capped runs.** No load record (M8): what else ran on the workstation then is not in the
  logs.
- **Camera moving** = a second whose server `near 1s:` `cam_moving_frames` grew; "world seconds" = seconds with
  UpdateViews publishes (`pub_uv` > 0: the world view, no load or front end).

### 6.2 The runs

| run | build | bound us | min | world s / moving s | older % all / moving | wait_off % (give-up state) | give-ups; s in give-up; longest | waits; mean / max us | EXE presents/s p50 | server published/s p50 (moving) | cameras skipped | submit_ms p50 / p99 | submit_max_ms p50 / p99 / max | units frame_ms_max p50 / p99 / max | host 1s gaps > 1.5 s | slow writes |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| `20261122-144652-9352-1` | 0.1b2 (33c3fcb44) | 3400 | 5.4 | 229 / 75 | **99.4 / 98.0** | 98.3 | 104; **212 s**; **108.9 s** (14:49:52) | 1,135; 1,705 / 3,206 | 167 | 151 (123) | 31,945 | 3.52 / 11.44 | 5.2 / 15.0 / 355 | 0.7 / 2.4 / 235 | 1 (load) | 0 |
| `20261122-151219-29608-1` | 0.1b2 | 3400 | 7.8 | 315 / 140 | 46.7 / 24.3 | 43.0 | 96; 99 s; 13.8 s (15:15:07) | 33,093; 1,370 / 3,527 | 165 | 165 (165) | 9,090 | 2.52 / 10.54 | 3.6 / 19.5 / 401 | 0.7 / 8.1 / 212 | 1 (load) | 0 |
| `20261122-152245-13668-1` | 0.1b2 | 3400 | 4.2 | 158 / 49 | 54.8 / 48.0 | 26.5 | 138; 27 s; 3.5 s | 6,031; 765 / 3,161 | 41 (p5 10) | 54 (138) | 808 | 9.00 / 13.36 | 12.5 / 221 / 378 | 4.1 / 65 / 225 | 1 (load) | 0 |
| `20261122-182306-41156-1` | 0.1b2, a second game folder (not the Steam one) | 3400 | 0.6 | 0 | - | - | - | - | - | - | - | - | - | - | 0 | 0 |
| `20261122-205204-11656-1` | main 0d3114c03 + dev switch | 8000 | 1.1 | 0 | - | - | - | - | - | - | - | - | - | - | 0 | 0 |
| `20261122-205902-2956-1` | main 0d3114c03 + dev switch | 8000 | 8.8 | 380 / 132 | 46.9 / **65.3** | 37.5 (moving 55.6) | 116; 106 s; 6.9 s (21:04:25) | 16,449; 2,220 / 6,727 | 103 (p5 16) | 80 (**76** against 128 presents) | 6,583 | **8.05** / 15.58 | 12.0 / 35.7 / 451 | 2.0 / 16.9 / 273 | 2 (load) | 1 (load) |

Builds from `server.log` `build:` (repo 33c3fcb44 built 2026-11-22 12:31 = the 0.1b2 installer; 0d3114c03 built 20:11).
Runs 182306 and 205204 ended at the load with `server-fatal.txt` "engine-exception ... kRtE_StreamRead threw a C++
exception" (no world seconds; a crash, not a pacing matter: noted here for whoever owns load crashes). Neither build
carries 0674's fix (470981026 is not an ancestor of 0d3114c03; the fog lines have no `camwin=`), so gray flashes on zoom
are expected in all six. Run 152245 presented only 41 a second at the median (p5 10): turns were being processed there
(the proxy's run snapshot: 1,722 calls a frame, frame p99 167 ms); its EXE-side slowness is not the world's pacing.

**Worst seconds** (time of the composite line, EST):
- 144652 14:48:33-14:48:38: every frame older, in the give-up state (159-166 `wait_off` a second), the server at 146-153
  of 165 frames; 14:52:06-14:52:18: older 142-166 of 151-166, the server's frame 10.0-11.7 ms, 81-93 published a second.
- 151219 15:18:37-15:18:43: older 164-166 of 164-166, `submit_ms` 10.3-10.7, 91-96 published against 165 presents.
- 205902 21:03:41-21:03:52: older 100-139 of 100-139 (all), `submit_ms` 12.8-14.8, 65-68 published against 100-139
  presents; 21:04:26 `submit_ms` 15.1, 64 published, 155 presents.
- The server's longest passes (all 15-21 s after a load's end, `units: load setup ... ended`): 14:48:46 355 ms (units
  234 ms, 100 bakes), 15:14:51 401 ms (117 bakes), 15:15:04 326 ms (units 212 ms), 15:24:07 378 ms (228 bakes),
  15:24:21 350 ms (units 225 ms, camera moving), 21:01:37 451 ms (`units: slow frame ... (drawn frame 1): 273.4 ms =
  ... the controller 79.4 ... prepare 192.0`). Outside these, no second of play had a server pass over 100 ms, and only
  one moving second per run over 50 ms.
- Gaps between the server's `host 1s:` lines over 1.5 s: only at loads (5.7, 3.8, 2.8, 14.2 and 6.7 s). The one slow
  log write: 21:00:27 `render log: the line before took 13151.5 ms to write (... render.log 13151.5 ms ...)`, during
  the load (no world seconds yet); the composite's own log stopped for 12.8 s at the same time (`stats el=12818ms`), so
  the disk under `Documents\My Games` stalled for both processes [I]. Cause row 1 exists on this disk, but not in play.
- The EXE's own long frames: 205902 21:04:57-21:05:45 6-46 presents a second, `ft_max` 100-500 ms, while turns 5-23
  went by (`feed: rolling refresh in turn N` every 2 s in server.log): turn processing, the whole picture slow, the
  interface included. 2-38 moving seconds per run had an EXE frame of 100 ms or more; these logs do not say why, and
  whether stock has them on the same turns is not known (P2/P3).

### 6.3 The cause the logs name: the server's steady frame against a 6 ms EXE frame (the study's row 6), with the fog particles as the stage

- **The share of older frames follows the server's frame time, and the server's frame time follows the fog's
  particles.** Per second, `submit_ms` (`Scene::draw` to the sink's end) against `fog 1s` `cpu` (the render thread's
  time in one particle frame, mean over the second): correlation 0.95 / 0.98 / 0.67 / 0.89 in the four runs with world
  seconds, slope 1.2-1.3 ms of draw per ms of particles (2.1 in 152245). Example 205902: seconds with particles under
  1 ms: draw p50 4.2 ms, older 9 %; particles 6-9 ms: draw 14.2 ms, 67 published against 135 presents, older 99 %.
  151219: particles under 1 ms: draw 2.5 ms, all 165 cameras drawn; particles 6-9 ms: draw 10.3 ms, 95 published, older
  99 %. The remaining draw (draw minus particles) is 1.9-5.6 ms (7.2-7.5 in 152245, where turns ran); terrain CPU 0.4-1.1 ms, forest 0.4-1.0 ms, units 0.6-2.5 ms
  (per-second maximum) in the same seconds, so the units are not the stage here (unlike the laptop's huge turn-350 save,
  section 5.5).
- **When the particles are expensive:** the worst seconds draw 5,600-6,800 particle sprites (`particles=` first
  field) and 775-800 cloud plane quads with 360-490 terrain tiles visible: a wide view over unexplored land. Sprites
  alone do not set the cost: seconds with about 5,900 sprites cost 2.5-3.5 ms in some spans and 6-9 ms in others [I:
  the split is not logged]. What the call holds [C, `render/src/fow/particles.cpp` `ParticleRenderer::frame`, 743-823 at 04f32cd42]: it first
  **joins the previous frame's step on the particle thread** (`p->join()`), then emits every view's sprites, sorts them
  back to front, maps and uploads the vertex buffer and draws, then launches the next step. The step was moved off the
  render thread on 2026-11-16 (`fog-of-war.md` 10.3: 0.57 ms at about 1,200 sprites at the city camera, 30 turns), but
  the join makes the render thread wait whenever a step lasts longer than the time to the next frame: about 6 ms at
  165 Hz. Whether the 5-9 ms is the join's wait or the emit and sort of 6,000 sprites is the first thing P1 / 0688 must
  split [I]. *Split 2026-11-23 by ticket 0692 step 1 on recorded bundles (`fog-of-war.md` 12): it is the join's wait for
  the step (huge map, 6,179 sprites: step 9.8 ms, about 1.4 ms per 1,000 moved particles; wait 6.3 ms with 3 ms of other
  work between calls, 3.9 ms with 5 ms), not the emit (0.6 ms) or the sort (1.1 ms) [L, bench on a loaded workstation].*
- **Why that is a stutter at 165 Hz and not on the laptop:** one server draw per camera event, the newest camera only
  (`skipped_cams` 6,583-31,945 a run). With a draw of 10-15 ms and an EXE frame of 6 ms the server finishes every second
  or third camera; the world picture changes 65-95 times a second while the EXE's interface (flags, banners, resource
  icons, unit flags, the cursor) moves 135-165 times; each world picture is shown for 2-3 presents and then jumps 2-3
  camera steps. That is the slipping of the interface against the terrain on arrow-key pans and drags. On the laptop's
  30 fps cap the same frame time is under one EXE frame and gives an age-1 world, not repeats.
- **The give-up state makes it permanent at 3,400 us.** After 8 misses in a row the composite stops waiting and takes
  the newest older picture; it recovers only when this frame's picture is ready at the insertion without a wait
  (`shell/composite/shell_composite.cpp` 2935-2942 at 04f32cd42 [C]). With the server's frame longer than the EXE's publish-to-insertion time that
  almost never happens: 144652 spent 212 of its 229 world seconds in the give-up state, one episode 108.9 s; 98 % of
  its frames were older and at least 41 % of its presents repeated a server picture (presents minus server frames
  published). With 8,000 us (205902) the waits came back (16,449, mean 2.2 ms, max 6.7 ms) but the draw was over the
  bound in the moving seconds (`submit_ms` p50 11.6 ms there), so 56 % of the moving frames were still in the give-up
  state and 65 % older; and the waits cost the EXE frame rate (103 presents a second at the median against 165 in the
  0.1b2 runs) [I: other differences between the runs (save, view, turn) are not controlled].
- **Rows not supported by these logs:** row 1 (log writes) only at a load (one 13.2 s write, above), none in play;
  row 2 / row 7 only as one 326-451 ms pass 15-21 s after each load (first unit frame `prepare 192.0` ms, or 117-228
  bakes in one second), not a run of frames; rows 4, 5 and 8 cannot be seen in per-second lines (no wake or budget
  field), but the steady seconds need none of them; row 3 (camera-driven pre-draw work) is not separated from the draw
  by these lines (`predraw_max_ms` is a run maximum). The study's note on row 6 ("no: this gives a world one frame
  behind, not 3-5 per second") holds at 30 fps; at the reporter's 165 Hz it is the steady cause. The "3-5 terrain
  updates a second" itself is not in these per-second numbers: the lowest moving second published 64-95 server
  frames; a run of several 100 ms frames would need the per-frame CSV or P1's freeze counter [I: the eye may read 2-3
  repeats followed by a 2-3-step jump as a much lower rate; not shown].

### 6.4 What this says about 0677's open question

The open question (section 4): is the interface placed with frame N-1's camera, or is the world shown late by older
frames? These logs have no camera compare, but they show the second mechanism in most of the reporter's moving
frames: 24-98 % older per run (65 % with the 8,000 us build), mostly in the give-up state with no wait at all, and the
server publishing a new world 76-165 times a second against 128-165 presents. Nothing in them needs the first
mechanism; the laptop's stock capture (5.2: stock k = 0 in 24 of 24 recordings) already says stock places the
interface with its own world. **So the reporter's slipping is the world shown 1-3 presents late and repeated, as on the
laptop, only more often** [I: per second, no per-frame ages]. For the long freeze of rec2 2:29 (terrain still about 5
recorded frames, about 210 ms, while units and flags move, no units left behind), the one event in these logs of that
length in play is the server's first unit frame 15-21 s after a load (212-273 ms units, 326-451 ms passes; 15:24:21 with
the camera moving): while the server's units are not ready the EXE still draws its own units, which move with the
interface over a world that stands still [I: the recording's clock is not joined to the logs; the run of the
recording is not known].

### 6.5 Against the laptop (5.2, 5.5)

| | laptop, huge T350, 30 fps cap, bound 8,000 us | reporter, workstation, vsync about 165 Hz, bound 3,400 / 8,000 us |
|---|---|---|
| older | 86 % (ages 1: 4,403, 2: 33, 3+: 34) | 47-99 % all, 24-98 % moving (8,000 us: 65 % moving); ages not logged |
| EXE frame | 33.3 ms | 6.0 ms (103-167 presents a second at the median) |
| server draw (`submit_ms` p50) | 12.19 ms | 2.5-9.0 ms per run; 10-15 ms in the worst spans |
| the stage | units 7.8 ms (733 listed, 704 culled) | fog particles 5-9 ms in the worst spans; units 0.6-2.5 ms |
| give-up state | (the wait ran out the bound: insertion - publish p50 8.53 ms) | 27-212 s per run, longest 108.9 s |
| result for the eye | world one frame late, single-frame freezes | world repeated 2-3 presents, then a 2-3-step jump |

### 6.6 Not verified

- Per-second lines only; ages, per-frame freezes and the camera at Present were not logged in these builds.
- One reporter session on a shared workstation without a load record; the saves, the views and the turns differ
  between runs, so the 3,400 against 8,000 us comparison is not controlled.
- The split of the fog particle call (the join's wait against emit, sort and upload) is inferred from the code, not
  measured. *(Measured offline 2026-11-23, ticket 0692: `fog-of-war.md` 12; the live split still needs 0688's timer.)* A particles-off arm on the same view (`C5_RENDER_FOW_PARTICLES=0`, test only) or P1's stage timer would
  settle it.
- The EXE's long frames in play (100-500 ms) are not explained by these logs; stock's frames on the same turns were
  not recorded.
- That rec2 2:29 was the post-load first unit frame is an inference without a time join.
