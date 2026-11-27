# Camera extras: camera rotation and a bigger zoom-out (ticket 0781, design)

Ticket 0781 (the owner's idea 1 for Extras, beta 6, 2026-11-25): [reworded for privacy] camera control options: a bigger zoom-out and the ability to
rotate the camera, perhaps via the CameraStartRotatingCCW / CameraStartRotatingCW / CameraStopRotatingCCW /
CameraStopRotatingCW events. Investigation and design only; no product code. impl-0781, 2026-11-25.

Tags: `[C]` read-only headless Ghidra decompile of build 4390913 (scratch, never committed: D83) or the source of this
repository; `[L]` a live run on a Goldberg test instance; `[I]` inferred; `[O]` the offline scene harness (sceneshot).
Addresses are build-4390913 VAs at ImageBase 0x400000; the GameViewState (GVS) offsets hold on build 26577625 too
(`research/02-exe-internals/new-exe-build-26577625.md`: structure offsets unchanged, 11 of 11 camera patterns carried).
The controller is the object at GVS+0x44; its fields are written below as GVS offsets (controller +X = GVS +X+0x44).

## 0. Short answers

| Question | Answer | Conf. |
|---|---|---|
| Does stock rotate? | Yes, but only from the debug panel: `DebugMenu.lua` (shown with `~` only when `UI:DebugFlag()` is set; that `config.ini` `DebugPanel = 1` sets it is [I]) binds numpad 4 / 6 to `Events.CameraStartRotatingCCW` / `CW` and the key-up to `...Stop...`. No other Lua of the game, Community Patch, Vox Populi or EUI fires them. Any Lua context can fire them: the live trial did, through the harness's Lua channel, and the camera turned | [C] Lua, [L] |
| What do the events do? | Set / clear a bit of the controller's key-motion byte GVS+0x148 (CCW = 0x20, CW = 0x10); each frame a set bit yaws the camera about the point where its view axis meets z = 0, at 40 degrees a second (constants 40.0 / -40.0, degrees to radians 0.01745). Height, pitch (45 degrees) and the zoom level do not change | [C], [L] CCW bit 0x20 at -40.1 deg/s, CW bit 0x10 at +40.2 deg/s |
| Pitch events (`CameraStartPitchingUp/Down`)? | Do nothing on the map camera: bits 0x40 / 0x80 only mark the camera as moving; the pitch stayed 45.000 degrees | [C], [L] |
| Is stock rotation usable as it is? | **No: the camera's clamp is written for a yaw of 0 or 180 degrees.** The clamp box (GVS+0x154..+0x160; the explored area's bounding box [I], not the map's) is tested against the camera's ground point in y but the camera's own position in x. With any other yaw the camera's x is off its ground point by up to `distance x sin(yaw)`, so near the box's x edges every pan and zoom-out step is refused (the move is dropped whole); on an early map (a box about 500 units wide) the camera is stuck | [C], [L] |
| Does a data write raise the zoom maximum? | Yes. Every zoom step clamps the new level to [GVS+0x138, GVS+0x13C] read at that step; a heap write of GVS+0x13C (D228: data writes allowed) took effect at the next wheel step. The camera's Reset (at each camera init; every game load [I]) writes 11.0 / 20.5 again, so the write is repeated after each load | [C], [L] |
| What changes past 20.5 in the EXE? | **Stock crashes**: with the maximum raised, the stock EXE crashed while zooming from 22.5 to 24.5 on a Tiny save, in FluidFOW's "FOW Particles" pass (0x59F8C0), a write past its per-frame staging memory (2.2). With the patch (D33: FluidFOW off) no crash up to 30.5. Both: the far plane stays 4,000 (the ground is cut from about zoom 24 at the top of the view, wholly at height 4,000), the EXE makes one wrap copy a side (a Tiny map outgrows it at about 24); fog ramp row and shadow fade move with the maximum; overlay texels thin (1.0 a world unit at 20.5, 0.76 at 22.5) | [L], [C] |
| What does the server assume? | Nothing about yaw (it copies the matrices; the models take the frame's camera whole); about the zoom only the stock range 11..20.5 in the distance fog's ramp row (four modules). Its cost grows with the area seen (1.9-2.3 ms a pass at 20.5, 2.9 at 22.5, 3.2-3.4 at 24.5 on the workstation, Tiny); its fog particles reach their 10,240 cap at 24.5; past one wrap copy it draws black tiles and unexplored terrain without clouds (a fog leak) | [C], [L] |
| Recommendation | `CameraZoomOut = 1` (maximum 22.5) and `CameraRotation = 1`, both default 0, both only on the product path with the EXE's FluidFOW off; rotation needs the clamp correction (4.2 B); 24.5 and beyond need the far plane and the wrap copies first (4.2 A) | |

## 1. How stock exposes rotation

### 1.1 Who fires the events [C, the installed Lua of c5h-new-a (VP 5.4.8, EUI), read 2026-11-25]

- `Assets/UI/InGame/DebugMenu.lua` `InputHandler`: KeyDown `VK_NUMPAD4` -> `Events.CameraStartRotatingCCW()`, `VK_NUMPAD6`
  -> `Events.CameraStartRotatingCW()`, KeyUp -> `Events.CameraStopRotatingCCW/CW()`; numpad 8 / 2 the same for
  `CameraStartPitchingUp/Down`. `DebugMenu` is a hidden LuaContext of `InGame.xml`; `InGame.lua` (and Community Patch's
  override) shows it on `~` only when `UI:DebugFlag()` is set and the game is not multiplayer or hot seat.
- No other file under the install's `Assets/`, `(1) Community Patch`, `(2) Vox Populi` or the EUI files names any of the
  rotation or pitch events (grep of `.lua` and `.xml`). VP's `WorldView.lua` binds the arrows (pan) and PageUp/PageDown,
  `+`/`-` (zoom); numpad 1, 3, 4, 6, 7, 8 only leave the ranged-attack modes; no game data hotkey uses numpad 4 / 6, `[`
  or `]` (the `KB_*` / `VK_*` keys of the Gameplay XML and VP's database files).
- So a player triggers rotation today only with the debug panel enabled. An extra needs its own binding (1.4).

### 1.2 What the EXE does with them [C: 0x741EB0 the controller update, 0x65EE30, 0x41E020 Reset]

- The events set or clear bits of the controller's key-motion byte (GVS+0x148): pan 1 / 2 / 4 / 8, rotate 0x10 / 0x20,
  pitch 0x40 / 0x80. Which event sets which bit: CCW = 0x20 [L, stock arm], CW = 0x10 [L, product arm].
- The controller update (0x741EB0, every map frame): bit 0x10 calls the yaw step with `dt x 40.0`, bit 0x20 with
  `dt x -40.0` (degrees; the step converts with 0.017453 rad/degree). The yaw step (0x65EE30) moves the camera and its
  target so that the point where the view axis meets z = 0 is the pivot, multiplies the camera-to-world matrix by a
  rotation about +z, and moves them back: a yaw about the looked-at ground point, height and pitch unchanged.
- Bits 0x40 / 0x80 only set the "moving" flag; no pitch code runs for them in the map camera.
- Pans follow the rotated camera: the key pan (0x669B60) moves along the camera's right row and back row projected on
  the ground, so "right" stays screen-right after a rotation.
- The camera's pan and zoom moves are checked by 0x47E750 (pan through 0x669B60, zoom through 0x405030, SetPosition
  0x556940, and others) and clamped by 0x463510 (glide): the ground point's y is `y + z x back.y / back.z` (exact for any
  yaw), but its x is taken as the camera's own x: the x offset `z x back.x / back.z`, zero at yaw 0 and 180, is never
  added. A move whose result fails the check is dropped whole (no partial move).
- Mouse picking (0x6668F0) unprojects through the camera's own view and projection: correct at any yaw [C, section 5 of
  `research/02-exe-internals/camera-and-picking.md`].
