# A clean colour reference for the server's terrain (issue 3019, 2026-11-14)

Written by the queue implementer of issue 3019 (account B), branch `task-3019-clean-colour-reference`. Measurement
only: no renderer code was changed. Epic 3200 could not judge its colour criterion (D67) because the stock frame holds
river water, borders, the selection, shadows and tree crowns over the terrain, and the stripped EXE gave no terrain-only
picture (`terrain-surface.md` 5, last paragraph). This file gives (a) a terrain-only reference made from the EXE's own
terrain draws, (b) the cause of each difference between it and what the server draws, measured by swapping one input
at a time, and (c) a clean-plot mask from game data with the colour numbers on it.

Tools (all in `render/evidence/3019/`, usage in each file's docstring):
- `terrain_export.py`: 3601's capture exporter (`shell/evidence/3600/3601/xml_export.py`) with the selection changed
  to the terrain materials (73 TerrainShader, 76, 78), PS named by SHA-256 against the player's EXE.
- `terrain_replay.cpp` (+ `build_replay.bat`): a D3D11 replayer of a list of captured draws (VS/HS/DS/PS,
  tessellation, one shared depth buffer) from a text manifest; writes the target, the depth and a per-pixel draw index
  (a pixel shader whose input mirrors the DS output signature, depth test EQUAL).
- `replay_manifest.py`: writes the manifest from an export and the variants below, runs the replay.
- `compare_variants.py`, `colourstats.py`: statistics (COMMON 25: median, p95, max of the largest channel difference,
  MAD and signed mean per channel, share within 16, the D67 criterion MAD <= 4 and 95 % within 16).
- `clean_mask.py`, `census_3019.py`, `analyse_3019.py`: the clean-plot mask (census of every plot through the Lua
  channel on 3608's stock arm) and the numbers on it.

## 1. The terrain-only reference: the EXE's terrain draws replayed [L capture, C replay]

Input: 3601's two stock captures on Tiny `C5H-3600-TINY_0009V` (turn 9, every plot revealed and visible), c5h-c, 32-bit
pin `i0ship-26234371-ia`, RenderDoc 1.47, 2554 x 1411, VT present (folder hash 70402AD7..., `water-system.md` 6.1):
camera 1 `..._frame309938.rdc` (LookAt (33,14), zoom level 17), camera 2 `..._frame265572.rdc` (Honolulu (51,14), three
wheel ticks in). Exports `C:/c5-64bit-work/3019/exports/cam{1,2}` (219 / 150 MB, game data, D83).

**What the EXE draws for the terrain in a frame [L, both captures]:**

| Camera | TerrainShader draws into the scene target (back buffer, D24S8 depth) | Pass-2 draws | TessFactor | LodMip |
|---|---|---|---|---|
| 1 (zoom 17) | 150: **145 with the pass-0 PS** (0x067e6e58, reads t8, t9), 5 with the pass-1 PS (0x067e8d68) | 150 into a 2554 x 1411 RGBA8 offscreen (5154), PS 0x067ea680, HS 0x067f9cb8 | 2.0 | **2.0** |
| 2 (zoom 11) | 58: 57 pass 0, 1 pass 1 | 58 | 2.0 | 0.0 |

- One draw per tile (4,096 control points = 16 x 16 patches of 16 points), the 16-control-point patch list of
  `terrain-surface.md` P11; every PS sampler slot MIN_MAG_MIP_LINEAR, no anisotropy, LOD bias 0.
- **Pass 0 is the common case**, not pass 1 as P17 assumed for the owner's settings: tile+0x2FC (the tile has a spec cache t8,
  `terrain-bake-records.md` +0x2FC) selects it. Pass 1 only on the few tiles without t8. `render/src/terrain/terrain.cpp`
  draws every tile with pass 1 (`render_pass` -1 -> 1). [L] *Since issue 0189 (2026-11-14) the server draws pass 0 where
  the tile has a t8, with t9 and FlagB as the EXE (`terrain-surface.md` 8).*
- **LodMip is 2 at the zoomed-out camera 1** (TR+0x2424, open in P17): the EXE samples the height page two mips down,
  i.e. a smoother surface; the server always draws LodMip 0. TessFactor 2 at both zooms (P17's value). [L] *Since issue
  0191 (2026-11-14) the server draws the EXE's LOD per frame (LodMip 2, LodStep 4 here); `terrain-surface.md` 7.*
- PS constants: Arg6 1.0 (shadow on), FlagC 1.0; DistanceFog FogColor (0.573, 0.714, 0.953, 1), FogParams x = -0.05
  (density 0.05) at both cameras, w 0.632 (camera 1) / 0 (camera 2). The scene target's RTV is declared
  TEXTURE2DMS but its depth target has one sample: replayed single-sampled.
- Textures bound to the pass-0 PS: t0 the tile's BC1 cache (256 x 256, 6 mips), t1 the overlay (2048 x 2048 RGBA8,
  12 mips, rendered in the frame), t2 the fog ramp (128 x 8), t3 the shadow map (2048 x 2048 R16, rendered in the
  frame), t4 the FOW mask (1024 x 1024 RGBA8, rendered in the frame), t5 the FOW base (BC1 1024), t6/t7 32^3 LUTs,
  t8 the tile's spec cache (BC1 256), t9 a shared BC3 1024 x 1024 (11 mips). *Identified 2026-11-14 by issue 0189: t9 is
  the install's `specscreenmap.dds` (TerrainTextures.fpk), byte-equal; t0 / t8 are mostly the near-camera RGBA8 caches,
  not BC1 (`terrain-surface.md` 8).* Textures rendered in the frame hold their
  frame-start contents in the export (the previous frame's: the scene is static).

**The replay:** the 150 (58) scene draws in frame order into an empty target with the capture's shaders, buffers,
textures, samplers and states (`terrain_replay.exe`, RTX 4080, 2026-11-14 13:40 EST). It shows the textured terrain
with its borders, shadows, fog and spec, without water, rivers, trees, units or interface: the terrain-only reference.
**Fidelity [L]:** against the stock picture of the same launch and camera (`D:/c5-64bit-work/3600/captures/
cam{1,2}_stock.png`) on all drawn land, the median difference is 0 at both cameras and the best integer shift is (0, 0)
(any 1-px shift raises the MAD from 17.6 / 10.8 to 21.0 / 14.9); the rest is what is drawn on top (water at the shore,
rivers, trees, units, the interface). On the clean-plot mask: section 3.

## 2. Each cause, measured by swapping one input to the server's value [L, replay]

`render/src/terrain/terrain.cpp` (2026-11-14) draws with the EXE's own programs and these inputs: pass 1, Arg6 0 (no
shadow), overlay t1 (0,0,0,0), FOW mask t4 (1,1,1,1) with FOW base black, fog density 0 (DistanceFog all 0), identity
LUTs, t8/t9 white, LodMip 0, its own bake's caches. Each variant below replays the EXE's draws with ONE of these
changes; `server` has all of them except the caches and LodMip (so `server` vs the joined frame isolates the server's
own bake and geometry, section 3). Numbers: the variant against the EXE's replay on all drawn land (world z above the
water plane + 0.5, minus a 16-px frame): MAD R/G/B, signed mean R/G/B (variant - EXE), median / p95 / max, within 16.

