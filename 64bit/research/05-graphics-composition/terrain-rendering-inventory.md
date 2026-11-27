# Terrain rendering inventory: what package I2-E draws, from which art, with which shaders (epic 2900, task 2905)

Status: first version 2026-11-11 01:09 EST, written by the team-2900 coordinator from the results of tasks 2901-2904
(`asset-pipeline.md` sections 1-4) and a listing of the install copy (the session's agent pool was full when the worker
was due, so no separate trace of the bake was made). Purpose: let package I2-E (the server's terrain renderer, 150-300 h
raw, tracker 0034) be split by seam and re-estimated. The file-format and shader side is in
[asset-pipeline.md](asset-pipeline.md); every address below is explained there.

Inputs: install copy `%USERPROFILE%/Desktop/pure_vp/Sid Meier's Civilization V` (read-only; it has Vibrant Textures
`Assets/DLC/VT` and the VP modpack), the EXE copy of build 4390913. Listing: `evidence/2900/2905/inventory_listing.py`
and its output `inventory_listing.out.txt` (names, counts and sizes only). Nothing was run live.

## 1. What the DX11 terrain frame is made of (the short version)

1. **Load** [C, 2904]: `StartInitTerrainFromGameplayMap` 0x60ABB0 turns every plot into an 8-byte cell (terrain / plot
   type / natural-wonder flags, continent style); `CreateTerrain` 0x709B50 seeds `srand(mapRandomSeed)`, matches hex
   patterns to pieces, blends the pieces' heights into the CPU height field 0xD91270 (64x64 samples per plot), writes the
   coast strips and carves rivers and lakes (river and coast geometry use `rand()`).
2. **Per tile bake on the GPU** [C, 2903]: `0x694a60` builds each terrain tile's cache: TerrainShaderScreen blends up to
   8 texture layers of the tile (piece `_T1`/`_T_BLEND` and the style's terrain diffuse/height/spec) into the "Terrain
   Albiedo/Normal/Spec Temp" targets, TerrainDecalShader draws the decals into those same three targets (roads,
   railroads, farms, the river bed, improvement grounds; *corrected 2026-11-15, epic 0166, terrain-decals.md 4.1 [L]:
   before NormalCalculate and into the blend targets, not a separate "Decal Layer" after it*), NormalCalculate, CacheBake lights the result (sun / shadow cube maps of the style, Terrain ColorKey
   LUT) into two "Terrain Cache" targets, CompressBC compresses them.
3. **Per frame** [C, 2903]: TerrainShader (material 73) draws each tile as an `SV_VertexID` grid over its height texture
   with bicubic tessellation and samples only the tile's cache (t0/t8) plus screen-wide inputs: Terrain Overlay
   (borders, hex overlays), FOW mask and cloud layer, distance-fog ramp, screen-space shadow, the TerrainFOW / Shadow
   ColorKey LUTs, the spec screen map. WaterShader draws ocean, coast, lakes and rivers; WaveShader the coast waves;
   FluidFOW, Overlay_System, IceShader and GridShader produce or draw the rest.
   *Corrected 2026-11-15, epic 0169 (`polar-ice.md` 0, 3, 4 [C, L]; `overlay-system.md` via 2.4 below): IceShader (59)
   draws one static polar-ice mesh built once per load (builder `0x4956F0`, draw `0x5FACB0`), twice per stock frame
   (into the water reflection and into the scene after the ocean); GridShader (58) draws UI nine-slice panels, not the
   hex grid, which is pass 4 of DrawSimpleOverlay into the Terrain Overlay target. Replaced: "IceShader and GridShader"
   read as the ice and grid drawers of the terrain frame.*

## 2. Inventory by item

Origins follow the game's lookup order (`asset-pipeline.md` 1.2): a loose package file (Vibrant Textures) beats every
pack; base packs beat loose base files. Formats from the 2901 sweep (`evidence/2900/2901/terrain_side_files.csv`).

### 2.1 Terrain types and their pieces [C file contents; I where marked]

`Assets/terrain/pieces/<type>/<style>/` (loose, 1,499 files, 975 MB): **299 pieces + `missing_hex`** at the root (the
fallback). A piece = `<name>.csv` (pattern) + `_H.dds` (height, A16B16G16R16 -> R16G16B16A16_UNORM) + `_T1.dds`
(texture/material) + `_H_BLEND.dds` + `_T_BLEND.dds` (blend masks). 151 of 300 are 512x512, the rest 128x128 (2901).

