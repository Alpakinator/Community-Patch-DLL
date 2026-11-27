# The polar ice: IceShader, its mesh and its inputs, and the server's route (epic 0169, seam G)

Status: skeleton by lead-0169, 2026-11-15. Sections 0-6 filled by task 4301 (check and design, 2026-11-15 12:31-13:25
EST, worker 4301, branch `task-4301-ice-check`); the later ice tasks of epic 0169
(`tracker/issues/0169-i2e-seam-g-lighting-colour-grading.md`) extend it. Confidence marks: [C] decompile, disassembly or
dump, [L] live, [I] inferred. Addresses are DX11 EXE VAs at ImageBase 0x400000 (Steam build 4390913, TimeDateStamp
0x546CD0A8); static reading from the `llvm-objdump` listing of task 2903 (`D:/c5-64bit-work/2900/2903/dx11.asm`),
behaviour only, no decompiler text (D83).

What was known before this file (2026-11-15): IceShader is material 59, drawn through `0x4956F0`
(`asset-pipeline.md` 3.2); the Ice step `0x5A0BC0` runs inside CreateTerrain after the river carve and writes the CPU
height field (`asset-pipeline.md` 4.1 step 8); `Ice_Reload` is `0x77CC30` (`world-systems-and-stubs.md`); the ice draw
binds the FOW mask `0xDA12AC` as t8 (store at `0x495DC5`, `disable-systems-and-masks.md`; `fog-of-war.md` 2.8); stock's
water reflection pass draws the polar ice as draw 182 (IceShader, 290,355 indices, Tiny 9V camera 1, `water-system.md`
6.3); under D33 the ice is not drawn (`fog-of-war.md`, fog_edge picture [L]). Files: `ice_d`, `ice_d_sides`,
`icedisplacement`, `ice_environment_diffuse` (BC1 cube), `ice_bump.lean`, `ice_bump_sides.lean`,
`Assets/terrain/Ice/IceSettings(_Low).xml` (`terrain-rendering-inventory.md` 2.1, `water-system.md` 8.3).
*Corrected 2026-11-15 by 4301 (section 3): `0x4956F0` is the ice **mesh builder** (it creates the draw records of
material 59 at `0x495CEB`, which is why the 2903 table lists it), not the draw; the draw is `0x5FACB0`. The Ice step does
**not** write the CPU height field: it builds the ice mesh from the ice plots, IceSettings and `IceDisplacement.dds`.*

## 0. Results in one screen

- **The ice is one static mesh per load**, built by `cvTerrainIceSystem` (the EXE's own source name) in CreateTerrain
  (`0x70AF1C` -> `0x5A0BC0` -> builder `0x4956F0`) from the ice plots (feature 0, read from the DLL at load by
  `0x60ABB0` -> `0x639850`), `IceSettings_Low.xml` (what the instances select) and `IceDisplacement.dds`. The object is
  **Ice = 0xD68F9C** (TerrainSystem+0x529BC). One IMMUTABLE VB (8 B per vertex: TEXCOORD0 R16G16_SNORM position inside
  the chunk's box (*corrected 2026-11-15, section 7: the object's box*), TEXCOORD1 R8G8B8A8_UNORM) and one IMMUTABLE R16 IB, split into chunks (one draw record each, base
  vertex + start index) [C, L].
- **Sizes [L]:** Tiny (56 x 36, 272 ice plots): 1 chunk, VB 489,112 B (61,139 vertices), IB 580,710 B (290,355 indices,
  96,785 triangles), about 1.0 MB. Huge (128 x 80, turn 350, 1,314 ice plots): 8 chunks, VB 2,198,304 B (274,788
  vertices), IB 2,566,644 B (1,283,322 indices, 427,774 triangles), about 4.5 MiB.
- **Not random per session [L]:** VB and IB equal byte for byte over three loads in two processes on two instances and
  two days (3601's capture of 2026-11-13 on c5h-c, and this task's load and in-process reload on c5h-d). No CRT `rand`
  caller in the builder's static closure [C, 2904's closure]. **It does not change in play**: only a load or the debug
  Lua hook `Ice_Reload` rebuilds it [C].
- **Stock draws it twice per frame [L]:** once into "Reflection Texture Raw" (1024 x 1024) inside the reflection pass,
  and once into the back buffer right after the ocean water draws and before the second terrain pass, the coast waves
  and the models. One draw per chunk, no culling on Tiny (the whole mesh, 290,355 indices, both times).
- **Why D33 has no ice [C, L]:** both callers test view bit 0x1 (Terrain): the scene draw sits in the terrain render
  `0x657070` (called only with bit 0x1), the reflection's ice call tests bit 0x1 itself (`0x5A103E`). The mesh **is
  built under D33** (same sizes, [L]); only the draws are gone. The reflection pass itself still runs under D33 (it
  drew a model) [L].
- **Route recommended: (a), tap the EXE's ice mesh once per load** (the water tap's pattern) and draw it in the server
  with the EXE's IceShader blobs read at run time; every input the ice samples already exists in the server (FOW mask,
  cloud base, TerrainFOW LUT, distance-fog ramp, OverlayProj) except its own five textures and the IceFrame block.
  Section 5 has the sizes.

## 1. Premises and their answers

| # | Premise | Answer | Mark |
|---|---|---|---|
| I1 | Under D33 the EXE draws no ice, in the scene or the reflection; the gate is the ice draw's caller | **Holds.** Scene: `0x657070` (TerrainSystem render, reached only with view bit 0x1) calls the ice draw at `0x6570FA` when TerrainSystem+0x75FDB (`0xD8C5BB`, read 1) is set. Reflection: the pass `0x5A0F80` runs without bit 0x1 (callers `0x58953D`, `0x749507`), but its ice call `0x5A1053` is behind `test [view+0x1B4], 0x1` at `0x5A103E`. Live D33 arm (darkred d33 config.ini, bit 0x1 held clear by 3601's `mask_hold.py`): no IceShader draw in the frame, a model draw into a 1024 x 1024 RGBA8 target (the reflection, by size and depth target) still there; grey screen, no ice (Visual Checks `vc-20261115-125749-workstation-d3a9`) | C gate, L draws |
| I2 | Stock draws the ice with IceShader in the scene and once more into "Reflection Texture Raw"; per-draw state | **Holds** (section 2): two draws per frame, both the whole mesh on Tiny, same shaders, textures and states except target, viewport and IceFrame contents | L |
| I3 | Built once at load from the ice plots and the CPU height field by `0x5A0BC0` (or a sibling), not per frame; whether it uses `rand()` | **Holds, corrected:** built once per load by `0x4956F0` (called by `0x5A0BC0`); its inputs are the ice plots, IceSettings and `IceDisplacement.dds` and the water height (`0xD91274` -> Ice+0xFF14); no height-field input was seen [C, not exhaustive]. No `rand` caller in its static closure (2904) [C]; equal bytes over three loads in two processes [L] | C, L |
| I4 | The mesh does not change in play; or which events rebuild it | **Holds:** writers of the mesh fields are the builder, the free `0x537190` (callers `0x440200` = Ice_Reload's body, and `0x668594`) and `0x64C7xx` (teardown). `Ice_Reload` `0x77CC30` is registered by static init `0x9C54CD` in the debug Lua function table at `0xCC2320` (with the `Water_*` hooks), not reached in play [C; I not reached]. No event handler touches Ice. A feature change from Lua during play was not tried live | C, I |
| I5 | VB/IB reachable by data reads (handle tables) and/or a CPU copy; sizes on Tiny and huge | **Holds:** Ice+0xFEE4 VB handle, +0xFEE8 IB handle, +0xFF2C chunk array, +0xFF30 count; handles resolve through the renderer's tables (`water-system.md` 9.1). IMMUTABLE, no CPU copy of the vertices kept (the three 0x2080-byte objects at +0xFED8..+0xFEE0 are renderer batch objects, not vertex data [C]). Sizes in section 0 | C, L |
| I6 | The ice's other inputs; which the server has | t7 distance-fog ramp `[0xDA12B4]` (0188), t8 FOW mask `[0xDA12AC]` (0168), t9 cloud base `[0xDA12B0]` = FOWBaseLayer 1024^2 BC1 (0168), t10 TerrainFOW LUT `[0xD90E60]` 32^3 (0168), OverlayProj in IceFrame rows 8-11 (0167/0168). **No shadow map, no reflection, no environment of the sky** bound. Own: t0 `Ice_Environment_Diffuse.dds` cube, t1 `Ice_D`, t2/t3 `Ice_Bump.lean` pair, t4 `Ice_D_Sides`, t5/t6 `Ice_Bump_Sides.lean` pair (t3 is bound but the PS does not sample it [C, 2903 blob summary]); IceFrame (shared id 42, 480 B) per draw | C, L |
| I7 | What the server's transparent-black reflection loses where ice is reflected | **Not answered in pixels:** the exports have no replay (render targets before and after a draw, `water-system.md` 6.2), and the polar camera's frame has no ocean draw that reads the reflection near the ice in a measurable way without one. What is known: stock draws the whole ice mesh into the reflection every frame (no culling on Tiny) [L]; the water samples it (t4) only where ocean is drawn; so the loss is bounded to ocean pixels whose reflected ray meets an ice floe, i.e. open water inside or next to the polar rows. Method for the module task: draw the server's reflection with and without the ice at the capture's camera and diff the water | I |
| I8 | The ice's files resolve through `c5r::gamefs`; which copy wins; which settings file | `fpkscan lookup` (the game's lookup, `render/tools/fpkscan`) on c5h-d with the VP MODS folder: `Ice_D.dds` -> **VT** `Assets/DLC/VT/Terrain/ice_d.dds` (asset-set dependent, D94); `Ice_D_Sides`, `Ice_Environment_Diffuse`, `IceDisplacement` -> `Resource/DX9/TerrainTextures.fpk`; both `.lean` -> `Resource/Common/TerrainModels.fpk`; `IceSettings.xml` -> the pack copy (1,270 B) over the loose one (1,299 B); `IceSettings_Low.xml` -> loose only. VP adds no ice file. **The instances load `IceSettings_Low.xml`**: the choice at `0x5A0BD9` needs the temp heap (`0x6196D70`, +0x18 - +0x14) >= 64 MB **and** TerrainDetailLevel `[0xD90F74]` > 1; live the heap is 32 MB (TerrainDetailLevel 3), and the settings block Ice+0xFF50 holds the Low file's NearSamples 8 / FarSamples 16 [L, huge save] | C, L |

## 2. What stock draws: the ice draws in a frame

Capture set-up [L, 2026-11-15]: instance c5h-d (F:, Goldberg, CP/VP/EUI 5.4.8), 32-bit DLL pin `i0ship-26234371-ia`
(3C2F47DA...) in the CP slot, no proxy, no server, the instance's own config.ini and GraphicsSettingsDX11.ini (2554 x
1411, frame cap 30), RenderDoc 1.47 preload. Asset set: VT present, folder hash `70402AD7...F68BA4` (55 files, 47,300,858
B; equal to c5h-c's of 3601). Save `C5H-0169-4301-TINY_0009V` (copy of 3601's `C5H-3600-TINY_0009V`: Tiny, turn 9,
every plot revealed and visible; SHA-256 `27420a41...`). Camera: `UI.LookAt(Map.GetPlot(44,3), 0)` (the south pole;
`shell/evidence/0169/4301/find_ice.lua`: 272 ice plots, rows 0-4 and 32-35). No EXE popup over the map (D114).
Captures `F:/c5-64bit-work/0169/4301/captures/c5h-d_20261115-124000_frame1613.rdc` (load 1, 12:43) and `..._frame3495.rdc`
(in-process reload, 12:45); export with `cli.py rdc-export --select materials:59`; frame order with
`render/evidence/0169/4301/frame_order.py`. Reference set and manifest: section 6.

