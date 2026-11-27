# The residency's first draw after a load: catch-up of the visible set (ticket 0409, 2026-11-16)

Ticket 0409 fixes cause 2 of ticket 0404 (`terrain-surface-verification.md` 14): on the huge save the server's first
frames after a load showed 58-74 visible land tiles unbaked (flat in the material fallback colour) for 5-19 frames,
because `c5r::bake::Residency::beforeDraw` baked at most `burst` = 16 visible tiles a frame (and at most
`render_bake_budget_ms` = 4 ms of CPU), while nothing was resident at the load's first draw.

## 1. The rule as built [C]

`ResidencyOptions::catchUpVisible` (`render/include/c5r/bake/residency.h`; default **on**; server.exe has no option and
takes the default; the test server `shell_server.exe` takes `--bake-catch-up 0|1` since ticket 0499, 2026-11-17, see
section 3.1):

- Before the draw, count the visible land tiles that have **no bake at all** and can be baked (`needsBake`: land, in
  the radius, not failed at this generation). If that count is **above the frame's burst** (`burst` minus the frame's
  near-pair up-res attempts, 0332), every one of them is baked now, nearest first, with **no burst and no
  `budgetMs` limit**. This is a load's first draw (nothing resident) and any jump to a part of the map the bake-ahead
  has not reached (radius 0 / 5 after a jump, or a jump during the bake-ahead).
- Then, in the same catch-up, the land tiles **at the view's edge**: tiles outside the residency's visible set, without
  a bake, that `c5r::terrain::Renderer` may still draw (section 2). They are found with the same x / y box plus one
  sample of margin and z over the whole byte range (-1 to 255 * sz + 1), nearest first.
- Stale tiles (an older bake is drawn, no fallback colour) are not part of the catch-up; they keep the burst, and the
  ordinary loop after the catch-up gets no budget left in that frame (it has baked more than the burst).
- With as many tiles without a bake as the burst or fewer (an ordinary pan, the frames after the catch-up) the burst
  and `budgetMs` apply as before 0409.
- `catchUpVisible = false` is the behaviour before 0409 (the control arm of the tests).

Counters (`ResidencyStats`, appended to `statsLine`): `catch_up=<switch> catch_up_draws catch_up_bakes
catch_up_edge_bakes catch_up_max_tiles catch_up_max_ms first_draw_unbaked first_draw_bakes first_draw_ms`.

Log lines (the server's `render.log`, through the residency's log):
- every load's first draw, catch-up or not:
  `residency: first draw of load N (frame F): V visible land tiles, U without a bake; B baked before the draw (E at the
  view's edge) in T ms (catch-up: every one | within the burst | burst, catch-up off); R still without a bake`;
- the first 16 other catch-up draws of the process: `residency: catch-up draw (frame F): U visible land tiles without a
  bake > burst K; B baked before the draw (E at the view's edge) in T ms; R still without a bake`.

## 2. Why the view's edge: the renderer culls with wider z bounds than the residency [C, L test]