| Type folder | Pieces | Styles | Plot flag the CSV patterns use (2904's cell flags) |
|---|---|---|---|
| Grass / Plains / Desert / Tundra | 12 each | Africa, America, Asia, Europe | 0x1 / 0x2 / 0x4 / 0x8 (flat) |
| Grass Hill / Plains Hill / Tundra Hill | 36 each | 4 styles | 0x20.. (hills, same order) |
| Desert Hill | 37 | 4 styles | hills |
| Snow / Snow Hill | 3 / 9 | Generic | 0x10 / hill |
| Mountain | 36 | 4 styles | 0x400 (e.g. a 3-hex vertical mountain range in a 10x10 pattern) |
| Coast / Ocean | 1 / 1 | Generic | 0x2000 / 0x4000 |
| Wonder_* (14 natural wonders: Crater, Fuji, Geyser, Gibraltar, Kilimanjaro, Krakatoa, Lake Victoria, Mesa, Mt Kailash, Mt Sinai, Potosi, Reef, Sri Pada, Uluru) | 4 each | 4 styles | the wonder's own bit (table in asset-pipeline 4.2; unknown wonders such as VP's get Fuji's 0x40000) |

The CSV format [C for the layout, I for the meaning of the last line]: a count line, the four texture file names
(`_H`, `_T1`, `_H_BLEND`, `_T_BLEND`), the pattern size (`4, 4` for flat land, `10, 10` for hills and mountains), the
pattern rows of plot-flag words where `0x80xxxxxx` is "any", then a pair (`0,1`, `2,1`; [I] anchor or rotation).
`Resource/Common/TerrainPiecesLibrary.fxsb` (26.3 MB, first dword 0x14) is the binary library the EXE actually loads
(2904); its format is not decoded, and it is too small to hold the piece textures [I: patterns and indices only].
The pattern matcher itself was not found (2904); it is the main open RE item for a re-bake.

**Terrain material textures**, `Resource/DX9/TerrainTextures.fpk` (126 entries) with 40 of them replaced by Vibrant
Textures (`Assets/DLC/VT/Terrain/**`, 46 terrain DDS):

| Item | Files (per style `afri_`/`amer_`/`asia_`/`euro_`/`generic_`) | Format / size |
|---|---|---|
| grassland, plain, desert, tundra, tundra hill, marsh | `<style>_<type>_d.dds`, Europe also `_h`, `_s` | BC3 1024x1024 (`_h` up to 2048x2048) |
| mountain | `<style>_mountain_base_d/h/s`, `amer_/euro_mountain_top_d/s` | BC3 |
| snow, coast, shallow seas, ocean floor | `generic_snow_d/h/s`, `euro_coast_d/h/s`, `euro_shallow_seas_d/h/s`, `worl_ocean_floor_d/h` | BC3 |
| natural wonders | `amer_crater_d`, `generic_crater_h`, `generic_gibraltar_d`, `generic_mesa_d/h/s`, `generic_reef_d/h`, `generic_volcano_d/h` | BC3 |
| riverbank | `generic_riverbank_d/s` | BC3 |
| forest / jungle ground | `forest_tile1..4`, `forest_<style>`, `forest_overlay_<style>`, `jungle_<style>`, `jungle_overlay_<style>` | drawn by the Forest system (2.6) [I] |
| lighting | `sunlight_<style>_terrain`, `shadowlight_<style>_terrain` (cube maps, CacheBake t4/t5), `specscreenmap` (TerrainShader t9) | BC1 cube / 2D |
| ice | `ice_d`, `ice_d_sides`, `icedisplacement`, `ice_environment_diffuse`; settings `Assets/terrain/Ice/IceSettings(_Low).xml` | |
| misc | `brush`, `white`, `checkerboard`, `hexgridid` (hex id texture [I]), `marsh_stamp`, `generic_snow_h_sm` | |

Style = the continent art style (Generic, America, Asia, Africa, Europe; `0xb9a428`), per plot from `GetContinentType`
(2904) and per tile the lowest set bit of tile+0x138 (2903).

### 2.2 Water, coast and rivers

| Item | The art | Shader | Geometry / EXE computation |
|---|---|---|---|
| Ocean, coast, lakes | TerrainTextures: `waterbumps`, `waterdepthcolor`, `watersky`; VT: `Water/waterdepthcolor_new`, `euro_shallow_seas_d`; Water bump `.lean` textures (t0-t3); Reflection target; "Water Currents" target (WorldCurrents 256x256); distance-fog ramp | WaterShader (91) passes 0/2/3/5; WaterFrame (shared 26) traced | vertex buffer Water+0x58 built at load; coast strips with `rand()` jitter (0x5674C0, 0x639D40) [C, 2904] |
| Coast waves | `wave_coastline_01` (TerrainTextures); `fx_water*` in EffectTextures.fpk | WaveShader (92), own buffer 0xf80 not traced | `0x6a6490` [I] |
| Rivers | TerrainTextures `river_alpha`, `river_diffuse` (VT `river_diffuse_new`), `river_rough`, `river_smooth`; DecalTextures `riverbank_d/h` (+ endcaps), `floodplains_d/h/depth` (+ endcaps); Rivers ColorKey | WaterShader (91) passes 1/4 (river VS 0x06816a20), own World buffer; RiverShader (69) has no draw object [I: unused] | RiverVisSystem draw object 0x6b1580; river mesh from `rand()` (0x631990, 0x54BAB0) on the load thread [C]; river banks / flood plains are TerrainDecalSystem decals [C, L] (*corrected 2026-11-15, epic 0166, terrain-decals.md 2.2 and 4.4; replaced "[I]"*: Riverbank and Floodplains with their end caps, made by 0x559FF0 through 0x60F180 from the map, deterministic, 61 decals on Tiny t0008 and 326 on the huge save; the capture draws `riverbank_d` and its end caps as decals into the blend targets; bug 0130's dark channel is the missing decal) |

*2026-11-13 (epic 3600 draft): the water system is gathered in [water-system.md](water-system.md), which is current for
seam C. Two points added there [I, join]: GATE2's static IMMUTABLE VB 881,600 B + IB 329,760 B (huge t350, owner left
open in GATE2) is the river mesh (River+0x1C/+0x20, `research/02-exe-internals/world-systems-and-stubs.md` 4), and a
re-implemented river mesh needs the rand() calls of the coastline and coast-strip steps before it.*

### 2.3 Decals (roads, railroads, farms, improvements)

`Resource/DX9/DecalTextures.fpk` (75 entries): `roadsandrails_d/h/sref`, `roads_rails(_h/_sref)`, `bridges_diff/sref`,
`crops_<style>_01..08_d`, `crops_generic_01..08_h`, `crops_half_built_*`, `crops_destroyed_01_d`, `wheat_farm(_half_built)_d/h`,
`floodplains_*`, `riverbank_*`, `tree_shadow_2`, `blank_texture`, `grey`. Producers: LandmarkSystem's GameView listeners
(route curves 0x5EC250 -> 0x583CE0, farm fields 0x748FC0) write into TerrainDecalSystem (TerrainSystem+0), whose batch
draw 0x742ef0 uses TerrainDecalShader (78) pass 1 **into the tile bake** [C draw, L target: the blend's three targets
before NormalCalculate, terrain-decals.md 4.1]. So decals are part of the bake: a decal add or remove invalidates
exactly its rectangle's tiles through 0x4387D0 [C, terrain-decals.md 5.1]. Improvement models are not terrain (D33:
models stay out), but their ground decals (the `decal` / "Hex Ground Decal Template" meshes) **are turned into
TerrainDecalSystem decals** by the landmark and wonder visualizers [C, L: terrain-decals.md 2.2], so they are in the
bake too.