**Frame order (load 1, 599 draws) [L]:** 0-28 UI (320 x 190), 29-83 a 2554 x 1411 RGBA16F pass, 84 shadow (2048^2
R16), **85 one model + 86 IceShader into the reflection target** (1024^2 RGBA8, depth 1024^2 D24S8), 87-89
DrawSimpleOverlay, 90-97 FOWDrawMask, 98-167 TerrainShader into the back buffer, 168-170 WaterShader (ocean),
**171 IceShader into the back buffer**, 172-241 TerrainShader into a 2554 x 1411 RGBA8 target (the second terrain pass),
242-243 WaveShader, 244-337 models, 338-365 the fog (FOWDrawMask, FOWBaseLayer, particles), then GridShader and the UI.
Load 2: the same with ice at 88 and 191.

| | Reflection draw (86) | Scene draw (171) |
|---|---|---|
| Call | DrawIndexedInstanced 290,355 indices, 1 instance, start 0, base 0 (= the one chunk) | the same |
| Shaders | VS `9a689742...` (vs_5_0, 1,248 B, at VA 0x67A07E8), PS `24a6504a...` (ps_5_0, 8,824 B, 0x67A1688); no HS/DS/GS | the same |
| Input layout | TEXCOORD0 R16G16_SNORM @0, TEXCOORD1 R8G8B8A8_UNORM @4; stride 8; triangle list | the same |
| VB / IB | IMMUTABLE 489,112 B (`7e190be1...`) / IMMUTABLE R16 580,710 B (`c40fee81...`) | the same buffers |
| Constant buffers | VS and PS cb1 = IceFrame (shared id 42, 480 B; no own buffer, `programs.txt`) | cb1, other contents (camera) |
| Textures | t0 128^2 x6 BC1 cube (8 mips), t1 512^2 BC3, t2 512^2 RGBA8, t3 512^2 A8, t4 512^2 BC3, t5 512^2 RGBA8, t6 512^2 A8 (all 10 mips), t7 128 x 8 RGBA8 (fog ramp), t8 1024^2 RGBA8 (FOW mask), t9 1024^2 BC1 (cloud base), t10 32^3 RGBA8 3D (TerrainFOW LUT) | the same; t8 redrawn by 90-97 before this draw |
| Samplers | s0-s9 MIN_MAG_MIP_LINEAR WRAP, s10 MIN_MAG_MIP_LINEAR CLAMP; LOD 0..max, no bias | the same |
| Blend | ONE / INV_SRC_ALPHA (premultiplied), alpha ONE / ZERO, write all | the same |
| Depth | test LESS_EQUAL, **write on**, no stencil | the same |
| Raster | solid, cull back, front counter-clockwise, depth clip on | the same |
| Target | "Reflection Texture Raw" 1024 x 1024 RGBA8, viewport 1024^2, depth 1024^2 D24_S8 | back buffer, viewport 2554 x 1411, depth 2554 x 1411 D24_S8 |

**IceFrame (480 B = 30 rows) [L values, C rows read]:** the VS reads rows 0-3 (a matrix), 4.xy, 5.xy, 6.x, 7.x, 8-11.xy,
12.xyz, 13; the PS reads 12, 14-29 in part (2903 `set_blob_summary.txt`). Rows 4.xy and 5.xy = the chunk's box (*corrected 2026-11-15, section 7: the whole mesh's box*) (min x,
min y) and (max x, max y): Tiny (-1793.0, -1098.5) / (1823.9, 1043.1), equal to the chunk record read by
`shell/tools/ice_probe.py`, so a vertex's world x, y = lerp(min, max, (snorm + 1) / 2) [I from the values]. Row 7.x =
2.1167 = water height - 8; rows 8-11 = an OverlayProj-shaped matrix (0.0024038 = 1/416, 0.003125 = 1/320, -0.5, (-2.5385,
2.6, 0.5, 1)); row 12 = the camera position; rows 14-15 = two light directions ((0.433, -0.25, 0.866), (0, -0.423,
0.906)); rows 16-19.x = IceSettings values 0.18431, 0.005, 0.009, 150 (SpecularPower); row 20 = (0.0033, 0.2758, 0.6187)
(the subsurface colour of Ice+0xFFF4); row 21.x = 10.1167 (the water height); rows 24-25 = (0.0329, 0.3297, 0.1450);
row 28 = (0.5725, 0.7137, 0.9529, 1). **The unread components (.zw of rows 4 and 5, .yzw of rows 6, 7, 16-19, 21, 26,
27) hold changing garbage** (camera-like values in this capture, zeros in 3601's): a comparison of IceFrame bytes must mask them [L, C].
The block is filled per draw by `0x5FACB0` from the view's matrices (`0xDA1310` scene / `0xDA1990` reflection,
`0xDA12D0` / `0xDA1A90`), OverlayProj `0xDA1690`, the light block `0xDA1C40`..`0xDA1C6C`, `0xDA1B60`..`0xDA1B94`, and the
ice object's settings fields Ice+0xFF1C..+0x1000C [C reads; the row-by-row map is the module task's].

## 3. The ice mesh: who builds it, from what, when, and whether it is random

**Object [C, L].** Ice = **0xD68F9C** = TerrainSystem (0xD165E0) + 0x529BC (CreateTerrain's `lea ecx, [ebx+0x529BC]` at
`0x70AF16`; Ice_Reload's `mov ecx, 0xD68F9C`). Fields (offsets from Ice):

| Offset | Field | Live Tiny / huge |
|---|---|---|
| +0xFEA5 | built flag (set by `0x5A0BC0`; the draw returns when 0) | 1 |
| +0xFEAC | per-call pass value passed to the record submit (`0x5FB45E`) | - |
| +0xFED8, +0xFEDC, +0xFEE0 | three renderer batch objects (0x2080 B each, made by `0x8BD0C0`), used by the draw's three sub-passes named "Ice" | pointers |
| +0xFEE4 | VB handle (vertex format id 0x16 = stride 8, IMMUTABLE, creator `0x8C6140`) | 0x588 / 0x2A1 / 0x4A4 per load |
| +0xFEE8 | IB handle (R16, IMMUTABLE, creator `0x8C62F0`) | |
| +0xFEF0..+0xFF08 | texture handles of t1, t0, t2, t3, t4, t5, t6 (copied into each draw record +0x18..+0x24) | |
| +0xFF14 | water height (from `0xD91274`) | 10.1167 |
| +0xFF18 | IceSettings float (+0x70 of the parsed block) | 25.0 |
| +0xFF2C, +0xFF30 | chunk array (0x14 B: min x, min y, max x, max y, draw-record handle), count | Tiny 1, huge 8 |
| +0xFF50.. | the parsed IceSettings block (`0x745890`) | Low file's values |

Draw record per chunk (material 59, `0x495CEB`): +0x04 IB, +0x08 VB, +0x0C triangles, **+0x10 start index, +0x14
base vertex** (huge: chunk k starts at index 0 / 163,845 / 322,314 / ..., base vertex 0 / 35,178 / 68,862 / ...), +0x7C
= 0x0B, textures t0-t6 from the object, t7 `[0xDA12B4]`, t8 `[0xDA12AC]`, t9 `[0xDA12B0]`, t10 `[0xD90E60]`, state
words 2, 2, 0, 0, 2, 0, 0, 0, 0, 0, 5 [C, L]. A chunk covers a 2048-unit-wide quarter of one pole on huge (4 per pole);
the chunking keeps every chunk under 65,536 vertices so the IB stays R16 [L sizes, I reason].

**When and from what [C calls; I the meaning of `0x6F6210`].** Per load: LoadGameState slot 7 `0x60ABB0` calls
`0x6F6210` on the ice object (`0x60ADBF`, with map data; probably the grid set-up) and hands it each plot whose feature
is 0 (FEATURE_ICE, `ICvPlot1` +0x74) through `0x639850` (`0x60B6B9`; `asset-pipeline.md` 4.2 point 1). Then
CreateTerrain `0x709B50`, after the river mesh and the carve, calls `0x5A0BC0` (`0x70AF1C`, no condition around it),
which picks the settings file (`IceSettings.xml` when the temp heap is at least 64 MB and TerrainDetailLevel > 1,
else `IceSettings_Low.xml`), parses it into Ice+0xFF50 and calls the builder `0x4956F0(settings, 0xD91270)`. The builder
collects the ice hexes (`0x68E970`; none: it logs "No ice hexes..." and returns), builds a crack/floe structure
(`0x5417B0`, `0x5C32F0`, `0x532E60`, `0x662D90`, `0x577B10`, `0x6CFCF0`; the settings name CrackCost,
InitialPotential, Boundary/InteriorKillRate, EdgePull, SmallPoly/LargePoly heights and depths, skirt slopes), reads
`assets\Ice\IceDisplacement.dds` on the CPU and displaces with it (`0x5E70C0`; DisplaceFrequency, Min -8, Max 15), then
creates the VB, the IB and one draw record per chunk. The two settings files differ in DumpInput (1 / 0), FarSamples
(48 / 16), NearSamples (16 / 8) and LatticeVertsPerHex (Low only, 8.0).

**Randomness [C, L].** 2904's static call closure of CreateTerrain (depth 12, `evidence/2900/2904/rand_in_bake.txt`)
lists every `rand` caller; none is under `0x5A0BC0`/`0x4956F0` (the ice step sits inside that closure). Live: the ice
VB (`7e190be1...`) and IB (`c40fee81...`) are equal in 3601's capture (c5h-c, 2026-11-13, save C5H-3600-TINY_0008 played
one turn) and in this task's two loads (c5h-d, 2026-11-15, the 9V copy, load and in-process reload); the chunk array
equals too (box and counts; its hash differs only by the draw-record handle). Under D33 (third process) the sizes and
counts are equal; its bytes are not in that capture (no ice draw). So the mesh is a function of the map (and the
settings file and `IceDisplacement.dds`), not of the session.

**Changes in play [C].** Nothing rebuilds the mesh but a load and `Ice_Reload` (`0x77CC30`: with no argument it calls
`0x440200(0xD91270)` on Ice: free `0x537190`, two resets, `0x5A0BC0`). `Ice_Reload` is a debug Lua function registered
by the static initialiser `0x9C54CD` into the table at `0xCC2320` (count `0xCC6320`, at most 0x400) beside the
`Water_*` hooks [C]; it is not called in a normal game [I, as `sources/shell-gates/stubs/terrain.md` row 19]. The free
`0x537190` is also called from `0x668594` (unload). A feature change from Lua during play (removing an ice plot) does not
reach the mesh by any path found [C, not live].

**Under D33 [L].** D33 config.ini + view bit 0x1 held clear (no proxy), Tiny: built 1, VB 489,112 B, IB 580,710 B,
1 chunk of 96,785 triangles, the same as stock (`ice_probe.py`, tag d33-load1).

**Minimap [C].** The minimap also draws the ice chunks: `0x62D0AF` copies each chunk's record with material 0x42 = 66
(MiniMapShader) and its own textures. Minimap is on in D33 (0192), so the EXE's minimap shows ice under D33 [I].

## 4. Why the ice is not drawn under D33

- **Scene [C]:** the only scene draw is `0x7AD9C0` -> `0x5FACB0(view, 0, 0)` at `0x6570FA` in the TerrainSystem render
  `0x657070`, which the GVS render calls only when view bit 0x1 (Terrain) is set (`water-system.md` 2). D33 clears bit
  0x1 (option c1), so the call never happens.
- **Reflection [C, L]:** the reflection pass `0x5A0F80` is **not** behind bit 0x1: it is called at `0x58953D` (when
  view+0x1B8 bit 0x8 and +0x890 = 0) and at `0x749507` (no view-bit test before it in that function), and inside it draws the systems whose view
  bits are set: 0x40 `0xD02200`, 0x20 `0xCD0D98`, 0x2 `0xDB13D8`, **0x1 the ice** (`0x5A103E` -> `0x751F30` ->
  `0x5FACB0(target, 0, 1)`). Live D33: the frame still has one model draw into a 1024 x 1024 RGBA8 target with a 1024^2
  depth target before the overlay pass (the reflection, [I] by size and place), and no IceShader draw.
  *(Made 2026-11-15 by 4315.)* *Correction owed in `water-system.md` section 2 (row "Reflection pass `0x5A0F80`: skipped under c1 (view bit 0x1)
  [C]"): the pass runs under D33; only its ice part is behind bit 0x1. Not edited here (the file is not 4301's); for the
  lead.*
- **The mesh is there [L]** (section 3). So the fix is a draw, not a build: either the server draws the ice (route a/b),
  or the EXE's own draw is reached without bit 0x1 (no data-only switch for that was found: the scene call is inside the
  terrain render; the TerrainSystem+0x75FDB byte only gates the ice inside it).