| Variant (cause) | Camera 1 (zoom 17), 1,419,353 px | Camera 2 (zoom 11), 2,795,230 px |
|---|---|---|
| `nofog`: no distance fog | 3.7/6.6/16.9; -3.2/-6.6/-16.9; 0 / 89 / 149; 71.9 % | 2.0/3.7/9.5; -1.4/-3.6/-9.5; 0 / 54 / 79; 80.7 % |
| `nooverlay`: no overlay (borders, city area) | 0.4/0.5/0.7; -0.4/-0.3/-0.7; 0 / 0 / 89; 98.2 % | 2.8/2.2/1.8; -2.8/-1.0/+0.4; 0 / 24 / 175; 93.0 % |
| `noshadow`: no shadow (Arg6 0) | 0.7/0.7/0.3; +0.7/+0.7/+0.3; 0 / 0 / 81; 98.3 % | 1.2/1.1/0.7; +1.2/+1.1/+0.7; 0 / 0 / 82; 97.6 % |
| `pass1`: no spec (pass-1 PS) | 0.8/0.8/1.0; -0.8/-0.8/-1.0; 0 / 1 / 182; 98.8 % | 0.4/0.4/0.6; -0.4/-0.4/-0.6; 0 / 4 / 52; 99.2 % |
| `nofow`: every plot visible | 0 (max 1) | 0 (max 1) |
| `server`: all of the above | 5.5/8.2/18.7; -3.7/-7.1/-18.5; 1 / 96 / 190; 69.3 % | 6.1/7.1/12.0; -3.4/-3.9/-8.9; 3 / 60 / 175; 72.5 % |
| `server_p0`: as `server` but with pass 0, t8, t9 | 4.7/7.4/17.8; -2.8/-6.2/-17.5; 0 / 93 / 151; 70.8 % | 5.7/6.7/11.4; -3.0/-3.5/-8.3; 1 / 60 / 175; 73.4 % |