- The overlay window (0x51B450, the 2048 x 2048 Terrain Overlay target's OverlayProj) is the world-axis bounding box of
  the view's ground footprint (eight frustum-edge intersections), padded by 64 and snapped to 64 units: it follows any
  yaw by itself (wider at 45 degrees) [C], [L: 896 x 704 at yaw 0, 896 x 896 at yaw -145, 704 x 896 at yaw -81, same
  zoom].

### 1.3 Live: the events on a Goldberg instance [L]

Set-up (all live runs of this note): instance c5h-new-a (Goldberg, EXE build 26577625, mods548: CP, VP 5.4.8, EUI;
Vibrant Textures present, D94), the -ia 32-bit DLL `Release-5.4.8-50-g5d18d3d6c` (sha256 ff0f6e86...), save
`C5H-4773-TINY_0018` (Tiny, Spain, turn 18), hidden window (cloak, offscreen), 30 fps cap, 2554 x 1411 client. Driver
`proxy/evidence/0781/drv_0781.sh` (phases `launch`, `trial`, `full`, `zoom`, `install`, `remove`), reader and the test
write `proxy/evidence/0781/camx.py` (catalogue symbols through `shell/tools/gt_reader.py`). The events were fired with
`tools/harness/lua.py --state InGame`; each Lua call took about 13 s to answer in this game, so the "1 s" and "3 s"
rotations of the driver ran 13-14 s; the rate is taken from the 20 Hz watch, not from the driver's sleeps. Camera
states: `F:/c5-64bit-work/0781/live/s1.jsonl`, `s1-rotccw.jsonl`, `s3.jsonl` (scratch; numbers copied here). Pictures
DIAGNOSTIC, in Visual Checks under issue 0781 (D112; none committed).

| What (stock arm, run s1 2026-11-25 21:05-21:10 EST) | Result |
|---|---|
| `Events.CameraStartRotatingCCW()` then Stop | key bits 0x20 for the whole turn (60 of 60 samples at 20 Hz); yaw changed at **-40.1 degrees a second**; height 424.26, pitch 45.000 and zoom 14.256 unchanged; the camera circled its ground point (-1184, 234), which stayed fixed in every state |
| `CameraStartRotatingCW()` then Stop | yaw changed the other way: 0 -> -145.3 (= +214.7) between the two Lua calls, i.e. +40 degrees a second for about 14.4 s between the start and the stop taking effect [I: the 20 Hz watch of this turn wrote nothing, so the rate is not measured directly] |
| `CameraStartPitchingUp()` 1 s | no change (pitch 45.000) |
| UI.LookAt of the selected unit while rotated | the yaw stays (-81.3 before and after) |
| Key pan RIGHT 600 ms at yaw -81 | **no move** (camera x -1603 outside the box's x -1440..-928: every step refused) |
| Wheel out at yaw 49, zoom 11.0 (run s2: one notch after each raise of the maximum to 22.5 ... 30.5) | **no move**: every step refused, the level stays 11.0 (the camera x -986 against the box's x max -928; a step out moves it toward +x) |
| Wheel in x7 at yaw 49 | accepted to the minimum 11.0 (height 261) |
| Key pan UP 800 ms at yaw 49 | accepted (the camera x stayed inside the box) |
| Key pan RIGHT 600 ms at yaw 0 (run s3) | moved x -1184 -> -930, stopped at the box's x max -928 (as designed) |
| The stock picture while rotated | terrain, water, rivers, borders, the fog's clouds, units, the city and its banner all drawn correctly at every yaw seen (-145, -81, 49); the minimap rectangle as before (shots `s1-rotccw`, `s1-rotlookat`) |

So the stock rotation itself works and the EXE draws a rotated view correctly; what makes it unusable is the clamp
(1.2): a rotated camera near the explored area's x edges cannot pan or zoom out. At yaw 180 the clamp is exact again
(the x offset is 0) [C; not tried live].

### 1.4 What a VP64Options key would turn on

`[Extras] CameraRotation = 1` (4.1): keys bound to the EXE's own rotation events (no new camera code: the yaw, its
speed and the picture are the EXE's), the clamp correction that makes a rotated camera pan and zoom (4.2 B 2, data
writes only), and a "north up" key. Without the clamp correction the key alone would ship the stuck camera of 1.3.

## 2. Zoom-out

### 2.1 The limits and the data write [C, L]

- Reset (0x41E020, from the camera init 0x4686C0) writes min 11.0 (0x9E0038), max 20.5 (0x9F7A74), base height 50.0,
  zoom step 2.0 per wheel notch (GVS+0x130), pan speed 1.0.
- Each zoom step (0x5F2EB0, from the wheel and key commands) asks 0x7A6460 for the new level: `level + step x sign`,
  clamped to [GVS+0x138, GVS+0x13C] read there; the camera then moves along the line to the cursor's ground point to the
  height `50 e^(level x 0.15)` and the move is checked by 0x47E750 (the clamp box, 1.2). Nothing else clamps the height.
- Every frame the controller copies max, min and the current level to the globals 0xD90FB8 / 0xD90FB4 / 0xD90FB0 and
  writes the zoom fraction `(level - min) / (max - min)` to 0xD90FAC. Their readers: the view-block hand-off 0x617F30 (the
  render side's copy), the shadow pass 0x523CA0 (shadows fade from max - 3 to max - 2 at shadow quality 1-2,
  `terrain-surface.md` 9), the distance fog's FogParams.w (the ramp row, `terrain-surface.md` 6), UpdateViews' Strategic
  View branch and the four other scene cameras (0x4813D0, 0x510140, 0x6E4870, 0x73E520).
- So a raised maximum is one heap float the patch can write (D228 base: a data write, no code). Consequences in the EXE
  itself: the zoom fraction at a given height drops (at the load zoom 14.26: 0.343 with max 20.5, 0.210 with max 26.5
  [L]), which moves the distance fog's ramp row and the shadow fade to the new top of the range.

### 2.2 Live: past 20.5 in the EXE (stock arm, run s3, 2026-11-25 21:29-21:32 EST) [L]

Fresh load (yaw 0), a key pan to the box's x edge, six wheel notches out, then the maximum raised by the test write
(`camx.py setmax`) in 2.0 steps with one notch out after each:

| State | zoom level | height | far | views | overlay window (texels a unit) | zoom fraction |
|---|---|---|---|---|---|---|
| load | 14.26 | 424 | 4000 | 1 | 896 x 704 (2.29) | 0.343 |
| stock maximum | 20.50 | 1082 | 4000 | 2 | 2048 x 1408 (1.00) | 1.000 |
| max 22.5, one notch | 22.50 | 1461 | 4000 | 2 | 2688 x 1792 (0.76) | 1.000 |
| max 24.5, one notch | (24.5 set) | 1912 when the EXE crashed (zoom 24.28, the glide toward 1972 cut short) | 4000 | 2 | 3584 x 2304 (0.57) | - |

- **The stock EXE crashed during the step from 22.5 to 24.5**, 21:31:21: `0xC0000005`, a write to an unmapped page, in
  the build-26577625 counterpart of 4390913's **0x59FEEA** (the crash site carried between the builds by
  `tools/exebuild/addr_match.py`, `bytes16`, high confidence; the 26577625 address stays in the local scratch, D202).
  [C] 0x59FEEA is a `rep movsd` in 0x59F8C0, the **"FOW Particles"** pass of FluidFOW (called from
  FluidFOW's draw 0x602B10 when FOWLevel > 2), copying the view's OverlayProj (0xDA1690 + 0x40 v) into a 0x90-byte
  per-draw constant block it has just taken from the per-frame staging allocator (0x4199D0 -> 0x8CFDC0); the write ran
  off the end of the mapped staging memory. The pass builds one quad per fog cell inside each view's rectangle
  (0xDA1B60 / 0xDA1B90 + 0xC v), sorted back to front, capped at 0x2800 (10,240) quads, through a 0xC0000-byte stack
  buffer. Root cause [I, from the code and the size of the view]: at zoom 24 the view covers the whole Tiny map twice
  (two views) in fog cells, and the particles' per-frame vertex and constant staging overruns the fixed per-frame
  staging memory, which has no bounds check. The crash handler's own text ("32-bit address space nearly exhausted",
  "Avoid zooming out too far") is its generic advice: the process had 1.6 GB free in all. Evidence: the crash box
  (closed by impl-0781, own test crash, `--fatal`), `crashes.log`, the minidump and a 2.08 GB full dump in
  `F:/c5-64bit-work/0781/crash/` (scratch). **So in stock the zoom maximum is also a crash limit**: a zoom-out extra
  must not let the EXE draw its own fog particles past about zoom 22.5 (section 4).
- The last frame before the crash (shot `s3-z24.5`): the clouds (fog of war) cover the map; on the left a **grey band**
  with no terrain or clouds: the part of the view beyond the one wrap copy the EXE makes per side (views = 2): the view
  is wider than the Tiny map (3,583 units) there. City banners and unit icons keep their screen size.
- Up to 22.5 nothing else changed: far stays 4000, the overlay window grows with the footprint (1.00 -> 0.76 texels a
  world unit), the camera kept its 45-degree pitch, the shadow factor 1.0 (shadow quality 3).
- The zoom level GVS+0x140 is the controller's target's: the zoom command (run from the window procedure) sets it and
  moves the target, then recomputes it from the target's height (0x405030, also after a refused move); the camera glides
  to the target in the frame update. After the crash the modal crash box still pumped the game window's messages, so
  the test's later notches moved the target and the level on to 30.5 while no frame ran (camera height 1912) [C, L].

### 2.3 Live: past 20.5 with the patch (product arm, run p1, 2026-11-25 21:46-21:52 EST) [L]

The 0.1b4 release installer (`VP64-Patch-0.1b4-std-only-for-VP5.4.8.exe`, sha256 84980836...) laid on the same instance
with VP 5.4.8's own 32-bit DLL as the player's (`drv_0781.sh install`; removed with `remove` after the run), its default
options (ServerRenderer, Units, Cities, Landmarks, FarmFields, Combat on; D33). Same save, fresh load (yaw 0), no pan
first, six notches out, then the maximum raised as in 2.2. The server's per-second lines (`render.log` of the run,
`proxy/evidence/0781/passcost.py`: the 1-s lines starting within 2.2 s after each camera read, the camera at rest):

