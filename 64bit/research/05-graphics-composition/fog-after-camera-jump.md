# The first frame after a far camera jump: no fog, no water, purple sea tiles (ticket 0663)

Written 2026-11-22 by impl-0663 (account B, branch `task-0663-fog-after-jump`). Origin: ticket 0662's live check
(`city-renderer.md` 18.1, "New finding"): in the first back-buffer dump after two of four minimap jumps into unexplored
land, opaque purple (150, 4, 222) blocks and clouds thin enough to show rivers and relief. Scripts:
`server/evidence/0663/` (`offline_0663.py`). Pictures and logs read: 0662's output `C:/c5-64bit-work/0662/out1/{exe,beta}/`
and logs `C:/c5-64bit-work/0662/live1/pp/arm-*/runs-Logs-VP64/run/` (c5h-new-a, EXE build 26577625, the product layout
of main 328fd71f3 laid by `lay.py`, Tiny save `C5H-4985-SMKK_0009`, Vibrant Textures; not in git).

## 1. Answer

- **The purple is the server's terrain module drawing deep-sea tiles in its material fallback colour [C].** Tiles of
  the EXE's height class 1 (highest sample below 2.0: open sea, `terrain-bake-residency.md` 1) are never baked, by the
  EXE or the server. `c5r::terrain` (`render/src/terrain/terrain.cpp`, the draw loop) draws **every** tile in the view,
  and a tile without a colour cache goes to its own pixel shader (`kOwnHlsl`, decision D58 1c): with
  `render_fallback = material` (the default, `srv_render_host.h`) the colour is a hash of the tile's material byte,
  `h = m * 2654435761`, rgb = bytes 1, 2, 3 of h. **Material byte 37 gives exactly (150, 4, 222)**, and no other byte does
  (`offline_0663.py colour`). That shader reads no fog, no overlay and no light. The EXE draws no such tile: its in-play
  list holds class 3 only (`terrain-bake-residency.md` 1); in the run, `near 1s` shows the EXE's list with
  `list_nocache=0` (85 tiles) while the server drew 129 with `nocache=31` at the same view [L, render.log].
- **Normally nothing of it is seen [L]:** on explored sea the server's ocean is drawn over it; on unexplored sea the
  ocean's pixel shader discards (mask R = 0, `water-system.md` 10.2) and the three cloud planes (gated by the mask's G)
  cover it. In the five dumps after each purple frame, the purple pixels' area is plain cloud: mean (213, 216, 214),
  no purple tint (`offline_0663.py dumps`).
- **In the first frame after a far jump the server's fog mask is empty, so nothing covers it [C for the mechanism, L for
  the pictures].** Section 2. With the mask all zero: the terrain's own shader fades every land pixel to the cloud base
  texture (R = 0: the "see-through clouds" with relief and rivers), the ocean and river shaders discard everywhere (R =
  0: no water at all), the cloud planes draw nothing (G = 0), and the class-1 tiles show their purple.
- **It is the patch's alone [C, not compared live].** Stock builds its mask every frame in that frame's own window and
  draws no class-1 terrain; stock's equivalent pixels under a missing cloud would be the scene clear, not purple. A
  live stock arm at the same jumps was not run (section 5).