Where each one lies (difference images, Visual Checks, section 5):
- **Distance fog** is the largest: a blue haze that grows toward the top of the screen (the far terrain); the EXE's
  terrain is bluer and brighter there (signed B -17 / -9.5: without fog the far land is darker and less blue). At camera
  1 it changes 28 % of the land pixels by more than 16. It is in seam F (issue 0168), but the server's water already
  computes the same DistanceFog block (`water-system.md` 10.7, issue 3609), so the terrain's share is separable.
  *Fixed 2026-11-14 by issue 0188 (`terrain-surface.md` 6): the server's terrain sets the EXE's block and binds the
  install's fog ramp; the `nofog` and `server` rows here describe the server before it.*
- **Overlay**: the culture-border line and its glow, and the tinted area of the selected city (camera 2). Seam E
  (issue 0167).
- **Shadow**: the shadows of trees, units and buildings on the ground (Arg6 * t3). The shadow map is drawn by the EXE's
  model passes; no ticket holds the terrain's shadow input. *Since issue 0190 (2026-11-14, terrain-surface.md 9): the module takes t3, Arg6, ShadowMatrix0 and the TerrainShadow LUT t7 (its replay with the EXE's shadow: 0 pixels differ); the tap that feeds it in the server is drafted as `NEW-shadow-tap.md`.*
  *Done 2026-11-15, epic 0169 (replaces "drafted" as the state): the shadow tap (ticket 0312, task 4304) and its binding
  in the server's terrain and water (4308) are in the product, live in every drawn frame (`terrain-surface.md` 11.9-11.11,
  13.7); at frame level shadowed land is at stock's two-session ceiling (13.6: tiny9v pooled 97.5 % within 16 against
  stock's 97.1 %; at 3601's camera 2, `w2`, 93.0 % against 92.0 %).*
- **Spec (pass 0)**: highlights on hills and mountains from t8 / t9; small on average, up to 182 on single pixels.
- FOW: 0 on this fully visible save; on a save with fogged plots it is seam F.