| zoom | height | server pass busy mean / max (ms, 60 passes/s) | fog cloud planes | fog particles drawn / wanted | picture (shots `p1-z*`) |
|---|---|---|---|---|---|
| 14.26 (load) | 424 | 1.13-1.22 / 2.6-2.9 | 156 | 1,105 / 1,405 | as stock |
| 20.5 (stock max) | 1,082 | 1.89-2.25 / 4.8-5.4 | 775 | 5,514 / 4,415 | as stock |
| 22.5 | 1,461 | 2.90-2.92 / 6.6-6.9 | 1,312 | 9,017 / 6,642 | **correct**: the map and the fog's clouds as in stock at 22.5 |
| 24.5 | 1,972 | 3.15-3.38 / 8.0-8.1 | 2,451 | **10,240 (the cap)** / 9,449 | **broken at the left edge**: past the one wrap copy a band of black tiles and **unexplored terrain without clouds** (coasts of unexplored land visible: a fog leak); stock showed a grey band in the same place (2.2) |
| 26.5 | 2,662 | 4.00-4.70 / 10.3-10.9 | 3,616 | 10,240 / 13,618 | the top of the picture black (beyond the far plane, 4,000), the left as at 24.5, the clouds thinner (particles capped) |
| 28.5 | 3,593 | 2.90-3.19 / 6.7-7.1 | 4,096 | 10,240 / 16,054 | most of the ground beyond the far plane |
| 30.5 | 4,851 | 2.90-3.34 / 6.9-7.5 | 6,144 | 10,240 / 16,054 | the camera is higher than `far`: no ground at all, only the cloud planes (nearer) and the UI |

