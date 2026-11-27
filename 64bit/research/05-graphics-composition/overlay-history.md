# The overlay's leading edge kept from history (ticket 0793, D269 step G)

Ticket 0793 (epic 0673, lead-0673; D269, the retained-world proposal
`research/07-architecture-decisions/architect-2026-11-25-retained-world.md` section 7 and rules R1-R5). Built
2026-11-26 by impl-0793 on branch `task-0793-fow-leading-edge`. Marks: [C] code and unit or scene tests of this
repository, [L] measured live, [I] inferred.

## 1. The hole it closes

Since 0674 (`pan-coherence.md` 3.1), a frame whose camera N sees ground outside the overlay pair's window (a zoom-out
step, a pan step past the window's margin, a far jump) takes a mask window made from camera N, and the EXE's overlay of
frame N-1 (borders, grid, yields, lens tints: `overlay-system.md`) is resampled into that window by
`c5r::fow::OverlayResampler`. The ground the N-1 overlay never held, the leading edge, was (0, 0, 0, 0) for that frame:
borders and grid lines missing in a strip for one frame [C, 0674's `overlay_resample_world_points`]. The overlay cannot
be the server's own (the EXE draws it), so the fix keeps the overlay's past (proposal section 7).

## 2. The design as built [C]

**Deviation from the proposal's text, and why.** Section 7 proposes "the last few overlays with their windows (22 MB each
with mips; eight are about 180 MB)". A ring of eight covers 8 frames (about 130 ms at 60 fps): it fills a pan reversal or
a zoom-out right after a zoom-in, but not ground seen a second or a minute earlier. Rules R1 and R2 ask for a store keyed
by the world and sized for the whole map. Built instead: **one whole-map history texture**, which holds every piece of
ground any received overlay covered since the map was loaded, at less memory than the ring.

- **The store (R1, R2).** `OverlayResampler` (`render/src/fow/resample.cpp`, API in `render/include/c5r/fow/fow.h`) keeps
  an R8G8B8A8 colour texture and an R8 "held" mask in world space: u = (x - x0) / W, v = (y1 - y) / H. On a wrapping
  map W is one wrap period (the camera block's `wrapPeriod`, 64 W - 1) and the history sampler wraps u, so the EXE's wrap
  copies of a window (x past the seam) and the camera windows on the other side read the same ground; on a map that does
  not wrap, W is the world box plus 128 units each side. H is the world box's height plus 128 units each side (the EXE's
  windows reach up to 128 units past the camera's footprint: `fog-of-war.md` 9.5). Density 1 texel per world unit, capped
  at 16 M texels and 8,192 per side. Sizes [C, the test's printed figures]: Tiny 3,583 x 2,473 (1.000 texel per unit,
  43 MB with the mask); a huge map (8,191 x 5,541 world units) about 0.58 texels per unit, at most 80 MB. The EXE's
  overlay itself is 1.6 to 3.6 texels per unit (`overlay-system.md` 4), so the history is 1.6x to 6x coarser than the
  live overlay: a strip filled from it is a little softer for its one frame, not missing.
- **Writes.** `remember(ctx, src, proj, world, frameKey)`: the received overlay drawn into the history at its window, one
  quad per wrap copy the window reaches, colour and held = 1 where the source's uv lies in [0, 1] (MRT, no blending; the
  source read trilinear by the derivatives, so a zoomed-in overlay is minified correctly). The Scene calls it for every
  bound pair (`srv_render_scene.cpp`, after `OverlayCopiesFromFrame`, view 0's matrix `oc.proj[1]`, key = the pair's
  generation). A call with the last key does nothing; the same window as the last absorbed one is drawn again only every
  `kRefreshCalls` = 30 new keys (at rest the ground under the window is the live source's own in the move; the refresh
  keeps a border that changed at rest current for a later return).
- **Removal only on evidence (R3).** A history texel changes only when a newer overlay covers that ground. A missing,
  late or refused pair writes nothing and removes nothing. The whole history is cleared only when the map changes: the
  world key is the camera block's `gridSeq` (incremented at every map load), with the box and the wrap period; also by
  `forget()` and by `release()` (device loss, shutdown).