- **Not tied to the smoke's option set [L].** The beta arm (the first beta's set: ServerCities, ServerLandmarks,
  ServerFarmFields, ServerUnits 1) drew the same 31 / 24 uncached class-1 tiles at the same two destinations (table in
  section 3). Its first dumps after those jumps came later or on a good frame; its action 1 (LookAt Tonga -> Tahiti)
  shows the same empty-fog frame on land (`city-renderer.md` 18.1: "the fog or the clouds of the old view over the new
  one", which this section corrects: it is **no** fog, not the old view's).

## 2. Why the mask is empty in that frame [C]

1. **The frame rule.** The server draws EXE frame N with the overlay pair of frame N-1 (decision 2 (i),
   `overlay-system.md` 5: texels and OverlayProj of N-1 together, because frame N's overlay exists only at Present N).
   `Scene::overlayPoll` keeps "the newest pair"; `FowScene::beforeWorld` (`server/src/render/srv_fow_scene.cpp`) gives
   the mask builder `overlayProj` = that pair's matrices (the mask's **window**) and `viewProj` = camera N.
2. **The mask's plots come from camera N, their place from the window of N-1.** `MaskRenderer::buildMask`
   (`render/src/fow/mask.cpp`) clears the target to 0, takes the plot range from `viewProj`
   (`plotRangeFromCamera`, because the server leaves the range empty) and places the hexagons through `overlayProj`.
   While the camera moves by small steps the two agree to within the window's margin (the 64-unit steps of
   `fog-of-war.md` 9.5). After a jump farther than the window is wide (the minimap jumps of 0662 moved 900-2,400 world
   units; the window at that zoom is about 830 x 700), **every hexagon of camera N's plots falls outside the N-1
   window**: the target stays all zero.
3. **Every fog reader samples that target through the same N-1 window** (terrain t4 with `Options::overlay.proj`,
   water and river t10, cloud planes), so all of them read zero for every pixel of the new view (R, G, B = 0: neither
   explored nor unexplored).
4. **Frame N+1 is right**: at Present N the pair of N arrives (`overlayPoll` marks the scene dirty, a new pair
   generation rebuilds the mask, `fog 1s` `with=42` against 23-26 presents in the jump's second is those redraws), and
   N+1 is drawn with the window of N, which holds camera N+1's view at a still camera.

So the defect lasts **one EXE frame per far jump** *(corrected 2026-11-22 by 0665's live step, section 6: one or two
presented frames, after one frame of the old place)*, the same one-frame class as 0659's model records (`city-renderer.md`
18.1, known issue A4.7). The first frame after a far jump is long: the composite's `stats` line of the jump's second
has `ft_max` 212.7 ms and `ft_p99` 122 ms (exe arm, 04:39:11) against 33.8 ms at rest, so that one frame can stay on
screen for a fifth of a second. Why 0662's dumps 0.42-0.59 s after the click still caught it is not settled [I: the
EXE takes the click on its next frame, the jump frame then stalls; the dump is taken at the next Present after the
request, so a dump requested during the stall lands on the jump frame]. A per-frame record would settle it (section 5).

## 3. Measurements [L, 2026-11-22, from 0662's run; read offline 08:30-08:42 EST]

First dump of each jump (`offline_0663.py dumps`, half-size PNGs; "water" = b > r + 50 and b > g + 20, purple excluded):

| exe arm jump | destination (camera x) | first dump after (s) | exact purple pixels | water in the first dump | water in dumps 1 and 5 |
|---|---|---|---|---|---|
| 3, minimap 0.03 | 244 (unexplored, sea) | 0.59 | 184,209 (20.5 %) | 2.8 % | 0.1 % / 0.1 % (sea under clouds) |
| 4, minimap 0.5 | 1427 (explored, coast) | 0.50 | 0 | **1.0 %** | 14.7 % / 14.7 % |
| 5, minimap 0.97 | -973 (unexplored, sea) | 0.42 | 188,718 (21.0 %) | 1.3 % | 0.1 % / 0.1 % |
| 6, minimap 0.5 | 1427 | 0.39 | 0 | 14.7 % (a good frame) | 14.7 % / 14.7 % |

Jump 4's first dump (`exe_jump4_0_bb.png`) is the same defect on explored land: the EXE's unit models in place, the
terrain in cloud-base colour, no water, no borders; the dump after it shows the coast in full.

Per second from render.log (`offline_0663.py lines`; `hex` = seen / visible / unexplored hexagons of the mask):