- **A D33 config.ini without the view-mask clear crashes** at the end of the load at VA `0x8CD492` (River render with
  River disabled; the same crash as `water-system.md` 9.7) [L, 2026-11-15 12:48, c5h-d, pid 31260]: the D33 arm needs
  the proxy or 3601's `mask_hold.py`. The crash box was closed under the MC's D60 grant for that box.

## 5. The route for the server, with sizes

The server's frame (increment 2) has terrain, water, rivers, waves and fog; it needs the ice at stock's place: after the
ocean water and before the coast waves and the fog's cloud planes, with depth write on (models of increment 3 would test
against it), and once into the server's reflection target if that target is ever drawn (today transparent black,
`water-system.md` 13.3).

**(a) Tap the EXE's ice mesh at load (recommended).** The mesh is static per load, built under D33, IMMUTABLE and
reachable through handles: the water tap's pattern (`shell/composite/water_tap.cpp`, `water-system.md` 9.5) fits as it
is. Read Ice+0xFEA5, +0xFEE4, +0xFEE8, +0xFF2C, +0xFF30 and each chunk's draw record (+0x0C, +0x10, +0x14); resolve the
handles through the VB/IB tables; `CopyResource` into staging; send VB, IB, the chunk array and the object's tail
(+0xFE80..+0x10010, *corrected 2026-11-15 by 4306, section 7: +0xFE80..+0x1001F*, the settings and the water height, for IceFrame) as a new system in the same image (`VPShellWater`
gains an "ice" part) or an image of its own. Byte patterns to check (about 12): the ice object at `0x70AF16` and
`0x77CC47`, the builder's stores of +0xFEE4/+0xFEE8/+0xFF2C/+0xFF30 (`0x495BEA`, `0x495C02`, `0x495C83`, `0x495C89`),
the record field stores +0x10/+0x14 (`0x495D34`, `0x495D42`), material 0x3B at `0x495CD9`, the built flag at
`0x5A0C09`, the vertex format id 0x16 at `0x495BDD`. Size: tap side 1-1.5 days (patterns, the part, the reader's
checks, tests on the stand-in and one live Tiny and huge copy); data once per load 1.0 MB Tiny, 4.5 MiB huge. Exactness:
byte-equal to stock by construction (the tap copies stock's own buffers).

**(b) Port the builder from this file's description (D83).** Possible in principle: not random per session [L], inputs
known (the ice plots, IceSettings_Low.xml, IceDisplacement.dds, the water height). But the builder is large (eight
sub-steps of crack and floe generation, a CPU displacement pass, chunking) and nothing in this file describes them below
the call level; a port must be byte-equal to be pixel-exact (float order, the crack search), which needs per-step
reference data. Size: 4-8 days for a describe-then-port with byte comparison against the tap of (a) [I]. Only worth it
for true EXE absence (D144's later state); not for increment 2.

**The draw in the server (both routes).** A `c5r` module `ice` (`render/src/ice/`), as the water module: the EXE's
IceShader blobs read at run time from the player's EXE (VS VA 0x67A07E8, PS 0x67A1688, material_passes.csv row 59),
input layout and states of section 2, one `DrawIndexed(prims*3, start, base)` per chunk. Inputs: VB/IB/chunks (from the
tap), five textures through `c5r::gamefs` (`Ice_Environment_Diffuse.dds` cube, `Ice_D.dds` (VT), `Ice_D_Sides.dds`,
`Ice_Bump.lean`, `Ice_Bump_Sides.lean`; the `.lean` reader of 3602, `water-system.md` 8.3), the FOW mask, cloud base and
TerrainFOW LUT (0168), the fog ramp (0188), and IceFrame built from the server's camera (matrices, position, OverlayProj
as for terrain and water), the sun block (the terrain's), the chunk box and the settings fields from the tap. Size: 1.5-2
days with the IceFrame row map (the first half day: trace `0x5FACB0`'s stores row by row; then the module and a frame
test). Tests: (1) unit: the tap image reader refuses crafted images (as the water reader); (2) offline (D137) against this task's
reference set: the tapped VB/IB byte for byte against the capture's, the module's IceFrame against draw 171's (unread
components masked), and the module drawn with the capture's own inputs (VB, IB, IceFrame, textures) as a smoke test;
there is no stock target after the draw to compare pixels with (no replay), so the picture check is live: (3) live,
Tiny 9V, look-at (44,3): the joined product against stock at the same camera, median / p95 / max and a difference
image (COMMON 25), diagnostic until the epic's acceptance. Reflection: draw the ice into the server's reflection target
only when that target is drawn (today it is transparent black by decision 3 (ii)); until then the reflected ice is the
named difference of I7.

**Totals.** Route (a) + the module: about 3-3.5 days (tap 1-1.5, module 1.5-2, live check 0.5). Route (b) adds 4-8
days and is not needed for increment 2.

## 6. Open points

- I7 in pixels: needs a replay (render targets around a draw) or the server's own reflection with and without ice.
- The IceFrame row map (row 0-3 matrix choice, rows 8-11 against `0xDA1690`, rows 22-29) from `0x5FACB0`'s stores: the
  module task.
- The temp-heap condition of the settings choice on the owner's own install (32 MB on the instances [L]; the EXE gets the size
  from its start-up [I]); if the owner's game picks `IceSettings.xml`, its ice differs from the instances' (FarSamples 48).
- Whether the TerrainSystem+0x75FDB byte (the scene ice gate, 1 live) is a config switch (a "show ice" option).
- `water-system.md` section 2's reflection row (section 4 above) is owed a correction. *(Done 2026-11-15 by 4315: water-system.md 2 corrected.)*
- Reference set (game data, never committed, D83): `F:/c5-64bit-work/0169/4301/` = `captures/` (three .rdc),
  `ref/stock_load1/` and `ref/stock_load2/` (the two ice draws each: state.json, VB, IB, IceFrame, textures, DXBC),
  `ref/frame_order_*.json`, `ref/ice_probe.json` (the four probes), `saves/` (the two save copies), `crash/` (the D33
  crash's minidump and crashes.log). Committed manifest: `render/evidence/0169/4301/manifest.json` (75 files, 257,717,867
  B, SHA-256 each). Scripts: `shell/tools/ice_probe.py`, `shell/evidence/0169/4301/` (launch, reload and capture,
  huge probe, find_ice.lua), `render/evidence/0169/4301/` (export, frame order, manifest).

## 7. IceFrame row by row (task 4306, 2026-11-15)

Source: the static reading of the ice draw `0x5FACB0` (2903's `llvm-objdump` listing, lines 683216-683785, read only;
behaviour described here, no listing text committed, D83), checked against draw 171's and draw 86's captured blocks of
4301's reference set (`ref/stock_load1/buffers/51172_9b6b504e.bin` scene, `51180_70d5a20c.bin` reflection) and the
probe's object tail (`ref/ice_probe.json`, Ice+0xFE80..+0xFF7F). Ice = 0xD68F9C; offsets below are from Ice.

**What the draw does per call [C].** `0x5FACB0(target, pass flag, reflection flag)` (3 stack arguments, `ret 0xC`):
returns when the built flag +0xFEA5 is 0 or the chunk count +0xFF30 is 0. W = `[0xD90FD8] - [0xD90FCC]` (the world box
width, SSE). It makes three copies k = 0, 1, 2 of everything per call and draws `2 * ([0xD90FBC] != 0) + 1` of them
(1, or 3 when the map wraps):

| k | world matrix T_k | view used | camera position | cull box (min x, min y, max x, max y) |
|---|---|---|---|---|
| 0 | identity | V | (cx, cy, cz) | (`[0xDA1B60]`, `[0xDA1B64]`, `[0xDA1B90]`, `[0xDA1B94]`) |
| 1 | Translation(+W, 0, 0) | T_1 · V | (cx - W, cy, cz) | the box moved by -W in x |
| 2 | Translation(-W, 0, 0) | T_2 · V | (cx + W, cy, cz) | the box moved by +W in x |

V = `0xDA1310` (scene) or `0xDA1990` (reflection flag set), P = `0xDA12D0` or `0xDA1A90`; (cx, cy, cz) = row 3 of
D3DXMatrixInverse(V) (computed here, not read from `0xDA1440`). The products are `0x8A868C` (D3DXMatrixMultiply,
pOut = pM1 x pM2) and the translations `0x8A86BC` (D3DXMatrixTranslation). The cull box is view 0's second footprint
rectangle (`fog-of-war.md` 3.5 lists the four rectangle arrays `0xDA1B00`/`B30`/`B60`/`B90`), the same for the scene and
the reflection call. For each copy it begins a batch on Ice+0xFED8 + 4k (name string `0xA29C18`, Ice+0xFF34 = 0x15 live
on the instances, 0xB under D33 [L probe]), fills the batch's 480-byte constant block (below), then submits the draw
record of every chunk whose box (chunk +0x00..+0x0C) overlaps the copy's cull box (four `>=` tests, edges inclusive),
with Ice+0xFEAC as the pass value.

**The rows [C store, L value].** Every component the draw stores, by row; values from draw 171 (Tiny 9V, south pole):