*State of section 2's causes and of the later ones, 2026-11-15 (epic 0169, seam G; the table above is 3019's
measurement of the server of 2026-11-14 and stays as measured).* Done: distance fog (0188), LodMip (0191), spec pass 0
(0189), overlay (0167), fog of war (0168), the models' shadow (0312 = 4304, wired by 4308), the tile styles (0341 =
4305: each tile's Sunlight / Shadowlight cubes and Terrain LUT in the bake, t6 / t7 per style; `terrain-surface.md`
12.6), the polar ice in the scene (4306-4308; `polar-ice.md` 7-12). **Frame level, live, against stock's two-session
ceiling**: `terrain-surface.md` 13.6 (ACCEPTANCE, the product build, 33 cameras on Tiny 9V, 0166's Tiny turn-8 save and huge t350); at
3601's two cameras `w1` all_kept 80.7 % within 16 against stock's 80.4 %, `w2` 88.6 % against 88.1 %. What stays:
the near-camera RGBA8 caches drawn as BC1 (0332; `terrain-surface.md` 10), the server's t8 set (0367; 8), water and
overlay +-1 run to run (0336), the ice in the server's water reflection (decision 3; `polar-ice.md` 12.1), and,
found by 13.6, the terrain's wrap copies 1 world unit off (`terrain-surface.md` 2 and 13.6).

## 3. The clean-plot mask and the numbers on it

**The census [L].** `census_3019.py` on 3608's stock arm (32-bit pin `i0ship-26234371-ia`, darkred `--arm stock`, save
copies named `C5H-3019-*`, every backed-up file restored and verified equal by SHA-256, copies deleted) reads per plot
through the Lua channel: plot type, terrain, feature, improvement, resource (as the active team sees it), route,
IsRiver and the three river-edge flags, owner, city, units, revealed, visible, water, lake. 2026-11-14: Tiny 9V on
c5h-a 15:00-15:03 (2016 plots), Tiny t18 (`C5H-3000-TINY-T0018`, copied from c5h-b) and huge t350
(`C5H-VP8P-HUGE_0350`, 10,240 plots) on c5h-c 15:39-15:46; each load about 3 min. Outputs
`C:/c5-64bit-work/3019/census/*.json` (game data, not committed; regenerate with `run_census.sh` as in the script's
docstring).