| arm | second | view | tiles | nocache | visible / visible_land | fog hex | cull |
|---|---|---|---|---|---|---|---|
| exe | 04:39:10 | A (1152) | 127 | 2 | 123 / 121 | 4/207/10 | 148 |
| exe | 04:39:11-21 | jump 3 dest (244) | 129 | **31** | 127 / 96 | 6/33/195 | 2 |
| exe | 04:39:22-30 | jump 4 dest (1427) | 129 | 0 | 129 / 129 | 0/204/0 | 183 |
| exe | 04:39:31-40 | jump 5 dest (-973) | 129 | **24** | 128 / 104 | 0/54/180 | 7 |
| beta | 04:31:21-30 | jump 3 dest | 129 | **31** | 127 / 96 | 6/33/195 | 2 |
| beta | 04:31:31-39 | jump 4 dest | 129 | 0 | 129 / 129 | 0/204/0 | 183 |
| beta | 04:31:40-44 | jump 5 dest | 129 | **24** | 128 / 104 | 0/54/180 | 7 |

`nocache` = `visible - visible_land` at every view: the uncached tiles are exactly the non-land (class 1) ones, and the
server draws them in every frame at those views (hidden by clouds, section 1).

## 4. What would fix it (for the MC; product code is outside this ticket's touches)

Two independent defects; the first alone removes the purple, the second the whole frame.

- **F1, the class-1 tiles [C; small].** *(Done by ticket 0665, 2026-11-22: section 6.)* Do not draw an uncached tile that is not land (`Tile::land` false in the
  residency, height class 1) with the own shader in the product: stock draws nothing there (its list is class 3 only,
  and the scene clear stays under the ocean). Either skip such tiles in `c5r::terrain`'s draw loop when the colour source
  is the caches, or draw them only in the diagnostic fallbacks (`constant`, `height`, `normal`). What to check: the
  ocean is alpha-blended over what lies under it (SRC_ALPHA / INV_SRC_ALPHA, depth LESS_EQUAL without write,
  `water-system.md` 10.4), so on **explored** open sea the purple seabed shows through wherever the ocean's alpha is
  below 1, today, at rest; stock blends over the scene clear there. Whether that alpha reaches 1 on class-1 tiles was
  not measured [open; weak L: the first dumps of 0662's four resting views, Tonga's with 10 class-1 tiles in the
  visible set, hold 0-27 pixels with r and b both above g + 40, i.e. no visible tint]: if it does not, F1 also changes resting pixels of explored deep sea (toward stock), and rule A
  applies (sceneshot replays go through the same module; `merge-check-scene-bundle-rule-a`). Effect on the jump frame:
  the scene clear (84, 84, 84) on unexplored sea instead of purple. *(Corrected 2026-11-22 by 0665's live step,
  section 6: in the product the pixels left undrawn are black (0, 0, 0), and the no-fog frame can be presented twice.)*