The residency's cull (`Impl::setView`) takes a tile's z bounds from the high bytes of its own samples (tile + 1 sample)
times sz. The renderer's cull (`render/src/terrain/terrain.cpp`, the draw loop) uses the same x / y box but z bounds
taken in `setHeightField` over **every LOD level 0..10 of the tile's height page** with 3 texels of margin; the coarse
levels are 2 x 2 box averages with a 3 x 3 sharpen (`c5r/bake/heightpage.h`), and level 10 is one texel for the whole
1024-sample page. So the renderer's boxes are taller and it draws (and counts in `FrameStats::landTilesWithoutCache`,
the count the server publishes per frame as the frame's unbaked tiles, `VPFrameSlot::tilesUnbaked`) tiles beyond the
residency's visible set. In the unit test after a jump across the 16 x 12 test map (`view(12, 8)`, camera 180 high),
the residency's visible set was 24 land tiles; after baking all of them the renderer still drew **3 land tiles without
a cache**. With the edge pass (z over the whole byte range, a superset of the renderer's bounds) it draws 0
(2026-11-16, `catch_up_after_a_jump_and_after_a_new_field`).

This also explains 0404's tail of 1, 1 unbaked tiles in run2 after the visible set was baked [I]: the bake-ahead's ring 1
reached those edge tiles a frame or two later.

## 3. Tests [C]

`render/tests/bakeresidency/residency_test.cpp` (render suite; plain clang-cl Release and MSVC ASan):

| test | what it shows |
|---|---|
| `catch_up_first_draw_bakes_every_visible_tile` | first draw at a high camera: 165 visible land tiles (>= 74), all baked before the draw with burst 16 and a budget of 4 ms; 0 gray frames; the renderer's own count of land tiles drawn without a cache is 0; the log line; the stats line; the next frame bakes nothing and is not a catch-up |
| `catch_up_off_keeps_the_burst_at_the_first_draw` | the switch off (control arm, behaviour 0404 measured): 16 bakes, 1 gray frame, the renderer draws visibleLand - 16 land tiles without a cache |
| `catch_up_after_a_jump_and_after_a_new_field` | a jump to the other side of the map with burst 3: a second catch-up, the edge tiles baked (`catch_up_edge_bakes` > 0), the renderer's count 0; a new field (load 2) logs its first draw again |
| `catch_up_keeps_the_burst_for_a_pan` | radius 0, bake-ahead done, ten stale tiles under the camera, a pan of one tile (5 new tiles without a bake, burst 8): no catch-up, exactly 8 bakes before the draw |
| `visible_first_then_rings_and_gray_counter` | unchanged test of the gray counter, now with `catchUpVisible = false` (it needs gray frames) |
| `slow_bake_is_logged_and_counted` | a stand-in bake of 520 ms is logged (`residency: slow bake: tile (...)`) and counted (`slow_bakes=1`); a fast one is not |
| `radius_zero_evicts_after_a_move` | unchanged in what it checks; its second draw gets 5 s of budget instead of 50 ms: under ASan on the loaded workstation the 24 bakes of the new view took more than 50 ms (failed twice on 2026-11-16, also in 0393's rerun 1, `terrain-decals.md` 20.6), a timing check the test did not mean to make |

Results 2026-11-16 (commit 4434a622): plain (clang-cl Release) 37 tests passed, 0 skipped; MSVC ASan 37 passed, 0
skipped, 0 ASan reports (peak 937 MB under the 6,144 MB cap). The server's scenebundle tests
(`build_scenebundle_tests.py --run`: bundle_format 527, recorder 159, sceneshot 163, sources 24 with the settle cases)
passed against this residency.

Stand-in baker timings in the test (not the pipeline baker): 165 bakes in 68-243 ms on the loaded workstation.

### 3.1 The test server's switch `--bake-catch-up 0|1` (ticket 0499, 2026-11-17) [C, L test]

0409 turned the catch-up on without touching `shell/tests/test_residency.py`, whose
`test_gray_frames_counted_when_budget_too_small` (`--bake-burst 1 --bake-rate 1`) expects gray frames: since 0409 the
first draw bakes the whole visible set and the test failed every run on main (0494, 2026-11-17: 12 of 12, idle and
loaded). Ticket 0499 gives `shell_server.exe` the switch `--bake-catch-up 0|1` (`ResidencyOptions::catchUpVisible`;
default 1, so a command line without it behaves as before; any other value exits 2 with `bad value for --bake-catch-up`)
and adds `catch up S catch up draws N catch up bakes N` to the end of the server's `residency totals:` line, which the tests
parse (the per-second `residency 1s:` line carries them too, as `catch_up_*=`, but reaches the log's 1,024-byte buffer:
on 2026-11-17 it was 1,023 characters and its last field, `tilegen_err`, was cut).

- `test_gray_frames_counted_when_budget_too_small` passes `--bake-catch-up 0` (the control arm, behaviour before 0409)
  and checks that the totals line says `catch up 0`, 0 catch-up draws and bakes.
- `test_catch_up_leaves_no_gray_frames_with_a_small_budget` (new): the same burst 1 and rate 1 with the default: `catch
  up 1`, catch-up draws > 0, catch-up bakes > 1, 0 gray frames and 0 gray tile frames over more than 10 frames: the
  measure of 0409's acceptance in the test server.
- `test_bad_command_line`: `--bake-catch-up` 2, -1, `yes` and 0.5 each exit 2 with the reason and start nothing.

Results 2026-11-17 13:18-13:26 EST, workstation (account B's work beside it), branch `task-0499-bake-catch-up-switch`
(main 048a00e63 + the change), plain `shell/build.cmd`: `shell/tests/test_residency.py` in full 3 times, 42 passed, 0
skipped, 0 failed each (154.7 s, 150.2 s, 151.4 s). One more run of the two tests with their logs kept, both over 95 frames
of 3 s with the suite's camera over tile (16, 10): catch-up off `gray frames 19 gray tile frames 190 catch up 0 catch up
draws 0 catch up bakes 0`; default `gray frames 0 gray tile frames 0 catch up 1 catch up draws 1 catch up bakes 30` (one
catch-up at the first draw: 20 visible land tiles without a bake plus 10 at the view's edge, `catch_up_edge_bakes=10`,
in 10.4 ms with the stand-in baker; after it burst 1 sufficed) [L test].

## 4. Live: five huge product-path loads with 0404's driver [L, 2026-11-16 06:47-07:32 EST]

**Update 2026-11-16 (ticket 0417):** the single slow bakes of 1.7-9.7 s at the first draw are the render thread waiting in the D3D11 driver's Flush under GPU memory pressure (the board at about 8.5-9.9 GB of 16 GB), not a pipeline first use and not disk reads; a pipeline warm-up does not prevent them (off by default). Render-thread log writes no longer block on a windowless console. See [terrain-bake-slow-first-use.md](terrain-bake-slow-first-use.md); the residency fix is ticket 0428.

**Setup.** As 0404 (`terrain-surface-verification.md` 14), on instance c5h-f (Goldberg, F:), save
`C5H-VP8P-HUGE_0350` (c499d3c6...) copied as `C5H-0409P-HUGE_0350` (removed at each restore), the D33 product path
(ServerRenderer = 1, the composite with its manifest, Overlay kept on, frame cap 30), client 2554 x 1411, Vibrant
Textures present. Builds of this branch by `render/evidence/0409/build_pp_0409.sh` (0404's recipe), x64 DLL 74588135...
(the pin's release DLL): runs 1-3 at a5017203 (server.exe 1b887769..., proxy 14fc4e1a..., composite a7e02d4f...), runs
4-5 at 4434a622, which adds only the slow-bake log line (server.exe 269d923d..., proxy 55460b60..., composite
8328d1ef...). Driver `render/evidence/0409/run_0409.py` (0404's `run_0404.py` copied; only names, stop file, pins and the
analysis's render.log lines changed) through `live_0409.sh`: the load screen closed at the first moment its Continue
button showed, composite back-buffer dumps every 2 s from `NO_FIELD` to 12 s after the click, the composite's lockstep
CSV per EXE frame (`unbaked` = the server frame's `VPFrameSlot::tilesUnbaked`, the renderer's own count). The
workstation was loaded (11 registered heavy jobs, 7 other game instances): every duration is an indication, not a timing
result (COMMON 23). Restored by hash after runs 1-3 and after runs 4-5 (backup equal, `set-proxy --original` 0,
provision verify ok, save copy removed); no dialog box at any check. Raw data `F:/c5-64bit-work/0409/run{1..5}/`; table
`render/evidence/0409/summary_0409.txt` (`summary_0409.py`).

Seconds after the click; "frames with unbaked tiles" counts the CSV rows whose `unbaked` > 0:

| run | EXE fps after the click | first server frame (= first frame without an unbaked tile) | first draw (render.log): 75 visible land tiles, 75 without a bake, 77 baked (2 at the view's edge) in | slowest bake | frames with unbaked tiles after the click (whole launch) | no terrain on screen |
|---|---|---|---|---|---|---|
| run1 | 19.4 | +18.8 | **10,807 ms** | 9,675 ms (no tile logged: build before the log line) | **0** of 4,345 (0) | 18.7 s |
| run2 | 10.9 | +7.7 | **394 ms** | 36 ms | **0** of 1,746 (0) | 7.7 s |
| run3 | 12.3 | +6.4 | **390 ms** | 33 ms | **0** of 1,712 (0) | 6.4 s |
| run4 | 8.5 | +11.6 | **2,571 ms** | 1,662 ms, tile (111, 27), bake 25 | **0** of 1,787 (0) | 11.6 s |
| run5 | 8.5 | +13.0 | **5,609 ms** | 4,853 ms, tile (109, 28), bake 29 | **0** of 1,859 (0) | 13.0 s |

0404's four loads on the build before (same save, driver and machine, c5h-g): 5-19 frames with 58-74 unbaked tiles after
the click. With the catch-up: **0 frames with an unbaked tile in 5 of 5 loads**, and no server frame of the five
launches had one. The first server frame is the clean one (`first_taken_rel` = `first_clean_rel` in every run).

**The first frame's extra latency [L].** The first draw waits for its 77 bakes. Without a slow bake that is 390-394 ms
(runs 2 and 3; the other bakes 4-15 ms each), where 0404's first frames waited for 1 bake (33-71 ms): the first server
frame comes about 0.33-0.36 s later than before, and it is complete; before, the first complete frame came 1.2-15.8 s after
the first server frame. In 3 of the 5 loads **one** bake among the first 30 took 1.7-9.7 s and held the first frame
2.6-10.8 s; the map then shows no terrain (the D33 clear colour) that much longer instead of flat-coloured tiles.

**Single slow bakes are older than 0409 and are the first frame's real cost [L, I for the cause].** 0404's render.logs
have them too: run1 one bake of 11,646 ms before a draw (`predraw_max_ms`) 1-2 s after the field, holding the server's
next frame while unbaked tiles were on screen; run3 2,735 ms in the bake-ahead. The catch-up does not make them; it moves
the one that falls among the visible tiles in front of the first frame. The two logged ones were neighbouring visible
tiles, (111, 27) as the 25th bake and (109, 28) as the 29th, both "ok"; runs 2 and 3, on the same instance and the same
install files minutes apart, had none. What such a bake waits for is not measured: the residency times only the
baker's call. [I: a first use inside the pipeline baker that is cheap on a quiet machine and slow under load, for example
a shader variant the GPU driver compiles on first use (as the EXE's fow-models `D3DCompile` stalled 0.7-13.7 s in 0404),
a terrain layer's or a style's textures, or a height page.] Proposed to the MC as a follow-up (render/bake): time the
pipeline's steps in such a bake, and warm the pipeline (one throwaway bake per style and shader variant at `setMap`,
before the field's first draw) so no first use lands in the first frame. Since commit 4434a622 every bake of 500 ms or
more is named in render.log (`residency: slow bake: tile (x, y) took N ms of CPU (...)`, `slow_bakes=` in the stats
line).

Visual Checks (all DIAGNOSTIC, info, `visualcheck.py list --issue 0409`): each run's contact sheet of the presented
frames around the load screen's end and its end shot. In run2's sheet the server's first frame (t = 8.1 s) shows the
whole view baked, with no flat fallback colour.

## 5. Open points

- The ordinary pan: within the burst, stale re-bakes and new tiles share the burst nearest first, so a pan that shows
  new tiles together with more than `burst` stale ones can leave a few new tiles in the fallback colour for a frame
  (the pan test: 8 bakes, 5 gray tile frames counted). Not changed here (the acceptance keeps the burst for pans); a
  rule "tiles without a bake first" inside the burst would remove it.
- The catch-up has no time bound by design (the acceptance: every visible tile before the frame). A camera that sees
  far more tiles than the game's (strategic view is not drawn by the server) would make one long frame.
