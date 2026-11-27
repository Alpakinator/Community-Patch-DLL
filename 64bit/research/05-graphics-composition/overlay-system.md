# The overlay system: culture borders, hex highlights, selection, ranges and the hex grid (epic 0167)

Opened 2026-11-14 12:57 EST by lead-0167 as the skeleton for epic 0167's design (brief
`docs/briefs/epic-0167-borders-and-overlays.md`; issue `tracker/issues/0167-i2e-seam-e-borders-and-overlays.md`;
ticket 0138). Sections 1-8 are task 3701's, section 9 is task 3702's. Marks: [C] confirmed by decompile or dump, [L]
verified live, [I] inferred. EXE behaviour is described, never quoted from a decompiler (D83). Addresses are DX11 VAs
at ImageBase 0x400000.

**Status:** sections 1-8 written 2026-11-14 13:22-13:30 EST by 0167-W1 (task 3701, branch
`task-3701-overlay-design`); the design is not yet reviewed by the MC. No code is written in `render/`, `server/` or
`shell/` before the MC's review (the brief's gate).

**The result in five lines.** (1) Under D33 the EXE still renders the whole overlay into the Terrain Overlay target
every frame: culture borders, every highlight style, the unit-selection ring **and the hex grid** (P1 [C + L]). (2) Only
the terrain and water draws that would show it are gone, so the target is simply never seen. (3) Everything the player
sees on the ground from this system is in that one 2048x2048 RGBA8 texture plus one ortho matrix per view; nothing else
(GridShader is a UI shader, not the hex grid). (4) Route (a), a per-Present copy of that target into the server, carries
all of it, the grid included, so task 3705 is not needed as written. (5) The selection ring is the only animated
content found; with decision 2 (i) it shows one EXE frame late, and capture-based checks have to leave it out or take
states without a selected unit.

### Evidence used in this file (all Tiny, all captures and pictures on D: only, never committed, D83)

| Name | What | Pins and configuration | Save | Where |
|---|---|---|---|---|
| 3601 cam1 / cam2 | stock captures of 2026-11-13 (3601), camera 1 (look-at 33,14 zoomed out), camera 2 (Honolulu bay) | 32-bit `i0ship-26234371-ia`, stock config, RenderDoc 1.47 | `C5H-3600-TINY_0008` (copy of `c5h-9028-tiny-t0008`), c5h-c | `D:/c5-64bit-work/3600/captures/c5h-c_20261113-235332_frame{309938,265572}.rdc` (read only) |
| 3601 black | 3310's black arm: the D33 key list minus River and Arrows, no view-mask clear (terrain drawn) | same DLL | same | `..._20261114-010946_frame14671.rdc` |
| 3701 stock A / B | stock with diagnostic stimuli (below), camera on the unit (A), camera at (53,11) (B) | 32-bit `i0ship-26234371-ia` (SHA-256 3C2F47DA...), `darkred.py config --arm stock`, no marker, RenderDoc 1.47 preloaded by the wrapper | `C5H-0167-TINY_0008` = copy of c5h-a's `c5h-9029-tiny-t0008` (SHA-256 b2ceb663..., turn 9), CP/VP/EUI | `D:/c5-64bit-work/0167/captures/c5h-a_20261114-131137_frame{1762,2066}.rdc`, instance c5h-a, 2026-11-14 13:14-13:15 |
| 3701 D33 | the 32-bit in-process D33 arm with the same stimuli, camera as A | `i2-shell-test-26234371` (SHA-256 EB8D26C0..., `deps/i2-shell-test.lock`), `darkred.py config --arm d33` (Overlay on, D93), marker `clear=0x1`; config mask 0xFF240667, **view 0xFF240666** (Terrain bit clear, Overlay bit 0x4 set); no proxy, server or compositor | same | `..._20261114-131611_frame399.rdc`, 2026-11-14 13:18 |

Stimuli (decision 6 (ii), Lua channel, `shell/evidence/0167/3701/stimuli.lua`, Lua state WorldView): select the active
player's first land unit (unit 1002, type 83, at plot (50,13)), `UI.LookAt` it, `Events.SerialEventHexGridOn()`,
`SerialEventHexHighlight` with "MovementRangeBorder" on its six neighbours (colour sent 0.39/0.73/0.96/0.78),
"WorkedFill" on two ring-2 plots (colour sent 0/1/0/0.2), "Culture" on its own plot (colour sent 1/0/1/1). Asset set:
c5h-a's install, VT present (the asset-pipeline 1.7 table gives 45b3b433... for c5h-a; not re-hashed here, and no colour
comparison is made in this file). Tools (repository code, output never committed): `shell/evidence/0167/3701/`
(`convert_xml.py` = RenderDoc's zip.xml conversion, `overlay_census.py` = frame census with materials matched by shader
hash against the player's EXE, `proj_dump.sh` = constants of chosen draws, `target_dump.py` = frame-start texels of a
texture as PNG, `target_probe.py` = handles and OverlayProj by ReadProcessMemory, `callers.py` = direct call sites).
Pictures in the Visual Checks feed: `vc-20261113-235332-workstation-351a` (3601 cam2 target), `vc-20261114-131830-
workstation-5d23` (D33 target), the two in-game diagnostic shots of 13:14 and 13:18 (issue 3701).

## 1. Premise results (P1-P4, P6, P9, P10, P13, P14)

| # | Result | Mark | Source |
|---|---|---|---|
| P1 | **Holds.** Overlay_System's render 0x6EAD50 is called from pass A (call at 0x5895A9, after a test of the view mask against the Overlay bit at 0x58959C) and pass B (call 0x749570 after `test byte [GVS+0x1B4], 4` at 0x749562), separately from the terrain render 0x657070 (bit 0x1, calls 0x5895E5 / 0x74959C). Its per-frame update 0x703B10 (call 0x6FBC9E) is behind bit 0x4 too. D33's view keeps 0x4. Live: the D33 capture has the same 11 draws into the target as the stock capture at the same camera, and no terrain or water draw; the target's frame-start texels match stock (1,572,494 against 1,572,530 non-zero texels; the only differences above 2/255 lie in the 256-texel blocks around the selection ring). | C + L | disassembly (llvm-objdump of the Ghidra input copy); 3701 D33 and stock A |
| P2 | **Holds.** Every highlight style sent drew into the target: HexContour ("Culture"), SplineBorder ("MovementRangeBorder"), FilledHex ("WorkedFill"); none drew anywhere else. | C (capture) | 3701 stock A, D33 |
| P3 | **Holds.** Per frame: clear to (0,0,0,0), the border mesh with depth (DrawSimpleOverlay pass 2), clear depth ("Post Culture Borders Z Clear"), then everything else without depth. | C (capture, 6 of 6) | all six captures |
| P4 | **Holds, with three corrections.** OverlayProj maps world to the target and covers a view-sized window, not the map. (a) It is an array, one matrix per view: `0xDA1690 + 0x40 * v` (view 0 at 0xDA1690, the wrap view at +0x40); the inventory's "TerrainFrame 0x040" is the frame's offset, not the address. (b) The window is snapped to multiples of 64 world units in origin and size, so it changes in steps when the camera moves, not continuously [I from 6 of 6 values]. (c) Its size changes with the zoom: 576x448, 832x640, 896x704, 1280x896 world units seen; aspect 9:7 or 13:10, so texels are not square. | C (capture) + L (read) | section 4 |
| P6 | **Holds.** `[0xDA12A4]` is a small handle (0x248 in stock A, 0x247 in the D33 run: it differs per run); its view-table entry `0x414C348 + 12*h` holds two non-null view objects ([0] and [1]) and a null [2]; its texture record `0x4170B80 + 0x398*h` says 2048 x 2048, 12 mips, format 28 (R8G8B8A8_UNORM). `[0xDA12A8]` (the "Screen Overlay") is handle +1: 2554 x 1411 (the client size), 12 mips, format 28. Under RenderDoc the view objects carry RenderDoc's vtables, so a tap's d3d11.dll vtable check refuses there (expected; the product has no RenderDoc). | L | `target_probe.py` on c5h-a, 2026-11-14 13:14 (stock) and 13:18 (D33) |
| P9 | **Holds for the movement and fire ranges, and the selection is not a highlight style.** MovementRangeBorder is a SplineBorder style sent by base `InGame.lua` (and FireRangeBorder etc. by `Bombardment.lua`, `disable-systems-and-masks.md` 5). The selection mark is the **UnitSelectionHighlight** of `Civ5ArtDefines_Overlay.xml` (colour 0.9/0.9/0.45/1, pulse speed 2, clockwise rotation speed 1): one textured quad per view drawn by Overlay_System itself (pass 3) into the same target, at the selected unit's plot. No separate system draws a selection hex on the ground. | C (capture + file) | 3701 stock A; 3601 cam1/cam2 (the ring is in all six captures) |
| P10 | **Fails.** The hex grid is DrawSimpleOverlay pass 4 into the same target (one draw per view: 7,056 + 1,008 indices at stock A, 5,544 + 2,520 at stock B), with a 512x512 runtime texture; it appears only with the grid on. The GridShader (material 58) draws in every capture are UI nine-slice panels (54 indices = 9 quads, screen-pixel rectangles in VS b0, a screen ortho in b1, into the back buffer without depth), not the hex grid. So the grid needs no pass and no depth rule of its own in the server. | C (capture) | 3701 stock A/B, D33; all six captures for GridShader |
| P13 | Holds (3702: VPUI's `Highlights.xml` with 43 styles is the one loaded; `CultureBorders.xml` and `HexGrid.xml` come from `MiscData.fpk`). See section 9. | C | 3702 |
| P14 | **Holds except for the selection ring** [L, 3702, section 9.3]: at every still camera of both maps, border-band pixels outside moving things changed by more than 12 between shots 10 s apart on 0-1.05 % (p95 0-2), the grid on 0-1 %, the city-banner highlights on 0 %; the unit-selection state on 10-19 % (the animated ring). 3701's contribution: the selection ring changes every frame (rotation and pulse), so it is outside any still-camera equality; everything else in the target was equal between stock A and the D33 run at the same camera (above, P1). | C (capture) | - |

- **P13 holds** [C files and rules, L count] (3702, 2026-11-14): `Highlights.xml` loads from VPUI
  (`Assets/DLC/VPUI/Core/`, 43 styles = the live count); `CultureBorders.xml` and `HexGrid.xml` from `MiscData.fpk` over
  the loose `Assets/Overlays/` copies, which differ from the pack only inside the XML comment, so the parameters are the
  same either way; `Civ5ArtDefines_Overlay.xml` is byte-equal in both. Equal on c5h-b, pure_vp and the Steam install.
  Section 9.4.

## 2. Per overlay: events, EXE state, style file, what draws it in stock and under D33

What draws it, in stock and under D33 alike, is Overlay_System's render 0x6EAD50 with DrawSimpleOverlay (material 67)
into the Terrain Overlay target; the pass numbers are the material's passes as matched by shader hash (`material_passes.csv`
of 2903). Under D33 nothing changes in what is drawn into the target (P1); what is missing is only the terrain and water
draws that sample it. Rows marked "capture" were seen in 3701 stock A and D33.

| Overlay | Made by (events, state) | Style source | Draw into the target | Seen |
|---|---|---|---|---|
| Culture borders | plot owners and border changes from the DLL; the EXE's Overlay subscribes to `SerialEventHexCultureChanged` (B29) and the plot refresh family (`coupling.md` 2) | `CultureBorders.xml` (MiscData.fpk copy, P13): widths, alphas, anti-alias widths, `randomness`, `expansion`, spline contour textures | pass 2, one indexed mesh of the whole map's borders (7,776 indices in all six captures, whatever the camera: not culled to the view), drawn only with view 0's OverlayProj, with the 2048x2048 D24S8 depth target (cleared to 1.0 before); world matrix identity, colour (1,1,1,1) in the constants, so the colours are in the vertices [I]. Picture: a line with an inner glow fading inwards (3601 cam2, VC 351a) | capture (all six) |
| HexContour styles (e.g. "Culture", "TempBorder", the default style `""`, 8 in VPUI) | `Events.SerialEventHexHighlight(hex, on, colour, style)` from UI Lua; kept per plot by the handler 0x574A00 (entries of 0x188 B; style map 0xCF0A48 from `Highlights.xml`) | VPUI `Highlights.xml` (`type="HexContour" width texture`) | pass 1, one 36-index ring per plot, world matrix = the hex lattice basis and the plot's centre, a 128x128 contour texture, **the event's colour** (the "Culture" style has none: the 1/0/1/1 sent was drawn) | capture |
| SplineBorder styles (MovementRangeBorder, FireRangeBorder, GroupBorder, RangedAttackPreview, WorkedOutline, ...; 17 in VPUI) | same event; one outline around the set of plots carrying the style | `Highlights.xml` (`type="SplineBorder" width texture color`) | pass 1, one mesh for the whole set (1,296 indices for a six-plot ring), world identity, a 128x128 contour texture, **the style's colour** when the style has one (MovementRangeBorder drew 100/185/245/200 of the XML) | capture (MovementRangeBorder) |
| FilledHex styles (WorkedFill, OwnedFill, EnemyFill, CityOverlap, ...; 14 in VPUI) | same event | `Highlights.xml` (`type="FilledHex" width color`) | pass 5, one 12-index hexagon per plot at its centre, untextured, **the style's colour** (WorkedFill drew alpha 50/255 = the XML's, not the 0.2 sent) | capture |
| StaticTexture styles (ValidFireTargetBorder, AMRBorder, SampleStaticTexture) | same event | `Highlights.xml` (`static_texture_highlight.dds`) | not stimulated; by elimination pass 0 [I] | - |
| HexModel style (SampleHexModel only, model `SelectedAniOneTurn.fxsxml`) | same event | `Highlights.xml` | a model, so probably not into the target [I]; no VP Lua sends this style (sample entry) | - |
| Unit selection ring | the selected unit (EXE UI state); animated on the EXE's clock | `Civ5ArtDefines_Overlay.xml` `UnitSelectionHighlight`: colour 0.9/0.9/0.45/1, `Pulse` true, `PulseSpeed` 2, `Rotation` clockwise, `RotationSpeed` 1 | pass 3, one 6-index quad per view at the unit's plot with a 512x512 texture pair; the constants carry the hex basis and plot centre (rows 0-3), **a per-frame rotation and scale (rows 4-7, different in every capture)**, the colour (row 12) and a pulse value (row 13) | capture (all six) |
| Movement range | MovementRangeBorder (SplineBorder above), sent by base `InGame.lua` on selection / move mode | as above | as above | capture (by the Lua channel, not by the game's own selection) |
| Fire range | FireRangeBorder (SplineBorder), ValidFireTargetBorder / AMRBorder (StaticTexture), base `Bombardment.lua` | as above | as above [I for the StaticTexture pass] | - |
| Hex grid | the grid toggle: `Events.SerialEventHexGridOn/Off` (the in-game option sends them; whether the toggle state lives in the EXE or in `OptionsManager` was not traced) | `HexGrid.xml` (MiscData.fpk copy, P13): one element, `color="255,255,255,75" width=".2"` | pass 4, one mesh per view over the window's hexes (7,056 + 1,008 / 5,544 + 2,520 indices), world identity, a 512x512 texture created at run time; drawn only while the grid is on | capture |
| Path line, arrows, yield icons | not Overlay_System: MovementPath, Arrows, YieldIcons; off under D33 | - | section 6 | - |

Where the pass-0..5 numbers come from: the six passes of DrawSimpleOverlay share one VS for passes 0, 1, 3, 4, 5 and a
second for pass 2 (the depth-tested border pass) [C, shader hashes].

## 3. The Terrain Overlay target: format, lifetime, clear, draw order

- **Resource** [C capture, L record]: Texture2D 2048 x 2048, `R8G8B8A8_TYPELESS`, 12 mips, bind SRV | RTV; one RTV
  (mip 0) and one SRV (all 12 mips), both `R8G8B8A8_UNORM` (not sRGB). Handle `[0xDA12A4]` (P6). Created by the two
  Overlay start-ups 0x57D2D6 / 0x6FFFAE (inventory 2.4) [C]; lives for the session (same resource through a capture
  frame and between the frames of one run). Companion depth target: 2048 x 2048 `D24_UNORM_S8_UINT`, used by the border
  pass only.
- **Contents are premultiplied** [C capture]: 0 texels with max(r,g,b) > a + 1 in the three decoded targets, matching
  the TerrainShader composition `colour * (1 - t1.a) + t1.rgb` (`terrain-surface.md` 1).
- **Per frame, in this order** [C capture, 6 of 6; the deferred command lists inline in execution order]:
  1. clear depth to 1.0, clear the target to (0,0,0,0);
  2. pass 2: the culture-border mesh, depth on, view 0's OverlayProj;
  3. clear depth (the "Post Culture Borders Z Clear");
  4. for view 0: pass 3 selection ring, pass 1 contours and spline borders, pass 5 filled hexes, pass 4 grid;
  5. clear depth; for the wrap view (1), the same with view 1's OverlayProj (in stock A: ring, two filled hexes,
     grid; the contours and spline borders of view 0 did not reach the wrap window);
  6. `GenerateMips` on the target.
  All of it is recorded on the EXE's deferred contexts and executed on the immediate context before the first terrain
  draw that samples it (the target is not written again later in the frame). So **at the EXE's Present the target holds
  this frame's overlay with its mips** [C capture for the order; I that the Present hook sees it complete, by D3D11's
  in-order execution on the immediate context].
