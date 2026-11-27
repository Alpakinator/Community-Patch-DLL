# Fog of war: what stock draws, the FluidFOW mask builder, and the server's route (epic 0168, seam F)

Started 2026-11-14 21:02 EST by lead-0168 as a skeleton; filled by tasks 3950 (section 3, the builder's behaviour,
static) and 3951 (section 2, the stock frame and the D33 joined arm, live), and by the lead (sections 1, 4, 5). Brief:
`docs/briefs/epic-0168-fog-of-war.md` (premises P1-P16, route choice, decisions 1-6). Distance fog is not here: it is
done for the terrain by 0188 (`terrain-surface.md` 6) and for water and rivers by 3609 (`water-system.md` 10.7).

Rules for this file (D83): behaviour, formats, addresses, constants and measurements only; no decompiler output, no
shader disassembly text, no bytes of the EXE or of the game's files. Marks: `[C]` decompile or dump, `[L]` live,
`[I]` inferred. Every measurement names its date, save, pins, install and asset set (D94).

## 1. Summary and status (lead)

(lead-0168, 2026-11-14 22:15 EST; C1 = tasks 3950 static and 3951 live, both done.)

- **Stock's fog of war has four parts** [C, L]: (1) the FOW mask, a 1024 x 1024 RGBA8 target (512 at FOWLevel 0-1)
  cleared and redrawn every frame by FOWDrawMask as MAX-blended hexagons per plot in the overlay's window: R explored,
  G unexplored, B visible, with linear ramps over a third of a plot at each edge; (2) the terrain, water, river and
  model shaders read it (R, B) to fade to the cloud base `fogcloudtile_dif_02.dds` (terrain; flat 0.4 grey on water)
  and to the TerrainFOW LUT on seen plots; (3) after the models, FOW CULL (depth only) and three translucent cloud
  planes (`fogcloudtile_dif_02/03/01`, 18 draws) gated by the mask's G; (4) at FOWLevel 3 one particle draw.
- **The mask is stateless and deterministic** (P8 bit-equal at rest and after a reload; no dissolve in it), so the
  server can rebuild it exactly from `PlotRecord::fow` with the EXE's own programs: route (a), no interim look-alike.
- **The D33 product leaks map information today** [L, 2.9]: unexplored terrain, coast and sea are drawn in full.
- FluidFOW's large memory (8 x 24 MB, the particle grid) is a FOWLevel-3 cost only [C, L]. *2026-11-15 (lead-0168): now
  [L] by 8.3: +283 MB EXE private at FOWLevel 3, about 3 MB at FOWLevel 2 (huge save, turn 350); under D33 with
  render_fow=1 the EXE's FluidFOW stays off (8.4).*
- Route, modules, failures and decisions: section 5. Open: section 6.
- *2026-11-16 (ticket 0340): the FOWLevel-3 particles are drawn by the server, a look-alike of FluidParticles from a
  static description (section 10), within the pre-stated tolerances of stock's sprite count, coverage, mean alpha and
  drift at two huge-map cameras (10.3).*

## 2. The stock frame: what draws the fog of war, from what (task 3951)

Written 2026-11-14 22:10 EST by task 3951 (branch `task-3951-fow-stock-captures`). Answers P3, P4, P5, P6, P7 (live),
P8, P9, P11 (capture), P13, P14 (capture), P15 and the lead's four checks of 3950's static reading; the D33 joined arm
at the same cameras in 2.9.

**Setup of every measurement here.** Instance c5h-a (Goldberg, C:), 2554 x 1411, `GraphicsSettingsDX11.ini`
**FOWLevel = 3**, VT present (55 files, folder hash 45B3B433...), 32-bit pin `i0ship-26234371-ia` (3C2F47DA...) in the CP
slot, `darkred.py config --arm stock` (the EXE's FluidFOW, FOW and DistanceFog on; no D33 key), CP/VP/EUI, RenderDoc
1.47 preload (9027), frame cap on. SHA-256 backup `C:/c5-64bit-work/0168/3951/backup-c5h-a`, restored by hash.

**The save had no seen-not-visible plot [L].** `C5H-3000-TINY-T0018` (copy `C5H-0168-TINY_0018`, E337244C...): at load 31
plots revealed and the same 31 visible (census through the Lua channel, `st3951.py census`). The class was made by player
input on the copy: pathfinder (9,22) -> (11,22) and warrior (8,24) -> (10,24) by clicks (the target pixel from
gt_reader's projection, checked with `UI.GetMouseOverHex` before each click: `plotpix.py`), turn end (Enter; the copy
blocked the end of turn on production and research, chosen by Lua `Game.CityPushOrder` / `PushResearch`: diagnostic
stimuli, no reveal), pathfinder to (11,24), warrior to (11,25), turn end. Visibility drops only at a turn change
[L: the moves inside turns 18 and 19 left every revealed plot visible]. States used:
- **t20** (`C5H-0168-TINY_0020F`, saved by Lua `UI.SaveGame`): 50 revealed, 41 visible, 9 seen-not-visible (water (6,24),
  (6,25), (7,26); land (9,26), (10,20), (11,20), (10,21), (11,21), (12,21)), 1,966 unexplored.
- **t21** (`C5H-0168-TINY_0021R`, A7B077A9...): 57 revealed, 46 visible, 11 seen-not-visible. The reload of 0020F came up
  at turn 21: the harness's load-screen dismissal sends Enter, which ended turn 20 because nothing blocked it; 0021R
  holds a unit that needs orders, so Enter cannot end its turn. The t21 censuses before and after a reload are equal
  plot for plot.

**Cameras** (Lua, for C7 and later tasks; `st3951.py look`): **cam1** = `UI.LookAt(Map.GetPlot(9, 23), 0)`, no wheel
tick (zoom level 14.2556, camera height 424.26); **cam2** = `UI.LookAt(Map.GetPlot(8, 25), 0)`, then 4 wheel ticks in
at (1277, 705), 2 s apart (zoom 11.0); cursor parked at (2404, 1310). cam1 holds all three classes on land and water;
cam2 holds seen water (6,25), (7,26), seen land (9,26), visible land and water, unexplored water and land.

**Captures** (archived 2026-11-14 in `D:/c5-64bit-work/0168/3951/captures/`, SHA-256 checked against the C: originals,
which were then removed; game data, D83). The same folder holds `pics/`, `analysis/<label>/` (mask.npy, mask_*.png,
analysis.json, classes.json), the censuses, the three saves (`saves/`), `joined-arm/` and each export's `draws.json`
and `frame.json`; the full exports and the zip.xml conversions were not kept (regenerate: `fow_export.py CAPTURE OUT`,
about a minute each):

| Label | File | State | When (EST) |
|---|---|---|---|
| cam1 a | `c5h-a_20261114-213411_frame16934.rdc` | t20, launch 1 | 21:45:31.3 (trigger) |
| cam1 b | `c5h-a_20261114-213411_frame17255.rdc` | t20, launch 1 | 21:45:43.2 (+11.94 s, 321 frames) |
| cam2 a | `c5h-a_20261114-213411_frame19926.rdc` | t20, launch 1 | 21:47:13.2 |
| cam1 t21a | `c5h-a_20261114-214835_frame1272.rdc` | t21, launch 2 (~36 s after the turn change) | 21:51:11.5 |
| cam1 t21b | `c5h-a_20261114-214835_frame2681.rdc` | t21, launch 2 | 21:51:59.6 (+48.1 s) |
| cam2 t21 | `c5h-a_20261114-214835_frame3774.rdc` | t21, launch 2 | 21:52:37.1 |
| cam1 t21reload | `c5h-a_20261114-215331_frame1306.rdc` | t21, launch 3 (load of 0021R) | 21:56:31.7 |

Tools (`render/evidence/0168/3951/`): `fow_export.py` (every draw of a capture through 3601's exporter, `frame.json`),
`fow_analyse.py` (passes, mask writers, frame-start mask as `mask.npy`, t4-t7, TerrainFrame, cloud files), `fow_focus.py`
(one line per draw), `fow_cbdump.py`, `fow_readers.py`, `fow_clears.py`, `fow_classes.py` (per-class texels, edge
profiles, mask compares), `p9_shots.py`; drivers `shell/evidence/0168/3951/st3951.py`, `reload.sh`, `run_3951j.py`.

### 2.1 P3: the mask target and its writers [L, all seven captures]

- **Target**: resource "FOW Mask" = the terrain PS's t4, **1024 x 1024 `R8G8B8A8_TYPELESS`, 11 mips** (bind SRV + RTV),
  the same resource in every capture of a launch. One window per frame: its viewport is the whole 1024 x 1024 and it is
  sampled with **the same OverlayProj as t1** (TerrainFrame 0x040, equal in the terrain DS, PS and VS cb1 and in the
  water's WaterFrame 0x040): cam1 (0.002404, 0.002841; offset 2.846154, -0.818182), i.e. 832 x 704 world units;
  cam2 (0.003472, 0.003906; 4.333333, -1.5), 576 x 512. These are single-view maps (no wrap copy on screen); a wrap view
  was not captured [open].
- **Texel of a world point** [L: all 46 / 41 visible plot centres of t21 / t20 read B = 255 only with this convention]:
  NDC = (x, y, 0, 1) x OverlayProj (row vectors; x, y = HexToWorld's centred world), u = 0.5 NDC.x + 0.5,
  **v = 0.5 - 0.5 NDC.y**, texel = (u, v) x 1024.
- **Writers, in frame order**: `ClearRenderTargetView` of the mask to **(0, 0, 0, 0)** (before draw 100 of 687 at cam1),
  then **FOWDrawMask (52) only**: cam1 9 draws (pass 2 x 1, pass 1 x 2, pass 0 x 6), cam2 5 draws (1, 2, 2); each a
  `DrawIndexed` of 1,152 indices (triangle list, VB stride 40, 28,800 B; IB 36,864 B), viewport 1024 x 1024, **blend ONE
  / ONE with op MAX on colour and alpha**, depth off; VS and PS share one 1,536-B cb0 (96 float4 = 24 matrices of 4 rows:
  per hexagon a scale (0.15384 = 2 x 64 / 832) and an NDC centre; 1,152 / 24 = 48 indices a hexagon). The mask pass sits
  after the shadow map (draws 97-99) and before the scene clear and the terrain (109 on): it is redrawn every frame,
  so the frame-start contents that a capture exports are the previous frame's finished mask.
- No other draw writes it; FOWBaseLayer (53), FOWParticleShader (51) and the terrain read it (2.3).

### 2.2 P4: what the mask holds per plot class, and the edges [L]

Plot centre texels (nearest texel, `fow_classes.py`), every capture (t20 cam1: 9 seen, 41 visible, 119 unexplored plots
in the window; t21 cam1: 11, 46, 112):

| Class | (R, G, B, A) at the centre | Plots, all captures |
|---|---|---|
| visible | **(255, 0, 255, 255)** | land and water alike, every plot |
| seen, not visible | **(255, 0, 0, 255)** | land and water alike, every plot |
| unexplored | **(0, 255, 0, 255)** | every plot whose hexagon is drawn; plots at the window's rim read **(0, 0, 0, 0)** (the clear: no hexagon drawn there) |

- So **R = explored, B = visible, G = unexplored, A = 1 wherever a hexagon was drawn** [L]. The terrain PS's `t4.x` (R) and
  `t4.z` (B) are the two channels it reads (`terrain-surface.md` 1; the brief's contradiction "x * (1 - y)" vs ".xz" is
  settled for the data: G is not "visible", it is the unexplored class). A writer that put "visible" in G would be wrong.
- **Edge profile** (mean over the plot pairs of cam1 t20: 21 seen/unexplored, 17 seen/visible, 37 visible/unexplored;
  sampled every world unit along the 64-unit line between the two centres): each class's own channel stays 255 up to the
  shared hex edge (32 units from its centre) and falls **linearly to 0 at about 43 units** (seen R, visible B: 255 at 32,
  ~62 at 40, 0 at ~43-44) or **about 45-46 units** for unexplored G (214 at 34, 96 at 40, 16 at 44, 0 at 46). With the
  MAX blend both ramps overlap at the edge, where (R, G, B) = (251-253, 252-253, 0 or 253). In units of the hexagon's
  inner radius 32: **1.33-1.35 for explored and visible, ~1.42 for unexplored**. At cam1 1.23 texels per world unit in x,
  1.45 in y, so a ramp is 13-17 texels wide.
- The lead's check (1) of 3950 [L]: clear to 0, one hexagon per plot, MAX blend, one pass per class, seen (w,0,0,1),
  visible (w,0,w,1), unexplored (0,w,0,1), ramp to 0 at about 1.33x (1.40x unexplored): **confirmed**, with the ramp end
  measured at 1.33-1.35x and ~1.42x. Which FOWDrawMask pass draws which class is not separated here (corrected 2026-11-14 23:00 by lead-0168 from 7.1, task 3952 [L cb0]: program pass 2 = seen, 1 = visible, 0 = unexplored; replaced "not separated here") (counts per pass:
  p2 1 draw, p1 2, p0 6 at cam1: p0 most likely the unexplored rim [I]). Mask size 1024 at FOWLevel 3 [L]; 512 at other
  levels not seen.

### 2.3 P5: what covers unexplored plots in the back buffer [L]

**Stock draws the clouds into the scene target with separate draws, after the models (the brief's risk 2).** In the
scene pass (back buffer with its D24S8 depth, cleared to (0.33, 0.33, 0.33, 0) = (84, 84, 84)), cam1 t20 draws 248-390:
models (draws 249-369), then
1. **draw 370: FOWDrawMask pass 3** (the FOW CULL draw of 3950's reading): 144 indices, colour write mask 0, depth test
   LESS_EQUAL with depth write: depth only, no colour;
2. **draws 371-388: FOWBaseLayer (53) x 18** = 3 groups of 6 (cam2: 3 x 3), 144 indices each, VS/PS cb0 3,328 B,
   **blend SRC_ALPHA / INV_SRC_ALPHA**, depth test LESS_EQUAL without write; t1 = the mask; t0 = a cloud texture:
   group 1 **`fogcloudtile_dif_02.dds`** (BC1, the same resource the terrain binds as t5), group 2 **`_dif_03`** (BC3),
   group 3 **`_dif_01`** (BC3) [L: mip 0 equal by SHA-256 to the files in the install's `Resource/DX9/EffectTextures.fpk`,
   read with 9012's `fpk.py`]: the three cloud planes of 3950's reading, one texture each;
3. **draw 389: FOWParticleShader (51)** x 1, 5,688 indices (VB 1,310,720 B), the same blend and depth, t0 a 512 x 512
   BC3 (mip-0 SHA-256 F775A8C1...: not matched to a file), t1 the mask (FOWLevel 3).
- Under them the terrain is drawn as everywhere (pass 0 / 1 with t4 = the mask, t5 = `_dif_02`): where t4.x = 0 its PS
  gives the cloud base colour (`terrain-surface.md` 1) [C for the PS; I that terrain tiles under unexplored plots are drawn:
  the draw count did not separate them]. The water's PS (91 pass 3 / 4) binds the same mask as t10 and the TerrainFOW
  Generic LUT as t11.
- So P5 as written ("nothing of FluidFOW draws into the back buffer") is **false** [L]: the server needs the cloud
  planes (18 FOWBaseLayer draws, 3 textures, alpha blend over the finished scene including models), the FOW CULL depth
  draw and, at FOWLevel 3, the particle draw, besides the mask inputs of the terrain and water. The lead's check (2) of
  3950 is **confirmed** in kind and order (FOW CULL, three planes, particles, all into the scene target after the world);
  the plane heights and the `mask.y` gating were not measured live.
- The "flat grey" of PROBES.md line 28 with FluidFOW off is the scene clear (84, 84, 84) where nothing is drawn [I: not
  re-measured here].

### 2.4 P6: the FOW base texture t5 [L]

t5 of every terrain draw = **`fogcloudtile_dif_02.dds`** (BC1 1024 x 1024, 11 mips; mip 0 SHA-256 22A98943D4E8428A...),
the same resource at both cameras and in all seven captures. **FOWBaseLayer does not draw into it**: it samples it (its t0
in the first 6 of 18 draws); t5 is a file texture, never a render target.

### 2.5 P7: FOWAnim (TerrainFrame 0x090) [L]

| Capture | FOWAnim (x, y, z, w) |
|---|---|
| cam1 a / b | (0.00195367, 0.00195367, -1129.895, 0) / (same, same, -1151.910, 0) |
| cam2 a | (same, same, -1330.591, 0) |
| cam1 t21a / t21b / cam2 t21 | (same, same, -83.578 / -178.464 / -251.945, 0) |
| cam1 t21reload | (same, same, -85.504, 0) |

- **xy constant** (0.0019536701 in all captures of the map, 1/511.857). The lead's formula k = floor(W x 0.002) / W gives
  7 / 3584 = 0.00195313 for W = 56 x 64: 0.03 % off; 7 / 3583 fits [I: what W is exactly is 3950's].
- **z falls at -1.97 per second** (t20: -200.70 over 101.9 s; t21: -94.886 over 48.1 s and -168.367 over 85.6 s; the
  11.9-s pair gives -1.84, trigger timing jitter), i.e. **z = -2 T** (the lead's check (3): confirmed). z restarts with
  the process: -83.6 and -85.5 at about 36 s after the load of two launches, so stock's own phase differs launch to launch;
  the server can take z from its own clock with no visible loss beyond a phase.

### 2.6 P8 and P9: determinism [L]

**P8 holds: the mask is a deterministic function of the plot states and the window.**

| Pair | Texels equal (of 1,048,576) |
|---|---|
| cam1 a vs b: one process, 11.9 s apart, at rest | **1,048,576 (all)** |
| cam1 t21a vs t21b: one process, 48 s apart, the first ~36 s after a turn change | **all** |
| cam1 t21b vs t21reload: same save, new process | **all** |
| cam1 t21a vs t21reload | **all** |
| control: cam1 t20 a vs t21b (a turn passed, 7 more plots revealed) | 972,349 (92.7 %), the rest in the changed plots' area |

So no simulation state survives in the settled mask: no drift at rest, no history, the same after a reload; the
dissolve (the mask's motion after a change) was over within ~36 s of a turn change [the dissolve itself was not caught].

**P9, stock's own shot-to-shot at rest** (two harness shots 5 s apart, largest channel difference, the interface and a
16-px frame masked; `p9_shots.py`): cam1 median 2, p95 26, max 217, 88.2 % within 16; cam2 median 1, p95 24, max 174,
89.9 % within 16. On cloud-like pixels (min channel >= 170, grey) median 8 (cam1 and cam2), elsewhere median 0: **the
clouds move between shots (FOWAnim), the mask edges do not**. The rest's p95 24-27 is water, waves and unit idle
animation. Difference figures: Visual Checks `vc-20261114-220516-workstation-f09c` (cam1), `-220517-...-514a` (cam2).
A third shot (`vc-20261114-215118-workstation-00f5`) is a harness PrintWindow artifact on the stock arm (no interface,
grey corners, no clouds) and is not used.

### 2.7 P11 (capture side): the TerrainFOW LUT [L]

Every terrain draw's t6 (32 x 32 x 32 `R8G8B8A8`, two resources 2473 / 2476 with equal bytes, SHA-256 of the volume
49245115241D673E...) equals the water's t11 (the TerrainFOW Generic volume, `water-system.md` 10.4) and the model
draws' t9 / t8: **one TerrainFOW volume on screen at both cameras**. The terrain's t7 (TerrainShadow) is B68239D5... The
volumes are kept for C4's bit compare in `D:/c5-64bit-work/0168/3951/exports/cam1_a/textures/2473_49245115.bin` and
`2476_49245115.bin` (with `.json` metadata; also the mask 5427, t5 5448, the clouds 5451 / 5454 and TerrainShadow 2488). The style of the Madrid tiles was not read [open: C4 names it with `tileStyle`].

### 2.8 P13, P14, P15 [L]

- **P14 holds**: the model draws that read the mask (159 at cam1 t20: 4 pixel shaders, the mask in PS slot t7, or t6 for
  one; t8 = the cloud base `_dif_02`; t9 = the TerrainFOW volume) carry **OverlayProj exactly in VS cb1 rows 8-11**
  (0.002404 / 0.002841 / -0.5 / (2.846154, -0.818182, 0.5, 1), equal to TerrainFrame 0x040). The ice (IceShader 59) reads
  the mask as t8.
- **P13**: stock's models read the mask and the FOW LUT like the terrain, so models on seen-not-visible plots are tinted
  by the same `x * (1 - z)` LUT weight and models under unexplored plots take the cloud base [C bindings, L draws]; which
  models exist on which plots is the DLL's decision, not the mask's [I]. Picture comparison: 2.9.
- **P15 holds**: no FOWCell (50) draw in any of the seven captures.

### 2.9 The D33 joined arm at the same cameras (diagnostic)

Run 2026-11-14 22:03-22:08 EST on c5h-a with 3702's product-path driver (`shell/evidence/0168/3951/run_3951j.py`,
`joined.sh`): ServerRenderer = 1, composite 078A8D1B + proxy D0A6A09A + server.exe 9C2F9A9C (release) built from commit
e95b5387 (= main 7665ed05 + the epic's research; no product change), x64 DLL 74588135 (pin `x64-04b1211b` release deps),
D33 in the shipped A'' form with Overlay on and the white fog mask as shipped; save `C5H-0168J-TINY_0021R` (copy of the
t21 state); same camera procedure; popups checked (D114). The stock references are the t21 stock shots of the same
state and cameras. Camera blocks equal to stock's within 0.01 world units.

| Camera | Joined picture | Stock reference | Joined vs stock (UI masked): median / p95 / within 16 | on stock's cloud-like pixels | elsewhere |
|---|---|---|---|---|---|
| cam1 | `vc-20261114-220614-workstation-3baf` | `vc-20261114-215638-workstation-8ef8` | 29 / 149 / 39.9 % | median 104 | median 18 |
| cam2 | `vc-20261114-220734-workstation-92bf` | `vc-20261114-215300-workstation-6315` | 14 / 148 / 52.1 % | median 98 | median 8 |

Difference figures `vc-20261114-220914-workstation-3c92`, `-220915-...-8acc`. Pictures and camera blocks:
`D:/c5-64bit-work/0168/3951/joined-arm/{stock,joined}/`.

**What the player sees today [L, pictures]:** no cloud anywhere; the terrain, rivers, mountains, coast and sea of
**unexplored** plots are drawn in full (cam1: the whole land mass east and south of Madrid and the sea to the west and
north; cam2: the sea and coast north-west of Madrid), so **the D33 product shows plots the game has not revealed**: the
information leak of the brief is confirmed (it was [I]). Seen-not-visible plots are drawn fully lit, not darkened
(stock darkens them: the water hexes (6,24), (6,25), (7,26) are a dark band at cam1 and cam2 in stock, normal blue in
the joined arm). Units and their flags are the same in both arms at both cameras (the EXE draws models by the DLL's
visibility). The cam2 joined picture also holds the EXE's combat-preview tooltip (bottom left, beside the unit panel),
an EXE hover window that Esc did not close; it does not cover the map area compared.

**P13 from the pictures [L, I]:** at these cameras no model stands on an unexplored plot in the joined arm either (the
DLL does not reveal units, cities or improvements there), so the white mask's loss shows only as untinted models on
seen-not-visible plots; with no model on a seen plot at these cameras that part was not seen [open for C7]. *2026-11-15 (lead-0168): seen live by
3957: 8.1 (a sheep darkened on the seen plot (11, 23), stock) and 8.5 (seen-plot models lit in the joined arm, the white
mask, decision 3 (i)).*

### 2.10 Not verified / open (task 3951)

- A wrap view (two OverlayProj matrices in one frame) was not on screen: whether the mask gets one window per view is
  not shown live.
- Which FOWDrawMask pass (0, 1, 2) draws which class (settled by 3952 in 7.1, 2026-11-14); the hexagon cb layout beyond "24 matrices of 4 rows" (3950's).
- The dissolve: no capture fell inside a mask change (the earliest was ~36 s after a turn change).
- The cloud planes' heights and the particle texture's file (512 x 512 BC3, F775A8C1...).
- The TerrainFOW style name of the Madrid tiles (C4 with `tileStyle`).
- cdb reads of FluidFOW+0x1338 were not needed (FOWAnim read from the captures).

## 3. The FluidFOW mask builder and the FOW programs: behaviour (task 3950)

Task 3950, 2026-11-14 21:04-21:45 EST (app crash 21:10-21:28), static only (no game run). Sources: read-only headless Ghidra 12.1.4 on a copy
of the `civ5` project (EXE SHA-256 `b2ce96ae...c88e`, Steam build 4390913), helper `render/evidence/0168/3950/Dec3950.java`
with `dec.sh`; the shader dumps of task 2902 (`D:/c5-64bit-work/2900/2902/dump/asm/`, copied to
`D:/c5-64bit-work/0168/3950/asm/`); the x64 DLL fork's `CvGame.cpp` / `CvMap.cpp` / `CvPlot.cpp` for P10. Decompiler
output, batch lists and dump copies are archived on `D:/c5-64bit-work/0168/3950/` (D83, D133; the C: work copy and the project copy were removed 2026-11-14 21:45). Everything below is behaviour. Marks as in the header.

### 3.1 Answer in one paragraph (the route's key question)

**The FOW Mask is a stateless, order-independent rasterisation of the plot fog states into each view's OverlayProj
window, rebuilt from scratch every frame [C].** No history, no random numbers, no time and no thread timing enter it:
the per-plot state grid is written directly by the HexFOWStateChanged handler (one plot per event, or all plots), and
the render clears the target and draws one hexagon per plot of the views' plot rectangles with a MAX blend. So P8 holds
statically: at rest, and equally one frame after any change, the mask is a function of (plot states, the views' plot
rectangles, the OverlayProj matrices, the map's hex-to-world transform, the mask size from FOWLevel). **There is no
dissolve in the mask**: a plot that changes state is drawn in its new class in the next frame [C]. What moves in stock
is elsewhere: the cloud uv (FOWAnim, time), and at FOWLevel 3 a particle simulation drawn into the scene (3.5). The
particle simulation is the only history-carrying part (job threads, `rand()` seeding); it does not write the mask.

### 3.2 The state grid and how a plot change enters it [C]

- **Owner.** FluidFOW_System, static object `0xDA94D8`. Its per-plot state grid: data pointer `+0x13EC`, height
  `+0x13E8`, width `+0x13E4`; one dword per plot, **index = gridX * height + gridY** (column-major). Values:
  **0 visible, 1 seen (revealed, not visible), 2 unexplored**; any plot outside the map reads as 2.
- **Per-game init `0x5B6AF0`** sizes the three grids (`+0x13EC` state, `+0x1494` override list, `+0x153C` cull flags) to
  the map, sets every state to 0 (visible), every override to 0, every cull flag to 1, the override switch `+0x1304` = 0,
  `+0x12F8` = 1 (base layer enabled) and the dirty flag `+0x1344` = 1. It sets the cloud scale `+0x1338` (3.6). Only at
  FOWLevel 3 does it continue: reads `Assets\Effects\FOW\FluidFOWSettings.xml` (SpawnFrequency, Size*/Mass* means,
  ranges, deviations, FogHeight, HeightVariation, FluidDensity, Speed, MasterSize, ForceStrength, MaxRotate), calls the
  particle grid builder `0x729C40` and seeds particles over the world box with `rand()` (3.5).
- **The HexFOWStateChanged handler `0x67AAA0`** (registered by start-up `0x44A570` and removed by shutdown `0x683C70`
  [C]; that it is the B114 subscription of `coupling.md` 2 is [I] from the record; event record: hex x at +4, hex y at
  +6 (shorts, cube coordinates), mode byte at +8, "all plots" byte at +9). Mode byte -> state: **3 -> 0 visible,
  1 -> 1 seen, 0 -> 2 unexplored, 2 -> 0 visible**. With "all plots" set, every grid entry takes the state; else the hex
  is converted to grid (gridX = hx + (hy >> 1), gridY = hy) and that one entry is stored (`0x5862A0`, a plain store).
  Either way the dirty flag is set. The byte's encoding is the server's `FowBits` (3 visible, 1 revealed, 0 unexplored)
  [I: the EXE's translation from the DLL's FogOfWarModeTypes was not traced; the shader channels in 3.3 fix which state
  is visible]. In state 1 the handler also calls `0x44C820` every 10th event under a condition; that call does not write
  the state grid [C] (purpose not traced).
- **A second handler `0x6AC1D0`** (registered in the same start-up; event type not identified) drives an override:
  kind 0 = clear the override list and switch it on (`+0x1304` = 1); kind 1 = mark grid plot (x, y) in the list; kind 2 =
  switch off. While on, every plot that is not unexplored and not in the list is drawn as **seen** (3.3). Which game
  event posts it is open [I: a temporary "only these plots visible" view]; it is off after every per-game init.
- **The update `0x4090F0`** (per frame from `0x6FBA80` when view bit 15 is set; argument = the frame's delta time, the
  application timer's double at `[0x5F54F38]+0x10` cast to float) writes FOWAnim (3.6) and, when the dirty flag is set,
  rebuilds the **cull flags** `+0x153C`: a plot's flag is 1 when none of 21 neighbours around it (a radius-2 hex
  neighbourhood in offset coordinates, indices wrapped modulo the grid in both directions) is unexplored, and 0 in the
  two rows nearest each map edge (gridY < 2 or gridY > height - 2). The flags only feed FOW CULL (3.5).

### 3.3 The mask render `0x49C940` [C]

Called from the frame function `0x5893F0` after the "Clear" of the scene and after the Overlay_System render `0x6EAD50`,
before the terrain (`0x657070`), every frame while view bit 15 (FluidFOW) is set. Its flag argument is 1 when bit 3 of
GameViewState+0x1B8 is clear: then the target is cleared to (1, 1, 1, 1) and nothing is drawn (everything visible, a
"fog off" view). Normal case:

- **Target** `0xDA12AC` "FOW Mask": created by start-up `0x44A570`, **1024 x 1024 at FOWLevel 2 and 3, 512 x 512 at
  FOWLevel 0 and 1**, engine format 0x102 (RGBA8 in the stock frame, `terrain-colour-reference.md` 1). Cleared to
  (0, 0, 0, 0) each frame. **Blend state**: enable, colour ONE / ONE / **MAX**, alpha ONE / ONE / MAX, write mask RGBA
  (built at start-up by `0x8BCDE0` -> `ID3D11Device::CreateBlendState` [C]; its id `+0x1334` is the second argument of
  the mask's target begin, where the scene's "Clear" passes 8 and FOW CULL 3 [I: that argument selects the blend
  state]). With MAX the draw order does not matter.
- **Geometry** (built once at start-up, two sets of 24 hexagons in one vertex and one index buffer): each hexagon has 6
  inner vertices at the plot's own hexagon corners (in cube space, corner directions (2/3, -1/3, -1/3) and its five
  rotations, i.e. the cell boundary) with weight 1, and 6 outer vertices at the same corners scaled about the centre
  by **1.33 (set 0)** or **1.40 (set 1)** with weight 0; 16 triangles (inner fan of 4, ring of 12). Each vertex carries
  its position, its hexagon's slot 0..23 in the batch, and its weight. So every plot is a filled cell with value 1 and a
  **linear ramp to 0 over the band from 1.0x to 1.33x (1.40x for unexplored) of its hexagon**, measured along each
  corner-to-centre line.
- **Per-hexagon matrix** (FOWDrawMask's own constant buffer, 0x600 = 24 float4x4, one per slot): Translate(the plot's
  cube coordinates (hx, hy, -hx-hy)) * M_h2w `0xD91030` * Scale(1, 0.866, 1) * Translate(-ox, -oy, 0) (ox, oy =
  GridHeightmap +0xB4 / +0xB8) * **OverlayProj[v]** (`0xDA1690 + 0x40 v`). The first four factors are HexToWorld's x, y
  (`camera-and-picking.md` 5); the last maps world to the overlay window. **The mask therefore uses exactly the
  overlay's window per view** (P1, P3 windows [C]).
- **Which plots**: for each view v (g_NumViews `0xD90FA4`), gridX over [`0xDA1BC0`[2v], `0xDA1BE0`[2v]) and gridY over
  [`0xDA1C04`[2v], `0xDA1C24`[2v]) (the view's plot rectangle). Direct writers [C]: the strategic-view path of
  UpdateViews `0x51DF70` (x = the visible columns widened by one and clamped to [0, map width], y unclamped), the
  camera `0x4813D0` (the whole map, [0, W) x [0, H), for every view) and a static initialiser; the writer for the normal
  3D view was not found by direct reference (corrected 2026-11-14 23:00 by lead-0168 from 7.1: it is the view footprint `0x5D0370`, its writes through an
  indexed address [I]; Y from gyMin - 1 by the captures [L]) (open: 3951's capture or a cdb read of the range gives it). At most 2,048
  hexagons per pass over all views; more are dropped [C].
- **Three passes**, each over all views, 24 hexagons per draw:

| Pass | Plots drawn | Hexagon set (outer) | FOWDrawMask program pass | Writes (w = the vertex weight) |
|---|---|---|---|---|
| 0 | state 1 (seen), and every non-unexplored plot not in the override list while the override is on | set 0 (1.33) | 2 | (w, 0, 0, 1) |
| 1 | state 0 (visible) | set 0 (1.33) | 1 | (w, 0, w, 1) |
| 2 | state 2 (unexplored, and outside the map) | set 1 (1.40) | 0 | (0, w, 0, 1) |

  **Result per texel (MAX over all hexagons covering it): x = explored (seen or visible), z = visible, y = unexplored,
  w = 1 where any hexagon reaches, else 0** [C]. At a plot centre: unexplored (0, 1, 0, 1), seen (1, 0, 0, 1), visible
  (1, 0, 1, 1); across a boundary each channel ramps linearly over the outer band of the plot that owns it, and the
  neighbouring class's band overlaps it (both channels non-zero near edges). This settles P4: the second value the
  terrain reads is **z (blue)**, and the brief's contradiction "`t4.x * (1 - t4.y)`" is wrong in `terrain-surface.md` 1.

### 3.4 Who reads the mask, and how (P2) [C, dumps of task 2902]

| Reader | Mask slot, coordinate | Use |
|---|---|---|
| TerrainShader pass 0 PS `0x067E6E58` and pass 1 PS `0x067E8D68` | t4 at TEXCOORD1.xy (v3.xy), the same interpolant as the overlay t1 in both passes; reads .x and .z | colour = lerp(cloud t5 at v2.zw, colour, x); then colour = lerp(colour, TerrainFOW LUT t6(colour), x * (1 - z)); then distance fog |
| WaterShader ocean PSs `0x068179F0`, `0x0681B500`, `0x06820B00`; river PSs `0x06819F60`, `0x06826EA0` | t10 at v5.xy = OverlayProj (WaterFrame 0x040, VS `0x06815A48` / `0x06816A20`), the same coordinate as the overlay t9; reads .x and .z | **discard where x == 0** (no water or river over fully unexplored texels); colour (after the overlay) = lerp(constant grey 0.4, colour, x); then lerp to TerrainFOW LUT t11(colour) by x * (1 - z). No cloud texture: water under fog fades to flat 0.4 grey |
| `0x0682C078` (sixth WaterShader PS) | no mask | - |
| FOWBaseLayer PS `0x0677F210` | t1 at the layer point's OverlayProj uv; reads **.y** | 3.5 |
| FOWParticleShader PS pass 0 `0x0677D4B0` | t1; reads **.y** | 3.5 |
| FOWDrawMask pass 4 PS `0x0677ED60` | t1 .xyz, alpha 0.5 | a mask visualiser (no caller found) [I debug] |
| Building PS, ForestShader PS | t7 / t6 .xz | `disable-systems-and-masks.md` 5 (unchanged) |

So P2 holds [C]: every terrain, ocean and river PS samples the mask at its overlay coordinate. The OverlayProj-to-uv
mapping is ndc * (0.5, -0.5) + (0.5, 0.5) in the particle VS and the same minus (1, 1) in the water and base-layer
VSs; the two agree under wrap addressing [I: sampler address modes not read].

### 3.5 What FluidFOW draws into the scene (P5 statically false) [C draw calls and shaders, I blend and depth states]

`0x602B10` runs in the frame function **after the world draws** (terrain `0x657070`, the water and the other world
systems, FOW_System `0x55B8B0`) and before `0x519B90`, with the scene target as argument, when `+0x12F8` and `+0x12FC`
are set (both set at game start):

1. **FOW CULL** (`0x6EA7A0`, FOWLevel >= 1): quads of 64 x 55.4 world units (the plot's bounding box) at every plot of
   the views' rectangles whose cull flag is 1 (no unexplored plot within the radius-2 neighbourhood), drawn with
   FOWDrawMask program pass 3: the VS forces depth 0 and the PS outputs (0, 0, 0, 0) [C]; meant to fill depth so the
   cloud layers below fail the depth test there [I]. The target begin passes flags 3 [meaning I].
2. **FOW Base Layer** (FOWLevel >= 1): three horizontal planes of 64 x 55.4-unit quads tiling each view's world
   rectangle (`0xDA1B00..`, `0xDA1B30..`, `0xDA1B60..`, `0xDA1B90..`; *2026-11-18, ticket 0572 [C]: x from the clamped
   `0xDA1B00` / `0xDA1B30`, y from the unclamped `0xDA1B64` / `0xDA1B94`, no clamp of the blocks: section 11*), at heights 0.2 x 56 = **11.2**, 0.2 x 191.25 =
   **38.25** and 0.2 x 255 = **51** world units [I: the constant is the second argument of `0x517E10`, read as the
   quads' z], textured with `FogCloudTile_dif_01/02/03` [per draw object: layer 1
   `_02`, layer 2 `_03`, layer 3 `_01`] at uv = (world.xy + FOWAnim.zw * s) * FOWAnim.xy with s = **1.0, 1.5, 2.0**.
   FOWBaseLayer's own constant buffer (0xC50): WVP[24] at 0x000, World[24] at 0x600, OverlayProj[v] at 0xC00, the
   scaled FOWAnim at 0xC40 [C]. PS: m = mask.y at the plane point's OverlayProj uv; **discard where m < 0.01**; output
   colour = cloud rgb, alpha = m * cloud alpha [C]; blended with [I] source-alpha blending.
3. **FOW Particles** (FOWLevel 3 only): sprites of `FX_Fog_dif.dds` from the FluidParticles simulation (object
   `0x68F470`; per-frame job scheduled by `0x5B57F0` -> `0x76E460`; flow field `flowfield.dds`; spawn by `rand()`;
   sorted with `qsort_s` each frame, `0x59F8C0`), gated by mask.y like the base layer (discard below 0.01, alpha = m *
   sprite alpha). FOWParticleShader own buffer (0x90): matrix at 0x00, OverlayProj[v] at 0x40, (1, 1, 1, 1) at 0x80 [C].

So in stock, unexplored ground is the terrain PS's cloud base (t5) **plus up to three translucent cloud planes and (at
High) particles drawn over the finished world**, all fading out where mask.y falls below 0.01, i.e. about 0.4 of a
plot beyond the unexplored hexagons. The server has nothing that draws these: a pass of its own is needed (risk 2 of
the brief; 3.8). 3951's captures decide how much of the picture they are.

**Memory (corrects the epic brief's route (c) cost) [C static, I size]:** the 8 x 24 MB per-thread blocks (`0x68F470`,
only when its last argument = FOWLevel 3) and the particle grid (`0x729C40`, only from `0x5B6AF0` at FOWLevel >= 3)
exist **only at FOWLevel 3**. At FOWLevel 0-2 FluidFOW's own allocations are the mask target, two small vertex and
index buffer pairs and the three cloud textures. The -221 MB / -294 MB measurements (`allocations-and-memory-map.md` 5)
were taken on installs with `FOWLevel = 3` (the owner's `GraphicsSettingsDX11.ini` reads `FOWLevel = 3` on 2026-11-14;
`0xD90F94` is FOWLevel [I: the settings reader around `0x5D84D0`..`0x5D8DE6` references the "FOWLevel" string and
writes `0xD90F94`; that code is not a defined function in the project]).

### 3.6 FOWAnim and the cloud texture (P6, P7 statically) [C]

- **t5** = `0xDA12B0` = `FogCloudTile_dif_02.dds` at FOWLevel >= 1 (loaded by name at start-up; `_03` and `_01` are the
  second and third base layers). At FOWLevel 0 it is instead a 32 x 32 RGBA8 texture of zeros (black, alpha 0).
- **FOWAnim** (TerrainFrame 0x090, `0xDA1C40..4C`) = **(k, k, -2.0 * T, 0)** where T = the sum of the update's delta
  times since FluidFOW start-up (accumulator `+0x1320`, zeroed at start-up only, not per game; runs only while the map
  is drawn) and **k = floor(W * 0.002) / W**, W = the world box width `[0xD90FD8] - [0xD90FCC]` (set per game by
  `0x5B6AF0`): the cloud texture repeats a whole number of times across the map width, so it is seamless over the wrap
  [C]. Before the first game, FOWAnim = (0.002, 0.002, 0.002, 0.002). The constant -2.0 is `[0x9ED65C]`.
- **P7 statically**: xy constant per map; z decreases linearly with accumulated frame time at 2 world units per time
  unit; the time unit is the application timer's [I: seconds, as the other systems' updates fed by the same value].
  The server can compute FOWAnim from its own clock and the map width; only the phase (T) differs from stock.

### 3.7 The other programs and systems

- **FOW_System (key 14, `0xDAAA20`, start-up `0x5BB6E0`, render `0x55B8B0`) owns FOWCell (50)** [C]: its draw objects
  are built in `0x5BB6E0` ("FOW.cpp", material 0x32 = 50), and its render runs only when **FOWLevel == 0**. FOWCell's PS
  passes output the vertex colour with alpha 1 or 0.5. So P15 statically: at FOWLevel >= 1 FOW_System draws nothing;
  it is the low-quality fog that replaces FluidFOW's base layers at FOWLevel 0 [I purpose]. Corrects the inventory's
  "FOWCell under FluidFOW".
- **FOWDrawMask (52)**: pass 0-2 described in 3.3 (VS `0x0677A9F8`, PSs `0x0677DF08`, `0x0677E2F8`, `0x0677E6E8`);
  pass 3 FOW CULL (VS `0x0677B260`, PS `0x0677EAD8`); pass 4 the visualiser (VS `0x0677B8B8`, PS `0x0677ED60`). One
  draw object per hexagon set (`+0x948` set 0, `+0x950` set 1: 384 triangles = 24 hexagons, set 1 from index 1,152),
  one for FOW CULL (`+0x95C`, on the quad buffers). The program object `+0x924` (`0x680110`, "FOWDrawMask").
- **FOWBaseLayer (53)**: VS `0x0677C140`, PS `0x0677F210`; three draw objects `+0x94C`, `+0x954`, `+0x958` (slot 0 cloud,
  slot 1 mask), program object `+0x928`.
- **FOWParticleShader (51)**: pass 0 VS `0x067796F0` / PS `0x0677D4B0` (sprites), pass 1 VS `0x0677A050` / PS
  `0x0677DB38` (vertex colour, alpha 1; use not traced); draw object `+0x85C` of the particle object.

### 3.8 For the route (P8 and the size of route (a))

- **Route (a) is a small, exact job for the mask** [I size]: per frame, per view, draw the plots of the window with
  the EXE's own FOWDrawMask passes 0-2 read from the EXE file (D85), MAX blend, from `PlotRecord::fow` (3 -> 0, 1 -> 1,
  0 -> 2, 2 -> 0) and the hex-to-world transform the server already uses. The geometry and the per-hexagon matrices are
  built from 3.3. **C2: 2.5-4 h** with tests. Exactness risks: the plot rectangle per view (the EXE's 3D-path range was
  not read; a server range wider than the EXE's adds ramp texels at the window border that stock leaves 0), the 2,048
  cap, the override list (needs its event), FOWLevel (mask size 512 or 1024; read the player's `GraphicsSettingsDX11.ini`).
- **C3 (the dissolve): nothing to do in the mask** (0 h): stock switches the hexagon at the next frame. Any perceived
  dissolve in stock is the cloud drift or the particles.
- **New: the cloud planes (FOW CULL + three FOWBaseLayer planes)**, the EXE's own programs again, drawn by the server
  after its world and before the image goes to the composite: **2-3 h** [I], needs the server's depth buffer (or the
  cull quads alone) and the scene order of 3.5. Without it, unexplored ground shows only the terrain's t5 lerp (and
  grey 0.4 water), which may already be most of the stock look (3951's captures decide).
- **Particles (FOWLevel 3)**: a history-carrying simulation (`rand()` seeds, per-frame job, flow field); not
  reproducible frame for frame. Route (b)-style look-alike or an intended difference; or render as at FOWLevel 2
  (no particles), which stock itself does on lower settings.
- **What the server lacks**: nothing per team (the fog bits are the active team's, and the DLL re-sends every plot at
  an active-player change, 3.9); the map's world-box width for FOWAnim [I: = map width x 64]; the player's FOWLevel; the
  override event (open). No generation counter is needed beyond "fog bits changed" for caching, since the mask is
  rebuilt each frame in stock anyway.

### 3.9 P10 statically: the server's fog bits

- At a load the DLL sends no `GameplayFOWChanged` while graphics are not initialised (`CvPlot::updateFog`, CvPlot.cpp
  501-519); the server's **snapshot** (`AtLeaveUpdateBody`, `srv_remote_feed.cpp` 220-300) re-reads
  `GetActiveFogOfWarMode` for every plot when there is no table, the grid changed, the turn went back or jumped by more
  than one, or events were dropped; at **every turn change** every plot is re-read; between those, each
  `GameplayFOWChanged` patches its plot (`OnFowChanged`, 392-415) [C code].
- **Active-player change (risk 5)**: `CvGame::setActivePlayer` (CvGame.cpp 6285-6335), with graphics initialised,
  publishes the new player and calls `CvMap::updateFog`, which sends `GameplayFOWChanged` for **every plot** with the
  new active team's mode; so the server's records follow a hotseat or observer switch through events, without a turn
  change [C]. A queue overflow during that burst forces a snapshot at the next LeaveUpdate [C]. Deferred fog plots
  (`updateFog(true)`) are sent later by `CvMap::updateDeferredFog` [C]. Not covered: the brief's active-team guard
  (`GetActiveTeam() < 0` reads fog 0 = unexplored for every plot until the next refresh) [C].
- `OnFowChanged` ignores the `bSetAll` argument for the plot table (patches only (x, y)); the DLL never sends
  `bSetAll = true` (its only call passes false) [C], while the EXE's handler does honour it (3.2). Harmless today.

### 3.10 P11 and P14 statically

- **P11 (TerrainFOW LUT per style)**: the table is `0xD90E60 + 4 * style` [C: the terrain draw object `0x5AD770`, the
  CacheBake object `0x6CF830`, `0x520340` and the instanced-model draw `0x75A220` all index it with stride 4; style
  order Generic, America, Asia, Africa, Europe]. Every one of the 25 volumes (5 types x 5 styles) is filled by the same
  loader `0x681A00` from `<Type>_<Style>_ColorKey.tga` (`asset-pipeline.md` 3.5) [C], so the texel order that
  `bake::buildColorKeyLut` implements (reading order from the top row, BGRA -> RGBA, x fastest; confirmed on the Generic
  TerrainFOW volume by 3609 [L] and on the Terrain volumes by the bake) is the order for every style [C by the shared
  loader; per-style bit equality left to C4 / 3951's capture]. A style whose TerrainFOW file is missing keeps the
  identity volume.
- **P14 (the model shaders' mask coordinate)**: the model-pass shared buffers filled by `0x4FB320` (frame function, view
  bit 0x2) and `0x674AD0` hold, per view v: a matrix at rows 0-3, g_View[v] at rows 4-7, **OverlayProj[v] (`0xDA1690 +
  0x40 v`) at rows 8-11, FOWAnim at row 12**, the inverse view (`0xDA1410`) at rows 13-16 and fog terms after [C]. That
  is the layout the building VS reads ("CB1 rows 8-11", `disable-systems-and-masks.md` 5), so its mask coordinate is
  OverlayProj [C content; I that this buffer is shared id 16]. Consequence for decision 3 (ii): the EXE's model draws
  sample the mask in the overlay window (the server's mask would fit them) and read FOWAnim from `0xDA1C40`, which
  under D33 nobody writes (zero) [I].

### 3.11 Corrections owed in other files (for the lead; not edited here)

- `terrain-surface.md` 1: the FOW weight is `t4.x * (1 - t4.z)`, not `(1 - t4.y)`; the PS reads `.xz` (3.3, 3.4).
- `terrain-bake-passes.md` 3.3 row t10: the TerrainFOW LUT is `0xD90E60 + 4 * style` (the terrain draw object
  `0x5AD770` and CacheBake `0x6CF830` index the table with stride 4) [C]; asset-pipeline 3.5's table formula is right.
- `terrain-rendering-inventory.md` 2.5: FOWCell (50) belongs to FOW_System and draws only at FOWLevel 0; FluidFOW
  draws three cloud planes and (FOWLevel 3) particles into the scene after the world (3.5).
- `asset-pipeline.md` 3.2 rows 50-53 and 3.3 "the four FOW programs ... not traced": the own buffers are traced here
  (FOWDrawMask 0x600 = 24 matrices; FOWBaseLayer 0xC50; FOWParticleShader 0x90; FOWCell 0x40 not read); 3.4 t5 is
  `FogCloudTile_dif_02.dds` (FOWLevel >= 1).
- `render/src/terrainsets/sets.cpp` FOW rows (lines 182-190, 301-321, 474-492): the "not traced" own buffers and slots
  of 3.3, 3.5, 3.7 (FOWBaseLayer and FOWDrawMask t0 cloud / t1 mask; particle t0 sprite / t1 mask).
- `allocations-and-memory-map.md` 5 and `disable-systems-and-masks.md` 2 row 15 / 3: FluidFOW's memory cost is a
  FOWLevel-3 cost (3.5); the measurements should name FOWLevel.
- `disable-systems-and-masks.md` 5: the building VS's rows 8-11 are OverlayProj (3.10, P14).
- `water-system.md` 10.2: the ocean and river PSs also lerp to a constant 0.4 grey by mask.x before the LUT (3.4).

## 4. Premises: answers (lead, from sections 2 and 3)

Written 2026-11-14 22:14 EST by lead-0168. Captures: Tiny copy `C5H-0168-TINY_0018` at turns 20/21, stock arm on
`i0ship-26234371-ia`, c5h-a, FOWLevel 3, VT 45B3B433 (section 2 setup).

| # | Premise (brief) | Answer | Mark | Where |
|---|---|---|---|---|
| P1 | t4 sampled at the overlay coordinate | holds: the terrain PSs read t4 at the t1 interpolant | C | 3.4 |
| P2 | pass 0, ocean and river PSs read the mask the same way | holds; water also discards where x = 0 and lerps to a flat 0.4 grey by x (no cloud texture) | C | 3.4 |
| P3 | one 1024 RGBA8 target, every view's window, redrawn every frame | holds for size, format (11 mips), clear to 0 and per-frame redraw (FOWDrawMask only, MAX blend, before the terrain); 512 at FOWLevel 0-1 [C]; per-view windows [C] but no wrap view seen live | C, L | 2.1, 3.3 |
| P4 | x explored, z visible; centre values | **R explored, G unexplored, B visible, A drawn**; centres visible (1,0,1,1), seen (1,0,0,1), unexplored (0,1,0,1); window rim (0,0,0,0); edges ramp from the hex edge to 1.33-1.35x (1.42x unexplored) of the inner radius, both classes overlapping | C, L | 2.2, 3.3 |
| P5 | nothing of FluidFOW draws into the back buffer | **false**: after the models, FOW CULL (depth only), 18 FOWBaseLayer draws (three planes, three cloud files, alpha blend, depth test without write, gated by mask.y), and at FOWLevel 3 one particle draw | C, L | 2.3, 3.5 |
| P6 | t5 one of the cloud files, fixed | holds: `fogcloudtile_dif_02.dds` (hash equal to the install's), all cameras; FOWBaseLayer only samples it | C, L | 2.4, 3.6 |
| P7 | FOWAnim xy constant, z linear in time | holds: (k, k, -2 T, 0), k = 0.00195367 on this map (whole repeats over the map width), z falls 1.97/s, restarts per process | C, L | 2.5, 3.6 |
| P8 | settled mask deterministic in plot states and window | **holds**: bit-equal at rest (12 s, 48 s) and after a reload in a new process; stateless by construction | C, L | 2.6, 3.1 |
| P9 | stock's own shot-to-shot small away from edges and clouds | holds: median 1-2, p95 24-26, 88-90 % within 16; clouds move (median 8), mask edges do not | L | 2.6 |
| P10 | the server's fog bits equal the DLL's | the snapshot, turn refresh and events cover a load, every turn and an active-player change (the DLL re-sends every plot) [C]; live census left to C7 (done B(1)) | C | 3.9 |
| P11 | `buildColorKeyLut` gives every style's volume | one shared loader for all styles, stride 4 (`0xD90E60 + 4 style`) [C]; on screen one volume, equal to the Generic one already bound as water t11 [L]; per-style bit compare in C4 | C, L | 2.7, 3.10 |
| P12 | the server's fog does not touch the white mask | by design (nothing here writes `0xDA12AC`, the white texture, draw objects or config); C7 proves it with the log lines | C | 5.4 |
| P13 | no model on unexplored plots in stock | none seen at the two cameras; models read the mask and LUT like the terrain (tinted when seen, cloud base when unexplored); seen-plot models not seen live | C, L partly | 2.8, 2.9 |
| P14 | the building VS's mask coordinate is OverlayProj | holds: VS cb1 rows 8-11 = OverlayProj exactly; row 12 FOWAnim | C, L | 2.8, 3.10 |
| P15 | FOW_System draws nothing | holds at FOWLevel 1-3 (it draws only at FOWLevel 0) | C, L | 2.8, 3.7 |
| P16 | VT present | yes, folder hash 45B3B433 on c5h-a | L | 2 |

## 5. Route and design for the server (lead; MC review)

Drafted 2026-11-14 21:55 EST by lead-0168 from section 3; checked against section 2 at 22:15 (section 2 confirms the
mask, the cloud planes after the models, FOWAnim and P8 live). **Not yet reviewed by the MC.**

### 5.1 Route (decision 1): (a), the EXE's own FOW programs driven by the server

Section 3 removes the reason the brief gave for route (b): the mask is not a simulation. It is cleared and redrawn every
frame from the plot states as MAX-blended hexagons, so re-implementing it with the EXE's own FOWDrawMask passes 0-2 (read
from the player's EXE at run time, D85) is exact by construction at rest and at a change (no dissolve exists). Route (b)
(an own shader) would be no quicker and could only be less exact; route (c) (FluidFOW back on) is not needed. **No
interim (b) step**: route (a)'s mask, bound into the terrain and water draws, is itself the quickest way to close the
map-information leak (unexplored ground becomes the cloud base t5, unexplored water is discarded or fades to 0.4 grey,
section 3.4), so it is built first and the cloud planes after it.

What stock draws for the fog of war has four parts; the server builds each from the same inputs:

| Part | Stock (section 3) | Server | Exactness |
|---|---|---|---|
| Mask t4 / t10 | FOWDrawMask passes 0-2, 24 hexagons per draw, MAX blend, per view in OverlayProj[v], 1024 (FOWLevel 2-3) or 512 (0-1) | the same programs and geometry, from `PlotRecord::fow` and the overlay pair's OverlayProj (C2) | exact if the per-view plot range matches (open, section 2) |
| Terrain and water fog terms | t4 .x/.z, t5 = `FogCloudTile_dif_02.dds`, t6 = TerrainFOW LUT of the tile's style, FOWAnim; water t10, t11 | bound by the terrain and water modules (C4) | exact (the EXE's own PSs) |
| Cloud planes | FOW CULL (depth-0 quads where no unexplored plot is within 2) and three FOWBaseLayer planes at 11.2 / 38.25 / 51 world units, `FogCloudTile_dif_02/03/01`, FOWAnim x 1.0 / 1.5 / 2.0, gated by mask.y, after the world | the same programs over the server's colour and depth after its terrain and water (new child) | exact except the cloud phase (T) and the models below (5.4) |
| Particles (FOWLevel 3 only) | FluidParticles: `rand()`-seeded sprites, a per-frame job, sorted | **not drawn**: an intended difference (decision 7); *since ticket 0340 (2026-11-16, section 10): a look-alike re-implementation with the EXE's FOWParticleShader* | not reproducible frame for frame in any route |

### 5.2 Inputs and where they come from

- **Plot states**: `PlotRecord::fow` (bit 0 revealed, bit 1 visible) maps to the EXE's state as 3 -> visible, 1 -> seen,
  0 -> unexplored, 2 -> visible (section 3.2, the HexFOWStateChanged mode byte). The DLL re-sends every plot at an
  active-player change (3.9), so no per-team data and no new feed field are needed: **no change to `srv_feed.h` or
  `srv_remote_feed.cpp`**, so the parity pair (done D) tests only that the fog does not disturb the game.
- **Window**: the overlay pair's `OverlayFrame::overlayProj[v]`, `numViews` (0167's tap, frame N-1 with its texels):
  mask and overlay share the window, as in stock. Plot range per view: the plots whose 1.40x hexagon meets the window,
  **stock's own rule**, which is narrower than the window (rim plots read (0, 0, 0, 0), 2.2): 3952 reads the drawn
  plots from the captures' FOWDrawMask constant buffers and fits the rule (section 6); until then the plots whose 1.40x
  hexagon meets the window, wrapped in x.
- **Hex-to-world**: the server's own (`c5r::water` world_from_grid, the bake), which already equals the EXE's globals.
- **FOWLevel**: from the player's `GraphicsSettingsDX11.ini` at server start (mask size 512 or 1024; cloud planes at
  level 1-3; level 0 is FOW_System's FOWCell look, not built: decision 7).
- **FOWAnim**: (k, k, -2 T, 0), k = floor(W x 0.002) / W with W the world width of the map, T the server's accumulated
  frame time while the map is drawn (the water's clock, `water-system.md` 10.7). Only the phase of T differs from stock.
- **The art from the install** (D83, `c5r::gamefs`): `FogCloudTile_dif_01/02/03.dds` (EffectTextures.fpk), the
  `TerrainFOW_<Style>_ColorKey.tga` volumes through `bake::buildColorKeyLut` (a missing style file = identity, as the
  EXE's loader does), the FOWDrawMask (52) and FOWBaseLayer (53) programs from the EXE's .shader section.

### 5.3 Modules and children (wave 2 after the review)

- **3952 C2 `c5r::fow::MaskRenderer`** (`render/src/fow/`, `render/include/c5r/fow/`, `render/tests/fow/`):
  `init(dev, exePath)`; `build(ctx, plots (w x h states), views[] (OverlayProj, plot range), hexToWorld, size, out RTV)`;
  a mask dump tool. Acceptance: against the capture's t4 from section 2's plot census and OverlayProj, equal or each
  difference named. 2.5-4 h.
- **3955 the cloud planes `c5r::fow::CloudRenderer`** (same folders, own files): `init(dev, exePath, gameDir)`;
  `draw(ctx, colour RTV, depth DSV, mask SRV, views[], cull flags, FOWAnim)`; the FOW CULL flags (radius-2
  neighbourhood, wrapped, two edge rows) computed from the plot states. Acceptance: 3019-style replay of the captured
  cloud draws with the module's inputs. 2-3 h. (Replaces the brief's C3 "dissolve", which section 3 shows does not
  exist.)
- **3953 C4 terrain and water inputs**: terrain `Options::fow` (mask SRV, cloud t5, per-style t6, FOWAnim; default =
  today's constants, bit-equal), water and river t10 through `setTexture(Slot::FowMask)`. Needs the terrain files after
  0191 (MC #1109). 2-4 h.
- **3954 C5 `srv_fow_scene`**: per drawn frame, build the mask from the feed and the overlay pair, bind it to terrain and
  water, run the cloud pass after the world and before the image is handed to the composite; option `render_fow`
  (default 1); the failure route (5.5). Needs `srv_render_scene.*`, `srv_water_scene.cpp`, `srv_render_options.cpp`,
  `srv_render_host.cpp` (shared, asked). 2-3 h.
- **3956 C6 models' fog**: only if decision 3 takes (ii). **3957 C7** live acceptance, parity pair, ASan, research.
- Lead before wave 2: `render/include/c5r/fow/fow.h` with both classes stubbed, the scene's source interface, the
  shared-file grants asked.

### 5.4 What stays different from stock (named, D81 point 5)

- The cloud phase (T) from the server's clock; FOWLevel 0's FOWCell look. *Corrected 2026-11-16 by ticket 0340 (section
  10): the particles at FOWLevel 3 are drawn, a look-alike of stock's FluidParticles (the same behaviour from its own
  seed; stock's `rand()` state and history are not reproducible), measured statistically against stock (10.3). Replaced:
  "the particles at FOWLevel 3 (decision 7)".*
- **Models and the clouds' order**: stock draws its models before the cloud planes [L, 2.3], so cloud edges cover
  models near unexplored plots; in the joined frame the EXE draws its models over the server's image, so models stay
  uncovered under cloud edges [I how much shows: no model stood near an unexplored edge at the two cameras, 2.9]. Measured 2026-11-19 for a server-drawn landmark (wheat) under the cloud edge at the capital: stock hazes it too (mean opacity 0.46, the server 0.21) [L, ticket 4886, city-renderer.md 12.7].
- *Corrected 2026-11-16 by ticket 0339 (section 9): decision 3 (ii) is built and on by default (`model_fow=1`): the
  EXE's model draws read the server's mask behind the white handle, the cloud base layer and FOWAnim, so models on
  seen-not-visible plots are tinted as stock [L, 9.3]; what stays different is the window lag during pans (9.3) and the
  models over the cloud planes (the bullet above). Replaced: "Models on seen-not-visible plots keep the white mask
  (fully lit) unless decision 3 takes (ii) ..." (FOWAnim under D33: 0 at the install, [L] 9.3).*

### 5.5 Failure list (decision 4, R6 / D132)

- **At install (the first drawn frame), D125's route** (report, popup, end of game; never "draw without fog", D118): the
  FOWDrawMask or FOWBaseLayer program not found or not creatable from the EXE file; `FogCloudTile_dif_01..03.dds` not
  readable; the mask target not creatable; `render_fow=1` with `render_overlay=0` (decision 5 (ii)); no fog bits at the
  first drawn frame (active team below 0, the table missing).
- **In play, LOUD and counted, the game goes on**: a plot event outside the grid, a sequence gap or overflow (the
  snapshot repairs it), a stale or refused overlay pair (the mask keeps the last good pair, as the overlay does), a
  failed draw. A missing `TerrainFOW_<Style>` file is not a failure (the EXE keeps the identity volume too).

### 5.6 Decisions for the MC after C1 (the brief's 1-6, and a new 7)

1. Route: **(a)**, without an interim (b) step (5.1).
2. Dissolve: **moot** (none in stock's mask); its child id 3955 goes to the cloud planes, which are in scope.
3. Models: **(i)** keep the white mask in this epic; (ii) sized at 3-5 h, fits technically (P14), for increment 3 or a
   follow-up.
4. Failure list: as 5.5.
5. No overlay pair: **(ii)**, the fog needs the pair in the product; (i) only in tools and tests.
6. 0012 / 0073: superseded by D33 and this epic; close them at this epic's merge.
7. (new) FOWLevel 3 particles: **not drawn**, an intended difference, with a follow-up ticket for a look-alike of the
   FluidParticles behaviour if the owner wants it; FOWLevel 0 drawn as level 1 (named). *The follow-up is ticket 0340
   (2026-11-16, section 10): the look-alike is drawn at FOWLevel 3.*

## 6. Open questions

(lead-0168, 2026-11-14 22:14 EST; each with the child that settles it.)

- **Settled 2026-11-14 by 3952 (7.1): `plotRangeFromCamera` gives stock's rectangles exactly at both cameras.** Was: stock's per-view plot range in the normal 3D view: rim plots of the window read (0, 0, 0, 0) in the captures, so
  stock draws hexagons for fewer plots than the window holds; the writer of the 3D path's range was not found (3.3).
  3952 reads the drawn plots from the captures' FOWDrawMask cb0 (24 per draw) and fits the rule; a wider range would
  turn rim cloud into ramp texels.
- A wrap view (two OverlayProj in one frame) live: 3952 tests it offline, C7 live.
- **Settled by 3952 (7.1): pass 2 seen, 1 visible, 0 unexplored.** Was: which FOWDrawMask pass draws which class (3.3 says 0 seen, 1 visible, 2 unexplored; the captures' counts suggest
  otherwise at cam1, 2.2): 3952 from the captures' draw order.
- **Settled by 3955 (7.2): heights 11.2 / 38.25 / 51 and scales 1.0 / 1.5 / 2.0 in the capture, the gate offline, the particle texture `fx_fog_dif.dds`.** Was: the cloud planes' heights, the `mask.y` gate and the particle texture (512 x 512 BC3 F775A8C1, unmatched): 3955.
- W in k = floor(W x 0.002) / W: the capture's k = 1 / 511.857 fits W = 3583 (not 56 x 64 = 3584); 3954 takes W from
  the server's world box (the EXE's `[0xD90FD8] - [0xD90FCC]`).
- The override handler `0x6AC1D0`'s event (3.2): not needed unless a game event shows it; C7 watches for it.
- The TerrainFOW style names of the tiles on screen: 3953 (C4) with `tileStyle`.
- Models' distance fog under D33 (the brief's note for the MC): not measured here.

## 7. The modules as built

### 7.1 The mask module as built (task 3952)

Written 2026-11-14 22:54 EST by task 3952 (branch `task-3952-fow-mask-module`). Code: `render/src/fow/mask.cpp`, interface
`render/include/c5r/fow/fow.h` (MaskRenderer part), tests `render/tests/fow/mask_test.cpp`, tool `render/tools/fowmask/`
(`fowmask.exe`, `fowmask_input.py`, `fowmask_compare.py`), evidence scripts `render/evidence/0168/3952/`. Measurements on
3951's captures (section 2: Tiny `C5H-0168-TINY_0018` copies at t20 / t21, c5h-a, FOWLevel 3, VT 45B3B433, pin
`i0ship-26234371-ia`, stock arm), copied to `C:/c5-64bit-work/0168/3952/captures/`; the module on the workstation's GPU with
the programs of `CivilizationV_DX11.exe` build 4390913 (the Ghidra copy, SHA-256 `b2ce96ae...c88e`).

**Settled from the captures' FOWDrawMask cb0** (`fowmask_export.py` exports only the mask draws and the first terrain
draw; `fow_drawn.py` maps each non-zero hexagon matrix's row 3 back to its plot) [L, cam1_a, cam2_a, cam1_t21b, cam2_t21]:

- **Pass to class**: draw order seen, visible, unexplored; **program pass 2 = seen, pass 1 = visible, pass 0 =
  unexplored** (hexagon set 1, StartIndexLocation 1,152; the others set 0, start 0). Every drawn hexagon is of its pass's
  class (cam1 t20: 9 seen, 41 visible, 142 unexplored), so 3950's table (3.3) holds; 3951's "1/2/6 draws" were p2/p1/p0.
- **Slot order**: x outer, y inner (column-major); unused slots of the last draw hold an all-zero matrix (degenerate).
- **The drawn plots form a full rectangle per view** (cam1 X 1..16 x Y 18..29 = 192 = 9 + 41 + 142; cam2 X 3..13 x
  Y 21..28 = 88), every plot of it drawn in exactly one pass.
- **Vertex** (VB stride 40, 720 slots of which 576 used): POSITION float3 = the corner in cube coordinates
  ((2/3, -1/3, -1/3) and its five rotations; outer x 1.33 set 0, x 1.40 set 1), NORMAL float3 = (slot, slot, slot) with
  slot 0..23, COLOR0 / COLOR1 R8G8B8A8_UNORM = 0, TEXCOORD0 float2 = (w, w), w 1 inner, 0 outer. Index buffer R32_UINT,
  48 indices a hexagon (inner fan 5-1-0, 5-4-1, 1-4-2, 4-3-2; ring per corner pair (a, b = a + 1 mod 6): b, 6 + b, a and
  6 + b, 6 + a, a). The module's vertices are bit-equal to the capture's VB (cube corners as float 1/3, 2/3, scaled by
  float 1.33 / 1.40).
- **Matrix**: rows 0-2 = the linear part (cam1: 0.15384 = 64 x 0.0024038; row 1 (0.07692, 0.15746)); row 2 zero;
  row 3 = the plot centre's NDC (z 0.5, w 1). The module's matrices (product hexToWorld x Scale x Translate x OverlayProj
  in float, then (hx, hy, hz, 1) x it) are within 4.8e-7 of the capture's.
- **Rasteriser and blend state**: cull none, solid, depth clip on; depth off (LESS_EQUAL, no write); blend ONE / ONE MAX
  on colour and alpha, write mask all; viewport the whole target; **no texture or sampler bound** in passes 0-2.
- **Mips**: the target carries `D3D11_RESOURCE_MISC_GENERATE_MIPS`, and its lower mips hold the 2 x 2 mean of the level
  above (mip 1 against a box filter of mip 0: max 0.5, mean 0.006 levels, cam1_a) [L]: **stock calls GenerateMips** after
  the mask draws. The terrain's SRV is R8G8B8A8_UNORM with all 11 mips; the terrain draw's samplers 0-2 are
  MIN_MAG_MIP_LINEAR, clamp [L]; which of them reads t4 was not separated [I: the lower mips are read where the terrain is
  minified].

**Stock's per-view plot range** (open in section 6; [C] the view footprint `0x5D0370`, read 2026-11-14 with
`render/evidence/0168/3952/dec.sh`, output kept on C: only; [L] 4 of 4 captures equal): 0x5D0370 (the water's view
footprint, `water-system.md` 10.2 step 1) also writes the mask's ranges. Per view: the bounding box (min x, y; max x, y)
of the four frustum corner rays cut with z = 0 and z = 127.5, **unclamped**; each corner -> n = ((x + 0.5 [0xD90FE4]) /
sx, (y + 0.5 [0xD90FE8]) / sy, 0, 1) x M_w2h (0xD90FF0) -> cube rounding `0x5DA590` (each component rounded half away from
zero; when the sum is not 0 the component with the largest change is re-derived from the other two) -> the integer
transform at 0xD91130.. (identity [I], camera-and-picking.md 5) -> grid (gx = hx + (hy >> 1), gy = hy). Then
**X [gxMin - 2, gxMax + 2), clamped to [0, [0xD911C4]]; Y [gyMin - 1, gyMax + 1)**, Y unclamped (rows outside the map are
unexplored hexagons). The writes go to `0xDA1BC0 / 0xDA1BE0` (x, clamped) and `0xDA1C00.. / 0xDA1C20..` (the raw corner
values). The Y start is **gyMin - 1** by the captures (cam1: gyMin 19 -> 18; cam2: 22 -> 21); the static reading of which
global the mask render takes for y0 gave gyMin, so one of 0x49C940's y reads is the clamped `0xDA1BC4` (= gyMin - 1) [I];
the captures decide. `[0xD911C4]` was taken as the map width [I] (a camera at the east edge would show whether it is
width - 1). Checks: `fow_rangefit.py` (Python) and `mask_test.cpp` `range_from_camera_stock_cameras` (the module):
cam1 X [1, 17) Y [18, 30), cam2 X [3, 14) Y [21, 29), equal to the drawn rectangles. The OverlayProj window alone does
not fix the range (cam1 and cam2 share the window top y = 640 but end at rows 29 and 28).

**The module** (`c5r::fow::MaskRenderer`; interface changes against the lead's stub, all additive):

- `init(dev, exePath, size)`: FOWDrawMask passes 0-2 from the .shader section (VS `0x0677A9F8` shared, PSs `0x0677DF08`,
  `0x0677E2F8`, `0x0677E6E8`, through `terrainsets::findSet("FOWDrawMask")`), the input layout above, the two hexagon sets
  (576 vertices, 2,304 indices, immutable), the 0x600-byte dynamic cb0 shared by VS and PS, the MAX blend, depth-off and
  cull-none states, and the target: size 1024 or 512 only, R8G8B8A8_TYPELESS, full mip chain (11 / 10), UNORM RTV and SRV,
  GENERATE_MIPS. Errors: BadLayout (no device, another size), Io / BadMagic / Truncated (the EXE), NotFound (a blob missing:
  "FOWDrawMask blob 0x0677a9f8 not in the .shader section"), Device.
- `build(ctx, in, stats)`: checks everything first (states null, size 0 or above 4096, `stateCount` != width x height, a
  state above 2, placement missing or not finite, views null or 0 or above 4, an OverlayProj not finite or with a zero
  scale, a range wider than 4,096 plots); on an error nothing is drawn and the target keeps its contents. Then clear to 0,
  three passes (seen with program 2 / set 0, visible 1 / set 0, unexplored 0 / set 1), all views per pass, x outer y
  inner, 24 hexagons a draw with the unused slots zeroed, at most 2,048 hexagons a pass (the rest counted in
  `MaskStats::dropped`), then GenerateMips. A plot's state: y outside the map or (no wrap) x outside -> unexplored; with
  wrap, x mod width. The override list of 3.2 is not modelled (no event known). Leaves IA, VS, PS, OM (no target), RS and
  the viewport changed, HS / DS / GS unbound.
- Range when `View::x1 <= x0`: `plotRangeFromCamera` (stock's rule above) when `View::viewProj` is set and
  `MapInput::worldWidth` and `worldHeight` are > 0, else `plotRangeFromWindow` (every plot whose 1.40x hexagon's bounding box
  meets the OverlayProj window; cam1: X [2, 17) Y [16, 31), wider than stock's, so its rim draws ramps where stock has the
  clear).
- **`fow.h` changes**: `MapInput` + `stateCount`, `hexToWorld[16]`, `gridScale[2]`, `gridOrigin[2]` (the server's
  `heightfield::HeightGrid` has hexToWorld, sx, sy, ox, oy), `worldHeight` (with `worldWidth` = `c5r::water::World` width
  and height); `View::viewProj` now also feeds the range; free functions `nominalPlacement`, `convertFowBits` (counts bytes
  with bits above 1), `plotRangeFromCamera`, `plotRangeFromWindow`; constants `kHexagonsPerDraw` 24,
  `kMaxHexagonsPerPass` 2,048, `kMaxRangeSpan` 4,096. The CloudRenderer part is unchanged.

**The comparison** (`render/evidence/0168/3952/compare_all.sh`: `fowmask_input.py` -> `fowmask.exe` -> `fowmask_compare.py`
against mip 0 of the capture's t4, the frame-start mask, equal to the frame's by P8; the live Tiny M_h2w, sx, sy, ox, oy
and world box of `D:/c5-64bit-work/i2-ground-truth/tiny/grid_constants.json`; the range by the module's camera rule, which
equals the drawn rectangle, so the "stock range" and "camera rule" runs are byte-identical):

| Capture | All four channels equal | R equal | G equal | B equal | A equal | Texels differing | By |
|---|---|---|---|---|---|---|---|
| cam1_a (t20) | 99.9857 % | 99.9971 % | 99.9896 % | 99.9968 % | 100 % | 173 channel values | +-1 |
| cam2_a (t20) | 99.9886 % | 99.9940 % | 99.9970 % | 99.9950 % | 100 % | 146 | +-1 |
| cam1_t21b | 99.9866 % | 99.9981 % | 99.9896 % | 99.9977 % | 100 % | 153 | +-1 |
| cam2_t21 | 99.9879 % | 99.9940 % | 99.9970 % | 99.9944 % | 100 % | 153 | +-1 |

- Per plot class of the texel's nearest plot (cam1_a): visible R, B, A 100 % equal, G 78 texels +-1; seen R, G, A 100 %,
  B 11 texels +-1; unexplored 30 / 31 / 23 texels +-1 in R / G / B, A 100 %. Texels where stock drew nothing (A = 0,
  32,078 at cam1, 91,240 at cam2): all equal (the range matches).
- **Every difference is +-1 on a ramp texel** (stock's value 2..255 there, never a centre or a clear texel): the
  per-hexagon matrices differ from stock's by up to 4.8e-7 (float product order of D3DX against the module's), which moves
  a ramp edge by a fraction of a texel and the interpolated weight by one level. The vertices are bit-equal.
- With the window rule instead of stock's (no camera): 94.3 % (cam1) / 85.7 % (cam2) of texels equal; the rest is the
  wider range's rim (A 96.9 % / 91.3 %).
- Visual Checks (diagnostic, offline mask pictures, D112): `vc-20261114-225252-workstation-89e8` (cam1_a),
  `vc-20261114-225254-workstation-e1a2` (cam2_a), each with stock's t4 as reference and the difference figure.

**Tests** (`render/tests/fow/mask_test.cpp`, C5R_EXE needed; 13 tests): the target as stock (1024: 11 mips, TYPELESS,
GENERATE_MIPS; 512: 10 mips); one plot per class (centres (255, 0, 255, 255), (255, 0, 0, 255), (0, 255, 0, 255), the
clear (0, 0, 0, 0) outside, pass counts and draws); the edge ramp (B 255 to the edge at 32 units, about half at 37.3, 0
at 43.5; G of the unexplored neighbour to 0 at 44.8; both channels high at the edge; mip 1 within 2 levels of the 2 x 2
mean); two wrap views (a plot reaching the mask only through the wrap copy, and unexplored without wrap); empty and
all-visible maps; the 2,048 cap (2,399 unexplored -> 2,048 drawn, 351 dropped, 87 draws); the window rule; stock's camera
rule at cam1 and cam2 with the live and the textbook placement, the east-west clamp, and its negative cases; build taking
the camera rule (192 hexagons at cam1) and the window rule without a world box; `convertFowBits`; negative init (no
device, sizes 300 and 2048, a missing EXE file -> Io, an EXE whose .shader section holds one valid program but no
FOWDrawMask -> NotFound, build before init -> NotFound); negative build inputs (NaN and zero-scale OverlayProj, plot
tables of 63 and 65 for 64, state 7, null states, width 0, placement missing or infinite, 0 and 5 views, null views, a
10,008-plot range, no context), each leaving the target as it was.

**Corrections owed in other sections** (for the lead; this task edits 7.1 only): 2.2 "which FOWDrawMask pass draws which
class is not separated here" and 2.10 -> settled above (p2 seen, p1 visible, p0 unexplored); 3.3 "the writer for the
normal 3D view was not found by direct reference" -> it is the view footprint `0x5D0370` (its references to the range globals are DATA
references, the writes going through an indexed address [I]), with Y from gyMin - 1 by the captures; section 6's first and third
open questions -> settled here; `asset-pipeline.md` 3.2 row 52 -> the own buffer is traced in `sets.cpp` (this task).

**Not verified**: a wrap view live (two OverlayProj in one frame; the module draws each view's range through its own
matrix, tested offline only); `[0xD911C4]` (the x clamp; width taken); the y0 global of 0x49C940 (gyMin - 1 from the
captures); FOWLevel 0-1 (512 target) live; the 2,048 cap live (a Tiny view never reaches it); the override list (3.2); the
module inside the server (3954) and the terrain and water reading it (3953).

### 7.2 The cloud planes as built (task 3955)

Written 2026-11-14 22:52 EST by task 3955 (branch `task-3955-fow-cloud-planes`, lead-0168). Code:
`render/src/fow/cloud.cpp` (`c5r::fow::CloudRenderer`, `cullFlags`, `hexToWorldXY`, `planeBlocks`; interface in
`render/include/c5r/fow/fow.h`, CloudRenderer part). Tests: `render/tests/fow/cloud_test.cpp` (no game files),
`cloud_game_test.cpp` (C5R_EXE, C5R_GAME_DIR). Evidence tools: `render/evidence/0168/3955/` (`export.sh`,
`cloud_draws.py`, `cloud_cb.py`, `replay_prep.py`, `cloud_replay.cpp`, `build_replay.bat`, `replay_all.sh`,
`replay_compare.py`, `compare_*.json`). Inputs: 3951's captures cam1 t21b (`c5h-a_20261114-214835_frame2681.rdc`) and
cam2 t21 (`..._frame3774.rdc`), copied to `C:/c5-64bit-work/0168/3955/captures/` and re-exported there (game data, not
committed, D83; the C: folder was removed at hand-back, the per-draw constants (`cb/`), the replay manifests, numbers
and A | B | difference figures are archived in `D:/c5-64bit-work/0168/3955/`); census `census_t21reload.json`; the setup of section 2 (c5h-a, FOWLevel 3, VT 45B3B433, pin
`i0ship-26234371-ia`, stock arm). Behaviour only here, no shader text (D83).

**What the captured cloud draws hold [L, both cameras].** The export holds each constant buffer's frame-start contents
only; FOWBaseLayer's own buffer (3,328 B, DYNAMIC) and FOWDrawMask's (1,536 B) are rewritten by Map/Unmap before each
draw, so `cloud_draws.py` rebuilds them per draw from the zip.xml (frame start plus every Unmap in execution order, the
0154 method; its draw numbering equals the export's: 626 and 443 draws).
- **Vertex and index buffers** (shared by FOW CULL and the planes, IMMUTABLE): 24 unit quads, stride 32: POSITION
  float3 (corner x, y in {-1, 1}, 0), NORMAL float3 (x, y, slot 0..23), TEXCOORD float2; corners in the order (1, 1),
  (-1, 1), (1, -1), (-1, -1); indices R32_UINT, per quad (2, 1, 0), (3, 1, 2); every draw is `DrawIndexed(144)`.
- **Plane quads** (FOWBaseLayer pass 0): World[slot] = Scale(32 sx, 32 sy, 1) x Translate(cx, cy, h), WVP[slot] =
  World x View x Proj (the VP solved from each slot agrees to 1.5e-4), cb rows 192-195 OverlayProj, row 196 =
  (k, k, z x s, 0). **Heights h = 11.2, 38.25, 51 [L]** (3950's reading confirmed); **s = 1.0, 1.5, 2.0 [L]** (row 196
  z = -178.464, -267.696, -356.928 at cam1); textures plane 1 `_02` (BC1), 2 `_03` (BC3), 3 `_01` (BC3) [L]. The quad
  centres are **the 64-sample blocks of the water's footprint** (`water-system.md` 10.2): cx = float(bx x 64) x sx +
  32 sx + minX, cy likewise with sy and minY; block order bx outer, by inner, 24 per draw. Block ranges: cam1
  [4, 16) x [20, 32) (12 x 12 = 144 quads, 6 draws per plane), cam2 [5, 13) x [23, 32) (8 x 9 = 72, 3 draws per plane;
  the issue's "3 x 3" is 3 draws per plane). The same frames' ocean draws hold 132 and 64 quads = [4, 16) x [20, 31) and
  [5, 13) x [23, 31): **the cloud planes take one more block row at the far (high-y) end than the ocean** [L, two
  cameras; why: I, the planes stand up to 51 units above the ground, which shows further north on a pitched camera].
  `planeBlocks()` is the ocean's rule with by1 + 1. *Corrected 2026-11-18 by ticket 0572 (section 11) [C, decompile of
  the plane draw `0x517E10`]: the planes are not the ocean's rule. Their rows come from the view's footprint **before**
  its clamp to the world box (`0xDA1B60` / `0xDA1B90`, y) and their columns from the clamped one (`0xDA1B00` /
  `0xDA1B30`, x); no block index is clamped (by0 can be negative, by1 past the box), the far end is by1 + 2 (one more than
  the ocean's), and a view keeps at most 2,048 quads. At these two mid-map cameras both rules give the same blocks, so
  the replay below could not tell them apart; at a map's south or north edge they differ (the seams of 0572). Replaced
  as the rule: "`planeBlocks()` is the ocean's rule with by1 + 1".*
- **FOW CULL quads** (FOWDrawMask pass 3, one draw of 9 quads at both cameras): World = Scale(32 sx, 32 sy, 1) x
  Translate(HexToWorld(plot).x + 32 sx, HexToWorld(plot).y + 32 sy - 128, 102); the culled plots are exactly the nine
  the 21-plot rule gives from the t21 census: (8, 23), (8, 24), (9, 23), (9, 24), (10, 23), (10, 24), (11, 24),
  (11, 25), (12, 25), in gx-outer, gy-inner order [L]. With M_h2w of `camera-and-picking.md` 9 the centres match the
  captured ones to 0.0005 world units (`cloud_test.cpp`). The -128 in y and the height 102 put the depth-0 quad, on
  screen, over the plot's own ground at the default 45-degree pitch [I]. Unused slots of a draw are zero matrices [C].
- **The cull flags' 21 plots [C]**: the update's offset table is dx, dy in [-2, 2] without the four corners (a 5 x 5
  box minus 4 = 21, the plot itself included); indices wrap modulo the grid in x and y; rows y < 2 and y > H - 2 are 0,
  i.e. **rows 0, 1 and H - 1** (3.2's formula; the issue's "two rows at each map edge" is not what stock does).
- **The cull plot range [C]**: FOW CULL loops gx over [X0, X1] and gy over [Y0, Y1 + 1] of the same per-view globals
  (0xDA1BC0 / 0xDA1BE0 / 0xDA1C04 / 0xDA1C24) that the mask loops as [X0, X1) x [Y0, Y1) (3.3), skips plots outside
  [0, W) x [0, H) (no wrap), at most 2,048 per view. The module takes View's range in the mask's convention and adds
  the one column and two rows; with no range given (x1 <= x0) it draws every flagged plot whose quad centre lies within
  the footprint widened by 64 x 128 units (an approximation until 3952 fits stock's range, section 6).
- **States [L]**: FOW CULL blend off with write mask 0, depth LESS_EQUAL with write; planes blend SRC_ALPHA /
  INV_SRC_ALPHA (alpha ONE / ONE, ADD), depth LESS_EQUAL without write; both rasterizer SOLID, CULL_FRONT,
  FrontCounterClockwise TRUE, depth clip; both samplers MIN_MAG_MIP_LINEAR, WRAP, MaxAnisotropy 0; views: cloud
  `BC1_UNORM` / `BC3_UNORM`, mask `R8G8B8A8_UNORM` (no sRGB); target `R8G8B8A8_UNORM`, depth `D24_UNORM_S8_UINT`.
- **The mask's mips [L]**: the mask the planes sample has all 11 mips filled and consistent (G mean 162.43 at mip 0,
  162.0 at mip 10): the EXE generates them after drawing it. The planes sample at mip > 0 on oblique views, so **the
  server's mask needs a generated mip chain too** (for 3952 / 3954).
- **The gate**: the EXE's PS discards where mask.y < 0.01 and outputs alpha = mask.y x cloud alpha [C]; offline it
  holds with the EXE's own PS (G = 2/255 leaves the target untouched, `cloud_game_test.cpp`). Not isolated in a live
  stock picture (the stock frame cannot be split into world and clouds without a replay of the whole scene).
- **The particle texture (FOWLevel 3, 2.3) is `fx_fog_dif.dds` [L]**: the mip-0 SHA-256 F775A8C1559ACDC1... of the
  capture's 512 x 512 BC3 equals the DXT5 file in `Resource/DX9/EffectTextures.fpk` (pure_vp install, 2026-11-14).

**The module** draws, per frame, after the server's world and before the composite: FOW CULL for every view, then
plane 1 for every view, plane 2, plane 3; WVP = World x View::viewProj in float; FOWAnim from `fowAnim()`. Inputs it
needs beyond the mask's (fow.h `CloudWorld`): M_h2w, sx, sy, ox, oy, the world box and the sample counts, all in the
server's height field (`HeightGrid`) and `water::worldFromGrid`; the scene depth cleared to 1 (the server's D32_FLOAT
works with LESS_EQUAL as stock's D24S8). `init` fails (D125's route, design 5.5) when a program or a cloud file is
missing.

**Replay against the captured draws [L, 2026-11-14 22:46 EST, RTX 4080]** (`replay_all.sh`): arm A = the capture's 19
(cam1) / 10 (cam2) cloud draws with their own per-draw constants, buffers, cloud textures and mask; arm B = the module
with its own inputs (census states, the camera solved from the draws, the Tiny placement, the captured FOWAnim) over the
same captured mask; both onto the stock clear colour (84, 84, 84) with depth 1. Largest channel difference per pixel:

| Camera | Cloud pixels (either arm changed the base) | Median / p95 / max | Within 16 | Equal | Draws, quads (A = B) |
|---|---|---|---|---|---|
| cam1 t21b | 1,390,697 | **0 / 0 / 1** | 100 % | 99.95 % | 19: cull 9, planes 3 x 144 |
| cam2 t21 | 978,520 | **0 / 0 / 61** | 99.9999 % | 99.86 % | 10: cull 9, planes 3 x 72 |

Causes: the differences of 1 are float rounding of the module's WVP (World x VP in one product, VP solved from the
capture) against the EXE's chain (Scale x Translate x View x Proj); the only pixels over 4 are two at cam2, (402, 401)
and (1331, 400), where four plane quads meet and arm A leaves a one-pixel crack that arm B covers (the same rounding at
a shared corner). No difference comes from the block ranges, heights, textures, FOWAnim scaling, cull plots or states.
Pictures (diagnostic, no terrain under the clouds): Visual Checks `vc-20261114-224735-workstation-ffda` (cam1),
`vc-20261114-224737-workstation-af94` (cam2), each with arm A as reference and an A | B | difference figure.

**FOWLevel 0 (MC #1131 (7)), from the static description (3.3, 3.5, 3.6, 3.7) [C; not captured live]:**
- Stock at FOWLevel 0 still draws the mask (FOWDrawMask passes 0-2, **512 x 512**, as at level 1), and the terrain,
  water and models still read it; but **t5 is a 32 x 32 RGBA8 texture of zeros** (black, alpha 0) instead of
  `fogcloudtile_dif_02.dds`, so the terrain PS's lerp(t5, colour, mask.x) fades unexplored ground to **black**; water
  fades to its flat 0.4 grey as at every level (3.4).
- **No FOW CULL, no cloud planes** (both need FOWLevel >= 1, 3.5) and no particles.
- **FOW_System draws FOWCell** (material 50) only at FOWLevel 0: its own cell geometry over each view's plot ranges with
  vertex colours, alpha 1 or 0.5 (two PS passes) (3.7); the colours and which cells get which alpha were not traced.
- **The server drawing level 0 as level 1** (design 5.4, decision 7): the same 512 mask, but t5 = `_02` (unexplored
  ground shows the cloud texture, not black), FOW CULL and the three cloud planes over unexplored plots, and no FOWCell:
  where stock shows black ground under FOWCell cells, the server shows clouds. Making level 0 exact needs FOWCell traced
  (its own buffer 0x40, its vertex colours) and a capture at FOWLevel 0.
- A capture at FOWLevel 0 was **not** taken: the brief allowed it only if it fit in 30 minutes, and claiming an
  instance, changing and restoring `GraphicsSettingsDX11.ini` by hash, a RenderDoc launch, the camera and the export
  would not have (3951's comparable series took 1.2 h). Open for C7 or a follow-up.

**Open (3955):** stock's per-view plot range for FOW CULL (shared with the mask's, section 6; the module's fallback is
an approximation); no capture had a wrap view (two views): the module draws FOW CULL for all views, then each plane for
all views [I order]; the planes' extra far block row was seen at two cameras of one map only. Section 6's items "the
cloud planes' heights, the mask.y gate and the particle texture" are answered above (heights and texture [L], gate [C]
plus offline).

### 7.3 The scene in server.exe: `srv_fow_scene`, `render_fow`, the failure route (task 3954)

Written 2026-11-14 by 3954 (branch `task-3954-fow-scene`, from main 589c9bda + the epic at 59e1a779). Code
`server/src/render/srv_fow_scene.{h,cpp}`; additive blocks (MC #1131 grant (a)) in `srv_render_scene.{h,cpp}`,
`srv_water_scene.cpp`, `srv_render_host.cpp`. Against `c5r::fow` (`render/include/c5r/fow/fow.h`): built on the
lead's stubs (init returns `Unsupported`), then (23:00, epic 15bcd94f) on 3952's mask and 3955's cloud planes (7.1, 7.2).

**Per drawn frame** (`Scene::decide` / `Scene::draw`) [C code + stand-in tests]:

1. `decide()` asks `FowScene::admit()` after it chose a camera: the frame is **held** (not drawn) until the fog can be
   drawn: the feed's plot table of the field's map size, a revealed plot (at the install only) and an overlay pair.
   **The field's grid is 4 rows taller than the DLL's map** (`HeightGrid::mapH` = the DLL's height + 4 margin rows,
   `heightfield.h`): the table must be `mapW x (mapH - 4)` [L, 2026-11-14 23:10: 3954's first live fog run compared
   56x36 with the grid's 56x40 directly, held every frame and took the install-failure route after 10 s; fixed in
   4c44ca8d; the stand-in field now has the margin rows too].
2. `draw()`, before the terrain: `FowScene::beforeWorld()` builds the mask with `MaskInput` = the plot states
   (`PlotRecord::fow` through `stateFromFowBits`; bits above 1 counted), the map (table width x height, `stateCount`,
   `wrapX` from the camera block, the world box and the placement below), the views (`overlayProj` = the overlay pair's
   `OverlayProj[v]` as the EXE wrote it, `viewProj` = camera block `view[v] x proj`, view `min(v, numViews - 1)` when the
   pair and the camera block differ in `numViews`, counted `view mismatches`; plot range left 0, so 3952's
   `plotRangeFromCamera` gives stock's range from `viewProj` and the box). **Rebuilt only when** the fog generation
   (+1 on any plot state change or a new table), the pair's generation or slot changed, or the last build failed; else
   the kept mask is bound again. The mask goes to the water and river t10 (`WaterSceneFow`, a free function defined in
   `srv_water_scene.cpp` as 3704's `WaterSceneOverlay`; `WaterScene::draw` calls `setTexture(Slot::FowMask, mask)`,
   which the module shares with the river draw; null = the module's own input, the picture before 3954).
3. **For the terrain (task 3953)**: `Scene::fow().terrainInputs()` = `FowTerrainInputs {on, mask (t4), cloudBase (t5),
   fowAnim[4] (TerrainFrame 0x090)}`, valid during `draw()` after step 2; `on` false = keep today's constants. The
   TerrainFOW LUT (t6) is the terrain module's. **Wired by 3954 after 3953 merged (2026-11-14 23:30, 7.4's call)**:
   `Scene::draw` copies them into `c5t::Options::fow` (enabled, mask, cloudBase, anim) when `on`; the mask's window is
   `Options::overlay.proj`: the bound overlay pair's copy matrices, or, when no overlay is bound (the tap lost or
   stale), the fog's kept pair through `OverlayCopiesFromFrame` (`FowScene::keptPair`; t1 stays transparent), and the
   same matrices go to the water through `WaterSceneFow(w, mask, proj)` (used for `WaterFrame.OverlayProj` only when
   no overlay is bound) [C test: `no_overlay_pair` checks `FrameStats::fowOn` and t10 with the pair lost].
   `setTileStyles` is **not called**: the server has no continent art types (`PlotRecord` carries none; the server's
   bake runs without `plotArtTypes`), so every tile keeps style 0 (Generic) after each field; all five TerrainFOW files
   of the installs measured are equal (7.4), so nothing differs today [I for other installs or mods]. A feed field for
   the art types is a follow-up (proposal in the hand-back).
   *Superseded 2026-11-15 by task 4305 (epic 0169, ticket 0341; `terrain-surface.md` 12.6, and the note at the end of
   7.4): the feed carries the art types (`PlotRecord::artType`), the scene calls `setTileStyles` after every field and
   when the art types change, so t6 is the tile style's TerrainFOW volume in the product. This item describes 3954's
   build. Replaced as the current state: "`setTileStyles` is **not called**", "the server has no continent art types",
   "every tile keeps style 0".*
4. After the terrain and the water, into the same colour and depth: `FowScene::afterWorld()` = `CloudRenderer::draw`
   (FOW CULL and the three planes) with the mask, the same `MaskInput` and FOWAnim; skipped while the mask failed.

**Where the inputs come from** [C]:

- **Plot states**: the host's drained `SrvFeed::Batch` goes to `Scene::fowFeed()` (every batch, also without changed
  plots). The table is re-read (`SrvFeed::CopyPlots`, about 20 B per plot) on a new `snapshotGen`, an overflow event,
  a sequence gap, a change of the batch's `dropped` counter, or any changed plot; the states are compared plot by plot,
  so a change of another field (owner, route) neither redraws nor rebuilds. A fog change marks the scene dirty (a
  redraw under a still camera, as the overlay's new generation does).
- **The world box and the placement** (since the merge of 3952 / 3955): from the scene's height field of this load
  (`HeightGrid`): the box through `c5r::water::worldFromGrid` (width (samplesX - 1) sx, height (samplesY - 1) sy,
  bit-equal to the EXE's 0xD90FCC..0xD90FDC: 3583 x 2216.094 on Tiny) -> `MapInput::worldWidth / worldHeight` and
  `CloudWorld` min / max; the placement `HeightGrid::hexToWorld` (M_h2w as the tap carries it), sx, sy, ox, oy ->
  `MapInput::hexToWorld / gridScale / gridOrigin` and `CloudWorld`; samplesX / Y -> `CloudWorld`. Without a field the
  camera block's copies (aabbMin / Max, worldHeight, h2w, sx, sy, ox, oy). A placement that is all zero or not finite
  (the stand-in's field) takes `c5r::fow::nominalPlacement`, noted once and counted `nominal placement frames`.
  *Replaced 2026-11-14 23:10: W was the camera block's `aabbMax[0] - aabbMin[0]` before the merge.*
- **World width W** (FOWAnim's k = floor(W x 0.002) / W): the box's width above (3951's Tiny: 3583 gives k = 7 / 3583 =
  1 / 511.857, section 2.5), else `wrapPeriod` (also 64 W - 1) when not finite or not positive [C test
  `all_visible_todays_picture`: the stand-in's 255 gives k = 0; the formula checked at 3583].
- **T** (FOWAnim z = -2 T): the server's clock, summed over drawn frames with at most 250 ms per step (stock's T runs
  only while the map is drawn); `render_water_freeze=1` holds it at 0 (diagnostics). Phase differs from stock (5.4).
- **FOWLevel**: `render_fow_level` (server.cfg, -1..3, default -1), else `FOWLevel = N` of
  `<Documents>\My Games\Sid Meier's Civilization 5\GraphicsSettingsDX11.ini`, Documents = `C5_DOCUMENTS` (a harness
  instance's redirection, inherited by server.exe from the EXE) else `SHGetFolderPathW(CSIDL_PERSONAL)`; a missing file,
  key or a value outside 0..3 gives 3 (noted, not LOUD) [I that stock reads a missing key as 3]. Mask size 1024 at 2-3,
  512 at 0-1; cloud planes at every level (level 0 drawn as level 1, decision 7). Read at every device start.
- **The overlay pair**: the scene's own copy of the newest `OverlayFrame` (`m_ovlFrame` while `m_ovlHave`); the fog keeps
  its own copy of the last usable pair's matrices, so a stale or lost tap keeps the last good window (`kept pair
  frames`, counted, not LOUD). A pair with numViews 0 or above 4, a non-finite matrix or a zero x / y scale is not used
  (`bad pairs`, LOUD once per reason, the last good pair kept).
- **The mask's window** *(added 2026-11-22 by ticket 0674)*: the pair's OverlayProj, unless camera N's footprint leaves
  it; then a window made from camera N by the EXE's rule, with the overlay moved into it. Key `render_fow_mask_window`
  (server.cfg / render_config): `camera` (default) | `pair` (the picture before 0674). As built:
  [pan-coherence.md](pan-coherence.md) section 3.

**Option** `render_fow` (server.cfg): 1 (default) | 0 (no fog object does anything: terrain and water constants, no
cloud pass; the picture before 3954 bit for bit [C test]). `HostConfig` lives in `srv_render_host.h`, which 3954 did not
hold: `render_fow` and `render_fow_level` are parsed by `ParseHostConfig` into file-scope values beside it (3607's
`g_waterMode` pattern; `RenderFowOption()`, `RenderFowLevelOption()`), to move into `HostConfig` when that header is next
changed. `SceneOptions::fow` defaults to **0**, so tools and the 0167 / 3607 scene tests that build a `Scene` directly
keep today's picture and LOUD counts; server.exe takes server.cfg's value (default 1). **Note for every live arm**:
`render_overlay=0` or the composite's `overlay_tap=0` with `render_fow=1` is an install failure (decision 5 (ii)); an
overlay-off arm must set `render_fow=0` too. On the product path such keys go through vpproxy.ini
`server_cfg_extra=<file>` (TEST ONLY, queue ticket 0307, `proxy-design.md` 25; asked by 3954 at 22:58); 3954's
`live_arms.sh` uses it. *Replaced 2026-11-14 23:40: "the product path has no switch for server.cfg keys".*

**The failure route** (design 5.5, decision 4, MC #1131 (4)) [C code + stand-in tests]:

- **The install is the first frame the scene wants to draw** (camera OK, a field), not the renderer's start (the front
  end and the load screen draw no map). The modules are initialised at `Scene::init` and their status kept. At the
  install: mask or cloud module not started, `render_overlay=0` (or no overlay source), or the overlay tap OFF = an
  install failure at once; no plot table, no revealed plot ("no fog bits", the active team below 0) or no pair = the
  frame is held, and an install failure after `fowWaitMs` (10 s; `SceneOptions::fowWaitMs`) of the fog's own time from
  the first wanted frame (*corrected 2026-11-16, ticket 0429: render-thread gaps above 1 s between two checks are not
  counted; was "10 s from the first wanted frame" in wall time, which the server's own post-field stall ate; see 7.5*)
  (*corrected 2026-11-17, ticket 0491: the 10 s end the wait only once the EXE has presented 30 map frames since the first
  wanted frame, else the slow-load bound of 12 x the wait (120 s) does; see 7.6*).
  An install failure is LOUD once (`LOUD: fog of war: <reason>; unrevealed plots cannot be hidden, no frame is
  drawn (design 5.5, D118, D125)`) and `FowScene::failed()` carries the text.
- The host runs `FowFatalStep(scene, cfg.fatal, done)` after every poll, beside `OverlayFatalStep`: once, it calls
  `HostConfig::fatal(kFowFatalExitCode = 5, text)` (server.exe: `SrvRemote::ReportAndExit`, which does not return), the
  overlay's route of `overlay-system.md` 10.3 (one exit code for "the 64-bit path cannot start its picture"). **No frame
  is drawn from the first wanted frame until the server ends** (decide() returns false while held or failed). Without a
  hook (tests, test hosts such as `render_host_test.exe`): the note `no fatal hook ... draws on WITHOUT the fog` and the
  scene draws on without the fog (0167's rule), counted `without`.
- **In play nothing is fatal** (D132 R6), each LOUD once per reason (or the first three) and counted: a fog event for a
  plot outside the grid (`setAll` events excepted), a feed sequence gap, an overflow (both re-read the table), fog bits
  above bit 1, a bad pair, a failed mask build (retried every frame; the water then gets no mask and the clouds are
  skipped) or cloud draw. A table of another size than the field (a new load before its snapshot) holds the frames
  (noted once per episode, LOUD once if longer than `fowWaitMs`), then `drawing again after N held frames`.
- Logs: per second ` | fow=<OFF|WAITING|ACTIVE|FAILED|WITHOUT> level with held built gen pair revealed T`; totals
  `fow totals: state frames with held without masks clouds build errors cloud errors bad pairs kept pair frames view
  mismatches fog events state changes table reads seq gaps overflows outside events bad bits`.

**Tests** (`server/tests/render/fow/`: `test_fow_build.py` builds `test_fow_scene.exe` from every `server/src/render`
unit + the test, as 0167's `test_scene_build.py`; `test_fow_scene.py` runs it): a Scene on WARP with the EXE's terrain
programs (C5R_EXE), a fake overlay source, fake `c5r::fow` modules (the fake's "clouds" clear the target red when any
plot of its input is unexplored, so a cloud pass before the terrain's clear would leave no red) and a fake plot table.
Eleven cases, each asserting its outputs and the log: `all_visible_todays_picture` (render_fow=1 with every plot
visible = render_fow=0's picture bit for bit, t10 = the mask, terrain inputs, FOWAnim, the map and cloud world from the
field, the nominal placement noted once, no redraw without a change, 0 LOUD), `real_modules` (3952's and 3955's
modules on WARP with the EXE's FOW programs and the install's cloud files, C5R_GAME_DIR: installed, mask built with
hexagons, cloud draws counted, no error, no LOUD; unexplored plots change the frame), `one_plot_per_class` (bits 3 / 2 visible, 1 seen, 0 unexplored; the clouds after the terrain),
`snapshot_replaces_table` (a snapshot and a changed plot redraw and rebuild under a still camera; an owner change does
not; a table of another size holds, never fatal, then draws again), `overflow_rereads` (overflow, gap, dropped,
outside event, bad bits: LOUD 4, re-reads, never fatal), `no_overlay_pair` (render_overlay=0 and tap OFF fail at once;
WAITING holds then fails after the wait; a pair within the wait installs; lost pair kept; bad pair LOUD once, last good
kept), `option` (render_fow 0 / 1, render_fow_level -1..3, others refused with the key; fow=0 touches nothing),
`install_failure_fatal_once` (the real modules with an EXE path that does not exist (the stubs' Unsupported before
the merge): fatal once, code 5, the text names the mask module; 0 frames drawn with a
hook; without one LOUD once and drawing on; cloud module failure; no revealed plot; no table), `in_play_fault_never_fatal`
(build and cloud failures LOUD once each, frames drawn, hook never called), `fow_level` (the ini key, a bad value, no
key, no file), `clock` (z = -2 T, a 600 ms pause counts 250 ms). **Results** [C, 2026-11-14, workstation]: plain
(clang-cl) 22:43 10 of 10 PASS; MSVC `/fsanitize=address` 22:44 10 of 10 PASS, 0 ASan reports. The server's render
suites with the fog in the tree (`pytest server/tests/render`: render host, residency, 0167's overlay scene and reader,
3607's water, the fog's), plain, 23:03: 143 passed, 0 skipped (`render_host_test.exe` runs with `render_fow=1` and the
stubs: one fog LOUD line, no hook, draws on: the host tests count only their own LOUD lines). After the merge of 3952 /
3955 (real modules, 11 cases): plain 11 of 11 PASS, ASan 23:06 11 of 11 PASS with 0 reports (before the margin-row
fix); plain again 23:13 after it, 11 of 11. **Final** (61a178d8: the terrain wiring and the depth copy, epic 828d96b6):
plain 23:47 11 of 11 PASS; MSVC ASan 23:48 11 of 11 PASS, 0 reports; the server's render suites (`pytest
server/tests/render`, plain) 23:37 on 45eebc4b (before the depth copy): 143 passed, 0 skipped (with the real modules and
no overlay pair, `render_host_test.exe` waits the fog's 10 s install, then draws on without it: no hook).

**Live (a): the install failure on the dev product path** [L, 2026-11-14 22:53-22:57, workstation instance c5h-b
(Goldberg, VT), Tiny `C5H-0168F-TINY_0021R` = a copy of 3951's turn-21 state (`A7B077A9`), drivers
`shell/evidence/0168/3954/live.sh` + `run_3954j.py` (3951's joined arm: ServerRenderer = 1, composite + manifest, D33
A'' with Overlay on, x64 DLL 74588135), server.exe `dc642836` release + proxy `0c29146b` + composite `c2f4b118` of
commit c17aed09 (the c5r::fow stubs), pack=0]: render.log `fog: FOWLevel 3 (<instance>\...\GraphicsSettingsDX11.ini)`,
`mask module Unsupported: c5r::fow::MaskRenderer is a stub`; 22:56:23.403 `fog: plot table 56x36 (generation 1, 57 plots
revealed)` (= 3951's census, 46 visible + 11 seen); 22:56:25.243 the first wanted frame (`terrain: state NO_FIELD ->
OK`); .249 the one LOUD line `LOUD: fog of war: the FOW mask (c5r::fow::MaskRenderer: the EXE's FOWDrawMask programs,
1024 target) could not start: Unsupported: ...`; 0 frames drawn (`drawn=0`, `published=0`); server.log `remote: exit
code 5 (fog of war: ...)` at +56.43 s; vpproxy.log `server pid 48672 exited (code 5)`, `call rt:LockSync ... failed:
PeerDied`, `FATAL: the server failed a call (on_server_death=exit): the process ends with exit code 3`; the EXE gone
(the harness saw state `gone` 22:56:29); restore by hash ok. No picture (the map never showed). As 0167 12.5 (a), this
layout has pack=0: no report folder, popup or config.ini restore (those are (b)). Logs kept in
`F:/c5-64bit-work/0168/3954/live/out/run-logs` (D141).

**Live (b): D125's route on the shipped pack layout** [L, 2026-11-14 23:03-23:05, oldpc testbed job
`20261114-225934-23ea`, instance tb2; `shell/evidence/0168/3954/neg_fow_job.py --fault fog` over
`tools/testbed/jobs/release_d125.py` unchanged (0167 3706's pattern; no file or switch fault: the server of the stub
build cannot install its fog), submitted by `submit_d125.sh`; the stub build of (a), pack `3954-d125` by
`tools/release/pack.py` (x86 = Release-5.4.8's DLL `262aee88...`, x64 `74588135...`), pack=1, config.ini in the A''
form by `vp64helper on`; save `TB-TINY_0008` (`b2ceb663...`, the testbed's Tiny, not 3951's state: the testbed runs the
release tag's mods)]: server.log `LOUD: fog of war: the FOW mask ... could not start: Unsupported ...` at +36.31 s,
`remote: exit code 5 (fog of war: ...)` at +36.35 s; the EXE ended by the proxy 92.1 s after the launch, no "Game
Crash" box; one report folder, no `.dmp`; **report.txt itself names the reason** (`server.log, last FAILED or LOUD
line: ... result: FAILED: fog of war: the FOW mask ...`, beside `server pid 4860: exit code 5`; the 0167 follow-up of
12.5 (b) is in this proxy), and `server.log.tail.txt` names it too; `vp64helper failed` restored config.ini (SHA-256 =
the player's before the run), wrote `failed=1`, no side file, the popup on screen (picture
`vc-20261114-230437-oldpc-2f0a`, DIAGNOSTIC: a stub build is not a fault a player meets); the instance restored by hash
(`backup_restore_ok`, provision verify rc 0). The job's rc 5 comes from its own 14th check only ("a log tail names the
fog's reason" looked for tail texts in `report_facts`, which lists the tails by name without their text): the fact
holds (grep of the fetched report folder: `report.txt` and `server.log.tail.txt` both carry "fog of war"). 13 of 14
checks pass as run, 14 of 14 in fact. Results fetched to `F:/c5-64bit-work/0168/3954/d125`.

**Live (c): the fog drawn on the dev product path** [L, 2026-11-14, c5h-a, Tiny `C5H-0168F-TINY_0021R`, drivers
`live_fog.sh` / `run_3954j.py` with `C5_3954_MODE=fog` (3951's two cameras, popups closed, one DIAGNOSTIC picture each
with 3951's stock picture as the reference)]:
- *Attempt 1* (23:08-23:14, server `8f2059b5` of 77942c64, 3952 + 3955 merged): `fog: plot table 56x36 ... read`,
  then `fog: frames held from frame 82: the plot table is 56x36, the field 56x40`, and after 10 s the install failure
  `LOUD: fog of war: the plot table is 56x36, the field 56x40 10023 ms after the first frame of the map`, exit code 5,
  the game ended: the margin-row bug above (the route itself worked: no frame drawn without the fog).
- *Attempt 2* (23:16-23:21, server `08d257f6` of 4c44ca8d, no terrain fog yet): `fog: installed at frame 83 after 0 ms:
  table 56x36 (57 revealed, fog generation 1), pair generation 51 (1 views), 0 frames held`; per second `fow=ACTIVE
  level=3 with=30-31 built=30-31` (the mask is rebuilt about every frame: each Present brings a new overlay generation);
  0 LOUD lines in render.log and server.log; restore by hash ok. Pictures (Visual Checks, issue 3954, DIAGNOSTIC):
  the cloud planes cover the unexplored plots at both cameras where stock has them; the ground under the cloud edges and
  over seen plots still shows the cache colours (the terrain's fog inputs were 3953's, not yet wired), and the models of
  the EXE stand over the clouds (5.4).

**Live (d): three arms with the terrain's fog wired** [L, 2026-11-14 23:29-23:43, c5h-a, the same save and driver,
`live_arms.sh` (`run_3954j.py`, `C5_3954_MODE` = fog / fowoff / ovloff; the off arms through vpproxy.ini
`server_cfg_extra=` (0307), unset before each restore), server.exe of 45eebc4b (epic 828d96b6: 3952, 3953, 3955, 0307 +
3954's wiring), each arm prep / run / restore, restores by hash ok; logs `F:/c5-64bit-work/0168/3954/run-logs/<arm>`,
pictures Visual Checks issue 3954 (DIAGNOSTIC, D112)]:
- **fog** (render_fow=1): installed at frame 82 after 0 ms (0 held), `fog 1s: fow=ACTIVE level=3 with=26-31 built=26-31
  hex=9/40/39 drop=0 cull=9 planes=72/72/72` (seen / visible / unexplored hexagons of the view, 9 FOW CULL quads, 72
  blocks per plane), 0 LOUD in render.log and server.log. The pictures now show stock's look by eye: clouds over the
  unexplored plots, the seen plots darkened by the TerrainFOW volume and the unexplored ground under the cloud edges as
  the cloud base. **But most of the EXE's models were missing** on visible ground (the units and horses around Madrid
  and east of the river, most of Madrid's buildings) against the fowoff arm's picture: the cloud pass's FOW CULL
  writes depth 0 where no unexplored plot is within 2 (7.2), stock runs it after its models (2.3) so nobody reads that
  depth again, but in the joined frame the server's depth goes to the composite and the EXE draws its models against
  it after the server's image [L by the pictures; I that depth is the mechanism, confirmed by the fix below]. **Fixed
  (2026-11-14 23:46)**: `FowScene::afterWorld` copies the scene's depth into a scratch texture of the same description
  (MiscFlags 0) and runs the cloud pass on the copy; the scene's depth keeps the world's [C test
  `all_visible_todays_picture`: a fake cloud pass that clears its depth to 0 leaves 0 zero texels in the scene's depth;
  negative run with the copy disabled: 19,200 of 19,200 texels 0, FAIL]. Cost: one depth copy per drawn frame
  (indication only). **Re-run with the fix** [L, 23:49-23:53, c5h-a, server of 61a178d8, `live_arms.sh c5h-a fog`]:
  installed at once, `fog 1s: fow=ACTIVE ... hex=9/40/39 cull=9 planes=72/72/72`, 0 LOUD in render.log and server.log;
  the pictures show the units, horses and Madrid's buildings again, with the clouds, the seen-plot tint and the cloud
  base as before: by eye close to 3951's stock picture except the cloud phase and the models over the cloud edges (5.4).
  Logs `run-logs/fog-depthcopy` (the first fog arm: `run-logs/fog-nodepthcopy`, pictures `F:/c5-64bit-work/0168/3954/captures/fog-nodepthcopy`).
- **fowoff** (`render_fow=0`): `fog: render_fow=0: no fog of war`, `fow=OFF`; the picture is the one before epic 0168
  (terrain, water and models of the unrevealed plots show: the D33 leak of 2.9).
- **ovloff** (`render_fow=0`, `render_overlay=0`, MC #1204 after 0307's gray picture): `render_overlay=0: no overlay`,
  terrain `state=OK drawn=20-31` per second, 0 LOUD; both pictures show the whole world without borders: **not gray**.
  0307's gray shot (with a main build that has no fog code) is not reproduced; its cause was not looked into (reported
  to the MC on thread 0168, 23:44).

**Live (e): D125's route with the REAL modules on the shipped pack layout, a fault a player can meet: the install has
no `fogcloudtile_dif_01.dds`** [L, 2026-11-17 17:00-17:04 EST, ticket 0342, workstation, Goldberg instance c5h-b
(claimed, a test copy; never the owner's install), VT present; drivers `server/evidence/0342/s01_inputs.sh`,
`s02_composite.sh`, `s03_fault.py prep|run|restore`, `s04_evidence.py`; evidence `server/evidence/0342/run1/`
(`facts.json`, `logs-excerpt.txt`, the report's `report.txt` / `helper.log` / `popup-summary.txt`, `prep-state.json`,
`restore.json`; local paths scrubbed); the whole work folder archived in `F:/c5-64bit-work/0342/run1` (D141)].
- **The build: 0463's validated take 2 of pin batch 3** (verdict validated 2026-11-17 16:28, D162 yes; tools commit
  f8e9f424b, fork 4f4daac70, layout `x64-4f4daac7-pf8e9f42`; archived by 0463 in `F:/c5-64bit-work/0463/take2` and
  `layouts/`). **Pack manifest `manifest.json` SHA-256 `88c83c85f36b36605ba2fed50d04eee8575cafb6ef057b1481e45ed58f686ca3`**
  (release Release-5.4.8; x86_dlls `262aee88...` = the tag's own 32-bit DLL; x64 DLL `CvGameCore_Expansion2_x64.dll`
  `2d737bd5...`; server.exe `ed829bda...`, purity `ef0072e9...`), all seven pack files checked against it after the copy;
  the take's proxy `4815ed81...` and vp64helper.exe `2ab9ae71...`. The server at f8e9f424b has the real `c5r::fow`
  modules (it predates 0491's install-wait change, which this fault does not reach).
- **The layout = an i2 install as the installer lays it** (`tools/installer/build_installer.py` payload, `install.iss`):
  the take is thin (Release-5.4.8's descriptor has no `shell_config`; an i2 take is i2-rc1, not taken yet), so the
  compositor was built at the same tools commit f8e9f424b from a `git archive` copy (`shell_composite.dll` `865e4cbd...`,
  its build's `shell_composite.ini` `1c0933e2...`, `shell_composite.sha256` by `proxy/tools/shell_manifest.py`); in
  `(1) Community Patch`: the proxy, vp64helper.exe, the tag DLL as `CvGameCore_Expansion2_x86.dll`, `VP64\` = the pack
  byte for byte (laid by `provision.py set-proxy --bin <pack> --fallback-dll`, plus its manifest.json and purity.tsv),
  **vpproxy.ini = the installer's text** (`mode=on`, `fallback_dll=CvGameCore_Expansion2_x86.dll`, `x64_dir=VP64`,
  `pack=1`; no test key), then **`vp64helper on`** (config.ini's D33 keys in the A'' form, VP64Options.ini `[Renderer]
  ServerRenderer = 1`; `status` exit 3 = on). Save: a copy of the testbed's Tiny `REL548-TINY_0008` (`b2ceb663...`, turn
  8) as `C5H-0342-TINY_0008`. Not the installer itself (no i2 installer exists yet): the files and settings it would lay.
- **The fault**: `fogcloudtile_dif_01.dds` exists in this install only inside `Resource/DX9/EffectTextures.fpk` (190
  entries; no loose copy, no other pack, none in MODS: `server/evidence/0342/fpk_find.py`). The instance's copy (SHA-256
  `db76b3df...`, backed up first) was rewritten with that index entry renamed to `fogcloudtile_dif_x1.dds` (same length;
  payloads, offsets and size unchanged; `b8767791...`), so neither the EXE's nor the server's file system (`c5r::gamefs`)
  has a file of the old name; `_02` and `_03` stay.
- **Timeline** (server.log seconds from the server's start; vpproxy.log seconds from the proxy's boot, about 22.7 s
  earlier [I: from the server's exit at 53.176 and the proxy's PeerDied at 75.921]): 11.776 `fog: FOWLevel 3 (...): ... mask module ready, cloud module NotFound: fogcloudtile_dif_01.dds not
  readable from the install: NotFound: gamefiles: no file named 'fogcloudtile_dif_01.dds' (1778.4 ms)` (noted at
  `Scene::init`, not LOUD); `fog 1s: fow=WAITING` while the EXE loads; **53.156 the first wanted frame** (`terrain: state
  NO_FIELD -> OK`, present 23); **53.157 the one LOUD line** `LOUD: fog of war: the cloud planes
  (c5r::fow::CloudRenderer: FOWBaseLayer, fogcloudtile_dif_01..03.dds) could not start: NotFound:
  fogcloudtile_dif_01.dds not readable from the install: ...; unrevealed plots cannot be hidden, no frame is drawn
  (design 5.5, D118, D125)` (the same line once in render.log, `fow=FAILED`), then `fog: the 64-bit path cannot go on
  ... the server ends (exit code 5)`; **53.176 `remote: exit code 5`**. Proxy: 75.921 `channel 0: wait failed inside a
  call: PeerDied`, `server pid 58244 exited (code 5)`, `report: the 64-bit path failed: stage server-death, code PeerDied;
  D33 session ...`, 76.099 the report folder written, 76.293 `helper: started ... vp64helper.exe failed --pid ...`,
  `FATAL: ... (no play, D125): the process ends with exit code 3`. Wall clock: launch 17:00:07, EXE gone 17:02:54
  (181 s after the launch, the driver's poll), popup seen by the dialog watcher 17:02:59 (kind `patch`).
- **No frame drawn**: every per-second render.log line of every module has `drawn=0` (120 lines) and the scene's
  `published=0` in all 40 lines; the map was never shown without its fog.
- **D118 / D125's outputs**: one report folder `VP64\reports\20261117-170254` (report.txt, helper.log,
  popup-summary.txt, vpproxy.ini, VP64Options.ini, manifest.json and five log tails; no `.dmp`); **report.txt names the
  reason**: `stage: server-death`, `server pid 58244: exit code 5 (0x00000005)`, `server.log, last FAILED or LOUD line:
  ... result: FAILED: fog of war: the cloud planes ... fogcloudtile_dif_01.dds not readable from the install ...`;
  `vp64helper failed` restored config.ini to the player's bytes (SHA-256 = before `on`), no side file left
  (`vp64-config-side.ini`, `vp64-options-side.ini`), wrote `failed=1` into vpproxy.ini, and showed the popup ("The 64-bit
  part of the patch failed, so the game was ended. Your config.ini is back to your own settings ...", the report's path,
  the hand-in line); no other box, no "Game Crash" box. The popup of the ended game was closed by the claim's owner
  (`cli.py dismiss --button Close --patch-popup`, COMMON 29; rc 0, `settled.mode` `patch_box`, marker cleared).
  **11 of 11 facts** of `s03_fault.py` true.
- **The popup's own text names the stage and the IPC code, not the fog** ("The 64-bit part of the patch failed
  (server-death: PeerDied)"): the proxy's view of a server that exited; the reason is in report.txt and the server.log
  tail (as in Live (b)). A player sees "server-death: PeerDied" [L]; whether the popup should carry the report's reason
  line is a product question, not checked here.
- Restored [L, 17:03:59-17:04:11]: vpproxy.ini back to set-proxy's copy by hash, the added files removed, the report
  folder moved into the work folder, `set-proxy --original`, `frontend.py restore`, the backup restored by SHA-256 (all
  18 entries equal or absent as before, EffectTextures.fpk `db76b3df...` again, config.ini `9b50852e...`,
  VP64Options.ini `4deb0536...`), `provision.py verify` rc 0; the claim released. No picture was taken: the instance's
  windows are cloaked on the workstation and the check is the logs' (D112: a picture of the popup would be the only
  player-visible output, not taken; 0491 fault2 the same).
- Not verified: the installer's own i2 run (none built; the layout above is its payload by hand); a fault on the
  other two cloud files or on the FOW programs (same `CloudRenderer::init` path, not run); the modpack (DLC) form.

### 7.4 C4: the fog inputs of the server's terrain and water draws (task 3953)

Task 3953 (impl-3953 under lead-0168), 2026-11-14 22:24-, branch `task-3953-fow-terrain-water-inputs`. Code:
`render/include/c5r/terrain/terrain.h` (`Fow`, `Options::fow`, the Renderer calls below), `render/src/terrain/terrain.cpp`;
tests `render/tests/terrain/fow_test.cpp` (`c5r_terrain_fow_test`) and `render/tests/water/fow_mask_test.cpp`
(`c5r_water_fow_mask`); evidence tools `render/evidence/0168/3953/`. Section heading 7 is the lead's; this is 7.4 only.

**What the terrain module binds** (into the EXE's own TerrainShader PSs, pass 0 and pass 1; 3.4):

| Input | Off (the default) | `Options::fow.enabled` |
|---|---|---|
| t4 mask | 1x1 (1, 1, 1, 1): x 1, LUT weight x (1 - z) = 0 | `fow.mask` (`MaskRenderer::srv()`), a Texture2D R8G8B8A8_UNORM view; null = `BadLayout` |
| mask coordinate | OverlayProj = identity without an overlay | `Options::overlay.proj` per wrap copy, **also when `overlay.srv` is null** (then t1 stays the transparent 1x1): the mask shares the overlay's window (P1) |
| t5 cloud base | 1x1 black | `fow.cloudBase` (`CloudRenderer::cloudBase()`), any Texture2D view; null = `BadLayout` |
| t6 TerrainFOW LUT | 32^3 identity | the volume of the tile's style: `fowLut(style)` |
| TerrainFrame 0x090 FOWAnim | 0 | `fow.anim` (`c5r::fow::fowAnim`); not finite = `BadLayout` |

- **LUTs**: `init(dev, exe, gameDir)` reads `TerrainFOW_<Style>_ColorKey.tga` for the five styles through
  `bake::buildColorKeyLut` (the shared loader's rule, 3.10); a missing file keeps the identity volume (the EXE's loader
  does too); a file that is there but does not parse **fails init**, naming the file (as the river's ColorKey; the water
  module's t11 skips a bad file instead). `setFowLut(style, tga, size)` replaces one (null = identity; unchanged on error),
  `fowLut(style)`, `fowLutsFromFile()`.
- **Tile style**: `setTileStyle(tx, ty, style)`, or `setTileStyles(artTypes, mapW, mapH, wrapX)` over the DLL's
  continent art types (`Plot:GetContinentArtType`, the DLL's map = the field's rows - 4) with `bake::tileStyle`'s 4-plot
  rule; a tile of more than one plot takes its first EXE tile's style [I: exact for 1-plot tiles]. Every style is 0
  (Generic) after `setHeightField`: the caller sets the styles again after each field. `tileStyleAt`.
- **Stats**: `FrameStats::fowOn`, `fowStyleTiles[5]` (EXE-PS tiles per t6 style, wrap copies counted).
- **Water and river t10**: no module change; the scene binds the mask with `water::Renderer::setTexture(Slot::FowMask)`,
  which reaches both the ocean and the river PS (test below); t11 is already the TerrainFOW Generic volume (3609).

**Tests** (2026-11-14, Release, RTX 4080 hardware device, EXE `%USERPROFILE%/tools/ghidra-projects/bin/CivilizationV_DX11.exe`;
`render/build.py --test -R "c5r_terrain|c5r_water|c5r_fow"`: 11 of 11 passed, 0 skipped, at 22:42; 14 of 14 after the
merge of 3952 / 3955, 23:10, `c5r_terrain_fow_test` 10 tests; under MSVC `/fsanitize=address` (`--asan`) 11 of 11 at
22:56 and 14 of 14 at 23:14, 0 skipped, 0 ASan reports) [run]:
- `fow_default_is_bit_equal`: the default against the fog on with a visible mask (1, 0, 1, 1), with (1, 1, 1, 1) and a
  style-3 tile, and the fog off with inputs given: **0 of 76,800 bytes differ** each (wrap copy in view).
- Per class through the EXE's PSs: unexplored (0, 1, 0, 1) = the cloud base on 6,144 of 6,144 px (+-1); seen
  (1, 0, 0, 1) = the tile style's LUT (two synthetic styles on their own tile columns, identity on the Generic column);
  visible = untouched; FOWAnim: the cloud's texel = world x (391 of 391 sampled px), z = -64 shifts it by 64 texels
  (the DS's uv `(world.xy + FOWAnim.zw) * FOWAnim.xy`, `terrain-surface.md` 1, confirmed) [run]; the mask placed by
  `overlay.proj` without an overlay texture (8,640 + 8,640 px on the right side of a two-half mask).
- `fow_lut_volume_is_the_tga_rule`: the bound volume equals `buildColorKeyLut`'s bytes; null restores identity.
- Negative: fog on with a null mask, a null cloud base, FOWAnim NaN or infinite, a mask view R32_FLOAT or B8G8R8A8, a
  cloud base that is a 3D view: `BadLayout` each, the picture after them unchanged; bad LUT TGAs (truncated, no header,
  RLE type 10, 24 bpp, fewer than 32^3 pixels, style 5): their errors and the volume unchanged; tile and style out of
  range, art types of the wrong size or null: `BadLayout`, styles unchanged; a new field resets the styles.
- `c5r_water_fow_mask` (C5R_GAME_DIR = the pure_vp copy): ocean and river each alone, magenta clear, the PS's own
  output: own t10 and a visible mask **byte-equal**; seen drawn on the same pixels and different (the Generic LUT);
  unexplored **0 px drawn** (the discard); `setTexture(FowMask, nullptr)` restores the first picture byte for byte.

**The TerrainFOW volumes against the capture** [L capture, run module]: with C5R_GAME_DIR = c5h-a's install (VT
present, 45B3B433..., the install of 3951's captures) all five files exist and give **the same volume**, FNV-1a 64
`0f547b990c53207c`, each **bit-equal to the capture's t6** (`2473_49245115.bin`, 131,072 bytes;
`render/evidence/0168/3953/luts_3953.sh`, test `game_fow_luts_from_the_install`). **Two styles are on screen** [L]:
the terrain draws bind two t6 resources with equal bytes (2.7): 2473, the one the water binds as t11 (the Generic slot
0xD90E60[0], `water-system.md` 10.4), and 2476. So the tiles of 2473 are **Generic** (cam1_a 9 of 66 terrain draws,
cam2_a 6 of 34, cam1_t21a 10 of 70) and the rest one other style, **America by the loader's creation order [I]** (the
resource id after Generic's). P11 "one volume on screen" holds only because the five files of this install are equal.
**Naming each tile's style with `tileStyle` is open**: it needs the continent art types of 3951's map
(`Plot:GetContinentArtType` through the Lua channel on the T0018 save, C7 or a short live run). The art types and
tile+0x138 bits that 3406 read live (`D:/c5-64bit-work/3400/w6/live/tiny_art.txt`, `tiny_tiles.txt`, save
`c5h-9028-tiny-t0008`) give style 1 to all 170 drawn tiles, not the capture's Generic tiles, and their land does not match
the T0018 census (1,339 of 2,016 plots), so that save is taken as another map [I] and not used for the styles.

**Replay** (`replay_3953.py`, 3019's `terrain_replay.exe` built from `render/evidence/3019/`; 2026-11-14 22:45-22:52,
RTX 4080): the EXE's scene draws of terrain pass 0 / pass 1 (m73p0 / m73p1), ocean (m91p3) and river (m91p4) of
3951's captures cam1_a, cam2_a (t20) and cam1_t21a (t21), replayed as captured (`exe`) and with the module's inputs:
t6 = the module's volume of each draw's tile style, t11 = the module's Generic volume, FOWAnim = `fowAnim(W, T)` in
float32; the style of a draw = 0 where its captured t6 is the Generic resource, else 1 (above). Variant **`mask`**
(added 23:05 after 3952 and 3955 merged, epic @15bcd94f): the same with **3952's mask** as t4 and t10, made by
`render/tools/fowmask` with `--derive` (the module's own plot range, as the server runs it) from 3951's census
(`census_t20` for cam1_a / cam2_a, `census_t21reload` for cam1_t21a, equal to t21 plot for plot) and the Tiny ground
truth's placement, with the mip chain as 2 x 2 means rounded half up (`mask_3953.py`; stock's GenerateMips is that mean
within 0.5, 7.1). In the other variants **t4 / t10 and t5 are the capture's own** (3952's mask and 3955's cloud base are stubs at this commit), so this
replay checks t6 / t11 / FOWAnim and the binding slots, not the mask. Pixel kinds: terrain by the replay's id buffer;
ocean / river = the pixels that change when those draws are left out (the id pass mirrors the terrain DS's signature
and missed most ocean pixels). Classes: the captured mask at the pixel's world position (the depth is the terrain's
under the water, which writes no depth).

| Variant | Capture | all drawn px | terrain p0 | terrain p1 | ocean | river |
|---|---|---|---|---|---|---|
| **mask** (3952's mask, module t6 / t11 / FOWAnim) | cam1_a | 99.99 % equal, max 1 (316 px differ) | 0 / 0 / 1 | equal | 0 / 0 / 1 | equal |
| | cam2_a | 99.99 %, max 1 (362 px) | 0 / 0 / 1 | equal | 0 / 0 / 1 | equal |
| | cam1_t21a | 99.99 %, max 1 (226 px) | 0 / 0 / 1 | equal | 0 / 0 / 1 | equal |
| module (W 3583, T = the capture's) | cam1_a | 3,047,714: **100 % equal** | 2,274,015 equal | 46,910 equal | 681,717 equal | 45,072 equal |
| | cam2_a | 3,081,198: **100 % equal** | 1,677,851 equal | 187,365 equal | 1,151,557 equal | 64,425 equal |
| | cam1_t21a | 3,132,392: **100 % equal** | 2,254,412 equal | 47,982 equal | 785,015 equal | 44,983 equal |
| clock (T = 42 s, the server's own) | cam1_a | 86.6 % equal; 0 / 12 / 73 | unexplored 317,127 px: 11 / 34 / 73, 0.4 % equal | 0 / 12 / 36 | 0 / 0 / 18 | 0 / 3 / 34 |
| | cam2_a | 90.6 %; 0 / 15 / 72 | unexplored 226,279: 22 / 54 / 72 | equal | 0 / 0 / 34 | equal |
| | cam1_t21a (T 41.8 s, near 42) | 93.9 %; 0 / 1 / 17 | unexplored 214,479: 1 / 4 / 17 | 0 / 2 / 12 | 0 / 0 / 8 | 0 / 0 / 4 |
| w3584 (W = samplesX * sx) | cam1_a | 89.7 %; 0 / 2 / 22 | unexplored: 2 / 5 / 22 | 0 / 2 / 14 | 0 / 0 / 6 | 0 / 1 / 7 |
| | cam2_a | 92.7 %; 0 / 1 / 18 | unexplored: 2 / 6 / 18 | equal | 0 / 0 / 11 | equal |

(median / p95 / max of the largest channel difference.) **Causes, every difference named**: `mask`: one level on 0.01 %
of the pixels, spread over all three classes and both water kinds, from the module mask's texels that differ by +-1
from stock's (7.1: 99.99 % of texels equal) and the box mips' rounding [I: not separated between the two]. `module` has none (FOWAnim
`fowAnim(3583, T)` is bit-equal to the capture's (0.00195367, 0.00195367, -2T, 0) at all three; the LUT volumes are
bit-equal). `clock`: the cloud phase only (the intended difference of 5.4): on unexplored ground through t5, on the
mask's edge ramps (x < 1), and through the translucent ocean and river over that ground; visible and seen interiors
equal. `w3584`: k = 7 / 3584 instead of 7 / 3583 (0.03 %) drifts the cloud uv by about a texel over the window: **the
caller takes W from the world box, `(samplesX - 1) * sx` = `water::worldFromGrid(g).width` = 3583** (3954's draft takes
the camera block's aabb width, 3583 on Tiny: right). Results archived 2026-11-14 23:17 in `D:/c5-64bit-work/0168/3953/` (`results/<label>.json`, `logs/`, `luts/`, `masks/`, `diff/`; the C: work folder, exports and replays removed: regenerate with `export_3953.sh`, `luts_3953.sh`, `mask_all.sh`);
difference images in the Visual Checks feed, issue 3953 (9 entries, DIAGNOSTIC, D112: replays, not the product).
The cloud base t5 stays the capture's in every variant: 3955's `cloudBase()` is the install's `fogcloudtile_dif_02.dds`,
whose mip 0 equals the capture's t5 by SHA-256 (2.4); the lower mips were not compared here.

**End to end with the epic's modules** (test `game_fow_with_the_epics_mask_and_cloud_base`, C5R_EXE + C5R_GAME_DIR):
`MaskRenderer` (512, one view, plot range from the window) and `CloudRenderer::cloudBase()` bound through
`Options::fow` with `fowAnim`; every plot visible = the default picture byte for byte in the middle of the frame;
every plot unexplored = no cache colour left (the cloud base); every plot seen differs from visible (the TerrainFOW
LUT) [run].

**For 3954 / the scene** (not wired here: 3954 was not merged into `epic-0168-fog-of-war` at 22:44): per drawn frame,
`o.fow.enabled = f.on; o.fow.mask = f.mask; o.fow.cloudBase = f.cloudBase; memcpy(o.fow.anim, f.fowAnim, 16)` from
`FowScene::terrainInputs()`; the overlay pair's matrices in `o.overlay.proj` (they place the mask); and
`setTileStyles(artTypes, mapW, mapH, wrapX)` after every successful `setHeightField` (the scene's `applyField`), from the
DLL's continent art types (`MapInputs::plotArtTypes` of the bake pipeline). With the fog on and the mask or cloud base
missing, the terrain draw fails `BadLayout` (D118: never the map without its fog).

**Not verified**: 3955's cloud base in the replay (only its mip 0 is known equal); each tile's style by `tileStyle` on
3951's map (the art types of that map are not read, above); a seen plot under a style whose TerrainFOW file differs (all
five are equal in this install; another install or mod could differ); multi-plot tiles' style; the live product (C7).
*2026-11-15 (lead-0168): the tile styles of 3951's map are settled by 8.2 (Lua art types through `tileStyle`; the
capture's second volume is America); the live product by section 8.*
*Correction 2026-11-15 (task 4305, epic 0169, ticket 0341): the scene now calls `setTileStyles` after every field and
whenever the art types change, from the feed's `PlotRecord::artType` (pure `GetContinentType`) through the residency,
which hands the same array to its bake (`MapInputs::plotArtTypes`); so t6 is the tile style's TerrainFOW volume in the
product, and t7 (TerrainShadow) is now per style too. In play the styles stay the load's, as the EXE keeps them when VP's
farm changes an art type [L] (`terrain-surface.md` 12.6). This replaces "every tile keeps style 0" and "`setTileStyles`
is **not called**" of 7.3 item 3 (that text stands for 3954's build); `render_tiles=tap` still leaves every tile at
style 0.*
*Pointer 2026-11-15 (epic 0169, task 4315): "a seen plot under a style whose TerrainFOW file differs" above was checked
on ten installs by 4303 (`terrain-surface.md` 12.2 [L]): all equal, no mod overrides a ColorKey file; another install or
mod could still differ. The FNV-1a 64 `0f547b99...` above and 4303's `6e4482dc...` for the same volume differ (4303: another
seed or byte order [I]); the SHA-256 49245115... is the cross-check (`terrain-surface.md` 12.1).*

### 7.5 The install wait counts the fog's own time, not the server's stalls (ticket 0429, 2026-11-16)

Written 2026-11-16 by impl-B-0429 (account B), branch `task-0429-fow-install-wait`. Marks as above.

**What failed** [L, 0415's run a5, 2026-11-16 12:10-12:16 EST; logs `F:/c5-64bit-work/0415/a5/out`]: the D33 product
path (3308's recipe, instance c5h-d, the huge save t350 as `C5H-0415-HUGE_0350`, server.exe 573a33b2..., composite
678ee432... of 0415's branch, FOWLevel 3, particles on), the loaded workstation (10+ registered heavy jobs). From
`render.log`: the field (128x84) at 12:14:44.841; the fog's first wanted frame (`fog: frames held from frame 47: no plot
table from the feed`) at 12:14:51.834; the plot table at 12:14:52.831; then **no per-second line of the render thread
from 12:14:52.831 to 12:15:05.514 (12.7 s)**: the server's post-field stall of 0407 (`fow-models-compile.md` 1: the
server's first GPU work after the field waits 8.7-23.4 s in the NVIDIA driver when the board is near full). The overlay's
tap was ACTIVE from 12:14:54.343, but its pair is taken on the render thread, which was not there. At the first check after
the stall, 12:15:05.490, the install failed `no overlay pair yet (overlay ACTIVE) 13657 ms after the first frame of the
map` (wall time), 34 ms before `overlay: first pair bound` (12:15:05.524); server exit 5, the EXE exit 3, the game lost
at the load. The wait (`FowSceneOptions::installWaitMs`, 10 s) was wall time and counted the stall.

**The change** [C, `server/src/render/srv_fow_scene.cpp` `FowScene::tickClock` / `waitedText`, `srv_fow_scene.h`]:
`FowScene::admit` (the fog's check, called by `Scene::decide` at every host-loop iteration that has a frame to draw, about
every 100 ms while held: the host's event wait times out 10 times a second) keeps its own clock. Each admit-to-admit gap
counts up to `FowSceneOptions::stallGapMs` (1000 ms); the rest of a longer gap is a **render-thread gap** (the server
stalled, or no frame was wanted: a hidden camera) and is not counted. Both branches use it: the install's failure when the
fog's own time since the first wanted frame passes `installWaitMs`, and the in-play hold's LOUD line (never fatal). The
cap per gap keeps the bound: a thread that comes back only every 3 s still counts 1 s per check and fails after 11
checks, never a hang. A gap above the cap while installing or held is noted once per gap (`fog: the render thread came
back after N ms (frame wanted for the install): M ms of it not counted ...`); the install's note and the failure's text
name the wall time, the time not counted, the number of gaps and the longest, for example:
`no overlay pair yet (overlay ACTIVE) 10100 ms of the fog's own wait after the first frame of the map (24200 ms wall time;
14100 ms of it not counted: 1 render-thread gap(s) above 1000 ms between two checks, the longest 15100 ms (the server
stalled, or no frame was wanted))`. A fog install that never gets its pair therefore still fails by D118 (the popup and
report of 0167's route, exit 5), within the limit plus the stall time.

**Tests** [C, 2026-11-16, workstation]: `server/tests/render/fow/test_fow_scene.cpp` case `stall_not_counted` drives the
fog's step through `Scene::fowAdmit` with a made-up clock (the product's 10-s wait, a check every 100 ms) and injects a
stall as a jump of the clock: (a) 15 s without a check before the first pair: no failure, the hook not called, the pair
one check later installs, the note names the 15,100-ms gap; (b) no pair at all with a 15-s stall inside the wait: fails
once the fog's own time passes 10 s (at wall 24.2 s, not before; the limit plus the stall is 25 s), the text naming the
stall, the hook called once with code 5; (c) a check every 3 s: fails after 12 checks; (d) in play a 15-s stall in a
held episode does not count toward the hold's LOUD line, which comes after 10.1 s of the fog's own time. The suite
(`test_fow_scene.py`, 19 cases) passes plain (clang-cl /O2, 147 s) and under ASan (MSVC /fsanitize=address, 258 s).

**Live: five huge loads** [L, 2026-11-16 12:47-13:20 EST; `server/evidence/0429/acceptance.txt`, runs
`F:/c5-64bit-work/0429/l1..l5`]: the same D33 product path as 0415's a5 (3308's recipe, `server/evidence/0429/run_0429.py`),
instance c5h-f, the huge save t350 as `C5H-0429-HUGE_0350`, FOWLevel 3, particles on, one AI turn each; server.exe
66bb49fe... (release, this branch), the pin's x64 DLL 74588135..., composite 6bb7fbda...; the workstation loaded (6-11
registered heavy jobs at each start, `quietlock.py check`). All five installed the fog and played the turn (rc 0), no
fog-install failure. **l1 had the stall**: one render-thread gap of 15,952 ms between two fog checks; the install came
2,803 ms into the fog's own wait, 17,755 ms of wall time after the first wanted frame, where the wall-time wait would
have failed it as in a5. l2 had a 5,708-ms gap (install at 2,774 ms own, 7,482 ms wall); l3-l5 had none (3.9-6.2 s).
The stall itself is 0407's / 0428's to remove; this change only stops it from ending the game.

### 7.6 The install wait counts the EXE's map frames: huge late loads (ticket 0491, 2026-11-17)

Written 2026-11-17 by impl-0491 (account B), branch `task-0491-fog-install-wait`. Marks as above.

**What failed** [L, 0459's runs, 2026-11-17; server run folders `F:/c5-64bit-work/0459/20261117-052759-56572-1` and
`20261117-054528-20216-1`; `research/03-dll-engine-boundary/ui-queue-guard-product.md` 3.2]: 9074's Marathon save
`TS1.20-T1100` (180 x 112, 42 majors + 20 city states) on the D33 product path (development layout, pack=0, x64 DLL 0334's
m43 build cf981126...), instance c5h-h. Both loads: the map's first wanted frame at server time 305.9 s (run 1; the camera
block at Present 38, frame 39), `fog: frames held from frame 39: no plot table from the feed`, the plot table 7.9 s later
(313.7 s), then at 315.85 s `no overlay pair yet (overlay WAITING) 10003 ms of the fog's own wait ... no render-thread gap
above 1000 ms`: exit 5, the proxy's FATAL exit 3. The overlay reader never left WAITING after 26.8 s (`tap block state
NONE`, newest slot none): the composite never published a pair. 0492's guarded run (`ui-queue-guard-product.md` 7.3) ended
the same way at 300.0 s, so the UI queue overrun is not the cause.

**Why** [L: T650 by 0459's run, T1100 by run a1 below]: the composite's overlay tap copies the target at an EXE Present
of a map frame (camera OK and the scene-clear list executed) and publishes the copy at a later Present, once its query has
signalled (`shell/composite/overlay_tap.cpp` Work / PollInflight). So a pair needs a few EXE Presents after the map's first
frame. On these loads the EXE's main thread is still busy after the map's first frame and presents only now and then: in
0459's T650 load (same layout, `20261117-060345-65504-1`) the first wanted frame was Present 38 (208.5 s), the plot table came
at 215.0 s, the pair was the **copy of Present 41** bound at 217.9 s, and the fog installed at frame 43 after 9,383 ms with
**4 frames held** before: five EXE Presents in 9.4 s. T1100 is a larger and later state (more units, cities and route art);
the server's per-second `fog 1s` line shows `held` 0 or 1 each second over its 10 s, so the EXE presented too few frames for
the tap to copy and publish one before the 10-s wait ended. The server's own render thread was not stalled (no gap above
1 s). The wait measured time while the thing waited for was the EXE's progress.

**The change** [C, `server/src/render/srv_fow_scene.cpp` `FowScene::admit`, constants `kInstallPresents` = 30 and
`kInstallSlowFactor` = 12]: the install wait (`installWaitMs`, 10 s of the fog's own time, 0429's clock unchanged) ends the
wait only once the EXE has presented at least **30 map frames** since the first wanted frame (the frames the fog held:
each new camera-block `frameStamp` is one EXE Present of a map frame). An EXE that presented fewer is a load still going on
and is waited for up to the **slow-load bound, installWaitMs x 12 = 120 s** of the fog's own time. 30 is the composite's own
limit for a tap that cannot find its target (`overlay_fail_frames`, 30 map frames): a tap that had 30 map frames and still
published no pair is a fault, and an EXE that presents normally (30 frames in well under a second) still fails at 10 s as
before. A fault while the EXE presents nothing (it never comes back) fails at the bound. When the 10 s pass with fewer than
30 frames the fog notes it once (`fog: the install's wait passed 10000 ms with only N map frame(s) presented by the EXE (its
load goes on ...)`); the failure's text names the frames and the rule (`...; N map frame(s) presented by the EXE in the
wait; the wait ends after 10000 ms once the EXE has presented 30, else after the slow-load bound of 120000 ms (ticket
0491))`). All wait reasons (no table, no revealed plot, no pair) follow the rule: the table also comes late on these loads
(7.9 s on T1100). The in-play hold (never fatal) is unchanged.

**The composite's mask wait** [C, `shell/composite/fow_models.cpp` `WaitTick`]: the model fog (0339) failed its install
(TIMEOUT, which fails the server's fog) after `fow_models_wait_ms` (30 s) of wall time from the first map frame without a
mask; the server publishes its first mask only after its own install, which may now take up to 120 s. So the same rule:
the 30 s end the wait only after **60 map frames**; with fewer the wait lasts up to `fow_models_wait_ms` x 6 = **180 s**
(above the server's bound, so a server that gives up first names its own reason), one note when the 30 s pass
(`fow_models: no mask yet after N ms with only M map frames presented ...`), the TIMEOUT text naming the frames and the
bound. No new ini key (0490: the shipped ini equals the defaults).

**Tests** [C, 2026-11-17, workstation, `server/evidence/0491/s03_suites.sh`, results in `server/evidence/0491/suites.txt`]:
- `server/tests/render/fow/test_fow_scene.cpp` case **`slow_load_waits`** (new): the fog's step through `Scene::fowAdmit`
  with a made-up clock (a check every 100 ms) and the EXE's frames written every N checks: (a) a frame every 2.5 s and the
  pair at 40 s: one note at 10.1 s (5 frames), the install, no LOUD; (b) a frame every 5 s, no pair: fails at the bound
  (own 120,100 ms, 25 frames); (c) a frame every check: fails at 10.1 s as before (102 frames); (d) no frame after the
  first: fails at the bound with 1 frame; (e) a frame every 2.5 s, no pair: fails at 72.5 s once 30 frames were presented.
  Changed to the rule: `no_overlay_pair` (3 frames past the 300-ms wait = a note, then drawn every 20 ms until it fails
  with 30 frames), `install_failure_fatal_once` (no revealed plot, no table: the same), `stall_not_counted` (c) (a check
  every 3 s now fails after 30 checks, not 12). Plain (clang-cl) 3 passed (173 s); MSVC ASan 3 passed (459 s), 0 reports.
- `shell/tests/test_fow_models.py` **`test_slow_load_waits_to_the_bound`** (new; composite test mode `slowload`: a map frame
  every 400 ms): past `fow_models_wait_ms`=1500 one note, no failure; TIMEOUT at the bound (9,000 ms) with the frames named;
  `test_no_mask_in_time_is_a_timeout` now also checks that a full-rate host had its 60 frames. Plain 27 passed with
  `test_config_defaults.py` (no ini key added); ASan 19 of 20 (0415's `test_new_map_load_puts_back_until_a_mask_built_after_it`
  missed its 14-s window once; alone twice under ASan: passed both), 0 reports.
- `server/tests/render/test_decal_wiring.py` **`test_install_wait_counts_from_the_exes_first_map_scene`** (new; the rig with
  render_overlay=1 and the overlay block NONE: no map scene): past the 1,500-ms wait one note and no failure; the slow-load
  bound (9,000 ms from the field) fails it once with the text naming the missing scene. Plain 18 passed, ASan 18 passed.

**Live run a1: T1100 with the change** [L, 2026-11-17 09:33-09:41 EST, workstation instance c5h-h (claim impl-0491,
`provision.py align548` and `verify` rc 0 first), driver `server/evidence/0491/s02_live.sh c5h-h 1100 2 a1` (0492's driver
without the queue watch; perf/profilekit/acceptance.py run, 2 AI turns asked), build of this branch at 73978161e:
server.exe 634109CB..., proxy 9004BFEB..., composite 156DC0B8... (`s01_build.sh`, against a private copy of 0334's m43 source
and deps), x64 DLL m43 CF981126...; the same development layout as 0459 (pp_4255.py prep, pack=0, D33 with Overlay on,
ServerRenderer = 1); server run `20261117-093503-33476-1`, kept in `F:/c5-64bit-work/0491/t1100-a1-runfolder`; excerpts in
`server/evidence/0491/t1100-a1/`]:
- the map's first wanted frame at server time 301.9 s (Present 38, frame 39), the plot table at 311.8 s;
- 311.9 s: `fog: the install's wait passed 10000 ms with only 3 map frame(s) presented by the EXE (its load goes on ...)`:
  where 0459's build failed, this one waited;
- 315.19 s: the overlay tap ACTIVE, `first pair bound: slot 0 generation 1, copy of Present 41`; 315.21 s `fog: installed at
  frame 43 after 13309 ms of the fog's own wait (13309 ms wall time, no render-thread gap above 1000 ms) ... 4 frames held
  before`. The per-second lines show one held frame at 09:40:05, :09, :15 and :19 (one EXE Present every 4-6 s), then
  `fow=ACTIVE with=8..24` per second: once the pair came the EXE presented normally;
- the composite's model fog: `fow_models: INSTALLED` at 09:40:18.638 (its first map frame: camera OK *and* the scene-clear
  list executed) and SWAPPED 0.6 s later. So Presents 38-40 had the camera OK but no map scene yet (the EXE still on its load
  screen): the tap could take its first copy only at Present 41 [L]. The server cannot see the scene flag; it counts the
  camera-OK Presents, which on these loads come as slowly.
- **Then the next fixed install bound ended the game**: 321.86 s `decals: the 64-bit path cannot go on: decals cannot
  install: no decal set within 20000 ms after the load (... control block present, state NONE, generation 0 ...)`, exit 5,
  20.0 s after the map's first frame (`srv_render_scene.cpp` `Scene::decalPoll`, `SceneOptions::decalWaitMs` 20 s from the
  first field with a valid camera grid). 0459's runs never reached it (the fog failed at 10 s). Same root cause (the EXE's
  decal tap publishes its set from the EXE's load, which is still going on); outside 0491's file list: asked on the ticket
  (2026-11-17 09:44); the MC added `Scene::decalPoll` to 0491 at 09:45 (ticket version 4).

**Run d1: T1100 again** [L, 2026-11-17 09:43-09:51 EST, same build and layout; server run `20261117-094439-33788-1`, kept in
`F:/c5-64bit-work/0491/t1100-d1-runfolder`; meant as a diagnostic with a longer decal bound, but the driver's
`server_cfg_extra` step failed on a POSIX path given to Windows Python, so it ran as the product does]: the same course as
a1: first wanted frame 315.8 s (Present 38), table 324.9 s, the slow-load note at 325.8 s (3 frames), the pair (copy of
Present 41) at 328.2 s, **fog installed at frame 43 after 12,444 ms**; then the decal bound again at 335.7 s, 20.0 s after
the map's first frame. The composite's decal tap (`shell_composite.log`) shows why the decal set was late: its first build
began at 09:49:58.9 (server time about 319.7 s, 4 s after the map's first frame), and was **restarted three times because a
batch of the EXE's decals moved during the build** (09:49:58.9, 09:50:11.3, 09:50:13.4: the EXE still building its decal
batches), then published a snapshot at 09:50:13.98 whose walk check missed (`no walk block for this generation ... a batch
moved after the build began`), i.e. 0406's walk hold repeated the build; the server's bound ended the game 1.7 s later with
the control block still NONE. Unlike the fog's pair, **the EXE presented normally during the decal delay** (lockstep
15-19 frames a second from 09:50:10 on; the fog drew from 328.2 s): the EXE's Present rate is not the decal tap's progress
signal [L].

**Run d2: when the decal set comes** [L, 2026-11-17 09:51-10:1x EST, same build and layout, DIAGNOSTIC: the TEST ONLY
server.cfg key `render_test_decal_wait_ms=300000` through vpproxy.ini `server_cfg_extra=` (`EXTRA_CFG` of `s02_live.sh`);
server run `20261117-095441-70244-1`, kept in `F:/c5-64bit-work/0491/t1100-d2-runfolder`]:
first wanted frame 324.0 s, the fog's note at 334.0 s (2 frames), pair (copy of Present 41) and **fog installed at 337.7 s
after 13,719 ms**; **decal generation 1 (28,781 decals) READY and applied at 346.7 s: 22.7 s after the map's first frame,
9.0 s after the EXE's first map scene**; then generations 2-5 as the EXE added its decals (34,581 at 352.9 s, ...).
The game was in play at 10:00:21 (load 397.6 s; the map with fog, decals and units; acceptance.py's turn-1100 picture), the
UI queue guard of 0492 dropped 3,142 KB at the load's end as on stock. **Then the first AI turn hung in the EXE**: autoplay
armed for 2 turns at 10:00:56, the EXE's last Present at 10:02:40 (the composite's log ends, the server drew nothing new),
the EXE's main thread spinning (about 1 core) while server.exe (the DLL) was idle (0.01 core), the Lua channel not polled;
acceptance.py ended at 10:30:58 (`no new turn for 1800 s after turn 1100`), no box. A full dump
(`F:/c5-64bit-work/0491/dumps/c5h-h-x86-70244-20261117-103444.dmp`, cdb `~0 kb`, `!runaway`: thread 0 38 min of user
time) puts the main thread at EXE RVA 0x1B1FC5 (**VA 0x5B1FC5**) with return RVAs 0x1AA0D8 and 0x1B8A27 [C]: the same site
as **9074's stock T1100 run 1 first-turn hang** (the record-list loop VA 0x5B1F80-0x5B202A, same return RVAs;
`research/03-dll-engine-boundary/upstream-13406-product.md` section 6). So the hang is the known T1100 EXE failure, seen on
stock too, not a fault of 0491's waits or of the product path [C for the site; I for the cause]. The run's map, fog and
decals were drawn before it. The game was stopped on the MC's word (#4154: `cli.py stop --coordinator-override`, the
newest autosave unchanged since 10:00:45), the instance restored by hash (verify rc 0).

**Run f1: T650, the final build, 2 AI turns** [L, 2026-11-17 10:45-10:59 EST, instance c5h-f, `s02_live.sh c5h-f 650 2
f1`, build at 8173c0898 (the fog and decal rules): server.exe 3bc65d7b..., proxy d475ab55... (main bccdf8663 merged, with
0492's queue guard), no test switch; server run `20261117-104645-20832-1`, kept in `F:/c5-64bit-work/0491/t650-f1-runfolder`;
excerpt and per-turn CSV in `server/evidence/0491/t650-f1/`]: first wanted frame 306.2 s, table 315.0 s, the fog's note
at 316.2 s (3 frames), pair (copy of Present 41) and **fog installed at 318.7 s after 12,512 ms**: **this T650 load would
have failed under the old 10-s wait** (0459's T650 got its pair at 9.4 s: the margin was never there). Decal generation 1
(14,572 decals) at 319.6 s, 13.4 s after the field and 0.9 s after the first map scene. In game after 383.4 s; **turns
650 -> 651 (180.5 s) -> 652 (143.0 s)** played, autoplay handed back at 652, acceptance rc 0; 0 LOUD lines in server.log;
`fog 1s` ACTIVE in all 417 lines after the install; no box; restored (verify rc 0). Pictures (acceptance.py, Visual Checks
`vc-20261117-105214-workstation-5518`, `-105619-...-c2ed`, `-105841-...-2a51`) are DIAGNOSTIC (development layout, D112).

**Run b1: T1100, the final build, no test switch** [L, 2026-11-17 11:00-11:10 EST, c5h-f, `TT=900 s02_live.sh c5h-f 1100 2
b1`; server run `20261117-110203-25132-1`, kept in `F:/c5-64bit-work/0491/t1100-b1-runfolder` with the proxy's
`exe-crash.txt`; excerpt in `server/evidence/0491/t1100-b1/`]: first wanted frame 339.8 s, the fog's note at 349.8 s (2
frames), pair (copy of Present 41) and **fog installed at 354.0 s after 14,216 ms**; **decal generation 1 (27,275 decals) at
360.7 s: 20.9 s after the field** (the old 20-s decal bound would have failed it), 6.7 s after the first map scene; in game
after 412.9 s. Then, in the first AI turn (autoplay armed 11:08:34), **the EXE ended with 0xC0000005, a write to 0xA25D7000 at
VA 0x55D91A** (RVA 0x15D91A, the proxy's boot thread = the EXE's main thread), no box (the dialog watcher's 0419 crash record,
crash.collect into the instance's run folder; 0410: an EXE crash on the product path shows none); the server ended with exit
3 (PeerDied). The site lies in the function of 9074's stock T1150 / T1300 load crashes (writes at 0x55D9E2 and 0x55D947, the
B event manager's dispatcher) and #13397's first-autoplay-turn crash at 0x55D9E2 (`upstream-13406-product.md` 6): the known
late-game EXE failure class of these saves [C site; I cause], reported on the ticket (D155). The instance was restored (verify
rc 0).

**Run b2: T1100, the final build, 2 AI turns played** [L, 2026-11-17 11:14-11:33 EST, c5h-f, same driver and build; kept in
`F:/c5-64bit-work/0491/t1100-b2-runfolder`; excerpt and per-turn CSV in `server/evidence/0491/t1100-b2/`]: first wanted
frame 302.0 s, the fog's note at 312.0 s (3 frames), pair (copy of Present 41) and **fog installed at 315.4 s after
13,390 ms**; decal generation 1 (27,866 decals) at 322.8 s, **20.9 s after the field** (again past the old bound), 7.5 s
after the first map scene; in game after 366.4 s; **turns 1100 -> 1101 (453.1 s) -> 1102 (197.4 s)**, autoplay handed back
at 1102, acceptance rc 0, no box; `fog 1s` ACTIVE in all 725 lines after the install. One LOUD line, not fatal and not
0491's: at 498.9 s `decals: in play: the tap says FAILED LAYOUT (generation 6 in force, the tap retries)`, the composite's
`decal_tap: FAILED LAYOUT: batch 25 slot 921: rectangle (11551.7, 313.892, 11519, 327.875) ... not valid` (a decal rectangle
whose max x is below its min x at the map's wrap seam; the last good set stayed). Restored (verify rc 0). Pictures
`vc-20261117-112056-workstation-f3f9`, `-112913-...-ff24`, `-113238-...-28f8`: DIAGNOSTIC (development layout).

**T1100 on the product path, summary of 0491's five loads** (all 2026-11-17, workstation, m43 x64 DLL): the fog installed in
all five (12.4-14.2 s after the map's first frame, 2-3 EXE frames in the first 10 s, the pair always the copy of Present
41); the first decal set came 20.9-22.7 s after the field in the three runs that reached it, 6.7-9.0 s after the first map
scene; in the first AI turn one run played both turns (b2), one hung at VA 0x5B1FC5 (d2) and one crashed at VA 0x55D91A
(b1), both at the known stock T1100 failure sites (upstream-13406-product.md 6); a1 and d1 ended at the old decal bound
(build before the decal rule).

**"No popup" in 0459's runs is the development layout, not a defect of the route** [L]: every run above and 0459's use the
development layout (pp_4255.py: pack=0), in which the proxy writes no report and starts no helper by design (0167 12.5 (a);
server.log says so: `no report and no popup (auto: ... vpproxy.ini says pack=(none: 0); ticket 0418)`). With the shipped
pack (pack=1) a fog install failure ends the game loud. **Forced fault, run fault2** [L, 2026-11-17 11:39-11:46 EST, c5h-f,
`server/evidence/0491/s04_fault.py` prep / run / restore: the development layout of the final build plus 0422's pack=1 test
layout on top (tools/release/pack.py --layout of this build, the instance's 32-bit DLL as the x86 entry labelled test only,
manifest.json and purity.tsv into VP64\\, the x64 DLL also under the pack's name `CvGameCore_Expansion2_x64.dll`, the REAL
vp64helper.exe), the fault by `server_cfg_extra` = `render_overlay=0` (TEST ONLY: the fog's install fails at the first wanted
frame by the same FowFatalStep route as the wait's failures), save 9074's T650 as `C5H-0491-T0650F`; facts in
`server/evidence/0491/fault2/facts.json`, the work folder in `F:/c5-64bit-work/0491/fault2`]: server.log 280.575 s `LOUD: fog
of war: render_fow=1 needs the overlay pair ... no frame is drawn (design 5.5, D118, D125)`, `exit code 5`; the report folder
`VP64\reports\20261117-114511` (stage server-death, `server pid ...: exit code 5`, the server.log's last FAILED line naming
the fog); **the popup** (VP64HelperPopup: "The 64-bit part of the patch failed, so the game was ended ..."), seen by the
dialog watcher (kind patch), closed by the claim's owner after the game had ended (`cli.py dismiss --button Close
--patch-popup`, COMMON 29), marker cleared; 7 of 7 facts; the instance restored (provision verify rc 0). The popup itself was
not pictured (the instance's windows are cloaked on the workstation). The first attempt (fault1, 11:30-11:37) ended at the
boot's pack check instead (`stage: pack, code: file-missing: CvGameCore_Expansion2_x64.dll`), a layout error of the driver,
fixed for fault2; it too ended with the report and the popup.

**The decal change** [C, `server/src/render/srv_render_scene.cpp` `Scene::decalPoll`, ticket version 4 (MC 09:45)]: with an
overlay source (render_overlay=1, the product) `decalWaitMs` (20 s) counts from **the EXE's first map scene of the load**:
the first overlay pair of another generation than the one held when the wait began (the tap copies only at a map frame
with the scene drawn, so the pair marks the end of the EXE's load as the player sees it). Until that scene comes the wait
lasts up to the **slow-load bound, decalWaitMs x 6 = 120 s from the field**, with one note when the 20 s pass
(`decals: no decal set N ms after the field, and the EXE has drawn no map scene of the load yet (its load goes on) ...`).
Without an overlay source the wait counts from the field as before. The failure text names the time since the field and
which start applied. The rule is not the fog's Present count: d1 shows the EXE presenting normally while the decal tap's
builds restarted. Measured margins at the product's 20 s: T1100 9.0 s after the scene (d2), T650 [see below]. The per-Scene
state lives in a static map inside `decalPoll` (0491 holds only that function; Scene's members are in
`srv_render_scene.h`): to move into Scene when the header is next changed.

### 7.7 The fog and overlay scene suites red from a unit extra's default (mc-red, 2026-11-22)

**Symptom** [L, 2026-11-22 09:50-09:56 EST, workstation, main 7fc1768ef]: `run_all.py --only fow-scene --only overlay-scene`
failed both suites (each 1 passed, 2 failed; fow 84 s, overlay 14 s) from commit ea8878286 (D235: `[Extras] FixAirBases`
default 0 -> 1). Every failing case was "a LOUD line" (or a LOUD count one too high): `LOUD: extras: an [Extras] option is
in effect but this program does not link server/src/extras/: no extra is drawn`, once per Scene.

**Cause** [C]: `Scene::configureExtras` (`server/src/render/srv_render_scene.cpp`) decided whether to make the scene's
extras module by looping over **every** `[Extras]` row with `Vp64Opt::Extra(...).on`. The module (`server/src/extras/srv_extras.cpp`)
draws only `VisibleAreaOutline`; the unit extras (ids 23-27, `server/src/extras/units/`) and `FixRoadLayout` (`SceneRoads`)
belong to other modules. While every extra defaulted to 0 the loop never fired; with FixAirBases (a unit extra) on by default
it fired in every Scene: server.exe made the module and dropped it at once (`anyOn()` false, no line), and every program that
links the render units without `server/src/extras/` (the scene tests, by design, extras.md X10.2) wrote the LOUD line. Not a
missing link in the test builders: linking the extras there would only have hidden the wrong gate.

**Fix** [C]: `srvextras::SceneExtrasWanted(options)` (declared in `srv_extras.h`, defined in `srv_render_scene.cpp` beside the
factory registry so every program has it) counts only the ids `Extras::configure` reads (today `kVisibleAreaOutline`); a new
scene extra adds its id there. `configureExtras` asks it instead of the loop. The LOUD line stays for a scene extra in effect
in a program without the module. server.exe's behaviour is unchanged (the module was dropped silently before).

**Result** [L, 2026-11-22, workstation, plain clang-cl, game files read-only from a 4390913 copy outside the pool, branch
`mc-red-fowscene`]: before (main 7fc1768ef, 09:50-09:56) fow-scene 1 passed 2 failed (84 s), overlay-scene 1 passed 2 failed
(14 s; 14 of the LOUD lines in its log); after (09:56-10:02) fow-scene 3 passed (120 s), overlay-scene 3 passed (14 s), no
`LOUD: extras` line. Command: runbook 2.3 with `--only fow-scene --only overlay-scene`, about 6 min with the three builds.
The other users of the render units, same branch (10:02-10:33): render-host 169 passed, shadow-ice-scene 6, water-scene 29,
forest-scene 3, scenebundle-programs 4, scenebundle-py 22; server.exe, units (79/79), unit extras (13/13), run_extras_test,
unitfeed, farmfields, trigger scene and sceneshot `--smoke` built (runbook 2.10, the mc-red pass). Not run: ASan,
offline-equals-live, a live game.

## 8. Live acceptance (task 3957, C7)

Task 3957 (impl-3957 under lead-0168), branch `task-3957-fow-live-acceptance`, started 2026-11-14 23:33 EST. Phase 1
(this part) runs before the scene (3954) is merged: the stock references, FluidFOW's memory by FOWLevel, the Tiny map's
tile styles, and the research corrections of the brief's done H. Phase 2 (the joined arms, done B(1), C, D, E, F) after
3954 lands. Driver `shell/evidence/0168/3957/st3957.py`; work `C:/c5-64bit-work/0168/3957/`, archive
`F:/c5-64bit-work/0168/3957/` (D141: F: is the long-term store since 2026-11-14; was D: under D133; game pictures and
data never committed, D83).

**Setup of every phase-1 measurement.** Stock arm: 32-bit pin `i0ship-26234371-ia` (3C2F47DA...) in the CP slot
(COMMON 18), CP/VP/EUI, no proxy, no server, no RenderDoc, frame cap as the instance has it; arms by `config.ini`
[DisableSystems] and `GraphicsSettingsDX11.ini`: **S3** = the instance's own config (OverlayDebug, ColorKeyEdit = 1 only)
at FOWLevel 3 (as the owner's), **F3** = S3 plus FluidFOW = 1, **S2** = S3 at FOWLevel 2. SHA-256 backup of the standard files
before any change, restored by hash at the end. Saves: copies with their own names: `C5H-0168-TINY_0021R` (3951's t21
state, A7B077A9...) and `C5H-0168-HUGE_0350` (of `C5H-VP8P-HUGE_0350`, C499D3C6...). Cameras: Tiny cam1 / cam2 of
section 2 plus **cam3_p13** = `UI.LookAt(Map.GetPlot(11, 23), 0)` after cam2 (zoom 11.0): plot (11, 23) is seen, not
visible, and holds a resource (resource 8) [L, census t21]; huge: 3702's cameras in its order (`v1_city_default` (100, 30),
`v2_city_zoomin` 4 ticks, `v3_second_zoomin` (99, 31), `b_land` (96, 27), `b_water` (103, 31), `x_arrow` (84, 31)) plus
`p13_model` (the nearest seen-not-visible plot with an improvement, resource or city) and `fog_edge` (the nearest
unexplored plot with a revealed neighbour), both from the census at run time; each camera two harness shots 5 s apart
(P9), popups checked before each camera (D114). The stock pictures are DIAGNOSTIC stock references (D112).

### 8.1 Phase 1: the Tiny stock references [L, 2026-11-14 23:36-23:41 EST]

c5h-b (Goldberg, C:, 2554 x 1411, VT present: the harness's Visual Checks entries name the folder hash), arm S3, save
`C5H-0168-TINY_0021R`: in game 123 s after the launch, focus guard ok (4 vetoes ok, self-test ok), active player 0 /
team 0 at turn 21; census 2,016 plots, **57 revealed, 46 visible** (11 seen-not-visible): equal in counts to 3951's t21
census. `darkred handles`: FOWMask `0xDA12AC` = 0x269 and FOWBaseLayer `0xDA12B0` = 0x26A (non-zero: FluidFOW created
both), config mask 0xFFEFFFF7. No popup was open at any camera (the top-band check, D114). EXE private after the load
1,683.8-1,689.6 MB (three samples 10 s apart), largest free block 1,547.3 MB.

| Camera | Shots a / b (Visual Checks) | Stock shot-to-shot, all (median / p95 / max, within 16) | cloud-like px: median / p95 | the rest: median / p95 |
|---|---|---|---|---|
| cam1 | `vc-20261114-233932-workstation-b69a` / `-233937-...-a7c8` | 1 / 23 / 239, 90.4 % | 6 / 20 (590,472 px) | 0 / 24 |
| cam2 | `vc-20261114-234000-workstation-bd54` / `-234005-...-c8f0` | 0 / 21 / 226, 92.2 % | 6 / 19 (538,264 px) | 0 / 22 |
| cam3_p13 | `vc-20261114-234020-workstation-42fd` / `-234025-...-a913` | 0 / 19 / 241, 93.9 % | 5 / 20 (384,648 px) | 0 / 19 |

(`shell/evidence/0168/3957/p9_all.sh` with 3951's `p9_shots.py`: interface rectangles and a 16-px frame masked; the
pairs are 5 s apart, 3951's were 5 s too: cam1 2 / 26 / 217, 88.2 %; cam2 1 / 24 / 174, 89.9 %, so this launch is
within a few levels of that one.) **P13 seen live**: at cam3_p13 the sheep of (11, 23) (RESOURCE_SHEEP, a seen-not-visible
plot) are drawn **darkened like the ground under them** (stock's models read the mask and the TerrainFOW LUT, 2.8), while
the sheep on visible plots beside them are fully lit; in the joined product (white mask, decision 3 (i)) those sheep
are drawn fully lit: the intended difference of 5.4, to be shown side by side in phase 2.

### 8.2 Phase 1 (d): the Tiny map's tile styles [L art types, L capture, 2026-11-14 23:39-23:43]

The continent art types of every plot (`Plot:GetContinentArtType`, Lua channel, read only, `art_tiny.json`): 1,517 none,
311 America (1), 23 Asia (2), 135 Africa (3), 30 Europe (4). `bake::tileStyle`'s 4-plot rule over them (x wrapping;
`shell/evidence/0168/3957/styles_3957.py`, the rule as in `replay_3953.py`) gives the map's tiles (56 x 38 tile ids):
Generic 1,425, America 388, Asia 37, Africa 210, Europe 68. Against the styles the stock captures bound (3951's t21
captures re-exported, `styles_export.sh`: each terrain draw's TileOffset gives its tile, its t6 resource gives Generic
(= the water's t11) or not):

| Capture | Terrain draws | Rule = capture's Generic / other split | Rule's styles | Capture's t6 |
|---|---|---|---|---|
| cam1_t21a | 70 | **69** | Generic 11, America 59 | 2473 (Generic) 10, 2476 60 |
| cam2_t21 | 34 | **33** | Generic 7, America 27 | 2473 6, 2476 28 |

- **The tiles on screen are Generic and America**: every non-Generic draw is America by the rule, so the second volume
  2476 is **America's TerrainFOW** (3953's "America by the loader's creation order [I]" confirmed [L]).
- The one tile that differs at both cameras is **(11, 28)**: its four listed plots (10, 27), (11, 26), (11, 27), (12, 26)
  are all art type 0, so the rule says Generic, while the EXE binds America; plot (10, 26) next to them is America. That
  is the "fifth plot" some tiles list (3406: about one tile in six on the huge save; `heightpage.h` comment) [I which
  plot]. Effect on the picture: none in this install (all five TerrainFOW files are equal, 7.4); with unequal files the
  server would tint a few such tiles with the wrong style's LUT. *2026-11-16 (0383): (10, 26) is that tile's fifth plot
  by the traced rule (1 <= 11 <= (28 + 1) / 2, even row: (tx-1, ty-2); `terrain-bake-passes.md` 5.3), and `tileStyle`
  now lists it: the server gives (11, 28) America as the EXE does.*

### 8.3 Phase 1 (c): FluidFOW's memory by FOWLevel on the huge save [L, 2026-11-14 23:41 - 2026-11-15 00:12 EST]

The MC's ask (6) of #1131 and the correction owed by 3.11 (the cost should name FOWLevel). c5h-b, huge save
`C5H-0168-HUGE_0350` at turn 350 (active player 0), 32-bit pin, stock arm; six launches interleaved S3, F3, S2, S2, F3,
S3; each sampled three times 10 s apart starting 60 s after the map was reached (`tools/harness/memory.py`, read only;
`memory.jsonl` in the archive). Per launch the mean of its three samples:

| Arm | config / FOWLevel | EXE private MB (launch 1, launch 2; mean) | Largest free block MB | `0xDA12AC` FOW Mask |
|---|---|---|---|---|
| S3 | stock / 3 | 1,988.6, 2,004.8; **1,996.7** | 1,237.1, 1,209.5; **1,223.3** | 0x269 (made) |
| F3 | FluidFOW = 1 / 3 | 1,707.5, 1,719.6; **1,713.6** | 1,522.4, 1,475.8; **1,499.1** | 0 (none) |
| S2 | stock / 2 | 1,729.5, 1,703.6; **1,716.6** | 1,465.3, 1,538.1; **1,501.7** | 0x269 (made) |

- **FluidFOW at FOWLevel 3 costs +283 MB private and -276 MB of the largest free block** (S3 - F3; per launch pair
  +281 / +285 MB). This is the "-294" of `disable-systems-and-masks.md` 2 row 15 (one run, FOWLevel not named), now
  named: the owner's setting, FOWLevel 3.
- **At FOWLevel 2 FluidFOW costs about nothing**: S2 - F3 = +3 MB private, +2.6 MB of largest free, inside the
  launch-to-launch spread of one arm (13-26 MB private, 47-73 MB largest free). S3 - S2 = +280 MB. So the whole cost is
  the FOWLevel-3 part (the per-job-thread particle blocks and the particle grid, 3.5 [C]) [L]: FluidFOW at FOWLevel 2
  draws the mask, the FOW CULL and the three cloud planes (the server's route (a) reproduces exactly that, 5.1) for
  no measurable memory.
- With FluidFOW on (S3, S2) the EXE makes the "FOW Mask" `0xDA12AC` = 0x269 and the cloud base `0xDA12B0` = 0x26A; with
  FluidFOW = 1 (F3) both handles stay 0, as under D33 (`darkred.py handles`).
- Indications only, the workstation shared with other teams (COMMON 23): memory is not timing, but the other teams'
  load moves the free-block figure less than the EXE's own allocations; one Goldberg instance, frame cap on.

### 8.4 Phase 2: the joined arms on Tiny (done C, P12, the MC's ask (6)) [L, 2026-11-15 00:13-00:25 EST]

**Build** (COMMON 18): this branch at `35981e9d` (epic `ea63ffd8` with 3952-3955 merged; later commits change only
evidence and research), `shell/evidence/0168/3957/build_joined.sh`: server.exe `3505edc3` and proxy `842b188f` release
against 9045's release deps (x64 DLL `74588135`, pin `x64-04b1211b`), composite `94fcca48`. **Layout**: the D33 product
path as shipped (3702's drivers through `run_3957j.py`: ServerRenderer = 1, composite + manifest, D33 A'' with Overlay on,
WhiteFogMask = 1, SHA-256 backup and restore by hash), c5h-b, VT present. Arms: **fog** = the shipped settings
(`render_fow` default 1, FOWLevel 3 from the player's ini, no server.cfg override): its pictures are the **ACCEPTANCE**
pictures; **fowoff** = `render_fow=0` through `server_cfg_extra=` (TEST ONLY, two LOUD "TEST ONLY" lines in
`vpproxy.log`): DIAGNOSTIC, the product before the epic. Save `C5H-0168J-TINY_0021R` (copy of 3951's t21 state); the
cameras of 8.1 by the same Lua `UI.LookAt` and wheel procedure (camera blocks equal to stock's within launch noise:
`ref-how approximate`); popups checked first, none open (D114); 3702's `shot()` kept the first of up to 10 harness
shots whose top bar matches the stock reference (1 candidate each).

| Camera | ACCEPTANCE (fog) | fowoff (DIAGNOSTIC) | Stock reference (8.1, shot a) |
|---|---|---|---|
| cam1 | `vc-20261115-001713-workstation-ab58` | `vc-20261115-002232-workstation-e811` | `vc-20261114-233932-workstation-b69a` |
| cam2 | `vc-20261115-001741-workstation-aa26` | `vc-20261115-002353-workstation-dd02` | `vc-20261114-234000-workstation-bd54` |
| cam3_p13 | `vc-20261115-001800-workstation-a5e1` | `vc-20261115-002413-workstation-babf` | `vc-20261114-234020-workstation-42fd` |

(each entry carries its difference image, `compare_3957.py`, as the comparison.)

**Per class against stock** (`compare_3957.py`: the class of the pixel's terrain point by the census and the nearest
plot centre, ray cast against the ground truth's height field `D:/c5-64bit-work/i2-ground-truth/tiny`; masked: the
interface widened 16 px, a 16-px frame, 16 px around every pixel a model may cover; largest channel difference,
median / p95 / max and share within 16; "stock" = stock's own shot-to-shot 5 s apart at the same camera):

| Camera, class | fog (shipped) | fowoff (before the epic) | stock shot-to-shot |
|---|---|---|---|
| cam1 unexplored (46 plots) | 25 / 62 / 169, 35.0 % | 83 / 182 / 215, 1.0 % | 7 / 22 / 77, 88.0 % |
| cam1 seen (11) | 7 / 109 / 197, 69.4 % | 51 / 124 / 216, 6.3 % | 3 / 25 / 72, 85.5 % |
| cam1 visible (46) | 5 / 33 / 177, 82.8 % | 5 / 65 / 210, 78.1 % | 0 / 25 / 139, 88.6 % |
| cam2 unexplored (19) | 25 / 63 / 99, 35.1 % | 81 / 161 / 190, 0.4 % | 7 / 20 / 62, 90.9 % |
| cam2 seen (4) | 5 / 26 / 105, 87.8 % | 45 / 81 / 142, 11.7 % | 5 / 20 / 68, 91.5 % |
| cam2 visible (20) | 5 / 26 / 212, 90.0 % | 5 / 28 / 212, 89.7 % | 0 / 21 / 176, 93.1 % |
| cam3_p13 unexplored (7) | 35 / 78 / 172, 3.5 % | 158 / 187 / 210, 0.1 % | 5 / 13 / 80, 97.4 % |
| cam3_p13 seen (6) | 6 / 126 / 192, 68.9 % | 49 / 124 / 189, 10.1 % | 0 / 14 / 24, 97.6 % |
| cam3_p13 visible (32) | 5 / 34 / 176, 87.0 % | 5 / 120 / 202, 77.5 % | 0 / 24 / 93, 92.1 % |

Land / water split, ground-only pixels (neither picture cloud-like) and the JSON per camera: `cmp/tiny_<mode>_<cam>.json`
in the archive. **Reading** [L pictures, I causes where marked]:
- **The leak is closed**: with the fog the unexplored plots show clouds and the seen plots are tinted, as in stock;
  without it (fowoff) unexplored terrain, coast and sea are drawn in full (the 2.9 finding). Unexplored medians fall
  from 81-158 to 25-35, seen from 45-51 to 5-7; seen water (the dark band of (6, 24), (6, 25), (7, 26)) matches stock
  (cam2 seen water 7 / 23 / 56, 87.9 % within 16, stock 6 / 19 / 57, 90.9 %).
- **What is left on unexplored plots is the cloud picture, not the mask**: the cloud planes' phase (the server's
  clock T against stock's, 5.4; 3953's `clock` replay alone gave unexplored 11-22 / 34-54 / 73) and **the FOWLevel-3
  particles, not drawn** (decision 7): cloud-like pixels (p9_shots' rule) cover 26.6 / 13.7 / 24.9 % of the kept pixels
  in stock at cam1 / cam2 / cam3 and 17.7 / 7.2 / 17.9 % in the joined picture [I: the particle layer is the brighter,
  denser white over the cloud edge in stock; not separated from the phase]. Seen-land p95 109-126 at cam1 and cam3 sits
  where the cloud planes overhang seen plots next to unexplored ones (cam3: the seen mountains east of (11, 23)), the
  same phase and particle cause [I].
- **Visible plots**: median 4-5 in both arms (the fog changes nothing there: fog and fowoff equal in median and within
  2 p95 at cam2); the rest is the terrain, water and models of other seams (the visible-land offset of 0167 / 3200 /
  0188's acceptances, trees off under D33, `terrain-colour-reference.md`), not the fog.
- **P13 live**: at cam3_p13 the sheep on the seen plot (11, 23) are **darkened in stock and fully lit in the joined
  picture** (the white mask, decision 3 (i), intended); the ground under them is tinted in both.
- **The overlay over fogged plots** (`overlay-system.md` 5): the city border at cam1 / cam2 runs over visible plots only
  in this state; not measurable here [open].

**P12, the white mask unchanged** [L]: the proxy's `shell: white mask:` lines are **identical in both arms** (after
masking addresses, times and thread ids): `WhiteFogMask = 1: on`, `checks passed`, White.dds loaded, handle 0x2, mask
global 0 -> 2, the boot pool pass (high-water 38,240, live 38,037, building family zero->ours 36,185, forest 0); no
"left alone", no stopped pass.

**The MC's ask (6): what the EXE's own FluidFOW does with `render_fow=1` under D33** [L]: nothing. FluidFOW stays off
(D33's config.ini key), `0xDA12AC` holds **the white mask's handle 0x2** in both arms (no "FOW Mask" target; the EXE's
FOW handles of `darkred.py handles`), and the EXE's private bytes are the same with the server's fog on and off: Tiny
1,231.6 against 1,228.1 MB after the load (+3.5, inside launch noise), largest free block 1,981.4 against 1,993.4 MB.
**The server** carries the fog: 692.4 against 665.0 MB private after the load (+27 MB: the 1024 mask with its mips, the
three cloud textures, the five TerrainFOW volumes, the cloud pass's depth copy, the programs) [L total, I split].
The fog of war at FOWLevel 3 therefore costs the 32-bit EXE nothing, against FluidFOW's +283 MB in stock (8.3).

**Done B(1) at count level** [L]: the server's `fog 1s` line reads `revealed=57` = the DLL census's 57 revealed plots
of the active team, and the mask passes drew `hex=11/46/42` (seen / visible / unexplored) at cam3's window, i.e. all
11 seen and 46 visible plots of the census; state ACTIVE in every 1-s line after the map (186 lines), 0 held frames,
0 LOUD lines in `render.log`, `server.log`, `vpproxy.log` (fog arm). A plot-by-plot comparison needs a dump of
`PlotRecord::fow` the server does not write today [open: a debug dump in 3954's scene would settle it].

### 8.5 Phase 2: the joined arms on the huge save [L, 2026-11-15 00:25-00:45 EST]

Same build, layout and procedure as 8.4; save `C5H-0168J-HUGE_0350` (copy of the instance's `C5H-VP8P-HUGE_0350`), the
eight cameras of 8.1 (`plan_huge.json`); turn 350, active team 0; DLL census 9,808 revealed, 774 visible.

| Camera | Classes on screen (plots) | ACCEPTANCE (fog) | fowoff (DIAGNOSTIC) | Stock reference (8.1) |
|---|---|---|---|---|
| v1_city_default | visible 93 | `vc-20261115-002955-workstation-c7f2` | `vc-20261115-004128-workstation-e3d1` | `vc-20261114-234544-workstation-2c9a` |
| v2_city_zoomin | visible 39 | `vc-20261115-015236-workstation-950f` (retake 01:52, a new launch; the first, `vc-20261115-003104-...-e578`, is a PrintWindow artifact, NOT USED) | `vc-20261115-004203-workstation-698d` | `vc-20261114-234613-workstation-e750` |
| v3_second_zoomin | visible 39 | `vc-20261115-003137-workstation-ad80` | `vc-20261115-004225-workstation-9900` | `vc-20261114-234637-workstation-c06a` |
| b_land | visible 39 | `vc-20261115-003231-workstation-81d7` | `vc-20261115-004248-workstation-9af7` | `vc-20261114-234656-workstation-7253` |
| b_water | visible 39 | `vc-20261115-003255-workstation-2e33` | `vc-20261115-004310-workstation-85fb` | `vc-20261114-234715-workstation-9584` |
| x_arrow | visible 39 | `vc-20261115-003319-workstation-d5a9` | `vc-20261115-004341-workstation-29e0` | `vc-20261114-234735-workstation-0240` |
| p13_model (103, 37) | seen 20, visible 19 | `vc-20261115-003348-workstation-8526` | `vc-20261115-004407-workstation-7db5` | `vc-20261114-234753-workstation-972e` |
| fog_edge (111, 4) | unexplored 10, seen 31 | `vc-20261115-003449-workstation-de1d` | `vc-20261115-004426-workstation-408b` | `vc-20261114-234814-workstation-5623` |

Per class against stock (median / p95 / max, within 16; stock shot-to-shot beside it; the v1 / v3 stock figures from
the retaken pair of 8.1):

| Camera, class | fog (shipped) | fowoff | stock shot-to-shot |
|---|---|---|---|
| fog_edge unexplored | 13 / 28 / 158, 63.4 % | 187 / 202 / 216, 0.9 % | 5 / 13 / 121, 98.3 % |
| fog_edge seen | 9 / 28 / 69, 78.8 % | 73 / 190 / 215, 5.5 % | 8 / 25 / 68, 82.7 % |
| p13_model seen | 3 / 10 / 27, 99.6 % | 36 / 77 / 124, 15.1 % | 0 / 0 / 0, 100 % |
| p13_model visible | 5 / 37 / 243, 90.9 % | 5 / 33 / 126, 92.5 % | 0 / 0 / 239, 99.0 % |
| v1 / v2 (retake) / v3 / b_land / b_water / x_arrow visible | median 4-8, p95 30-89 (v2 7 / 51 / 168, 89.1 %) | the same within 1 median, p95 within 1 (b_water 31 / 33) | 0-4 median |

- **At the six cameras near the capital every plot on screen is visible** (the huge save's 432 unexplored plots lie in
  the polar rows): the fog draws nothing there, and the fog and fowoff pictures agree per class within one level of
  median (the per-class figures are the other seams' differences: decals, trees under D33, lighting; not the fog). The
  fog arm against the fowoff arm directly (whole frame, interface cut): v1 / b_land / b_water / x_arrow / v3 medians 0-1,
  the rest is model animation and the PrintWindow candidates [I].
- **fog_edge**: stock shows clouds over the unexplored polar sea and ice; the fowoff arm shows **open sea where stock
  has clouds** (the leak, and the ice there is not drawn under D33: IceShader is seam G); the fog arm draws the cloud
  edge where stock does (unexplored median 187 -> 13). What is left: the cloud phase and the particles (8.4), and the
  missing polar ice under the clouds' gaps [L picture].
- **p13_model** (a Rome/Aztec improvement and city row on seen plots north of the river): seen ground tinted as in
  stock (seen median 36 -> 3, 99.6 % within 16 on the unmasked ground); the models on those seen plots (city towers,
  pyramids, the fort) stay fully lit in the joined picture and darkened in stock: the white mask's intended
  difference (decision 3 (i)), shown side by side in the feed.
- **Memory** [L, indication]: EXE private 1,393.7 (fog) against 1,392.8 MB (fowoff) after the load, largest free
  1,892.2 against 1,903.3 MB; server 1,511.4 against 1,492.0 MB (+19 MB). `0xDA12AC` = the white mask's handle 0x2 in
  both. Stock S3 on the same save: 1,996.7 MB EXE private (8.3): the shipped fog keeps D33's EXE saving whole.
- **Logs**: `fog 1s` lines `fow=ACTIVE ... revealed=9808` (= the DLL's 9,808) from the map on, 431 lines, 0 held;
  0 LOUD lines in `render.log`, `server.log`, `vpproxy.log` of the fog arm; the white-mask lines equal between the arms
  (as on Tiny). Server frames per second at the frame cap (30-31) in both arms [indication, COMMON 23].

### 8.6 Phase 2: the parity pair (done D) [L, 2026-11-15 00:45-00:58 EST]

`shell/evidence/0168/3957/parity.sh` on c5h-b: the joined product layout of 8.4 (same build), then 0178's method
(`proxy/tools/live/arm.py`, `proxy-design.md` 24.5): Tiny copy `C5H-0168P-TINY_0018` of `C5H-3000-TINY-T0018`
(E337244C, our own name, removed after), 30 AI turns each with the harness checklist (turn 18 -> 48, 159 s of turns),
arm **fow-on** (server.cfg as shipped, `render_fow` 1) then **fow-off** (`render_fow=0` through `server_cfg_extra=`,
TEST ONLY), `eq_stats.py` after the load and at turn 48:

- after the load: **376 keys each, 0 differing: IDENTICAL**; at turn 48: **455 keys each, 0 differing: IDENTICAL**
  (`eq-load.txt`, `eq-end.txt` in the archive). Both arms rc 0, focus guard ok.
- The arms really differed: the fow-on run folder's `render.log` has 188 `fow=ACTIVE` lines (the last at turn 48:
  `revealed=33`, `gen=2`), the fow-off one 230 `fow=OFF` lines; 0 LOUD lines in either `render.log` / `server.log`.
- **Deviation from the brief**: the brief asks for the player moving units to reveal plots in its turns; the checklist
  ends the player's turns without moving units, so the active team's fog changed little (31 revealed at turn 18, 33 at
  48; the AI players' moves do not change the active team's fog bits). The pair proves that the fog does not change the
  game (no DLL read was added: the scene only reads the feed, 5.2); it does not exercise many fog events. A pair with
  scripted player moves is open.

### 8.7 Phase 2: nothing regressed, 30 AI turns on the huge save (done E) [L, 2026-11-15 00:59-01:46 EST]

`shell/evidence/0168/3957/eturns.sh` on c5h-b: the joined product layout of 8.4 (same build), `C5H-0168J-HUGE_0350`,
3308's turns (the player ends each turn; `turns.py end` / `wait`), 30 AI turns (350 -> 379), no reload, arm **fog**
(shipped) then arm **fowoff** (`render_fow=0`, TEST ONLY), each restored by hash. Figures from `run.json` and the copied
logs (`e_summary.py`; `e_summary.jsonl` in the archive):

| | fog (shipped) | fowoff |
|---|---|---|
| rc, end | 0, max turns 30 | 0, max turns 30 |
| EXE crashes / server crashes / "Game Crash" or FATAL | **0 / 0 / 0** | 0 / 0 / 0 |
| LOUD lines render / server / proxy / composite | **0 / 0 / 0 / 0** | 0 / 0 / 2 (the two TEST ONLY lines) / 0 |
| gray frames after the bake-ahead (`gray_after_ahead`, every 1-s line) | **0** in 1,263 lines | 0 |
| fog state lines | ACTIVE 1,157, WAITING 106 (the load), HELD 0, FAILED 0 | OFF 1,233 |
| lockstep over the turns (3308's slices): exact / older / none / nopub; giveups = recoveries | 92,803 / 3,956 / 0 / 0; 16 = 16 | 95,114 / 1,613 / 0 / 0; 7 = 7 |
| EXE private after the load / at the last turn, MB | 1,394.3 / 1,548.0 | 1,405.3 / 1,554.3 |
| EXE largest free block after the load / last turn, MB | 1,895.1 / 1,735.8 | 1,908.0 / 1,733.7 |
| server private after the load / last turn, MB | 1,526.3 / 1,613.0 | 1,438.0 / 1,598.6 |
| server `mem periodic` at the end: private / video_local, MB | 1,613.0 / 552.8 | 1,598.6 / 528.1 |
| turn wall time median / max, s (indication, COMMON 23) | 37.0 / 48.5 | 35.9 / 46.5 |
| white-mask lines (`vpproxy.log`, addresses and times masked) | 5 lines | **identical** to the fog arm's |

- **P12 holds** [L]: the white mask's lines are the same in every pair of arms (Tiny, huge pictures, huge 30 turns):
  checks passed, the boot pass (building family zero->ours 36,185, forest 0), no "left alone", no stopped pass.
- The fog's `gen` counter reached 57 over the 30 turns (fog changes drawn; `revealed=9808` at the end, the player's
  units idle) and no frame was held or drawn without the fog after the load.
- Older-frame lockstep and give-ups are higher with the fog (3,956 against 1,613; 16 against 7) [indication: one run per
  arm on the shared workstation; I: the fog's extra passes make the server's frame heavier; not a timing claim, COMMON 23].
- **Not covered**: the 0172 cameras' model-texture check (models as in the fog-off arm) was not repeated; the pictures of
  8.4 / 8.5 show the models textured in both arms.

### 8.8 Phase 2: the failure route with the real modules (done F, live part) [L, 2026-11-15 01:53-01:56 EST]

`after_e.sh` arm `tiny:ovloff` on c5h-b: the joined layout of 8.4 (the real `c5r::fow` modules, build `3505edc3`),
`render_overlay=0` through `server_cfg_extra=` (TEST ONLY), the fog at its default (`render_fow` 1): the scene's
install-failure case "fog needs the overlay pair" (decision 5 (ii), design 5.5). Timeline from `server.log`:
48.506 s `terrain: state NO_FIELD -> OK` (the first wanted frame), **48.515 the one LOUD line** `fog of war: render_fow=1
needs the overlay pair (the mask shares its window), but render_overlay=0 (decision 5 (ii)); unrevealed plots cannot be
hidden, no frame is drawn`, 48.541 `fog: the 64-bit path cannot go on`, **48.589 `remote: exit code 5`**; `vpproxy.log`
49.414 `call rt:LockSync ... failed: PeerDied`, `FATAL: the server failed a call (on_server_death=exit): the process ends
with exit code 3`; the harness saw the EXE `gone` (01:56:06). **`render.log`: drawn=0 and published=0 in every 1-s line,
no line with a drawn frame**: the map was never shown without its fog (D118). The instance was restored by hash.
- With the overlay off by configuration the failure is **immediate at the first wanted frame**, not after the 10-s wait;
  the 10-s wait (the tap's pair not yet published while the overlay is on) was not provoked live [open: its stand-in test
  `no_overlay_pair` covers it, 7.3].
- This dev layout has pack = 0: no report folder, popup or config.ini restore (0167 12.5 (a)). **D125's route on the
  shipped pack layout was shown live by 3954 (7.3 Live (b), oldpc) with the stub build only; with the real modules it
  was shown by ticket 0342 on 2026-11-17 (7.3 Live (e): 0463's take, the install without `fogcloudtile_dif_01.dds`, 11 of
  11 facts).** *Corrected 2026-11-17 (0342); was: "with the real modules it is not re-shown [open: a pack-layout job with
  a player-meetable fault, e.g. a copy of the install without `fogcloudtile_dif_01.dds`, is the remaining step]".*

### 8.9 Phase 2: the suites (done G) and where the definition of done stands [2026-11-15 02:09 EST]

**Suites** (`shell/evidence/0168/3957/suites.sh`, this branch at the build commit's product code, workstation, through
the launch gate; C5R_EXE = the Ghidra copy of the EXE, C5R_GAME_DIR = c5h-b's install, VT present) [run]:
- render `c5r_fow|c5r_terrain|c5r_water`: plain **14 of 14 passed, 0 skipped** (01:59); MSVC `/fsanitize=address`
  **14 of 14, 0 skipped, 0 ASan reports** (02:03);
- the server's fog scene (`test_fow_scene`, 11 cases in 3 pytest tests): plain 3 passed (02:05), ASan 3 passed, 0 ASan
  reports (02:08);
- 0167's overlay scene (`test_scene_overlay`): plain 3 passed (02:06), ASan 3 passed, 0 reports (02:09).

**The brief's definition of done, as 3957 leaves it** (A is C1's, recorded in sections 1-6):
- **B(1)** fog state: count-level equal after the load on both maps (`revealed` 57 = 57, 9,808 = 9,808; the window's seen
  and visible hexagon counts = the census's), 8.4 / 8.5; **plot by plot and at every turn of a 30-turn run: open** (the
  server writes no dump of `PlotRecord::fow`). B(2)-(4): offline by 3952 / 3953 (7.1, 7.4; 8.2 names the second
  on-screen TerrainFOW volume America).
- **C** frame level: ACCEPTANCE pictures on the shipped system at 3 Tiny and 8 huge cameras with stock references,
  per-class figures and difference images (8.4, 8.5); the unexplored class stays outside stock's own shot-to-shot by the
  cloud phase and the FOWLevel-3 particles (intended, 5.4), seen models lit (intended, decision 3 (i)).
- **D** parity pair: IDENTICAL (8.6), with the deviation named there (no scripted player moves).
- **E** 30 AI turns: 0 crashes, 0 LOUD, 0 gray frames after the bake-ahead, white mask unchanged (8.7).
- **F**: the real build's install failure live on the dev layout (8.8); D125's pack-layout route with the real modules:
  shown by ticket 0342 on 2026-11-17 (7.3 Live (e)). *Corrected 2026-11-17; was "open (3954 showed it with the stubs)".*
- **G**: above. **H**: the corrections listed in 3.11 and the brief's H were made in place on 2026-11-14 / 15
  (`terrain-rendering-inventory.md` 2.5 and row F, `terrain-bake-passes.md` 3.3, `asset-pipeline.md` 3.2-3.4,
  `water-system.md` 10.2 / 10.4, `overlay-system.md` 5, `terrain-colour-reference.md` causes table,
  `renderer-in-server-design.md` 2, `disable-systems-and-masks.md` 2 / 5, `allocations-and-memory-map.md` 5;
  `terrain-surface.md` 1-2 by 3953). `sets.cpp`'s FOW rows are not 3957's (3952 traced row 52).
- **Pointers owed outside section 8** (for the lead): 7.4's "Not verified: each tile's style by `tileStyle` on 3951's
  map" is settled by 8.2; 2.9's "[open for C7]" (P13 seen-plot models) by 8.1 and 8.5; section 1's "FluidFOW's large
  memory is a FOWLevel-3 cost only [C]" is now [L] by 8.3.

**Archive** (D141): `F:/c5-64bit-work/0168/3957/` (pictures, comparisons and difference images, censuses, art types,
saves, memory records, the runs' logs, parity evidence, styles results; SHA-256 checked); the C: scratch and the build
trees were removed at hand-back.

## 9. The EXE's model draws read the server's mask (ticket 0339, decision 3 (ii))

Ticket 0339 (account B, branch `task-0339-fow-models-server-mask`), 2026-11-15 22:16 EST on. The MC allowed the three
EXE writes by name (answer #2058 to question #2056, as D136's kind; every write checked against its expected old value,
a mismatch is the D118 / D125 route with a named reason). Replaces 5.4's third bullet ("Models on seen-not-visible plots
keep the white mask"): with `model_fow=1` (the default since this ticket) they read the server's mask.

### 9.1 What is written, by whom, and why it is enough [C]

| Write | By | Expected old value | Why |
|---|---|---|---|
| The white handle's view-table entry `0x414C348 + 12 h` (+0, the view the bind function `0x8BD160` takes) := an SRV of the composite's own copy of the server's mask | `shell/composite/fow_models.cpp`, Present (the EXE's main thread, between frames), after its first copy | the white view read at the install (a D3D11 SRV: its vtable equals a reference SRV's) | every draw object whose mask slot holds the white handle (building family t7, forest t6, improvements and resources at load, cities and wonders by the proxy's pool pass, ice t8) now samples the server's mask: one write moves them all |
| `0xDA12B0` (FOW base layer) := the handle of `FogCloudTile_dif_02.dds`, loaded by the EXE's loader `0x8C6540` (bare name, as FluidFOW's own call at `0x44AF23` does) | `proxy/src/proxy_shell.cpp` `White::InstallBase`, at the boot, after the white handle | 0 (compare-and-swap) | the builders copy it into t8 (building `0x7B2695`, city `0x5A25E5`) / t7 (forest `0x6C224A`) of objects built later |
| The base-layer slots of the live draw objects whose mask slot is white (building family +0x28, forest +0x26) | the proxy's pool pass (boot, then once a second from Update) | 0 | objects built at app Startup (cities, wonders, trees) copied 0 (handle 0 = the dark red); with a real mask an unexplored texel shows `lerp(base, c, m.x)` = the base |
| FOWAnim `0xDA1C40..4C` := the server's (k, k, -2 T, 0) of the mask's frame | the composite, every Present that copied a new mask | the value written last (at the install: 0, or FluidFOW's start value 0.002 x 4) | the model passes `0x4FB320` / `0x674AD0` copy it into cb row 12 (pattern at `0x4FB640` / `0x674DF1`); the base layer's uv moves with it |

The model passes copy OverlayProj[v] into cb rows 8-11 (at `0x4FB62D` / `0x674DDE` each pass addresses the source
OverlayProj 0xDA1690 + the view's offset and the destination row 8, 0x80 bytes into the block; pattern bytes replaced
by this description, 2026-11-18, D83 audit) [C, this ticket's byte check of the DX11 EXE on disk, 2026-11-15], so the models sample the mask in the overlay
window, which is the window the server builds its mask in (the overlay pair, 5.2): only the frame differs (9.3).

**Hand-over** (`shell/common/shell_fowmask.h`, 0167's slot protocol in the other direction): the server
(`server/src/render/srv_fow_mask_pub.cpp`, called by `FowScene::handOver` after each mask build) makes the section
`Local\VPShellFowMask.<EXE pid>` once it knows the EXE's pid (the scene's tap blocks: one line in
`srv_render_scene.cpp`), makes three NT-shared slot textures (the mask's size and full mip chain, R8G8B8A8 typeless,
`Local\VPShellFowMaskTex.<pid>.<epoch>.<slot>`) **only while the composite sets `wanted`**, copies each new mask
(CopyResource, all mips) into a slot that is neither the newest nor the composite's `inUseSlot`, and publishes it with
its pair (OverlayProj per view, generation, presentCount) and FOWAnim once the copy's event query signalled. The
composite names the newest slot in `inUseSlot`, checks the slot's record after naming it (the server checks `inUseSlot`
after claiming a slot: one of the two sees the other), copies it into a texture of its own on the EXE's device (all
mips) on the immediate context at Present, so the next frame's command lists read it, and lets go of the slot when its
own query signalled.

**Failure route** (MC #2058): a byte pattern that does not match, a white entry that is not a D3D11 view, the proxy's
base-layer refusal (`VP64SHELL_FOWBASE_WHY`, e.g. `0xDA12B0` not 0 at the boot or a white entry whose base slot is foreign),
a checked write whose old value differs (the view entry, FOWAnim), a slot texture that does not open, a local texture
that cannot be made, an access violation, no mask within `fow_models_wait_ms` (30 s) of map frames: the composite puts
the white view and FOWAnim back, writes `consumerState` FAILED with the reason into the section, and the server's
`FowScene` fails its install with that text (`FowFatalStep`: the report, the popup, the end of the game, D125). The
server stopping (STOPPED) puts the white view back (LOUD, counted) until masks come again. Its heartbeat still for
`fow_models_stale_ms` (2 s) is a *late* episode since ticket 0415 (2026-11-16): the last mask stays bound, one line at the
start and one at the end, LOUD once only past `fow_models_late_loud_ms` (30 s); a new map load in the same process puts the
white view back until a mask built after the load is copied (`fow-models-compile.md` section 6). (Corrected 2026-11-16 at
0415's merge; it read: "STOPPED, or its heartbeat still for 2 s, puts the white view back".) Without the white mask (WhiteFogMask = 0: `VP64SHELL_WHITE` not set) or with `model_fow=0` the model fog is OFF:
nothing wanted, no slot texture, today's picture. At a resize and at the process's end (DLL_PROCESS_DETACH) the
view and FOWAnim are put back and logged.

**Cost**: the server makes 3 slots of the mask (1024, 11 mips: 3 x 5.3 MB of video memory) only while wanted; the EXE
one local copy (5.3 MB of video memory) and one CopyResource per new mask (a mask is rebuilt when the fog generation or
the overlay pair changes: every frame of a pan, none at rest).

### 9.2 Tests [C, 2026-11-15]

- `shell_unit` (proxy toolchain, `proxy/tools/test_proxy_rt.py --only-shell`): **313/313** (was 267 before this ticket;
  the "white" section unchanged and passing), test_proxy_rt **19/19**; under MSVC `/fsanitize=address` 313/313, no report,
  the canary caught (25/25). New section "model fog's base": not wanted (no second load), WhiteFogMask = 0, on (two loads,
  `0xDA12B0` 0 -> 4, building t8 and forest t7 written only for white entries), the Update pass, a foreign base slot
  (named, no further base write, white unaffected), every base pattern and operand changed (named, white still on, no
  load), the global not 0 at the boot, the loader's 0 / non-handle / no view / exception, a second boot taking it over.
- `server/tests/render/fow/test_fow_scene` **16/16** (two new cases): the section only with the EXE's pid, nothing copied
  without `wanted`, the record (pair, FOWAnim) and the slot texture by name, the named slot never written, the consumer's
  FAILED fatal once with its reason, OFF releasing the slots.
- `shell/tests/test_fow_models.py` **13/13** (stand-in host with a synthetic EXE in the DLL, the real producer code as
  `fowmask_producer.exe`): hand-over, swap, read-back of every copy against its record, FOWAnim and the lag counted,
  STOPPED -> white view back -> swapped again, the restore at the process's end, the six failure modes each FAILED with
  its reason at the server (WHITE, MISMATCH at the install and in play, BASE twice, EXCEPTION), OFF twice, TIMEOUT, and the
  composite's seven byte patterns against the DX11 EXE on disk.

### 9.3 Live on the D33 product path [L, 2026-11-15 23:17-23:58 EST]

**Setup.** c5h-b (Goldberg, C:, 2554 x 1411, VT present), the instance of 3957's stock references. Build from this branch
at `2952786e`: server.exe `107A0756...` and proxy `67B24731...` release against 9045's release deps (x64 DLL `74588135`, pin
`x64-04b1211b`), composite `8CF85685...`; `C:/c5-64bit-work/0339/build_product.sh`. Driver
`shell/evidence/0339/run_0339.py` on 3957's `run_3957j.py` (D33 in the shipped A'' form with Overlay on, WhiteFogMask 1,
ServerRenderer 1, FOWLevel 3 from the player's ini, SHA-256 backup and restore by hash), arms **mfog** (the repository's
`shell_composite.ini`: `model_fow=1`, shipped) and **white** (`model_fow=0`, TEST ONLY: the product before 0339); saves
3957's copies (`C5H-0168J-TINY_0021R` turn 21, `C5H-0168J-HUGE_0350` turn 350); 3957's cameras and procedure (popups
checked, none open: D114); stock references = 3957's phase-1 shots of the same instance (FOWLevel 3, `i0ship-26234371-ia`).
Note: c5h-b's front-end Lua was found in a patched state left at 05:12 (not by this ticket; `frontend.py status`), which
refused two prepares; `frontend.py -i c5h-b restore` put the original files back by hash before the arms ran again.

**The install, live** (Tiny, mfog): proxy `shell: white mask: base layer (ticket 0339): loaded FogCloudTile_dif_02.dds in
1.6 ms: handle 0x3`, `0xDA12B0 0 -> 3`, boot pool pass `base layer ... zero->ours 36,194` (= the building family's white
entries); composite `INSTALLED: white handle 0x2 ..., cloud base 0x3, FOWAnim start (0, 0, 0, 0)` (so nobody writes
`0xDA1C40` under D33 [L], 3.10's [I]), section open 1.8 s later, `SWAPPED` at the first mask (generation 1, pair
generation 48), FOWAnim written from then on; server `3 slots of 1024x1024, 11 mips ... 16.0 MB of video memory`,
consumer ACTIVE; 0 LOUD lines in `render.log`, `server.log` and the composite's log. The white arm: composite `OFF
(model_fow=0)`, reported in the section, the server made no slot texture. EXE private after the load (three samples, one launch each, an indication): Tiny mfog 1,265.5 MB against
white 1,271.1 MB, huge 1,443.8 against 1,446.8 MB: **no EXE cost** (the composite's local mask is 5.3 MB of video
memory, not private bytes); server private Tiny 739.6 / 740.1 MB, huge 1,619.5 / 1,620.5 MB (its three slots are video
memory).

**Models on seen-not-visible plots against stock** (`shell/evidence/0339/models_0339.py`: model pixels = where mfog and
white differ by more than 24 in a channel inside a box; largest channel difference to stock's shot a; stock a against b
5 s apart for scale). Tiny cam3_p13, the three sheep of the seen plot (11, 23), box (1180, 600)-(1420, 780), 9,377 model
pixels:

| On the sheep's pixels | median / p95 / max | within 16 | mean value |
|---|---|---|---|
| mfog (model fog) vs stock | **3 / 47 / 104** | **79.7 %** | 73.9 |
| white (before 0339) vs stock | 75 / 198 / 240 | 1.3 % | 113.9 |
| stock shot a vs shot b (P9) | 2 / 45 / 99 | 81.1 % | (stock 71.7) |

So the sheep are **tinted as in stock, within stock's own shot-to-shot** (the rest is their idle animation), where the
white mask drew them fully lit. Figure: `C:/c5-64bit-work/0339/captures/models_tiny_cam3_sheep.png` (stock / mfog /
white / the model-pixel mask), Visual Checks `vc-20261115-235244-workstation-2197` (the ACCEPTANCE picture
`vc-20261115-233421-workstation-a580`, the white arm `vc-20261115-234105-workstation-0f49`).

Huge turn 350, camera p13_model (the citadel, a pyramid and farms on seen-not-visible plots around (103, 37)), box
(827, 425)-(1727, 985), 149,458 model pixels (stock's camera matched without a shift):

| On the models' pixels | median / p95 / max | within 16 | mean value |
|---|---|---|---|
| mfog vs stock | **0 / 61 / 231** | **67.8 %** | 89.8 |
| white vs stock | 49 / 113 / 238 | 12.0 % | 129.5 |
| stock shot a vs b | 0 / 0 / 157 | 99.9 % | (stock 90.0) |

More than half of the models' pixels are **bit-equal to stock** (the EXE draws the same static models with the same
mask texels, base layer and LUT); the rest are the model edges over the server's ground, which differs from stock's by
the other seams (3957's figures). Figure `vc-20261116-000940-workstation-c095` (ACCEPTANCE picture
`vc-20261115-235000-workstation-27bd`). The other seven huge cameras and the Tiny cam1 / cam2 are in the feed under 0339
(ACCEPTANCE, mfog) and their white-arm twins (DIAGNOSTIC).

**WhiteFogMask = 0 still gives today's dark red** [L] (0172's driver through `shell/evidence/0339/run_0339w.py`, huge
turn 350, the same build with `model_fow=1` by default): the proxy `WhiteFogMask = 0: off (nothing loaded, nothing
written); the model fog (ticket 0339) needs the white mask: it stays off`, the composite `OFF: no white mask in this
process`, reported in the section; dark-red share of the window at the capital **14.32 %** and Akhetaten **4.87 %**
(0172's off arm 14.2 / 4.56 %, its on arm 0.57 / 0.50 %, stock 0.66 / 0.60 %); the load view's shot was a grey
PrintWindow frame (capture glitch, not a result).

**The window lag** (the composite's 10-s line: at each Present the EXE's OverlayProj of the frame just drawn against
the window of the mask its models read; Tiny mfog, frame cap 30): at rest **every frame exact** (0 lag frames in every
still 10-s window). The mask its models read was built from the overlay pair of **4.0 Presents before** the frame (mean
over all map frames; the overlay tap copies at Present N, the server builds and publishes at its next frame, the
composite copies at the following Present, the frame after that draws with it). The window moves in steps of about a plot
(one plot = 79 texels at the 832-unit window) [I: the per-frame offsets were not traced (`fow_models_trace=1` would);
the means of 79-151 texels fit steps of one or two plots], so the lag shows only in the frames where the window stepped within the
last four: during the camera moves and the 16-s arrow-key pan **19-91 of 300 frames a 10-s window** (6-30 %) read a
window 1-2 plots off (mean 100-151 texels, max 403 texels when `UI.LookAt` jumped between cameras); 236 of 6,612 map
frames over the whole run. Huge (zoom 11 at the last cameras: one plot = 113 texels of the 576-unit window): 9,881
exact of 10,490 map frames, 609 with a lag, 170-186 of 300 frames during the arrow-key pan (mean 408 texels, the pan
is faster on this map), 0 at rest. In those frames a model near a fog boundary takes the fog of a plot one or two plots away
for up to four frames (133 ms at 30 fps) [I: visible as a brief flicker of the tint on models at fog edges while
panning; not seen in the still pictures]. Not removed here; a follow-up could shift the copy by the window difference
(a whole-texel CopySubresourceRegion offset, exact for a pure translation) [I]. *Corrected 2026-11-16 by 0394 (9.5):
most of these frames are not translations: the window's size steps by 64 units as the camera moves (227 of 266 off
frames on Tiny); removed by 0394 with a shift and a resample pass; the "mean 100-151 texels" above counted only the
translation part.*

### 9.4 Not verified, and follow-ups (2026-11-16)

- **Not verified live**: the failure route end to end in the game (each failure mode is tested on the stand-in with the
  real producer and the server's scene cases, 9.2; no live arm forced a mismatch); a resize or a second load in one
  process with the model fog swapped (the shutdown path puts the view back and installs again, stand-in only); the
  restore at the process's end in the game (the harness stops the EXE by its launcher, no DLL detach; the stand-in shows
  it); trees (Forest stays off in D33); FOWLevel 0-1 (512 mask); the cloud base's look on a model under an unexplored
  edge (no model stood there at the cameras); the per-frame lag offsets (not traced).
- **A resize to show the restore path live hung the game (2026-11-16 00:25, c5h-b, not the model fog)** [L, dump]: the
  driver called `OptionsManager.SetWindowResolution_Cached` + `CommitResolutionOptions` straight from the Lua channel,
  which run-and-drive-the-game.md already forbids (a window-touching call on the game-core thread). The presenting main
  thread then waited in `EnterCriticalSection` on a lock held by a thread in `NtUserSetWindowPos` (through the focus
  guard's hook): a deadlock, no `shell_composite` frame on any stack (dump
  `F:/c5-64bit-work/0339/dumps/c5h-b-x86-65992-20261116-003331.dmp`, taken 00:33 with the game still in that state). The
  driver now uses a one-shot `Events.LocalMachineAppUpdate` handler, as that file says. Stopped at 01:01 by the MC's
  order (#2146, D60: `cli.py stop --coordinator-override`, rc 0, no leftovers); the 15 standard files restored by hash
  (9 equal, 6 absent as before) and `provision.py verify c5h-b` rc 0, no misses; the front-end Lua at its original
  hashes. **Can a player reach it?** [I] Not by this route: a player changes the resolution in the options screen, whose
  Lua runs in the front-end / in-game UI context on the EXE's main thread, the thread that also owns the window; the
  deadlock needs the call on the game-core thread (the harness's Lua channel inside the DLL's update), which no player
  path uses. The main-thread route was run live on the product path before (3008 / 9031 in
  `shared-texture-and-compositor.md` and `shell-foundations-verification.md`: the composite adopted the new chain in
  34-69 ms), but not yet with the model fog swapped in; its shutdown path (the view put back at ResizeBuffers) is
  tested on the stand-in only.
- **Cost** [L, I]: the mask is rebuilt and handed over at every overlay pair, i.e. every frame (the overlay tap copies
  every map frame): 6,588 copies in 6,612 Tiny map frames, one 1024 x 11-mip CopyResource on each side per frame. Not
  timed (no timing claim made; COMMON 23).
- **Follow-up proposed** (for the MC to number): remove the pan lag by copying the mask shifted by the window
  difference (the composite knows both OverlayProj at its Present; for a pure translation a texel offset is exact,
  the strip that comes into view stays as the previous mask's edge) [I], or hand the EXE's current OverlayProj to the
  server earlier so that the 4-Present path shortens. *Done 2026-11-16 by ticket 0394 (section 9.5), the first way, with
  two corrections to this text: the offset is not a whole number of texels (64 world units = 78.77 texels at the
  832-unit window, 93.09 at 704; it is rounded per mip, at most half a texel off), and the entering strip is filled with
  a chosen value, not the previous mask's edge.*

### 9.5 The mask moved to the frame's window (ticket 0394, 2026-11-16)

**Note 2026-11-16 (ticket 0407):** the resample pass's programs are compiled at build time (`fow_models_resample.hlsl`, fxc in `shell/build.cmd`); `d3dcompiler_47` is no longer loaded at run time. See [fow-models-compile.md](fow-models-compile.md).

Ticket 0394 (account B, branch `task-0394-fog-mask-lag`). Removes 9.3's window lag of the model fog during pans.

**What the composite does now** [C, `shell/composite/fow_models.cpp`]:
- The newest slot is copied as delivered into `g_src` (on the EXE's device, all mips) with its record (the window the
  server built it in). The texture behind the white handle (`g_local`, what the models read) is made from `g_src` for a
  given EXE window: when every OverlayProj element but the translation is equal (same zoom), every view moves by view 0's
  offset (the wrap view is view 0 moved by 3583 units) and the offset is under half the mask, `g_local` = `g_src` moved
  by `dx = (tx_src - tx) W / 2`, `dy = (ty - ty_src) H / 2` texels (texture v runs against NDC y): one
  `CopySubresourceRegion` per mip with the offset rounded at that mip (`lround(dx / 2^m)`), and the strip that has no
  source (the plots that came into the window) copied from a fill texture.
- **The window's size steps too** [L, 2026-11-16 01:43-01:48, c5h-b Tiny, the trace's new columns]: the EXE snaps the
  window's size as well as its origin to 64 units, so while the camera moves the size changes by one step on one axis
  (832 x 640 <-> 832 x 704, 896 x 640 <-> 896 x 704, 576 x 448 <-> 576 x 512, 704 x 512 <-> 704 x 576 ...) without any
  zoom: of 266 Tiny frames whose window differed from the mask's in the first arm, **227 differed in size** and only 34
  were pure translations (r2). (Corrects 9.3's picture of a window that "moves in steps of about a plot": it moves and
  resizes in such steps; overlay-system.md P4 had the snap of origin and size [I], this is the live effect.) So the
  first form (translation only) moved 2 % of the off frames; the second form adds a **resample pass**: when the scale
  differs (same views), a full-screen triangle draws mip 0 of `g_local` from `g_src` through the uv map of the two
  windows (`u' = ax u + bx`, `v' = ay v + by`, bilinear, outside [0, 1] = the fill), then `GenerateMips` (the server
  builds its mask's mips with `GenerateMips` too, `fowmask_producer.cpp`'s note on `c5r::fow::MaskRenderer`). Its two
  programs are compiled once at the install by the system's `d3dcompiler_47.dll` (`D3DCompile`, vs_4_0 / ps_4_0; no
  build step); the immediate context's state it changes is saved and put back around the draw. Without the compiler or
  a render-target view the pass is off (LOUD once) and such frames read the mask as built. `g_local` is now made with
  RENDER_TARGET and GENERATE_MIPS. Otherwise (a jump over half the mask, views that do not move together, a view count
  change) `g_local` = `g_src` as built: 0339's behaviour for those frames, counted.
- **When**: at the Present after each new mask, for the window of the frame just presented; and again at the **next
  frame's first ExecuteCommandList** on the immediate context when the EXE's OverlayProj changed (a one-call hook in
  `shell_composite.cpp`'s ExecuteCommandList detour, granted by the MC, answer to #2175). OverlayProj[N] is final at
  UpdateViews of frame N, which runs before the frame's first list (overlay-system.md "the frame rule",
  lockstep-probe.md 2 [L]), and the model passes' lists execute after it, so the copies on the immediate context are
  ordered before the frame's model draws [C, D3D11]. A host without ExecuteCommandList (the stand-in without
  `--deferred`) moves the mask at the Present instead (one frame late at each window step).
- **Exactness**: the window moves in steps of 64 world units, which is 78.77 texels in x at the 832-unit window
  (93.09 in y at 704): not whole texels, so the moved mask is at most half a texel (0.41 world units in x at that zoom)
  off the mask the server would build for that window, per mip at that mip's texel. A resampled mask is a bilinear
  sample of the server's: exact where the mask is linear (inside the ramps and plateaus), smoothed over one source texel
  at the ramps' kinks. At rest the offset is 0 and the copy is `CopyResource` (bit-equal, as 0339). Named per COMMON 25
  as a step: the exact version needs the mask built for the frame's own window, which no process has before the frame.
- **The strip's fill** (MC #2175 answer: say which fog state it means and pick the one that matches stock most often).
  The building PS reads `m = mask.xz` (x explored, z visible; disable-systems-and-masks.md "Why the models are dark
  red"): `c = lerp(base, c, m.x)`, `c = lerp(c, LUT(c), m.x (1 - m.y))`; the forest PS discards where `m.x == 0`.
  So **zero** (0, 0, 0, 0) and **unseen** (0, 1, 0, 1) look the same on models, the **unexplored** look (the cloud base
  layer, trees discarded); **seen** (1, 0, 0, 1) is the seen look (the LUT tint); **edge** (the default) takes, for each
  strip texel, the moved mask's nearest known texel 24 mip-0 texels further in (past the server's undrawn rim), i.e. the
  state of the plots beside the strip, so it can give any look, the visible one included where the plots beside the
  strip are visible. The edge fill is made by the resample pass (a clamp of the source uv) for every move. Choice and
  measurement: the live numbers below.
- **Keys** (`[composite]` of `shell_composite.ini`): `fow_models_shift` (1; 0 = the comparison arm, 0339's lag),
  `fow_models_fill` (edge, the default | zero | unseen | seen), `fow_models_verify` (0; 1 = diagnostic: when the mask
  built for the window the local mask was moved to arrives, both are read back (mip 0) and compared: the known texels,
  and for the strip the server's class and which fill would have given the models' look (x and z within 16), the edge
  candidate included; at most every `fow_models_verify_ms`, 1000; the frame waits for the read-back).
  `fow_models_trace=1` writes per frame: the source offset, the shift, the residual, how the local mask was made, the
  frame's and the mask's window sizes and views, whether the frame's window moved after its frame start.
- **Which elements**: a window "changes size" when any element that places a point in the mask differs (the x, y and w
  columns); the z column is ignored: the wrap view's z translation carries float noise (0.500007629 against 0.49999997,
  2026-11-16 [L]) that the first form read as a size change. A resample needs every view to map the same way (else as
  built, `views`).
- **Measures** (the 10-s lines): `10 s:` as in 9.3 (the **source lag**: the server's window against the frame's, what the
  models read before 0394); `10 s shifted:` (the **effective lag**: a frame is off when the scale differs or the residual
  is over half a texel on an axis; frames moved, made as built and why, frames whose window moved after the frame start);
  `10 s strip:` (frames that read a strip, how many of them put a strip texel on screen: every 8th strip texel from the
  window to world at z = 0 and through the camera block's view 0 and projection; an upper bound of "drawn on a model",
  since a model must also stand there; with the window centre's check). The section's `lagFrames` now counts the
  effective lag.
- **Cost**: two more textures of the mask's size on the EXE's device (`g_src`, the fill: 2 x 5.3 MB of video memory at
  1024, 11 mips); per moved mask one full-screen draw into 1024 x 1024 and a `GenerateMips` (the edge fill and size
  steps), or about 33 `CopySubresourceRegion` calls for a shift with a constant fill; at rest nothing more than 0339 (a
  `CopyResource` per new mask). The programs' compile at the install (once per device). Not timed (no timing claim;
  COMMON 23).

**Stand-in tests** [C, 2026-11-16, `shell/tests/test_fow_models.py` **16/16**, of which 3 new; build of 4eb455c5]: the
stand-in's synthetic EXE and `fowmask_producer.exe` take the window from one clock (`shell_fowmask.h` `FowTestPan`: an
832 x 704 window stepping 64 units, 6 steps in x and 3 in y, out and back over 3 s, 896 units wide at steps 3 and 4), so
the producer's masks lag the frames by the hand-over's latency at each step; the producer writes a 5 x 5 block of the
pair generation at the world origin's texel, and after every local mask the composite reads back the texel where the
window it moved to puts the origin (and, on a shift with a constant fill, the strip's edge texel against the fill).
First form, `--deferred` (the frame as a command list, the frame-start hook), fill zero: 687 read-backs 0 bad; source
lag 8,856 of 158,248 frames, **effective lag 0**, residual max 0.248 texels; 0 frames whose window moved after the
frame start. With the size steps and the resample pass (`--deferred`, fill zero, verify every eligible mask): 677
read-backs 0 bad, 6,468 source-lag frames, effective lag 0 (4,182 shifted, 2,286 resampled); the verify lines: shifts
99.97 % bit-equal on the known part, resamples 99.97-99.99 %. Without `--deferred` (moved at the Present), fill seen
and fill edge: read-backs 0 bad, a few dozen frames still off (a step between the Present and the next frame) against
some 8,500 source-lag frames. `fow_models_shift=0`: read-backs 0 bad, effective lag = source lag, nothing moved.
`server/tests/render/fow/test_fow_scene.py` 3/3 (16 cases; its case list now names 0339's two model-fog cases, which
the program ran but the list check missed: it failed on `main` since 0339).

**Live on the D33 product path** [L, 2026-11-16 01:58-03:30 EST, c5h-b (Goldberg, C:, 2554 x 1411, VT present), 0339's
saves, cameras and pan (`shell/evidence/0394/run_0394.py` on 0339's driver: the arrow keys held 4 s each way, RIGHT,
LEFT, UP, DOWN), server.exe `EF08719D...`, proxy `12C6F5C8...` (repository 244d935e..0837124c: no server or proxy change
in this ticket), x64 DLL `74588135` (pin `x64-04b1211b`), composite as named per arm; `shell_composite.fowmodels.csv`
per frame, analysed by `shell/evidence/0394/lag_0394.py` (a "move" = map frames whose window differs from the mask's,
runs closer than 1 s merged; the last four are the pans) and `verify_0394.py`]. **Before** = the source lag (the window
the models read with 0339's composite; the same in the comparison arm `fow_models_shift=0`, which reads it), **after** =
the effective lag. Off frames per pan, largest offset in plots (64 units; a size step counts as one plot):

| Map, arm | Pan 1 | Pan 2 | Pan 3 | Pan 4 | Whole run |
|---|---|---|---|---|---|
| Tiny, before (shift=0, composite of 244d935e) | 27 off, 1 plot | 62, 1 | 29, 1 | 50, 1 | 237 of 6,998 map frames |
| Tiny, after (composite 244d935e, fill zero) | 0 (source 27) | 4 (source 66; a view-count change) | 0 (29) | 0 (50) | 4 of 7,245 (source 241) |
| Tiny, after (composite 4eb455c5, fill edge) | 4 (source 62) | 0 (4) | 0 (29) | 0 (49) | 4 of 7,000 (source 240) |
| huge t350, before (shift=0) | 105 off, 1 plot | 101, 1 | 108, 1 | 102, 1 | 608 of 10,660 |
| huge t350, after (composite 244d935e, fill zero) | 0 | 0 | 0 | 0 | 15 of 9,829 (source 609) |
| huge t350, after (composite 4eb455c5, fill edge) | 0 | 0 | 0 | 0 | 17 of 13,754 (source 614) |
| **Tiny, after (0837124c, composite 972292E6, the hand-back's)** | 0 (source 27) | 4 (62) | 4 (29) | 1 (50) | 12 of 7,358 (source 237), all view-count changes |
| **huge t350, after (0837124c)** | 0 (source 105) | 0 (101) | 0 (107) | 0 (105) | 17 of 11,517 (source 610): 9 too far, 8 views |

The frames still off are camera jumps (`UI.LookAt` between the cameras: 7-11 plots, more than half the mask, "too far")
and a view-count change (the wrap view appears or goes); none during the pans. 0 frames whose window moved after the
frame start in every arm (OverlayProj is final before the frame's first command list [L]); 183-540 local masks per run
were made at a frame start. The mean pair lag stays 4.0 Presents (the hand-over is unchanged).

**The moved mask against the mask built for its window** (the verify, 4eb455c5; mip 0): Tiny 9 read-backs (all
resamples), huge 19: known texels 95.98 % / 98.31 % bit-equal, p50 0 and p95 0 in all but one Tiny read-back. The rest
is the server's undrawn band at its window's edge (about a plot): when the window shrinks by a step, the moved mask
holds drawn plots where the new mask has its band (the one Tiny read-back at 86.8 % had 13 % of its texels, about one
plot along one edge, at 255). **Measured on 0837124c** (the inner part, known texels at least 128 texels from the
window's edge): Tiny 9 read-backs, 97.10 % bit-equal, p99 at most 1, max 9; huge 17 read-backs, 99.23 % bit-equal, p99
at most 1, max 5 (the whole known part 95.97 % / 97.18 %: the 255s are in the edge band). So inside the window the
models read the mask the server builds for their frame, up to the bilinear smoothing of a resample (a channel
difference of at most 9 of 255).

**The strip** (MC #2175): frames that read a strip and how often it reaches the screen (every 8th strip texel at z = 0
through the frame's camera, an upper bound of "on a model"; 4eb455c5 / 0837124c): Tiny 237 / 233 frames, 45 / 45 of
them reach the screen with 2.8 % / 3.0 % of the strip's samples; huge 597 / 593 frames, 204 / 205 reach the screen,
12.5 % / 12.6 % of the samples (in 7,212 and 11,374 map frames, so 0.6 % and 1.8 % of all frames put some strip on
screen). The true look of the strip's texels
(the server's mask built for that window, x and z within 16) and which fill gives it:

| Map | true look: unexplored | seen | other (visible, edges) | zero / unseen fill right | seen fill right | **edge fill right** |
|---|---|---|---|---|---|---|
| Tiny t21 (early, mostly unexplored), 4eb455c5 | 87.9 % | 0.0 % | 12.0 % | 87.9 % | 0.0 % | **91.3 %** |
| Tiny t21, 0837124c | 86.6 % | 0.0 % | 13.4 % | 86.6 % | 0.0 % | **90.3 %** |
| huge t350 (late, mostly explored), 4eb455c5 | 28.9 % | 55.6 % | 15.5 % | 28.9 % | 55.6 % | **83.5 %** |
| huge t350, 0837124c | 29.7 % | 41.0 % | 29.3 % | 29.7 % | 41.0 % | **64.4 %** |

No constant is right on both maps (zero / unseen is right on Tiny, seen on the late map); the edge fill is right most
often on both, so it is the default (`FILL_DEFAULT`), and it never shows the visible look except where the plots beside
the strip are visible. Zero and unseen mean the unexplored look on models (the cloud base, trees discarded), never the
untinted visible look (the MC's concern).

**At rest, ACCEPTANCE pictures on 0837124c** (shipped settings, no key changed; Visual Checks under 0394, e.g.
`vc-20261116-032614-workstation-c7f2` Tiny cam3_p13, `vc-20261116-033650-workstation-23fd` huge p13_model; figures
`vc-20261116-034004-workstation-5f44`, `vc-20261116-034005-workstation-ff3a`), `models_0339.py` on model pixels against
3957's stock shot: Tiny sheep median 16 / p95 83, 50.5 % within 16, against the comparison arm (0339's behaviour, same
session) median 29 / p95 91, 35.1 % and 0339's own 3 / 47, 79.7 %: shots of the same setting differ this much (sheep
animation, the cloud phase), so no change is visible at rest; huge p13_model median 1 / p95 63, 68.3 % (comparison arm
0 / 61, 70.2 %; 0339 0 / 61, 67.8 %): unchanged.

**Not verified** (2026-11-16): a pan-frame picture against stock (stock's pan frame is not reproducible; the verify
compares the masks instead); trees (Forest off in D33); FOWLevel 0-1 (512 mask); the generated mips of a resampled mask
against the server's (mip 0 only compared); a machine without `d3dcompiler_47.dll` (the pass logs LOUD and leaves size
steps as built: by reading only); the edge fill's share on maps other than these two; the frame cost of the pass (no
timing, COMMON 23).

## 10. The FOWLevel-3 particles (FluidParticles) and the server's look-alike (ticket 0340)

Ticket 0340 (account B, branch `task-0340-fow-particles-look-alike`), 2026-11-16 from 04:00 EST. Static pass 04:05-04:23
EST: read-only headless Ghidra 12.1.4 on a copy of the `civ5` project (EXE SHA-256 `b2ce96ae...c88e`, build 4390913)
with 3950's helper `render/evidence/0168/3950/Dec3950.java` (driver `shell/evidence/0340/dec.sh`); the decompiler output
was kept in `C:/c5-64bit-work/0340/dec/out/` only and removed at the hand-back, never committed or archived (D83). The FOWParticleShader blobs were read from 2902's
disassembly dump (`D:/c5-64bit-work/2900/2902/dump/asm/067796f0_vs_5_0.asm`, `0677d4b0_ps_5_0.asm`). Install files read
from c5h-a's install (VT present): `EffectTextures.fpk:flowfield.dds`, `fx_fog_dif.dds`, `EffectData.fpk:
fluidfowsettings.xml` (copies in `C:/c5-64bit-work/0340/assets/`, game data, not committed). Everything below is
behaviour, addresses and constants. Marks as in the header.

### 10.1 What the EXE does (static) [C unless marked]

**Objects.** The FluidParticles object is embedded in FluidFOW_System at **`0xDA94D8 + 8`** (all particle offsets below
are relative to it; the system's own offsets are `sys+`). Start-up `0x44A570` loads `flowfield.dds` as a **CPU image**
(file stream, converted to the engine's 16-byte float4 format; stored at `sys+0x1340`) and `FX_Fog_dif.dds` as a GPU
texture (`sys+0x92C`), then calls the constructor `0x68F470(texture, flow image, simulate)`; if either file fails the
system sets FOWLevel to 2 (`sys+0x134C` = 2) and switches the simulation off (`sys+0x1300` = 0). The constructor builds
the FOWParticleShader (51) program object (`0x6730A0`), a dynamic vertex buffer of 40,960 vertices (10,240 sprites x 4,
stride 32) and an index buffer of 61,440 entries, the draw object (t0 = the sprite texture, t1 = the FOW mask
`0xDA12AC`), two job objects (`+0x898` the per-frame step, `+0x89C` the same step every 16,384th frame with an extra call
first), and, when simulating, one 24 MB particle block per job thread (`+0x40[i]`, 196,608 records of 128 B; count
`+0x440[i]`; thread count `0x81D870`, 8 on the workstation: the 8 x 24 MB of 3.5) and two 768 KB arrays (sort indices
`+0x864`, sort keys `+0x894`). The sprite texture is an **atlas of 64 x 64 frames**: `+0x8F0` = 64 / width, `+0x8F4` = 64 /
height, `+0x8F8` = frame count (64 for the 512 x 512 file), `+0x8FC` = frames per row (8).

**Constants and settings.** Smoothing length **h = 60** (`+0x868`), h^2 = 3600 (`+0x87C`), the poly6 constant 315 / (64 pi
h^9) (`+0x880`, unused by the step), **K = 15 / (pi h^6)** (`+0x884`, the spiky kernel's constant), iterations
`+0x840` (= `sys+0x1324` = 1 at every game start), the active-area margin `+0x844` (= `sys+0x1328` = **3 cells**, set at
start-up `0x44A68A`). From `Assets/Effects/FOW/FluidFOWSettings.xml` (per-game init `0x5B6AF0`, children found by name,
values parsed with a stringstream, so `.5f` reads as 0.5): SpawnFrequency (int, default 20), SizeMean / Min / Max /
Deviation (1, 0.25, 4, 1), MassMean / Min / Max / Deviation (1, 0.125, 8, 1), MassAndSizeCorrelation (0), FogHeight
(0), HeightVariation (60) [defaults C]; FluidDensity (50), Speed (5), Drag (0.001), MasterSize (1), ForceStrength (1e-5),
MaxRotate (0.01), copied into `+0x888`, `+0x870`, `+0x874`, `+0x878`, `+0x88C`, `+0x890` by `0x597F50`. A failed read
of one value stops the reading of the rest of its group (two groups: the spawn values, then FluidDensity..MaxRotate),
which keep their defaults. **The install's file** [L, c5h-a, 2026-11-16]: SpawnFrequency 22, SizeMean 2.5, SizeMin 1.0,
SizeMax 4.0, SizeDeviation 1.0, MassMean 1, MassMin 0.25, MassMax 4.0, MassDeviation 1, MassAndSizeCorrelation 0.5,
HeightVariation 50, FluidDensity 45, Speed 3.5, Drag 0.004, MasterSize 0.8, ForceStrength 0.000035, MaxRotate 0.0025,
FogHeight 11 (every value parses).

**The cell grid** (`0x729C40`, at each game start): cells of 64 world units (shift 6); the spawn box is x in
[-32 nx, 32 nx), nx = floor(W / 64) + 1, and y in [-32 ny, 32 ny), ny = floor(H / 64) + 21 (W, H the world box,
centred at 0: 3583 x 2216.094 on Tiny); the grid adds one cell on every side (origin = box min - 64). Per cell: an SoA
store of up to **8 groups of 4 particles** (2 KB), a staging store of 32 records (4 KB), and counts.

**Spawn** (`0x5B6AF0`, at each game start, FOWLevel 3 only): a lattice over the spawn box, x outer, y inner, step
SpawnFrequency; one particle per lattice point, each from the C runtime's `rand()` (no `srand` seen in this path; the
sequence continues whatever the process drew before):
- (u, v) uniform in (-1, 1) by `rand() / 32768 * 2 - 1`, redrawn until u^2 + v^2 < 1; s = u^2 + v^2; f = 0 when s <=
  1e-4, else sqrt(-2 ln s / s) (Marsaglia's polar method); with c = MassAndSizeCorrelation x 0.5:
  **size = ((1 - c) u + c v) f sqrt(SizeDeviation) + SizeMean**, **mass = ((1 - c) v + c u) f sqrt(MassDeviation) +
  MassMean**; redrawn (up to 10 tries in all, the last kept) while size is not in (SizeMin, SizeMax) or mass not in
  (MassMin, MassMax);
- **z = 0.2 (rand() / 32768 x HeightVariation + 0.198367) + FogHeight** (0.2 is the global height scale `0xB9A6DC`);
- **id = n x 256 + floor(rand() x 64 / 32767)** (n counts particles from 1; the low byte is the atlas frame, 64 only for
  rand() = 32767);
- velocity (0, 0), density and rotation 0; then all particles are binned into their cells (`0x5BFC50`).

**The step** (one per drawn frame: `0x5B57F0`, when `sys+0x1300` is set, hands the view's footprint min / max
(`0xDA1B00`, `0xDA1B30`, view 0) to `0x76E460`, which starts the step job; the particle draw of the next frame waits for
it). Step `0x509990`:
1. The **active rectangle**: the cells of footprint min and max, widened by the margin (3 cells) and clamped to [margin,
   cells - margin]. Cells outside it are not touched (their particles stand still); cells on the rectangle's outer ring
   are copied unchanged; inside, particles move.
2. Density pass (`0x4CE970` per group): for each particle i, rho_i = sum over the particles j of the 3 x 3 cells around
   it (2D distance r in x, y; i itself included) of **m_j K max(0, h - r)^3**; then p_i = m_i / (2 FluidDensity rho_i).
3. Force and move (`0x6B9F60` inside, `0x781A20` on the ring): F_i = sum_j **3 K (h - r)^2 / max(r, 0.001) (x_i - x_j)
   (rho_i + rho_j) p_j** over r <= h (a repulsion); the flow field's texel at **ix = (int(x) + 512) mod 1024, iy = (512 -
   int(y)) mod 1024** (unsigned arithmetic; one texel per world unit, the 1024 x 1024 field tiles every 1024 units)
   gives (fx, fy) = its first two float channels; then **v' = v + Speed (F / rho_i + ForceStrength (fx, fy)) - Drag v**
   and **x' = x + Speed v'** (Speed / iterations with one iteration; Drag applies on the last iteration only). x' and y'
   wrap inside the active rectangle's inner box [(c0 + 1) 64, (c1 - 1) 64) (periodic), so particles circulate within
   the area around the view. z, mass, size, rotation and id are unchanged.
4. Rebin (`0x4D07D0`): every moved record goes into its new cell's staging (at most 32 per cell; more are not kept
   [I: the bin call is given 32 as its capacity]), the staging is converted to the SoA groups, and the map's
   wrap edges are copied as ghost cells (`0x587700`).

**The draw** (`0x59F8C0`, from `0x602B10` after the cloud planes, FOWLevel 3 only, target begin "FOW Particles"):
- Per view, the cells of a second rectangle of the view (`0xDA1B60` / `0xDA1B90`, x [c(min), c(max) + 1), y [c(min),
  c(max) + 2), clamped) [I: the same footprint as above or a wider one; not traced]; per non-empty particle, in cell
  order, at most **10,240 sprites** over all views.
- Per sprite (`0x5149B0`): the target angle = atan2 of the normalised velocity in [0, 2 pi); the stored rotation moves
  toward it by at most **MaxRotate per drawn frame** (the shorter way round) and is written back; half extent **e = size
  x MasterSize x 16**; a flat square in the horizontal plane at the particle's z: with u = (cos a, sin a), w = (-sin a,
  cos a): v0 = c - e u + e w, v1 = c - e u - e w, v2 = c + e u - e w, v3 = c + e u + e w, uv (u0, v0), (u0, v1), (u1,
  v1), (u1, v0) of the atlas frame (col = frame mod 8, row = frame / 8, 1/8 each); triangles (0, 1, 2), (0, 2, 3);
  NORMAL = 0.66 x the corner in another space (unused by the VS); the sort key = the centre's clip w.
- The sprites are **sorted far to near** by the key (`qsort_s`, `0x4CD540`: descending), written to the vertex buffer and
  drawn with **FOWParticleShader pass 0**, one draw per view: own buffer 0x90 B = M (rows 0-3; the VS uses its x, y and
  w columns: position = (p M).xyw, **z forced to w / 1024**, i.e. NDC depth 0.000977), OverlayProj[v] (rows 4-7; the
  mask uv = ndc (0.5, -0.5) + 0.5), row 8 = (1, 1, 1, 1). PS: m = mask.y; discard where m < 0.01; colour = texture
  rgb; alpha = m x texture alpha, discard where < 0.01. Blend and depth as the cloud planes (2.3): SRC_ALPHA /
  INV_SRC_ALPHA, LESS_EQUAL without write, so **FOW CULL's depth 0 hides the particles where no unexplored plot is
  within two plots**, and the mask gate fades them out over the unexplored band's edge.

**Not traced** (left as [I] in the look-alike): the exact pixel conversion of `flowfield.dds` (an A8R8G8B8 file:
R mean 78.6, G mean 80.0, B 0, A 255 over 1024 x 1024 [L]) into the float4 image (`0x8CC400`; taken as unorm / 255,
channel order R, G, so the field only pushes toward +x and +y); the rectangle at `0xDA1B60` / `0xDA1B90`; the extra call
of the 16,384th frame (`0x4CBE40` returns 0x1F1); the bin's overflow rule. **`M` is the view's ViewProj [L]**: in 3951's
capture cam1 t21b the particle draw's own buffer (`D:/c5-64bit-work/0168/3955/cb/cam1_t21b/cb0_389.bin`, 256 B) holds
rows 0-3 equal to the ViewProj solved from the same frame's cloud planes to 6 digits (2.13511, 2.73275 / 0.716058 /
0.707107, ..., 2461.38 / -790.283 / 349.886 / 395.512), rows 4-7 OverlayProj, row 8 (1, 1, 1, 1).

### 10.2 The server's particles as built (ticket 0340) [C code + tests]

**`c5r::fow::ParticleSim`** (`render/src/fow/particles.cpp`, interface in `fow.h`): 10.1's behaviour, in one thread:
`reset(worldWidth, worldHeight, settings, flow, seed)` (the grid and the lattice spawn, the generator = the C runtime's
`rand()` formula seeded with `seed`), `step(footprint min, max)` (the active rectangle, density, force and move with the
flow field, the wrap in the inner box, the rebin with 32 a cell), `emit(viewProj, footprint, atlas)` (the cells of the
footprint, the rotation step, the four corners and uv, the clip-w key; at most 10,240), `sortSprites` (descending key,
stable), `saveState` / `loadState` (byte-exact, "C5FP" version 1; the flow field is the install's and is checked by size
and hash, not stored). Cells are vectors of `Particle` (44 B); memory: about 44 B per particle (the huge map's 128 x 80
plots: spawn box 8,256 x 5,760, about 98,000 particles at SpawnFrequency 22, about 4.3 MB) against stock's 8 x 24 MB
blocks and its 2 KB + 4 KB a cell [I: not measured live yet]. Differences from 10.1, named: one thread (stock's jobs
split the cells; per-thread staging order is not reproducible anyway); no ghost cells at the grid's edges (the
neighbour query does not wrap: the edges are the spawn box's, outside the map); the draw's rectangle is view 0's footprint
(as the step's), `0xDA1B60` / `0xDA1B90` not traced; the rasterizer's cull mode is NONE (stock's not read); the seed is 1
unless `C5_RENDER_FOW_PARTICLE_SEED` says otherwise (stock's `rand()` state depends on everything the process drew
before; not reproducible). **`ParticleRenderer`**: `init(dev, exe, gameDir)` reads FOWParticleShader pass 0 (VS
`0x067796F0`, PS `0x0677D4B0`) from the EXE and `fx_fog_dif.dds`, `flowfield.dds`, `fluidfowsettings.xml` through
`c5r::gamefs` (EffectTextures.fpk, EffectData.fpk); `frame()` = emit for every view, sort, one draw per view (the view's
ViewProj and OverlayProj in the 0x90 B buffer, t0 the atlas, t1 the mask, linear wrap samplers, SRC_ALPHA /
INV_SRC_ALPHA, LESS_EQUAL without depth write, 16-bit indices (0, 1, 2), (0, 2, 3)), then one step with view 0's
footprint for the next frame, **on a worker thread** (`std::async`; added 2026-11-16 06:58 after the 30-turn control
arm, 10.3): the next `frame()` (or any access to the state through `sim()`, `reset()`) waits for it first, as stock's draw
waits for its step job; the order of steps and draws is unchanged, so the particles stay repeatable (the tests' byte
equality holds).

**In server.exe** (`srv_fow_scene.cpp`): at FOWLevel 3 `FowScene::init` starts the particle module after the mask and
cloud modules (`fog: FOWLevel 3 (...) particles on (seed 1)`); `afterWorld` draws them after the cloud planes on the
cloud pass's depth copy (FOW CULL's zeros hide them where stock's do; the scene's own depth is untouched). Spawned at the
first particle frame and again when the world box changes (`fog: particles spawned for a W x H world: N particles`); a
new game on a map of the same size keeps the old particles [I: stock spawns per game; the scene has no game-start
event]. One step per drawn frame: the scene draws a frame per Present while the overlay pair changes (30-31 a second
live, 7.3), so the particles move at the frame rate as stock's do. A particle module that cannot start (a file missing) is
LOUD once and the fog is drawn as at FOWLevel 2, as stock does when its particle files are missing (10.1): not an install
failure (the cloud planes and the mask still hide the map). Per second: ` particles=<sprites>/<moved>/<alive> lost=<n>
cpu=<ms> gpu=<ms>` on the `fog 1s` line (CPU ms of emit, sort, upload and step; GPU ms of the draw from timestamp
queries read four frames later); totals `particles (0340): on|off frames resets errors lost`. Switches (test and tool,
not player settings): `C5_RENDER_FOW_PARTICLES=0` (off), `C5_RENDER_FOW_PARTICLE_SEED=<n>`, `C5_FOW_PARTICLE_DUMP=<folder>`
(+ `C5_FOW_PARTICLE_DUMP_FRAMES`, default 600; the statistics records of 10.3, written only while `<folder>\on` exists). FOWLevel 0-2 and `render_fow=0` draw
exactly what they drew before (test `particles`: FOWLevel 3 with the particles off = FOWLevel 2, byte-equal).

**The offline replay (epic 0301) [C]:** the particles carry history (every drawn frame since the map's spawn), so a
recorded span that starts mid-game cannot be replayed 1:1 without their state at its first frame. That needs a bundle
member (`particles.bin` = `ParticleSim::saveState()` at the span's first frame, about 190 KB on a 1000 x 900 test world;
the huge map's whole state about 4.3 MB) and its recorder and player wiring: ticket 0403 (MC, 2026-11-16, answer on
0340's thread; `srv_render_host.cpp` is held by epic 0375). Until then the player turns the particles off when it seeds
the fog's clock (`FowScene::setClockState`, `fog: the offline player seeded the clock: particles off`), so its replay of
a bundle stays 1:1 with a recording made with the particles off (`C5_RENDER_FOW_PARTICLES=0` in the recording server's
environment, until 0403 adds the `render_fow_particles` key to the bundle's `render_config`). (Corrected 2026-11-16 by
ticket 0403: the member exists from bundle format version 6; the recorder writes `particles.bin` and
`render_fow_particles`, the player turns the particles back on from it with `FowScene::setParticleState`, and the replay
is byte-equal with the particles drawing: `research/06-tooling-and-workflows/offline-scene-harness.md` section 23. The
sentences above describe format 5 and earlier.) The state itself is
byte-exact: `particles_test` loads the state of step 10 into a fresh simulation and both go on byte-equal for 10 steps
and an emit; `particles_game_test` draws byte-equal pictures from two renderers with one seed over 30 frames.

**Tests** (2026-11-16, workstation; C5R_EXE = the Ghidra copy of the EXE, C5R_GAME_DIR = c5h-a's install, VT present):
`render/tests/fow/particles_test.cpp` (no game files: settings reader, flow field, `rand()` (41, 18467, 6334 for seed
1), spawn (4,277 particles on a 1000 x 700 world = the lattice, sizes 1.000-3.997, z 11.04-21.04), step (841 moved, the
far cells unchanged, the inner box held, repeatable), emit (552 sprites = the footprint's cells, rotation within
MaxRotate), the state round trip, negatives) and `particles_game_test.cpp` (the EXE's program and the install's files:
every setting of the install's file read; 4,794 particles on a 1000 x 900 world, 2,162 sprites in the first frame; the
gate: where G = 1, 10,208 of 10,208 pixels changed, where G = 2/255 or 0, none; with depth 0, none of 65,536; 30 frames
byte-equal; an install without the files: NotFound): `c5r_fow` plain 6 of 6 passed, MSVC `/fsanitize=address` 6 of 6
passed, 0 skipped, 0 ASan reports; the server's fog scene (`test_fow_scene`, 18 cases in 3 pytest tests) plain 3 passed,
ASan 3 passed, 0 ASan reports (06:13-06:28 on the synchronous step; again 08:30-08:42 on the hand-back's code with the
step on its own thread: render plain 6/6 (07:45), ASan 6/6, server plain 3/3, ASan 3/3, 0 ASan reports). `server/tests/render/fow/test_fow_scene.cpp` cases `particles` (real modules: on, one spawn, 8 frames, no LOUD,
the scene's depth untouched, pictures differ from particles off, two runs byte-equal, particles off = FOWLevel 2, the
player's seed turns them off) and `particles_fail` (fakes: none provided = no LOUD; a failed start = LOUD once, drawn as
FOWLevel 2, never fatal).

### 10.3 Live acceptance (statistics against stock) [L, 2026-11-16 05:13-05:38 EST; tolerances stated before the run]

Measured alike in both arms by `shell/evidence/0340/pstats.py` from per-frame records of the particles of the cells view 0
draws: the stock EXE's read from its memory (`stockpart.py`, read-only), the server's from `C5_FOW_PARTICLE_DUMP`.
Tolerances, stated 2026-11-16 04:55 EST before any live run (the server against stock at the same camera, means over at
least 300 steps): **sprite count within +-25 %**; **gated coverage** (the share of the footprint's ground over unexplored
plots that the sprites cover with alpha >= 0.01) **within +-0.10 absolute**; **mean alpha** over that ground **within
+-0.10 absolute**; **drift per step** (matched ids) **within a factor of 2**; steps per second reported (stock steps once
per drawn frame as the server does; both at the instance's frame cap). World coverage of the ground footprint stands in
for screen coverage (the stock reader has no camera matrix; both arms are measured alike).

**Setup.** c5h-b (Goldberg, C:, 2554 x 1411, VT present: the Visual Checks entries name the asset set, D94),
GraphicsSettingsDX11.ini FOWLevel 3 (the instance's own, the owner's setting), backed up by SHA-256 and restored by hash after
every arm (`backup verify rc 0`). Save: copies of the instance's `C5H-VP8P-HUGE_0350` (C499D3C6...), turn 350, active
team 0, 9,808 plots revealed, 774 visible. Cameras: `v2_city_zoomin` (100, 30, 4 wheel ticks: zoom 11.0) then
**fog_edge** (111, 4) (0168's C7 camera) and **fog_edge2** (88, 3) (the next cluster of the same census rule: the huge
save's other C7 cameras show no unexplored plot, 8.5), popups checked first (D114), cursor parked. Stock arm (`st0340.py`,
05:13-05:21): 32-bit pin `i0ship-26234371-ia`, no proxy, no server; `stockpart.py` read 25 s at 20 Hz per camera (490
and 475 samples, 750 and 748 steps). Server arms (`run_0340.py`, the joined product layout of commit b788c2cd: server.exe
cbe37005 release, proxy e6d014da, composite 081605e8, x64 DLL 74588135 (pin x64-04b1211b), D33 A'' as shipped):
**stats** (05:21-05:30, `C5_FOW_PARTICLE_DUMP`, one 25-s window a fog camera: 750 records each, DIAGNOSTIC pictures) and
**ship** (05:30-05:38, no switch: ACCEPTANCE pictures). Analysis `analyse.sh` / `pstats.py`; records, results, logs and
pictures archived in `F:/c5-64bit-work/0340/` (`live/`, `captures/`; 198 files checked by SHA-256; the C: folder removed).

**Found on the way [L]:** stock's draw rectangle `0xDA1B60` / `0xDA1B90` **equals view 0's footprint** `0xDA1B00` /
`0xDA1B30` in every sample at both cameras (`draw_rect_equals_footprint: true`), as the server assumes; stock's grid on
the huge map: origin (-4160, -3040), 130 x 95 cells (nx = 128, ny = 93: 10.1's rule [L]); stock steps 30.0 times a
second at the instance's frame cap, one per drawn frame [L]; the server spawned **101,083 particles** for the 8191 x 4655
world in about 60 ms (render.log 05:34:41.000 -> .064) with 0 lost to the cell cap.

| Camera | Arm | Sprites | Gated coverage | Mean alpha (gated) | Drift per step (matched ids) | Speed per step |v| Speed | Steps / s | Drift units / s |
|---|---|---|---|---|---|---|---|---|
| fog_edge | stock | 613.5 +- 5.4 | 1.000 | 0.667 +- 0.005 | 0.0432 +- 0.0064 | 0.0427 | 30.0 | 1.30 |
| fog_edge | server | 605.7 +- 3.7 (**-1.3 %**) | 1.000 (**0**) | 0.717 +- 0.004 (**+0.050**) | 0.0446 +- 0.0029 (**x 1.03**) | 0.0446 | 30.0 | 1.34 |
| fog_edge2 | stock | 547.1 +- 2.2 | 1.000 | 0.750 +- 0.002 | 0.0387 +- 0.0048 | 0.0384 | 30.0 | 1.16 |
| fog_edge2 | server | 546.6 +- 2.1 (**-0.1 %**) | 1.000 (**0**) | 0.764 +- 0.001 (**+0.013**) | 0.0386 +- 0.0013 (**x 1.00**) | 0.0386 | 30.0 | 1.16 |

**Every figure is within its tolerance** (sprites +-25 %, coverage and mean alpha +-0.10, drift x 0.5-2). Coverage is 1.0 in
both arms at both cameras (the sprites, about 50 units across at size 2.5 x MasterSize 0.8 x 16, overlap everywhere over
the unexplored ground at zoom 11): it does not separate the arms; mean alpha does. The server's mean alpha is higher by
0.05 / 0.01 [I: its rotation and atlas frames start from its own seed; stock's particles have moved for 3,554 steps more
by the first sample and its rand() state differs]. Pictures: ACCEPTANCE (ship) against the stock shots at the same
camera, map area x 180-2350, y 40-980, largest channel difference: fog_edge **median 8, p95 27, max 80, 81.2 % within
16**; fog_edge2 **median 9, p95 29, max 77, 76.3 % within 16** (Visual Checks `vc-20261116-053652-workstation-58f0`,
`-053712-...-10f2` ACCEPTANCE; difference figures `vc-20261116-054045-workstation-5f35`, `-054047-...-dacd`; stock
`vc-20261116-051930-workstation-a04d`, `vc-20261116-052012-workstation-bd30`); the server's own shot-to-shot between the two launches
(stats against ship, fog_edge): median 4, p95 18, max 79, 93.1 % within 16. For scale: 3957's fog_edge unexplored class
before the particles was median 13 / p95 28 / max 158, 63.4 % within 16 (8.5; a per-class mask, not this box). By eye the
two pictures show the same cloud bank over the polar plots; the remaining difference is the phase of the clouds
(FOWAnim's T) and of the particles.

**Indications (COMMON 23, the shared workstation, not timing results):** the particle frame's CPU time (emit, sort,
upload, step, one thread) 1.1-2.3 ms in the `fog 1s` lines at the fog cameras (stats arm 1.6-2.3 with the dump on, ship
arm 1.1-2.3); the draw's GPU time 0.04-0.13 ms (timestamp queries, RTX 4080). Memory: the module's own about 4.5 MB of
particles (101,083 x 44 B) plus 12,350 cells' vectors, and 1.8 MB of video memory (vertex buffer 1.31 MB, indices 0.12
MB, atlas 0.35 MB) [C sizes]; server.exe at the end of the ship arm private 1,775 MB, video_local 388 MB (`mem periodic`;
no particles-off arm on this build for a difference). Stock's FluidFOW at FOWLevel 3 costs the EXE +283 MB (8.3); the
EXE under D33 here: private 1,473 MB at the end of the ship arm. Log lines: render.log, server.log, vpproxy.log 0 LOUD
in both server arms; the composite logged one LOUD in the stats arm at 05:25:17 (`fow_models: the server's mask is stale
(its heartbeat is still)`, during the first frames after the load, 3-s frame times, the dump's arm) and none in the ship
arm [I: the load's frame stall, not the particles: the ship arm with the same particles had none].


**30 AI turns on the huge save** [L, 2026-11-16 05:38-06:05 EST, c5h-b, the same build and save, the shipped settings
(arm `turns`: `run_0340.py run --max-turns 30`, 3308's turns, the player ends each turn), restored by hash]: rc 0, end
"max turns 30" (350 -> 379), **0 EXE crashes, 0 server crashes, no "Game Crash" box, no dialog**; LOUD lines render.log
0, server.log 0, vpproxy.log 0, **shell_composite.log 2**: `fow_models: the server's mask is stale (its heartbeat is
still)` at 06:00:20 and 06:03:22, each 1-3 s after a decal generation was applied in the server (render.log
`decals: generation 210 applied` 06:00:18.48 -> `set generation 210 ... in use` 06:00:19.70, the fog's 1-s line
06:00:18.16 -> 06:00:20.45: a stall of the server's render thread of about 1.3 s; generation 233 at 06:03:19.17 ->
06:03:21.72 before the second). Fog states ACTIVE 1,315 lines, WAITING 108 (the load), FAILED / HELD 0; gray frames
after the bake-ahead 0 in 1,423 lines; particles 101,083 alive, 1 lost to the cell cap in 30 turns, sprites about 1,200 at
the city camera (CPU 3.4-3.5 ms, GPU 0.05 ms a particle frame there); EXE private 1,460 MB after the load, 1,626 MB at the
last turn; server 1,645 / 1,795 MB; turn wall time median 43.1 s, max 52.1 s (indication); lockstep exact 46,797, older
35,379, none 0, give-ups 1,035 = recoveries 1,034 (+1 at the end).

**The control arm without particles** (`turnsoff`, 06:28-06:56, the same build, save and turns, `C5_RENDER_FOW_PARTICLES=0`
TEST ONLY) [L]: rc 0, 30 turns, 0 crashes; **LOUD 0 in all four logs** (no stale-mask line although decal generations
were applied as in the particle arm); lockstep exact 74,202, **older 4,944, give-ups 182** = recoveries 182; turn median
44.4 s, max 49.6 s; server 1,588 / 1,715 MB (after the load / last turn), EXE 1,452 / 1,624 MB. *Corrected 2026-11-16
06:58: the particle arm's two stale-mask LOUD lines and its 7x older-frame lockstep (35,379 against 4,944) come with the
particles: the step ran on the render thread (3.5 ms a frame at the city camera), so frames missed the composite's
lockstep bound and a decal apply's stall then reached the mask's heartbeat limit. Replaced: "[I: the decal apply's stall,
not the particles: 0394's huge run without particles logged the same line ...]" (0394's line exists, but this build's
control has none).* Fix: the step runs on a worker thread between two drawn frames, as stock's step job does (10.2); the
30 turns were run again with it (below).

**30 turns again, the step on a worker thread** (`std::async`, server 958af0b9, 07:05-07:35) [L]: rc 0, 0 crashes, LOUD
0 / 0 / 0 in render, server and proxy logs, **2 in the composite** (the same stale-mask line at 07:30:51 and 07:31:04);
the particle frame's render-thread CPU **0.6 ms** at the city camera (was 3.4-3.5), GPU 0.05 ms; lockstep exact 68,637,
**older 7,956, give-ups 223** = recoveries 223 (control 4,944 / 182; synchronous 35,379 / 1,035); turn median 46.7 s, max
54.4 s; server 1,630 / 1,820 MB, video_local 818 MB at the end. Render-thread gaps (fog 1-s lines more than 1.8 s apart):
control one (3.0 s); synchronous three (4.1, 2.3, 7.8 s); worker thread nine (1.9-3.9 s and **13.6 s** at 07:30:48 ->
07:31:02, right after decal generation 216 was applied, the composite's two LOUD lines at its end). The gaps follow decal
applies in every arm and their length varies with the machine's load [I: one run per arm on the shared workstation; the
13.6-s stall is not explained]. The step was then moved to a thread of its own, started once (no task per frame through
the C runtime's pool); the turns were run once more (below).

**30 turns, the step on its own thread** (server 7000001a of commit 379742bb, 07:58-08:28) [L]: rc 0, 0 crashes, no box,
LOUD 0 / 0 / 0 in render, server and proxy logs, **3 in the composite** (the stale-mask line at 08:03:03, 08:05:04,
08:10:51); render-thread CPU of a particle frame **0.57 ms**, GPU 0.05 ms; lockstep exact 30,001, **older 46,391, give-ups
1,980** (recoveries 1,975); render-thread gaps 4.7, 23.0 (08:03:00 -> 08:03:23, the first minutes after the load), 4.8,
1.9, 2.4, 2.0, 2.3, 2.3 s; turn median 45.4 s; server 1,488 / 1,972 MB.

**What the four 30-turn runs say, and what they do not** (one run per arm on the shared workstation, other teams' games
and suites running; COMMON 23: indications) [L]:

| Arm (time EST) | Render-thread CPU of a particle frame | Older-frame lockstep | Give-ups | Composite stale-mask LOUD |
|---|---|---|---|---|
| particles, step on the render thread (05:38-06:05) | 3.4-3.5 ms | 35,379 | 1,035 | 2 |
| particles off, control (06:28-06:56) | - | 4,944 | 182 | 0 |
| particles, `std::async` step (07:05-07:35) | 0.6 ms | 7,956 | 223 | 2 |
| particles, own thread (07:58-08:28) | 0.57 ms | 46,391 | 1,980 | 3 |

The older-frame counts swing by 6x between the two runs whose render-thread cost is the same (0.6 ms), so they follow the
machine's load more than the particles; the render thread's own particle cost fell from 3.5 to 0.6 ms [L]. **The
stale-mask LOUD lines came in all three particle runs and not in the one control run: the particles are not cleared of
them** (the worker thread takes CPU beside the render thread: about 3 ms a frame [I from the synchronous cost]). Open: an
interleaved series particles on / off (at least three pairs) in a quiet window or on the testbed to attribute the lines
and the lockstep; the composite's stale limit against the server's decal-apply stalls (seen in every arm) is the other
candidate. Stock's own particle step costs it 8 job threads (10.1).

## 11. The cloud planes at the south and north map edges (ticket 0572, 2026-11-18)

Ticket 0572 (account B, branch `task-0572-cloud-seams-south-edge`). Found by epic 0398's HI acceptance phase on i2-rc1
(#5917, camera-selection runner step 7: DOWN held 3 s at the south edge of `HI-KIT-MEDIEVAL-LARGE-T200`, 1914 x 1051,
FOWLevel 3, compositor on, c5h-f): the product shows two straight horizontal cloud seams (about y 385 and 405 of 703)
and a hard cloud-to-black line (about y 455) with black below, on both product runs (Visual Checks
`vc-20261118-193725-workstation-d3ac`, `vc-20261118-195324-workstation-90f6`); stock on c5h-a shows continuous clouds
to the bottom of the screen (`...9ece`). Pictures: `F:/c5-64bit-work/0398/4609/cam-f-i/product-{1,2}/step_0007.png`,
stock `F:/c5-64bit-work/0398/phase/i2-rc1/runs/camera-selection/pipe-h/stock-1/step_0007.png`.

### 11.1 Root cause [C]

**Stock's plane draw** (`0x517E10`, called three times from `0x602B10` after FOW CULL, one call a plane; read
2026-11-18 20:00-20:10 EST with read-only headless Ghidra 12.1.4 on a copy of the `civ5` project, EXE SHA-256
`b2ce96ae...c88e`, 3950's `Dec3950.java`; output kept on C: only and removed at the hand-back, D83). Per view v (stride
0xC in the footprint arrays, 0x40 in OverlayProj):
- the four corners of the plane rectangle: **x min = `0xDA1B00`[v].x, x max = `0xDA1B30`[v].x** (the footprint
  clamped to the world box, water-system.md 10.2 step 1) and **y min = `0xDA1B60`[v].y (`0xDA1B64`), y max =
  `0xDA1B90`[v].y (`0xDA1B94`)** (the same footprint **before** the clamp);
- each to a sample coordinate ((c - world min) / HexScale, in float), rounded half away from zero (`< 0.0f` at `0xA0B4EC`:
  ceil(c - 0.5), else floor(c + 0.5), 0.5 at `0x9FCF90`), then divided by 64 toward zero (`(n + (n >> 31 & 63)) >> 6`);
- blocks **[bx0, bx1 + 1) x [by0, by1 + 2)**, **no clamp of any index** (by0 is negative when the view sees south of the
  world box, by1 + 2 past the box's last row when it sees north of it);
- per block a quad Scale(32 sx, 32 sy) x Translate(float(bx 64) sx + 32 sx + minX, float(by 64) sy + 32 sy + minY, h)
  (32 at `0xA3B900`; sx, sy = 1, 0.866 at `0xB9A6D4` [C, read]); bx outer, by inner; **at most 2,048 quads a view**
  (`< 0x800`; the rest are skipped, the loop goes on), 24 a draw.

**The server's rule before the fix** (`c5r::fow::planeBlocks`, `render/src/fow/cloud.cpp`, 3955): the ocean's rule
(water-system.md 10.2: x and y from the **clamped** footprint, by0 clamped at 0) with by1 + 2, and no quad cap. At a
mid-map camera both rules give the same blocks (3955's two cameras: the clamp did not bite), so 7.2's replay could not
see the difference.

**So at the south edge** the server's three planes end at the world box's south side (world min y, about 2 plot rows
south of the map's last row [I: the field's grid is 4 rows taller than the map, 7.3, taken as 2 on each side]), while stock's go on to the bottom of
the screen. The planes stand at 11.2, 38.25 and 51 world units; on the pitched camera the same world y at a greater
height projects higher on the screen, so their three ends are three horizontal lines: the 51 plane's end and the 38.25
plane's end are the two seams (the cloud above each line is one plane thicker), the 11.2 plane's end is the
cloud-to-black line. Below it nothing of the server's is drawn but the FOWLevel-3 particles (the cloud puffs in the
black: they are spawned over a box 21 cells taller than the map, 10.1, and gated by mask.y, which is 1 there, so the
mask is not the cause); the black is the server's clear (0, 0, 0, 0), transparent outside the terrain [C,
`srv_render_scene.cpp`] [I that the composite shows it black; stock clears its scene to (84, 84, 84), 2.3]. Not the
cause: the cloud base t5 (a terrain input; there is no terrain past the box), the mask window's 64-unit steps or the
strip's edge fill (9.5: the model fog only), the EXE's own draw (D33: the EXE draws no world).

**The north edge**: the same clamp cut the planes at the box's north side + 2 rows; a view wholly north of the box drew
no plane at all (the clamped footprint's min y above its max: blocks [45, 41) in the test below). **The east and west
edges**: stock takes x from the clamped footprint too, so on a map that does not wrap both stop the planes at the world
box in x: **no difference there** [C]. On a wrapping map each view (the wrap view included) has its own footprint, x
clamped in both.

### 11.2 The fix and its test [C, 2026-11-18]

`planeBlocks` takes y from the footprint without the clamp (`water::viewFootprint` with the world box opened in y; x
keeps the clamped one), clamps no block index, keeps by1 + 2, and limits a sample coordinate to +-1e9 before the int
conversion (a near-horizontal view; stock's x87 store would give 0x80000000 there); the plane loop keeps at most
**2,048 quads a view and plane** in bx-outer order (`kPlaneCap`). Files: `render/src/fow/cloud.cpp`,
`render/include/c5r/fow/fow.h` (comments), `render/tests/fow/cloud_test.cpp`.

Test `c5r_fow_cloud` (`testPlaneBlocks`, no game files; 3951's cam1 moved by 25 block rows, T(0, +-25 x 64 x 0.866) x VP,
on the Tiny world): cam1 [4, 16) x [20, 32) and cam2 [5, 13) x [23, 32) unchanged (the captured blocks); **south edge
[4, 16) x [-4, 7)**, **north edge [4, 16) x [45, 57)** (the box's top row + 2 = 41), east edge (cam1 moved 0.8 of the
world width in x) [48, 56) (box right + 1 = 56: x still clamped). **Against the code before the fix** (commit
68beeb454's `cloud.cpp`, the same test): exit 1, 4 failures: south [4, 16) x [0, 7), north [4, 16) x [45, 41) (no
plane). With the fix: `c5r_fow_*` 6 of 6 passed, 0 skipped (`render/build.py --test -R c5r_fow_`, C5R_EXE and
C5R_GAME_DIR the defaults of `deps/local-settings.ini`); under MSVC `/fsanitize=address` 6 of 6, 0 ASan reports; the
server's fog scene (`run_all.py --only fow-scene`) plain 3 passed, ASan 3 passed, 0 skipped, 0 ASan reports
(`suites_0572.sh`, 2026-11-18 22:13-22:43).

Offline replay: no bundle of the scene library holds a camera at a map edge (2026-11-18), and a named camera cannot
move the overlay pair's OverlayProj, which is the mask's window (7.3): a south-edge `.vpcam` added to an existing
bundle would draw the planes over a mask built for the recorded window, so the offline picture would not test the
fix. Live pictures: 11.3; an edge bundle recorded for the offline replay: 11.4.

### 11.3 Live: the defect, the fix and stock at the same cameras [L, 2026-11-18 21:01-21:29 EST]

**Setup.** c5h-a (Goldberg, C:, 2554 x 1411, VT present, FOWLevel 3 from the instance's GraphicsSettingsDX11.ini),
claimed by impl-0572; save copies of the instance's `C5H-VP8P-HUGE_0350` (C499D3C6..., turn 350). Cameras: **south_edge**
= `UI.LookAt(Map.GetPlot(111, 0), 0)` (below 0168's fog_edge (111, 4)), **north_edge** = `UI.LookAt(Map.GetPlot(111,
79), 0)`, no wheel tick (zoom 14.2556 in every arm), cursor parked, popups checked first (D114: none open). Drivers
`server/evidence/0572/` (`st0572.py` on 3957's stock driver; `run_0572.py` / `arms_0572.sh` on 0340's / 3957's joined
driver; `picdiff_0572.py`). Builds (`build_0572.sh`, `build_before_0572.sh`, `steps_0572.sh`): the pin layout
`x64-c29442e8-p58724e0`'s bin (x64 DLL 2B56A1CD, COMMON 18) with the arm's server.exe and proxy, one composite
(395D2040) for both arms, D33 in the shipped A'' form with Overlay on, the fog as shipped.
- **stock** (21:03-21:08): the 32-bit pin `i0ship-e96457fd-ia` (3EAA3290) in the CP slot, no proxy, no server.
- **fixed** (21:16-21:22): server.exe C7B94496 / proxy 70E30807 of commit bfe552206 (the fix). ACCEPTANCE pictures.
- **before** (21:23-21:29): server.exe 4EB03619 / proxy 5E6526D6 of commit 68beeb454 (the fix's parent, built the
  same way with the same deps): one factor between the two server arms, the fix. DIAGNOSTIC pictures.

Largest channel difference against the stock shot at the same camera (`picdiff_0572.py`; boxes of the 2554 x 1411
shot without the interface: **map** x 180-2350, y 40-980; **south** x 560-2180, y 700-1400, the lower part of the screen,
which shows the ground past the world box at south_edge; near-black = every channel <= 24):

| Camera, arm | Map box median / p95 / max, within 16 | South box median / p95 / max, within 16 | Near-black in the south box (stock) |
|---|---|---|---|
| south_edge, before | 10 / 25 / 59, 78.1 % | **33 / 219 / 237, 33.4 %** | **34.6 %** (0.01 %) |
| south_edge, **fixed** | 10 / 25 / 56, 78.1 % | **11 / 27 / 62, 72.1 %** | **0.01 %** (0.01 %) |
| north_edge, before | 7 / 32 / 174, 79.2 % | 0 / 10 / 107, 97.0 % | 0.01 % (0.01 %) |
| north_edge, **fixed** | 6 / 30 / 101, 80.0 % | 0 / 8 / 104, 97.8 % | 0.01 % (0.01 %) |

- **The flip [L]:** before, the south-edge picture shows exactly the HI phase's defect at this camera (two straight
  seams at about y 945 and 990, the cloud-to-black line at about y 1095 of 1411, black below with particle puffs);
  fixed, the clouds go on to the bottom of the screen as stock's do, and the south box falls from p95 219 to 27. At the
  north edge, before shows a straight seam near the top of the map area and a darker band above it (the planes cut at
  the box's north side + 2 rows); fixed shows neither (p95 32 -> 30, max 174 -> 101).
- **What is left** is the size of 0340's ACCEPTANCE figures at fog cameras (median 8-9, p95 27-29, 10.3): the clouds'
  phase (FOWAnim's T) and the particles, not the planes' extent. The black under the server's clouds past the world
  box (its clear is (0, 0, 0, 0); stock's scene clear is (84, 84, 84), 2.3) does not show at this camera: three planes
  over unexplored ground are opaque enough that the near-black share equals stock's [L]; a camera where the mask fades
  the planes out past the box was not seen.
- Visual Checks: stock `vc-20261118-210715-workstation-a5cc` (south), `vc-20261118-210734-workstation-581a` (north);
  ACCEPTANCE (pass) `vc-20261118-212105-workstation-6439` (south), `vc-20261118-212126-workstation-fd8c` (north);
  before (fail, DIAGNOSTIC) `vc-20261118-212754-workstation-e860`, `vc-20261118-212824-workstation-83e6`; each with
  its difference figure. Restore by hash after every arm (backup restore ok, `provision.py verify --quick` ok), D155
  marker no box. ACCEPTANCE here means the joined product layout as 0340 and 0394 used it (the branch's server in the
  pin layout), not an installer build: the release's own pictures come with the next candidate.

**Found on the way [L]:** at 21:03 c5h-a's `MainMenu.lua` was still patched by an earlier `frontend.prepare` (load token
of 20:33, before this ticket's claim, never restored); `st3957.py`'s backup took that state as the original, so its
restore put the patched file back and the first joined arm's prepare was refused ("does not match install.manifest.tsv").
`frontend.py restore` gave the manifest's copy back (verify ok). A backup before a run should check the frontend files
against the instance manifest first.

### 11.4 Offline: a south-edge bundle and the flip in the player [L, 2026-11-18 21:35-21:53 EST]

**The bundle** (`record_edge.sh c5h-a`: 0516's recipe, 0403's `rec_live.py` unchanged, save `huge` = the copy
C5H-0301-HUGE_0350 of c5h-a's `C5H-VP8P-HUGE_0350`, the fixed layout of 11.3, `--after 1500 --frames 8 --dump all`,
settle 200 s; beside it `runner.py camera server/evidence/0572/scenes_edge.json --frame south_edge` = (111, 0), default
zoom): **`C5H-0301-HUGE_0350_20261118-213520_workstation`**, format 8, 504 MB; camera set 21:39:11 (zoom 14.2556),
span 21:40:16, frames 2118..2176, all 8 dumped (2554 x 1411), particles spawned (state 4,497,220 B); stamp and checker
`scenebundle: OK`, restore rc 0, no box. Kept in `F:/c5-64bit-work/0572/bundles/` (not in the library: adding it is
the MC's call; it is the library's first map-edge camera).

**The replays** (`offline_0572.sh`; the players of both commits, `steps_0572.sh readers` / `readers-before`):
- **fixed** (sceneshot 81E3290F of bfe552206): `--dumped --compare --tolerance 0:0` **OK, 8 of 8 frames byte-equal** to
  the server's own dumps (P1 at 0:0).
- **before** (sceneshot B4E7D0CB of 68beeb454): **exit 10 Differs** in every frame, depth equal (the planes write no
  depth), colour 1,184,984-1,185,354 of 3,603,694 pixels (798,231 of them in alpha), largest channel difference 255:
  the planes clamped at the world box. Its pictures show the two seams and the cloud-to-black line.
- Against the stock shot of 11.3 (same save and camera procedure, another launch; the offline picture has no interface):
  fixed lower box median 11 / p95 30, 71.3 % within 16, near-black 0 %; before median 32 / p95 220, 32.6 % within 16,
  near-black 34.6 % (stock 0.01 %).
- Visual Checks (DIAGNOSTIC): fixed `vc-20261118-215330-workstation-f4b6` (pass), before
  `vc-20261118-215332-workstation-e011` (fail), each with the stock reference and its difference figure (two earlier
  entries of the same pictures, `...-215311-...-0316` and `...-215312-...-c47a`, are duplicates and say so).

So the defect and its fix reproduce offline from one bundle with one factor (the player's `planeBlocks`): the bundle is
a regression scene for the cloud planes at a map edge.

**The library is unchanged by the fix [L, 2026-11-18 21:58-22:03 EST]:** the standing check "offline equals live"
(`test_offline_equals_live.py`, tolerance 1:1) with the fixed player over the library's bundles
(`F:/c5-64bit-work/scene-library/bundles`, all recorded before the fix): **8 passed** (`library_0572.sh`). None of their
cameras sees past the world box in y, so their planes' blocks are the same under both rules; the fix draws nothing new
there (merge check rule A: no new bundle member or `render_config` key is needed; the inputs, camera and world box, are
already in every bundle).

## 12. 0692: the particle call's cost split (ticket 0692 step 1, 2026-11-23) [L: bench timings; C: code read]

Written 2026-11-23 03:10 EST by worker-0673j under lead-0673 (branch `claude/lead-0673-holistic-d0fd9d`). Why: 0687
(`pan-coherence.md` 6.3) found the server's draw following the render thread's particle call on the reporter's workstation
(5-9 ms at 5,600-6,800 sprites) and asked which part of `ParticleRenderer::frame` holds the time. No product code was
changed; 0688's instruments are not on main yet (only its touches), so the split was taken with local timing points.

**Answer: the particle simulation step holds the time.** The render thread waits for it in `join()` at the start of
`frame()`. At 6,179 sprites (6,720 moved particles) on the huge map, the step takes 9.8 ms (p50) on the particle thread:
the force-and-move pass 6.8 ms, the density pass 2.7 ms and the rebin 0.3 ms. The call's own work after the join is
small: emit 0.63 ms, sort 1.12 ms, upload (Map, copy, Unmap) 0.07 ms, state set-up and draws 0.01 ms, and the GPU
0.35-0.52 ms. The step's length scales with the particles it moves (the view's footprint plus 3 cells on each side),
about 1.4 ms for every 1,000 moved particles. The render thread waits for whatever part of the step is still running when
the next frame starts: `max(0, step - time since launch)`. That gives 3.9-6.3 ms of wait at 6,000 sprites when 3-5 ms of
other work separate two particle calls, which matches the 5-9 ms seen live.

### 12.1 Method

- **Scratch build, outside git.** `server/evidence/0692/run_series.sh build` makes a copy of the tree with `git archive`
  (`render`, `tools/exebuild`, `exebuild`, `server/src`, `shell/common`, `models`, `perf/quietlock`, the settings
  reader) in `C:/c5-64bit-work/0692/src`. `patch_particles.py` adds the timing points to that copy of
  `render/src/fow/particles.cpp`, and the bench `partbench_test.cpp` goes into the copy's `render/tests/fow/`, where the
  folder's glob builds it as `c5r_fow_partbench` with `render/build.py --config Release` (clang-cl, the product's
  toolchain). **What was added to the copy only:** QPC marks in `ParticleRenderer::frame`, taken after the join, after
  the footprints and emit, after the sort, after `Map`, after the copy, after `Unmap`, after the state set-up and the
  draws, and after the launch; the step's own length timed on the particle thread, plus its three passes inside
  `ParticleSim::step` (density, force and move, rebin); and, only in the `est` series (`g_est0692`), two estimates
  taken outside the timed stages: a stable sort of (key, index) pairs of the same sprites, and a count of the sprites
  whose 4 corners' screen box meets some view's clip rectangle. The drawn output is unchanged.
- **Inputs, from the C: copy of the scene library:** `C5H-0301-HUGE_0350_20261122-124422_workstation` (the huge map,
  128 x 80, 101,083 particles in `particles.bin`, 2 views) and `C5H-0301-FOG_0008_20261119-124546_workstation` (Tiny,
  26,080 particles, 2 views). Each bundle's first recorded camera (`cameras.bin`, 2554 x 1411, zoom as recorded) is
  moved back along its back axis by `dist` world units to vary the sprite count: huge 0-1700, Tiny 0-1400. The world
  box is the camera block's. The EXE program and the install's files are read at run time (`render_exe`,
  `render_game_dir`: the pure_vp install with Vibrant Textures; the shader taken from the 4390913 EXE copy).
- **The bench's frame:** each block (round, gap, zoom) loads the bundle's particle state, draws 30 warm-up frames, then
  measures 300 frames. Between frames it clears the target, calls `frame()` between GPU timestamp queries, calls
  `Flush`, and then busy-waits `gap` ms. The gap stands for the rest of the render thread's frame: 0687 measured "draw
  minus particles" at 1.9-5.6 ms, so gaps of 0, 3 and 5 ms were used. The target is 2554 x 1411 with depth cleared to 1
  (every sprite passes) and the mask G = 1 everywhere: an upper bound for the GPU. Device: NVIDIA GeForce RTX 4080, the
  reporter's GPU, on the same workstation.
- **Series (`run_series.sh run`), 2026-11-23 02:53:42-03:04:59 EST:** 6 interleaved rounds (the zoom order turns each
  round), 3 gaps, 6 zooms, 2 bundles, 300 frames each: 64,800 frames. **Estimate series (`est`)** 03:05-03:06 EST:
  2 rounds, gap 0, 200 frames. Every run went through `quietlock.py run`.
- **Machine load: the workstation was NOT QUIET.** The quiet-lock check listed 5-9 heavy jobs at every load record
  (game instances c5h-new-b, c5h-a and c5h-c, the 0630 suites, the 0642 and 4983 `run_all` builds, the 0691 builds).
  Total CPU was 68-98 % in the samples at the start, middle and end. The CPU is an i9-13900KS (8 P-cores and 16
  E-cores, 32 threads). Which kind of core the particle thread ran on is not controlled. **These are indications
  (grade C), not timing results.** The per-round medians of the call's total stay within about +-10 % at low gaps (the
  table's last column in `split.py`'s output); the p99 values carry the load.
- **Analysis:** `python -I server/evidence/0692/split.py C:/c5-64bit-work/0692/runs/series.csv --est
  C:/c5-64bit-work/0692/runs/est.csv`. p50 / p90 / p99 are taken over every frame of every round. The CSVs and the
  load records are in `C:/c5-64bit-work/0692/runs/` and are not in git.

### 12.2 The split (huge map; p50 / p90 / p99 in ms, every frame of 6 rounds, 1,800 frames a row)

Gap 3 ms, the middle of 0687's "draw minus particles":

| sprites (moved) | join (wait) | emit | sort | upload | draws (CPU) | **call total** | step (particle thread) | density | force + move | rebin | GPU |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 1,233 (1,744) | 0.00 / 0.00 / 0.01 | 0.15 / 0.19 / 0.35 | 0.20 / 0.28 / 0.38 | 0.02 / 0.02 / 0.04 | 0.01 / 0.02 / 0.02 | **0.39** / 0.52 / 1.21 | 2.52 / 2.84 / 3.18 | 0.78 | 1.62 | 0.12 | 0.31 / 0.66 / 3.83 |
| 2,264 (2,947) | 0.95 / 1.42 / 3.63 | 0.25 / 0.34 / 0.76 | 0.36 / 0.51 / 0.87 | 0.03 / 0.04 / 0.06 | 0.01 / 0.02 / 0.03 | **1.63** / 2.19 / 4.99 | 4.19 / 4.65 / 5.77 | 1.17 | 2.84 | 0.17 | 0.29 / 0.34 / 1.44 |
| 3,564 (4,450) | 3.09 / 3.85 / 10.36 | 0.38 / 0.53 / 1.44 | 0.60 / 0.85 / 1.45 | 0.04 / 0.05 / 0.10 | 0.01 / 0.02 / 0.04 | **4.17** / 5.07 / 28.84 | 6.41 / 7.05 / 10.49 | 1.77 | 4.41 | 0.22 | 0.27 / 0.42 / 2.25 |
| 6,179 (6,720) | 6.34 / 7.82 / 14.85 | 0.66 / 0.89 / 1.54 | 1.15 / 1.58 / 4.70 | 0.07 / 0.09 / 0.19 | 0.01 / 0.02 / 0.04 | **8.28** / 10.19 / 29.18 | 9.65 / 11.07 / 15.96 | 2.67 | 6.68 | 0.30 | 0.35 / 1.24 / 2.25 |
| 9,061 (8,922) | 8.93 / 10.89 / 16.57 | 0.98 / 1.29 / 3.08 | 1.80 / 2.47 / 5.04 | 0.11 / 0.15 / 0.31 | 0.01 / 0.02 / 0.03 | **11.78** / 14.39 / 29.07 | 12.23 / 14.05 / 17.96 | 3.41 | 8.54 | 0.33 | 0.57 / 1.52 / 2.64 |
| 10,240 cap (11,136) | 11.07 / 13.82 / 18.60 | 1.14 / 1.53 / 2.52 | 2.01 / 2.76 / 5.27 | 0.12 / 0.17 / 0.32 | 0.01 / 0.02 / 0.03 | **14.26** / 17.70 / 34.32 | 14.52 / 17.14 / 20.27 | 4.12 | 10.04 | 0.40 | 0.43 / 1.31 / 4.20 |

(The density, force and rebin columns are p50 only.) **The join's wait against the gap (p50, ms; in brackets the
model `max(0, step - gap)`):**

| map | sprites | step | gap 0 | gap 3 | gap 5 |
|---|---|---|---|---|---|
| huge | 1,233 | 2.69 | 2.45 (2.69) | 0.00 (0.00) | 0.00 (0.00) |
| huge | 3,564 | 6.67 | 6.40 (6.67) | 3.09 (3.41) | 0.92 (1.20) |
| huge | 6,179 | 9.81 | 9.54 (9.81) | 6.34 (6.65) | 3.90 (4.37) |
| huge | 9,061 | 12.07 | 11.80 (12.07) | 8.93 (9.23) | 6.71 (7.23) |
| Tiny | 3,093 | 3.38 | 3.17 (3.38) | 0.15 (0.37) | 0.00 (0.00) |
| Tiny | 6,459 | 6.25 | 6.01 (6.25) | 3.07 (3.34) | 0.94 (1.19) |
| Tiny | 9,772 | 8.92 | 8.68 (8.92) | 5.74 (6.02) | 3.58 (3.85) |

**Linear fits (gap 0, every frame):** step 1.36-1.44 ms per 1,000 moved particles (force and move 0.97-1.03, density
0.35-0.38, rebin 0.03-0.04); emit 0.10-0.11, sort 0.22-0.25, upload 0.013-0.019 and GPU 0.04-0.06 ms per 1,000
sprites; the draw calls are flat at 0.01-0.03 ms. On Tiny the moved count is about 0.65 times the sprite count, because
the footprint reaches the map's edge, so the same sprite count costs a shorter step there (6,459 sprites: 6.3 ms).
The full tables for both maps and all three gaps come from `split.py`.

**What the numbers say:**
- The step is 80-85 % of the work behind the call at every zoom. Of the step, the force-and-move pass is about 70 % and
  the density pass about 27 %. Both evaluate the 3 x 3 cells around every particle: up to 9 x 32 neighbours, each with a
  `sqrt`.
- The step runs on its own thread, but it starts only at the end of the call (`launch`) and has to finish by the next
  call's `join`. When it is longer than the render thread's frame (about 6 ms at 165 Hz), the render thread waits for
  the difference **in every frame**. At the reporter's 165 Hz there is no slack to absorb it.
- The step region is not capped by the sprite cap (10,240). Past the cap the step goes on growing: a smoke run at
  `dist` 4000 on the huge map moved 32,400 particles, step 36.8 ms.
- The sort (`std::stable_sort` of 132-byte `ParticleSprite`s) is the largest stage on the render thread itself, about
  twice the emit. The upload, the draw calls and the GPU do not matter here.
- 26-39 % of the emitted sprites (huge 0.61-0.82 in view, Tiny 0.67-0.72) lie wholly outside every view's clip
  rectangle: they are emitted, sorted, uploaded and drawn for nothing. The emit covers the bounding rectangle of the
  view's ground footprint, a trapezoid. *(Estimate series; the count treats a corner behind the camera as in view.)*
- Every view's draw draws the whole list (`DrawIndexed(n * 6)` once per view, with both views' sprites in it). With 2
  views the GPU works on each sprite twice. At 0.3-0.6 ms of GPU time this is not urgent.

### 12.3 Cuts that keep the picture (proposals for the MC)

*Corrected 2026-11-23 (step 2): A, B, C and D were built and measured (12.5); E was not. B's estimate holds only for the
force pass: in the density pass the skip is slower, so it was taken out there. Was: "(proposals for the MC; NOT
implemented)".*

The estimates are for 6,179 sprites on the huge map with a gap of 3 ms (call p50 8.28 ms). "Byte-identical" means the
particle state and the vertex buffer stay byte-equal to today's. `particles_test` and `particles_game_test` already
check byte equality across runs, so an equality test against today's code proves it. Rule A then runs on every fog
bundle.

| # | Cut | Why it keeps the picture | Estimate [I unless marked] | Files |
|---|---|---|---|---|
| A | **Parallel step in row bands.** Pass 1 (density) splits freely: each particle writes only itself and reads positions that pass 1 does not change. Pass 2 (force and move) runs per band of cell rows into per-band `moved` lists, joined in band order, which is the serial loop's order (y outer, x inner). Rebin stays serial. A small pool of K threads replaces the one particle thread. | Same arithmetic per particle and same rebin order: byte-identical state. | Step 9.65 -> about (2.67 + 6.68) / K + 0.3 + sync: **about 2.7 ms at K = 4, 1.6 ms at K = 8**. That is below a 3 ms gap, so the join's wait drops from 6.3 to about 0 and the call from **8.3 to about 1.9 ms**. Total CPU stays the same, spread over more cores. Stock's own step uses 8 job threads (10.1). | `render/src/fow/particles.cpp` (`ParticleSim::step`, `ParticleRenderer::Impl` worker), `render/include/c5r/fow/fow.h` (a thread count, if exposed), `render/tests/fow/particles_test.cpp` (equality with 1 and K threads) |
| B | **Skip neighbour pairs beyond h = 60** with a conservative squared-distance test (skip only when `d2 > (60 (1 + 1e-5))^2`; the existing `r <= h` and `max(..., 0)` decide all other pairs). | A pair beyond h adds `max(k^3 kSpiky, 0) mass = +0` to the density and `0 * dx * t = +-0` to the force. Adding a zero leaves a finite sum unchanged, and the force sum is never -0, so the result is byte-identical. | About 31 % of the 3 x 3-cell area lies within h (pi 60^2 / 192^2), so about 69 % of the pair bodies (the `sqrt` and the arithmetic) are skipped; **the step about x 0.4-0.6** on one thread (not measured). This also helps the laptop and oldpc, which have fewer cores. | `render/src/fow/particles.cpp` (`ParticleSim::step`), `particles_test.cpp` |
| C | **Sort (key, index) pairs, then gather in sorted order into the mapped buffer**, instead of `stable_sort` of 132-byte sprites. | The comparator and stability are the same, so the permutation and the vertex buffer are byte-identical. | Sort **1.16 -> 0.39 ms** (p50, est series [L]); 0.57 at p99. The copy (0.07 ms) moves into the gather. | `particles.cpp` (`frame`, `ParticleSim::sortSprites`), `fow.h` if `sortSprites` changes, `particles_test.cpp` |
| D | **Cull sprites wholly outside every view's clip rectangle** after the rotation update (emit still updates `rot` for every particle of the footprint, so the state is unchanged). | A sprite outside the viewport draws no pixel. The kept sprites keep their relative order, and so their blending order. | 26-39 % fewer sprites in the sort, the vertex build, the upload and the GPU: **about 0.4-0.6 ms** of emit and sort at 6,000 sprites. Rule A must confirm the near and far planes (sprites with a corner behind the camera are kept). | `particles.cpp` (`ParticleSim::emit` or `frame`), `particles_game_test.cpp` |
| E | **Draw each view with the sprites in its own clip box only** (follows from D). | The same as D. | GPU only, about 0.1-0.3 ms with 2 views. Low priority. | `particles.cpp` (`frame`) |

**Not recommended:**
- **Double buffering** (draw the state one step older while the next step runs). It is not byte-identical per frame:
  the animation shifts by one frame, and the emit's rotation write would race the step unless `rot` is split off. It
  also gives nothing while the step is longer than a frame: the particle thread falls behind anyway. It is worth a look
  only after A and B, and only if the wait is still there.
- **Sorting only when the camera moved.** The particles move every step, so the order changes even with a still
  camera: not identical.
- **Fewer steps.** These change the motion: not stock's behaviour (10.1, one step per drawn frame).

**Expected together (A + B + C + D) at 6,000 sprites:** the call at about 1.0-1.3 ms and the step at about 1-3 ms off the
render thread. Through 0687's slope (about 1.2 ms of draw for each ms of particles), the reporter's server draw of
10-15 ms would come down to about 3-5 ms [I]. Order by value: A (removes the wait), then B (also cuts the CPU), then C,
then D.

### 12.4 Not verified

- Only the bench was timed, not server.exe: the render thread's real work between calls was replaced by a busy-wait.
  The live figure needs 0688's stage timer, or the `fog 1s` cpu field before and after a cut.
- The workstation was loaded (68-98 % CPU, 5-9 heavy jobs), and the particle thread's placement on P-cores or E-cores
  was not controlled. The p99 values carry this load. No oldpc or laptop figure yet: the ticket asks for one before and
  after a cut.
- The `dist` zoom moves the camera back along its own axis. The game's zoom also changes the pitch, so the sprite count
  for a given screen view differs from the game's. The reporter's 5,600-6,800 sprites correspond to `dist` about 1000 on
  the huge map here.
- The GPU figures are an upper bound (depth 1, mask G = 1 everywhere). The game's FOW CULL zeros would reject more.
- B's and A's figures are estimates from the measured pass times, not measured. Neither was built. *(Built and measured
  2026-11-23 in step 2: 12.5. B's estimate was wrong for the density pass: there the skip costs more than it saves.)*

### 12.5 The cuts as built (ticket 0692 step 2, 2026-11-23) [C: code and proofs; L: tests, rule A, bench]

Written 2026-11-23 04:20 EST by worker-0673m under lead-0673 (branch `claude/lead-0673-holistic-d0fd9d`), MC #11156.
Every cut is its own commit and keeps the particle state, the drawn sprite list's order and the picture byte-identical.
Files: `render/src/fow/particles.cpp`, `render/include/c5r/fow/fow.h`, `render/tests/fow/particles_test.cpp`,
`render/tests/fow/particles_game_test.cpp`.

| Commit | Cut | What | Why identical |
|---|---|---|---|
| `24fec2180`, narrowed by `8cd299b75` | B | The force pass skips a neighbour pair with `d2 > kSkipD2 = (60 (1 + 1e-5))^2` when `d2` and `t` are finite. **The density pass computes every pair again** (`8cd299b75`): there the skip made the pass 52-54 % slower on both machines (its pair body is cheaper than the mispredicted branch; 69 % of pairs are beyond h, in no predictable pattern). | The proof is at `kSkipD2` in `particles.cpp`: r >= 60.0006 > h, so `in` is false, w = +0, and `w dx t` is +-0 when dx and t are finite; fx and fy start at +0 and can never become -0 (round to nearest, default MXCSR), and subtracting +-0 leaves any other float unchanged. A NaN or inf t (a zero density) is computed as before. No FMA (x64 SSE, `/fp:precise`). |
| `2610be29f` | C | `ParticleSim::sortOrder` (fow.h): a `stable_sort` of (key, index) pairs with the same comparison; `frame()` gathers `sprites[order[i]]` into the mapped vertex buffer. `sortSprites` stays (tests). | With finite keys the comparison is a strict weak order and a stable sort's result is unique. Tested also with ties (+0/-0) and NaN keys, n 0-10,240: the same permutation. |
| `d1e410845` | A | Passes 1 and 2 of `ParticleSim::step` in bands of cell rows (4 bands per thread, at most one per row), on the calling thread plus `n - 1` helpers of the sim's own (`StepPool`: a task counter tagged by the run's number, so a late helper never takes a later run's task; the caller waits for tasks, never for an idle helper; helpers take the caller's MXCSR). Each band writes its own moved list; the lists are rebinned in band order. | Pass 1 writes only each particle's own density and pressure and reads only positions and masses; pass 2 reads pass 1's output after the whole pass and writes only its own band's list; band order = the serial loop's order (y outer, x inner); the same arithmetic per particle. Identical for any thread count. |
| `2a399f235` | D | `cullOrder` in `frame()`: the draw order of only the sprites some view can show. Dropped when, for every view, the whole square lies beyond one of the four side planes `w -+ x`, `w -+ y` of `(p M)` (the VS's position, 10.1), bound by centre + rho x the plane's xy gradient, margin 1e-5 of the terms' magnitudes. `C5_RENDER_PARTICLE_CULL=0` turns it off. | A dropped sprite is clipped whole and draws no pixel; no depth write; the kept sprites keep their order. Emit is unchanged, so every particle's rotation still advances and `stats.sprites` still counts every emitted sprite. A non-finite key or vertex keeps every sprite. |

**Thread rule (cut A):** `ParticleSim::defaultStepThreads()` = one step thread for every 4 logical processors, at least
1, at most 4: the workstation (32 logical) 4, the laptop and oldpc (4 cores / 8 threads) 2, a 4-thread machine 1. The
step thread plus its helpers then use at most a quarter of the logical processors, which leaves the render thread, the
EXE and the game three quarters. `C5_RENDER_PARTICLE_THREADS=1..16` overrides it (a diagnosis or a bench; the state is
the same for any count). A new `ParticleSim` steps on 1 thread; `ParticleRenderer::init` sets the default.

**Tests [L, 2026-11-23]:**
- `particles_test`: `namespace ref` is a copy of the step, emit and sort before 0692 (8535c8787) working on a state's
  own bytes. The state after every step and emit, the sprites and their sorted order (`sortSprites` and the gathered
  `sortOrder`) are byte-equal to it on synthetic states (the lattice; a dense one with losses at spawn and rebin and a
  varying flow field; pairs at exactly h (d2 = 36^2 + 48^2 = 3600), just inside and just beyond the skip margin; a lone
  particle of mass 0 (density 0, pressure NaN, whose pairs beyond h spread NaN); a NaN mass) and on **all 16 bundles'
  `particles.bin`** of the C: library with the install's `flowfield.dds` (3 footprints, 9 steps each), each at **1, 2,
  4 and 8 threads and with a count changed every step** (1, 8, 2, 4, 3, 16). Mutation checks: the skip at d2 > 3400
  fails the dense, pairs-at-h, mass-0 and bundle cases.
- `particles_game_test`: a renderer on 1 and on 7 step threads against the default (4), and the cull off against on
  with one view, two views (a copy shifted by 700 world units, as a wrap view), one and two perspective views:
  byte-equal pictures, stats and states over 40 frames. Mutation: a centre-only cull fails all four cull cases.
- Render ctest whole (`render/build.py --test`, at `2a399f235`): **108 of 108 passed**, 0 skipped (03:45-03:59).
- Rule A (`run_all.py --only offline-equals-live`, the C: library, the asset root `mods548-vt`, tolerance 1:1, clean
  worktree): **17 passed** at `2a399f235` (03:56-04:13) and again at the final head `1bd07c6cc` (B narrowed;
  04:16-04:48, run_all `OK: 1 suite(s) passed`).
- Builders (every script that links `c5render.lib`, found by searching the build and test scripts; particles.cpp is
  named by none, it reaches them through render/'s CMake), from a clean worktree, each through quietlock: at
  `2a399f235` 03:51-04:15 render lib, `server/build_server.py` (pins-0.1b), units build + run (80 of 80), units extras
  build + run (13 of 13), unitfeed, trigger scene, effect routes test, trigger emitter test, farmfields, unit shadow,
  scene shadow; at `1bd07c6cc` 04:18-04:53 render lib, render tests (`build_render_tests.py`), scenebundle tests,
  sceneshot (`--smoke`), fow scene build + its 31 cases (all PASS, `particle_state` among them), models, cut tap, decal,
  ice, water, overlay scene, forest, liveterrain (3 builders), `shell/build.cmd`: **all 29 steps rc 0** (summary and logs
  in `C:/c5-64bit-work/0692m/allb/`, removed after the hand-back).

**Bench [L] (`server/evidence/0692/run_cuts.sh`, `oldpc_cuts.py`, `cuts_stats.py`):** partbench (12.1) built for each
revision in a scratch tree outside git with `patch_particles_v2.py` (coarser stages present in every revision: join,
emit with footprints, sort (D: cull + order), copy (Map + copy/gather + Unmap), step and its passes). The huge bundle
(`C5H-0301-HUGE_0350_20261122-124422_workstation`, 101,083 particles, 2 views), `dist` 600 / 1000 / 1300 (3,564 /
6,179 / 9,061 sprites before D), gap 0 and 3 ms, 30 warm-up and 300 measured frames a block. Arms interleaved, the
order turned each round. Numbers: the mean over rounds of each round's median, +- the 95 % interval (t); the
difference is paired by round.

*oldpc, quiet place, grade A:* job `20261123-035026-fc89` (every cut, 6 rounds, 03:51-03:56 EST, `undisturbed` true,
foreign 0.029 cores) and job `20261123-041416-3678` (base / BCAD / final, 8 rounds, 04:14-04:18, `undisturbed` true,
foreign 0.072 cores). i7-6700K, RTX 2080, 2 step threads. Full tables: `server/evidence/0692/stats_oldpc_*.md`.

| oldpc, 6,179 sprites (6,720 moved) | call, gap 3 | call, gap 0 | step (particle thread) | density | force | sort | GPU |
|---|---|---|---|---|---|---|---|
| base 8535c8787 | 2.62 +- 0.01 | 5.62 +- 0.01 | 4.37 | 1.37 | 2.71 | 0.77 | 1.32 |
| + B (both passes) | 3.01 (+0.39 +- 0.02) | 6.01 (+0.39) | 4.77 | 2.09 | 2.38 | 0.77 | 1.48 |
| + C | 2.45 (-0.17 +- 0.01) | 5.45 (-0.17) | 4.75 | 2.09 | 2.36 | 0.23 | 1.17 |
| + A (2 threads) | 0.73 (-1.89 +- 0.01) | 3.57 (-2.05) | 2.86 | 1.26 | 1.29 | 0.23 | 1.16 |
| + D | 0.81 (-1.82 +- 0.01) | 3.56 (-2.06) | 2.78 | 1.17 | 1.30 | 0.31 | 1.12 |
| **final 8cd299b75** (B in the force pass only), job 3678 | **0.81 (-1.83 +- 0.03, -69 %)** | **3.23 (-2.39 +- 0.05, -43 %)** | **2.42 (-45 %)** | 0.78 | 1.34 | 0.31 | 1.10 |

At 9,061 sprites (job 3678): gap 3 4.69 -> **1.17** ms (-3.52 +- 0.07, -75 %), gap 0 7.84 -> 4.14 (-47 %); at 3,564
sprites gap 3 0.70 -> 0.47 (-33 %). The call's p99 over all frames at 6,179 sprites, gap 3: 3.82 -> 1.33 ms.

*Workstation, an indication (grade C):* `run_cuts.sh run 6`, 03:59-04:11 EST, NOT QUIET (6-9 heavy jobs of other
teams and three of mine (rule A, the builders), total CPU 67-100 %), 4 step threads. Every cut, 6,179 sprites: gap 3
base 7.58 +- 0.72 ms, + B 8.69, + C 8.19, + A **1.08** +- 0.04 (-6.50 +- 0.74, -86 %), + D 1.18 +- 0.04; gap 0 10.76 ->
3.64 (-66 %); step 9.2 -> 2.6 ms (density 2.59 -> 3.99 with B in both passes, force 6.32 -> 5.77). Series 2 (base /
BCAD / final, 6 rounds, 04:11-04:22, 7 heavy jobs, CPU 100 %): final at 6,179 sprites gap 3 7.70 +- 0.43 -> **1.16** +-
0.02 ms (-6.54 +- 0.42, -85 %), gap 0 10.85 -> 3.51 (-68 %), step 9.18 -> 2.27 ms; at 9,061 sprites gap 3 11.41 -> 1.69
(-85 %). Tables: `server/evidence/0692/stats_ws_*.md`.

**Why the numbers moved:**
- **A removes the wait.** The render thread waits for `max(0, step - time since launch)` (12.1). The step falls from
  4.4 to 2.4 ms on oldpc (2 threads) and from about 9 to 2.6 ms on the loaded workstation (4 threads), below the 3 ms gap,
  so the join's wait at 6,179 sprites goes from 1.36 ms (oldpc) and 5.8 ms (workstation) to 0. With 2 threads each pass
  takes 52-55 % of its serial time (oldpc), with 4 threads 27-33 % (workstation, density 3.99 -> 1.09, force 5.77 ->
  1.31). Total CPU of the step is about the same, spread over the helpers.
- **B (force pass only)** cuts the force pass by 9-12 % (2.71 -> 2.38 oldpc; 6.32 -> 5.77 workstation): the skipped
  pairs' `sqrt`, division and arithmetic. In the density pass the skip cost more than it saved (+52 % oldpc, +54 %
  workstation): its pair body (a `sqrt` and five multiplies) is cheaper than a branch that goes either way at random
  for 31 % / 69 % of the pairs. Hence `8cd299b75`: density pass 2.09 -> 0.78 ms at 2 threads (final), step 2.78 -> 2.42.
- **C** cuts the sort from 0.77 to 0.23 ms (oldpc) and 1.1 to 0.4 ms (workstation) at 6,179 sprites: 8-byte pairs
  instead of 132-byte sprites; the gather into the buffer costs the same as the old copy.
- **D** drops 25-29 % of the sprites (6,179 -> 4,646 at `dist` 1000; 9,061 -> 6,428 at 1300; 3,564 -> 2,704 at 600). Its
  test costs about what it saves on the CPU (sort + cull 0.23 -> 0.31 ms oldpc; call +0.08 ms at 6,179 sprites gap 3,
  -0.01 to -0.05 elsewhere) and it saves 0.04-0.14 ms of GPU (two views draw the list twice). Net about zero on the
  CPU: **D is kept only for the GPU and can be dropped** (its own commit) if the lead prefers the simpler code.

**Not verified (step 2):** the live figure (the `fog 1s` cpu field and 0688's pacing lines on a workstation instance,
arranged by the lead); the laptop; a frame with more than 10,240 emitted sprites in the bench (the cap's identity is
covered by the reference tests' code path, not by a bundle that reaches it); the render ctest whole ran at `2a399f235`, before `8cd299b75` (the particles tests ran at both).

**Proposal (not approved, not built): the pair loops vectorised over the particles of one cell.** Every particle of a
cell has the same 3 x 3 neighbourhood, so 4 (SSE) or 8 (AVX) particles of a cell can walk the same neighbours in the
same order, one lane each: every lane's sum is the scalar code's sum in the scalar order, and `sqrtps` / `divps` are
correctly rounded like `sqrtss` / `divss`, so the state stays byte-identical. Estimate [I]: passes 1 and 2 about x 3 on
one thread, which would also help the 4-thread machines that `defaultStepThreads` gives 1 thread.

### 12.6 Live (workstation, c5h-new-a) [L, 2026-11-23 06:11-06:25 EST]

Written 2026-11-23 06:27 EST by worker-0673p under lead-0673 (branch `claude/lead-0673-holistic-d0fd9d`); MC-B's grant
#11285, c5h-new-a claimed 06:07 and released 06:26. The step 2 acceptance's live figure (12.5 "Not verified").

**Setup.** EXE 26577625, mods548 Goldberg instance on C:, hidden (cloaked, parked launch, focus guard on), `--timing`
(uncapped; the instance's `WaitForVSync = 1` does not bind a hidden window: 160-280 fps), `FOWLevel = 3`, the ring's
slots 2554 x 1411 (the reporter's back buffer size, pan-coherence.md 6.1). Builds (worker-0673o's builds of the branch
head, copied to `C:/c5-64bit-work/0673/live-0692/`): server.exe `383330d5...` (built 05:17 from the tree of `aed29d9e8`,
0691 + 0692 merged; 0697's commits touch only the proxy and the composite), proxy `38b2269e...` and composite
`c15cf3f3...` (of `f7d8bce6c`, 0697), x64 DLL `b84aa3e9...`. Laid by `server/evidence/0692/live0692.sh prep` (0691's prep:
the reporter's `[Renderer]` set, `lockstep_mode=exact` AT THE LOAD, `lockstep_csv=1`, `pacing=1`), save
`C5H-4773-TINY_0018` (already on the instance; turn 18, 31 plots revealed of 56 x 36, AI autoplay set to 0). Method:
0691's key and drag series once, then `wide0692.py`: wheel -30 at the centre (fully zoomed out: most of the Tiny map in
view, nearly all unexplored), 3 s settle, 60 s still; `an0692.py` cuts render.log's `fog 1s` and `pass 1s` lines to the
hold. **The workstation was NOT QUIET**: CPU 100 % in three samples (06:12, 06:18), a second game instance and 63
python processes of other teams running: an indication (grade C, COMMON 23). L2 is the control: the same launch with
`--env C5_RENDER_PARTICLE_THREADS=1` (the step on one thread; cuts B and C still in).

| 60 s still, wide view (median over seconds; p90) | L1: 4 step threads (default) | L2: 1 step thread (control) |
|---|---|---|
| sprites drawn / moved / alive | 5,550 / 5,213 / 26,080 | 5,556 / 5,213 / 26,080 |
| particle call cpu ms (`fog 1s` cpu, mean per frame) | **1.33** (p90 1.38, max 1.45) | 2.71 (p90 2.94, max 3.08) |
| particle gpu ms | 0.40 (0.45) | 0.43 (0.50) |
| server pass busy mean ms (`pass 1s`) | 4.83 (5.27) | 6.44 (6.73) |
| `draw` stage mean ms (the particle call is in it: `afterWorld`) | 1.64 (1.72) | 3.01 (3.26) |
| terrain / forest / predraw / handover mean ms | 1.19 / 0.71 / 0.37 / 0.27 | 1.32 / 0.77 / 0.39 / 0.28 |
| passes per second | 157 | 120 |
| composite presents in the hold (an0691 `hold`): fps, interval p50 / p99 / max ms | 158.6, 6.06 / 12.5 / 113 | 117.2, 8.08 / 16.7 / 78 |
| lockstep wait p50 / p99 ms | 2.10 / 4.02 | 3.49 / 5.94 |
| long passes (>= 20 ms) in the hold | 6, all `top=handover` (16-57 ms) | (not listed) |

**Reading.**
- **The particle call at 5,550 sprites costs 1.33 ms on the live server**, against the reporter's 5-9 ms at 5,600-6,800
  sprites before the cuts (pan-coherence.md 6, 0687; the reporter's build had none of 12.5) and the bench's 1.16 ms
  at 6,179 sprites with 3 ms of other work (12.5, workstation series 2, loaded). Live and bench agree within the
  sprite-count difference and the load.
- **The step threads (cut A) are half of it live:** one thread doubles the call (2.71 ms, +1.38) and the `draw` stage
  grows by the same 1.37 ms, the pass by 1.61 ms, and the presents fall from 159 to 117 fps. The draw stage moves
  with the particle call 1:1, as the reporter's 1.2 ms of draw per ms of particle time (pan-coherence.md 6).
- **What is left of the server's draw** in this view: terrain 1.2, the particle call 1.3 (inside `draw` 1.6), forest
  0.7, predraw 0.4, handover 0.3 ms; the pass is 4.8 ms busy. The six long passes are all in `handover` (the fence
  and the flush), on a machine at 100 % CPU [I: contention, not the particles; the particle stage is never `top`].
- **0688's pacing over the whole of L1** (`pacing_report.py`, the CSV): F2 (world-update interval) p50 5.3 / p99 12.2 /
  max 247 ms, 166 updates/s; F3 0 freezes; F4 (age) p50 4.3 / p99 9.1 ms; F5 0.9999 (the load's held frames only).
  L2: F2 p50 6.0 / p99 15.3 / max 516 ms, 137/s; F3 0 freezes. F1's max (11.6 s / 11.1 s) is the load.

**Not verified.** A quiet machine (no MC window; this is an indication, not a grade A figure); the reporter's own save
and map size (a Tiny turn-18 save: 26,080 particles alive against the huge bundle's 101,083; the sprite count of the
view matches); the laptop and oldpc live (the oldpc bench of 12.5 stands); the cull's (D) share live; vsync-paced
presents as the reporter plays (`--timing` uncapped). Outputs (not in git): `C:/c5-64bit-work/0673/live-0692/`
(`L1.*`, `L2.*`, `marks.tsv`, `live0692.log`, `L1.pacing.json`, `L2.pacing.json`). Scripts:
`server/evidence/0692/live0692.sh`, `wide0692.py`, `an0692.py`; runbook 4.17.