- **No crash with the patch**: the EXE's FluidFOW (and so its FOW Particles pass of 2.2) is off under D33, and the
  server's own fog draws at most 10,240 particles (its own cap) [L; C for D33's set].
- **Two hard ceilings, the same with and without the patch**: (1) the far plane stays 4,000 at every zoom (read at every
  state; the EXE does not move it with the zoom); the view's top corners reach it at about zoom 24, the screen centre at
  about 26.9 (height 2,828), the whole ground at 4,000 height; (2) the EXE makes at most one wrap copy a side, so on a
  Tiny map (3,583 units wide) the view outgrows the copies at about 24. On a larger map (2) comes later, (1) does not.
- The server's pass time on this Tiny map grows with the zoom up to 26.5 (1.9-2.3 ms at 20.5, 2.9 at 22.5, 3.2-3.4 at
  24.5, 4.0-4.7 at 26.5 on the workstation, RTX 4080, other jobs on the machine: one run, not a PERFORMANCE.md series)
  and falls again when the far plane cuts the ground away; the fog particles reach their cap at 24.5.

## 3. What the server assumes about yaw and zoom [C: the source at origin/main 899b3d1e8, 2026-11-25]

| Part | What it takes from the EXE | Yaw | Zoom past 20.5 |
|---|---|---|---|
| Camera block `VPShellCamera` (`shell/common/shell_shared.h`) | the full matrices: g_Proj, g_View[0..3], camWorld, fov, aspect, near, far, viewport, wrap data | none: copied, never rebuilt from angles | none; but the block has **no zoom level, minimum or maximum** (the server computes the fraction from the camera height, below) |
| Terrain LOD (`render/src/terrain/terrain.cpp` exeFrameLod, the EXE's rule ported, `terrain-surface.md` 7) | the view-projection | frustum sphere test and projected size: no yaw term | the level is set by the nearest visible tile, clamped at kMaxLodLevel 4; more tiles drawn, each at the frame's one level |
| Bake residency (`render/src/bakeresidency/residency.cpp` setView, rings) | the view-projection | frustum box test per 8 x 8 block and tile: no yaw term | more visible tiles; with `BakedTileCameraRadius = -1` (the default) the whole map is baked anyway, so no more memory; with a radius `r >= 0` the kept set (r rings around the visible set) grows with the footprint |
| Overlay pair and the fog of war mask (`overlay_tap`, `srv_fow_scene`) | the EXE's 2048 x 2048 overlay target and its OverlayProj per view | the EXE's window is the axis-aligned box of the footprint (1.2): follows yaw | the window grows with the footprint on the same 2048 texels: 2.3 texels a world unit at 14.26, 1.6 at 20.5 (`overlay-system.md` 4); at 24.5 about 0.6 [I, by the footprint's width]: borders, highlights and the fog mask's edge get softer, as in the EXE's own drawing |
| Distance fog (terrain `distanceFogBlock`, water, forest, ice: `fog.w < 0`) | nothing: the server computes the ramp row `fogZoomFraction(height, 11, 20.5)` | none (the ramp's u is the screen height) | **the stock range 11..20.5 is hard-coded in four modules**: past 20.5 the row stays 1 (the haziest row); the EXE with a raised maximum writes `(level - 11) / (max - 11)` instead, so the server and the EXE's tapped model constants would use different rows [I: the tapped PS constants carry the EXE's FogParams] |
| Shadows (`shadow_tap`, `shadow_factor` 0xD90FA8) | the EXE's shadow map, ShadowMatrix and fade factor | none | the factor follows the EXE's raised maximum: at shadow quality 1-2 the fade moves to the new max - 3 .. max - 2; at quality 3 it is 1.0 at every zoom |
| Models: cities, landmarks, resources (`srv_model_layer_c5r.cpp`, `render/src/landmarks/inputs.cpp` applyFrameCamera) | the EXE's tapped draws and their constants at the tap's camera | the frame's view-projection replaces the tapped camera rows whole (4882); the wrap copy is chosen by the x translation along the view-projection's row 0, valid at any yaw; a yaw change between the tap and the frame (one frame at 40 deg/s: 1.3 degrees) is drawn at the frame's camera | the EXE culls its own model lists at its own camera (`keepCulled`, 0709): whatever the EXE draws past 20.5 the server draws |
| Units (`srv_units_scene`) | the DLL's unit feed | the server's own pass, frustum only | more units drawn; the EXE's unit **flags** are the EXE's UI (below) |
| Trees (`forest`), cut mask (`cut_tap`), decals (`decal_tap`), ice (`ice_tap`) | per-map data (plot grids, decal sets, the ice mesh) | none: view-independent data | more trees and decals drawn |
| Wrap copies | the EXE's g_NumViews views (view 0 and at most one copy each side, `camera-and-picking.md` 4) | none | a footprint wider than the map would need more than one copy a side; the EXE makes at most three views. The top edge of the view is 1,934 units wide at 20.5 and 3,524 at 24.5 against a Tiny map's 3,583 (`proxy/evidence/0781/footprint.py`): at 24.5 and beyond on Duel and Tiny maps the view can show more than three copies' worth [I] |
| Far plane | far from the EXE (4000 measured at zooms 11 and 14.26) | none | the top corners of the view reach a slant distance of 4,000 at about zoom 24 (4,268 at 24.5): past that the far plane cuts the top of the picture in the EXE and in the server alike unless the EXE moves far with the zoom (section 2.2) |

**Offline in the scene harness [O, 2026-11-25 21:0x-21:4x EST]** (`proxy/evidence/0781/camx_offline.py`: named cameras made
from the Tiny bundle `C5H-0301-TINY_0009_20261119-123934_workstation`'s last camera block, yawed about its ground point
and pulled out along its view axis; `offline_0781.sh` runs sceneshot `0707w` with the shared asset root
`mods548-vt`; pictures on F: only, DIAGNOSTIC, not committed). Twelve named cameras, every one replayed (sceneshot
exit 0):

| Cameras | What the server drew |
|---|---|
| yaw 0, 30, 45, 90 (zoom 14.26) | correct rotated views: terrain, water, the river, borders, fog of war and its clouds, units, the city, resources, trees |
| yaw 180 | terrain, water, borders, fog and units right; **the city, the resources and the trees missing** (their shadows on the ground drawn). The log: `models: camera jump of more than a quarter wrap period ... draws 2 tapped constant(s) at the nearest copy` (also at yaw 90, where they were drawn). Cause [C, `render/src/landmarks/inputs.cpp` wrapCopyOf]: the wrap copy for a tapped constant is chosen from the difference of the tapped and the frame's translation rows along row 0, which assumes the two cameras differ by a move, not a turn; a recorded tap re-aimed by 180 degrees breaks that. Live the tap and the frame differ by at most one frame (1.3 degrees at 40 a second), so this is a limit of yawing a recorded bundle, not of the product [I; the live product arm below] |
| pulled out to zoom 16.9, 18.8, 20.3, 22.5, 24.9 (yaw 0 and 45) | correct at every distance; the fog's clouds cover the unexplored map (turn 8); at 24.9 the view reaches past the map's edge on the right: black, because the recorded block has one view (no wrap copy; live the EXE adds one per side) |

So the server draws a rotated or a farther view of terrain, water, units, borders and fog from the camera block alone;
the tap-fed layers follow the EXE's own camera per frame, so their live behaviour is the product arm's question.

**Live with the patch (run p1, 2026-11-25 21:5x EST) [L]:** `CameraStartRotatingCW` set bit 0x10 for the whole turn (18
of 18 samples) and turned the camera at **+40.2 degrees a second**; at yaw -135.6 the server's picture (shot `p1-rotcw`)
had the terrain, water, the river, borders, the fog's clouds, units, the city's buildings and its banner, resources
and trees in place: the tap-fed models follow a rotated EXE camera, unlike the offline yaw-180 replay. The clamp
behaved as in stock: camera x -1492 outside the box's -1440..-928, RIGHT and LEFT pans and a zoom-out refused, a zoom
in accepted (x -1413, inside). Views: 3 at yaw -135.6 (the rotated frustum crossed both seams of the Tiny map), handled
by the server without a fault line. Not measured: the server's lag during a rotation, and pictures at exactly 90 and
180 degrees.

## 4. Implementation tickets, keys and cost

### 4.1 The keys (D255: each its own key in `VP64Options.ini [Extras]`, offered by the installer, default off)

Both are changes to how the game is controlled and seen, not fixes: they belong under `EnableVisualAdditions` (default
`0`), each with its own line at `0` until the owner says otherwise. Neither changes rules, saves or what is sent to other
players (the camera is local), so they fit the Extras rule. With the patch off neither is used (as every extra).

| Key | Values | What it turns on |
|---|---|---|
| `CameraRotation` | `0` (default) / `1` | Hold-to-rotate keys (proposed `[` and `]`, 40 degrees a second, the EXE's own yaw) and a "north up" key (proposed `\`) that sets the yaw back to 0; the clamp correction of 4.2 B; the server's checks of 4.2 C. Off: no binding, the EXE as stock |
| `CameraZoomOut` | `0` (default), `1` (later `2`) | The zoom maximum raised by one wheel notch to 22.5 (`1`); `0` = stock 20.5. `2` (24.5) only after 4.2 A 3 (c)-(d). Not a free level: 2.2, 2.3 and 3 set the ceiling (the EXE's FOW particles crash off the D33 path, the far plane, the wrap copies, the UI draw list) |

The installer's options page: two checkboxes under the visual additions ("Rotate the camera with [ and ]", "Zoom out
further: one / two more steps"), written only when changed (as the other extras, installer-notes).

### 4.2 Tickets (sizes for one implementer; every one with its docs/OPTIONS.md entry, D255)

**A. `CameraZoomOut` (8-12 h).**
1. The write: GVS+0x13C = 20.5 + 2 x N, a heap float, re-applied whenever it reads 20.5 again (the camera's Reset at
   every load) and only while the GVS is the validated map camera (`exe_camera.h`'s checks; build table symbol
   `cam_app`, offset 0x13C). Data write only (D228 base); on an unknown build it stays off with one log line. Where:
   the proxy's per-frame UI bridge (it already writes EXE heap data for the UI draw list, `proxy_uidraw.cpp`) or the
   composite's Present hook (today read-only by design: 3007's option B); the ticket picks one.
2. The server: read the same key; the distance-fog ramp row (`fogZoomFraction`, `water::zoomFraction`, forest, ice) keeps
   the stock curve (11..20.5, clamped at 1), and the tapped model constants' FogParams.w is replaced by the same value,
   so ground and models use one row; the shadow fade follows the EXE (no change).
3. **The guard and the ceiling.** (a) The write is made only while the EXE's FluidFOW is off (the D33 set, i.e. the
   server draws the fog): with FluidFOW on, the EXE's own FOW Particles pass crashed between 22.5 and 24.3 on a Tiny map
   (2.2), and on a larger, later map it may overrun at a smaller zoom [I]; any other state (patch off, `ServerRenderer =
   0`, a refused build) leaves 20.5 with one log line. (b) The first ticket ships N = 1 (22.5): verified live with the
   patch (2.3: correct picture, no crash, 1.5x the pass time). (c) N = 2 (24.5) needs two things first: the far plane
   (4,000, the EXE's; whether a data write of GVS+0x11C moves it, and what the server's depth and the EXE's UI do with
   it, is untried) and a third wrap copy or a cap by map width (the EXE makes one copy a side; on a Tiny map the view
   outgrows it at 24, and the server then draws black tiles and unexplored terrain without clouds, a fog leak, 2.3). (d)
   The server's fog particles reach their cap (10,240) at 24.5: the clouds thin; a cap that grows with the view or a
   sparser far field belongs to N = 2.
4. Scene-bundle rule A: a bundle member or a gap note for the key (merge check `merge-check-scene-bundle-rule-a`).
5. Live acceptance on a late Huge save (the UI draw list, K4 in `known-crash-causes.md`: the grown buffer holds about
   1,500 unit flags; a 1.8x footprint at 22.5 and 3.3x at 24.5 put more units in view).

**B. `CameraRotation` (10-14 h).**
1. The binding: the patch already ships its own copy of VP's `WorldView.lua` (the installer's `vpfix` payload, D225:
   `(2) Vox Populi/Core Files/Overrides/WorldView.lua` in the 0.1b4 payload); its `DefaultMessageHandler` gets the keys:
   key down fires `Events.CameraStartRotatingCW/CCW`, key up `...Stop...` (and the emergency `KeyUpEvent` handler stops
   both, as it does for the pans), only when the option is on (read from the proxy through its Lua bridge,
   `proxy/src/proxy_lua.cpp`). Keys `[` and `]` collide with nothing in the game's or
   VP's hotkey tables [C, grep]; EUI's own bindings to be checked in the ticket.
2. **The clamp correction (required).** Without it a rotated camera is stuck near the explored area's edges (1.3). Data
   only: each frame while the yaw is not 0 or 180, the patch moves the clamp box's x range (GVS+0x154 / +0x158) by the
   camera's ground-point x offset `z x back.x / back.z`, so the EXE's test of the camera's x is the test of its ground
   point; the box's y side is already right. Needs, before the code: when the EXE rewrites the box (it did not change
   in 25 minutes of reads on a turn-18 save [L]; it should grow with exploration [I]) and that the glide clamp 0x463510
   accepts the moved box. A code-write alternative (add the x offset in 0x47E750 and 0x463510) is allowed only behind
   `FullExeOptimizations` (D228) and is not needed.
3. "North up": a data write of the camera's rotation rows and position about the ground point (the same transform the
   EXE's yaw step applies), or the events for a computed time (imprecise: 40 degrees a second against a frame of 33
   ms); the ticket picks the write.
4. The server: no change found to be needed (section 3); the ticket's live check covers the models at the frame's
   camera during a rotation (the tap is at most one frame old: 1.3 degrees at 40 a second), the lockstep (D242) during
   a rotation, the wrap copy choice, and picking (the EXE's, exact at any yaw).
5. The offline harness: a named camera with a yaw far from the recorded one cannot re-aim the model tap (the cities,
   resources and trees vanished at yaw 180 offline while terrain, water, units, borders and fog were right, 3); the
   ticket records a bundle live while rotated instead of yawing a recorded camera.

**C. Acceptance and cost (6-8 h, laptop and oldpc through the testbed).** Stock against product pictures at yaw 90 and
180 and at the raised maximum (D112, D114), and a PERFORMANCE.md series of the server's pass and the EXE's frame on a
late Huge save at 20.5 against 22.5 and 24.5.

### 4.3 Performance cost of the bigger zoom-out (estimate, to be measured by C)

The cost grows with the ground area in view; the footprint of the camera (`proxy/evidence/0781/footprint.py`: 45-degree
pitch, 29-degree vertical field at 2554 x 1411, area in plots of 64 x 55.43 units):

| zoom | height | plots in view (no map edge) | against 20.5 |
|---|---|---|---|
| 14.26 (load) | 425 | 80 | 0.15 |
| 20.5 (stock maximum) | 1,082 | 520 | 1.00 |
| 22.5 (`CameraZoomOut = 1`) | 1,461 | 948 | 1.82 |
| 24.5 (`CameraZoomOut = 2`) | 1,972 | 1,726 (top corners at the far plane) | 3.32 |

The plot counts at 20.5 match what the server drew live (350-540 terrain tiles zoomed out, `pan-coherence.md` 5.10).
Measured costs to scale from (`pan-coherence.md` 5.8 and 5.9, Tiny saves, not a PERFORMANCE.md series): the server's
pass on the workstation 1.9-2.4 ms in the city view (about 80 plots) and 4.7-6.4 ms in the wide view (about 520), i.e.
about 8 us a plot in view all in (terrain, forest, the fog's particles, models); the laptop's busy time per drawn frame
15.7 ms still in the city view and 23.2 ms in the wide hold, about 17 us a plot.

Measured in this ticket's product arm (2.3, Tiny turn 18, workstation, one run): 1.9-2.3 ms at 20.5, 2.9 ms at 22.5
(x1.4), 3.2-3.4 ms at 24.5 (x1.6). A Tiny map saturates (at 22.5 most of it is in view, and most of it is cloud), so
these are the low end. Scaled by the plot count from the wide-view figures above, the high end for a map large enough
to fill the view [I]:

| | 20.5 (stock) | 22.5 (`CameraZoomOut = 1`) | 24.5 (`2`, later) |
|---|---|---|---|
| server pass, workstation (RTX 4080) | 1.9-6.4 ms | 2.9 ms (Tiny, measured) to about 8-10 ms | 3.2-3.4 ms (Tiny, measured) to about 14-18 ms |
| server busy per frame, laptop | 23.2 ms | about 30 ms | about 43 ms |

So one notch more can cost a laptop-class machine its 30 fps in the wide view under D242's exact lockstep (33.3 ms) on
a large map; two notches cost it clearly; a workstation-class machine stays above 60 fps at one notch. On a late Huge game the per-plot
cost is higher (more units, cities and improvements in view: the laptop's 0689 split had 733 units). Memory: with the
default `BakedTileCameraRadius = -1` the whole map is baked already, so no more video memory; with a radius `r >= 0` the
kept tiles are r rings around a 1.8x / 3.3x larger visible set. The EXE side: more world anchors and unit flags in the
UI draw list (K4), the overlay target's texel density 0.76 / 0.57 texels a unit (borders and highlights softer, as in
the EXE's own drawing). D255 (2): on an 8 GB machine the extra stays off.

## 5. Not verified

- Which event sets which bit is [L] for both (CCW 0x20 in stock, CW 0x10 with the patch); the event-to-bit handlers
  themselves were not found statically (the event strings have no direct code reference in the analysed project).
- The clamp correction of 4.2 B 2 is a design: when the EXE rewrites the clamp box, and whether the glide clamp accepts
  a moved box, are untried. The box did not change in about 50 minutes of reads on one turn-18 save.
- Rotation at exactly 90 and 180 degrees live; a rotation on a Huge or late save; rotation under the lockstep's
  measurements (D242) and the pan coherence.
- Whether a data write of GVS+0x11C (far) moves the far plane, and what the server and the EXE's UI do with a larger far.
- The stock FOW Particles crash: the exact buffer that overruns (the per-frame staging allocator 0x8CFDC0 is named from
  the call, not decoded), the zoom at which it starts on a larger map, and whether FOWLevel 2 avoids it [I]. One crash,
  one save.
- All cost figures are single runs on a loaded workstation (other jobs on the machine) on a Tiny map: no
  PERFORMANCE.md series. The laptop and Huge figures of 4.3 are scaled, not measured.
- The UI draw list (K4) at 22.5 on a late Huge save; EUI's own key bindings against `[`, `]` and `\`.
- Strategic View with a rotated or raised camera (mode 2 has its own cameras; not touched).
- DX9 and Tablet EXEs: not looked at.

## 6. Commands (for the runbook; 0781's touches did not include it)

```
python -I proxy/evidence/0781/camx.py state --pid <game pid> [--out F.jsonl --label L]   # zoom, max, height, yaw, pitch, far, key bits, overlay window
python -I proxy/evidence/0781/camx.py box --pid <game pid>                               # the clamp box against the camera and its ground point
python -I proxy/evidence/0781/camx.py watch --pid <game pid> --seconds 1 --every 50 --out F.jsonl
python -I proxy/evidence/0781/camx.py setmax --pid <game pid> --value 22.5               # TEST ONLY heap write of GVS+0x13C; put 20.5 back
MSYS_NO_PATHCONV=1 python -I tools/harness/lua.py --instance <inst> --state InGame "Events.CameraStartRotatingCW() return 1"   # then ...StopRotatingCW
bash proxy/evidence/0781/drv_0781.sh <inst> launch|full <tag>|stop|install|remove        # the stock / product trial (ARM=, PAN_FIRST=)
python -I proxy/evidence/0781/passcost.py <render.log> <states.jsonl>                    # the server's pass and fog lines per camera state
python -I proxy/evidence/0781/camx_offline.py prep <bundle> <work> NAME:YAW:SCALE ...    # yawed / pulled-out named cameras; then offline_0781.sh <work>
python -I proxy/evidence/0781/footprint.py                                               # plots in view per zoom
```

Traps: with the maximum raised the stock EXE crashed between 22.5 and 24.5 (2.2): a stock arm past 22.5 needs a crash
box plan (D155). Each Lua call took about 13 s in that game, so rotations are timed from the `watch` file. The scene
library's bundle files are read-only: `chmod -R u+w` the working copy before `camx_offline.py prep`. Editing
`drv_0781.sh` while it runs breaks the running copy (bash reads the file as it goes).

Sources: the ticket (`tracker/issues/0781-extra-camera-rotate-zoom.md`); `research/02-exe-internals/camera-and-picking.md`;
`research/sources/shell-gates/CAMERA.md`; `terrain-surface.md` 6, 7, 9; `overlay-system.md` 4; `pan-coherence.md` 5;
`research/03-dll-engine-boundary/upstream-13406-product.md`; evidence scripts `proxy/evidence/0781/`.