**The mask (`clean_mask.py`) [C code, L check].** Clean plot = revealed and visible, not water, and none of: river
edge, feature, improvement, resource, route, unit, city, owner-border edge (its owner differs from a neighbour's,
owned on at least one side), a neighbour that is not visible (the fog-of-war clouds of an unseen plot reach over its
visible neighbours: on Tiny t18 every candidate plot lay under cloud edges). Tiny 9V: 189 of 2016 plots clean; huge
t350: 23 of 10,240 (9,466 not visible to the active team); Tiny t18: 0 (31 visible plots, each next to fog or with a
feature). Pixel to plot: the nearest plot centre of HexToWorld (`camera-and-picking.md` 5, 9) at the pixel's terrain
point; the terrain point from the replay's depth, or for a live picture by ray casting its camera block against the
height field at the EXE's drawn height (`(u16 >> 8) * 0.2`, bug 0134). Check: drawn-below-water agrees with the plot's
IsWater on 93.9-99.3 % of the pixels (the shore slopes are the rest; 3608's rectangle mapping had 88-95 %), and the
plot outlines fall on the culture border and the hex corners in the replay. Then erode 12 px, remove a 16-px frame and
the interface rectangles (widened from 3608's after looking at the pictures: unit panel with portrait 520 x 310, top
band 60, top-right 420 x 100, the icon columns right 120 x 900 and left 100 x 1000, the minimap 360 x 260), and
**remove what models can cover**: from every pixel of a plot with a feature, improvement, resource, unit or city, the
screen column up by the projected length of a 30-world-unit post (trees and buildings stand up and overlap the plots
behind them on screen, which an erosion misses: with 12 units, tree bands stayed in the mask at camera 1).
Negative result: a "strict" mask that also drops plots with a feature, improvement, route, river or city neighbour
leaves 14,370 px at camera 1 and none at camera 2 on Tiny 9V (forests everywhere): not usable on these saves.

### 3.1 Tiny 9V, 3601's two cameras: the replay as the clean reference (2026-11-14 15:58 EST) [L pictures, C replay]

Columns as in section 2 (MAD R/G/B; signed R/G/B; median / p95 / max; within 16):

| Comparison | Camera 1 (zoom 17), 261,969 px | Camera 2 (zoom 11), 434,284 px |
|---|---|---|
| **EXE replay vs stock picture of the same launch (the reference's fidelity)** | 3.8/2.9/2.6; +0.6/+0.3/-1.6; 0 / 27 / 242; 93.9 % | **0.2/0.2/0.1; -0.1/-0.1/-0.0; 0 / 0 / 173; 99.7 %** |
| `nofog` vs EXE | 1.9/3.4/8.3; -1.8/-3.4/-8.3; 0 / 75 / 135; 87.0 % | 0.6/1.3/3.0; -0.5/-1.3/-3.0; 0 / 13 / 61; 97.2 % |
| `lod0` vs EXE | 2.9/2.8/1.8; +0.0/+0.0/-0.0; 2 / 12 / 105; 97.7 % | (LodMip already 0) |
| `pass1` vs EXE | 0.2/0.2/0.2; -0.2/-0.2/-0.2; 0 / 1 / 125; 99.7 % | 0.6/0.6/1.0; -0.6/-0.6/-1.0; 0 / 7 / 42; 98.9 % |
| `nooverlay`, `noshadow`, `nofow` vs EXE | 0 (border plots and model places are outside the mask) | 0 |
| `server` (every input of the server, the EXE's caches) vs EXE | 2.1/3.5/8.6; -2.1/-3.5/-8.6; 0 / 75 / 135; 86.2 % | 1.1/1.9/4.0; -1.0/-1.9/-4.0; 0 / 15 / 61; 95.9 % |
| `server_lod0` vs EXE | 4.5/5.6/9.7; -2.0/-3.5/-8.6; 3 / 74 / 136; 84.9 % | - |

- **The replay is the clean reference:** at camera 2 it equals the stock picture on 99.7 % of the clean pixels within
  16 (median and p95 0). At camera 1 (zoomed out) the p95 27 is tree tops and units still inside the mask [I, pictures].
- **On clean ground only distance fog, spec and LodMip remain** between the EXE's draw and the server's inputs: border,
  shadow and fog of war are 0 there by construction. Fog breaks the criterion (blue -8.6 / -4.0, p95 75 at camera 1).

**The server's own frame against it.** 3608's joined ACCEPTANCE pictures (D112; `cam{1,2}_3601_joined.png`, the
product of 2026-11-14 04:00, server.exe with its own bake) against the replay re-projected to the joined picture's
camera block (`replay_manifest.py --viewproj`): with the server's inputs (what the server's own bake and geometry
add) and as captured (the criterion against the clean reference).

| | cam1_3601 (237,031 px) | cam2_3601 (363,420 px) |
|---|---|---|
| joined vs replay with the server's inputs (`server_lod0` / `server`) | 7.1/6.2/4.3; +1.7/+2.6/+1.4; 5 / 24 / 204; 91.6 %; fail | **3.2/2.5/1.6; +0.3/+1.8/+1.1; 0 / 14 / 100; 96.1 %; pass** |
| joined vs EXE replay (the criterion against the clean reference) | 8.6/7.6/5.3; +1.6/+2.3/+0.3; 7 / 29 / 204; 87.3 %; fail | 4.0/3.6/6.0; -1.2/-0.6/-4.5; 3 / 39 / 119; 86.8 %; fail |
| joined vs 3608's stock picture (stock vs stock 5 s later) | 8.3/6.7/3.6; +4.3/+3.9/+1.3; 3 / 51 / 225; 91.1 % (0 / 0 / 43) | 8.7/9.0/12.7; -2.8/-3.1/-9.9; 8 / 71 / 212; 77.2 % (0 / 1 / 115) |

- At cam2_3601 the server's bake and geometry are inside the criterion (MAD 3.2 max, 96.1 % within 16); what fails
  against the clean reference is distance fog (blue -4.5) and spec. At cam1_3601 the residual is fine texture speckle
  across the clean plots and forest-floor decals reaching over plot edges (seam D, 0166) [L picture, I cause: the
  bake's detail at that zoom; the EXE's LodMip at that camera is not known, the replay uses 0 as the server does].
- The server's frame is a little brighter than the EXE's caches give (signed R/G +0.3 to +2.6 against the
  server-input replay) [L, small, cause not separated: the server's bake].
- 3608's own joined-vs-stock pair fails by more (median 3-8, p95 51-71): the stock picture carries the distance fog
  (B -9.9 at cam2_3601) and models that the mask does not fully remove; the replay is the better reference.
- **Per tile** (the replay's draw index = one EXE tile, tiles with at least 500 clean pixels), joined vs server-input
  replay: cam1_3601 12 tiles, medians 3-9; cam2_3601 21 tiles, medians 0 (5 tiles) to 9; the worst tile (median 9,
  MAD 11/8/5, p95 29) borders forest plots whose floor decals the server does not bake. Lists in
  `render/evidence/3019/results_3019.json`.

### 3.2 Epic 3200's acceptance cameras (huge t350, Tiny t18): 3608's pairs on the clean mask [L pictures]

The 3608 pairs are the current product (joined ACCEPTANCE pictures) and the stock arm at the same camera procedure
(another launch: the camera blocks differ by 0.006-0.066 in the largest view element; the best integer shift within
+-3 px is (0, 0) at both huge cameras and cam1_3601, (0, -1) at cam2_3601 with no gain).

| Camera | Clean px (plots in view) | Joined vs stock | Stock vs stock (5 s) |
|---|---|---|---|
| Huge v1_city_default | 289,405 (8) | 6.5/6.1/5.3; +5.5/+4.3/-3.5; 8 / 19 / 191; 92.7 %; fail | 0 / 0 / 0 |
| Huge v2_city_zoomin | 155,272 (3) | 6.4/6.4/4.4; +5.7/+5.6/-0.8; 7 / 21 / 243; 93.7 %; fail | 0 / 1 / 52 |
| Huge v3_second_zoomin | 0 (the city fills the view) | - | - |
| Tiny v1, v2, v3 (t18) | 0 (no clean plot: 31 visible plots, each next to fog of war or with a feature) | - | - |

- On huge the clean plots are desert; the difference is an even offset over them (joined brighter in R and G by
  +4.3 to +5.7, bluer stock), not structure [L picture]. Its direction matches Tiny 9V's bake offset and missing
  distance fog [I: not separated on huge: no capture of the huge save, section 4 of `renderdoc-capture.md`].
- So 3200's colour criterion (MAD <= 4 and 95 % within 16) is **not met** at the two huge cameras that have clean
  ground (MAD 6.1-6.5, 92.7-93.7 % within 16), cannot be judged at huge v3 and the three Tiny t18 cameras for want of
  clean ground, and on Tiny 9V is met by the server's bake at one of two cameras once the server's missing inputs are
  set aside.

## 4. Causes and proposed fixes

| Cause | Size on clean ground (Tiny 9V) | Fix |
|---|---|---|
| Distance fog (DistanceFog block, fog ramp t2) | p95 75 / 13-15, blue -8.6 / -4.0 | new ticket `NEW-terrain-distance-fog.md` (from seam F, 0168: the water already computes the block). *Done 2026-11-14 by issue 0188: the module's block and ramp give the EXE's replay with 0 differing pixels at both cameras (`terrain-surface.md` 6); the `server` rows above are from before.* |
| LodMip (height page mip per frame) | MAD 2.9/2.8/1.8, p95 12 at zoom 17 | new ticket `NEW-terrain-pass0-spec-and-lodmip.md` *Done 2026-11-14 by issue 0191 (`terrain-surface.md` 7): with every scene draw's LodMip / LodStep / TessFactor replaced by the module's values for the capture's camera (2 / 4 / 2 at camera 1, 0 / 1 / 2 at camera 2: byte-equal to the captured buffers) the replay has 0 differing pixels at both cameras (`render/evidence/0191/replay_0191.py`, results `replay_cam{1,2}.json`); the lod0 variant reproduces 3019's figure there (camera 1 MAD 3.3/3.2/2.0, p95 15, max 162, over 1,419,353 land px).* |
| Spec (pass 0 with t8, t9) | p95 1-7, max 42-125 | the same new ticket *Done 2026-11-14 by issue 0189 (`terrain-surface.md` 8): pass 0 where the tile has a t8, t9 = `specscreenmap.dds`, FlagB 1; the replay with the module's inputs has 0 differing pixels at both cameras (before: camera 1 p95 1, max 182).* |
| The server's bake against the EXE's caches | cam2_3601 MAD 3.2 (passes); cam1_3601 7.1; huge offset +5 R/G [I] | decals: seam D (0166); the rest is measured here, not separated. *2026-11-15 (epic 0169): the tile style is one part (Africa as Generic: t0 p95 12, +2.4 / +2.5 / -1.1, `terrain-surface.md` 12.3 [L, cache level]), done by 4305 (12.6); at 4309's huge cameras bare Africa ground is then at stock's ceiling (13.6: pooled median 0, 95.8 % within 16 against 92.6 %); 3608's huge v1 / v2 were not re-measured. The near-camera RGBA8 caches drawn as BC1 (0306 / 0332, `terrain-surface.md` 10) and the t8 set (0367) stay* |
| Overlay (borders, city area) | 0 on clean ground; off it p95 24, max 175 | seam E (0167) |
| Shadows of models on the ground | 0 on clean ground; off it max 82 | new ticket `NEW-terrain-shadow-input.md` *(numbered 0190; module side done 2026-11-14, terrain-surface.md 9; the tap: `NEW-shadow-tap.md`)* *Done 2026-11-15, epic 0169: the tap is 0312 (4304) and is wired by 4308 into the terrain and the water; shadowed land at stock's ceiling (`terrain-surface.md` 13.6)* |
| Fog of war | 0 on the fully visible save | seam F (0168) *2026-11-14 (3957): built by epic 0168, route (a) (`fog-of-war.md` 5, 7): the module's mask against stock's t4 99.99 % of texels equal (+-1 on ramp texels, 7.1); the replay of the captured terrain and water draws with the module's mask, t6 and FOWAnim 99.99 % of pixels equal, max 1 (7.4); the cloud planes' replay median / p95 / max 0 / 0 / 1 and 0 / 0 / 61 (7.2); left: the cloud phase from the server's clock (intended) and the FOWLevel-3 particles (intended); frame level live: `fog-of-war.md` 8* |
| Texture filtering, gamma | not found: every PS sampler MIN_MAG_MIP_LINEAR, no anisotropy, LOD bias 0; the server's frame and the replay agree in level (signed mean within 3) | none |

*Causes named after 3019, with their state on 2026-11-15 (epic 0169; `terrain-surface.md` 12.5 is the list with
sizes):* tile styles (0341): done by 4305 (12.6); the polar ice: done in the scene by 4306-4308 (`polar-ice.md` 7-12),
the floes equal stock at frame level, the ice in the server's water reflection stays (decision 3, follow-up ticket;
13.6: the ice class tiny9v 91.4 % within 16 against stock's 94.7 %); the water fog colour's 1 ulp: done by 4305; the
near-camera RGBA8 caches (0332), the t8 set (0367) and water + overlay +-1 (0336): open; the terrain's wrap copies 1
world unit off: found by 13.6, open.

## 5. Not verified / limits

- The replay is a D3D11 replay by this project's tool, not RenderDoc's; its fidelity is shown against the stock picture
  of the same launch (section 1) and on clean plots (3.1: 99.7 % within 16 at camera 2).
- Shadow map, overlay and FOW mask hold the frame-start contents (the previous frame): in a static view the same [I].
- The causes are measured at two cameras of one Tiny save; no capture of the huge save (it runs the 32-bit address
  space out, `renderdoc-capture.md` 4). The size of the fog term depends on the zoom (FogParams.w 0.632 at zoom 17,
  0 at zoom 11).
- The replay at 3608's joined cameras reuses the capture's tile set: tiles outside the capture's view are not drawn
  (not in the mask); the EXE's LodMip at those cameras is not known.
- No ACCEPTANCE picture was taken (D112): every picture here is a diagnostic; the joined pictures used are 3608's.
- *2026-11-15 (epic 0169): the frame-level comparison this file could not make on the huge save is made live by 4311
  against stock's two-session ceiling per plot class (ACCEPTANCE, D112; `terrain-surface.md` 13.5-13.6), with 4309's
  census, plot classes and masks (made after this file's census and clean mask and 0166's done C, 13.2) and cameras
  (3601's two cameras on Tiny 9V among them). It compares with stock pictures, not with this file's replay, and 3608's
  huge cameras v1 / v2 are not among its cameras.*