- **F2, the jump frame's fog [C for the cause; the fix is a design choice, D185 / COMMON 30: for the Architect].**
  *(Done 2026-11-22 by ticket 0674's route (c) of epic 0673's design: this list's route (a), but with the overlay of N-1
  resampled into the camera-made window instead of bound transparent (transparent only where the N-1 window does not
  reach, or for a frame whose resample fails); not this list's (c). As built, tests and rule A:
  [pan-coherence.md](pan-coherence.md) section 3. Not verified live.)*
  The mask needs a window that holds camera N's view. Routes, from least to most change:
  (a) when camera N's footprint lies outside the N-1 pair's window (testable before the build: the footprint corners
  through `overlayProj` outside [-1, 1]), draw that frame with a window the server makes from camera N (the EXE's own
  rule: the footprint snapped to 64 units, `fog-of-war.md` 9.5; the server only needs a window that holds the view,
  because it samples its own mask), and bind the overlay as transparent for that one frame (its N-1 texels hold the old
  place anyway; the terrain's one OverlayProj serves t1 and t4, `terrain.cpp`, issue 3953);
  (b) on such a frame build the mask over a camera-made window but keep the overlay of N-1 (its borders would sit at the
  old place's texels: worse);
  (c) hold the server's frame N until the pair of N (decision 2 (ii), only on far jumps): the composite would then
  insert an older frame (the old place under the new interface) for that frame, which is the defect in another form.
  (a) is the recommendation; it also covers 0659's case if the model records' fit uses the same "view outside the pair"
  test (not examined here).

## 5. Not verified, and the live check that would settle it

**MC 2026-11-22 (#10139):** no live step in 0663 (no 26577625 mods-form instance free); the live check below is the
acceptance of the F1 ticket, and F2 goes to the Architect only if F1's live check still leaves a visible frame. The
proposed ticket texts are in `server/evidence/0663/acceptance-0663.txt`.

- **Stock at the same jumps** (step 2 of the ticket): not run. [C] says stock cannot show purple (no class-1 terrain
  draws) and builds its mask in its own frame's window.
- **That exactly one frame is affected** [C by the frame rule, not measured]: a live run with the composite's
  `lockstep_csv=1` (per-frame camera and present time, `shell_composite.ini`) and back-buffer dumps requested in a tight
  loop from just before each minimap click (`shell_composite.shot`, one dump per Present it is seen at) would give the
  frame index of every dump against the frame of the camera change.
- **The beta set showing the purple in a picture**: the counts say the same tiles are drawn (section 3); no dump caught
  its jump frame at a sea destination.
- **The material byte 37's meaning** (which terrain the material texture codes as 37) is not looked up; any value would
  do, since F1 removes the draw.

## 6. F1 done: open-sea tiles are no longer drawn (ticket 0665, 2026-11-22)

Written 2026-11-22 by impl-0665 (account B, branch `task-0665-hide-open-sea-tiles`, on main 73d299cae). Scripts and
outputs: `server/evidence/0665/`.

**The change [C].** `c5r::terrain::Renderer::draw` (`render/src/terrain/terrain.cpp`, the tile loop after the cull)
skips a tile when all of these hold: `Options::color == Caches`, `Options::fallback == Material`, the tile has no colour
cache, `Tile::land` is false (the EXE's height class 1: the highest sample of the tile and 4 around it below 2.0), and
the draw is not the Screen Overlay pass. Such a tile is counted in `FrameStats::tilesWithoutCache` as before (the
`nocache` field of render.log keeps its meaning: in-view tiles without a cache) and in the new
`FrameStats::seaTilesHidden`; `tilesDrawn` no longer counts it. The fallbacks Constant, Height, Normal and WorldPos
(diagnostics, `render_fallback`) and the colour sources other than Caches draw every tile as before. A non-land tile
that has a cache is drawn with it (none exist in the product: the bake never bakes class 1). The Screen Overlay pass
(trees' t3, ticket 4551) is unchanged. No new input, no new option: the server's `render_fallback = material` default
takes the new path.

**What it changes on screen [C].** Under the open sea there is now nothing of the terrain, as in stock (the EXE's
in-play list holds class 3 only, `terrain-bake-residency.md` 1): the scene clear (84, 84, 84) and the far depth (1.0)
stay there until the ocean, which blends over it without writing depth (`water-system.md` 10.4). In the first frame
after a far jump (section 2: no fog, the ocean discards) the open sea shows the scene clear instead of purple; the rest
of that frame (no clouds, no water, terrain in the cloud-base colour) is F2's and unchanged. At rest the depth under
explored and unexplored open sea is now the far plane instead of the seabed's, as in stock; colour changes at rest only
where the ocean's or the clouds' alpha is below 1 over a class-1 tile (the rule A replays below measure it).

**Module test [L, 2026-11-22 08:55].** `c5r_terrain_test` case `draw_hides_open_sea_tiles_without_a_cache` (a 4 x 4
field of 1-plot tiles, west half land, east half class 1, material byte 37 everywhere, clear 84,84,84): Material as the
colour source draws 16 tiles, 11,400 of 19,200 pixels purple; Caches + Material draws 8, hides 8 (tilesWithoutCache
16, landTilesWithoutCache 8), 5,700 purple pixels (the land half) and 13,500 clear ones; a cache on one sea tile draws
it (9 drawn, 7 hidden); a camera over the sea half: 0 drawn, 4 hidden, **0 purple pixels**; fallbacks Constant, Height
and Normal and the Constant source draw all 16. The whole render suite: 108 of 108 passed, 0 skipped
(`render/build.py --test`, Release, hardware device, the EXE copy of this machine's `render_exe` setting).

**The ocean's alpha over class-1 tiles reaches 1 [L, 2026-11-22 09:13-09:22].** Rule A replays of the whole C: scene
library (16 bundles of epics 0301 and 0455/4980, 8 dumped frames each, EXE 4390913 copies, asset root `mods548-vt`) with
a sceneshot built from this branch (`server/evidence/0665/rulea_0665.sh`, summary `rulea_sum.py`): **colour EQUAL in
all 128 frames** (0 pixels differ), so nothing of the seabed showed through the ocean or the clouds at rest, explored
or not: the question of section 4 is answered, and F1 changes no resting colour. **Depth differs in 13 of the 16
bundles** (every frame of each; 3,100 to 733,563 values per frame, all "in coverage": covered in the live dump, not
in the replay; max abs 0.03-0.09), and is equal in the three without open sea in view (`C5H-0301-FOG_0008`,
`C5H-0301-TINY_0009`, `C5H-4980-C2BWONDER_0009`). The cause is only the hidden sea tiles: the same replays with
`--set render_fallback=constant` (the sea tiles drawn again, in another colour that the ocean covers) are byte-equal in
colour and depth in all 16 bundles, 128 of 128 frames (09:22-09:35, exit 0 each). So the server's depth under open
sea is now the far plane, as stock's (no class-1 terrain and an ocean that writes no depth).

**Rule A consequence (for the MC at merge).** No new input, option or member: the bundles record `render_fallback =
material` and the player draws with the same module. But the P1 check compares depth byte for byte, so after this
merge `offline-equals-live` fails on the 13 bundles until they are recorded again (their `dumps/<frame>.depth.f32`
members hold the seabed's depth; the colour members stay valid). The changed members: `dumps/*.depth.f32` of
`C5H-0301-HUGE_0350`, the four `C5H-0301-SCNB_0009`, and `C5H-4980-` `C1MOUNTAIN`, `C2ANATURAL`, `C3AGARRISON`,
`C3BGARRISON2`, `C4PORT`, `C5AIR`, `C6CARRIER`, `C7INTERCEPT` (full ids in `server/evidence/0665/acceptance-0665.txt`).
Until a re-record, a replay with `--set render_fallback=constant` reproduces those dumps exactly (not P1).

**Live [L, 2026-11-22 11:51-12:19].** c5h-new-a (EXE 26577625, mods form, CP+VP+EUI 5.4.8, Vibrant Textures), the kit
save `C5H-4985-SMKK_0009`, the layout of this branch laid by `lay.py` (server.exe `e6ac934b...`, composite
`1c900aa7...`, `lockstep_csv=1`; not the installer's, so every picture is DIAGNOSTIC, D112). Driver
`server/evidence/0665/live_0665.sh` / `live_0665.py`; frame rows `frames_0665.py`. From camera A (45,13) minimap clicks
at 0.03 / 0.5 / 0.97 / 0.5 of the panel (camera x 1152 -> 244 -> 1427 -> -973 -> 1427, every step 900-2,400 units),
the composite's dump requested continuously from 0.6 s before each click to 2.4 s after it.

| arm | jumps | pictures | purple (150, 4, 222) pixels |
|---|---|---|---|
| exe (4985's smoke set: ServerCities/Landmarks/FarmFields 0) | 12 | 82 dumps | **0** |
| beta (the first beta's set) | 32 | 221 dumps | **0** |
| stock (`lay.py helper off`: ServerRenderer 0, the player's config.ini; PrintWindow, no composite) | 8 | 390 captures | 0 |

- **What a dump can catch.** The composite takes the trigger file on its control thread every 500 ms
  (`Sleep(500)` in the control loop, `shell_composite.cpp`) and dumps at the next Present; a dump costs about 100 ms
  (the frames that carry one are 100-190 ms against 33 ms). So the dumps sample about one frame in ten, not every frame;
  a per-frame series needs a composite change (a trigger that dumps N consecutive Presents) [C].
- **The frame at the step (offset 0: the first composite frame whose camera row has the new x) shows the old place
  [L, 2 dumps]:** beta jump 20 (to 0.03) shows the explored coast of x = 1427 complete and fogged, under the new
  interface; its row is `kind older, age 1` (the composite inserted the server frame of the previous camera). Jump 1 the
  same. Not purple, not fogless: the "old place under the new interface" one-frame form of 0663 section 4 (c).
- **The no-fog frame is still presented, and it can last two frames [L].** exe jump 0 (to 0.03), dump at offset +2
  (row `older, age 1`: the second server frame drawn with the new camera): the clouds missing, the land in cloud-base
  colour with its rivers, and **the open sea black (0, 0, 0)** where the purple was (21.6 % of the picture; 0 purple).
  beta jump 19 (to 0.5), offset +1 (row `exact`, again the second server frame of the new camera): no fog, no water on
  the explored coast, the EXE's units and labels drawn over it. beta jump 12, offset +2 (`exact`, the third server
  frame): fogged and correct; every dump at offsets +3 to +5 (15, both patched arms) is fogged. So the frame without fog of section 2 is
  presented for one or two frames (33-70 ms, longer when the frame is slow) after the old-place frame; the second one
  exists when the overlay pair of the first new frame has not reached the server before it draws the next one
  [I: consistent with section 2's rule, the pair arrives at Present N].
- **The black is the scene under the server's frame in the product [L],** not the (84, 84, 84) of the offline clear:
  where nothing is drawn (no terrain since F1, ocean and clouds discarded in the no-fog frame) the presented pixel is
  (0, 0, 0). At rest no black shows (0.7 % of the picture in every resting dump, the interface's own black).
- **Stock** [L, PrintWindow]: 0 purple, no black area, the new place shown 0.6-0.8 s after the click (the click's own
  path); one capture in jump 1 was a flat (84, 84, 84) frame (a PrintWindow glitch of the kind `tools/hi/capture.py`
  retakes; not counted).
- **Answer for the MC (ticket text):** F1 removes the purple; **a frame without fog is still visible** (black open sea,
  see-through land, no water) for one or two frames after every far jump: F2 (ticket 0666) goes to the Architect.
- Visual Checks (issue 0665, DIAGNOSTIC): sheets beta/stock of the first sequence `vc-20261122-121943-workstation-47e1`,
  `-7fad`, `vc-20261122-121944-workstation-9bc8`, `vc-20261122-121945-workstation-7697` (each with the stock arm's sheet
  of the same click as reference); single frames: exe offset +2 no fog `vc-20261122-121953-workstation-7ee4`, beta
  offset +1 no fog `vc-20261122-122002-workstation-4a63`, beta offset 0 old place `vc-20261122-122004-workstation-58d0`.
- *(2026-11-23, ticket 0668, section 7: F2 was done by ticket 0674's camera window (0666 closed on it); live on main
  81baa8966 no frame without fog was caught after 21 far jumps, while the window of N-1 (`render_fow_mask_window=pair`)
  still shows it.)*

## 7. 0659's "clouds over revealed land after a minimap jump" is this frame, and it is gone on main (ticket 0668, 2026-11-23)

Written 2026-11-23 by impl-0668 (account B, branch `task-0668-clouds-minimap-jump` on main 81baa8966). Scripts:
`server/evidence/0668/` (`build_0668.sh`, `live_0668.sh`, `live_0668.py`, `counters_0668.py`); pictures and logs in
`C:/c5-64bit-work/0668/` (not in git).

**The question.** 0659 (`city-renderer.md` 17.9, EXE build 4390913, main of 2026-11-22 09:20) saw, 0.52 s after a
minimap click onto the revealed coast (camera x -973 -> 1427), the land "under the clouds, with only the units drawn
over them" (`vc-20261122-092319-workstation-803b`), where stock was clear. Same fault as section 2's, or another one
(a fog mask lagging the camera for several frames)?

**Offline: the same frame [L, by the pictures, 2026-11-23 10:55].** 0659's dump of that click
(`C:/c5-64bit-work/0659/out1/product/product_jump2_0.png`) and 0662's first dump after the same click on 26577625
(`C:/c5-64bit-work/0662/out1/exe/exe_jump4_0_bb.png`, read by section 3 as jump 4) are the same picture: the terrain
faded to the cloud base texture with relief and rivers showing through, no water, no borders, the EXE's units and city
banners over it. That is section 1's frame with an all-zero mask (R = 0 fades land to the cloud base, the cloud planes
draw nothing, the ocean discards), not a mask lagging behind: a lagging mask would show the opaque clouds of the old
view, not see-through ones. The dump fell on that frame because a dump costs a frame and the jump frame is long
(section 2, 6).

**Live on main 81baa8966 [L, 2026-11-23 11:02-11:55 EST, c5h-new-a, MC-B's GO #11551].** EXE 26577625, Goldberg, mods form, CP+VP+EUI
5.4.8, Vibrant Textures (D94). This branch's own build of main 81baa8966: server.exe `c22b9b1e...` (`build_server.py`
on the 0.1b pins, 106 s), composite `d78ef29b...`, laid by `lay.py prep` + `helper on` (not the installer: every
picture DIAGNOSTIC, D112), the shipped `shell_composite.ini` with `lockstep_csv=1` (`lockstep_mode=exact`, ticket 0691).
The first beta's option set (ServerUnits, ServerCities, ServerLandmarks, ServerFarmFields, ServerCombat 1, ServerArc,
ServerPath 0). The kit save `C5H-4985-SMKK_0009` (`74fc7428...`), hidden launches at the 30 fps cap. From camera A
(45,13) at 0659's zoom: minimap clicks at 0.03 / 0.5 / 0.97 / 0.5 of the panel (camera x 244 unexplored sea, 1427 the
revealed coast, -973 unexplored sea, 1427), three times per launch: 12 far jumps of 900-2,400 units per arm. Pictures
from 0.6 s before each click to 2.4 s after it. At the end (11:55) `lay.py restore ok`, the kit copy taken out by
hash, `provision.py verify` 0, `leftovers` clean, the test-only `server_cfg_extra` line removed after its arm.
Not compared: the dumps of the beta arm now come in runs of consecutive frames (frames 4557, 4558, 4559, 4560, 4562
for one click), not one per 500 ms as in 0665 section 6 [L; why not looked at].

How a picture is placed and judged (`live_0668.py analyze`):
- **Frame.** A PrintWindow capture is stamped with `QueryPerformanceCounter` before it; the composite's lockstep CSV
  rows carry `present_qpc` on the same clock, so the capture is given the last frame presented at or before it; a far
  step is a CSV row whose `cam_x` moved over 300 units (offset 0 = the first frame with the new camera). A dump's frame
  is the composite's "shot: wrote ... frame N" line. The placement of a PrintWindow capture is good to about one frame
  (the window's content can trail the Present) [I].
- **Class.** Each picture's world area (half-size columns 230-1229, rows 40-579: the interface cut away) against the
  jump's first good picture (before the click: the old place) and its last (the new place at rest): mean absolute
  difference per channel <= 12 counts as OLD or NEW, otherwise ODD (neither place at rest). FLAT = the PrintWindow
  glitch of the hidden window (the flat scene clear (84, 84, 84) over half the picture or more), left out. NOUI = an ODD
  picture without the interface's top bar: a PrintWindow capture part-way through a frame (runbook 4.10), not a presented
  frame. Every ODD picture was looked at.

| arm | launch | pictures | FLAT | OLD / NEW | ODD | NOUI | non-flat at offsets 0-5 | ODD after the step | fog counters over the 12 jumps |
|---|---|---|---|---|---|---|---|---|---|
| betapw (beta set, `render_fow_mask_window=camera`, the default; PrintWindow) | 11:03 | 536 | 289 | 78 / 165 | 3, all the **old** place before the step (no pre-click reference: those were FLAT) | 1 | 17: all NEW (+0: 3, +1: 3) | **0** | camwin 27 (2-4 per jump), ovl_moved 27, ovl_move_fail 0, 0 LOUD |
| pair (beta set + TEST-ONLY `render_fow_mask_window=pair`, the picture before 0674; PrintWindow) | 11:17 | 505 | 212 | 106 / 183 | **4: the frame without fog** (jumps 1, 7, 8, 9, at frame offset -1 or 0; water 1.0-2.1 % of the coast's 15 %) | 0 | 16: 1 ODD at +0, 15 NEW | 4 | camwin 0 (the key named in render.log) |
| stock (patch off, `lay.py helper off`; PrintWindow) | 11:31 | 538 | 4 | 176 / 353 | 0 | 5 (part-way frames: no interface; stock's own grey unbaked tiles in two) | (no CSV) | 0 | - |
| beta (beta set, camera; the composite's dumps) | 11:47 | 85 dumps (9 of the 12 clicks moved the camera: clicks 2, 3, 7 left it in place) | 0 | 0 / 63 | 22, all the old place before the step (dumps have no pre-click reference) | 0 | 9: all NEW (+0: 3, +1: 1) | **0** | camwin 34, ovl_moved 34, ovl_move_fail 0, 0 LOUD |

**Answers.**
1. **Cause [L by the pictures, C by section 2]:** 0668's frame is section 2's empty mask of the far jump's frame (0663's
   F2, ticket 0666), not a separate lag. **This ticket closes into 0666.**
2. **On main it is gone [L]:** with the camera window of ticket 0674 (`pan-coherence.md` 3.1), 0 frames without fog in
   247 PrintWindow pictures of 12 far jumps (17 of them on the step's first six frames) and 85 dumps of 9 far jumps (9
   on the first six frames), against 4 in the positive control with the window of N-1 on the same build, save and set. Every far jump took 2-4 camera-window frames
   (`camwin`), each one a frame that the pair's window would have left empty, and every overlay move succeeded.
   Stock shows no such frame either (0 ODD in 529 presented pictures).
3. **Fix route:** none needed beyond 0674's (merged c23de660f). Still open: 0665 section 6's "old place under the new
   interface" frame was an `older` lockstep row; since ticket 0691 every row is `exact` (betapw 9,572 rows, pair 10,341,
   all kind 0), so that form is gone too [L, CSV kinds; no picture at offset 0 showed the old place].

**What the method can and cannot see.** PrintWindow of the hidden window returned the flat clear in 41-57 % of the
captures in the patched arms (1 % in stock); the good captures came about every 60-100 ms against 33 ms frames, so one
frame in two or three is sampled. The positive control shows that rate catches the defect (4 pictures in 12 jumps, all
at the step); a frame without fog of one frame per jump at the same rate would be expected about 4 times in the camera
arm and was seen 0 times [I: same rate assumed], and the counters cover every frame. A per-frame series of presented frames still needs a
composite trigger that dumps N consecutive Presents (0665 section 6).

**Visual Checks** (issue 0668, DIAGNOSTIC, 2026-11-23 11:56): sheets of the first sequence's four clicks per arm, the
patched ones with stock's sheet as reference: beta `vc-20261123-115614-workstation-0426`, `-115615-...-0eea`,
`-115616-...-f200`, `-115617-...-d3c7`; betapw `-115618-...-3b2c`, `-115619-...-3258`, `-115620-...-3025`,
`-115621-...-4603`; pair `-115622-...-6ed7`, `-115623-...-8f3e`, `-115624-...-282d`, `-115625-...-3596`; stock
`-115625-...-8311`, `-115626-...-c45e`, `-115626-...-3284`, `-115628-...-1441` (the PrintWindow sheets can hold FLAT
captures). Single frames at offset 0 of the click -973 -> 1427: camera (default) correct
`vc-20261123-115653-workstation-cf5c`; positive control without fog `vc-20261123-115655-workstation-fa97`.

**Not verified.** The installer-laid layout (D112, D162), 4390913 (0659's build; the server and composite are the same
code on both builds), the reporter's eye.
