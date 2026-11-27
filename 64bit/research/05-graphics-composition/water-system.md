# The EXE's water system: ocean, coast, lakes, rivers, coast waves (seam C of package I2-E)

Status: first version 2026-11-13 22:40 EST, written by the drafting agent of epic 3600 for the MC from the knowledge
base only (nothing was run, decompiled or captured for it). It gathers what other files say about water into one
place, adds three conclusions that follow from joining them (marked **[I, join]**), and lists the open questions the
epic's first task answers. Epic 3600 (`docs/briefs/epic-3600-water.md`, `tracker/issues/3600-water-in-server-renderer.md`)
extends this file; its findings go here, corrected in place.

Addresses are DX11 EXE VAs at ImageBase 0x400000. Marks: [C] confirmed by decompile or dump, [L] live, [I] inferred.

## 1. What draws water in the stock frame

| Item | Shader set (material) | Draw | Geometry | Inputs | Source |
|---|---|---|---|---|---|
| Ocean, coast, lakes | WaterShader (91), passes 0/2/3/5: VS `0x06815a48`, PS `0x068179f0`, `0x0681b500`, `0x06820b00`, `0x0682c078` | `0x739ae0`, called from the TerrainSystem render `0x657070` [C] | one quad per plot over the camera's plot rectangle, refilled every frame into the DYNAMIC VB Water+0x54 / IB Water+0x58, one draw per 14 x 14-plot cell [L, section 6.3 and 7; C 3602 section 8.4, 3603 section 9.3, 3604 section 10.2; *corrected 2026-11-14 by 3601, was: "vertex buffer at Water+0x58, built at load"*] | WaterFrame (shared id 26, 0x250 B, layout [C], 22 of 22 offsets checked against the writer); t0-t3 LEAN bump textures, t4 ReflectionMap, t5 WaterBlendMap = "Water Currents" target, t6 WaterColor, t7 WorldHeightMap = TR+0x12ac[cell] (the terrain's per-cell height page), t8 WaterFogRamp, t9 OverlayMap, t10 FOWMask, t11 TransFOWColor (3D), t13 DynamicShadowTexture (its sampler word is never written: an EXE slip) [C] | `asset-pipeline.md` 3.2-3.4; `asset-pipeline-verification.md` 5.1; `terrain-rendering-inventory.md` 2.2 |
| Rivers | WaterShader (91), passes 1/4: VS `0x06816a20`, PS `0x06819f60`, `0x06826ea0` | RiverVisSystem (static object 0x1A0C694) render `0x463640` "River Visualization", called from `0x657070` when view bit 0x100 is set (test at `0x6570D1`) [C] | river mesh built at load inside CreateTerrain (section 3) | own buffer 0x80 B ("river World": an identity matrix, section 8.1), WaterFrame, t7 = River+0x24 (a 32x32 texture), t12 ColorKey Rivers (3D, 5 styles) [C] | `asset-pipeline.md` 3.2, 3.4; `02-exe-internals/world-systems-and-stubs.md` 4 |
| Coast waves | WaveShader (92), one pass; t0 `g_WaveTexture` | Wave_System (0x1A0C6C8, Startup 0x549C80, update 0x6AB850, render 0x643B60, view bit 0x200000) draws the chunks built per game by `0x6a6490` [C 3603, L 3601] | 3 chunks on Tiny, each an IMMUTABLE VB (format 0x19, 12 B, 4,464 vertices) + R16 IB (11,904 indices) [L, section 7] | `Wave_Coastline_ALL.dds` (Wave_System+0x68, section 8.1; *corrected 2026-11-14, was `wave_coastline_01.dds`*), `WaveSettings.xml` | `asset-pipeline.md` 3.2; `02-exe-internals/disable-systems-and-masks.md` row 21 |
| Currents map | WorldCurrents (93), **pass 3 only** (section 8.5); t0 = texture handle 0 | `0x53a9f0`, once per load (no WorldCurrents draw in either stock play-frame capture [L 3601]), draw object `0x444870`, into the 256x256 RGBA8 "Water Currents" target `0xda12c4` [C] | a full-screen quad | t0 handle 0 [C], what it resolves to [I]; CurrentsFrame (shared 43) [C] | `asset-pipeline.md` 3.2, 3.4; `evidence/2900/2903/draw_objects.txt` |
| Reflection | (the scene's own materials) | reflection pass `0x5A0F80`, into "Reflection Texture Raw" `0xda12c0` (created by Water::Startup `0x5F06A0`) [C]; it draws models and the polar ice (IceShader) into a target cleared to (0,0,0,0), no terrain and no sky [L 3601, section 6.3]; *an orthographic camera along a fixed direction, not a mirror (13.6, 2026-11-16)* | the scene's models and ice | - | `02-exe-internals/world-systems-and-stubs.md` 5; `asset-pipeline.md` 3.4 |

The Water object is TerrainSystem+0x2772C = **0xD3DD0C** [C]. WaterFrame takes its fields +0x6c, +0x70, +0x80, +0x90,
+0x9c, +0xa8, +0xb8, +0xd0 [C offsets]; their meaning and values are [I] (`asset-pipeline.md` 3.3, 3.6). Debug Lua hooks
exist: `Water_SetScale` 0x5A8770, `Water_SetAutoReload` 0x5F7980, `Water_SetSpec` 0x605320, `Water_SetSpeed` 0x74D7A0 [C];
whether they are registered in a normal game is not established [I]. *Added 2026-11-14 (3601) [L]: `Water_SetSpeed` writes Water+0xD8 (0xD3DDE4) and `Water_SetScale` Water+0xD4; writing +0xD8 and +0xE0 to 0 freezes the water (section 7).*

Sampler states, slot names and the WaterFrame layout are in `render/src/terrainsets/sets.cpp` (validated against the
blobs, `asset-pipeline.md` 3.7). *Updated 2026-11-14 by 3602:* the WaterShader own buffer, WaveShader's own buffer, the
shared ids 27 and 43 and the pass choice are traced (section 8.1, 8.2); still not traced: the blend, depth and rasterizer
states of every water pass (question 6). **The size inconsistency is settled (section 8.1):** `programs.txt` is right
(WaveShader own 0xf80 + shared 27 of 0x80; WorldCurrents shared 43 of 0x40, no own buffer); `material_table.txt`'s
`hdr0`/`cbsize` columns belong to the row above (a row offset in `materials.py`). (Was: "Inconsistency to settle
[C both, I which is right]".)

The art (all through `c5r::gamefs`, read at run time, D83): TerrainTextures `waterbumps`, `waterdepthcolor`, `watersky`,
`wave_coastline_01`, `river_alpha`, `river_diffuse`, `river_rough`, `river_smooth`; Vibrant Textures `Water/
waterdepthcolor_new`, `Water/river_diffuse_new`, `Water/euro_shallow_seas_d` (so the water's colour depends on the asset
set, D94); `Assets/ColorKeys/Rivers_<style>_ColorKey.tga`; the distance-fog ramp. The `.lean` bump textures:
`waterbumps16.lean`, `riversmooth16.lean` (`Resource/Common/TerrainModels.fpk`) and `riverrough_new16.lean`
(`Upgrade1Models.fpk`); format and slots in section 8.3 [C] (*2026-11-14, 3602; was "where they live and their format
are not established [I]"*). Water::Startup loads `waterdepthcolor_new.dds`, `river_diffuse_new.dds` and
`river_alpha_new.dds` by these names (section 8.3).

## 2. Water under the shell configuration (D33, option c1)

| Piece | Under D33 | Source |
|---|---|---|
| Water::Startup `0x5F06A0` (Water object, reflection target) | runs: TerrainSystem stays on under c1, only its view bit is cleared [I: not checked live that Water::Startup is inside TerrainSystem::Startup's path] | `world-systems-and-stubs.md` 5 (option c1, c2) |
| Ocean, coast, lake draw `0x739ae0` | skipped: it is under `0x657070`, behind view bit 0x1 [C]; its buffers Water+0x54 / +0x58 exist (made empty by Water::Startup) and the water clock stands still [L, 3601] | `world-systems-and-stubs.md` 5; `terrain-rendering-inventory.md` 5.3 |
| River render `0x463640` | skipped twice: behind bit 0x1 and bit 0x100; `[DisableSystems] River=1` also skips RiverVisSystem::Startup, so River+0x10 (batch), +0x14 (WaterShader material) and +0x24 (32x32 texture) stay NULL [C] | `world-systems-and-stubs.md` 4 |
| **River mesh** | **built anyway** by the Terrain bake: `0x709B50` at `0x70AE88` -> `0x6D51D0` (Terrain+0x50C) -> `0x560B40` (this = River) -> `0x738D10` mesh + `0x6B1580` (VB River+0x1C, IB River+0x20, draw record +0x18, triangle count +0x28, +0x2C = 1) [C static]; *2026-11-14 [L, 3601]: they exist in a D33 game, VB 110,464 B and IB 41,280 B R32 on Tiny (P2 holds; was "[I]")* | `world-systems-and-stubs.md` 4 |
| Coast waves | `Waves` is in D33's `[DisableSystems]` list; no start-up gate for it was found (the single-key probe: loads, +4 MB, "no clear difference"), *corrected 2026-11-14 (3601 [L], 3603 [C]): the key gates Wave_System's start-up (config bit 0x200000 at 0x476861) and the per-game mesh build (StartupGame 0x4692B1 skips the builder 0x67C170, 3603 section 9.4); under D33 Wave_System is empty (P5 fails). With `Waves` taken off the list and view bit 0x200000 cleared, the data exist and nothing is drawn (section 7, decision 2). Was: "the key clears only its view bit ... data probably exist under D33 [I]".* | `disable-systems-and-masks.md` 2 (row 21), 5 |
| Reflection pass `0x5A0F80` | runs under c1; only its ice part is behind view bit 0x1 [C, L]. *Corrected 2026-11-15 by epic 0169 (4301, `polar-ice.md` 1 I1 and 4; granted by the MC, #1804, decision 4). Replaced: "skipped under c1 (view bit 0x1) [C]".* The pass is not behind bit 0x1: it is called at `0x58953D` (when view+0x1B8 bit 0x8 and +0x890 = 0) and at `0x749507` (no view-bit test before it), and inside it draws each system whose view bit is set: 0x40 `0xD02200`, 0x20 `0xCD0D98`, 0x2 `0xDB13D8`, 0x1 the polar ice (`0x5A103E` -> `0x751F30` -> `0x5FACB0`) [C]. Live under D33 (view bit 0x1 held clear, Tiny): one model draw into a 1024 x 1024 RGBA8 target with a 1024^2 depth target (the reflection, by size and place [I]) and no IceShader draw [L]. So under D33 the EXE's reflection holds the models of the set bits and no ice; the server draws no reflection (decision 3 (ii), 13.3) | `polar-ice.md` 4; `world-systems-and-stubs.md` 5 |
| What the composite shows today | the server's flat placeholder plane at the field's water height (10.117 on the huge save, 0xD91274) in (42, 74, 110), D58 point 7 [C code `render/src/terrain/terrain.cpp` 898-915; L 3200, 3310] | `terrain-surface.md`; `renderer-in-server-design.md` 4.10 |

Heights: the field holds the sea floor under water (0 on the 3004 dump far out; samples below 12,899 u16 = z 10.12 are
under the water plane); the EXE draws its hex outline over water at about z 10.0 [L fit], and picks water at z 12
[L] (`camera-and-picking.md` 10 item 6; `terrain-surface.md`, "Water outline height").

## 3. The meshes and the random numbers

**Static facts [C].** CreateTerrain (0x709B50) seeds the CRT once with `srand(mapRandomSeed)` (the DLL's
`ICvPreGame1::mapRandomSeed`, vtable +0x118). rand() is called on the load thread, in this order of the steps of
`asset-pipeline.md` 4.1: step 3 the coastline curves (`0x5674C0`, `rand() % 1000`, through `0x59BBC0` > `0x491050`),
step 6 BlendAllCoastMeshes `0x639D40` (one strip, then one segment), step 7 the river mesh `0x6D51D0` (rand in
`0x631990` and `0x54BAB0`); also `0x60F180` (route / coast-grid builder, via `0x6E27D0`) in the static closure. The
block-blend and coast-strip job bodies call none. MSVCR90's generator is `x = x*214013 + 2531011`, result
`(x >> 16) & 0x7FFF` (`asset-pipeline.md` 4.2 point 2, 4.4; `evidence/2900/2904/rand_in_bake.txt`).

*Added 2026-11-20 (epic 0615, ticket 4911; `live-terrain.md` 8.15) [C]: the coast strips and step 6's draws.*
- **What the strips are.** The coast strips are not a water mesh. They are height and material writes into the
  terrain field along every land-water edge, lakes included. They make the beach (material 3) and the slope under the
  shallow water at the CoastStyles.xml heights (0.19226-0.2244 as normalised u16 heights; the waterline line is at 0.19836 in stock).
  Neither the ocean surface nor the waves read them other than through the height pages (8.4, 10.2).
- **The draws.** Step 6 draws exactly two `rand()` values on the load thread whenever the map has a coast:
  - `% coasts`;
  - then, for the picked coast, `% (segments / 2)`.
- **What the draws decide.** They pick a job for a value that has no effect on the field (a CEG routine's result; the
  job only makes an unused temporary allocation). Their only effect is the stream position of the river mesh after
  them.
- **Lakes.** No lake carve exists: 0x412680 carves rivers only. Lakes have no mesh of their own, as 4.6 of
  `live-terrain.md` already said.

**[I, join] 1: the unnamed static buffers of GATE2 are the river mesh.** *Confirmed live 2026-11-14 (3601, section 7, P3) [L].* GATE2 (huge t350 save, all systems on) found
one static IMMUTABLE vertex buffer of 881,600 B and index buffer of 329,760 B built by `CreateTerrain -> 0x6d51d0 ->
0x560b40 -> 0x6b1580` and left their owner open ("coast or ice?", `sources/shell-gates/GATE2.md` "What the game
creates"). `world-systems-and-stubs.md` 4 names the same chain with `this` = RiverVisSystem and the buffers at
River+0x1C/+0x20. So these are the river mesh: about 1.2 MB on the huge save. Being IMMUTABLE, they cannot be shared
with the server as buffers; a CPU readback through a staging copy on the EXE's device works for any usage (GATE2,
"Alternatives": about 10-30 ms of PCIe and map stalls per 100 MB).

**[I, join] 2: a re-implemented river mesh needs the whole stream before it.** The river mesh's random numbers come
after those of steps 3 and 6 on the same generator. To reproduce them, a port must make exactly as many rand() calls
before step 7 as the EXE does, which means porting the coastline jitter and the coast-strip choice too (both are bake
stage 2 work, `research/07-architecture-decisions/architect-2026-11-12-renderer-scope.md` 3.3). Still open: whether
the load thread runs foreign jobs that call rand while it waits (`0x81D790`), and whether the pattern matcher calls
rand through an indirect call (`asset-pipeline.md` 4.2 point 2). The CRT of MSVCR90 keeps the rand state per thread
[I: CRT behaviour, not checked in this EXE], so job threads would not disturb it; the load thread's own foreign jobs
would.

**[I, join] 3: the meshes are probably the same on every load of a save.** Two loads of one save gave an identical
material byte layer and heights that differ in 28-43 samples, all in blocks written by the multithreaded blend jobs
(`asset-pipeline.md` 4.2 point 2, `shared-texture-and-compositor.md` 11). The river carving (step 7) rasterises the
river mesh into the field; carved river beds that are equal across loads point to an equal mesh. *Measured 2026-11-14 (3603, section 9.7) [L]: the
river VB and IB are equal byte for byte over two loads in one process (D33, Tiny), and the IB and the first 28 bytes of
every vertex equal stock's (the last 4, an unused COLOR slot, differ by configuration)* (was: Not measured: a hash of
River+0x1C/+0x20 over two loads).

Rivers do not change during play: the engine event RiverTileAdded is subscribed by Terrain (0x687630) and RoadLayout,
not by RiverVisSystem, and no step writes heights after load (`world-systems-and-stubs.md` 4; `asset-pipeline.md` 4.1).

## 4. What is seen live

- **Stock animates water**: two stock shots of one camera seconds apart differ ("stock animates water, trees and
  units", `renderer-in-server-verification.md` 6; stock's own two shots 0-5 / 1-50 / 225-252 median / p95 / max over the
  whole picture, `renderer-in-server-design.md` 4.10). A frame comparison of water against stock needs the same time
  phase or a stock-against-stock ceiling.
- **Ships**: at the same camera the EXE hides more of a near ship's hull and the server more of a far ship's
  (`renderer-in-server-design.md` 4.10, issue 3310): the two water surfaces differ in height and extent.
- **Deep ocean in the "black" arm**: with the D33 list minus River, Arrows and Overlay and without the view-mask
  marker (the EXE draws its own terrain and water, FluidFOW off), the EXE draws no water far from the coast; stock
  does (`renderer-in-server-design.md` 4.10). Cause not established [I]: candidates are the FOW mask (FluidFOW off), the
  currents map, or a per-cell condition of `0x739ae0`.
- **Rivers on the server's ground**: the server's bake shows the river channel's water material where stock shows a
  river-bed decal under the river water (bug 0130, bake stage 2; `terrain-bake-verification.md` 4.2). *Confirmed
  2026-11-15, epic 0166: the river bed is **TerrainDecalSystem decals** (Riverbank and its two end caps, priority 0.4, `riverbank_D*.dds` + `grey.dds`; Floodplains with end caps on flood-plain rivers), made from the map by 0x559FF0 through 0x60F180, deterministic across loads and present under D33, and drawn in the tile bake into the blend targets before NormalCalculate (terrain-decals.md 2.2, 4.4) [C, L]; the server's decal pass draws them bit-equal to the EXE on the river tile B_river (terrain-decals.md 11.4) [L]; the joined product gets them when the decal tap is wired into the bake (4105).*

## 5. Open questions (epic 3600, task 3601 answers them first)

1. What geometry `0x739ae0` draws (Water+0x58: vertex format, count, per cell or one mesh, built from what), and which
   pass runs at the owner's settings. (*3602, static: per-frame quads, section 8.4; the pass follows `[0xd90f68]`, section
   8.2; the value of `[0xd90f68]` at the owner's settings is 3601's.*)
2. The river mesh's vertex format and draw record (River+0x18), the own buffer 0x80 ("river World"), River+0x24; that
   the buffers exist under D33; their hashes over two loads.
3. The wave geometry, what draws it (Wave_System render 0x643B60 and/or `0x6a6490`), and whether it exists under D33.
4. The Water object's field values (+0x6c..+0xe4) and the WaterFrame wave constants (0x080-0x0c0): which come from a
   file, which from the clock; how the EXE's water time advances.
5. The WorldCurrents passes and their target; the reflection pass's content (sky, terrain, models). (*3602: currents
   answered statically, section 8.5, with t0 and the size words [I]; the reflection is open.*)
6. Blend, depth and rasterizer states of every water pass; whether water writes depth. (*3602: not in the draw
   objects; still open, section 8.2.*)
7. The `.lean` textures: file, format, which two the ocean binds. (*Answered by 3602, section 8.3.*)
8. The deep-ocean observation of section 4.
9. Whether `Water_SetSpeed` (or another data-only switch) freezes the water's animation in a live game.
10. The WaveShader / WorldCurrents buffer-size inconsistency of section 1. (*Answered by 3602, section 8.1.*)

*Status 2026-11-14 (task 3601): 1 answered [L] (6.3: the per-plot grid, pass 3 at the owner's settings); 2 answered [L]
(6.3 and 7: river format, sizes, both loads equal, present under D33; the own buffer's content is 3602's: an identity
matrix); 3 answered [L + C] (7: three chunks, absent under D33, present with `Waves` off the list); 4 answered [L] (7:
+0xDC and +0xE4 advance with the frame time while the draw runs); 5 answered [L] (6.3: currents not redrawn per frame;
reflection = models and ice only); 6 answered [L] (6.3); 8 narrowed [L] (7, P14: no ocean quads outside the per-view
rectangle; why the rectangle is smaller in that arm is [I]); 9 answered [L] (7: Water+0xD8 = +0xE0 = 0).*

## 6. The stock capture and its export (task 3601, 2026-11-14)

### 6.1 Set-up [L]

- Instance c5h-c (C:, Goldberg, CP/VP/EUI 5.4.8), 32-bit DLL pin `i0ship-26234371-ia` (3C2F47DA...) in the CP slot,
  no proxy, no server: the stock arm. Graphics settings are the instance's copy of the owner's (`GraphicsSettingsDX11.ini`),
  2554 x 1411, cloaked. RenderDoc 1.47 preloaded by the harness (`launch --preload`, `renderdoc-capture.md` 1).
  Asset set: VT present, folder hash `70402AD78740961507F2C806DDD3CC95817AA6D7F44AD846D76E974C78F68BA4` (55 files,
  47,300,858 B; `shell/evidence/3310/vt_hash_3310.py` on c5h-c's install, 2026-11-14). *This is not P15's
  `45b3b433...`: either that value belongs to other instances or c5h-c's VT differs; not resolved by 3601.*
- Save: copy `C5H-3600-TINY_0008` (= `c5h-9028-tiny-t0008`, SHA-256 b2ceb663...29163). Only 28 plots were revealed at
  turn 8 (no lake in sight), so every plot was made revealed **and visible** for the active team with
  `shell/evidence/3600/3601/reveal_all.lua` (`SetRevealed` + `ChangeVisibilityCount`; fog over water is seam F, out of
  scope). That raised first-contact leader screens and city-state / natural-wonder popups; they were closed by the
  screens' own buttons and Esc; one click ended turn 8, so the captures are at **turn 9**. The state was saved as
  **`C5H-3600-TINY_0009V`** (in c5h-c's ModdedSaves) so 3608 can load it directly. `find_water.lua` lists the best
  camera centres (map 56 x 36, 86 river plots, 3 lake plots).
- Cameras (camera blocks from `gt_reader.py camera`, scratch copies in `D:/c5-64bit-work/3600/notes/3601-cam{1,2}.json`):
  **camera 1** = `UI.LookAt(Map.GetPlot(33,14))`, zoom level 17 (zoomed out): ocean, deep ocean, coast, two lakes, a
  river, Carthage; GVS position (352.85, -854.34, 640.32), target (351.96, -854.34, 640.36). **Camera 2** = Honolulu
  (the capital, 51,14) 3 wheel ticks in, zoom level 11: a bay, coast with surf, the river, ocean; position (1471.94,
  -518.60, 261.00). No popup or EXE window over the map in either captured frame (D114): only the always-on interface
  and a mouse tooltip ("Ocean" / "Forest, Grassland, River"). Reference shots (Visual Checks, diagnostics):
  `D:/c5-64bit-work/3600/captures/cam{1,2}_stock.png`.
- Captures: camera 1 `c5h-c_20261113-235332_frame309938.rdc` (143 MB), camera 2 `..._frame265572.rdc` (100 MB), and the
  black arm of 7.4 `c5h-c_20261114-010946_frame14671.rdc` (84 MB), all under `D:/c5-64bit-work/3600/captures/`, never
  committed (D83). *Harness note [L]: `cli.py capture` reported 4,096 B and 745,223 B as "written" while RenderDoc was
  still writing 100 MB and 143 MB to D: (its 2-second size check fell in a pause of the write); wait until the size
  holds for 30 s before using a capture.*

### 6.2 The export (format and limits)

`shell/evidence/3600/3601/xml_export.py CAPTURE.rdc OUTDIR` converts the capture with RenderDoc's own `renderdoccmd
convert -c zip.xml` (hidden desktop, `tools/harness/rdc.py`) and walks the chunks in frame order, keeping the D3D11 state
per context (reset at `FinishCommandList` / `ExecuteCommandList` without "restore" and at `ClearState`) and the contents
of every resource (the capture's initial contents, creation data, then each `Unmap` and `UpdateSubresource`). Format:
`D:/c5-64bit-work/3600/captures/FORMAT.md`: per capture `draws.json` (index), `draws/<n>_<system>/state.json` (call and
arguments, topology, input layout, VB / IB with offsets and strides, per stage the shader, constant buffers, SRVs,
samplers, blend / depth-stencil / rasterizer descriptors, viewports, render targets and depth target), `buffers/`,
`textures/` (every subresource, with a `.json` of the descriptor), `shaders/` (DXBC). Exports: `cam1_frame309938/`
(61 MB), `cam2_frame265572/` (55 MB), `black_frame14671/` (51 MB).

Facts found on the way [C/L]:
- **The EXE's shader blobs carry no reflection** (no RDEF chunk in any of the 77 DXBC blobs of the capture), so draws
  cannot be named by cbuffer or texture names. The exporter names them by SHA-256 against the blobs read at run time
  from the player's EXE at the VAs of `evidence/2900/2903/material_passes.csv` (shader-model-5 columns): every water,
  wave and currents pass matched.
- The zip.xml conversion stores state objects under `Descriptor` and their ids under `pState`; textures created with
  data carry their subresources as repeated `SubresourceContents` buffers after `pInitialData`.
- **No replay, so no render target before and after each draw** (needed by 3604's per-draw comparison). The 1.47 zip has
  no `renderdoc.pyd`: the replay API is reachable from Python only inside `qrenderdoc.exe`. qrenderdoc on a hidden
  desktop (allowed by the MC, desk #339) starts, but every `--python` / `--ui-python` script waits for a debugger:
  its log shows "Importing debugpy from ... ms-python.debugpy ..." then nothing, because its settings file has Python
  debugging on. **qrenderdoc 1.47 ignores the APPDATA environment variable** (log 2026-11-14 01:15:40: it still read
  `%USERPROFILE%/AppData/Roaming/qrenderdoc`), so a private settings folder does not work. Changing the owner's settings
  file is refused (MC #424). Open: the MC asks the owner; until then the exports hold the frame-start contents only.

### 6.3 Draw list per water system (camera 1; camera 2 the same kinds) [L]

| Draw (frame order) | System | Shader (material, pass) | Geometry | States |
|---|---|---|---|---|
| 170-182 (13; cam 2: 8) | reflection, into "Reflection Texture Raw" (1024 x 1024 RGBA8, cleared to (0,0,0,0)) | 170-181 model shaders (not in the material table), **182 IceShader (59)** with 290,355 indices | the scene's models and the polar ice | blend ONE / INV_SRC_ALPHA, depth write on, cull back |
| 350-355 (6; cam 2: 2) | ocean, coast, lakes | WaterShader **pass 3** (PS 0x6820B00, VS 0x6815A48) | per-cell grids: 16, 56, 4, 44, 154, 11 quads | blend SRC_ALPHA / INV_SRC_ALPHA (alpha ONE / ONE), **depth test LESS_EQUAL, depth write OFF**, cull front (front counter-clockwise) |
| 356 | rivers | WaterShader **pass 4** (PS 0x6826EA0, VS 0x6816A20) | the river mesh, 10,320 indices | blend SRC_ALPHA / INV_SRC_ALPHA, **depth write ON**, LESS_EQUAL, cull back |
| 508-509 (cam 2: 4) | coast waves | WaveShader pass 0 (PS 0x682D118, VS 0x682C580) | one chunk each (11,904 indices) | additive ONE / ONE, depth write off, LESS_EQUAL, cull none |
| none | currents | WorldCurrents passes 0-3 | - | **no WorldCurrents draw in either frame**: the 256 x 256 currents map is not redrawn per frame; its contents at the frame start are in the export (t5, res 13350) |

- **P1 confirmed [L]: at the owner's settings the ocean runs WaterShader pass 3 and rivers pass 4** (3602: `[0xD90F68]` = 2).
- Vertex formats (input layouts): ocean `POSITION R32G32B32A32_FLOAT` (16 B: world x, world y, u, v; no z); river
  `POSITION R32G32B32_FLOAT, TEXCOORD0 R32G32_FLOAT @12, TEXCOORD1 R32G32_FLOAT @20, COLOR R8G8B8A8_UNORM @28` (32 B);
  waves `POSITION R16G16_FLOAT, TEXCOORD R8G8B8A8_UNORM @4, BLENDINDICES R8G8B8A8_UINT @8` (12 B). Index formats: ocean
  R16, river R32, waves R16.
- **Question 1 [L]: the ocean geometry is one quad per plot, 64 x 55.424 world units (the hex spacing), over the
  camera's plot rectangle, one draw per non-empty 14 x 14-plot cell, each binding that cell's 1024 x 1024 A8 height page
  as t7; u, v = the plot's place in the page (e.g. (0.688, -0.750)..(0.750, -0.688)). Camera 1: 285 quads = a 19 x 15
  plot rectangle, world x -255.5..960.5, y -553.8..277.6.** No land test (quads also lie over land; depth hides them).
- Constant buffers: water VS and PS bind cb0 (256 B) and cb1 = WaterFrame (592 B = 0x250); its row 0xC0 holds the water
  height **10.1167** (= P8's value) and rows 0x100-0x130 the Water fields of section 7. Waves bind cb0 4,096 B (the own
  buffer, 0xF80 used) and cb1 128 B (shared id 27). Bytes per draw are in the export.
- Textures bound to the ocean PS (pass 3): t0 512 x 512 R16G16B16A16_UNORM and t1 R16_UNORM (the waterbumps `.lean`
  pair), t2 / t3 8 x 8 placeholders, t4 the reflection target, t5 the currents map (256 x 256 RGBA8), t6 256 x 256
  (waterdepthcolor), t7 the cell's height page, t8 128 x 128, t9 2048 x 2048 RGBA8, t10 1024 x 1024 RGBA8 (FOW mask),
  t11 32 x 32, t12 8 x 8, t13 2048 x 2048 R16_UNORM (shadow). River: t0-t3 256 x 256 `.lean` pairs, t5 128 x 128 BC3,
  t6 256 x 256 BC1, t7 32 x 32 (River+0x24). Samplers: MIN_MAG_MIP_LINEAR on every slot of these draws.
- **Question 5 [L]: the reflection pass draws only models and the polar ice (IceShader) into a target cleared to
  (0,0,0,0): no terrain and no sky.** Decision 3 therefore lands on (ii): for increment 2 (no models in the server) the
  reflection input is a transparent black target, exact wherever no model or ice would be reflected.
- **Question 6 [L]: ocean water does not write depth (test only); river water does.**

## 7. Premise results and live reads (task 3601, 2026-11-13/14)

Reads with `shell/evidence/3600/3601/water_probe.py`, `handle_probe.py`, `wave_probe.py`, `cell_pages.py`
(ReadProcessMemory by the harness PID, TimeDateStamp checked), same instance, pin and save copies as 6.1; the raw reads
(JSON) are in `D:/c5-64bit-work/3600/notes/3601-reads/` (not committed: EXE memory, D83). Renderer
buffer tables [C, disassembly of 0x8C6140 / 0x8C61E0 / 0x8C62F0 / 0x8C63A0, agrees with 3603]: VB handle h -> desc
0x3020840 + 0x30h (+0 ByteWidth, +4 usage, +8 bind, +0x24 vertex format id), ID3D11Buffer* 0x301A708 + 4h; IB handle h
-> desc 0x3074E00 + 0x30h (+0x2C DXGI format), pointer 0x306ECC8 + 4h; vertex stride = [0x2E49F68 + 4 * format id]
(live: format 1 = 32, 2 = 20, 9 = 24, 0xA = 32, 0xE = 16, 0x19 = 12). Handles are separate per kind: the same small
integer can be a VB, an IB and a texture.

**D33 configuration used.** `darkred.py config --arm d33` (Overlay on, D93). The pin `i0ship-26234371-ia` **ignores
`crashlogs\viewmask.enabled`**: with the marker alone the EXE crashed at the first world frame in the River render
(VA 0x8CD492, return 0x46365A, view mask 0xFF472F67; 2026-11-13 23:42; the MC files this as a bug). The D33 view mask
was therefore held from outside by `mask_hold.py` (view &= config & ~clear, re-applied every 10 ms by the harness PID;
what the DLL's ViewMaskSync does; allowed on test instances only, MC #339). With it the game loads and plays: masks
config 0xFF040667, view 0xFF040666. Not the product path (no proxy, no server): the EXE's world-system start-ups do not
depend on which DLL is loaded [I].

| # | Result | Evidence |
|---|---|---|
| P2 | **holds [L]** | D33: River+0x18 draw record 0x94BB, +0x1C VB 0x47F = 110,464 B IMMUTABLE format 0xA (3,452 x 32 B), +0x20 IB 0x462 = 41,280 B IMMUTABLE R32 (10,320 indices), +0x28 = 3,440 triangles, +0x2C = 1; +0x10 / +0x14 / +0x24 = 0 (start-up skipped, as section 2 says). Stock: the same sizes (VB 0x589, IB 0x56B), +0x24 = texture 0x1F |
| P3 | **holds [L]** | huge copy `C5H-3600-HUGE_0350` (of `C5H-VP8P-HUGE_0350`, t350), D33 with the mask held, 2026-11-14 01:31: River+0x1C VB = **881,600 B** IMMUTABLE format 0xA (27,550 vertices x 32 B), +0x20 IB = **329,760 B** R32 (82,440 indices), +0x28 = 0x6B58 = **27,480 triangles**: GATE2's static pair is the river mesh (section 3's join 1 is now [L]) |
| P4 | **fails [L + C]** | Water+0x54 = **VB** (DYNAMIC, format 0xE, 131,072 B = 2,048 quads x 4 x 16 B), Water+0x58 = **IB** (DYNAMIC R16, 24,576 B), +0x5C = 2,048 quads: created empty by Water::Startup (0x5F0BDA / 0x5F0BF3), present under D33, refilled by the draw 0x739AE0 every frame and grown there when needed (0x73A145 / 0x73A160). *Corrects section 1 and inventory 2.2: the VB is +0x54, not +0x58, and its content is per frame.* Nothing to tap: the server builds the grid (6.3, question 1) |
| P5 | **fails [L + C]** | Under `Waves=1` Wave_System 0x1A0C6C8 +0x00..+0xAC are all 0 (no texture, material, batch, no chunks): its start-up is gated on config bit 0x200000 (0x476861) and the per-game mesh build is skipped (3603). Decision 2 test below |
| P6 | **holds for the river mesh [L]** | river VB SHA-256 `bc1edafd1b8081f2...` and IB `863be6f2bf52851f...` equal in the capture of the TINY_0008 load (cameras 1, 2) and of the TINY_0009V load (black arm, other process and config). Waves: one load only (chunk VBs 647d19eb / de590cc4 / 1eb49959..., IB a873aafa for all three); the chunk sizes and bounding boxes are equal in the stock and the D33-minus-Waves loads |
| P12 | **holds [L]** | below, the water clock |
| P14 | **narrowed [L], cause of the rectangle [I]** | below |
| P16 | **holds [L] for the resource** | the ocean PS's t7 per draw (A8_UNORM 1024 x 1024, 11 mips, e.g. res 43205 view 43206) is the same view the terrain draws bind to their vertex shader (view 43212 in 73 VS bindings, 43214 in 217) in the same frame: the water samples the terrain's 8-bit height page. A texel comparison with `(u16 >> 8)` of the tapped field is left to 3604 (the pages are in the export) |

**Water object 0xD3DD0C [L]** (equal in D33 and stock unless said): +0x10..+0x30 texture handles (3602: lean pairs,
river alpha, depth colour), +0x34 = 0x1A, +0x48 draw record, +0x54 / +0x58 / +0x5C as P4, +0x60 0.1, +0x64 0.1, +0x68 2,
+0x6C 0.5, +0x70..+0x78 (0.941177, 0.819608, 0.4), +0x7C 3444.31, +0x80..+0x88 (0.482353, 0.678431, 0.984314), +0x8C
53.8174, +0x90..+0x98 (0.398894, 0.56685, 0.720809), +0x9C..+0xA4 (-0.102513, 0.901409, 0.420658), +0xA8..+0xB0
(-0.327505, -0.66811, 0.66811), +0xB4 0.705882, +0xB8..+0xC0 (0.00917059, -0.81617, 0.577739), +0xC8 0.198367, +0xCC
1.43, +0xD0 5, **+0xD4 0.008 (the scale `Water_SetScale` writes, 0xD3DDE0), +0xD8 0.18 (the speed `Water_SetSpeed`
0x74D7A0 writes, 0xD3DDE4), +0xDC the clock, +0xE0 0.03, +0xE4 the second clock**, +0xE8 = 3 (the ocean pass), +0xEC 4.
The values from +0x60 to +0x8C reappear in WaterFrame rows 0x100-0x130 (6.3).

**The water clock (questions 4 and 9) [L].** Stock, three reads 2 s apart: **+0xDC advances 0.721 per second and +0xE4
0.0300 per second** (+0xE4 / +0xDC = 1/24 in both arms; 0.0300 = +0xE0 per second, 0.721 = 4 x +0xD8 per second within
the read timing). Both advance only while the water draw runs: under D33 they stay at 0.145891 / 0.00607878 (equal in two
D33 processes). **P12 holds: writing Water+0xD8 = 0 and Water+0xE0 = 0 (a data write, `water_poke.py`) stops both
clocks; on two full shots 4 s apart the open-sea pixels changed 0 % frozen against 93.8 % (mean 3.39) with 0.18 / 0.03
written back.** For decision 4 (i) the frame record needs +0xDC and +0xE4; a frozen comparison writes the two speeds to 0
in both arms.

**Decision 2 test (Waves taken off D33's `[DisableSystems]`, view bits 0x1 and 0x200000 held clear) [L],
C5H-3600-TINY_0009V, 2026-11-14 01:05.** (a) Wave_System is filled: texture +0x68, material +0x7C = 92 (WaveShader),
batch +0xAC, **3 chunks** (+0x58), each VB IMMUTABLE format 0x19, 53,568 B (4,464 vertices x 12 B), IB R16 23,808 B
(11,904 indices), instances 248 + 248 + 37 = 533 (+0x60), the same sizes and bounding boxes as stock. (b) No wave reaches
the frame: the shot at the default camera shows no surf (grey D33 frame, `d33waves_default.png` against
`d33_default.png`: 2.1 % of pixels differ, all animated units), and the wave instances do not change over 10 s (the
update 0x6AB850 is behind the same view bit). (c) EXE private bytes after the load, five reads 5 s apart: 1,417.1 MB
against 1,418.3 MB for D33 (one load each; the difference is inside the noise; an indication only, COMMON 23). So
**decision 2 (i) works: the view bit alone stops the wave draw and update, at no measurable memory cost.** The change to
`proxy/src/proxy_shell.cpp` is the lead's and the MC's.

**P14, the deep ocean of 3310's black arm [L, cause I].** Arm: D33 list minus River and Arrows (Overlay on), no view-mask
clear, so the EXE draws its terrain and water with FluidFOW, FOW and DistanceFog off; TINY_0009V, look-at (33,14),
default zoom (`black_cam1.png`, capture `frame14671`). The land and the near water are flat deep red (FluidFOW off) and
the open sea to the west is the grey clear colour, with a stepped edge. The capture has 4 ocean draws with 3 + 36 + 8 + 96
= **143 quads = a 13 x 11 plot rectangle (world x -63.5..768.5, y -498..111)**; the per-view rectangle globals read live
at the same moment (0xDA1B00.. view 0: (-26.2, -463.5)-(732.0, 82.2)) match it, and the grey region is everything outside
that rectangle: **no water quad exists there; the pixel shader is not the cause**. All 15 cells of the map have a height
page (TR+0x12AC, `cell_pages.py`), so the cells are not the cause either. Why the rectangle covers less than the screen in
this arm but the whole screen in stock (camera 1: 19 x 15 plots) is not established [I: the rectangle is UpdateViews'
per-view footprint, `camera-and-picking.md` 4; candidates are a far distance that DistanceFog sets, or the
map-wrap copies]. Consequence for the server: it builds its own grid over the whole visible area and does not inherit
the gap. The black arm also binds an 8 x 8 placeholder at t10 instead of the 1024 x 1024 FOW mask.

Not verified: the wave meshes over two loads; render targets per draw (6.2); the WaterFrame
members against the Water fields one by one (3604).

## 8. Shader sets, `.lean` textures and the currents map (task 3602)

Written 2026-11-13 23:32 to 2026-11-14 00:30 EST by W2 of team-3600 (issue 3602, branch `task-3602-water-sets`). Method: headless
Ghidra 12.1.4 (`-readOnly -noanalysis`) on the 2903 project copy of the EXE (build 4390913, SHA-256 b2ce96ae...), the
`llvm-objdump` listing of 2903, the 2902 D3DDisassemble dump of the blobs, the `.lean` files of the Goldberg install
copy (`%USERPROFILE%\Desktop\pure_vp\Sid Meier's Civilization V`, read-only); no game ran. Code: `render/src/terrainsets/
sets.cpp` (layouts), `c5r/terrainsets/lean.h` (`.lean` loader), `c5r/water/currents.h` (currents map); tests
`render/tests/terrainsets/` (`game_terrainsets_water_sets`, `lean_test.cpp`) and `render/tests/water/currents_test.cpp`. Scratch scripts kept
in `evidence/3600/3602/` (no game data): `cbshift.py` (the row shift of `material_table.txt`), `fpkls.py` (pack lister),
`cmp_raw.py` and `cmp_noise.py` (map comparisons), `fitN.py` (the noise-lattice search of 8.6).

### 8.1 The water sets' buffers [C]

| Set (material), slot | Buffer | Size | Writer | Members |
|---|---|---|---|---|
| WaterShader (91) slot 0 | own ("river World") | 0x80 | river render `0x463640`, through the block of River+0x10 | 0x00 World float4x4 = **identity** (16 floats from a local identity; the river mesh is in world units); 0x40..0x7f never written and never read; the ocean passes declare no cb0 |
| WaterShader slot 1 | shared 26 WaterFrame | 0x250 | `0x739ae0` | unchanged (asset-pipeline.md 3.3; 3604 owns its computation) |
| WaveShader (92) slot 0 | own | 0xf80 = 248 x 16 | Wave_System render `0x643b60` | per wave: (atlas cell u, v offset, age in s, unused), copied per batch from the wave array Wave+0x5c (`memcpy` of count x 16 from the batch's first index); batches of at most 248 (`0x6a6490`: `n % 0xf8`) |
| WaveShader slot 1 | shared 27 WaveFrame | 0x80 | `0x643b60`, per view | 0x00 ViewProj = g_View[v] x g_Proj; 0x40 Wave+0x50 x Wave+0x1c (half size at age 0); 0x44 Wave+0x50 x Wave+0x20 (half size at end); 0x48 1 / Wave+0x24 (lifetime); 0x4c `*(*(0xd90fc4)+4) + 1` (z of the wave plane) [I meaning]; 0x50 Wave+0x28, 0x54 1 / (Wave+0x2c - Wave+0x28) (fade in); 0x58 Wave+0x30, 0x5c 1 / (Wave+0x34 - Wave+0x30) (fade out); 0x60 1 / float(Wave+8), 1 / float(Wave+0xc) (atlas cell); 0x70 (Wave+0x40, +0x44, +0x48, 1.0) tint |
| WorldCurrents (93) slot 1 | shared 43 CurrentsFrame | 0x40 | `0x53a9f0` (this = Water) | 0x00 float [0xd91264], float [0xd91268] (bss, written through a pointer): the grid's plots, Tiny 56 x 40 [L, 8.6]; 0x10 NoiseAmplitude = Water+0x64 = 0.1; 0x20 NoiseCell = Water+0x68 = 2.0; 0x30 GradientScale = Water+0x60 = 0.1 (Water::Startup sets +0x60, +0x64, +0x68 from 0x9e402c, 0x9e402c, 0x9dbbbc) (*register order corrected 2026-11-14 01:15*); no own buffer |

Wave_System's fields come from `WaveSettings.xml` (`0x549c80` reads it into the object; `Resource/Common/EffectData.fpk`,
1,859 B) [C name, file located]. Its texture is **`Wave_Coastline_ALL.dds`** (Wave+0x68, `EffectTextures.fpk`, 43,832 B), not
`wave_coastline_01.dds` as section 1 had it; sampler state 3. The wave update `0x6ab850` adds the frame time to every
wave's age and, past the lifetime Wave+0x24, restarts it at a negative delay drawn from an LCG (`x * 0x343fd +
0x269ec3`, modulo 0xffffcfef) between Wave+0x38 and Wave+0x3c, and picks a new atlas cell (`0x5eb230`). Wave vertex
positions are float16 (`0x6a6490` decodes x, y with `D3DXFloat16To32Array` at a 12-byte stride to get each batch's bounds).

**Question 10 settled.** The blobs' reflection decides it: the WaveShader VS declares `cb0[248]` (0xf80) and `cb1[7]`
(fits 0x80), the WorldCurrents PS `cb1[4]` (0x40) and no cb0. `programs.txt` is right. `material_table.txt` is shifted
by one row: `materials.py` reads the header words from `0x5d1e5a0 + i * 0x520`, which is material i-1's `+0x500` and
`+0x510` in the layout of asset-pipeline.md 3.1 (`0x5d1e5c0 + m * 0x520`), so its `hdr0` and `cbsize` on row i belong to
material i-1 (61 of its 72 immediate sizes equal `programs.txt`'s own size one row up; the other 11 have no own size in
`programs.txt`). The evidence files are left as they are (verbatim outputs).

`validateSet` passes on every blob of the three sets (SM5, 4_1, 4_0); the test `game_terrainsets_water_sets` also checks
one mutated description per check (buffer too small, an uncovered read, a missing array, a missing slot, a texture of the
wrong dimension or missing, a blob not in the section, a blob of the wrong stage, a wrong handle, overlapping members).

### 8.2 Pass choice and pass records [C]

- **Ocean pass = Water+0xe8**, set in Water::Startup `0x5f06a0` from the water-quality global `[0xd90f68]`: 1 -> pass
  2, 2 -> pass 3, anything else (0) -> pass 0. Pass 5 is never selected. 3601 read Water+0xe8 = 3 live (c5h-c, the
  test instances' settings) and Water+0x60/+0x64/+0x68 = 0.1, 0.1, 2.0 [L].
- **River pass** (`0x463640`): 4 when `[0xd90f68]` is 1 or 2, else 1. The river `.lean` pair is loaded only when
  `[0xd90f68]` != 0 (pass 1 reads none).
- "Reflection Texture Raw" is 1024 x 1024 only when the ocean pass != 0 and `[0xd90f98]` == 1; else a 32 x 32 RGBA8
  all-zero texture. "Water Currents" exists only when `[0xd90f68]` > 0 (else handle 0).
- Water::Startup also sets Water+0xcc = 1.43, +0xd0 = 5.0, +0xe0 = 0.03, +0xdc = +0xe4 = 0, +0x6c = 0.5, +0xd4 = 0.008,
  +0xd8 = 0.18, +0x7c and +0x8c from `pow` calls, +0xc8 = its argument x 1/65535 (TerrainSystem::Startup passes 0x32c8);
  other fields come from `0x51f9e0` [C values; meanings for 3604].
- Each draw opens a pass record through `0x8cd6a0(target, a, b, c, name, ...)` (stored by `0x8ccfb0` at record +0x3c,
  +0x40, +0x44): ocean (8, 3, 0, "Water"), river (8, 4, 0, "River Visualization"), waves (4, 10, 0, "Waves"), currents
  (0, 9, 0x80, "WaterCurrent") [C values; I: render-state / sort ids].

**Question 6 (blend, depth and rasterizer states).** The draw objects (`0x8c69b0`, 0x80 bytes) carry buffers, textures,
samplers and counts, not states; the states hang on the pass record words above. From 3601's export `cam1_frame309938`
(Tiny, stock, 2026-11-14) [L]:

| Draw | Blend (RT 0) | Depth | Rasterizer |
|---|---|---|---|
| ocean, pass 3 (6 draws) | SRC_ALPHA / INV_SRC_ALPHA, add; alpha ONE / ONE | test LESS_EQUAL, **no write** | solid, **cull FRONT**, FrontCounterClockwise true |
| river, pass 4 | SRC_ALPHA / INV_SRC_ALPHA, add; alpha ONE / ONE | test LESS_EQUAL, **write ALL** | solid, cull BACK, FrontCounterClockwise true |
| waves (2 draws) | **ONE / ONE, add** (additive) | test LESS_EQUAL, no write | solid, cull NONE |

The same export confirms the layouts of 8.1 by value [L]: the river's cb0 holds the identity in its first 64 bytes and
uninitialised values after it; the wave cb0 records are (0.25, 0.5, age, ~0) (atlas cell offsets of an 8 x 2 atlas);
WaveFrame = ViewProj, (10.24, 2.56, 0.1111, 11.12) (half sizes, 1 / 9 s lifetime, plane z = water height + 1), (0.05,
0.2532, 6.5, 0.4545) (fades), (0.125, 0.5, 0, 0) (atlas 8 x 2), (0.0478, 0.0478, 0.0478, 1) (tint). The D3D buffers
behind slot 0 are larger than the programs' blocks: 256 bytes for WaterShader's 0x80 block, 4,096 for WaveShader's
0xf80 (the ocean draw's slot 0 still holds the terrain tile's block of the previous TerrainShader draw: the ocean passes
do not declare cb0). `sets.cpp` keeps the block sizes.

### 8.3 The `.lean` bump textures (question 7) [C]

Files (Goldberg install copy, 2026-11-13): `Resource/Common/TerrainModels.fpk`: `waterbumps16.lean` 3,495,286 B,
`riverrough16.lean` and `riversmooth16.lean` 218,506 B each, `ice_bump.lean` and `ice_bump_sides.lean` 1,747,661 B each;
`Resource/Common/Upgrade1Models.fpk`: `riverrough_new16.lean` 218,506 B. The EXE loads them by name through its file
system (`0x404790` + the reader `0x8f3820`).

Format (all six files fit it exactly; the reader checks only the magic):

| Offset | Field | waterbumps16 | river files | ice |
|---|---|---|---|---|
| 0x00 | "LEAN" | | | |
| 0x04 | f32, read into the loader's record, not used by Water::Startup [I meaning] | 0.33 | 0.7 (riverrough_new16: 2.5) | 0.184 |
| 0x08, 0x0c | width, height | 512, 512 | 256, 64 | 512, 512 |
| 0x10 | mip count (full chain) | 10 | 9 | 10 |
| 0x14 | bytes per channel | 2 | 2 | 1 |
| 0x18 | offset of the four-channel block | 0x24 | 0x24 | 0x24 |
| 0x1c | offset of the one-channel block = 0x24 + texels x 4 x bpc | 2,796,236 | 174,812 | 1,398,136 |
| 0x20 | u8, read, not used (1); 0x21-0x23 uninitialised (0xcc) | | | |

Each block holds the whole mip chain, largest first, rows tightly packed (texels of the chain: 349,525 for 512 x 512,
21,847 for 256 x 64; file size = 0x24 + texels x 5 x bpc exactly). Water::Startup creates from each file two textures
with the blocks as initial data, full chain: **R16G16B16A16_UNORM** (format word 0xb) from the four-channel block and
**R16_UNORM** (0x38) from the one-channel block (the renderer's bits-per-pixel table `0x8c8db0` confirms the words are
DXGI formats: 0xb -> 64, 0x38 -> 16, 0x1c -> 32). The PS samples the four-channel texture whole and the one-channel
texture's x (LEAN mapping: B.xy and the second moments; the meaning of the channels is [I]).

| Slot | Ocean (`0x739ae0`) | River (`0x6b1580`) |
|---|---|---|
| t0 LEAN1_1 | Water+0x1c = waterbumps16 four-channel | Water+0x24 (0xd3dd30) = riversmooth16 four-channel |
| t1 LEAN1_2 | Water+0x20 = waterbumps16 one-channel | Water+0x2c (0xd3dd38) = riversmooth16 one-channel |
| t2 LEAN2_1 | unbound | Water+0x28 (0xd3dd34) = riverrough_new16 four-channel |
| t3 LEAN2_2 | unbound | Water+0x30 (0xd3dd3c) = riverrough_new16 one-channel |

All with sampler state 0. Which of the two decompiled stack objects is which was settled on the listing (`0x5f0787`
to `0x5f08e5`): the first texture created in each case is the four-channel one, and the handle stores follow it.
`riverrough16.lean` is not loaded by this EXE (the `_new` file is). Other water art Water::Startup loads by name:
`waterdepthcolor_new.dds` (Water+0x14, ocean t6), `river_diffuse_new.dds` (Water+0x18, river t6), `river_alpha_new.dds`
(Water+0x10, river t5).

Loader: `c5r::terrainsets::parseLean`, `leanMip`, `createLeanTextures`, `loadLean` (through `c5r::gamefs`; nothing of
the files is committed, D83). Negative tests for every header check (`lean_parse_rejects`).

### 8.4 The ocean geometry is built every frame (premise P4 fails) [C]

`0x739ae0` rebuilds the ocean mesh on every call, per view (map-wrap copy): it takes the view's visible rectangle
(`0xda1b00..` per view) in plots (divided by the hex spacing 0xb9a6d4 / 0xb9a6d8, rounded outward), clamps it to the map,
and emits **one quad per plot of the rectangle, with no land/water test** (land hides the water by depth). A quad is
centred on the plot with half size 32 x spacing (one plot = 64 samples). Vertex = float4 (world x, world y, u, v) with
(u, v) = (sample x - 896 x page x + 64, the same in y) x (1/1024, -1/1024): the position in the cell's 1024 x 1024 height
page (terrain-bake-passes.md 5.0); stride 16; indices 0, 1, 2, 0, 2, 3 (16-bit). Quads are bucketed by cell (up to 16 x
16 = 256 cells; TR+0x12ac[cell] is that cell's height page, t7) and drawn one draw per non-empty cell. Buffers: **vertex
buffer Water+0x54** (dynamic, `0x8c61e0`, vertex format id 0xe), **index buffer Water+0x58** (dynamic, `0x8c63a0`,
R16_UINT), capacity Water+0x5c quads, grown by x1.2 when short. (Correction 2026-11-13: section 1, the 2903 notes in
`sets.cpp` and the epic brief's P4 said "vertex buffer Water+0x58, built at load"; the buffers were swapped and the mesh
is per frame.) Consequence for the epic: the ocean needs no tap; the server builds the same quads from its own camera.
3601 read the buffers live on 2026-11-13 23:43 (instance c5h-c, save `C5H-3600-TINY_0008`, D33 configuration, one world
frame): Water+0x54 = a DYNAMIC vertex buffer of 131,072 B, format 0xE, stride 16; Water+0x58 = a DYNAMIC R16_UINT index
buffer of 24,576 B; Water+0x5C = 2,048 quads [L] (section 7 has the read).

### 8.5 The currents map [C unless marked]

- **Target**: "Water Currents" `0xda12c4`, created by TerrainSystem::Startup (`0x6b8546`, `0x8c6650(256, 256, 1 mip,
  0x102, ...)`; `0x8bc880` turns 0x102 into R8G8B8A8: texture R8G8B8A8_TYPELESS, views R8G8B8A8_UNORM and _SRGB).
- **When**: once per load, not per frame. `0x53a9f0` is called from Water::Startup, from `0x444870` (which builds the
  currents draw object Water+0x48, called by TerrainSystem's `0x4b2050`) and from a debug hook at `0x406c90` (sets
  Water+0x60 = a x 0.01 (gradient scale), +0x64 = b x 0.01 (noise amplitude), +0x68 = c (noise cell), then recomputes).
- **What**: one draw of WorldCurrents **pass 3** only (passes 0-2 are never drawn) over a full-screen quad of 2
  triangles (draw-object buffer handles 1 and 1), CurrentsFrame as in 8.1, t0 = the draw object's texture handle **0**
  (`0x444870` writes 0 and nothing sets it later), sampler state 0.
- **The pass, as behaviour** (from the blob's code): for a texel at uv,
  1. *noise*: with N = floor(Size / NoiseCell) cells per axis, p = uv x N, cell c = floor(p), f = p - c; each lattice
     corner k gets two signs from a hash: hy = frac(17 c.y / N.y)^2, h = frac(13 (3721 hy + c.x) / N.x)^2, sign x =
     sign(frac(1860.5 h) - 0.5), sign y = sign(frac(930.25 h) - 0.5); d_k = (f - k) . signs_k; with s = smoothstep(f)
     = f^2 (3 - 2f) and D = 6 f (1 - f), X = sx00 + s.x (sx10 - sx00) + (d10 - d00) D.x, Y = sx01 + s.x (sx11 - sx01) +
     (d11 - d01) D.x, Z = sy00 + s.y (sy01 - sy00) + (d01 - d00) D.y, W = sy10 + s.y (sy11 - sy10) + (d11 - d10) D.y,
     n = (Z + s.x (W - Z), X + s.y (Y - X)); noise colour = (0.5 - 0.5 a n.x, 0.5 + 0.5 a n.y, 0, 1), a = NoiseAmplitude.
  2. *flow*: Sobel of t0's red channel at a step of 1 / Size: gx = left column - right column, gy = top row - bottom
     row (weights 1, 2, 1), g = (gx, gy) x GradientScale; flow colour = (0.5 - 0.5 g.y, 0.5 + 0.5 g.x, 0, |g|).
  (Register order of CurrentsFrame corrected 2026-11-14 01:15, see 8.6: NoiseAmplitude = Water+0x64 = 0.1, NoiseCell =
  Water+0x68 = 2.0, GradientScale = Water+0x60 = 0.1.)
  3. result = lerp(noise colour, flow colour, min(|g|, 1)).
  Passes 0-2 [C, not drawn]: pass 0 writes (0.5 - 0.5u, 0.5 + 0.5v, 0, 1), pass 1 the flow colour alone, pass 2 the
  noise colour alone.
- **Settled by 3601's capture (8.6) [L]**: Size = the grid's plots (Tiny 56 x 40); the quad's uv as the server's (uv
  (0, 0) at the top left); the UNORM view (the stored bytes equal the server's UNORM output). **Still [I]**: what
  texture handle 0 resolves to (`0x8bd160` reads the view table `0x414c348` at index 0): a texture whose red channel
  varies only in its corner near uv (0, 0), so the flow term shows only there (8.6); not the height field.
- **Server**: `c5r::water::Currents` runs the EXE's pass-3 blobs (read at run time) into its own 256 x 256 R8G8B8A8
  target; `currentsModel` is an independent CPU model written from the description above, and the tests compare the two.
  Results: 8.6. The game test also writes the GPU maps for Size 56 x 40 and 128 x 84 (null t0) as raw RGBA8 rows to
  `<C5R_WORK>/currents_gpu_<w>x<h>.rgba8`, for the comparison with a capture's "Water Currents" target.

### 8.6 Test results (2026-11-14, workstation GPU, EXE build 4390913 at `C5R_EXE` = the Ghidra copy)

`python render/build.py --build-dir C:/c5-64bit-work/3602/build --test` (clang-cl /W4 /WX), 2026-11-14 00:13 EST: 18 of
18 test executables pass, 0 skipped (`C5R_GAME_DIR` = the Goldberg install copy, `C5R_EXE` = the Ghidra copy). The
numbers below come from running the two test executables directly:

- `c5r_terrainsets_test`: 20 tests, 0 failed, 0 skipped; 18 sets, 342 blob references validate; the three water sets
  validate at every level and every mutated description fails with the expected code (`game_terrainsets_water_sets`).
  `.lean`: `waterbumps16.lean` (`TerrainModels.fpk`, 512 x 512, 10 mips), `riversmooth16.lean` (`TerrainModels.fpk`),
  `riverrough_new16.lean` (`Upgrade1Models.fpk`) (256 x 64, 9 mips) resolve through `c5r::gamefs` with the default
  options, parse, and fill their files exactly; mip 1 equals the 2 x 2 box average of mip 0 within 0.35-0.38 of 65,535
  (mean |d|, channel 0), which confirms the mip addressing on real data.
- `c5r_water_currents`: 9 tests, 0 failed, 0 skipped. GPU (the EXE's pass 3) against the CPU model, 65,536 texels each (start-up parameters with the corrected
  register order, 2026-11-14 01:15):

| Input | Texels equal | Within 1 of 255 | Max |d| |
|---|---|---|---|
| noise only, Size 56 x 40 (Tiny) | 61,029 (93.12 %) | 65,536 (100 %) | 1 |
| noise only, Size 128 x 84 (huge) | 61,447 (93.76 %) | 65,536 (100 %) | 1 |
| a smooth 56 x 40 R32_FLOAT source | 58,674 (89.53 %) | 65,536 (100 %) | 1 |

  Every remaining difference is the GPU storing one step below the model, at texels whose model value x 255 has a
  fraction in (0.5, 0.56]: the GPU's float -> UNORM8 conversion rounds those down, which D3D11's conversion tolerance
  (0.6 of a unit) allows [L, this GPU].
- **Finding [L]: the noise hash is chaotic in the last bit.** With IEEE division and separate multiply-add the model
  matched only 65.7-81.8 % of texels exactly (ties then rounded up), with differences up to 40 of 255; with the division as a multiplication by
  the reciprocal and every `mad` fused (`std::fma`) every texel is within 1 (the C5R_CUR_EXP experiment, 2026-11-14 00:00,
  modes 0-3: max |d| 40 / 25 / 40 / 1). Consequences: the currents map depends on the GPU's arithmetic; a stock capture
  on another GPU or driver may differ from this one in a few percent of texels by up to about 40 steps; the server
  reproduces the EXE's map exactly only by running the EXE's own blob on the same GPU (which `Currents` does), not with
  a CPU port.

**Against the stock capture [L], 2026-11-14 00:50-01:15 EST.** 3601's export `cam1_frame309938` (RenderDoc capture
`c5h-c_20261113-235332_frame309938.rdc`, instance c5h-c on the workstation GPU, Tiny save `C5H-3600-TINY_0008`, stock
arm, VT and folder hash as 3601's section 6 names them) holds the ocean draws' t5 = resource 13350, 256 x 256
R8G8B8A8_TYPELESS, frame-start contents (the map is made at load, so no draw of the frame writes it), SHA-256
`37be5df3...`. The server's map (`Currents::compute`, Size 56 x 40 = the Tiny grid's plots, start-up parameters,
null t0) against it, texel by texel:

| Texels | Count | Equal | Within 1 | Max-channel \|d\| |
|---|---|---|---|---|
| all | 65,536 | 60,175 (91.82 %) | 60,392 | 35 |
| captured alpha = 255 (no flow term) | 60,184 | 60,175 (99.985 %) | 60,184 (100 %) | 1 |
| captured alpha < 255 (flow term active) | 5,352 (8.17 %) | 0 | - | median 19, p95 32, max 35 |

How it was settled: the first comparison (00:50) with the register order of 2903's reading gave 39 equal texels; a
search of the noise lattice on the alpha-255 texels (numpy port of the noise, checked against the server's own GPU map
at 99.8 % within one step) found one exact fit, **28 x 20 cells, no flip, no transpose (100 % within one step)**, the
next best 6 %. 28 x 20 = floor((56, 40) / 2.0): the noise cell is **2.0**, which led to re-reading `0x53a9f0`: register
1 = Water+0x64, register 2 = Water+0x68, register 3 = Water+0x60 (*corrected 2026-11-14 01:15; the first version of
this section and of `sets.cpp` had registers 1-3 = Water+0x60, +0x64, +0x68*). So [L]: **Size = the grid's plots (Tiny
56 x 40, i.e. map height + 4)**, NoiseAmplitude 0.1, NoiseCell 2.0, GradientScale 0.1, and the server's quad
orientation (uv (0, 0) at the top left) is the EXE's.

The 8.17 % that differ are the flow term. They form one rectangle around uv (0, 0) under wrap (columns 0-51 and
236-255, rows 0-55 and 232-255, 72 columns and 76 rows), nowhere else: the texture behind handle 0 has gradients in its
red channel only in its corner near (0, 0) (about the first 18-20 % of u and v, the Sobel step adding one plot) and is
constant elsewhere. It is not the height field (no coastlines show) but whatever texture holds renderer handle 0: an
EXE quirk (the draw object's texture is never set). Its gradient scale is small (0.1), so the corner differs by up to
35 steps. Asked of 3601 (`D:/c5-64bit-work/3600/notes/3602-request-to-3601-currents.md`): which texture the view table
`0x414c348` holds at index 0. Until then the server binds a null t0 and reproduces 91.8 % of the map exactly and 100 %
of the texels outside that corner within one step.

### 8.7 Open after 3602

- The currents map's t0 (8.6): the texture behind renderer handle 0, which makes the corner around uv (0, 0) (8.17 % of
  the texels). Size, parameters and orientation are settled [L].
- The layouts against captured bytes: the ocean, river and wave draws of `cam1_frame309938` were exported while 3602
  ran, but the export was being rewritten at the hand-back (its files moved); comparing the captured cb1 of the wave
  draw (WaveFrame) and cb0 of the river draw (identity) with 8.1 is a five-minute check for 3604 / 3606.
- Question 6 for ocean passes 0 and 2 and river pass 1 (not in this capture's settings), the meaning of the
  pass-record words, the `.lean` header float.

## 9. The mesh tap (task 3603)

Written 2026-11-14 by W3 of team-3600 (issue 3603, branch `task-3603-water-mesh-tap`). Static facts come from an
`llvm-objdump` listing of the Ghidra copy of the DX11 EXE (`%USERPROFILE%\tools\ghidra-projects\bin`, TimeDateStamp
0x546CD0A8, `reverse-engineering-setup.md` 2), read 2026-11-13 23:35-23:50; behaviour only (D83). Live confirmations
are 3601's (section 7; read 1 of 2026-11-13 23:43, instance c5h-c, save `C5H-3600-TINY_0008`).

### 9.1 How a renderer handle becomes a D3D11 buffer [C; L 3601]

The EXE's renderer hands out small integer handles for buffers and draw records; the objects keep the handles, not
pointers (as for textures, `terrain-rendering-inventory.md` 5.1).

| Table | Address | Entry | Filled by |
|---|---|---|---|
| VB handle -> `ID3D11Buffer*` | 0x301A708 + 4h | pointer | VB creation 0x8C6140 (from data; IMMUTABLE unless its last argument is set), 0x8C61E0 (DYNAMIC, CPU write) |
| VB handle -> the EXE's description | 0x3020840 + 0x30h | +0 ByteWidth, +4 Usage (1 IMMUTABLE, 2 DYNAMIC), +8 BindFlags, +0xC CPU access, +0x18 byte "created", +0x24 vertex format id | same |
| vertex format id -> stride | 0x2E49F68 + 4 * id | dword (a BSS table filled at start-up) | live [L 3601]: id 1 = 32, 2 = 20, 9 = 24, 0xA = 32, 0xE = 16 |
| IB handle -> `ID3D11Buffer*` | 0x306ECC8 + 4h | pointer | IB creation 0x8C62F0 (data), 0x8C63A0 (DYNAMIC) |
| IB handle -> description | 0x3074E00 + 0x30h | as the VB's; +0x2C = DXGI format, 57 (R16_UINT) or 42 (R32_UINT), from the creator's "32-bit" argument | same |
| draw record handle -> record | 0x313DE88 + 0x80h | 128 bytes, zeroed by 0x8C69B0, +0 u16 = material id | 0x8C69B0 |

Pools: VB and IB handles are below 0x1800 (6,144; the allocators 0x8C5B50 refuse beyond), draw records below 0x20000.
Draw record fields seen written by the river and wave builders: +0x04 IB handle, +0x08 VB handle, +0x0C primitive
count (indices / 3), +0x18 + 2k = texture handle of slot tk (u16), +0x4C + 2k = a per-slot state word (u16), +0x7C and
+0x7E bytes. The tap checks a buffer pointer by its vtable against D3D11 buffers it created itself (vertex, index and
staging), then the D3D11 description against the EXE's own (ByteWidth, bind flags, stride or index format).

### 9.2 Rivers: RiverVisSystem 0x1A0C694 [C; L 3601]

- Built once per load by CreateTerrain (0x6D51D0 calls 0x560B40 with this = River at 0x6D52A4), also under D33: the
  build is not gated by the River key. 0x560B40 runs the mesh generator 0x738D10 into two temporary arrays and the
  upload 0x6B1580.
- 0x6B1580 first frees the old handles, clears River+0x2C, then: **River+0x1C = VB** (format id 0xA, stride 32,
  IMMUTABLE), **River+0x20 = IB** (32-bit indices, R32_UINT, IMMUTABLE), **River+0x18 = draw record** of material
  0x5B = 91 (WaterShader) with +0x04 IB, +0x08 VB, +0x0C triangles, +0x7C = 0x0E, and texture slots t0-t3 = Water+0x24 /
  +0x2C / +0x28 / +0x30, t4 = [0xDA12C0] (reflection), t5 = Water+0x10, t6 = Water+0x18, **t7 = River+0x24**, t8 =
  [0xDA12B4], t9 = [0xDA12A4], t10 = [0xDA12AC], t11 = [0xD90E60], t12 = [0xD90E9C]; last **River+0x28 = triangles**
  and **River+0x2C = 1**. The render 0x463640 draws only when +0x2C = 1.
- Live on Tiny under D33 [L 3601]: record 0x94BB, VB 0x47F (110,464 B = 3,452 vertices), IB 0x462 (41,280 B = 10,320
  indices = 3,440 triangles = River+0x28). On the huge save GATE2's IMMUTABLE pair (881,600 B + 329,760 B) fits the same
  shape (27,550 vertices, 27,480 triangles) [I].
- Render 0x463640: the river own buffer gets a 4x4 identity matrix in its first 64 bytes (river vertices are world
  space); pass 1 when dword [0xD90F68] > 2, else pass 4 (3602 reads the same dword as the water-quality setting).

### 9.3 Ocean: Water 0xD3DD0C [C; L 3601] - P4 fails

0x739AE0 (this = Water = TerrainSystem+0x2772C, called at 0x6570C0 from the terrain render, behind view bit 0x1) counts
the visible plots per view **every call**, grows **Water+0x54 = VB** (DYNAMIC, format 0xE, stride 16, 4 vertices per
quad), **Water+0x58 = IB** (DYNAMIC, R16, 6 indices per quad) and **Water+0x5C = capacity in quads** when needed, then
maps both and fills them for this frame's views. So the ocean geometry is per frame and view-dependent, not built at
load; under D33 (bit 0x1 cleared) the draw never runs and nothing is there to tap. The server makes this grid itself
(3602's description in its note: one quad per visible plot, bucketed per 896-sample height-page cell; task 3604). The
tap copies only the Water object's 0xEC bytes (fields for WaterFrame, `asset-pipeline.md` 3.3) as a diagnosis record.

### 9.4 Coast waves: Wave_System 0x1A0C6C8 [C; L 3601 for the start-up] - P5 fails

- App start-up 0x549C80 runs only when config bit 0x200000 (`Waves`) is set (test at 0x476861): it makes material 0x5C
  = 92 (WaveShader) at +0x7C, loads `Wave_Coastline_ALL.dds` into +0x68, three constant buffers +0x70..+0x78 and a batch
  +0xAC. Live under D33 every field +0x00..+0xAC is zero [L 3601].
- Per game, GVS::StartupGame at 0x4692B1 sets view bit 0x200000, then tests the config mask: when `Waves` is disabled it
  clears the view bit and **skips 0x54BE70 (a jump to 0x67C170), the wave mesh builder**. So under D33 the wave mesh does
  not exist. The render 0x643B60 (pass A 0x5895F7, pass B 0x7495AE) and the update 0x6AB850 (0x6FBBE1) are all behind
  view bit 0x200000: decision 2 (i) works as far as the code shows (take `Waves` off D33's list, clear view bit
  0x200000), at the price of the start-up's material, texture and buffers (+4 MB in the single-key probe).
- The mesh (0x67C170, chunks made by 0x6A6490): **Wave+0x54 = array of 36-byte chunks, count Wave+0x58**. Chunk: +0x00
  draw record (material 92, +0x7C = 1, +0x7E = 1, t0 = Wave+0x68, state word 3), +0x04 VB (format 0x19, **4,464 vertices**,
  IMMUTABLE), +0x08 IB (R16, **11,904 indices**), +0x0C first wave, +0x10 waves in the chunk (at most 248), +0x14..+0x20
  bounding box (min x, min y, max x, max y) from the vertices' first two half floats. Waves: Wave+0x5C, 16 bytes each,
  count Wave+0x60; the update advances each wave's time (+8) and respawns it with the object's own LCG (seed Wave+0x04,
  `x * 0x343FD + 0x269EC3`, not the CRT's rand). The render copies a chunk's waves into the WaveShader own buffer:
  **248 x 16 B = 0xF80**, the own-buffer size of question 10 [C].
- Under decision 2 (i) the chunks' VB/IB are static per load and the tap takes them; the waves array changes every frame
  in stock (with the view bit cleared it stays at its spawn state), so the tap copies it once, flagged DYNAMIC.

### 9.5 The tap and its image (`shell/composite/water_tap.cpp`, ini `water_tap`, default 0 until 3608)

- **Byte patterns (29)**, checked before the first read, on top of the composite's own EXE validation: the river object
  at its build call; the upload's VB, IB, draw record, material 91, +0x28 and +0x2C stores; the river pass dword; the
  VB and IB creators' description tables, buffer tables, stride table and index-format store; both pool capacities;
  the draw-record table; Water = TerrainSystem+0x2772C and the ocean's +0x54/+0x58/+0x5C stores; the wave object at its
  render call; the render's chunk count and chunk fields; the chunk builder's array, vertex and index counts, format id
  and VB field; the update's wave count and array. Each pattern names the offset or table the tap then reads; a
  mismatch refuses the EXE for good (FAILED REFUSED). `test_water_signatures_match_exe_on_disk` checks all 29 against
  the installed EXE.
- **When:** once per load (the camera block's gridSeq), after the camera has been OK for `water_delay_frames` (60)
  Presents and `water_scene_frames` (30) scene-clear frames have passed (the height-field tap's rule: a reload keeps the
  camera OK through the load screen); again when the server raises requestSeq. *Changed 2026-11-16 by ticket 0408: on
  the first load in a process (the composite's load gate, `WaterTapEnv::firstLoadOpen`) the copy starts with the
  height-field tap, after `tap_first_delay_frames` (3) Presents with the camera OK, under the load screen; reloads keep
  the two counts (`terrain-surface-verification.md` 14.1).*
- **How:** one Present gathers (guarded reads, every handle resolved and checked, every count checked against the EXE's
  own: triangles = record +0x0C = River+0x28, indices within the IB, waves within the chunk) and issues one
  `CopyResource` per buffer into a STAGING buffer; the next Presents map them with DO_NOT_WAIT (a blocking map only
  after 240 Presents); then every index is checked against its VB, the river and wave objects are re-read (a load that
  started meanwhile fails the copy, CHANGED), the image is hashed and written into `Local\VPShellWater.<pid>.<load>.<copy>`
  through views of at most 4 MiB. The ack protocol is the height-field tap's (VPShellTap steps 1-4).
- **Image** (`VPShellWaterImage` 120 B, records of 128 B, data 16-byte aligned), in this order: River object (0x30 B);
  if River+0x2C = 1, its draw record (128 B), VB, IB and a DRAW record (triangle list, indexCount = 3 x River+0x28,
  material 91, pass 1 or 4); the Water object (0xEC B); the Wave_System object (0xB0 B); if there are chunks: the chunk
  array, the waves (DYNAMIC), and per chunk its draw record, VB, IB and DRAW (material 92, pass 0). IB records carry flag
  bit 2 when every index was checked. `VPSHELL_WATER_MAX_RECORDS` was raised from 256 to 1024 (4 records per chunk; the
  chunk count of the huge map is not measured); the layout's offsets did not change.
- **Failures:** a fault reading EXE memory disables the tap for the process (EXCEPTION); a failed check fails this load
  (LAYOUT, retried at the next load); TOO_BIG above `water_max_kb` (64 MiB); SECTION, D3D, ACK_TIMEOUT (60 s), CHANGED,
  KILLED (water_tap=0 during a copy). The composite keeps running in every case.
- **The reader** (`server/src/render/srv_water_tap.cpp`) acks every copy it looked at with its result: stale -3 (load
  != the camera block's gridSeq), open -2, version / sizes -4, hash -1 (each record and the image; a block without an
  image hash is refused), Truncated and BadLayout (c5r::Err) for sizes, offsets, roles, counts, every draw index inside
  its VB, and the map size against the camera block.
- **Tests** (stand-in, 2026-11-14): `shell/tests/test_water_tap.py`, with the reader run as
  `server/tests/render/water/water_reader_test.exe` (built by `build_water_tests.py`). Results and the measured cost:
  9.6.

### 9.6 Results on the stand-in, and what is open (2026-11-14)

- **Tests:** `shell/tests/test_water_tap.py` 43 passed, 0 skipped, with `test_build_cmd.py` 46 passed (00:17, builds
  `C:/c5-64bit-work/3603/shell-build` and `reader-build` of commit cd8dc656; 7 min 24 s). Covered: the synthetic copy
  equal byte for byte to the expected buffers (every record hash recomputed in Python), RELEASED and the section gone
  after the ack, a load without rivers, 8 hostile objects that fail the load with their reason (handle past the pool, a
  non-D3D11 object, sizes that disagree, an index past its VB, absurd chunk count, a wrong draw record, objects that
  change during the copy, a section that cannot be created), a fault that disables the tap for good, the image limit,
  the ack timeout, a new load superseding an unacknowledged copy and the stale refusal of the next copy, requestSeq
  (same load, same bytes), the kill switch before and during a copy, both refusals of the real path, an unknown test
  mode, the 29 patterns against the installed EXE, and 22 reader refusals on crafted sections.
- **Cost on the stand-in (an indication, COMMON 23; test_host at about 6,000 frames a second, 4 KB image, six buffers):**
  composite CPU per copy 1.2-1.7 ms, spread over 3-4 Presents (wall 1.6-2.0 ms from the gather to READY); it is mostly
  fixed cost (six staging buffers, the section, the ini keys), so the river mesh of the huge save (about 1.2 MB) should
  add little [I]. Per-Present cost while idle in test mode 0.07-0.08 ms mean (the test's trigger-file check each
  Present; the real path has no file check) [L stand-in].
- *Superseded 2026-11-14 01:35 by 9.7 (the live part ran after all; this line said it was open for wave 2).*

### 9.7 Live: the tap on the D33 product path, Tiny (2026-11-14 01:25-01:29) [L]

- **Set-up.** Instance c5h-b (C:), driver `shell/evidence/3600/3603/run_3603.py` (3308's product-path driver: D33 with
  Overlay on, VP64Options `ServerRenderer = 1`, the composite and its manifest beside the proxy, no test markers), save
  copy `C5H-3603-TINY_0008` of `c5h-9028-tiny-t0008` (SHA-256 b2ceb663..., turn 8), one load and one reload in one EXE
  process (pid 63028). x64 DLL of the pin `x64-04b1211b-pa803776` (74588135...); **server.exe 14AD994B... and proxy
  24AC8D57... built from this branch** against that pin's dependency build (`build_server.py --deps`, nothing
  relinked), composite 4C316ABD... with `water_tap=1`. Guard checks 0, harness stop rc 0, 15 files restored equal by
  SHA-256, save copies removed, claim released. Evidence (game data, never committed, D83):
  `D:/c5-64bit-work/3600/3603-live/` (`run2/load3_copy1/`, `run2w/load5_copy2/`, `logs/`).
- **The pinned proxy cannot run the product path.** Attempt 1 with the pin's own proxy (a803776cf) died at the end of
  the load at VA 0x8CD492 (the River render with its view bit set): that proxy predates 3305/3308 (it loads the composite
  and writes the view mask only through the test markers `crashlogs\shell.helper` / `viewmask.enabled`, which the product
  layout leaves out). 3311 used its own proxy build for the same reason. A product-path run on the pins needs a proxy and
  server built from a commit that has 3305.
- **The tap on the real EXE:** all 29 patterns match (`EXE patterns match (29)`, EXE at 0x00EB0000). Load 3: `READY load
  3 copy 1: 7 records (1 VB, 1 IB, 1 draws; 151744 buffer bytes), systems 0x2` (river only: Wave_System empty, P5),
  section 153,360 B; reload: `READY load 5 copy 2`, the same 7 records. The river VB 110,464 B (stride 32, 3,452
  vertices) and IB 41,280 B (R32, 10,320 indices) as 3601 read them (section 7 / its note).
- **Against the stock capture** (3601, `cam1_frame309938`, stock on c5h-c, the same save state): **IB equal byte for byte**
  (SHA-256 863be6f2...). **VB: bytes 0-27 of every vertex equal** (POSITION float3, TEXCOORD0 float2, TEXCOORD1 float2);
  **bytes 28-31 differ in all 3,452 vertices**: the layout's COLOR (R8G8B8A8_UNORM at offset 28). The builder writes into
  that slot the bit pattern of the third float of a spline point (0x63D540 evaluates a Catmull-Rom curve over float3
  control points; the colour is that curve's z, a value near 0: stock 0x09FFFFFF / 0x00000000 = 6.2e-33 / 0, D33
  0xB7FFFFFF / 0x00001FBF = -3.1e-05 / 1.1e-41, alternating by bank). **The river VS does not read it**: the capture's
  vs_5_0 (`43ca74a3...`) declares `dcl_input v0.xyz, v1.xy, v2.xy` and no v3. So the tapped mesh equals stock's in every
  byte the shader reads; the four COLOR bytes are near-zero noise whose value depends on the run's configuration (stock
  and 3310's black arm, both with the River system on, give the same bytes bc1edafd...; D33 gives other, stable ones)
  [L bytes, C shader, I why the noise differs: x87 rounding or an unset z of the control points; not traced].
- **Two loads (P6) [L]:** VB and IB equal byte for byte between load 3 and load 5. The River object differs only in the
  three handles (+0x18 record 0x94BB -> 0x1C83, +0x1C VB 0x47F -> 0x274, +0x20 IB 0x462 -> 0x271), the draw record only
  in its IB / VB handle fields, the Water object only at +0x48 (a handle, 0x94D5 -> 0x93C9); the image hashes differ
  through those handles and the load number. Every difference is a renderer handle.
- **Cost (an indication, COMMON 23; the workstation at 100 % CPU from other teams):** composite CPU per copy 2.70 ms (load
  3) and 1.87 ms (load 5), wall 6.4 / 4.5 ms over 3 Presents from the gather to READY; per Present while idle 0.0004-0.0008
  ms mean (windows of 10,000 Presents; max 0.06-0.09 ms after the load, 1.65 ms in the window that held the copy).
- **Behaviour seen:** the reload first sets the camera's grid invalid; the tap then closes the unacknowledged copy of the
  old load (`FAILED CHANGED ... a new load (seq 0)`), and copies the new load once it settles. The pinned and this
  branch's server.exe do not read the water block yet (3607 wires the reader into the scene), so each copy ends in
  ACK_TIMEOUT after 60 s, as designed.
- **Still open:** waves live (needs decision 2 (i): `Waves` off the D33 list; then the chunks' VB format 0x19 stride 12,
  4,464 vertices, are in reach of the same code); the huge save (river mesh about 1.2 MB, cost per copy); the reader
  inside the scene (3607).

## 10. The water module (task 3604)

Written 2026-11-14 by team-3600 W4 (issue 3604). Static reading of the 2903 listing (`llvm-objdump` of the Steam EXE
build 4390913, `D:/c5-64bit-work/2900/2903/dx11.asm`) of the writer 0x739AE0, the view footprint 0x5D0370, Water::Startup
0x5F06A0 and its texture load 0x51F9E0; constants read from the EXE's `.rdata` (values only); the ocean VS/PS read with
`shaderdump --out`. Nothing ran in the game. Behaviour only (D83); the code is `render/src/water/water.cpp`. 3602's
static note and 3601's live read (Water+0x54 a DYNAMIC VB of format 0xE, stride 16; Water+0x58 a DYNAMIC R16 IB; ocean
pass 3 at the owner's settings [L]) agree with 10.2 and 10.3.

### 10.1 WaterFrame, member by member [C]

The writer runs once per frame and fills one WaterFrame per view `v` (g_NumViews 0xD90FA4; the per-view buffer id is
Water+0x34[v]). `f(o)` is the Water field at Water+o (Water = 0xD3DD0C). Single precision (SSE) unless marked x87.

| Offset | Member | Value |
|---|---|---|
| 0x000 | ViewProj | g_View[v] x g_Proj (D3DXMatrixMultiply) |
| 0x040 | OverlayProj | 0xDA1690 + 0x40v, copied |
| 0x080 | WaveScale | k * f(0xE4) * (0.5555556, 0.6666667, 0.1388889, -0.8333333), k = float(sqrt(2.0)) (x87, stored as float) |
| 0x090 | WavePhase | W = 0xD90FD8 - 0xD90FCC (world box width); n = float(trunc(f(0xD4) * W)); s = 1 / W; (s n, (n+1) s, (n+2) s, (n+3) s). **Not time**: a whole number of bump tiles across the map, so the bump texture wraps with the map |
| 0x0A0 | WaveConst | (1.7, 1.2, 1.7, 1.2) |
| 0x0B0 | WaveR11 | f(0xDC) * s n; the VS adds it to every bump texture coordinate: the only time-like member [I: f(0xDC) is the water clock; 0 after start-up, its writer not found] |
| 0x0C0 | WaveR12 | f(0xC8) * HexScale.z (0xB9A6DC, 0.2) * 255: **the water height**, the z of every ocean vertex; f(0xC8) = 0x32C8 * (1/65535), so 10.1167 |
| 0x0D0 | float | f(0xD0) (5.0 after start-up) |
| 0x0E0, 0x0F0 | float3 | f(0x90..0x98), f(0x9C..0xA4) (unit vectors, 10.3) |
| 0x100, 0x110, 0x120, 0x130 | float4 | f(0x70..0x7C), f(0x80..0x8C), f(0xA8..0xB4), f(0xB8..0xC4) |
| 0x140 | FogParams | (-d, exp(-(d * camZ)) * (-1 / d) * [0xDA1C64], 0, [0xDA1C6C]), d = [0xDA1C60]; exponential and products in x87, stored as float; camZ = 0x150's z |
| 0x150 | CameraPos | 0xDA1440 + 0x40v = the inverse view's translation (inverse views 0xDA1410 + 0x40v) |
| 0x160, 0x1A0 | ShadowMatrix1, ShadowMatrix0 | 0xDA1890, 0xDA1790 + 0x40v, copied. *2026-11-16 (0382): 0x160 is the reflection's view-projection (the camera of "Reflection Texture Raw"), not a shadow matrix: 13.6* |
| 0x1E0 | HeightmapUV | Translation(-[0xD90FCC], -[0xD90FD0], 0) x Scaling(1 / [0xD90FE4], -1 / [0xD90FE8], 255 / (0.2 * 65535)); the two reciprocals in x87, stored as float |
| 0x220 | float | [0xD90FA8] = GVS+0x78C, the camera's eased shadow factor (written by 0x523D97); not time |
| 0x230 | FogColor | 0xDA1C50..0xDA1C5C |
| 0x240 | float | f(0x6C) (0.5) |

Nothing else is written (0x0B4-0x0BC, 0x0C4-0x0CC, 0x0D4-0x0DC, 0x0EC, 0x0FC, 0x148, 0x15C, 0x224-0x22C, 0x244-0x24C
keep what the mapped block held; the module writes 0). Before the views the writer checks three art files for a newer
time stamp (Water_SetAutoReload) and reloads them through 0x51F9E0.

*Correction 2026-11-14 (3604) to `asset-pipeline.md` 3.3 and `sets.cpp`: WavePhase was described as "time-derived
offsets from Water+0xd4 and the frame time (0xd90fd8 - 0xd90fcc)"; 0xD90FD8 - 0xD90FCC is the world box width, not a
frame time, and the member holds tile frequencies (`sets.cpp` is 3602's to correct).*

### 10.2 The ocean geometry: built every frame, nothing to tap [C]

P4 fails: Water+0x54 (VB) and +0x58 (IB) are rebuilt by the writer every frame (3601 [L]; `sets.cpp` names them the
other way round, 3602 corrects). Per view:

1. **Footprint** (0x5D0370, when the views are set up): the four lines where the view's frustum side planes meet
   (0xDA1510 + 0x60v: planes 0 and 1 each with planes 2 and 3) are cut with the planes z = 0 and z = 127.5 (the plane
   constant -127.5 at 0x9FD800); the bounding box of the 8 points, clamped to the world box (0xD90FCC..0xD90FDC), is
   stored as min (0xDA1B00 + 0xCv) and max (0xDA1B30 + 0xCv).
2. **Blocks**: each corner becomes a sample coordinate ((x - [0xD90FCC]) / HexScale.x, (y - [0xD90FD0]) / HexScale.y),
   rounded half away from zero (floor(c + 0.5) or ceil(c - 0.5), in double), then divided by 64 toward zero. Blocks
   [x0/64, x1/64 + 1) x [y0/64, y1/64 + 1); the low ends clamp at 0, the high ends at GridHeightmap+0xC - 1 and +0x10 - 1
   (sample counts, so the high clamp never bites). One quad per block, **no land test** (land hides it by depth).
3. **Grouping**: each block belongs to height page (px, py) = (bx * 64 / 896, by * 64 / 896) (16-bit counters per view
   x 256 pages; 4 views at most). A first pass counts, a prefix sum over (view, page) gives each group's start, a second
   pass fills the quads in block order (bx outer, by inner) inside each group. When the total exceeds the capacity
   Water+0x5C both buffers are recreated with capacity total * 1.2 (truncated).
4. **Vertex** (float4, 16 bytes): with h = 32 * HexScale.x, cx = float(bx * 64) * HexScale.x + h + minX, left = cx - h,
   right = h + cx (y likewise with HexScale.y, minY); u = ((x - minX) * (samplesX / [0xD90FE4]) + float(64 - px * 896)) *
   (1/1024), v = ((y - minY) * (samplesY / [0xD90FE8]) + float(64 - py * 896)) * (-1/1024) (the two ratios in x87,
   stored as float). Order (left, top), (right, top), (right, bottom), (left, bottom); indices 4q + (0, 1, 2, 0, 2, 3),
   16-bit (they wrap past 65,535 vertices: 16,384 quads, more than a Huge map's 10,752 plots in one view). (u, v)
   addresses the page's texels (page px covers samples px * 896 - 64 .. + 959, rows bottom-up, `terrain-bake-passes.md`
   5.0), stretched by samplesX / (samplesX - 1).
5. **Draws**: per view its WaterFrame, then per page k = 0..255 with a non-zero count one draw: start index 6 * start,
   primitive count 2 * count; 14 textures: t0/t1 Water+0x1C/+0x20 (the waterbumps16 LEAN pair, 3602), t4 0xDA12C0
   (reflection), t5 0xDA12C4 (currents), t6 Water+0x14 (`waterdepthcolor_new.dds`), t7 TR+0x12AC[k] (the page), t8
   0xDA12B4 (fog ramp), t9 0xDA12A4 (overlay), t10 0xDA12AC (FOW mask), t11 0xD90E60 (TerrainFOW LUT, Generic), t13
   0xDA12BC (shadow); t2, t3, t12 unset. Sampler words: t6 3, t8 5, t11 5 (written twice), all others 0 (t13's from the
   object's zeroing 0x8C69B0). Pass = Water+0xE8. A page past the table ((samplesX + 896) / 896 x (samplesY + 896) / 896
   pages) binds a null t7.

The VS (0x06815A48) reads POSITION float4: world (x, y, WaveR12) through ViewProj; bump coordinates from (x, y),
WavePhase and WaveR11; (u, v) passed on for t7; the overlay coordinate from OverlayProj. The ocean PSs discard where the
FOW mask's x is 0 and index `waterdepthcolor_new` with the page's depth (x 4, saturated) [C disassembly, 2026-11-14].
*Added 2026-11-14 by issue 3957 (epic 0168) from `fog-of-war.md` 3.4 [C, task 3950's read of the 2902 dumps]: before the
TerrainFOW LUT (t11, weight x * (1 - z) as in the terrain), the ocean and river PSs also lerp the colour to a **constant
0.4 grey by the mask's x** (no cloud texture on water); the mask's channels are R explored, G unexplored, B visible
(`fog-of-war.md` 2.2). The river PS reads the same t10 / t11 (rivers share the slot).*

### 10.3 The Water fields after start-up [C]

Water::Startup 0x5F06A0 sets +0x60 = +0x64 = 0.1, +0x68 = 2.0 (constants of the currents pass 0x53A9F0), +0x6C = 0.5,
+0x7C = 2^11.75 (x87 pow), +0x8C = 2^5.75, +0xC8 = arg * (1/65535) (arg 0x32C8 from TerrainSystem's start-up), +0xCC =
1.43, +0xD0 = 5.0, +0xD4 = 0.008 (Water_SetScale writes it), +0xD8 = 0.18 (Water_SetSpeed), +0xDC = 0, +0xE0 = 0.03,
+0xE4 = 0 (so WaveScale is 0 unless something sets +0xE4 [I: a live read decides]), +0xE8 = the pass from [0xD90F68]
(1 -> 2, 2 -> 3, else 0; [I] that it is TerrainWaterQuality; 3601 read 3 live at the owner's setting 2). Then 0x51F9E0 fills
+0x70..+0xC4 from **`watersky.dds`** (TerrainTextures.fpk, 2 x 4 B8G8R8A8, 2 mips): the file is converted to a float
image (0x8CC4E0 format 2 [I: R32G32B32A32_FLOAT]) and its first six texels t0..t5 (row-major from the top left [I])
give +0x70 = t0.rgb / t0.a, +0x80 = t1.rgb / t1.a (+0x7C, +0x8C keep their powers of two), +0x90 = normalize(t2.rgb -
0.5), +0x9C = normalize(t3.rgb - 0.5), +0xA8 = normalize(t4.rgb - 0.5), +0xB4 = t4.a, +0xB8 = normalize(t5.rgb - 0.5),
+0xC4 = t5.a. So "watersky" is a parameter texture (colours and directions), not a sky picture. With the workstation's
install (`%USERPROFILE%/Desktop/pure_vp/...`, 2026-11-14, `c5r_water_water` test `game_art_gives_the_stock_fields`):
+0x70 (0.941, 0.820, 0.400), +0x80 (0.482, 0.678, 0.984), +0x90 (0.399, 0.567, 0.721), +0x9C (-0.103, 0.901, 0.421),
+0xA8 (-0.328, -0.668, 0.668, a 0.706), +0xB8 (0.009, -0.816, 0.578, a 0) [C rule, L file]. **3601's live read of the
Water object** (c5h-c, Tiny `C5H-3600-TINY_0008`, D33, one world frame, 2026-11-13 23:43, its note
`D:/c5-64bit-work/3600/notes/3601-objects.md`) holds exactly these values for +0x60..+0xE0 to the six printed digits,
including the start-up constants, **except +0xDC = 0.145891 and +0xE4 = 0.00607878** [L]: the two members the writer
turns into WaveR11 and WaveScale change after start-up, so something advances them per frame (the clock and a wave
amplitude [I]); their writer is not found. `stockWaterFields()` is the live object for every other field [L]. The reflection input t4 is "Reflection Texture Raw" (1024 x 1024 when the pass is not 0 and
[0xD90F98] == 1, else a 32 x 32 zero texture).

The water clock: no time enters WaterFrame except f(0xDC) [C for the writer]. Who advances +0xDC (and whether
+0xD8 is its rate) is not traced [I]; question 4 and decision 4 stay with 3601.

### 10.4 What the module draws (`render/src/water/water.cpp`, 2026-11-14)

`c5r::water::Renderer` builds the ocean mesh by 10.2 from the camera (the footprint from ViewProj with the same two
z planes), WaterFrame by 10.1, the height pages with the bake's own `buildHeightPage` (11 levels), and draws with the
EXE's VS and the pass's PS (default 3). Inputs from other seams are replaceable through `setTexture`: *since issue
3609 (2026-11-14, 10.7)* t0/t1 the install's waterbumps16 LEAN pair (3602's loader), t4 zero (the EXE's own
no-reflection texture; decision 3 (ii)), t5 3602's currents map (computed at the first draw after a height field), t9
transparent (*since 3704, 2026-11-14: the server's scene binds the EXE's Terrain Overlay target there, with each view's
OverlayProj through `FrameInputs::overlayProjWrap`; `overlay-system.md` section 10*), t10 white (visible; *since epic
0168, 2026-11-14: the server's scene binds `c5r::fow::MaskRenderer`'s mask there with `setTexture(Slot::FowMask)` for the
ocean and the rivers, in the overlay pair's window; no module change, test `c5r_water_fow_mask` (`fog-of-war.md` 7.4,
task 3953); the scene wiring is task 3954, live in 3957*), t11 the TerrainFOW_Generic_ColorKey LUT, t13 zero (no shadow), and the DistanceFog
defaults of stock with the zoom fraction from the camera's height. *Before 3609: t0/t1 flat LEAN (zw 0.5, xy 0), t5 0x80
grey, t11 identity, t13 white, fog density 0: the cause of the live look of 3609.* The art: t6 `waterdepthcolor_new.dds` with an sRGB view (sampler state 3 takes the second view), t8
`fogramp.dds`, the fields from `watersky.dds`. Pass states: those of the stock capture (10.6) [L]: blend SRC_ALPHA /
INV_SRC_ALPHA with alpha ONE / ONE, depth LESS_EQUAL without write, cull FRONT with FrontCounterClockwise (*corrected
2026-11-14 01:30; was "alpha INV_SRC_ALPHA, no culling [I]"*). Map wrap: views T(-width) x view
and T(+width) x view are added when the field wraps [I: the EXE's view order].

Exactness by construction: every WaterFrame member computed in SSE is bit-equal; ViewProj (D3DX's summation), CameraPos
(an inverse) and FogParams.y (x87) may differ in the last bit or two when computed from the view (A3's tolerance: 2 ulp
relative; given the EXE's camera position and ViewProj they are bit-equal). The footprint is computed in double from the
frustum, so a block boundary can move only when a footprint corner lies within a float ulp of a 64-sample line; the
quad that differs then lies at the view's edge.

Tests (2026-11-14, `c5r_water_water`, 13 tests, 0 skipped, clang-cl Release, hardware device): WaterFrame against a
hand-computed case (every member; bytes the writer skips are 0), the stock fields and the sky rule, the footprint of a
down-looking camera and its clamp, hand-computed quads and page grouping, 16-bit overflow and the 16-page limit, every
API check's negative test, and a draw of each ocean pass (0, 2, 3, 5) with the EXE's programs that covers every pixel of
an all-water view. Against the stock capture: 10.6.

### 10.5 watershot (`render/tools/watershot/`, 2026-11-14)

What it does: reads 3601's capture export (`D:/c5-64bit-work/3600/captures/<capture>/`, format v1 of
`captures/FORMAT.md`, section 6), and per water draw (default the ocean draws):

- `watershot replay EXPORT --exe EXE` replays the draw with the capture's own shaders, constant buffers, vertex and
  index buffers, textures, samplers and states into a target holding the capture's "before" contents, and compares
  the result with the capture's "after" target: share of pixels equal, the rest by largest channel difference in 8
  classes (1, 2, 3-4, 5-8, 9-16, 17-32, 33-64, 65+), median, p95, max (the 3404 comparator's pattern,
  `terrain-bake-comparison.md`). A difference here is the replay path (GPU, driver, a state the export lacks), not the
  module.
- `--module --game DIR` puts the module's own WaterFrame (10.1) and ocean mesh (10.2) in place of the capture's
  (textures stay the capture's), and reports per draw whether the module's batch has the capture's quad count and how
  many quads are bit-equal: the module's exactness in pixels.
- `watershot frame EXPORT --exe EXE --game DIR [--gt DIR | --tap FILE]` checks each view's captured WaterFrame member by
  member against `computeWaterFrame`. Inputs taken from the block (marked "input"): ViewProj (as the camera), OverlayProj,
  the shadow matrices, CameraPos, fog colour and parameters, 0x220, and the fields +0xDC, +0xE4, +0xC8 solved from
  WaveR11, WaveScale.x, WaveR12 (the float within 8 ulp whose forward formula gives the captured bits). Everything else
  (WavePhase, WaveConst, WaveScale y-w, the copied fields, HeightmapUV, 0x240) is computed from the stock fields and the
  world (the height field when given, else solved from HeightmapUV [I]).
- `watershot live --camera CAM.json (--gt DIR | --tap FILE) --exe EXE [--game DIR]` draws the module's ocean at a
  ground-truth camera (the epic 3000/3200 camera JSON) into a PNG (a diagnostic picture: no terrain, constant inputs).

Exit 0 report, 2 usage, 4 input missing or unparsable, 6 device or set-up. Key names it reads from `state.json` and the
texture files (agreed with 3601 through `D:/c5-64bit-work/3600/notes/3604-watershot-input.md`): `call`, `args`
(D3D11 argument names), `ia` {`topology`, `elements` (D3D11_INPUT_ELEMENT_DESC names), `vb` {slot: {stride, offset,
file}}, `ib` {format, offset, file}}, `vs`/`ps` {`file`, `cb` {slot: {size, file}}, `srv` {slot: {format (the view's),
file}}, `samp` {slot: D3D11_SAMPLER_DESC names}}, `om` {`blend` (D3D11_BLEND_DESC names, `RenderTarget[0]`,
`BlendFactor`, `SampleMask`), `depth_stencil` (D3D11_DEPTH_STENCIL_DESC names, `StencilRef`), `rt` [{format, w, h}],
`dsv`}, `rs` {`rasterizer` (D3D11_RASTERIZER_DESC names), `viewports`}; texture JSON {format, width, height, depth,
mips, array_size, dim, written_in_frame, subresources [{offset, row_pitch, slice_pitch}]}; per draw `rt_before_0` and
`rt_after_0`. Not reproduced: depth contents (cleared to 1 when a depth target is bound), scissor.

Test (`c5r_watershot_test`, 2026-11-14): the test writes an export of the module's own ocean draw (EXE programs, the
page, eleven input textures with known contents, the module's samplers and states) in this format; its replay is
equal in 6,144 of 6,144 pixels with the capture's parts and with the module's parts; a WaterFrame with the water 40
units higher changes pixels; the frame check finds all 22 members equal; and each reader check has a negative test.

*Reader adapted 2026-11-14 01:05 to the shape 3601's exporter really writes (its first export, seen at 00:40): enums
as `{"v": n, "s": name}`, contents under `data` (`data.file`, `data.size`), stages `VS`/`PS` with `shader.file`, state
objects `{id, desc}` (desc null in that first pass), render targets `om.rtv[]` with `view_desc` / `res_desc`, draws named
`draws/<nnnn>_candidate/` before classification. watershot classifies a candidate as ocean when its VS bytecode is the
EXE's ocean VS 0x06815A48; without `draws.json` it indexes the `draws/` folders. Both shapes are tested.*

### 10.6 Against the stock capture (2026-11-14, partial)

Capture `c5h-c_20261113-235332_frame309938.rdc` by 3601 (c5h-c on C:, Tiny `C5H-3600-TINY_0008`, stock, 32-bit pin
`i0ship-26234371-ia`, RenderDoc 1.47, frame 309938, 2554 x 1411), its export `D:/c5-64bit-work/3600/captures/frame309938/`
as it stood at 00:40-01:04 (draw states and buffers; no textures, targets or state descriptors yet; the exporter then
restarted).

- **Ocean draws** [L]: 6 draws with the ocean VS (`draws/0350..0355`), 16, 56, 4, 44, 154 and 11 quads (index counts
  96, 336, 24, 264, 924, 66) with contiguous start indices 0, 96, 432, 456, 720, 1644: one draw per page batch, as
  10.2. The colour target is **multisampled** (`D3D11_RTV_DIMENSION_TEXTURE2DMS`, R8G8B8A8_UNORM) with a depth target
  bound: a pixel-exact replay needs the same sample count and the depth contents. watershot replays a multisampled
  target with the export's sample count (`res_desc.SampleDesc.Count` or `samples`), every sample filled from the
  resolved "before" target, and resolves the result for the comparison (test: a 4x replay of the synthetic draw equals
  the single-sampled one in 6,143 of 6,144 pixels, the other by 1); depth contents are not exported (cleared to 1; open).
- **WaterFrame (A3)** [L]: `watershot framefile .../buffers/907_9b1e0e1f.bin` (the ocean draws' VS cb1, 592 B) against
  `computeWaterFrame`: **19 of 22 members bit-equal**. The world box solved from the block is the Tiny map's exactly
  (-1791.5, -1108.047)..(1791.5, 1108.047), samples 3584 x 2560. Per-frame inputs solved from the block: +0xDC =
  765.640, +0xE4 = 31.901 (the live object at another moment held 0.146 and 0.006: both change at run time), +0xC8 =
  0.198367 (= 13000/65535, stock), fog density 0.05, camera (352.85, -854.34, 640.32). Members computed by the module
  and equal: WaveScale, WavePhase, WaveConst, WaveR11, WaveR12, +0xD0, +0x90 (WaterColorA), +0x70, +0x80, +0xA8,
  HeightmapUV, +0x6C. Not equal: +0x9C and +0xB8 (two normalised watersky vectors, 1 ulp in two components each,
  relative 7e-8 and 1.5e-7: D3DX's normalise is not reproduced; in double 2 of 4 vectors are exact, in float 1 of 4,
  with the CPU's rsqrt not tried) and FogParams.y (relative 1.1e-6: the x87 exponential). Tolerance for A3: bit-equal
  except these three members, within 2e-7 relative (normalised vectors) and 2e-6 (FogParams.y).
- Found on the way [L]: the texel conversion is byte * (1/255) in float (not byte / 255): with it +0x70 and +0x80 became
  bit-equal (`water.cpp` skyTexels).
- worldFromGrid equals the EXE's globals 0xD90FCC..0xD90FE8 bit for bit on both ground truths (Tiny 3584 x 2560: box
  +-1791.5 x +-1108.047, width 3583, height 2216.094; Huge 8192 x 5376: +-4095.5 x +-2327.375, 8191, 4654.75) [L, epic
  3000's `grid_constants.json`].

**The finished export `cam1_frame309938` (01:20; 22 draws: 13 reflection, 6 `ocean_p3`, 2 wave, 1 `river_p4`;
`"replay": false`, so no targets yet):**

- **The ocean mesh is exact** [L]: `watershot replay .../cam1_frame309938 --module` builds the module's mesh from the
  captured ViewProj and the world solved from the block, and compares it quad by quad with the captured vertex buffer:
  **285 of 285 quads bit-equal**, in 6 batches with the capture's pages and counts (pages 1, 2, 3, 17, 18, 19: 16, 56,
  4, 44, 154, 11 quads), so the footprint (computed in double from the frustum), the block rounding, the grouping and
  the vertex arithmetic of 10.2 reproduce the EXE on this camera.
- **WaterFrame**: `watershot frame` on the same export: 19 of 22 bit-equal, as above.
- **The module's parts in pixels** [L]: each ocean draw replayed twice on the workstation's RTX 4080 (same textures,
  states and start), once with the capture's WaterFrame and vertex / index buffers and once with the module's: equal in
  100 %, 99.9998 %, 100 %, 99.9998 %, 99.9996 %, 100 % of 3,603,694 pixels (draws 350-355); the 29 differing pixels
  differ by 1 (the three 1-ulp members). What is left for the pixel acceptance is the replay path itself against the
  EXE's target (needs the export's replay step).
- **The black water was the flat LEAN pair, not the reflection input** [L, diagnostic; *corrected 2026-11-14 by issue
  3609, 10.7; this line said "the water's colour comes from the reflection input: with the module's zero t4 the PS
  gives black water, so decision 3 (ii) needs a real sky input"*]. The black picture (vc-20261114-001938-workstation-b024)
  had the module's constant inputs of 10.4, among them the flat LEAN t0/t1 whose zero second moments drive the PS to
  saturated black and white; with the install's waterbumps16 LEAN pair and the other 3609 inputs, the module's zero t4
  replays the capture's ocean draws equal in 99.78-100 % of the pixels (10.7). Stock's t4 holds only models and ice on
  (0,0,0,0) (13.3), so the zero t4 is right for decision 3 (ii).
- **Pass states of the ocean** [L, draw 350]: blend SRC_ALPHA / INV_SRC_ALPHA with alpha ONE / ONE, write mask 15;
  depth on, LESS_EQUAL, no write, stencil off; rasterizer solid, cull FRONT with FrontCounterClockwise, depth clip on;
  every PS sampler slot 0-13 MIN_MAG_MIP_LINEAR (addresses per slot); a D24_UNORM_S8_UINT depth target is bound.
  The module now uses these states (it had no culling and alpha INV_SRC_ALPHA before, 2026-11-14 01:30). PS slots 2, 3
  and 12 are bound in the capture (left over from earlier draws; the ocean PSs do not read them).
- One view (g_NumViews 1) at this camera although the map wraps: the EXE adds wrap views only when needed; the module
  adds them whenever the field wraps, and their footprints clamp to empty (no quads) when off the map [I].

**Second camera, export `cam2_frame265572`** (01:40) [L]: 2 ocean draws (pages 3 and 19, 16 and 40 quads): mesh 56 of
56 quads bit-equal; WaterFrame **20 of 22** bit-equal (FogParams equal this time; the two normalised watersky vectors
differ by 1 ulp as before); module parts against capture parts: 2 and 3 of 3,603,694 pixels differ, by 1.

Open at this writing: the per-draw pixel comparison against the EXE's own target (the export's replay step:
rt_before / rt_after, and the target's sample count), and views with map-wrap copies (both cameras had one view).

### 10.7 The live look of the sea (task 3609, 2026-11-14)

Written 2026-11-14 02:29-03:30 EST by team-3600 W4 (issue 3609, branch `task-3609-water-live-look`). Symptom: in the
joined product (3607's live run, 13.5) the sea was white near the coasts and black beyond. Method: offline first (E5),
with watershot on 3601's export `cam1_frame309938` (Tiny `C5H-3600-TINY_0008`, stock, 32-bit pin
`i0ship-26234371-ia`), then two live runs on c5h-b (C:).

**The cause** [L, replay on the workstation's RTX 4080]. `watershot replay <export> --module --camera 3601-cam1.json
--own <slots> [--legacy <inputs>]` replays each of the six stock ocean draws (350-355) with the scene's own WaterFrame
(built from the camera JSON as the scene builds it: stock fields, the block's +0xDC / +0xE4, the module's fog and shadow
values) and the module's own textures in the named slots, against the same draw with the capture's inputs. Largest
channel difference per pixel, per draw (median / p95 / max over the draw's pixels that differ):

| Module inputs | Draws 350 / 351 / 353 / 354 / 355 (352 is equal in all rows) |
|---|---|
| as before 3609 (`--legacy lean,currents,shadow,fog`) | median 86 / 77 / 190 / 133 / 84; max 139-255 |
| only the flat LEAN pair as before | median 97 / 191 / 162 / 147 / 84; max 229-255 (saturated black and white) |
| only the fog defaults as before (density 0, grey) | median 0 / 1 / 13 / 13 / 28; max up to 81 |
| only the neutral currents (0x80 grey) as before | median 1-2; max up to 77 |
| only the white shadow t13 with the old shadow blend 0 | no effect: the same as the next row |
| **all fixed (3609)** | **350, 352, 355 equal in 100 %; 351, 353 in 99.9997 % (by 1); 354 in 99.78 % (max 77)** |

The PS of pass 3 reads t0 (RGBA16, four channels) and t1 (R16) as a LEAN pair: mean slope and second moments. The flat
pair has second moments 0, so the PS's variance term is 0 and its specular divides by it: the sea goes to saturated
black and white (test: 73.18 % of a flat-LEAN draw's pixels pure black or white, 0.00 % with the install's pair). The
fog term was the second cause (density 0 and a grey colour: the far sea grey-black). The footprint is not a cause: the
module's mesh from the camera JSON is the capture's (285 of 285 quads, 10.6). OverlayProj and the shadow matrices
differ (identity in the scene) but the PS reads t9 / t10 / t13 at constant textures, so they do not matter there.

**The fixes** (`render/src/water/water.cpp`, `water.h`): t0/t1 = the install's `waterbumps16` LEAN pair through 3602's
`terrainsets::loadLean` / `createLeanTextures` (512 x 512, 10 mips, R16G16B16A16 / R16); t5 = 3602's currents map
(`c5r::water::Currents`, 256 x 256, computed at the first draw after `setHeightField`; `Options::computeCurrents`);
t11 = `TerrainFOW_Generic_ColorKey.tga` through the bake's `buildColorKeyLut`; t13 = zero (no shadow; stock's darkening
is 0.67 x t13.x x shadow blend); `FrameInputs::shadowBlend` = 1 (stock's 0x220); `Fog` defaults = stock's DistanceFog
[L both captures]: colour (146, 182, 243) / 255, density 0.05, scale 5e-5, and w (WaterFrame 0x14C, the fog ramp row) =
`zoomFraction(camera height)` = (zoom - 11) / (20.5 - 11) with zoom = ln(height / 50) x 6.6667 [C: the camera
controller 0x742920 writes 0xD90FAC from its fields +0xF4 / +0xF8 / +0xFC; L: equal to both captures' 0x14C]. Under D33
the EXE's DistanceFog system is off, so the server uses these values. `Renderer::inputTexture(slot)` returns the
module's own view per slot (tests). Tests: `render/tests/water/ocean_inputs_test.cpp` (4 tests: the zoom fraction, the
fog and shadow defaults, the game inputs' formats and the currents' 1 x 1 -> 256 x 256 after a draw, the flat-against-
stock LEAN pixel check), with `c5r_water_water` 14 and `c5r_water_river` 6.

**The coast waves in the scene** (decision 2 (i), 12; `server/src/render/srv_water_scene.cpp`). The tap's VPWSYS_WAVE
records (the Wave_System object at exeOffset 0, the chunk array at Wave+0x54 of 36 bytes per chunk, the wave array at
Wave+0x5C of 16 bytes per wave, and per chunk its draw record (0x80 bytes, also Wave+0x54, skipped by its size), VB,
IB and draw) are parsed by `ParseWaveRecords` (k-th wave draw = chunk k; R16 indices cut to the draw's count; the
uninitialised fourth float of each wave set to 0) and handed to 3606's `WaveRenderer` (`setChunks` once per image).
Per drawn frame, after the ocean and the rivers: `updateWaves` with dt = the server clock's step since the last drawn
frame (at most 0.25 s; 0 when frozen), then one `WaveRenderer::draw` per ocean view with that view's ViewProj (the
WaterFrame's first 64 bytes [I: the EXE's WaveFrame and WaterFrame hold the same ViewProj]) and plane z = the image's
water z + 1. A wave part that does not parse leaves the ocean and the rivers drawn and logs its reason once
(`no coast waves (...)`); a failing wave draw counts `waveErrors` and logs once per distinct error. The per-second line
gains `w_wave_chunks`, `w_wave_draws`, `w_wave_errors`. Scene tests (`water_scene_test.exe`, `test_water_scene.py`):
`waves_drawn` (chunks, order ocean-then-waves, plane z 11.117, first frame by 0, 0.1 s later the ages +0.1 and the
cycle-passing wave restarted at -2.10 s in cell (0.25, 0.5) as 12.1's seed-0 sequence, a 5 s gap advanced by 0.25 s,
freeze holds the ages), `waves_bad` (no chunk array, a wave array of the wrong size, R32 indices, no wave renderer, a
failing wave draw: the ocean drawn every time), and `real_module_draws` now draws a tapped wave chunk with the EXE's
WaveShader on WARP. All 15 cases pass, also under MSVC ASan (`build_water_tests.py --asan`, 0 reports); `server/tests/render` 101 passed, `render/build.py --test` 23 of 23, 0 skipped (2026-11-14 03:10-03:22).

**Live, the joined product against stock** [L, 2026-11-14 03:10-03:18 EST]. Instance c5h-b (C:, claimed under 3600),
save copy `C5H-3609-TINY_0009V` of c5h-c's `C5H-3600-TINY_0009V` (turn 9, every plot revealed; SHA-256 27420a41...).
Driver `shell/evidence/3600/3609/run_3609.py`; each arm restored by SHA-256 at its end (`backup_restore_ok` true both).

- Stock arm (`run_3609.py stock`): the 32-bit pin `i0ship-26234371-ia` (3c2f47da...) in the CP slot,
  `darkred.py config --arm stock`, no proxy and no server. Server arm (`prep` / `run` / `restore`, 3308's product-path
  driver): server.exe, proxy and composite of this branch at 83ce8698's code on the x64 pin `x64-04b1211b-pa803776`
  (DLL 74588135), D33 configuration with `Waves` on (decision 2 (i)), `water_tap=1`.
- Cameras, the same procedure in both arms: `UI.LookAt(plot)` then wheel ticks at the window centre until the zoom
  level reads the target (a tick steps the zoom by 2.0 from the LookAt's 14.2556, so stock's 17 of 3601's cam1 is not
  reachable: cam1 = plot (33, 14) at 16.2556; cam2 = plot (51, 14) at 11, 3601's), Esc until the top band is undimmed
  (0 rounds needed), the mouse parked over the minimap. The two arms' view matrices differ by at most 0.090 (cam1) and
  0.025 (cam2) per element (the camera's own easing), projections equal: the pairs are "approximate" references.
- The server drew the module's water from the tap in every frame after the load (`w_why=MODULE` 90 of 90 seconds
  after it): `image used (meshes 0xA ..., waves 3 chunks / 533 waves)`, `w_wave_chunks=3 w_wave_draws=9` (3 chunks x 3
  ocean views: the map wraps), `w_wave_errors=0`.
- `compare_3609.py` over the water pixels (largest channel difference; mask = water plane z 10.1167 over samples below
  it, minus a 16 px frame; the interface and hills in front not removed):

| Camera | Water pixels | Median | p95 | Max | Within 8 |
|---|---|---|---|---|---|
| cam1 (open sea and coast) | 1,965,463 | 7 | 34 | 218 | 55.1 % |
| cam2 (a bay inside a city's border) | 292,186 | 18 | 110 | 217 | 34.7 % |

  Mean colour over three cam1 regions, server against stock: far sea (139.2, 168.6, 213.9) / (139.5, 168.9, 214.3);
  deep sea (57.7, 72.4, 99.2) / (57.7, 72.4, 99.2); shallow water (52.6, 88.6, 112.6) / (52.8, 88.7, 112.6). So the colour,
  the depth gradient, the fog and the coast surf match stock; what is left per pixel at cam1 is the wave pattern's phase
  (the server's own clock, decision 4 (ii)) and the waves' phase. At cam2 most of the difference is the city border
  (stock draws its line and fill over the water; the overlay is not in the server's water, seams E/F) and the sea
  resources' models (D33 draws them dark red); in the bay below the border the mean is (42.5, 84.8, 103.2) against
  stock's (49.0, 89.8, 103.9) (median 5, p95 48 over 84,000 pixels): a darker red channel near the city, not explained
  [I: candidates the models' reflections in stock's t4, the border's glow].
- Pictures (Visual Checks, all DIAGNOSTIC, D112): server vc-20261114-032023-workstation-5b1e (cam1) and
  vc-20261114-032024-workstation-6650 (cam2), each with its stock pair and the difference image; stock
  vc-20261114-031327-workstation-d7ba (cam1), vc-20261114-031352-workstation-6a47 (cam2); the offline replays of draw 354
  per old input vc-20261114-031525..031528 (5 entries). Files under `D:/c5-64bit-work/3600/3609/{live2,stock,pics_cam1_*}`.
- The first live run of the fixes (02:52-02:56, before the waves, `D:/c5-64bit-work/3600/3609/live/`) showed the same
  blue sea; its cam1 stopped at zoom 18.26 (the old target 17 oscillated), so it is not a pair.

Not verified: other maps and zoom levels, the map-wrap views on screen, the reflection of models (decision 3 (ii) leaves
them out), timing (no frame-rate claims from the workstation).

*Pointer 2026-11-14 (issue 0188, `terrain-surface.md` 6) [L, 3601's export `cam1_frame309938`, ocean draw 350 and river
draw 356, WaterFrame of both]: (a) the WaterFrame's FogColor (0x230) blue is 0x3f73f3f5 = 243 * (1/255) in float;
`water.h`'s `Fog` default 243.0f / 255.0f is 0x3f73f3f4, one ulp lower (red and green agree) - not corrected here
(`water.h` is outside 0188's files; a one-character fix: `243.0f * (1.0f / 255.0f)`). (b) The water's FogParams.y
(0x144) is 0xa365f51b at camera 1, the value of the product density * camZ rounded to float as `water.cpp` computes it:
right for the water; the terrain's DistanceFog block (another writer) is 0xa365f52b, the product in extended
precision.*

## 11. Rivers in the module (task 3605)

Written 2026-11-14 by team-3600 W4 (issue 3605, branch `task-3605-water-rivers`). Sources: the river render 0x463640 in
the 2903 listing (static, behaviour only, D83), the draw record of 9.2, 3601's exports `cam1_frame309938` and
`cam2_frame265572` (Tiny `C5H-3600-TINY_0008` / `0009V`, stock, c5h-c, 32-bit pin `i0ship-26234371-ia`), the river
shaders' D3DDisassemble (`shaderdump --out`). Code: `render/src/water/river.cpp` (`RiverRenderer`, `loadRiverArt`,
`riverColorKeyFile`), the call in `water.cpp`, API in `water.h`; tests `render/tests/water/river_test.cpp`; checks in
`render/tools/watershot/watershot.cpp` (`replay --system river --module`).

### 11.1 The stock river draw [C + L]

- **One draw per frame, view 0 only** [C]: 0x463640 draws the whole mesh once (when River+0x2C = 1) with no loop over
  the views. Its material binds shared buffer 26, and Water+0x34 = 0x1A = 26 [L 3601] is the buffer the ocean writer
  fills for view 0 (views 1 and 2 use the ids at Water+0x38..+0x40 = 0x134..0x136). So the river takes **view 0's
  WaterFrame** and is not drawn in the map-wrap copies [C; the wrap behaviour not seen live]. In both captures the
  river's cb1 is the same buffer contents as the ocean draws' (`buffers/907_9b1e0e1f.bin` in cam1) [L].
- **Pass** [C]: 4 when `[0xD90F68]` (TerrainWaterQuality) is 1 or 2, else 1; 4 at the owner's settings [L, both captures].
- **Own buffer (cb0)**: the first 64 bytes are the identity (the mesh is in world units) [C, L]; bytes 64-127 are never
  written (left from earlier use; the VS reads cb0[0..3] only [C]).
- **Inputs of the VS** [C]: POSITION xyz, TEXCOORD0 xy, TEXCOORD1 xy; the vertex's COLOR (bytes 28-31, the noise 9.7
  describes) is not read. Captured input layout: POSITION R32G32B32_FLOAT @0, TEXCOORD0 R32G32_FLOAT @12, TEXCOORD1
  R32G32_FLOAT @20, COLOR R8G8B8A8_UNORM @28; R32 indices; one triangle list of 10,320 indices (Tiny).
- **Textures** [L, cam1 draw 356]: t0 / t1 riversmooth16 `.lean` (256 x 64 R16G16B16A16 / R16, 9 mips), t2 / t3
  riverrough_new16, t4 the reflection target, t5 `river_alpha_new.dds` (128 x 32 BC3, 6 mips), t6
  `river_diffuse_new.dds` (256 x 64 BC1, 9 mips, **sRGB view**; VT's copy where VT is installed), t7 River+0x24 =
  **a 32 x 32 RGBA8 texture whose texels are all zero** (frame-start contents, not written in the frame), t8 the fog
  ramp, t9 the overlay, t10 the FOW mask, t11 the TerrainFOW LUT, t12 the **Rivers ColorKey, style Generic** (32^3),
  t13 an 8 x 8 texture left from earlier draws. **Neither river PS reads t7 or t13** [C: pass 4 PS 0x06826EA0 declares
  t0-t6, t8-t12; pass 1 PS 0x06819F60 t5, t6, t8-t12], so River+0x24, NULL under D33, does not matter to the picture;
  the module binds a zero 32 x 32 texture as stock has.
- **Samplers** [L]: MIN_MAG_MIP_LINEAR on every slot; clamp on t5, t6, t8, t11, t12, wrap elsewhere.
- **States** [L]: blend SRC_ALPHA / INV_SRC_ALPHA (alpha ONE / ONE); depth test LESS_EQUAL **with write**; cull BACK with
  FrontCounterClockwise (the ocean culls FRONT).
- **Rivers ColorKey files**: this install has only `Assets/ColorKeys/Rivers_Generic_ColorKey.tga`; for the other four
  styles ColorKeySystem keeps its identity volume (asset-pipeline.md 3.5). The draw record binds `[0xD90E9C]` = the
  Generic entry whatever the map's style [C, 9.2].

### 11.2 What the module does

`RiverRenderer` loads the EXE's river VS 0x06816A20 and the PSs of passes 1 and 4, the art through `c5r::gamefs`
(`loadRiverArt`: the two `.lean` files with 3602's loader, both DDS files, the five ColorKey volumes by the terrain bake's
TGA rule `bake::buildColorKeyLut`, identity where a file is missing), takes the tapped mesh with `setMesh` (stride 32,
R32 or R16, triangle lists, every index checked against the vertices), and draws each tapped draw record with the
stock states and samplers, the shared inputs of the ocean (t4, t8-t11) and view 0's WaterFrame. `Renderer::draw` calls
it after the ocean when a river mesh is set (`FrameStats::riverDraws`, `riverPassUsed`); `Options::riverPass` -1 takes
the tap's pass (4 at the owner's settings), `riverStyle` picks the ColorKey (default Generic, as the EXE).

### 11.3 Against the stock capture (2026-11-14) [L]

`watershot replay <export> --system river --module` (workstation RTX 4080, inputs from
`%USERPROFILE%/Desktop/pure_vp/Sid Meier's Civilization V` and, as a cross-check, c5h-c's install read-only):

- **The module's river inputs equal the capture's byte for byte** on both cameras: t0-t3 (the `.lean` blocks, all
  mips), t5 and t6 (the DDS pixel data, all mips; with both installs: their river files are equal), t7 (zero), t12 (the
  Rivers Generic ColorKey volume from the TGA by the bake's rule: the rule holds for this ColorKey type too), and the own
  buffer's World (identity).
- **Pixels**: the river draw replayed with the module's WaterFrame in place of the capture's, everything else the
  capture's: **3,603,694 of 3,603,694 pixels equal** on camera 1 (draw 356) and on camera 2 (draw 218). (The two
  WaterFrame members that differ by 1 ulp, 10.6, did not change a river pixel.)
- The mesh is the tap's: 3603 showed the tapped VB and IB equal to the capture's in every byte the VS reads (9.7).
- Diagnostic picture: Visual Checks `vc-20261114-015930-workstation-0cdd` (`D:/c5-64bit-work/3600/3605/cam2_replay/`).

Open: the comparison against the EXE's own target per draw (the export's replay step is still missing, 6.2), and the
river in the joined product (3607 wires the tap's mesh into `setMesh(System::River, ...)`). Where the frame-level
comparison of 3608 shows the river bed through the river water, the cause is bug 0130 (the river-bed decal of bake
stage 2, decision 5), not the river draw: the river PS blends over whatever the terrain drew there. *Confirmed
2026-11-15, epic 0166 (section 4): the river bed is **TerrainDecalSystem decals** (Riverbank and its two end caps, priority 0.4, `riverbank_D*.dds` + `grey.dds`; Floodplains with end caps on flood-plain rivers), made from the map by 0x559FF0 through 0x60F180, deterministic across loads and present under D33, and drawn in the tile bake into the blend targets before NormalCalculate (terrain-decals.md 2.2, 4.4) [C, L]; the server's decal pass draws them bit-equal to the EXE on the river tile B_river (terrain-decals.md 11.4) [L]; the joined product gets them when the decal tap is wired into the bake (4105).*

## 12. Coast waves (task 3606)

Written 2026-11-14 01:41-03:00 EST by W2 of team-3600 (issue 3606, branch `task-3606-water-waves`). Method: the
listing and decompile of 3602 (`0x643B60`, `0x6AB850`, `0x5EB230`, `0x6A6490`), 3601's two capture exports
(`cam1_frame309938`, `cam2_frame265572`, Tiny save `C5H-3600-TINY_0008`, stock, instance c5h-c), the install copy's
`WaveSettings.xml`; no game ran for the offline part. Code: `render/include/c5r/water/waves.h`, `render/src/water/
waves.cpp` (namespace `c5r::water`: `parseWaveObject`, `waveObjectFromSettings`, `computeWaveFrame`, `updateWaves`,
`waveAtlasCell`, `WaveRenderer`), tests `render/tests/water/wave_test.cpp`; script `evidence/3600/3606/
cmp_wave_capture.py`.

### 12.1 What the waves are [C, L where marked]

- **Settings.** `WaveSettings.xml` (`Resource/Common/EffectData.fpk`) fills Wave_System. Its attributes, and where they
  land in the object (the offsets are the readers' [C]; the attribute -> field mapping is [L]: every WaveFrame member
  made from it equals the capture's): CrestPagesU / CrestPagesV / CrestPagesInUse -> +0x08 / +0x0C / +0x10 (8, 2, 16);
  StartDistance / EndDistance -> +0x1C / +0x20 (0.08, 0.02, fractions of the hex diameter +0x50 = 128); CycleTime ->
  +0x24 (9 s); FadeInStart / FadeInEnd / FadeOutStart / FadeOutEnd -> +0x28..+0x34 (0.05, 4.0, 6.5, 8.7 s);
  RestartDelayMin / Max -> +0x38 / +0x3C (2.1, 5 s); Color_R/G/B -> +0x40..+0x48 = (float(Color / 255)) ^ float(2.2)
  (64 -> 0.04777576; the EXE rounds the base and the exponent to float before `pow`: only that rounding gives the
  capture's bits). WavesPerEdge (1.13) is the builder's (0x67C170, not ported). The LCG seed Wave+0x04 is 0 [L by the
  pages below; I: that it is RandomSeed].
- **WaveFrame** (shared id 27, 0x80) as 0x643B60 writes it, with names corrected (*section 8.1 called 0x40 / 0x44
  SizeMin / SizeMax; they are the start and end distance of the wave's travel, x hex diameter; corrected 2026-11-14*):
  ViewProj; (StartDistance x D, EndDistance x D, 1 / CycleTime, plane z); (FadeInStart, 1 / (FadeInEnd -
  FadeInStart), FadeOutStart, 1 / (FadeOutEnd - FadeOutStart)); (1 / pagesU, 1 / pagesV, -, -); (tint, 1). **Plane z
  = the water height + 1** (capture: 11.116731 = float(10.116731 + 1), the WaterFrame's 0xC0 water height) [L].
- **The vertex shader** (blob 0x0682C580): each vertex = a wave's centre (POSITION, float16 xy) and a corner (TEXCOORD
  .xy in {0, 1}) and its uv in the atlas cell (.zw); position = centre - (2 corner - 1) x lerp(StartDistance,
  EndDistance, age / CycleTime) x D at z = plane z; uv = cell offset (the wave record's u, v) + .zw / pages; fade =
  sat((age - FadeInStart) x rate) x (1 - sat((age - FadeOutStart) x rate)). The PS outputs (texel x fade x tint) ^
  (1 / 2.2), alpha 1; the blend adds it (ONE / ONE).
- **The update** (0x6AB850, every frame while view bit 0x200000 is set): age += dt for every wave; a wave **past** the
  cycle time (age > CycleTime) restarts at age -(RestartDelayMin + r x (Max - Min)), r = a / 0xFFFFCFEF with a =
  lcg(state), and takes atlas page lcg(a) % CrestPagesInUse (`0x5EB230`: u = (p % U) / U, v = (p / U) / V); state =
  lcg(a) for the next restart; lcg(x) = (x x 0x343FD + 0x269EC3 mod 2^32) mod 0xFFFFCFEF. **The state starts from the
  seed at every call and is never stored back**, so the k-th restart of any frame always gets the same delay and page.
  With seed 0: the first restart of a frame gets page 10 = cell (0.25, 0.5) and delay -2.102 s, the second page 4 = (0.5,
  0) and -4.490 s, the third page 14 = (0.75, 0.5) and -3.942 s. **The captures show exactly this [L]**: per chunk
  201-232 of 248 waves sit in cell (0.25, 0.5), 12-32 in (0.5, 0), a few in (0.75, 0.5) and others; the lowest age in
  both captures is -4.490. The pages are all even (u in multiples of 0.25): lcg(lcg(x)) keeps x's parity. So stock's
  surf uses mostly one crest page of the 16: an EXE property the server reproduces, not a defect to fix.
- **The draw** (per view, per chunk whose bounds meet the view's rectangle): the chunk's waves copied into the own
  buffer, one DrawIndexed of the whole index buffer (11,904 indices for a full chunk; cam2's partial chunk 1,776), t0 =
  `Wave_Coastline_ALL.dds` (256 x 256 BC1, 9 mips, BC1_UNORM view, sampler linear clamp), states as below [L].

### 12.2 The capture checks (2026-11-14, `cmp_wave_capture.py`) [L]

Six wave draws (cam1: 508, 509; cam2: 278-281), the server's values from `WaveSettings.xml` through `computeWaveFrame`
(identity ViewProj, plane z = 10.116731 + 1):

| Check | Result |
|---|---|
| WaveFrame 0x40..0x7F (the members the server computes) | **equal bit for bit** in all six draws, after the tint rounding of 12.1 (the first run used double: tint 1 ulp low) |
| WaveFrame 0x68..0x6F (never written by the EXE) | 0 in cam1; stale non-zero bits in two cam2 draws (the CPU block is reused, and the VS reads only .xy of that register) |
| input layout, stride, index format, topology | POSITION R16G16_FLOAT @0, TEXCOORD R8G8B8A8_UNORM @4, BLENDINDICES R8G8B8A8_UINT @8, stride 12, R16, triangle list: as the server |
| blend, depth, rasterizer, sampler | ONE / ONE add (colour and alpha), depth LESS_EQUAL without writes, solid, cull none, FrontCounterClockwise, depth clip; MIN_MAG_MIP_LINEAR clamp: as the server |
| t0 | BC1, 256 x 256, 9 mips (`Wave_Coastline_ALL.dds` of EffectTextures, 43,832 B = 128 + 43,704) |
| draw call | DrawIndexed of the whole chunk (11,904 or 1,776 indices), as the server |

**Not compared: pixels.** The export has no per-draw render targets (no replay step, 6.2), and the waves are added
onto the ocean in the same multisampled target, so the wave pixels cannot be separated from the final frame without
the target before the draw. The server's wave pixels are checked against their own definition instead (12.3).

### 12.3 Tests (`c5r_water_wave`, `python render/build.py --test`)

Synthetic: the object parser and every check (truncated, page counts 0 or too many, cycle 0 or NaN, fades of zero
or negative span, delays reversed, hex diameter 0, an infinite tint); the settings parser (comments skipped - the
shipped file names attributes in its comment -, missing and unparsable attributes, a fractional page count, an
unclosed comment); WaveFrame member by member and its NaN / null refusals; the atlas cells; the update (no restart
under or at the cycle time, a restart's delay range and page, the chained state, the same output on every call,
negative and NaN dt). With the EXE and the install: the renderer's refusals (no texture, chunks with a short vertex
buffer, a partial triangle, an index past the vertices, 0 or 249 waves, waves past the array, no target); one wave
drawn with the EXE's WaveShader into a 64 x 64 target: lit only inside its square (half size from the distances),
additive (a second draw adds, never lowers), invisible while waiting (negative age); the install's settings with seed
0 give the three first restarts above.

### 12.4 Decision 2 (i) in the configuration (granted by the MC, #455)

- `shell/tools/darkred.py`: `Waves` moved from `D33_OFF` to `D33_ON`, so a D33 configuration writes `Waves = 0` (only
  removing it from `D33_OFF` would leave a `Waves = 1` from an earlier run in the instance's config.ini, and the stock
  arm would then run without waves).
- `proxy/src/proxy_shell.cpp` `ClearBits`: while the composite is loaded (the Terrain rule), view bit 0x200000 is
  cleared with Terrain's, so the EXE builds the wave chunks (start-up and per-game builder run) but neither updates nor
  draws them. `proxy/tools/rt_tests/shell_unit.cpp` line 352 expects 0xFFDFFFFE accordingly.
- 3601's measurement of this configuration (one load each, EXE private bytes 1,417.1 MB with Waves started against
  1,418.3 MB without) is **an indication only** (COMMON 23): one load per arm, inside the noise.

### 12.5 Live: the wave tap under decision 2 (i), Tiny (2026-11-14 02:03-02:08) [L]

- **Set-up.** Instance c5h-c (C:, claimed under 3600, released), driver `shell/evidence/3600/3603/run_3603.py` (3603's,
  unchanged: 3308's D33 product path with Overlay on, `water_tap=1`), save copy `C5H-3603-TINY_0008` of
  `c5h-9028-tiny-t0008` (SHA-256 b2ceb663...), one load and one reload in one EXE process (pid 3180). x64 DLL of the pin
  `x64-04b1211b-pa803776` (`--deps D:/c5-64bit-work/9045/bF/deps`); **server.exe, proxy and composite built from this
  branch at ea8ca38d** (`build_server.py --src D:/c5-64bit-work/9045/src-x64f --config release`, `shell\build.cmd`).
  config.ini by `darkred.py` with this branch's lists: `Waves = 0`. Restore: 8 files equal by SHA-256, 7 created files
  absent again, the save copy removed (no full verify, D110). Dumps: `D:/c5-64bit-work/3600/3606-live/run1/` (D83).
- **The proxy holds the view bit** (`shell: view mask` lines): config 0xFF240667 (Waves on), clear 0x00200001; at the
  load GVS::StartupGame sets bit 0x200000 and the watcher clears it 10 ms later (`cleared 0x00200000`).
- **The tap carries the waves:** `READY load 3 copy 1: 21 records (4 VB, 4 IB, 4 draws; 383,872 buffer bytes), systems
  0xA` (river + wave), the same at the reload (load 5).
- **Against the stock captures:** the three chunk vertex buffers equal byte for byte the captured ones (SHA-256 647d19eb...,
  de590cc4..., 1eb49959...), and the index buffers too (a873aafa..., the same for all three chunks). The Wave_System object
  holds the settings of 12.1 with **seed 0** and the tint's bits equal the capture's.
- **The waves array is at its spawn state and does not move:** ages spread over 0.001-8.997 s, every one of the 16 pages
  used (33-34 waves each). Load 3 against load 5: u, v and age equal in all 533 waves (only the unused fourth float
  differs, uninitialised); so no update ran between the loads (the view bit holds) and the spawn is deterministic.
  Stock's frames show the steady state of 12.1 instead (most waves on page 10): the server's update reaches it after
  about one cycle (9 s plus the delays) from the tapped spawn.
- Not done live: the server drawing the waves in the frame (needs the reader in the scene, 3607) and a picture.

### 12.6 Checks of the decision-2 lines (2026-11-14) [L]

`proxy/tools/test_proxy_rt.py --build <this branch's server_build>`: 79 of 79 checks (unit, alt, loader, guard, shell;
`shell_unit` with the new expected mask 0xFFDFFFFE). Shell suite (`pytest shell/tests`, this branch's shell build): 445
passed; the 28 failures and 14 errors all came from auxiliary builds the run had not made (C5_WATER_READER,
C5_FRAME_BUILD, C5_RENDER_TEST_BUILD) and two timeouts of `test_tagged` under the machine's load; with those builds the
four files (`test_water_tap`, `test_join_ring`, `test_lockstep`, `test_tagged`) passed 95 of 95. `test_d33_config` 13 of
13 with Waves in D33_ON. render: 21 of 21, 0 skipped; MSVC ASan of the water tests 4 of 4, 0 reports.

## 13. The water inside server.exe (task 3607)

Written 2026-11-14 by W3 of team-3600 (issue 3607, branch `task-3607-water-into-server`).
`server/src/render/srv_water_scene.{h,cpp}`, called from the terrain scene (`srv_render_scene.cpp`); the option in
`srv_render_host.cpp`.

### 13.1 What the scene does

- **Owns** the water tap's reader (9.5) and the water module (`c5r::water`, 10) behind a small interface
  (`WaterModule`), so that the stand-in tests run its decisions without a GPU.
- **Per frame, before the terrain draw,** it decides whether the module draws or the terrain module's flat placeholder
  plane stays (`c5r::terrain::Options::water`). The module draws when four things hold: it initialised; it has the
  height field of the camera's load; the tap delivered an image of that load; and that image's Water object is usable.
  Otherwise the plane is drawn, and the reason is logged once per load and reason (`water scene: load seq N: placeholder
  plane (NO_TAP | NO_MODULE | NO_FIELD | OTHER_LOAD | NO_FIELDS ...)`), never LOUD: a missing tap or module is a fallback.
- **Draw order and depth [L 3601, 6.3; 10.6]:** the water is drawn after the terrain into the terrain's colour and depth
  targets, depth not cleared. The module sets each pass's states: the ocean tests LESS_EQUAL without writing depth; the
  river writes depth.
- **Meshes:**
  - The river mesh goes to the module as tapped, with the river own buffer (identity World, 9.2) as its constants.
  - A system's chunks (waves: one VB/IB pair per 248 waves) are joined into one VB and one IB. The draws get baseVertex
    and startIndex offsets.
  - The ocean has no mesh to tap; the module builds it per frame (10.2).
- **Ocean pass:** Water+0xE8 of the tapped object (3 at the owner's settings), else the module's default.

### 13.2 The water clock: decision 4 (ii), final (MC #447, 2026-11-14)

- **The EXE's water clock stands still under D33** [L, 3601, section 7]. Water+0xDC and +0xE4 advance only while the
  EXE's own water draw runs, so under D33 they stay at their start-up values (0.145891 / 0.00607878).
- The server therefore advances both fields itself, from the tapped values, at the EXE's measured rates:
  - +0xDC by 4 x Water+0xD8 per second: 0.721/s at the default speed 0.18 [L 3601];
  - +0xE4 by Water+0xE0 per second: 0.03/s [L 3601].
  - The capture's 765.64 / 31.90 (10.6) are both about 1,062 s of this clock [I: consistent with the rates].
- **Phase equality with stock is therefore by chance.**
- `render_water_freeze=1` (server.cfg, diagnostics) holds both fields at the tapped values, as P12's data write
  (Water+0xD8 = +0xE0 = 0) freezes stock. MC #447: done-B diagnostics freeze both arms; ACCEPTANCE pictures show running
  water.
- `vp_frame.h` is not used.

### 13.3 The reflection input: decision 3 (ii)

- The scene binds t4 = a 1024 x 1024 RGBA8 target cleared to (0,0,0,0). That is stock's "Reflection Texture Raw" as it
  is wherever no model or polar ice is reflected (3601, 6.3), at the size stock makes it when the ocean pass is not 0.
- **Why an empty target is exact [C, the ocean PS of pass 3, capture shader `2324_8f808623`, read with `fxc /dumpbin`]:**
  the PS reads t4 in four bilinear taps around a position, averages them, and uses the result in only two
  places (*corrected 2026-11-16 by issue 0382: the position is not a screen-space one; it is the world position
  projected by WaterFrame 0x160 (0xDA1890, the reflection's view-projection), see 13.6; this line said "around a
  screen-space position"*):
  - an additive reflection term, `rgb += (avg.rgb)^2.2 * cb1[36].x`;
  - the output alpha, `o.w = sat(avg.a * k + k)`.
  With (0,0,0,0) both reduce to what stock gets where its target is empty.
  - The captured target (`13376`, frame-start contents) is (0,0,0,0) in 1,045,128 of 1,048,576 texels; the rest are
    models.
  - Stock and the server therefore differ only within the four-tap footprint of reflected models and ice. That is an
    intended difference (no models in the server, increment 3).
- **3604's black sea is not caused by the empty t4** [C by the same reading]. In 3604's live diagnostic the sea drew
  black with the module's 1 x 1 zero t4, and blue in a replay with the captured t4. But the captured t4 is the same
  (0,0,0,0) wherever the replay showed sea, so the zero t4 cannot explain the black. The cause must be another input of
  `watershot live` (the flat LEAN pair, the neutral currents, the fog defaults, or the camera) [I, not traced here]. The
  live run (13.5) shows what the product draws.

*2026-11-16 (issue 0382): decision 3 (ii) stays for the models (increment 3); the polar ice is now drawn into the target
(13.6).* *(2026-11-16, recorded by W-4470 for epic 0375: the server's units are drawn into the same target too when
`render_units=1` and `render_units_passes=1`, through the water's models hook `WaterSceneReflectionModels` (called in
13.6's reflection pass before the ice hook, with the same target and reflection camera; `unit-server-scene.md` 7), so
"no models in the server" above holds only with the units off; the EXE's other model systems stay out of t4.)*

### 13.4 The option (server.cfg, `srv_render_host.cpp`; MC #443 grant)

- `render_water`:
  - `1` (default): the module, with the plane as the fallback;
  - `placeholder`: the plane only, with the tap still read and acked;
  - `0`: no water.
- `render_water_freeze`: `0` or `1`.
- Any other value refuses the configuration with its key, like the other `render_*` keys.
- The mode and the freeze switch are parsed beside `HostConfig::water`, in two file-scope values of the host. The
  struct is in `srv_render_host.h`, which 3607 does not hold; they move into it at its next change.
- The per-second line `terrain 1s:` gains a ` | water 1s: w_mode= w_why= w_draws= w_placeholder= w_cpu_ms= w_load=
  w_meshes= w_tap_ok= w_tap_refused= w_tap_result=` part. The totals line gains `water totals: ...`.

### 13.5 Tests and live (2026-11-14)

- **Stand-in, scene level:** `server/tests/render/water/water_scene_test.cpp` (14 cases) through
  `test_water_scene.py`: 16 passed, 0 skipped. Each case has its own fake camera block and fake water tap section,
  published as shell_composite.dll publishes them, and a fake module that records its inputs. The cases:
  - no tap;
  - the module draws (river mesh, own buffer, t4 bound, ocean pass 3, fields, clock source);
  - the clock over 2 s (+1.44 / +0.06) and frozen;
  - a refused image (hash, acked -1);
  - a stale load (acked -3);
  - an image of another load;
  - a NaN camera and a camera of size 0;
  - a module that does not initialise (no LOUD);
  - render_water=0 and placeholder (the tap still acked);
  - two wave chunks joined;
  - the reason logged once over 100 frames;
  - the real `c5r::water` module with a missing EXE;
  - the real module with the installed EXE and game drawing one frame on WARP into a 64 x 64 target with depth.
- **Stand-in, host level:** `test_water_host.py`, through render_host_test.exe, which has the render units of
  server.exe: 7 passed, 0 skipped. (2026-11-15, ticket 0369: these runs are event-driven now, not 20-25 s long: the fog's
  10 s install wait comes before the first frame the water scene sees; `renderer-in-server-verification.md` section 14.)
  - The refused key values.
  - render_water=0 and placeholder.
  - With the installed game: the plane until the water image arrives (NO_TAP, logged once), then the module's water
    (`w_draws` > 0, `water totals: draws N draw errors 0`), the tap acked 0, no LOUD water line.
  - A stale image: acked -3, and the plane stays.
- **Under MSVC ASan:** water_scene_test.exe 14 of 14 cases, 0 ASan reports (with the real module on WARP). The
  host-level tests were not run under ASan.
- **Live (2026-11-14) [L]: two joined loads on Tiny, D33 product path.**
  - Set-up:
    - instance c5h-b; the driver `shell/evidence/3600/3607/run_3607.py` (3603's, with one diagnostic picture);
    - save copy `C5H-3603-TINY_0008` of `c5h-9028-tiny-t0008`;
    - one load and one reload per run;
    - the x64 DLL of the pin (74588135...); server.exe and proxy built from this branch on the pin's dependency build
      (the pinned proxy cannot run the product path, 9.7);
    - composite 7322ED08... with `water_tap=1`; `render_water` at its default 1.
  - **Run 1 (02:03-02:08):** server.exe 33287C0E..., proxy 03988A9A...
    - Each load's image was read and acked 0, about 1 s before the terrain field (`water: tap load seq 3 copy 1 read: 7
      records ... acknowledged in 1.9 ms`). The DLL logged RELEASED, so the server's ack closed the section.
    - Then `the water module draws (meshes 0x2: rivers tapped, waves none; the ocean from the field)`.
    - 32,061 water draws, 0 placeholder frames, **0 LOUD lines**.
  - **Run 2 (02:14-02:19):** the same with 3605's rivers merged; server.exe D2493E19..., proxy 3442CE8E...
    - Per second at the load view: `w_ocean_draws=4 w_ocean_quads=132 w_pages_missing=0 w_river_draws=1 w_ocean_pass=3
      w_river_pass=4 w_gpu_ms=0.053`, with water CPU 0.03 ms per frame (indications, COMMON 23).
    - 38,902 water draws, 0 placeholder frames, **0 LOUD lines**.
  - Guard checks 0, harness stop rc 0 and restore by SHA-256 (9 equal, 6 absent) in both runs.
  - Evidence: `D:/c5-64bit-work/3600/3607-live/run{1,2}/` (render.log, composite log, pictures).
- **What the picture shows (diagnostic, Visual Checks vc-20261114-020750-workstation-b205 and
  vc-20261114-021804-workstation-3499, both `fail` for the look):**
  - Water is drawn by the module and no flat plane remains.
  - The tapped river is drawn (run 2).
  - The sea is not stock's. It is white near the coast. Beyond a straight edge it is black: the ocean's 132 quads cover
    less than the visible sea, and outside them nothing is drawn over the terrain's dark sea floor.
  - Both are inside the module (its footprint, and its colour with the constant inputs: flat LEAN pair, neutral
    currents, fog off), not inside the scene. They go to 3604 / 3608.
  - The scene's own parts behave as designed: draw order, depth, placeholder off, clock, tap, and 0 LOUD.
- Seen on the way: during a reload the camera block's grid is invalid for a moment, and the scene then keeps drawing the
  previous load's water, as the terrain keeps its previous field. The log line reads `load seq 0: the water module
  draws` (cosmetic).

### 13.6 The reflection camera and the reflected ice (issue 0382, 2026-11-16)

**"Reflection Texture Raw" is not a mirror image [C, L].** The EXE draws the models and the polar ice into it with an
**orthographic camera that looks along a fixed direction** over view 0's ground footprint, and the ocean and river PSs
read it at the **world position projected by WaterFrame 0x160**. WaterFrame 0x160 (0xDA1890 + 0x40v) was named
"ShadowMatrix1" in 10.1 and in `asset-pipeline.md` 3.3 / `sets.cpp`; it is the reflection's view-projection, written by
the second half of the shadow set-up `0x615950`. Static reading (2903's listing, behaviour only, D83):

| Step | What `0x615950` does (view 0 rectangle 0xDA1B60 min / 0xDA1B90 max, x y z) |
|---|---|
| centre | c = ((B60 + B90) * 0.5) per component, SSE; z is (0 + 0) * 0.5 = 0 (below) |
| window | l = (B60.x - B90.x) * 0.5, b = (B60.y - B90.y) * 0.5 (r = -l, t = -b) |
| shadow (first half) | eye = c + 5000 * L, L = TerrainSystem+0x7572C = (0.4330, -0.25, 0.8660) (angles 150 / 30 deg); LookAtRH(eye, c, (0,1,0)) x OrthoOffCenterRH(l, -l, b, -b, 30000, -30000) -> ShadowMatrix0 0xDA1790 |
| reflection | eye = c - 5000 * D, **D = TerrainSystem+0x75744 = (-cos 80 deg sin 135 deg, -sin 80 deg sin 135 deg, cos 135 deg) = (-0.12279, -0.69636, -0.70711)**; view = D3DXMatrixLookAtRH(eye, c, (0, 1, 0)) -> **0xDA1990**; proj = the same ortho -> **0xDA1A90**; view x proj -> **0xDA1890** |
| wrap views v >= 1 | 0xDA1890 + 0x40v = (g_View[v] x inverse view 0 0xDA1410) x 0xDA1890 = T(shift_v) x 0xDA1890; 0xDA1990 + 0x40v likewise |

- **The depth is reversed [C, checked by the test]:** zn 30000 > zf -30000, so depth = z_view / 60000 + 0.5 falls with
  the distance from the eye (the centre at 5000 gives 0.41667). Under the ice's LESS_EQUAL the surface furthest along D
  wins: the target shows the scene as seen from below, which is what makes it a "reflection" without a mirror matrix [I
  for the intent].
- The imports: `0x8A86E0` = D3DXMatrixLookAtRH, `0x8A86D4` = D3DXMatrixOrthoOffCenterRH (import slot 0x9D6B60), `0x8A868C`
  = D3DXMatrixMultiply. Constants: 0.5 (0x9FCF90), 5000 (0x9DC2C8), up.y 1 (0x9F72F4), zn 30000 (0x9F1588), zf -30000
  (0x9FCFDC) [C].
- The three directions (+0x7572C sun, +0x75738 the ice's second light (0, -0.4226, 0.9063), +0x75744 the reflection) are
  written at TerrainSystem start-up `0x6B83FA` from double radians (30/150, 25/90, 135/80 degrees) and again by `0x457DFD`
  from the float degrees +0x75718..+0x75724 (90, 25, 80, 135) [C]; no other writer but the zeroing at `0x733A43`. So the
  reflection direction is a constant in play [I: no Lua or settings path to +0x75718 was looked for].
- The rectangle B60 / B90 is the footprint of 10.2 step 1 **before** its clamp to the world box, with z = 0 at both ends
  (`0x5D0370` stores the unclamped box there and the clamped one in B00 / B30) [C].
- The pass (`0x5A0F80`, from view bit tests 0x40, 0x20, 0x2, then the ice behind 0x1): runs when [0xDA12C0] (the target)
  and [0xD90F98] are not 0; clears colour (0,0,0,0), depth 1.0, stencil 0 [C]; then the systems draw with 0xDA1990 /
  0xDA1A90 (the ice: `polar-ice.md` 7, reflection flag).
- **The water's read [C, the pass-3 PS]:** uv = (p.x / p.w * 0.5 - 0.5, p.y / p.w * -0.5 - 0.5) with p = (world, 1) x
  WaterFrame rows 0x160..0x190 (columns x, y, w), plus 6 x the bump normal's xy, then four taps at +-2 r around it (r from
  the LEAN variance), averaged, `^2.2`, times cb1[36].x (added to the colour); alpha = sat(avg.a * k + k). The sampler is
  s4 (wrap), which is why the offset of -0.5 works.
- **Checked [L, 2026-11-16]:** `c5r::water::reflectionCamera` (below) from the scene's view-projection against four stock
  captures: 4301's Tiny loads 1 and 2 (the reflected ice draws 86 / 88: IceFrame rows 0-3 and the eye in row 12, from the
  scene ice draws' rows 0-3) and 3601's cam1 and cam2 (WaterFrame 0x000 -> 0x160): **largest relative difference 9e-8 to
  1.3e-6 per element**, the eye within 0.0002 world units. In both 3601 frames WaterFrame 0x160 equals the reflected ice
  draw's rows 0-3 bit for bit. The wrap rule is [C] only (neither capture has a wrap view in its WaterFrames). Scripts:
  `render/evidence/0382/check_rule.py` (scratch emulation), the test `c5r_water_reflection` (below).

**The module (`render/include/c5r/water/water.h`):** `reflectionDirection`, `reflectionCamera(sceneViewProj, out)`
(view, proj, viewProj, centre, eye, half sizes; BadLayout for a matrix not finite, fewer than the 8 footprint points, a
zero-size rectangle or a result not finite), `kReflectionSize` 1024; `Renderer::draw` gives its wrap views T(shift) x
`FrameInputs::shadowMatrix1` (before 0382 they took view 0's matrix unchanged; no pixel changed then, t4 being empty).
LookAt and the ortho follow D3DX's formulas in float; bit-equality with D3DX is not claimed (the relative differences
above).

**The scene (`server/src/render/srv_water_scene.cpp`):** t4 is a 1024 x 1024 RGBA8 render target with a D24S8 depth;
per drawn frame, before the ocean, the scene clears it ((0,0,0,0), depth 1, as `0x5A0F80`), computes the reflection camera
of view 0 and puts its view-projection into WaterFrame 0x160, and calls the reflection hook (`WaterSceneReflection`)
with the target, the camera (1024 x 1024) and the scene camera's z. The terrain scene (`srv_render_scene.cpp`, 16 lines in
`Scene::draw`) sets the hook while it has an ice mesh: the ice module draws its frame inputs with the reflection camera
(`polar-ice.md` 13). Without a hook (no ice, `render_ice=0`, `render_ice_reflection=0`) the target stays (0,0,0,0) and
the picture is the one before 0382 bit for bit (0x160 changes no pixel with an empty t4: test
`renderer_reflection_matrix_per_view_and_t4`). A camera without a footprint (no ground in view) leaves 0x160 the identity
and the hook uncalled (counted `w_refl_badcam`, logged once per text). A failed hook draw is LOUD once per new text,
counted, the target cleared again, the frame drawn. Option `render_ice_reflection` (server.cfg, default 1). render.log line of its own each second: `water refl 1s:
w_refl_ice w_refl_frames w_refl_draws w_refl_errors w_refl_badcam`; totals "reflected-ice frames / errors / no reflection
camera". **Found on the way [L, 2026-11-16]:** the joined per-second line (`terrain 1s: ... | water 1s: ...`) is cut at
the log's 1,536-byte body (`srv_render_common.cpp` Emit) inside the water part (it ended at `w_oce`): the water's own
fields after `w_ocean_draws` (quads, pages, river draws, passes, GPU ms, waves) have not reached render.log on the
product path. Not fixed here (the file is not 0382's); the reflection's fields have a line of their own for that reason.

## 14. Acceptance of the epic (task 3608, 2026-11-14)

Written by W3 of team-3600 (issue 3608, branch `task-3608-water-acceptance`).

**Builds and settings.**
- Builds, from this branch (epic-3600-water bf13984e + main, with the `water_tap` default switched to 1 at 72add72c):
  server.exe EE46F007... and proxy A784AB1A... on the x64 pin's dependency build (DLL 74588135..., `x64-04b1211b-pa803776`;
  MC #487), composite 18DED941....
- The joined arm is the product path of today (MC #546 (b)): D33 in config.ini with Overlay on and decision 2 (i) (Waves
  on, view bit cleared by the proxy), `[Renderer] ServerRenderer = 1`, the repository's `shell_composite.ini`, and
  `water_tap = 1` by the code default with no ini key (MC #557).
- The stock arm: the 32-bit pin `i0ship-26234371-ia`, `darkred.py --arm stock`, no proxy, no server.
- Instances c5h-b and c5h-c (C:). Asset set: the instances' install (VT present, folder hash 45b3b433...).
- Drivers: `shell/evidence/3600/3608/run_3608.py` (3609's camera procedure on 3308's product-path driver),
  `compare_3608.py`, `d_compare.py`, `grab_water.py`. Every arm was restored by SHA-256, and the save copies were
  removed.
- Pictures and data: `D:/c5-64bit-work/3600/3608/` (D83).

### 14.1 A: product level, exact

| Item | Result | Where |
|---|---|---|
| River mesh tap against the stock capture (Tiny) | IB equal byte for byte; VB equal in bytes 0-27 of all 3,452 vertices, the 4 COLOR bytes (not read by the river VS) differ by configuration | 9.7 (3603) |
| Wave meshes tap against the stock capture (Tiny) | the three chunk VBs and the shared IB equal byte for byte | 12.5 (3606) |
| Two loads, Tiny | river VB / IB equal; wave chunks and waves equal | 9.7, 12.5 |
| **Two loads, huge (P6 on huge) [L, 3608]** | the water arm of D: the load of t350 and the reload after 30 AI turns, both images kept by `grab_water.py`; **16 buffers equal byte for byte**: river VB 881,600 B and IB 329,760 B (GATE2's static pair, 3), 7 wave chunks of 53,568 B + 23,808 B | `D:/.../3608/huge-water-images/load{3_copy1,5_copy2}` |
| Ocean per draw (watershot) | the module's mesh 285 of 285 and 56 of 56 quads bit-equal; the module's WaterFrame and mesh in the capture's draws: 99.9996-100 % of pixels equal (the rest off by 1); with 3609's input fixes, 3 of 6 draws pixel-equal, 2 off by 1 in at most 13 px, draw 354 99.78 % equal (max 77) | 10.6 (3604), 10.7 (3609) |
| River per draw | 3,603,694 of 3,603,694 pixels equal on both cameras | 11.3 (3605) |
| Waves | WaveFrame equal bit for bit in all six draws; states, layout and t0 as stock; pixels not compared (no per-draw targets) | 12.2 (3606) |
| WaterFrame (A3) | 19-20 of 22 members bit-equal; FogParams.y and two normalised watersky vectors within 1-2 ulp | 10.6 (3604) |

**A2 rests on replays with the capture's own inputs.** The captures have no per-draw render targets: the owner's RenderDoc
settings change has not been run (6.2). So each draw is compared against its own replay with the capture's inputs, not
against the EXE's target.

### 14.2 B: frame level against stock

**Method.**
- Cameras: 3000/3200's three ground-truth views per map (`shell/evidence/3000/road/plan_{tiny,huge}.json`: plot and
  wheel ticks from the LookAt zoom). Then four water cameras chosen by Lua in the first arm (the revealed plot of each
  class nearest the capital: open ocean, coast, lake, river mouth, or the nearest river plot when no mouth is revealed).
  The plan is stored once and reused by the other arm.
- Both arms used the same procedure: LookAt, wheel ticks, Esc until the top band is undimmed (D114; 0 rounds were
  needed), the mouse parked over the minimap.
- Each joined picture is a whole-screen **ACCEPTANCE** picture. Each stock camera was shot twice, 5 s apart; the
  second shot gives stock against stock, the animation ceiling.
- Mask (`compare_3608.py`):
  - water pixels by height: the water plane over samples below it, minus a 16 px frame;
  - minus the interface's fixed rectangles, dilated by 16 px;
  - kept only on the plots the active team sees at load (Lua `plots.json`; revealed and visible), eroded by 16 px.
  - The pixel-to-plot mapping is chosen where the plots' IsWater agrees best with the height mask (0.88-0.95 agreement).
  - Not removed: models, and borders over water.
- Statistic: the largest channel difference per pixel; median / p95 / max over the mask. Difference images:
  `D:/.../3608/<map>/diff/`.

**Tiny, the ground-truth save `C5H-3000-TINY-T0018` (turn 18; only 31 plots visible, 8 of them water):**

| Camera | Plot | Mask px | Joined vs stock | Stock vs stock |
|---|---|---|---|---|
| v1_city_default | (8, 23) | 344,335 | 13 / 89 / 226 | 10 / 35 / 187 |
| v2_city_zoomin | (8, 23) | 722,301 | 19 / 145 / 221 | 13 / 47 / 202 |
| v3_second_zoomin | (8, 24) | 948,514 | 13 / 82 / 226 | 9 / 38 / 189 |
| w_ocean | (6, 24) | 871,069 | 17 / 137 / 235 | 11 / 45 / 235 |
| w_coast | (7, 23) | 670,319 | 16 / 93 / 226 | 11 / 42 / 164 |
| w_lake | (7, 21) | 237,896 | 14 / 67 / 249 | 12 / 71 / 249 |
| w_rivermouth (nearest river plot) | (9, 23) | 383,514 | 19 / 70 / 253 | 14 / 58 / 253 |

**Tiny, 3601's fully visible state `C5H-3600-TINY_0009V` (turn 9, every plot revealed and visible; the water cameras):**

| Camera | Plot | Mask px | Joined vs stock | Stock vs stock |
|---|---|---|---|---|
| cam1_3601 (open sea, coast) | (33, 14) | 1,746,755 | 8 / 42 / 255 | 7 / 36 / 197 |
| cam2_3601 (bay in a city border) | (51, 14) | 690,349 | 11 / 106 / 219 | 7 / 100 / 247 |
| w_ocean | (51, 10) | 1,704,308 | 8 / 96 / 255 | 8 / 95 / 255 |
| w_coast | (51, 13) | 1,154,147 | 7 / 99 / 230 | 6 / 91 / 233 |
| w_lake | (45, 11) | 111,533 | 8 / 106 / 209 | 6 / 104 / 175 |
| w_rivermouth | (52, 14) | 928,381 | 10 / 115 / 251 | 7 / 108 / 237 |

**Reading.**
- On the fully visible state the joined water is within stock's own shot-to-shot ceiling: median +0 to +4, p95 +1 to
  +15.
- On T0018 the p95 is higher (+20 to +98). Its difference images show two classes:
  - the edge of the clouds of unrevealed plots, which the mask's 16 px erosion does not fully remove (seam F: stock's
    fog of war over water, not drawn by the server);
  - the border lines over water (seam E).
- The remaining classes on both maps are intended differences:
  - the water's and the waves' phase (the server's own clock, decision 4 (ii), 13.2);
  - models: D33 draws resources and cities dark, and model reflections are left out (decision 3 (ii));
  - borders over water (seam E);
  - fog of war (seam F).
- The cam2 bay's red channel (10.7: 42.5 against 49.0) is not explained.
- ACCEPTANCE pictures (Visual Checks):
  - T0018: vc-20261114-034733-workstation-3756 (v1), -034759-603a (v2), -034815-1bbf (v3), -034832-d51e (ocean),
    -034848-75ff (coast), -034904-b871 (lake), -034919-d19c (river).
  - 9V: vc-20261114-041425-workstation-ef09 (cam1), -040746-6b5f (cam2), -040800-cf13 (ocean), -040815-eefb (coast),
    -040831-350e (lake), -041440-1d8d (river mouth).
  - Two first shots of the 9V arm were flat grey failed captures; they are marked `fail` and were retaken.
    run_3608.py retakes a flat capture since then.

**Huge, the ground-truth save `C5H-VP8P-HUGE_0350` (t350):**

| Camera | Plot | Mask px | Joined vs stock | Stock vs stock |
|---|---|---|---|---|
| v1_city_default | (100, 30) | 200,279 | 20 / 117 / 217 | 8 / 62 / 168 |
| v2_city_zoomin | (100, 30) | 35,008 | 10 / 75 / 198 | 0 / 29 / 97 |
| v3_second_zoomin | (99, 31) | 0 (no water on screen) | - | - |
| w_ocean | (104, 18) | 88,299 | 15 / 44 / 116 | 13 / 40 / 91 |
| w_coast | (103, 19) | 127,394 | 58 / 107 / 200 | 57 / 105 / 180 |
| w_lake | (104, 27) | 729,038 | 14 / 121 / 255 | 10 / 65 / 226 |
| w_rivermouth | (103, 27) | 364,814 | 13 / 134 / 251 | 9 / 41 / 240 |

On huge the water surface matches stock: w_ocean and w_coast are within the ceiling. Where the p95 is higher, the
difference image shows D33's static models along and on the water: the Great Wall on the river bank, docks and harbours,
and resources, all drawn dark red by the EXE under D33. These are not the water. The mapping agreement was 0.90-0.99.
ACCEPTANCE pictures: vc-20261114-042256-workstation-8c3a (v1), -042320-72f8 (v2), -042333-90a1 (v3, no water),
-042347-52d6 (ocean), -042400-48ba (coast), -042413-4084 (lake), -042428-86fc (river mouth). Each picture's figures and
difference image are in its record.

### 14.3 C: ships (3310's table, 2026-11-14 04:33-04:45) [L shots, C analysis, I causes]

(Replaces "not done" of 04:27 the same day.)

**Set-up.** One joined launch on c5h-a (C:), the builds and settings above, save copy `C5H-3608-HUGE_0350`. 3310's
cameras: the load view, 2 and 2 more wheel ticks in at the client centre, LookAt (100, 30) (`v1_city_default`), LookAt
(99, 31) (`v3_second_zoomin`); v2 was left out (in 3310 v1 = v2 byte for byte: two ticks already reach the closest
zoom). Per camera 3310's shot kinds: depth_write=1 (the shipped setting), depth_write=0, passthrough twice, by editing
the composite ini between the shots and putting it back (`run_3608.py run --ships`). These shots change the
composite's settings, so they are DIAGNOSTIC records (21 records, vc-20261114-043557-workstation-8737 to -043836-4278, result info; 21 records for 20 kept shots, one retaken), not ACCEPTANCE.
The black and stock arms are **3310's W10 round-1 arms** (`D:/c5-64bit-work/3300/w10/{black,stock}/out`, copied):
EXE-only arms on the same save and install, nothing in them depends on the server; the units list is W10's (the save's
units at load). Cameras against them (`cam_shift_3310.py`): load equal byte for byte, the others within 0.005-0.885 px
(black) and 0.13-0.93 px (stock). Analysis: 3310's `compare_3310.py`, `cam_shift_3310.py`, `tables_3310.py` unchanged;
results `D:/c5-64bit-work/3600/3608/huge-ships/{summary.json, cam_shift.json, tables.md, cmp/}`. Harness stop and
restore by hash 0, LOUD 0.

**Ships against the black arm (the EXE's own water depth), pixels decided in both arms:**

| View | Ship px | visible both | hidden both | only the server | only the EXE | distance only-server / only-EXE, median / p95 / max px |
|---|---|---|---|---|---|---|
| load | 30,939 | 24,379 | 831 | 4,139 | 1,590 | 32 / 121 / 220 ; **2.2** / 60 / 75 |
| load_in2 | 40,701 | 35,959 | 219 | 3,677 | 846 | 44 / 202 / 382 ; **3.0** / 11 / 22 |
| load_in4 (same camera) | 37,187 | 33,409 | 372 | 2,140 | 1,266 | 27 / 154 / 199 ; **2.2** / 16 / 61 |
| 3310 (flat plane), load r1 | 31,673 | 28,415 | 192 | 855 | 2,211 | 43 / 105 / 123 ; 47 / 83 / 92 |
| 3310 (flat plane), load_in2 = in4 | 45,517 | 42,039 | 263 | 2,193 | 1,022 | 118 / 339 / 382 ; 96 / 155 / 161 |

(v1 and v3 hold no ships, as in 3310.)

**Reading.**
- *Where the EXE's water hides a hull, the server's water now hides the pixels next to it* [L]: the pixels only the EXE
  hides lie a median 2-3 px from pixels the server hides (3310: 47-96 px, "different strips of a hull"). Hidden in both
  rose from 192 to 831 at the load view. This is the change 3310's seam 2 asked for (the server's water surface against
  the EXE's): the cut now follows the same water line.
- *The server hides more ship pixels than the EXE* (4,139 against 1,590 at load; 3310: 855 against 2,211). Per ship
  (`tables.md`): five of the ironclads at the load view agree within 2 points (8.0 / 8.8 %, 8.8 / 8.7 %, 44.1 / 40.7 %,
  4.5 / 6.0 %, 5.5 / 4.4 %); others are hidden more by the server (cruisers 25-31 % against 5-7 %; (111, 26) 19.0 %
  against 0.8 %). **These per-ship figures are not decisive** [L]: `load_in2` and `load_in4` are the same camera, yet the
  joined share of one ship moves between them by up to 21 points ((110, 28) 21.0 % / 4.1 %, (113, 28) 27.5 % / 6.7 %),
  while the black arm moves by at most 3. [I] cause: 3310's test for cached ground counts a pixel hidden when its
  depth_write=1 value differs from every depth_write=0 and passthrough value; on animated water with bobbing ships, a
  single depth_write=1 shot catches hull edges and wake at another phase. The flat water colour of 3310's run had an exact
  test instead. A decisive per-ship figure needs three or more shots of each kind (3310's round 2) or the frozen phase
  (`render_water_freeze`) in the joined arm.
- Side figures of the same run, not the water's: static models hidden by the server's depth at the load view 0.2 % (3310:
  9.0 %; issue 0134's height fix, renderer-in-server-design.md 4.14), and the picture against stock at the load view
  median / p95 20 / 127 (3310: 68 / 221).

### 14.4 D: nothing regressed (huge, 30 AI turns each arm)

- Save copy `C5H-3608-HUGE_0350` of `C5H-VP8P-HUGE_0350` (t350 -> t379), c5h-c, one reload after the turns.
- Water arm: the shipped settings. Placeholder arm: `water_tap=0`, so there is no tap and the server keeps the flat
  plane, as the product was before epic 3600.
- Both arms ran with the same builds, one after the other (water 03:35-03:57, plane 03:58-04:16).
- The lockstep record counts only the arm's own EXE process (`d_compare.py`).

| | water | placeholder |
|---|---|---|
| LOUD lines | 0 | 0 |
| turns (mean wall s) | 30 (27.6) | 30 (24.5) |
| lockstep frames / exact | 161,789 / 87.9 % | 169,624 / 87.6 % |
| lockstep older / none / noins | 4,786 / 170 / 14,565 | 5,255 / 99 / 15,667 |
| gray frames / gray tile frames (0139) / gray after the bake-ahead | 40 / 1,592 / 0 | 21 / 849 / 0 |
| EXE private / largest free block after the load (MB) | 1,426.7 / 1,909.3 | 1,447.5 / 1,861.1 |
| server private / video after the load (MB) | 2,205.8 / 1,643.0 | 2,132.4 / 1,639.4 |
| after the reload: server private / video (MB) | 2,535.4 / 1,610.8 | 2,452.1 / 1,577.5 |
| server CPU / GPU ms per frame (median, indication) | 0.661 / 0.984 | 0.619 / 0.912 |

**Reading.**
- 0 LOUD lines in both arms, and lockstep as good (87.9 % against 87.6 % exact).
- 0 gray frames after the bake-ahead in both arms.
- Memory after the load: the EXE is not larger with water. The water tap's section is transient and was released after
  the server's ack (9.5), so nothing of it stays in the 32-bit process. The server carries +73 MB private and +4 MB video
  for the water (the module, the meshes, the reflection target).
- **One difference to name: the gray counters before the bake-ahead are higher with water** (40 against 21 frames; 1,592
  against 849 tile frames). Candidate cause [I, not verified]: the water's GPU time (E) competes with the bake-ahead's
  budget during the two loads. The counters are cumulative over the run; both arms reach 0 after the ahead.
- Not done: a second pair of arms to see the spread.

### 14.5 E: cost (indication, COMMON 23)

- Water GPU time per frame from the scene's timestamps (`w_gpu_ms`, per-second lines of the 30-turn water arm on huge):
  median 0.211 ms, p95 0.777 ms. Tiny (3607's run): 0.053 ms.
- Server GPU per frame (lockstep record): 0.984 ms against the placeholder arm's 0.912 ms.
- Composite CPU of the tap per load: 1.9-2.7 ms (9.7). No frame-rate claim (workstation, D106).

### 14.6 F: tests under MSVC /fsanitize=address (2026-11-14 03:32-04:00)

| Suite | Result |
|---|---|
| `render/build.py --asan --test` | 23 of 23 (c5r water, terrainsets, watershot and the rest) |
| `server/tests/render` against `build_render_tests.py --asan` | 82 passed. Two water host tests timed out: their host lifetime was too short for the module's start under ASan. After longer timings (`test_water_host.py`) the water host file passed 7 of 7 under ASan. |
| `water_scene_test.exe` (`build_water_tests.py --asan`) | 15 of 15 cases |
| `shell/tests/test_water_tap.py` + `test_build_cmd.py` against `shell\build.cmd asan` and the ASan reader | 46 passed |
| ASan reports | 0 |

0 skipped. Plain builds, with the `water_tap` default 1: test_water_tap, test_build_cmd, test_errors and test_standin
gave 110 passed and 1 failed (`test_composite_under_ui`, a 10 s wait under the machine's load); the failed test passed
on its rerun.

### 14.7 Open after 3608

- C: a decisive per-ship figure (three shots of each kind, or the frozen phase; 14.3).
- The per-draw comparison against the EXE's own targets (the owner's RenderDoc settings).
- The cam2 bay's red channel.
- The gray-counter difference of D (cause not verified).
- A second D pair for the spread.
- Pictures on design V if 3516 takes it (MC #546).

## 15. Run-to-run variation of the ocean draw: the driver's internal threading (issue 0336, 2026-11-16)

Written 2026-11-16 by the implementer of 0336 (account B, branch `task-0336-water-overlay-determinism`). All runs on the
workstation, 2026-11-16 12:40-14:44 EST, NVIDIA GeForce RTX 4080, driver 32.0.16.1088 (nvidia-smi 610.88), other teams' games and builds on the
same GPU throughout (`quietlock.py check`: 6-11 registered jobs). Bundle: `C5H-0301-HUGE_0350_20261115-014344_workstation`
(format version 1, now in `F:/c5-64bit-work/scene-library/retired/`; today's player refuses format 1, so these runs used
the player built from the 0301 branch at f552b65e, exported to `C:/c5-64bit-work/0336/v1src`, with diagnostic switches
added in that copy only: `render/evidence/0336/diag-*.patch`). Scripts, outputs and tallies: `render/evidence/0336/`.

**Cause [L]: the D3D11 driver's internal threading optimisations, not the overlay, not the module's inputs, not the GPU's
arithmetic.** The device the player (and the server) creates with flags 0 lets the driver run its own worker threads;
on this driver the ocean draw (the EXE's ocean PS, pass 3) then gives, at about one pixel per frame, one of two values
that differ by 1 in one channel, chosen by timing, with byte-identical inputs. Created with
`D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS`, the same draws are byte-stable. *This corrects the premise
of 0336 and of `research/06-tooling-and-workflows/offline-scene-harness.md` section 16 ("not bit-stable when the water
module draws with the overlay bound"; "with render_overlay=0 the 8 runs are bit-identical"): the overlay is not needed,
the 8 bit-identical overlay-off runs of 3905 were a quiet interval. That file is outside 0336's touches; its owner
corrects it from here.*

The evidence, in the order it was taken (counts are redraws that differ from the frame's first draw in the same
process; a run draws the 8 dumped frames once, then each frame N more times with the same SceneFrame, no state between):
1. **Reproduced across runs** (8 runs, `--dumped`, no pictures): frames 1135 and 1142 take two states (7 + 1 each), the
   pixels 3905 named; 1109 and 1118 stable, one pixel from the live dump. CPU-side inputs traced in 18 further runs
   (WaterFrame bytes per view, ocean vertices and indices, the content hash of every bound texture t0-t13 except the
   per-batch t7): identical in all 18.
2. **Inside one process**, redrawing a frame with the same inputs varies at the same one pixel per frame: (2103, 68) of
   1109, (2018, 1196) of 1118, (912, 61) of 1135, (565, 1) of 1142, (1984, 60) of 1094; 0 to 31 of 480 redraws per
   run, in bursts. A fresh copy of the overlay texture per draw changes nothing (6 / 0 / 0 of 480 against 19 / 0 / 6).
3. **Which draw**: interleaved arms of 4 runs x 480 redraws: as recorded 21 of 1920, `render_overlay=0` 31 of 1920,
   `render_water=0` 0 of 1920. Switching parts off in the copy: the varying pixels are drawn by the ocean only (rivers,
   coast waves and the two map-wrap views leave them unchanged); with `render_tiles=none` (no terrain) the ocean alone
   still varies (4-5 of 480 per run); on WARP 2 runs x 80 redraws are equal and the two runs equal each other.
4. **Not a bad input**: binding a null SRV at one slot of the ocean draw at a time (water-only scene, 2 x 800 redraws):
   t5 (currents map) 0 and 0, t6 (`waterdepthcolor_new`) 0 and 0; t0+t1, t4, t7, t8, t9, t11, t13 still vary (4-17
   per 800; t8 7 and 0). An immutable copy of the currents map at t5 (same bytes, not a render target) still varies
   (13 of 2400 against 15 of 2400), so t5 and t6 are where the PS turns a last-bit difference into a step of 1 (the
   currents term is chaotic in the last bit, 8.6), not a source. Opaque instead of alpha blending: the same 4 of 800,
   so it is the PS output, not the blend.
5. **Time, not count**: frame 1094 redrawn without pause differs in redraws 0-4 (89-212 ms after the first draw) and not
   after; with 100 ms between redraws 23 of 160 differ, scattered; with 500 ms, 20 and 26 of 96, scattered up to 6.6 s.
   The pause (the driver's threads idle in between) provokes it; an extra GPU load from a second device in the same
   process does not (0 of 1920 with the load, 3 of 1920 without, interleaved).
6. **The flag**: the same runs with the device created with `D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS`:
   0 of 160 (no pause), 0 of 160 (100 ms), 0 of 96 (500 ms), against 5, 23 and 20-26 without it, back to back.
   The first draws' values are the same with and without the flag (the 8 shot hashes of the water-only runs equal), so
   the flag removes the second state and changes no other pixel.
7. **The acceptance series** (product configuration: `render_water=1`, `render_overlay=1`, the bundle's `render_config`):
   8 runs of the v1 player with the flag: all 8 frames byte-equal in all 8 runs (colour and depth), and equal to the
   majority state of the runs without the flag (hashes 6977efa5..., cc063f3d..., b1a8dd2a..., e15416ec..., 711de9a6...,
   e10f4d94..., 04e1009b..., a9028756...). Against the live dump 1109, 1118, 1135 and 1142 still differ by one pixel,
   1 in one channel: the live server drew them with flags 0, so a bundle recorded before the server has the flag keeps
   that residue, and the standing check of such bundles keeps `--tolerance 1:1`.
8. **Today's player with the flag, on today's library** (format 3, `F:/c5-64bit-work/scene-library/bundles`, 7
   bundles, 2026-11-16 14:37-14:44 EST): the player built from this branch with flags 0 (a scratch copy) and with the
   flag, one run each per bundle: the colour hashes of all 56 shots equal between the two (the flag changes no
   picture), and with the flag all 56 shots byte-equal to the live dumps (`--tolerance 0:0`). 8 runs of the flagged
   player on `C5H-0301-HUGE_0350_20261116-042357_workstation`: all 8 frames byte-equal in all 8 runs and to the live
   dumps. (These bundles' live frames happen to carry no flipped pixel; the flag does not make a threaded live dump
   deterministic.)

**What changed** (MC #2757, option a): `server/tools/sceneshot/sceneshot.cpp` `MakeDevice` creates every device (the
adapter's and WARP) with `D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS`. The water module
(`render/src/water`) is unchanged: it cannot choose the device's flags, and its inputs were shown deterministic. The
server's own device (`server/src/render/srv_render_host.cpp`, held by epic 0375) keeps flags 0 until a follow-up measures
the flag's cost there (PERFORMANCE.md): frame time, the render thread's CPU per frame and submit cost, flag on against
off, interleaved, on the quiet testbed or in an MC window, behind a server.cfg key defaulting to off. With the flag in
the server, new live dumps and the player agree byte for byte and P1 can return to `0:0` for bundles recorded after it.
*(Done 2026-11-17 by 0435, section 15.1: the key is `render_prevent_driver_threading`; measured, it stays 0 for play and
is 1 by default while recording, so every bundle recorded from then on has byte-stable dumps.)*

**Tests [L, 2026-11-16 14:52-15:13 EST].** `build_scenebundle_tests.py --run` plain and `--asan` (C5R_EXE the Ghidra
copy; without it recorder_test and sceneshot_test fail their EXE cases by design): 4 programs, 527 + 159 + 163 + 24
passed, 0 failed, 0 skipped each, no AddressSanitizer report; `render/build.py --test -R c5r_water` 8 of 8, 0 skipped.

**Not verified.** The mechanism inside the driver (which of its threaded paths picks the other PS code; a deferred
shader recompile is the likeliest [I]). Other GPUs and drivers (oldpc, the laptop) were not tried. Whether the EXE's
own picture shows the same one-pixel flips (it creates its device without the flag [I]).

### 15.1 The server's device with the flag: `render_prevent_driver_threading` (ticket 0435, 2026-11-17)

Written 2026-11-17 from 16:15 EST by the implementer of 0435 (account B, branch `task-0435-server-device-threading-flag`,
from main 62b8dd67). Scripts and evidence: `server/evidence/0435/`.

**The change [C].** `server/src/render/srv_render_host.cpp`: new server.cfg key **`render_prevent_driver_threading`**
(0 | 1; any other value refuses the renderer's start, exit 2, as every render key). 1 creates the device with
`D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS` (0x8), 0 with flags 0 (the device before 0435). The
device note says which: `device created, feature level b100, flags 0x8 (render_prevent_driver_threading=1)`. Every
`host 1s:` line gets, after `bake_checks=`: `dev_flags=<the flags, decimal>`, `submit_n=` (frames whose submission was
timed in that second), `submit_ms=` / `submit_max_ms=` (the mean and largest CPU wall time of a frame's submission:
`Scene::draw`, every pass's D3D11 calls, to the sink's `end`, the hand-over included; QPC), and `rt_cpu_ms=` (the
render thread's own user + kernel CPU time since the last line, `GetThreadTimes`). The key is not a renderer input
(rule A, `research/06-tooling-and-workflows/offline-scene-harness.md`): it changes how the driver schedules the same
draws, not what is drawn, and the player always creates its device with the flag (0336); so no bundle member. The
default is set by the measurement below (15.1.4, decision 15.1.5). *Added 2026-11-17 18:00 (after the first arms of
15.1.3): with `render_test_record` (a recording) the key's default is 1, an explicit key wins; `scene.json`'s
`render_config` names the live device's flag as `render_prevent_driver_threading` ("1": the dumps were drawn
byte-stable). The player reads the key through `ParseHostConfig` and ignores it (its own device always has the flag);
the format version is unchanged (no reader requires the key; older bundles lack it and mean flags 0).*

**Tests [C].** `server/tests/render/test_render_host.py`: `test_device_flags_and_submit_counters` (default, `=0`, `=1`:
the device note's flags, `dev_flags` in every 1 s line, `submit_ms` positive and at most `submit_max_ms` in each second
that drew, `rt_cpu_ms` non-negative and positive in sum, `submit_n` at most the frames drawn) and `test_config_refused`
with `render_prevent_driver_threading=2` and `=on` (exit 2, nothing written into the section);
`test_recording_draws_with_the_flag_unless_told` (with `render_test_record`: no key 0x8, `=0` 0x0, `=1` 0x8, and
`render_config` says the same). Results [L, workstation, 2026-11-17]: render-host suite (`run_all.py --only render-host`)
plain **140 passed, 0 failed, 0 skipped** (16:24-16:36, before the recording test); MSVC ASan 137 passed, 3 failed (16:38-16:54):
`test_bake_yield.py` both timing tests and `test_residency_source.py::test_feed_changes_the_bake_ignores[owner-2]`, the
load-timing failures of main that ticket 0496 holds (0414's research 6.5), failed again on a rerun of the same build
(17:00), while the new tests passed under ASan (3 + 2 refusals); no ASan report. The recording test: 3 of 3 plain
(17:52). After the last code change (commit 3bee2315, the recording default) [L, workstation, 2026-11-17
18:05-18:35, run_all.py plain]: **render-host 143 passed, scenebundle-programs 4, scenebundle-py 21,
offline-equals-live 8 (the library's 8 bundles with this branch's player), 0 failed, 0 skipped**; under MSVC ASan the
8 new or changed tests (3 device flags, 3 recording defaults, 2 refusals) passed with no ASan report (18:36-18:40).

#### 15.1.1 The measurement: method (written 2026-11-17 16:15 EST, before the run)

One quiet headed testbed job on oldpc (`server/evidence/0435/cost_0435.py`, after 4461's `cost_4461.py`), the D33
product path of `tools/testbed/jobs/product_path.py` (its prep and restore), the huge save `TB-HUGE_0350`, **one
server.exe for every arm** (built from this branch by `server/evidence/0435/build_0435.sh`); the arms differ only by
the key through `vpproxy.ini server_cfg_extra=` (TEST ONLY): **f0** `=0`, **f1** `=1`. A warm-up f1 arm (not counted),
then 12 arms `f0,f1,f1,f0,f1,f0,f0,f1,f0,f1,f1,f0` (consecutive pairs ABBA BAAB ABBA against oldpc's level drift,
`research/08-performance/bake-ahead-after-0365.md` 4-5), each a fresh timing launch (no frame cap, WaitForVSync 0,
config.ini LoggingEnabled 0, the window on screen): the load's bake-ahead (the `residency: field ... (new, load N)` line
to `bake-ahead complete`, its ms and bakes, and the 1 s lines inside it), a 15 s settle, a 30 s **idle** window (the Lua
frame counter, `fps_idle.py --measure`, 3 blocks), then a 30 s **pan** window (`UI.LookAt` one plot east every 0.25 s
along the capital's row, the frame counter read at its start and end), each with server.exe's CPU seconds (psutil: the
render thread and the driver's own threads together), the 3D utilization of server.exe and the EXE (`gpu_sample.ps1`),
and from render.log over the same seconds the frames drawn, the terrain renderer's CPU ms per draw (`terrain 1s`
`cpu_ms`), `submit_ms`, `submit_max_ms` and `rt_cpu_ms` per frame; no AI turn (the flag acts on the render thread only).
Each arm is checked to have taken effect (the device note and every `dev_flags`; else exit 10). Statistics: per metric
the f1 - f0 difference inside each of the 6 pairs, its mean, 95 % t interval and smallest detectable difference (80 %
power), PERFORMANCE.md grade A.

Then the **live-dump series** in the same job (not timed): recording arms **r1** (f1 + the recorder:
`render_test_record=<empty folder>`, `_after 900`, `_frames 8`, `_dump all`), **r0** (the same with f0, the control),
**r1** again; after each arm's stop the checker (`recorder_test.exe --check`) and the player built from this branch
(`sceneshot.exe --dumped --compare --tolerance 0:0 --no-pictures`) 8 times on that bundle, on oldpc's own adapter (the
recording's, so P1 applies): each frame's colour and depth against the server's own dump.

Expected [I, from 15 items 5-6]: with the flag the driver runs the device's work on the calling thread, so the render
thread's CPU per frame and the submission's time rise, while the process's total CPU per frame changes little (the
driver's worker threads did that work before); the GPU per frame and the frames drawn per second about equal; the
EXE's frame time unchanged within about 1 % (the server's frame is not on the EXE's critical path except through
lockstep); the bake-ahead possibly a little longer (its bakes are D3D11 work on the render thread). r1's 2 x 8 replays
byte-equal to the live dumps in every frame; r0's may or may not differ (at about one pixel in some frames, 15 item 7).
*(Changed 2026-11-17 17:03 after 15.1.2: the second job adds WaitForVSync 1 arms v0 / v1, 6 pairs each configuration
in blocks of 4, idle and pan windows of 25 s, a 10 s settle, and a bake-ahead that has not ended 120 s after the map is
taken as its progress at that cut; the recording arms run with WaitForVSync 1.)*

#### 15.1.2 The first job: the uncapped flagged warm-up starved the bake-ahead [L, oldpc job 20261117-161802-0c9b]

Run 2026-11-17 16:36-16:46 EST (it started early: the quiet job ahead ended before its planned end), exit 4 after the
warm-up arm, by the driver's own rule of that version (no `bake-ahead complete` within 240 s of the map: a fatal error).
Instance tb1 restored by hash and verified. oldpc's RTX 2080 ('NVIDIA GeForce RTX 2080'; the job records no driver
version; 32.0.16.1088 by D175). Quiet proof: alone, foreign load 0.599 cores (`tvnserver.exe` 184 s of 246 s foreign
CPU: a remote-desktop server on oldpc was busy), so the arm is an observation, not a result. Build: server.exe
`1ca0e3db...` (this branch at 6dd5af4c, release, `repo_dirty=0`), pin layout `x64-04b1211b-pfdee656` (DLL `29f58e17...`),
proxy `ef23cb98...`, composite `92cb7af4...`.

The arm W0-f1 (`render_prevent_driver_threading=1`, WaitForVSync 0, timing launch, the huge save): the device note
`flags 0x8 (render_prevent_driver_threading=1)`, `dev_flags=8` in all 270 `host 1s:` lines after the field. Over the 270 s
from the field line (`residency: field 128x84 tiles (new, load 3)`, 4,922 land tiles) to the stop: **55,651 frames drawn
(206 per second), `submit_ms` 4.60 per frame (max 226), the render thread's CPU 4.80 ms per frame = 989 ms per second:
the thread was busy all the time**; the terrain renderer's own `cpu_ms` 1.17 ms per draw and its GPU 0.36 ms. **The
bake-ahead did not finish: 3,737 bakes, 2,114 of 4,922 land tiles resident after 270 s** (13.8 bakes per second; 0414's
arms baked about 390 per second and finished in 22.8 s, `research/08-performance/bake-ahead-after-0365.md` 6.4, with
product_path's settings). The residency bakes in the loop's idle slices, which run when no camera waits; at 206 cameras
per second and 4.8 ms of thread time per frame no idle time is left [I]. Whether the same happens with flags 0 at this
frame rate, and what the flag costs at a player's refresh rate, is what the later jobs measure (15.1.3, 15.1.4).

The second job (`20261117-170336-9fdb`, 17:05-18:13 EST) ran 13 launches and stopped at the 14th, an EXE crash in the
load without a box in a flags-0 arm (`research/02-exe-internals/crashes-huge-load.md` 7); tb1 was restored and verified
by job `20261117-181650-dbbb`. Its 13 arms are not a complete interleaved set and are not used for the comparison;
they showed the same starvation with flags 0 when uncapped (R1-f0: render thread 98 % busy, `submit_ms` 4.07, the
bake-ahead 73 % resident at the 120 s cut, 41 bakes per second, against R1-f1's 31 % and 22 per second), and a complete
bake-ahead with WaitForVSync 1 (R1-v1: 26.9 s, 7,549 bakes). The third job (`20261117-182013-3333`, the driver of
a4f1faf6 with the crash retry, server.exe `99397e79...` of a4f1faf6, the same code as 3bee2315; proxy `fffd83c2...`,
composite `92cb7af4...`, player `697edc9e...`, checker `3da4709f...`) is the measurement: 15.1.3 and 15.1.4.

#### 15.1.3 The live-dump series with the flag [L, oldpc job 20261117-182013-3333, 2026-11-17 18:26-18:43 EST]

Three recording arms first in the job (huge save `TBPP-HUGE_0350`, tb1, D33 product path, WaitForVSync 1, timing
launch; the recorder `render_test_record_after=900`, `_frames=8`, `_dump=all`; bundle format 8, EXE pin `d2ccb491...`,
VT present), each followed on the same machine and adapter (RTX 2080) by the checker (`scenebundle: OK` in all three)
and 8 runs of the player `sceneshot.exe --dumped --compare --tolerance 0:0 --no-pictures` (19.2-19.9 s per run):

| Arm | Device | Frames recorded | Player runs exit 0 | Frames byte-equal to the live dump (colour and depth) | Distinct colour hashes |
|---|---|---|---|---|---|
| R1-r1 (18:26) | flags 0x8 (`=1`) | 1190-1358 (8) | 8 of 8 | **64 of 64** | 8 (one per frame, the same in every run) |
| R1-r0 (18:32) | flags 0x0 (`=0`, the control) | 1080-1244 (8) | 8 of 8 | 64 of 64 | 8 |
| R2-r1 (18:38) | flags 0x8 | 1122-1294 (8) | 8 of 8 | **64 of 64** | 8 |

**With the flag the live dumps are byte-stable against the player over 8 runs, in two recordings: 128 of 128 frames,
colour and depth, at 0:0** (0336's series, now on the server's own dumps). **The control did not show the flip**: the
flags-0 recording's 8 frames were byte-equal too, so this series alone does not prove that the flag is needed on oldpc's
GPU and driver; 0336 measured the flip on the workstation's RTX 4080 at about one pixel in some frames of some runs
(15, items 1-3), and "these bundles' live frames happen to carry no flipped pixel" (item 8) is the likely reading here
too [I]. Whether oldpc's RTX 2080 flips at all is not measured (it would need 0336's redraw series there).
`scene.json`'s `render_config` of the flagged bundles says `render_prevent_driver_threading: "1"`.

#### 15.1.4 The cost of the flag [L, oldpc job 20261117-182013-3333, 2026-11-17 18:44-20:40 EST, grade A]

In PERFORMANCE.md section 6's form (statistics by `cost_0435.py` itself; per-arm table and every metric in
`server/evidence/0435/results_20261117-182013-3333.txt`; job folder `F:/c5-64bit-work/0435/oldpc-3333`):

```
Metric / tool:        EXE frame time (Lua frame counter: idle 25 s = fps_idle.py --measure 3 blocks; pan 25 s = 100
                      UI.LookAt steps east, 0.25 s apart); server.exe CPU ms per drawn frame (psutil, all threads);
                      the render thread's CPU ms per frame (host 1s rt_cpu_ms / submit_n) and its share of a core;
                      the frame's submission (host 1s submit_ms: Scene::draw to the sink's end, QPC); the terrain
                      draw's own cpu_ms; server GPU ms per frame (3D utilization x window / frames, gpu_sample.ps1);
                      the load's bake-ahead (render.log field line -> bake-ahead complete), or its progress at a cut
                      120 s after the map; server/evidence/0435/cost_0435.py at a4f1faf6
Builds:               one build in every arm: server.exe 99397e79 (repo a4f1faf6 = the code of 3bee2315, release,
                      dirty 0), x64 DLL 29f58e17 (pin layout x64-04b1211b-pfdee656), proxy fffd83c2, composite 92cb7af4;
                      arms differ only by vpproxy.ini server_cfg_extra= render_prevent_driver_threading=0|1 (TEST ONLY)
                      and, between configurations, GraphicsSettingsDX11.ini WaitForVSync (f: 0, v: 1)
Game / config:        TB-HUGE_0350 copy (c499d3c6), D33 product path (ServerRenderer = 1, ring + fence, lockstep),
                      timing launch (no frame cap), config.ini LoggingEnabled 0, no AI turn; tb1 (oldpc's own EXE, VT
                      present); RTX 2080, driver 32.0.16.1088 (D175; the job does not record it)
Series:               warm-up f1 (not counted) after 3 recording arms; then 24 arms, f and v in blocks of 4, each
                      configuration ABBA BAAB ABBA = 6 consecutive pairs; differences taken inside each pair; one game
                      at a time, ~5.8 min per arm; no crash (the retry was not needed)
Machine:              quiet job alone, undisturbed false: foreign load 0.513 cores mean (tvnserver.exe 3,000 s of 4,300 s
                      foreign CPU: oldpc's remote-desktop server was busy all evening; svchost 555 s, MsMpEng 325 s);
                      machine CPU mean 41.5 % (max 91.7 %); lowest available memory 6,622 MB; not interrupted
```

| Metric (mean of 6 arms each) | f0 | f1 | f1 - f0, paired, 95 % | v0 | v1 | v1 - v0, paired, 95 % |
|---|---|---|---|---|---|---|
| EXE frame time, idle, ms | 4.360 | 4.301 | **-0.059 [-0.109, -0.009]** (-1.4 %) | 16.667 | 16.667 | 0.000 (vsync) |
| EXE frame time, pan, ms | 4.404 | 4.237 | **-0.166 [-0.215, -0.117]** (-3.8 %) | 16.667 | 16.667 | 0.000 |
| server frames drawn per s, idle | 226.4 | 206.6 | -19.7 [-24.2, -15.3] (-8.7 %) | 60.1 | 60.5 | +0.4 [-1.2, +2.0] |
| submission per frame, idle, ms | 4.076 | 4.670 | **+0.594 [+0.524, +0.664]** (+14.6 %) | 4.160 | 4.665 | **+0.506 [+0.455, +0.556]** (+12.2 %) |
| submission per frame, pan, ms | 3.981 | 4.649 | +0.667 [+0.624, +0.711] | 4.379 | 4.773 | +0.394 [+0.354, +0.435] |
| terrain draw's own CPU, idle, ms | 0.865 | 1.183 | +0.318 [+0.300, +0.335] (+36.7 %) | 0.975 | 1.329 | +0.354 [+0.341, +0.367] |
| render thread CPU per frame, idle, ms | 4.286 | 4.740 | **+0.454 [+0.306, +0.601]** (+10.7 %) | 4.131 | 4.571 | **+0.441 [+0.275, +0.607]** (+10.7 %) |
| render thread CPU per frame, pan, ms | 4.317 | 5.069 | +0.752 [+0.645, +0.859] (+17.5 %) | 5.060 | 5.597 | +0.537 [+0.370, +0.704] (+10.7 %) |
| render thread share of a core, idle | 0.971 | 0.992 | +0.021 [-0.004, +0.045] | 0.250 | 0.277 | +0.027 [+0.017, +0.037] |
| server.exe CPU per frame (all threads), idle, ms | 7.657 | 7.293 | **-0.364 [-0.568, -0.160]** (-4.7 %) | 7.089 | 6.579 | **-0.510 [-0.693, -0.328]** (-7.2 %) |
| server.exe CPU per frame, pan, ms | 7.644 | 7.616 | -0.029 [-0.271, +0.214] | 8.826 | 8.112 | -0.715 [-1.139, -0.291] |
| server GPU per frame, idle, ms | 1.242 | 1.223 | -0.019 [-0.054, +0.016] | 2.335 | 2.278 | -0.057 [-0.170, +0.056] |
| EXE 3D utilization, idle, % | 24.8 | 24.7 | -0.12 [-0.48, +0.24] | 14.0 | 13.3 | -0.67 [-1.15, -0.19] |
| bake-ahead, ms (huge, 4,922 land tiles) | not complete at 120 s | not complete at 120 s | - | 24,428 | 26,642 | **+2,214 [+359, +4,069]** (+9.2 %) |
| bake-ahead: resident share at the 120 s cut | 0.826 | 0.420 | **-0.406 [-0.550, -0.262]** | 1 | 1 | - |
| bake-ahead: bakes per second to the cut | 45.2 | 26.1 | -19.1 [-27.6, -10.6] (-41 %) | (7,407 bakes) | (6,934 bakes) | -473 [-1,140, +193] |

**Why the numbers moved [C for the mechanism of the flag, I for the attribution].** With
`PREVENT_INTERNAL_THREADING_OPTIMIZATIONS` the driver does on the calling thread what its own worker threads did
before (command translation and submission). So the render thread's time per frame rises: the frame's submission by
0.4-0.7 ms (+9 to +17 %), the terrain draw's own `cpu_ms` by 0.25-0.35 ms (+27 to +37 %: its D3D11 calls are no longer
queued for another thread), the thread's CPU by 0.44-0.75 ms per frame. The process as a whole spends **less** CPU per
frame (-0.36 to -0.72 ms): the hand-off to the driver's threads cost more than the work it moved. The GPU's work per
frame does not change (server GPU per frame within ±0.06 ms, intervals include 0). Uncapped (f), the render thread is
already 97-99 % busy at ~226 frames per second, so the dearer frame lowers the server's frame rate (-8.7 %); the
EXE, which waits for the server's frame in lockstep only up to a bound, then runs slightly faster (-1.4 % idle, -3.8 %
pan) [I: less GPU and CPU contention from the server]. The bake-ahead runs in the loop's idle slices: uncapped there
are almost none either way (the huge bake-ahead was not complete 120 s after the map in any f arm), and fewer with the
flag (42 % against 83 % of the land resident at the cut; 26 against 45 bakes per second); at 60 Hz the bake-ahead
completes in both, 2.2 s (9 %) later with the flag, as each frame's submission takes 0.4 ms more of the 16.7 ms.

**A side finding for the MC (not this ticket's) [L]:** with WaitForVSync 0 and no frame cap, the server's render thread
is 97-99 % busy following the EXE's ~230 frames per second on oldpc, and the huge load's bake-ahead then takes minutes
with flags 0 too (83 % of the land resident after 120 s, 45 bakes per second against about 300 per second at 60 Hz).
A player without vsync or a frame limit sees gray tiles for that long. The render loop gives the bake-ahead only the
time no camera waits (0140's yield); a floor of bake time per second, or drawing at most one server frame per EXE
frame interval below some rate, would fix it. 0414's 22.8 s bake-ahead was measured with product_path's settings
(WaitForVSync as tb1 has it: 1, read by this job's prep; display 60 Hz).

#### 15.1.5 Decision (ticket 0435, 2026-11-17)

**`render_prevent_driver_threading` defaults to 0 in the product, and to 1 while recording a scene bundle**
(`render_test_record`; an explicit key wins either way). The flag changes no pixel a player sees beyond 0336's one
pixel by 1 in some frames, and costs the player the render thread's time (+0.45 ms per frame, +11 %) and the huge
bake-ahead +2.2 s at 60 Hz (and halves the bake rate when uncapped); it saves process CPU (-0.4 to -0.7 ms per frame),
which does not reach the player's frame time at 60 Hz (EXE frame time equal) [L, 15.1.4]. For the scene library the
determinism is the point: a bundle recorded from today's server has dumps that the player equals at 0:0 (128 of 128
frames over 16 player runs, 15.1.3), and its `render_config` says `render_prevent_driver_threading: "1"`. **For the
standing check** ("offline equals live", `offline-scene-harness.md` 16): bundles whose `render_config` says "1" can be
held at 0:0; bundles without the key (recorded before 0435) keep 1:1. That file and the check's tolerance logic are
not 0435's files; the library's owner changes them (the next re-recording of the library is enough for 0:0 on all).

**Not verified.** Other GPUs and drivers (only oldpc's RTX 2080; 0336's flip was seen on the workstation's RTX 4080, and
was not seen on oldpc in this series); the cost on the workstation; a frame-rate cap between 60 Hz and uncapped (the
render thread's share grows with the rate); whether a long play session changes any of this (no AI turn was played).