*Corrected 2026-11-15, epic 0166 (terrain-decals.md 2.2, 4.3, 8.2, 11.3). Replaced: "[C draw, I target]", "a decal
change re-bakes the tile [I]", "their ground decals inside `.gr2` files ... are a model concern", and the table's "The art
(all in `Resource/DX9/DecalTextures.fpk` ...)". The decal textures come from **several packs, Vibrant Textures' loose
files and the VP mod**, not DecalTextures.fpk alone: route curves `RoadsandRails_new_D/_H/_SREF.dds`
(Upgrade1Textures.fpk), farm fields from the `farm_types.xml` the EXE read (VP replaces it; VP's `crops_maize_d.dds` is
in the VP mod), city ground `CityBase_Diff_anc.dds` (VT loose) with `CityBase_HT/_SREF` (MiscTextures.fpk), model
grounds from ImprovementTextures.fpk and MiscTextures.fpk, the default spec `blank_texture_spec_decal.dds`; river,
flood-plain and tree-shadow textures from `decal_filenames.xml` (inside `Resource/Common/MiscData.fpk`) and
DecalTextures.fpk. Farm tracks draw the engine's 8 x 8 default texture (handle 0). The server resolves every name
through `c5r::gamefs` (terrain-decals.md 8.2, 11.4: 26 files byte-equal to the captured textures). Bridges have **no
decal key** (P9: a road across a river is the route curve over the river bed).*

**Roads, railroads and farm fields are the server's job in increment 2** (epic 3000, issue 3003, live 2026-11-11 on the
huge and a Tiny save: with the EXE's terrain pass off they do not draw, while improvement models stay; interim result of a
team not yet merged, `research/02-exe-internals/disable-systems-and-masks.md` on branch
`worktree-agent-ae601c7cc8cc4ccb4`). What they are made of, as far as this epic knows:

| Item | The art (load through `c5r::gamefs`; packs as in the note above, *corrected 2026-11-15*) | Geometry and selection in the EXE | Shader |
|---|---|---|---|
| Roads and railroads | `roadsandrails_d/h/sref`, `roads_rails`, `roads_rails_h`, `roads_rails_sref` (one atlas pair for both route types [I]) | RoadLayout listener 0x5EC250 builds route curves from the DLL's plot route bits (events B45/B46 road created/destroyed) and hands them to TerrainDecalSystem through 0x583CE0 [C, landmarks-and-models.md 3]; route type -> atlas region not traced | TerrainDecalShader (78) pass 1 into the tile bake; own buffer `TerrainDecal` 0x70 (DecalViewProj, type entry +0x24..+0x30) [C]; vertices POSITION.xy + TEXCOORD0.xy. *Corrected 2026-11-15 (terrain-decals.md 1.2, 4.2): "+0x24..+0x30" is the per-draw decal record's rectangle, not a type entry; vertices are R16G16_FLOAT halves, the position normalised to that rectangle* |
| Bridges | `bridges_diff`, `bridges_sref` | RoadLayout with river edges and ToggleBridgeState (B47); decal or mesh not established. *2026-11-15, epic 0166 (terrain-decals.md 2.2, 5.3, P9) [L]: no bridge decal key in TerrainDecalSystem on the huge save (188 road edges across rivers) or after a live road across a river; what stock draws there beyond the route curve and the river bed is not a decal* | not a decal |
| Farm fields | `crops_<style>_01..08_d` (Africa, America, Asia, Europe), `crops_generic_01..08_h`, `crops_half_built_01..08_d`, `crops_destroyed_01_d`, `wheat_farm(_half_built)_d/h` | farm-field builder 0x748FC0 (`ART_DEF_IMPROVEMENT_FARM` -> layout 0x6016F0), serial, 121-235 ms per farm group [C/L, landmarks-and-models.md 3]; style from the plot's continent style; half-built and pillaged states from the improvement state [I] | TerrainDecalShader, as above [I for farms] |

*2026-11-15, epic 0166: the paragraph below is superseded by route (a) (terrain-decals.md 0.2, 9-11): the server
copies the EXE's own decal records, vertex bytes and texture names through the decal tap instead of porting the
producers; the "table at TerrainSystem+0xc0" is the variant table of `decal_filenames.xml` (terrain-decals.md 1.3),
which names only the river, flood-plain and tree-shadow textures; every other class's textures come from its producer
(8.2-8.3).* What the asset pipeline would need for them (not built in this epic): the TerrainDecalSystem decal-type table (each
type's texture pair, atlas rectangle, size and blend: the `type entry +0x24..+0x30` read by 0x742ef0 and the table at
TerrainSystem+0xc0 that stubs/terrain.md names), how a route type and a farm variant pick their entry, and the decal
vertex format; the textures already load. Size: 3-6 h for the pipeline part (trace the type table and the selection,
add a `decals` description beside `terrainsets`); the geometry (route curves, farm-field layout) is seam D of section 3,
which is now required in increment 2 rather than optional.

### 2.4 Culture borders, hex grid and other overlays