| Row | Components | Source | Draw 171 value |
|---|---|---|---|
| 0-3 | all 16 | (T_k · V) · P (for k = 0: V · P; V copied, not multiplied by the identity) | the scene's view-projection |
| 4 | x, y | +0xFF1C, +0xFF20 = the **whole mesh's** box min x, min y | (-1793.0034, -1098.4958) |
| 5 | x, y | +0xFF24, +0xFF28 = the mesh's box max x, max y | (1823.86, 1043.0974) |
| 6 | x | +0xFFC0 + +0xFFC4 (SSE add) = the height range of a vertex | 33 = 25 + 8 |
| 7 | x | +0xFF14 - +0xFFC4 (SSE) = water height - LargePolyDepth | 2.1167316 |
| 8-11 | all 16 | T_k · OverlayProj, OverlayProj = `0xDA1690` (view 0's, also for the reflection call) | 1/416, 1/320, -0.5, (-2.538, 2.6, 0.5, 1) |
| 12 | x, y, z | the copy's camera position (table above) | (1088.469, -1254.792, 424.264) |
| 13 | x, y, z, w | `0xDA1C40..4C` = FOWAnim (`fog-of-war.md` 3.6: (k, k, -2T, 0), k = floor(W * 0.002) / W) | (7/3583, 7/3583, -105.49, 0) |
| 14 | x, y, z | +0xFF38, +0xFF3C, +0xFF40 (a unit vector) | (0.4330, -0.25, 0.8660) |
| 15 | x, y, z | +0xFF44, +0xFF48, +0xFF4C (a unit vector) | (-2.6e-17, -0.4226, 0.9063) |
| 16 | x | +0xFF0C (x87 load/store, bit-exact); equals the `.lean` files' +0x04 float (both ice files 0x3E3CBCBC) [L], so the object keeps the LEAN file's scale | 0.18431 = 47/255 |
| 17 | x | +0xFFD8 (x87) = TextureRepeat | 0.005 |
| 18 | x | +0xFFDC (x87) = BumpRepeat | 0.009 |
| 19 | x | +0xFFE0 (x87) = SpecularPower | 150 |
| 20 | x, y, z | +0xFFF4, +0xFFF8, +0xFFFC = Subsurface_R/G/B as (c / 255)^2.2 | (0.0033027, 0.27583, 0.61869) |
| 21 | x | +0xFF14 (x87) = water height (`0xD91274`) | 10.116732 |
| 22 | x, y | +0xFFE4, +0xFFE8 = SideTextureRepeat_U, _V | (0.005, 0.005) |
| 23 | x, y | +0xFFEC, +0xFFF0 = SideBumpRepeat_U, _V | (0.05, 0.05) |
| 24 | x, y, z | +0x10000, +0x10004, +0x10008 = Underwater_R1/G1/B1 as (c / 255)^2.2 | (0.032876, 0.32973, 0.14497) |
| 25 | x, y, z | +0x1000C, +0x10010, +0x10014 = Underwater_R2/G2/B2 as (c / 255)^2.2 | the same (the file's two colours are equal) |
| 26 | x | +0x10018 (x87) = 1 / Underwater_ColorRamp | 0.1 |
| 27 | x | +0x1001C (x87) = 1 / Underwater_AlphaRamp | 0.5 |
| 28 | x, y, z, w | `0xDA1C50..5C` = the DistanceFog colour (as WaterFrame 0x230); bit-equal to byte * (1/255) in float, not byte / 255 (243 / 255 is one ulp low: 0x3F73F3F4 against the captured 0x3F73F3F5) [L] | (146, 182, 243) * (1/255), 1 |
| 29 | x | -`[0xDA1C60]` (SSE sign flip) = -density | -0.05 |
| 29 | y | x87: exp(-(z * d)) * (-1 / d) * `[0xDA1C64]` with z = **`[0xDA1448 + 0x40k]`** (the camera z of EXE view k, the scene's also in the reflection call) and d = `[0xDA1C60]`; the exponent is not rounded to float (WaterFrame 0x144 rounds it) | -6.1266e-13 |
| 29 | z | 0.0 (stored) | 0 |
| 29 | w | `[0xDA1C6C]` = `0xD90FAC`, the zoom fraction (`water.h` zoomFraction) | 0.34269 |

**Not stored by the draw [C]:** rows 4.zw, 5.zw, 6.yzw, 7.yzw, 12.w, 14.w, 15.w, 16-19.yzw, 20.w, 21.yzw, 22.zw, 23.zw,
24.w, 25.w, 26.yzw, 27.yzw (41 floats, 164 bytes; 316 bytes stored). They keep whatever the batch's constant memory held: in 4301's capture vertex-like
floats (about 2400-2800) and 1.0 in the .w of rows 12, 14, 15, 20, 24, 25. A comparison masks them; the module writes 0
there. The VS reads only rows 0-13 (`cb1[14]`); the PS declares `cb1[30]`.

**What the VS makes of them [C, the VS blob's code read through D3DDisassemble, behaviour only].** A vertex's world x,
y = lerp(row 4.xy, row 5.xy, TEXCOORD0 * 0.5 + 0.5) (the **whole mesh's** box, one IceFrame for every chunk of a copy);
world z = TEXCOORD1.w * row 6.x + row 7.x, so alpha 0 is water - LargePolyDepth (8) and alpha 1 is water + 25 (+0xFFC0
= 25, also at +0xFF18 [L]; LargePolyHeight 10 + DisplaceMax 15 [I]); the normal = TEXCOORD1.xyz * 2 - 1; the overlay uv
from rows 8-11; the cloud-base uv = (world.xy + row 13.zw) * row 13.xy (as the terrain's); position = world x rows 0-3.
*Corrects section 2's "the chunk's box" and section 0's "position inside the chunk's box" [I]: the box is the object's
(+0xFF1C..+0xFF28), equal to the one chunk's on Tiny; on huge (8 chunks) the object box is (-4097.68, -2317.76) /
(4159.88, 2262.55) [L probe] and each chunk's own box is used only for the cull.*

**Checked against the capture [L, 2026-11-15, render/tests/seamg/ice_frame_test.cpp].** With the probe's tail and
the settings fields of the capture, `c5r::ice::computeIceFrame` gives draw 171's and draw 86's blocks bit for bit in
every stored byte (the 164 unstored bytes masked), the matrices fed as the captured product (the capture has V · P only;
the module's own product follows D3DXMatrixMultiply's scalar order, 2 ulp as WaterFrame, `water-system.md` 10.1).
Independent of the capture: rows 4-5, 14-16 and 21 equal the probe's object fields; row 13.xy equals k of the map width;
rows 17-27 equal `IceSettings_Low.xml`'s attributes by the rule above; row 29 equals the stock DistanceFog values with
the camera z; draw 86 (reflection, camera z 3535.5) carries the scene camera's row 29.y (z 424.26), which confirms the
`0xDA1448` read.

**For the tap (4307):** the draw reads the object from +0xFF0C to +0x1001F; section 5's tail +0xFE80..+0x10010 misses
rows 26-27 (+0x10018, +0x1001C). The module takes **+0xFE80..+0x1001F (0x1A0 bytes)**, section 8.

## 8. The module as built: `c5r::ice` (task 4306, 2026-11-15)

**Files.** `render/include/c5r/ice/ice.h` (the contract), `render/src/ice/ice.cpp` (+ its `CMakeLists.txt`), tests
`render/tests/seamg/` (`ice_frame_test.cpp`, `ice_draw_test.cpp`, `ice_util.*`, `CMakeLists.txt`). Own code from sections
2, 3 and 7 (D83); the EXE's blobs and the five files are read at run time.

**What it does.**
- `Renderer::init(device, const ShaderSection&, const GameFiles&, InitOptions)`: finds VS `0x067A07E8` and PS
  `0x067A1688` in the section (NotFound naming the VA), checks their stage (BadLayout) and, through `c5r::shaders::reflect`,
  their declarations against stock's draw: VS input exactly TEXCOORD0.xy + TEXCOORD1.xyzw (float), cb1 only and at most 30
  rows (the EXE's VS declares 14); PS cb1 only and exactly 30 rows (= 480 B, the IceFrame layout); PS resources exactly t0
  cube, t1 t2 t4-t9 2D, t10 3D (t3 is bound by stock but not declared by the PS); samplers within s0-s10 (the PS declares
  s0-s2, s4-s10). Then the input layout (TEXCOORD0 R16G16_SNORM @0, TEXCOORD1 R8G8B8A8_UNORM @4), the 480-byte dynamic cb1,
  11 samplers (s0-s9 linear wrap, s10 linear clamp), the states of section 2 and the seven own textures: t0
  `Ice_Environment_Diffuse.dds` (BC1 cube, sRGB view), t1 `Ice_D.dds` and t4 `Ice_D_Sides.dds` (BC3, sRGB views), t2/t3 and
  t5/t6 the four- and one-channel blocks of `Ice_Bump.lean` / `Ice_Bump_Sides.lean` as **R8G8B8A8_UNORM and A8_UNORM** (as
  stock's views [L]; `terrainsets::createLeanTextures` makes R8_UNORM for 8-bit files, which a PS reading .a would see as
  1.0, so the module creates its own). A missing file fails NotFound naming it; a damaged one the parser's reason; a
  16-bit `.lean` is Unsupported (not seen for the ice). The two `.lean` files differ only in header padding bytes; their
  blocks are byte-equal (so stock's t2 = t5, t3 = t6 [L]).
- `Renderer::setMesh(const Mesh&)`: uploads the VB / IB as IMMUTABLE buffers and keeps the chunks and fields; on any
  failure the previous mesh stays (or none).
- `Renderer::draw(ctx, rtv, dsv, FrameInputs, FrameStats*)`: computes one IceFrame per copy (1, or 3 with `wrapX`) with
  `computeIceFrame` (section 7), then per copy maps cb1 and issues `DrawIndexed(triangles * 3, startIndex, baseVertex)` for
  every chunk, skipping chunks outside the copy's cull box when one is given (`copyCullBox`, `chunkVisible`: the draw's
  inclusive overlap test). Every IceFrame is computed before any draw call, so a bad input draws nothing.

**The input for the tap's reader (4307): `c5r::ice::Mesh`.**

| Member | From the EXE (sections 3, 7) |
|---|---|
| `vb` (bytes) | the VB of handle Ice+0xFEE4 (IMMUTABLE, vertex format 0x16, stride 8): ByteWidth bytes, as copied |
| `ib` (bytes) | the IB of handle Ice+0xFEE8 (IMMUTABLE, R16): ByteWidth bytes |
| `chunks[i].box[4]` | chunk array Ice+0xFF2C, entry i (0x14 B): +0x00 min x, +0x04 min y, +0x08 max x, +0x0C max y (floats) |
| `chunks[i].triangles`, `.startIndex`, `.baseVertex` | the entry's +0x10 record handle -> draw record +0x0C, +0x10, +0x14 (int32) |
| `fields.raw[0x1A0]`, `fields.valid` | Ice+0xFE80 .. Ice+0x1001F as bytes (`field::` names the offsets the draw reads) |

`chunks.size()` = Ice+0xFF30. The module checks (BadLayout unless named): fields valid (NotFound) and every read field
finite; the mesh box (+0xFF1C..+0xFF28) min <= max; VB non-empty whole 8-byte vertices; IB non-empty whole R16 indices; at
least one chunk; per chunk a finite box with min <= max, triangles > 0, base vertex >= 0, start + 3 * triangles within the
IB, and max index + base vertex below the VB's vertex count (it scans the chunk's indices: 290,355 on Tiny). The tap need not
repeat these; it reports its own (pattern, handle, copy) failures. Suggested image part (4307 owns the format): a header
{magic, version, vb bytes, ib bytes, chunk count, 0x1A0}, then the 0x1A0 field bytes, then per chunk 7 x 4 bytes (box,
triangles, start, base), then VB, then IB: Tiny 1,070,266 B, huge 4,765,588 B (+ header).

**What the scene (4308) fills in `FrameInputs`:** `cam.view` / `cam.proj` (scene 0xDA1310 / 0xDA12D0; for a reflection
target 0xDA1990 / 0xDA1A90), `cam.width` / `height` = the target; `camPos` (or the view's inverse); `fogCamZ` = the scene
camera's z, also when drawing into the reflection; `overlayProj` = view 0's (the terrain's); `fowAnim` (`c5r::fow::fowAnim`,
as the terrain's TerrainFrame 0x090); `fog` (the stock DistanceFog defaults; `w` < 0 takes the zoom fraction of the camera
z); `worldWidth` and `wrapX` from the map; `cullBox` optional (without it every chunk of every copy is drawn: the same
pixels, more work); t7 the fog ramp, t8 the FOW mask (after its redraw of the frame), t9 the cloud base, t10 the
TerrainFOW LUT, all required (NotFound naming the slot).

**Tests [L, 2026-11-15 13:37-13:46 EST, workstation, hardware device; EXE the Ghidra copy (build 4390913, SHA-256
b2ce96ae...), install `pure_vp` (VT present: `Ice_D.dds` from `Assets/DLC/VT/Terrain`, as on c5h-d), reference set
4301's].** `c5r_seamg_ice_frame` (7 tests): the module's IceFrame equals all four captured blocks (load 1 draws 171 / 86,
load 2 draws 191 / 88) in every stored byte, row 29.y included (no ulp); rows checked against independent sources (above);
the map copies against a double-precision product; the cull box per copy; every error path. `c5r_seamg_ice_draw` (8
tests): the section's blobs equal the captured DXBC; the seven own textures read back equal the capture's bytes; the views'
formats and mips equal stock's; the module drawn with the capture's VB, IB, camera and t7-t10 (FOW mask at frame start,
mips generated) binds stock's states, samplers, layout, buffers and views (queried from the context after the draw), draws
one DrawIndexed of 290,355 indices, covers 412,652 of 901,562 pixels at 1277 x 706, and gives equal pixels on two
hardware devices (diagnostic picture, Visual Checks `vc-20261115-134143-workstation-13de`); with `wrapX` three copies, the
cull box keeps only copy 0's chunk; negatives for every init, setMesh and draw check (each asserting the status and that
no renderer, mesh or pixel is left half-made), the missing-file case on a stand-in install under `C5R_WORK` (each of the
five files absent in turn). Without the reference set the suite fails, naming `C5R_ICE_REF` (checked with a wrong path:
exit 1). Plain and ASan results: `render/evidence/0169/4306/test-runs.txt`.

**Not verified.** (1) Pixels against stock: there is no stock target after the ice draw (no replay); the picture check is
live, 4309's (section 5 test 3). (2) The EXE's own operation order of D3DXMatrixMultiply: the capture holds only V * P, fed
with an identity projection; the module's own product is scalar left to right, 2 ulp as WaterFrame [I]. (3) The wrap
copies and the cull live: the Tiny camera's capture has one draw; the copies' rows are [C] from the stores only. (4) A
huge mesh drawn (the reference set has the huge probe's chunk records but no huge VB / IB). (5) +0xFFC0 = 25's source in
the settings (LargePolyHeight 10 + DisplaceMax 15 [I]). (6) Cost: not measured (one draw of 96,785 triangles on Tiny).

## 9. The tap and the reader as built (task 4307, 2026-11-15)

Route (a) of section 5 as accepted at the C1 review (MC #1804): data reads and D3D copies of the EXE's resources only, no
EXE code called or written, guarded byte patterns, D125's route on a mismatch. Branch `task-4307-ice-tap`, worker 4307.

**Files.** Layout `shell/common/shell_ice_tap.h`; producer `shell/composite/ice_tap.{h,cpp}` (in `shell_composite.dll`,
called every Present after the shadow tap; key `ice_tap`, **0 in the shipped ini** until 4308 binds the ice, then 1);
reader `server/src/render/srv_ice_tap.{h,cpp}` (the lead's contract header kept, members added); tests
`shell/tests/test_ice_tap.py` (stand-in), `server/tests/render/ice/` (`ice_tap_test.cpp`, `build_ice_tests.py`,
`test_ice_reader.py`); live driver `shell/evidence/0169/4307/run_4307.py`.

**Sections [C, own code].** Control block `Local\VPShellIceTap.<exe pid>` (4096 B, `VPShellIceTapControl` 504 B, seqlock;
magic 'C5IC', version 1; state NONE / WRITING / READY / FAILED / OFF / NO_MESH; failReason; `failText[160]` for D125;
generation, loadSeq, chunk count, image bytes, FNV-1a 64, the copied handles; ack fields written by the server). One image
section per generation `Local\VPShellIce.<exe pid>.<generation>`, written through views of at most 4 MiB, unmapped at once;
**the producer keeps the current generation's section** (not closed on the ack, unlike the water tap) until a newer
generation replaces it, a failure or the kill switch, so a server started later still reads it (Tiny 1.07 MB, huge
4.77 MB of pagefile-backed commit; no address space kept). Image = `VPShellIceImage` (112 B: magic, version, sizes,
generation, loadSeq, pid, chunk count, fields base 0xFE80 and size 0x1A0, stride 8, index format 57 = R16_UINT, VB and IB
bytes, four offsets, image bytes, the two handles), then the fields Ice+0xFE80..+0x1001F (0x1A0 B), then per chunk 32 B
(`VPShellIceChunk`: box min x, min y, max x, max y; triangles = record +0x0C; start index = record +0x10; base vertex =
record +0x14; the record handle = chunk +0x10), then the VB, then the IB, each 16-byte aligned. Tiny 1,070,390 B (fields at 112, chunks at 528, VB
at 560, IB at 489,680), huge 4,765,732 B [L]; the file form (a scene bundle's `ice` member, D137) is the image as is.

**When it copies [C, own code; L below].** Every map Present (camera read OK) it reads 20 bytes of the object (built flag
+0xFEA5, VB / IB handles +0xFEE4 / +0xFEE8, chunk array +0xFF2C / +0xFF30) plus the camera block's gridSeq; when these
differ from the last copied state and have stayed equal for `ice_settle_frames` (30) map Presents, one copy starts (a new
load: new gridSeq and new handles; an `Ice_Reload`: new handles in the same load). Not built or 0 chunks after the settle
time: NO_MESH (a map without ice plots; no failure). The copy: the fields, the chunk array, each chunk's draw record
(material 0x3B, record IB / VB = the object's handles), both buffers resolved through the renderer's tables (water-system.md
9.1) after a vtable check against D3D11 buffers the tap made itself, checked (EXE size = D3D ByteWidth, bind flag, IMMUTABLE,
VB: vertex format 0x16 and stride table entry 8; IB: DXGI 57), every chunk's range checked (start + 3 x triangles within the
IB, base vertex within the VB), `CopyResource` into STAGING buffers on the EXE's device; the next Presents map them with
DO_NOT_WAIT, check every index + base vertex against the VB, re-read the head, the chunk array and the records (a change:
CHANGED, copied again once it settles), hash and publish. Per-load cost [L below].

**Byte patterns [C, checked against the EXE on disk by `test_ice_signatures_match_exe_on_disk`].** 21: the object at
0x70AF16 (CreateTerrain's `lea ecx, [ebx+0x529BC]` and the call of 0x5A0BC0) and 0x77CC42 (Ice_Reload's push of the grid
0xD91270 and `mov ecx, 0xD68F9C`); the Ice step's builder call and the built-flag store at 0x5A0C03; the builder's VB of
format 0x16 at 0x495BDD, its handle store +0xFEE4 at 0x495BEA, the IB create and its store +0xFEE8 at 0x495BFD, the chunk
entry size 0x14 at 0x495C10, the stores of +0xFF2C / +0xFF30 at 0x495C83, the material 0x3B record per chunk (handle at
chunk+0x10, the record create 0x8C69B0) at 0x495CD9, the record's +0x04 IB / +0x08 VB at 0x495CF8, +0x0C triangles = indices
/ 3 at 0x495D16, +0x10 start / +0x14 base at 0x495D2C; and the renderer's tables as the water tap checks them (0x8C615A,
0x8C616D, 0x8C6186, 0x8C6309, 0x8C6346, 0x8C6351, 0x8C5BDA, 0x8C69BE, 0x8C5C8A). Section 5's list is refined: the record
stores are checked over their whole instruction runs, the chunk entry size is added.

**Failures (D125, D132) [C, tests].** A pattern mismatch (or an EXE the composite did not validate): FAILED PATTERN with
`pattern <k> '<name>' at VA 0x... does not match` in failText, the tap off for the process. A refused description: FAILED
DESC (e.g. `VB handle 0x7: vertex format not 0x16 (...)`). A layout check (handle out of range or no buffer, a record of
another material or other handles, a chunk past the IB, a base vertex past the VB, an index past the VB, a count above
4096): FAILED LAYOUT for this object state (no retry until it changes). A fault: FAILED EXCEPTION, off for the process. A
section that cannot be made: SECTION; above `ice_max_kb`: TOO_BIG. The reader: `installFailure()` is true for FAILED with
any reason but CHANGED, and for a refused image before any accepted one; `lastWhy()` = "the ice tap failed: " + failText,
or the refusal's reason and text. `mesh()` is null after NO_MESH, FAILED or OFF (the applied mesh is not the EXE's any
more); a refused image keeps the last good mesh (D132).

**The reader's checks [C, tests]** (`CheckIceImage`, on a copy of the image): control block magic and version (-4), pid,
the expected image name, size limit (BadLayout); a generation not newer than the applied one (-3); the section opens (-2);
the view holds the image (Truncated); image magic, version, header / chunk / fields / stride / index-format sizes (-4);
FNV-1a 64 = the block's, a block without a hash refused (-1); image and block agree (BadLayout); 1..4096 chunks, whole
vertices and indices; the four blocks in order, aligned, inside the image; the fields' built flag 1, count and handles =
the image's, the 32 floats the draw reads finite, the mesh box min <= max; every chunk's box finite and ordered, 1+
triangles, its indices inside the IB, base vertex inside the VB, every index + base vertex below the VB's vertex count
(the module's setMesh checks, repeated, so a mesh the reader accepts is one `c5r::ice::Renderer::setMesh` accepts).

**Live checks [L, 2026-11-15, workstation, instance c5h-d (F:, Goldberg, CP/VP/EUI 5.4.8), the D33 product path by
`run_4307.py` over 3308's prep / run / restore: pinned layout `x64-04b1211b-pfdee656` (server.exe 5B254430..., x64 DLL
29F58E17..., proxy 75C40CEC...), this branch's composite (F1E228B6...) with `ice_tap=1`, VP64Options ServerRenderer = 1,
D33 with Overlay on (D93); asset set VT present (c5h-d's, as 4301's); no picture (the tap has no pixels). The reader is
`ice_tap_test.exe read` beside the game: the pinned server.exe does not read the tap yet (4308 binds it).]**
- **Tiny** (`C5H-0169-4307-TINY_0009V`, a copy of `C5H-3600-TINY_0009V`, SHA-256 27420a41...; 15:32-15:39 EST): READY
  generation 1 of load 3, handles VB 0x4E8 / IB 0x4CA, 1 chunk, image 1,070,390 B (1 view); wall 219.9 ms from the copy's
  start over 2 Presents, composite CPU 7.1 ms. The image's **VB (489,112 B, SHA-256 7e190be1...) and IB (580,710 B,
  c40fee81...) equal 4301's capture byte for byte** (`ref/stock_load1/buffers/43235_7e190be1.bin`, `43236_c40fee81.bin`).
  Against `ice_probe.py` at the same moment: the handles, both ByteWidths, the chunk count, the chunk array's SHA-256 (box +
  record handle, 0x14 B), the record's 96,785 triangles / start 0 / base 0, and the object's +0xFE80..+0xFF7F all equal.
  The reader accepted it (ack 0). A reload of the save in the same process: generation 2 of load 5, handles 0x29C /
  0x299, wall 39.7 ms, CPU 7.4 ms; VB, IB and chunks equal to load 1, and the fields equal except the per-load values
  +0xFED8..+0xFEEB (the three batch objects, the two handles) and +0xFF2C (the chunk array's address) [L].
- **Huge** (`C5H-0169-4307-HUGE_0350`, a copy of c5h-d's `C5H-VP8P-HUGE_0350`, SHA-256 C499D3C6...; 15:40-15:49 EST): READY
  generation 1, **8 chunks, 427,774 triangles, VB 2,198,304 B, IB 2,566,644 B, image 4,765,732 B** (2 views); wall 2,264
  ms over 3 Presents (the load's slow frames), composite CPU 18.3 ms. Equal to the probe in every check above. Chunks
  (start index, base vertex, triangles): (0, 0, 54,615), (163,845, 35,178, 52,823), (322,314, 68,862, 48,799), (468,711,
  100,031, 55,379), (634,848, 135,703, 58,257), (809,619, 173,200, 53,387), (969,780, 207,087, 55,643), (1,136,709,
  243,231, 48,871). Reload: generation 2, wall 53.8 ms, CPU 21.3 ms, the same bytes (masked as above).
- **Cost [L, indication only, COMMON 23]:** one copy per load costs the render thread 7 ms (Tiny) to 18-21 ms (huge) in the
  Present that finishes it (the index scan, the 64-bit hash, the image and section writes after the maps); the watch
  between copies reads 20 bytes per Present. A one-frame hitch per load; if it matters, the scan and the hash can be spread
  over Presents (not done).
- **Corrected 2026-11-16 (ticket 0385, with 0384):** done. The image's index check, build, hash and section write now run on a worker thread; the largest render-thread time in one Present of the copy fell from 4.53 to 1.46 ms on Tiny and from 19.05 to 1.41 ms on huge (quiet oldpc A/B). The taps also stop copying while the server reads nothing. See [shell-composite-taps.md](shell-composite-taps.md).

**Reference sets (D137; game data, never committed, D83).** The huge mesh as published: `F:/c5-64bit-work/0169/4307/ref_huge/`
(`ice.c5ice` = the file form, `vb.bin`, `ib.bin`, `fields.bin`, `chunks.json`), manifest
`shell/evidence/0169/4307/ref_huge_manifest.json` (SHA-256 and sizes), for 4306's 8-chunk draw offline; the Tiny images and
parts of load and reload: `F:/c5-64bit-work/0169/4307/tiny/{load1,reload1}/`. Run evidence: `shell/evidence/0169/4307/
steps_tiny.json`, `steps_huge.json` (blocks, probe summaries, every check, the composite's ice lines).

**Tests [L, 2026-11-15].** Stand-in (`shell/tests/test_ice_tap.py`, 45 tests, test_host --fake-exe on a hidden desktop):
45 passed, 15:21-15:32 EST (11:18 min), plain build. Reader (`server/tests/render/ice/test_ice_reader.py`): the 7 cases
(24 image refusals, 7 block refusals and a short view, each with its reason, ackResult, counter and text and the last good
mesh kept; section and file form equal; torn; first refused; producer states; fuzz of 1,776 single-byte changes and every
truncation) and the runner's own negatives: 11 passed plain and 11 under ASan (MSVC /fsanitize=address, no report). **The reader's
mesh is accepted by `c5r::ice::Renderer::setMesh`** (4306's checks; `ice_setmesh_test.exe` with the EXE's IceShader and
the pure_vp install on a hardware device): the live Tiny load and reload images and the huge reference set; a chunk moved
past the IB is refused. Stand-in under ASan (`shell/build.cmd asan`): 45 passed, 0 reports, 15:57-16:08 EST. Runs:
`shell/evidence/0169/4307/test-runs.txt`. (An ASan run needs `ASAN_OPTIONS=log_path="..."` quoted: unquoted, ASan
stops every process at start on the drive's colon, so the host never logs; shared-texture-and-compositor.md says so.)

**Not verified.** (1) The pixels: the tap delivers stock's bytes; drawing them is 4308's and 4309's. (2) An `Ice_Reload`
through the debug Lua hook live (only the stand-in's simulated one and a save reload, which has the same effect on the
object: new handles, the same bytes). (3) A map without ice live (NO_MESH is tested on the stand-in only). (4) The composite
CPU per copy on a quiet machine (the numbers above are indications). (5) A device loss between the copy and the maps live
(the stand-in's shutdown path drops the copy and copies again).

## 10. In the scene: the ice at stock's place in the server's frame (task 4308, 2026-11-15 14:58-, worker 4308, branch `task-4308-scene-wiring`)

**Files** [C]: `server/src/render/srv_render_scene.{h,cpp}` (IceSource, MakeTapIceSource, IceFatalStep; Scene::icePoll,
iceApply, IceHook, the inputs in Scene::draw), `server/src/render/srv_water_scene.cpp` (WaterSceneAfterOcean: the hook
between the ocean / rivers and the coast waves), `srv_render_host.cpp` (render_ice, render_test_ice_wait_ms, the fatal step,
the recorder hook). Coded against the contract header `srv_ice_tap.h` (the reader is task 4307's; until it merges its stub
reports Absent and no mesh, so nothing is drawn live and nothing fails: see the Absent rule below).

**What the scene does [C, tests below].**
- **Module:** with `render_ice=1` Scene::init makes a `c5r::ice::Renderer` on the scene's device from the EXE's section
  (`c5r::shaders::ShaderSection::open(exe)`, IceShader VS 0x067A07E8 / PS 0x067A1688) and the install's index the terrain
  shares (the five ice textures); a module that cannot start (no install, a blob or a texture missing) is an install failure.
  A device rebuild makes it again and re-applies the last good mesh.
- **Mesh:** the reader is made per EXE pid (as the shadow's) and polled each loop iteration with the generation last asked;
  a new generation's `c5r::ice::Mesh` goes to `setMesh` (the module's checks, section 8); accepted: kept as the last good mesh
  (`Scene::iceMesh`, for device rebuilds and the recorder), a redraw asked; refused: LOUD with the module's reason, counted
  (`mesh_refused`), the last good mesh stays and is drawn; a refused generation is not offered again.
- **Place:** after the ocean and the rivers, before the coast waves and the cloud planes (section 2's order; the waves are
  WaterScene's second step, the cloud planes FowScene::afterWorld): Scene::draw hands WaterScene a hook
  (`WaterSceneAfterOcean`) that WaterScene::draw calls right after the module's ocean and river draw; when no water is drawn
  (render_water=0, the placeholder, or its draw failed before the hook) the ice is drawn after the terrain, still before the
  cloud planes. Depth write on, LESS_EQUAL, into the terrain's depth (the module's states). Not into the reflection target
  (decision 3: the server's reflection is transparent black; the named difference of I7).
- **FrameInputs from the scene:** `cam` = the terrain's camera (view 0, the camera block's projection, the target's size);
  `camPos` and `fogCamZ` left to the module (row 3 of the view's inverse: the scene camera's z, 2 ulp as section 7);
  `overlayProj` = the terrain's copy-0 OverlayProj of the frame (the overlay pair's view 0, or the fog's kept pair; identity
  without either, as the terrain); FOWAnim, the FOW mask and the cloud base = the fog's of the frame when the terrain draws
  with the fog (`op.fow.enabled`), else the terrain's fog-off constants (mask white = visible, cloud base black; two 1 x 1
  textures of the scene); t7 = the terrain's fog ramp (`Renderer::fogRamp`), t10 = the terrain's Generic TerrainFOW LUT
  (`fowLut(0)`, [0xD90E60]); `worldWidth` = `c5r::water::worldFromGrid(field).width` (= [0xD90FD8] - [0xD90FCC] bit for bit,
  3583 on Tiny) and `wrapX` the field's (three copies on a wrapping map); no cull box (every chunk of every copy: the same
  pixels, more work; the EXE's footprint rectangle 0xDA1B60 is not in the camera block). [I] The fog's DistanceFog values are
  the module's defaults (stock's, as the terrain's and the water's).
- **Options:** server.cfg `render_ice=1` (default) / `0` (none: the picture before 4308, bit for bit);
  `render_test_ice_wait_ms` (default **0 = no bound**: an Absent ice reader is never an install failure by itself, because the
  stub and every composite without 4307's tap report Absent; the lead may set a bound once 4307's section is in every shipped
  composite). Shipped ini `ice_tap=1` is set by the lead at integration (4307's key).
- **Failures (D118, D125, D132):** `installFailure()` (or state Failed) -> LOUD once with the reader's `lastWhy()`,
  `Scene::iceFailed`, the host's `IceFatalStep` calls `HostConfig::fatal(5, why)` once; the module not starting likewise. OFF
  is LOUD once and draws no ice. In play (a mesh refused, a draw error: LOUD per new text, counted) never fatal.
- **Lines:** the init line names render_ice and the bound; the `shadow 1s:` line carries `| ice=<state> drawn=<frames> gen=..
  load=.. chunks=.. copies=.. draws=.. indices=.. meshes=.. mesh_refused=.. draw_err=.. frames=.. failed=0|1`; the totals line
  "ice totals".

**Tests [L, 2026-11-15, workstation, WARP, the EXE's IceShader from the Ghidra copy, install `pure_vp` (VT present)].**
Synthetic mesh (one quad of 4 triangles, both windings, over the camera's ground at z 55; fields: box, water height 30, +25 /
-8, the two directions, the repeats): `ice_drawn` (render_ice=0 and "on without a mesh" bit-equal; with the quad 19,200 of
19,200 px differ, 3 copies on the wrapping test field, the next frame equal), `ice_refused_keeps_last_good` (a chunk past the
IB: refused by setMesh, LOUD once, counted 1, the picture equal to the good mesh's, not offered again, never fatal),
`ice_install_fatal_once` (the source's installFailure: fatal once, code 5, its text; no install: "no game install" fatal once),
`ice_absent_bound` (the stub on a pid with render_test_ice_wait_ms=200: failed after the bound) - plain and ASan, 0 reports.

**Live [L, 2026-11-15 15:56-16:21 EST, c5h-e, the product path of terrain-surface.md 11.10's runs].** 4307's reader was not
merged, so: run 1 with the stub: ice ABSENT, nothing drawn, no failure (no bound), the pole without ice
(`vc-20261115-160053-workstation-b624`: median 23, p95 193, 42.4 % within 16 against stock). Runs 2 and 3 with the test key
`render_test_ice_file` = **this save's ice mesh from 4301's reference set** (the EXE's own VB 489,112 B and IB 580,710 B as
captured, the chunk record and the object's tail from the live probe, +0xFF80..+0x1001F from the captured IceFrame by section
7's row map: `shell/evidence/0169/4308/make_ice_member.py`, 1,070,310 B, SHA-256 bd4fcfe1...; the mesh is a function of the map,
section 3, so it equals what the tap would copy): mesh applied (1 chunk), **drawn in every frame** (3 copies on the wrapping
Tiny map, 3 draws, 871,065 indices = 3 x 290,355), mesh_refused 0, draw_err 0, 0 LOUD lines but the test key's own. At the
south pole (`vc-20261115-161146-workstation-261c`, UI.LookAt (44,3) at the load zoom) the server's ice against 4309's stock
session 1: **median 3, p95 39, max 255, 78.9 % within 16** over the whole frame (no mask; the floes, cracks, the water between
and the cloud planes over them as stock; the rest of the frame carries the increment's other differences). Run 3 recorded the
ice member and sceneshot replayed it (drawn in every frame as recorded). **Not verified:** the tap's own mesh live (4307), the
huge map's 8 chunks, the ice in the reflection (not drawn by decision 3).

## 11. Integrated: the team branch built whole, the real ice tap live, the 8-chunk case, the product build (task 4310, 2026-11-15 16:44-, worker 4310, branch `task-4310-integration`)

**Builds [L, 2026-11-15, workstation].** The team branch `epic-0169-seam-g` at 4379b668 (waves 2 and 3 merged, main merged,
`shadow_tap=1` and `ice_tap=1` in the shipped ini, 4307's reader in place of the stub) built whole **without any fix**:
`server/build_server.py --config release --offline` (server.exe + proxy, 78 s), `shell/build.cmd` plain and `asan`,
`render/build.py --test` (all modules) 32 of 32 and `--asan --test` 32 of 32, 0 skipped, 0 ASan reports (16:47-17:03 EST;
`shell/evidence/0169/4310/builds_4310.sh`, runs `render/evidence/0169/4310/test-runs.txt`). The server has no ASan build
of its own (build_server.py has none): its render units run under MSVC ASan in the server test programs below. Server
and shell suites: section 11.3.

### 11.1 The 8-chunk case (4306's open item) [L]

`render/tests/seamg/ice_huge_test.cpp` (`c5r_seamg_ice_huge`) on 4307's huge set (`C5R_ICE_HUGE`, default
`F:/c5-64bit-work/0169/4307/ref_huge`, manifest `shell/evidence/0169/4307/ref_huge_manifest.json`): the mesh is read from
the tap's file form `ice.c5ice` (header and chunk records as `shell/common/shell_ice_tap.h`) and checked against the set's
loose `vb.bin`, `ib.bin`, `fields.bin` byte for byte; the Tiny set of 4301 gives the frame's shared inputs t7-t10 (the
helpers `makeTex2D` / `Shared` moved from `ice_draw_test.cpp` into `ice_util.{h,cpp}`).
- `setMesh` takes the mesh unchanged: 8 chunks, 427,774 triangles, the chunks back to back in the IB (start of chunk i =
  start + 3 x triangles of chunk i-1), IB = 3 x 2 B x triangles.
- One draw issues **8 DrawIndexed with the chunks' ranges, in the EXE's order** (count = 3 x triangles, start index, base
  vertex): (163845, 0, 0) (158469, 163845, 35178) (146397, 322314, 68862) (166137, 468711, 100031) (174771, 634848, 135703)
  (160161, 809619, 173200) (166929, 969780, 207087) (146613, 1136709, 243231); FrameStats draws 8, copies 1, indices
  1,283,322. With wrap: 24 calls (the 8 ranges per copy k = 0, 1, 2). Cull: a box around chunk 5's centre keeps chunk 5's
  range only (7 culled); a box away from the ice draws nothing (0 px).
- Refusals keep the 8 chunks: the last chunk 1 triangle past the IB, a base vertex past the VB, zero chunks (BadLayout
  each), then the next draw is the 8 ranges again.
- The picture (1200 x 668, a top-down orthographic camera over the mesh's box) covers 87,933 px and is the same on two
  hardware devices. Without the set (`C5R_ICE_HUGE=C:/nowhere`) every case fails naming the variable; exit 1, 0 skipped.
- Plain and MSVC ASan: `render/build.py --test -R c5r_seamg` 3 of 3 (ice_draw, ice_frame, ice_huge), `--asan` 3 of 3, 0
  reports (17:06-17:08 EST).
- How the calls are seen: the test writes the DrawIndexed slot (12) of the runtime's context table (VirtualProtect in the
  test process only, restored with the last hook) with a recorder that forwards; the recorded calls are checked against the
  module's own FrameStats::draws. **Negative result:** a hook "self-check" that called `DrawIndexed(0, 7, 3)` with nothing
  bound **hung the hardware device** (GetDeviceRemovedReason 0x887A0006, DXGI_ERROR_DEVICE_HUNG, every later Map failed)
  on the RTX 4080; do not issue a draw without a bound pipeline even with count 0 [L]. (Whether swapping the object's
  vtable pointer for a copy would also work was not separated: that attempt had the same self-check.)

### 11.2 Live: the real ice tap on the product path, Tiny [L, 2026-11-15 17:11-17:15 EST, c5h-e]

Driver `shell/evidence/0169/4310/run_4310.py` (4308's prep / run / restore), numbers `shell/evidence/0169/4310/summary_live.txt`; the run's logs (render.log, server.log, the composite's log, the picture) and every build and suite log are kept in `F:/c5-64bit-work/0169/4310/` (10 MB).
The product build of 11.4 (server.exe 171545D8..., proxy 1393AE42..., composite 4F69BC1A... of commit 0b0dc14df, clean;
pinned x64 DLL 29F58E17...), shipped ini, D33 + Overlay (D93), server.cfg defaults (`render_shadow=1`, `render_ice=1`),
**no test key**; save `C5H-0169-4310-TINY_0009V` (copy of c5h-c's `C5H-3600-TINY_0009V`, 27420a41...), Vibrant Textures
present (D94).
- The composite: `ice_tap: EXE patterns match (21)`, copy of load 3 READY generation 1 (VB 489,112 B, IB 580,710 B, 1 chunk,
  96,785 triangles; image 1,070,390 B; 181.5 ms wall over 2 Presents, 6.7 ms composite CPU, an indication), acked by
  server.exe with result 0.
- The server: ice NONE -> WAITING -> **ACTIVE** (gen 1, load 3, 1 chunk, meshes 1) and **drawn in every frame** the
  terrain drew: 3 copies, 3 draws, 871,065 indices per frame (3 x 290,355: the whole mesh per map copy, as 4308's test-key
  run), frames 2526, mesh_refused 0, draw_err 0, failed 0. The shadow ACTIVE in the same frames (11.11 of
  `terrain-surface.md`). **0 LOUD lines**, no fatal, guard ok.
- The pole (UI.LookAt (44,3), load zoom; popups closed, D114): `vc-20261115-171400-workstation-b76a` (DIAGNOSTIC, D112)
  against 4309's stock session 1: **median 3, p95 38, max 255, 79.7 % within 16** over the whole frame (no mask). The same
  as 4308's run with 4301's reference mesh as a test key (median 3, p95 39, 78.9 %): the tap's mesh is the reference mesh
  (4307 showed it byte-equal).
- Instance restored by hash, `provision.py verify c5h-e` rc 0, claim released.

### 11.3 Suites (plain and MSVC ASan) [L, 2026-11-15 16:50-, workstation]

Drivers `shell/evidence/0169/4310/suites_4310.sh` (first pass 17:16-18:12 at 3fb51faa) and `reruns_4310.sh`; every run
through its build script's gate or `quietlock.py run`; 0 skipped everywhere; ASan = MSVC `/fsanitize=address` builds
(`--asan` / `bin-asan`), reports into a fresh folder per group, 0 reports in each.

| Suite | Plain | ASan |
|---|---|---|
| `render/build.py --test` (every module: terrain, water, seamg, ...) | 32 / 32 | 32 / 32 |
| `render/tests/seamg` with the 8-chunk case (11.1) | 3 / 3 | 3 / 3 |
| `server/tests/render` host files (`test_residency_source`, `test_render_host`, `test_bake_yield`, `water/test_water_host`, `test_decal_wiring`) | 96 / 109 at 3fb51faa (the 13 failures all `test_decal_wiring`, below); after the fix `test_decal_wiring` 15 / 15, so 109 / 109 | the same: 96 / 109, after the fix 15 / 15 (19:16-19:18) |
| `shadow/` (reader 4304, scene cases 4308) + `ice/scene_ice_test.py` | 46 / 46 | 46 / 46 |
| `ice/test_ice_reader.py` (with the setMesh program) | 11 / 11 | 11 / 11 |
| `overlay/` reader + scene | 39 / 39 | 39 / 39 |
| `decal/test_decal_tap.py` | 12 / 12 | 12 / 12 |
| `fow/test_fow_scene.py` | 3 / 3 | 3 / 3 |
| `water/test_water_scene.py` | 19 / 19 | 19 / 19 |
| `scenebundle` programs (bundle_format 413, recorder 138, sceneshot 137, sources 24 checks) | 4 / 4 | 3 / 4: sceneshot_test hit the build script's 600 s bound under ASan (timing, not a defect); re-run alone with `--only sceneshot_test --timeout 2400` (19:07-19:16, 8.6 min with its rebuild): 137 / 137, exit 0, so 4 / 4 |
| `scenebundle` pytest (`test_recorder_host`, `test_sources`, `test_scenerun`) | 21 / 21 | 21 / 21 |
| `server/tests/feed` (`run_feed_test.py --asan`: test_feed 119 checks, the inject negative) and `test_consumers.py` 20 / 20 | ok | ok |
| `shell/tests` (`test_shadow_tap`, `test_ice_tap`, `test_overlay_tap`, `test_decal_tap`, `test_water_tap`, `test_bake_tap`, `test_build_cmd`, `test_errors`, `test_standin`; 269 tests) | 269 / 269 (18:20-19:07, 47 min) | the composite under ASan (`shell/build.cmd asan`) with the epic's files `test_shadow_tap`, `test_ice_tap` and `test_standin`: 83 / 83, 0 reports (19:18-19:42, 23 min); the other six files not under ASan here (47 min plain for the set; their taps' ASan runs are their own tasks') |

**The merge broke one file [L]:** `server/tests/render/test_decal_wiring.py` (epic 0166's, outside 0169's touches; named
for the lead): 13 of 13 of its rig cases failed after the merge because the rig has no game install, so 4308's scene
cannot start `c5r::ice` (the module needs the install's ice textures) and takes the D118 install-failure route (`fatal
hook code 5: the polar ice cannot install: no game install ...`); 4308 had not run that file. Fixed in the test, not the
product (the route is right: a player without the ice's files gets the error, D118): the rig's `start()` and its one direct
`rig.start` add `render_ice=0, render_shadow=0` beside the `render_fow=0, render_overlay=0` it already had for the same
reason (the shadow reader would also turn Absent into an install failure after its 20 s bound in a rig without the shadow
section). **Driver errors of the first pass, not defects:** the scenebundle pytest ran without `C5_RENDER_TEST_BUILD`
(renderfx took the conftest's old default on D:; 7 failures) and the shell group without the four reader paths
(`C5_SHADOW_READER`, `C5_ICE_READER`, `C5_OVERLAY_READER`, `C5_WATER_READER`); both re-run with them (the table's numbers).
Every stand-in run now has the shipped `ice_tap=1`: the stand-in's fake EXE has no ice patterns, so each logs `ice_tap: FAILED
PATTERN ... DISABLED for the life of the process` and the composite goes on; all 269 shell tests pass with it [L]. The
scenebundle runner sets no ASan log folder: a report would end the program (halt_on_error), so exit 0 means none.

### 11.4 The named product build for acceptance (wave 5)

`C:/c5-64bit-work/0169-build/x64-04b1211b-p0b0dc14df-0169` (made by `shell/evidence/0169/4310/layout_4310.py`; manifest
`shell/evidence/0169/4310/product_build_manifest.json`, `SHA256.txt` and `README.txt` in the folder): commit **0b0dc14df**
of `task-4310-integration` (clean tree; VP_BUILD_REPO_DIRTY 0 in server and proxy), DLL pin 04b1211b8.
- `bin/`: the pinned layout `x64-04b1211b-pfdee656`'s bin (DLL **29F58E17...**, lua51_x64, CvGameDatabase_x64,
  CvLocalization_x64, civ5save.exe) with **server.exe 171545D84ACEEB32638DC9F9FBBCC2CA5F908284810C2F1EFA9466A45C52F32A** and its
  PDB of this commit (`bin/build-server.json` is its build record; the pin's build.json is left out).
- `proxy/bin/`: **CvGameCore_Expansion2.dll 1393AE426AD1D1641AF35B7110BCBAD012B7D78B99F49DA270340866FE9D4FC2**, vp64helper.exe
  16B73447... of this commit.
- `composite/bin/`: **shell_composite.dll 4F69BC1A9A6DEB2AED34652E01370239F5FA86936443B11D3037469DA06F8E61** (shell/build.cmd
  of this commit) and the shipped `shell_composite.ini` (AFB0491B..., `shadow_tap=1`, `ice_tap=1`).
- Use: run_3308-style prep with `--bin <folder>/bin --proxy <folder>/proxy/bin/CvGameCore_Expansion2.dll --composite
  <folder>/composite` (as 11.2). Built with `server/build_server.py --deps` of a release build_all of 04b1211b8 for the
  import libraries only (that build's own DLL, 125E2E2A..., is LTO's other variant, ticket 0358, and is not in the layout).
  Commits after 0b0dc14df on this branch touch evidence, research, the tracker and one test file
  (`server/tests/render/test_decal_wiring.py`, 11.3); no source of server.exe, the proxy or the composite. The lead removes
  the folder at the epic's end.

**Not verified here:** the huge map live with the real tap (4307 showed the tap's 8-chunk image live; the draw of those 8
chunks is offline, 11.1); the ice in the reflection (decision 3); an `Ice_Reload` in play.

## 12. Acceptance (task 4311, 2026-11-15 19:45-20:41 EST, worker 4311, branch `task-4311-acceptance`)

The one product build of 11.4 (`x64-04b1211b-p0b0dc14df-0169`, hashes checked against its manifest before every run), the
shipped `shell_composite.ini` (`ice_tap=1`), server.cfg defaults (`render_ice=1`), no test key; instance c5h-h (VT folder
hash `45b3b433...`, D94). Method, settings and every other class: `terrain-surface.md` 13.6 (frame level) and 13.7 (done B).

### 12.1 Frame level: the ice class against stock's two-session ceiling (ACCEPTANCE, D112 / D114) [L]

`compare_4309.py --arm final` (the ice class = pixels whose ray hits a FEATURE_ICE plot, cut at the water plane; mask:
the joined arm's passthrough frame + 16 px, interface, 16 px frame). Cells: median / p95 / max of the largest channel
difference, share within 16; J = the joined picture against stock session 1, C = stock session 2 against stock session 1.

| Save, camera | ice pixels | J | C (stock's ceiling) | Picture |
|---|---|---|---|---|
| tiny9v `p1_pole_44_3` (4301's south pole, load zoom) | 1.47 M | 0 / 24 / 222, 91.4 % | 0 / 17 / 223, 94.7 % | `vc-20261115-195834-workstation-4bb7` |
| huge `p1_pole_74_76` (the north, the most revealed ice within 4) | 1.92 M | 0 / 47 / 231, 89.0 % | 1 / 21 / 109, 93.3 % | `vc-20261115-201137-workstation-3d35` |
| huge `x1_seam_127_73` (the wrap seam, ice and water) | 1.03 M | 0 / 18 / 224, 94.1 % | 0 / 17 / 106, 94.3 % | (Visual Checks, issue 4311) |
| **huge pooled** | 2.96 M | **0 / 32 / 231, 90.8 %** | **1 / 19 / 109, 93.6 %** | |

- **The floes equal stock** at every ice camera: in the difference images the floes are black (0) and the medians are 0;
  the earlier whole-frame figures (4308's file mesh, 4310's real tap: median 3 / p95 38-39 / 78.9-79.7 % within 16) were
  unmasked frames and are superseded for acceptance by these per-class ones.
- **What is left on the ice class**: the water in the cracks between floes, where stock's water animation is already in
  the ceiling (C's p95 17-21) and where stock reflects the floes while the server's reflection target is transparent
  black (decision 3; the follow-up ticket draws ice into it) [I: the reflection not separated per pixel from the
  animation]; on huge `p1` also the cloud band along the map's top edge over ice plots (0168's cloud-plane phase,
  fog-of-war.md 8.4). The seam camera on huge is at the ceiling.

### 12.2 Done B (2): the real tap's 8 chunks on the huge save, product level [L, 2026-11-15 20:21-20:40 EST]

`run_4311b.py` (terrain-surface.md 13.7; the composite's one test switch there, `shadow_verify=10`, does not touch the ice
tap), save `C5H-0169-4311-HUGE_0350` (C499D3C6..., turn 350), two runs, both alike:
- **Composite**: `ice_tap: EXE patterns match (21)`, READY generation 1 of load 3, **8 chunks, 427,774 triangles, VB 2,198,304 B,
  IB 2,566,644 B, image 4,765,732 B** (2 views), wall 2,261 ms over the load's slow Presents, composite CPU 28.0 ms (run 1:
  2,538 / 33.1 ms; indications, COMMON 23), acked by server.exe with result 0.
- **The image against 4307's huge reference set** (`F:/c5-64bit-work/0169/4307/ref_huge`, its four files first checked
  against `shell/evidence/0169/4307/ref_huge_manifest.json`): the image section `Local\VPShellIce.<pid>.1` read whole: **VB
  SHA-256 d77ca9b7... and IB SHA-256 d8671cef... equal** the set's, the 8 chunk records (box, triangles, start index,
  base vertex) equal, image size equal, the fields Ice+0xFE80..+0x1001F equal **with the per-load values of section 9
  masked and two more words**: **+0xFEB4 and +0xFEB8 are heap pointers that differ between processes** (0x00DB3454 /
  0x0523E220 in run 1 against 0x01373454 / 0x057FE220 in 4307's process: the same low bits; 4307 compared a load and a
  reload in one process and could not see this) [L]. The draw reads neither (section 7's row map). Hence the image's
  FNV-1a 64 differs per process for the same mesh (Tiny in 13.6's runs: 09E2DAE8... and D819CC4F...): compare the parts,
  not the image hash, across processes.
- **Server**: ice NONE -> WAITING -> **ACTIVE** (gen 1, load 3, **chunks 8**, meshes 1) and **drawn in every frame** the terrain
  drew: 3 copies, **24 draws** (the 8 chunk ranges per copy, 11.1's order) and 3,849,966 indices (3 x 1,283,322) per frame,
  `drawn` = frames in each 1 s line, mesh_refused 0, draw_err 0, failed 0, 0 LOUD lines. The same in 13.6's final huge run
  (19,266 frames, ACTIVE, chunks 8, draws 24).
- **Not verified here**: the pixels of all 8 chunks on screen at once (the acceptance cameras show the north pole and the
  seam; 11.1's offline picture draws the 8 ranges); an `Ice_Reload` in play; the ice in the reflection (decision 3).

## 13. The ice in the water's reflection (issue 0382, 2026-11-16, branch `task-0382-polar-ice-reflection`)

Epic 0169's decision 3 left the ice out of the server's reflection target (12.1's "what is left"). **The reflection is
not a mirror** (`water-system.md` 13.6 [C, L]): stock draws the ice (and the models) into "Reflection Texture Raw" with an
orthographic camera along the fixed direction D = (-0.12279, -0.69636, -0.70711) over view 0's unclamped footprint
(0xDA1990 view, 0xDA1A90 projection), and the water reads it at the world position projected by WaterFrame 0x160
(0xDA1890 = view x proj). Section 2's draw 86 is that call: IceFrame rows 0-3 = 0xDA1890 (the k = 0 copy), row 12 = the
eye c - 5000 D, row 29.y the scene camera's z (7).

**What the server does now [C, own code]:** per drawn frame `srv_water_scene.cpp` clears its t4 target ((0,0,0,0), depth
1), computes the reflection camera (`c5r::water::reflectionCamera` of view 0), sets WaterFrame 0x160, and calls the hook
that `Scene::draw` sets while it has an ice mesh: `c5r::ice::Renderer::draw` with the frame's ice inputs (FOW mask, cloud
base, fog ramp, LUT, view 0's OverlayProj, three copies on a wrapping map) but `cam` = the reflection camera at 1024 x
1024 and `fogCamZ` = the scene camera's z, into the target and its D24S8 depth (stock's states: the module's). No cull box
(every chunk, as the scene draw). The ice is drawn twice per frame, as stock (section 2). Option `render_ice_reflection`
(default 1; 0 = the picture before 0382).

**Differences that stay [I]:** stock's t8 at the reflection draw is the FOW mask of the previous frame (the mask is redrawn
at draws 90-97, after the reflection), the server's is this frame's (at most one frame of fog movement); the models'
reflections (increment 3).

### 13.1 Live on the full shipped system [L, 2026-11-16 03:25-04:11 EST, c5h-g, VT hash 45b3b433...]

Product build `C:/c5-64bit-work/0382/build/x64-04b1211b-p8df66df0a-0382` (commit 8df66df0a: server.exe 12EB2E9C...,
proxy D55BC424..., composite F8A37501... built at 5b08b2523 with no shell change since, shipped `shell_composite.ini`
955F67DA...; pinned x64 DLL 29F58E17...; manifest `render/evidence/0382/product_build_manifest.json`), D33 + Overlay,
server.cfg defaults (`render_ice=1`, `render_ice_reflection=1`), no test switch; 4309's joined driver with this ticket's
texts (`render/evidence/0382/run_0382.py`, `chain_0382.sh`), 4311's three ice cameras, popups closed (D114). A first
DIAGNOSTIC run of Tiny p1 on build 5b08b2523 (arm `r0382dry`) found the reflection counters missing from render.log:
the joined per-second line is cut at 1,536 bytes (`water-system.md` 13.6); they now have their line of their own.

- **The reflected ice is drawn in every frame the water draws**: `water refl 1s: w_refl_frames=30-31 w_refl_draws=3`
  (Tiny, 1 chunk x 3 copies) / `24` (huge, 8 chunks x 3 copies), `w_refl_errors=0 w_refl_badcam=0`, 0 LOUD lines in
  render.log, restore by hash equal in both runs.
- **Against stock** (`compare_4309.py --arm r0382`, the ice class; cells median / p95 / max, share within 16; J = joined vs
  stock session 1, C = stock session 2 vs 1; "before" = 4311's `final`, 12.1):

| Camera | J before 0382 | J with 0382 | C (stock's ceiling) | Picture (ACCEPTANCE) |
|---|---|---|---|---|
| tiny9v `p1_pole_44_3` | 0 / 24 / 222, 91.4 % | 0 / 25 / 228, 89.6 % | 0 / 17 / 223, 94.7 % | `vc-20261116-034213-workstation-63ff` |
| huge `p1_pole_74_76` | 0 / 47 / 231, 89.0 % | **0 / 44 / 104, 91.2 %** | 1 / 21 / 109, 93.2 % | `vc-20261116-034808-workstation-c032` |
| huge `x1_seam_127_73` | 0 / 18 / 224, 94.1 % | **1 / 17 / 103, 94.3 %** | 0 / 17 / 106, 94.3 % | `vc-20261116-034836-workstation-aa78` |
| huge pooled | 0 / 32 / 231, 90.8 % | **0 / 28 / 104, 92.3 %** | 1 / 19 / 109, 93.6 % | |

- **Where the reflection lands** (`render/evidence/0382/edge_band.py`: the ice class's water pixels, split by stock1's
  picture into the band within 12 px of a floe and the open water further out; |arm - stock1|):
  - Tiny p1, the band (51,689 px): before 8 / 36, 76.4 %, mean 13.25; **with 0382 7 / 36, 79.5 %, mean 12.44**; ceiling
    7 / 33, 81.3 %, 11.50. The open water: 6 / 27, 85.0 % = the ceiling's 85.0 %.
  - huge p1, the ice class's water (577,690 px; the floes there are too dim for the band's floe test, 110 px): before
    3 / 18, 93.6 %, mean 5.37; **with 0382 2 / 9, 98.7 %, mean 2.96**; stock's own two sessions 3 / 17, 94.8 %, 5.24.
  - So the floes' reflection is now where stock has it. The Tiny ice class as a whole is 1.8 points lower than 4311's run
    although band and open water improved or equal the ceiling: the rest of the class (the cloud band over the bottom
    rows, 0168's phase) differs from run to run [I: not separated per pixel].
- DIAGNOSTIC crops (`vc-20261116-041143-workstation-27f0` Tiny floe edge at 2x, `vc-20261116-041144-workstation-aa33` huge):
  stock's teal sheen under the floe sides and the bright crack wedges, missing before, are drawn; the difference images
  of 4311's final against stock lose their bright bands along the floe edges.

**Not verified:** the wrap views' 0x160 against a capture with a wrap view in its WaterFrames (the rule is [C] only); the
reflection's pixels bit for bit (no replay of stock's reflection target, 6.2); bit-equality of the LookAt / ortho with
D3DX's own code (relative differences up to 2.1e-6 against the captures); the models' part of the reflection
(increment 3).

### 13.2 What the reflected ice costs [L, 2026-11-16]

`render/tools/reflbench` (built by `render/build.py --target reflbench`): one process per run, a hardware device, the ice
module with the EXE's IceShader and the install's ice textures, the tap's file form of the mesh (4307's Tiny load 1 and
huge sets), the reflection camera of the stock scene camera (Tiny: 4301's draw 171; huge: 4311's `p1_pole_74_76` camera
block; `render/evidence/0382/make_vp.py`), three map copies (the wrapping maps); per rep in one disjoint query the clear of
the 1024^2 RGBA8 target and its D24S8 depth, then the ice draw, timed by GPU timestamps, one rep in flight; 300 reps after
30 warm-up. t7-t10 are constant textures of stock's sizes.

| Machine | Map | Draws, indices per frame | Clear (median) | **Ice into the reflection: median / p95** | CPU of the draw call (median) |
|---|---|---|---|---|---|
| **oldpc** (RTX 2080, driver 31.0.15.4592; quiet job `20261116-032240-02ea`, alone, 3 interleaved sets) | Tiny | 3, 871,065 | 0.0024 ms | **0.0901-0.0908 / 0.093-0.098 ms** | 0.004 ms |
| oldpc | huge | 24, 3,849,966 | 0.0024 ms | **0.2229-0.2232 / 0.226-0.227 ms** | 0.0045 ms |
| workstation (RTX 4080; indication, COMMON 23: the machine was not quiet) | Tiny | 3, 871,065 | 0.0010 ms | 0.041 / 0.042 ms | 0.006 ms |
| workstation (indication) | huge | 24, 3,849,966 | 0.0010 ms | 0.104 / 0.107 ms | 0.007 ms |

So the reflection pass adds about 0.09 ms (Tiny) and 0.22 ms (huge) of GPU time per drawn frame on oldpc: 0.7 % of a
30 fps frame and 1.3 % of a 60 fps frame on huge. The three sets agree within 0.8 % (Tiny) and 0.2 % (huge). The cost is
the vertex work: the whole mesh is drawn for each of the three copies, as stock does without a cull box in the
server (section 10: `cullBox` not given); a cull box from the EXE's footprint rectangle (or the reflection's own window)
would skip the copies outside it [I: not measured]. Results: `render/evidence/0382/cost/` (oldpc's `reflbench.json` and
summary, the workstation's two runs). The job script: `render/evidence/0382/reflbench_job.py`.
