# Whole-map picture export from the server's renderer (idea 0457, stage 1 study)

Ticket 0457 (D169's idea from `research/07-architecture-decisions/architect-2026-11-16-increment-4-extras-epic.md` 4.5,
made a stage-1 task by D275). Written 2026-11-26 by impl-0457 (account A, branch `task-0457-whole-map-export`). No product
code changed. Marks: `[C]` this repository's code or a recorded camera block read by this ticket (the line names it), `[O]`
drawn by the offline scene harness (sceneshot) in this ticket, `[L]` measured live (cited, with its source), `[I]`
inferred. A value cited from another file keeps that file's mark and names it.

The idea: the player (or a tester) asks for one picture of the whole map, drawn by the server's world renderer as the
game would draw it, and gets a PNG file. Stock has nothing like it: the EXE's own whole-map views are the minimap
(`observer-switch-minimap.md`) and the Strategic View (a 2D view the camera block reports as `VPCAM_NOT_MAP`,
`shell/common/shell_shared.h`), neither a 3D picture.

## 0. Short answers

| Question | Answer | Mark |
|---|---|---|
| Can the server's renderer draw from a camera that is not the EXE's? | Yes. Every module takes the frame's `VPShellCamera` block (matrices copied, never rebuilt from angles); sceneshot already draws named camera blocks (`cameras/<name>.vpcam`), and 0781 drew yawed and pulled-out cameras that way | [C] `camera-extras.md` 3, [O] 0781 |
| Which camera? | A **parallel (orthographic) projection at the game's own 45-degree pitch**, cut into tiles: seamless by construction, looks like the game at any one place; a top-down orthographic "atlas" view as the second choice. Not stitched perspective shots (seams), not one far perspective shot (far plane 4,000, distance fog, LOD) | [I], section 4; [O] section 9 |
| Does the renderer accept an orthographic block? | **Yes, with four faults to fix for an export**: terrain, relief, rivers, decals, trees, borders, units, the fog mask and clouds draw at the game's look, and two tiles join without a seam; but the tapped models (cities) are refused by their wrap-copy rule, the cloud planes stop at 2,048 blocks a view (a fog leak past them), the ground past the wrap seam needs a wrap copy, and the eye must be far (or over the tile) or the water turns white | [O] section 9, [C] the causes |
| Size limits | D3D11: 16,384 texels a side, and one resource at most max(128 MB, a quarter of video memory) (`d3d11.h` of SDK 10.0.26100: `D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION` 16384, `..._RESOURCE_SIZE_IN_MEGABYTES_EXPRESSION_A_TERM` 128, `_B_TERM` 0.25). So a picture larger than about 4,096 x 4,096 is drawn in tiles. A huge map at the bake's own 4 pixels a world unit is 32,768 x 14,369 px (471 Mpx) oblique; at 2 px a unit 16,384 x 7,185 (118 Mpx) | [C] header; sizes [I] by the lattice, section 5 |
| Layers ready for the whole map today | Terrain (whole-map bake by default), farm fields, decals and roads, trees, water and rivers, ice, units, the fog of war (built from the server's own plot states) | [C] `architect-2026-11-25-retained-world.md` 4 table, code read |
| Layers that are not | **Cities, landmarks, resources and farm buildings** (the EXE's draws, tapped: known only where the EXE's camera has been since the load, through 0789's store); **the overlay** (borders, grid, yields: the EXE's texture; 0793's whole-map history holds only ground the camera covered, at 0.58-1 texel a unit); **shadows** (the EXE's shadow map, around its camera only) | [C] same table, `model-store.md` 1, `overlay-history.md` 2 |
| Fog | The picture shows what the active player has seen (the fog mask from `PlotRecord::fow`); no "reveal all" in a player's build (it would leak the map in multiplayer, the D33 leak of `fog-of-war.md` 1 in another form) | [I], design |
| Cost | GPU about 1-3 s and PNG encoding 10-60 s for a huge map at 2-4 px a unit; CPU memory one band of tiles (128-512 MB); no extra video memory beyond one tile target (64-128 MB) while the bake stays whole-map | [I], section 8; offline [O] figures in section 9 |
| Where it runs | Stage 2 as a developer tool on the offline harness (no product risk); stage 3 in the product as an extra, a key under `[Extras]` default 0, triggered from Lua, drawn on the render thread in slices so the EXE's frames under D242's exact lockstep are held at most one slice a frame | [I], section 7 |
| Step tickets | Five, drafted in section 10: E1 ready (offline export tool with the four renderer fixes), E2 ready (the missing layers on a late map, by data, no camera sweep), E3 blocked on E1 + E2 (product path), E4 blocked on E3 (options, installer, docs, the owner's acceptance), E5 blocked on 0594's study + E1 (massive maps, tile pyramid) | section 10 |

## 1. What "the whole map" is, in world units [C]

- The world's x axis is the plot lattice: plot centres 64 units apart, rows 55.43 apart (64 x 0.866), odd rows shifted 32
  (`research/02-exe-internals/camera-and-picking.md` 5). On a wrapping map one wrap period is `64 W - 1` units
  (`VPShellCamera::wrapPeriod`, `shell_shared.h`).
- The world box of the camera block covers the grid's `H + 4` rows: on the Tiny library bundle
  (`C5H-0301-TINY_0009_20261119-123934_workstation`, its last camera block, read by this ticket's `mapcams.py`)
  `aabbMin` = (-1791.5, -1108.047, 0), `aabbMax` = (1791.5, 1108.047, 255), `wrapPeriod` 3583, `wrapX` 1: 3,583 x 2,216.1
  units = 56 x 64 - 1 by 40 x 55.40, i.e. a 56 x 36 map plus the grid's 4 margin rows [C, block read 2026-11-26]. The
  origin is the map centre (`render/include/c5r/fow/fow.h` 127).
- The same block: viewport 2554 x 1411, fov 29.01 degrees, aspect 1.810, near 50, far 4,000, camera at height 424.26
  with right (1, 0, 0), up (0, 0.707, 0.707), back (0, -0.707, 0.707): the 45-degree pitch at the load zoom [C].
- The terrain is drawn from per-tile caches of 256 x 256 BC1 texels (6 mips, 43,680 B), one tile per plot
  (`terrain-bake-residency.md` table, `terrain-bake-records.md` t8 row [C cited]): **4.0 texels a world unit in x and 4.6
  in y**. A picture denser than that magnifies the cache; stock and the server then use the 512 "near pair" up-res
  (`extras.md` X-section on 4937 [C cited]).
- What the player sees at the load zoom: at the screen centre the view is about 562 units wide on 2,554 px, about 4.5 px a
  unit; at the zoom maximum 20.5 (height 1,082) about 1.8 px a unit [I: the camera geometry above, slant distance
  height / sin 45, horizontal field 2 atan(aspect x tan(fov / 2)) = 50.2 degrees]. **So 4 px a unit is "the game's own
  detail" and 2 px a unit "zoomed out".**

## 2. What exists already

| Piece | Where | What it gives the export | Mark |
|---|---|---|---|
| The offline player `sceneshot` | `server/tools/sceneshot/` (README), `research/06-tooling-and-workflows/offline-scene-harness.md` 16 | draws a recorded bundle frame, or a named camera block, with the same render units `server.exe` links; writes PNG through WIC and raw depth; target size = the recorded frame's size (`tg.ensure(dev, fr.width, fr.height)`, `sceneshot.cpp` 2574) | [C] |
| Named camera blocks made by script | `proxy/evidence/0781/camx_offline.py` (yaw, pull-out), `render/evidence/0454/4933/bundlecams.py` | the way to give the player any camera: a 712-byte `VPShellCamera` in `cameras/<name>.vpcam` registered in `scene.json` | [C] |
| Terrain module with an orthographic target | `render/src/terrain/terrain.cpp`; test `game_l1_surface_at_vertices_huge` draws the huge field "an orthographic target with one pixel per sample" (`terrain-surface.md` 7) | the terrain surface already draws under a parallel projection in a test; `Options::lodLevel` 0..4 fixes the LOD level (the EXE rule `exeFrameLod` picks it from the nearest visible tile) | [C cited] |
| Whole-map bake | `BakedTileCameraRadius = -1` default (D88): huge 4,922 land tiles, 410.1 MB of BC1 caches, 1.18-2.12 s of baker GPU time for all (`extras.md`, 4937) | every land tile's colour is resident; no bake work for an export | [L cited] |
| Ground footprint from any view-projection | `c5r::fow::footprintFromCamera`, `windowFromCamera` (`fow.h` 127-148): the four frustum corner rays of the inverse view-projection cut with z = 0 and z = 127.5 | projection-agnostic (rays from the near to the far corners are parallel under an orthographic matrix, still not parallel to the ground) | [C header; I for orthographic] |
| Fog mask from the server's own plot states | `fog-of-war.md` 1, 7.1 (FOWDrawMask's rule, `PlotRecord::fow`) | a mask for any window, the whole map included; 1024 x 1024 per window | [C cited] |
| Overlay history (0793) | `render/src/fow/resample.cpp` `OverlayResampler`, `overlay-history.md` 2 | the EXE's overlay (borders, grid, yields, lens tints) for every piece of ground any received overlay covered since the load, 1 texel a unit (Tiny) to 0.58 (huge), at most 80 MB; can be resampled into any window | [C cited] |
| Model store (0789) | `server/src/render/landmarks/srv_models_scene.*`, `model-store.md` 1 | every city, landmark, resource and farm-building instance the EXE drew since the load, with its own copy, drawn at the frame's camera | [C cited] |
| Unit scene | `server/src/units/`, `unit-server-scene.md` | the whole held unit set from the DLL's feed, culled at the frame's camera only | [C cited] |
| Trees, decals, ice, water mesh | `architect-2026-11-25-retained-world.md` 4 table | per-map data; trees built per plot on first view (frustum + 64) | [C cited] |
| A Lua trigger path | `proxy/src/proxy_lua.cpp` (the Lua bridge; the `Exe` table of VP 5.5 answered by the proxy, ticket 0765); the patch's own `WorldView.lua` override (`camera-extras.md` 4.2 B1) | a key or a menu entry can reach the proxy, which already talks to the server | [C] |
| Options file and Extras rules | `docs/OPTIONS.md`, `[Extras]`, D169/D255 (one key per feature, visual additions default 0) | where the product key goes | [C cited] |

Nothing in `server/src` exports a picture today (no screenshot or export function; a grep for "screenshot", "whole map",
"export" finds only the DLL's export lookups, 2026-11-26) [C].

## 3. The layers, one by one, for a whole-map picture

| Layer | Source | Whole map today? | Gap and the cheapest way to close it | Mark |
|---|---|---|---|---|
| Terrain colour and relief | server bake + EXE height field tapped once per load | yes (radius -1); with a radius `r >= 0` only rings around the camera | export bakes missing tiles on demand (1.2 ms GPU a tile [I 3023]) or refuses with one line when the radius is not -1 | [C cited] |
| Terrain LOD | `exeFrameLod` (nearest visible tile) | per frame | export fixes level 0 (`Options::lodLevel = 0`) so every tile has full relief; the rule's projected-size test is a perspective rule | [C cited], [I] |
| Farm fields, roads, decals | server's own, baked into the tiles | yes | none | [C cited] |
| Water, rivers, coast waves | mesh copied once per load; footprint from the camera | yes | the coast waves carry a clock state (`offline-scene-harness.md` 0): export takes the frame's state as it is | [C cited] |
| Trees | server records, built per plot on first view | yes, built on first export (a cost at the first export) | none | [C cited] |
| Polar ice | ice tap mesh, per map | yes | none | [C cited] |
| Units | DLL feed, whole held set | yes | they animate: a sliced export (section 7) draws tiles at different instants; a unit crossing a tile edge in between is cut or doubled. Fix: export while no unit moves (the player's own turn, idle), or one atomic pass offline | [I] |
| Fog of war (mask, cloud planes, particles) | server's plot states | yes; per window | mask per export tile window; cloud planes per tile (stock's rule: per 64-sample block of the view's footprint, at most 2,048 a view, `fow.h` 288); the FOWLevel-3 particles are a random simulation capped at 10,240 a view: off in an export, or a fixed seed | [C cited], [I] |
| Distance fog | `fogZoomFraction(cameraHeight, 11, 20.5)` in terrain, water, forest, ice (`render/src/terrain/terrain.cpp` 654, `water.cpp` 222, `forest/draw.cpp` 372, `ice.cpp` 351) | n/a | the export camera's eye height sets the haze row: an eye at the load-zoom height (424) with a negative near plane gives the load-zoom row; an eye far up gives the haziest row (section 9) | [C] grep 2026-11-26 |
| Overlay (borders, grid, yields, lens) | EXE texture, 1 frame old | **no**: only the ground the camera covered since the load (0793's history), coarser than live (0.58-1 texel a unit against 1.6-3.6) | (a) draw from the history, mark uncovered ground; (b) the server draws borders itself from plot ownership in the feed (a new module; the EXE's border look is a texture program, `overlay-system.md`); (c) leave the overlay out (default of a "clean map" option). Recommend (a) + (c) for stage 3, (b) only if the owner wants borders on unseen-by-camera ground | [C cited], [I] |
| Cities, landmarks, resources, wonders, farm buildings | the EXE's draws, tapped (D231, 0561) | **no**: only instances in the model store (seen by the EXE's camera since the load) | (a) draw the store, mark the rest; (b) the server's own city layout (`city-renderer.md`: the `own` layout is parked); (c) on a test instance only, sweep the EXE's camera over the map before the export so the store fills (a developer tool, never in the product: the player would see the camera fly). E2 measures how much of a late map the store holds after a normal session | [C cited], [I] |
| Shadows | EXE shadow map, ShadowMatrix, fade (`shadow_tap`) | **no**: the shadow map covers the EXE camera's shadow frustum | export draws without shadows (consistent over the whole picture); a server shadow pass per tile is a later choice | [C cited `camera-extras.md` 3], [I] |
| Water reflection | an orthographic camera along a fixed direction, drawing the paired tap's models (`water-system.md` 13.6) | as the models | follows the models' gap | [C cited] |
| Interface (banners, flags, icons) | the EXE's UI | not the renderer's | out of scope: the export is the world only (D80: the UI stays in the EXE) | [C cited] |

## 4. The camera

Four ways to put a whole map into one picture:

| Camera | Seams | Looks like the game | Problems |
|---|---|---|---|
| **A. Parallel projection at the game's pitch (45 degrees), tiled** | none: each tile is a sub-rectangle of one orthographic volume | yes at any one place (same angle, same models), without the perspective's shrinking toward the top | the ground's y axis is shortened by sin 45 = 0.707 (as on screen); specular and the distance fog need a chosen eye (below) |
| B. Top-down parallel projection, tiled | none | a map, not the game's view | tree cards and models seen from above; cloud planes flat; the most "atlas"-like |
| C. Perspective shots stitched | at every edge (each shot has its own vanishing point) | at each shot's centre | unusable for one picture without reprojection |
| D. One perspective shot from far away | none | no | the far plane 4,000 cuts the ground past zoom about 24 (`camera-extras.md` 2.3); the distance fog at its haziest; LOD at its coarsest; 1.8 px a unit at best |

Recommendation: **A as the default, B as an option.** Details of A [I, to be confirmed by E1 beyond section 9's trial]:
- The block: `camWorld` rows = the game's right (1, 0, 0), up (0, 0.707, 0.707), back (0, -0.707, 0.707) (yaw 0, as the
  load camera), position = the tile's ground centre + back x D; `view` = its inverse; `proj` = an off-centre
  orthographic matrix (D3DX row-major, `OrthoOffCenterRH`: depth 0 at the near plane, 1 at the far plane, the same
  convention as the EXE's PerspectiveFovRH); `numViews` = 1, or 2-3 on a wrapping map for the tiles at the seam (copies
  shifted by +-`wrapPeriod`, as the EXE builds them, `camera-and-picking.md` 4); `nearZ`/`farZ` written to match.
- The eye: under a parallel projection the eye position does not change what is visible, only what the shaders compute
  from it: the distance-fog row (from its height) and the view vector for specular and the water. **One eye for the
  whole export, far back along the view axis, and an off-centre orthographic projection per tile** (the trial's
  `seamL`/`seamR`: eye 6,000 back, near 50, far 12,000; no seam, water right, section 9). An eye near the ground and far
  to the side of a tile turns the water white (the trial's `obl45`); an eye per tile would change the view vectors from
  tile to tile. The far eye puts the distance fog at its haziest row: E1 checks whether that shows and, if it does,
  passes the export's own row (`fog.w >= 0`, which the modules take as given) [O, I].
- Density: 4 px a unit (the bake's texel density and the game's load-zoom detail) as "full detail"; 2 px a unit as the
  default for the product (a quarter of the pixels, the picture is still sharper than the game zoomed out).
- Yaw: 0 only (north up). A rotated export would need the model tap re-aimed, which a recorded tap cannot do
  (`camera-extras.md` 3, the yaw-180 limit).

## 5. Tiling and size limits

- **Per tile**: a D3D11 texture is at most 16,384 a side and one resource at most max(128 MB, video memory / 4) [C
  header]. An RGBA8 4,096 x 4,096 target is 64 MB plus a 64 MB depth target (D32 or D24S8); the export keeps one tile
  target, one staging copy for the readback, and the renderer's own per-window targets (fog mask 1024 x 1024, overlay
  window) [I]. Tile 4,096 x 4,096 at 4 px a unit covers 1,024 x 1,448 ground units (oblique): about 16 x 26 plots.
- **Picture sizes** (world box `64 W` by `55.43 (H + 4)` units; oblique height `0.707 x 55.43 (H + 4) + 300` units of
  relief and margin) [I, by the lattice of section 1; Tiny and huge dimensions [C], the others the standard sizes]:

| Map | Plots | Oblique 1 px/unit | Oblique 2 px/unit | Oblique 4 px/unit |
|---|---|---|---|---|
| Duel | 40 x 24 | 2,560 x 1,397 (4 Mpx) | 5,120 x 2,795 (14 Mpx) | 10,240 x 5,590 (57 Mpx) |
| Tiny | 56 x 36 | 3,584 x 1,868 (7 Mpx) | 7,168 x 3,736 (27 Mpx) | 14,336 x 7,471 (107 Mpx) |
| Small | 66 x 42 | 4,224 x 2,103 (9 Mpx) | 8,448 x 4,206 (36 Mpx) | 16,896 x 8,412 (142 Mpx) |
| Standard | 80 x 52 | 5,120 x 2,495 (13 Mpx) | 10,240 x 4,990 (51 Mpx) | 20,480 x 9,980 (204 Mpx) |
| Large | 104 x 64 | 6,656 x 2,965 (20 Mpx) | 13,312 x 5,930 (79 Mpx) | 26,624 x 11,861 (316 Mpx) |
| Huge | 128 x 80 | 8,192 x 3,592 (29 Mpx) | 16,384 x 7,185 (118 Mpx) | 32,768 x 14,369 (471 Mpx) |
| Massive maps idea (0594) | 1,000 x 500 | 64,000 x 20,054 (1.3 Gpx) | 128,000 x 40,108 (5.1 Gpx) | 256,000 x 80,216 (20.5 Gpx) |

  Top-down (B) is taller: `55.43 (H + 4)` units, e.g. huge at 4 px a unit 32,768 x 18,624 (610 Mpx).
- **The file**: sceneshot's `WritePng` (WIC, `GUID_ContainerFormatPng`) writes a whole picture in one `WritePixels` call
  with a 32-bit byte count (`w * h * 4`) [C, `sceneshot.cpp` 966-998]: at most 4 GB, i.e. about 1 Gpx; WIC accepts
  successive `WritePixels` calls of row bands [I, the WIC contract], so the export writes band by band (one row of tiles)
  and never holds the whole picture. PNG itself allows 2^31 - 1 a side; common viewers and browsers fail far earlier
  (tens of thousands of pixels a side, or a few hundred Mpx) [I]. So: up to about 16,384 px wide one PNG; above that the
  export also writes a tile pyramid (a folder of 256-px tiles and one HTML page, the "deep zoom" form) or caps the
  density [I, E4 decides]. The massive-maps idea needs the pyramid at any density (E5).
- **CPU memory**: one band = picture width x tile height x 4 B: huge at 4 px a unit with 4,096-row tiles 512 MB, with
  1,024-row tiles 128 MB [I]. The server is 64-bit (no address-space limit); the EXE is untouched.

## 6. Fog, visibility and options

- **Whose map**: the active player's, with that player's fog of war: unexplored plots under the clouds, seen plots in
  the seen tint, units only where visible (the unit scene's own filter). This is what the player could screenshot by
  panning. A "whole map revealed" export is a cheat in multiplayer and on a hot-seat game: not in the product; a
  developer build or the offline tool may draw it (the server's fog off) for testing [I, design].
- **Options for the product key** (E4 proposes the names; one key per feature, D255; visual addition, default 0):
  density (1 / 2 / 4 px a unit, default 2), projection (oblique / top-down), layers (units on/off, overlay on/off as a
  "clean map", grid off by default), output (PNG, or PNG + tile pyramid above a size), folder.
- **What stays different from stock** (D66 asks every look-alike to name itself): no perspective; no shadows; the
  overlay coarser where it comes from the history; models only where the EXE drew them since the load (section 3); no
  fog particles. Each is named on the picture's sidecar JSON.

## 7. Where the export runs, and when

- **Offline (stage 2, E1)**: a scene bundle recorded live (the recorder's test keys, `offline-scene-harness.md` 15)
  holds every input the export reads; sceneshot (or a sibling tool linking the same units) draws the tiles at chosen
  cameras with a chosen target size. No product code, no risk to play, repeatable; it shows exactly which layers are
  missing on a real late save. The bundle's model tap and overlay hold only the recorded frames' windows, so the offline
  export shows the gaps of section 3 at their worst (no store history beyond the span) [C: the bundle members,
  `offline-scene-harness.md` 2].
- **Live (stage 3, E3)**: in `server.exe` on the render thread. Under D242 every EXE frame waits for the server's frame
  (exact lockstep; only a dead or hung server ends the wait). An export of N tiles drawn in one go stalls the game for
  N x (tile draw + readback) plus the PNG time if done on that thread: seconds on a huge map. So: **one tile per frame
  between the frame's own work** (a budget of a few ms), the readback mapped later without waiting (the composite's
  DO_NOT_WAIT pattern, `water-system.md` 9.5), the PNG encoded on a worker thread. A 32-tile huge export then takes 32
  frames (about 0.5 s at 60 fps) plus the encoding in the background [I]. Units animate between slices (section 3):
  the trigger is allowed only on the player's own turn with no unit moving, or the unit scene's state is frozen for the
  slices (E3 decides, by measurement).
- **Trigger**: a key in the patch's own `WorldView.lua` override (as 0781's rotation keys) or a button in the patch's
  options UI, calling a function the proxy registers (the Lua bridge); the proxy passes the request to the server on the
  existing channel. The server answers with the file's path for one line of in-game text [I].
- **Output folder**: beside the game's own screenshots or under the patch's folder; which folder the EXE uses for its
  screenshots is not established here [I, E4].
- **Headless** (no EXE): not possible with today's inputs: the height field, the water mesh, the overlay and the model
  taps all come from the EXE [C, `offline-scene-harness.md` 3].

## 8. Cost (estimates; E1 measures)

- **GPU per tile**: the server's pass costs about 8 us a plot in view on the workstation and 17 us on the laptop at
  screen size (`camera-extras.md` 4.3, from `pan-coherence.md` 5.8-5.9, not a PERFORMANCE.md series); a 4,096 x 4,096
  tile at 4 px a unit holds about 416 plots and 4.7x the pixels of a 2,554 x 1,411 frame: about 5-30 ms a tile [I]. Huge
  at 4 px a unit, 32 tiles: about 0.2-1 s of GPU; at 2 px a unit 8 tiles [I].
- **Readback**: 64 MB a 4,096 tile, a few ms over PCIe 4 [I].
- **PNG**: WIC's PNG encoder at an assumed 30-100 MB/s of raw pixels: huge at 2 px a unit (470 MB raw) 5-15 s, at 4 px
  a unit (1.9 GB raw) 20-60 s; the file a third to a half of the raw size [I].
- **Memory**: video memory one tile target and its staging copy (about 200 MB at 4,096) while the bake stays
  whole-map; CPU one band (128-512 MB); trees built for the whole map on the first export [I].
- **Play**: offline none; live one slice a frame (E3 sets the budget so a frame's exact lockstep wait grows by at most
  a few ms) [I].

## 9. The offline trial (this ticket, 2026-11-26 21:00-21:16 EST) [O]

**Set-up.** sceneshot built from this branch (main 941528a24 + the study) into `C:/c5-64bit-work/0457/ss` (clang-cl,
plain, 5 min, smoke OK). The Tiny library bundle `C5H-0301-TINY_0009_20261119-123934_workstation` (format 9, map 56 x 40
tiles, turn 8, 8 frames from 893; recorded on the workstation's RTX 4080) copied to `C:/c5-64bit-work/0457/tiny/bundle`
(the library copy is read-only: `chmod -R u+w` on the copy) and given named camera blocks by the scratch script
`C:/c5-64bit-work/0457/mapcams.py` (not committed: outside this ticket's touches; its rules are in the table below and
section 4). Every block keeps the recorded block's fields except `camWorld`, `view[0]` (`numViews` = 1, no wrap copy),
`proj` (orthographic, D3DX row-major, `OrthoRH` / `OrthoOffCenterRH` form) and `nearZ`/`farZ`. Asset root `mods548-vt`
(VT present), `--allow-other-exe` (the bundle's EXE pin against the install's EXE, as 0781 ran), the bundle's adapter.
Pictures DIAGNOSTIC (D112), in Visual Checks under issue 0457 (vc-20261126-210858-workstation-ae60, -210900-...-c244,
-210902-...-a504, -210905-...-71de, -211603-...-7521); on C: only, not committed.

| Camera | Projection | Ground covered | px a unit | Eye | near / far | sceneshot |
|---|---|---|---|---|---|---|
| `obl45` | orthographic, the recorded orientation (yaw 0, pitch 45) | the whole map: 3,583 x 1,979 view units, centred on the world box | 0.713 | 600 back from the map centre (height 434) | -6,000 / 6,000 | exit 0 |
| `obl45far` | the same | the same | 0.713 | 6,000 back (height 4,253) | 50 / 12,000 | exit 0 |
| `top` | orthographic, straight down | 4,011 x 2,216 (fit to the frame) | 0.637 | 424 up | -6,000 / 6,000 | exit 0 |
| `tile4` | orthographic, pitch 45 | 638 x 353 around the recorded ground point (1504, -219) | 4.000 | 600 back from that point | -6,000 / 6,000 | exit 0 |
| `seamL`, `seamR` | off-centre orthographic halves of one 1,277-unit-wide volume, one shared eye | 638 x 353 each, side by side | 4.000 | 6,000 back from the recorded ground point, shared | 50 / 12,000 | exit 0 each |

**What the server drew** (the pictures and each run's `sceneshot.log`):
- **The renderer takes an orthographic block.** Every run exits 0; the terrain (all 1,352 land tiles baked: 109.4 MB of
  caches, 370.6 ms of baker GPU time), relief, rivers, decals, trees, the culture border, units (22 held, drawn where
  visible), the fog mask and the cloud planes are drawn under the parallel projection, at the game's look
  (`tile4`, `seamL|R`: picture pass). The fog's mask window is made from the orthographic camera without complaint:
  `camera window [-1856, 1792] x [-1600, 1472]` for `obl45`, `[1088, 1856] x [-704, 64]` for `tile4` (0674's
  `windowFromCamera`) [O].
- **Tiles join without a seam.** `seamL | seamR` stitched to 5,108 x 1,411 (`C:/c5-64bit-work/0457/seam.py`): the mean
  RGB step across the joint equals the step between neighbouring columns inside the tiles in every row band (rows 0-299:
  0.91 against 0.48; 300-1,099, the island: 6.28 against 6.66; 1,100-1,410: 0.70 against 0.69); nothing visible in a
  1,000 x 800 crop at full size. One shared eye with off-centre projections is the construction to use [O].
- **The eye matters for water.** With the eye 600 back from the map centre (`obl45`, `top`) the lake 1,500 units to the
  side is drawn white; with the eye over the tile (`tile4`) or far back (`obl45far`, `seamL|R`) it is blue. The water's
  shading takes the view vector from the eye; a parallel projection needs the eye far (near-parallel view vectors) or
  over each tile. The distance-fog row (`fogZoomFraction` of the eye's height, section 3) is at its haziest with the far
  eye; no difference was visible between `tile4` (height 434) and `seamL` (height 4,253) at this scale, not measured
  [O; I for the cause].
- **Cities and other tapped models are not drawn** at any of the six cameras: `LOUD: models: 2 tapped constant(s) of the
  scene pass lie more than two wrap periods from the frame's camera ... not drawn` (the city's shadow, from the shadow tap,
  is drawn). Cause [C, `render/src/landmarks/inputs.cpp` `tapStep` / `wrapCopyOf` 463-484]: the wrap copy is chosen from
  `d = (tap row3[j] - frame row3[j]) / frame row0[j]` of the two view-projections, which is a camera step only when the
  two matrices share the projection; with an orthographic frame and a perspective tap `d` is no step at all and the copy
  is "unfit". 0781 met the same rule with a 180-degree yaw (`camera-extras.md` 3). An export must choose the copy from
  the instance's world origin (the store keeps origins, `model-store.md` 1) [C].
- **The fog's cloud planes stop at their cap.** In `obl45` the clouds cover the left 68 % of the picture and in `top`
  a middle band; past that, unexplored terrain is drawn under only the thin unexplored tint (a fog leak) and unexplored
  ocean is black. The plane pass takes "one quad per 64-sample block of the view's footprint, at most 2,048 a view"
  (`render/include/c5r/fow/fow.h` 288-289 [C]); the whole-map footprints hold about 56 x 56 (`obl45`) and 56 x 43 (`top`)
  blocks, so 2,048 stop after about 37 and 48 columns, as drawn [O; I for the column order]. An export tile must hold at
  most about 2,000 blocks of 64 x 55.4 units (about 7 M square units of footprint: at 4 px a unit a 4,096-px tile is
  1,024 x 1,448 units = 16 x 26 blocks, well inside) or raise the cap for the export [I].
- **No wrap copy, no ground past the seam.** `tile4` and `seamR` reach past x = 1,791.5 (the map's wrap seam): that strip
  shows terrain under the unexplored tint without clouds (the height pages wrap, the fog does not). The export gives the
  tiles at the seam a wrap copy (`numViews` 2-3), as the EXE does [O; C for the EXE's rule, `camera-and-picking.md` 4].
- **Black unexplored ocean** outside the clouds: no colour at all there in every whole-map shot [O]; cause not traced
  (the ocean draw's own footprint rule or 0665's hidden open-sea tiles, `fog-after-camera-jump.md`) [I]. Under the clouds
  stock never shows it; it matters only where the cap or a wrap gap lets it through.
- **Times (indications, not product figures: PERFORMANCE.md):** each run 59-64 s wall, of which the replay set-up is
  most (the units' history of 485 frames 19-20 s, the model layer's open 17-19 s); the shot itself (settle, draw,
  readback, PNG) 3.1-3.6 s at 2,554 x 1,411, settle 2.6-3.0 s [O]. The draw alone is not split out by sceneshot.

**What the trial does not show**: a huge map; a target size other than the recorded frame's (sceneshot draws at
`fr.width x fr.height`); the overlay (the Tiny bundle has `render_overlay=0`: "no overlay (terrain t1 and water t9
transparent)"); fog off (`--set render_fow=0` refuses the replay: exit 8, "feed record 0 (PLOTS of the fog) was not read
by the drain before it"); several views; a live server.

## 10. Step tickets (drafts for the MC; ids to be given by the MC, COMMON rule 26)

Each is filed ready or blocked with a blocker (D274), priority 1 (D275: idea work, only when a worker place is free),
parent 0006, increment 4, `answers: MC`, `trust: any`, kind task. Hours [I].

**E1. Offline whole-map export tool on the scene harness** (ready; machines: workstation; needs a GPU and the scene
library).
- title: "Idea 0457, step 1: export the whole map of a scene bundle to one picture, offline"
- touches: `server/tools/sceneshot/` (a `--map-export` mode or a sibling tool linking the same objects), its README,
  `render/src/landmarks/inputs.cpp` (the wrap copy from the instance origin when the projections differ),
  `render/src/fow/` (the plane cap per view, only if the export raises it), `research/05-graphics-composition/idea-0457-whole-map-export.md`,
  `research/06-tooling-and-workflows/runbook.md` (4.2), the ticket.
- acceptance: the tool builds the camera blocks itself (one shared eye far back, off-centre orthographic tiles, wrap
  copies at the seam, oblique 45 degrees or top-down), draws at a chosen tile size (not the recorded frame's), streams
  the PNG band by band (WIC `WritePixels` per band) and writes a sidecar JSON (density, projection, tiles, the layers
  drawn and missing with counts); the whole Tiny and HUGE_0350 library bundles exported at 2 and 4 px a unit with no
  seam (the joint step check of section 9), no fog leak (no unexplored ground without clouds), the city models drawn
  where the bundle's tap holds them; the scene-bundle tests and `--dumped --compare` P1 unchanged on every library
  bundle (a perspective frame draws byte-identical); a negative test for each refusal (bad density, a tile over 16,384,
  a picture over the band limit, a bundle without a field); sizes and times recorded here; DIAGNOSTIC pictures in Visual
  Checks; nothing in `server.exe`'s default behaviour changes (merge check rule A: no new default drawing).
- size 12-20 h.

**E2. The missing layers on a late map, by data** (ready; machines: workstation; a Goldberg instance and a late test
save, never the owner's saves).
- title: "Idea 0457, step 2: how much of a late map the model store and the overlay history hold, and what closes the rest"
- touches: `research/05-graphics-composition/idea-0457-whole-map-export.md`, the ticket, an evidence folder of its own.
- acceptance: on a late huge test save with the product's options, after a load and N minutes of normal camera use
  (scripted pans by the harness, no camera sweep of the whole map), the share of the map's cities, landmarks, resources
  and farm buildings that 0789's store holds and the share of the map's ground 0793's history covers, at three N; what the
  server's own city layout (`city-renderer.md`, the parked `own` layout) and a server-drawn border from plot ownership
  would cost; a recommendation per layer (draw what is held and mark the rest / the server's own / leave out), [L] and
  [C] marked.
- size 6-10 h.

**E3. The export in the product** (blocked; blocker: "E1 merged and E2's recommendation accepted by the MC").
- title: "Idea 0457, step 3: a whole-map picture from the running game (server.exe, sliced on the render thread)"
- touches: `server/src/render/` (an export job on the render thread: one tile a frame inside a budget, readback mapped
  without waiting, PNG on a worker thread), `proxy/src/` (the Lua function and the request on the existing channel),
  the patch's `WorldView.lua` override (a key, as `camera-extras.md` 4.2 B1), `server/src/vp64_options*` (the key), tests.
- acceptance: the key (proposed `[Extras] WholeMapExport`, a visual addition, default 0, D255) off: nothing changes
  (scene tests and P1 byte-equal); on: one key press on the player's turn writes the picture and one line of in-game
  text with its path; the EXE's frame time under D242 measured with the performance team's tools against the key off
  (PERFORMANCE.md series on the testbed or a scheduled window), each slice within the budget E3 states; units frozen or
  the trigger refused while a unit moves (E3 decides by measurement); no fog leak (the active player's fog only; no
  reveal in a player build); a live smoke on a Goldberg instance (merge check live smoke for product-path hooks).
- size 20-30 h.

**E4. Options, installer, documentation and the owner's acceptance** (blocked; blocker: "E3 merged").
- title: "Idea 0457, step 4: the export's options, installer entry, OPTIONS.md, and the owner's acceptance"
- touches: `docs/OPTIONS.md`, the installer's options page, `vp64helper`'s line, the ticket, the study.
- acceptance: density / projection / layers / folder keys as E3 left them, documented; above 16,384 px wide a tile
  pyramid with one HTML page beside the PNG (or a capped density, the MC's choice); the output folder settled (beside the
  game's screenshots or the patch's folder, read from the EXE's own behaviour); ACCEPTANCE pictures on the full shipped
  system (D112) of a Tiny and a huge game for the owner, who promotes or keeps the default.
- size 8-12 h.

**E5. Massive maps** (blocked; blocker: "0594's stage-1 study accepted and E1 merged").
- title: "Idea 0457, step 5: whole-map pictures of massive maps (tile pyramid only)"
- touches: the export tool of E1, the study.
- acceptance: a 1,000 x 500 map (0594's test map) exported as a tile pyramid at 1 and 2 px a unit within a stated
  memory bound (no whole picture in memory, section 5), time and disk recorded.
- size 6-10 h.

## 11. Not verified

- The whole trial is one Tiny bundle at turn 8 on one adapter; nothing on a huge map, nothing live.
- That WIC's PNG encoder accepts successive `WritePixels` bands for a picture above 4 GB raw, and its speed (section 5,
  8 [I]).
- The GPU time of a 4,096 x 4,096 tile (section 8 [I]); sceneshot does not split the draw from the settle.
- The black unexplored ocean's cause; whether the distance-fog row changes the look at a far eye (section 9).
- Which layers a real late game is missing (E2).
- The folder the EXE writes its own screenshots to.
- The picture sizes of Small, Standard and Large maps assume the standard map dimensions; map scripts make other sizes
  (Communitu 87 x 59 on Large is in the knowledge base).

## 12. Commands (this ticket, 2026-11-26; the runbook is outside this ticket's touches, so E1 adds them there)

```
python -I perf/quietlock/quietlock.py run --who impl-0457 --what "0457 sceneshot build" -- python -I server/tools/sceneshot/build_sceneshot.py --out C:/c5-64bit-work/0457/ss --smoke      # 5 min, clang-cl plain
cp -r <library bundle> <work>/bundle && chmod -R u+w <work>/bundle                         # the library copy is read-only
python -I C:/c5-64bit-work/0457/mapcams.py prep <library bundle> <work>                    # named orthographic blocks (scratch)
python -I perf/quietlock/quietlock.py run --who <t> --what "sceneshot cam" -- C:/c5-64bit-work/0457/ss/bin/sceneshot.exe --bundle <work>/bundle --camera <name> --out <work>/shots/<name> --asset-root C:/c5-64bit-work/scene-asset-root/mods548-vt --allow-other-exe
python -I C:/c5-64bit-work/0457/seam.py <work>/shots                                       # the joint check of section 9
```

The scratch scripts (`mapcams.py`, `run_cams.sh`, `seam.py`, `record_vc.sh`) stay in `C:/c5-64bit-work/0457/` until E1
takes them over or the MC merges this ticket; their rules are written out in sections 4 and 9.