The art definitions: `Assets/Overlays/CultureBorders.xml` (border cross-section: primary/secondary widths and alphas,
anti-alias widths, corner bias, dashed "cs_" style, `randomness = 6`, `expansion = 2`), `HexGrid.xml`, `Highlights.xml`,
`Civ5ArtDefines_Overlay.xml`, arrow textures; `spline_border_contour1/2.dds` (MiscTextures.fpk). Overlay_System render
0x6ead50 draws with DrawSimpleOverlay (67, 6 passes, shared buffer 29 = one matrix) into the 2048x2048 "Terrain Overlay"
target 0xda12a4 (culture borders, a Z clear, then overlays), which TerrainShader samples as t1 through OverlayProj
(TerrainFrame 0x040) [C; borders by DrawSimpleOverlay I]. Hex grid: pass 4 of DrawSimpleOverlay into the same Terrain Overlay target [C]; GridShader (58) draws UI nine-slice panels, not the grid. (Corrected 2026-11-14 lead-0167 from `research/05-graphics-composition/overlay-system.md` (3701, MC #696) [C]: replaced "Hex grid: GridShader (58) via 0x6ab930 [I]".) Inputs from the DLL:
plot owner, culture-border changes; from the EXE's UI Lua: selection and highlight state.
*Corrected 2026-11-14 (drafting of epic 0167, by reading only; replaced "Inputs from the DLL: plot owner, culture-border
changes, selection and highlight state"): the highlight state is made by UI Lua in the EXE through
`Events.SerialEventHexHighlight` and kept by the EXE's handler 0x574A00
(`research/02-exe-internals/disable-systems-and-masks.md` 5, bug 0127) [C]; plot owners and border changes come from
the DLL (`UpdateCountryBorder`, `renderer-in-server-design.md` 2.1) [C]. WaterShader samples the same target as t9. The
server's terrain and water draws bind t1 / t9 to a transparent 1x1 and OverlayProj = identity today. Seam E is epic
0167 (`docs/briefs/epic-0167-borders-and-overlays.md`); its design goes into `overlay-system.md` (to be written).*

### 2.5 Fog of war

FluidFOW_System (0x44a570) renders the "FOW Mask" 0xda12ac with FOWDrawMask (52) / FOWBaseLayer (53); FOWCell (50) and
FOWParticleShader (51) [I draw]; cloud layer `fogcloudtile_dif_01..03.dds` (EffectTextures.fpk, BC3/BC1 1024x1024);
TerrainShader t4 (mask), t5 (cloud), t6 (TerrainFOW ColorKey LUT) and TerrainFrame.FOWAnim. Distance fog:
`assets/DistanceFog/FogRamp.dds` (`fogramp.dds`, MiscTextures.fpk, 128x8) and the DistanceFog buffer (shared 19). Input
from the DLL: revealed / visible state per plot and team.

*Corrected 2026-11-14 by issue 3957 (epic 0168, C7) from `fog-of-war.md` 2, 3 and 7 (tasks 3950 [C], 3951 [L], 3952,
3955); the paragraph above is kept as the 2026-11-10 reading. Was: "FOWDrawMask (52) / FOWBaseLayer (53); FOWCell (50)
and FOWParticleShader (51) [I draw]". Current:*
- *The mask "FOW Mask" `0xDA12AC` (1024 x 1024 RGBA8 at FOWLevel 2-3, 512 at 0-1; R explored, G unexplored, B visible)
  is drawn only by **FOWDrawMask (52)** passes 0-2, every frame, MAX-blended hexagons per plot in the overlay's window,
  then GenerateMips. **FOWBaseLayer (53) does not render the mask**: it is the three translucent **cloud planes** drawn
  into the scene after the models (`fogcloudtile_dif_02/03/01.dds`, heights 11.2 / 38.25 / 51 world units, gated by the
  mask's G), after a depth-only **FOW CULL** draw (FOWDrawMask pass 3). At FOWLevel 3 **FOWParticleShader (51)** adds one
  particle draw (`fx_fog_dif.dds`). The terrain's t5 is `fogcloudtile_dif_02.dds` (FOWLevel >= 1).*
- ***FOWCell (50) belongs to FOW_System** (key 14, `0xDAAA20`), not to FluidFOW, and draws only at FOWLevel 0 [C, 3950;
  no FOWCell draw in seven FOWLevel-3 captures, L 3951].*
- *Distance fog is done in the server: terrain by issue 0188, water and rivers by 3609 (`terrain-surface.md` 6,
  `water-system.md` 10.7). The fog of war is epic 0168 (route (a): the EXE's own FOWDrawMask and FOWBaseLayer programs
  driven from the server's `PlotRecord::fow`; `fog-of-war.md` 5 and 7).*

### 2.6 Outside the terrain draw but on the terrain (scope questions for the MC)

- **Forests and jungles**: the Forest system (view-mask bit 0x80, Forest_System 0xDA91C8, draw 0x6BFE90 -> 0x63A7A0) draws
  trees; `forest_*`/`jungle_*` textures and VT `forests.xml`. With it off "trees gone" (disable-systems-and-masks.md row 7)
  [L]. D33 says terrain yes, models no: whether trees count as terrain is **not decided** - ask the MC.
- **Ice**: IceShader (59) via 0x4956f0, ice textures above; probably part of the terrain seam.
  *Corrected 2026-11-15, epic 0169 (seam G; `polar-ice.md`). Replaced: "via 0x4956f0" and "probably part of the
  terrain seam". `0x4956F0` is the ice **mesh builder** (CreateTerrain -> `0x5A0BC0` -> `0x4956F0`, once per load, from
  the ice plots, `IceSettings_Low.xml` on the instances and `IceDisplacement.dds`; not random per session) and the draw
  is `0x5FACB0` [C, L] (`polar-ice.md` 0, 3). Under D33 the mesh is built but not drawn: both callers test view bit 0x1
  [C, L] (4). What the server does now: the composite's ice tap copies the EXE's mesh once per load (4307, 9), the
  `c5r::ice` module draws it with the EXE's IceShader blobs and an IceFrame computed row by row (4306, 7-8), the scene
  draws it after the ocean and before the coast waves (4308, 10), live on the product build in every frame, Tiny 1 chunk
  and huge 8 chunks (4310, 4311: 11-12); at frame level the floes equal stock (`terrain-surface.md` 13.6). Not drawn:
  the ice in the server's water reflection (decision 3; the reflection target is transparent black), the one named
  difference of the ice class.*
- **Shadows**: TerrainShader t3 "Shadow Texture Blur" is a screen-space shadow of everything (units, buildings): in
  increment 2 the models stay in the EXE, so the server has no shadow casters [I: start with a white shadow]. *Corrected 2026-11-14 by issue 0190 (terrain-surface.md 9): t3 is world-mapped through ShadowMatrix0, not screen-space; the EXE keeps drawing it under D33, so the server takes it through a tap (drafted as NEW-shadow-tap).*
  *Done 2026-11-15, epic 0169 (replaces "drafted as NEW-shadow-tap" as the state): the shadow tap (ticket 0312, task
  4304) copies the EXE's ShadowBlur with ShadowMatrix0 per view and the factor `[0xD90FA8]` every map frame; the server
  binds the triple into the terrain (t3, Arg6, ShadowMatrix0 per wrap copy, t7 per tile style) and the water (t13,
  WaterFrame 0x220) (4308); live on the product build in every drawn frame (4310, 4311) [L] (`terrain-surface.md`
  11.9-11.11, 13.7). The quality `[0xD90F9C]` is the ini's `ShadowLevel` and sets ShadowBlur's size and format (q 3
  2048^2, q 2 1024^2 R16_UNORM, q 1 ShadowRaw's back-buffer-sized R16G16B16A16_FLOAT, q 0 a 4 x 4 texture of zeros: no shadow) [C, L] (11 S2b, 11.9).
  At frame level shadowed land is at stock's two-session ceiling on Tiny (`terrain-surface.md` 13.6).*
- **Colour grading**: the ColorKey LUTs (`Assets/ColorKeys/*.tga`, 22 files, 5 types x 5 styles, 32^3 RGBA8 volumes built
  at run time; asset-pipeline 3.5). An identity LUT is a valid start.
  *2026-11-15, epic 0169 (no replacement; the state now):* the LUT rule (texel order) is settled [L] by 3403
  (`terrain-bake-passes.md` 5.2) and the server's volumes are byte-equal to stock's (`terrain-surface.md` 12.1). Of the
  25 type / style names, 21 exist on every install tested (the non-Generic `Rivers_*` files are missing: identity);
  the 22nd file of the folder is `TerrainColorKey.tga`, which fits no type / style name [L, listing of `pure_vp`,
  2026-11-15]. Per style, only the bake's lighting cubes differ on those installs; every ColorKey type gives one volume
  for all five styles (`terrain-surface.md` 12.2). The server takes the tile styles since 4305 (ticket 0341): the
  bake's cubes and Terrain LUT, t6 and t7 per tile style (`terrain-surface.md` 12.6). No light term enters the terrain
  draw per frame; the water's light constants are the EXE's (12.4).

## 3. Seams for I2-E and re-estimates

Raw = a fresh estimate in the style of the project's raw figures; expected = what this project's record (3% to 30% of
raw) suggests: 25-35% where the remaining work is mostly porting known facts, 35-45% where reverse engineering is still
open (the bake, the river and coast meshes).

| Seam | Contents | Inputs | Known / unknown | Risk | Raw h | Expected h |
|---|---|---|---|---|---|---|
| A. Height field and terrain draw | tap of 0xD91270 (2904's spec, 84 MiB of heights + 42 MiB byte layer on Huge per load; corrected 2026-11-11 by 9019 from "80-120 MiB"), per-tile height textures, TerrainShader passes 0-2 (tessellated) with TerrainTile, TerrainFrame, DistanceFog; constant inputs for t1-t7 at first | DLL map size; camera; the tap through proxy and shell (epic 3000 side) | draw fully traced [C]; tap layout [C]; pass choice at the owner's settings [L open] | low | 25-40 | 6-12 |
| B. Terrain bake (tile caches) | port 0x694a60: layer selection per tile, TerrainShaderScreen (own 0x4c0 buffer, 16-24 slots not decoded), NormalCalculate, CacheBake (style cubes, Terrain LUT), CompressBC; the piece/pattern assignment (matcher not found; fxsb not decoded) **or** a tap of the EXE's cache textures (textures share, GATE2: 8-15 h) | pieces and terrain textures via `c5r::gamefs`; plot cells; the height field | chain and programs known [C]; buffers, layer records, matcher unknown | **high**: the long pole of I2-E | 60-110 | 20-40 (tap route 6-12) |
| C. Water, coast and rivers | WaterShader ocean/coast/lakes and river passes, WaterFrame and the Water fields, waves and currents, reflection; meshes: coast strips and rivers are built by `rand()`-driven EXE code | Water mesh and river mesh (port with the same `srand(mapRandomSeed)` sequence, or CPU readback of the EXE's buffers), height field | WaterFrame [C]; Water fields and river own buffer [I]; mesh code located, not ported | medium-high | 35-60 | 12-22 |
| D. Decals (roads, railroads, farm fields: required in increment 2, 3003) | TerrainDecalShader into the bake; producers' data (routes, farms, improvements) from the DLL; DecalTextures.fpk | DLL route and improvement data; bake (seam B) | draw and buffer [C]; route curve and farm-field geometry are LandmarkSystem code to port. *Corrected 2026-11-15, epic 0166 (terrain-decals.md 0.2, 9-11): taken by route (a), a tap of the EXE's decal records (no port: the layouts are random per session, P6), textures from several packs, VT and the VP mod by name through `c5r::gamefs`; the decal pass runs on the blend targets before NormalCalculate and is bit-equal to the EXE on four captured tiles [L]; the river bed and the improvement grounds are decals too* | medium | 20-40 | 7-14 |
| E. Borders and overlays | Overlay_System into the Terrain Overlay target: culture borders (CultureBorders.xml parameters, spline contours), hex grid, highlights | DLL plot owners, team colours, selection | target and sampling [C]; border geometry generation [I] | medium | 20-35 | 6-12 |
| F. Fog of war | FluidFOW mask, cloud layer, FOW LUT, FOWAnim; distance fog (*2026-11-14, 3957: distance fog done by 0188 and 3609; the rest is epic 0168, `fog-of-war.md`*) | DLL visibility per plot and team | targets and slots [C]; FluidFOW simulation not traced (*2026-11-14: traced by 0168's 3950 / 3951: the mask is stateless MAX-blended hexagons, not a simulation; only the FOWLevel-3 particles simulate*) | medium | 15-30 | 5-10 |
| G. Lighting, colour grading, ice, grid | ColorKey LUTs (texel order to verify), style cubes, spec screen map, IceShader, GridShader, shadow input | files; DLL ice / feature data | LUT build [C], texel order [L open]. *Corrected 2026-11-15, epic 0169 (seam G done; replaced "texel order [L open]"): texel order settled [L] by 3403 (`terrain-bake-passes.md` 5.2); the server's colour inputs byte-equal to stock's (`terrain-surface.md` 12.1); spec screen map done by 0189 (`terrain-surface.md` 8); tile styles in the feed, bake, t6 and t7 (4305, 12.6); the models' shadow by the shadow tap and the scene (4304, 4308; 11.9-11.11); the polar ice by the ice tap, `c5r::ice` and the scene (4307, 4306, 4308; `polar-ice.md` 7-11; the ice comes from the EXE's mesh, not from DLL feature data); GridShader is not the hex grid (2.4: epic 0167's overlay). Accepted at frame level against stock's two-session ceiling (`terrain-surface.md` 13.6), named differences left: the ice in the water reflection (decision 3), the terrain's wrap copies 1 world unit off (13.6 finding)* | low | 10-20 | 3-7 |
| **Total** | | | | | **185-335** | **59-117** (with the bake tapped: 45-89) |

The raw total sits at the top of the old 150-300 h figure because the bake (B) and the `rand()`-driven meshes (C) are now
visible as their own work. Two decisions shrink it most: (1) **tap the bake caches** instead of porting the bake while
the EXE's terrain still runs under stub c1 (GATE2 showed textures share at 0.07-0.26 ms per texture); (2) **CPU readback
of the water and river buffers at load** instead of porting their `rand()` code. Both keep the EXE's terrain system
alive and bind increment 2 to stub c1 (2904: under c3 there is nothing to tap).
*Corrected 2026-11-11 by issue 3204 (live, section 5): under c1 (Terrain view bit cleared, D33) the bake does **not** keep
running. It runs once, at load, for the tiles around the load camera only (135 of 10,752 on the huge save), and never
during play. So the tap gives colour only where the load view was; colour elsewhere needs another source (tracker 0099).
Replaced: "while the EXE's terrain still runs under stub c1".*

## 4. Open questions that need a live capture (route to epic 3000)

1. A RenderDoc (or helper-DLL) capture of one frame and of one tile bake on c5h-b: which TerrainShader / WaterShader pass
   runs at the owner's settings, the TerrainShaderScreen layer records and its 24-texture variant, blend/depth/raster states.
2. ~~Whether TerrainDecalSystem decals vanish when the terrain draw is off~~ answered 2026-11-11 by epic 3000 (3003,
   live, interim): roads, railroads and farm fields vanish; see 2.3.
3. The ColorKey texel order (dump one LUT volume) and the Water object's fields (+0x6c..+0xe4). *Texel order answered
   2026-11-13 by 3403 (`terrain-bake-passes.md` 5.2) [L]; noted here 2026-11-15 by epic 0169.*
4. Determinism of the height field and of the river/coast meshes: load the same save twice and compare (2904's
   `hfbake compare`).
5. Whether trees (Forest system) are in increment 2's scope (2.6). *Live part answered 2026-11-11 by epic 3000 (3009):
   with the terrain pass off (D33) and Forest on, trees do not draw: the forest PS discards where the FluidFOW "FOW
   Mask" reads 0 (`disable-systems-and-masks.md` section 5, "Why the models are dark red"); the scope decision stays the MC's.
   2026-11-11 (3011): with a white mask handle written into 0xDA12AC and into the boot-built forest draw objects (test
   DLL `whitemask=2`, data writes plus one EXE texture-loader call) trees draw in D33 + Forest at no memory cost [L].*

## 5. Seam B: the tile caches live, and the tap (issue 3204, epic 3200, 2026-11-11)

Setup for every [L] below: Goldberg instance c5h-e, DX11 EXE (TimeDateStamp 0x546CD0A8, loaded at 0x006F0000), DLL
`i2-shell-test` b92373aeb, D33 (`[DisableSystems]` set of `disable-systems-and-masks.md` section 5 and marker
`viewmask.enabled` `clear=0x1`), save copy `C5H-3200-HUGE_0350` (huge map, turn 350), CP/VP/EUI. Read-only tools:
`shell/tools/bake_probe.py` (ReadProcessMemory by pid), `shell/tools/bake_reader.py` (the tap's consumer), the tap itself
(`shell/composite/bake_tap.cpp`). Raw outputs: `D:/c5-64bit-work/3200/w3/live/` (not committed).

### 5.1 Where the tiles are [C + L]

- TerrainSystem is the static object 0xD165E0; **TR = [TS + 0x75FA4]** (a pointer, not an embedded object) [C 0x68EFE3,
  L]. TS+0x75FDC is the pre-bake condition byte (1 live).
- **Tile grid:** TR+0x2310 = tiles in x (128), TR+0x2314 = tiles in y (84), TR+0x2318 = count (10,752) [L]. TR+0x1ADC
  (= TR+0x1AE0) points at an array of 10,752 tile pointers, **column-major**: index = x * 84 + y [L, every entry checked].
  Further lists of the same capacity start every 0xA800 bytes: TR+0x1AE4 (pre-bake list, count TR+0x231C = 121, cursor
  TR+0x2320 = 121, done flag TR+0x2350 = 1), TR+0x1AE8 (4,767 entries, count TR+0x2324), TR+0x1AEC (96), TR+0x1AF0 (the
  visible list, count TR+0x2334 = 84). Dirty-tile list for the in-play bake: pointer TR+0x1B00 (= TR+0x1B0C, an inline
  array), count TR+0x1B04 (0 live), capacity TR+0x1B08 (0x200) [C 0x4242AA..0x4242F6, L].
- **One tile = one plot = 64 x 64 height samples.** tile+0xD4 = tile+0xD8 = 65 (64 in the last row/column), tile+0xDC /
  +0xE0 = the tile's origin in samples (x, y; steps of 64), tile+0xE4 / +0xE8 = offset into the height page (origin +
  offset = the tile's texel place in its 1024 page) [L]. So the shader's InvGridSize = 1/(0xD4 + 1) = **1/66 = 1/(P + 2)
  with P = 64**, the convention the c5r::terrain module assumed [L for the values, C for the formula]. For W1
  (`c5r::terrain::Options`): tilePlots = 1, tileD4 = 65.
- tile+0xEC = the height texture handle: a shared **1024 x 1024 A8_UNORM page, 11 mips** (4 pages for the 121 pre-baked
  tiles) [L, from the EXE's texture record 0x4170B80 + 0x398 * h: +0x28C width, +0x290 height, +0x294 mips, +0x29C bytes
  0x155555, +0x2B4 format 0x41]. tile+0x138 = 8, 9 or 1 (style bits), tile+0x13C = TR + 8, tile+0x1C4 = 0 or 1,
  tile+0x2FC = 1 for 120 of 121.
- **Texture handles** (tile+0x190..+0x19C, tile+0xEC) are small integers (< 0x3000), not pointers. The EXE's view table
  0x414C348 holds 12 bytes per handle; entry [0] (and [1] for render targets) are D3D11 view objects of the texture [C
  0x8BC0CA, L]. All of them carry the same d3d11.dll vtable as a texture, SRV or RTV the tap creates itself (Windows 11
  26100), which is how the tap checks a pointer before it calls into it.
- TR+0x2424 = 1.0f, TR+0x2428 = 2 (int), TR+0x242C = 1.5717f, TR+0x2430 = 0.25f under D33 [L]. **These are not the
  stock frame's values**: under D33 the terrain draw that writes them never runs (5.3), so they are start-up values.
  LodMip and TessFactor at the owner's stock frames stay open (terrain-surface.md, P17). *Measured 2026-11-14 by issue 0154
  (terrain-surface.md P17; renderer-in-server-design.md 4.16, last block): LodMip = L, LodStep = 2^L, TessFactor 2, L = 1
  at the default zoom on the workstation; huge on oldpc (1914 wide) TR+0x242C 1.98-2.02.*

### 5.2 P13 and P14: the bake under D33 [L]

- **The load-time pre-bake runs** (0x70CBC0 stage of LoadGameState -> 0x68EFE0 -> 0x4AE6F0, 4 tiles per call, sets
  tile+0x314 = 1): after the load TR+0x2350 = 1 and cursor = count = 121.
- **Which tiles have caches:** 135 of 10,752 tiles have a t0 cache (tile+0x198), 110 a t8 cache (tile+0x19C), 2 the
  second-LOD caches (+0x190/+0x194). All lie around the camera of the load: plots x 95-113, y 23-34. The state word
  tile+0x310 (= +0x314): 1 for 96 tiles, 3 for 37, 5 for 2, 0 for all others.
- **Coverage of the huge ground-truth cameras is partial** (`shell/tools/bake_coverage.py`, every tile's box projected at
  z 12 through `camera_v*_before.json`, a box test [I at the screen edges]): v1 sees 104 tiles, 58 with a cache; v2 42,
  26; v3 42, 18. The missing tiles are the far (north) rows, tile y 34-37: the save's load camera sits 3-4 tiles south of
  the ground-truth look-at plot (100,30). *Corrected 2026-11-11 13:40 (the first report to the MC said the caches cover
  those cameras).*
- **No bake in play:** after `UI.LookAt(30, 60)` (camera moved to world x -2145, checked) no tile gained a cache and no
  list changed; the visible list stays the one of the load (x 104-113). Under D33 the caches exist only for the load
  view; a pan shows tiles that never get one.
- **P14 holds:** the tap copied all 135 caches; `bake_reader.py` (a separate x64 process) opened them by name and decoded
  them. t0 = lit terrain colour (desert, plains, a farm field and a railroad decal in one tile; mean RGB 132-161 /
  115-141 / 54-97 over four tiles), not darkened by fog. t8 is near black except the railroad: a mask or specular layer,
  not colour. **Caches: 256 x 256 BC1, 6 mips (43,680 B), created BC1_TYPELESS** (the EXE's record says BC1_UNORM, the
  view's format). Pictures: `D:/c5-64bit-work/3200/w3/live/dump1/` (game art, not committed).

### 5.3 What requests a bake in play, and why nothing does under D33 [C unless marked]

- The per-view update of GameViewState, **0x5893F0** (and its twin 0x7492E0), tests the view mask GVS+0x1B4 once per
  system. For **bit 0x1 (Terrain)** it calls the TerrainSystem cull **0x51B070** (`test byte [GVS+0x1B4], 1` at 0x5894D2
  / 0x7494B7) and later the TerrainSystem render **0x657070** (test at 0x5895D6 / 0x74958D). Both tests read the same bit.
- 0x51B070 calls **0x7BA820** with TR: the visibility, LOD and cache state machine. It rebuilds the visible list
  (TR+0x1AF0, count TR+0x2334), moves tile+0x314 to 2, 3 or 5 for tiles that need a (re)bake, and **appends them to the
  dirty list TR+0x1B00** (push through 0x5E67C0 at 0x7BAD33, 0x7BADDA, 0x7BAEEB).
- 0x657070 calls the terrain draw **0x4241E0**, which first bakes every tile in the dirty list (0x627810 -> 0x694A60,
  loop 0x4242D0..0x4242F2), sets the count to 0, then draws.
- **The condition that is false under D33 is GVS+0x1B4 bit 0** (option c1 clears it through ViewMaskSync's `clear=0x1`).
  Both the cull that fills the dirty list and the draw that empties it are skipped [C; L: TR+0x1B04 = 0, visible list
  stale after a pan].
- **Is there a data-only trigger?** Ruled out [C]: (a) writing tiles into the dirty list TR+0x1B00/+0x1B04 does nothing,
  because its only consumer 0x4241E0 runs only under 0x657070, behind the same bit (its other caller 0x56CE27 belongs to
  another object with its own TR at +0x13914, not traced); (b) resetting the pre-bake cursor or count (TR+0x2320 /
  +0x231C / +0x2350) does nothing in play: 0x4AE6F0's only caller is 0x68EFE0, whose only caller is the LoadGameState
  stage 0x70CBC0 (call at 0x70CC2B), which runs during a load only [C for the chain, I that the stage does not tick in
  play]. (c) Setting bit 0 again is data only, but it runs the whole terrain cull **and draw** for that frame: the EXE's
  own terrain would be drawn, which is what c1 exists to stop. A one-frame version (bit on for one frame, that frame
  hidden by the composite) is conceivable but untested [I].
- **So a bake of a chosen tile in play needs a call into the EXE by address**: 0x627810 (the tile method, `this` = tile,
  two stack arguments: `[[0x3010570] + 0x33F74]` and a pointer its caller passes on) or 0x4AE6F0 (`this` = TR, one
  argument). That falls under question 0098 (with the owner). Thread and render state of such a call are not traced.

*Added 2026-11-13 (issue 3023, `terrain-bake-records.md`): in the stock game a bake in play needs no call by address.
The cull fills the dirty list at 1 tile per frame while the camera moves and 16 per frame once it settles. The pool
holds 6,144 BC1 slots (about 3,072 tiles), fewer than the 4,729 land tiles of the huge map. The per-tile layer
records that the bake reads exist for every tile after a D33 load.*

### 5.4 The tap (`shell/composite/bake_tap.cpp`, ini `bake_tap=1`)

- Checks seven byte patterns of the EXE before the first read (TerrainSystem, TR offset, pre-bake list and state write,
  the two cache copies, the view table), on top of the composite's own EXE validation: all match live [L].
- Every Present: every slot's tile (three small guarded reads) plus 512 grid entries of a rolling scan (all 10,752
  tiles in 21 Presents; one read per tile without a cache). The first version scanned 2,048 entries with three reads
  each: 0.28-0.36 ms mean per Present on the render thread [L, run A], hence the change. A tile with a cache and no slot gets one; a slot whose handles, view pointers or state word
  changed is copied again. Copies: `CopyResource` into two shared textures per slot (NT handles,
  `Local\VPShellBakeTex.<pid>.<slot>.<0|1>`), at most 24 copies or 3 ms CPU per Present; a slot is published valid only
  after an event query issued behind its copy completed. Manifest `Local\VPShellBake.<pid>` (64 B + 4,096 slots x 96 B =
  393,280 B). **A tile without a cache has no slot**: the manifest marks "no cache" by absence (and by state 0 for a slot
  whose cache went away), so the server needs a fallback colour for every tile not in the manifest.
- First live run (2026-11-11 13:20-13:28): the tap started at the front end, found the grid during the load and copied
  all 135 caches as they appeared (135 slots, all valid, read back by name).
- **Run A (2026-11-11 13:51-13:58, tap switched on in game through the ini):** 135 slots (110 with both caches, 25 t0
  only), **245 copies, 0.346 ms CPU in `CopyResource` in total = 0.0014 ms per copy, at most 0.051 ms in one Present**,
  all slots valid 92 ms after the tap started; all 245 shared textures opened by name from the x64 reader. GATE2's
  0.07-0.26 ms per texture was a different measure (whole tap path) [L]. **EXE memory:** private bytes 1,555.3 MB with
  the tap off, 1,561.7 MB 20 s after it was switched on (+6.4 MB: manifest 0.39 MB, slot table, the D3D11 runtime's
  share of 245 textures of 43,680 B = 10.2 MB of video memory) [L, one reading each].

- **Run B (2026-11-11 14:08-14:16, the 512-entry scan, tap on from the start):** the same 135 slots / 245 copies, 0.554
  ms CPU in `CopyResource` (0.0023 ms per copy, at most 0.110 ms in one Present), all copies done 64 s after the tap
  started (during the load, as the caches appeared); steady state in game **0.061-0.079 ms mean per Present, max 0.42
  ms** over six windows of 1,000 Presents [L]. Not graded per PERFORMANCE.md (the tap's own timer, no A/B of frame time).
  *Verification 9038, 2026-11-13 [L, turn-359 save, 176 windows incl. a reload, AI turns and pans]: 0.067-0.143 ms mean
  per Present, median 0.084 ms; 135 slots, 361 copies; the 0.061-0.079 ms above is the at-rest figure.*
  All 245 textures read back by name. Logs: `D:/c5-64bit-work/3200/w3/live/shell_composite.run{A,B}.log`.
- Negative and hostile-input tests: `shell/tests/test_bake_tap.py` (27 tests on the stand-in, 0 skipped, 2026-11-11
  14:18): missing cache texture, handle past the view table, a pointer whose vtable is not D3D11's, a tile pointer
  below 64 KB and an unmapped one (SEH, the tap disables itself), absurd counts, absurd tile fields, no terrain renderer,
  a manifest section that exists too small, the real path refusing an EXE without the patterns or not validated, the
  kill switch, an unknown test mode, every reader refusal (exit codes 2-7), the coverage tool.

### 5.5 The load view (MC decision 2026-11-11, point 2) [L]

- The camera after the load screen is **not** the camera the pre-bake used: the game moves it at the start of the turn.
  Load view of `C5H-3200-HUGE_0350` (camera translation (2975.9, -1183.1, 424.3), byte-identical in D33 and stock):
  97 terrain tiles visible (x 104-116, y 25-33), **30 with a cache** (`bake_coverage.py`, z 12). So the load view is not
  fully cached either; colour at it falls back to the cached-tile mask (decision point 1). Many uncached tiles are water.
- Under stock at the same view **3,084** tiles hold a t0 cache (3,051 a t8 cache; state 1 for 3,000), against 135 in D33:
  in stock the in-play bake follows the camera.
- Files (not committed, game art): `D:/c5-64bit-work/3200/w3/loadview/{d33,stock,stock2}/loadview.png` (2554 x 1411)
  with `loadview_cam_before/after.json`; `coverage_loadview.json`; `reader_runA.json`. `stock2` was taken 14 s after
  entering the game, `stock` 6 s after; both mean RGB (109, 117, 113).