- **Who samples it** [C capture]: TerrainShader t1 (every terrain draw of the frame), WaterShader t9 (ocean pass 3 and
  river pass 4), and the tree shader t2 (2160-index draws, PS hash 95325aff, not in `material_passes.csv`; trees are
  outside this epic). The "Screen Overlay" 0xDA12A8 (2554 x 1411) is a different target, filled by TerrainShader
  passes 2/5/7 and sampled by the tree draws; it is not part of the ground overlay.
- **What a capture holds of it** (task item 4) [C]: RenderDoc records the target's **frame-start contents**: all 12 mips
  (16,777,216 B for mip 0), which are the **previous frame's** finished target. The value the terrain draws of the
  captured frame sampled is rendered inside the frame and is not in the capture without a replay (none is available:
  qrenderdoc waits for a debugger, `water-system.md` 6.2). At rest the two are equal except for the selection ring
  (P14 above); `target_dump.py` writes the frame-start texels.

## 4. OverlayProj

- **Where** [C capture + L]: an array of float4x4 at `0xDA1690`, one per view, stride 0x40 (`0xDA1690 + 0x40 * v`).
  Built by the ortho block 0x51B450 at the end of UpdateViews (`lockstep-probe.md` 1.1: after the views and
  `g_NumViews` are final, before the view-block hand-off), so it is final at the lockstep publish point of frame N.
  Copied unchanged into DrawSimpleOverlay's VS b1, into TerrainFrame +0x040 and WaterFrame +0x040 of each draw, each
  draw taking the matrix of the view it belongs to (stock A, draws 85-140: 52 terrain/water draws with view 0's, 2 with
  view 1's). Live reads of `0xDA1690` and `+0x40` (13:14, 13:18) gave the captured constants' values to the printed
  precision (a byte compare is 3703's).
- **Form** [C capture], row-major as stored: `diag(sx, sy, -0.5, 1)` with the last row `(tx, ty, 0.5, 1)` (view 1's last
  row has z 0.499985 / 0.4999847). `sx = 2 / Wx`, `sy = 2 / Wy` with the window size Wx x Wy in world units; NDC x =
  `x * sx + tx`.
- **Values seen** (world units in the shaders' world space, whose origin is the map centre; Tiny: the terrain's
  TileWorld translation is (-1792, -1108.48)):

| Arm | Wx x Wy | window x | window y | view 1 shift |
|---|---|---|---|---|
| 3601 cam2 (Honolulu bay) | 576 x 448 | 1152 .. 1728 | -512 .. -64 | +3583 (tx -5 -> 7.44097) |
| 3601 cam1 (zoomed out) | 1280 x 896 | -320 .. 960 | -640 .. 256 | - |
| 3601 black | 896 x 704 | -128 .. 768 | -576 .. 128 | - |
| 3701 stock A, D33 (same camera) | 832 x 640 | 1024 .. 1856 (view 0, tx -3.46154) | -576 .. 64 | +3583 (tx 5.15144) |
| 3701 stock B (camera at 53,11) | 832 x 640 | 1216 .. 2048 (derived from the view 1 read, tx 4.68990, minus the 3583 shift) | -704 .. -64 | +3583 |

  (Window x = `(NDC - tx) / sx` for NDC -1 and 1; the wrap view's window is view 0's moved by -3583 in x.)

  Every origin and size is a multiple of 64 world units (one hex width) [C for the six]; [I] that the EXE snaps the
  window, so OverlayProj changes only when the camera crosses a 64-unit step or the zoom changes the size. The wrap
  view's matrix is view 0's shifted by **3583** world units (not the map width 3584 = 56 x 64) [C capture, two
  captures]: a server that derives the wrap matrix itself would be off by one world unit; it must take each view's
  matrix from the EXE.
- **Texel density** varies with the zoom: 2048 / 576 = 3.6 texels per world unit at the closest window seen, 2048 / 1280
  = 1.6 at the widest; x and y densities differ (9:7 or 13:10 windows on a square target).
- **In the server today**: identity (`terrain.cpp` lines 21-22, `water.cpp` line 748), which with the transparent 1x1
  t1/t9 shows nothing.

## 5. Where the overlay enters the server's passes; the grid's pass and depth rule; the frame rule (decision 2)

- **Entry points** [C for the shaders, `terrain-surface.md` 1, `water-system.md`]: TerrainShader t1 with TerrainFrame
  +0x040, WaterShader t9 with WaterFrame +0x040; both are the EXE's own pixel shaders in the server (P5), so the texels
  and the matrix are all the server needs. The matrix must follow the draw's view: the server's wrap copies of terrain
  and water tiles take view 1's matrix where the EXE's do (section 4). Trees (t2) are outside the server today.
- **The grid**: inside the target (P10 fails), so it needs no pass, no shader and no depth rule of its own: it is
  composed by the terrain and water pixel shaders like every other overlay texel, before the fog of war and the distance
  fog, exactly as stock does. Decision 3 becomes moot: the grid comes with route (a). 3705 as written (GridShader) would
  draw the wrong thing; recommend the lead closes or re-scopes it.
- **Fog over the overlay**: TerrainShader applies FOW after the overlay; the server's FOW inputs are "visible"
  constants until seam F (0168), so overlay texels over fogged plots show brighter than stock: an intended difference
  (D81 point 5), named in every comparison.
  *Corrected 2026-11-14 by issue 3957 (epic 0168): the server now draws the fog of war (`fog-of-war.md` 5 and 7): the
  scene binds `c5r::fow`'s mask as terrain t4 and water / river t10 **in the overlay pair's window** (the same OverlayProj
  as t1, P1), with the cloud base t5 and the TerrainFOW LUT t6, so the terrain PS's FOW step after the overlay now acts
  as in stock and this is no longer an intended difference. The fog needs the overlay pair (MC #1131 decision 5 (ii)):
  without it the fog takes the failure route (`fog-of-war.md` 5.5). What is left of the brighter overlay over fogged
  plots is measured live in 3957 phase 2 (fog-of-war.md 8) [open until then].*
- **The frame rule (decision 2)**. Timeline of EXE frame N [C for the EXE order, `lockstep-probe.md` for the publish
  point]: UpdateViews (OverlayProj[N] final; the camera is published to the server here) -> pass A/B record the overlay
  draws on deferred contexts -> the immediate context executes them before the terrain draws -> Present N (the
  composite's hook; the target holds frame N's overlay and mips). The server draws its frame N after the camera of N,
  in parallel with the EXE's frame N. So the overlay texels of N exist only at the EXE's Present N, after the camera
  publish point by the whole of the EXE's frame. **Decision 2 (ii) would therefore cost a full EXE frame of wait in
  lockstep**; it is not free. **Recommendation (i) stands**: frame N binds the target copied at Present N-1 **together
  with the OverlayProj array of N-1** (texels and matrix are a pair; mixing N-1 texels with the N matrix would shift
  everything by the camera step). Consequences of (i):
  - static overlay (borders, grid, highlights at rest) is exact at rest, every texel;
  - a change (a new highlight, the grid toggle, a border change) appears one EXE frame late;
  - while the camera moves, the N-1 window lags the camera by one frame; whether the window has enough margin around
    the visible ground to hide that was **not checked** (open question 1); to be measured in 3706;
  - the selection ring is drawn at the phase of frame N-1: an offset of RotationSpeed x one frame time (about 33 ms at
    the harness's 30 fps cap) of rotation, and the matching pulse step.
- **The animation and exactness (the lead's question, 13:05)**. The rotation and pulse are computed by the EXE on the
  CPU per frame and written into pass 3's constants (rows 4-7 and 13 of VS b0); the result is in the target's texels.
  The tap copies texels, so it carries the animation as rendered; it never needs the clock. Comparisons:
  - **Done B (1), tap against the EXE's target of the same frame**: exact, ring included, as long as both reads are of
    the same Present (the tap publishes its frame number; the check reads the EXE's target in the same Present hook).
  - **Done B (2), terrainshot replay of a capture**: the capture holds only the previous frame's target (section 3), so
    its replay with t1 = the frame-start target equals the captured terrain draw everywhere except the ring's texels.
    Either take the B (2) state without a selected unit (no ring; then nothing animated is in the target [C, the D33 /
    stock A comparison]) or mask the ring's hex out. Note that the frame-start target is exactly what decision 2 (i)
    binds for that frame, so B (2) tests the product's own pairing.
  - **Done C, frame level against stock**: two different runs never share the ring's phase; mask the selected unit's
    hex (it is under the unit model, which the D33 picture draws dark red anyway) or compare without a selection.

## 6. The D33-off overlays: MovementPath, Arrows, YieldIcons (decision 4 inventory)

None of the three draws into the Terrain Overlay target, as far as can be seen [C: no direct reference to 0xDA12A4 lies
in their render functions; the references are the two start-ups, the overlay render 0x6EAD50..0x6EAF3D and nine sites
in other functions not traced; I: none of them was active in a capture]. So none of them comes with route (a), and none
of them is needed for 0138's selection, movement range and named highlights.

| System (bit) | Static objects, render | What it draws for the player | D33 | What it would take |
|---|---|---|---|---|
| MovementPath (0x800000) | 0xDAB490, 0xC8EEFC; R 0x70BE30 / 0x519B90 (`world-systems-and-stubs.md`) | the planned path line and turn markers of a unit's move order (when the player hovers a destination in move mode) [I from the name and the stock game] | off (key and view bit) | turning the key on in D33 (memory +5 MB measured at boot, `disable-systems-and-masks.md` table) and whether its render draws in the world pass (then the EXE draws it over the composite as a model-like layer) or into a target; not traced. A capture with a move order hovered (3702 can add the stimulus) would show where it draws. |
| Arrows (0x400000) | 0xB9C418; R 0x4E5590; `assets/UI/Overlays/ArrowSettings.xml` | combat and movement arrows (`SpawnArrowEvent`) | off; was a crash key (pass B 0x7495E8 -> 0x4E5590 crashes when the Arrows start-up was skipped) | the MC's: turning the key on needs its crash measured (the brief, decision 4). |
| YieldIcons (0x10000) | no render traced; the view bit is never read; the `YieldIconManager` Lua state exists in the UI | the per-plot yield icons when the player switches them on | key off | the icons are UI-side (D80 keeps world anchors in the EXE) [I]; not an overlay of this target. |

Recommendation (decision 4 (i)): new issues by the MC for MovementPath and Arrows, each starting from one stock capture
with the path line and an arrow on screen; YieldIcons with the UI anchors (D80), not with this epic.

## 7. The route (decision 1), sizes, and route (b)'s size

**Route (a), the tap, is confirmed by the premises and covers more than the brief assumed** (the grid too). Sizes:

- What is copied: the whole resource, 12 mips: 16,777,216 B (mip 0) x 4/3 = **22.4 MB per buffer**, two buffers about
  **45 MB of video memory on the EXE's device** (the brief's 32 MB assumed mip 0 only). Copying mip 0 alone (16 MB, one
  `CopySubresourceRegion`) and regenerating the mips in the server would save 6.4 MB per buffer but makes the server's
  mips depend on the driver's `GenerateMips` filter on another device; recommend the full `CopyResource` (exact by
  construction).
- Per Present: one `CopyResource` of 22.4 MB on the EXE's immediate context (GPU work about 45 MB of traffic per
  frame; the CPU call is of the order of the bake tap's 0.002 ms per copy [I]), one event query, a frame number and the
  OverlayProj array (`g_NumViews` x 64 B; two views seen) written into the shared section. No timing claim (COMMON 23).
- Reads in EXE memory, all data, SEH-guarded behind pattern checks (the bake tap's pattern): `[0xDA12A4]` (handle), the
  view table entry `0x414C348 + 12*h` (the view objects; `GetResource` on the SRV gives the texture; check 2048 x 2048,
  12 mips, R8G8B8A8 typeless family), `0xDA1690 + 0x40*v` for v < `g_NumViews`. The handle differs per run (0x247,
  0x248 seen): read it at start-up and after every load, never as a constant.
- Shared texture: created on the EXE's device as `R8G8B8A8_TYPELESS` (same typeless family, required by
  `CopyResource`), 12 mips, NT-handle shared, opened by `server.exe` with an `R8G8B8A8_UNORM` SRV over all mips (the
  EXE's own SRV format).
- Hours: the brief's 4-7 h (3703) and 3-5 h (3704) stand. 3705 (GridShader) drops: the grid is in the target; 0-1 h to
  close it with a grid check in 3706's tables instead.

**Route (b), re-implementing Overlay_System in the server**, for the record (P1 holds, so not needed now): it would
have to reproduce passes 1-5 with DrawSimpleOverlay (the EXE's blob at run time), the border mesh builder
(`CultureBorders.xml`, randomness, expansion, spline contours: undescribed), the SplineBorder outline builder for
highlight sets, the per-plot highlight table of 0x574A00 read every frame, the grid mesh and its run-time 512x512
texture, the selection ring's clock, the 64-unit window snap and the 3583-unit wrap. Size 20-30 h on top of (a)'s
server part (up from the brief's 15-25 h: the grid, the ring and the outline builder are now known to be in it), with
the near-miss risk on border and outline curves unchanged.

The tap as built and its live results: section 11 (task 3703).

## 8. Open questions and the MC's review answer

Open questions (3701, 2026-11-14):

1. **Window lag at a pan** (decision 2 (i)): whether the N-1 window ever leaves view N's ground uncovered at the edge
   during a fast pan; measure in 3706 (count transparent-overlay texels at the view edge during the 3311 pan script).
2. **Present hook completeness**: [I] that the target is final at the composite's Present hook (in-order execution of
   the immediate context); 3703 confirms with a texel compare of the tap against a second read in the same Present.
   *Answered 2026-11-14 by 3703 (section 11, live):* the tap equals the target at the Present in 577 of 577 checks
   [L], and every bind of the target in a frame was seen before that frame's Present [L, verify mode]. That the
   recorded overlay lists have **executed** by the Present remains [I] (D3D11's in-order immediate context); the server
   pictures showed complete highlights, no partial overlay.
3. **OverlayProj at Present**: [I] that `0xDA1690` still holds frame N's array at Present N (UpdateViews of N+1 comes
   later on the same thread); 3703 checks against the bytes in the frame's TerrainFrame (a capture with the tap on, or
   the composite's own `Map` hook). *Answered 2026-11-14 by 3703 [L]:* the array read at Present N equals, byte for byte,
   the array read in the OMSetRenderTargets hook when the EXE binds the target in frame N (the overlay render copies
   `0xDA1690` into its constants right after that bind), in 576 of 577 checks (1 without a bind); the TerrainFrame route
   was not needed.
4. **StaticTexture styles** (ValidFireTargetBorder, AMRBorder): which pass (0 by elimination) and whether they draw into
   the target: one capture with a ranged unit in attack mode (3702's fire-range camera).
5. **Grid toggle state**: where the EXE keeps it (Overlay object or options); only needed if the server ever draws
   without the EXE's target (route b).
6. **The ≤2/255 differences** between stock A and D33 outside the ring (14,491 texels) are not explained [I: rounding of
   the wrap pass or a blend order]; they do not affect route (a), which copies whatever the EXE drew.
7. **Wrap views**: only two views were seen on Tiny; the tap publishes `g_NumViews` matrices, not a fixed two.
8. **3705's fate**: the lead and the MC decide whether 3705 is closed or re-scoped (P10 failed).
9. **Corrections owed elsewhere** (*made 2026-11-14 by lead-0167 #2 in the four files named, each with a dated pointer here; 0173, which held the folder, is done*) (not made by 3701: only this file was granted while ticket 0173 holds the folder):
   `terrain-rendering-inventory.md` 2.4 ("Hex grid: GridShader (58) via 0x6ab930 [I]" is wrong: the grid is pass 4 of
   DrawSimpleOverlay into the Terrain Overlay target; GridShader draws UI nine-slice panels); `asset-pipeline.md` 3.2
   row IceShader/GridShader (line 480) and 3.3 OverlayProj ("0xda1690+0x40v" = one matrix per view v, stride 0x40);
   `renderer-in-server-design.md` 4.11 ("the server must draw them": the EXE does draw them, into a target nobody
   samples under D33); `disable-systems-and-masks.md` 5 (0127), the sentence "the EXE still draws no hex highlights ...
   under D33" (it draws them into the target; they are invisible because the terrain draw is off). Each with a dated
   pointer to this file.

**MC review answer** (MC #696, 2026-11-14 13:29 EST, through MC-B; recorded by lead-0167 13:30): **design approved**
(done A met; sections 1-8 read at 4640e8f5). (1) Route (a), the tap, as designed; it fits the Architect's rule R2 (D3D11
on the EXE's device from the compositor's hooks, guarded data reads at 0xDA12A4 and 0xDA1690, no call by address). (2)
Decision 2 (i): frame N binds the target copied at Present N-1 with N-1's OverlayProj array as one pair; 3706 measures
the edge during a fast pan. (3) 3705 withdrawn; a grid check goes into 3706's tables; epic size 18, estimate 12-22 h.
(4) MovementPath, Arrows and YieldIcons are out of this epic (an Architect hand-over design,
`research/07-architecture-decisions/architect-2026-11-14-hand-over-designs.md`, as MC tickets); its piece c is 0167's:
stock references with a move-order path line, a ranged-attack arrow, yield and resource icons (VP and EUI), a
trade-route arrow and floating yield text, and in 3706's tables whether each draws under D33 today. (5) A tap that
cannot install at game start (guard mismatch, missing target) takes D125's route (popup, report, exit, restore); a fault
during play (a missed or stale copy) is logged loudly and counted, and the game goes on (Architect rule R6, adopted as
D132). (6) Player input for ACCEPTANCE pictures; the Lua channel for diagnostics and done B. Video memory of 45 MB (two
12-mip buffers) accepted. The corrections of open question 9 are made in wave 2. *Note 2026-11-14 lead-0167: the E2
contract publishes three slots (the producer never overwrites the slot the server samples), so the video memory is
about 67 MB, not 45 MB; reported to the MC with the contract.*

## 9. Stock references and the D33 gap

(3702: cameras, arms, the census table per camera, stock's shot-to-shot ceiling (P14), the style files loaded (P13).)

Task 3702, 2026-11-14 (worker 0167-W2, branch `task-3702-overlay-references`). Driver
`shell/evidence/0167/3702/run_3702.py` (3608's stock and joined arms with the overlay cameras and stimuli added), census
`shell/evidence/0167/3702/compare_3702.py`, camera files `shell/evidence/0167/3702/cameras_tiny.json` and
`cameras_huge.json` (from `export_cams.py`); pictures, camera blocks, plot and owner lists, census JSON and difference
images only in `D:/c5-64bit-work/0167/captures/3702/<map>/` (D83; archived there from C: under D133: `stock/` and
`joined/` are the valid runs below; the superseded runs were deleted, their pictures stay in the Visual Checks
store, amended SUPERSEDED or SPOILED), every picture in the Visual Checks feed
under issue 3702. Valid runs (all rc 0, restored by SHA-256, c5h-b): stock Tiny 15:32-15:42 and stock huge
15:08-15:20; joined Tiny 15:00-15:08 (v2 and the v1 states retaken 15:58-16:04) and joined huge 15:21-15:32 (v1
states and item 5 retaken 16:04-16:15).

### 9.1 Cameras, arms, saves

- **Saves.** Tiny: copy `C5H-0167-TINY_0009V` of c5h-c's `C5H-3600-TINY_0009V` (SHA-256 27420a41..., turn 9, every
  plot revealed and visible, so the census needs no fog cut; the same map as 3701's `C5H-0167-TINY_0008`). Capital
  Honolulu (51, 14); its border meets water in view (a bay inside the border). Huge: copy `C5H-0167-HUGE_0350` of
  `C5H-VP8P-HUGE_0350` (SHA-256 c499d3c6...). Copies are made by the driver and removed at its end.
- **Cameras** (camera blocks `<cam>_camera*.json`, `plan.json` per map, so 3706 can return to them; `UI.LookAt` to the
  plot, wheel ticks at (1277, 705), popups closed by Esc until the top band is undimmed, D114; cursor parked at
  (2404, 1310) over the minimap in every arm): 3200's procedure `v1_city_default` (city, 0 ticks), `v2_city_zoomin`
  (city, 4 ticks in: zoom 11.0), `v3_second_zoomin` (the plot north of the city, no ticks: it keeps v2's zoom 11.0, as
  in 3200); `b_land` and `b_water` (the owned land / water plot nearest the city on a border, by Lua); Tiny also 3601's
  `w_cam1_3601` (33, 14) and `w_cam2_3601` (51, 14). Huge: 3200's plan plots exactly (city (100, 30), second (99, 31)).
  **Tiny deviation:** 3200's Tiny plots belong to another save (`C5H-3000-TINY-T0018`, Madrid (8, 23)), so on this
  save the same procedure runs at the capital.
- **Stimuli** at `v1_city_default`, each as an on/off pair at one camera (on: stock 2 shots, joined 1; off: the shot
  right after the stimulus was undone): `banner` (cursor over the city banner: mouse move, player input), `unit` (left
  click on a land unit's plot: player input), `ranged` (click on a ranged unit: player input; range mode by Lua
  `UI.SetInterfaceMode` in the retakes, key B before), `grid` (key G: player input; Lua `OptionsManager.GetGridOn`
  confirms). Lua is used for state checks, to put the units to sleep and to clear the selection (diagnostic, named in
  every record). Item 5 (MC #696) on huge: `x_path` (unit clicked, move mode, cursor 3 plots east), `x_arrow` (a
  ranged unit next to an enemy clicked, range mode, cursor on the enemy), `x_yield` (Lua `UI.SetYieldVisibleMode` +
  `SetResourceVisibleMode`, what the map options' checkboxes call), `x_trade` (Lua
  `Events.Event_ToggleTradeRouteDisplay`), `x_float` (Lua `Events.AddPopupTextEvent` at the city), each on/off.
- **Arms.** Stock: 32-bit pin `i0ship-26234371-ia` (SHA-256 3c2f47da...), `darkred.py --arm stock`, no proxy, no
  server. Joined: the D33 product path of 3308/3311 (ServerRenderer = 1, composite + manifest beside the proxy), D33 in
  the shipped A'' form (D119, D125: the 13 keys of `proxy/src/proxy_session_keys.h` off in config.ini, River and
  Arrows on, Overlay on, D93), x64 DLL 74588135 of `x64-04b1211b`, server.exe 16f55bc7..., proxy 2f723054...,
  composite 5bdae04c... built from this branch at bc05e6bb (the only later change on the branch is 0167's overlay-tap
  stub, which draws nothing). Asset set: c5h-b, VT present, folder hash `45b3b433...` (the feed's record; P15).

### 9.2 Lessons for driving the stimuli (2026-11-14, c5h-b, stock Tiny) [L]

- **A unit selection recentres the camera on the unit** (left click on Warrior (50, 13) moved the camera by (-32, -48)
  world units). A `UI.LookAt` back to the stimulus plot returns it to within 0.06 world units (sub-pixel), not byte-equal.
- **`UI.ClearSelectionList()` does not leave nothing selected** while the player has units that need orders: the game
  selects the next unit at once and pans to it (the head unit read back 1002 after every clear, 13:37 run), also with
  `OptionsManager.SetAutoUnitCycle_Cached(false)` (cached, not committed). In the first two runs (13:10, 13:18) every
  view picture therefore showed the Warrior's surroundings, not the planned camera; those feed entries are amended
  "SUPERSEDED". The fix used from 15:00: every unit of the active player put to sleep first (Lua `PushMission`
  `MISSION_SLEEP`, flags given as numbers: `PushMission(..., 0, 0, 1)`; booleans are refused, 15:08), on the save
  copy, the same in every arm. With it, the head unit after a clear is -1 and every camera block is equal before and
  after the shots in all later runs [L].
- **Never send ESC or letter keys after the state** (15:21 run): ESC with nothing to cancel opens the game menu, and
  the next keys act on the menu (once its "exit to the main menu?" confirmation opened; nothing was confirmed). The
  retakes leave modes by Lua `UI.SetInterfaceMode(INTERFACEMODE_SELECTION)`.
- **The harness PrintWindow artifact** (`research/03-dll-engine-boundary/proxy-design.md`, side observation 2) hit
  7 of the 9 shots at the joined huge stimulus camera (world without the EXE's UI or models, or all 84,84,84). The
  driver now takes up to 10 shots and keeps the first whose top 30 rows match the stock reference (mean max-channel
  distance < 50; artifact frames 69-117, good frames 31-37; flat frames never kept). Comparing the minimap corner
  does not work: under D33 the minimap is black (Minimap off).
- **The camera block read right after `UI.LookAt` can predate the end of a pan**: the driver reads it again after the
  shots and records `camera_equal_after`.
- Key G toggles the hex grid (`OptionsManager.GetGridOn` true, then false after a second G) [L]: a player-input
  stimulus works for the grid.
- The city banner's hover is found by moving the cursor up from the city's projected centre (gt_reader's camera, grid
  and heights; `run_3702.py` Geo): at the LookAt zoom 14.26 the banner sits 100-160 px above the plot centre.

### 9.3 The census and the D33 gap (2026-11-14 15:00-16:16 EST, c5h-b) [L]

Method (`compare_3702.py`'s text holds the details): pixel to world through the stock arm's camera block and the height
field, world to plot through the inverse of HexToWorld; **ground** = visible plots, minus the interface's fixed
rectangles + 16 px, minus pixels that move by themselves (stock shot 1 against shot 2 at that camera, > 12, + 16 px),
minus a 16 px frame. **Overlay masks**: a stimulus state = ground where stock "on" differs from stock "off" (the shot
right after the state was undone, same camera) by > 12 (banner: minus a 340 x 120 px box at the cursor); **border band**
= ground within 24 world units inward (8 outward) of an edge between plots of different owners. Per mask the largest
channel difference: median / p95 / max (share > 12). "Stock vs stock" = the ceiling (P14): for views and plain shots 1
against 3 (10 s apart; the motion mask came from 1 against 2), for states on-shot 1 against 2 (5 s). "Joined vs stock" =
the joined D33 picture against the stock picture; "joined on vs off" = does the stimulus change anything on those
pixels under D33. Joined and stock are different launches: their camera blocks differ by 0.001-0.003 in the view
matrix (`joined_view_max_abs_diff`), so "joined vs stock" also carries sub-pixel shifts. Asset set: c5h-b, VT present,
`45b3b433...` (P15). Fog of war: the stock pictures have it, the joined ones (seam F) do not; the joined builds predate
0185 (WhiteFogMask on by default, D136), so the joined pictures show the product without it.

**Tiny** (`C5H-0167-TINY_0009V`, capital Honolulu (51, 14); no ranged unit in reach, so no `ranged` row):

| Camera | Mask | Overlay px | Stock vs stock | Joined vs stock | Joined on vs off |
|---|---|---|---|---|---|
| v1_city_default | border band | 175,378 | 0 / 0 / 141 (0.11 %) | 20 / 106 / 251 (59 %) | - |
| v1_city_default | ground off the band | 2,200,113 | 0 / 0 / 18 (0.01 %) | 15 / 100 / 251 (53 %) | - |
| v1_city_default | banner (city highlights) | 288,880 | 0 / 0 / 162 (0 %) | 49 / 139 / 251 (91 %) | 0 / 157 / 255 (10 %) |
| v1_city_default | unit (selection + movement range) | 212,659 | 5 / 26 / 132 (19 %) | 46 / 99 / 255 (85 %) | 0 / 130 / 251 (14 %) |
| v1_city_default | grid | 236,765 | 0 / 0 / 55 (0 %) | 35 / 90 / 241 (96 %) | 0 / 0 / 52 (0 %) |
| v2_city_zoomin | border band | 521,965 | 0 / 0 / 200 (0.31 %) | 18 / 109 / 251 (55 %) | - |
| v3_second_zoomin | border band | 528,375 | 0 / 0 / 222 (0.24 %) | 25 / 116 / 251 (65 %) | - |
| b_land (50, 13) | border band | 372,624 | 0 / 0 / 162 (1.05 %) | 20 / 102 / 251 (58 %) | - |
| b_water (51, 13) | border band | 347,635 | 0 / 0 / 158 (0.77 %) | 21 / 99 / 251 (60 %) | - |
| w_cam1_3601 (33, 14) | border band | 20,628 | 0 / 0 / 0 (0 %) | 56 / 122 / 149 (95 %) | - |
| w_cam2_3601 (51, 14) | border band | 519,270 | 0 / 1 / 166 (0.43 %) | 21 / 111 / 242 (58 %) | - |

**Huge** (`C5H-0167-HUGE_0350`, Memphis (100, 30); b_land (96, 27) borders player 41, b_water (103, 31) player 31):

| Camera | Mask | Overlay px | Stock vs stock | Joined vs stock | Joined on vs off |
|---|---|---|---|---|---|
| v1_city_default | border band | 37,876 | 0 / 0 / 11 (0 %) | 79 / 152 / 201 (91 %) | - |
| v1_city_default | ground off the band | 2,752,820 | 0 / 0 / 243 (0.05 %) | 21 / 146 / 255 (57 %) | - |
| v1_city_default | banner (city highlights) | 1,490,448 | 0 / 0 / 239 (0 %) | 39 / 129 / 245 (86 %) | 0 / 127 / 255 (17 %) |
| v1_city_default | unit | 260,477 | 0 / 17 / 249 (10 %) | 3 / 116 / 255 (42 %) | 53 / 195 / 255 (61 %) * |
| v1_city_default | ranged (fire range) | 156,216 | 90 / 181 / 252 (82 %) * | 0 / 97 / 255 (18 %) | 106 / 202 / 255 (87 %) * |
| v1_city_default | grid | 193,521 | 0 / 0 / 64 (1 %) | 38 / 114 / 208 (93 %) | 0 / 0 / 15 (0 %) |
| v2_city_zoomin | ground (no border in view) | 2,930,792 | 0 / 0 / 175 (0.01 %) | 37 / 151 / 255 (66 %) | - |
| v3_second_zoomin | ground (no border in view) | 2,930,407 | 0 / 0 / 70 (0.07 %) | 49 / 156 / 255 (71 %) | - |
| b_land | border band | 595,864 | 0 / 2 / 53 (0.86 %) | 28 / 153 / 251 (71 %) | - |
| b_water | border band | 195,395 | 0 / 0 / 14 (0 %) | 55 / 170 / 251 (87 %) | - |

\* Not clean: the click on the ranged unit (and on huge the unit) recentred the camera, so these states were shot at
the unit's camera (`v1_city_default_camera_<state>_state.json`), while the masks are computed in the v1 camera's pixel
space; the two stock "ranged" shots also differ in the panel area (the attack preview). Read these rows from the
pictures, not from the numbers. The Tiny joined `unit_off` comes from the first joined run (the retake's candidates
were all flat).

**P14 [L]: holds, except for the selection ring.** With the camera still and no input, overlay pixels outside moving
things are equal between shots: border bands 0-1.05 % changed by > 12 over 10 s (p95 0-2), grid 0-1 %, city-banner
highlights 0 %. The unit-selection state changes on 10-19 % of its pixels within 5 s: the selection ring rotates and
pulses (`Civ5ArtDefines_Overlay.xml`, section 9.4; 3701's P14 note). What changes between still shots otherwise is not
overlay: units' idle animation, water, smoke and light shafts (the motion mask: 2-20 % of a frame).

**The D33 gap [L, pictures; the numbers above]**: under D33 (Overlay on, D93) the joined picture shows **none** of the
Terrain Overlay target's content:

- **Culture borders**: absent at every camera of both maps, over land and over water (the stock line and its inner glow
  are missing; the band's joined-vs-stock difference is above the rest of the ground: Tiny 55-65 % against 31-61 %,
  huge 71-91 % against 51-71 %).
- **City highlights** (cursor over the banner: OwnedFill, WorkedFill, their outlines, the purchase/vacant outlines,
  `CityBannerManager.lua`): absent; what changes under D33 on those pixels (10-17 %) is the EXE's UI (the banner's
  tooltip, the citizen and yield icons over the plots).
- **Selection ring and movement range** (unit clicked): absent on the ground.
- **Fire range** (`FireRangeBorder`, ranged unit in range mode): absent; the **ranged-attack target marker** (the red
  vertical beam over the target) and the attack-preview panel **are drawn under D33** [L, item 5 `x_arrow`].
- **Hex grid** (key G): absent: 0 % of the grid pixels change under D33 when the grid is switched on.

**Item 5 (MC #696), stock references and the same state under D33** (huge; the joined item 5 shots were taken at the
LookAt zoom 14.26, the stock ones after v2's zoom-in at 11.0, so the pairs are judged by eye, not measured):

| State | Stock (reference) | Under D33 (joined) | Mark |
|---|---|---|---|
| Move-order path line (`x_path`, Landship clicked, move mode, cursor 3 plots east) | the turn marker disc with "<1" and chevrons along the path | the "<1" and "4/4" texts (UI) only; **no disc, no path chevrons** (MovementPath off) | L |
| Ranged-attack arrow (`x_arrow`, Artillery at (84, 31) on Rifleman at (84, 33)) | red target beam, red fire-range ring, preview panel | beam and panel drawn; **no fire-range ring** | L |
| Yield and resource icons on | icons over every plot | **icons drawn** (UI-side; YieldIcons key off does not remove them) | L |
| Trade-route display | purple route arrows along the railway | not seen in the joined view; its camera differs (zoom), so not judged | I |
| Floating yield text (`AddPopupTextEvent`) | not visible 0.8 s after the call in either arm | - | not judged |

Stock `x_path__pathline_off` is spoiled (the game menu opened by ESC); the "on" picture is the reference.

### 9.4 P13: which copy of each overlay file the game loads (2026-11-14 13:03 EST, task 3702, desk work)

Read only, scripts `shell/evidence/0167/3702/p13_files.py` and `p13_packs.py` (pack index read by asset-pipeline.md
1.1's layout).
Installs compared: c5h-b (`%USERPROFILE%/c5-instances/c5h-b/install`), its source `%USERPROFILE%/Desktop/pure_vp/Sid
Meier's Civilization V`, and the Steam install (`C:/Program Files (x86)/Steam/steamapps/common/Sid Meier's Civilization
V`): **every file below is byte-equal on all three** [C]. No copy of any of the four names in the instance's `MODS` tree
[C]. SHA-256 prefixes (8 hex digits; the full values: run `p13_files.py`). The VPUI file's full SHA-256 is
8d6854d8bea32d74c86b63709752e50f6fd0023dc7c4e790b52568e61f350f05, `MiscData.fpk`'s
d19e1a29c456f0a16c31bb4460c281691ffc96d66cb7733bca1752fca59817ce:

| Name | Loose copy (path, B, SHA-256) | Pack copy (`Resource/Common/MiscData.fpk` 8,122,048 B d19e1a29; entry B, SHA-256) | Copy loaded | Mark |
|---|---|---|---|---|
| `Highlights.xml` | `Assets/Overlays/` 1,639 B 6b780643 (15 styles); `Assets/DLC/VPUI/Core/` 4,273 B **8d6854d8 (43 styles)** | 1,550 B ba6bc50d (14 styles) | **VPUI's** `Assets/DLC/VPUI/Core/Highlights.xml` | C rule + L count |
| `CultureBorders.xml` | `Assets/Overlays/` 1,014 B 860d47c6 | 1,014 B 382e5719 | **the pack copy** (base pack over base loose, LooseFilesOverridePAK = 0) | C rule, I not traced live |
| `HexGrid.xml` | `Assets/Overlays/` 1,291 B cb50938d | 1,291 B f3a4e214 | **the pack copy** (same rule) | C rule, I not traced live |
| `Civ5ArtDefines_Overlay.xml` | `Assets/Overlays/` 400 B 18df5a2f | 400 B 18df5a2f (identical) | either (same bytes) | C |

Why [C for the rules of asset-pipeline.md 1.2; I where it says so there]: `VPUI_2.Civ5Pkg` (priority 250, UISkin
`Expansion2Primary`, platform Common) names `Core` as a Skin `<Directory>`, registered with flags 7, so its loose
`Highlights.xml` beats every pack copy; `EUI_2.Civ5Pkg` (also priority 250) names `Core` too but has no `Highlights.xml`.
`CultureBorders.xml` and `HexGrid.xml` exist only in the base file system (loose `Assets/Overlays/`, flags 1, against
`MiscData.fpk`, flags 0 first mount), so the pack wins. **The pack and loose copies of `CultureBorders.xml` and
`HexGrid.xml` differ only inside the XML comment** (an ASCII drawing: `-` against `_` on one and two lines) [C, line diff]:
their parameters are the same whichever copy loads, so the open "[I] not traced" costs nothing. The loose
`Assets/Overlays/Highlights.xml` is the pack's 14 styles plus `TradeRoute` (and blank lines). The live count 43 after
the load (disable-systems-and-masks.md 5, 0127 arm C) equals VPUI's file and no other copy: **P13 holds** [C files, L
count].

Parameters (the loaded copies):

- `CultureBorders.xml`: `primary_width` 1.4, `secondary_width` 20, `primary_aa_width` 0.5, `secondary_aa_width` 0.3,
  `primary_alpha` 200, `secondary_alpha` 90, `corner_bias` 0.105; city-state (`cs_`) set: `cs_primary_width` 2.8,
  `cs_outline_width` 0.4, `cs_aa_width` 0.5, `cs_primary_alpha` 130, `cs_outline_alpha` 160, `cs_dash_width` 8;
  `randomness` 6, `expansion` 2. The comment draws a cross-section: a primary band (primary colour) on the border line,
  then a wider secondary band (secondary colour) inward.
- `HexGrid.xml`: `color` 255,255,255,75, `width` 0.2 (the comment: width runs 0.0-1.0 from the hex edge toward its
  centre).
- `Civ5ArtDefines_Overlay.xml`: one element `UnitSelectionHighlight`: colour (0.9, 0.9, 0.45, 1), `Pulse` true,
  `PulseSpeed` 2, `Rotation` clockwise, `RotationSpeed` 1. **The selection highlight is animated** (pulse and rotation):
  a still camera with a unit selected does not give equal shots (see P14, 9.3).
- VPUI `Highlights.xml`, 43 styles, each with a type, a width, a texture and an RGBA colour (0-255): 8 HexContour,
  17 SplineBorder, 14 FilledHex, 3 StaticTexture and 1 HexModel. The city screen's tile states (worked, owned, buy,
  vacant, enemy) come as pairs of a FilledHex fill and a SplineBorder outline in one colour; movement, fire and
  ranged-attack ranges, group and influence borders and the route planner are SplineBorder styles; the editor's
  three styles, the default style and the border, trade-route and culture highlights are HexContour. The styles and
  their parameters are in the player's own `Highlights.xml` (which copy loads, with its hash: 9.4), which the product
  may read at run time (D83); no product code reads it today. (A 29-row transcription of the file was replaced by
  these counts, 2026-11-18, D83 audit.)

  Five style types (corrected 2026-11-18: was "Four"; the list names five): HexContour (a ring at the hex edge, width
  as a share of the radius), FilledHex (a flat fill), SplineBorder (a smoothed outline around a group of hexes, width in world units [I]), StaticTexture (a texture on the
  hex), HexModel (a model on the hex; only the sample style). Styles a sender passes with its own colour (the Lua
  `Vector4`) override the file's colour for HexContour styles [I: `Culture` has none in the file but is drawn green
  in 3311's stock arm with Vector4(0, 1, 0, 1)].

## 10. As built: the overlay in the server (task 3704)

Written 2026-11-14 15:05 EST by 0167-W4 (task 3704, branch `task-3704-overlay-input`; commits c8432445 render input,
7524f092 scene, 152eca1a wrap matching, 59fbbb0b fatal hook). Code against the E2 reader interface
(`server/src/render/srv_overlay_tap.h`, the stub until 3703 lands); no live run yet (that is 3706's Tiny load).

### 10.1 The render modules

- **`c5r::terrain`** (`render/include/c5r/terrain/terrain.h`): `Options::overlay` = `{srv, proj[3][16]}`. `srv` is bound
  as TerrainShader t1 (must be a Texture2D `R8G8B8A8_UNORM` view, else `Err::BadLayout`); `proj[k + 1]` is the
  OverlayProj of the EXE view that shows wrap copy k (k = -1, 0, +1) as the EXE stores it. The module draws its wrap
  copies with TileWorld x shifted by k x samplesX x sx, so it binds `Translation(-k x samplesX x sx) x proj[k + 1]`
  (`overlayProjForCopy`): the copy samples the texels the EXE's view gave the original ground. TerrainFrame is uploaded
  once per copy when an overlay is set; OverlaySize = the bound texture's width. `srv` null (the default): the 1x1
  (0,0,0,0) and identity matrices, the picture before 3704 **bit for bit** [C: test `draw_overlay_default_is_bit_equal`,
  0 bytes differ]. Every matrix is checked finite whether or not a view is bound.
- **The DS's overlay coordinate** [C, test `draw_overlay_wrap_copies_take_their_view_matrix`, 2026-11-14, hardware
  device on the workstation]: u = 0.5 x NDC.x + 0.5, NDC = world x OverlayProj (row vectors; the world position after
  TileWorld). The composition is premultiplied as section 3 says [C, test `draw_overlay_composes_premultiplied`: 3072 of
  3072 pixels within 2/255 of colour x (1 - a) + rgb].
- Only tiles drawn through the EXE's pixel shader (tiles with a cache) show the overlay; the module's own fallback PS
  ignores t1, as before.
- **`c5r::water`** (`render/include/c5r/water/water.h`): t9 through the existing `setTexture(Slot::Overlay, srv)`;
  `FrameInputs::haveOverlayProjWrap` + `overlayProjWrap[2][16]` give draw()'s views 1 (-width) and 2 (+width) the EXE's
  own matrices unchanged (the water module moves the view, not the geometry, as the EXE does, and its width is the EXE's
  3583). Without them every view takes `overlayProj` (the behaviour before). Rivers take view 0's frame and the shared
  t9. [C: test `renderer_overlay_matrices_per_view_and_t9`.]

### 10.2 The scene (`server/src/render/srv_render_scene.*`)

- **Option** `render_overlay` (server.cfg; `HostConfig::overlay`): 1 (default) = an `OverlaySource` over the tap's
  reader (`MakeTapOverlaySource`); 0 = no source, t1 / t9 transparent.
- **Every loop iteration** (`Scene::poll`): `acquire()`; a pair with a new generation, slot or view asks for a redraw
  (a highlight can change under a still camera); a lost pair (acquire false) redraws once without it. A stale pair (the
  reader keeps returning the last good one) stays bound (D132; the reader counts it).
- **Wrap copies** (`OverlayCopiesFromFrame`): copy 0 = view 0's bytes; a view v > 0 whose x / y members ([0], [1], [4],
  [5], [13]) equal view 0's within 1e-4 relative and whose x translation is k x wrapPeriod (within 0.5 world unit; the
  camera block's `wrapPeriod` = 0xD90FE4 = 64 W - 1, 3583 on Tiny) is copy k, taken as the EXE wrote it. **The z / w
  members are not compared**: the EXE's wrap view differs from view 0 there by up to 1.5e-5 [L: 3701's read of 0xDA1690
  and +0x40, stock A, 2026-11-14 13:14: [14] 0.4999847 against 0.5, [10] -0.49999997, [15] 0.99999994]; a comparison of
  every member rejected the real wrap view (found in review before any live run; test case `copies` holds those bytes).
  A copy no EXE view shows (the EXE does not draw it) takes view 0's matrix moved by k x wrapPeriod, counted as
  `derived`; a view that fits no copy is counted `unmatched` and LOUD once per episode. A pair with numViews 0 or above 4,
  a non-finite matrix or a zero scale is not bound (the frame is drawn without the overlay, LOUD once per reason, `bad`
  counted).
- **Water**: `WaterSceneOverlay(&water, srv, proj)` before each draw (a free function: `srv_water_scene.h` was not
  3704's; it moves into `WaterScene` when that header is next changed); the water scene sets t9 and the per-view
  matrices from it.
- **Counters**: per second `overlay=<state> bound gen pairs stale refused derived unmatched bad`; totals `frames with /
  without, pairs, derived copies, unmatched views, bad pairs, failed`, and since 2026-11-14 18:09 (MC #891) `stale
  episodes N longest P Presents refused R`. Since issue 0194 (2026-11-14) an episode is counted in map frames, not
  Presents (section 11.1); P stays the episode's length in Presents.

### 10.3 The failure route (decision 5, MC #701 / #713, D132 R6) [C code + stand-in tests]

- **Finding (2026-11-14 13:50, against MC #701 (a) as first worded)**: a server-side call failure that comes back as a
  RemoteError reply **never reaches the proxy's Fatal(3)**: `proxy/src/proxy_rt.cpp` `CallFailed` logs it and returns
  the default ("the server's handler refused the call"). Fatal(3) and D125's report follow only transport failures (the
  server gone or not answering) and a failed boot. So "fail the next call" cannot end the game; the server process has
  to end. MC #713 (13:50) granted that route and the two shared lines; MC #746 (14:59) confirmed it, no proxy change.
- **As built**: the scene turns the source's `State::Failed` (VPOVL_FAILED: the tap could not install at game start, or
  a section layout the reader refuses) into one LOUD line `overlay tap FAILED (VPOVL_FAILED reason N NAME): <the
  reader's lastWhy>; ...` and `Scene::overlayFailed()`. The host's loop runs `OverlayFatalStep` after every poll: once,
  it calls `HostConfig::fatal(5, text)`; server.exe sets `cfg.fatal = &SrvRemote::ReportAndExit` in `StartRenderer`
  (`server/src/remote/srv_remote_main.cpp`): the server writes its report and exits with **code 5** (2 = an exception,
  3 = the engine is gone); the proxy's next call fails, giving the server-death report (its log tails carry server.log's
  FAILED line with the reason; its details line says "exit code 5") and Fatal(3) in a D33 session (D125: popup, report,
  end of game, config.ini restored). A stale or missed copy during play keeps the source Active and never calls the hook.
  Without a hook (tests) the failure is LOUD once and the scene draws on without the overlay.
- Tests: `server/tests/render/overlay/test_scene_overlay.{cpp,py}` (built by `test_scene_build.py`), 8 cases on WARP with
  the EXE's terrain programs and a fake source: `copies`, `todays_picture` (the in-tree reader and render_overlay=0 give
  the same picture, nothing bound, 0 LOUD), `pair_bound`, `failed_is_fatal_once`, `states_transparent`, `bad_pair`,
  `option`, `fatal_hook` (never for a stale pair in play; once, code 5, the reason named, for an install failure).
  Red on main from ea8878286 (2026-11-22) by a false `LOUD: extras` line, fixed in the scene's extras gate: fog-of-war.md 7.7
  [C, 2026-11-14 15:01, 8 of 8 PASS]. Not verified: the chain live (popup, report, exit) - 3706's negative test;
  server.exe was not rebuilt by 3704 (the lead's build after 3703's merge).

### 10.4 Not done in 3704

- **Done B (2), the offline replay of a captured t1** (terrainshot): no export of a capture's terrain draws exists
  (3601's exporter writes water draws only, without after-targets) and watershot's replay refuses hull and domain
  shaders; moved out of 0167 as ticket 0182 (MC #746, 2026-11-14 14:59). The binding's exactness rests on the render
  tests above and 3706's live comparison.
  *Corrected 2026-11-14 by 0182: 3019's export and replay tools (landed after 3704) already covered the tessellated
  terrain draws and t1; promoted to `render/tools/terrainreplay/`; done B (2) measured in `renderdoc-capture.md` 9.*

## 11. As built: the tap and the reader (task 3703, 0167-W3, 2026-11-14; branch `task-3703-overlay-tap`)

Code: producer `shell/composite/overlay_tap.cpp` (called from `shell_composite.cpp` at every Present of the target, after
the camera publish, and from the OMSetRenderTargets hook for the verify mode); reader
`server/src/render/srv_overlay_tap.cpp` (`srvrender::OverlayTapReader`, used by 3704's scene); layout
`VPShellOverlayTap` / `VPShellOverlaySection` / `VPShellOverlaySlot` in `shell/common/shell_shared.h` (the lead's E2
contract, offsets kept; 3703 added `missedTotal` at +60, `faultsTotal` at +64, the slot's `texEpoch` at +304 and the
reader-only reason `VPOVLF_VERSION` = 8, all in reserved bytes). Tests: `shell/tests/test_overlay_tap.py` (stand-in),
`server/tests/render/overlay/test_reader_overlay.py` with `test_reader_tool.cpp` / `test_reader_build.py`.

- **EXE checks before the first read** [C, llvm-objdump of the Ghidra input copy, SHA-256 b2ce96ae...; checked against
  the installed EXE by `test_overlay_signatures_match_exe_on_disk`]: eight byte patterns, each a description of what the
  instruction does: the Overlay start-up storing the handle of a newly created 2048 x 2048 target into `0xDA12A4`
  (0x57D2AC); the overlay render 0x6EAD50 binding `[0xDA12A4]` (0x6EAD96) and looping over `g_NumViews` 0xD90FA4
  (0x6EADAE); the render copying 16 dwords from `0xDA1690` into view 0's constants (0x6EAE36); the ortho block 0x51B450
  building `OverlayProj[0]` at `0xDA1690` (0x51B6D4) and `OverlayProj[v]` at `0xDA16D0 + 0x40 (v - 1)` for 1 <= v <
  `g_NumViews` (0x51B738, 0x51B74B: stride 0x40, bound `g_NumViews`, no upper bound in the EXE); the view table write of
  the texture creation (0x8BC0CA, as `bake_tap.cpp`). A mismatch refuses the EXE: `VPOVL_FAILED PATTERN`.
- **Per Present** (render thread): (1) the slot whose event query signalled is published (slot seqlock, `valid` 1,
  generation + 1, then the block's `newestSlot` / `generation`); (2) on a map frame (camera `VPCAM_OK` and the frame
  executed the scene-clear list; `overlay_need_scene=0` drops the second condition for the stand-in) and with no copy in
  flight: `[0xDA12A4]` and its view-table entry are re-read (one 4-byte and one 8-byte guarded read; re-resolved only
  when they changed, and after every load = camera `gridSeq` change); the view object's vtable is checked against a
  D3D11 texture, SRV and RTV made by the tap (so under RenderDoc the tap fails HANDLE, as P6 predicted); its texture must
  be 2048 x 2048, 12 mips, array 1, 1 sample, R8G8B8A8 family; `g_NumViews` must be 1..4; `OverlayProj` (64 B per view)
  is read; a slot is claimed (below); `CopyResource` of all mips and `End(query)`; the slot carries the camera block's
  `presentCount` of this Present. (3) The block is written (seqlock). One copy in flight at a time; a map frame that
  finds the previous copy still in flight is a missed copy (counted, LOUD).
- **Slots and the hand-over** (three, `VPSHELL_OVERLAY_SLOTS`, MC #703: about 67 MB of video memory on the EXE's device):
  textures `R8G8B8A8_TYPELESS` (the target's own format, required by `CopyResource`), 12 mips, bind SRV, `SHARED |
  SHARED_NTHANDLE`, named `Local\VPShellOverlayTex.<pid>.<slot>`; section `Local\VPShellOverlay.<pid>` (64 + 3 x 336
  B). The producer never writes the newest published slot or the slot the reader names in `inUseSlot`; with one copy in
  flight there is always one candidate. Race rule (Dekker style, full barriers on both sides): the producer makes the
  slot's seq odd and `valid` 0 **before** it reads `inUseSlot` (and gives the slot back if the reader named it); the
  reader writes `inUseSlot` **before** it checks the slot's seq, `valid` and generation (and goes back to its old pair if
  the slot moved on). Either the producer sees the slot named or the reader sees it invalid. **Not solved [I]:** there is
  no GPU fence between the EXE's device and the server's; a slot the reader stopped naming can be rewritten while the
  server's GPU still samples it from an earlier frame. At rest the overlay is the same frame to frame, so a torn read
  would differ only where the overlay changed; a keyed mutex would close it at the cost of a CPU wait on both sides.
- **Failures** (decision 5, MC #701, D132): before the first published copy of the process, a pattern refusal, an
  unvalidated EXE, a failed section or slot texture, an access violation, or `overlay_fail_frames` (30) consecutive map
  frames without a resolvable target (handle 0, no D3D11 view, wrong description, `g_NumViews` out of range) publish
  `VPOVL_FAILED` with the reason and one LOUD line `overlay_tap: LOUD: FAILED <reason> at the install: ...`; the state is
  final for the process. The reader's `state()` becomes `Failed` with that reason and `lastWhy()` = "the overlay tap
  could not install: VPOVL_FAILED <reason> ..."; 3704's render start turns it into the error the proxy maps to Fatal(3).
  After the install, the same problems, a missed copy and an exception are LOUD (a new text at once, a repeated text
  every 1000th time, 200 lines per process), counted in `missedTotal` / `faultsTotal`, and the tap goes on; the reader
  keeps the last good pair and counts one `stale` episode after 30 target Presents without a new generation (*corrected
  2026-11-14 18:09 by lead-0167 #3, MC #891 and 3706 item 6: an episode now also needs at least 250 ms without a new
  generation, `VPOVL_READER_STALE_MS`, because in AI turns the EXE presents about 250 times a second and 31 Presents
  (0.12 s) is a normal copy latency [L, W5 #904: 43 such episodes in 30 quiet AI turns, every one at 31 Presents]; and a
  stale episode is a lag, not a fault: logged in full for the first 3 episodes (`VPOVL_READER_STALE_FULL_LINES`), then
  counted only, never LOUD; the count and the longest ended episode in Presents are in the totals line. Replaced:
  "counts one stale episode after 30 target Presents", with one LOUD line per episode*). (*Corrected again 2026-11-14 by
  issue 0194, section 11.1: the 30 are counted in **map frames** (the block's `copiesTotal + missedTotal`), not in
  Presents of the target; Presents without a map frame, such as an AI leader screen, are noted as `noMapGaps`, not
  counted as stale. Replaced: "30 target Presents".*) A slot
  that fails a reader check (views, a non-finite matrix, size, mips, format, the shared texture) is refused (counted,
  LOUD once per text), never bound; a section layout the reader does not know is final (`VPOVLF_VERSION`).
- **Kill switch** `overlay_tap=0` (shipped `overlay_tap=1` in `shell_composite.ini`): slots and section released, state
  `OFF` (fail reason `KILLED` when copies had been made); `=1` again starts the tap anew (copies resume).
- **Verify mode** `overlay_verify=N` (diagnostic, blocks the render thread): every N-th copy is followed in the same
  Present by a second `CopyResource` of the target and one of the slot into staging textures, mapped and compared byte
  for byte over all 12 mips (22,369,620 B); and the OverlayProj array read at the Present is compared with the array read
  in the OMSetRenderTargets hook when the EXE binds the target's view on any context in that frame (the moment the
  overlay render copies `0xDA1690` into its constants).
- **Stand-in results** [L on the stand-in, 2026-11-14 15:01-15:18, workstation, `test_host --fake-exe`,
  `overlay_test=<mode>`; branch `task-3703-overlay-tap` at b376b3bf; plain build and MSVC ASan build both 20/20, reader
  tests 35/35 in both]: 60 consecutive pairs read by the reader while the producer copied at the stand-in's uncapped
  rate (thousands of Presents a second) were each one frame (texel (0,0) of mips 0 and 5 and OverlayProj element 12 carry
  the same frame stamp), generations, frames and presentCounts strictly growing, at least two slots used; the verify mode
  found 0 differing bytes of 22,369,620 in every checked copy. At that rate about half the Presents find the previous
  copy still in flight (counted as missed, LOUD rate-limited), which the live 30 fps frame should not show [I; the live
  count is in section 8]. The tap's CPU time in `CopyResource` + `End` on the stand-in: mean 0.003-0.005 ms, max
  0.05 ms per copy (ASan build; an indication only, COMMON 23). Every install failure (handle 0, a non-D3D11 view object,
  1024 x 1024 / 11 mips, 1 mip, B8G8R8A8_TYPELESS (90), g_NumViews 0 and 9, a fault in the view object, an uncreatable
  section) gave `VPOVL_FAILED` with its reason after `overlay_fail_frames` and one LOUD line, and the reader `Failed` with
  that reason; the target lost after the install gave LOUD rate-limited lines, `faultsTotal`/`missedTotal` counting and
  the block still ACTIVE, the reader keeping its pair and counting one stale episode; the kill switch at start and during
  copies (OFF, KILLED) and on again; the real path refused the stand-in's fake EXE (PATTERN) and an unvalidated image.
- **Live, done B (1)** [L, 2026-11-14 15:30-15:36, workstation, instance c5h-c, the D33 product path of 3308's driver
  (`shell/evidence/0167/3703/run_3703.py`: proxy, server.exe and composite from `task-3703-overlay-tap` with the team
  branch at 135980f8 merged in, so 3704's scene ran with this reader; server.exe 6942DA4B..., proxy BE5BDC0C..., x64 DLL
  9D5F19BC... built by `server/build_server.py --config debug` against the lead's deps of pin 04b1211b8; composite
  6B66FFF2...; config.ini D33 by darkred.py's list with Overlay on (15 keys, the 3308 driver's form, not the 13-key A''
  form); save `C5H-0167-TINY_0008` (copy of c5h-9028-tiny-t0008, SHA-256 b2ceb663...), turn 8; `overlay_verify=30`; one
  load and one reload]. 577 checked copies: **every one equal to a second read of the EXE's target in the same Present,
  22,369,620 bytes over 12 mips, 0 bytes different**, in all three states (plain 66 checks, highlight 60, selection 69,
  the other 382 at the load, the reload and between states); **OverlayProj (2 views) read at the Present equal byte for
  byte to the array read when the EXE bound the target in that frame in 576 of 577** (the first check had no bind before
  it). Run 1 (15:23-15:28, before the bind match was by resource) gave 437 texel-equal checks of the plain state. The
  target's handle was 0x247 in both runs (the 3701 D33 run also saw 0x247; stock 0x248) and was resolved again after the
  reload (same handle and views). The EXE bound the target 6 times per Present interval (counted in verify mode); the
  bind uses a render-target view that is **not** one of the two views in the handle's table entry (no bind matched them
  by pointer; matched by resource since 6B66FFF2). The server's reader (3704's scene): ACTIVE from the first copy,
  all three slot textures opened, 18,326 pairs bound, 0 stale, 0 refused, 0 LOUD lines in render.log; the diagnostic
  pictures (Visual Checks, issue 3703, three entries of 15:33) show the server's terrain with the "Culture" contours, the
  MovementRangeBorder outline and the culture border drawn from the tapped target. Composite: 1 LOUD line (the first
  missed copy; 47 copies of about 17,000 missed because the previous copy was still in flight, at a frame rate near 100
  per second: this run was not capped at 30 fps); CPU in `CopyResource` + `End` per copy mean 0.0019 ms, max 0.22 ms (an
  indication, COMMON 23; the verify copies themselves block 9-23 ms and are diagnostic only).

### 11.1 The stale episodes in AI turns: AI leader screens, not a late event query (issue 0194, 2026-11-14)

**Cause [L, offline from 3706 item 6's archived records, 2026-11-14 19:10-19:30].** Every stale episode the reader
counted in 3706's huge-save AI turns is a run of Presents **without the scene-clear list** (lockstep CSV `kind` 4,
`noins`): the EXE draws no world in those frames, so they are no map frames, the tap issues no copy (and counts no
missed one), and the newest generation cannot move. The copy's event query is not late. Script
`shell/evidence/NEW-overlay-stale/stale_vs_noins.py RUN_DIR`: for each `stale` line of render.log it takes "copy of
Present P" and the lockstep CSV's last segment (`shell_composite.lockstep.csv`, one row per Present of the target) and
measures the run of `noins` frames that starts at frame P.

| Run (records in `D:/c5-64bit-work/0167/captures/3706/runs/`) | Episodes | Explained by a `noins` run starting at the copy's Present | `noins` run per episode, frames / ms | Copies resumed after (render.log) |
|---|---|---|---|---|
| `huge-turns-on2` (the quiet ON arm, 17:16-17:35) | 43 | **43** | 293-531 / 564-966 ms | 0.58-0.97 s |
| `huge-turns-on` (run 1, machine loaded by 0158, 16:14-16:38) | 40 | **40** | 204-464 / 569-957 ms | 0.46-0.95 s |

In both runs, in all 83 episodes, the `noins` run starts at the CSV frame number equal to the last copy's Present P
(the script allows 2 frames; it needed 0). The quiet run's 158,344 frames hold 45
`noins` runs (19,348 frames), 44 of them longer than 30 frames; the loaded run's 120,607 frames hold 45 runs (9,818
frames), 41 longer than 30. So 3706's "34-55 Presents" (run 1) and "every one at 31 Presents" (run 2) were the
moments the old rule fired (31 Presents and 250 ms after the last copy), not the episodes' length: **every episode
lasted 0.5-1.0 s**, the length of the run of frames without a world.

**What the `noins` runs are [I, strong]:** AI leader screens (diplomacy, trade, discussion), answered in about 0.9 s
by the harness's in-game handler (`tools/harness/diplo.py`, armed by `turns.py` in every AI turn of the driver).
insertion-point-and-frame-structure.md 9.2 found these screens to be the only frames of AI turns without the
scene-clear list (W7: 19,511 frames over 42 screens in 30 AI turns on the same huge save, 464 frames per screen; here
19,348 `noins` frames in 45 runs, 430 per run, 1.5 runs per turn). The run records do not hold the handler's screen log,
so the match of each run to a screen is not shown per run.

**Consequence.** During a leader screen the composite inserts no server frame and the EXE renders no overlay; when the
map returns, the first map frame copies the target and the next Present publishes it. The overlay is not late against
the world on screen: the episodes were not a lag. Done E's "0.4-0.7 s without a new copy" of 0194's title, and
section 12.6's "[I for the cause]" (a normal copy latency at 250 Presents per second; a query polled with DONOTFLUSH),
are corrected by this: the cause is the frame kind, not the query.

**The fix (reader, `server/src/render/srv_overlay_tap.cpp`):** the stale threshold counts **map frames**, not Presents:
the block's `copiesTotal + missedTotal` (at every map frame after the install the tap issues a copy or counts a missed
one), so an episode is more than `VPOVL_READER_STALE_MAP_FRAMES` (30) map frames without a new generation and at least
`VPOVL_READER_STALE_MS` (250 ms). A real lag (a late query, a lost target: map frames go on, missed copies count up)
still counts; Presents without a map frame are noted (`noMapGaps`, the first 3 in render.log: "N Presents of the
target, M ms, with 0 map frames since the last copy (frames without the scene-clear list, such as an AI leader
screen): not a lag, not counted as stale") and never counted. No layout change: both counters were already published.
Replaced: `VPOVL_READER_STALE_PRESENTS` (30 Presents of the target).

**Diagnostics added to the tap (`shell/composite/overlay_tap.cpp`):** `overlay_trace=1` writes one row per Present into
`shell_composite.overlaytap.csv` (VPSHELL_OUT_DIR, else the DLL's folder): present, qpc, map frame, the in-flight
copy's poll result (-1 none, 0 not signalled, 1 signalled now), its age in Presents and ms, the action (C copy, M
missed: previous in flight, S no slot, P target problem, N not a map frame), generation, missed total. Test modes
`overlay_test=leader` (a window of Presents without map frames) and `overlay_test=latequery` (a window in
which the in-flight query reads as not signalled) reproduce both cases on the stand-in
(`shell/tests/test_overlay_tap.py`: `test_leader_screen_frames_are_not_stale`, `test_late_query_is_counted_stale`;
reader-only: `server/tests/render/overlay/test_reader_overlay.py` `test_reader_presents_without_map_frames_are_not_stale`).
(Corrected 2026-11-20, ticket 0607: the window was 3 s of wall clock from test frame 40 and made both tests flaky; it
is now a handshake with the reader, section 11.2.)

**The copy's event query, measured per Present [L, 2026-11-14 19:38-19:45, workstation, c5h-a, `C5H-0167-HUGE_0350`
turns 350-355 = 6 AI turns with 3706's per-turn stimuli, configuration A'' (D33), release server.exe d07cf595 / proxy
49a5d0fc / composite 8d8413bd (this branch, `overlay_trace=1`), x64 DLL 74588135 (pin 04b1211b); other teams' games
and servers ran on the machine (0175's server, impl-B-0192's c5h-b game, 0188's render tests), no capacity series;
`trace_summary.py` over 26,686 Presents]:**

| Figure | All | Player-turn windows (7 s from each stimulus) | AI-turn time |
|---|---|---|---|
| Presents / map frames / not map frames (`N`) | 26,686 / 23,502 / 3,184 | 1,970 / 1,970 / 0 | 24,716 / 21,532 / 3,184 |
| Copies issued / missed, previous copy in flight (`M`) / no slot / target problem | 23,353 / 149 (0.63 %) / 0 / 0 | 1,939 / 31 / 0 / 0 | 21,414 / 118 / 0 / 0 |
| Copy signalled at the next Present / 2nd / 3rd / 4th | 23,210 / 137 / 3 / 2 | 1,911 / 26 / 1 / 1 | 21,299 / 111 / 2 / 1 |
| Age at signal in ms, median / p99 / max | 3.9 / 83 / 1,143 | 33.1 / 33.8 / 95 | 3.8 / 85 / 1,143 |
| Longest wait of a copy in flight (last unsignalled poll) | 3 Presents, 103 ms | 3 Presents, 18 ms | 3 Presents, 103 ms |
| Longest gap between generations, map frames / Presents | 4 / 470 | 4 / (a window edge) | 4 / 470 |
| Gaps the old rule counted (> 30 Presents and 250 ms) / the 0194 rule counts (> 30 map frames and 250 ms) | 9 / **0** | - | - |

So the query of a copy issued at Present N is read signalled at Present N + 1 in 99.4 % of copies and never later
than N + 4: the GPU is at most a few frames behind, DONOTFLUSH polling is not a cause (the copy is issued before the
EXE's own Present, which flushes the context), and three slots are enough (0 copies missed for want of a slot). The
missed copies (`M`, 0.63 %) are map frames that come while the previous copy is one to three Presents old; each costs
one map frame of extra age (about 4 ms at the AI-turn rate, 33 ms at the 30 fps player-turn cap), at most three in a
row. The age of 1,143 ms with a signal at the next Present is one long EXE frame (a 1.1 s Present interval), not the
query. **No tap change is made for them** (ticket item 2 "fix the cause found": the late query is not the cause): a
fourth slot or two copies in flight would put a second 22.4 MB copy on the GPU exactly when it is behind, for at most
three map frames of age. The reader in this run: stale 0, 3 noted gaps (82-117 Presents and 250 ms with 0 map frames,
the first three of the run's 9 `noins` runs longer than 30 frames, 3,182 `noins` frames), 0 LOUD lines in render.log, 1
rate-limited `missed copy` LOUD line in the composite's log. A first 6-turn run (19:30-19:36, the same builds without the
trace's flush, so its trace was lost at the launcher's stop) gave the same reader result: stale 0, 9 `noins` runs over 30
frames, 3 noted gaps of 52-96 Presents; the composite counted 418 missed copies of 20,000 (2.1 %, other teams' jobs ran).
Diagnostic only (the trace's writes are in the frame): the done-E pair below runs without it.

**The missed-copy line is no longer LOUD (MC, 2026-11-14 20:08, after 0192's huge runs showed one rate-limited
`overlay_tap: LOUD: missed copy: the previous copy is still in flight` per arm).** A map frame that finds the previous
copy in flight is a counted lag of one map frame (above), not a fault: since this issue it is counted in `missedTotal`,
the first 3 per process are logged as `overlay_tap: missed copy (lag, counted, D132): the previous copy is still in
flight, N Presents old (missed M, copies C)` (the third adds "further ones are counted only"), and the per-1000-copies
line carries the running count; never LOUD. Copies missed for a fault (no free slot, the target lost or changed after
the install, an exception) stay LOUD and rate-limited as before. Stand-in: `test_pairs_exact_and_consecutive` (no LOUD
line at all at the uncapped rate, at most 3 in-flight lines) and `test_late_query_is_counted_stale` (exactly 3).
Replaced: every missed copy LOUD (a new text at once, a repeated one every 1000th time).

**Done E's pair rerun with the 0194 reader [L, 2026-11-14, workstation, c5h-a, `C5H-0167-HUGE_0350` turns 350-379 = 30
AI turns each, 3706's driver and per-turn stimuli unchanged (`shell/evidence/NEW-overlay-stale/run_0194.py`,
`chain.sh on off`), configuration A'' (D33), `lockstep_csv=1`; release server.exe d07cf595 / proxy 49a5d0fc (this
branch), x64 DLL 74588135 (pin 04b1211b release deps of 9045), composite 8d8413bd (this branch at 7eea91ca: the reader
rule and the trace code with the trace off; built before the missed-copy logging change, so its in-flight misses are
still LOUD here); ON 19:45-20:06, OFF (`overlay_tap=0`) 20:06-20:24; no capacity series ran; other teams' work did in
both arms (ON: impl-B-0192's c5h-b game until about 19:58, 0175's x64 server, 0188's render tests; OFF: 0175's c5h-b
game from 20:11 and its server), so the machine was shared alike, not quiet; `pair_summary.py`]:**

| Figure (30 AI turns) | **ON** | **OFF** |
|---|---|---|
| Turns, end, stop rc, EXE crashes | 30, max turns, 0, 0 | 30, max turns, 0, 0 |
| **Reader stale episodes** / noted gaps without map frames | **0** / 3 (first 3 noted) | - (source OFF) |
| Runs of frames without the scene-clear list (> 30 frames) / such frames | 45 (44) / 14,902 | 45 (44) / 18,592 |
| Lockstep exact / older / none / nopub / late | 116,408 / 2,106 / 0 / 0 / 0 (98.2 % exact) | 128,764 / 2,910 / 0 / 0 / 0 (97.8 %) |
| Lockstep giveups / recoveries | 12 / 12 | 16 / 16 |
| Server frames drawn | 136,641 | 151,916 |
| Terrain GPU ms per frame, mean / max of the per-second values (indication) | 1.09 / 7.1 | 1.24 / 7.9 |
| Turn wall s, median / max (indication) | 31.6 / 41.8 | 29.3 / 35.5 |
| EXE private / largest free after the load (MB) | 1,388.3 / 1,906.3 | 1,405.1 / 1,887.1 |
| EXE private / largest free after the last turn (MB) | 1,546.9 / 1,729.1 | 1,535.8 / 1,737.7 |
| Server private after the load / last turn (MB) | 2,128.8 / 2,232.1 | 2,123.4 / 2,234.8 |
| Tap copies / missed (previous in flight) / faults | 120,000+ / 1,297 (1.1 %) / 0 | - |
| LOUD lines render.log / server.log / composite | 0 / 0 / 2 (the in-flight missed-copy line, first and 1000th; not LOUD since 327b6b28) | 0 / 0 / 0 |
| Gray frames after the bake-ahead | 0 | 0 |

**Reading:** with the overlay on, 0 stale episodes in 30 AI turns (3706's quiet ON arm: 43 on the old rule), with the
same 44 leader-screen runs in both arms; lockstep, the server's GPU time and both processes' memory equal the OFF arm's
within run-to-run noise (the exact share 98.2 against 97.8 %). The OFF arm drew 15,000 more frames (3,700 more of them
without the scene-clear list) and its median turn was 2.3 s shorter; 3706's quiet pair had 28.2 against 28.0 s with
the same tap, so this difference is not shown to be the overlay's: the two arms shared the machine with different
other jobs (above) [I]. The reader change itself costs nothing measurable (two counter reads per server frame). Turn
and GPU times are indications (COMMON 23: workstation, shared).

**Suites on the final build (this branch at 327b6b28; `shell/evidence/NEW-overlay-stale/build.sh`, `suites.sh`, builds
on `C:/c5-64bit-work/0194`, 2026-11-14 20:24-20:45) [L]:** `shell/tests/test_overlay_tap.py` (stand-in, 22 cases with
the two new ones) 22 passed plain and 22 under MSVC `/fsanitize=address`; `server/tests/render/overlay/test_reader_overlay.py`
36 passed plain and 36 under ASan; `test_scene_overlay.py` 3 passed (its 8 cases) plain and the 8 cases PASS under ASan;
0 skipped, 0 ASan report files. (The first scene run failed for want of `C5R_EXE` in the script's environment, not in
the code; rerun with it set.)

**Live check of the final composite [L, 2026-11-14 20:46-20:51, c5h-a, the same save, turns 350-352 = 3 AI turns,
composite b14cb069 (this branch at e848903a), the rest as the pair]:** 0 LOUD lines in the composite's log, render.log
and server.log; the composite logged exactly 3 `missed copy (lag, counted, D132): the previous copy is still in flight,
1-2 Presents old` lines, the third with "further ones are counted only", and its per-1000 line counted 237 missed of
11,000 copies; the reader: stale 0, 3 noted gaps without map frames; rc 0, stop rc 0, startup watch ok; restored by
hash, `provision.py verify c5h-a --quick` ok.

**Records** (D83, D133: never committed): `D:/c5-64bit-work/0194/runs/` (108 MB): `huge-diag1`, `huge-diag` (with
`shell_composite.overlaytap.csv`, the trace), `huge-on`, `huge-off`, `huge-check` (each: run.json, run.log, render.log,
server.log, the composite's log, the lockstep CSV, memory), `offline/` (the scripts' JSON outputs: on1/on2 of 3706,
diag2_trace, pair2), the build and suite logs.

### 11.2 Four timing-dependent tests made deterministic (ticket 0607, 2026-11-20)

Ticket 0607 (account B, branch `task-0607-overlay-tap-timing`) took four tests that failed on a loaded machine and
passed alone: the two 0194 window tests of `shell/tests/test_overlay_tap.py` and two tests of
`server/tests/render/test_render_host.py` (the renderer's own; written here because the ticket's research touch is this
file; the render notes `renderer-in-server-design.md` and `renderer-failure-modes.md` mention the earlier failures).
None of the four was a product fault; each test waited on a wall-clock length where it needed an event.

**(1) `test_late_query_is_counted_stale` and (2) `test_leader_screen_frames_are_not_stale` [C code, L runs below].**
Cause: the test mode's window (`overlay_test=latequery` / `leader`, `overlay_tap.cpp` TestFrame) was 3,000 ms of wall
clock from test frame 40, which the stand-in reaches a few ms after the install. The test then waited for the window's
log line, started `test_reader_tool.exe` (a process start, a D3D11 device, the section and texture opens) and read for
1.8 s. Its window of 1.8 s plus the start-up had to fit in the 3 s from frame 40; on a loaded machine it did not, the
copies resumed while the reader ran and it bound every new generation: `accepted` 9 to 91 instead of 1 (lead-0590's
counts in the ticket: main 4 of 5 failed alone, accepted 65, 9, 91, 46). Not epic 0590's carry (it failed the same on
b34cb5e35 and on main). Fix, in the test mode only (`g_test` leader / latequery; with no `overlay_test` the function
does nothing and `InTestWindow` is false, so the product path is unchanged): the window is a handshake through the
existing block. It opens at the first test frame >= 40 at which the tap is installed (the block ACTIVE, a pair to bind)
**and no copy is in flight**, so no generation can be published inside it (leader makes no copy; latequery's next copy
is never signalled inside it); it closes at the first Present after a reader has acknowledged a pair (`ackServerPid`,
which the reader writes after `inUseSlot`) and let it go (`inUseSlot` = 0xFFFFFFFF, the reader's `release()` at its
exit); a 20 s cap ends it if no reader comes. The start and end lines carry the tap's counters ("TEST leader window
starts at test frame N (copies C, missed M, generation G) ...", "... window ends at test frame N: the reader let go
(X ms; copies C, missed M, generation G)"), and the tests compare those instead of two block reads around the reader
(the old `b1` read after the reader's exit would race the resumed copies under the handshake). Leader: start == end
counters and the reader's one pair is the start's generation; latequery: generation unchanged, at most one copy issued
(the window's first map frame), missed up by more than 30.

**(3) `test_tile_source_asks_for_another_tile_size` [C code, L runs below].** Cause: `render_host_test.exe --seconds 6`
with a bounded wait (10 s) for "bake manifest seq 1 applied" after a bakefake start (its own wait 15 s). The host's 6 s
had to cover the field build, the bakefake's process start, the tile rebuild and the manifest; under ASan or a loaded
machine it ended first (0600's ASan run 05:36-05:42, 0454's milestone 2a run 09:3x). Fix: `rig.start_until()` and
`rig.stop(p)` (ticket 0496's pattern: `--seconds` only a bound of 120 s, 300 s under ASan), the waits bound to the
process (`p=`), the bakefake living until the rig closes it, its start-up wait `fx.WAIT_S` (was 15 s; every
`bakefake()` caller gets it, a bound only).

**(4) `test_resize_request` [C code, L runs below].** Cause: `--seconds 6`, and a fixed feed of 39 cameras at 40 Hz
(about 1 s) for a consumer that needs 4 frames: under ASan on a loaded machine it took 3 of 4 before the cameras ended
(recorded 2026-11-13 in `renderer-in-server-design.md`), or the host's 6 s ended first (0454 W7's run, #8096). Fix:
`start_until`/`stop`; the test waits for the host's "v3 sink: client requested 400x300" line before the first camera
(so every frame after it is drawn into the new generation); cameras go on in blocks of 10 until the consumer has its 4
frames (bounded by `fx.WAIT_S`); the consumer's own timeout is `fx.WAIT_S` (was 12 s).

A negative case joins (2): `test_window_without_a_reader_ends_at_its_cap` (no reader: the leader window stays open
with the generation and copies frozen well past the old 3 s, ends at 20 s with "the cap passed without a reader's
hand-back", copies resume).

**Runs [L, 2026-11-20, workstation, branch `task-0607-overlay-tap-timing` at 1af9a8fe0; shell, reader and render-test
builds in `C:/c5-64bit-work/0607/` (shell-build, reader, rt), plain (not ASan); runner
`C:/c5-64bit-work/0607/run_0607.sh`, each pytest start through `quietlock.py run` with `quietlock.py check` saved
before it].** The machine was busy throughout: 6 to 12 other owners' heavy jobs registered at each start (team-0042's
and 0455's render builds and ctest runs on F:, 0454's extras tests, a shell build; the cmake configure of this
ticket's first shell build on F: took 428 s, 11:15-11:22).
- Each of the four tests alone, 10 runs, 11:41-11:59 EST: **10 of 10 each**.
- The overlay windows of those 20 runs (the start, acknowledge and end lines): the reader acknowledged its pair 398 to
  4,106 ms after the window opened (median 1,329 ms) and the window lasted 2,210 to 5,931 ms (median 3,142 ms); **11 of
  the 20 lasted more than 3,000 ms**, so under the old fixed window each of those runs would have seen copies resume
  while the reader ran, which is the reported failure. All 20 ended with "the reader let go", none at the cap.
- Whole files, twice each, 11:59-12:25 EST (7 to 11 other heavy jobs at each start): `shell/tests/test_overlay_tap.py`
  **24 passed** (489.9 s) and **24 passed** (482.2 s); `server/tests/render/test_render_host.py` **76 passed** (257.0
  s) and **76 passed** (247.7 s); no skip. In the overlay file the windows lasted 2,284 to 5,328 ms (ended by the
  reader) and the cap test's window ended at 20,000 ms as designed.
- Not run: the ASan variants (`shell\build.cmd asan`, `build_render_tests.py --asan`) of the changed tests; the change
  is test-mode control flow and test code, the ASan suites of 0194 and 0600 cover the same paths.

**Earlier mentions of these flakes** (left as they were, read with this section): `terrain-decals.md` (0166's run:
"0194's latequery window: accepted 2 against 1, then accepted 113"), `renderer-in-server-design.md` (3306's
`test_resize_request` took 3 of 4 frames under ASan, 2026-11-13) and `renderer-failure-modes.md` (0600's ASan
`test_tile_source_asks_for_another_tile_size`, "a load-bound timing of that test [I]": confirmed here [C code]).

## 12. Acceptance (3706)

Task 3706 (0167-W5, branch `task-3706-overlay-acceptance`, 2026-11-14 from 15:19 EST). Drivers in
`shell/evidence/0167/3706/` (`run_3706.py` on 3702's `run_3702.py` and through it 3608 -> 3308; `pan_probe.py`;
`neg_d125_job.py` on `tools/testbed/jobs/release_d125.py`; `item8_suites.sh`). Pictures and run folders on C: (D133),
archived to `D:/c5-64bit-work/0167/captures/3706/` at the end, never committed (D83).

**The result in six lines** [L]: (1) the server binds the EXE's Terrain Overlay target exactly: 450/450 (Tiny) and
579/579 (huge) verified copies byte-equal over 12 mips, OverlayProj equal (12.1). (2) On the shipped product path the
borders over land and water, the city highlights, the selection ring and movement range, the fire range and the grid
now draw under D33 at 3702's cameras, ACCEPTANCE pictures against stock; what still differs on those pixels is the
intended classes (fog, decals, trees, dark models, lighting) (12.2). (3) The one-frame lag of decision 2 (i) leaves at
most 4.5e-4 of the screen uncovered during a fast pan (12.3). (4) D125's route for a tap that cannot install works live
(oldpc), a fault in play is counted and the game goes on (12.5). (5) On 30 huge AI turns the overlay costs nothing
measurable in lockstep, GPU time or memory against the overlay-off arm; done E is met as amended by the MC (#891) (12.6,
12.7). (6) Every suite of 3703 and 3704 passes plain and under ASan, 0 skipped (12.8).

**Configuration of every live run below unless named otherwise** [L]: workstation, instance c5h-a (VT present, the
instance's install), the joined D33 product path of 3308's prep (proxy + server.exe + composite beside the proxy,
VP64Options.ini `[Renderer] ServerRenderer = 1`, the repository's `shell_composite.ini` with `overlay_tap=1`); config.ini
in the shipped **A'' form** read back live from the instance (13 keys = 1: OverlayDebug, Decal, Forest, Combat, Particle,
Projectile, FOW, FluidFOW, YieldIcons, DistanceFog, Minimap, ColorKeyEdit, MovementPath; Overlay, River, Arrows, Waves,
Terrain = 0); **release** server.exe `b94fd398...` and proxy `2fbb45f9...` built from this branch (`server/build_server.py
--config release --deps` = a C: copy of 9045's release deps, so the x64 DLL is the pin's own `74588135...`, no DLL
link), vp64helper `719d2c71...`; composite `7c1a156d...` (`shell\build.cmd` of this branch before 3703's bind-by-resource
fix) for the Tiny runs of 15:36-15:55, `0480b9f4...` (with it) for every run from 15:57 on. Tiny save `C5H-0167-TINY_0009V` (copy of c5h-c's `C5H-3600-TINY_0009V`, 3702's Tiny
save, every plot revealed and visible); huge `C5H-0167-HUGE_0350` (copy of `C5H-VP8P-HUGE_0350`).

### 12.1 Item 1: done B (1) again on the merged branch

- Cited from 3703 (section 11, "Live, done B (1)"): 577 checks, 0 bytes different over 12 mips, OverlayProj equal in 576
  of 577 (debug builds, the 15-key D33 list). Done B (2) (terrainshot replay) is ticket 0182, outside the epic (MC #746).
- **Repeat on this branch, the shipped A'' form and the release build** [L, 2026-11-14 15:58-16:03, Tiny, c5h-a;
  composite `0480b9f4...` = this branch with 3703's bind-by-resource verify fix (team branch 067bafda merged);
  `overlay_verify=10`; states by the Lua channel (decision 6 (ii), diagnostic), 25 s each, the composite log sliced per
  state]:

| State (stimulus) | Verified copies | Texels equal (22,369,620 B, 12 mips) | OverlayProj at Present = at the frame's bind |
|---|---|---|---|
| plain (`Events.ClearHexHighlights`, `UI.ClearSelectionList`) | 75 | 75 | 75 |
| selection (`UI.SelectUnit` of unit 1002 at (50, 13), the capital's nearest land unit) | 75 | 75 | 75 |
| grid (`Events.SerialEventHexGridOn`) | 75 | 75 | 75 |
| plain_end (grid off, cleared) | 75 | 75 | 75 |
| highlight (WorldView Lua state: "Culture" on the capital, "MovementRangeBorder" on its 6 neighbours, "WorkedFill" on 2 plots; 9 sent), run of 16:11-16:14 | 75 | 75 | 75 |
| plain again in that run | 75 | 75 | 75 |

  **Item 1: 450 of 450 verified copies equal byte for byte over all 12 mips, and OverlayProj equal in 450 of 450**, in
  every state of done B (1) and the grid [L]. The first try of the highlight state (15:58 run) sent its chunk to the
  channel's default Lua state, where `Vector4` is nil (`attempt to call global 'Vector4' (a nil value)`): no highlight
  was set, so it was rerun in the WorldView state (the state 3701's stimuli used). An earlier run with the composite before 3703's fix (15:38-15:39)
  logged 185 texel-equal checks and "no bind of the target seen" for every OverlayProj check (the EXE binds the target
  through a render-target view that is not in the handle's table entry, section 11). Server: `overlay=ACTIVE`, stale 0,
  refused 0, bad 0, 0 LOUD lines in render.log.
- **The same on the huge save** [L, 2026-11-14 17:34-17:40, c5h-a, `C5H-0167-HUGE_0350`, turns 350-354 with item 6's
  per-turn stimuli, `overlay_verify=30`, the quiet machine; `binds_when.py`]: **579 of 579 verified copies equal byte for
  byte over 12 mips; OverlayProj equal in 578 of 579** (the first had no bind before it). The EXE bound the target in
  every verified frame: 53 of 53 in the player-turn windows, 507 of 507 in AI turns (up to 12 binds per Present interval
  in AI turns, 6 in the player's turn), 18 of 19 before the first turn: the EXE redraws the target every frame
  (section 3), so a "drew into the target" condition never marks a frame as unchanged. Lockstep stayed as in the
  overlay-off arm (turns 350-354: exact 2,810-3,857, older 23-107 per turn) even with a verify every 30th copy.

### 12.2 Item 2: ACCEPTANCE pictures against 3702's stock references (done C, D)

- **Capture** [L, 2026-11-14]: the harness's PrintWindow shot of the joined product path is often not the frame the
  player sees: in the first Tiny run 24 of 33 captures were rejected, 19 of them flat grey (84, 84, 84) and 5 others
  (the server's world alone, without EXE UI or models, and a few normal ones refused by the first band threshold of 20);
  in the retake 6 of 15 (2 flat); v2 (4 ticks in) failed 4 of 4 in the first run (proxy-design.md's side observation 2; 3702's huge arm 5 of 12).
  run_3706.py therefore takes up to two PrintWindow captures, keeps one only if it is not flat and its UI band (rows
  0-40) is within 35 levels of the stock picture's (normal captures 11-18, flat 53, UI-less 57-63), and otherwise takes
  **the composite's back-buffer dump** (`shell_composite.shot` trigger; the frame as presented, after composition, with
  the EXE's UI): checked by eye on v1 banner (16:05): UI, banner tooltip and the city all present. Every kept picture's
  Visual Checks entry names its source; every spoiled one is recorded as "PrintWindow artifact" (diagnostic).
  Side finding: the D33 dark-red models (0098/3009, an intended difference) are dark red in the back buffer but black
  in PrintWindow captures, so the two sources are not interchangeable on model pixels (the census masks models out).
- **Composite of the item-2 runs**: Tiny run 1 (15:45-15:55) `7c1a156d...`, Tiny accept2 (16:04-16:10, the spoiled cameras
  retaken) and all later runs `0480b9f4...` (= 3703's final tap; the copy path is unchanged by its verify-mode fix).
- **Tiny census** [L, `compare_map.sh tiny` = 3702's `compare_3702.py` unchanged on 3702's archived stock arm
  (`D:/c5-64bit-work/0167/captures/3702/tiny/stock`, 15:32-15:42) against this task's joined pictures (run 1 with the
  accept2 retakes), 2026-11-14 16:41; `C:/c5-64bit-work/0167/captures/3706/tiny/census.{json,md}`, difference images
  in `diff/`]. Largest channel difference on the mask: median / p95 / max (share > 12). "Before" = 3702's joined arm
  (the same path without the tap, section 9.3):

| Camera | Mask | Overlay px | Stock vs stock | Joined vs stock, before (no tap) | **Joined vs stock, with the tap** | Joined on vs off (before -> now) |
|---|---|---|---|---|---|---|
| v1_city_default | border band | 175,378 | 0 / 0 / 12 (0 %) | 20 / 106 / 251 (59 %) | **6 / 99 / 251 (37 %)** | - |
| v1_city_default | ground off the band | 2,200,113 | 0 / 0 / 12 (0 %) | 15 / 100 / 251 (53 %) | 15 / 100 / 251 (53 %) | - |
| v1_city_default | banner (city highlights) | 288,880 | 0 / 0 / 162 (0 %) | 49 / 139 / 251 (91 %) | 61 / 171 / 255 (98 %) | 10 % -> **22 %** |
| v1_city_default | unit (selection, movement range) * | 212,659 | 5 / 26 / 132 (19 %) | 46 / 99 / 255 (85 %) | 44 / 97 / 255 (80 %) | 14 % -> 20 % |
| v1_city_default | grid | 236,765 | 0 / 0 / 55 (0 %) | 35 / 90 / 241 (96 %) | **10 / 85 / 241 (45 %)** | 0 % -> **97 %** |
| v2_city_zoomin | border band | 521,965 | 0 / 0 / 12 (0 %) | 18 / 109 / 251 (55 %) | **1 / 98 / 251 (29 %)** | - |
| v3_second_zoomin | border band | 528,375 | 0 / 0 / 12 (0 %) | 25 / 116 / 251 (65 %) | **6 / 106 / 251 (42 %)** | - |
| b_land (50, 13) | border band | 372,624 | 0 / 0 / 12 (0 %) | 20 / 102 / 251 (58 %) | **5 / 92 / 251 (37 %)** | - |
| b_water (51, 13) | border band (over water) | 347,635 | 0 / 0 / 12 (0 %) | 21 / 99 / 251 (60 %) | **8 / 91 / 251 (43 %)** | - |
| w_cam1_3601 (33, 14) | border band | 20,628 | 0 / 0 / 0 (0 %) | 56 / 122 / 149 (95 %) | 50 / 64 / 149 (94 %) | - |
| w_cam2_3601 (51, 14) | border band | 519,270 | 0 / 1 / 12 (0 %) | 21 / 111 / 242 (58 %) | **4 / 99 / 251 (36 %)** | - |

  \* camera moved by the click in the stock arm (9.3's note): read from the pictures. The stock-vs-stock column here is
  shots 1 against 2, the pair that made the motion mask, so its maximum is 12 by construction; the non-circular ceiling
  (shots 1 against 3) is 9.3's column (border bands 0-1.05 % changed).
- **Reading** [L numbers, I for the classes]: with the tap the border band's median falls from 18-25 to 1-8 at every
  camera with a border near the city, over land and over water, and its changed share from 55-65 % to 29-43 %, now at
  or below the rest of the ground at the same camera (ground off the band 30-61 %); the grid now draws under D33 (97 %
  of the grid pixels change when the grid is switched on, 0 % before) and its joined-vs-stock median falls from 35 to
  10; the city highlights now change twice the pixels (22 % against 10 %, the rest of the banner mask is the EXE's
  tooltip and icons, which differ in placement between launches). What remains on the overlay masks is the ground's
  own difference (same p95 of 85-106 as off the band), by class: trees (Forest off under D33), the distance fog at the
  top of the view (DistanceFog off; seam F), fog of war (the stock pictures have it, the joined ones do not: seam F,
  and 0185 / D136 WhiteFogMask), decals and farm fields (seam D), the dark-red models and their 16 px margin (0098 /
  3009, 0172), lighting (seam G): the intended classes of D81 point 5. w_cam1 (zoomed out, a 20,628 px band) did not
  improve: [I] its band is at the view's far edge, inside the distance-fog and tree area.
- **Band geometry** [L, `shell/evidence/0167/3706/band_geometry.py`, Tiny v1, 16:44]: `highlight_accuracy.py` needs an
  offline render's world image, so its idea is applied to the pictures: per state, each arm's mask = what the stimulus
  changed in that arm (on against off, > 24), frame and UI rectangles cut; distances from each arm's mask pixels to the
  other's. **Grid: median 0 px both ways, 83-84 % within 1 px, 93-94 % within 3 px, p95 4-5 px** (IoU 0.51 at
  270,854 / 292,492 px): the grid is drawn where stock draws it. Banner (city highlights) and unit (selection, movement
  range): joined -> stock median 0 px, 67 % / 79 % within 1 px, but stock -> joined p95 252 / 368 px: the stock masks
  also hold what is not overlay (the banner tooltip and icons placed differently, units' idle animation; the unit click
  moved the stock camera, 9.3), so these two are read from the pictures; their overlay shapes (OwnedFill/WorkedFill hexes,
  the movement-range outline, the selection ring) are on the same plots as stock by eye (`v1_city_default__banner_*`,
  `__unit_*` of both arms). The selection ring rotates and pulses (P14), so its pixels never match between launches.
- **Huge census** [L, 2026-11-14 17:51; the huge ACCEPTANCE run 17:40-17:49 (c5h-a, the quiet machine, composite
  `0480b9f4...`, every picture the composite's back-buffer dump, kept at the first try) against 3702's archived stock
  arm (`D:/c5-64bit-work/0167/captures/3702/huge/stock`, 15:08-15:20); `C:/c5-64bit-work/0167/captures/3706/huge/`]:

| Camera | Mask | Overlay px | Joined vs stock, before (no tap, 9.3) | **Joined vs stock, with the tap** | Joined on vs off (before -> now) |
|---|---|---|---|---|---|
| v1_city_default (Memphis) | border band | 37,876 | 79 / 152 / 201 (91 %) | 79 / 147 / 203 (91 %) | - |
| v1_city_default | ground off the band | 2,752,820 | 21 / 146 / 255 (57 %) | 21 / 146 / 255 (57 %) | - |
| v1_city_default | banner (city highlights) | 1,490,448 | 39 / 129 / 245 (86 %) | **8 / 91 / 239 (38 %)** | 17 % -> **99 %** |
| v1_city_default | unit * | 260,477 | 3 / 116 / 255 (42 %) | 3 / 114 / 255 (42 %) | 61 % -> 63 % |
| v1_city_default | ranged (fire range) * | 156,216 | 0 / 97 / 255 (18 %) | **0 / 41 / 255 (8 %)** | 87 % -> 98 % |
| v1_city_default | grid | 193,521 | 38 / 114 / 208 (93 %) | **21 / 107 / 240 (58 %)** | 0 % -> **82 %** |
| b_land (96, 27) | border band | 595,864 | 28 / 153 / 251 (71 %) | **18 / 144 / 251 (55 %)** | - |
| b_water (103, 31) | border band (over water) | 195,395 | 55 / 170 / 251 (87 %) | **48 / 167 / 251 (65 %)** | - |
| v2 / v3 (no border in view) | ground | 2,930,792 / 2,930,407 | 37 / 151 / 255, 49 / 156 / 255 | 37 / 151 / 255, 49 / 156 / 255 | - |

  \* the stock click moved the camera (9.3's note). **Reading** [L numbers, I classes]: the city highlights, the grid,
  the fire range and the borders over land and water now draw under D33 (on/off 82-99 %, the grid from 0 %), and the
  joined-vs-stock share on those masks falls by 16-48 points. The v1 border band (37,876 px at the far top right, along
  the coast) is unchanged at 91 %: in the stock picture that area is under the fog of war and the farm-field decals, so
  the band's pixels are dominated by those classes (seen in `diff/v1_city_default__border_band_diff.png`); the borders
  themselves (the purple foreign line along the coast) are drawn in both. On the huge save the ground's own difference
  (51-71 % changed, p95 107-167) is larger than on Tiny: roads, railways, farms and other improvements (decals, seam D),
  the fog of war (seam F; 0185 / D136), trees (Forest off), the dark-red models and their margins (0098 / 3009, 0172),
  lighting (seam G). Every remaining class on the overlay masks is one of these intended ones; no overlay pixel was
  seen drawn in the wrong place (pictures `v1_city_default__*` and `b_*` of both arms, by eye).

### 12.3 Item 3: the window edge during a fast pan (open question 1, decision 2 (i))

[L, 2026-11-14 15:54-15:55, Tiny accept run, camera on the capital (zoom 11), 9 arrow-key pans of 1.5 s RIGHT/LEFT over
20 s; `pan_probe.py probe` polled the composite's main section and the overlay section every ~1 ms and saw **every
Present** (721 consecutive presentCounts 12305-13025, 0 missed); `analyze` at --step 8 px, ground plane z = 10.1167.]
Per camera Present N, the ground under a grid of screen points was mapped through three OverlayProj pairings:

| Pairing | Lag N - slot's Present (frames) | Frames with any ground outside the window | Uncovered share of the screen: mean / p95 / max |
|---|---|---|---|
| `same` (the slot copied at Present N: the EXE's own pairing, the control) | 0 (720 frames) | 0 | 0 / 0 / 0 |
| `newest` (the newest published slot when N's camera was published) | 1: 708, 2: 13 | 3 | 4.2e-7 / 0 / 2.3e-4 |
| `bound` (the slot the server named in `inUseSlot` at that moment) | 1: 213, 2: 508 | 15 (2.1 %) | 1.8e-6 / 0 / 2.7e-4 |

- The control is exact: the EXE's own window always covers its view's ground, so the method's plane and matrices are
  right [L].
- **Against decision 2 (i): the lag is one EXE frame, as designed; the "2" in the bound column is the sampling instant.**
  Order within the composite's Present hook N [C, `overlay_tap.cpp` and `shell_composite.cpp`]: the camera block of N is
  published, then the tap publishes the slot copied at Present N-1 (its event query has signalled by now; it is polled
  with DONOTFLUSH at the next Present, so a copy is never published in its own Present), then copies the target of N.
  So when camera N appears, the newest pair is N-1's: the `newest` column shows lag 1 in 708 of 721 frames, lag 2 in 13
  (the query of N-1 not yet signalled at Present N, the case the slot ring exists for). The server takes its pair at
  the start of each draw (`Scene::poll` -> `acquire`); the probe reads `inUseSlot` right after camera N is published,
  before the server has started its draw for N, so it mostly sees the pair the server bound for N-1 (lag 2 relative to
  N) [I from the order; the server does not log its pair's presentCount per frame]. The pair the server draws N with is
  therefore N-1's (lag 1) except when it starts before Present N's publish or the query was late (lag 2): the `newest`
  row is the expected case and the `bound` row its upper bound.
- **Its size**: at most 2.7e-4 of the screen (about 960 of 3.6 million pixels, scattered points at a corner of the view,
  no whole row or column at any edge) in 15 of 721 pan frames; in the other 706 frames the lagging window still covers
  the whole view. The 64-unit snap gives the window enough margin for this pan speed [L, Tiny, zoom 11, keyboard pan].
  Faster pans (edge scroll at a wider zoom, the huge map) were not measured.
- Torn or stale texels (W3's point: no GPU fence between the two devices): **not observable by this probe**, which reads
  matrices and slot numbers only; the pictures of item 2 are the check for visible tearing (none named so far).

- **The same on the huge save** [L, 2026-11-14 17:48-17:49, the huge ACCEPTANCE run, camera on Memphis (zoom 14.26),
  10 arrow-key pans of 1.5 s over 20 s; the probe saw all 721 Presents 11033-11753]: `same` 0 of 720 frames uncovered
  (the control); `newest` lag 1 in 718 frames, 2 in 3, uncovered in 5 frames, at most 4.5e-4 of the screen; `bound` lag
  1 in 88, 2 in 633, uncovered in 25 frames (3.5 %), at most 4.5e-4 (about 1,600 px), mean 5.8e-6; again no whole row
  or column at an edge. [I] The ground plane z = 10.1 is a coarser stand-in for the huge map's hills than for Tiny's.

### 12.4 Item 4: piece c under D33 with the tap (MC #696 (4))

[L, by eye, the huge ACCEPTANCE run 17:40-17:49 (`x_*` cameras of 3702's driver, the same means as 3702's stock arm:
clicks and the cursor are player input, the mode switches and the display toggles are Lua, diagnostic) against 3702's
stock references; pictures `C:/c5-64bit-work/0167/captures/3706/huge/joined/x_*` (Visual Checks, issue 3706, ACCEPTANCE
records of the full product); side-by-side figures made with `pair_figure.py`, diagnostic, not recorded.]

| Piece c reference (3702, 9.3) | Draws in the joined D33 picture today? | What the system is |
|---|---|---|
| Move-order path line (`x_path`: Landship, move mode, cursor 3 plots east) | **No path disc and no chevrons** (only the "<1" and "4/4" texts of the UI); the unit's movement-range glow on the ground **is** drawn now (Terrain Overlay target) | MovementPath, off under D33 (key and view bit): ticket 0179 |
| Ranged-attack arrow (`x_arrow`: Artillery (84, 31) on Rifleman (84, 33)) | the red target beam and the attack-preview panel (as before the tap) **and now the red fire-range ring** (FireRangeBorder, in the target) | the beam and panel are UI / models; the ring is a highlight style; the arrow proper is Arrows (on in config.ini under A'', its view bit cleared by the proxy): 0179 **(corrected below, 2026-11-14)** |

**Correction, 2026-11-14 20:0x EST (epic 0179, task 3801 [L]; granted by the MC through lead-0179):** the arc **does draw**
under the A'' form. Tiny copy `C5H-0179-TINY_0009V`, c5h-c, joined D33 product path (server.exe 71a0c296, proxy 5651efed,
composite 78ce2fc6 release from `task-3801-arrows-live` a2cb8b59, x64 DLL 74588135), an Archer aimed at a barbarian two
plots away: the EXE's red arc is drawn over the joined picture, the same shape as stock's at the same camera (Visual
Checks `vc-20261114-195012-workstation-d8a4`). Why: Arrows is on in config.ini under A'', so its config bit 0x400000 is
set and the proxy's view rule ("view &= config | 0xFF000000" minus Terrain and Waves, `proxy/src/proxy_shell.cpp:796`)
keeps the view bit (`vpproxy.log`: config 0xff640767, view 0xff440766); `NEVER_RESTORE` (`:509`) only stops the proxy from
setting back bits it cleared itself. The "view bit cleared by the proxy" above was wrong for this layout. Why the huge
`x_arrow` shot showed no arc is not known [I: no `SpawnArrowEvent` at that cursor position, or the arc taken for the
beam]. MC #1030: the EXE's arc stays in increment 2 (route (b)). Details: `arrows-and-path-line.md` 5.
| Yield and resource icons on | **drawn** (as before the tap) | UI-side (YieldIcons key off does not remove them) |
| Trade-route display | **drawn**: the purple route arrows along the railway appear at the same places as in stock (3702 could not judge it: its joined camera differed) | not the Terrain Overlay target [I: a UI or arrow-system draw that survives D33] |
| Floating yield text (`AddPopupTextEvent`) | not judged: 0.8 s after the call neither arm's picture shows it clearly (17,529 px change on/off in the joined arm, 26,135 in stock, mostly animation) | UI |

### 12.5 Item 5: the failure route of decision 5 (D125 for an install failure, D132 for a fault in play)

- **(a) The overlay chain on the dev product path** [L, 2026-11-14 15:41-15:42, Tiny, release build]: composite key
  `overlay_test=nullhandle` (the composite's own test hook: a synthetic target whose handle stays 0; no EXE byte).
  Composite 15:42:28 `overlay_tap: LOUD: FAILED HANDLE at the install: handle [0xDA12A4] = 0x0 (no target) (for 30 map
  frames)`; server at +66.7 s `render: LOUD: overlay tap FAILED (VPOVL_FAILED reason 2 HANDLE): ...` and `remote: exit
  code 5 (...)`; proxy `vpipc: ... server pid ... exited (code 5)`, `call rt:LockSync (0xFFF00041) ... failed: PeerDied`,
  `FATAL: the server failed a call (on_server_death=exit): the process ends with exit code 3`; the EXE was gone 152 s
  after the launch (the harness's wait saw state `gone`). This layout has `pack=0`, so the proxy writes no report and
  starts no helper: the report, popup and config.ini restore of D125 need the shipped pack layout (`pack=1`,
  `proxy_rt.cpp` `CallFailed` [C]); that is (b).
- **(b) D125's route on the shipped pack layout** [L, 2026-11-14 15:44-15:46, oldpc testbed job `20261114-154008-b4d4`,
  instance tb2, MC #786; `neg_d125_job.py --fault overlay` = `release_d125.py` unchanged with the same composite test hook;
  the release build above, pack `3706-item5` by `tools/release/pack.py` (x86 = Release-5.4.8's own DLL `262aee88...`,
  x64 `74588135...`, purity `3721cafb...`), `pack=1`, config.ini put in the A'' form by `vp64helper on`; save
  `TB-TINY_0008` (`b2ceb663...`)]: 13 of the job's 14 checks pass: composite `LOUD: FAILED HANDLE at the install`; server
  `LOUD: overlay tap FAILED (VPOVL_FAILED reason 2 HANDLE)` and exit code 5 at +32.5 s of the server; the EXE ended by
  the proxy 86.5 s after the launch, no "Game Crash" box; one report folder, no `.dmp`; `vp64helper failed` restored
  config.ini (helper.log: "restored Decal, Forest, FOW, FluidFOW, DistanceFog, YieldIcons, MovementPath, Particle, Combat,
  Projectile, Minimap"; SHA-256 `7b53b17c...` = the player's before the run, after the failure and at the end), wrote
  `failed=1`, showed the popup (picture `vc-20261114-154540-oldpc-22e0`, DIAGNOSTIC: the fault is a test hook); the
  instance restored by hash. **The failed check:** `report.txt` itself says only `stage: server-death`, `code: PeerDied`,
  `the failed call: rt:LockSync`, `server pid ...: exit code 5`; the reason `VPOVL_FAILED HANDLE` is in the report
  folder's attached `server.log.tail.txt` (6 lines) and `render.log.tail.txt` (3), not in `report.txt` or the popup's
  text. A tester's report therefore carries the reason, but a reader of `report.txt` alone sees only exit code 5 [L].
  Proposed follow-up (not 0167's file): the proxy quotes the server log's last `FAILED` line in `[failure]`.
  **Ruling (MC #834, 2026-11-14 16:02): item 5 passes, 14 of 14 under the MC's reading** (the report folder, which a
  tester sends whole, names the reason in `server.log.tail.txt`); the strict `report.txt`-only check stays recorded as
  failed above; the improvement is its own ticket outside 0167.
- **A fault in play (D132)**: `overlay_test=lost` does **not** reach play in a real game [L, 15:44-15:45]: the composite's
  synthetic test world counts every Present from the process start, front end and load screen included, so its target is
  "lost" (frame 40) long before the first map frame, and the run took the install-failure route instead (the same
  LOUD lines, exit code 5, FATAL). The mode is a stand-in mode only. In-play faults that occurred by themselves are
  cited instead (missed copies: below and section 11).
- **A fault in play that happened by itself** [L, Tiny accept run 15:47-15:55, the configuration above]: the composite
  logged `LOUD: missed copy: the previous copy is still in flight (missed 1, faults 0, copies 56)` once and counted 10
  missed copies in 14,000 (`missedTotal`); the game went on (the camera and stimulus series, the pan, the stop), the
  server stayed `overlay=ACTIVE` with stale 0, refused 0, bad 0 and 0 LOUD lines in render.log and server.log. This is
  D132's behaviour for a missed copy; a forced stale episode in play was not produced (no live hook for it, above).

### 12.6 Item 6: 30 AI turns on the huge save, overlay on against overlay off (done E)

- **Overlay-ON arm, run 1** (while 0158's capacity series loaded the machine; the pair is run 2 below) [L, 2026-11-14 16:14-16:38, c5h-a, `C5H-0167-HUGE_0350`, turns 350-379 = 30 AI turns; the
  configuration above with composite `0480b9f4...`, `lockstep_csv=1` (logging only); before every turn: highlights
  around the capital in the WorldView Lua state ("Culture", "MovementRangeBorder" x 6, "WorkedFill" x 2: 9 sent) and two
  arrow-key pans of 1.2 s, then the highlights cleared]: rc 0, `max turns 30`, the launcher's stop rc 0, restored by
  hash, `provision verify --quick` ok. **0 EXE crashes** (no "Game Crash", no 0x574B03 in the run's records).
  Turn wall time median 37.3 s, max 56.9 s. Lockstep over the 30 turns (3308's per-turn slices of the composite log):
  exact 8,934, older 99,623, none 0, nopub 0, giveups 2,188, recoveries 2,178, late 0. Gray frames after the bake-ahead:
  0 (0139; 39 gray frames before it). Server: `overlay=ACTIVE`, 49,331 pairs, refused 0, bad 0, unmatched 0.
  Memory after the load: EXE private 1,388.8 MB, largest free block 1,914.4 MB; server private 2,206.6 MB, video (as
  render.log reports it) 1,337.1 MB; after the last turn: EXE 1,551.6 / 1,733.3 MB, server 2,624.0 MB.
- **LOUD lines: 40 in render.log (= server.log), 3 in the composite's log** [L]: every server line is
  `overlay: LOUD: stale: no new copy for 34-55 Presents of the target (... DLL counts N missed ...); the last pair stays
  bound`, one per stale episode; the composite's are the rate-limited `LOUD: missed copy: the previous copy is still in
  flight`: **2,203 missed copies of 108,000 (2.0 %)**. The EXE presented about 80 times a second during the AI turns
  (116,847 Presents in 24 min), so an episode is 0.4-0.7 s in which the copy's event query had not signalled (the tap
  polls it with `D3D11_ASYNC_GETDATA_DONOTFLUSH`) [L for the counts, I for the cause; *the cause corrected 2026-11-14 by
  issue 0194, section 11.1: every one of the 40 is a run of 204-464 Presents (0.57-0.96 s) without the scene-clear list,
  an AI leader screen, in which the tap has nothing to copy; the query was not late*]. This is D132's designed in-play
  behaviour (logged LOUD, counted, the game goes on, the last pair stays bound), but **done E as written ("0 LOUD
  lines") is not met by the overlay-ON arm**: reported to the lead (queue #863) with options (amend done E to name the
  counted D132 episodes, or a tap change such as a flush after the copy's `End`, and a rerun). On the Tiny save the tap
  missed 10 of 14,000 and the server logged no stale episode (12.5). (Run 1's "0.4-0.7 s" assumed 80 Presents per
  second over the whole run; run 2 below shows the AI-turn rate near 250 per second, so 34-55 Presents are nearer
  0.15-0.25 s.)
- **When the stale episodes fall, and the server's GPU then** (offline from the ON arm's logs,
  `shell/evidence/0167/3706/stale_when.py`, 2026-11-14 16:45): **all 40 episodes fall in AI-turn time, none in the
  player-turn windows** (the 30 windows of highlights and pans, each taken as 7 s from its first stimulus, are 17.5 % of
  the time; 40 of 40 outside them by chance has a probability of about 0.0005) [L]. The server's terrain GPU time per
  frame in the seconds around an episode (the episode's second and the 3 s before) against all other seconds: median
  9.72 against 9.12 ms, mean 10.22 against 9.41 ms, max of the per-second maxima 94 against 153 ms; frames the server
  drew per second 48 against 37 [L]. So the episodes coincide with AI turns and with a slightly higher server frame rate,
  not with a clear rise of the server's GPU time: GPU contention with the server's terrain is not shown by these
  per-second averages [I: they could hide a short spike; a per-frame GPU timeline of both devices would settle it].
- **Overlay-OFF arm** [L, 2026-11-14 16:58-17:16, the same instance, save, builds and driver, composite `overlay_tap=0`
  (the kill switch: no tap, the server's source OFF, t1 / t9 transparent), the same per-turn stimuli]: rc 0, 30 turns,
  0 LOUD lines anywhere, 0 crashes, gray after the bake-ahead 0.
- **Overlay-ON arm again** (run 2) [L, 17:16-17:35, the same instance, save, builds, driver and stimuli, right after the
  OFF arm on the same quiet machine; asked by the lead after run 1's figures]: rc 0, 30 turns, 0 crashes.
- **The like-for-like pair is ON run 2 against OFF** (`item6_summary.py`, `phases.py`); run 1 ran while 0158's
  capacity series (4-20 headless servers) loaded the machine until 16:54 and is kept as a load case, not as the pair.
  Time figures are indications (COMMON 23: workstation, no quiet lock):

| Figure (30 AI turns, turns 350-379) | **ON run 2 (quiet)** | **OFF (quiet)** | ON run 1 (machine loaded) |
|---|---|---|---|
| Lockstep exact / older (composite, summed over the turns) | **132,009 / 2,959** | **131,410 / 2,846** | 8,934 / 99,623 |
| Lockstep none / nopub / late | 0 / 0 / 0 | 0 / 0 / 0 | 0 / 0 / 0 |
| Lockstep giveups / recoveries | 14 / 14 | 14 / 14 | 2,188 / 2,178 |
| Server frames drawn | 157,199 | 155,144 | 52,531 |
| Server terrain GPU ms per frame, mean / max of the per-second values | 1.15 / 7.2 | 1.29 / 10.1 | 9.53 / 20.2 |
| the same, median in player-turn windows / in AI turns | 1.41 / 0.72 | 1.75 / 0.69 | 6.08 / 9.69 |
| Turn wall seconds, median / max | 28.2 / 34.8 | 28.0 / 35.1 | 37.3 / 56.9 |
| EXE private / largest free block after the load (MB) | 1,405.7 / 1,873.8 | 1,381.5 / 1,933.8 | 1,388.8 / 1,914.4 |
| EXE private / largest free block after the last turn (MB) | 1,530.6 / 1,731.5 | 1,532.7 / 1,735.6 | 1,551.6 / 1,733.3 |
| Server private after the load / after the last turn (MB) | 2,119.2 / 2,230.2 | 2,134.8 / 2,238.9 | 2,206.6 / 2,624.0 |
| Server video (render.log's figure) after the load (MB) | 1,663.8 | 1,643.7 | 1,337.1 |
| Tap: copies / missed copies / CPU per copy mean, max (ms) | 138,000 / 869 (0.6 %) / 0.0013, 0.221 | - | 108,000 / 2,203 / 0.0029, 1.137 |
| LOUD lines (server / composite) | **43 / 1** | 0 / 0 | 40 / 3 |
| Gray frames after the bake-ahead (0139) | 0 | 0 | 0 |
| EXE crashes (0127's 0x574B03) | 0 | 0 | 0 |

- **Reading** [L]: with the overlay on, lockstep, the server's GPU time, the frame count, the turn times and the memory of
  both processes are those of the overlay-off arm within run-to-run noise: the overlay costs nothing measurable on the
  huge save. Run 1's losses (8 % exact, 9.5 ms GPU per frame, +370 MB server private, which came at once between the
  load and the end of the first turn and then +48 MB over 29 turns) were the machine load, not the overlay. **The one
  remaining difference is the LOUD lines**: 43 server lines `overlay: LOUD: stale: no new copy for 31 Presents of the
  target ...` (every one at exactly 31 Presents, all 43 in AI-turn time, 0 in the player-turn windows) and the
  composite's one rate-limited `missed copy` line (869 missed of 138,000). In AI turns the EXE presents about 250 times
  a second (the server drew 250 frames per second around the episodes, 120 elsewhere), so 31 Presents are about 0.12 s:
  the reader's stale rule counts Presents, and a normal copy latency at AI-turn frame rates trips it [I for the cause,
  L for the counts]. (*Corrected 2026-11-14 by issue 0194, section 11.1: the 31 was the moment the rule fired, not the
  episode; each of the 43 lasted 293-531 Presents (0.56-0.97 s) of frames without the scene-clear list, AI leader
  screens; copies resumed 0.58-0.97 s after the line. Replaced: "a normal copy latency at AI-turn frame rates trips it".*) **Done E's "0 LOUD lines" is therefore not met only by these counted D132 episodes**; the MC's
  ruling on the wording is question #865 (lead-0167), and a change of the stale rule to a time threshold (or to Presents
  that changed the camera or the target) is the lead's proposed 3707.
- **Done E as amended by the MC (#891, 2026-11-14 18:0x, answer to #865): met.** The amended text: 0 EXE crashes, 0 gray
  frames, 0 LOUD lines other than D132 stale and missed-copy episodes, which the run report counts with their rates. The
  ON run 2 arm: 0 crashes, 0 gray frames after the bake-ahead, no other LOUD line; **stale episodes 43 in 30 turns =
  1.43 per turn, every one 31 Presents long (about 0.12 s at the AI-turn rate of about 250 Presents per second), all in
  AI turns; missed copies 869 of 138,000 = 0.63 %** (run 1 under load: 40 episodes = 1.33 per turn, longest 55
  Presents, missed 2.0 %). These figures predate the MC's second condition (a stale episode is a lag, not a fault: the
  reader logs the first 3 and counts the rest, and an episode needs at least 250 ms without a new generation,
  `VPOVL_READER_STALE_MS`: as built by lead-0167 on the team branch at 3a232e28, sections 10.2 and 11); under that floor
  all 43 would still count, since each lasted 0.56-0.97 s (*corrected 2026-11-14 by issue 0194, section 11.1; replaced:
  "under that floor none of the 43 would count"*); under 0194's map-frame rule none counts. The floor follows from this
  arm's analysis (queue #904: every episode at exactly 31 Presents, about 0.12 s). Scripts here that grep
  `LOUD: stale` (`stale_when.py`, `item6_summary.py`) need the new text for runs on the new build. The cause of the
  episodes (the copy's query not signalled within 31 Presents at high frame rates) is a follow-up
  (`tracker/issues/NEW-overlay-stale-episodes-cause.md`, the lead's draft).

### 12.7 Item 7: cost (done F), indications only (COMMON 23)

- **Video memory of the three slots** [C size, L the tap's own line in every run]: 3 x 22,369,620 B (2048 x 2048 RGBA8
  with all 12 mips) = 67,108,860 B = 64.0 MiB on the EXE's device (`overlay_tap: 3 slots of 2048x2048, 12 mips, format 27
  created ... 64.0 MB of video memory`): the "about 67 MB" of MC #703 (decimal MB).
- **Composite CPU in `CopyResource` + `End` per copy** [L, workstation, an indication]: the huge ON arm run 2 on the quiet
  machine (17:16-17:35): **mean 0.0013 ms, max 0.221 ms** over 138,000 copies (138 windows of 1,000); run 1 under
  0158's load: mean 0.0029 ms, max 1.137 ms over 108,000 (window means 0.0010-0.0066 ms); Tiny (15:47-15:55, inside
  0158's 15:45-16:15 window): mean 0.0089 ms; 3703's live B (1): mean 0.0019 ms, max 0.22 ms (section 11). The copy
  itself is GPU work (22.4 MB per Present) that these CPU figures do not show.
- **The server's GPU time with the overlay bound** [L, an indication]: the server's terrain GPU time per frame
  (render.log `gpu_ms`: the terrain draws that sample t1) on the huge save, the like-for-like pair: ON run 2 mean
  1.15 ms (max of the per-second values 7.2) against OFF 1.29 ms (10.1); in the player-turn windows (camera moving)
  median 1.41 against 1.75 ms, in AI turns 0.72 against 0.69 ms. **No measurable cost** within run-to-run noise.
  Run 1's 9.53 ms was the machine load (12.6).

### 12.8 Item 8: the suites of 3703 and 3704 on the merged branch, plain and under MSVC ASan (done G)

[L, 2026-11-14 17:49-18:53, workstation, `shell/evidence/0167/3706/item8_suites.sh` through the gates, builds on
`C:/c5-64bit-work/0167/3706/suites`; the branch with the team branch at 3a232e28 merged (3703's final tap and
lead-0167's stale floor; the composite's sources were unchanged by that commit, so its build of 17:49 is of the final
code); 0 skipped in every suite (a skip fails the run, COMMON 9); 0 ASan report files in all three report folders]:

| Suite | Plain | MSVC `/fsanitize=address` |
|---|---|---|
| `shell/tests/test_overlay_tap.py` (stand-in: every tap negative, kill switch, verify) | 20 passed | 20 passed |
| `server/tests/render/overlay/test_reader_overlay.py` (reader negatives) | 35 passed | 35 passed |
| render `c5r_terrain_test`, `c5r_water_water` (overlay input, default bit-equal) | 2 / 2 ctest | 2 / 2 ctest, 0 ASan reports |
| `server/tests/render/overlay/test_scene_overlay.py` (8 cases incl. `failed_is_fatal_once`, `fatal_hook`) | 3 passed (all 8 cases inside) | the 8 cases PASS (the binary directly) |
| `server/tests/render/test_render_host.py` (render host) | 47 passed | 47 passed |

An earlier pass of the same script on the branch before 3703's last commits (15:19-15:33) had the stand-in 20/20 plain
and under ASan.

### 12.9 Item 9: research and tracker (done H)

- Open question 9's four corrections (made by lead-0167 #2 at 13:40) were re-read after the live results
  (2026-11-14 18:10) and still read right: `terrain-rendering-inventory.md` 2.4 (the grid is pass 4 of DrawSimpleOverlay
  into the Terrain Overlay target), `asset-pipeline.md` 3.2 (GridShader draws UI nine-slice panels) and 3.3 (OverlayProj
  one matrix per view), `renderer-in-server-design.md` 4.11 and `disable-systems-and-masks.md` 5 (the EXE draws the
  highlights into a target nobody samples under D33; epic 0167 taps it). The live results add, not contradict: with the
  tap the server shows them (12.1, 12.2).
- Ticket 0138's log carries the result for the MC to close it with the epic (done D met).
- Archived (D83, D133): pictures, difference images, census files and the run records (run.json, render.log,
  server.log, the composite's log, pan probes, the oldpc job's output) in `D:/c5-64bit-work/0167/captures/3706/`
  (823 MB; the team's captures folder 1.9 GB of its 4 GB). Never committed.
