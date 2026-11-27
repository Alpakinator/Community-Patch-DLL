# Trees: the EXE's Forest system, its Tree_Shadow decals and t8 marks, and the route for the server (issue 0370)

Status: design study, 2026-11-16 05:06-05:30 EST, implementer of 0370 (account B), branch
`task-0370-trees-shadow-decals`. Increment 3 groundwork (D143). No product code. Confidence marks: [C] read in a
decompile, disassembly or capture record, [L] seen live or in a stock capture, [I] inferred. Addresses are DX11 EXE VAs
at ImageBase 0x400000 (Steam build, TimeDateStamp 0x546CD0A8). Static reading: read-only headless Ghidra on a scratch copy
of the `civ5` project (`render/evidence/0370/gh.sh` with 0338's `Dec.java`); the decompiler text stays in
`C:/c5-64bit-work/0370/out/` and is not committed (D83). This file states behaviour in own words.

What was known before (2026-11-16): the Forest system's objects and entry points (`world-systems-and-stubs.md` row 7);
its boot-built draw objects and their fog-mask slot, and that trees draw under D33 + Forest + the white mask
(`disable-systems-and-masks.md`, 3011 X2 [L]); the Tree_Shadow decals and the 165 Tiny / 104 huge t8 decisions they
alone cause (`terrain-decals.md` 2.2, 4.4, 14.6); ForestShader = material 44, VS `0783e181` / PS `95325aff`
(`scene-catalogue.md` 7, `unit-draws.md`); Forest's memory, -17 MB on the huge save (PROBES 2026-11-09).

## 0. Results in one screen

1. **What a tree is [C, L].** A card of 9 or 10 vertices and 8 triangles (a bent 3 x 3 grid), taken at random from the
   meshes of one `.gr2` per kind and style: `forest_<style>_2.gr2` (12 cards) and `jungle_<style>_2.gr2` (17 cards),
   the Shared DLC's loose files. The EXE names only the `_2` set (models, `<kind>_<style>_2.dds`, `<kind>_overlay_<style>_2.dds`);
   the base game's `forest_<style>.gr2` / `.dds` in `TerrainModels.fpk` / `TerrainTextures.fpk` are not used by this
   build. Vibrant Textures replaces the forest diffuse files (`Assets/DLC/VT/Terrain/forest/forest_<style>_2.dds`) and
   `forests.xml` [L: texture hashes in stock captures].
2. **Placement [C].** `forests.xml` (VT's copy: 200 random trees, 0 gridded, overlay 0.6, spacing 2.0) is rounded to a
   multiple of 90: **180 trees per plot** in the owner's install (base game: 360). At game start the EXE makes **20 random
   layouts** of a 96 x 96 tile square, each masked by one of the four L8 masks `forest_tile1..4.dds` (the hex shape), with
   a minimum distance of `space_between x (size_a + size_b)` and a random size 0.5-1.0 per tree, sorted. Each forest or
   jungle plot gets one of the 20 layouts at random when its feature arrives. All of it uses the CRT `rand()`: the layout
   is different in every session (as 0367 measured for the shadows).
3. **Per frame [C, L].** For every forest or jungle plot inside a view's frustum the EXE fills one constant block per
   90 trees (x, y, z, scale) and draws one `DrawIndexedInstanced(2160, 1)` per block: **2 draws per plot**, 46-156
   ForestShader draws per frame in the catalogue's Tiny captures (3,290-6,682 visible trees; 12,064 where the wrap view
   draws every block twice). A tree is drawn with
   scale 0 (hidden) when the plot's "cut" mask has a bit (routes, tree-cut surfaces of landmark models and other stamps),
   when it stands below the water height or on a slope with normal z < 0.9 [C]; 15-25 % of the slots are hidden in the
   captures [L]. On hill plots (the EXE's kinds 3/4; hills confirmed by 4550, section 10) every tree is drawn at 0.75 x its size [L].
4. **The draw [C, L].** One pass, scene target, depth test LESS_EQUAL with depth write, alpha blend SRC_ALPHA /
   INV_SRC_ALPHA, alpha test at 0.1, back-face culling, no shadow-map pass and no water-reflection pass, **no LOD and no
   fade** (one card mesh at every distance; no distance term besides the distance fog). The PS takes its luma from the
   tree texture and its chroma partly from a world-space overlay texture (period 1,000 units), applies the Terrain
   ColorKey LUT of the style, mixes in the terrain's screen overlay with `overlay_alpha`, then the fog of war (discard
   where the mask is 0, cloud base, TerrainFOW LUT) and the distance fog. Trees are unlit: the normal is not read.
5. **Tree shadows [C, L].** The first time a plot's trees are built, the Forest adds **one Tree_Shadow decal per plot**
   (category 3, macro-shadow pass 1, `tree_shadow_2.dds`), one quad per drawn tree, half-size 6.0 world units (3.6 for
   the plot kinds 3 and 4, the hills [L, 4550 section 10, 2026-11-16]; the quads are in the height field's sample frame, not world units). It stays when the plot leaves the view, but a cut-stamp change over the plot
   clears the record's built flag and the decal goes until the plot is seen again (8.8; *corrected 2026-11-16, 0420: was
   "It stays when the plot leaves the view" without the exception*). So the shadows (and their tile+0x300 marks, the 165 /
   104 t8 decisions of 0367) exist only for plots that have been on screen in the session. *Corrected 2026-11-16 (4552,
   12.4): the decals do, but the tile+0x300 marks of **every** forest plot are set at the load (huge: 3,020 bits with every
   plot's tiles marked, against stock's 3,015-3,023; mechanism [I], `0x546110` not read); the record form is in 12.1.*
6. **Cost in the 32-bit EXE.** *Measured 2026-11-16 (0420, 8.6): **+29.4 MB** private after the load on the huge save,
   product path, 6 interleaved launches per arm on oldpc (95 % interval +25.3 to +33.4).* Before: memory **+17 MB** (huge save, 2026-11-09 probe, inside that probe's 20 MB noise), +25 MB
   (3009, R4 against R0) and +28 to +38 MB (3011, X2 against X1), one run each [L]. The direct allocations the code shows are about 2-3 MB [I]; the rest
   is not attributed. Frame time: **not measured** in any run; the CPU work per frame is one visibility test per
   forest or jungle plot of the whole map plus about 180 tree updates per visible plot on the EXE's main thread [C], the
   GPU work 46-156 small draws [L].
7. **The routes (section 4).** (B) Forest on in config.ini costs 17-38 MB of the EXE's address space, draws stock's
   trees by construction (with the white mask of D136), brings the Tree_Shadow decals and their t8 marks into the
   server's bake through the existing decal tap [I], and lets the server tap the EXE's real layouts as an oracle. It fails
   at the latest when landmarks and cities leave the EXE: their tree-cut stamps go with them and the EXE's trees would
   stand inside the server's improvements and cities [I]. (A) The server draws the trees: everything it needs is in the
   install, the feed and this file; the layout stays random, so acceptance is per tree only against a tap of B.
8. **Recommendation (section 5):** route A as the target, landed before or with the cities-and-landmarks epic;
   route B as a short bridge and as A's oracle (R5: tap, compare, replace). A: **36-62 h** in five packages; B: **4-8 h**
   in one.
9. **T0 measured (ticket 0420, 2026-11-16, section 8).** Route B on the product path draws stock's trees, fogs them
   through the server's mask and brings the Tree_Shadow decals and stock's tile+0x300 count (huge 3,015-3,023) through the
   decal tap unchanged; it costs **+26 to +29 MB** of EXE private bytes at the huge load and no resolved frame time at idle
   (+0.06 ms, 95 % interval -0.02 to +0.14). **It hides every border line and the territory wash under forest and jungle**
   (the EXE draws its trees after the composited server frame). The forest tap (`forest_tap=1`) writes the session's 20
   layouts and plot records per load for route A, checked against the decal tap tree for tree (one shadow per built plot).
10. **Route A is epic 0431 (2026-11-16, section 9)**: the trees as a stage after the water, before the cloud planes, in
   the server's colour and depth, with the server's own Screen Overlay as the tree PS's t3, so the borders and the wash
   reach the trees as in stock; packages 4550-4555.

## 1. Sources and method

- Stock captures: 3907's catalogue exports (`F:/c5-64bit-work/0301/3907/exports/`, `rdc-export --select all`, save
  `C5H-0301-CAT_0009` = Tiny t9, stock config.ini, VT present, 2554 x 1411, i0ship 26234371e). Scanned by
  `render/evidence/0370/scan_forest.py` (every ForestShader draw: shaders, index and instance count, targets),
  `forest_draw.py` (bindings, samplers, states), `bi.py` (vertex groups per slot), `cb7stats.py` / `scales.py` / `perdraw.py` (tree
  slots, extents, scales, per draw), `match_tex.py` (texture payloads against the install's files by SHA-256). Outputs beside them
  (`*.out.txt`).
- Assets: `find_trees.py` (every pack entry and loose file named like tree/forest/jungle) and `extract.py` (the forest
  models, textures, masks and `forests.xml` into scratch `C:/c5-64bit-work/0370/assets/`, not committed); the models
  read with 9012's `gr2summary.py`.
- Shader behaviour: `fxc -dumpbin` of the two captured blobs into scratch (not committed); section 2.4 describes it.
- EXE code: three read-only Ghidra passes (`gh.sh p1..p4`, registered through `quietlock.py run`), functions listed in
  section 2.

## 2. What stock draws for forest and jungle

### 2.1 Files [C, L]

| What | Files the EXE asks for | Where they come from in the owner's install (VT present) |
|---|---|---|
| tree cards | `forest_{europe,america,asia,africa,europe}_2.gr2`, `jungle_{...}_2.gr2` (table at Forest+0x140, `0x780D80`; the fifth entry, the generic style, repeats Europe) | `Assets/DLC/Shared/Terrain/{Forest,Jungle}/<Style>/` (loose, plain strings; 12,340 / 18,432 B). Forest: 12 meshes `Plane01..12`, 9 vertices, 8 triangles each; jungle: 17 meshes `Tree_Forrest_01..17`, 9 (one with 10) vertices, 8 triangles. Card heights 7.7 (forest) and 8.1-8.8 (jungle) world units, widths 11-19 |
| tree texture (t0) | `<kind>_<style>_2.dds` | forest: VT loose `Assets/DLC/VT/Terrain/forest/forest_<style>_2.dds`; jungle: `Upgrade1Textures.fpk`; BC2 256 x 256, 9 mips [L: payload hash = the captures' t0] |
| overlay (t1) | `<kind>_overlay_<style>_2.dds` | `Upgrade1Textures.fpk`, BC2 (Africa forest BC3) 128 x 128, 8 mips [L: hash] |
| placement masks | `forest_tile%i.dds`, i = 1..`num_tiles` (`0x726050`, also `0x6F6AE0`) | `TerrainTextures.fpk`, L8 128 x 128; the EXE stops with "Incorrect tile texture format (L8 required)" otherwise |
| parameters | `forests.xml` (`0x5DC3C0`: `num_tiles`, `random_trees_per_tile`, `gridded_trees_per_tile`, `overlay_alpha`, `space_between`) | VT's loose `Assets/DLC/VT/forests.xml` wins in this install: 4, **200**, 0, 0.60, **2.00** (base `TerrainModels.fpk` copy: 4, 360, 0, 0.60, 1.33) [L: 2 draws per plot = 180 trees] |
| shadow texture | `tree_shadow_2.dds` (decal variant 3, `decal_filenames.xml`) | `DecalTextures.fpk`, A8R8G8B8 64 x 64, 7 mips (`terrain-decals.md` 1.3) |
| LUTs (t5, t8) | ColorKey `Terrain` and `TerrainFOW` volumes of a style, `[0xD90E4C + 4 i]` / `[0xD90E60 + 4 i]` | built at run time from `Assets/ColorKeys/*.tga` (`asset-pipeline.md` 3.5) |

Not used by this build [C: no string in the EXE]: the base game's `forest_<style>.gr2`, `jungle_<style>.gr2`,
`forest_generic.gr2`, `forest_<style>.dds` and their overlays, and `FeatureModels.fpk`'s `feature_jungle.gr2` (not
looked into; it is an art row of the feature, not the Forest system). *Correction for
`terrain-rendering-inventory.md` 2 ("forest / jungle ground ... drawn by the Forest system [I]"): `forest_tile1..4` are
placement masks read on the CPU, never drawn; the textures drawn are the `_2` set above.*

### 2.2 The system's life cycle [C]

- **App Startup** (gate bit 0x80 at `0x47672D`): `Forest_System::Startup` `0x726050` allocates a 1 MB dynamic buffer
  (`0x8BD0C0`), a pool of 2,000 per-plot entries of 0x34 B (`0x5304E0`), reads `forests.xml` (`0x5DC3C0`; the two tree
  counts are rounded to the nearest multiple of 90), sets the file table (`0x780D80`), loads the ten `.gr2` files
  (`0x601C90`), builds the draw objects (`0x6C1CD0`, below) and loads the L8 masks into an array of 0x4008-byte entries
  at Forest+0xA4. `0x469980` registers four event listeners (`0x655280`, `0x715720`, `0x6085A0`, `0x71BF40`).
- **The draw objects (`0x6C1CD0`)**: for each of the 5 style entries and each kind (forest, jungle) one immutable vertex
  buffer and index buffer holding **90 cards chosen with `rand()`** from that model's meshes, each card's vertices
  tagged with its slot 0..89 (BLENDINDICES.x), and one pool draw object (program 44) with the texture slots of
  `disable-systems-and-masks.md` (3011). The ColorKey LUT index per entry is mapped {entry 0 -> 0, 1 -> 3, 2 -> 2,
  3 -> 4, 4 -> 1} [C; what the mapping means against the terrain's style order is not checked, I].
- **Game start** (`0x440230` -> `0x45DF30`): makes the **20 layouts** (Forest+0x98, 12-byte vectors of 28-byte trees),
  layout k from mask `k mod num_tiles`, then sorts each (`0x6021D0`, an introsort; the two halves of a sorted layout are
  the two draws of a plot [L: each draw covers about 72 x 36 units, one half of a plot]). Sets the "ready" flag +0x13C.
- **Plot records**: the listeners `0x5E7EE0` (jungle) and `0x5CB2B0` (forest) add a record (0x8C bytes, list at
  Forest+0x80/+0x90) of kind 2 / 1, or 4 / 3 when the event's flag byte is set [C]. The flag is very likely **hills**: in
  the capture every plot whose trees are drawn at 0.75 x size stands higher (tree z 12-37 against 12 on flat land), and
  the kinds 3/4 also get the smaller shadow quads [L for the pairs, `perdraw_L01.out.txt`; *confirmed 2026-11-16 by 4550 (section 10): kind 3/4 = plotType hills on all 870 tapped records [L]*],
  with the plot's cube coordinates and **layout index `rand() % 20`**. `0x655280` -> `0x63DDC0` handles removal [I:
  not read]. `0x715720`: when a "cut" stamp with bits 0-2 changes (the 0x20 event of `0x62AE50`, below), every record
  is refreshed (`0x5962A0`).
- **Per frame** (`0x439E00`, from UpdateWorldCamera `0x6FBD0B`): for every record, the plot centre (HexToWorld
  `0x6851C0`) is tested against the six planes of each view (`0x654420`, margin 64 units). A visible plot gets its
  instance blocks (`0x6D7560`: `(random + gridded) / 90` blocks of 90 float4 = cb7) and, on its first build, its
  shadow decal (`0x770EE0`). The style comes from the terrain's per-tile style byte.
- **Render** (`0x6BFE90` -> `0x63A7A0`, gated by bit 0x80 at `0x589632`): per view, a "Forest" marker, the cb1
  constants (camera, FOW and fog matrices, sun and fog values, `overlay_alpha`), then every visible record's draws.

### 2.3 The placement rule [C]

For each layout, until it holds N trees (N = the rounded `random_trees_per_tile`; the gridded count is read but 0 in
both files and its placement was not read):

1. size `s` = 0.5 x (1 + `rand()` / 32767), so 0.5-1.0;
2. up to 20 tries: a point (x, y) uniform in [0, 96)^2; rejected if any tree already placed lies closer than
   `space_between x (s + s_other)`;
3. if the point's mask texel (*corrected 2026-11-16, 4550, section 10: truncated, not rounded, and read one byte before the texel, file index `v x 128 + u - 1`; was "`round(x / 96 x 127)`, `round((1 - y / 96) x 127)`"*) is 0 the try does not count (the loop
   repeats); otherwise the tree is stored with its local position centred (x - 48, y - 48) and moved through two EXE
   matrices into plot-relative world units, plus (x, y) and `s`. (*Corrected 2026-11-16, 4550, section 10: every tree past index 350 gets size 0, so the base game's 360 leave 9 per
   layout undrawn; was "After 350 trees, a tree whose 20 tries all failed gets size 0"*; never reached at 180.)

Per frame per tree (`0x6D7560`): world position = plot origin + the stored offset; z from the height field; the
instance scale is the stored size, or **0** when (a) the cut mask has any of bits 0-2 at the tree (`0x4365E0`, a
2-unit grid of 16 x 16 blocks, `0x4A0A50`), (b) z is below 10.12 (0.2 x 50.58: the water height), or (c) the terrain
normal there (`0x719BA0`, the byte normal map) has z < 0.9. Measured scales of drawn trees 0.375-1.0, median 0.66-0.71
[L, `cb7stats.out.txt`]: on plots of kind 3/4 (hills, above) **every tree is drawn at 0.75 x its size** (0.375-0.75; both
draws of the plot), on the others at 0.5-1.0 [L, `perdraw_L01.out.txt`: 12 of 46 draws]; the 0.75 is applied in `0x6D7560` at `0x6D75A8` (constant `0xA0A4AC`) [C, 4550, section 10; was "not traced"].

**The cut mask** is stamped by `0x62AE50` (polygon -> mask bits + a 0x20 event on the 4 MB queue), called from routes
(`0x5EC600`), the landmark visualiser (`0x700030`), wonders (`0x65A150`), CreateTerrain (`0x709B50`), the farm builder
side (`0x748DC0`) and others, and from `0x69F250` for every model mesh of kind 3, the **"Tree cut surface"** meshes of
landmark and building models (`TreeCut`, `Tree cut` in `landmarks-and-models.md` 4.7; value 2) [C for the callers and the
string, I for which bit each caller writes; *the route stamp's bit is 0x02 [L, 2026-11-20, section 14.1, ticket 4934]*]. So improvements, wonders, routes and other stamped shapes clear the trees
under them, live.

### 2.4 The draw [C, L]

From the captures (every ForestShader draw of 27 exports is the same shape, `scan_forest.out.txt`):

- Input: one 36-byte vertex (position, normal, uv, BLENDINDICES R8G8B8A8_UINT), immutable VB of 90 cards (32,400 B
  jungle, 29,160 B forest), 16-bit IB of 2,160 indices, `DrawIndexedInstanced(2160, 1)`, triangle list.
- VS: world = card position x `cb7[slot].w` + `cb7[slot].xyz` (no rotation: the 90 cards keep their modelled
  orientation); clip = cb1 c0-c3; FOW mask uv from cb1 c4-c7 (mapped to 0..1); FOW base-layer uv from the world xy and
  cb1 c8; the tree's own position passes to the PS (for the overlay and the fog).
- PS, in order: t0 (tree) at the card uv, **discard if alpha < 0.1**; t6 (FOW mask) at the mask uv, **discard if
  mask.x = 0**; t1 (overlay) at world xy x 0.001: its chroma replaces the tree's by the overlay's alpha (YUV mix, luma
  kept); t5 (Terrain ColorKey LUT of the style); the screen overlay t3 at the pixel, weighted by cb1 c10.x (=
  `overlay_alpha` 0.6) and its alpha; fog of war: lerp toward t7 (cloud base, BC1 1024) by mask.x, then t8 (TerrainFOW
  LUT) by mask.x (1 - mask.y); distance fog: t4 (fog ramp 128 x 8) by the screen height and an exponential of the
  distance to the camera (cb1 c12-c14), colour cb1 c13. Output alpha = the tree texture's alpha.
- States: scene target (MSAA RGBA8) with the scene depth (D24S8), depth LESS_EQUAL and write, stencil off; blend
  SRC_ALPHA / INV_SRC_ALPHA (alpha ONE / ONE); back-face culling (front counter-clockwise); samplers linear, wrap for
  t0-t3 / t6-t7, clamp for t4, t5, t8. t2 (Terrain Overlay) is bound but not declared by the PS.
- Not drawn in the shadow-map pass or the water reflection (no ForestShader draw there in any capture); trees are not
  shadow casters (`terrain-surface.md` 9: their shadows are the decals).
- Draw order: after the terrain and water passes, among the model draws, once per view (two views where the map wraps:
  `cities/*` draw every block twice).
- **LOD and fade: none.** No distance-dependent mesh, count or alpha; a plot is drawn whole while its centre is inside
  the frustum (64-unit margin) [C]. The per-session randomness (cards, layouts, plot layout index) is the only
  variation.

### 2.5 Tree shadows and the t8 marks [C, L]

`0x770EE0` (called on a record's first build from `0x439E00`, and for all records from `0x546110`) walks the plot's
layout and, for each tree that is drawn (size not 0, not cut, above water, slope as in 2.3), adds a quad of half-size
`6 x k` (k = 1.0 for kinds 1/2, 0.6 for kinds 3/4) centred on the tree, then hands the quads to the decal variant
wrapper `0x60F180` (category 3, pass 1) as one decal; the record keeps the decal handles (+0x20/+0x24) and
`0x6A09C0` removes them when the record is rebuilt. Draw side and texture: `terrain-decals.md` 4.4 (macro-shadow
target, ONE / ONE, `tree_shadow_2.dds`, priority 0.1); the decal's tiles get +0x300 = 1 like every decal's, which is
where 0367's 165 Tiny / 104 huge t8 decisions come from (`terrain-decals.md` 14.6). Because records are built only
when first visible, **a session's shadows cover only the plots it has shown**: 98 Tree_Shadow decals on the huge save
in one stock census, "the camera's surroundings only" (`terrain-decals.md` 3.3) [L]. *Amended 2026-11-16 (4552, 12.1 and
12.4): the area marks cover every forest plot from the load; the decal's record form (names, 6 vertices per quad, uv,
rect, binary16 of (p - min) x (1 / extent)) is in 12.1.*

## 3. Cost in the 32-bit EXE

| Measure | Value | Source |
|---|---|---|
| Private memory, Forest key off against stock | -17 MB (Forest-off arm 1,968 MB private / 1,702 MB working set against the stock 1,985 / 1,715), huge t350 save `VP8P-HUGE_0350` in play; the probe itself calls deltas under about 20 MB noise (one run per arm) | `research/sources/exe-world-subsystems/PROBES.md` row Forest, 2026-11-09 [L, one run] |
| D33 + Forest against D33 | +25 MB (1,678 vs 1,653) | 3009 R4 vs R0 (`disable-systems-and-masks.md`) [L, one run] |
| D33 + white mask + Forest against D33 + white mask | +28 / +38 MB (1,671 / 1,681 vs 1,641 / 1,643) | 3011 X2 vs X1 [L, one run each] |
| What the code allocates | 1 MB dynamic buffer, 2,000 x 0x34 B entries, 10 VB/IB pairs (about 0.35 MB), 20 textures (about 1.1 MB), 4 masks (64 KB), 20 layouts of 180 x 28 B, 0x8C B per forest plot, Granny files of 10 models | [C, sizes I] |
| Frame time | not measured (no run with Forest on/off timed) | - |
| CPU per frame | per forest/jungle plot of the map: HexToWorld + 6 plane tests per view; per visible plot about 180 trees with HexToWorld, a transform, a normal and a mask lookup | [C] |
| GPU per frame | 46-156 draws of 720 triangles, alpha-tested and blended; 3,290-6,682 visible trees on Tiny (12,064 drawn with the wrap view) | [L, catalogue] |
| Load | not separately measured; Forest is logged nowhere in `system_init.log` | `disable-systems-and-masks.md` |

Against D33's saving (-426 MB on the huge save) Forest gives back 4-9 %. The unattributed part of the 17-38 MB was not
looked into (candidates: the Granny runtime's copy of the ten files, the pool and the D3D11 objects behind them) [I].

## 4. The two routes

| | A: the server draws the trees | B: Forest key on (removed from the D33 list in config.ini) |
|---|---|---|
| What changes | a forest module in the renderer (placement 2.3, draw 2.4, shadows 2.5), one scene stage after the terrain | `shell/tools/darkred.py` D33 list and the product config writer; nothing in the server |
| Inputs | feed: plot feature (forest / jungle) and `kFeatureChanged` (have), fog (have, 0168), heights and normals (have, bake), the plot style (have, terrain), the plot type for the hill kinds 3/4 (have); the cut mask: routes (0166's route curves), the tree-cut surfaces of the landmark and building models the server places (increment 3), farm and wonder stamps | none new; the EXE reads its own feature events, height field (Terrain stays on: config on, view bit cleared) and normals |
| Assets (D83) | `.gr2` cards through `c5models` (plain loose files), DDS through `c5r::gamefs`, `forests.xml`, the L8 masks, ColorKey LUTs (0169's loader), ForestShader bytecode from the EXE's shader file at run time as for the terrain; nothing committed | none |
| Tree shadows + t8 | the server adds its own Tree_Shadow decals (one per plot, a quad per tree, pass 1; `c5r::decal` already draws pass-1 decals into the macro shadow) and marks their tiles for t8 itself | the Forest adds them to the EXE's TerrainDecalSystem; the server's decal tap (0166) reads every record and tile+0x300, so shadows and t8 marks reach the server's bake **without new code** [I: P1 shows the tap reads all classes; not run with Forest on] |
| Fog of war | the server's mask, cloud base and LUT, as for the terrain (0168) | trees fogged by the composite's copy of the server's mask in the white handle's view (forest t6 is in the list of `fog-of-war.md` 9) [I: not run with Forest on]; t7 base written by the proxy's pool pass |
| Depth and order | trees in the server's depth: right against the terrain and, in increment 3, against the server's units and cities | the EXE draws trees after the composite, so it needs the server's depth in the EXE's depth buffer (`depth_write=1`, a temporary increment 2 switch) to be hidden by hills and to hide the server's units standing in forests [I] |
| Picture against stock | random per session in stock too, so equal only in statistics, or tree for tree against a tap of B in the same session | stock's own code: equal by construction except the screen-overlay tint (t3 is filled by TerrainShader passes 2/5/7, which do not run under D33: borders and highlights do not tint trees) [I] |
| EXE address space (D125) | 0 | +17 to +38 MB (section 3) |
| Lockstep (D81) | in the server's frame | the EXE's camera of the same frame; trees and server terrain agree only while lockstep holds [I] |
| When it breaks | - | when Landmark and City leave the EXE (increment 3, sheet section 6): their tree-cut stamps are gone and EXE trees stand in the server's improvements, wonders and cities; also against "true absence" (D144) and the end of the white mask (sheet 6) |
| D80 | in line (the world renderer re-implemented) | a step back: one more world system kept in the EXE |

## 5. Recommendation, estimate, packages

**Route A is the target; route B is a bridge and A's oracle.** B costs little, gives the beta its forests and their
shadows at once and lets A be accepted tree for tree in one session (R5: tap the EXE's layouts and records, draw the
same trees, compare, then switch Forest off). B cannot outlive the cities-and-landmarks epic, so A must be accepted no
later than that epic. If the MC does not want trees back before increment 3, T0 still runs as A's oracle, in a test
configuration only.

| Package | What | Estimate | Depends on |
|---|---|---|---|
| T0 (B) | Forest on in a D33 + white-mask product run: trees, fog, Tree_Shadow decals and t8 marks through the tap (count per plot against 2.5, the t8 decisions against 0367's 165 / 104), the screen-overlay tint, memory (3 launches) and frame time (testbed, COMMON 23); a read-only tap of Forest+0x98 (layouts) and the records (+0x80/+0x90) for A's oracle. Decision for the MC with the numbers: ship B in the beta or keep it test-only | 4-8 h | none |
| T1 | `c5r::forest` placement: `forests.xml`, masks, 20 layouts, plot records from the feed, per-frame blocks with the hide rules (water, slope, cut mask from what the server knows); offline tests against T0's tapped layouts (same positions when seeded from the tap) | 10-16 h | T0 (oracle) |
| T2 | `c5r::forest` draw: ForestShader from the EXE's shader file, cb1/cb7, 90-card VB/IB per style and kind from `c5models`, textures and LUTs, fog inputs from 0168, the screen-overlay input (or none, as under D33), the stage after the terrain; frame-level comparison with T0's EXE trees seeded from the tap | 10-16 h | T1, 0168 |
| T3 | Tree_Shadow decals and t8 marks from the server's placement into its decal set (pass 1) and tile marks; bake comparison against T0's tap | 4-8 h | T1, 0166 |
| T4 | Cut mask: rasterise the tree-cut surfaces (`.gr2` meshes of kind 3) of the models the server places, plus routes and farm stamps; until the landmark epic, read the EXE's mask (`0x4365E0`'s grid) as a stand-in | 6-12 h | cities-and-landmarks epic for the model part |
| T5 | Live acceptance on the product path: Tiny and huge, frame time and server memory, Visual Checks; Forest off again (B retired) | 6-10 h | T2-T4 |

Total A: **36-62 h** (T1-T5), plus T0 4-8 h. All hours [I], in the style of the increment 3 sheet.

## 6. Open points

- The flag that makes plot kinds 3 / 4 (trees x 0.75, shadow quads x 0.6): the capture says hills [L by height, I];
  the event's sender (`0x5E7EE0` / `0x5CB2B0` callers) and the place of the 0.75 in the code were not traced.
- Which bit each `0x62AE50` caller writes, and whether roads clear trees in practice [I].
- *Answered 2026-11-16 (4551, section 11):* the ColorKey index mapping of `0x6C1CD0`: the LUT index is {0,3,2,4,1}[style entry] for t5 and t8 alike; with one volume per type on every install measured it changes no pixel [C].
- *Answered 2026-11-16 (0420, 8.1, 8.8): the layouts hold the plot-relative offsets after the matrices; the tap writes them.* The two EXE matrices of the placement (layout square to plot-relative world units) [not read; T0's tap gives the
  result directly].
- Route B's three [I] cells (tap gets the shadows, fog through the white handle, depth) are T0's checks. *Answered 2026-11-16
  (0420, 8.4-8.5): the tap gets the shadows and the bits [L]; fogged trees are drawn dimmed [L, no stock picture at that
  plot]; depth: the trees cover the server's borders and overlay [L]; their order against hills and the server's own models
  was not checked separately.*

## 7. Evidence

`render/evidence/0370/`: the scripts named in section 1 and their outputs (`scan_forest.out.txt`, `match_tex.out.txt`,
`cb7stats.out.txt`, `perdraw_L01.out.txt`, `bi.out.txt`, `forest_draw.out.txt`); `gh.sh` (the Ghidra wrapper; run from the worktree root as
`sh render/evidence/0370/gh.sh NAME d:0x439E00 ...`, needs 0338's `Dec.java` in the script folder). Scratch, not
committed: the extracted assets, the shader disassembly and the decompiler output under `C:/c5-64bit-work/0370/`.

Sources: the files named above; `research/sources/exe-world-subsystems/PROBES.md` (memory row); 3907's catalogue
exports on F:.

## 8. Package T0: the forest tap and route B measured (ticket 0420, 2026-11-16)

Implementer of 0420 (account B), branch `task-0420-forest-on-oracle`. Static reading as in section 1 (a read-only copy
of the `civ5` Ghidra project in `C:/c5-64bit-work/0420/ghidra`, `render/evidence/0420/gh.sh`; the decompiler text stays
in `C:/c5-64bit-work/0420/out/`, D83).

### 8.1 The Forest object's containers [C]

Forest_System is the static object **0xDA91C8** (`mov ecx, 0xDA91C8; call 0x439E00` at `0x6FBD06` in UpdateWorldCamera).

| Offset | What | Read by |
|---|---|---|
| +0x78 | an indexed list of plot records (a pool with a free list and a used list): +0x78 free head, +0x80 the record array (0x8C bytes per record), +0x8C the used list's length, **+0x90 its first index**, +0x94 its last index | `0x439E00`, `0x546110`, `0x63DDC0` (remove by cube coordinates), `0x6EBC20` (insert, sorted by the plot centre's depth: `0x71CA10` appends, `0x56D380` inserts before), `0x6D8EF0` (unlink) |
| record +0x00 / +0x04 | next index / previous index (bit 31 of +0x04: in use); the end is **0x0FFFFFFF** | the walks above |
| record +0x08 | built flag: the plot's instance blocks and its shadow decal were made (set by `0x439E00` on the first frame the plot is in a view) | `0x439E00` |
| record +0x28 / +0x2C / +0x30 | the shadow decal handles (a vector: data, count, capacity) | `0x770EE0` (`this` = record + 8), `0x6A09C0` |
| record +0x78 | layout index (`rand() % layout count`) | `0x439E00`, `0x546110` |
| record +0x7C / +0x80 / +0x84 | the plot's three cube coordinates (ints; converted to float and passed to HexToWorld `0x6851C0`) | `0x439E00`, `0x63DDC0` |
| record +0x88 | kind 1 forest, 2 jungle, 3 / 4 the same with the event's flag (very likely hills, section 2.2) | `0x439E00`, `0x770EE0` |
| +0x98 / +0x9C / +0xA0 | the layouts: a vector (first, count, capacity) of 20 vectors of 12 bytes (first, count, capacity), each of 28-byte trees | `0x45DF30`, `0x439E00` |
| +0x128 / +0x12C | random / gridded trees per plot (`forests.xml`, rounded to 90); blocks per plot = their sum / 90 | `0x439E00`, `0x5A3BB0` |
| +0x130 | `num_tiles` (the masks) | `0x45DF30` |
| +0x138 | `space_between` | `0x5A3BB0` |
| +0x13C | ready flag: set by StartupGame (`0x440230`) after the layouts were made; the listeners add no record while it is 0 | `0x439E00`, `0x5CB2B0`, `0x5E7EE0` |

**A tree (28 bytes, `0x5A3BB0`)**: +0x00, +0x04, +0x08 its offset from the plot after the two EXE matrices (+0x00 and
+0x04 are added to the record's first two cube coordinates as floats; the third coordinate is then minus their sum), +0x0C
1.0, +0x10 / +0x14 its point in the 96 x 96 placement square, +0x18 its size (0.5-1.0; 0 for a tree that found no place,
section 2.3; `0x770EE0` skips size 0). So the layouts already hold the plot-relative positions route A needs: section 6's
open point "the two EXE matrices" is answered by the tap's data, not by reading the matrices.

### 8.2 The forest tap as built [C, tests]

`shell/composite/forest_tap.{h,cpp}` (contract in the header), in `shell_composite.dll`, key `forest_tap` in
`shell_composite.ini` (default **0**; test configurations only). Per `forest_poll_ms` (500) while a map is loaded: one
guarded read of the header (+0x78..+0xA3, +0x128..+0x13F); with the ready flag set, the 20 layouts and the record list
(walked from +0x90, every record 0x8C bytes, checked: layout count 1..64, trees per layout <= 4096 and <= capacity,
layout index < count, kind 1..4, the walk exactly as long as +0x8C). A set that differs from the last file's and stays the
same for `forest_settle_polls` (2) polls is written to `<composite output folder>\forest_tap\forest_<pid>_<load>_<generation>.bin`
(80-byte header with FNV-1a 64; the layouts as the EXE holds them; 32 bytes per record: slot, layout, cube coordinates,
kind, built, decal handles). At most one file per `forest_min_write_ms` (5000) and `forest_max_files` (200) per process.
Twelve byte patterns (the object, ready flag, list head and end, record size and fields, layout vector and tree layout)
are checked before the first read; an EXE without them, or one the composite did not validate, is refused for good. SEH on
every read: a fault turns the tap off for the process. No EXE function is called and nothing is written into the EXE.
Reader: `render/evidence/0420/forest_file.py` (summary, `--diff` between generations, `read()` for route A).

Tests (`shell/tests/test_forest_tap.py` on `forest_tap_host.exe`, 19 cases, 2026-11-16 09:16 plain and 09:24 under
MSVC `/fsanitize=address`, all pass): every byte of the file against the synthetic object; one file per set; a new
generation on a change and on a new load; the spacing and the file limit; a rebuild after a shutdown writes nothing new;
negatives with no file and one LOUD line each (not ready, 1,000 layouts, kind 9, layout index past the count, a loop, a
short list), a fault (tap off), the real path on an image without the patterns, an unvalidated EXE, three broken
patterns in an image holding the others, the switch off, no map. A 20th case (added after the first live run) checks
every pattern against the installed EXE's file: in job `af13` two pattern addresses were one and two bytes past their
instructions (`0x439F04` for `0x439F03`, `0x5A3E8F` for `0x5A3E8D`), the tap refused the real EXE for good, and the
synthetic pattern image could not see it. 20 of 20 pass, plain and ASan, after the fix (2026-11-16 11:25).

### 8.3 Route B live: method (oldpc, 2026-11-16) [L]

Job `render/evidence/0420/forest_t0.py` (after `tools/testbed/jobs/minimap_step_a.py`; submit lines in
`render/evidence/0420/submit_0420.sh`), on oldpc's `tb1` (the host's own EXE, loaded at base 0x3F0000; Vibrant Textures
present, the workstation's `pure_vp` base), each arm one launch with `product_path.py`'s prep and restore by hash
(`provision.py verify` rc 0 after every arm). Arms: **fo** = the product path (proxy, `server.exe` ServerRenderer = 1,
the composite with `model_fow=1`, the white mask) with config.ini D33 and **Overlay and Forest kept on**
(`product_path.py --keep-on Forest`): route B as it would ship; **ft** = fo plus the test key `forest_tap=1`; **d33** =
today's product list. Masks after the load: fo/ft config 0xFFAC3EE7, view 0xFF8C3EE6 (bit 0x80 set); d33 0xFFAC3E67 /
0xFF8C3E66. Ship (`build_0420.sh`): server.exe `3b1505b3` and proxy `4e2c6407` built from this branch (main f7a6a680,
release, deps of the pin 04b1211b8), x64 DLL `29f58e17` (pin layout `x64-04b1211b-pfdee656`), composite `f651e4fa`
(job af13) / `5048311e` (pattern fix). Saves: `TB-TINY_0009V` (SHA-256 27420a41..., Tiny turn 9, every plot revealed
and visible) and `TB-HUGE_0350` (c499d3c6...). Per arm: load and 20 s settle, memory (`memory.py`), the cameras of
4309's plans (UI.LookAt; units asleep and the auto unit cycle off; popups closed by Esc while the back buffer's top band
is dim, D114: none was open), the composite's back buffer per camera into the host's Visual Checks store, 2 AI turns.
Stock references: 4309's stock session 1 on the workstation at the same plots (2554 x 1411, resized to the testbed's
1914 x 1051: approximate, another machine, screen and aspect; trees are random per session in stock too).

Jobs: `20261116-091950-af13` (Tiny ft, fo, d33; huge ft, d33; 10:49-11:11, exit 5 = the tap's LOUD refusal, every arm
otherwise in game, 2 of 2 turns, no crash box, server alive); `20261116-092032-9e22` (quiet: huge, warm-up fo, then
fo/d33 ABBA x 3, a 45 s idle window per launch); `20261116-113144-c78e` (Tiny ft, d33; huge ft, fo, d33 with the tap
fixed, a decal watcher, a fogged-forest and a border-over-forest camera found in the game).

### 8.4 What route B draws: trees as stock, but over the borders and the territory wash [L]

- **Trees are drawn** in the product path with Forest on, forest and jungle, at every camera with forest (job af13: Tiny
  4 cameras in fo and ft, huge 6 cameras in ft), and look like stock's at the same plots (the same cards, density,
  colour, hill plots smaller; the river, coast and city plots clear) [L, by eye against 4309's stock session 1;
  Visual Checks `vc-20261116-105521-oldpc-cc97` (fail, below), `-105533-oldpc-4aaa`, `-105544-oldpc-8ab4`,
  `-105556-oldpc-f1ca` (pass), comparison figures `F:/c5-64bit-work/0420/figs-af13/`]. The difference to the resized
  stock picture (largest channel, all pixels) is median 8-12, p95 112-123 on Tiny (D33: 8-15 / 111-127) and median
  12-29, p95 149-160 on huge (D33 the same or worse): the framing differs between the two machines, so these numbers say
  only that Forest on moves the picture toward stock, never away [L].
- **The screen-overlay tint (acceptance 3), and the borders: route B is not equal to stock where a border or territory
  crosses a forest** [L, Tiny w1, plot 33,14, Carthage]: stock draws the territory wash and the border line over the
  trees (the trees take the overlay through t3 at `overlay_alpha` 0.6, section 2.4; *corrected 2026-11-16 by 4551, section 11: no overlay pass follows the trees in stock, the borders reach them only through t3; was "and the border is drawn after them"*);
  in route B the server draws the overlay into its terrain under the composite and the EXE then draws its trees on top,
  so **the purple border line disappears under the forest and the trees inside the border carry no wash**. Section 4's
  [I] "t3 is filled by TerrainShader passes that do not run under D33" is the mechanism for the wash; the hidden border
  line is a second, larger effect of the draw order (the EXE's trees come after the composited server frame). Highlights
  drawn by the overlay (selection, movement ranges) fall under trees the same way [I: not pictured].
- **Fog of war** [L, job c78e, huge, plot 76,41, the forest plot with the most fogged forest around it, found in the
  game]: the fogged trees are drawn dimmed toward the fog colour, so the forest's t6 reads the server's mask behind the
  white handle as section 4 expected (`vc-20261116-121937-oldpc-bd91`); no stock picture exists at that plot, so their
  exact colour against stock is not verified.
- **Borders on the huge save** [L, job c78e, plot 94,39, a visible owned forest next to another owner's forest]: the cyan
  and purple border lines through the forest, drawn by the server (D33 picture `vc-20261116-122549-oldpc-f461`), are
  covered by the EXE's trees almost everywhere in route B (`vc-20261116-121948-oldpc-583f`, fail). On a late-game map
  with many borders in forest and jungle this is the most visible thing route B breaks.

### 8.5 Tree_Shadow decals and the t8 marks through the decal tap [L]

The decal tap and the server's decal reader take the Forest's shadow decals with no change: the server logs every
generation with its decal count and the EXE's tile+0x300 count of the image's area block (`render.log`, "decals:
generation N applied"). Job af13, the generation after the load settled, and the last one:

| Save, arm | decals after the load | area tiles (tile+0x300) after the load | last generation: decals / area |
|---|---|---|---|
| Tiny, ft | 724 (g3) | 522 | 757 / 528 (g9, after the tour and 2 turns) |
| Tiny, fo | 723 (g3) | 535 | 757 / 541 (g10) |
| Tiny, d33 | 630 (g2) | 276 | 629 / 276 (g3) |
| huge, ft | 17,829 (g2) | 3,018 | 18,123 / 3,022 (g27) |
| huge, d33 | 17,710 (g4) | 2,721 | 17,847 / 2,726 (g20) |

- **The area bits come back to stock's count**: on the huge save 3,018-3,022 tiles with Forest on, 2,721-2,726 under
  D33, against 0367's EXE bits of stock 3,015-3,021 and D33 2,719 on the same save (terrain-decals.md 14.6) [L]. So the
  165 / 104 t8 decisions that 0367 traced to the tree shadows' bits alone arrive with route B by construction [I for the
  decisions themselves: they need +0x2FC, which the tap does not read; the bits they rest on are stock's]. Tiny9v has no
  stock bit count to compare with (0367 measured TINY_0008); Forest adds 246-259 tiles there.
- Forest adds about 94 decals at the Tiny load (every plot of Tiny9v is visible, so most forest plots are built at once)
  and about 120 at the huge load, more as the camera visits plots (+276 at the end of the huge tour and 2 turns): one
  decal per plot first seen, as section 2.5 says [L].
- The server in this build bakes t8 by its own rule (`decalAreaFromExe off` in every generation's log line), so its t8
  for tree tiles comes from the Tree_Shadow rectangles in the set, not from the EXE's bits [L, the log; whether that
  differs from the bits on tree tiles was not measured].
- Per plot: one Tree_Shadow decal per built record with one quad per drawn tree (section 2.5) is checked against the tap
  in 8.8 (job c78e: the decal watcher's copy of the image against the tap's built records).

### 8.6 Memory and frame time of the EXE [L]

**Memory, quiet series** (job `20261116-092032-9e22`, oldpc quiet, huge save, product path; warm-up fo, then fo / d33
ABBA x 3, one launch each, private bytes after the load and a 20 s settle, `memory.py`): Forest on **1,404.4 MB**
(sd 2.7, n 6) against D33 **1,375.1 MB** (sd 1.7, n 6): **+29.4 MB** paired (95 % interval +25.3 to +33.4, smallest
detectable difference 5.3 MB); largest free block 2,002.7 against 2,005.0 MB (-2.3, inside its spread); load to in game
133.9 s both. Job af13 (one run each, after the load / after the tour / after 2 AI turns): Tiny fo +21.3 / +23.2 /
+21.1 MB, ft +23.2 / +25.2 / +23.1; huge ft +26.3 / +29.3 / +33.1 MB against d33. Section 3's +17 to +38 MB is now
**+29 MB on the huge save at load** [L, grade B: quiet and interleaved, 6 per arm, but no metrics-ledger record].

**Frame time**: job 9e22's 45 s idle windows read exactly 60.0 fps in all 13 launches: the window was synced to the
display (WaitForVSync = 1 in the testbed instances, as 0095 found), so the series says nothing about the cost; it is run
again unsynced (job `20261116-120410-c2eb`, 8.7).

### 8.7 Frame time, unsynced [L]

Job `20261116-120410-c2eb` (2026-11-16 12:05-13:16, oldpc quiet: alone, foreign cores 0.151, undisturbed false as in
every oldpc quiet job of the day), the series of 9e22 with `WaitForVSync = 0` in the instance's GraphicsSettingsDX11.ini
(backed up and restored by hash with the standard files) and a refresh-cap refusal (60 Hz display): huge save, product
path, warm-up fo, then fo / d33 ABBA x 3, one launch each, a 45 s idle window in three blocks on the human's turn by the
Lua frame counter (`fps_idle.py --measure`, timing launch, no frame cap, window on screen, not in the foreground):

| | fo (Forest on) | d33 | fo - d33, paired (95 % interval) | smallest detectable (80 %) |
|---|---|---|---|---|
| idle frame rate | 246.0 fps (sd 4.2; 237.9-250.3) | 249.6 fps (sd 0.9; 248.7-250.7) | -3.7 fps (-8.3 to +1.0) | 6.2 fps |
| frame time (1000 / fps per launch) | 4.067 ms | 4.006 ms | **+0.061 ms (-0.017 to +0.139)** | 0.104 ms |
| private MB after the load | 1,434.1 (sd 2.1) | 1,408.2 (sd 2.2) | **+25.9 MB (+22.9 to +28.8)** | 3.9 MB |

**Frame time: no difference resolved** at the idle camera of the huge save's start (the start plot shows little
forest); the series can rule out more than about +0.14 ms per frame there. One fo launch (the last) idled at 237.9 fps
(its first block 223.9), the source of most of the spread. Not measured: a camera over dense forest, panning (the per-plot
builds and shadow decals of first sight, section 2.2), and the AI turn. Memory with the window unsynced: +25.9 MB (9e22:
+29.4 MB synced): Forest costs **26-29 MB** of the EXE's private bytes at the load of the huge save [L, grade B: quiet
and interleaved, 6 per arm, no metrics-ledger record].

### 8.8 The forest tap on the real EXE [L]

Job c78e (composite `5048311e`, `forest_tap=1` in the ft arms): the twelve patterns matched, no LOUD line, files written
per load and change (Tiny 6 generations, huge 21), each read back by `render/evidence/0420/forest_file.py`:

- **Layouts**: 20, each **180 trees** (VT's `forests.xml` 200 rounded to 90s), sizes 0.500-1.000 (median 0.74), no size 0,
  +0x0C = 1.0 in every tree, square points 1.6-94.5 inside the 96 x 96 square, offsets at most 0.96-0.97 from the plot (in
  HexToWorld's units: within a plot), the same in every generation of a load (the layouts never change after the start)
  [L, the 8.1 layout confirmed].
- **Records**: Tiny9v 180 forest and jungle plots (kinds 1/2/3/4 = 79/54/27/20), huge 690 (268/243/71/108) falling to 684
  over 2 AI turns (6 forests cut); the three cube coordinates always sum to 0; all 20 layouts in use [L].
- **The shadows match the tap exactly**: at every census the decal tap's Tree_Shadow count equals the tap's built records
  and their decal handles: Tiny 95 at the load, 166 after the tour; huge 96 at the load, 323 after the tour, 271 after 2
  AI turns (one decal per built record, section 2.5), each decal pass 1, priority 0.1; quads per decal 19-180, median
  131-156, i.e. one per drawn tree of the plot's 180 (the rest hidden by water, slope or the cut mask) [L].
- **Correction to 2.5**: the built count also falls (huge 323 after the tour to 271 after the turns, with the Tree_Shadow
  count following): a cut-stamp change over a plot (`0x715720` -> `0x5962A0`, routes and improvements of the AI turns)
  clears its built flag, `0x439E00` then removes the decal and builds it again only when the plot is in a view [C for the
  path, L for the counts]. The first file after a load lists every record as built with no decal handles on the huge
  save (690 / 0; Tiny 180 / 180), a state of the first frames after the load [L; not looked into].
- Cost: one poll (header, 20 layouts, the record list) per 500 ms took **0.080 ms mean, 0.290 ms max** over 385 polls
  on huge (the log line of each file; render thread, an indication: not a timing series); files 107 KB (Tiny) and
  123 KB (huge). The tap logs "not ready" once per load until StartupGame has made the layouts.

For route A (T1): read a file with `forest_file.read()`, take the record's layout and cube coordinates, add each tree's
+0x00 / +0x04 to the first two coordinates (the third is minus their sum), HexToWorld it, scale by +0x18 (0.75 x on kinds
3/4, section 2.3), hide by the rules of 2.3: the same trees as the EXE draws, tree for tree, in that session.

### 8.9 Could route B ship for the beta? (for the MC) [L, I where marked]

| | Route B (Forest on in the product's config.ini) | Evidence |
|---|---|---|
| Trees | drawn, as stock's (cards, density, colour, hills) | 8.4, ACCEPTANCE candidates on Tiny and huge |
| Fog of war on trees | dimmed through the server's mask (white handle) | 8.4, no stock picture at that plot |
| Tree shadows and t8 marks | arrive through the decal tap with no change; the EXE's tile+0x300 count equals stock's on huge (3,015-3,023 against 3,015-3,021) | 8.5, 8.8 |
| **Borders and the territory wash** | **covered by the EXE's trees** wherever they cross a forest or jungle (the server draws them under the composite; stock draws them over the trees) | 8.4, Tiny w1 and huge 94,39 (fail) |
| EXE memory | **+26 to +29 MB** private at load on huge (two quiet series of 6 + 6 interleaved) | 8.6, 8.7 |
| Frame time | +0.061 ms per frame at idle (95 % interval -0.017 to +0.139; 4.07 against 4.01 ms), not resolved | 8.7 |
| Load time | unchanged (133.9 s both, huge) | 8.6 |
| Stability | 0 LOUD lines, no crash box, 2 of 2 AI turns, server alive, in 2 + 3 Forest-on launches with turns and 13 + 13 idle launches | jobs af13, c78e, 9e22, c2eb |
| Later epics | breaks when cities and landmarks leave the EXE (their cut stamps go, section 4) [I] | section 4 |

What it would break for a beta player: every border line and every territory colour that runs through forest or jungle
disappears under the trees (on the late-game huge save most borders cross forest somewhere), and overlay highlights fall
under trees the same way [I for the highlights]. A fix inside route B would need the overlay drawn after the EXE's trees,
which the composite cannot do without drawing over every EXE model [I]. The decision is the MC's.

## 9. Route A: the design of epic 0431 (lead-0431, 2026-11-16 13:55 EST)

Branch `epic-0431-trees`, child tickets 4550-4555. Written before any code; every point marked [I] is a premise a
package checks first (EFFECTIVENESS E6) and corrects here in place.

### 9.1 Where the trees sit in the server's frame

Today's `Scene::draw` (`srv_render_scene.cpp`): clear -> terrain (`c5r::terrain`, overlay t1, fog t4-t6, shadow t3) ->
water (ocean, rivers, the ice at its hook, coast waves) -> the cloud planes (`FowScene::afterWorld`) -> the composite.
Route A adds two steps **after the water and the ice, before the cloud planes**, in the scene's own colour and depth:

1. **The screen overlay** [I, premise P2 of 4551]: stock's tree PS reads t3 = the "Screen Overlay" `[0xDA12A8]` (client
   size, RGBA8, 12 mips), filled by TerrainShader passes 2 / 5 / 7 (PS `0x067ea680`, which reads only the overlay;
   `overlay-system.md` 3, `asset-pipeline.md` 3.7). The server makes its own: the terrain tiles drawn again with the EXE's
   pass-2 programs (or 5 / 7 for the untessellated paths, as the terrain draw picks) into a server-owned target of the
   frame's size, with the same overlay pair (texels + OverlayProj of frame N-1, decision 2 (i)) the terrain binds. Its
   place, clear, depth state and mips are read from the 3907 stock captures before any code.
2. **The forest stage**: per visible forest / jungle plot two `DrawIndexedInstanced(2160, 1)` with the EXE's ForestShader
   (material 44, VS `0783e181` / PS `95325aff` by hash; VAs found by 4551), stock's states (2.4), t3 = step 1's target,
   the fog inputs the terrain binds (mask, cloud base, FOWAnim, TerrainFOW LUT), the terrain's fog ramp, the Terrain
   ColorKey LUT of the plot's style, once per wrap copy that shows the plot.

**Why this is "the overlay over the trees as stock"**: in stock the border lines and the territory wash reach a tree
pixel through t3 (the PS mixes it in by `overlay_alpha` 0.6 x its alpha, 2.4); the ground under the trees takes it
through terrain t1 as in the server today. Route B lost both because the EXE's trees came after the composited frame.
Whether stock also draws an overlay pass **after** the trees (0420's 8.4 says "the border is drawn after them" [I]) is
premise P2b of 4551: the draws after the last ForestShader draw in a stock capture are listed; if one writes overlay
pixels over the trees, it joins the stage after step 2.

Units: epic 0375 puts the units into the same frame. Trees and units both test and write depth; only alpha-blended edges
depend on their order, which is taken from the captures (4551 lists the material order of the model draws).

### 9.2 Code places

| Where | What | Package |
|---|---|---|
| `render/include/c5r/forest/placement.h`, `render/src/forest/placement.cpp` | `forests.xml` (rounded to 90), the four L8 masks, the 20 layouts (2.3, the EXE's rule with a CRT-compatible `rand()`, seeded per session like stock), layouts taken from a tap file instead (offline), plot records (kind 1-4, layout index, cube coordinates), per plot its two cb7 blocks with the hide rules (water 10.12, slope normal z < 0.9, cut mask, hills x 0.75) | 4550 (T1) |
| `render/include/c5r/forest/draw.h`, `render/src/forest/draw.cpp` | `Renderer`: init (ForestShader from `c5r::shaders`, the 90-card VB / IB per style and kind from `c5models`, the `_2` textures and overlays through `c5r::gamefs`), cb1 row by row (as the ice's `computeIceFrame`), `draw(ctx, rtv, dsv, FrameInputs, blocks)` | 4551 (T2) |
| `render/src/terrain/`, `render/include/c5r/terrain/terrain.h` | one added entry: the screen-overlay pass of 9.1 step 1 into a caller's target | 4551 (shared: asked of the MC) |
| `render/include/c5r/forest/shadows.h`, `render/src/forest/shadows.cpp` | Tree_Shadow quads per built plot (half-size 6.0, 3.6 on kinds 3/4) in the decal set's record form | 4552 (T3) |
| `render/include/c5r/forest/cut.h`, `render/src/forest/cut.cpp` | the cut-mask interface the placement reads (bits 0-2 at a world point), fed by the stand-in | 4550 (interface), 4553 (T4) |
| `render/tests/forest/` | the module's tests, plain and MSVC ASan | all |
| `server/src/render/srv_forest_scene.{h,cpp}` | `ForestScene` (pattern of `WaterScene` / `FowScene`): feed batches and the plot table, the placement, the renderer, the screen-overlay target, the stage hook, counters, the second / totals lines, `ForestFatalStep` | 4554 |
| `server/tests/render/forest/` | the scene's tests with fake sources | 4554 |
| shared, after 0375's merge: `srv_render_scene.{h,cpp}` (member, poll, feed, the draw hook after the water), `srv_render_host.cpp` (the fatal step, options), `srv_render_options.cpp` (`render_forest`), `server/build_server.py` (the new file); for T3 the decal set's hand-off (`srv_residency_source.*` / `c5r::decal`); for T4 `shell/composite/` (a cut-mask tap: new files plus their registration) | the wiring | asked of the MC |

### 9.3 What the server reads (the feed and the scene)

- **The plot table** (`SrvFeed::PlotRecord`, snapshot after a load, patched by events): `feature` (forest or jungle),
  `plotType` (hills -> kinds 3 / 4 [I, 6: the capture says hills]). How the EXE decides "forest" / "jungle" for its two
  listeners `0x5CB2B0` / `0x5E7EE0` (the feature's Type, its art define, or a landmark layout handler), whether records
  exist for unrevealed plots, and the sender of the kind 3 / 4 flag are premise P1 of 4550: checked against 0420's tap
  records (the cube coordinates and kinds of every record against the save's plot table) before the mapping is coded.
  The feature ids are resolved per snapshot through pure getters (`ICvFeatureInfo1`, `proxy/model/pure-methods.tsv`),
  never by a fixed number.
- **Events**: `kFeatureChanged` (a plot gains or loses its trees: record added with a new `rand() % 20` layout, or
  removed with its blocks and shadow decal), `kSnapshot` / `kOverflow` (rebuild from the table), `kPlotState` (a route or
  improvement change: the cut stand-in's change reaches the placement through 4553's source, not through the feed).
- **The height field** (the scene's `HeightGrid`): z of each tree, the water rule (z < 10.12), and the slope rule
  (the EXE's byte normal map `0x719BA0`, normal z < 0.9) [I, premise P3 of 4550: the normal the server can compute from
  the same field equals the EXE's byte normal at the tree; measured as drawn-tree counts per plot against the tap].
- **The style** per plot: the terrain's per-tile style byte (4305's rule) and the ColorKey index mapping
  {0->0, 1->3, 2->2, 3->4, 4->1} of 2.2 [C for the map, I for its meaning: 4551].
- **The camera block** (views, OverlayProj, wrap), **the overlay pair** and **the fog inputs** as the terrain draw has
  them; no new tap for the draw itself.

### 9.4 Residency and memory

Device objects, made at init: 10 VB / IB pairs (5 style entries x 2 kinds; 29,160 / 32,400 B VB, 4,320 B IB each,
about 0.35 MB), 20 textures (about 1.1 MB), one dynamic cb7 (1,440 B) and cb1, the screen-overlay target of the frame's
size (2554 x 1411 RGBA8: 14.4 MB, 19.2 MB with mips; whether the PS needs the mips is P2 of 4551). CPU: the 20 layouts
(20 x 180 x 28 B, about 100 KB), per forest plot one record and, once the plot has been in a view (as stock: 2.2,
8.8), its two cb7 blocks (2 x 1,440 B) and its shadow quads; about 3-4 KB per built plot, a few MB on a huge map.
Blocks are rebuilt only on a feature change of the plot, a new height field, or a cut change over the plot. No LOD
(stock has none, 2.4). The server's memory and frame time with and without the stage are measured in 4555 by
PERFORMANCE.md (testbed, COMMON 23).

### 9.5 Failure statuses (D118, D125, D132)

- **Install failure** -> LOUD once, `ForestScene::failed()`, the host's `ForestFatalStep`: `HostConfig::fatal` once, exit
  code 5 (the overlay's route): `forests.xml` missing or not parsable, a count outside 90..4096, a mask not L8
  128 x 128, a `.gr2` / `.dds` of the `_2` set missing or of another shape than 2.1, a ForestShader blob or the
  screen-overlay programs not in the EXE's section or with declarations other than checked, a device error at init.
  Never a picture without trees.
- **In play** -> LOUD (the first 20, then every 100th), counted, never fatal: a feature event for a plot outside the
  grid, a style byte outside 0..4 (drawn with the generic entry, as the EXE), a cb7 block with a value not finite (the
  plot is skipped that frame), a draw error (the frame stands without trees), the cut source stale or refused.
- `render_forest=0`: no stage (today's picture), for tests and comparison arms only.

### 9.6 Comparison method

- **4550 placement, offline, tree for tree**: seeded from 0420's tap files
  (`F:/c5-64bit-work/0420/oldpc-c78e/out/*/ft-*/forest_tap/`, read with `render/evidence/0420/forest_file.py`): the
  tapped layouts and records, the height field of the same save, the cut stand-in empty (or the EXE's grid once 4553 has
  it) -> per built plot the module's drawn-tree count and bounding rectangle against the Tree_Shadow decal of the same
  session (`forest_t0.json`'s census: triangles / 2 per decal and its rectangle; one quad per drawn tree, 8.8). Unit
  tests: the placement rule on a fixed `rand()` sequence, the mask texel rule, the rounding, the sort, the hide rules one
  by one, a negative for every refusal.
- **4551 draw, per draw**: from a stock capture with Forest on (3907's catalogue, Tiny t9), every ForestShader draw's
  inputs (VB, cb7, textures, camera) into the module: cb1 equal to the captured block byte for byte (named ulps aside),
  the draw's pixels against a replay of the captured draw with the capture's own buffers (exact, or within 1 per
  channel, named), then the whole stage of one frame seeded from the capture's cb7 blocks against the captured frame's
  tree pixels. The card choice (90 random cards per VB at start-up) is the capture's in this test and the module's own
  `rand()` in the product.
- **4552 shadows**: per built plot, the server's Tree_Shadow decal (quads, pass 1, priority 0.1) against the decal census
  of the same session; on the huge save the tile+0x300 count against 3,015-3,023 (8.5). *Done 2026-11-16: 12.3-12.4.*
- **4555 live**: product path with Forest still in the D33 list, Tiny and huge at 4309's cameras and 0420's forest
  cameras (Tiny w1 33,14 and 13,27; huge 94,39 and 76,41). A live picture is never tree for tree equal to another
  session (cards, layouts and layout indices are random per session in stock too): the exactness is proven by 4550 and
  4551 offline; live, the compare tool's numbers against stock (median, p95, max, difference image, COMMON 25) with the
  tree areas named as a random class, the borders and the wash over forest by eye and by the overlay pixels at the
  border cameras, the server's memory and frame time (PERFORMANCE.md, testbed), 30 AI turns with no crash and no D118
  report. ACCEPTANCE pictures only on the full shipped system (D112, D114, asset set named, D94).

### 9.7 Packages and waves

| Ticket | Package | Wave | Notes |
|---|---|---|---|
| 4550 | T1 placement (+ the cut interface) | 1 | offline; `render/src/forest/` only |
| 4551 | T2 draw + the screen-overlay pass | 1 | offline; needs the terrain files for step 1 (asked) |
| 4552 | T3 shadows and t8 marks | 2 | after 4550; needs the decal hand-off files (asked) |
| 4553 | T4 cut-mask stand-in: a read-only tap of the EXE's grid (`0x4365E0`) in the composite and its reader | 2 | self-contained: proposed for account B; premise P4: who owns the grid and whether it is stamped with Forest off under the product's D33 list (routes, landmarks, improvements) [I]; the model part (tree-cut surfaces) waits for the cities-and-landmarks epic |
| 4554 | the scene: `srv_forest_scene.*` and the wiring | 2 | the shared server files after 0375's merge |
| 4555 | T5 live acceptance | 3 | Tiny and huge, testbed for timing |

Estimate after this design: **42-72 h** (36-62 of section 5, plus 3-5 for the screen-overlay pass and 3-5 for the
scene wiring, which section 5 did not count) [I].

## 10. T1 as built (ticket 4550)

Worker W1 of epic 0431, 2026-11-16 14:02-14:40 EST, branch `task-4550-forest-placement`. Offline only (no game client).
Static reading: read-only headless Ghidra on a scratch copy of the `civ5` project (`render/evidence/0431/4550/gh.sh`,
passes g1-g4) and 0370's decompile text in `C:/c5-64bit-work/0370/out/`; the decompiler text stays in scratch, this
section states behaviour in own words (D83). Inputs: 0420's forest tap and census of job c78e
(`F:/c5-64bit-work/0420/oldpc-c78e/out/`), the height fields and plot tables of the same saves (10.1). Scripts and their
outputs in `render/evidence/0431/4550/` (`*.out.txt`).

### 10.1 Premise P1: which plots have a record, and the kind [L]

Each tap's first file (every record) against the plot table of the same save (`p1_records.py`, `p1_tiny.out.txt`,
`p1_huge.out.txt`):

| | records | forest / jungle plots in the table | kinds 1 / 2 / 3 / 4 | kind 3 / 4 = plotType hills |
|---|---|---|---|---|
| Tiny9v (`TB-TINY_0009V`) | 180 | 180 (106 feature 5, 74 feature 1), each with one record | 79 / 54 / 27 / 20 | 47 of 47, and no hill plot of kind 1 / 2 |
| huge (`TB-HUGE_0350`) | 690 | 690 (339 / 351), each with one record | 268 / 243 / 71 / 108 | 179 of 179, and none of 511 others |

- **The kind**: 1 / 3 stand on feature 5 (`FEATURE_FOREST` in these saves), 2 / 4 on feature 1 (`FEATURE_JUNGLE`); the
  kind 3 / 4 flag is exactly **plotType hills** on both saves (870 records). This answers section 6's first open point and
  2.2's "very likely hills" by data; the event's sender was not read. The scene maps the feature ids by their Type
  through the getters (design 9.3), not by these numbers.
- **The cube coordinates**: `hx = x - (y >> 1)`, `hy = y`, `hz = -(hx + hy)` for every record (camera-and-picking.md's
  ToHexFromGrid) [L].
- **Unrevealed plots: not answered.** Every plot of Tiny9v is revealed and visible; the huge save has 432 unrevealed
  plots, none of them forest or jungle. [I]: the DLL sends `GameplayFeatureChanged` from `CvPlot::setFeatureType`
  whatever the plot's revealed state (`CvPlot.cpp`), and the trees of an unrevealed plot are discarded by the fog mask
  (2.4), so a record per forest / jungle plot of the whole table is the safe choice for 4554. A tap of
  `C5H-0301-TINY_0009` (175 of its 180 forest / jungle plots unrevealed) would settle it.
- **The tables used**: huge: the scene library's `C5H-0301-HUGE_0350_20261116-042357_workstation` (save SHA-256
  c499d3c6..., the tapped save). Tiny9v: no bundle names that save; 4308's `F:/c5-64bit-work/0169/4308/bundle3` (Tiny 56 x
  36, every plot revealed and visible, the same 180 forest / jungle plots at the same places, no save hash in its
  scene.json) is taken as the same save [I].
- **Height fields are the same per save across sessions** to a few samples: huge 66 of 44,040,192 samples differ
  between two sessions' C5HF (at most 139 u16), Tiny 6-10 of 9,175,040 [L]; so a bundle's field stands in for the tap
  session's.

### 10.2 Premise P3: the slope rule's normal [C, L]

What the EXE does per tree (`0x6D7560`, disassembly) [C]: the normal starts as (0, 0, 1); when `[0xD8C584]` is not 0 it
comes from `0x719BA0` at the sample (trunc(w.x), trunc(w.y)) of `(hx, hy, hz, 1) x M_h2w` (x wraps on a wrapping map);
the tree is hidden when 0.9 > normal z. `0x719BA0` (this = GridHeightmap 0xD91270): when the pointer at +0x10030 is set,
a field of 2 bytes per sample in 64 x 64-sample tiles (`0x439D50`) decoded as x = (b0 - 128) / 255, y = (b1 - 128) / 255,
z = sqrt(1 - x^2 - y^2); else `0x5E1300`, a 5 x 5 edge-preserving gradient of the height field:

- the heights h / 255 of the 5 x 5 samples around the point (x wraps; a sample outside the grid is not read by the EXE:
  the module takes the centre's height [I]);
- two weighted sums, one per axis: weight = smoothing (0.0228, 0.2293, 0.4948, 0.2293, 0.0228) across the axis x
  derivative (0.0702, 0.3526, 0, -0.3526, -0.0702) along it x a range factor e^f, written as the cubic Taylor polynomial
  1 + f + f^2 / 2 + f^3 / 6 with f = -3.3333 x ((r - h) / 128)^2, where r = h0 + ((h(+1) - h(-1)) >> 1) / 255 along that
  axis (integer difference of the raw u16, arithmetic shift);
- gradient = -(sum of w h / sum of |w|) x sz per axis (about 1.166 x the true slope on a plane); normal = (-sy gx,
  -sx gy, sx sy) normalised.

Who fills the byte field was not found (no instruction with the operand 0x10030; `[0xD8C584]` is only read: `0x68F1B0`,
`0x49B5A0`, `0x770EE0`, `0x739AE0`, `0x6D7560`) [C].

**Measured by the drawn-tree counts** (`p3_normals.py`, `p3_normals_tiny.out.txt`, `p3_normals_huge.out.txt`; census
after the load, the tapped layouts and records, cut mask empty):

| normal from the C5HF | Tiny9v: plots equal / more / fewer (of 95) | huge (of 96) |
|---|---|---|
| the 5 x 5 filter above (the module's `slopeNormalZ`) | **46 / 49 / 0** | **42 / 54 / 0** |
| the same without the range factor | 25 / 57 / 13 | 18 / 59 / 19 |
| central difference (1 sample) | 19 / 74 / 2 | 12 / 80 / 4 |
| central difference, byte-quantised | 19 / 74 / 2 | 14 / 80 / 2 |
| no slope rule | 17 / 78 / 0 | 7 / 89 / 0 |

"Fewer" (the server hides a tree the EXE draws) is a wrong rule; "more" can be the cut mask (10.5). The filter is the
only candidate with no "fewer" plot and the most exact ones, and the exact plots of the five proof sets hold 750-1,460
slope-hidden trees each set (10.5): **the server's normal from the C5HF, with the EXE's filter, decides the slope rule as
the EXE does** [L by counts]. Whether these sessions read the byte field or ran the filter is not known; `[0xD8C584]` was
set (without it no tree would be slope-hidden) [I].

### 10.3 The placement rule: details and corrections to 2.3 [C, L]

- **The mask texel truncates** (x87 FISTP with the rounding control set to chop, `0x5A3D0B`) [C]: u = trunc(x x (1/96)
  x 127), v = trunc((1 - y x (1/96)) x 127). *Correction to 2.3 step 3, which says round.*
- **The EXE's copy of a mask is one byte later than the file**: the texel the rule reads at (u, v) is the file's texel at
  index v x 128 + u - 1. All 7,200 tapped trees (20 layouts x 180, Tiny9v and huge) stand on a set texel by this rule;
  by index v x 128 + u, 264 of them would stand on an empty one, every miss with a set texel one column to the left
  [L, `proof_*.out.txt` "masks ... accepted 3600"]. The load path that shifts it was not read [I]; u = v = 0 reads a byte
  before the texels (the module refuses it: the square's corner, outside every mask). Layout k uses mask k mod 4 [L: per
  layout the right mask has 0 misses, each other mask 3-58]. The masks are `TerrainTextures.fpk`'s `forest_tile1..4.dds`
  (L8 128 x 128, one level; 8,060 / 9,078 / 8,551 / 9,589 texels set) [L].
- **Draw order per tree** [C, `0x5A3BB0`]: size = (r / 32767 + 1) x 0.5; then per try x = (r / 32767) x 96, y = (r /
  32767) x 96 (x drawn first); a try fails when a placed tree lies closer than (its size + size) x space_between; after 20
  failed tries the 20th point is kept; the mask test comes after the tries, and a point on an empty texel draws a new
  size and new tries without counting.
- **Size 0 past 350**: every tree with index above 350 gets size 0 (`0x5A3DA7`; the earlier test "after 350 and 20 tries
  failed" is subsumed) [C]. *Correction to 2.3's "(After 350 trees, a tree whose 20 tries all failed gets size 0)"*: with
  the base game's 360 trees, 9 trees per layout are never drawn.
- **The offset**: the square point goes to the sample frame as M_h2w's origin + (x - 48, y - 48), then through M_w2h
  (`0xD90FF0`) to the plot-relative hex offset [C]; the module inverts M_h2w's 2 x 2 part instead: equal to the tap within
  4.8e-7 [L]. So the layout needs only the height field's `hexToWorld`.
- **The sort**: square y descending [L: 3,580 of 3,580 neighbour pairs]; 11 of Tiny9v's 20 layouts have two trees with the
  same y; the module keeps their order, the EXE's introsort may not [I] (it can only move a tied tree across the
  90-tree block boundary).
- **forests.xml** (`0x5DC3C0`) [C]: each count to float x float(1/90), half away from zero, x 90; a missing file shows the
  box "Could not load forest xml file" and leaves the counts as they were. VT's copy (`Assets/DLC/VT/forests.xml`) is what
  `c5r::gamefs` resolves in the test install (`%USERPROFILE%/Desktop/pure_vp/...`, the testbed's `pure_vp` base) [L, the
  install test's log line].
- **0.75 on kinds 3 / 4**: in `0x6D7560` itself (kind 3 or 4 at `0x6D75A8` -> the factor `[0xA0A4AC]` = 0.75) [C].
  *Answers 2.3's "where the code applies the 0.75 was not traced".*
- **Per tree in `0x6D7560`** [C]: the cb7 entry is HexToWorld of (cube + offset): x = w.x sx - ox, y = sy w.y - oy,
  z = the height at (trunc((ox + x) / sx), trunc((oy + y) / sy)); scale = size x factor, or 0 when the cut mask
  (`0x4365E0` at the world x, y, bits & 7), the water (0.2f x 50.583656f > z) or the slope (10.2) hides the tree. The cut
  cell: col = int(ox + x) >> 1, row = int((oy + y) x 1.1547344) >> 1, 16 x 16-cell blocks (`0x4A0A50`), 0 outside the grid
  (`c5r/forest/cut.h`).
- **The shadow decal's frame**: its quads and rectangle are in the sample frame (M_h2w's output before x sx - ox), half
  size 6 x (0.6 on kinds 3 / 4) in those units [C `0x770EE0`, L: every fully drawn plot's rectangle equal to the census
  within 0.0005]; the census clamps rectangles to the sample box (x at most samplesX - 1, one Tiny plot at the seam).
- **CreateTerrain's own stamp**: `0x709B50` stamps a list of polygons (built by `0x467C60`, not read) into the cut mask
  with a per-polygon bit byte (call `0x70A0D1`) [C]; what they cover is [I] (10.5: shores and rivers).

### 10.4 The module as built [C, tests]

`render/include/c5r/forest/placement.h`, `render/src/forest/placement.cpp`, `render/include/c5r/forest/cut.h`,
`render/src/forest/cut.cpp` (in `c5render`, pure CPU, no D3D):

- `parseForestsXml`, `roundTo90`, `loadInstall(GameFiles)` (forests.xml + `forest_tile1..N.dds`, the xml's origin
  returned), `parseTileMask` (L8 128 x 128 only), `maskAccepts` (10.3), `CrtRand` (MSVC's rand), `buildLayout` /
  `buildLayouts` (10.3, seed from the caller), `squareToOffset`, `sortLayout`, `checkLayouts` (tapped layouts),
  `PlotRecord` / `makeRecord` / `recordFromCube` / `kindOf(forest, jungle, hills)`, `slopeNormalZ` (10.2), `buildPlot`
  (per plot: `blocks` x 90 `Instance` = cb7, `HideCounts`, the drawn trees with sample-frame and world centres and scale,
  the shadow half-size and rectangle for 4552), and `Placement` (the session: init from the install or from tapped
  layouts; `setPlot(PlotInput)` with plain x, y, forest, jungle, hills, so 4554 feeds it from `SrvFeed::PlotRecord`
  without the render library seeing server headers; layout `rand() % 20` from the same stream as stock; blocks built on
  first use and rebuilt when the cut source's generation moves or on `invalidate`).
- `cut.h`: `CutSource` (bits at a world point, a generation), `EmptyCut` (what 4550 ships), `DenseCut` (a cell grid, set
  whole or from the EXE's 16 x 16 block form: directory + pool, as 4553's reader will copy it) and `cutCell` (the EXE's
  cell rule).
- Statuses: every refusal returns `BadLayout` / `Unsupported` / `NotFound` / the DDS parser's code and leaves the output
  empty or the object unchanged; refused: a missing file (named), no `<Forest>`, a missing or non-numeric attribute,
  num_tiles 0 or above 64, a rounded random count outside 90..4096, a gridded count above 0 (`Unsupported`: its placement
  was not read), a negative spacing, a mask not L8, not 128 x 128 or empty, a layout that does not fill within 100,000 x N
  draws, a matrix not finite or not invertible, tapped layouts of the wrong count or with values not finite, a record of
  kind 0 or with cube coordinates not summing to 0, a plot both forest and jungle or outside 0..65535, a layout index past
  the count, a value not finite in a built block.
- Tests (`render/tests/forest/placement_test.cpp`, 28 cases): the rand sequence, the rounding, forests.xml (VT and base,
  12 refusals), the masks (5 refusals) and the texel rule with its shift, the rule's draw order on a fixed rand sequence
  (every tree recomputed), the 20 tries (exact stream position), the distance, the mask bound, size 0 past 350, the sort,
  the layout and tapped-layout refusals, the records, positions and blocks, each hide rule (water at 12,896 / 12,900 u16,
  slope, cut bits 0-2 against bit 3, size 0), the slope normal against the plane formula, the Placement state and its
  refusals, the cut cell rule and the block form, and the install (VT's forests.xml, the 4 masks, 20 layouts every tree on
  its mask). 2026-11-16: `python render/build.py --test -R c5r_forest` (clang-cl /W4 /WX) "100% tests passed, 0 tests
  failed out of 1 ... tests passed, 0 skipped"; under MSVC `/fsanitize=address` (`--asan`, through `quietlock.py run`):
  canary reported, "100% tests passed, 0 tests failed out of 1 ... tests passed, 0 skipped, 0 ASan reports" (peak 212 MB).
- Proof mode of the same executable: `c5r_forest_placement.exe --proof IN OUT [--game DIR]` (10.5).

### 10.5 The offline proof, tree for tree [L]

`run_proof.sh` / `proof_4550.py` (outputs `proof_tiny_*.out.txt`, `proof_huge_*.out.txt`), 2026-11-16 14:33: per census
of job c78e, the tap file whose built records equal the census's Tree_Shadow count (the same moment), its layouts and
built records through the module (`--proof`, cut mask empty), each decal paired with the built record nearest its
rectangle's centre (all paired), the module's drawn count and rectangle against the decal's quads (triangles / 2) and
rectangle.

| set | tap file | plots | exact (count, rectangle within 0.01) | module draws more | fewer | rect only | trees in the exact plots: drawn / slope-hidden / water-hidden |
|---|---|---|---|---|---|---|---|
| Tiny9v after the load | gen 2 | 95 | **46** | 49 | 0 | 0 | 7,501 / 750 / 29 |
| Tiny9v after the tour | gen 6 | 166 | **54** | 112 | 0 | 0 | 8,846 / 826 / 48 |
| huge after the load | gen 4 | 96 | **42** | 54 | 0 | 0 | 6,465 / 1,084 / 11 |
| huge after the tour | gen 10 | 323 | **58** | 265 | 0 | 0 | 8,988 / 1,441 / 11 |
| huge after 2 AI turns | gen 21 | 271 | **58** | 213 | 0 | 0 | 8,969 / 1,460 / 11 |

- **No plot where the module hides a tree the EXE draws, and none where the counts agree but the rectangle does not**:
  positions, sizes, the water rule and the slope rule are the EXE's [L].
- **Every mismatch is the module drawing more trees** (1 to 53 per plot): a hide the module does not model, which is the
  cut mask (empty here until 4553). What the plot table shows on or next to those plots (huge after the tour, 265): a
  river 99, a route / improvement / wonder / city on the plot 93 or next to it 26, the coast 26, a resource 16, nothing 5;
  Tiny9v after the load (49): the coast 31, a river 14, a resource 2, nothing 2. Routes and improvements are expected
  (2.3). The coast and the rivers are not in 2.3's list: on Tiny9v 34 of the 41 plots with 1-10 extra trees touch water
  against 13 of the 46 exact plots, which fits CreateTerrain's own stamp (10.3) or the river decals' [I]. They are 4553's:
  its stand-in reads the EXE's whole grid, whatever stamped it. *Settled 2026-11-16 by 4553 (14.6): with the tapped grid every built plot is exact; the stamps are the river mesh's (0x09), CreateTerrain's replay and the landmark, farm and city paths (14.1, 14.5) [L].*
- The huge height field and plot table are from another session of the same save (10.1); after the 2 AI turns the tap's
  records lost 6 forests and the table still has them (they are not built, so not compared).

### 10.6 Not verified, open

- Records for unrevealed plots (10.1); who fills the byte normal field and whether these sessions used it (10.2); the
  EXE's order of trees with equal square y (10.3); the mask copy's one-byte shift's cause (10.3).
- Gridded trees (`gridded_trees_per_tile` above 0): refused, not implemented.
- The proof compares counts and rectangles, not single tree positions of hidden trees (the census has no per-quad data);
  cb7 values against a stock capture's blocks are 4551's (9.6).
- Old places for the lead to correct at the merge: 2.3 step 3 ("round" -> trunc, and the one-byte shift), 2.3's "after
  350" sentence, 2.3's "where the code applies the 0.75 was not traced", 2.2 / 6's "very likely hills" (now [L] by the
  plot table), section 0 item 5's "(3.6 for the plot kinds 3 and 4, the hills [I])" ([L]).

## 11. T2 as built (ticket 4551)

Worker W2 of epic 0431, branch `task-4551-forest-draw` (from `epic-0431-trees`), 2026-11-16 from 14:02 EST. Offline only
(D137): the 3907 stock catalogue exports on F: (`F:/c5-64bit-work/0301/3907/exports/`, Tiny t9, stock config.ini, VT
present, 2554 x 1411, i0ship 26234371e; every forest plot of these captures has terrain style 2, Asia) and the RenderDoc XML
of the same captures (`.../exports/xml/`, `.../3907/xml/`). Static reading: 0370's read-only decompiler output of
`0x63A7A0`, `0x6C1CD0`, `0x439E00` and `0x780D80` in its scratch (`C:/c5-64bit-work/0370/out/p1.txt`, `p2.txt`; not
committed, D83); this section states behaviour in own words. Scripts and outputs: `render/evidence/0431/4551/`.

### 11.1 Premises checked before the code (E6), 2026-11-16 14:03-14:17 EST (the TerrainTile and HS lines 14:40)

**P2: what writes the Screen Overlay `[0xDA12A8]` (the tree PS's t3)** [C capture, 9 exports: cities c_ancient /
c_modern, p4 shown / hidden / shown2a, units_land L01 / L09, units_sea S01 / S04; `p2_overlay_target.py`,
`p2_overlay_target.out.txt`; the XML's chunks are in execution order, checked against draws.json's draw order, equal in
all 9]:

- The resource: Texture2D 2554 x 1411 (the back buffer's size), `R8G8B8A8_TYPELESS`, **12 mips**, bind SRV | RTV,
  `MISC_GENERATE_MIPS`; the tree PS's view `R8G8B8A8_UNORM` over all 12 mips.
- Per frame, in this order, in all 9: `ClearRenderTargetView(t3, (0, 0, 0, 0))`; then **one draw per terrain tile of the
  scene** with the TerrainShader **pass-2 programs** (VS `0x067d6490` = the scene's pass-0 VS, HS `0x067f9cb8`, DS
  `0x067fa080`, PS `0x067ea680`; VAs by SHA-256 of the captured blobs against the EXE's `.shader` section,
  `shader_vas.py`); then `GenerateMips(t3)`. L01 112 draws, L09 129, c_ancient 143, c_modern 143, p4 43, S01 74, S04 80:
  in every capture exactly as many as the scene's terrain draws (pass 0/1 into the scene target), the same tiles in the
  same order, with the same TerrainFrame (cb1) values in every stored byte (L01: only never-stored bytes differ; the
  other captures' blocks are bit-identical) [C].
- **TerrainTile (VS / HS / DS cb0) of each pass-2 draw against the scene draw of the same tile** [C, 767 pairs of 8
  exports, `p2_overlay_tiles.py`, `p2_overlay_tiles.out.txt`]: TileWorld and TileOffset equal, **LodStep doubled**
  (2 -> 4, 1 -> 2), LodMip equal (the same height mip), **InvGridSize 0** (no cache uv). *Corrected 14:40: the first
  text of this line said "the same TerrainTile cb2"; cb2 is DistanceFog, equal in every draw.*
- Each pass-2 draw takes the **next coarser patch list** of the tile index buffer: index count one quarter of the scene
  draw's, at the range after it (L01 scene 16,384 indices at 65,536, overlay 4,096 at 81,920; c_ancient 4,096 at 81,920
  / 1,024 at 86,016; p4 65,536 at 0 / 16,384 at 65,536) [C]: with the doubled LodStep the pass covers the whole tile at
  half the patch density (level L + 1 of `terrain-surface.md` 7's rule, the height read at the scene's mip L).
- The pass-2 HS `0x067f9cb8` differs from the scene's `0x067f98f0` only in reading the tessellation factor from
  TerrainFrame row 15 (`TessFactorOne`, 1.0) instead of row 14 (`TessFactor`, 2.0 in these captures) [C disassembly
  diff, scratch; `asset-pipeline.md` TerrainFrame 0x0F0]: one quad per patch, the bicubic surface evaluated at the patch
  corners only.
- State: no depth target bound (depth off), blend off (ONE / ZERO, write all), cull NONE, solid, front CCW, depth clip on,
  the full 2554 x 1411 viewport [C, writer draw 273 of L01 and the first writer of each export]. With no depth test,
  where two tiles overlap on screen the later tile wins.
- PS `0x067ea680` (404 B) returns t1 (the Terrain Overlay `[0xDA12A4]`, 2048^2, sampler s1) at the DS's TEXCOORD1.xy, the
  overlay window uv (OverlayProj) [C disassembly, scratch]. So the Screen Overlay is the Terrain Overlay **projected onto
  the displaced terrain surface in screen space**, premultiplied like its source.
- Place in the frame: after the scene's terrain passes, the water and the ice, before the coast waves (`m92p0`) and the
  model draws; it is written nowhere else in the frame [C, 9 of 9].
- **Mips and the tree PS** [C disassembly]: the PS samples t3 with `sample` (s3, MIN_MAG_MIP_LINEAR wrap) at
  `SV_Position.xy / cb1[9].xy`, one texel per pixel, so the selected LOD is 0; the generated mips are not expected to
  reach a tree pixel [I until 11.5's replay with a one-mip t3 measures it].

**P2b: draws after the last ForestShader draw** [C capture, same 9]: the ForestShader draws are contiguous in every frame
(L01 394-439, L09 422-467, c_ancient 671-826, c_modern 691-846, p4 181-268, S01 243-264, S04 277-302). After them come
only: single buildings and animated building parts (VS `7ec4e9a5` / `7a9e17f6`, PS `f0e1f0a6`), the units (`29a0b061` /
`60d270e1`, `5def4f99`, `82e59838`, `da2382af`), the city draws `ec8fba0f` / `70ed01e8`, FOW CULL (`m52p3`), the cloud
planes (`m53p0`), the FOW particles (`m51p0`) and, in the p4 captures, depth-less 2D draws (GridShader `m58p0` nine-slice
panels and the `3b190b25` / `7728d7a8` families: the EXE's in-world UI). **No TerrainShader, overlay or border pass runs
after the trees**: stock's borders and territory wash reach a tree pixel only through t3 (and the ground under the trees
through terrain t1). *Correction of section 8.4 ("the border is drawn after them") and 9.1's open point: no pass after
the trees draws borders; in stock the border line is in t3, so the trees carry it at `overlay_alpha` x its alpha.*

**The order of the ForestShader draws against the other model draws** [C, 9 of 9]: scene terrain -> water, ice -> the
Screen Overlay pass -> coast waves -> city-block colour draws (`7ec4e9a5` / `c704c80b`) and one `c4bb25da` / `f0e1f0a6`
draw -> **all ForestShader draws** -> single buildings, animated parts, units, the `ec8fba0f` city draws -> FOW CULL,
cloud planes, particles -> UI. So in the server the trees go after the terrain, water and ice and before the units
(epic 0375) and the cloud planes; only alpha-blended edges depend on that order.

**ForestShader in the EXE's shader section** [C, `shader_vas.py` on the owner's Steam EXE, read only]: **VS `0x0676C6A0`
(1,100 B), PS `0x0676D388` (2,264 B)**, SHA-256 `0783e181...` / `95325aff...` equal to the captured blobs; both already
in `create_sites_table.inc` (material 50 sites).

**cb1 of the tree draws (240 B = 15 rows), from the render setup `0x63A7A0`** [C decompile for the stores, L for the
values in the captured blocks]: per view v of the frame (v = 0 .. `[0xD90FA4]` - 1), with the "Forest" marker, one block:

| Row | Stored | Source | Read by |
|---|---|---|---|
| 0-3 | all | `D3DXMatrixMultiply(g_View[v] 0xDA1310 + 0x40v, g_Proj 0xDA12D0)` (the reflection variant 0xDA1990 / 0xDA1A90 is never used for trees) | VS (clip) |
| 4-7 | all | OverlayProj[v] `0xDA1690 + 0x40v` | VS (FOW mask uv `(x, y) * (0.5, -0.5) + (-0.5, -0.5)` of the projected world point) |
| 8 | all | FOWAnim `0xDA1C40..4C` | VS (cloud uv `(world.xy + r8.zw) * r8.xy`) |
| 9 | x, y | the target size: the ints `[0x301056C]` / `[0x3010568]` as unsigned floats, or the given target's +0x28C / +0x290 | PS (t3 uv) |
| 10 | x | `overlay_alpha` (Forest+0x134, `forests.xml`, 0.6) | PS |
| 11 | x, y, z | `0xD8BD0C..14` = the shadow direction (0.4330127, -0.25, 0.8660254) (`terrain-bake-passes.md` AuxDir) | none |
| 12 | x, y, z | the camera position of view v, `0xDA1440 + 0x40v` | PS (distance fog) |
| 13 | all | DistanceFog colour `0xDA1C50..5C` | PS |
| 14 | all | (-density `0xDA1C60`, exp(-(density x cam z)) x (-1 / density) x scale `0xDA1C64` in x87 precision, 0, `0xDA1C6C`) | PS (x, y, w) |

Rows 9.zw, 10.yzw, 11.w and 12.w are never stored (the captured blocks hold stale bytes there). With two views (the wrap
captures cities/* and p4/*) the frame has two blocks; view 1's rows 0-3 are view 0's with the world shifted by the wrap
period (row 3 x moves by 3583 x row 0 x) and row 12 x by -3583, and **the same blocks are drawn in both views** (c_ancient
78 cb7 blocks x 2 views = 156 draws) [L]. Then for every view the frame's whole draw list (the blocks that `0x439E00` filled
for plots visible in any view) is drawn.

**The draw objects (`0x6C1CD0`)** [C]: style entry e = 0..4 (the terrain tile's style byte, `0x439E00` reads tile +0x164,
the same byte the terrain's `[0xD90E4C + 4 style]` uses, style order Generic, America, Asia, Africa, Europe) takes the
models `forest_` / `jungle_` + {europe, america, asia, africa, europe}`_2.gr2` (`0x780D80`): so the Generic style draws
the Europe cards. For each entry and each card slot k = 0..89 the EXE calls `rand()` twice, **first the forest card
(rand() % forest meshes), then the jungle card (rand() % jungle meshes)**, 900 calls in all. Each VB holds the cards one
after the other, 36-byte vertices: position, normal, uv (the mesh vertex's 32 bytes) and BLENDINDICES.x = k; the other
three bytes of the 36 are not written and the VB is sized (largest card's vertex count x 90) vertices, so the tail and
those bytes hold stale bytes [L: the captured jungle VB is 900 vertices with 818 used, the tail non-zero]. The IB holds
each card's indices plus its first vertex (16-bit), sized (largest index count x 90); both buffers immutable; the draw is
`DrawIndexedInstanced(indices of the 90 cards, 1)` (2,160 in every capture). The texture slots of the draw object: t0 the
`<kind>_<style>_2.dds`, t1 `<kind>_overlay_<style>_2.dds`, t2 `[0xDA12A4]` (bound, not declared by the PS), t3
`[0xDA12A8]`, t4 `[0xDA12B4]` (fog ramp, sampler 5), **t5 `[0xD90E4C + 4 i]` and t8 `[0xD90E60 + 4 i]` with i =
{0, 3, 2, 4, 1}[e]** (samplers 5), t6 `[0xDA12AC]` FOW mask, t7 `[0xDA12B0]` cloud base. *Answer to 6's open point (the
ColorKey mapping): i is not the style; an America tile's trees take the Africa LUTs, Africa's the Europe LUTs, Europe's
the America LUTs, Generic and Asia their own [C]. In the installs measured so far the five Terrain volumes are one volume
and the five TerrainFOW volumes another (`asset-pipeline.md` 3.5, `fog-of-war.md` 2.7), so the permutation changes no
pixel there [L for those installs]; the module keeps it.*

**The PS, in order** [C disassembly; extends 2.4]: t0 at the card uv, discard if alpha < 0.1; t6 at the mask uv, discard
if mask.x = 0; t1 at world xy x 0.001, its UV (BT.601 YUV) replaces the tree's by t1's alpha, luma kept; t5 at that
colour (3D LUT); `colour x (1 - overlay_alpha x t3.a) + t3.rgb x overlay_alpha` (premultiplied overlay at the pixel);
lerp from t7 (at the cloud uv) to the colour by mask.x; lerp toward t8(colour) by mask.x (1 - mask.y); distance fog from
t4 at (screen y 0..1, r14.w) and the exponential of the squared distance from the **tree's base point** (cb7 xyz, one fog
value per tree) to r12 (r14.x, r14.y); alpha = t0's alpha.

### 11.2 The Screen Overlay pass for the server (design, and built in c5r::terrain)

From 11.1. The MC extended the epic's touches to `render/src/terrain/` and `terrain.h` on 2026-11-16 14:03 (#2736, the
epic's log on main), so it is **built**: `c5r::terrain::Renderer::drawScreenOverlay(ctx, rtv, cam, o, stats)`
(`terrain.h`; `draw()` and it share one body, `drawPass`, which differs only where the pass is tested; `draw()` is
unchanged). Everything below is stock's behaviour [C, 11.1] unless marked.

- **Entry**: `drawScreenOverlay(ctx, rtv, cam, const Options& o, stats)`, called by the scene right after its terrain, water and ice and before the forest stage (9.1), with the
  **same camera, field, LOD state and `Options::overlay`** (the overlay pair of frame N-1 and OverlayProj per wrap copy,
  decision 2 (i)) as that frame's terrain draw. It does not advance the LOD state (the scene draw did).
- **Tiles**: exactly the tiles the frame's terrain draw drew, in the same order, each wrap copy with its own OverlayProj;
  every tile with the EXE's pass-2 programs (VS `0x067d6490`, HS `0x067f9cb8`, DS `0x067fa080`, PS `0x067ea680`, already
  created by `init` as the module's pass 2), whatever its colour source (the PS reads only t1: a tile without a cache is
  drawn too; the water placeholder plane is not).
- **Constants**: TerrainFrame as the scene draw's (TessFactorOne row 15 = 1.0 is what the pass-2 HS reads); TerrainTile
  as the scene draw's except **LodStep x 2** and **InvGridSize 0**; LodMip unchanged; DistanceFog as bound (not read).
- **Patch list**: the module's list of level L + 1 (`ib[L + 1]`, n/2 patches a side). At L = 4 (the rule's clamp) the
  next list would be level 5, which the module does not build [I: stock's index buffer holds 5 levels (0..4) in the
  captures' 174,592-byte IB = 87,296 indices = 16 x (4096 + 1024 + 256 + 64 + 16); a level-4 scene would need a level-5
  list of 1 patch or a clamp; not seen in any capture: decide with a live check or keep L + 1 clamped to 4 and name it].
- **Target**: the frame's size (the scene's colour target), `R8G8B8A8_TYPELESS` with RTV / SRV `R8G8B8A8_UNORM`.
  Stock's has 12 mips and `GenerateMips` after the pass; the tree PS never reaches them (11.4: a one-mip t3 gives
  equal tree pixels in two captures whose t3 holds borders), so the server may keep **one mip** (14.4 MB at 2554 x 1411
  instead of 19.2, no GenerateMips) [L for the two captures; I in general: the PS's derivative gives LOD 0 at every
  pixel by construction]. If equality with stock's resource is wanted, 12 mips and GenerateMips cost 4.8 MB and one call.
- **State**: `ClearRenderTargetView(t3, (0, 0, 0, 0))` first; no depth target (depth off), blend off (write all), cull
  NONE, the full viewport; later tiles overwrite earlier ones where they overlap on screen (no depth test), as in stock.
- **Then** the forest stage binds the target's SRV as `FrameInputs::screenOverlay` (t3) and the scene's own colour and
  depth targets as rtv / dsv. The scene keeps the overlay target as a member (made at its init and at a resize; its
  failure is an install / device failure of the forest stage, `ForestFatalStep`, 9.5).
- **Tests** (`render/tests/forest/draw_test.cpp`, 2 of its 15): a flat 2 x 2 field, a camera looking down, an overlay of
  one colour: at the EXE rule's level (4 here), level 1 and level 4 fixed, the pass draws that colour on exactly the
  pixels where the scene draw draws land (13,608 of 13,608, none elsewhere, no other value), the same tiles and culls as
  the scene draw, LodStep twice the scene's below the clamp, LodMip equal, the LOD state unchanged, no depth target and
  depth off, blend off; without an overlay the target stays 0; refusals before init, before a field, a BGRA, float or
  smaller target, a null context or target, a NaN overlay matrix or camera, lodLevel 7, none changing the LOD state.
  **Not done**: a comparison with a captured Screen Overlay, which needs the 3907 save's field and the EXE's tile
  caches (C5H-0301-CAT_0009); no reference set holds them today [I: the terrain's ground truth is another save]. The
  coarser surface (tess factor 1, half the patches) is the EXE's by construction of the same programs and constants.

### 11.3 The module as built (2026-11-16)

`render/include/c5r/forest/draw.h`, `render/src/forest/draw.cpp` (`c5r::forest`), in `c5render` through the forest
folder's glob; no placement header included (the cb7 blocks are plain `Block {entry, kind, trees[360]}`).

- `Renderer::init(dev, ShaderSection, GameFiles, InitOptions{vsVa, psVa, randSeed})`: finds the two blobs by VA,
  checks stage, the VS input (POSITION, NORMAL, TEXCOORD0 float + BLENDINDICES uint), VS cb1 <= 15 rows and cb7 = 90
  rows and nothing else, PS cb1 = 15 rows alone, PS resources exactly t0, t1, t3, t4, t6, t7 (2D) and t5, t8 (3D),
  samplers within s0-s8; creates the input layout, cb1 (240 B) and cb7 (1,440 B) dynamic buffers, stock's states and
  nine samplers; loads the 16 `_2` textures once per file (entries 0 and 4 share Europe's) as plain 2D textures in their
  own DDS format and mips; reads the 8 `.gr2` models with c5models (`cardsFromGr2`: every mesh in file order, Real32
  Position / Normal and the first two of TextureCoordinates0's three floats); draws the EXE's 900 `rand()` calls with a
  caller-seeded `CrtRand` (MSVC LCG) in the EXE's order (`chooseCards`) and builds the 10 immutable VB / IB pairs
  (`buildCardBuffers`: VB = largest card x 90 vertices, IB = largest index count x 90, used part as the EXE's, tails 0).
- `replaceCards(entry, kind, vb, ib, indexCount)`: a capture's own buffers, checked (whole vertices / indices, index
  count, every used index inside the VB, slot < 90).
- `computeForestFrame(in, view, out)`: cb1 by the store map of 11.1; `forestFrameStored(off)` the mask.
- `draw(ctx, rtv, dsv, FrameInputs, blocks, count, stats)`: every check first (t3, t4, t6, t7 and per drawn entry
  the t5 / t8 LUT of `lutIndex(entry)`, block entry, kind and finite values, every view's cb1), then per view one cb1
  and every block with its entry's buffers and textures, `DrawIndexedInstanced(indexCount, 1)`.
- Failure route: every missing input is a status naming it (D118 / D125); nothing is drawn after a failed check.

### 11.4 The proof (2026-11-16, hardware device of the workstation, the 3907 exports, the `pure_vp` install copy with VT)

Test `render/tests/forest/draw_test.cpp` (15 tests, 13 of the forest module and 2 of the Screen Overlay pass; inputs listed by `render/evidence/0431/4551/manifest.txt`, made
by `make_manifest.py`; numbers in `forest_perdraw_L01.csv`, `forest_perdraw_two_views.csv`, `draw_test_*.out.txt`):

- **Programs**: the EXE's blobs at `0x0676C6A0` / `0x0676D388` equal the captured ones byte for byte.
- **Textures**: the module's `forest_asia_2.dds`, `forest_overlay_asia_2.dds`, `jungle_asia_2.dds`,
  `jungle_overlay_asia_2.dds` read back equal the captures' t0 / t1 in every byte of every mip, same view formats
  (BC2_UNORM) and mip counts [L].
- **Cards**: every one of the 90 slots of the captured forest VB (810 vertices) and jungle VB (818 of 900) is one of the
  module's 12 / 17 cards from `forest_asia_2.gr2` / `jungle_asia_2.gr2`, vertex for vertex in the card's order;
  `buildCardBuffers` with the slots' cards equals the captured VB in the 33 bytes the draw reads of every used vertex and
  the IB in all 2,160 indices [L]. The VB tail (82 jungle vertices) and the three pad bytes of each vertex hold stale
  bytes in stock, zeros in the module (never read).
- **cb1**: computed from each captured block's own inputs (view = rows 0-3 with proj identity, the camera = row 12, ...)
  equal to **all 6 captured blocks (L01, S04 one view; c_ancient, p4_shown_all two views) in every stored byte,
  row 14.y included (0 ulp)** [L]; the 28 unstored bytes are 0 in the module's block.
- **Per draw**: every ForestShader draw of L01 (46), p4_shown_all (88) and c_ancient (156), 290 draws, drawn by the
  module (EXE programs, own t0 / t1, computed cb1, the capture's VB / IB, cb7 and t2-t8) and by a plain replay of the
  captured draw (captured blobs, cb1 bytes, textures and the recorded states), each into a cleared 2554 x 1411 target:
  **difference 0 in every pixel of every draw** (median 0, p95 0, max 0; L01 107,850, p4 369,788, c_ancient 82,912 drawn
  pixels; draws with pixels on screen: 12 / 14 / 21, the rest stand outside the window by the EXE's 64-unit plot margin).
- **The whole forest stage** of each frame (all draws in the frame's order, both views, into one target): **difference 0**
  (drawn L01 89,669, p4 316,227, c_ancient 77,063 pixels; difference images all black, `C5R_WORK`, not committed).
- **t3's mips** [L]: the whole stage drawn with a one-mip t3 (mip 0 of the capture's target, no mips) against t3 with its
  generated mips: **0 of 316,227 (p4_shown_all) and 0 of 77,063 (c_ancient) tree pixels differ**; in these two captures
  t3 holds borders and territory (476,065 / 532,925 non-zero texels), and the stage with t3 = 0 differs from the captured
  t3's in 11,164 / 4,196 tree pixels (max 103 / 95 per channel): t3 reaches the trees, its mips do not (L01's t3 is empty).
- **Suites** at commit `1a0f04e2` (the code; later commits are text), through `quietlock.py run`: plain `python
  render/build.py --test` **61 of 61 passed, 0 skipped** (c5r_forest_draw 15 of 15, c5r_terrain_test and
  c5r_terrain_fow_test passed with `draw()` on the shared body); MSVC ASan `--asan --test` **61 of 61 passed, 0
  skipped, 0 ASan reports**, canary reported (`render/evidence/0431/4551/suites.out.txt`), 2026-11-16 14:48-15:05.

### 11.5 Not verified, open

- The card choice's `rand()` order is the EXE's by the static reading (forest then jungle per slot, entries 0..4); no
  capture shows a known `rand()` state, so the choice itself is checked only by its structure and by every captured slot
  being one of the cards [I for the sequence].
- The module's mesh order = the `.gr2` file's mesh order; the EXE's order of `0x601C90` was not read. It changes which
  card a given `rand()` value picks, not the set [I].
- Styles other than Asia (entry 2): no capture; the LUT permutation of 11.1 is from the static reading only [C, not L].
- MSAA: stock draws the trees straight into the swap chain's back buffer (res 2419, debug name "Swap Chain Backbuffer",
  its RTV recorded as `TEXTURE2DMS`); the replays here draw into a single-sample target on both sides. The server's scene target decides; the module draws into whatever `rtv` / `dsv` it is given.
- The Screen Overlay pass (11.2) is built and tested on synthetic fields only; its pixels against a captured Screen
  Overlay are not compared (no field of the 3907 save). The scene (4554) owns the target, the call order (terrain,
  water, ice, `drawScreenOverlay`, trees) and the failure route.
- The overlay pass at the clamp level 4 draws level 4's own list [I: stock has no level-5 list; not seen in a capture].

## 12. T3 as built (ticket 4552)

Worker W3 of epic 0431, 2026-11-16 15:12 EST on, branch `task-4552-forest-shadows` (from `epic-0431-trees` 56a5823d).
Offline only (no game client). Inputs: five stock decal tap recordings (.dtap = the tap's image, terrain-decals.md 10.4):
0366's `F:/c5-64bit-work/0366/ref/C5H-0366-{TINY,TINYB}_0008/armS/decals/load0003.dtap` and `.../C5H-0366-HUGE_0350/...`,
4111's `F:/c5-64bit-work/0166/4111/ref/C5H-0166-4111-{TINY_0008,HUGE_0350}/armS/decals/load0003.dtap` (stock config, VT
present, pinned shell test DLL); the D33 product-path image of the scene library's huge bundle
(`F:/c5-64bit-work/scene-library/bundles/C5H-0301-HUGE_0350_20261116-042357_workstation/decals/load0003.dtap`, workstation,
2026-11-16 04:23, pins of its scene.json); 0420's forest tap and census of job c78e and the height fields and plot tables of
4550's proof (10.5). Scripts and outputs in `render/evidence/0431/4552/`.

### 12.1 The EXE's Tree_Shadow record, read from stock recordings [L]

`quad_form.py` (`quad_form.out.txt`) and the test program's `--dtapcheck` (`dtapcheck.out.txt`) over the five stock
recordings: 692 Tree_Shadow decals, 102,771 quads.

- **Names and keys**: albedo and height `Tree_Shadow_2.dds`, spec `blank_texture_spec_decal.dds` (the default spec, 1.3),
  priority 0.1, pass 1, in every decal; an even triangle count (two per quad).
- **One quad per drawn tree, 6 vertices, one pattern in all 102,771**: corners (min, min) (max, min) (max, max) | (min,
  min) (max, max) (min, max) with uv (0, 1) (1, 1) (1, 0) | (0, 1) (1, 0) (0, 0). Half-size 6.0 or 3.6 (80,310 / 22,461
  quads). The quads follow the layout's order (sample y descending) in every decal.
- **The rectangle** is the box of every corner, clamped to the world rectangle TDS+0x6C (0, 0, samplesX - 1, samplesY -
  1): 5 Tiny decals per session lie at the east edge and are clamped; their vertices stay normalised to the clamped
  rectangle (positions up to 1.97 / 2.08) [L].
- **The vertex positions are binary16 (round to nearest even) of (p - min) x (1 / extent) in single precision**: for every
  quad a single-precision centre c exists (within 0.08 of the decoded one) whose corners c - h and c + h give the EXE's
  half bytes on both axes by that arithmetic, in all 102,771 quads. With a division (p - min) / extent instead, 2 quads of
  TinyB's 25,408 have no such centre; truncation fails 7-8 % of the quads, double precision 0.4-0.5 %. This shows the
  arithmetic is consistent with the bytes; it does not show the EXE's own centres (the stock sessions have no forest tap).
  *Correction of terrain-decals.md 1.2 ("half2 position normalised to the decal's rectangle ((x - min x) / width ...)"):
  for the Tree_Shadow decals at least, the bytes require x (1 / width) [L]; the other producers were not checked.*
- **AddDecal's index point and cell** (terrain-decals.md 18.1): the walk block's point equals (min + max) x 0.5 in
  single precision and the cell trunc((point - origin) x inverse) clamped, for every small decal of 0366's three stock
  images (462, 461, 17,609) and of the D33 bundle image (17,577), Tree_Shadow decals included [L].

### 12.2 The module as built [C, tests]

`render/include/c5r/forest/shadows.h`, `render/src/forest/shadows.cpp` (`c5r::forest`, pure CPU). For the scene (4554):

1. `shadowDecal(PlotBlocks, plotX, plotY, worldRect, ShadowDecal&)`: one built plot's decal in the form of 12.1, from
   4550's drawn trees (`DrawnTree::sampleX/Y`, `PlotBlocks::shadowHalf`); a plot with no drawn tree gives no decal (quads
   0) [I: no census shows a decal without quads]. `worldRectOf(HeightGrid)` = (0, 0, samplesX - 1, samplesY - 1) when no
   tap set is at hand. The content hash is FNV-1a 64 of pass, priority, rectangle, triangles and vertex bytes (not the
   tap's hash, which includes the EXE's handles).
2. `mergeShadows(tap set, forest decals oldest first, marks, out)`: a new `DecalSet`; the tap's set is never modified.
   The forest decals are appended after the tap's own, so every tap decal keeps its index; list orders follow the tap's
   newest; the two names are added when absent (compared without case or path). **Order** [C 18.3, I]: each pass is
   selected and sorted on its own and pass 1 holds only tree shadows in every census, so only the order among the forest
   decals matters; with the tap's walk block each forest decal joins the EXE's index as AddDecal would put it (12.1's
   point and cell, at the head of its cell's list, the last built first; a side over 256 samples: the large list's end),
   i.e. newer than every tap decal, which stock interleaves by time [I]. Pass-0 lists are unchanged on every tile (tests;
   10,752 of 10,752 huge tiles in 12.4). With the tap's area block (4111) the bits of every forest decal's plain tile square
   and of the `marks` are set (stock's rule, terrain-decals.md 14.6); without one nothing is made (the residency's 4105
   rule: a tile with decals). A tap set that already holds Tree_Shadow decals (the EXE's Forest on) is refused
   (`Unsupported`): the shadows would draw twice.
3. `shadowChange(decals built or dropped, worldRect, ShadowChange&)`: their rectangles (a `DecalChange`'s `rects`), the
   tiles `tilesForRects` raises for them, and their plain tile squares (`DecalChange::areaTiles`, and what the scene adds
   to its `marks`: stock's bit is never cleared until the next load).

Statuses: every refusal returns `BadLayout` (or `Unsupported` above) and leaves the output empty: a world rectangle not
finite or empty, a half-size not finite or <= 0, a tree centre not finite, more than 10,922 quads (AddDecal's 3 x
triangles <= 0xFFFE), a plot outside the world, a null list or entry, vertices not 6 x quads, a rectangle outside the
tap's world, more than 2^32 - 1 vertices, list orders past 2^32 - 1, an inconsistent walk.

### 12.3 The offline proof per plot (acceptance a) [L]

`run_proof.sh` / `proof_4552.py` (outputs `proof_{tiny,huge}_census_*.out.txt`), 2026-11-16: 4550's seeding (the tap file
whose built records with decal handles equal the census's Tree_Shadow count, its layouts and records through the
placement, cut mask empty), each plot's decal by `shadowDecal` against the census decal paired with it (quads =
triangles / 2, the rectangle within 0.01):

| set | plots | exact | module more | fewer | rect only |
|---|---|---|---|---|---|
| Tiny9v after the load | 95 | **46** | 49 | 0 | 0 |
| Tiny9v after the tour | 166 | **54** | 112 | 0 | 0 |
| huge after the load | 96 | **42** | 54 | 0 | 0 |
| huge after the tour | 323 | **58** | 265 | 0 | 0 |
| huge after 2 AI turns | 271 | **58** | 213 | 0 | 0 |

The same plots as 4550's (10.5), as expected: every "more" plot is the cut mask (4553), listed by name with what the plot
table shows in each output file (coast, river, route or improvement on or next to the plot). Every census decal is pass 1,
priority 0.1. **Not possible: quad by quad in the same session**: 0420's decal watcher kept the images in memory only
(`forest_t0.py` `DecalWatcher`), and no recording holds a forest tap and a decal image of one session; 12.1 checks the
quad form on stock recordings instead.

### 12.4 The tile+0x300 count on the huge save (acceptance b) [L]

The forest decals of the huge sets merged (`mergeShadows`) into the D33 bundle image (17,712 decals, walk and area block;
its EXE bits 2,718, the D33 count of 0420 being 2,721-2,726), counted per tile of 128 x 84 by the module's own calls
(`decalsForTile`, the residency's 4105 rule with `decalAreaFromExe` off; `areaChangedAt`, the rule with it on):

| forest decals in the set | marks (decals of earlier tap files of the load / of every forest plot) | area bits (switch on) | tiles with decals (4105's rule) |
|---|---|---|---|
| none (the D33 image) | - | 2,718 | 2,993 |
| 96 (after the load) | 229 decals of tap generations 2-3 | 2,912 | 3,058 |
| 323 (after the tour) | 1,365 | 2,949 | 3,065 |
| 271 (after 2 AI turns) | 4,680 | 2,949 | 3,058 |
| 96 (after the load) | + every forest plot of the map (690) | **3,020** | 3,058 |
| 323 (after the tour) | + every forest plot (690) | **3,020** | 3,065 |
| 271 (after 2 AI turns) | + every forest plot (684) | **3,020** | 3,058 |

- **Stock's 3,015-3,023 is reached only when every forest and jungle plot of the map marks its tiles**: D33's bits plus the
  plain squares of every plot's shadow decal give 3,020 (stock's own counts: 3,015 / 3,021 / 3,020 in 4106 / 4111 / 0367,
  3,018-3,022 in 0420's Forest-on arm). With only the plots built in view the count stays 2,912-2,949. This fits a shadow
  decal for every record at the load (2.5: `0x770EE0` "for all records from `0x546110`"; 8.8's first tap file of each load
  lists every record as built, 690 / 0 handles; 0367's stock huge load removed 592 Tree_Shadow decals) that the cut-stamp
  refresh then removes again, leaving the bits [L for the count, I for the mechanism: `0x546110` and its caller not read].
  **For the scene (4554)**: after a load, make every forest plot's decal once and keep its `areaTiles` as marks (stock's t8
  set), and draw only the decals of plots built in a view.
- **The t8 marks come with the set** [L]: every plain-square tile of the forest decals has decals in the merged set (387 of
  387 after the load, 879 of 879 after the tour, 740 of 740 after the turns), a pass-1 list, and with the area block its
  bit; so with `decalAreaFromExe` off (today's default, 8.5) the residency bakes those tiles with areaChanged, and with it
  on (MC decision of terrain-decals.md 14.6, (a1); not flipped on this branch) the merged block carries them. Under 4105's
  rule a tile whose forest decal was dropped loses areaChanged; stock's bit stays.
- Pass-0 lists: equal to the D33 image's on all 10,752 tiles in every set. The merge of the huge set took 5.4-10.5 ms on the
  workstation (an indication, not a timing result: one run each, the machine shared; 17,712 + up to 323 decals) [L].

### 12.5 Tests [C]

`render/tests/forest/shadows_test.cpp` (`c5r_forest_shadows`, 15 cases, plus `shadows_util.cpp`: a .dtap reader for the
proof modes): the record's corners, uv and order decoded, the rectangle and the box's 0 and 1, the hash; hills (0.6f x
6.0f); the clamp at the east and west edges with positions past 1 and below 0; no drawn tree; a plot through `buildPlot`
(180 trees, both kinds); the merge (indices, names, list orders, vertices, the tap untouched, pass-0 lists of every tile,
4111's tie rule without a walk), the walk (points, cells, ranks newest first, the tap's ranks moved, the EXE's selection
of pass 1, pass 0 unchanged on every tile), a large forest decal and a clamped cell, the area block and marks (outside
ones ignored, a block of the wrong size ignored); the change (rectangles, `tilesForRects`, plain squares, every tile whose
list changes is in it); negatives with outputs for every refusal of 12.2 (13 for the record, 14 for the merge, 6 for the
change) and the reader's. Suites, 2026-11-16 (`render/evidence/0431/4552/suites.out.txt`): plain `python render/build.py
--test` **63 of 63 passed, 0 skipped** (15:35-15:39); MSVC ASan `--asan --test` **63 of 63 passed, 0 skipped, 0 ASan
reports**, canary reported, peak 1,583 MB (15:40-15:54).

**A trap for the scene (4554)** [C, `srv_residency_source.cpp` `decalChanges`, read only]: the residency skips a
`DecalChange` whose `generation` is at or below the generation that was in force when the field arrived (`changesSkipped`,
12.3 of terrain-decals.md). The merged set keeps the tap's generation, so a forest change sent with the tap's generation
in force can be skipped and its tiles keep their bake without the new shadows. The scene must give forest changes a
generation the residency does not skip (or raise the tiles another way); this module does not choose it.

### 12.6 Not verified, open

- Quad by quad in one session (12.3); the EXE's tree centres themselves (12.1 shows only that the arithmetic fits).
- The order of the forest decals against a pass-1 tap decal of equal priority (never seen) and the EXE's interleaving
  in time [I].
- `0x546110` (who calls it, when every record gets a decal) was not read; 12.4's count is the evidence.
- Old places for the lead at the merge: section 0 item 5 and 2.5 (the record form of 12.1, and that every forest plot
  marks its tiles at the load, 12.4); 9.6's "4552 shadows" line (done, 12.3-12.4); terrain-decals.md 1.2's "/ width"
  (12.1); terrain-decals.md 14.6's "Named follow-up, no id yet" for the tree shadows (this ticket).

## 13. The scene as built (ticket 4554)

Worker W4 of epic 0431, 2026-11-16 15:12-16:00 EST and (item 2, the shadow seam and the header clash) 16:00-16:13 EST, branch `task-4554-forest-scene` (from `epic-0431-trees` 56a5823d).
Offline (no game client): a WARP device, the EXE's programs from the Ghidra project's copy of `CivilizationV_DX11.exe`
(read-only; render/build.py's default, the Steam EXE's bytes) and the `pure_vp` install copy with VT
(`%USERPROFILE%/Desktop/pure_vp/Sid Meier's Civilization V`; D94: VT present). The shared server files (`Scene`, the host,
the options) were held by epic 0375: the scene was built and tested on its own first; after 0375's merge and the MC's
grant (main ef9b829a) the wiring was applied (13.6, item 3, 2026-11-16 17:20-).

### 13.1 Files, and why not `server/src/render/` [C: compile and link errors, 2026-11-16]

| File | What |
|---|---|
| `server/src/forest/srv_forest_scene.{h,cpp}` | `SceneForest` (what `Scene` calls, abstract), `ForestScene` (the placement, the feed, the visibility rule, the Screen Overlay target, the stage, counters, lines, the Tree_Shadow decals of 13.8), the renderer itself, the factory `MakeSceneForest`, `ForestFatalStep` (inline) |
| `server/src/forest/srv_forest_ids.h` | the feature-id table (header only, 13.3) |
| `server/tests/render/forest/` | `test_forest_scene.cpp` (19 cases), `test_forest_build.py`, `test_forest_scene.py` |

- **The two forest headers could not share a unit** (fixed in item 2, 2026-11-16 16:05, granted by lead-0431).
  `c5r/forest/placement.h` and `c5r/forest/draw.h` both defined `c5r::forest::CrtRand`, and `Kind` was an `enum class` in
  one and a plain `enum` in the other: a redefinition error in any unit that includes both. Now `CrtRand` is in
  `render/include/c5r/forest/crtrand.h` (the same class, included by both), and draw.h's card kind is `CardKind` (`kForest`
  0, `kJungle` 1 unchanged; the type name was used nowhere). The modules' code is unchanged: `render/build.py --test -R
  c5r_forest` 3 of 3 plain and under ASan (0 reports, canary reported), the draw's per-draw replay still difference 0
  (`render_forest_suites.out.txt`). The renderer's wrapper unit `srv_forest_draw.{h,cpp}` of the first item was folded
  into `srv_forest_scene.cpp` (it holds `c5r::forest::Renderer` and the frame's `Block`s itself); the stage test's pixels
  are the same as before (29,652 tree pixels, every one changed by the red overlay). *Replaces: "The renderer therefore
  sits in its own unit ... not done here".*
- **`c5r::forest::Renderer` needs `c5r_models.lib`.** Its `cardsFromGr2` calls the `.gr2` reader (`c5m::read_gr2`,
  `c5m::Mesh::read`, ...), built by `render/src/units/CMakeLists.txt` as `<render build>/src/units/c5r_models.lib` beside
  `c5render.lib`. Before epic 0375's merge `server/build_server.py` links only `c5render.lib`, and every render test
  builder that globs `server/src/render/*.cpp` (`build_render_tests.py`, `fow/test_fow_build.py` and the others) links
  only `c5render.lib` too: a forest unit in `server/src/render/` breaks all of them at link time (`lld-link: error:
  undefined symbol: ... c5m::read_gr2 ...`, the first test build). So the units live in `server/src/forest/`, outside
  every glob, as 0375's `server/src/units/` do, and `Scene` reaches them only through `SceneForest` and a factory that
  `srv_forest_scene.cpp` registers at static initialisation (`MakeSceneForest` returns null in a program that does not
  compile `server/src/forest/`: 0375's `MakeSceneUnits` pattern). The wiring (13.6) adds `server/src/forest/*.cpp` to
  `build_server.py`'s render units; `c5r_models.lib` is linked there by 0375 (`c5r_models_lib`, commit 41ec8c39).
  *Correction of design 9.2's "server/src/render/srv_forest_scene.{h,cpp}" and of the brief's "no build file edit is
  needed": the files moved, and the server's build needs the forest folder and c5r_models.*

### 13.2 The rules as built

- **Records** (design 9.3): one `c5r::forest::Placement` record per plot of the feed's table whose `feature` is the forest
  or the jungle id (13.3); kind 3 / 4 when `plotType == 1` (PLOT_HILLS; 10.1). The table comes through the `FowPlotTable`
  seam (`MakeFeedPlotTable` = `SrvFeed::CopyPlots`, the fog's). `onBatch`: a new snapshot generation, a `kSnapshot` or
  `kOverflow` event, or a rise of `Batch::dropped` re-reads the table and applies every plot in index order (y-major),
  each new record taking `rand() % 20` from the load's stream; otherwise, when the batch names changed plots, the table is
  copied once and only the plots whose `feature` or `plotType` moved are patched (`Placement::setPlot`: the same kind
  keeps record and blocks; another kind gets a new record with a new layout index; neither loses record and blocks).
  `kFeatureChanged` events of a plot outside the table are LOUD and counted (the table is the truth for the record).
  [I: the order in which stock's listeners draw layout indices at a game start was not read; live pictures are random
  per session in stock too (9.6).]
- **The random streams**: a session seed (option, else QPC xor pid x 2654435761: random per session as stock); the card
  choice's seed and each load's layout seed are drawn from it with `CrtRand` (the same cards after a device rebuild; new
  layouts at every new load, as stock's game start).
- **A field** (`onField`): a new `loadSeq` or another `M_h2w` = a new load: the 20 layouts again (`Placement::initWith`;
  forests.xml and the masks are read once, at `init`) and every record again; the same load re-applied (a device
  rebuild, a tile-size change) = every block rebuilt from the field (`invalidateAll`).
- **Visibility** (2.2, `0x654420`): the plot centre = HexToWorld of its cube coordinates (`M_h2w`, `x sx - ox`,
  `y sy - oy`, z = the height at that sample, as `buildPlot` takes a tree's z) against the six planes of `view[v] x proj`
  of every camera-block view (left, right, bottom, top, near z >= 0, far; each normalised by its xyz length), inside when
  every signed distance is >= -64. A plot visible in any view is drawn in every view (11.1: the frame's block list per
  view). [I: the plane order and normalisation inside `0x654420` were not read; the rule is the one 2.2 states, the plane
  set of the terrain's `exeTileSphereVisible`.]
- **Blocks**: built on a plot's first visibility (`Placement::blocks`, stock's first build), kept until a feature change of
  the plot, a new field or a cut generation change (4553's source through `setCutSource`; `EmptyCut` until then).
- **Style entry** per plot: the terrain renderer's `tileStyleAt` of the tile that holds the plot centre's sample
  (`floor(sample / tileSamples)`, x wrapped on a wrapping map). [I: which tile's +0x164 `0x439E00` reads was not read;
  with the server's 1-plot tiles this is the plot's own tile.]
- **The stage** (`drawStage`, after the water and the ice): nothing when no plot is visible (t3 is read by the trees
  only); else `c5r::terrain::Renderer::drawScreenOverlay` into the scene's own target (the frame's size,
  `R8G8B8A8_TYPELESS`, one mip, UNORM views; remade on a size change) with **the camera and `Options` of that frame's
  terrain draw**, then `c5r::forest::Renderer::draw` into the scene's colour and depth with: views = the camera block's
  `view[0..numViews-1]` and `proj`; per view the OverlayProj of its wrap copy (`Options::overlay.proj[k + 1]`, k found as
  `ShadowCopiesFromFrame` does: rows 0-2 equal to view 0's, row 3 = view 0's + k x wrapPeriod x row 0); the terrain's fog
  of war (`Options::fow`) when on, else a white mask, a black cloud base and the fog's FOWAnim (the ice's rule); the
  distance fog of `Options::fog`; `overlay_alpha` of forests.xml; t4 = the terrain's fog ramp; t5 = the five
  `Terrain_<Style>_ColorKey.tga` volumes the scene builds itself (`c5r::bake::buildColorKeyLut`); t8 = the terrain's
  `fowLut(i)`; t2 = the overlay texture (bound, not read).
- **Lines**: `forest 1s: state= drawn= visible= built= blocks= draws= cpu_ms= records= built_total= load= ids= target= |
  adds= removes= changes= rereads= overflows= feature_events= outside= build_err= rec_err= ovl_err= draw_err= held= empty=
  failed=` (a line of its own, as the fog's) and `forest totals: ...` (appended to the terrain's totals line).
- **The Tree_Shadow decals**: 13.8. `setPlotHook` (a plot built, a plot that lost its record, every record dropped)
  stays for tests and later users; `placement()`, `shadowAt`, `marks()`.

### 13.3 The feature ids by name

The render side has no name source: the feed's `PlotRecord` carries type indices, and the render thread never reads the
DLL (bug 0103). So `srv_forest_ids.h` holds a process-wide table that the feed's DLL side fills: for every feature type it
reads in a snapshot or refresh pass `ForestFeatureTagSeen(id, ICvFeatureInfo1::GetArtDefineTag())` (`GetFeatureInfo` and
`GetArtDefineTag` are on the D30 purity list, `proxy/model/pure-methods.tsv` rows 46 and 5), and
`ForestFeatureIdsResolved()` just before `SrvFeed::EndSnapshot` (13.6). Tags `ART_DEF_FEATURE_FOREST` /
`ART_DEF_FEATURE_JUNGLE` [I: the art defines of Civ5Features.xml; on 4550's saves they should resolve to 5 / 1, to be
confirmed by the `feature ids resolved: forest 5, jungle 1` line of 4555's first live run]. Header only, so the DLL side
includes it without the renderer. `ForestScene::poll()` reads it every loop iteration; a change of resolved ids applies
every record again; `setFeatureIds` (tests, a host with its own source) overrides it. No resolved ids within `idsWaitMs`
(10,000 ms) of the first plot table is an install failure naming the missing wiring (never a session without trees).
Rejected: a fixed 5 / 1 (design 9.3: never by number); a scan of `0 .. GetNumFeatureInfos` (not on the purity list, and
`CvGlobals::getFeatureInfo` asserts out of range).

### 13.4 Failure statuses as built (design 9.5)

Install failure (`failed()` with the text, LOUD once, `ForestFatalStep` -> `fatal(5, why)` once; without a hook LOUD once
and state WITHOUT, no stage): at `init` no device, no install index (`gameDir` empty or not opening), forests.xml or a mask
refused (`c5r::forest::loadInstall`'s status), the renderer refused (the ForestShader blobs by VA, the `_2` models and
textures, the device: `Renderer::init`'s status), a Terrain ColorKey missing or refused, the 1 x 1 constants; at the first
frame the Screen Overlay target not made, the terrain without a fog ramp (started without the install), the layouts
refused for the field (`M_h2w` not invertible), the ids not published in time (13.3). In play (LOUD the first 20, then
every 100th or on a new text; counted; never fatal): a feature event outside the table, a plot the placement refuses, a
plot whose blocks fail to build (skipped that frame), a failed Screen Overlay pass or tree draw (the frame stands without
trees). `render_forest=0` (`ForestSceneOptions::on` false): state OFF, no records, no stage.

### 13.5 Tests (2026-11-16)

`server/tests/render/forest/test_forest_scene.cpp`, built by `test_forest_build.py` (every `server/src/render/*.cpp` and
`server/src/forest/*.cpp` with the feed, options and ring-sink units, linked with `c5render.lib` and `c5r_models.lib`), run
by `test_forest_scene.py` (C5R_EXE and C5R_GAME_DIR required, no skip). Inputs made by the program: a field of 8 x 10 grid
plots (the 8 x 6 table) flat at u16 20,000 (z 15.69, above the water rule) with the textbook `M_h2w`, a fake plot table
(forest on rows 0-1, jungle on rows 2-3, hills at x = 1 / 2, a marsh at (0, 4)), camera blocks; the real terrain and
forest modules on WARP at 320 x 240.

| case | what it shows |
|---|---|
| snapshot | no record before the ids; then 32 records, kinds 1-4 exactly as the table (hills -> 3 / 4), cube coordinates, layout < 20, 180 trees per plot (VT's forests.xml); the same seed gives the same layout indices, another seed others |
| feature_change | none -> forest added (a new layout), forest -> none removed, forest -> jungle and forest -> forest on hills new records, an event without a change changes nothing; untouched plots keep record and layout; one table copy per patch batch; a plot change without a feature change changes nothing; the event of plot 9999 LOUD once and counted; the hook told of the 3 removals |
| overflow | `kOverflow` and a moved `dropped` counter each re-read the table (a row lost, a plot back), a new snapshot generation too |
| ids_by_name | nothing until resolved; the forest and jungle tags (a marsh tag ignored) + resolved -> 32 records; another forest id -> every record again ("they CHANGED") |
| ids_never_published | no ids within the wait: an install failure naming the ids, fatal(5) once |
| visibility | the scene's set equals an independent double-precision plane test; 2 plots inside the frustum, 20 with the 64-unit margin, 48 in all (the margin decides); the stage builds exactly the visible plots once, a second frame builds none, a wider camera builds only the new ones; a wrap view (k = -1) adds the map's right edge, and two views give the union of each view's set |
| stage_overlay_before_trees | trees into a cleared target over the terrain's depth: 29,652 tree pixels of 76,800 (32 plots, 64 blocks, 64 draws); a transparent overlay leaves the Screen Overlay all 0; a red premultiplied overlay (204, 0, 0, 204) fills it (66,240 pixels, no other value) and changes **29,652 of 29,652** tree pixels, all redder, none moved; back to transparent gives frame 1's pixels exactly (the pass runs before the trees of the same frame: t3 is never a frame late) |
| render_forest_off | state OFF, no records, nothing drawn, no target made |
| install_failures | no install; an install that does not open; no device; no EXE; the VS VA naming the PS; and through a loose folder in front of the install (gamefs `extraLooseRoots`): forests.xml with num_tiles 0, without `<Forest>`; forest_tile1.dds as A8R8G8B8; forest_europe_2.gr2 not a Granny file; jungle_asia_2.dds not a DDS; Terrain_Generic_ColorKey.tga not a TGA: each a failure naming it, LOUD, fatal(5, why) once, WITHOUT after the hookless step |
| frame_failures | the Screen Overlay target not made (a 0 x 0 frame: WARP made 20,000 x 20,000 and 2^20 x 1 targets without an error, so no size provokes a real `CreateTexture2D` failure there [L]); the terrain without its fog ramp; a field with `M_h2w` all 0 (layouts refused) |
| in_play_faults | a NaN overlay matrix: the Screen Overlay pass fails three times, LOUD and counted, never failed() and no fatal step; the next good frame draws |
| field_reload | the same load: blocks rebuilt, layouts and records kept; a new load: a new layout seed, records again; a device rebuild keeps the records and draws |
| shadow_seam | the load's marks equal an independent computation (every record built, its decal, the plain squares); no decal before a view; the marks' change (generation 0xF0000001, no rectangles, the marks as area tiles), then none; a frame: one decal per built plot, one change (0xF0000002) with their rectangles; `composeDecals` = the tap's set plus every decal, Tree_Shadow count right, the tap's generation, the tap untouched, cached while nothing changes; a built plot losing its forest: its decal gone, its rectangle in the next change (0xF0000003), its tiles still marked; a tap set holding Tree_Shadow decals refused LOUD once, the set returned as it is, not failed; a new load: decals dropped, marks computed again |
| residency_generation | the real `ResidencySource` (stand-in baker, the install's names): `setDecals(composeDecals(tap generation 5))`, then the field (generation 5 in force); the forest's change sent with generation 5 is **skipped** (`changesSkipped` 1, 12.4's trap); the same change with its band generation 0xF0000001 is **applied** (32 rectangles, 40 tiles raised) |
| scene_wiring | (item 3) `Scene` itself in the offline player's form (no section: `init`, `loadField`, the feed's real table through `SrvFeed::BeginSnapshot` / `SetPlot` / `EndSnapshot` / `Drain` and `forestFeed`, `poll`, `draw`), render_tiles none, no water, overlay or fog: `render_forest=0` and `render_forest=1` without published feature ids give **bit-equal frames (0 of 76,800 pixels differ)**; with the ids published (`ForestFeatureTagSeen` / `Resolved`, read by `Scene::poll`) the frame differs in **29,652 pixels**, the tree pixels of the stage test (32 plots drawn); the forest's totals line comes with the scene's |
| host_config | (item 3) `ParseHostConfig`: `render_forest` 0 / 1 accepted, 2 refused naming the key; `render_test_forest_ids_wait_ms` -5 refused; `render_test_forest_seed` 77 accepted, x refused; `render_test_cut_fail` 1 accepted, yes refused; `render_test_cut_wait_ms` 0 accepted, -1 refused |
| cut_tap_rebuild | (item 3, 13.9) a fake producer of ticket 4553's control block and image sections (the field's grid, 16 x 20 constant tiles): generation 1 empty -> 29,652 tree pixels, 32 decals; generation 2 the same content -> every built plot rebuilt (32), no decal change; generation 3 bits 0-2 everywhere -> 26 decals dropped, 6 kept (plots with trees past the grid's east edge, which read 0 as the EXE's lookup, each kept decal equal to the plot built under the same grid), 1,246 tree pixels; generation 4 bit 3 only -> the 32 decals and the 29,652 pixels back |
| cut_tap_failures | (item 3, 13.9) the producer FAILED -> install failure naming the cut tap, fatal(5) once; a first image with a wrong hash -> install failure; no control block past the bound -> "no cut-mask grid ... ABSENT"; the tap OFF past the bound -> no failure; render_test_cut_fail=1 -> failure once the pid is known (not before); no pid -> no reader, no failure |
| factory | `MakeSceneForest` returns a `ForestScene` in a program that links `server/src/forest/` |

Results, 2026-11-16 (logs in `render/evidence/0431/4554/`):

- plain (clang-cl /std:c++14 /W3 /WX /O2, WARP): **15 of 15 PASS, exit 0** (item 2, 16:08; `test_forest_scene_plain.out.txt`);
  the first item's 14 of 14 through `test_forest_scene.py`: **3 passed** in 36 s (`pytest_plain.out.txt`).
- MSVC `/fsanitize=address` (c5render and c5r_models from `render/build.py --asan`, the ASan runtime beside the binary):
  **15 of 15 PASS, exit 0, 0 AddressSanitizer reports** (item 2, 16:10; `test_forest_scene_asan.out.txt`); the first
  item's 14 through the runner **3 passed** in 92 s (`pytest_asan.out.txt`).
- **Item 3 (the wiring and the cut tap, 2026-11-16 17:20-19:02; `item3_suites.out.txt`)**: the forest scene tests **19 of 19
  PASS** plain and under MSVC ASan (0 reports). The suites that compile `Scene` and the host, plain:
  - fow 3 passed;
  - overlay 3;
  - water 27;
  - shadow / ice scene 6;
  - decal tap 13;
  - units 7 (41 cases);
  - scene bundle 626 / 162 / 163 / 24 checks (sceneshot on the format-4 library);
  - render host (`build_render_tests.py`) **117 passed**.

  These ran twice, before and after the cut tap's integration. Also:
  - the cut reader suite passed;
  - `shell/tests/test_cut_tap.py` **26 passed** after the default flip;
  - `run_remote_feed_test.py` OK with the two feed calls;
  - `render/build.py --test` **67 of 67, 0 skipped** (main has 67 now, not 63);
  - `build_server.py --config release` OK in 119 s, server.exe SHA-256 acbe8ea2... with the `srv_forest_*` objects
    linked (319 lines of the map).

  Not run under ASan: the other suites (one ASan pass each would cost about 10 minutes of c5render-asan build per
  builder; the forest's own tests, which link every render unit and the forest units, ran under ASan).
- The render forest suites after the header change (item 2): `render/build.py --test -R c5r_forest` **3 of 3 passed, 0
  skipped** plain and under ASan (**0 ASan reports**, canary reported) (`render_forest_suites.out.txt`).
- `server/build_server.py --config release --no-proxy --offline` on this branch (the DLL deps of 0325's `deps-release`,
  fork copy `C:/c5-64bit-work/0325/src-x64f` at the pin 04b1211b8): **OK in 71 s**, server.exe SHA-256 bfa68fd4...
  (`build_server.out.txt`; nothing of the server changed: the forest units are outside its glob until the wiring). The
  forest unit compiled with exactly the build's render flags (28; first item from the build's `render-compile-args.json`,
  item 2 from `build_server.render_compile_args('release')` itself): **OK** (`compile_with_server_args.py`,
  `compile_with_server_args.out.txt`). Their link with `c5render.lib` +
  `c5r_models.lib` is the test program's.
- The scene suite that compiles every render unit (the fog's, `server/tests/render/fow/`) on this branch: built and **3 passed** in 65 s (its 19 cases; `fow_scene_suite_plain.out.txt`). The render host suite
  (`build_render_tests.py`, `server/tests/render/test_*.py`) was not run: no file it compiles changed.

### 13.6 The wiring as applied (item 3, 2026-11-16, after 0375's merge and the MC's grant)

Applied on `task-4554-forest-scene` after merging `epic-0431-trees` 1046801b (main with 0375). *Replaces the proposed
patch of the first item, which was written against main before 0375; the lines were re-fitted to 0375's units stage.*

- `server/build_server.py` `render_sources()`: `server/src/forest/*.cpp` after 0375's `server/src/units/*.cpp` (stems
  `srv_forest_*`, unique; `c5r_models.lib` is linked by 0375's `c5r_models_lib`).
- `server/src/render/srv_render_scene.h`: includes `forest/srv_forest_scene.h`; `SceneOptions::forest` (0 here: tools,
  the offline player and scene tests), `forestIdsWaitMs` (10,000), `forestSeed` (0: random); `Scene::forestFeed(batch)`,
  `Scene::forest()`; private `m_forest` (a `SceneForest` from `MakeSceneForest()`), `composedDecals(tap)`,
  `forestDecalPoll()`.
- `server/src/render/srv_render_scene.cpp`:
  - `init`, after the ice: `render_forest=1` makes the forest scene once (kept across device rebuilds) and inits it with
    the shared install index (issue 0198), the EXE, the seed and the ids wait; `render_forest=0` drops it and says
    "render_forest=0: no trees"; a program without `server/src/forest/` says so LOUD.
  - `releaseDevice`: the forest's D3D objects (records, layouts, decals kept). `applyField`: `onField(grid, loadSeq)`.
  - `poll` (the live path and the offline player's): the forest's `poll()` (the published ids; on the live path first
    `setExePid(exePid())` for the cut tap's reader, 13.9), then `forestDecalPoll()`.
  - `draw`: **the stage after the water and the ice and before 0375's units pass** (`m_units->drawScene`) and the cloud
    planes: stock's order is terrain, water, ice, the Screen Overlay pass, coast waves, city-block draws, all
    ForestShader draws, then buildings and units, then FOW CULL and the cloud planes (11.1). Called with the frame's
    camera block, the terrain renderer, its camera and `op` (the overlay pair, the fog of war as bound) and the fog's
    FOWAnim.
  - Decals: the three `setDecals` calls (a device rebuild in `init`, `decalPoll`, `loadDecalImage`) take
    `composedDecals(cur)` = the tap's set with the forest's Tree_Shadow decals (13.8). `forestDecalPoll()`: the forest's
    `takeDecalChanges`; with a residency and a tap set in force, `setDecals(composed)` then `decalChanges` (P13) once the
    field is ready; without them the change is dropped (the decals join the set when the tap's set arrives).
  - Lines: the forest's 1-second line after the units' (RLog, render.log); its totals line after the units' (RNote).
- `server/src/render/srv_render_host.cpp`: `g_forest` (default 1 where the program links `server/src/forest/`, i.e.
  server.exe; 0 in a program without it, e.g. `render_host_test.exe`), `render_forest` 0 / 1 (1 refused in a program
  without the forest scene), `render_test_forest_ids_wait_ms` 0..600000, `render_test_forest_seed`, `render_test_cut_wait_ms`
  0..600000 and `render_test_cut_fail` 0 / 1 (13.9); `SceneOpts` passes
  them; the loop feeds `forestFeed(batch)` after `unitFeed` and runs `ForestFatalStep(h.scene.forest(), fatal, done)` after
  `UnitsFatalStep`. The recorder's `render_config` does not carry `render_forest` (the bundle format is 0301's; the player
  draws without trees, 13.7).
- `server/src/remote/srv_remote_feed.cpp` (granted for these two calls only): `FeatureCache::IsNW` calls
  `srvrender::ForestFeatureTagSeen(iFeature, p->GetArtDefineTag())` beside the `IsNaturalWonder` read (both pure, D30
  list); the snapshot pass calls `srvrender::ForestFeatureIdsResolved()` just before `SrvFeed::EndSnapshot()`. The include
  is header-only `forest/srv_forest_ids.h`.
- `server/src/render/srv_render_options.cpp`: nothing (`render_forest` is a server.cfg key, not a player option, D89).
- `shell/composite/shell_composite.cpp` (MC queue #2985): `Config::cutTap` default 1 (was 0): a product-path default
  change, 13.9.
- The render test builders that compile `srv_render_scene.cpp` without `server/src/forest/` link unchanged
  (`MakeSceneForest` null, `render_forest` 0).

### 13.7 Not verified, open

- Live: nothing (4555): the ids' tag text, the visibility rule's plane details against `0x654420`, the style tile, the
  pictures against stock.
- Only WARP; the hardware device was not run (the module's own tests ran on it, 11.4).
- The offline player (`srv_scene_sources`, design 9.2) does not draw trees: a bundle carries no forest member and no
  feature ids, and the player's `Scene` has `render_forest` 0. A bundle member (the ids, the session seed) is a follow-up
  for 4555 or the harness epic.
- MSAA: the stage draws into whatever colour and depth the scene has (the module draws into any `rtv` / `dsv`, 11.5).
- Stock's 12 mips and `GenerateMips` of the Screen Overlay: not made (11.4: the tree PS samples mip 0).
- The cut source is `EmptyCut`; 4553's reader plugs in through `setCutSource`.
- Tree_Shadow decals (13.8): with the default residency rule (`decalAreaFromExe` off) the marks change nothing in a bake
  (that rule reads only the decals); they reach the picture with the switch on or through the merged area block. The
  marks' first change raises every marked tile once per load (387 on the huge save after the load, 12.4), counted in the
  residency's `areaRaises` only with the switch on [I: its cost not measured]. The decals' rectangles come from the grid's
  world rectangle (`worldRectOf`), not from a tap set's own one (equal on Tiny and huge, shadows.h [L]).

### 13.8 The Tree_Shadow decals in the scene (item 2, 2026-11-16) [C, tests]

With 4552's `c5r/forest/shadows.h` (12.2), all in `srv_forest_scene.cpp`:

- **The load's marks** (12.4: stock reaches 3,015-3,023 bits on the huge save only when every record marks its tiles at the
  load): every time records are applied from a plot table (a load, a re-read, new ids), every forest / jungle plot is
  built once outside the placement's cache (`buildPlot` with the session's layouts and the cut source), its decal made
  (`shadowDecal`) and the plain squares of all of them (`shadowChange`'s `areaTiles`) join the marks (sorted, unique). A
  decal dropped later adds its tiles too; the marks are cleared only at a new load. They go to `mergeShadows` as `marks`
  and, once, to a change's `areaTiles`.
- **The decals**: only plots built in a view (`drawStage`'s first build, the placement's rule); a rebuild replaces the
  decal; a plot that loses its record, a new field of the same load (every block rebuilt), a cut source change, a new
  table or new ids drop it (the hook). A plot with no drawn tree has none. Kept oldest first (the merge's add order).
- **`composeDecals(tap)`** = `mergeShadows(tap, the decals, &marks)`: the tap's decals keep their indices, the forest's
  follow, the generation is the tap's. Cached on (tap pointer, a version that moves with every decal or mark change). A
  tap set with Tree_Shadow decals (`treeShadowCount` > 0: the EXE's Forest on) or a merge the module refuses: LOUD (once
  per tap generation / per text, then every 100th), counted, the tap's set returned as it is; never fatal (lead-0431's
  rule). Null tap: null (nothing to merge into).
- **`takeDecalChanges(out)`**: the decals built or dropped since the last call (copies kept until then) and the marks
  added since then, as ONE `DecalChange`: `rects` and `areaTiles` from `shadowChange`, plus the new marks; `worldRect`
  the grid's. **Its generation is `kForestDecalGenBase` (0xF0000000) + n**, n = 1, 2, ...: the residency skips a change
  whose generation is at or below the tap generation in force when its field arrived (12.4's trap, 12.5), and the merged
  set keeps the tap's generation; the band lies above any tap generation of a session (the tap counts sets from 1 per EXE
  run). `previousGeneration` = generation - 1. A change `shadowChange` refuses is sent with `all` (every decal tile
  re-baked), LOUD. `residency_generation` shows the trap and its answer on the real `ResidencySource`: the change with
  the tap's generation is skipped, the band's is applied (40 tiles raised).
- Counters: `forest 1s: ... | shadows= marks= changes= merges= tap_shadow_refused= merge_err= shadow_err=` and the totals'
  `tree shadows: decals marks (plots) changes merges tap sets refused merge errors shadow errors`.

## 14. T4 as built (ticket 4553)

Implementer of 4553 (account B, dispatcher-B), branch `task-4553-cut-tap`, 2026-11-16 15:15-18:00 EST; the text of
the issue log's drafts, placed by lead-0431 at the merge (18:00). The tap's design and tests in detail:
`shell-composite-taps.md` section 7.

### 14.1 P4: who owns the grid, and is it stamped with Forest off under D33 [C, L, 2026-11-16]


- **One grid, a static object of the Terrain system: `0xBA392C`** [C]. Every caller of the lookup `0x4365E0` (8 sites:
  `0x585490`, `0x770EE0`, `0x55D0C0` x2, `0x6D7560`, `0x451DF0`, `0x7B43B0`, `0x6919E0`) and of the stamp `0x62AE50` (12 sites)
  loads `ECX = 0xBA392C` just before the call; no other object is passed. It is the "plot mask grid" of
  `cities-and-landmarks.md` 2.7 (0338), the same object.
- **Made by CreateTerrain, freed by the Terrain shutdown** [C]: `0x709B50` calls `0x6F73A0(this = 0xBA392C)` at `0x70A04E`
  (size from the GridHeightmap, below; every tile set to "uniform 0"; byte +0 = 1; posts an event of type 0x1D), and then,
  at `0x70A0CC`, replays CreateTerrain's own polygon list into it through `0x62AE50` (bits per polygon, set). The river mesh
  build `0x6D51D0` (from `0x709B50` at `0x70AE88`) stamps the river triangles through `0x68FCB0` -> `0x52BE30` with bits
  0x09 (set). `TerrainSystem::ShutdownGame` `0x6684D0` calls `0x710400(this = 0xBA392C)` at `0x66859E` (frees the
  directory, resets the counts, byte +0 = 0); the static destructor `0x9D4400` frees the directory and the pool. Terrain is
  on in the product's D33 list (`shell/tools/darkred.py` D33_ON), and `water-system.md` row "River mesh" says [L, 3601] the
  river mesh is built in a D33 game with River off: so the grid exists and carries the river stamps under D33.
- **The stamp `0x62AE50` has no system gate** [C]: it bounds the polygon, rasterises it (`0x6287A0`: per covered cell
  new = (old & ~mask) | (value & mask), value 0xFF to set, 0x00 to clear; x wraps by the width) and posts the 0x20 event.
  Its 12 callers and their systems (cities-and-landmarks.md 2.7 for the bits): CreateTerrain's replay `0x709B50` (Terrain);
  route curves `0x5EC600` and the farm builder side `0x748DC0` (listeners registered under the Landmark bit,
  world-systems-and-stubs.md 4); the landmark visualiser `0x700030` / `0x45B2A0` (from `0x85E7F0` / `0x85E900`); the
  farm manager `0x86F690` / `0x46BFE0` (from `0x543D10`); wonders `0x65A150` (cvWonderVis); tree-cut meshes `0x69F250`
  (`0x598380` -> `0x614350`, the City update's path); `0x449C30` (from `0x704000`), `0x680F80` (from `0x52A360`),
  `0x617E20` (from `0x402C40` / `0x678EB0`, bit 0x04). None of them is in D33_OFF's systems (Decal, Forest, River, FOW,
  FluidFOW, DistanceFog, YieldIcons, Arrows, OverlayDebug, ColorKeyEdit) [C for the call paths named]. Pass g8
  (16:05): `0x704000` and `0x75D7D0` (-> `0x52A360` -> `0x680F80`) are called from the City update `0x537BF0`; the tree
  cut `0x598380` from `0x60BE50` (called by the city block placement `0x5BAC00`, cities-and-landmarks.md 1.12) and
  `0x686AF0` (from `0x766A70` / `0x5DB6B0`, not read); `0x402C40` from `0x5BF9A0`, `0x678EB0` from `0x45C200` / `0x738200`
  (the DynamicGeometry / visualiser group of the Landmark family, cities-and-landmarks.md 2.7) [C for the calls; I for
  the system of `0x5BF9A0`, `0x766A70`, `0x5DB6B0`]. No stamp path found under the Forest, River or Decal bits.
- **The route stamp writes bit 0x02, shared with the object footprints [L, ticket 4934 premise P1, 2026-11-20 03:31-03:33]**
  (*was [I]: section 2.3 "I for which bit each caller writes", cities-and-landmarks.md 2.7 listed `0x5EC600` under 0x02
  without a live check*). Instance c5h-e (old build 4390913, EXE stamp 0x546CD0A8), its own i0ship DLL
  (Release-5.4.8-39-g26234371e), stock systems (not the product path), save `C5H-VP8P-HUGE_0350`. The grid was read from
  outside with ReadProcessMemory: `render/evidence/0454/4934/gridread.py`, `p1_drive.py`, `p1_find.lua` and `p1_run.sh`;
  the grids are in scratch, not committed. The EXE is relocated: the address is `0xBA392C` - 0x400000 + the EXE's load
  base, which is why the cut tap's patterns carry `R()`. The test: three forest plots in a row, (100..102, 20), revealed,
  visible, no route, no improvement. `SetRouteType` through the Lua channel, the grid read 20 s after each step:
  - Two reads 20 s apart before any change: no difference (the grid was quiet).
  - **Road set on the three plots: 551 cells set, all bit 0x02**, nothing else. The cells form a band 2-7 cells across
    (median 6, so about 12 samples; the decal strip's half width is 2.27, roads.md 1) along the three plots and into the
    route plot west of them.
  - Road to railroad: no cell changes.
  - **Route taken off: 618 cells of 0x02 cleared**, the 551 plus 67 that were set before the road: the part of the
    neighbouring route's corridor that the polygon covers. The stamp clears by polygon, with no reference count, so
    removing a route can open a gap in a neighbour's corridor (a stock defect, not pictured).
  - Consequence for FixRoadLayout (4934): the EXE's route cells cannot be told from other 0x02 cells (model tree-cut
    surfaces, wonders, visualiser parts) by the grid alone. The MC chose option (A) (#7664, 2026-11-20): clear bit 0x02
    only within the band of the tap's own route-curve records, on plots with no improvement and no city, then OR the
    server's road footprint in (`docs/extras/roads.md` 4c).
  - **Seen live on the product path [L, ticket 4934 W7, 2026-11-20 08:38/08:42, c5h-b, huge save turn 350, DIAGNOSTIC]:**
    with FixRoadLayout on, the forest clears lanes along the server's road pieces and stock's own lanes on bare plots are
    filled with trees again (camera (75,27): stock's railroad loop and its lane are gone, our two straight branches each
    have a lane); with it off the picture is stock's. Cells per tap set on that save: 82,032 cleared on 346 plots,
    147,678 kept on 468 plots with an improvement or a city; our footprint 192,858 cells. Details and Visual Checks ids:
    `extras.md` X3.8.
- **Forest only reads it** [C]: the Forest's readers are `0x6D7560` (per tree, bits 7) and `0x770EE0` (the shadow quads,
  bits 7); its listener `0x715720` takes the 0x20 events with bits & 7 to rebuild records. With Forest off nothing of the
  grid changes but these reads.
- **Answer (static): the grid is the Terrain system's and is stamped under D33 by Terrain, Landmark and City paths: the stop
  condition does not hold [C].** The live read (the tap on oldpc, Forest-on and D33 arms of the same save, acceptance item
  "the Forest-off grid equal to the Forest-on grid") confirms it; the tap is the reading instrument (cdb on the live-session
  job cannot run the D33 config). If that read finds the grid empty or different under D33, this point is corrected and the
  lead is asked (the stop condition then holds for the shipped default).

### 14.2 P5: address, size, pointers, the change event [C, 2026-11-16]


*Reading it from outside the process (4934, 2026-11-20 [L]): the running EXE is relocated, so read at `0xBA392C` -
0x400000 + the EXE's load address (and the same for `[0xD90FC4]`); `render/evidence/0454/4934/gridread.py` does this.*

The object `G = 0xBA392C` (byte offsets; `0x6F73A0`, `0x60DA90`, `0x4A0A50`, `0x49F2D0`, `0x4CD580`, `0x710400`):

| G+ | what |
|---|---|
| +0x00 | byte: 1 after `0x6F73A0` (grid made), 0 after `0x710400` |
| +0x04 / +0x05 | scratch of the stamp: value byte (0xFF set, 0 clear) and bit mask (written by every `0x62AE50` / `0x68FCB0`) |
| +0x08 / +0x0C | width / height in cells: GridHeightmap samples x / y (`[0xD91270]+0x0C` / `+0x10`) halved, **rounded up to a multiple of 16** |
| +0x10 / +0x14 | tiles x / y = width / 16, height / 16 |
| +0x18 | directory: tilesX x tilesY entries of 8 bytes, row-major (tile (tx, ty) at ty x tilesX + tx): u16 state (1 = the whole tile holds the byte at +4; 0 = pooled, u32 pool index at +4) |
| +0x1C | the pool's free-list head (0x0FFFFFFF = none; a freed block's first dword = the next free index, u16 at +4 = 1) |
| +0x20 | pooled blocks in use |
| +0x24 / +0x28 / +0x2C | the pool: data (256-byte blocks, cell (c, r) of a tile at r x 16 + c), blocks made, capacity (reserved tiles / 4 at creation; doubles when full: the data pointer moves) |

- **Lookup** (`0x4365E0`): col = int(ox + x) >> 1, row = int((oy + y) x 1.1547344) >> 1 with ox, oy = GridHeightmap +0xB4 / +0xB8
  (`[0xD90FC4]`, 0 before a map), 0 when col or row is negative or not below G+0x08 / G+0x0C (the rounded sizes), else the
  cell's byte & mask (`render/include/c5r/forest/cut.h` holds this rule). A tile turns from uniform to pooled on its first
  differing write (`0x49F2D0` takes a block from the free list or appends one); nothing found turns a pooled tile back
  except the clear `0x497080` at creation and shutdown.
- **Change event**: every stamp posts event type **0x20** on the EXE's 4 MB queue with the stamp's bounding box (min x, min
  y, max x, max y as floats) and its bit mask; the creation posts type **0x1D** [C]. The tap cannot listen to the queue
  without calling EXE code, so it watches the grid itself: a copy of the header, directory and pool per poll, compared with
  the last published copy; its generation moves when the bytes differ (14.3).
- Sizes: Tiny (samples 4,096 x 2,240 [I from 4550's 9,175,040]) 2,048 x 1,120 cells, 128 x 70 tiles, a 71,680-byte
  directory; huge (44,040,192 samples) 4,096 x 2,688 cells [I: sample counts not split], 256 x 168 tiles, 344,064 bytes; the
  pool's size is measured live.

### 14.3 The tap and the reader [C, tests]

(details: `shell-composite-taps.md` section 7)

- `shell/composite/cut_tap.{h,cpp}` in `shell_composite.dll`: a below-normal worker thread polls the grid every
  `cut_poll_ms` (500): the GridHeightmap pointer, the grid's header, the sample counts and origin, the directory and the
  pool, the header again; checks (sizes multiples of 16, tiles = cells / 16, roundup16(samples / 2) = cells, the origin
  finite, states 0 / 1, pool indices below the blocks, pooled tiles = in-use count); a copy whose bytes differ from the
  published image and stay for `cut_settle_polls` (2) polls is published as image section `Local\VPShellCut.<pid>.<gen>`
  with control block `Local\VPShellCutTap.<pid>` (seqlock, ack fields). 13 byte patterns checked first (FAILED PATTERN for
  good), SEH on every read (FAILED EXCEPTION), no EXE call, no EXE write. The change event 0x20 is not listened to (EXE
  code); the watch replaces it. Switch `cut_tap` (code default 0: OFF published, no worker) for lead-0431 to set to 1.
- `server/src/render/srv_cut_tap.{h,cpp}`: `CutTapReader`, a `c5r::forest::CutSource` over the newest accepted image
  (`DenseCut::setFromBlocks`); install failure = producer FAILED or the first image refused (the scene's exit 5); in play a
  refused or torn image is LOUD (20, then every 100th), counted, the last good grid kept; OFF drops the grid (LOUD once);
  NO_GRID keeps it. It includes `../../../shell/composite/cut_tap.h` (no include path); 4554 adds `srv_cut_tap.cpp` to
  `server/build_server.py`.

### 14.4 Tests

`shell/tests/test_cut_tap.py` 26 cases, plain 26/26 (15:56) and MSVC ASan 26/26 (15:59);
`server/tests/render/cut_tap/` 31 cases, plain 31/31 (15:49) and ASan 31/31 (16:00); 0 skipped, 0 ASan reports.

### 14.5 P4 live: the grid is stamped with Forest off, and the Forest key changes nothing in it [L]

(oldpc `tb1`, product
path, jobs `20261116-170924-f98e` and `20261116-172903-79b4`; `shell-composite-taps.md` 7.3; `render/evidence/4553/control_diff.out.txt`)

- Under the product's D33 (Forest off) the grid is made and stamped exactly as with Forest on: huge generation 1 at the
  load holds the river mesh's 0x09 (4,094 pooled tiles), then about 40 s of landmark, farm and city stamps (about 10,160
  pooled tiles; bits 0x01 654k, 0x02 926k, 0x04 5.8k, 0x08 341k, 0x10 392k cells on huge) [L, both arms].
- Forest-on against D33 at the census after the load differs in bits 0x01, 0x02, 0x10 only (huge 282,198 / 285,732 cells;
  Tiny 4,981-7,839, bit 0x02 only); two sessions of the **same** arm differ by the same amount in the same bits (huge
  281,438 and 286,968; Tiny 6,963 and 7,084); bits 0x04 and 0x08 never differ. The difference is the session's randomness
  (farm-field and landmark variants use `rand()`, cities-and-landmarks.md 2.6, 2.7), not the Forest key: **the Forest-off
  grid equals the Forest-on grid up to what two sessions of one arm differ by** [L]. Consequence for 4554: only the
  session's own grid is right (a recorded one is not), which the live tap provides.

### 14.6 The proof with the tapped grid [L]

(`render/evidence/4553/proof_4553.py` + `cut_proof.exe` = 4550's proof mode
with the `cut IMAGE` input read by server.exe's reader; `run_proof.sh`; outputs `proof_*_census.out.txt` and the control
`proof_*_empty.out.txt`). Job f98e's ct arm (Forest on, forest tap, cut tap, one session), the cut image copied at each
census, the height fields and plot tables of 4550 (Tiny 4308 bundle3; huge scene-library bundle of 09:02, save c499d3c6):

| set | built plots | exact with the tapped grid | exact with an empty cut (control) | trees hidden by the cut |
|---|---|---|---|---|
| Tiny after the load (cut gen 4) | 94 | **94** | 41 | 1,222 |
| Tiny after the tour (gen 4) | 166 | **166** | 48 | 3,457 |
| Tiny after 2 AI turns (gen 5) | 166 | **166** | 48 | 3,457 |
| huge after the load (gen 4) | 97 | **97** | 46 | 1,758 |
| huge after the tour (gen 4) | 324 | **324** | 60 | 13,165 |
| huge after 2 AI turns (gen 16) | 275 | **275** | 60 | 10,061 |

Every built plot is exact (drawn count equal, rectangle within 0.01) on both saves at every census: no "more", no "fewer",
no "rect". The empty-cut control reproduces 4550's picture (the "more" plots by the coast, rivers, routes, improvements);
with the grid they are all exact, so 10.5's "coast and rivers" are the cut grid's stamps (river mesh 0x09 and the others)
[L]. Many trees that the empty run hid as "water" are hidden by the cut first (huge tour: water 1,303 -> 111) [L].

### 14.7 Not verified, open
: the reader inside server.exe (4554, which adds `srv_cut_tap.cpp` to `server/build_server.py`
and sets `cut_tap=1`); the tap's cost to the EXE (a below-normal worker, 9-10 ms per 500 ms poll on huge, max 14.6 ms);
which caller writes CreateTerrain's replay list (0x467C60) and the systems of `0x5BF9A0`, `0x766A70`, `0x5DB6B0` [I].
For the lead, other files: cities-and-landmarks.md 2.7's open "who consumes the type-0x20 mask events" is answered (the
Forest's listener `0x715720`, bits & 7) and its bit table holds live (bits 0x01, 0x02, 0x04, 0x08, 0x10 all seen; 0x20-0x80
never set on these saves).

### 14.8 Torn reads during the growth after a huge load are not LOUD (ticket 0500, 2026-11-17) [L, C, tests]

0414's oldpc job `20261117-084445-3bce`, arm 7-sleep (save TB-HUGE_0350, product path, cut tap on at 500 ms polls; the
arm's `shell_composite.log` in `F:/c5-64bit-work/0414/oldpc-3bce/out/7-sleep`), read 2026-11-17: load 3 at 11:34:46.4,
generation 1 at 46.95 (4,094 pooled tiles, the river mesh), then the stamps' growth to generation 4 at 11:35:25.5 (10,161
pooled tiles; 78 polls, 12 refused, every refusal a torn read; the last torn read 11:35:22.5): **about 39 s of growth**,
as 14.5's 40 s [L]. Four of those torn reads came in a row (11:35:03.1-04.6, 2 s) and met the old rule (the same reason
`cut_loud_polls` = 4 polls in a row: LOUD, meant for a layout the tap does not know); the next good read published
generation 2 at 11:35:06.2. The other 11 arms and the warm-up had 1-8 torn-read runs each, none 4 long [L]. A torn read
(the header +0x00..+0x2F not byte-equal before and after the copy: a stamp took a pool block meanwhile) says the EXE is
writing, not that the layout is unknown. **Rule since 0500** (`shell/composite/cut_tap.cpp`, `ReadGrid` result 4): a torn
read has its own run; it is counted (`refusedReads`, and "(N torn)" in the worker's last line), logs one plain line per
run, ends any other reason's run, and is LOUD (once per run) only when every poll for `cut_torn_loud_s` (default **120 s**,
at least `cut_loud_polls` polls) was torn: three times the whole growth after a huge load, while in 0414's 13 huge loads the
longest stretch of torn reads without a good read between was 4 polls (2 s), so a run that long means the EXE never settles (or the header
holds a field that changes on its own, a layout question again). The other reasons keep `cut_loud_polls`' rule. Tests
(`shell/tests/test_cut_tap.py`, test mode `cut_test=torn` writing the header's free head during the copy for
`cut_test_torn` polls): 30 torn polls in a row give no LOUD and the next good read publishes generation 1 (the image
byte-equal to the test grid, `refusedReads` 30); torn at every poll with `cut_torn_loud_s=1` gives one LOUD and no image.
Suite 28/28 plain (26 before + these 2; MSVC build, 2026-11-17 13:05). No game run made [C, tests].

### 13.9 The cut tap in the scene, and its default (item 3, 2026-11-16 18:00-18:15) [C, tests]

The cut mask is ticket 4553's (section 14). In the scene:

- **Who reads**: `ForestScene` makes a `CutTapReader` once the EXE's pid is known (`SceneForest::setExePid`, which
  `Scene::poll` calls on the live path with `exePid()`, the main section's tap blocks, as the decal reader gets it). A new
  pid gets a new reader. The reader becomes the placement's cut source (`setCutSource`). Where no pid comes (the offline
  player, scene tests without a section), there is no reader and the source is `EmptyCut`.
- **A new grid generation** (`poll()` returns true when the reader's generation moved): every plot built so far is built
  again at once with the new grid. Stock refreshes every record on a cut change (2.2). The plot's Tree_Shadow decal
  follows: a decal whose content hash is unchanged is kept and sends no change; a changed decal is replaced; a decal with
  no drawn tree left is dropped. Each of these is part of the next `takeDecalChanges`.
- **Failures** (D118, D125):
  - The reader's `installFailure()` (the producer FAILED, or the first image refused) is the scene's install failure:
    "the cut-mask tap could not install: <the reader's reason>", then `ForestFatalStep` -> exit 5 -> the host's report and
    popup.
  - No grid within `cutWaitMs` (20,000 ms; server.cfg `render_test_cut_wait_ms`, 0 = no bound) after the first field is
    also an install failure. It names the reader's state (ABSENT: a composite without the cut tap; WAITING; NO_GRID).
  - The tap OFF (`cut_tap=0`, a test switch) is never a failure: the reader logs it LOUD once and the trees are placed
    with an empty mask.
  - In play the reader counts and logs a refused image and keeps the last good grid (14).
  - `render_test_cut_fail=1` (server.cfg) makes the reader's install failure happen on purpose when the pid is known: one
    negative launch for 4555 (exit 5, the report).
- **Lines**: the 1-second line adds `| cut=<NO_PID|ACTIVE|OFF|WAITING> gen= accepted= refused= changes= rebuilds=`.

**The default (a product-path change).** Granted by the MC (queue #2985, under #2973's three conditions):
`shell/composite/shell_composite.cpp`'s `Config::cutTap` default goes 0 -> 1, and only that line and its comment changed.
The shipped `shell/composite/shell_composite.ini` has no `cut_tap` key, and the proxy reads only `model_fow` from that ini
(`proxy/src/proxy_shell.cpp` 1693-1702). So the product path takes the code default, and nothing writes 0 into it.

The three conditions:

1. **Unchanged where no tree is drawn.**
   - `render_forest=0` and `render_forest=1` without feature ids draw bit-equal frames (`scene_wiring`, 0 of 76,800
     pixels differ). `render_forest=0` creates no forest scene and no reader.
   - The scene-bundle suite (`sceneshot` on the library's bundles, format 4, whose render_config carries no
     `render_forest`, so the player draws without trees) passes unchanged: 626 / 162 / 163 / 24 checks (13.5).
   - The stock route never loads the composite: the proxy loads `shell_composite.dll` only on the product path, after its
     SHA-256 check (`proxy_shell.cpp` lines 15, 321-378). On the product path the tap only reads the EXE's grid (its
     worker guards its reads, 14) and writes its own sections, so the EXE's picture does not change. *[I for the live
     picture: 4555's run.]*
2. **A grid that cannot be read or validated fails loud**: `cut_tap_failures` covers FAILED, a first image with a wrong
   hash, an absent tap past the bound, and `render_test_cut_fail=1`: each gives fatal(5) once with a reason naming the
   cut tap. OFF and "no pid" never fail.
3. The tap's per-frame cost is 4555's measurement.

The cut tap's own tests after the flip: `shell/tests/test_cut_tap.py` (13.5). It drives `cut_tap.cpp` through
`cut_tap_host.exe` with its own switch file, so no test asserts the composite's default and none needed a change.

## 15. Live acceptance (ticket 4555)

Worker W5 of epic 0431, 2026-11-16 from 19:06 EST, branch `task-4555-forest-live` (from `epic-0431-trees` 8967bace). Route A
live on oldpc's testbed (`tb1`, the host's own EXE; Vibrant Textures present, D94), the D33 product path of
`tools/testbed/jobs/product_path.py` with Forest still in the D33 list: the EXE draws no trees, server.exe draws them
(`render_forest=1`, its default), the composite reads the cut mask (`cut_tap=1`, its default since 4554).

### 15.1 Method

- **Builds** (`render/evidence/0431/4555/build_4555.sh`, workstation, each through its gate): server.exe, the proxy and
  `vp64helper.exe` release from this branch (`server/build_server.py`, fork source and release deps of the pin 04b1211b8:
  own copies of 4652's source and 0439's deps, made after 0325's copies were removed by their owner during the first
  build, 2026-11-16 19:17-19:20); the composite (`shell/build.cmd`); the same three from the epic's merge base with main
  `e683e70f` (arm m: the product before the epic); the four x64 libraries of the pin layout `x64-04b1211b-pfdee656`
  (COMMON 18). SHA-256 prefixes in 15.2.
- **Driver** `render/evidence/0431/4555/forest_t5.py` (on 0420's `forest_t0.py`, imported for the decal watcher, the
  back-buffer dump and the popup check; prep, stop and restore by hash through `product_path.py`). Arms: **a** the product
  path; **n** a with `render_forest=0` (vpproxy.ini `server_cfg_extra=`, TEST ONLY: the comparison arm); **n0** n with
  `cut_tap=0` (the tap-cost pair n / n0); **m** the merge base's server, proxy and composite (MC #2973 condition 1);
  **s** stock (the instance's own config.ini, Forest on, no proxy, the instance's pinned 32-bit DLL
  `i0ship-26234371-ia`), pictures by PrintWindow with the window on screen (quiet-testbed.md 21.3). Every launch with
  `--park none --assert-policy hold`. Per camera: UI.LookAt, popups closed (D114), the EXE's camera read before and after
  the pictures (`shell/tools/gt_reader.py`; a moved camera fails the arm, E9), units asleep and the auto unit cycle off.
  Product arms: the composite's back buffer (a: ACCEPTANCE; a also by PrintWindow, DIAGNOSTIC); stock: PrintWindow.
  Cameras: 4309's (Tiny w1 33,14, w2 51,14, s1 42,12, x1 55,14; huge s00 101,25, s01 111,25, s03 101,35, s06 84,31,
  s15 96,34, s24 90,16) plus the forest cameras of 9.6 (Tiny 13,27; huge 94,39 border, 76,41 fog).
- **Jobs** (`render/evidence/0431/4555/submit_4555.sh`): smoke (Tiny a, w1), func (Tiny s,a,n,m,s; huge s,a@30,n,s with
  the tour; huge a plays 30 AI turns after its tour), quiet (huge, warm-up a, then a / n / n0 in six rotations, 45 s idle
  windows at the start camera and at 94,39, `WaitForVSync = 0` backed up and restored by hash as 8.7), neg
  (`render/evidence/0431/4555/neg_cut_job.py`: `render_test_cut_fail=1` on the shipped pack layout of
  `release_d125.py`, instance tb2).
- **Analysis** `render/evidence/0431/4555/analyse_4555.py` (func, quiet). The compare: COMMON 25's figures (median, p95,
  max of the largest channel difference, share within 16, a difference image) of a against stock session 1 at the same
  camera, split into the **trees** class (the RANDOM class: where a differs from n by more than 24, dilated 6 px) and
  the **rest**, beside stock's own ceiling (session 2 against session 1). `compare_4309.py`'s plot classes were not used:
  they need 4309's workstation camera blocks and heights at 2554 x 1411; these pictures are oldpc's at its own size.
- **Expected** (design 9.6): trees in a where stock has them, not tree for tree; the border lines and the territory wash
  over the trees in a as in s (route B hid them, 8.4); `forest: feature ids resolved: forest 5, jungle 1` (13.3);
  Tree_Shadow decals one per built plot with drawn trees; huge merged area bits near 3,015-3,023; 0 unexpected LOUD lines.

### 15.2 Builds shipped (SHA-256 prefixes) [C]

| Part | This branch (8967bace, epic HEAD; arms a, n, n0) | Merge base e683e70f (arm m) |
|---|---|---|
| server.exe | `b054f577` | `edaa9ed1` |
| proxy `CvGameCore_Expansion2.dll` (x86) | `af038192` | `1b6467f0` |
| `vp64helper.exe` | `8214af42` | `0879ce25` |
| `shell_composite.dll` | `655eb0f0` (cutTap 1 by default) | `6f7ad3e6` (no cut tap) |
| x64 DLL and libraries | pin layout `x64-04b1211b-pfdee656`: DLL `29f58e17`, lua51_x64 `30724d6d`, CvGameDatabase_x64 `9c37b39e`, CvLocalization_x64 `35863ede` | the same |

Stock arm s: tb1's own pinned 32-bit DLL `i0ship-26234371-ia` (`3c2f47da`, set by 0446 at 18:03). Saves: `TB-TINY_0009V`
(`27420a41`, shipped with the job) and the host's `TB-HUGE_0350` (`c499d3c6`), each run on a copy `TBPP-...`. The D125
pack `4555-d125`: x86 = Release-5.4.8's committed DLL `262aee88`, x64 `29f58e17`, purity `7b1a91fb`, save `TB-TINY_0008`.

### 15.3 MC #2973 condition 2: a cut grid that cannot be read ends in D125's route, never silently [L]

Job `20261116-193234-7ea0` (oldpc tb2, 2026-11-16 19:37-19:41 EST; `neg_cut_job.py --fault cut` over
`release_d125.py` unchanged, fetched to `F:/c5-64bit-work/4555/oldpc-7ea0`): the shipped pack layout (pack=1,
config.ini in the A'' form by `vp64helper on`, fallback_dll = the tag's DLL) with `render_test_cut_fail=1` through the
TEST ONLY `server_cfg_extra=`. server.log +21.320 s `LOUD: forest: install failure: render_test_cut_fail=1: the cut-mask
tap's reader fails on purpose (a negative launch of the install failure) (render_forest=1 cannot draw the trees; the host
ends the game, D118 / D125)`, then `forest: the 64-bit path cannot go on: ...; the server ends (exit code 5)` and
`remote: exit code 5`. The proxy wrote one report folder (no `.dmp`): `report.txt` stage `server-death`, `server pid
12764: exit code 5`, its last FAILED line names the cut-mask tap; the EXE ended by itself 77.1 s after the launch (the
failure came in the front end, at `ICvPreGame1::civilization`, before any map), no "Game Crash" box; `vp64helper failed`
restored config.ini (SHA-256 = the player's), wrote failed=1, no side file, the popup on screen 2 s later (Visual Checks
`vc-20261116-193827-oldpc-a60a`, DIAGNOSTIC: the fault is a test key). **15 of 15 checks**, the instance restored by hash
(backup restore ok, provision verify rc 0), the host's My Games unchanged, job exit 0. Condition 2 holds live.

### 15.4 Finding: every server tree is drawn with style entry 0 (Generic), so America forests are green where stock's are autumn-coloured [L, C]

Job `20261116-193200-8ad2` (Tiny a, n, m) against the stock sessions of job `20261116-194821-957c` (Tiny s, s), the same
cameras and machine; fetched to `C:/c5-64bit-work/4555/func-partial` and `stock-partial`, figures
`C:/c5-64bit-work/4555/figs-partial/`.

- **What is seen [L].** On America plots (art type 1 in 4309's census: Tiny 13,27, 12,26, 14,28, 60,14, 62,16) stock's
  forests are autumn-coloured (orange-brown); the server's are green (camera `forest_13_27`, and the east of `x1_seam_55_14`).
  On Asia plots (art type 2: 42,12, 51,14; cameras w1, w2, s1) both are green and look alike. In numbers (largest channel,
  the tree class, a against stock session 1; stock's own session 2 against session 1 beside it): forest_13_27 median 38
  / p95 99 against stock's 22 / 78 (arm n, no trees at all: 30 / 102); the Asia cameras 19-24 / 77-102 against 9-19 /
  67-95.
- **Why [L, log; C, code].** The server's render.log of every arm: `residency: tile styles of load seq 3: art types 56x36
  from none yet (all Generic) (table generation 0), tiles Generic 2240 America 0 Asia 0 Africa 0 Europe 0`, and at the
  arm's end the terrain line still has `art_fills=0 plots_changed=0`: the feed's plot table came after the field
  (`ResidencySource::onField`, `srv_residency_source.cpp` 246-251 takes it only when it is there), and the only later
  path that takes it, `onPlotsChanged` (lines 399-432), never ran (no plot changed in the session). So every terrain tile
  keeps style 0, `ForestScene::styleAt` (`server/src/forest/srv_forest_scene.cpp` 731-749, `terrain.tileStyleAt`) returns
  0 for every plot, and every block is drawn with entry 0: `forest_europe_2` / `forest_overlay_europe_2` (mean RGB 106, 108,
  65, alpha 78) instead of America's overlay (147, 113, 40, alpha 130, the autumn chroma the tree PS mixes in by the
  overlay's alpha, 2.4). The America overlay's colour is the stock picture's colour; the europe overlay's is the server's.
  Sure: high for "all entries 0" (the log's counters); high for "this is the autumn difference" (the overlay colours and the
  art types of the plots agree; not proven by a picture with the styles set).
- **For the lead (a defect outside the forest scene, and one inside).** (1) The residency's art types (issue 4305's
  intake) are not taken when the feed's first table arrives after the field and no plot changes follow; the trees depend
  on them through `tileStyleAt`. Either the residency re-reads the table when its generation moves (a poll, not only on
  `onPlotsChanged`), or the forest scene takes the style from the feed's `PlotRecord::artType` of the plot itself (it
  already copies that table, 13.2), with `bake::tileStyle`'s rule. Ticket proposal: "server trees and terrain styles:
  take the art types when the feed's table comes after the field" (owner: the residency's, issue 4305's area; the
  forest scene's part is lead-0431's). (2) Latent, inside the forest scene: `ForestScene` fills `fr.colorKeyLut[i]` and
  `fr.fowLut[i]` for i = 0..4 in the server's art-type order (`kStyles` Generic, America, Asia, Africa, Europe;
  `srv_forest_scene.cpp` 944-948 and the build loop at 281-305), but `c5r::forest::Renderer::draw` indexes them with
  `lutIndex(entry)` = {0, 3, 2, 4, 1} (`render/src/forest/draw.cpp` 115, 668-715), the EXE's slot order of
  `[0xD90E4C + 4i]` (Generic, Europe, Asia, America, Africa). With styles set, an America plot would get Africa's
  ColorKey and TerrainFOW volume, Europe America's, Africa Europe's. In this install every Terrain and TerrainFOW volume
  maps tree colours identically (`render/evidence/0431/4555/lut_check.out.txt`), so it changes no pixel here; another mod's
  style volumes would show it. Fix: fill the arrays at `lutIndex(s)` (or index by entry in the draw). Not fixed here (the
  worker's brief: describe, do not fix).
  *Lead-0431, 2026-11-16 23:09: point (2) is not a defect, as far as the knowledge base says [C]. The table `[0xD90E4C + 4 i]`
  is in the ColorKey system's style order Generic, America, Asia, Africa, Europe (`asset-pipeline.md` 3.5: `0xd90e4c + type
  x 0x14 + style x 4`, styles at `0xb9a428`), which is the order the scene fills (`kStyles`); the draw's `lutIndex(entry)`
  then gives an America tile's trees Africa's volumes, which is what stock does (11.1: "an America tile's trees take the
  Africa LUTs"). The "slot order (Generic, Europe, Asia, America, Africa)" above is not supported by a source; unchanged
  code, no ticket.*

### 15.5 Functional run: the trees, the borders and the wash over them, the census [L]

Jobs `20261116-193200-8ad2` (oldpc tb1, 19:32-20:12 EST: Tiny a, n, m; huge a with 30 AI turns, n; its stock arms were
refused by the driver's own check, 15.8) and `20261116-194821-957c` (tb2, 19:48-20:10: the stock sessions, Tiny s, s and
huge s, s), fetched to `F:/c5-64bit-work/4555/oldpc-8ad2` and `oldpc-957c`; analysis
`render/evidence/0431/4555/func_8ad2.out.txt` (`analyse_4555.py func ... --stock ...`), figures `F:/c5-64bit-work/4555/figs/`
(per camera a | stock side by side, |a - stock| x 4 with the tree class outlined, the border crops, n - m). Every product
arm: in game (Tiny 90-109 s, huge 134-160 s), **0 unexpected LOUD lines** (n: the 2 TEST ONLY lines of `server_cfg_extra`),
the camera unchanged at every camera, no popup open (0 Esc rounds), restored by hash. Pictures 1914 x 1051 (both arms).

- **The trees [L].** Drawn at every camera with forest or jungle (Tiny 5, huge 8). Per camera, largest channel, a against
  stock session 1, with stock session 2 against session 1 (stock's own ceiling) and arm n (no trees) beside it, median /
  p95 in the tree class (the RANDOM class, 15.1):

  | Camera | a | stock ceiling | n (no trees) | tree share |
  |---|---|---|---|---|
  | huge s00 101,25 | 15 / 88 | 13 / 90 | 18 / 106 | 0.23 |
  | huge s01 111,25 | 18 / 89 | 17 / 96 | 23 / 116 | 0.18 |
  | huge s03 101,35 | 16 / 76 | 16 / 79 | 22 / 88 | 0.41 |
  | huge s06 84,31 | 17 / 68 | 16 / 69 | 28 / 95 | 0.39 |
  | huge s15 96,34 | 18 / 77 | 18 / 79 | 30 / 100 | 0.48 |
  | huge s24 90,16 | 12 / 71 | 9 / 72 | 8 / 67 | 0.08 |
  | huge 94,39 (border) | 17 / 70 | 16 / 70 | 36 / 111 | 0.55 |
  | huge 76,41 (fog) | 10 / 36 | 10 / 36 | 26 / 59 | 0.51 |
  | Tiny w1 33,14 (Asia) | 23 / 102 | 19 / 95 | 46 / 125 | 0.24 |
  | Tiny w2 51,14 (Asia) | 20 / 77 | 14 / 67 | 34 / 103 | 0.29 |
  | Tiny s1 42,12 (Asia) | 19 / 96 | 9 / 92 | 30 / 108 | 0.11 |
  | Tiny x1 55,14 (part America) | 24 / 80 | 17 / 67 | 33 / 101 | 0.33 |
  | Tiny 13,27 (America) | **38 / 99** | 22 / 78 | 30 / 102 | 0.33 |

  On the huge save the server's trees are at stock's own ceiling at every camera (the medians within 0-3, the p95 within
  0-7 of stock against stock), fogged forest included (76,41: equal). On Tiny the Asia cameras are 4-10 above the ceiling
  in the median, and the America plots are wrong in colour (15.4): there the trees are farther from stock than no trees
  at all. Outside the trees (the "rest" class: ground, models, interface) a against stock is median 0-3 / p95 8-22 at
  every camera, the same as arm n (the server's terrain, not this epic). Water is a class of its own (animated, never
  equal between sessions).
- **Borders and the territory wash over the trees [L]: as stock.** Saturated pixels (border lines and wash) in the tree
  class of the central crop: huge 94,39 a **25,396** against stock 27,880 / 29,311 (n, the ground without trees, 132,536);
  Tiny w1 a **11,519** against stock 13,512 / 12,622 (n 33,689). The crops (`*_border_a_s1_n.png`) show the cyan and purple
  border lines running over the jungle in a as in stock, where route B covered them (8.4, VC `-121948-583f`). This is the
  point of the epic, and it holds.
- **Census [L].** `forest: feature ids resolved: forest 5, jungle 1 (by art define, ...)` in every a arm (13.3's [I]
  confirmed). Records: Tiny 180 (= stock's tap, 8.8), huge 690 (= 8.8's), 657 after 30 AI turns (33 removed, 0 added).
  **Tree_Shadow decals = built plots with drawn trees at every second** (`shadows=` equals `built_total=`: Tiny 155 after
  the tour, huge 247 at most, 236 at the end). The load's marks: Tiny 433 tiles of 180 plots, huge 1,704 tiles of 690
  plots. **Merged area tiles**: huge, the load's tap set (generation 3, 17,642 decals) has the EXE's 2,722 area tiles
  (D33) and the forest's marks add **+296 = 3,018**, inside stock's 3,015-3,023 (12.4); after 30 AI turns 2,762 + 272 =
  3,034 (no stock count after 30 turns). Tiny: 285 + 247 = **532**, inside stock's Forest-on 522-541 on this save (8.5).
  The EXE's decal tap carries **0** Tree_Shadow decals in every product arm (Forest is off in the EXE), so the server's
  are the only ones (13.8's refusal rule never fired: `tap_shadow_refused=0`).
- **The cut tap live [L].** `cut=ACTIVE`, 0 refused images; Tiny 4 generations (grid 1,792 x 1,280 cells, 112 x 80
  tiles), huge 192 generations in the session of 30 AI turns (grid 4,096 x 2,688 cells: 14.2's [I] huge size confirmed;
  *corrects 14.2's Tiny "2,048 x 1,120 [I]": Tiny9v is 1,792 x 1,280 cells, samples 3,584 x 2,560*), **45,644 plot
  rebuilds** over those 192 grid changes (every built plot again per change, 13.9), 0 build or draw errors. The tap's poll
  time on the generations it published (the composite's READY lines; below-normal worker, not a timing series): Tiny mean
  1.26 ms (max 1.93), huge **11.1 ms (max 29.1)** over the 30 turns, 8.4 ms (max 22.3) in arm n's load and tour.

  The Tiny Asia gap has the same cause as 15.4 [I]: entry 0 draws Europe's overlay (alpha 78) where stock draws Asia's
  (alpha 117). On the huge save the art types arrived 1 s after the map was up (`art_fills=1` at 19:45:44, in game at
  19:45:43, the tour from 19:46:03), so its trees had their styles; on Tiny `art_fills=0` all session.

### 15.6 MC #2973 condition 1 live: render_forest=0 draws the frame before the epic [L, with its limit]

Arm n (this branch, `render_forest=0`, cut tap on) against arm m (server, proxy and composite of the merge base
`e683e70f`), Tiny, the five cameras, the composite's back buffer, two sessions (`*_diff_n_m_x4.png`): **on the ground,
79-89 % of the pixels are bit-equal and 96-98 % within 16** (median 0, p95 3-11); the rest are the EXE's own models drawn
in random variants per session (city and landmark blocks: three blobs at w1) and the coast foam; the water differs
everywhere (its animation runs on the server's clock; never equal between two sessions). Nothing of the server's ground,
borders or wash differs (`w1_nm_land.png`-style view in `C:/c5-64bit-work/4555/` during the run, not kept). A live frame is
never bit-equal between two sessions, so this is the live form of the condition; the bit-equal proof is 4554's offline one
(`scene_wiring`, 0 of 76,800 pixels, 13.5). In arm n the server logs `forest: render_forest=0: no trees (the picture
before epic 0431)` and makes no forest scene and no cut reader; the composite's cut tap still runs (its cost: 15.9).

### 15.7 Stability: 30 AI turns on huge in arm a [L]

Job 8ad2, huge a, `--assert-policy hold` declared at the launch: tour, then **30 of 30 AI turns** (turns 350-379, 28.0 s
mean per turn from end turn to the next human turn, 22.4-31.7; wall time on oldpc with tb2's stock job running beside it,
not a timing claim), **no crash box, no dialog marker (dialogwatch rc 0), no D118 report** (server.exe alive at the end,
0 LOUD lines in server.log, render.log, vpproxy.log and shell_composite.log, no popup), **0 forest LOUD lines** (9.5's
in-play statuses never fired: build_err, rec_err, ovl_err, draw_err, merge_err, shadow_err all 0). Memory of that one
run: EXE private 1,379 MB at the load, 1,393 after the tour, 1,522 after the turns (largest free block 2,013 -> 1,901 MB);
server.exe 1,661 -> 1,789 MB. The forest followed the game: 33 forest and jungle plots lost their trees, 192 cut-grid
generations (routes, improvements) rebuilt the built plots 45,644 times, the shadow decals followed (236 at the end).

### 15.8 What went wrong in the runs (for the next driver)

- **Stock's own config.ini is not empty**: it disables `OverlayDebug` and `ColorKeyEdit` (`shell/tools/darkred.py`
  `STOCK_OFF`). The driver's first stock check refused any disabled key, so job 8ad2's four stock arms ended in prep
  (exit 2, nothing changed); fixed to compare with `STOCK_OFF`, the stock sessions ran as job 957c on tb2.
- **A stock arm has no run folder of its own**: `product_path.newest_run` found the previous job's (the D125 negative's on
  tb2, whose `Logs/VP64/run` folder stays after its restore), so 957c's stock arms copied that run's logs and ended with exit
  5 for its two expected LOUD lines. The pictures and restores were right; the analysis takes stock arms by their pictures
  (`stock_ok`), and the driver no longer looks for a run folder in a stock arm.
- **Two sessions are never bit-equal live**: the water's animation and the EXE's random model variants differ, so a
  compare of two live frames needs a water class and a model allowance (15.6); a tree class defined as "a differs from n"
  must leave the water out (the first analysis counted the sea as trees).

**15.4 confirmed live (job `20261116-212642-80d0`, 2026-11-16 21:27-21:31 EST, tb1, Tiny a with one AI turn before the
cameras, `--turns-first`)** [L]: at the load again `art types ... from none yet (all Generic)`; during the AI turn the
feed's plot changes let the residency take them (`art_fills=1` at 21:29:16; the turn 21:29:12-21:29:19); the pictures from
21:29:36 show the America forests **autumn-coloured as stock's** (figure
`F:/c5-64bit-work/4555/figs/style_check_13_27_before_after_stock.png`: before | after | stock). Tree class against stock
session 1 (`render/evidence/0431/4555/style_80d0.out.txt`), median / p95: 13,27 **23 / 88** with styles against 36 / 96
without (stock's own 21 / 78); x1 18 / 73 against 22 / 77 (stock 16 / 65); w1 (Asia) 21 / 105 against 21 / 97 (stock
17 / 92). So the cause of 15.4 is the missing art types, and with them the trees reach stock's ceiling on Tiny too, as on
huge. A player who loads a save sees Generic trees until the first plot change of the session (in practice the first
AI turn); on the huge save the art types came 1 s after the map. VC `vc-20261116-212936-oldpc-7ef0`, `-212949-oldpc-dd7f`,
`-213002-oldpc-cc3a` (ACCEPTANCE, pass, after one turn).

### 15.9 Memory and frame time: set 1 (job `20261116-194257-1251`, incomplete) [L]

oldpc quiet (alone; `undisturbed=False`, foreign cores 0.165, as every oldpc quiet job of the day), 2026-11-16
20:07-21:22 EST, huge `TB-HUGE_0350`, product path, `WaitForVSync = 0` (backed up and restored by hash), timing launches
(`--timing`, no frame cap, window on screen), warm-up a, then a / n / n0 in rotations of three; per launch: in game, 20 s,
EXE private bytes and largest free block (`memory.py`), server.exe private bytes, a 45 s idle window at the start camera
(`fps_idle.py --measure`, the Lua frame counter, 3 blocks), UI.LookAt 94,39 (dense jungle and forest, units asleep), 10 s,
a second 45 s window, server.exe private bytes again; the server's own per-frame times are its render.log means inside
each window (terrain `gpu_ms` / `cpu_ms`, forest `cpu_ms`). Analysis `render/evidence/0431/4555/quiet_1251.out.txt`.
**Launch 16 (arm n) crashed in the load** (15.10), so the set has 4 complete rotation blocks (a 5, n 4, n0 5 launches):
below PERFORMANCE.md's six per arm, so it is grade B; set 2 (job `20261116-212659-8674`, below) is the full set.

| (set 1, mean, sd) | a (trees) | n (render_forest=0) | n0 (n, cut_tap=0) |
|---|---|---|---|
| idle fps, start camera | 254.2 (1.9) | 250.8 (1.3) | 250.9 (0.6) |
| idle fps, 94,39 | 242.2 (1.2) | 248.1 (1.2) | 248.7 (0.9) |
| EXE private MB after load | 1,410.4 (1.8) | 1,411.2 (5.1) | 1,406.6 (2.5) |
| server private MB after the windows | 1,669.3 (1.0) | 1,635.6 (3.8) | 1,632.7 (4.3) |
| server forest cpu_ms per frame, 94,39 | 0.774 (0.005) | - | - |

Paired per block (4 pairs, 95 % interval): a - n frame time at 94,39 **+0.101 ms (+0.059 to +0.143)**, at the start camera
-0.051 ms (-0.104 to +0.002, not resolved); server private bytes after the windows **+33.6 MB (+26.2 to +41.1)**; EXE
private -0.8 MB (-11.3 to +9.8, not resolved; the EXE draws no trees). n - n0 (the cut tap): frame time +0.006 ms at
94,39 (-0.013 to +0.024), +0.003 ms at the start (-0.026 to +0.032), EXE private +4.1 MB (-2.2 to +10.5): nothing
resolved.

### 15.10 One EXE crash in a huge load (0410's signature) [L]

Job 1251, launch 16 (arm n, `render_forest=0`), 2026-11-16 21:19:54 launch: the EXE ended in the load with 0xC0000005 at
`CivilizationV_DX11.exe+0x37C6DA` (**VA 0x77C6DA**), thread 23408 (the proxy's boot thread), no box; the proxy's
`exe-crash.txt` and the server's `remote: LOUD: exe-crash: ... no report and no popup (auto: ... pack=(none: 0))`; the
dialog watcher marked it as a crash without a box (signature `exe-crash: 0xC0000005 EXCEPTION_ACCESS_VIOLATION at
CivilizationV_DX11.exe+0x37C6DA`). This is ticket 0410's crash (`research/02-exe-internals/crashes-huge-load.md`: the
EXE's farm-field weld loop at 0x77C1C0, in the load gap), not the trees' (arm n has no forest scene). Count in this
ticket: 1 of 37 huge product-path loads (func 2, set 1 16, set 2 19; arms a 14, n 12, n0 11), 0 in 2 huge stock loads.
The driver had treated the watcher's "dialog" answer as a box left open (exit 8: nothing restored); the arm was restored
by job `20261116-212433-eae8` (`restore_4555.py`: server_cfg_extra unset, `product_path.restore` by hash, provision verify
rc 0), and the driver now restores and goes on when the game is gone (forest_t5.py `crash_no_box`).

### 15.11 Memory and frame time: set 2 (job `20261116-212659-8674`, the result) [L, grade A]

The method of 15.9, 2026-11-16 21:33-23:04 EST, oldpc quiet (alone; `undisturbed=False`, foreign cores 0.172), all 19
launches in game and restored (exit 0 each), no crash; 6 launches per arm in six rotations of a / n / n0 after a warm-up a.
Analysis `render/evidence/0431/4555/quiet_8674.out.txt`; paired differences per rotation (6 pairs; t with 5 degrees of
freedom; "detectable" = the smallest difference the series detects at 80 % power).

| (mean, sd, n = 6) | a (trees) | n (render_forest=0) | n0 (n, cut_tap=0) |
|---|---|---|---|
| idle fps, start camera 101,25 | 252.0 (2.9) | 248.9 (2.1) | 248.3 (3.4) |
| idle fps, dense forest 94,39 | 240.8 (3.0) | 247.8 (1.2) | 248.1 (1.7) |
| EXE private MB after the load (+20 s) | 1,411.8 (2.5) | 1,411.6 (2.3) | 1,409.4 (4.2) |
| EXE largest free block MB | 1,970.4 (3.3) | 1,976.5 (8.9) | 1,990.9 (11.1) |
| server private MB after the load (+20 s) | 1,313.1 (33.1) | 1,460.2 (34.5) | 1,494.3 (9.5) |
| server private MB after the two windows | 1,668.1 (5.9) | 1,631.1 (4.4) | 1,633.5 (3.7) |
| server forest cpu_ms per frame (start / 94,39) | 0.707 / 0.775 | - | - |
| server terrain gpu_ms per frame (start / 94,39) | 0.341 / 0.251 | 0.342 / 0.251 | 0.342 / 0.251 |
| cut tap poll ms on its READY generations (mean / max) | 9.2 / 16.0 | 9.9 / 16.2 | - |
| load to in game, s | 135.7 | 135.9 | 134.2 |

**The server's trees (a - n):**
- **Frame time at the dense-forest camera: +0.119 ms per frame (95 % +0.079 to +0.159; detectable 0.055)**, 4.154 against
  4.035 ms (-7.1 fps). At the start camera, where little forest shows, a is **faster**: -0.049 ms (-0.082 to -0.017;
  set 1: -0.051, -0.104 to +0.002). Not explained [I: the start camera's frame is bound by the EXE, and the server's
  lockstep hand-over may interleave differently with the stage's extra work; the composite's lockstep CSV, not fetched,
  would show it].
- **The server's memory: +37.0 MB after the two idle windows (+30.4 to +43.7; detectable 9.0)**; set 1 +33.6 (+26.2 to
  +41.1). 20 s after the load arm a is 147 MB *lower* (-191 to -103): the server is still filling its residency then, and
  a fills later [I: the forest's load marks re-bake every marked tile once per load (13.7) and its decal changes come after
  the field]; the sample after the windows (about 120 s in) is the comparable one. Design 9.4 expected "a few MB on a huge
  map" for the CPU side plus 14.4 MB for the Screen Overlay target at 2554 x 1411 (here 1914 x 1051: 8 MB): the +37 MB is
  larger than that estimate [I: the 10 card VB / IB pairs, 20 textures, the five ColorKey volumes, the cut reader's dense
  grid (4,096 x 2,688 cells = 11 MB) and its image copy].
- **The EXE: +0.2 MB private (-2.7 to +3.1; detectable 3.9): unchanged**, as it should be (the EXE draws no trees; route B
  cost it +26 to +29 MB, 8.6-8.7). Largest free block -6.1 MB (-18.0 to +5.8), not resolved. Load time unchanged.
- The server's own frame: the forest stage's CPU time on the render thread is 0.71 ms per frame at the start camera and
  0.78 ms at 94,39 (its GPU time is not timed separately; the terrain's GPU time is unchanged, -0.001 ms).

**The cut tap (n - n0, MC #2973 condition 3; both `render_forest=0`, so the server reads no grid):** frame time +0.004 ms
at 94,39 (-0.020 to +0.028; detectable 0.033) and -0.009 ms at the start (-0.090 to +0.072): **no resolved cost**; EXE
private +2.2 MB (-3.5 to +7.9; detectable 7.8): not resolved; **EXE largest free block -14.4 MB (-26.2 to -2.6)**, resolved
in this set but not in set 1 (n 1,989.1 against n0 1,991.4, 4 pairs) [I: the tap's published image sections, mapped once
in the EXE's process per generation, 0.3-0.4 MB on Tiny and several MB on huge, and their churn over the load]. The
tap's own poll on its READY generations: 9.2-9.9 ms mean, 16 ms max on huge (below-normal worker, off the EXE's render
thread; 14.7's 9-10 ms confirmed), Tiny 1.3 ms; in 30 AI turns 11.1 ms mean, 29.1 ms max (15.5).

### 15.12 Acceptance of 4555, what is not verified, proposals (2026-11-16 23:10 EST)

| Ticket item | Result | Where |
|---|---|---|
| 1 driver, builds, arms | `forest_t5.py` (a, n, n0, m, s), builds 15.2 | 15.1, 15.2 |
| 2 pictures and compare | huge: the trees at stock's own ceiling at all 8 cameras; Tiny: at the ceiling once the art types are in (15.4), Generic-styled before the first plot change of a session | 15.5, 15.4 |
| 2 borders and wash over the trees | **as stock** (huge 94,39, Tiny w1; route B's defect gone) | 15.5 |
| 2 census | ids 5 / 1; Tree_Shadow decals = built plots; huge merged area 3,018 (stock 3,015-3,023), Tiny 532 (stock 522-541) | 15.5 |
| 3 30 AI turns, huge, a | 30 / 30, no box, no report, 0 LOUD, server alive | 15.7 |
| 4 memory and frame time | +0.119 ms per frame over dense forest (-0.049 at the start camera), server +37 MB, EXE unchanged | 15.11 (15.9 set 1) |
| 4 / MC #2973 c3: the cut tap's cost | no resolved frame time or private bytes; EXE largest free block -14 MB in set 2 only | 15.11 |
| 6 / MC #2973 c1: render_forest=0 = before the epic | holds live outside water and the EXE's random models; bit-equal offline (4554) | 15.6 |
| 6 / MC #2973 c2: grid failure -> D125 | 15 of 15, popup and report | 15.3 |
| 5 ACCEPTANCE pictures | 14 back-buffer pictures of arm a (13 of job 8ad2 + the smoke's), 11 pass, 2 fail (Tiny America, 15.4); 3 more after one AI turn (pass); `visualcheck.py list --issue 4555 --result undescribed` empty | below |

ACCEPTANCE Visual Checks (oldpc, pulled; arm a, the composite's back buffer, product path of 15.2, VT present):
pass `vc-20261116-193445-oldpc-8527` (Tiny w1), `-193459-oldpc-79fb` (w2), `-193513-oldpc-5a50` (s1), `-194615-oldpc-a48f`,
`-194628-oldpc-6c56`, `-194641-oldpc-40be`, `-194654-oldpc-1b87`, `-194707-oldpc-1d1c`, `-194719-oldpc-835f` (huge s00-s24),
`-194733-oldpc-70d8` (huge border 94,39), `-194746-oldpc-4da2` (huge fog 76,41), `-193056-oldpc-3be7` (smoke, w1); fail
`-193527-oldpc-194a` (Tiny x1), `-193541-oldpc-c06e` (Tiny 13,27); after one AI turn, pass `-212936-oldpc-7ef0` (w1),
`-212949-oldpc-dd7f` (x1), `-213002-oldpc-cc3a` (13,27). Each with stock session 1 of job 957c as its reference and the
side-by-side figure as its comparison.

**Not verified:** the forest stage's GPU time on its own (no timer: only its CPU time and the EXE's frame rate); panning
and the first-sight builds' cost; a fogged-forest picture on Tiny; the start-camera speed-up's cause (15.11); the workstation
GPU (all on oldpc's RTX 2080); stock's tile+0x300 count after 30 AI turns (to compare the 3,034); the jungle on Africa and
Europe plots (no camera on those art types in this tour) with styles set.

**Proposals for the lead** (ids by the MC, COMMON 26): (1) "Server trees and the terrain's styles: take the feed's art
types when its table comes after the field" (15.4; the residency, issue 4305's area; or the forest scene reads
`PlotRecord::artType` itself) - visible to every player on a load, until the first plot change; (2) "Forest LUT order:
fill `colorKeyLut` / `fowLut` at `lutIndex(style)`" (15.4, latent with VP's files); (3) driver hygiene for evidence
drivers: a stock arm reads no run folder, a box-less load crash restores and continues (15.8, done in `forest_t5.py`).

### 15.13 Follow-up: the residency takes the late table (MC #3242, 2026-11-16 23:13-) [C, tests; L below]

**The fix** (15.4's proposal 1; lead-0431's brief of 23:13). `ResidencySource::onFeedGeneration(snapshotGen)`
(`server/src/render/srv_residency_source.{h,cpp}`): when the feed's table generation moved and the field has no art types
from a table yet (the table came after the field) or only an earlier load's (`m_artStale`), the residency copies the
table (`copyPlots`, caller `kPlotsChanged`), takes its art types (`artFromTable`), restyles the tiles with their re-bakes
(`restyle(true)`) and sets `m_stylesChanged` (the scene's poll then gives the renderer the styles: `ApplyTileStyles`, so the
terrain's t6 / t7 and the forest scene's `styleAt` follow). It logs `residency: tile styles of load seq N after the field:
art types WxH from the feed's table (table generation G, WxH), tiles Generic .. America .. Asia .. Africa .. Europe ..`.
At most one copy per generation (`m_artPolledGen`); a field that has its table leaves later generations to
`onPlotsChanged` as before (the in-play rule `followArtChanges` unchanged); nothing else in the residency changed.
**Where it is called: `Scene::fowFeed(b)`** (`srv_render_scene.h`), the hook both the host's loop and the offline player
(`server/tools/sceneshot/sceneshot.cpp`) call for every drained batch, not `Scene::poll`: the batch carries the table's
generation, and the player reads a PLOTS record (caller `kPlotsChanged`) only inside the drain before it ("was not read by
the drain before it" otherwise), so a copy made in `poll` would put a recorded PLOTS record where the player cannot read
it. *The brief said "from the scene's poll"; this is the place in the drain's handling that both paths share.*

**Tests** (`server/tests/render/test_residency_source.py`, `render_host_test.exe` with new test switches `--feed-table
WxH`, `--feed-art A`, `--feed-rounds 0` in `server/tests/render/render_host_test.cpp`):
`test_table_after_the_field_takes_the_styles_without_plot_changes` (the field first, 'none yet'; then a 6 x 2 table, every
plot America, no plot patch: one "after the field" line from the feed's table, America tiles, `art_fills=1`,
`plots_changed=0`, `style_raises` > 0 and the same a second later: restyled once) and
`test_table_before_the_field_unchanged` (the table first, the field takes it in its own line, no "after the field" line,
`style_raises=0`). Without the fix the first fails by construction (no plot change, so `art_fills` stays 0 and the line
never comes, as 15.4's live log). Results: 15.14.

### 15.14 Follow-up results (2026-11-16 23:21 - 2026-11-17)

**Suites** (workstation, each through its gate; `render/evidence/0431/4555/followup/suites.out.txt`): `test_residency_source.py`
36 passed (its 34 and the 2 new; plain); the render host suite (`test_render_host.py`, `test_residency_source.py`,
`test_bake_yield.py`, `test_decal_names_mods.py`, `test_decal_wiring.py`, `water/test_water_host.py`) 126 passed; fow 3,
overlay 3, shadow and ice scene 6, water scene 27, decal 13, units 7, forest scene 3; scene bundles 626 / 162 / 163 / 24
checks, 4 programs passed (the library as this branch reads it; no format refusal, so main was not merged for it);
under MSVC ASan `test_residency_source.py` 35 of 36 with **0 ASan reports**, the two new tests passing; the one failure,
`test_radius_from_the_options_file_and_its_override` (no `terrain 1s` line inside its 3 s run), is a timing flake older than
the fix: alternated with the epic before it (7cee35bae, its own ASan build), base 2 of 3 and fix 3 of 3 passed. server.exe
`b6fcc94b`, proxy `992de6f3`, vp64helper `6f76197a` (release, this branch 268b3712, the pin 04b1211b8 as a git archive of the
fork clone and 0441's release deps; `build_server: OK in 111 s`).

### 15.15 Follow-up live: the art types at the load, America's forests autumn at once [L]

Job `20261116-233734-204c` (oldpc tb1, 2026-11-17 00:45-00:54 EST; it waited 70 min behind 0441's quiet job), the product
path of 15.2 with this branch's server.exe `b6fcc94b` and proxy `992de6f3` (composite `655eb0f0` unchanged: `shell/` has not
changed since), Tiny a and huge a with the tour, no AI turn; fetched to `F:/c5-64bit-work/4555/oldpc-204c`, ship
`F:/c5-64bit-work/4555/ship2`. Both arms exit 0, in game, 0 unexpected LOUD lines, every camera unchanged, restored by hash.

- **The load's lines [L].** Tiny: the field at 00:46:40.068 `art types 56x36 from none yet (all Generic)`, then 00:46:42.110
  `tile styles of load seq 3 after the field: art types 56x36 from the feed's table (table generation 1, 56x36), tiles
  Generic 1551 America 353 Asia 336 Africa 0 Europe 0` (2.0 s later, 2 s before the map was up at 00:46:44), and at the end
  `art_fills=1 plots_changed=0`: the case of 15.4 is fixed. Huge: the same order (the table came 4.0 s after the field this
  time): `Generic 7531 America 151 Asia 111 Africa 1509 Europe 1450` at 00:50:52.978, before the map (00:50:58). The style
  counts come from the same rule (`restyle`, `bake::tileStyle`) as a field that finds its table first, so a table-first load
  gives the same counts [C: one code path; not run live in that order here].
- **The trees against stock** (`render/evidence/0431/4555/followup/compare_204c.out.txt`; tree class median / p95 against
  stock session 1; before the fix = job 8ad2; stock's own ceiling = session 2):

  | Camera | after the fix | before | stock ceiling |
  |---|---|---|---|
  | Tiny 13,27 (America) | **23 / 81** | 36 / 98 | 21 / 79 |
  | Tiny x1 55,14 (part America) | **19 / 69** | 24 / 77 | 17 / 66 |
  | Tiny w1 33,14 | 21 / 93 | 23 / 99 | 19 / 95 |
  | Tiny w2 51,14 | 17 / 74 | 20 / 76 | 15 / 68 |
  | Tiny s1 42,12 | 20 / 104 | 16 / 93 | 11 / 90 |
  | huge, 8 cameras | 10-19 / 36-90 | 10-17 / 36-90 | 9-18 / 36-95 |

  America's forests are autumn-coloured at the load as stock's (figure: the job's `forest_13_27.png` beside
  `oldpc-957c/out/TB-TINY_0009V/s-n0/forest_13_27.png`). Tiny s1's tree class is 12 % of the frame and takes in the EXE's
  landmark in its middle, a different random model each session, so its numbers say little. Huge is unchanged (each
  camera within 2 of the first run).
- **ACCEPTANCE pictures** (pass, with stock session 1 as reference): Tiny `vc-20261117-004809-oldpc-f62b` (13,27, replaces
  the FAIL `-193541-oldpc-c06e`, which stays recorded as fail), `-004756-oldpc-9613` (x1, replaces the FAIL `-193527-oldpc-194a`),
  `-004716-oldpc-b94e`, `-004730-oldpc-16c3`, `-004743-oldpc-7bfb`; huge `-005129-oldpc-ebb0`, `-005144-oldpc-33ef`,
  `-005157-oldpc-3aad`, `-005210-oldpc-1130`, `-005224-oldpc-ddb4`, `-005238-oldpc-6069`, `-005251-oldpc-8d14` (border 94,39),
  `-005305-oldpc-75f7` (fog 76,41). None undescribed.