- **Reads.** `resample()` is unchanged where the source reaches. Where it does not, it takes history / held under the
  bilinear footprint (unheld texels are (0, 0, 0, 0) and held 0, so the quotient is the mean of the held texels) where
  held > 0.25, else (0, 0, 0, 0) as before. Ground never seen in this map stays transparent: nothing is invented.
- **Switch.** `render_fow_overlay_history` = 1 (default) | 0 (the picture before 0793), parsed by
  `ParseRenderFowOverlayHistory` (`srv_render_options.cpp`, declared in `srv_render_scene.h`), read by `ParseHostConfig`
  and written into a recorded bundle's render_config (`srv_render_host.cpp`, two lines as 0674's
  `render_fow_mask_window`; added after 0789's merge, lead-0673 #16415 / #17347), so an offline replay draws what was
  recorded and a bundle without the key replays with 1. The environment variable `C5_RENDER_FOW_OVERLAY_HISTORY` (0 / 1)
  overrides it at `Scene::init` (A/B runs, tests); the render log says which (`fog: render_fow_overlay_history=1 (...)`,
  `... by C5_RENDER_FOW_OVERLAY_HISTORY`). *(Corrected 2026-11-26 11:1x; was: the key env-only until 0789 merged.)*
- **Counters (R3's "each bridged frame is counted").** render.log `fog 1s:` gains `ovl_hist=<camera-window moves this
  second whose whole leading edge the history held>` and `ovl_hole=<moves whose edge had ground the history never held>`;
  at the end `overlay history totals (0793): render_fow_overlay_history 1, offers, refused, absorbed, skipped, remember
  failures, clears, leading edges, filled whole, with a hole, edge cells N held M, history WxH (d texels per world unit)`.
  The edge is counted on a CPU mirror of the held mask in 32 x 32-unit cells (cells of the target window whose centre the
  source window does not hold; cells off the history's box are not ground). A move whose edge holds unseen ground is
  expected on a first visit; `ovl_hole` at a still camera cannot happen (no camera-window frame at rest).

## 3. Tests [C]

`render/tests/fow/mask_test.cpp` (c5r_fow_mask), 4 new cases (hardware device, WARP without one; no EXE needed), on a
"world colour" source whose texel colour is a function of the world point (64-unit blocks anchored to the world, x
modulo the wrap period):

- `overlay_history_fills_leading_edge`: overlays A and then B (A panned 256 units right) remembered, the pan back to a
  camera window C (B moved 576 left and 64 down) with B as the pair: every block inside B shows B's colour, every block
  only A held shows A's colour from the history (before 0793: transparent), every block neither held is (0, 0, 0, 0);
  the counters: 1 leading edge, 1 with a hole, some but not all edge cells held. A zoom-out step inside A and B's union:
  every block shown, none transparent, counted filled whole. R3: a newer overlay of A's ground (other colours) replaces
  A in the history. `forget()` and a new map key each leave the edge transparent again. The same key twice draws once.
- `overlay_history_wrap_seam`: an overlay straddling the seam (x 1,408..2,240 on Tiny's 3,583 period) fills a camera
  window on the left copy (x -2,112..-1,280) from the history's other side; the same inputs on a map that does not wrap
  leave the ground past the box transparent.
- `overlay_history_refusals_and_refresh`: a world box empty or not finite, a negative wrap period, no source, a window
  with a cross term: BadLayout, the history untouched; 60 calls with one window draw twice (the refresh rule); a huge
  map's history within 16 M texels; `release()` drops it.
- `overlay_history_gpu_time`: GPU time of `remember()` (a 2048^2 overlay into the Tiny history) and of `resample()` with
  a history, printed (an indication, no speed claim, docs/briefs/PERFORMANCE.md).

0674's `overlay_resample_world_points` (no `remember()` call: the strip still exactly (0, 0, 0, 0)) passes unchanged.

Results: section 5.

## 4. Not verified

- Live: the history fills edges in a real game (section 6's counters, laptop, 2026-11-26) [L]; whether the filled strip
  is visible at the owner's frame rate, and the flash detector's (`server/evidence/0736/flash0736.py`) score on pans and
  zooms with and without the history, are not measured yet (the night's pictures were covered by a dialog; section 6).
  *(Corrected 2026-11-26 07:3x; was: "no game was run for 0793".)*
- The history's staleness: a border that changed while its ground was out of every window shows its old state in a
  filled strip for one frame (the proposal's accepted trade: "Borders and yields change slowly"). A lens or overlay mode
  switched while elsewhere (yields shown, a lens tint) likewise for one frame on return. Not measured.
- The world box's source: the camera block's `aabbMin` / `aabbMax` and `gridSeq`; a save loaded into a map of the same
  size takes a new `gridSeq` [I from `shell_shared.h`'s comment "incremented whenever the values below changed"; not
  checked live for two loads of equal size]. If it did not change, the old game's overlay would fill strips until
  covered.
- The `refresh` interval (30 new keys) and the 0.25 held threshold are choices, not measurements.

## 5. Results

**Unit tests [L, 2026-11-26 00:59-01:02 EST, workstation, RTX 4080 (hardware device)].**
`python -I render/build.py --test -R mask --build-dir F:/c5-64bit-work/0793/render --game-dir <4390913 copy>` through
quietlock: 5 of 5 ctest entries pass; `c5r_fow_mask.exe` alone 21 cases, 0 failed checks, 0 skipped. Printed:

- `overlay_history_fills_leading_edge`: history 3583 x 2473 at 1.000 texel per world unit; pan back: source blocks
  36/36, history blocks 36/36 (the leading edge), never-seen blocks clear 50/50; zoom-out step over seen ground 150/150
  blocks, 0 clear.
- `overlay_history_wrap_seam`: 80/80 blocks from the history's other copy.
- `overlay_history_refusals_and_refresh`: 60 calls with one window, 2 drawn, 58 skipped; the huge map's history
  4869 x 3446 (0.594 texels per world unit, 80.0 MB with the held mask).
- `overlay_history_gpu_time` (n 200 after 5 warm-up; an indication under docs/briefs/PERFORMANCE.md, the workstation
  shared with other agents, no speed claim): `remember()` of a 2048^2 overlay into the Tiny history median 0.0072 ms
  (p90 0.0072, max 0.0089); `resample()` 2048^2 with the history median 0.0377 ms (p90 0.0382, max 0.0412). 0674's
  `overlay_resample_gpu_time` in the same run: 0.0439 / 0.0432 ms median (zoom / pan), so the history's reads add no
  measurable time to the move on this GPU. `remember()` runs once per new overlay generation with a new window, or
  every 30 generations at rest; the move only on camera-window frames.

**Scene test [L, 2026-11-26 01:06-01:12 EST, WARP].** `server/tests/render/fow/test_fow_build.py --out
F:/c5-64bit-work/0793/fowb --c5render F:/c5-64bit-work/0793/render`, then `test_fow_scene.exe`: 35 of 35 cases pass,
among them 0674's `camera_window_moves_overlay` unchanged (its strip still transparent: the history held only the
pair's own small window) and the new `camera_window_history`: frame 1 at rest with the pair's window [-128, 64]^2,
frame 2 with the pair's [-32, 32]^2 (a camera window [-128, 64]^2): history on, 39 of 39 strip points (outside
[-32, 32]^2, 3 or more units from a block edge) equal frame 1's overlay at the same world point, offers 2, absorbed 2,
1 edge filled whole, 0 holes, the init note `render_fow_overlay_history=1`; with `render_fow_overlay_history=0` 39 of 39
transparent, no offers; with the camera block's world box empty in y (the rig's default) 2 offers refused quietly
(`no world box in the camera block`, one plain note, no LOUD line), 39 of 39 transparent.

**Builders, rule A, live: see section 6** (as they finish).

## 6. Builders, suites and the laptop

**Builds [L, 2026-11-26].** On the branch merged with main 45a315df2 (VP 5.5 pins): `server/build_server.py --src
keep/pins-5.5/src-x64-a2627c8ac --deps keep/pins-5.5/deps-x64-release --config release` OK (2,000 s under a loaded
workstation), `proxy/build_proxy.py` OK, `shell/build.cmd` OK (all on F:). The laptop's tb1 is VP 5.4.8 (Community Patch
modinfo v151, job `20261126-043540-dc9a`), so the laptop SHIP was built separately: scratch branch
`task-0793-548ship` = main e98068385 (the last commit before the 5.5 pin move) + 0793's code commit e7c68e1fc (applied
clean), `build_server.py` on `keep/pins-0.1b` OK (1,685 s; the bin's x64 DLL equals the pin's by SHA-256 `b84aa3e9...`),
the composite from the shell build above (`shell/` unchanged between e98068385 and 45a315df2), the purity file from
`proxy/tools/derive_purity.py --commit 1b87dd185` (169 rows). Not for merge: the branch exists only for that SHIP.

**run_all [L, 01:23-04:17, `--out F:/c5-64bit-work/0793/ra`, 13 suites, no offline-equals-live].** Pass: cut-tap 1,
decal-tap 13, ice-reader 11, overlay-reader 36, shadow-reader 40, overlay-scene 3, shadow-ice-scene 6, water-scene 29,
forest-scene 3. Failed, read:
- fow-scene 2 of 3: `test_fow_scene_every_case_is_listed_here` (the Python suite's case list lacked
  `camera_window_history`): fixed in `server/tests/render/fow/test_fow_scene.py` (91d58173b). The C++ cases all pass.
- render-host 234 of 235, scenebundle-programs 0 of 2 (`bundle_format_test.exe` 600 s and `sceneshot_test.exe` 1,500 s
  timeouts, every inner case that printed passed: recorder 268/268, sources 24/24), scenebundle-py 32 of 34 (two
  recorder-host cases whose host drew 0 frames in its 5 s: "the render thread did not stop within 5 s"): all timing
  failures on a workstation where one cmake configure took 792 s. Rerun 07:16-08:14 (`--no-build`): render-host
  235/235, fow-scene 3/3, scenebundle-py 34/34; scenebundle-programs again only `sceneshot_test` at its 1,500 s timeout.
- **`sceneshot_test` was slowed by the history itself [L, corrected in the code].** The same binary alone (`--work`),
  2026-11-26 08:18-09:31: with the history 3,224 s (WARP 1,947 s, hardware 1,277 s), with
  `C5_RENDER_FOW_OVERLAY_HISTORY=0` 1,136 s (WARP 810, hardware 326); 322 of 322 checks pass in both. The hardware arm
  slowing as much as WARP pointed at CPU work per Scene, not GPU work: every new Scene's first frame compiled the
  history's four programs (D3DCompile, level 3) and made and cleared its textures (44 MB on Tiny), and an offline replay
  makes a Scene per bundle and mode. Fix (commit 43612e159): `remember()` does its bookkeeping first and device work only
  when it draws; the first window after a load (or `forget()`) is drawn after `kRefreshCalls` (30) pairs at rest or at
  once by a move, so a replay of a few frames at rest draws nothing; the resampler's bytecode is compiled once per
  process. The cost of the rule: ground seen only in the first half second after a load, then left by a zoom-in, is not
  in the history [I]. The timing after the fix: section 6, final checks.

**Laptop [L, 2026-11-26 04:35-05:00, MC-B's grant 04:35-05:45, Intel HD 630, 3840 x 2160 at 60 Hz].** Probe
`20261126-043651-e649` rc 0. Two product jobs of `server/evidence/0793/lap0793.sh` (capture_job keys 150/300/600/1200 ms,
drags 300/1000 ms left and right, zoom 3 ticks x 4; 2 repeats; the purity file through impl-0784b's `purity_wrap.py`,
copied from commit 4f3e317a0 to a local scratch folder and shipped with the job): `on` `20261126-043807-4644` (04:38-04:47,
18 cases rc 0, vpproxy.log `boot: purity: loaded (pack=0) ... 140 method keys, 29 Lua names; bound to the x64 DLL
b84aa3e90f51...`, server.log `fog: render_fow_overlay_history=1`), `off` (`C5_RENDER_FOW_OVERLAY_HISTORY=0`)
`20261126-044950-98c0` (04:50-05:00, rc 0, `render_fow_overlay_history=0 by C5_RENDER_FOW_OVERLAY_HISTORY`, purity
loaded). Both fetched (reduced frames about 1 GB each, then `--max-mb 0 --include out/render.log`).

- **The pictures are DIAGNOSTIC [L].** In every case of both jobs the crop is covered by a Windows Defender Firewall
  alert for another instance's EXE (`C:\c5-testbed\instances\tb1-55\install\civilizationv_dx11.exe`, a VP 5.5 instance
  set up that night) left open on the laptop's screen; nothing was clicked (MC-B #16823: the owner answers it).
  `flash0736.py --min 6` on the on job: 0 hits in 18 cases, most frame distances 0.0, because the dialog barely changes:
  not a result. The flash check is rerun once the alert is gone (granted 09:00-09:40, only after a screen grab shows it
  gone).
- **The counters are a result [L]: the game ran under the dialog.** Sums over the jobs' `fog 1s` lines:

  | job | camera-window moves (`camwin` / `ovl_moved`) | edge filled whole from the history (`ovl_hist`) | edge with ground never seen (`ovl_hole`) | move failures |
  |---|---|---|---|---|
  | on, 4644 | 169 / 169 | 134 | 27 | 0 |
  | off, 98c0 | 167 / 167 | 0 | 159 | 0 |

  The 8 on-moves in neither column are moves whose camera window did not reach past the pair's (no edge cell on the
  map's ground). So on the 4.15 key, drag and zoom cases after a load, 134 of 161 leading edges (83 %) were ground an
  earlier overlay had held, and are drawn now instead of transparent; the 27 others are first sightings (the cases start
  at the load's view, so the first moves of each direction meet new ground). No `overlay history totals (0793)` line
  in either render.log: the job's stop ends the server before the scene's end lines (the same for 0674's
  `mask window totals` line in these logs) [L].

**The laptop rerun with clean pictures [L, 2026-11-26 12:13-12:31, D272: the alert answered Cancel by the owner's
word, a job pressing only that dialog's Cancel button (laptop; oldpc's two prompts were left to others)].** SHIP
`ship548b` (the code with the first-window rule), purity loaded in both jobs. `on` `20261126-121258-f62e`: 166
camera-window moves, 134 edges filled whole from the history, 25 with ground never seen; `off`
`20261126-122150-be78` (`C5_RENDER_FOW_OVERLAY_HISTORY=0`): 165 moves, 0 filled, 157 with a transparent edge.
`flash0736.py --min 6` over all 18 cases of each job (keys 150-1200 ms, drags 300/1000 ms both ways, zoom 3 x 4; 2
repeats): **0 hits in either arm**, the top score of every case about 0.0 (the moving frames' own distances 15-35).
So the detector at its 160-pixel grey reduction does not see the one-frame transparent strip at all (the off arm
passes too): it cannot tell the arms apart, and the evidence that the edge is now drawn is the counters above and
the tests. A detector for this layer would compare the leading-edge strip of the frame at full size [I, not built].

**Final checks on the merged head [L, 2026-11-26 09:47-11:04, workstation, all outputs on F:].** Head fabc86859
(0793 + main 6293f9f40, after the deferral fix): `render/build.py --test` 109 of 109; `build_server.py` on pins-5.5 OK
(169 s); `shell/build.cmd` OK after `gen_table.py` (runbook 1.14); `run_all.py`, 13 suites (no offline-equals-live):
all pass (render-host 237, cut-tap 1, decal-tap 13, ice-reader 11, overlay-reader 36, shadow-reader 40, fow-scene 3,
overlay-scene 3, shadow-ice-scene 6, water-scene 29, forest-scene 3, scenebundle-programs 4 programs with
`sceneshot_test` 328 of 328 inside its timeout (the suite 746 s), scenebundle-py 35); builders: units 107/107, units
extras 14/14, extras (136 + roads 121), unitfeed, farmfields, trigger scene, effect routes 394/394, liveterrain, derived,
unit shadow, sceneshot `--smoke`: all OK. The laptop SHIP was rebuilt with the fix (`ship548b`: scratch ec968ad94 on
pins-0.1b). After 0789's merge (origin/main 869437685) the host's two key lines were added (above).

**Rule A [L, 2026-11-26 11:18-11:34, MC-B's slot #17357, head 10eb0990f].** `run_all.py --only offline-equals-live`
(runbook 2.4, the C: library copy matched the F: master, 17 bundles): 18 passed, 0 failed, 0 skipped (667 s). The
bundles' frames are at rest (no camera-window frame, as for 0674), so the history draws nothing in them: with the
first-window rule not even the history's textures are made; bundles recorded from now on carry
`render_fow_overlay_history`, older ones replay with the default 1 [C].

**The checks of the MR head [L, 2026-11-26 11:08-13:04, head 10eb0990f = 0793 + origin/main 869437685 + the host
lines].** render ctest 109/109; `build_server.py` pins-5.5 OK; shell OK after `gen_table.py`; run_all 13 suites: 12
pass at once, scenebundle-py 34/35 with `test_bundle_from_the_host` again a host that drew 0 frames in its 5 s while
the laptop fetch and other agents loaded the box (`overlay history totals` in that log: offers 0, so nothing of 0793
ran), then 35/35 on a `--no-build` rerun at 13:01-13:04; all 13 other builders OK (units 107/107, effect routes
394/394, sceneshot `--smoke`, ...). That test failed the same way twice on 2026-11-26 under load and passed when
rerun each time [L]: a timing test, not this change.

**With 0753's record ahead (impl-0793b, 2026-11-26 15:2x).** origin/main 1cf5f1ad3 (0753 merged as 556242107: frame
jobs J3, record ahead, `render_record_ahead` default `auto`) merged into the branch (merge c7630c442): text conflicts
only (this section's README row, the issue log); `srv_render_host.cpp` and `srv_render_scene.cpp/.h` merged clean and
0793's hunks are unchanged. How the two meet in `Scene::draw` [C, source at c7630c442]: the history's `remember()` (where
the overlay frame is taken, about line 1162) and the camera-window move `m_ovlResample.resample()` (line 1236) run on the
immediate context before record ahead's record point (`recordAhead()`, line 1375, before `unitShadowStep`); the recorded
terrain and middle lists read `op.overlay.srv` as the move left it, and record ahead writes none of the resampler's
textures. So a recorded frame binds the same overlay as an inline one, and the merge moved no logic in 0793's files.

**Reduced rule A on the combined head [L, 2026-11-26 17:57-18:27, MC-B's go #17918, head 6ea53bf13].** History at its
default 1 with record ahead forced on (`C5_SCENESHOT_FRAME_JOBS=1 C5_SCENESHOT_RECORD_AHEAD=1`, 0753's `raon` arm):
`run_all.py --only fow-scene --only overlay-scene --only offline-equals-live` (runbook 2.4; the C: library copy matched
the F: master, 17 bundles): fow-scene 3/3, overlay-scene 3/3, offline-equals-live 18 passed, 0 failed, 0 skipped (707 s).
The suite keeps no player log, so one bundle (`C5H-0301-FOG_0008_20261119-124546_workstation`) was replayed by hand
with the same variables at tolerance 0:0 (`F:/c5-64bit-work/0793b/one.sh`, outside git): `record ahead 1 (TEST:
C5_SCENESHOT_RECORD_AHEAD)`, `fog: render_fow_overlay_history=1`, record ahead lists 8 (terrain 7, middle 7, no
prediction 1, mispredicted 0, failed 0), 8 of 8 frames colour and depth EQUAL (0 of 3,603,694 pixels differ). As before,
the bundles are at rest, so this shows the two paths coexist byte-exactly, not the history's moving edge.

**The checks of the merged head [L, 2026-11-26 15:28-19:2x, head 6ea53bf13 = 0793 + origin/main 1cf5f1ad3, pins-5.5,
all outputs on `F:/c5-64bit-work/0793b`, a heavily loaded workstation: the render ctest took 1,926 s against 471 s in
the morning].** `gen_table.py` (both tables) OK; `render/build.py --test` 109 of 109; `build_server.py` on pins-5.5 with
the proxy OK; `shell/build.cmd` OK; `run_all.py` 13 suites: 11 pass at once (render-host 247, cut-tap 1, decal-tap 13,
ice-reader 11, overlay-reader 36, shadow-reader 40, fow-scene 3, overlay-scene 3, shadow-ice-scene 6, water-scene 29,
forest-scene 3); scenebundle-programs: three programs pass, `sceneshot_test` hit its 1,500 s timeout under the load
(not a result), and alone with `--timeout 3600` passed 328 of 328 (WARP and hw, 1,837 s, still under load);
scenebundle-py 34/35 twice with `test_bundle_from_the_host` again the host that drew 0 frames in its 5 s (`draws 0`,
`overlay history totals (0793)`: offers 0, so nothing of 0793 ran; 0753's MR saw the same case fail under load);
the third run (`--no-build`, 19:22-19:26) passed 35 of 35. Builders: units 108/108, units extras 14/14, extras 136 + roads 121, unitfeed, farmfields,
trigger scene, effect routes 394/394, liveterrain, derived, unit shadow, sceneshot `--smoke`: all OK.
