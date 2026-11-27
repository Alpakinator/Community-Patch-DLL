# Live terrain: terrain changed by a mod during play, drawn at once (epic 0465, Extras C)

Started 2026-11-18 16:41 EST by lead-0465 (account B), package T0 of the Architect's design
(`research/07-architecture-decisions/architect-2026-11-16-increment-4-extras-epic.md` section 9, 9.3 and 9.4). Status:
**T0 in progress; nothing here is product code.** Section 1 holds what the knowledge base already answers, with the
file it comes from; section 2 the premises that need a live run and how each is run; section 3 the risks. Live results
are added to section 2 as they come, dated, with the save, the build and the configuration.

Rule E1 holds throughout: the mod changes the game; the patch only shows it. No EXE code byte (D4), no decompiler
output (D83): heights and materials re-made by the server are written from the description of `CreateTerrain` in
`asset-pipeline.md` 4.1-4.2.

## 1. Premises answered from the records (2026-11-18)

### P1. The server's heights are a copy of the EXE's, taken once per load [C]

- The server does not compute heights. The composite DLL in the EXE copies the EXE's `GridHeightmap` (0xD91270: the u16
  height layer and the u8 material layer, 16 x 16-sample tiles in a pool) into a "C5HF" section once per load, after
  `CreateTerrain` returns; `server/src/render/srv_render_scene.cpp` reads it (`readTap`, `Scene::loadField`) and hands it
  to the renderer (`setHeightField`). Decision "tap" of 2026-11-11 (`asset-pipeline.md` 4, 4.3; D13, D58).
- The material byte layer is a product of the same load step (the byte of the piece with the highest `_T_BLEND` weight,
  `asset-pipeline.md` 4.1 step 5), and the server's bake derives its per-tile layer lists from it
  (`terrain-bake-records.md`, `terrain-bake-blend.md`). So a terrain-type change (grass to desert) is stale in the
  server's colour too, not only a plot-type change in its heights.
- Consequence for T1: there is no height or material producer in the server to extend. T1 is a new producer: steps 3-7
  of `CreateTerrain` (coastline curves and their jitter, the piece matcher, the block blend, coast strips, river and lake
  carving) for the changed region. Two of its inputs were never decoded: the hex pattern matcher was not located and
  `TerrainPiecesLibrary.fxsb` (27,595,584 bytes) is not decoded (`asset-pipeline.md` 4.1 step 4, 4.2 point 2). Epic
  2900 (task 2904) estimated a whole-map re-bake at 40-80 h; the Architect's T1 estimate (16-30 h) did not include
  these two unknowns [I: the 9.4 table names no decode].

### P2. Stock never updates the field during play [C static; live check in 2.1]

- `CreateTerrain` (0x709B50) has one caller, the load; the height setter's other writers are the editor loader 0x74D1A0
  and 0x40B410 (no static caller). The field is static from the end of `CreateTerrain` to the next load
  (`asset-pipeline.md` 4.1, indirect calls not followed to the end). *Refined 2026-11-18 by 4900 (section 4.1): both
  are Nexus tool code (the terrain editor's heightmap load; 0x40B410 writes only its own stack grid), unreachable in a
  game [I]; the only live terrain edit in stock is the marsh stamp on the material layer.*
- The DLL announces only feature changes to the engine (`CvPlot::setFeatureType` calls `GameplayFeatureChanged`);
  `setPlotType` and `setTerrainType` only mark the plot's layout dirty (Architect 9.2 [C, the DLL fork]). The server's
  feed sees all three: its plot table reads terrain, plot type and feature through pure getters and refreshes them at
  every `LeaveUpdate`, with a `changeGen` per record (`server/src/feed/srv_feed.h`, `PlotRecord`).
- The server's bake already re-bakes a tile when its plot's terrain, plot type, feature or river/wonder bits change
  (`server/src/render/srv_residency_source.cpp`, `BakeRelevantChange`, issue 3308) [C]; it re-bakes from the stale field,
  so the picture does not change [I, to be seen live].

### P3. Who in the EXE reads the field [C, `world-systems-and-stubs.md` 7]

- Readers: HeightAtGrid 0x6D8B60, HeightAtWorld 0x530A10, HexToWorld / GridToWorld 0x6851C0 (Lua `HexToWorld`
  0x50F370, `GridToWorld` 0x63C280). Per frame: unit placement (0x42FA80 > 0x50EAC0 > 0x5024B0 > 0x530A10); also cities
  (0x537BF0 > 0x5BAC00 / 0x79C2D0 > 0x530A10), landmarks (0x5AB0C0), forest (0x439E00), overlay, the FOW cull
  0x6EA7A0, arrows (0x79AF20 > 0x6851C0), EUI unit flags and city banners (through Lua `HexToWorld`).
- Mouse picking intersects a flat plane at z = `[grid+0xB0] * -60` and does not read the field; only Strategic View
  picking calls HexToWorld [C]. So picking on new ground needs nothing from the field except in Strategic View.
- Which readers follow a data write at once was measured by 4901 (2.4) [L]: Lua HexToWorld, flag and banner placement
  and Strategic View picking follow at once; a unit takes the new height when it moves; an idle unit, the city model
  and banner, trees, resource art and the EXE's terrain drawing do not. *Corrected 2026-11-18 by lead-0465 from 4901's
  2.4; replaced: "Units sample every frame [C, the chain above]; the rest is open (2.4)" - the chain exists, but a
  standing unit keeps its height until it moves [L].*

### P4. The EXE's field is plain heap data [C]; writing it is a data write (D4)

- Layout (`asset-pipeline.md` 4.2 point 5, offsets live-confirmed 2026-11-11): the u16 layer is a directory of 16 x 16
  tiles at `[grid+0x1C]` (8 bytes per tile) over a pool at `[grid+0x28]` (512 bytes per tile); `Init` pools every tile,
  so every sample has its own pool slot; the u8 layer has the same shape at +0x5C / +0x68 (256 bytes per tile). Sample
  (x, y) is in tile `(y>>4)*tilesX + (x>>4)` at `(y&15)*16 + (x&15)`; height `z = u16 / 255 * 0.2`.
- A write of samples into the pool is a data write: allowed (D4), and foreseen by D132 rule R4 ("D81's second bake stage
  writes changed heights into the EXE's field in the same frame").
- The EXE's own region invalidation, the tile rectangle invalidation 0x4387D0 (`terrain-bake-residency.md` 3), is code:
  calling it would be an EXE call by address (D132 R2: needs the MC's yes, as under D136). In the product the EXE's
  terrain draw is off (D33) and the server draws the terrain, so the EXE's tile caches are not what the player sees [C,
  D33]; whether any reader that still runs keeps a derived copy is P3's open part.

### P5. "Live equals reload" cannot be bit-exact, and may not be local [L + I]

- Two loads of one save are not bit-identical: 28-43 samples differ by 1 u16 and one by 46 (0.036 world units), the
  material layer is identical (`asset-pipeline.md` 4.2 point 2, 2026-11-11 [L]); comparisons use `hfbake compare --tol 1`.
- `CreateTerrain` draws its `rand()` sequence for the whole map from `srand(mapRandomSeed)`: the coastline jitter
  (`rand() % 1000` per curve), the coast strip's special segment and the river mesh (`asset-pipeline.md` 4.2 point 2)
  [C]. A change that adds or removes a coastline therefore shifts the sequence for every later curve, so after a reload
  coasts far from the change may move [I: from the order; measured in 2.3]. If so, "live equals reload" holds for the
  changed region and its blend ring, and everywhere else stock itself changes the picture on reload; the acceptance
  compares the region exactly (tol 1) and the rest by rule.

### P6. The server's derived layers and where each comes from today [C, the files named]

| Layer | Source in the server | After a live change |
|---|---|---|
| Heights, material bytes | tap once per load (P1) | stale: T1 |
| Terrain colour (bake) | the server's own baker over heights, materials and the feed (`terrain-bake-*.md`) | re-baked on the change (P2) but from stale inputs |
| Ocean, coast, lakes, rivers | water mesh tap once per load (`srv_water_tap.h`, `water-system.md`); coast strips and river meshes are `rand()`-driven EXE code | stale: needs a server producer for the region |
| Polar ice | ice mesh tap once per load (`srv_ice_tap.h`, `polar-ice.md`) | stale |
| Decals (roads, fields, river bed) | decal tap generations (`srv_decal_tap.h`) and the server's farm fields (`srv_farmfields.h`) | follow the EXE's decal system; heights under them stale |
| Borders, highlights | the EXE's overlay target, per frame (`srv_overlay_tap.h`) | the EXE draws them from its own data |
| Model shadow map | the EXE's map, per frame (`srv_shadow_tap.h`) | follows what the EXE draws |
| Fog of war | the server's own, from the plot table (`srv_fow_scene.h`) | follows the feed |
| Trees | the server's forest scene from the feed and the cut tap (`srv_forest_scene.cpp`) | re-placed on a feature or plot-type change [C, line 575]; heights stale |
| Minimap | the EXE's own panel (0192 done; the server's minimap 0377 is backlog) | stock behaviour: 2.1 |
| Natural wonder models, cities | the EXE today; epic 0561 moves cities and landmarks to the server | 2.1, and shared with 0561 |

### P7. Natural wonders in the height step [C, `asset-pipeline.md` 4.2 point 1]

A natural wonder replaces the plot's pattern flags with its own (Fuji 0x40000, Mesa 0x80000, Uluru 0x400000, ...);
Fountain of Youth and El Dorado keep the terrain's; any other wonder, VP's own included, gets Fuji's 0x40000. This is
the community report's item (c) (`research/02-exe-internals/community-tile-looks-mountain-is-flat.md` 6): a producer
written from this description reproduces the Fuji fallback unless it is told otherwise, and the acceptance case wants a
neutral base instead. That is a deliberate departure from stock, so "live equals reload" does not hold for it; it needs
the MC's word at C1. *2026-11-24: built as an option with a load-time pass, so live and reload agree (8.13.2, 8.13.5;
ticket 0613).*

## 2. Premises that need a live run (T0 live, child 4901)

Run on a Goldberg instance, on copies of a save, on the current pins (COMMON 18), stock configuration first and the
product (64-bit server, D33) second. Each transition is made through the game's Lua (`Plot:SetPlotType`,
`Plot:SetTerrainType`, `Plot:SetFeatureType`), pictures before the change, after it (with 2 s and with a camera move
away and back) and after a save and a reload; popups closed first (D114).

- 2.1 What stock shows on each transition (land to water, water to land, flat to hills to mountain and back, grass to
  desert, a natural wonder added and removed): terrain, water and coast, rivers, trees, the wonder model, units standing
  on the plot, the minimap and Strategic View.
- 2.2 The same in the product.
- 2.3 The field's extent of change across a reload: tap the field after the change and after the reload
  (`hfbake compare --tol 1`), count the differing samples inside the changed plots plus a two-plot ring and outside it.
- 2.4 A data write into the EXE's field (a test-only writer, never shipped, no code byte): raise the samples of a few
  plots and see which readers follow at once (units, cities, flags and banners, Strategic View picking, landmarks) and
  which keep the old height until a reload.

Results below by w2-4901 (account B), 2026-11-18 17:01 EST onwards.

### 2.0 Setup of the live runs

- Instance **c5h-c** (Goldberg, `%USERPROFILE%\c5-instances`), stock route: the stock DX11 EXE, the 32-bit DLL pin
  **`i0ship-e96457fd-ia`** (pin batch 4, SHA-256 3eaa3290...e053603, set by `provision.py set-dll`), CP + VP + EUI
  5.4.8 (profile mods548), **Vibrant Textures** (D94). Instance files backed up by SHA-256 first
  (`tools/scenario/runner.py prepare`, backup under `C:/c5-64bit-work/0465/4901/stock1/backup/`).
- Save: **`C5H-0301-4901-LT_0008`**, a copy of `c5h-9028-tiny-t0008` (Tiny 56x36, wrapping in x, turn 9; SHA-256
  b2ceb663...c29163). Copies saved from it during the run (`UI.SaveGame` on the main thread):
  `C5H-0301-4901-LTA0` (land to water alone, SHA-256 2e7e31ba...4dafd3), `-LTB0` (A + flat to mountain alone,
  04d35407...0a0cd5), `-LTS1` (B + set 1, d004ee95...67de7), and the later ones named in 2.1.
- Drivers (all in `render/evidence/0465/4901/`, none in the product): `phases.lua` (the plots and every change, as a
  mod would make them: `SetPlotType(t, true, true, true)`, `SetTerrainType(t, true, true)`, `SetFeatureType(f)`),
  `drive.py` (Lua channel, picture rounds through the harness shot and Visual Checks, field dumps), `reload.sh`
  (harness stop, front-end prepare, launch, in game, dialog check), `fieldcmp.py` (2.3), `plotboxes.py` and
  `imgdiff.py` (pictures per plot), `fieldwrite.py` (2.4). Field dumps are read from outside with ReadProcessMemory
  (`shell/tools/gt_reader.py` for the u16 layer, the u8 layer read the same way at +0x5C/+0x68); a Tiny dump is
  4.4 MB (`.npz`), kept under `C:/c5-64bit-work/0465/4901/` until the merge.
- Studio plots around the active player's capital (51,14), revealed and made visible by `phases.lua` `setup()`:

  | Name | Plot | Before | Change |
  |---|---|---|---|
  | LW1 | (53,13) | flat desert, flood plains, river, coastal | to water (`PLOT_OCEAN`; the DLL makes it coast) |
  | LW2 | (55,15) | flat grass, forest, coastal | to water |
  | WL | (51,12) | coast water in the capital's bay | to flat (`PLOT_LAND`, then grass) |
  | FH | (50,16) | flat grass, a warrior of the active player placed on it | to hills, to mountain (plot type + terrain), back to hills (grass), back to flat |
  | TR | (52,16) | flat grass, forest, river | to hills (plot type only) |
  | GD | (51,17) | flat grass | to desert (terrain only) |
  | NW | (53,15) | flat plains | Uluru added (`SetFeatureType(21)`), then removed (`-1`) |
  | FM | (51,18) | flat grass | to mountain (plot type + terrain); 2.3's second case |

- DLL facts the run showed on the way [L]: land to water keeps a flood plains feature and the river flag on the new
  coast plot (LW1: feature 4, `IsRiver` true, also after the reload); a forest is removed by the DLL when its plot
  becomes water (LW2: feature -1). `SetPlotType` takes a fourth argument `bEraseUnitsIfWater` (the fork's
  `CvPlot::setPlotType`); with `bRebuildGraphics` true it only marks the plot's layout dirty, which reaches the EXE
  as `GameplayPlotStateChange` (resource, improvement, route) [C, CvPlot.cpp 7438 and `updateLayout`].

### 2.1 What stock shows on each transition [L, 2026-11-18 17:14-17:53 EST, setup of 2.0]

How it was run: one picture round per state (`drive.py round`): popups closed through the InGame popup queue
(D114, none was open), the camera on (52,15) at a fixed zoom (load default, 3 wheel ticks out, 1 in; the EXE's view
matrix checked against the first process' read after every load, `frame.sh`, max difference 0.11), a shot 2 s after
the change, a shot after `UI.LookAt` to (20,22) and back with 4 s settle (which did not move the camera: see the
correction below), and a Strategic View
shot (`ToggleStrategicView()`). States: before (`s0`), live after set 1 (`s1`, which includes LW1 and FM made before
the saves of 2.3), reload of `LTS1` (`r1`), live set 2 on that game (`s2`: FH hills to mountain, Uluru removed),
reload of `LTS2` (`r2`, SHA-256 6fc2f743...a574f), live set 3a and 3b (`s3a`, `s3b`: FH back to hills, then flat),
reload of `LTS3` (`r3`, 66c332e6...35fe76). Every reload is a new process. Pictures are compared per plot by
`plotboxes.py` (the plot centre projected with the EXE's camera, changed pixels in a box against the state before) and
by eye on the contact sheets (`C:/c5-64bit-work/0465/4901/stock1/shots/sheet_*.png`, not kept); the live field was
dumped after every set and compared with the load's (2.3, row 1).

Cells: **at once** (in the 2 s shot), **after camera away and back**, **only after reload**, **never** (not even after
the reload), n/a (the layer does not exist on that plot).

| Transition | Terrain colour | Heights | Water and coast | Rivers | Trees | Wonder model | A unit on the plot | Minimap | Strategic View |
|---|---|---|---|---|---|---|---|---|---|
| Land to water (LW1 river and flood plains, LW2 forest) | only after reload | only after reload | only after reload | only after reload (LW1: the river is drawn to the new coast) | **at once** (LW2: the DLL removes the forest, the trees go) | n/a | n/a (none placed; `bEraseUnitsIfWater` true kills it [C]) | only after reload | hex colour only after reload; the forest symbol goes **at once** |
| Water to land (WL) | only after reload | only after reload | only after reload | n/a | n/a | n/a | n/a | only after reload | only after reload |
| Flat to hills (FH, TR) | only after reload | only after reload | n/a | TR: river drawn as before, after reload on the hill | TR: kept at the old ground height live; on the hill after reload | n/a | FH: the warrior stays at the old (flat) height live; on the hill after reload | n/a (hills are not shown) | hills symbol **at once** |
| Hills to mountain (FH) and flat to mountain (FM) | only after reload | only after reload | n/a | n/a | n/a | n/a | FH: the warrior stays at the old height live, on the mountain after reload (r2) | mountain symbol only after reload | mountain symbol **at once** |
| Mountain to hills to flat (FH, set 3) | only after reload | only after reload (the mountain stays drawn live with the warrior on top) | n/a | n/a | n/a | n/a | stays on the drawn mountain live; flat after reload | only after reload | symbols **at once** (hills, then none) |
| Grass to desert (GD) | only after reload | n/a (flat both) | n/a | n/a | n/a | n/a | n/a | never (the minimap shows no desert colour, before or after) | hex colour only after reload |
| Natural wonder added (Uluru on NW) | only after reload | only after reload (the rock is in the field: z 12.0 before, 24.19 after the reload) | n/a | n/a | n/a | only after reload | n/a | never | wonder symbol only after reload |
| Natural wonder removed (NW, set 2) | only after reload | only after reload (live the rock stays; z 12.02 after the reload) | n/a | n/a | n/a | only after reload | n/a | never | the symbol stays live, gone only after reload |

- **No cell is "after camera away and back"** [L, re-run 18:20-18:24]. *Correction 2026-11-18 18:24 (w2-4901): the
  camera rounds of s0-r3 did not move the camera. `UI.LookAt` ignores a plot outside the area the camera may reach:
  with only the studio revealed, the camera can be panned about 8 plots and no further, and `UI.LookAt` to (20,22),
  (26,18), (26,31) or (45,3) left the camera's position unchanged, read from the EXE. It replaces "the camera round
  changed nothing anywhere".* The re-run: the whole map revealed (`SetRevealed` only, no visibility), a new live set
  C on the r3 game ((53,14) land to water, (54,13) water to land, (51,16) flat to mountain, (50,17) flat to hills
  under the warrior, (54,18) grass to desert, (54,15) Uluru added), the camera panned by a held arrow key (posted)
  1,856 world units (about 29 plots, half the Tiny map, measured from the EXE's camera) for 5 s and back: 40 s later
  every plot's box equals the 2 s shot (changed pixels 0-307 of 6,400, animation), so nothing moved. The frame 4 s
  after the return lacks units, flags and resource icons (the same frame kind as in the earlier rounds); it is not a
  terrain change. A pan across a huge map was not tried; mountain to flat and wonder removal were only seen with
  the non-moving rounds. Visual Checks: set C 2 s `vc-20261118-182236-workstation-d789`, after the pan
  `-182318-...-8d1c`, Strategic View `-182346-...-0f17`, 40 s later `-182444-...-6e07`.
- So in stock **only feature removal and the Strategic View's plot-type symbols follow a change at once**; everything
  that comes from the height field, the material layer or the load-time meshes waits for a load. This is P2 live [L],
  and it is why the scenario runner never changes plot types (`scene-catalogue.md` 2).
- Strategic View is split [L]: its plot-type symbols (hills, mountain) and a feature removed by the DLL follow at once,
  but its hex colours (land, water, desert) and the natural-wonder symbol (added or removed) only after a reload.
- Units sample the field every frame (P3), so a live change leaves them standing at the old drawn ground, which is
  consistent with the picture; nothing floats or sinks [L].
- A round-trip FH flat > hills > mountain > hills > flat does not come back bit-exact: after the last reload 3,369 of
  the plot's 4,096 samples differ from the first load by up to 60 u16 (0.047 world units), because its neighbours
  changed meanwhile (TR hills, GD desert, FM mountain) [L].
- Capture notes: in two of 27 round shots the units and their flags were missing (`s2_live_back`, a frame with no
  interface at all, retaken as `s2_live_retake`; `r1_reload_back`), marked in Visual Checks; not a terrain effect.
- Visual Checks ids (issue 4901, all DIAGNOSTIC, D112), per state t2s / back / sv: s0 `vc-20261118-171500-...-2880`,
  `-171508-...-2f3f`, `-171512-...-358e`; s1 `-171638-...-5823`, `-171646-...-dff8`, `-171651-...-5d84` (and the
  full-scale `-171821-...-65a1`, `-171830-...-3af2`, `-171836-...-d942`); r1 `-173715-...-b01b`, `-173723-...-a7f0`,
  `-173727-...-8cc7`; s2 `-173912-...-1b3e`, `-173920-...-67f9` (artefact), `-173924-...-d58d`; r2 `-174617-...-f37a`,
  `-174625-...-0be0`, `-174629-...-2f35`; s3a `-174719-...-6371`, `-174727-...-369a`, `-174731-...-2219`; s3b
  `-174738-...-c503`, `-174747-...-c06e`, `-174751-...-3d5f`; r3 `-175207-...-abc5`, `-175215-...-7442`,
  `-175219-...-e2a4` (all `vc-20261118-HHMMSS-workstation-XXXX`). The reload shots are the stock references for 2.2.

### 2.2 The same in the product [L, 2026-11-18 18:40-19:05 EST]

*(Replaces the "not run" note of 18:35: C: was freed at 18:36, MC #6135.)*

Set-up: c5h-c, the product as shipped: `shell/evidence/3308/run_3308.py prep` with the batch-4 pin layout
`x64-c29442e8-p58724e0` (proxy 3DFD7521..., server.exe FC73E60E..., x64 DLL 2B56A1CD..., checked after the prep;
both logs' build lines `repo 58724e072 dirty=0 dll_pin c29442e8c`), `shell_composite.dll` 2B30EA27... built by
`shell\build.cmd` from `git archive` of 58724e072 (shell, render, options, server, models, deps; 45 s; build tree under
`C:/c5-64bit-work/0465/4901/pp/`), the pin's own `shell_composite.ini` with 3308's composite keys
(`render/evidence/0465/4901/pp_keys.py`; main's ini differs), `[Renderer] ServerRenderer = 1` (server.log: "from the
file"), D33 with Overlay kept on, vpproxy.ini mode=on; 0 LOUD lines in server.log, vpproxy.log and
shell_composite.log. Saves: `LT_0008` (sets A, B and 1 made live, as in 2.1), then `LTS1` reloaded (the stock run's
save of the same changes) and set 2 made live on it. The whole map revealed in each process so the camera can pan;
rounds by `pp_round.sh` (2.1's round, the pan of 1,856 world units, and a shot 40 s later). The product keeps the load
zoom 14.26 (the posted wheel did not change it on this route; stock rounds are at 18.26), so the per-plot boxes use the
product's own camera read. Restored by `run_3308.py restore` (backup restore ok, `set-proxy --original` rc 0, front end
restored, the huge save copy removed, `provision.py verify` rc 0). Vibrant Textures (D94).

| Transition | Product, live (2 s, after the pan, 40 s) | Product, after the reload | Different from stock (2.1)? |
|---|---|---|---|
| Land to water (LW1, LW2) | terrain, heights, coast unchanged; LW2's trees go **at once** (the server's forest) | drawn as water with the new coast | no |
| Water to land (WL) | unchanged | drawn as land | no |
| Flat to hills (FH with the warrior, TR) | heights unchanged; **TR's trees are re-placed at once** (a new, sparser layout, at the old ground height); the warrior stays | hills, trees on them | **yes: TR's trees change at once** (stock: never live) |
| Hills to mountain (FH, set 2), flat to mountain (FM) | unchanged, the warrior on the old ground | mountain (FM) | no |
| Grass to desert (GD) | unchanged | desert | no |
| Uluru added (NW) | no rock; the plot's wheat art is drawn pale in the 2 s frame and as loaded by 40 s (a re-bake of the plot from the stale inputs, P2) [I for why] | the rock | the 2 s flicker only |
| Uluru removed (NW, set 2) | the rock stays | (not reloaded on the product) | no |
| Strategic View | hills and mountain symbols at once, as stock (the EXE draws it) | as stock | no |
| Minimap | the EXE's (0192); not separately compared | — | — |

- So the product shows a live terrain change exactly as stock does, except that **the server's forest follows a
  feature or plot-type change at once** (P6: `srv_forest_scene.cpp` re-places trees on such a change) — still at the
  old ground height, because the field is the load's tap. That is the first live layer the product already has.
- Window shots on the product path are PrintWindow frames: some came out black (`p1_live_sv`) or without the EXE's
  interface, units and cities (a known trait, proxy-design.md 20); they are diagnostics only (D112) and marked in
  Visual Checks; the per-plot boxes compare terrain and trees, which those frames do show.
- Not done on the product: the field through the server's tap (2.3 and 2.4 are about the EXE's field, which the
  server copies once per load, P1), the reload of `LTS2`, set 3, and the ice and water taps of 4.8.
- Visual Checks (DIAGNOSTIC, issue 4901), t2s / back / sv per state: p0 `vc-20261118-184754-workstation-dcbf`,
  `-184832-...-9c38`, `-184853-...-38ef` (40 s `-184934-...-2909`); p1 live `-185011-...-d057`, `-185033-...-42cf`,
  `-185037-...-7f07` (black; retake `-185239-...-4106`; 40 s `-185108-...-480c`, with the stock s1 picture as its
  reference); p1 reload `-185842-...-e9b0` (popup), `-185915-...-3308`, `-185928-...-99ef` (40 s `-190012-...-0d95`,
  stock r1 as reference); p2 live `-190221-...-c925`, `-190308-...-4512`, `-190320-...-e29d` (40 s
  `-190347-...-8432`).

### 2.3 How far a reload changes the field (P5) [L, 2026-11-18]

Fields dumped from the running EXE after the load (u16 heights and u8 materials, 3584 x 2560 samples), compared
sample by sample with `fieldcmp.py` (the test of `hfbake compare --tol 1`: a sample differs when |a - b| > 1).
Samples are given to the plot whose centre (Lua `HexToWorld`) is nearest; a plot owns 4,096 samples; "ring" is hex
distance 1-2 from the changed plot, "outside" the rest including the 4 margin rows.

```
python -I render/evidence/0465/4901/fieldcmp.py F0_load1.npz FA_reload.npz --centres centres.txt --plots 53,13 --tol 1
python -I render/evidence/0465/4901/fieldcmp.py FA_reload.npz FB_reload.npz --centres centres.txt --plots 51,18 --tol 1
```

| Case (both sides loaded from a save) | inside (4,096) | ring 1 (24,576) | ring 1-2 (73,728) | outside (9,097,216) | max |Δ| inside / ring / outside | materials differing inside / ring / outside |
|---|---|---|---|---|---|---|
| live field before any change (load of `LT_0008`) vs live field after set 1, same process | 0 | 0 | 0 | 0 | 0 | 0 / 0 / 0 |
| land to water (53,13): load of `LT_0008` vs load of `LTA0` | 4,096 | 13,115 | 20,488 | 9,506 | 8,202 / 7,197 / 3,519 | 3,280 / 1,315 / 58 |
| flat to mountain (51,18): load of `LTA0` vs load of `LTB0` | 4,096 | 11,964 | 11,964 | 3 | 38,980 / 21,441 / 50 | 4,002 / 1,120 / 0 |

- **The reload's change is local [L].** Flat to mountain changes its own plot and ring 1 only; the 3 samples outside
  are at hex distance 5, 24 and 29 and two of them recur in the other case: the known load noise (P5's first point).
- **Land to water reaches 3-4 plots along the coast, not the whole map [L].** Of the 9,506 samples outside the
  two-plot ring, 9,503 lie in 9 plots at hex distance 3-4, all on the same coastline: (53,10) 3,457, (54,10) 1,389,
  (55,15) 1,364, (0,15) 1,006, (0,16) 921, (0,14) 583, (53,9) 428, (52,9) 191, (52,10) 164 (x = 0 is next to x = 55,
  the map wraps). The 3 others are single noise samples. So the whole-map `rand()` sequence (P5's second point) does
  **not** move distant coasts on this map: the changed coastline curve is re-made (and smoothed) along its own length,
  and the other curves keep their jitter. Why the order does not shift them is not established [I: each curve may
  draw its own jitter, or the curves are drawn in an order where this one came last]; a map with more coastlines
  after the changed one in the EXE's order would test it (open).
- The live field does not change at all during play (row 1, bit-identical heights and materials after eight changes
  of all kinds): P2 confirmed live [L].
- Consequence for the acceptance (R3): "live equals reload" can be judged on the changed plots plus a ring of 2 for
  plot-type changes away from water; outside that, at most a few noise samples differ (tol 1). **Corrected
  2026-11-19 (lead 3, from 4906's stock route, 6.7) [L]:** land/water changes are **not** local in general: 4906's
  LW (2 plots), WL (2 plots) and the large change (7 plots) re-rolled 195,597, 1,690,208 and 1,235,711 samples
  outside ring 4, as whole plots at hex distance 5-26 (other piece variants, P5's `rand()` shift). The single LW1 case
  above, which reached only 3-4 plots along its coastline, was not representative. So for land/water changes "live
  equals reload" holds only up to the reload's own re-roll and cannot be judged region-exact; changes away from water
  can (6.7: inside + ring 1 only). *(Replaced: "and plus the length of the changed coastline (here 4 plots) for
  land/water changes".)*

### 2.4 Which EXE readers follow a data write into the field [L, 2026-11-18 17:58-18:06 EST, stock route]

Run on the reloaded `LTS3` game (2.1, c5h-c, stock route of 2.0). `fieldwrite.py raise` wrote raised heights into
the u16 pool of the EXE's GridHeightmap from outside (WriteProcessMemory into committed private read-write pages
only, checked with VirtualQueryEx before every write; no image page, no code byte; D4) at five plots: FH (50,16) with
the warrior, the capital (51,14), the wheat plot (53,15), the forest TR (52,16), a resource on hills (50,13); every
sample within 30 samples of the plot centre, with a cosine falloff over the outer third; 16,308 samples. First +8
world units (u16 +10,200), restored, then +30 (u16 +38,250), restored; after the last restore the dumped field equals
the load's dump bit for bit (heights and materials). Pictures at half and full scale, compared per plot at the old and
the new projected height (`plotboxes.py`; +30 world units is about 55 back-buffer pixels up at this camera).

| Reader | After the write | Evidence |
|---|---|---|
| Lua `HexToWorld` (and so EUI's flag and banner placement, Strategic View picking, P3) | **at once**: z 12.03 > 20.03 (+8) and 12.02 > 42.02 (+30) | Lua answers right after the write |
| Terrain drawing (the EXE's terrain tiles) | **does not follow**, also not after a camera move to (20,22) and back | no change in any box |
| A unit standing on the plot (the warrior on FH) | **does not follow**: it stays on the drawn ground, flag unchanged | boxes at +8 and +30, also after the camera round |
| A unit moved onto a raised plot (`SetXY` to (53,15)) | **takes the new height at the move**: the figures stand about 30 units above the drawn ground, their shadows on the ground; the flag sits higher by the same amount | `w2_raised_moved_full`, the shadows apart from the figures |
| The city (51,14): model and banner | does not follow | no change at +30 |
| Trees (forest TR) | do not follow | no change at +30 |
| Resource art (wheat field, the hills resource) | does not follow | no change at +30 |
| Strategic View picking | not tested by mouse; its height source (`HexToWorld`, P3) follows at once | — |

- So the per-frame chain of P3 (units 0x42FA80 > ... > 0x530A10) does not make an idle unit follow [L]: a unit's
  height is taken when it is placed or moved, and kept [I: cached in the unit's scene node]. This corrects P3's
  "Units sample every frame [C, the chain above]" for a standing unit: the call chain exists, but an idle unit does
  not move with the field. (P3 is section 1's; the lead edits it, sent in the hand-back.)
- For T1/T2 this means [I]: writing new heights into the EXE's field (D132 R4's "second bake stage") is enough for
  the readers that ask on demand (`HexToWorld`: flags, banners, SV picking; units at their next move), and not
  enough for the ones that placed once: units standing still, cities, trees, resource art. Those need their own
  refresh (a re-placement) or come from the server (epic 0561 for cities and landmarks).
- No box, no assert and no crash during the writes (dialog marker clear) [L]. The warrior was left floating on
  (53,15) in that test game; the save copies were not changed by the writes.

## 3. Risks (as of 2026-11-18)

- R1 T1's size: the matcher and the piece library are undecoded (P1); this decides the epic's size more than any
  other item.
- R2 Water and coast meshes and ice are taps of once-per-load EXE products (P6): land to water needs a server producer
  for coast strips and possibly rivers, `rand()`-driven in the EXE.
- R3 Live equals reload is not exact and possibly not local (P5): the acceptance needs a rule per layer. *(2026-11-19,
  6.7 and 2.3's correction [L]: local for changes away from water (inside + ring 1); not local for land/water changes,
  whose reload re-rolls whole plots at distance 5-26, so those are judged against the reload only up to its own
  re-roll.)*
- R4 Readers in the EXE that cache heights (P3, 2.4) may need an EXE call (the MC's yes) or wait for increment 3.
- R5 Overlap with epic 0561 (cities and landmarks in the server, natural wonder models) and with ticket 0552 (the
  minimap's fog) and 0377 (the server's minimap).

## 4. The engine-reuse route (child 4900; the MC's #5965, D85)

Question: can the EXE be made to rebuild its own field, materials and water, coast, river and ice meshes for a
changed region during play (by a data write or an approved means, D136/D165), so that the server only re-taps? If
yes, T1 and most of T2 shrink to triggering and re-tapping, and live equals reload by construction where the rebuild
is the load's own code. If no, the server-side re-bake of P1 is sized instead. Results are added here by 4900.

**Method (2026-11-18 16:52-, w1-4900).** Static only so far. Ghidra 12.1.4 headless, read-only, `-noanalysis`, on a
scratch copy of the `civ5` project (the EXE copy SHA-256 b2ce96ae..., `reverse-engineering-setup.md` 2) with
`evidence/2900/2904/StrXrefDecomp.java` (modes r, f, s), an `llvm-objdump` listing of the same copy, and two small
scratch readers of the image (the static Lua registry table, raw pointer search). Scratch:
`C:/c5-64bit-work/0465/4900/` (deleted after the merge); nothing from it is committed (D83). The DLL side was read in
the fork's `CvGameCoreDLLUtil/include/ICvDLLUtility.h`. Nothing in 4.1-4.4 was run live.

**Result in one paragraph.** The EXE has **no regional producer** of heights, materials, coast strips or the river
mesh: each is a whole-map step of `CreateTerrain`, whose working inputs (coastline polylines, piece placements, region
masks) are temporaries freed when it returns, and nothing in play calls any of them. The only live terrain edits the
EXE makes are on the **material layer**: the marsh stamp (from `GameplayFeatureChanged`) and the Nexus terrain
editor's brush (a tool mode, not a game). Two whole-map rebuilds are reachable without a code byte, through the EXE's
own Lua globals: **`Ice_Reload`** (the ice mesh, from a per-plot ice grid that is plain .data) and **`Waves_Reload`**
(the coast-wave mesh, from the DLL's current plots). So the route is **closed for heights, materials, coast and rivers**
(regionally, and in play at any size except by re-running the load's terrain step through EXE calls by address,
about 8-13 s on the huge save and about 2 s on a small one, 4.4) and **open in part for ice and coast waves**. Section 4.5 sizes the server-side re-bake.

### 4.1 Every EXE path that writes the field, the material layer or the terrain meshes [C unless marked]

Callers were listed from Ghidra's references (direct calls and data pointers; indirect calls through computed
pointers are not in that list, so "no caller" means "no static reference").

| Path | Writes | Region or whole map | Frees / allocates | Thread | Also touches | Reached in play? |
|---|---|---|---|---|---|---|
| `CreateTerrain` 0x709B50 (only caller `LoadGameState` slot 7 0x60ABB0, at 0x60BBD1) | everything below, in the order of `asset-pipeline.md` 4.1 | whole map | allocates the grid layers (Init), the piece library (+0x75D58), the river VB/IB, the ice VB/IB, the TerrainRenderer (0x5BCEF0, 0x29D1C B, stored last at TS+0x75FA4); frees its own temporaries at its end (the polyline and placement buffers, through 0x813CF0) | load thread (the load state machine) | `srand(mapRandomSeed)` first; decals laid before the renderer exists (rivers, flood plains, farm tracks 0x6E27D0); RoadLayout, City, Marsh, the sub-grid 0xD16AEC (`world-systems-and-stubs.md` 7) | no; a second run on a live map needs the teardown 0x6684D0 first (it frees the renderer, the lighting textures, the ice, the river and Marsh state and zeroes `[0xD90FC4]`) [C for the frees, I that nothing else blocks a re-run] |
| `GridHeightmap::Init` 0x47E350 (callers 0x709B50, 0x74D1A0, 0x40B410) | both layers allocated and zeroed, ready byte +9 = 1 | whole grid | allocates (pools every tile) | caller's | none | no. Correction: 0x40B410's call inits a **64 x 64 grid on its own stack** (the Nexus unit viewer's, see below), not 0xD91270 |
| Block blend 0x6BA6F0 > 0x425060 | heights (setter 0x5A3490) and material bytes (0x775130) per 64 x 64 block, in job bodies 0x608270 > 0x70B490 | takes the map's width and height and runs every block; no region argument. A single block is a job object (vtable 0x9E04E8) that only EXE code builds | the masks and placements are CreateTerrain's temporaries | job threads (0x56B480), waited on by the load thread | none outside the field | no |
| Coast strips 0x639D40 (called only at 0x70AD84) | heights and material bytes along the coasts (job body 0x626500 > 0x6990D0) | whole map (args: map size, the coast curve list, TerrainDef) | temporaries | jobs + load thread (`rand` twice on the load thread) | CRT rand state | no |
| River mesh 0x6D51D0 (only at 0x70AE88) and the carve 0x412680 (only at 0x70AEF3) | river VB/IB + record (River 0x1A0C694); heights through 0x5FC1C0 > 0x46AEF0 and 0x4A7DD0 > 0x7A1FD0 > 0x772510; material bytes through 0x7A1FD0 > 0x5C8C10 (a writer of 0x775130 not named before; the eight are 0x70B490, 0x6990D0, 0x5C8C10, 0x46AEF0, 0x6B5A50, 0x678FF0, 0x74D1A0, 0x40B410) | whole map | frees and remakes the river handles (0x6B1580) | load thread (`rand` in 0x631990, 0x54BAB0) | CRT rand state | no; the engine's RiverTileAdded handler 0x44B5C0 only sets a bit in a per-plot grid (TS+0x50C+0xCC), it builds nothing |
| Ice step 0x5A0BC0 (callers 0x70AF1C and **0x440200**) > builder 0x4956F0 | ice VB/IB, chunk records, built flag Ice+0xFEA5 (Ice = 0xD68F9C, `polar-ice.md` 3) | whole map | 0x440200 frees the old mesh first (0x537190) | the caller's (load thread, or the Lua thread for `Ice_Reload`) | reads the ice-plot grid and the field; no rand | **yes, through `Ice_Reload`** (4.2) |
| Coast-wave mesh 0x67C170 (callers 0x54BE70 from GVS::StartupGame 0x4692E2, and **0x62A9E0**) | Wave_System chunks (`water-system.md` 9.4) | whole map; reads every plot's terrain through the DLL (`ICvPlot1` +0x70: TERRAIN_COAST 5 / OCEAN 6) | remakes the chunks | caller's | its own LCG, not the CRT's | **yes, through `Waves_Reload`** (4.2) |
| Marsh stamp 0x6B5A50 (via 0x6A74D0; callers Marsh::OnMarshPlot 0x630B70 and the remove handler 0x7620B0) | **material bytes only**: the marsh material over a 128 x 128-sample square around the hex, only where the height lies in 13,000..18,000; the remover looks up the hex's record and stamps a material derived from it (0x7B3F80) [I: the one before the marsh] | **region** (one hex's square) | none; calls the tile invalidation 0x4387D0 over the square when the renderer exists | event dispatch of the A bus [I: main thread] | the EXE's tile caches (+0x1CC, +0x300) | **yes**: listeners registered by Marsh::Startup 0x517880 (0x630B70 / 0x7620B0; 0x47ACC0, called from TS Shutdown 0x6CCA90, hands the same two to 0x85F960 / 0x85FA60 [I: unregisters]), fed by `GameplayFeatureChanged` (4.2) |
| Nexus terrain-editor brush 0x678FF0 (only caller 0x61ECE0, only caller 0x4A3130, a slot of the .rdata vtable at 0xA7958C) | material bytes of a 32 x 32-sample square around the brush, then 0x4387D0 | region | none | the editor's input handler | - | no: a method of the terrain editor below |
| Nexus heightmap load 0x74D1A0 (no static reference) | re-Inits 0xD91270 from a caller's dense u16 and u8 arrays and writes every sample through 0x5A3490 / 0x775130, then builds **its own** TerrainRenderer (0x60FE40, `TerrainEditorSystem.cpp`, 0x29D1C B) | whole grid | allocates a renderer | - | - | no. It is the `TerrainEditorSystem` of the `-Nexus` tool mode (WinMain 0x602380 tests "-Nexus"; "Nexus Version Mismatch ... terrain editor version" at 0x9EA060) [C strings, I that the object exists only in that mode] |
| Nexus unit viewer 0x40B410 (pointer at 0xA7A034 only, no reference to it) | its own 64 x 64 stack grid (0xA13 everywhere), not the game's | - | - | - | registers "SetWindow", "SendUnitTypes", "ReloadUnitLibrary", ... | no |
| Dense export 0x493DD0 | reads only | - | - | - | - | no callers |

Notes to the table:

- *Correction 2026-11-18 (4900) of `terrain-decals.md` 5.1 and `terrain-bake-residency.md` 3*: 0x678FF0 is the
  **Nexus terrain editor's material brush** (32 x 32 samples, centred on the brush), not a map-script or plot-type edit;
  its only caller chain is 0x61ECE0 < 0x4A3130 (editor input handler). 0x678FD0, the function just before it, is
  unrelated: it registers 0x4B30A0 into a Lua table through 0x904E30 (its callers 0x4518D0 "Parse Map Scripts",
  0x4CC270, 0x60D180 are Lua set-up, not terrain edits). And 0x6B5A50 is the marsh stamp, so "terrain edits (map
  scripts, plot type changes)" should read "the marsh stamp and the editor brush". Replaced there: "0x678FF0 ... called
  from 0x61ECE0 ... terrain edits (map scripts, plot type changes)". Those files are not this ticket's; the
  correction is noted here and asked for in the hand-back.
- *Correction 2026-11-18 (4900) of `world-systems-and-stubs.md` 7 and `asset-pipeline.md` 4.1*: 0x40B410 does not
  write the game's field (its Init is on a stack grid); the game's field has two Init callers that matter, 0x709B50
  and 0x74D1A0, and the latter is unreachable in a game [I].
- Nothing writes heights after `CreateTerrain` returns (`asset-pipeline.md` 4.1 stands, extended here by the marsh
  stamp and 0x5C8C10 on the byte layer).

### 4.2 Triggers that could start a rebuild during play without a code byte

| Kind | Found | What it rebuilds | Needs |
|---|---|---|---|
| A data write the EXE polls | **none for the field, materials, coast or river meshes**. Two dirty bytes exist for decals: TS+0x27818 (0xD3DDF8) +0x2B1A0 (flood plains) and +0x2B1A1 (river banks); 0x559FF0 re-lays that decal kind while its byte is 0 and 0x625E3D sets it back to 1. Their runner 0x7857F0 has no static reference and no pointer in the image, so whether it is polled per frame is **open** [C bytes, I runner] | river-bank and flood-plain **decals** only (from the per-plot river grid), not the river water mesh | a data write (allowed, D4) once the runner is known |
| A vtable or import slot | none that runs a terrain step in play. The steps are reached only from `LoadGameState` (.rdata vtable 0xA20EBC slot 7); swapping the heap object's vptr (the old c3 idea) means re-entering the load | - | - |
| An engine event the DLL already sends | `GameplayFeatureChanged` (slot 172 > 0x426EF0) is the **only** per-plot terrain-ish notification: `ICvDLLUtility.h` has no plot-type, terrain-type or river change method [C, the fork's header]. The handler switches on the old and the new feature: marsh (2) posts the marsh add/remove events (4.1 row "Marsh stamp"); flood plains (4) sets bit 2 in the per-plot grid of 0xD3DDF8 (+0x40A0) and clears its dirty byte +0x2B1A0 (the listener 0x6BF2A0 > 0x6F3830, registered by 0x5C9E30 and 0x6E3C50; that the handler's post 0x6F93C0 reaches it is [I]); jungle (1), forest (5), oasis/fallout (3, 6) and natural wonders post model events [I: the ids are the base game's feature rows]. **Ice (0) has no case**: a feature change to or from ice reaches nothing | marsh material stamp (region), flood-plain decals [I runner], feature and wonder models | nothing new: stock does it |
| The EXE's own Lua globals | the static registry at 0xCC2320 (count 0xCC6320, at most 0x400 entries, filled by static initialisers 0x9C5xxx, pushed into a new Lua state by 0x905D00 from 0x44B8B0) holds 68 names, among them `HexToWorld`, `GridToWorld`, `ToggleStrategicView`, `InStrategicView`, `IsGameCoreBusy` (UI globals mods use) and the debug hooks **`Ice_Reload`** (0x77CC30), **`Waves_Reload`** (0x757850), `Water_SetAutoReload`, `Water_Set*`, `ReloadTextures`, `FOW_SetAll`. There is **no** `Terrain_*`, heightmap or river reload | `Ice_Reload()`: frees and rebuilds the whole ice mesh from Ice+0xA0 (a 255-byte-stride per-plot grid in .data, bit 0x80 = ice; set at load by 0x639850 via 0x42E940, cleared by 0x6F6210) and the current field. `Waves_Reload()`: re-reads `WaveSettings.xml` and rebuilds the coast-wave chunks from the DLL's current plots | a call of an EXE function, though through its own Lua binding from a UI Lua state: **named here as needing the MC's yes (D132 R2 / D136)**. That they are globals of the UI states is [I, strong: same table as `HexToWorld`, which EUI calls as a global]; a read-only `type(Ice_Reload)` in the Lua channel settles it |
| An EXE call by address | (1) a whole-map terrain rebuild: the teardown 0x6684D0 (this 0xD165E0) then 0x60ABB0's work (plot table from the DLL, then 0x709B50 with `mapRandomSeed`), on the load-thread context; (2) the ice step alone, 0x440200(0xD91270) on Ice (what `Ice_Reload` does); (3) the wave builder 0x62A9E0; (4) a block blend for one block (build a job object): no inputs left after the load | (1) everything, whole map, equal to a reload by construction (the same code, the same seed, the DLL's current plots) apart from the 28-43-sample blend noise of P5 [I] | the MC's yes for each (D132 R2); (1) also every system that holds a pointer into the freed state (the taps of 4.3, City, RoadLayout, Marsh, the decal records) [I, not traced] |

Ice through a data write: the per-plot ice grid is .data inside the Ice object, so setting or clearing bit 0x80 of a
plot's byte is a plain data write (D4). Followed by `Ice_Reload`, the EXE would rebuild the ice for the new ice
plots. The byte's index is the hex-to-grid conversion of 0x42E940 (through 0x56D480 / 0x4B68E0, row stride 255) [C],
not yet reproduced exactly; that and the call need a live test [I].

### 4.3 The composite's taps and whether each re-fires after such a rebuild [C, own code]

| Tap (file) | Re-copies when | After a data write into the field | After `Ice_Reload` / `Waves_Reload` | After a whole-map rebuild (teardown + CreateTerrain) |
|---|---|---|---|---|
| Height field + material bytes (`shell_composite.cpp` `TapOnPresent`, C5HF) | a new grid seq (any change of the grid key: `[0xD90FC4]`, ready byte, sample counts, offsets; or every 600 frames a re-read of the constants) or a new `requestSeq` from the server | **no** by itself (the key is unchanged); yes when the server raises `requestSeq`, and then it copies the **whole** field (huge: 132,120,704 B, copy 462.4 ms, `proxy-route-compositor.md` [L]); no region copy exists | not affected | yes: the teardown zeroes `[0xD90FC4]`, so the key changes twice (gone, back) and the gate opens a new load copy [I that the gate's reload conditions (scene frames) are met in play] |
| Water (`water_tap.cpp`: river VB/IB and records, wave chunks) | a new grid seq, or the server's `requestSeq`; one copy per load otherwise | - | **no** for `Waves_Reload` unless the server asks (`requestSeq`); the copy then takes the new chunks | yes (new grid seq) |
| Ice (`ice_tap.cpp`) | the built flag, the VB/IB handles or the chunk array differ from the last copy and stay equal for `ice_settle_frames` (30) | - | **yes, by itself**: new handles in the same load are its designed case (`polar-ice.md` 9) | yes |
| Decals (`decal_tap.cpp`, generations) | the decal change signal moves (`terrain-decals.md` 5.2) and stays still for `decal_settle_frames` | - | - | yes, as decals are re-laid; the renderer pointer goes 0 and back [C, `terrain-decals.md` 5.3] |
| Bake tap (`bake_tap.cpp`, the EXE's tile bits) | per its own reads of TS+0x75FA4's tiles | - | - | the old renderer is freed: the tap must see the pointer change [I] |

### 4.4 Cost (acceptance (d)) and the whole-map rebuild as a documented option

**What the MC allowed (2026-11-18, #6011 replaced by #6013).** (1) Yes, test only and never shipped, on c5h-b or
c5h-c with a save copy and a SHA-256 backup and restore: time `Ice_Reload()` and `Waves_Reload()` from the UI Lua
state, before and after one ice-bit data write. (2) No live run of the whole-map rebuild by address and no build of
such a caller: it is sized from the load records below and kept as an option for the C1. No product use of any EXE
call is approved; the C1 puts the two routes side by side for the Architect and the owner.

**(1) Approved by the MC (#6011/#6013), test only; not run by 4900.** c5h-b and c5h-c were claimed (worker-4871
and w2-4901) and F: was on hold (MC #5993), so the run moved to 4901 (W2, on c5h-c), lead-0465 2026-11-18 17:30
(#6024). The ice rebuild alone and the wave rebuild alone have no recorded time [open]. The recipe as handed over:

1. Set-up: a save copy with ice at the poles (Tiny `C5H-...-TINY_0008` copies have it, `polar-ice.md` 3), the
   instance backed up by SHA-256 (AGENTS.md 4), a dialog watcher, stock configuration first, then the product (D33 +
   server, `ice_tap=1`), and say which.
2. Read only, in the UI Lua state (the in-game UI context, not the DLL's): `print(type(Ice_Reload),
   type(Waves_Reload), type(HexToWorld))`. All three should be `function`; anything else closes the route for that
   hook. Both hooks take no arguments, and with one they only log "could not be called".
3. Baseline: `python -I shell/tools/ice_probe.py -i <inst> --tag before --out ice.json` (built flag, VB/IB handles,
   chunk count; reads only). List the ice plots with `shell/evidence/0169/4301/find_ice.lua`.
4. Time the calls: from the Lua channel, `Ice_Reload()` 5 times, then `Waves_Reload()` 5 times, with the wall-clock
   round trip of each call and of a no-op call as the baseline (`os.clock` is process CPU time, so it is not wall
   time). Each call blocks the UI thread while it runs [I], so the round trip is the stall. After each `Ice_Reload`,
   `ice_probe` should show new VB/IB handles with the same chunk count.
5. The ice-bit write (a data write into .data, D4; never a code byte): the per-plot ice grid starts at live VA
   `module base + 0x96903C` (= Ice 0xD68F9C + 0xA0 at ImageBase 0x400000), with a row stride of 255 bytes. Ice+0x94
   and +0x98 hold the grid's two dimensions and +0x9C its wrap flag; bit 0x80 means ice. The plot-to-byte mapping
   (0x42E940) is not reproduced, so calibrate first: read the W x H bytes (row stride 255) and match the set 0x80 bits
   against `find_ice.lua`'s ice plots. Then clear 0x80 in the byte of one ice plot at a pole edge, keeping the other
   bits, and save the old byte. Call `Ice_Reload()` once, timed, and take `ice_probe` plus a diagnostic picture of that
   pole (D112, D114, Visual Checks with `--issue 4901`). Write the old byte back, call `Ice_Reload()` again, and
   check that `ice_probe`'s chunk boxes and counts equal the baseline.
6. Taps, in the product run: the composite log (`Logs/shell_composite.*.log`) should show the ice tap copying again
   after about 30 settled Presents on the new handles, with a new generation (`polar-ice.md` 9). The water tap does
   **not** re-copy after `Waves_Reload` by itself. Raise `requestSeq` in its control block (`VPShellWaterTap`,
   `shell_shared.h`; our own shared section, not EXE memory) by one from the test script, and look for a new copy with
   the new wave chunk handles in the log.
7. End: the instance restored by hash, and every box seen gets its root cause (COMMON 30).

**(2) The whole-map rebuild, sized from the records [L, indications, workstation, not COMMON 23]:**

| Map | `StartInitTerrainFromGameplayMap()` | Source |
|---|---|---|
| Huge `VP8P-HUGE_0350`, t350, B4 configuration, Release 5.4.8 DLL | 7,703 ms | 2026-11-09, `load-sequence.md` 2 (n = 1) |
| the same, A5 configuration | 9,180 ms | the same |
| Huge `C5H-0317-HUGE_0350` (= `C5H-VP8P-HUGE_0350`, C499D3C6), product path (D33 + server), x64 pin 04b1211b8 | 8,825 / 9,936 / 10,283 / 10,541 / 12,672 ms (five loads, c5h-e on F: and c5h-c on C:) | 2026-11-15, `shell/evidence/0317/data/system_init-*.log` |
| two loads today on c5h-b and c5h-c, stock configuration (no composite log), the map not identified | 2,216 / 1,832 ms (whole load 5,958 / 5,127 ms) | 2026-11-18 17:06-17:15, the instances' `Logs/system_init.log`, read only [L, map unknown; a small map by the load total, I] |

So on huge a rebuild costs about **8-13 s** per change (the product path's five loads give 8.8-12.7 s, median 10.3 s),
on a small map about 2 s, the same for one plot and for a flooded continent. The step's log line covers the plot table
(0x60ABB0) and `CreateTerrain`; a rebuild in play would add the teardown and whatever decal and landmark re-layout
follows (the load's 18 s farm-field gap on huge comes after this step, `load-sequence.md` 1.1; whether a rebuild sets
it off again is [I, not traced]).

**Its risks [C where an address is given, I otherwise]:**

- **Where it can run.** The step belongs to the load state machine. In play it would have to run on the thread that
  draws frames, between two frames, so the picture freezes for the whole time. And the DLL's game thread must be idle
  (`IsGameCoreBusy` false), because 0x60ABB0 reads every plot through `ICvPlot1` [I].
- **The teardown in play.** 0x6684D0 frees the TerrainRenderer (TS+0x75FA4 = 0), the lighting textures, the ice
  (0x537190), the river and Marsh state, and zeroes `[0xD90FC4]`. Until the rebuild ends, the per-frame terrain update
  and draw (0x51B070, 0x657070) and every field reader (units 0x530A10, `HexToWorld`, picking, which returns while
  `[0xD91279]` is 0) see no map; whether each is guarded is not known for the update and draw paths [I]. Re-Init of a
  live grid is what every in-process reload already does, so the field's memory is reused, not leaked
  (`asset-pipeline.md` 4.2 point 2 measured a reload in one process) [L for the reload, I for play].
- **State the rebuild repeats.** It lays the river, flood-plain and farm-track decals again (before the new renderer
  exists) and replays the marsh records (0x63B0F0). Whether the teardown removed the old ones first is not traced, so
  duplicates are possible [I]. It writes into RoadLayout, City and the sub-grid 0xD16AEC as at a load
  (`world-systems-and-stubs.md` 7), and the new renderer registers its event handlers again (0x678290: HexFOWStateChanged
  > 0x701BB0), possibly twice [I]. `srand(mapRandomSeed)` resets the CRT `rand` state of that thread, which only
  cosmetic users read (the DLL has its own generator) [I].
- **What the player sees.** Units follow at once (they sample the field every frame, P3). Cities, landmarks and
  banners that cache a height are P3's open part. Picking works again once the ready byte is 1. The server's fog comes
  from the plot table and is not touched; the EXE's own FOW state lives in the renderer it rebuilt [I].
- **What the taps and the server do.** The field tap's key changes twice (`[0xD90FC4]` 0 and back), so it starts a
  new load copy (132 MB, about 0.5 s on huge). The water tap copies again on the new grid seq; the ice tap sees new
  handles; the decal tap sees the renderer pointer go to 0 and back and a new decal generation (4.3). The server takes
  a new load seq as a new load: a full scene reload, with its own whole-map re-bake [I: `srv_render_scene.cpp` treats a
  new load seq as a new field; the bake cost on huge is in `terrain-bake-residency.md` 4].
- **Live equals reload holds by construction** for this option (the same code, seed and plots), apart from P5's
  28-43 samples of blend noise [I].

### 4.5 Verdict and the size of the server-side re-bake (acceptance (e))

**Verdict (2026-11-18, static).**

- **Heights, material bytes, coast strips, river mesh: closed** for a regional rebuild. Reason: the EXE has no
  regional producer (4.1); the per-block blend jobs exist but their inputs are freed when `CreateTerrain` returns, and
  no data write, slot or event starts any of them (4.2). A whole-map rebuild exists only as EXE calls by address
  (teardown + the load's terrain step). It costs about 8-13 s on huge (median 10.3 s over five loads on the product
  path) and about 2 s on a small map, whatever the size of the change. It freezes the picture while it runs, frees
  state that other systems and our taps hold, and needs the MC's yes. It is a documented option for the C1 (4.4):
  changes are rare, so a stall per change against a reload is a real choice. It is not "drawn at once".
- **Ice: open in part.** A data write of the per-plot ice bit plus the EXE's own `Ice_Reload` rebuilds the whole ice
  mesh, and the ice tap re-copies by itself. Needs the MC's yes for the call and a live test of the index and the cost.
- **Coast waves: open in part.** `Waves_Reload` rebuilds them from the DLL's current plots; the water tap needs a
  `requestSeq` to take them. Same yes and test.
- **Marsh: already live in stock** (material stamp + tile invalidation from `GameplayFeatureChanged`); the server's tap
  of the material layer is once per load, so the server misses even this until it re-taps.
- **The consumer side stays open**: the server can write its own new heights and material bytes into the EXE's field
  as data (D4, D132 R4) for the EXE's readers (P3, P4). The route fails on the producer, not on the consumer.

**Size of the server-side re-bake (T1 + the water parts of T2), from `asset-pipeline.md` 4.1-4.4 and this section
[I, estimates]:**

| Part | Port, hours | Unknowns | Settling the unknown first, hours (included in the port) |
|---|---|---|---|
| Coastline curves (step 3): `rand() % 1000` jitter, spline resampling (0x4595B0) and smoothing (0x6545F0), whole map, replaying the CRT sequence | 6-10 | the exact number of `rand` calls before steps 6 and 7 | 2-4: a live count of the CRT state (MSVCR90's per-thread seed) before and after each step on two saves, by reads only |
| Hex pattern matcher (`HexPatternMatcher::ComputePatternPositions` not located; its mangled name at 0xA2B7E0 has no reference) | 10-20 | where it is (reached through an indirect call); whether it draws `rand` (P5); the natural-wonder rules (P7) | 4-8: find it statically from the readers of the HexPatternCell table 0x60ABB0 fills, then confirm by a live read of its output |
| `TerrainPiecesLibrary.fxsb` (27,595,584 B, first dwords 20, 385024, 0, 2, 4, ...), or the flat-file path under `assets/terrain/pieces` | 8-16 | the format beyond its header | 6-12: decode the header and the piece records, cross-checked against the flat files of the same pieces |
| Block blend (step 5): region masks with flood fill (0x6414D0), the piece sampler, the weighted average per 64 x 64 block, mirrored pieces (0x463B10), material = the piece with the highest `_T_BLEND` | 8-14 | x87 precision of the accumulation | 2-3: compare against a live dump with `hfbake compare --tol 1`; ±1 u16 is accepted (`asset-pipeline.md` 4.5) |
| Coast strips (step 6) with the special segment | 6-10 | the strip geometry; which strip and segment the two `rand` draws pick | 2-4: decompile 0x6990D0's writes into a description, check one strip against a dump |
| Rivers and lakes (step 7): the river mesh 0x6D51D0 and the carve 0x412680 (triangle raster, the 0.1753 floor, blend to a target height) | 8-14 | the river mesh's `rand` use (0x631990, 0x54BAB0); how lakes differ from rivers in the carve | 2-4: as for the coast strips; the floor is settled (`asset-pipeline.md` 4.1 step 7 as corrected by 0556) |
| Region selection (the changed plots plus the blend ring), a "live equals reload" rule per layer, checks against live dumps | 8-12 | P5's non-locality of the jitter (how far a changed coastline moves other coasts) | 2-3: measured by 4901 (2.3) |
| **Total** | **54-96** | | of which settling: 20-38 |

Against 2904's 40-80 h (whole map, no region work) and the Architect's T1 16-30 h, which counted neither decode.

The two decodes (matcher, fxsb) are the largest risk: until one of them is done, the lower bound is not reliable.
A cheaper alternative [I] would be a local re-bake from a piece-free model of the server's own, not the EXE's pieces.
It would differ from a reload and so break "live equals reload", so it is no substitute under the current acceptance.

### 4.6 Region producers in the server for water, coast, rivers and ice (for the C1) [C inputs, I hours]

What each mesh needs, beyond the new field and material bytes, if the server makes it itself for a region:

- **Ocean and lake surface: nothing more.** It is built every frame from the camera, one quad per visible block with
  no land test, and its depth comes from the height pages (`water-system.md` 8.4, 10.2). The server already builds it
  this way, so new heights give new water at once. Lakes have no mesh of their own [I: no lake mesh was found; the
  carve only lowers the field].
- **Coast look: the field and the bake.** The coast is the heights that step 6's strips write plus the bake's colour
  from the material bytes, so it comes with T1's region re-bake (step 6 included) and the server's bake (P2). There
  is no separate coast mesh in the EXE except the coast waves.
- **Coast waves: the plots' terrain and the wave settings.** 0x67C170 reads each plot's terrain (coast 5 / ocean 6)
  through the DLL, plus `WaveSettings.xml`, and keeps its own LCG (seed Wave+0x04), so it needs nothing from
  `CreateTerrain` and the feed already carries terrain. A region port would rebuild the chunks that touch the
  region. The algorithm is not described in the records yet; port 6-10 h, plus the 36-byte chunk and 16-byte wave
  layout of `water-system.md` 9.4. Cheaper, if the MC agrees: `Waves_Reload` plus the server's `requestSeq` (4.2).
- **River water mesh: the river records, the coastline curves and the `rand` stream.** 0x6D51D0 builds from the
  per-plot river edges and flow directions (0x60ABB0's second pass; the feed has the river bits) and the curves of step
  3, and it draws `rand` after steps 3 and 6. A region can rebuild the rivers that cross it, but equality with a
  reload needs the stream position, which needs steps 3 and 6 replayed for the whole map (P5) [C order, I count].
  Covered by the "Rivers and lakes" row of 4.5.
- **Ice: the ice plots, `IceSettings(_Low).xml`, `IceDisplacement.dds` and the water height.** No `rand` and no other
  `CreateTerrain` input (`polar-ice.md` 3), so it is independent of T1. It is chunked by pole quarter, but its
  crack and floe structure runs over all the ice hexes it collects, so a region rebuild is in practice a chunk or
  pole rebuild [I]. A server port of the builder 0x4956F0 (lattice, cracks, floes, displacement) is not described yet:
  port 12-20 h. Cheaper, if the MC agrees: the .data ice bit plus `Ice_Reload`, and the ice tap re-copies by itself
  (4.2, 4.3).

### 4.7 Not verified / open (2026-11-18)

- Nothing in section 4 was run live. (1) of 4.4 (timing `Ice_Reload` and `Waves_Reload`, the ice-bit write) is
  allowed by the MC and moved to 4901, with the recipe in 4.4. The exact index of a plot's ice byte (0x42E940) is not
  reproduced; the recipe calibrates it. *(Update 2026-11-18 18:30, w2-4901: the stock half of (1) ran; results and the
  calibration in 4.8.)*
- That the registry's names are globals of the UI Lua states is [I, strong]. A read-only `type(Ice_Reload)` settles it.
  *(Settled 2026-11-18 by 4.8 [L]: all three are functions in InGame and in `_G`.)*
- The runner of the river and flood-plain decal re-layout (0x7857F0) has no static reference: whether it is polled per
  frame, which would make its two dirty bytes a data-write trigger, is open.
- The thread of the marsh listener and of `GameplayFeatureChanged`'s posts is [I].
- Indirect calls were not followed to the end. "No regional producer" rests on direct references, the job vtables
  named and the absence of any terrain name in the Lua registry or in `ICvDLLUtility.h`.
- The map of today's two instance loads (2.2 and 1.8 s) is not identified.

### 4.8 Live timing of Ice_Reload and Waves_Reload (4901) [L, 2026-11-18 18:07-18:30 EST, stock route]

Run by w2-4901 for 4.4 (1) (MC #6011/#6013, test only, never shipped) on c5h-c, stock route of 2.0 (stock DX11 EXE,
32-bit DLL `i0ship-e96457fd-ia`, CP + VP + EUI 5.4.8, Vibrant Textures), in the reloaded `C5H-0301-4901-LTS3` game
(Tiny 56x36, 272 ice plots), the instance backed up by SHA-256 (2.0). Driver `render/evidence/0465/4901/iceitem.py`;
`shell/tools/ice_probe.py` for the ice object. The product half (the ice and water taps) belongs to the product run
(2.2).

- **The hooks [L].** In the InGame state (the in-game UI context) and in `_G`, `type(Ice_Reload)`,
  `type(Waves_Reload)` and `type(HexToWorld)` are all `function`. Both reloads ran with no argument, printed nothing,
  raised no error and opened no box.
- **Time [L, indication: workstation with two other teams' heavy jobs running (a server release build and an HI
  phase run, `quietlock.py check` 18:06), not COMMON 23; Lua channel round trips, wall clock, n = 5 each, 1 s apart]:**

  | Call | Round trips (ms) | Median |
  |---|---|---|
  | no-op (`return 1`), before | 130.4, 132.4, 97.6, 132.4, 135.4 | 132.4 |
  | `Ice_Reload()` | 263.7, 262.3, 248.2, 238.1, 239.4 | 248.2 |
  | `Waves_Reload()` | 75.5, 133.7, 97.1, 132.6, 137.4 | 132.6 |
  | no-op, after | 129.0, 98.0, 133.0, 100.3, 32.9 | 100.3 |
  | `Ice_Reload()` single calls around the bit writes | 344.1, 355.9, 272.6, 213.3 | — |

  So on Tiny `Ice_Reload` costs about **115 ms** over the channel's own round trip (248 - 132; the singles say
  80-225 ms), and `Waves_Reload` is not distinguishable from a no-op at this resolution (the channel's round trip
  varies by about 100 ms, its poll granularity [I]). Neither number scales a huge map; the ice mesh here is 96,785
  primitives in one chunk.
- **Does Ice_Reload rebuild [L].** After each call the ice object has new VB/IB handles (0x588/0x56A > 0x304/0x326 >
  0x2A1/0x29E > ...) with the same chunk count (1), the built flag 1 and, when the input is unchanged, the same sizes
  (VB 489,112 B, IB 580,710 B, 96,785 primitives).
- **The ice-bit write [L].** Calibration: the 56 x 36 bytes at module base + 0x96903C (VA 0xD6903C = Ice + 0xA0), row
  stride 255, read as `byte[y * 255 + x]`: bit 0x80 is set on exactly the 272 `FEATURE_ICE` plots and on no other
  (the transposed reading matches 26); Ice+0x94/+0x98 hold 56 and 36, +0x9C holds 1 (the map wraps). The write cleared
  0x80 in the byte of (45,4), a south pole-edge ice plot (0x80 > 0x00, the address inside the EXE's `.data` section by
  the section table read from the process, page protection PAGE_READWRITE; no code byte), twice. After `Ice_Reload`
  the mesh is smaller: VB 487,480 B (204 vertices fewer at stride 8), IB 579,228 B, 96,538 primitives (247 fewer).
  The byte written back (0x80) and `Ice_Reload` again: VB 489,112 B, IB 580,710 B, 96,785 primitives, as before.
- **What redraws [L].** The ice is redrawn at once after `Ice_Reload` (3 s shot). With the one bit cleared the ice
  outline changed **along the whole visible south pole edge**, not only at (45,4): the edge pieces are laid anew. With
  the bit restored the outline is the loaded one again (pixel differences only in the water's animation). So the ice
  rebuild is deterministic for a given grid, and one changed plot re-lays the edge far from it [I: the edge pieces
  draw from one random sequence, as P5 supposed for the coasts]. For a live-terrain producer this means the ice of a
  region cannot be rebuilt alone with the EXE's code without moving the rest of the pole's outline.
- The first 3 s frame after the first `Ice_Reload` (`ice1_cleared`, camera not on the pole) showed the fog of war gray
  and no units or interface; a later frame was normal. Not investigated; the same frame kind as 2.1's capture notes.
- Visual Checks (DIAGNOSTIC, issue 4901): the pole before `vc-20261118-182812-workstation-0897`, bit cleared 3 s
  `-182821-...-9206` and 13 s `-182852-...-df4c`, restored `-182928-...-21fc`; camera test `-182730-...-b8b9`.
- **Not done:** the product half (the ice tap re-copying on the new handles, the water tap after `requestSeq + 1`);
  `Waves_Reload`'s effect on the wave chunks (no water probe was run); a huge map. *(Update 2026-11-19 22:05, 0586:
  the product half ran live (10.6): the ice tap's new generations and the water tap's copy after `requestSeq + 1`, 533 to
  531 waves after `Waves_Reload` on `lw`; a huge map is still open.)*

## 5. Offline harness: what the scenes need (lead's draft for C1, 2026-11-18) [C for the existing contract, I for the plan]

- A scene bundle holds one height field per load (C5HF member, `research/06-tooling-and-workflows/offline-scene-harness.md`
  2.1), one water image per load (2.2) and the ice per load (2.12). A live change makes these inputs change inside one
  load, so a transition scene needs each of them as a sequence in the bundle (a new field, water or ice generation at a
  recorded frame), with the recorder writing it and the player replaying it at that frame. Under rule A
  (`offline-scene-harness.md`, "The two rules") that bundle change lands in the same merge as the first code that reads
  a field, water or ice image during a load.
- Scenes per transition, each recorded three times on the same build: before the change, after it (live) and after a
  save and a reload; the acceptance compares live with reload by the rules of section 2.3's result.
- The test mod (`tools/scenario/mods/liveterrain/`, epic touches) makes the transitions on fixed plots of a Tiny save copy
  through the game's own Lua calls (`Plot:SetPlotType`, `SetTerrainType`, `SetFeatureType`), so that a scene can be made
  again by a command, and a "large change" (a whole landmass to water) gives the cost case.
- *As built by 4906 (2026-11-18, w4-4906): the mod, the scene chain and the bundle design are in section 6. Two
  refinements of the draft above: (1) the recorder writes one bundle per server process (its keys are read at the
  start, offline-scene-harness.md 9 and 15), so a bundle per state comes from a chain of processes, with the live change
  made inside a recorded span (6.4); (2) the bundle already names a second copy of one load
  (`heightfield/load<NNNN>-<k>.c5hf`, `water/load<NNNN>-c<copySeq>.wtap`), so the generations need an activation
  record and a player that switches at a frame, not a new file name (6.5).*

## 6. The test mod, the transition scenes and the generations design (ticket 4906, T4) [C for the code, I where marked]

Written 2026-11-18 20:39- EST by w4-4906 (account B), branch `w4-4906` off `epic-0465-extras-c-live-terrain`
(b19000fb4). **Status (2026-11-19 01:10): the offline parts built and tested (6.1, 6.3, 6.5, 6.6); both routes'
scenes recorded live on c5h-a (6.7 stock, 6.8 product and the bundles).** *(Replaces "no scene is recorded yet: no
game instance was granted".)* 6.2 and 6.4 are the runbook the live part followed.

### 6.1 The test mod (`tools/scenario/mods/liveterrain/`) [C, offline tests; L, the live run of 6.7]

- `liveterrain.lua` is loaded by `lt.py` through the harness Lua channel into the InGame state and lives in the
  channel's persistent `vp` table (`vp.liveterrain`, version 1); `lt.py` loads it again when it is missing (every new
  process) or of another version. **Nothing is installed into the game**: no mod folder, no `.modinfo`, no file of the
  instance changes (decision of w4-4906: the DLL sees the same Lua calls a real mod would make, and the instance needs
  no backup beyond the harness's own). The calls are the game's own only: `Plot:SetPlotType(t, true, true, true)`,
  `Plot:SetTerrainType(t, true, true)`, `Plot:SetFeatureType(f)` (rule E1); no memory write, no EXE call, no
  `Ice_Reload` / `Waves_Reload` (the offline test asserts their absence).
- Transitions, one command each (`lt.py -i INST apply ID`), on the fixed plots of 2.0 plus MW (51,18): `lw` (LW1 and
  LW2 to water), `wl`, `fh_hills`, `fh_mountain`, `fh_hills_back`, `fh_flat`, `gd`, `nw_add` (Uluru on flat),
  `nw_remove`, `mw_add` (MW flat to mountain with `FEATURE_KILIMANJARO`), `mw_flat` (MW made flat, the wonder kept:
  0554 (a)), `ice_remove` / `ice_add` (`FEATURE_ICE` off and on the pole-edge water plot IC (45,4) of 4.8), and
  `lt.py large [--landmass ID]` (a whole landmass to water; the cost case, 6.6). The table with each
  transition's calls and checked starting state is the mod's README.
- Each command returns **the changed plots**: the whole map is snapshotted before and after, and every plot whose plot
  type, terrain, feature, river, lake, city, improvement, resource, route or units differ is listed with both states
  and its `HexToWorld` z. So the DLL's own side effects are in the list (from `CvPlot::setPlotType`: a new land plot
  turns adjacent deep water to coast; `erase` removes the feature, resource, improvement and route of a plot that
  becomes or stops being water; `setFeatureType` forces flood plains on a river desert plot) [C, the fork's
  CvPlot.cpp 294-339, 7225-7443, 7544-7560].
- Exit codes: 0 done, 1 a Lua error, 2 usage (refused by `lt.py` before the game: unknown transition, a plot not
  `X,Y`, a type name not `UPPER_CASE`, a save name not `C5H-0301-4906-*`), 3 no game / timeout / save not completed,
  4 refused by the mod (nothing changed). Every id is validated in Lua before the DLL sees it: `setFeatureType` ASSERTs
  on a bad index (CvPlot.cpp 7546) and a failed ASSERT opens a system-modal box under the channel's Lua lock
  (`run-and-drive-the-game.md` 3); a plot made water that holds a city is refused (`erase` would kill the city). The
  large change refuses a landmass with a city, unit, owned plot, improvement or natural wonder (an owned plot with an
  improvement crashed the DLL from the channel once, `run-and-drive-the-game.md` 3).
- Negative commands for the live run (each exit 4, `rec` unchanged): `set --plot 99,99 --pt PLOT_HILLS`, `set --plot
  50,16 --feat FEATURE_NOT_A_ROW`, `set --plot 50,16 --pt PLOT_NOPE`, `apply fh_mountain` on a fresh copy (FH is flat),
  `set --plot 51,14 --pt PLOT_OCEAN` (the capital).
- Offline tests: `PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 python -I -m pytest -q tools/scenario/mods/liveterrain/tests`
  (19 passed, 2026-11-18 20:55): the transition ids, the usage refusals, exit codes 0/1/3/4 through a fake channel,
  the save wait, the centres file. The Lua has no offline run (no Lua interpreter on the workstation); it ran live
  in 6.7 (every command as designed; mod v2 after the landmass rule was widened for an unowned ancient ruin).

### 6.2 The scene list (the chain) [I, the runbook]

Every reload is a new game process loaded from the save the step before made, so **reload of transition k = before
of transition k + 1**, and one load per transition covers all three states. Order (each starts from the state the one
before leaves): 0 `setup` on a copy of `C5H-0301-4901-LT_0008` saved as `C5H-0301-4906-ST00` (stock) or `-PT00`
(product; the same save bytes are not reused across routes, each route makes its own chain), then the changes away
from water first (the beta's staged subset, MC #6462 on D195: land<->water comes after 0.1b): 1 `fh_hills`,
2 `fh_mountain`, 3 `fh_hills_back`, 4 `fh_flat`, 5 `gd`, 6 `nw_add`, 7 `nw_remove`, 8 `mw_add`, 9 `mw_flat`,
10 `ice_remove`, 11 `ice_add` (camera on (45,6)), then 12 `lw`, 13 `wl`, 14 `large` (last: it may move coasts, 2.3),
each saved as `-ST<k>` / `-PT<k>`, and a last process that loads `-ST14` / `-PT14` and only captures (reload of 14).
15 processes per route. A run cut short after k = 11 still holds every transition of the staged subset.

| State | Recorded | Where (outside git, D83; D133/D141) |
|---|---|---|
| before k (= reload k-1) | the EXE's field, both layers (`drive.py field`, .npz); the camera; a Visual Checks picture at (52,15) 3 s after the camera move; with `--sv` Strategic View; the named plots' record | `F:/c5-64bit-work/0465/4906/scenes/<route>/<kk>_<id>_before*` |
| live k | the same 2 s after `apply`, plus the apply record (the changed plots) | `..._live*`, `..._apply.json` |
| reload k | = before k+1 (12: the last process) | |
| product, per process | one scene bundle whose span holds the frames before and after the change (6.4) | `F:/c5-64bit-work/0465/4906/bundles/PT<kk>_<yyyymmdd-hhmmss>_workstation` |

**Commands (the runbook; `W=C:/c5-64bit-work/0465/4906`, `F=F:/c5-64bit-work/0465/4906` (F: only outside its
holds), `S=render/evidence/0465/4906/scenes.py`, `LT=tools/scenario/mods/liveterrain/lt.py`; owner env w4-4906;
`pool.py list` first, then the claim):**

```
# stock route: backup by SHA-256, the 32-bit pin, the save copy (byte copy of c5h-9028-tiny-t0008 = LT_0008's source)
#   --dll pin: provision.py set-dll's current pin (2.0 ran on i0ship-e96457fd-ia, batch 4; COMMON 18 at run time)
python -I tools/scenario/runner.py prepare --instance c5h-b --work $W/stock --dll pin \
       --save-src c5h-9028-tiny-t0008 --save-as C5H-0301-4906-LT_0008
python -I tools/harness/cli.py -i c5h-b launch            # no RenderDoc; then: drive.py ingame, dialogwatch marker
python -I $LT -i c5h-b setup ; python -I $LT -i c5h-b save C5H-0301-4906-ST00
#   negative commands of 6.1 here, each exit 4, rec unchanged (on this unsaved process state)
for k, id in 1 fh_hills, 2 fh_mountain, ... 9 mw_flat, 10 ice_remove, 11 ice_add, 12 lw, 13 wl, 14 large:
  python -I $S reload -i c5h-b --save C5H-0301-4906-ST<k-1> --log $W/stock/reload.log
  python -I $S step   -i c5h-b --route stock --out $F/scenes/stock --k <k> --transition <id> --save C5H-0301-4906-ST<k> --sv
python -I $S reload -i c5h-b --save C5H-0301-4906-ST14 --log $W/stock/reload.log
python -I $S capture -i c5h-b --route stock --out $F/scenes/stock --tag 15_reload --sv
python -I tools/harness/cli.py -i c5h-b stop --settle 10 --human-turn-hook tools.harness.turns:stop_readiness
python -I tools/scenario/runner.py restore --instance c5h-b --work $W/stock

# product route: x64-c29442e8-p58724e0 (D33 + server), the composite built by shell\build.cmd from git archive of
# 58724e072 (as 2.2), run_3308.py prep (backup, set-proxy, composite, D33 minus Overlay, vpproxy keys), then the
# recorder's extra file (issue 0307, TEST ONLY) named in vpproxy.ini
#   layout = %USERPROFILE%/c5-i0/dlls/x64-c29442e8-p58724e0 (bin/: server.exe and the x64 DLL; proxy/bin/: the proxy)
python -I shell/evidence/3308/run_3308.py prep --instance c5h-b --work $W/pp --bin <layout>/bin \
       --proxy <layout>/proxy/bin/CvGameCore_Expansion2.dll --composite $W/pp/shell
python -I render/evidence/0465/4901/pp_keys.py --instance c5h-b --work $W/pp \
       --pin-ini $W/pp/src/shell/composite/shell_composite.ini     # the pin's ini + run_3308's composite keys (run sets them, prep not)
python -I proxy/tools/live/set_ini.py <instance>/.../vpproxy.ini server_cfg_extra=$W/pp/extra.cfg
python -I $S copysave -i c5h-b --src c5h-9028-tiny-t0008 --to C5H-0301-4906-PL_0008      # the product chain's own copy
#   process 0: reload PL_0008 with a bundle folder (it records the loaded state), setup, save PT00; then per k:
python -I $S reload -i c5h-b --save C5H-0301-4906-PT<k-1> --log $W/pp/reload.log --extra-cfg $W/pp/extra.cfg \
       --bundle $F/bundles/PT<k>_<yyyymmdd-hhmmss>_workstation --after 1500 --frames 16 --dump all
python -I $S step -i c5h-b --route product --out $F/scenes/product --k <k> --transition <id> --save C5H-0301-4906-PT<k> \
       --bundle $F/bundles/PT<k>_... --before-dumps 3
recorder_test.exe --stamp <bundle> save.name=... save.sha256=... save.turn=... repo_commit=... build_config=release pins.dll_lock=...
recorder_test.exe --check <bundle>                         # exit 0 "scenebundle: OK"
python -I proxy/tools/live/set_ini.py <vpproxy.ini> --unset server_cfg_extra
python -I shell/evidence/3308/run_3308.py restore --instance c5h-b --work $W/pp     # set-proxy --original, restore by hash
python -I $S index $F/scenes --out $F/scenes/index.json ; python -I $S index $F/bundles --out $F/bundles/index.json
```

`render/evidence/0465/4906/chain.sh INSTANCE stock|product OUT WORK [FIRST_K]` runs the loop above (setup and save of
process 0, then k = 1..14 and the last capture) with a new bundle folder per process on the product route, stops at
the first failed step and on a `STOP` file in OUT, and resumes from FIRST_K.

In the product loop the `step`'s before capture runs while the server counts to frame 1500 (900 was about 37-43 s after the
game is up, 3904); the capture takes about 10-20 s [I], so the wait for the third dump follows it. A run whose span
started before the capture ended is seen in the step's output (`bundle_dumps_before_change` above 3) and is recorded
again. The save copies live in the instance's `ModdedSaves/single` and are removed (or kept for the merge) at the end;
nobody else's save is touched.

Compares (6.3): the field of live k against reload k with the changed plots of `apply` (`regioncmp.py`); the field of
before k against live k must be bit-identical (P2, 2.3 row 1); the live picture against the reload picture per changed
plot (`piccmp.py` with `scenes.py rects`); stock against product per state. Visual Checks: issue 4906, every picture
DIAGNOSTIC (D112), popups closed first (`popups.lua`, D114).

### 6.3 The compares (`render/tests/liveterrain/`) [C, tests]

- `regioncmp.py A B --centres C --plots X,Y ... [--tol 1] [--rings 4]`: A and B are `drive.py field` dumps or C5HF
  taps (the bundle's heightfield member). Samples go to the plot whose centre is nearest (rows 0.866 apart; x wraps);
  samples farther than 0.75 plot from every centre (the 4 margin rows) go to no plot. Per region **inside**, **ring 1**,
  **ring 2**, **ring 3**, **ring 4** (each ring apart: the plots at hex distance k from the nearest changed plot) and
  **outside**: plots, samples, samples with |Δ| > tol, max |Δ|, material bytes differing; then the outside differences
  per plot with their hex distance. This is 2.3's counting (4901's `fieldcmp.py`), with the rings split and the C5HF
  form added. 2.3's "ring 1-2" is ring 1 + ring 2.
- `piccmp.py A B (--rect X0,Y0,X1,Y1 ... | --mask M) [--diff D.png]`: per pixel the largest channel difference; over
  the region the median, p95 and max (COMMON 25), mean, the counts over --thr (24) and over 8, and a difference image
  (A | B | |A-B| x 4 of the region's box, outline drawn, outside dimmed).
- Input checks, each with a negative test (exit 2, "REFUSED"): regioncmp 24 (file missing, unknown form, a bad npz,
  an npz without the grid, sample counts, grid geometry, a one-sided material layer, wrap flags, the centres file
  missing / header / line / a plot missing / a plot twice / a plot outside, the field not fitting the map, a centre
  outside the field, no plot / plot text / plot outside / plot twice, tol, rings, C5HF truncated, C5HF flags);
  piccmp 12. Positives: identical fields, inside only, each ring apart, outside with its distance, `--rings 2`, the
  tolerance (|Δ| = 1 passes at tol 1), materials, the wrap distance, margin rows outside, C5HF equals npz,
  `--heights-only`; pictures: identical, a known difference inside and outside the region (median 0, p95 40, max 40
  for 100 of 1,600 pixels), a mask.
  `PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 python -I -m pytest -q render/tests/liveterrain`: 50 passed (2026-11-18 21:00).
  Not registered with ctest: the render suite's Python may lack numpy and PIL, and a failing environment would break
  the suite for everyone (decision of w4-4906).
- **Not checked against 2.3's numbers [open]:** 4901's dumps (`F0_load1.npz`, `FA_reload.npz`, ...) were deleted with
  its scratch (`C:/c5-64bit-work/0465/4901/` absent on 2026-11-18 21:00), so the tool's count could not be compared
  with 2.3's table (inside 4,096, ring 1 13,115, ring 1-2 20,488, outside 9,506 for land to water). The first live
  `lw` pair of 6.2 repeats that case and is the check: inside must be the plot's samples, and ring 1 + ring 2 and
  outside of the same order as 2.3's.

### 6.4 The scene bundle per state, and which members cannot hold a mid-load change [C for the recorder, I for the plan]

- One bundle per server process: `render_test_record` is read at the server's start and the folder must be empty
  (offline-scene-harness.md 9, 15). So every process of the product chain gets a new folder, written into the
  `server_cfg_extra` file that `shell/evidence/3308/run_3308.py prep` points vpproxy.ini at (`scenes.py reload
  --extra-cfg WORK/... --bundle F:/.../PT<kk>_...`; that the proxy re-reads the file at each launch was [I], [L] since 6.8; the first
  reload checks it: the new folder must fill).
- The span holds both states of the process: keys `render_test_record_after=1500` (900 started the span about 37-43 s after
  the game was up in 3904's runs; 1500 leaves the before capture time), `_frames=16`, `_dump=all` (draws about 250 ms
  apart, so about 4 s of game time; as run in 6.8).
  `scenes.py step --bundle DIR --before-dumps 3` waits until the recorder has written 3 dumps, then applies the
  transition: frames 1-3 (and the following ones until the DLL's next `LeaveUpdate` reaches the feed) are **before k**
  (= reload k-1), the later ones **live k**; `feed.bin` carries the plot change between them (P2 replays it). The
  process's last state, reload k, is the first frames of the next process's bundle. The state pictures of the driver
  are taken outside the span (before the wait and 2 s after the change); the span's frames are the server's own dumps.
- Members and a change inside one load:

  | Member | Holds a mid-load change? | Why |
  |---|---|---|
  | `heightfield/load<NNNN>.c5hf` | **no** | one copy per load; a later copy of the same load only after a new grid seq or the server's `requestSeq` (4.3), which a live terrain change does not make. Live the server draws the load's field (P1), so the bundle is right for today's product; it is what T1 changes |
  | `water/load<NNNN>.wtap` | **no** | the same: once per load, re-copied only on a grid seq or `requestSeq` (4.3) |
  | `ice/gen<N>.ice` (harness 2.12) | **yes, by generation**, but the mod's transitions make none | the ice member is already per generation, and `shadow.bin` names per frame the ice generation drawn (`iceGeneration`, bit 2); the player switches the mesh at the frame whose record names a new one [C, harness 2.12]. A new generation comes only from new handles (4.3), which only `Ice_Reload` makes; the mod never calls it |
  | decals (format 3) | yes, by generation | the decal tap records generations as the EXE re-lays decals; for these transitions stock re-lays none live except the marsh stamp (4.1), which the mod does not make [I] |
  | `feed.bin` (plot tables, units) | **yes** | the plot tables of every `CopyPlots` and drain, in call order: the change is in it |
  | `forest.bin` (trees) | **yes** | read through the feed (caller 4); the server re-places trees on a feature or plot-type change (2.2) |
  | overlay, cameras, frames, water clock, fog, units, near pairs | yes, per frame | per-frame members |
  | bake-cache tap | not in the bundle | left out on purpose (harness section 0) |

- So a live bundle of today's product is complete: its stale field, water and ice are what the server drew. It stops
  being complete the day T1 or T2 makes the server read a new field, water or ice image inside one load: 6.5.

### 6.5 Design note: field, water and ice generations inside one load (rule A; built with T1, not here) [I]

**When it lands.** Under rule A (offline-scene-harness.md, "The two rules") this bundle change lands in the same merge
as the first code that makes the server's render thread read a new field, water or ice image during a load (T1's
first reader), with its `FormatVersionChange` line in `server/src/render/srv_scene_bundle.cpp` and a raised
`format_version`. **The version number is coordinated with epic 0561's lead through lead-0465 (MC #6158)**, because
0561 holds `srv_render_scene.*` and may raise the format for cities and landmarks at the same time. Nothing of it is
built in 4906 (no change to `srv_render_scene.*`, `shell/composite/`, the recorder or the player). It is product: it
counts under rule A at T1's merge, with its positive and negative tests and its row in the harness file's section 3.

**What exists.** The recorder already writes a later copy of a load (`heightfield/load<NNNN>-<k>.c5hf`,
`water/load<NNNN>-c<copySeq>.wtap`), and the player takes "the field of the load" [C, harness 15]. What is missing is
*when* a copy became the one the renderer draws with, and a player that switches at that frame. **The ice already has
both** (harness 2.12): one member per generation (`ice/gen<N>.ice`) and, per recorded frame, the generation drawn
(`shadow.bin`'s `iceGeneration`, flag bit 2), with the player swapping the mesh at the frame whose record names a new
one [C]. The proposal gives the field and the water the same shape.

**Proposal.**
1. A member `generations.bin` (kind `generations`, version 1; 16-byte header as every member), **one record per
   frame of `frames.bin`**, same order and frame ids (as `shadow.bin`): `frameId` (i64), the field generation drawn
   (u32 `fieldGen`, 0 = the load copy) and the water generation drawn (u32 `waterGen`, 0 = the load copy), and for a
   frame where either differs from the frame before, a change record: `kind` (field / water), `source` (u32: 1 a tap
   re-copy, 2 the server's own producer), the region (u32 x0, y0, x1, y1 in samples, or the whole field), and the
   SHA-256 of the region's samples (u16 then u8) as the render thread had them. The ice keeps its own record in
   `shadow.bin`.
2. Source 1 (a tap re-copy: the field after a `requestSeq`, the water after `Waves_Reload` + `requestSeq`): the copy is
   a member as today (`-<k>`, `-c<copySeq>`), named by its generation; the ice after `Ice_Reload` is already handled.
3. Source 2 (T1's region producer in the server, from the feed's plot changes): no copy is stored. The player runs the
   same producer at the frame whose record names the new generation, from `feed.bin`, and **checks** the region's
   SHA-256 against the record; a mismatch
   is a refusal, not a tolerance. If the producer reads anything the bundle lacks (the piece library, a tap of the
   EXE), that input is a member of its own under rule A. A `render_config` key turns the producer on and off
   (`render_live_terrain`, name to be fixed by T1), so a bundle says which it was recorded with (rule A, 0495's
   addition).
4. The player, before drawing frame k, seeks `generations.bin` to record k (as the ice source seeks `shadow.bin`) and
   applies a field or water generation that differs from the one it holds, through the same path the live reader uses
   (the regional upload, not a scene reload); each replayed frame is checked against its record (a MISMATCH line fails
   the run, as for the shadow and ice).
5. Checker refusals to add, each with a negative test: a record count or frame ids other than `frames.bin`'s; a
   source-1 generation whose member is missing or not listed; a generation going back or skipping (not +1 at a change);
   a change record on a frame where neither generation changed, or none where one did; a region outside the field;
   an unknown kind or source; a source-2 hash mismatch at replay. A bundle of the new version without
   `generations.bin` is refused (the recorder writes it always, all zero generations when nothing changed).
6. Sizes: a whole-field copy per generation is 27.5 MB on Tiny and 132 MB on huge (harness 2.1, 4.3); source 2 stores
   none. That is why the producer's output is checked by hash, not stored.

**The acceptance it serves.** "Live equals reload" (R3, 2.3): the live bundle's last generation of the field against
the next process's load copy, by `regioncmp.py` with the changed plots (inside and rings 1-4 at tol 1, outside by
2.3's rule: a few noise samples, and the changed coastline's length for land/water changes).

### 6.6 The cost case: the large change [I, defined; not measured]

- **Definition.** `lt.py large` on the chain's state 13 (Tiny, after `wl`): the largest landmass that may be
  turned to water (every plot unowned, no city, unit, improvement or natural wonder; `lt.py landmasses` lists them,
  largest first), every plot `SetPlotType(PLOT_OCEAN, true, true, true)` in map order. Its size is the number of
  plots `apply` returns as changed (the landmass plus the DLL's coast changes around it), and its field region is
  what `regioncmp.py` counts between live 14 and reload 14: changed samples inside, rings 1-4, outside, and their
  bounding box in samples.
- **What it measures (for T1, under PERFORMANCE.md):** the producer's time for that region against the reload's
  `StartInitTerrainFromGameplayMap()` time on the same save (4.4: about 2 s on Tiny), at least six interleaved runs per
  arm with the quiet lock, the machine's load and the build named; the memory of the generation (6.5: a hash and the
  region, or a whole copy for a tap source); and the frame time while the producer runs (whether the picture stalls).
  The same rule on a copy of the huge save (`C5H-0317-HUGE_0350`) is the large case's upper end for T1; it is not
  part of 4906's scenes.
- `ms` in the apply record is `os.clock` around the Lua calls (process CPU time): it times the DLL's work for the
  change, an indication only, not the cost case.

### 6.7 Results: the stock route [L, 2026-11-18 21:58-23:20 EST]

*(Replaces "no scene recorded as of 21:10".)* Instance **c5h-a** (claimed by w4-4906 for 0465), stock route: the stock
DX11 EXE, the 32-bit DLL pin **`i0ship-e96457fd-ia`** (`runner.py prepare --dll pin`, backup by SHA-256 under
`C:/c5-64bit-work/0465/4906/stock/backup/`), CP + VP + EUI 5.4.8, Vibrant Textures (D94), the frame cap at 30 fps.
Base save: `C5H-0301-4906-LT_0008`, a copy of c5h-a's `c5h-9029-tiny-t0008` (SHA-256 b2ceb663...c29163, the same bytes
as `c5h-9028-tiny-t0008` and 4901's `LT_0008`); the chain's saves `C5H-0301-4906-ST00` .. `-ST14` in c5h-a's
`ModdedSaves/single`. Data (outside git): `F:/c5-64bit-work/0465/4906/scenes/stock/` (every state's field .npz,
camera, record, pictures; `compare_stock.json` and the difference images in `compare/`).

- **The mod live [L].** The first run loaded it and every command answered as designed. The six negative commands
  (`negatives.sh`: outside the map, an unknown feature, plot type and terrain, `apply fh_mountain` on the flat FH, the
  capital made water) each ended with exit 4 and the named plots' record unchanged; no box. Every transition's starting
  state was met in the chain and each `apply` changed its plots (14 applies). The DLL's own side effects seen: water to
  land made the rest of the bay (51,13) a lake; LW1 kept its flood plains and river on the new coast (as in 2.0); LW2's
  forest went; the large change's last plot became `TERRAIN_OCEAN` because no land was left next to it, the others
  coast. The named plots' states on the base save equal 2.0's.
- **The large change (the cost case on Tiny) [L].** `landmasses`: no landmass was allowed at first (the two continents
  owned, the capital's island holding a city, landmass 4 an ancient ruin); mod v2 accepts an unowned ancient ruin (the
  DLL's `erase` removes it, no owner involved), so **landmass 4, 7 plots** ((24,10), (23,11), (24,11), (22,12), (23,12),
  (24,12), (23,13); forests, a hills plot, two resources and the ruin), all to water: 7 plots changed, `ms` 62 (Lua CPU
  time). It is the largest allowed landmass of this save; the cost case's upper end stays the huge save (6.6).
- **P2 again [L]:** before k and live k are bit-identical (heights and materials) for all 14 transitions.
- **Live against reload, by region** (`regioncmp.py`, tol 1; samples over tol; materials in brackets):

  | k | Transition | Changed plots | Inside | Ring 1 | Ring 2 | Ring 3 | Ring 4 | Outside | Max |Δ| inside / outside |
  |---|---|---|---|---|---|---|---|---|---|
  | 1 | flat to hills (FH, a warrior on it) | 1 | 4,096 (158) | 11,631 (11) | 0 | 0 | 0 | 0 | 16,888 / 1 |
  | 2 | hills to mountain | 1 | 4,095 (3,900) | 10,785 (1,007) | 0 | 0 | 0 | 1 | 38,322 / 2 |
  | 3 | mountain to hills | 1 | 4,095 (3,900) | 10,785 (1,007) | 0 | 0 | 0 | 1 | 38,322 / 2 |
  | 4 | hills to flat | 1 | 4,096 (158) | 11,631 (11) | 0 | 0 | 0 | 1 | 16,888 / 2 |
  | 5 | grass to desert | 1 | 0 (3,142) | 0 (436) | 0 | 0 | 0 | 0 (3) | 0 / 1 |
  | 6 | Uluru added on flat | 1 | 4,095 (3,809) | 10,761 (1,014) | 0 | 1 | 37 | 1 (3) | 15,665 / 2 |
  | 7 | Uluru removed | 1 | 4,095 (3,809) | 10,761 (1,014) | 0 | 1 | 37 | 1 | 15,665 / 2 |
  | 8 | Kilimanjaro added (flat to mountain) | 1 | 4,096 (1,482) | 12,287 (178) | 0 | 0 | 0 | 0 | 24,745 / 1 |
  | 9 | the mountain wonder made flat | 1 | **0** | 0 | 0 | 0 | 0 | 2 | 0 / 921 |
  | 10 | ice removed | 1 | 0 | 0 | 0 | 0 | 0 | 2 | 0 / 921 |
  | 11 | ice added | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 / 1 |
  | 12 | land to water (LW1, LW2) | 2 | 8,192 (6,795) | 21,695 (2,964) | 7,808 | 8,283 | 5,319 | **195,597** (35,207) | 8,185 / 13,242 |
  | 13 | water to land (WL) | 2 | 8,080 (5,190) | 18,197 (4,295) | 28,930 | 48,266 | 55,822 | **1,690,208** (261,428) | 12,025 / 28,414 |
  | 14 | the large change (7 plots to water) | 7 | 28,563 (24,047) | 13,014 (1,259) | 7,760 | 6,923 | 1,530 | **1,235,711** (153,460) | 20,698 / 39,041 |

  (Ring 1 holds 6 plots = 24,576 samples per changed plot; a plot 4,096. k = 13 lists 2 changed plots: WL and (51,13), the
  rest of the capital's bay, which the DLL made a lake (`IsLake` false to true, a new landmass 7) when WL closed it.)
- **Changes away from water are local [L]:** heights and materials differ only in the changed plot and ring 1 (the
  wonders: a few samples at ring 3-4, max |Δ| 8), and outside only the known single-sample load noise (2.3, P5; one
  sample at (17,14) recurs, as 2.3's did; two runs show one sample of |Δ| 921 at (53,10)/(49,23), alone in its plot).
  This holds for every transition of the beta's staged subset (MC #6462).
- **Land/water changes are not local in this chain [L]; this refines 2.3.** Land to water of LW1 + LW2 changed 195,597
  samples outside ring 4, water to land 1.69 M, the large change 1.24 M, as whole plots (4,096 of 4,096 samples)
  scattered over the map at hex distance 5-26 (e.g. (47,14), (7,11), (37,11), (3,10), (36,13)). That is P5's second
  point, the whole-map `rand()` sequence shifted by a changed coastline, now seen: distant plots get other piece
  variants (D194 calls them plausible variants). 2.3's single case (LW1 alone, from `LT_0008`) reached only 3-4 plots;
  why that case did not shift the sequence is open [I: a change that keeps the number of coastline curves may keep the
  draw count]. For section 2.3 and R3 (lead's sections): "live equals reload" for land/water changes can only be judged
  on the changed plots, their ring and the changed coastline; elsewhere stock itself redraws other variants on reload.
- **The wonder made flat (k = 9) changes nothing in the field [L]:** Kilimanjaro on a flat plot is drawn as before
  (0554 (a) and P7: the wonder's pattern flags win over the plot type), confirmed here by the field.
- **Ice (k = 10, 11)** is not in the field (0 samples); its picture changes only after the reload (ice_add's box:
  median 163 over the box, the ice drawn after the reload only).
- **Pictures** (live 2 s after the change, against the reload's picture, boxes of +-60 back-buffer pixels around the
  changed plots, `piccmp.py`): every box differs (median 33-163); the units' idle animation inside the FH and NW boxes
  dominates those numbers, so the pictures are diagnostics of what changed, not a measure. Visual Checks issue 4906,
  DIAGNOSTIC (D112), popups closed by `popups.lua` before every picture (D114); the ids are in each state's
  `<tag>_capture.json`.
- **Capture notes [L]:** 2 of 29 state pictures (`04_fh_flat_before`, `07_nw_remove_before`, both the first picture
  after a reload) were frames without the EXE's interface, units, flags, resource icons and trees (2.1's frame kind);
  `frames.py` flags them by the interface's top bar; they were retaken in new processes (`_retake`), and `scenes.py`
  now retakes such a frame itself (up to 3 times, 3 s apart). Not a terrain effect; no box, no crash in the 16 game
  processes.

### 6.8 Results: the product route and the bundles [L, 2026-11-18 23:30 - 2026-11-19 00:56 EST]

Same instance c5h-a after the stock restore. The product as shipped: `run_3308.py prep` with the pin layout
**`x64-c29442e8-p58724e0`** (D33 with Overlay kept, `[Renderer] ServerRenderer = 1`, vpproxy.ini mode=on), the
composite built from a `git archive` of 58724e072 (`shell_composite.dll` SHA-256 3a34d2c3..., not 4901's 2B30EA27...:
the build is not byte-reproducible across trees), the pin's `shell_composite.ini` with run_3308's composite keys
(4901's `pp_keys.py`), and the recorder's keys through `server_cfg_extra` (TEST ONLY, issue 0307), a new bundle folder
per process (`render_test_record_after=1500`, `_frames=16`, `_dump=all`). The chain's own base copy
`C5H-0301-4906-PL_0008` (the same bytes as LT_0008), saves `-PT00` .. `-PT14`. Data: `F:/c5-64bit-work/0465/4906/
scenes/product/` and the bundles `F:/c5-64bit-work/0465/4906/bundles/PT00_20261118-232959_workstation` and
`F:/c5-64bit-work/0465/4906/scenes/bundles/PT01_...` .. `PT15_...` (one per process, the id names the process).

- **The proxy re-reads `server_cfg_extra` at every launch [L]** (6.4's [I] settled): each process wrote its bundle into
  the folder written into the extra file just before its launch.
- **Every bundle is complete by its manifest [L]:** format 8, 16 frames, all 16 dumped, 62 members, 651 MB, counters 0
  (no record error, no refusal, no feed overflow), the only version note the bake-cache gap. In every process the step
  waited for the span's 3rd dump and then made the change (`bundle_dumps_before_change: 3`), so frames 1-3 are the
  state before and the last ones the live state [L for the order; that the feed caught the change within the span is
  [I] until a replay looks at it]. The checker's verdict: all 16 OK (below).
- **The server's field in a bundle is the EXE's live field [L]:** `regioncmp.py` of PT01's
  `heightfield/load0003.c5hf` against the EXE's field dumped live in the same process: 0 samples differ, max |Δ| 0,
  0 material bytes. So a live bundle carries the stale field of the load, which is what the server draws (P1), and the
  C5HF reader of `regioncmp.py` agrees with the dump reader.
- **The product's fields equal the stock's [L]:** per state, the EXE's field on the product route against the stock
  route's (`compare_routes.py`, 29 states): at most 3 samples over tol 1 (single samples; max |Δ| 2,363 on one of them),
  at most 3 material bytes, i.e. load noise (P5, which saw single samples of 46 and here 921). The
  live-against-reload table of 6.7 holds on the product unchanged (each region's count equal to stock's, or within 1-2
  noise samples outside: lw 195,596, wl 1,690,209, large 1,235,713 outside).
- **Pictures, stock against product (same state, the world rectangle 300,90-1050,560 of the half-scale shot,
  `piccmp.py`, 29 states):** median of the per-state medians 6 (3-6; 56 for `04_fh_flat_before`, the product's
  one-colour frame), p95 14-110 (median 54) (the server's terrain against the EXE's plus units' animation: what the
  renderer epics measure, not a live-terrain effect). The product's live pictures show the transitions as stock does (2.2's finding: only the server's
  forest follows a change live; this chain has no forest transition on land).
- **Capture notes [L]:** the product route gave PrintWindow's known frames: one-colour (84,84,84, the D33 clear
  colour) and interface-less ones (ticket 0401); `scenes.py` retook 28 of them 3 s later, and 7 were kept after three
  tries (one t2s picture, `04_fh_flat_before`, a one-colour frame; six Strategic View shots). All 37 artefact entries
  (with the stock route's two) carry an ARTEFACT note in Visual Checks. No box, no crash in 16 processes.
- **Restores (MC #6596) [L]:** `run_3308.py restore` (backup restore ok, `provision.py verify` rc 0), `frontend.py
  restore` ok, then `backup.py verify` against the backups taken **before** each prepare: the product one (23:30) all
  equal or absent as before; the stock one (22:04) all equal or absent except `instance.json`, the instance's own
  provisioning record (the set-dll / set-proxy history it appends to); `provision.py verify` ok with no changed mutable
  file. c5h-a released 2026-11-19 01:00.
- **The checker [L, 01:05-01:10]:** `recorder_test.exe` built from the 58724e072 archive (`build_scenebundle_tests.py`
  into `C:/c5-64bit-work/0465/4906/pp/sbt`, lead's yes #6613, after the restores); every bundle stamped (save name and
  SHA-256 of the save it loaded, turn 9, `repo_commit=58724e072`, `build_config=release`, `pins.dll_lock=x64-c29442e8-
  p58724e0`) and then `--check`: **all 16 `scenebundle: OK`**. Index of every bundle file with its SHA-256:
  `F:/c5-64bit-work/0465/4906/scenes/bundles_index.json` (PT01-PT15, SHA-256 445bda4a...) and
  `F:/c5-64bit-work/0465/4906/bundles_index.json` (PT00, f0c00682...). Not done: a `sceneshot` replay of a bundle
  (whether the replayed frames after the change show the live state, offline equals live), a follow-up step.

**The fields of every state, SHA-256 prefixes of the .npz files** (each .npz also holds the dump's pid and time, so
"before" and "live" files differ although their arrays are bit-identical; the full hashes of every file are in
`index.json` of each scenes folder: stock 586173f8..., product fa9b15e3...):

| k | Transition | stock before / live field | product before / live field |
|---|---|---|---|
| 1 | `fh_hills` | 101e3b8a / 9197f9e2 | 45f69bda / 81425cd4 |
| 2 | `fh_mountain` | 4fd112b1 / 68fed57f | db5e87e4 / ed0ee34c |
| 3 | `fh_hills_back` | af01ac02 / 782e8cc8 | 84e8a684 / 84c33b12 |
| 4 | `fh_flat` | 881aff4c / b8aaf4cb | 132b6bbf / 279d1c0e |
| 5 | `gd` | bc760edb / 3151a28c | fbc4e773 / ac3553a5 |
| 6 | `nw_add` | c7755ee2 / 3038c791 | cef2d07a / 680664e7 |
| 7 | `nw_remove` | d32075c6 / b723faaf | 8a897960 / 911200f7 |
| 8 | `mw_add` | 8546a0a8 / 30de6989 | 73e53d15 / 0a710f26 |
| 9 | `mw_flat` | 1557fe55 / 921f2df7 | 77dd2636 / 3ebc7553 |
| 10 | `ice_remove` | 7e707632 / c3f40548 | bc025e56 / 9556a544 |
| 11 | `ice_add` | a9f698d4 / 70579ef4 | 4d0df587 / 1249178b |
| 12 | `lw` | d0651ba4 / a47455b4 | bf44f35b / cd6547d5 |
| 13 | `wl` | abe7f38b / 6e6f3489 | 696eb93c / 9076d9da |
| 14 | `large` | 22300e79 / 27f952da | 0b77a7c5 / 2e8690dd |
| 15 | reload of 14 | e42a7269 | 63c0889e |

### 6.9 The map seed: in the save, in the server, in the journal (ticket 4912, epic 0615) [C; live check at I1]

Written 2026-11-20 16:15-16:40 EST by w3-4912 (account B), branch `w3-4912`. Why: the coastline jitter of the EXE's region
fill is the first `rand()` after `srand(CvPreGame::mapRandomSeed)` in `CreateTerrain` (8.10), so a port of that fill (4910)
needs the load's map seed.

**Offline, from any `.Civ5Save` [C, the fork's `CvPreGame.cpp` 1720-1905 and the bytes of the two saves].** The pregame
block follows the header uncompressed (`srv_civ5save.h`). `CvPreGame::readArchive` reads, in this order around the seed:
... `s_leaderNames` (vector of strings), `s_loadFileName` (string), `s_localPlayerEmailAddress` (string), `s_mapNoPlayers`
(bool, **1 byte**), **`s_mapRandomSeed` (u32 LE)**, `s_loadWBScenario` (bool, 1 byte), `s_overrideScenarioHandicap` (bool,
1 byte), `s_mapScriptName` (string = u32 length + bytes). So: find the map script's path as a length-prefixed string
(e.g. `Assets\Maps\Continents.lua`, length 26) at readArchive's place, which was its **third** occurrence in both saves
(offsets 160 and 905/896 come before it: the first is the header's map-script type name; what the second is was not
checked [I]); the seed is the u32 that ends 2 bytes before that string's length field, preceded by one 0 byte.
Cross-check: `s_syncRandomSeed` sits after `s_slotStatus` (64 u32) and `s_smtpHost` (an empty string, u32 0), followed
by `s_targetScore` (u32 0) and `s_teamTypes`' count (64); a game the EXE started has both seeds equal
(`loadFromIni` sets both from `timeGetTime()` when `config.ini` has `MapRandSeed = 0`, engine-contract.md finding 1).
A load does not change the map seed: `GAMEOPTION_NEW_RANDOM_SEED` reseeds only the game's own RNG (`CvGame.cpp` 11047).
The scratch reader was `pgseed.py` (C:/c5-64bit-work/0615/w3/, not committed: a 30-line search for the string).

| Save (SHA-256) | Map script string at file offset | mapRandomSeed | syncRandomSeed |
|---|---|---|---|
| `c5h-9028-tiny-t0008` = `C5H-0301-4901-LT_0008` (b2ceb663...c29163), Tiny 56 x 36 | 8166 | **1039964182 = 0x3DFC9816** (bytes 16 98 fc 3d at 8160) | equal (at 12300) |
| `C5H-VP8P-HUGE_0350` (c499d3c6...88c3), Huge 128 x 80 | 8152 | **109008130 = 0x067F5502** (bytes 02 55 7f 06 at 8146) | equal (at 12270) |

Both sent to the 4910 worker through lead-0615 at 16:15. [L pending: the server's own read at I1 (below) must print the
same values.]

**In the server [C, code and the offline test].** Route without `srv_render_scene.*` (lead-0042 #8506: 0042's stage 4
changes that file; the feed and the C5HF header stay untouched, farm fields read the header's `mapSeed`, which the
composite leaves 0):
- `server/src/srv_liveterrain_seed.cpp` (compiled only into server.exe: it needs the DLL's interface headers, which the
  render test builders lack) registers a reader with `liveterrain::SetMapSeedSource` at static initialisation. The reader
  finds the module of the process that exports `DllGetGameContext` (the hosted DLL, direct and remote mode), takes its
  singleton context, `GetPreGame()->mapRandomSeed()`, `Destroy()`.
- `Scene::liveTerrainStep` (`srv_liveterrain_scene.cpp`) calls it **on the render thread** when a new load's field is
  handed to `LiveTerrain::onField` (`LoadIn::mapSeed`, `haveMapSeed`), and logs one line per load: `live terrain: load seq
  N: map seed S (0xS), CvPreGame::mapRandomSeed of the hosted DLL (ticket 4912)`, or a LOUD line with the reason, or "no map
  seed source in this build" in a binary without the reader (tests, tools).
- Why `server/src/` and not `server/src/render/`: the render units compile without the DLL fork's include path
  (`build_server.py` step 2) and every render test builder globs `server/src/render/*.cpp`, so a reader that needs
  `CvDllInterfaces.h` there would break them; in every binary but server.exe the source stays null and the seed "unknown".
- Thread safety: the seed is a plain u32 that the DLL sets while the load is set up (the save's pregame is read before
  the EXE's `CreateTerrain` runs, and nothing changes it during the load), so a render-thread read at the field hand-over
  sees the load's final value. In detail: `GetPreGame` / `Destroy` allocate and free a small wrapper on the DLL's own heap (`HeapCreate` without
  `HEAP_NO_SERIALIZE`: any thread); `mapRandomSeed()` reads one aligned 32-bit static that the game thread writes only
  while a load or a new game is set up (`PreGame::read`, `loadFromIni`, `setMapRandomSeed`), before the EXE builds the
  field the render thread is handed [C]. A new load starting while the render thread still takes the previous load's
  field could read the newer seed [I: not seen; that field is replaced within the next polls and read again].
- **Where and when, in one line:** `CvPreGame::mapRandomSeed()` of the DLL the server hosts, read once per load at the
  field hand-over (`Scene::liveTerrainStep`, the first poll that has the new load's field and a plot table of its size).
- **What other games give [C for the DLL's code, I for the EXE side until I1]:**
  - *A save from another map script* (Continents, Pangaea, Communitu, a `.Civ5Map` scenario or world-builder map): the
    seed is not the script's; it is the pregame's, written by every save in the same place (`readArchive` reads
    `s_mapRandomSeed` with no version or script condition), and the EXE's `CreateTerrain` seeds its `rand()` with the
    same pregame value when it builds the terrain of that load, whatever made the map (8.10). So the value read is the one
    that load's terrain was built with. A map script that draws its own random numbers does so at generation, not at
    `CreateTerrain`; it does not enter here.
  - *A save made by an older DLL* (an earlier VP or patch build): `readArchive` has read the seed at the same place in
    every archive version since version 0 (the version gates are around other fields), so the hosted DLL reads the
    save's seed; a pregame whose layout the DLL does not understand fails the whole load, it does not give another seed.
    The save's seed is the one its game was started with (`loadFromIni`'s `timeGetTime()` or `config.ini`'s
    `MapRandSeed`, both seeds set together), and a load never changes it (only `GAMEOPTION_NEW_RANDOM_SEED`, which
    reseeds the game's own RNG, not the map seed).
  - *A new game* (not loaded): `loadFromIni` sets the seed before the map is made; the hand-over reads that value.
  - *A seed of 0*: never seen from a real game (`loadFromIni` replaces 0 by the clock); the scene treats it as no seed.
- **Without the seed (the MC's #8552 condition 1):** `DecideMapSeed` (`srv_liveterrain.cpp`, tested) decides the line and
  the report; the producer adapter (`srv_liveterrain_c5r.cpp`) then makes every change of that load that turns a plot
  between land and water wait for the reload, without asking the producer (it keeps its own table, as for every wait;
  every differing plot is held), so no coast is ever drawn from seed 0. In server.exe a missing reader (a build fault)
  or a failed read is one LOUD line (the fault once per session, a failed read every load) and the server's non-fatal
  D118 report (`SrvExeCrash::ReportServerFailure`, stage `live-terrain-seed`, "the game goes on") once per session,
  written by `ReportSeedFault` beside the reader; a server.exe without the reader file has no report writer either, so
  that fault is in the log only (said in the LOUD line). In test and tool binaries (`HostIsServerExe()` false: they
  export none of server.exe's host functions) the null source stays a plain line.
- **The journal** (`render_live_terrain_journal`, 9.4) carries the seed as a 12-byte trailer after the feature names:
  `"SEED"`, u32 1, u32 seed, written only when the seed is known. A journal without it is byte for byte the form before
  4912, so the library's bundles decode and replay as before (rule A); the decoder refuses any other bytes after the
  names (another tag, another version, a cut trailer). `Replay::init` hands the journal's seed to the producer's `LoadIn`.
  Test: `test_liveterrain.exe map_seed` (the trailer, its refusals, the source registration, the adapter's land/water
  wait without the seed, `DecideMapSeed`'s outcomes; all cases 207 checks, 0 failed, 2026-11-20 17:12).

### 6.10 The land/water chain of ticket 4912: runbook for I1 (Fri 2026-11-20 19:00-22:30) and I2 (Sat 09:00-11:00) [I, the runbook; results in 6.11]

Written 2026-11-20 17:20 EST by w3-4912 before the run (the brief's order). The instance is named by MC-B through
lead-0615 by 18:30; `INST` below. Everything runs from the worktree of branch `w3-4912` with `C5_POOL_OWNER`,
`C5_JOB_OWNER`, `C5_QUIETLOCK_WHO` = `w3-4912` (the chain script sets them).

**What is recorded.** On Tiny (`c5h-9028-tiny-t0008`, SHA-256 b2ceb663...c29163, turn 8, seed 0x3DFC9816), both routes,
the land/water transitions only, each from the state the one before leaves (6.2's rule: reload k = before k+1):

| k | id | plot | change |
|---|---|---|---|
| 0 | `setup` | - | map revealed, studio visible, warrior on FH; save `..T00` |
| 1 | `lw` | LW1 (53,13), LW2 (55,15) fixed | land to water (a river plot on the coast and a forest) |
| 2 | `wl` | WL (51,12) fixed | the capital's bay: coast to land (the DLL makes (51,13) a lake, 6.7) |
| 3 | `lake` | picked (6.1 v3 rule), nearest to the capital | an inland flat plot to water: a new lake |
| 4 | `coast_land` | picked | an open coast plot to grass |
| 5 | `mouth` | picked | a river plot on the sea to water |
| 6 | `ice_land` | picked, nearest to the capital among water plots beside `FEATURE_ICE` | water beside ice to tundra |
| 7 | `large` | the largest allowed landmass (6.7: landmass 4, 7 plots, on this save) | a whole island to water |
| 8 | reload of 7 | - | capture only |

9 game processes per route; each state's field (.npz), camera, record, pictures (Visual Checks issue 4912, DIAGNOSTIC,
D112; popups closed in a loop first, D114) and each change's apply record with the picked plot (`<kk>_<id>_pick.json`).
The product route records one scene bundle per process (span from frame 1500, 16 frames, all dumped) whose
`render_live_terrain_journal` must carry the seed: `scenes_4912.py seed BUNDLE --expect 1039964182` after each process
(exit 0; the chain logs it), and server.log's line `live terrain: load seq N: map seed 1039964182 (0x3DFC9816)` per load
(the live check of 6.9). Stock (4906's 6.7) took about 80 min for 16 processes; 9 processes per route about 45-50 min
each [I].

**Files.** `render/evidence/0615/4912/chain_4912.sh` (modes below), `scenes_4912.py` (scenes_0586.py with 4912's saves,
the picked plots, the seed check), `server/evidence/0615/4912/build_4912.sh` (the product's builds in the team tree
`C:/c5-64bit-work/0615/srvtree`: server.exe, proxy and composite built release at the run's head (the SHA-256 of each
in the layout's `SHA256.txt` and in 6.11), layout = the batch-5 layout `x64-1fd51353-p1e09fc7` with these three). Outside git: scenes and bundles under
`F:/c5-64bit-work/0615/4912/` (`scenes/stock`, `scenes/product`, `bundles/PTkk_<time>_workstation`), logs, backups and
`extra.cfg` under `C:/c5-64bit-work/0615/w3/run/`.

**Before 19:00** (no instance needed): `bash server/evidence/0615/4912/build_4912.sh build && ... layout` at the branch's
head (done 17:10 for b7d2822a0); `git rev-parse HEAD` into the log; `PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 python -I -m pytest
-q tools/scenario/mods/liveterrain/tests render/evidence/0615/4912` (C5_LUA51_DLL set: the mod's Lua offline); build
`tools/harness/native/` (`build.bat`) in the worktree.

**I1, in order** (each line ends non-zero on a failed step or an open dialog box: then stop, report to lead-0615 at once,
D155; COMMON 30 for a crash):

```
python -I tools/harness/pool.py list                                   # INST free or granted
python -I tools/harness/pool.py claim --name INST --ticket 0615 --owner w3-4912 ; python -I tools/harness/pool.py list   # the claim shows
bash render/evidence/0615/4912/chain_4912.sh check INST                # EXE build 4390913 (stamp 0x546CD0A8); else stop (D200)
# stock route (backup by SHA-256 inside runner.py prepare; the 32-bit DLL pin, COMMON 18)
bash render/evidence/0615/4912/chain_4912.sh stock-prep INST           # prepare, launch, in game on C5H-0615-4912-SL_0008
#   negative commands of 6.1 here (each exit 4, `lt.py rec` unchanged) + `lt.py pick lake|coast_land|mouth|ice_land` (read only)
bash render/evidence/0615/4912/chain_4912.sh stock INST                # process 0 .. 8 (resume: ... stock INST K)
bash render/evidence/0615/4912/chain_4912.sh stock-end INST            # stop (newest save settled), frontend restore, runner restore
# product route
bash render/evidence/0615/4912/chain_4912.sh product-prep INST         # run_3308 prep (backup), composite keys, server_cfg_extra, save copy PL_0008
bash render/evidence/0615/4912/chain_4912.sh product INST              # process 0 .. 8, a bundle per process, the seed check per bundle
bash render/evidence/0615/4912/chain_4912.sh product-end INST          # stop, frontend restore, extra unset, run_3308 restore (provision verify 0)
python -I render/evidence/0615/4912/scenes_4912.py seed F:/c5-64bit-work/0615/4912/bundles/PT08_...   # the last bundle
# MC #6596: backup.py verify against the pre-prepare backups of both routes; then the release
python -I tools/harness/pool.py release --owner w3-4912 INST ; python -I tools/harness/pool.py list
```

If the window runs short, the product route goes first in the order of value: the seed live check needs only its
process 0 and one transition; a route cut after k = 3 still holds lw, wl and the new lake.

**After the run (offline, no instance):** `regioncmp.py` live k against reload k with the apply's changed plots (6.3),
before k against live k (P2: bit-identical expected on both routes), stock against product per state
(`compare_routes.py`), `recorder_test.exe --stamp` and `--check` per bundle (6.8), the index of every file with its
SHA-256 (`scenes_4912.py index`), the results into 6.11 and the hand-back. A sceneshot replay of a bundle is a rule A
run: only in a slot from MC-B through lead-0615 (MC #8300), against the working copy `C5_SCENE_LIBRARY` on C: (MC #8488);
these bundles are evidence, not library members.

**I2 (huge, Sat 09:00-11:00, by a new START-REQUEST through lead-0615):** a copy `C5H-0615-4912-HL_0350` of
`C5H-VP8P-HUGE_0350` (c499d3c6..., seed 0x067F5502), product route first: process 0 with a bundle, `lt.py setup` is NOT
run (it reveals the whole map of a 128 x 80 game; the camera moves by `look` only), then `lake` and `coast_land` with
`--near` on the plot the camera shows (at least one land-to-water and one water-to-land case), each with a bundle and
the seed check (`--expect 109008130`), then the same two on the stock route. The chain's save names take `HT`/`HS` for
this save; the commands are the steps of `chain_4912.sh` by hand.

### 6.11 Results of I1: the Tiny land/water chain on both routes [L, 2026-11-20 18:34-20:10 EST, workstation, c5h-a]

**Setup as run.** Goldberg instance c5h-a (C:, mods548: CP + VP + EUI 5.4.8, Vibrant Textures, D94; EXE stamp 0x546CD0A8,
build 4390913, checked 18:33:50), claimed by w3-4912 for 0615 (MC-B's grant via lead2-0615, 18:31). Base save: c5h-a's
`c5h-9029-tiny-t0008` (SHA-256 b2ceb663...c29163, the same bytes as `c5h-9028-tiny-t0008` = `C5H-0301-4901-LT_0008`),
copied as `C5H-0301-4912-SL_0008` (stock; runner.py wants 0301 in the copy's name) and `C5H-0615-4912-PL_0008` (product).
- *Stock:* the stock DX11 EXE with the 32-bit DLL of the i0 pin 5d18d3d6c, instance-aware build: runner.py's `--dll pin`
  refused because that build is not laid out under the user's `c5-i0/dlls` yet (deps/i0.lock: "to be laid out by the MC"),
  so a C: copy of `F:/c5-64bit-work/0551/layouts/i0ship-5d18d3d6-ia/CvGameCore_Expansion2.dll` (SHA-256 e72f0880...,
  equal to its layout's SHA256.txt) was named (`C5_CHAIN_STOCK_DLL`). 18:57-19:30.
- *Product:* the batch-5 layout `x64-1fd51353-p1e09fc7` (its DLL and stubs) with server.exe, the proxy and the composite
  built release from w3-4912 @b7d2822a0 in the team tree (`build_4912.sh`, 17:07-17:10: server.exe 26090633..., proxy
  ad81b27e..., composite 08736140... (16:45, the same shell/ tree); main's later 0619 change to `proxy_livereload` is not
  in that proxy), `run_3308.py prep` (D33 minus Overlay,
  `[Renderer] ServerRenderer = 1`), 4901's composite keys, the recorder through `server_cfg_extra` (TEST ONLY, issue 0307),
  a new bundle folder per process (span from frame 1500, 16 frames, all dumped). 19:32-20:10.
- Chain `chain_4912.sh` (6.10). Ends: the harness's stop (newest save settled), `frontend.py restore`, runner.py restore
  (stock) / `server_cfg_extra` unset + `run_3308.py restore` (product, `provision_verify_rc` 0); `backup.py verify`
  against both pre-prepare backups: every file equal or absent as before, except `instance.json` (its own provisioning
  record, as 6.8). **18 game processes, no dialog box, no crash, no LOUD line** in any of the 9 product processes' server
  and render logs (`server/evidence/0615/4912/live_lines_i1_tiny.txt`, local paths replaced).

**The seed, live [L]:** every product load logged `live terrain: load seq 3: map seed 1039964182 (0x3DFC9816),
CvPreGame::mapRandomSeed of the hosted DLL at the field hand-over (ticket 4912)`, the value read offline from the save
(6.9), 2.1-2.7 s after the "waiting for the bake's plot table" line and right before "producer requested". The journal
of every recorded bundle carries it (`scenes_4912.py seed`: PT01-PT08 exit 0, 1039964182). PT00 is empty: process 0
ended (setup and save, 13 s) before the span's frame 1500 (the span starts about 60 s after the game is up); its state
is PT01's first frames.

**What was changed and what was drawn [L].** Every transition's starting state was met and its calls changed the plots;
the picks (6.1 v3) were the same on both routes:

| k | id | plot | changed plots (the DLL's own side effects) | product: live terrain | waves gate |
|---|---|---|---|---|---|
| 1 | lw | (53,13), (55,15) | the two to coast (LW1 keeps flood plains and river, LW2 loses its forest) | wait for reload: Water | the coast waves wait (2 plots) |
| 2 | wl | (51,12) | (51,12) to grass; (51,13) becomes a lake (as 6.7) | wait: Water | wait (1) |
| 3 | lake | picked (53,16) (distance 3 from the capital, 63 candidates) | (53,16) to coast water with `IsLake` true: a new one-plot lake | wait: Water | wait (1) |
| 4 | coast_land | picked (49,11) (distance 3, 171 candidates) | (49,11) coast to grass | wait: Water | wait (1) |
| 5 | mouth | picked (52,13) (distance 2, 12 candidates; the preflight on the base save had picked (52,14): the map had changed) | (52,13) to water; the lake (51,13) joins the sea (`IsLake` false); (53,13) loses its river | wait: Water | wait (1) |
| 6 | ice_land | picked (53,5) (distance 9, 216 candidates) | (53,5) to tundra; its six deep-ocean neighbours turn coast (one, (54,4), keeps its ice) | wait: Water | wait (6) |
| 7 | large | landmass 4, 7 plots (as 6.7) | all to water, (23,13) ocean, the others coast | wait: Water | wait (6) |

So today's product draws no land/water change live (the producer's own Water reason, with the seed known: 4910's port
is not in this build) and asks no `Waves_Reload` (MC #8527: none ran; the gate's lines are above). **Not recorded:** the
PT06 (ice_land) journal holds 0 jobs: the recorder closed the span 0.36 s before the change reached the producer
(`recorder: live terrain journal ... 0 jobs` at 20:01:40.404, the change at 20:01:40.766), so that bundle has no live
state; the stock fields of k = 6 have it.

**P2 again [L]:** before k against live k is bit-identical (heights and materials) for all 7 transitions on both routes.

**Live against reload, by region** (`compare_4912.py` = 4906's `regioncmp.py`, tol 1; samples over tol, materials in
brackets; the numbers of the product route equal the stock's except 1 to 4 noise samples or material bytes outside):

| k | id | inside | ring 1 | ring 2 | ring 3 | ring 4 | outside | max |Δ| inside / outside |
|---|---|---|---|---|---|---|---|---|
| 1 | lw | 8,192 (6,795) | 21,695 | 7,808 | 8,283 | 5,319 | 195,597 (35,210) | 8,185 / 13,242 |
| 2 | wl | 8,080 (5,190) | 18,197 | 28,930 | 48,266 | 55,067 | 1,683,504 (260,380) | 12,025 / 28,414 |
| 3 | lake | 4,096 (3,094) | 12,523 | 10,446 | 8,965 | 25,950 | 1,076,067 (123,215) | 5,908 / 33,034 |
| 4 | coast_land | 4,059 (3,259) | 11,447 | 5,698 | 8,125 | 14,001 | 224,351 (40,661) | 9,059 / 8,028 |
| 5 | mouth | 10,549 (5,717) | 29,170 | 36,785 | 52,000 | 61,605 | 1,607,725 (258,454) | 7,837 / 22,777 |
| 6 | ice_land | 28,672 (2,654) | 13,876 | 786 | 7,415 | 16,602 | 1,750,986 (290,422) | 15,300 / 21,004 |
| 7 | large | 28,262 (22,144) | 9,285 | 6,945 | 7,012 | 1,543 | 1,319,655 (161,115) | 19,578 / 33,792 |

- **lw repeats 6.7 exactly** (inside, every ring and 195,597 outside: the same counts two days apart from the same save),
  so the stock reload is deterministic for a given state and these fields are fit as references [L].
- **Every land/water change moves whole plots far outside ring 4** (0.2-1.75 M samples): 6.7's finding (the whole-map
  `rand()` sequence shifted by a changed coastline, P5's second point) holds for every new case, also for the one-plot
  lake and the one-plot open-coast change. coast_land moved the fewest (224,351), wl and the cases after it the most;
  whether the count follows the number of coast curves that change is for 4910's port to settle (8.10) [I].
- Pictures (DIAGNOSTIC, D112; Visual Checks issue 4912; popups closed in a loop first, D114): live against reload in
  boxes around the changed plots, median 22-127 (units' animation and the changed coast inside the boxes); diagnostics of
  what changed, not a measure (as 6.7).

**Where the recordings are (outside git):** stock `F:/c5-64bit-work/0615/4912/scenes/stock/` and product
`.../scenes/product/` (`<kk>_<id>_{before,live}_field.npz`, `_apply.json`, `_pick.json`, `_cam.json`, pictures,
`centres.txt`, `compare/`), bundles `F:/c5-64bit-work/0615/4912/bundles/PT01_*` .. `PT08_*`; numbers in git:
`render/evidence/0615/4912/compare_{stock,product}.json`; logs `C:/c5-64bit-work/0615/w3/run/` (chain log, backups,
the product processes' logs).

### 6.12 Results: the huge case, run in I1's window [L, 2026-11-20 20:19-20:46 EST, workstation, c5h-a]

On MC-B's yes (20:15, via lead3-0615) I2's content ran in I1's remaining window, as a registered heavy job (`quietlock.py
run`), at the old pins (product: the layout of 6.11; stock: the same i0 DLL copy): `render/evidence/0615/4912/huge_4912.sh
all c5h-a`. Copies `C5H-0615-4912-HL_0350` (product) and `C5H-0301-4912-HSL_0350` (stock) of c5h-a's `C5H-VP8P-HUGE_0350`
(SHA-256 c499d3c6...88c3, turn 350, 128 x 80); no `lt.py setup`. Product 20:19-20:34 (3 processes, a bundle each), stock
20:34-20:46 (3 processes); stop, front-end restore, run_3308 restore (`provision_verify_rc` 0) and runner restore,
`backup.py verify` of both pre-prepare backups rc 0; no box, no crash, no LOUD line (the logs under
`C:/c5-64bit-work/0615/w3/run-huge/`). c5h-a released 20:47:15.

- **The seed, live [L]:** every product load logged `map seed 109008130 (0x067F5502)` = 6.9's offline value; bundles
  HT01 and HT02 carry it in their journals (exit 0); HT03 is empty (the capture-only process ended before frame 1500).
- **Picks and changes [L]** (anchor = the active player's capital (101,25)): `lake` (101,22), distance 3 of 181
  candidates: flat plains to coast water with `IsLake` true. `coast_land` (103,19), distance 6 of 421: coast to grass,
  and the DLL turned its deep-ocean neighbour (104,18) to coast. Both waited for the reload on the product (Water; the
  waves gate waited; no `Waves_Reload`).
- **Live against reload (tol 1), identical on both routes:** lake: inside 4,096 (2,838 materials), rings 1-4 9,843 /
  17,522 / 25,519 / 29,156, **outside 6,447,902** (796,760 materials), max |Δ| 29,622; coast_land: inside 8,192 (3,033),
  rings 9,843-17,398, outside 1,269,232 samples **but 9 material bytes**, max |Δ| 11,210. Before == live bit-identical
  for both. So on huge a one-plot lake re-rolls whole plots far from the change (6.4 M samples, as Tiny's 6.11), while
  this coast change moved heights outside with almost no material change [I: the outside heights of coast_land are
  the reload's re-rolled piece variants on unchanged materials; not looked at plot by plot].
- **Files (outside git):** `F:/c5-64bit-work/0615/4912/huge/scenes/{product,stock}/` (`01_lake_*`, `02_coast_land_*`,
  `03_reload_*`: fields .npz, apply and pick records, cameras, pictures, `centres.txt`), bundles
  `F:/c5-64bit-work/0615/4912/huge/bundles/HT01_*`, `HT02_*`. The stock reload references: reload of lake =
  `02_coast_land_before_field.npz`, reload of coast_land = `03_reload_field.npz`. Numbers in git:
  `render/evidence/0615/4912/compare_huge_{stock,product}.json`.

## 7. The H-A gate (child 4902): the pattern matcher, the piece library, the offline reproduction

Written by w3-4902 from 2026-11-18 19:40 EST: the gate of `research/07-architecture-decisions/architect-2026-11-18-live-terrain-route.md`
section 4 (route H-A approved behind this gate by the owner, D192). Static work on a read-only scratch copy of the `civ5`
Ghidra project (EXE copy SHA-256 b2ce96ae..., `reverse-engineering-setup.md` 2), headless 12.1.4, `-readOnly
-noanalysis`. The query script and its outputs stay in `C:/c5-64bit-work/0465/4902/` (scratch, not committed, D83);
the launch scripts are `render/evidence/0465/4902/gh.sh` and `setup_scratch.sh`. Marks: `[C]` checked by w3-4902 in
the decompile or the file named; `[L]` a live run of w3-4902; `[I]` inferred. Values taken from other sections keep
their mark and source.

### 7.1 The matcher is found: 0x769960, reached only through a CEG call stub [C, 2026-11-18 20:00]

- **Why it had not been found.** The block blend 0x6BA6F0 first calls 0x574C00 with `ecx` = the blender object + 8 and
  one stack argument, the HexPatternCell grid. 0x574C00 is a CEG stub. It calls 0x675D80, which computes the real
  target at run time as a CRC-32 (table at 0xAB4960, the standard polynomial) over 55 code ranges, a 16-byte value
  and the caller's return address, and then jumps to it. So the target has no static reference anywhere, and Ghidra's
  analysis had not made a function there. The mangled name at 0xA2B7E0 (`asset-pipeline.md` 4.2 point 2) reads
  `HexPatternMatcher::ComputePatternPositions(DynamicHexGrid<HexPatternCell,SquareGridPolicy,0,0>&)`: a `__thiscall`
  with one reference argument, which fits the stub's call.
- **How it was located.** After computing the target, the same stub checks it: the MD5 of (the target as 4
  little-endian bytes, then the file bytes of one code range named by a self-relative pointer at 0x9DC798, 0x1400
  bytes, relocations undone) must equal the 16 bytes at 0xAA91F0, or the stub punishes (0x70C900). Hashing every
  address of `.text` (0x401000..0x9D5D30, 5.8 million candidates, 51 s) gave **exactly one match, 0x769960**. The CRC
  route gave another value (0xF9CFB0D4), because the 16-byte input is not all zeros as first assumed; the MD5 check is
  the stub's own acceptance test, so its single match is the target [C]. Scratch script
  `C:/c5-64bit-work/0465/4902/scripts/ceg_target.py` (it reads only the EXE copy).
- **Shape [C].** 0x769960 (`this` = the matcher M = blender + 8, argument = the grid) opens a profiler scope, then for
  each priority level L from M+0x19C (the highest level of any loaded pattern) down to 0 calls the level pass
  0x648A00(grid, L, M+0x1A0). 0x648A00 calls `rand()` through two shuffles, 0x693A00 (12-byte elements) and 0x907A00
  (4-byte elements). None of the three was in Ghidra's analysed set; they were created in the scratch copy for reading.
- **Where it runs [C].** The blender is a local object of `CreateTerrain` (constructed by 0x669920 at the stack slot
  passed as `ecx` at 0x70AC71, the library loaded into it at 0x70ACA9, the blend called at 0x70ACFA), so the patterns
  and the placements live only during the load; nothing of the matcher survives into play.

### 7.2 The matcher's data and rules [C, decompile of 0x769960, 0x648A00, 0x907A00, 0x693A00, 0x578B00, 0x75F2A0, 0x6EA000, 0x526520, 0x681320, 0x578780]

| Object | Layout |
|---|---|
| HexPatternCell (8 bytes) | +0 u32 flags (`asset-pipeline.md` 4.2 point 1; 0 = no requirement); +4 u8 continent (0 = any); +5 u8 "claimed": in a pattern cell, the cell belongs to the piece; in a grid cell, an earlier placement owns it; +6, +7 not read by the matcher |
| DynamicHexGrid (the map grid and each pattern) | +0x00 a 4 x 4 int matrix (cube coordinates into the grid's frame), +0x40 its inverse, +0x80..+0x88 an origin hex (3 ints), +0x94 width, +0x98 height, +0x9C the cells. The matcher indexes the map grid **x-major**: cell (x, y) at `cells + (height * x + y) * 8`. A pattern's cells are a fixed 10 x 10 array inline at +0x9C, cell (c, r) at index `c + 10 r` |
| Pattern (0x49C bytes; array at M+0, count M+4) | a pattern grid as above, then +0x3BC piece index, +0x3C0 / +0x3C4 width and height limits, +0x3C8 priority level, +0x3D0 the (c, r) byte pairs of the used cells (count, u8, at +0x498). A cell is used unless its flags are 0xFFFFFFFF and it is not claimed. Built at load from the library's piece records (7.3) by 0x6A00A0 > 0x681320 and 0x526520, only for records whose byte +0x3D0 has bit 2 |
| Transforms (64-byte int matrices; array pointer M+0xC, count M+0x10, inline room for 6) | made by the matcher's constructor 0x578B00: four, each `translate(t) x rotate(0) x S[k]` (0x75F2A0; row vectors, translation in row 3): (t, k) = ((0,0,0), 0), ((1,-3,2), 2), ((-4,0,4), 6), ((-2,-3,5), 9). S is the table of 12 signed cube-axis permutations at 0xC90150 (initialised data in the file): S[0] is the identity, S[2] maps (q, r, s) to (-s, -r, -q), S[6] to (s, r, q), S[9] to (-q, -r, -s). Each pattern is thus tried as it is, mirrored in two ways and turned by 180 degrees, shifted back into its 10 x 10 box [C for the numbers; the geometric reading is I] |
| Placement output (M+0x1A0) | a handle table of 0x138-byte blocks holding 20-byte placements {pattern index, transform index, anchor cube q, r, s} (0x578780 > 0x6D3A60); the block set-up in 0x425060 reads it as the blender's +0x1A8 |

The level pass 0x648A00 (map grid G of width W and height H, level L):

1. Anchor list: for a = -2 .. W+1 (outer loop) and b = -2 .. H+1 (inner), the offset hex (a, b) is converted to cube
   coordinates, `q = a - (b >> 1)` (arithmetic shift), `r = b`, `s = -(q + r)`, and taken through G's inverse matrix
   (+0x40). That is (W+4)(H+4) anchors.
2. **rand:** the anchor list is shuffled once per level by 0x693A00. The shuffle is MSVC's `random_shuffle`: for each
   element index i = 1, 2, ..., n-1 it draws `rand() & 0x7FFF` (further draws, each shifted in by 15 bits, only when
   i + 1 > 0x7FFF), reduces it modulo i + 1 and swaps element i with the element at that index. So it draws n - 1
   values when n <= 32,768 (Tiny and huge are both below).
3. For each anchor in the shuffled order, the pattern index list (0 .. count-1) and the transform index list (0 .. 3)
   are **each shuffled again** by 0x907A00 (the same algorithm on 4-byte elements; both lists are set to 0 .. n-1
   only at the start of the pass, so each shuffle starts from the order the previous one left). Then for each pattern
   p in that order whose level is L, and for each transform t in that order:
   - the composite matrix is `T[t] x translate(anchor) x G` (0x6EA000, row-vector products);
   - each used cell (c, r) of p goes to the grid as cube `v = (c - (r >> 1), r, -(c - (r >> 1)) - r, 1) x composite`,
     then `y = v.r`, `x = v.q + (v.r >> 1)`; cells outside 0 <= x < W, 0 <= y < H are not tested;
   - a cell **fails** when both flags are non-zero and either `(pattern flags & grid flags) == 0` or the two
     continents are both non-zero and differ; it also fails when the pattern cell and the grid cell are both claimed;
   - when every used cell passes and at least one claimed pattern cell lies inside the grid, the placement {p, t,
     anchor} is added and those grid cells are marked claimed (+5 = 1). The loops then go on with the next transform
     and pattern at the same anchor, so one anchor can take several pieces whose claimed cells do not overlap.
4. Levels run from the highest down to 0, so higher levels claim first.

**The matcher draws rand()**: per level, (anchors - 1) values, then per anchor (patterns - 1) + 3 values [C]. The CRT
is seeded by `srand(mapRandomSeed)` at the start of `CreateTerrain` (`asset-pipeline.md` 4.1 step 1), and the coastline
jitter and the route step 0x6E27D0 draw before the matcher (`evidence/2900/2904/rand_in_bake.txt`), so the choice of
pieces depends on the exact number of `rand()` calls made before it. This corrects the Architect's working assumption
(route note section 7, "that the matcher draws no rand"); what follows for the route is in 7.6. *Correction
2026-11-18 (4902) of the open item in `asset-pipeline.md` 4.2 point 2, "whether the piece choice in the matcher calls
rand through an indirect call": it does. That file is not this ticket's; the line is sent to the lead.*

### 7.3 TerrainPiecesLibrary.fxsb decoded [C, 2026-11-18 20:15-20:25; loader 0x404170 > 0x5FCA30, file of c5h-c's install, SHA-256 86faac1e...ce4c1c7a, 27,595,584 bytes]

All values little-endian. The loader accepts the file only when the first u32 is 20 (`asset-pipeline.md` 4.1 step 4);
otherwise it reads the flat files (0x6A9B40). Layout, in file order:

| Part | Content (this file) |
|---|---|
| u32 version | 20 |
| u32 n1, then n1 u32 | 385,024: the **row table**. Entry k is where texture row k starts, counted in elements of the run array its texture uses |
| u32 n2, then n2 bytes | 15,050,900: the **byte run array**: pairs (run length, value), for the three 8-bit textures |
| u32 n3, then n3 u16 | 5,294,778: the **u16 run array**: pairs (run length, value), for the height textures |
| u32 n4, then n4 records of 0x56C bytes | 299 piece records; the file ends exactly there |

A row is decoded by reading pairs from its start until the run lengths add up to the texture's width; every row of
every texture in this file closes exactly at its width (checked for all 299 x 4 textures). The piece record:

| Offset | Content |
|---|---|
| +0x000 | a DynamicHexGrid header: two 4 x 4 int matrices (identity in every record), origin (0, 0, 0), +0x94 / +0x98 width and height (10 x 10 in all 299) |
| +0x09C | 100 pattern cells of 8 bytes (cell (c, r) at index c + 10 r): u32 flags (0xFFFFFFFF = not part of the pattern), u8 continent, u8 claimed. In this file: 23,694 unused cells, 5,651 used and not claimed, 555 claimed |
| +0x3BC | u32 piece index (equals the record's position) |
| +0x3C0 / +0x3C4 | u32 width and height limits (10, 10) |
| +0x3C8 | i32 priority level: 1 (70 records), 2 (69), 3 (104), 4 (56) |
| +0x3D0 | u8 flags: bit 0 = the small texture size (128); bit 2 = the record becomes a matcher pattern (set in all 299). Seen: 12 (147), 13 (109), 5 (39), 4 (4) |
| +0x3D4, +0x3D8, +0x3DC, +0x3E0 | u32 row-table starts of the four textures: height (`_H`, u16 runs), material (`_T1`, byte runs), height weight (`_H_BLEND`, byte runs), material weight (`_T_BLEND`, byte runs) |
| +0x3E4 | u16 region the piece belongs to: 0 land (291 records), 1 coast (`Coast 21`, the four Reef and the four Gibraltar wonders), 2 ocean (`Ocean 21`); the blend accepts a sample only when the sample's region byte equals it (7.4), and these are the classes 0x5A8640 gives the plots (0 land, 1 coast with reef and Gibraltar, 2 ocean). +0x3E6 u16: a terrain-kind number, 0..26 |
| +0x3EA.. | six 64-byte name fields: the CSV name twice, then the four DDS names (`<name>_H.dds`, `_T1.dds`, `_H_BLEND.dds`, `_T_BLEND.dds`) |

All four textures of a record share one size: 512 x 512 (151 records, the hills, mountains and the larger flat
pieces) or 128 x 128 (148 records). 

**Cross-check against the flat files** under `assets/terrain/pieces` (the same install), every texture of every
record, by name: the names, sizes and row counts all agree, but **the values are not the same data**. Heights: 53 of
299 equal to the DDS red channel; the others differ in places by 1 or by multiples of 257 (an 8-bit step), so the
two were exported at different times. `_T1`: the library holds small material indices (0..36 seen: 0, 2, 11, 14, 28,
32, 33, 34, 36), the DDS a colour image. `_H_BLEND` differs by 1 on about a quarter of the pixels, `_T_BLEND` in
places by up to 255. Since the EXE reads the library whenever the version is 20, the library is the reference; the
flat files are not a substitute for it [C for the comparison, scratch scripts `fxsb_vs_flat.py`, `fxsb_rel.py`].

**The EXE's loaded copy is freed at the end of the blend [C]**: the library object (0x80188 bytes, at the blender
+4) is released by 0x6D78D0, which `CreateTerrain` calls right after the block blend (0x70AD06). So in play there is
no loaded copy to read; the brief's live cross-check cannot be made, and the decoder is checked instead by the
offline reproduction of 7.5. (Correction of the `[I]` in `architect-2026-11-18-live-terrain-route.md` section 4, "that
it stays loaded": it does not. That note is not this ticket's file.)

### 7.4 The block blend in detail [C, decompile of 0x425060, 0x608270, 0x512B20, 0x463B10, 0x70B490; constants read from the file]

- **Jobs.** One job per 64 x 64-sample block (the block size is itself a CEG-computed constant, 0x6D00F0, so the
  64 is [I strong] from the strides 12 x 64 and 0xC008 = 64 x 64 x 12 + 8). Block (bx, by) covers samples
  x = 64 bx .. 64 bx + 63, y = 64 by .. 64 by + 63 of the W*64 x (H+4)*64 field.
- **Accumulator** per sample (12 bytes): f32 acc = 0, f32 wsum = 1e-6 (0x358637BD), u8 region = 4, u8 material = 3,
  u8 best material weight = 0 (0x512B20). Then 0x6414D0 writes the region byte from the coast and river polylines
  (a flood fill; described in 7.5 when needed).
- **Placements to rectangles (0x425060).** For each placement {pattern p, transform t, anchor}: the anchor hex is
  taken to world by `M_h2w` (0xD91030) and rounded half away from zero to whole samples (sx = 1.0, so world units are
  samples). Each transform has a precomputed rectangle (x0, y0, x1, y1) relative to that point, one for 128 and one
  for 512 textures, built from four corner hexes (globals 0xD910F0, 0xD91100, 0xD91110, 0xD91120, set at run time)
  sent through the transform and `M_h2w` and rounded the same way; a mirrored transform gives x0 > x1 or y0 > y1. The
  piece record's flag bit 0 picks the 128 or the 512 rectangle. The rectangle's job record (0x30 bytes) carries the
  small flag, the four row-table starts, the rectangle, the texture size and the region (+0x3E4 u16). The placement
  is listed in every block the rectangle touches (x wrapped modulo the field width).
- **Per block, per listed placement (0x463B10)** in list order: the rectangle is clipped to the block (x wrapped by the
  field width when the rectangle lies past either edge) and the four textures are read 1:1 (one texel per sample,
  no scaling) over the clipped window, in reversed order along a reversed axis. For each sample whose region byte
  equals the piece's region: `acc += (h16 / 65535 - offset) * hw / 255`, `wsum += hw / 255` (h16 from `_H`, hw from
  `_H_BLEND`; f32 arithmetic, constants 1/65535 = 0x37800080 and 1/255 = 0x3B808081), and if the `_T_BLEND` value is
  greater than the stored best, it becomes the best and the sample's material becomes the `_T1` value.
- **Write-out (0x70B490).** `h = round((acc / wsum + offset) * 65535.0) & 0xFFFF` (65535.0 as a double), written
  through the setter 0x5A3490 at the sample (x wrapped by 0x548980); the material byte through 0x775130. A sample
  that no piece reached keeps acc 0 and gives `round(offset * 65535)`.
- **offset** = the f32 0x3E70FCF1 = 0.23533989 (blender +0x1DC, set by 0x669920 from 0x9EEDBC). It is not exactly
  60/255 = 0.23529412: a port must use the f32 value.
- The blender constructor also sets three numbers that the later coast step uses: 0x3C3F (15423, about 0.2353 x
  65535, the sea-level height), 0x1DE2 (7650) and 13,000 (blender +0x1D0..+0x1D8).

### 7.5 The exit measurement: an offline whole-map matcher + block blend (Tiny) [C for the code, L for the inputs, 2026-11-18 20:39-21:05]

- **Code** (ours, written from 7.2-7.4, not product, not shipped): `render/evidence/0465/4902/repro.py` (piece
  library reader, HexPatternCell grid from the plot table, the matcher with the MSVC LCG and `random_shuffle`, the
  four transforms, the rectangle table, the blend with f32 arithmetic and the write-out), `compare.py` (the masks and
  the counts below), `live.py` (read-only live inputs: the grid constants block 0xD90FBC..0xD9132C and the plot table
  through Lua), `load.sh` and `guard_files.py` (the harness load and the SHA-256 backup of the guarded instance files).
- **Inputs [L]**: instance c5h-c (Goldberg, stock DX11 EXE, the instance's 32-bit DLL as provisioned, CP + VP + EUI
  5.4.8, Vibrant Textures), save `C5H-0301-4901-LT_0008` (Tiny 56 x 36, wrapping in x, turn 9; 4901's copy, 2.0), loaded
  2026-11-18 20:39-20:42 through `load.sh` with no box; field tap 20:44 by 4901's `drive.py field` (3584 x 2560 samples,
  both layers); grid constants and plot table read 20:44. Live values the code takes from the constants block: the
  pattern grid is 56 x 40 (the map plus 2 margin rows above and below); its matrix G is the plane identity followed by
  the translation (-1, 2, -1), so plot (x, y) is grid cell (x, y + 2) [L, and C for the mapping in 0x60ABB0];
  `M_h2w` maps hex (0, 0, 0) to sample (32.0007, 128.003) with rows (63.9986, 0.00094) and (32, 64.0019); the four
  corner hexes are (0.5, 0, -0.5), (1.5, 2.0, -3.5) for the 128 rectangle and (0.5, 0, -0.5), (4.5, 8.0, -12.5) for the
  512 one (values rounded here; the code uses the f32 words). The "zero" transform parameter block is all zeros and
  the rotation table gives the plane identity for rotation 0 [L, read at 0x2DD1CD8 and 0xA3EB0C].
- **Region byte.** 0x6414D0 (the coast and river flood fill) is not ported; the code gives every sample the class of
  the hex it lies in (0x5A8640's classes). The comparison is therefore limited to samples it cannot matter for.
- **Masks.** *Inland*: samples whose hex and all six neighbours are land with no river edge and no lake on any of them
  (no coast strip and no river carve reaches them): 675,840 of the 9,175,040 samples. *Variant-free*: inland samples on
  which five runs from five different rand states (0x00000001, 0x12345678, 0xCAFEBABE, 0x0BADF00D, 0x7FFFFFFF) agree
  within 1 in height and exactly in material, i.e. where the piece choice does not matter (D194): 249,479.
- **Run**: 2,185 or 2,186 placements per state (state 0x00000001, levels 4 / 3 / 2 / 1: 2 / 18 / 1,316 / 849), and
  3,986,395 rand draws in the matcher for every state (7.6). Python, about 160 s per state on the workstation
  (an indication, COMMON 23).
- **Result at tol 1** (`compare.py`, output `C:/c5-64bit-work/0465/4902/cmp_tiny5.json`):

  | Mask | Samples | Height \|dh\| > 1 | Material differs |
  |---|---|---|---|
  | variant-free | 249,479 | 4,900 (1.96 %) | 5,344 (2.14 %) |
  | inland, one state (median \|dh\| 0 or 1 per state, 8 for 0x00000001) | 675,840 | 295,284 .. 350,726 | 56,908 .. 63,136 |
  | inland: the tap inside the five runs' height envelope (+-1) | 675,840 | 597,772 inside (88.4 %) | |

  The height values the reproduction makes are the tap's own values in kind (flat land 15,300 / 15,324 / 15,330 /
  15,340 ..., ocean 9,766 equal sample for sample in the open sea), and the material histogram on land matches the
  tap's within a few per cent per material, except material 37 (67,354 land samples in the tap, none in the
  reproduction) and materials 15 and 9 (about 34,000 in the reproduction, few in the tap). Material 37 is the water
  material: 7.0 million of the tap's samples carry it, nearly all on ocean and coast plots and the margin rows; its
  67,354 samples on land-class hexes lie along the coasts, where the EXE's region byte (polylines) and the coast step
  differ from the reproduction's per-hex class [L for the counts, I for the cause]. None of them is inland.

### 7.6 The rand replay (way 1 of the Architect's corrected note 3.1-3.2): cost and exactness [C for the counts, I where marked]

- **The matcher's own draws are a fixed number** that depends only on the pattern grid's size and the library:
  per level (from the highest, 4, down to 0: five passes, the empty level 0 included) `(A - 1) + A * ((P - 1) + 3)`
  with A = (gridW + 4)(gridH + 4) anchors and P = 299 patterns. Tiny (grid 56 x 40): A = 2,640, **3,986,395 draws**,
  equal to the count our run made for every state [C]. Huge (grid 128 x 84): A = 11,616, **17,540,155 draws**, again equal to
  our run's count [C]. In C these are a few tens of milliseconds; the whole matcher in our Python takes about 160 s on
  Tiny [L, workstation, an indication]; a native port would be well under a second on Tiny [I].
- **What way 1 still needs: the number k of draws made before the matcher in the same load** (after
  `srand(mapRandomSeed)`). Two producers draw before it on the load thread [C, the decompile of 0x5674C0 and
  0x60F180; the callers from `evidence/2900/2904/rand_in_bake.txt`]: (1) the coastline jitter 0x5674C0: per polyline,
  `n` halving rounds (up to 4, while the point count stays at least 6) and per round about one draw per current point
  (the first, the interior ones and one more), so
  the count follows from the polylines' point counts alone, not from the drawn values; (2) the decal set-up
  0x6E27D0 > 0x55D0C0 > 0x60F180: one draw per decal placed from a list of more than one variant. Both counts are
  functions of the map (plot types, rivers, the decal lists), so **way 1 can be exact**, but neither count is ported or
  measured here: k is unknown for both saves, and our runs used arbitrary start states.
- **Not established, so way 1 is not shown exact.** What would settle it: port the two counts (the coastline
  extraction 0x491050 for the polylines' point counts, and the decal loop of 0x55D0C0), or search k on one save with a
  native matcher, using the material layer as the fingerprint (the material layer is byte-identical across loads,
  `asset-pipeline.md` 4.2 point 2); the correct k must make the reproduction equal to the tap everywhere inland.
  Estimate [I]: the search 2-3 h, the ports 6-10 h (the coastline row of 4.5).
- The seed itself (`mapRandomSeed`) is not exposed to Lua (no binding in the DLL's Lua PreGame [C, the fork's
  sources]); the server would take it from the DLL (`CvPreGame::mapRandomSeed`) [I].

### 7.7 Residual, verdict and what is open (2026-11-18 21:25)

- **Where the variant-free residual lies [L, scratch `residual.py`]**: 4,482 of the 4,900 height samples above tol
  lie in two neighbouring plots at the wrap seam, (2, 14) (hills) and (2, 15) (flat); the other 418 are spread over 32
  plots (mostly 10-80 samples each, differences of 3 to 65 u16 and a few of about 3,600-4,000). Of the 5,344 material
  differences, 1,815 lie in the marsh plot (40, 16) and its neighbour (41, 16); the marsh stamp writes marsh material
  over the blend after the load's blend step (`asset-pipeline.md` 4.1 step 8, 4.1 table here), which explains them
  [I]; the reproduction has no marsh step. Not explained: the seam plots. Candidates [I]: the region byte (the
  reproduction gives the hex's class, the EXE floods from polylines), or the x wrap of rectangles at the seam.
- **Verdict on the gate's exit as defined (D194: heights and materials of the matched kinds at tol 1 wherever the
  variant does not matter): NOT PASSED as measured**. Tiny: 98.04 % of the variant-free samples are equal in
  height at tol 1 and 97.86 % in material, not 100 %. **Huge** (copy `C5H-4902-HUGE_0350` of the 128 x 80 turn-350
  save, loaded on c5h-c 21:37-21:41 with no box, tapped 21:41; three rand states 0x00000001, 0x12345678, 0xCAFEBABE;
  10,515 / 10,518 / about 10,520 placements; 17,540,155 matcher draws per state; about 12 min per state in Python):
  44,040,192 samples, 3,194,880 inland, 1,385,302 variant-free; of those **104,450 (7.54 %) differ in height above
  tol 1 and 70,653 (5.10 %) in material**; the tap lies inside the three runs' envelope on 80.5 % of the inland
  samples (`C:/c5-64bit-work/0465/4902/cmp_huge3.json`). Huge residual by class [L counts, I causes]: heights
  mostly in clusters of hill plots of one art style (continent art 4), 361 plots in all, the largest about 4,000
  samples; with only three states the variant-free mask is weaker there (big hill pieces have few variants, so three
  runs can agree by chance), so part of it is the mask, and a hill-specific rule difference is not excluded;
  materials mostly in and around marsh plots (about 4,000 samples per marsh plot: the marsh stamp). The decode itself is shown right in kind: the same
  height values, the open sea exact, the material histogram within a few per cent, the tap inside the five runs'
  envelope on 88.4 % of the inland samples.
- **Way 1 (replay of the whole-map matcher from `srand(mapRandomSeed)` plus the earlier draws)**: cheap in compute
  (7.6), **not shown exact**, because the earlier-draw count k is not established. Per the MC's #6396 the work stops
  here, before any region step; the choice between way 2 (read the CRT state as data before the matcher) and way 3
  (the server's own match with a cross-fade ring) is not made on this branch.
- **Open, in the order that would close the gate:** (1) the seam residual (port 0x6414D0's region fill, or compare
  with the region byte taken from a tap); (2) k for one save (a native matcher and a search with the material layer as
  the fingerprint, or the two counts ported), then a single-state run that must equal the tap everywhere inland;
  (3) on the huge save, the hill-cluster residual, with more rand states for a stricter mask (save copy
  `C5H-4902-HUGE_0350` stays in c5h-c's saves, SHA-256 c499d3c6...5188c3, equal to `C5H-VP8P-HUGE_0350`; the tap is in
  `F:/c5-64bit-work/0465/4902/huge/`, `huge_field.npz` SHA-256 0d2433e4...3a45cb05a; the Tiny tap
  `tiny_t0008_field.npz` e3023bfa...f2fdfda04c); (4) the marsh stamp, if materials must match in marsh plots.
  The Architect chose way 3 for 0.1b (route note 3.3, MC #6524); the k search is off the critical path and runs later
  as an oracle.
- **Not verified:** that `GetContinentArtType` (Lua) equals what the EXE reads through `ICvPlot1` +0xD8 [I]; the
  rounding of x87 against our f32/f64 arithmetic beyond the tol-1 test; the coastline and decal draw counts of 7.6
  (read from the decompile, not run); the reading of the corner hexes as the two rectangle sizes (it fits the 128 and
  512 textures: the small rectangle spans 128 samples [L, by the run's coverage] [I for the rest]).
- **Kept outside git:** the Tiny tap, globals and plot table in `F:/c5-64bit-work/0465/4902/` (`tiny_t0008_field.npz`
  4.4 MB, `tiny_globals.json`, `tiny_plots.json`); the five runs and the labels in `C:/c5-64bit-work/0465/4902/repro1/`
  (scratch, deleted after the merge unless the lead keeps them for (1)-(2)).

## 8. The region producer (child 4903, way 3 of the Architect's 3.3)

Written by w3-4903 from 2026-11-18 22:40 EST. Design: `architect-2026-11-18-live-terrain-route.md` 3.3 (way 3 for 0.1b,
MC #6524; the ring accepted for 0.1b by the owner, D198), built so that way 1 can later replace the placement source. The
code is ours, written from section 7 (D83). The piece library is read at run time from the player's install. 8.1 is
the interface that 4907 (the server hook) builds against; it was written before the code. 8.2 onwards describe the
design as built.

### 8.1 Interface for 4907 (2026-11-18 23:00; a stub against it is allowed)

Namespace `c5r::liveterrain`, header `render/include/c5r/liveterrain/producer.h`, library `c5render` (no D3D, no
server or IPC dependency). All coordinates are in the tap's own frame: plot (x, y) as the DLL numbers them,
sample (c, r) of the dense W*64 x (H+4)*64 field (`asset-pipeline.md` 4.2 point 5).

**Inputs.**

| Input | Type | Source in the server |
|---|---|---|
| Piece library | `PieceLibrary::open(path)`, immutable, shareable across loads | `<install>/Resource/Common/TerrainPiecesLibrary.fxsb` (version 20; any other version or size is an error, 7.3) |
| Map frame | `MapFrame`: plots W, H, wrap flag, `M_h2w` (16 f32), the four corner hexes (4 x 4 f32), the pattern grid's matrix G and its inverse (2 x 16 i32). `MapFrame::fromConstantsBlock(bytes, n)` parses the EXE's grid constants block 0xD90FBC..0xD9132C (880 bytes, 7.5) | the same block the field tap reads once per load (it is EXE data, read as data) |
| Plot table | `PlotTable`: W * H `PlotInfo` {terrain id, hills, mountain, feature id, natural wonder type name or empty, continent art id, river edge bits (W, NW, NE of river), lake} in the DLL's row-major order (index y * W + x) | the DLL's plots at load; the same table again after a change |
| Tap | `TapView`: the load's dense u16 heights and u8 materials (row-major, x fastest), samplesX = 64 W, samplesY = 64 (H + 4) | the server's existing tap of the load |
| Seed | `uint64_t`, made by `deriveSeed(frame, plotsAtLoad)` (FNV-1a over the map size and the load's plot table) | at load; the same save then gives the same picture (D137) |

**Calls.**

- `Status Producer::create(const PieceLibrary&, const MapFrame&, const PlotTable& atLoad, const TapView& tap,
  uint64_t seed, Producer& out)`: builds the own whole-map placement set (the native matcher of 7.2, with its
  shuffles fed by a generator seeded from `seed`, D194) and keeps a private copy of the tap as the current field.
  Once per load, before any change. Planned cost bound [I, to be measured in 8.6]: Tiny under 2 s, huge under 10 s,
  single-threaded on the workstation.
- `Status Producer::change(const PlotTable& after, ChangeResult& out)`: compares `after` with the producer's
  current plot table, and either produces the new samples of the window (`out.live == true`) or returns
  "wait for reload" (`out.live == false`, with a reason). On a live result the producer takes `after` as its
  current plot table, updates its own placements and writes the window into its current field, so that the next
  change builds on it. On "wait for reload" nothing changes inside the producer. The server writes the window into
  the EXE's field and its own copy (4907); after the next load a new `Producer` is created from the new tap.

**Output (`ChangeResult`).**

| Field | Meaning |
|---|---|
| `live` | true: write the window now; false: the change waits for the reload (stock behaviour) |
| `reason`, `reasonText` | for `live == false`: one of the reasons below, and a line for the log (plots named) |
| `changedPlots` | the plots whose pattern cell changed (x, y) |
| `window` | a list of rectangles in sample coordinates (x already wrapped into 0..samplesX-1, split at the seam if needed), each with its dense u16 heights and u8 materials; together they cover every sample with w > 0 |
| `counts` | samples in D, in w = 1, in the ring (0 < w < 1), and samples changed outside w > 0 (always 0; a non-zero value is a defect and is returned as an error instead) |
| `ms` | wall time of the call (planned bound [I]: under 100 ms for a window of up to 64 plots) |

Heights: `tap + w * (own - tap)` with the own blend of the new placement set, rounded as in 7.4. Materials: the own
material where w >= 0.5, the current field's elsewhere. w = 1 on the changed plots, on D (the samples the own blend
changes) and on every sample a removed or added piece reaches with weight above 0; w falls linearly to 0 over a ring
of `kRingSamples` (64, one plot) outside that set [I: the width is a first choice, to be judged on the pictures of 8.5].

**"Wait for reload" reasons** (enum `WaitReason`; the first one found is returned):

| Reason | When |
|---|---|
| `NoChange` | no plot's pattern cell changed (e.g. a feature change that does not touch the height step); nothing to draw |
| `Water` | the window touches a plot of class coast or ocean (0x5A8640's classes, 7.3), or a sample of material 37. Material 37 is the water ground that the Coast, Ocean and Reef pieces write (their `_T1` holds it) [C, the library]. On a land-class hex it marks the EXE's region byte saying water (the coast polylines), which the producer does not port |
| `Lake` | the window touches a lake plot |
| `RiverEdge` | the window touches a plot with a river edge (the river carve, 7.4 step 7, is not ported) |
| `Marsh` | the window touches a marsh plot or a sample of a material that no piece's `_T1` holds (the marsh stamp and other later writers: materials 1, 5, 22, 29, 38 and anything above 40 in this library [C, the library's `_T1` values]) |
| `Seam` | the window crosses the x wrap seam (the producer writes one unwrapped rectangle). *Changed 2026-11-18 23:58: the seam plots themselves no longer wait, because 4902's seam gap was found (8.3). Was: "the window touches the plots within 2 of the x wrap seam".* |
| `TooLarge` | the window exceeds `kMaxWindowPlots` (256 plots) or the call's cost bound; the large change of 4906 belongs here |

**Errors** (`Status`, never a crash; the server logs them and treats the change as "wait for reload"):
`Io` (library unreadable), `BadVersion` (library version not 20), `BadLayout` (library sizes or row tables
inconsistent; plot table size not W * H; tap size not 64 W x 64 (H + 4); constants block too short or the grid size
in it not W x (H + 4); a plot that maps outside the pattern grid), `Unsupported` (a map wider or taller than 256
plots), `OutOfMemory`.

**Threading.** `PieceLibrary` is immutable after `open` and may be shared by threads. A `Producer` is not
thread-safe: one thread calls `create` and then `change`, one call at a time (the server's terrain worker, not the
render thread). Neither call touches D3D or the IPC.

**Memory.** The library keeps its run arrays (about 26 MB) and decodes a piece's four textures on first use (a 512
piece is 1.25 MB, a 128 piece 80 KB; at most about 210 MB for all 299). A producer holds a copy of the tap (Tiny
13 MB, huge 132 MB) and its placement set (about 24 bytes per placement; 10,500 on huge).

### 8.2 Code as built (2026-11-18 23:55) [C for the code and the runs named]

- `render/include/c5r/liveterrain/producer.h`, `render/src/liveterrain/library.cpp` (the fxsb reader: when the file
  is opened, every row of every texture is checked to close at its width inside the run arrays; textures are decoded
  on first use, under a lock), `render/src/liveterrain/producer.cpp` (map frame, matcher, blend, change step),
  `render/src/liveterrain/CMakeLists.txt`.
- Tests: `render/tests/liveterrain/producer/producer_test.cpp`, 27 tests on a synthetic library, frame and maps.
  - Every input check has a negative test that checks the error code and that nothing was produced: library version,
    truncation, size, row overrun, missing file; frame short, grid mismatch, too large; plot table and tap sizes; no
    library; a change table of another size.
  - The wait reasons water, lake, river edge, marsh, an unknown material and the seam, each also checking that the
    producer stays as it was.
  - `NoChange`.
  - A live change whose changed samples all lie inside the window, counted over the whole field.
  - The same change on two producers gives the same window.
  - The diagnostic option.
  - `game_library` reads the install's file: 299 pieces, maximum level 4, 148 small pieces, material 37 made, 22 not.
- Build and run: `python -I render/build.py --build-dir C:/c5-64bit-work/0465/4903/build --test -R liveterrain
  --game-dir <install>`, run through the build gate. Result at 23:58: 1 of 1 suites passed, 0 skipped.
- Runner and evidence:
  - `render/tests/liveterrain/producer/lt_producer_run.cpp`: the offline runner on recorded inputs.
  - `render/evidence/0465/4903/`: `prep.py` turns 4902's recorded taps, plot tables and constants blocks into the
    runner's inputs, with plot edits; `whole_map.sh`, `chain.sh`, `chain_fh.sh`, `pick.py`, `pictures.py`,
    `sheet.py` and `raw2npz.py` are the run and picture scripts.

### 8.3 The native matcher and the own whole-map placement set [C, runs of 2026-11-18 23:10-23:20]

- The port follows 7.2, with one rule that 4902's Python did not have: **both index lists (patterns, transforms) are
  set to 0 .. n-1 at the start of each level pass**, not once per load. With it, the Tiny seam residual of 7.7 is gone:
  plots (2, 14) and (2, 15) now differ from the tap in **0** samples (they held 4,482 of the 4,900 before).
  *Correction of 7.7's candidate causes (the x wrap, the region byte): the cause was this list reset.* So the seam
  plots are no longer excluded. A window that crosses the seam still waits (`Seam`), because the producer writes one
  unwrapped rectangle.
- The choice among fitting pieces uses the same generator and shuffles as the EXE, but starts them from the load's
  seed in place of the EXE's state (D194). The seed comes from `deriveSeed`: FNV-1a over the map size and the plot
  table at load. Way 1 can later replace only this start state.
- Whole map, against the taps, on the variant-free inland samples of six runs (the native run and 4902's Python
  runs; compare.py of 4902). Matcher draws per load equal the EXE's count.

  | Map | Variant-free samples | Heights above tol 1 | Materials differing | Matcher draws | Placements |
  |---|---|---|---|---|---|
  | Tiny | 241,609 | **203** (0.08 %) | 4,539 (1.9 %), mainly the marsh stamp (7.7) | 3,986,395 | 2,184 |
  | huge | 1,257,628 | 21,833 (1.7 %) | 60,613 (4.8 %) | 17,540,155 | 10,521 |

### 8.4 The change step as built [C]

1. The new plot table gives the new pattern cells. The cells whose flags or continent changed are the changed cells.
   If there are none, the result is `NoChange`.
2. Every own placement whose used cells (inside the grid) include a changed cell is removed, and its claims are freed.
3. Re-match: the matcher of 7.2 runs, level by level, over the anchors within 12 offset steps of any freed cell (a
   pattern spans 10 hexes). Its shuffles are seeded from the load's seed mixed with the changed cells, so the same
   change on the same load gives the same picture. New placements can claim only free cells.
4. The full set (w = 1) is every sample of a removed or added piece's rectangle where its height weight or material
   weight is above zero: its reach, read from the library. A 512 piece reaches up to about 320 x 420 samples (5 to
   6.5 plots); a 128 piece reaches its whole 128 x 128.
5. The ring: w falls linearly from 1 to 0 over 64 samples outside the full set (two-pass chamfer distance, 3/4
   metric). Samples where the own blend before and after the change differ (D) also get w = 1.
6. The clean rule (8.5) is checked on every sample with w > 0. On a clean window the own blend over the window is
   made twice: with the old cells and placements, and with the new ones.
   - Heights: `round(current + w * (own new - current))`.
   - Materials: the own new material where w >= 0.5, the current material elsewhere.
7. The window is one rectangle: the full set plus the ring. Its samples with w = 0 keep their current values. The
   number of changed samples among them must be 0, or the call returns an error; no such error has happened. On a
   live result the producer keeps the new plot table, cells, placements and field.

- **Diagnostic option** (`Producer::Options::diagnosticIgnoreClean`, offline pictures only): draws a window that the
  clean rule would hold back, and names the rule in `reasonText` ("DIAGNOSTIC, clean rule ignored ..."). It is not for
  the product.

### 8.5 The clean-window rule, material 37, and the diagnostic pictures [C for the library facts, L for the pictures' inputs]

- **Material 37 is the water ground.** It is the `_T1` value of the `Coast 21`, `Ocean 21` and four Reef pieces (the
  library's region 1 and 2 pieces) [C, the library]. On a land-class hex it marks the EXE's region byte saying water:
  the coast polylines' flood fill, 0x6414D0, which is not ported. Its 67,354 land samples on Tiny lie along the coasts.
- **Materials no piece makes**: 1, 5, 22, 29, 38 and everything above 40 in this library [C, the library]. The marsh
  stamp writes 22, 29 and 38, by art style [I, 7.7's pairs]. A window holding any of them would erase a later writer's
  work, so it waits.
- **The rule as built.** A window waits on any of:
  - a water or margin hex (a class other than land);
  - material 37;
  - a lake;
  - a river edge;
  - a marsh plot;
  - a material no piece makes;
  - a crossing of the x seam;
  - a size above 256 plots or 256 x 64 x 64 samples.
- **Pictures.** These are offline renders of the heights as shaded relief, with the same renderer for every field;
  DIAGNOSTIC (D112). The install has Vibrant Textures (D94), but the pictures show heights only.
  - Visual Checks **vc-20261118-234052-workstation-55c8**: the Tiny tap at 4906's studio plot FH (50, 16), four changes
    on one producer: flat to mountain, mountain to flat, flat to hills, hills to flat.
    - Drawn with the clean rule ignored, because no hill or mountain window on Tiny is free of water.
    - The stepped coast inside those windows is the per-hex region, which is exactly what the rule prevents.
    - The last row puts the last step beside 4906's stock reload field
      (`F:/c5-64bit-work/0465/4906/scenes/stock/15_reload_field.npz`), drawn the same way.
  - **vc-20261118-235012-workstation-c128**: the huge tap with the clean rule on: hills to flat at (89, 56), then
    flat to mountain at (90, 56). Both went live, with smooth edges.
- **Numbers.** regioncmp at tol 1 counts differing samples on the changed plot, in rings 1-4 and outside. piccmp over
  the crop gives median / p95 / max.

  | Step | inside | ring 1 | ring 2 | ring 3 | ring 4 | outside | piccmp | ms |
  |---|---|---|---|---|---|---|---|---|
  | Tiny flat to mountain (diagnostic) | 4,096 | 19,499 | 24,929 | 14,512 | 2,760 | **0** | 0 / 97 / 243 | 127 |
  | Tiny mountain to flat (diagnostic) | 4,096 | 18,549 | 24,256 | 12,365 | 2,719 | **0** | 0 / 60 / 242 | 77 |
  | Tiny flat to hills (diagnostic) | 4,096 | 17,068 | 23,354 | 11,918 | 2,636 | **0** | 0 / 49 / 234 | 64 |
  | Tiny hills to flat (diagnostic) | 4,096 | 16,267 | 21,950 | 11,283 | 2,389 | **0** | 0 / 47 / 234 | 92 |
  | huge hills to flat (live) | 4,096 | 23,634 | 24,644 | 573 | 0 | **0** | 0 / 61 / 234 | 108 |
  | huge flat to mountain (live) | 2,011 | 14,141 | 15,482 | 5,564 | 225 | **0** | 0 / 30 / 234 | 91 |
  | Tiny last step against the stock reload | 1,872 | 16,782 | 41,349 | | | 1,760,813 | 3 / 163 / 243 | |

- **The stock reload re-rolls.** 4906's stock reload differs from the tap far outside the rings: 1.76 million
  samples. After this chain of changes (land and water changes among them) the reload picks different pieces over a
  wide area, so a comparison of the live result with the reload is dominated by the reload's own re-roll [L for the
  count, I for the cause].
- **Practical finding: the rule holds back most changes on these two maps.** On Tiny, 0 of 4 tried hill and mountain
  changes would be live; on huge, 2 of 4. A 512 piece's reach plus the ring is about 7 to 8 plots across, and these
  maps rarely have a land patch that size without a river, a lake or a coast.

### 8.6 Cost [L, 2026-11-18 23:10-23:50]

PERFORMANCE.md applies: these are single runs on the workstation, an indication only (COMMON 23).

| What | Tiny 56 x 36 | huge 128 x 80 |
|---|---|---|
| library open and check | 55-104 ms | 70-104 ms |
| whole-map placement set (create) | 217-290 ms (5 runs) | 903-1,262 ms (6 runs) |
| one change, live or waiting | 50-234 ms (8 calls) | 66-168 ms (6 calls) |
| own whole-map blend (tools only, not on the change path) | 0.8 s | 6.4 s |

4906's large change (a whole landmass to water) waits at once with `Water`; its window would also exceed `TooLarge`.
No timing series was run, so these numbers are not performance claims.

### 8.7 Not verified / open (2026-11-18 23:58)

- The region byte is the hex's class, not the EXE's polyline flood fill. This is why every water edge waits. Porting
  0x6414D0 would let coastal changes go live.
- The ring width (64 samples) is a first choice, to be judged on the pictures by the owner (D198). How visible a moved
  neighbour bump is in the game has not been seen: the pictures show heights only.
- The remaining whole-map residual is not explained: 203 samples on Tiny, 21,833 on huge in hill clusters. The k
  search (way 1) could find it with every inland sample comparable; it runs later as an oracle.
- That the Lua `GetContinentArtType` equals the EXE's continent byte [I, 7.7].
- The producer's own blend is computed per window. It has not been checked against a whole-map rerun after several
  changes; only the count outside each window is checked.
- ASan (COMMON 9): `render/build.py --asan --test -R liveterrain` passed on 2026-11-19 at 00:15. The canary reported
  its overflow, the suite passed with 0 skipped and 0 ASan reports, and the job's peak commit was 210 MB. The runner
  was not run under ASan.

### 8.8 How many changes go live: the measurement before widening (ticket 4908, 2026-11-19 01:00-01:15) [L, the runs named]

- **Method.** Two recorded inputs were used, the Tiny and the huge tap (4902's taps, prepared by `prep.py`).
  - `render/evidence/0465/4908/gen_cases.py` (seed **4908**, 30 cases per kind) changes one plot from the load's
    table per case:
    - a hill raised (flat land to hills);
    - a mountain raised (flat or hill land to mountain);
    - a hill flattened;
    - a mountain flattened (to flat grass);
    - a feature removed (forest, jungle or marsh);
    - a feature added (forest on bare flat land);
    - a natural wonder added (`FEATURE_ULURU` on bare flat land).
  - `lt_producer_run --batch` applies each case to a fresh producer of the load (`measure.sh`).
  - 4906's 14 transitions (`gen_4906.py`: the transitions' own plots only, without the DLL's side effects; `large`
    is the smallest landmass of 3 or more plots without a wonder) run as one chain on Tiny. Each step meets the state
    the previous ones left, as in the game: a step that waited is still pending in the next one.
  - Commands, run from the repository root:
    `python -I render/evidence/0465/4908/gen_cases.py --prep C:/c5-64bit-work/0465/4903/<map> --plots-json <map>_plots.json --seed 4908 --per-kind 30 --out C:/c5-64bit-work/0465/4908/cases_<map>`;
    `bash render/evidence/0465/4908/measure.sh <build> <out>`;
    `python -I render/evidence/0465/4908/summarize.py <out>`.
- **Result with 4903's rule** (the seam plots already do not wait):

  | map | kind | cases | live | no change | water | lake | river edge | marsh | seam | too large | live share of those that draw |
  |---|---|---|---|---|---|---|---|---|---|---|---|
  | Tiny | hill raised | 30 | 0 | 0 | 27 | 0 | 0 | 3 | 0 | 0 | 0 % |
  | Tiny | mountain raised | 30 | 1 | 0 | 27 | 0 | 1 | 1 | 0 | 0 | 3 % |
  | Tiny | hill flattened | 30 | 0 | 0 | 27 | 0 | 0 | 3 | 0 | 0 | 0 % |
  | Tiny | mountain flattened | 13 (all there are) | 0 | 0 | 11 | 0 | 0 | 2 | 0 | 0 | 0 % |
  | Tiny | feature removed | 30 | 0 | 30 | 0 | 0 | 0 | 0 | 0 | 0 | - |
  | Tiny | feature added | 30 | 0 | 30 | 0 | 0 | 0 | 0 | 0 | 0 | - |
  | Tiny | wonder added | 30 | 1 | 0 | 24 | 0 | 0 | 5 | 0 | 0 | 3 % |
  | Tiny | all | 193 | 2 | 60 | 116 | 0 | 1 | 14 | 0 | 0 | **2 %** |
  | huge | hill raised | 30 | 0 | 0 | 29 | 0 | 1 | 0 | 0 | 0 | 0 % |
  | huge | mountain raised | 30 | 0 | 0 | 29 | 0 | 0 | 1 | 0 | 0 | 0 % |
  | huge | hill flattened | 30 | 0 | 0 | 29 | 0 | 0 | 1 | 0 | 0 | 0 % |
  | huge | mountain flattened | 30 | 1 | 0 | 27 | 0 | 0 | 2 | 0 | 0 | 3 % |
  | huge | feature removed | 30 | 0 | 30 | 0 | 0 | 0 | 0 | 0 | 0 | - |
  | huge | feature added | 30 | 0 | 30 | 0 | 0 | 0 | 0 | 0 | 0 | - |
  | huge | wonder added | 30 | 0 | 0 | 30 | 0 | 0 | 0 | 0 | 0 | 0 % |
  | huge | all | 210 | 1 | 60 | 144 | 0 | 1 | 4 | 0 | 0 | **1 %** |

  4906's chain on Tiny: 0 of 14 live. Steps 1-3 and 5-13 wait for `Water`. Step 4 (`fh_flat`) is "no change": FH is
  back at its load state, because steps 1-3 never went live. Step 14 (`large`) is `TooLarge` (a window of 3,520 x
  1,043 samples).
- **Reading.**
  - Water blocks 89 % (Tiny) and 96 % (huge) of the changes that wait; marsh comes next.
  - Feature changes (forest, jungle, marsh) never change a pattern cell. They are "no change" for the height step,
    and the EXE's own feature models and the marsh stamp cover them already (4.1).
  - The order of payoff for 4908 is therefore water first, then marsh.

### 8.9 The widened rule as built (ticket 4908, option A, 2026-11-19 01:30-02:00) [C for the code, L for the runs]

The lead's option A (#6660, agreed by the MC in #6662): a change that keeps every plot's land or water class does not
move the EXE's coast lines or its region byte. Such a change may therefore reach water, so long as everything the
producer cannot reproduce is kept from the current field. Three options of `Producer::Options` (`producer.h`, all on
by default):

| Option | Samples kept from the current field (w forced to 0) | Why |
|---|---|---|
| `protectWater` | samples of coast, ocean and margin hexes, and samples of material 37 | the EXE's region byte and the coast strips (7.4) are not reproduced; material 37 marks where the EXE's region fill said water (8.5) |
| `protectMarsh` | samples of marsh plots, and samples of a material no piece makes (1, 5, 22, 29, 38, above 40) | the marsh stamp writes after the blend (4.1) |
| `protectRivers` | samples within `kRiverKeepSamples` (60) of the centre of a plot with a river edge: its hex plus about 25 samples beyond each edge | the river carve (7.4 step 7) lowers the field along the edge on both sides |

- Next to the kept samples, w falls to 0 over `kFeatherSamples` (32 samples) on the land side, through a chamfer
  distance from the kept set. The new land shapes therefore meet the kept ground without a step.
- A changed plot that is water, or whose land or water class changes, still waits (`Water`, "the coast lines move").
- `Lake` waits only with `protectWater` off; `RiverEdge` only with `protectRivers` off; `Marsh` only with
  `protectMarsh` off.
- The seam rule is unchanged from 4903: only a window that crosses the seam waits.
- Interface change, posted on the thread for 4907 and 4905: three new `bool` members of `Producer::Options`, which
  default to the widened behaviour. The calls are unchanged. The header is C++14-clean.
- The runner reports `visible` per batch case: the number of samples whose height the live window changed. 0 means
  the change went live, but every sample it would draw is kept.
- Tests (`producer_test.cpp`, 30 tests):
  - `change_live_next_to_water_keeps_it`, `change_live_next_to_marsh_keeps_it` and
    `change_live_next_to_river_keeps_it`: the change is live and every sample around the kept plot is bit-equal.
  - `change_wait_land_to_water`: the change waits even with the protection on.
  - The 4903 wait tests now run with the protection off.

### 8.10 Option B as built: the EXE's region fill, its port, and what the product does with it [C, decompile of 0x6414D0, 0x697A40, 0x686760, 0x5E0320, 0x7A5270, 0x5DE4B0, 0x450D40, and the block set-up in 0x425060; 2026-11-19 01:40; "as built" 2026-11-22, ticket 4916]

*Retitled 2026-11-22 (4916, lead13-0615); was "Option B, described: the EXE's region fill and what an exact port needs".
The description below (items 1-5, the coastline curves, the size estimate) is kept as it was written before the port;
the as-built summary comes first.*

**As built (epic 0615, D216; on main since the early milestone c1dc17d2a of 2026-11-21 and the second merge 33f50a43a
of 2026-11-22; first shipped in the first beta candidate, layout x64-1b87dd18-p06d9e82) [C for the code, L for the live
runs named in each section].** Each line points to the section that holds the detail; nothing is repeated here.

- **The port, our own code from the description (D83).** The coastline curves, the per-block lists and the region
  fill of this section, written from 8.10.1 by ticket 4910: `render/include/c5r/liveterrain/coastlines.h`,
  `render/src/liveterrain/coastlines.cpp` (8.10.2); the zero-length segments and non-finite points it refuses or skips
  (8.10.3). Offline against the taps (`lt_region_run`, 8.10.2): every coast-adjacent land hex without a river matches
  the tap's material-37 fingerprint, 47,708 of 47,708 samples on Tiny and 148,700 of 148,700 on huge, with the save's
  `mapRandomSeed`; the misses are on or next to river edges (the river carve, below).
- **The coast strips and lakes** (step 6 of `CreateTerrain`, ticket 4911): 8.15 (the EXE's behaviour, described),
  8.16 (the port and its API). Away from rivers the strips reach 8.3's inland tol-1 shares (Tiny 99.94 %, huge
  99.86 %).
- **The land/water step in the producer** (ticket 4913): `Producer::enableLandWater` and the change path, 8.17 (its
  head names which binary carried each number).
- **The map seed** (ticket 4912): read by the server from the DLL it hosts at each field hand-over, not exposed to Lua,
  carried in the journal's seed trailer; without a seed every land/water change of that load waits for the reload,
  so no coast is drawn from seed 0 (6.9).
- **In server.exe** (ticket 4914 (1)): wired in `C5rProducer::create` with the install's coast styles and TerrainDef
  read once per factory; on by default with live terrain (D198); the test switch `C5_RENDER_LIVE_LANDWATER=0` turns
  the step off completely (it is not an option; 8.17 "The server's land/water inputs", 11.4 key-off smoke PASS). The
  EXE's field write covers the land/water windows (8.13.4); the ice and wave gate gets each change's written windows
  (10.9); the feed's rolling refresh catches changes the DLL does not report (11.7).
- **Live** (ticket 4915; I1, I2, I3 and the final-code smokes, 6.11, 6.12, 11.4, 11.5): Tiny and huge, both builds
  (26577625 on c5h-new-a, 4390913 on c5h-a), 0 LOUD and no box after the h18 fix (11.8, 11.9). The headed video and
  live against reload on oldpc: 11.10; the two live-only looks it showed were stale 512 near pairs, fixed by 0667
  (11.11), and the AFTER videos are on F:.
- **What is not exact, and why.** The candidate's known differences are one list, 11.3. In short: a reload's own
  re-roll can move later coastlines (11.3 item 5, P5, 2.3); a wonder's fixed piece is placed again (item 2); the coast
  strips' band rest (item 3); ice on new land (item 4); the minimap and Strategic View colours are stock behaviour
  (item 7); a change can show one turn late where the DLL sends no notification (item 9, 11.7; follow-up 0656).
- **The river difference (MC #8420 (d): named, not ported).** The EXE's river water mesh (0x6D51D0) and its river
  carve (0x412680) run after the strips and are not ported: the river mesh is the EXE's, tapped once per load. The
  producer keeps the load's ground within 60 samples of a river-edge plot's centre (`protectRivers`, 8.9), except on
  the changed plots, on samples whose region byte the change moves, and on samples that are water after the change
  (the w7-ridge fix, 8.17). What the player sees until the next reload (11.3 item 1): where a coast moves next to a
  river, the shore keeps its old outline for about one plot (a small notch of water or a strip of the old bank), and
  the river's water ends where it ended at the load. The keep is wider than the river because the feed's river flag
  means "river on any edge" (`riverEdges = 7` in `PlotTableFromFeed`); the producer gets the real edges in ticket 0631
  (backlog p4, after the first beta). *Done 2026-11-23 (8.18): the feed carries each plot's own edge bits; the keep
  now covers only plots with an edge of their own.*

**The description as written before the port (2026-11-19, corrected in place by 4910 where marked):**

What fills the region byte that the block blend tests (7.4), in our own words:

1. **Per block, three lists** prepared by the block set-up (0x425060) from `CreateTerrain`'s coastline curves:
   - boundary polylines, each with a region value (a block's entries at job +0x414 / +0x41C);
   - fill polylines, each with a region value (+0x418 / +0x59C);
   - seed points with values (+0x720 / +0x724 / +0x764): four per hex, at (+-1, +-1) samples from the hex's
     truncated centre, each listed in the block it falls in, carrying the hex's class (0 land, 1 coast, 2 ocean)
     from the class grid that 0x5A8640 fills (7.3). *(Corrected 2026-11-20, 4910, 8.10.1 step 8; was: "one per hex
     centre in or near the block".)*

   All points are in sample coordinates and are truncated to integers.
2. **Boundary lines.** Every segment of every boundary polyline is drawn with a Bresenham line (0x686760: the
   integer midpoint form, stepping along the major axis). Each pixel inside the block gets the polyline's value
   (0x7A5270).
3. **Fill lines.** Every segment of every fill polyline is walked with the same Bresenham line (0x5E0320). Each pixel
   that is still unset (region byte 4) is set to the polyline's value and pushed as a seed (0x5DE4B0 > 0x487550).
   After each polyline, a **4-connected flood fill** (0x697A40) spreads the value from those seeds over unset
   samples. The fill stops at samples already set and at the block's edge. Both coordinates are wrapped before the
   block test, x by the field width and y by the field height (`(S * 100 + v) % S`). *(Corrected 2026-11-20, 4910;
   was: "x wraps by the field width"; the set-at-push was not stated.)*
4. **Hex seeds.** Each seed point that is still unset is set to its hex's class and flood-filled the same way.
5. Every sample still unset after that becomes 2 (ocean).

So the region boundaries follow the **coastline curves**, not the hex edges. Those curves are made at the start of
`CreateTerrain` (`asset-pipeline.md` 4.1 step 3); the full description is 8.10.1:

- the coast grid (0x489CC0) puts two points on every hex edge between land and coast and between coast and ocean,
  links the edges into chains and smooths them; 0x491050 extracts the chains as polylines;
- each polyline is jittered by `rand() % 1000` on a multiresolution pyramid (0x5674C0, 7.6);
- the coast-mesh converter (0x73C170, a job of 0x4BEF10) turns each polyline into five parallel lines; the middle one
  (the waterline) is smoothed three times and given the coast style's character offsets;
- `CreateTerrain` copies every waterline into one point array (the **boundary** list, value = the curve's type 0 or
  1), adds an offset line one sample to either side of it (the **fill** list, values type and type + 1; tangents
  from the 3-point derivative 0x4595B0) and cleans the offset lines' self-intersections (0x6545F0).
  *(Corrected 2026-11-20, 4910; was: "they are subdivided and jittered" and "`CreateTerrain` resamples them with a
  spline (0x4595B0)": 0x4595B0 only gives the tangents; the smoothing is 0x73C170's.)*

**What an exact port (option B) needs, and its size [I]:**

- The coastline extraction, the subdivision and jitter, the spline resampling, the normal offsets and the cleanup, as
  described above (4.5's coastline row: 6-10 h, of which the description and its check against a tap are 2-4 h).
- The per-block lists (1-2 h) and the region fill (1-2 h, simple as above).
- **`mapRandomSeed`.** The coastline jitter is the first `rand()` consumer after `srand(mapRandomSeed)` in
  `CreateTerrain` [C, the closure in `evidence/2900/2904/rand_in_bake.txt`: the other rand users come later]. So the
  curves can be exact without any other draw count, but the seed must come from the DLL (`CvPreGame::mapRandomSeed`,
  not exposed to Lua). The offline taps of 4902 do not carry it: a live read, or a field in 4907's feed, would be
  needed.
- Check: the region byte is not tapped, but material 37 marks the water region (8.5). A port is right when the
  reproduced region gives the tap's 37 samples, and the coast-adjacent land samples reach 8.3's tol-1 shares.
- **Total: about 8-14 h**, and it needs the seed in the offline inputs. It would let land-water changes (4906's `lw`,
  `wl`, `large`) and their coast strips be drawn; those are after the beta (MC #6462). Option B is built only on the
  MC's word. *(2026-11-20: built under D216, epic 0615, ticket 4910; the description it is ported from is 8.10.1.)*

#### 8.10.1 The coastline curves and the region fill, as described for the port (ticket 4910, 2026-11-20)

Written by w1-4910 from a read-only headless Ghidra pass over the analysed project (old EXE build 4390913, stamp
0x546CD0A8; scratch copy under `C:/c5-64bit-work/0615/w1/`, no output committed, D83) on 2026-11-20 15:20-16:10 EST:
decompiles and disassembly of 0x709B50 (`CreateTerrain`), 0x489CC0 and its four row jobs 0x445B50, 0x451080,
0x680610, 0x543560, the link helpers 0x780510 / 0x6EBF70 / 0x5EB8B0, the extraction 0x491050 > 0x4137A0, the jitter
0x5674C0 with its pyramid 0x6001A0 / 0x5089C0 / 0x679C40 and kernels 0x7961C0, 0x47FF10, 0x531770, 0x5DC7D0,
0x4E16C0, the coast-mesh converter 0x4BEF10 > 0x57BD70 > 0x73C170 with 0x512870, 0x53ED50, 0x4C0560, the style and
character-spline loaders 0x4BD430 and 0x437B80, the offset and cleanup code in `CreateTerrain` (0x70A240-0x70AC41),
0x67D5F0, 0x6545F0, 0x6BA6F0, 0x425060, 0x5A8640 and the fill of 8.10. Constants were read from initialised data
in the same project and are quoted as values. Everything is [C] unless marked; nothing here has been run against a
tap yet (the check is 8.10.2). Our own words throughout.

**Frames.** All work is in the pattern grid's frame (7.5): W x (H + 4) cells, cell (x, y) = plot (x, y - 2); a cell's
cube coordinates are its offset coordinates (q = x - (y >> 1), r = y) taken through the grid's inverse matrix
(0xD91210), a cube goes back to a cell through the forward matrix (0xD911D0) and then x = q + (r >> 1), y = r. A cell
whose y is outside 0 .. H + 3 does not exist; x outside 0 .. W - 1 wraps when the map wraps and does not exist
otherwise. Sample positions are `M_h2w` (0xD91030) applied to the cube as floats (row vectors, w = 1). The six
directions, as cube steps, in the EXE's order: d0 (0, 1, -1), d1 (-1, 0, 1), d2 (1, -1, 0), d3 (0, -1, 1),
d4 (1, 0, -1), d5 (-1, 1, 0) (two copies of the table, 0xC8FA98 as floats and 0xC8FB08 as ints). d(k + 3) is the
opposite of dk. Around a hex the directions run in the order 0, 4, 2, 3, 1, 5.

**Step 1: classes and kinds (0x445B50, one job per grid row).** Each cell gets the class of its pattern-cell flags
(7.3's rule: 0 land if any land bit of 0xF3EF9FFF, else 1 coast if any of 0x0C102000, else 2 ocean; the margin rows
carry the pre-fill 0x2000, so they are coast) and a **kind** 0..12 used to pick the coast style: mountain 11
(bit 0x400; the cell is also marked "mountain"), grass 1 (0x1), grass hill 2 (0x20), plains 3 (0x2), plains hill 4
(0x40), snow 5 (0x10), snow hill 6 (0x200), desert 7 (0x4), desert hill 8 (0x80), tundra 9 (0x8), tundra hill 10
(0x100), tested in that order; a water cell with any of 0x106000 gets 12; anything else 0. The numbers are the order
of `CoastStyles.xml`'s styles (Default, Grass, GrassHill, Plains, PlainsHill, Snow, SnowHill, Desert, DesertHill,
Tundra, TundraHill, Mountain, DefaultShallows; the name table at 0xB9A064).

**Step 2: edges (0x451080).** A cell owns its edges toward d0, d1 and d2. For d = 0, 1, 2 in that order, with the
neighbour N across the edge (it must exist): an edge is made only for the pairs land/coast and coast/ocean (land next
to ocean makes none). It gets the next free slot of the cell (0..2) and a **code**: d for land->coast (the cell is
land), d + 6 for coast->land, d + 12 for coast->ocean, d + 18 for ocean->coast. Codes below 12 are type 0 (the
land/coast line), 12 and above type 1 (the coast/ocean line); `code % 12 < 6` means "forward" (the owner is the lower
class). The slot also records a kind byte: the land cell's kind for type 0 (the owner's own, or N's when the owner is
the coast), 12 for type 1. Its two points are `centre + a_d` and `centre + b_d`, where centre is the cell's sample
position and a_d, b_d are fixed offsets made once per load (0x489CC0): with n_i = M_h2w(T + d_i) - M_h2w(T) (T the
inverse matrix's translation, i.e. the cube of cell (0, 0); [I] that 0x489CC0's first transform uses the inverse
matrix, the check of 8.10.2 tests it) and the hex corners c_ij = (n_i + n_j) * (1/3), the points sit a quarter and
three quarters along the edge: a_0 = 0.25 c_05 + 0.75 c_04, b_0 = 0.75 c_05 + 0.25 c_04; a_1 = 0.25 c_13 + 0.75 c_15,
b_1 = 0.75 c_13 + 0.25 c_15; a_2 = 0.25 c_24 + 0.75 c_23, b_2 = 0.75 c_24 + 0.25 c_23 (f32; 1/3 as 0x3EAAAAAB;
products formed first, the 0.25 term added to the 0.75 term). So a_d lies near the corner shared with the direction
p(d) and b_d near the one shared with q(d), where (p, q) = (4, 5), (5, 3), (3, 4) for d = 0, 1, 2 (0xA28E6C). For
d = 3, 4, 5 the same pair test records only the code (d plus the same 0 / 6 / 12 / 18) in a second list, the cell's
"in-edges" (owned by the neighbour). The job also stores, per cell, the width of the map in samples dx =
M_h2w(Ginv (W, 0, -W)).x - M_h2w(T).x, used to shift points across the x seam.

**Step 3: links (0x680610 with 0x780510 and 0x6EBF70).** An edge (cell c, direction d, neighbour N3 = c + d_d) has
two corners: the p-corner shared with N1 = c + d_p and the q-corner shared with N2 = c + d_q (d_p + d_q = d_d). At a
corner the line continues into the one other edge of the **same type** at that corner, if there is one: at the
q-corner, first the edge between c and N2 (an in-edge of c, owned by N2 as N2's edge toward the opposite of d_q),
else the edge between N3 and N2 (owned by N3, N3's edge toward the opposite of d_p); at the p-corner, first the edge
between c and N1 (owned by N1), else the edge between N3 and N1 (owned by N3). The link stores the target cell's
unwrapped offset coordinates and its slot. A forward edge's "next" is at its q-corner and its "previous" at its
p-corner; a reversed edge's are the other way round. Consecutive edges along a line always alternate between forward
and reversed, so walking a line visits a forward edge as a -> b and a reversed one as b -> a.

**Step 4: smoothing (0x543560 in ten passes, values 1, 2, 3, 2, 1, 2, 3, 2, 1, 2 from 0xB99C84).** Each pass runs
over all cells (one job per row) and, for value 1 and 3, writes new edge points into a scratch copy; value 2 copies
the scratch back. So all edges are updated from the same old values (three smoothings and two mountain pushes, in
the order smooth, push, smooth, push, smooth).
- The six-point window of an edge, in walking order: P0, P1 = the previous edge's two points (its b then its a, as
  stored), P2, P3 = the edge's own a and b, P4, P5 = the next edge's b then a; a neighbour across the x seam is
  shifted by k * dx (k = (x_unwrapped - x_wrapped) / W). With no previous edge, P0 = a + (a - b) and P1 = a +
  (a - b) * 0.5; with no next edge, P4 = b - (a - b) * 0.5 and P5 = b - (a - b). If the edge is reversed, the six
  points are taken in reverse order (P0 <-> P5, P1 <-> P4, P2 <-> P3).
- Smooth (value 1): new first = 0.25 P1 + 0.5 P2 + 0.25 P3, new second = 0.25 P2 + 0.5 P3 + 0.25 P4 (f32, summed
  left to right); stored back as (a, b) = (first, second) for a forward edge and (second, first) for a reversed one.
- Push (value 3): only for slots of a mountain cell. The tangents t1 = (P3 - P1) * 0.5 and t2 = (P4 - P2) * 0.5 (the
  3-point derivative of 0x7A5F70 at 1.5 and 2.5) give unit normals n = (t.y, -t.x) / |t|; a forward edge gets
  b += 4 n1 and a += 4 n2, a reversed one b += 4 n2 and a += 4 n1 (as read; the crossing of 1 and 2 is the EXE's).
  Other slots copy their points unchanged.

**Step 5: extraction (0x491050 > 0x4137A0).** Lines are taken in a fixed order, which is the rand order of step 6:
first, scanning rows y = 0 .. H + 3, then x = 0 .. W - 1, then slots 0..2, every edge that has a next, has no
previous and is not yet used (the open lines); then, in the same scan, every edge with a next not yet used (closed
loops). From the start edge the walk appends its two points in walking order and then follows the next links,
appending each edge's two points and marking the edge it left as used, until the current edge has no next or is
already used. A closed loop therefore ends by appending its start edge again. Each point carries x and y
(shifted by the accumulated k * dx when a link crosses the seam), a blend flag (1.0 on the first point, flipped
between 1.0 and 0.0 whenever the kind byte changes from the previous edge's) and two bytes: the edge's kind and the
previous point's kind. A line ends up **closed** when its last edge is its start edge; its wrap offset is then
(last point - second point). Its **type** is that of the start edge. A line with fewer than 4 points (one edge) is
dropped.

**Step 6: jitter (0x5674C0, per line in the order of step 5).** This is the only `rand()` user before the decal
set-up; `CreateTerrain` calls `srand(mapRandomSeed)` first (MSVC CRT: state = seed; rand = (state = state * 214013
+ 2531011) >> 16 & 0x7FFF).
- Rounds: with m = n (the line's point count) and k = 0, while m >= 6 and k < 4: m is made even (+1 if odd), m =
  m / 2 + 1, k += 1. With k = 0 the line is left as it is and draws nothing.
- The working size is t = 2^k (m - 2) + 2 >= n; the line's n points are copied in and the rest padded with its last
  point.
- A multiresolution pyramid (reverse Chaikin) is built by k decimations of a level of s points (s even) into m = s
  / 2 + 1 coarse points C and m - 2 details D: C_0 = 1.5 s_0 - 0.5 s_1, C_(j+1) = -0.25 s_2j + 0.75 s_2j+1 + 0.75
  s_2j+2 - 0.25 s_2j+3, C_(m-1) = -0.5 s_(n-2) + 1.5 s_(n-1); D_j = 0.25 s_2j - 0.75 s_2j+1 + 0.75 s_2j+2 - 0.25
  s_2j+3 (j = 0 .. m - 3). Reconstruction of a level of 2m - 2 points from C and D: first the details alone, F_0 =
  -0.25 D_0, F_1 = -0.75 D_0, then for i = 1 .. m - 3: F_2i = 0.75 D_(i-1) - 0.25 D_i, F_2i+1 = 0.25 D_(i-1) - 0.75
  D_i, and F_(2m-4) = 0.75 D_(m-3), F_(2m-3) = 0.25 D_(m-3); then the coarse points are added by Chaikin: F_2j +=
  0.25 C_(j+1) + 0.75 C_j, F_2j+1 += 0.75 C_(j+1) + 0.25 C_j (j = 0 .. m - 2; the pair of products is summed first,
  then added). The levels are kept in one array of 2t + 1 points, alternating between two halves; the exact index
  rule is in the port (`coastlines.cpp`) and follows from storing each coarse level and its details next to the
  level they came from.
- Amplitude: A = (float) pow(0.78, 5 - k) (the double 0x3FE8F5C280000000), then for each of the k rounds: rebuild the
  next finer level (count c), then displace every point of that level along its normal, in place and in order:
  the tangent is the 3-point derivative of the level (first point: p1 - p0; point i: (p_(i+1) - p_(i-1)) * 0.5,
  using the already displaced p_(i-1); last: p_(c-1) - p_(c-2)), the unit normal is (t.y, -t.x) / |t| (|t| from an
  x87 square root of t.y^2 + (f32) t.x^2, stored as f32), and the distance is d = ((rand() % 1000) * 0.001 - 0.5) *
  A * 35 (f32 in that order) for the first point and each interior point, while the **last point draws a rand() but
  moves by A along its normal, whatever was drawn**. After the round A = A * A.
- So a line draws exactly sum over the k rounds of that round's point count, 2^r (m_k - 2) + 2 for r = 1 .. k
  (m_k the coarsest count), and the count depends only on n. The first n points are copied back; a closed line then
  gets P0 = P_(n-2) - wrap and P1 = P_(n-1) - wrap.

**Step 7: the coast mesh's five lines (0x4BEF10 > 0x73C170, one job per polyline, same order).** The converter
makes five parallel lines per polyline from the coast styles (`CoastStyles.xml`, read at run time from
`TerrainModels.fpk`). The EXE keeps each `CoastStyle` element as 25 values; per line k = 0..4 (high side, high
beach, waterline, low beach, low side): an offset along the normal (-SmoothingOffset.HighSide, -HighBeach.Offset,
Waterline.Offset, +LowBeach.Offset, +SmoothingOffset.LowSide), a flag (0, 1, 1, 1, 0), a height (HighBeach,
HighBeach, Waterline, LowBeach, LowBeach .Height) and a character spline index (none, HighBeach, Waterline, LowBeach
.Character, none; 65535 in the file = none); then the two TerrainType values and the style's number (0x4BD430).
Style lookup by kind (0x4C0560) returns all zeros when no element has that number. Only line 2, the waterline,
reaches the region fill; its offset is 0 in every style of the shipped file, and its character is 0
(`CoastCharacterSpline_01_Water.txt`) for the land kinds, 6 (`CoastCharacterSpline_03_Water.txt`) for Mountain and
none for DefaultShallows, i.e. for every type-1 line. The coast strips of ticket 4911 read all five lines. The
converter works on 16-byte points (x, y, blend t, the two kind bytes A and B):
- (a) **First pass (offsets).** Two evaluators on the polyline's points: the 3-point derivative (0x512870, weights
  (f - 1, 1 - 2f, f) on p_i, p_(i+1), p_(i+2) for parameter i + f) and the 3-point quadratic B-spline value (0x7222E0,
  weights (0.5 f^2 - f + 0.5, -f^2 + f + 0.5, 0.5 f^2), which also returns p_i's two kind bytes). For an open line
  of n points, point j is evaluated at parameter 0 for j = 0, at (n - 2) - 0.01 for j = n - 1 (the last three
  points, f about 0.99) and at j - 0.5 for the interior (points j - 1 .. j + 1, f = 0.5, so the kind bytes are those
  of point j - 1). With the unit normal (T.y, -T.x) / |T| and the evaluated t: when the evaluated kind A differs from
  the last one, styles A and B are re-read (A = that kind, B = the evaluated byte B) and a parity bit (true at the
  start) flips; the weight of A is t, or 1 - t while the parity is true; each line k gets point j + (w A.offset[k] +
  (1 - w) B.offset[k]) * normal, with the source point's t and kind bytes. A **closed** line is evaluated at j + 0.5
  for j = 0 .. n - 2 (points j .. j + 2; the last one at f = 1) and the result goes to point j + 1 of each line;
  then each line's point 0 = its point n - 2 - wrap and its point n - 1 = its point 1 + wrap (x, y and t; the kind
  bytes of point 0 are not written by the EXE [I: the port copies point n - 2's]).
- (b) Two Chaikin rounds on every line (0x53ED50: out_2j = 0.25 p_(j+1) + 0.75 p_j, out_2j+1 = 0.75 p_(j+1) + 0.25
  p_j on x, y and t; both outputs take the kind bytes of p_(j+1); n -> 2n - 2 -> 4n - 6).
- (c) **Second pass (character offsets)**, lines 0 to 4 in order with one parity bit (true, set once before line 0)
  and one "last kind" carried across the lines: on points 0 .. c - 2 of each line (c = 4n - 6), in place and in
  order, the unit tangent u = T / |T| of the 3-point derivative at (j + 0.5) (that is (p_(j+2) - p_j) * 0.5; at j = c
  - 2, p_(j+1) - p_j); when the point's kind A differs from the last kind, styles A (its kind) and B (its byte B) are
  re-read and the parity flips; points 1 .. c - 2 then move: the weight w of A is the point's t, or 1 - t while the
  parity is true; for each of the two styles whose line-k character is not none, the entry (e1, e2) = table[j % 98]
  is turned into the line's frame, (e1 u.x - e2 u.y, e2 u.x + e1 u.y); the point moves by w times A's turned entry
  plus (1 - w) times B's. A closed line's ends are reset from the wrap offset again after each line.
- (d) a third Chaikin round (8n - 14 points per line). The converter keeps the five lines as ranges [k c', (k + 1)
  c') of one array (c' = 8n - 14) and the polyline's type; `CreateTerrain` takes line 2.
- **Character tables (0x437B80, once per load):** the first 98 lines `[x,y,z]` of the file give 2D points r; two
  reverse-Chaikin decimations (98 -> 50 -> 26, coarse part only) and two Chaikin rounds (26 -> 50 -> 98) give the
  smooth points s; with the extra points s_-1 = (2 s_0.x - s_1.x, s_97.y) and s_98 = (s_97.x + s_1.x - s_0.x, s_97.y)
  (the y of both is s_97's as read [I: may be a stack alias of s_0.y; it only touches entries 0 and 97]), the tangent
  of point i is (s_(i+1) - s_(i-1)) * 0.5, u its unit vector, e = s_i - r_i, and the entry is (e . u, e.y u.x - e.x
  u.y).

**Step 8: the point array and the two lists (`CreateTerrain` 0x70A240-0x70AC41).** For each curve in order, its
waterline points are appended to one array of 2D points and a **boundary** range {start, end, value} is added, value
= the curve's type (0 or 1). Then for each boundary range of n points, two offset lines of n - 1 points each are
appended, with t = p_(i+1) - p_i, u = t / |t| (x87 root, f32) and the midpoint m = (p_i + p_(i+1)) * 0.5: the first
line is m + (-u.y, u.x) with value = type, the second m + (u.y, -u.x) with value type + 1 (so a land/coast curve
fills land on one side and coast on the other, a coast/ocean curve coast and ocean). Each offset line is cleaned as
it is made: for i from its start to its end - 3: let s1 = distance of p_(i+2) from the segment p_i p_(i+1) and s2 =
distance of p_i from the segment p_(i+1) p_(i+2) (0x67D5F0, clamped to the segment ends); if min(s1, s2) < 0.01
(double), p_(i+1) = p_(i+2); otherwise for j = i + 2 .. min(i + 50, end - 1) - 1 the segment p_i p_(i+1) is tested
against p_j p_(j+1) (0x6545F0, determinant threshold 1e-4, bounding-box test with the same margin) and at the first
hit p_(i+1) .. p_j are all set to the crossing point. The offset ranges are the **fill** list.

**Step 9: the region fill (0x6BA6F0 > 0x425060, then 0x6414D0 per block).** The class grid is 7.3's classes on a
255-wide byte grid (cells outside the map read class 0). The block set-up lists a boundary or fill range in every
64 x 64 block that holds one of its points (points truncated toward zero, wrapped into the field by adding the field
size while negative and then the remainder; a range is listed once per block; at most 32 ranges of each kind per
block). For each plot cell, its hex's sample position is truncated to integers and the four points (+-1, +-1) are
added as seeds with the cell's class to the block each falls in (block index by division toward zero, no
lower-bound test; up to 16 per block). Then per block: 8.10 steps 2-5.

**Answer to 2.3's open point (acceptance (4)) [C for the mechanism; numbers in 8.10.2].** Every line's draw count is
a function of its own point count alone, and the lines draw in the fixed scan order of step 5. A plot change that
changes the point count of one line, or adds or removes a line, therefore shifts the jitter of **every line after
it in that order** (all later coasts move at a reload); a change that keeps every earlier line's count leaves the
later lines' jitter as it was. 2.3's single LW case (only 3-4 plots moved) is one where the changed line's count did
not change or it came late in the order; 6.7's wide re-rolls are the piece matcher shifting with the total count
(7.2, 7.6), which every count change also moves. **Consequence for the live path (lead-0615, 2026-11-20):** a
reload after a land/water change moves the jitter of later coasts too, so "live equals reload" cannot hold there;
live, only the lines that the change itself alters are redrawn (the others keep their load-time jitter), and the
lines a reload would move besides are named in the result, not drawn.

**Not verified (2026-11-20 16:10):** that 0x489CC0's origin transform is the inverse matrix (step 2); the y of the
two extra character-spline points (step 7); x87 against f32 rounding in the square roots and the double threshold;
that the coast styles of the installs we test equal the file read here (`TerrainModels.fpk` of c5h-d's install,
coaststyles.xml 5,477 bytes, the character files 2,161 and 2,137 bytes). *(2026-11-20 17:05: the port's check
against both taps is 8.10.2; the first point is borne out by it [C, offline check], the others stand.)*

#### 8.10.2 The port and its check against the taps (ticket 4910, 2026-11-20) [C for the code, C offline for the runs on recorded inputs]

- **Code** (ours, from 8.10.1): `render/include/c5r/liveterrain/coastlines.h`, `render/src/liveterrain/coastlines.cpp`
  (C++14, library `c5render`): `CoastStyles::parse` (coaststyles.xml and the character files, read at run time),
  `buildCoastCurves` (steps 1-8: the polylines in rand order with their point and draw counts, the five coast-mesh
  lines per polyline, CreateTerrain's point array and the boundary and fill ranges), `regionBlocks` / `regionField`
  (step 9 and 8.10: the region byte of any rectangle of 64 x 64 blocks). Tests:
  `render/tests/liveterrain/coastlines/coastlines_test.cpp` (synthetic island maps, wrapped and not, the draw count of
  every line against step 6's formula, a sub-rectangle equal to the whole field, another seed moving the curves, the
  install's coast styles, every input check with a negative test). Runner `lt_region_run` and the scripts
  `render/evidence/0615/4910/run.sh` and `view.py` (diagnostic picture of region against material 37).
  `render/build.py --test -R liveterrain` (2 of 2 suites passed, 0 skipped) and with `--asan` (canary reported, 0 ASan
  reports, peak commit 228 MB), 2026-11-20 17:15 EST.
- **Inputs**: 4902's taps and plot tables (7.7, prepared by 4903's `prep.py`), the coast styles of c5h-d's
  `TerrainModels.fpk` (coaststyles.xml SHA-256 ad0b9bba...203a76f), and mapRandomSeed from the saves' PreGame block
  (ticket 4912, W3, read offline 2026-11-20): Tiny 0x3DFC9816 (save `C5H-0301-4901-LT_0008`), huge 0x067F5502
  (`C5H-VP8P-HUGE_0350`).
- **The fingerprint.** Material 37 is written only by the coast, ocean and reef pieces (8.5), and those count only on
  samples whose region byte is not land; so on land hexes a 37 must be a wet region sample. A land material (not 37,
  not 3) on a wet region sample would be an error the other way. Material 3 (no piece won) forms a band of 2-6
  samples along every coastline on both sides of the boundary, so it says nothing either way and is left out.

  | Map (lines, all closed; jitter draws) | land samples | their 37 | 37 reproduced | coast-adjacent land hexes without a river: 37 / reproduced | land material called wet (all land) | coast-adjacent agreement (37 vs wet), port / per-hex class |
  |---|---|---|---|---|---|---|
  | Tiny (13; 4,736) | 2,138,112 | 67,354 | 50,867 (75.5 %) | 47,708 / **47,708 (100 %)** | **0** | 98.75 % / 93.76 % |
  | huge (53; 13,430) | 11,010,049 | 312,456 | 172,653 (55.3 %) | 148,700 / **148,700 (100 %)** | **9** | 98.69 % / 93.56 % |

  Every 37 that the port does not reproduce lies on a hex with a river edge or next to one: thin lines along the
  rivers (the river step writes 37 there after the blend [I: 0x412680 or the river mesh; not the region byte, which
  has no river input in this build]). The jitter is what makes the match: with seed 12345 or 0x3DFC9817 on Tiny the
  coast-adjacent agreement falls to 92.9 % and 93.1 % (below the per-hex class) and only 27-30 thousand 37 samples are
  reproduced; without the character offsets (step 7c) it is 98.60 % [C, offline on recorded inputs, 2026-11-20 16:50-17:05].
- **2.3's case, recomputed (acceptance (4)) [C, offline check].** Plot (53, 13) on Tiny from plains to coast (2.3's
  "land to water"): with the port, the same 13 lines in the same order with the same point counts (the changed
  line, line 3, keeps its 458 points) and the same 4,736 draws; only line 3 and its two offset lines move (1,219 of
  58,996 points, indices 11,018-44,817 of the array). That is why 2.3 saw only 3-4 plots along that coastline move.
  A change that alters a line's point count shifts every later line (8.10.1, the answer above).
  `lt_region_run --after` prints, for a second plot table, the lines whose count or start differs and the first
  such line.
- **Cost** (single runs of `lt_region_run` on the workstation, Release, an indication only, COMMON 23): 0.9 s for
  Tiny (curves, whole region field and the fingerprint over 9.2 million samples) and 3.3 s for huge (curves and
  field, no fingerprint).
- The 9 huge samples (a land material on a wet sample) lie in blocks (111-113, 43-44), around plot (112, 42),
  `FEATURE_GIBRALTAR`: a coast-class wonder whose piece (region 1, 7.3) writes its own non-37 materials on wet
  samples (3,986 such samples in those blocks, nearly all on its own and the neighbouring coast hexes) [I for the
  cause, C for the place, offline]. So no land material is called wet anywhere outside a wonder piece.
- **Open:** the kind bytes of a closed line's first point in
  step 7 (not written by the EXE; the port copies point n - 2's, which only affects the blend weights of the first
  two output points); heights at the coasts (they also depend on the piece choice, way 1, and on the coast strips,
  4911) are not compared here.

#### 8.10.3 Zero-length segments and points that are not finite (MC #9538 item 1, W10, 2026-11-21) [C for the code and the tests, C offline for the runs on recorded inputs]

**The fault (the MC's reviewer, 2026-11-21 15:53; plausible, never seen).** Each step of 8.10.1 that divides by a
length had no guard: step 4's mountain push, step 6's jitter move, step 7's offset and character passes and step 8's
offset lines all form 1 / |t|. Two equal neighbouring points give t = 0, so 1 / |t| is infinite and the new point is
NaN. In the region fill a NaN point truncates to INT_MIN (the x64 conversion of a NaN). Then the wrap loops of the
block lists run about 2^31 / field-width times, `a1 - a0` overflows in the line fill, and the strips' raster
(`Raster::triangle`) can walk about 2^31 rows on the producer's worker. The EXE's own code has the same arithmetic;
what it does with such a point is not described here.

**The fix (branch `w10-ff`).**
- **Each such division tests its result** (`std::isfinite` of the reciprocal) and, when it is not finite, the step
  leaves the point where it is:
  - step 4: no push;
  - step 6: no move, but the `rand()` draw is still made, so the rand order of every later line is unchanged;
  - step 7: the five lines take the point itself, and no character offset is added;
  - step 8: the zero-length segment's offset point is not made; the offset line goes on from the next segment.
- **Each case is counted:**
  - `CoastLine::degenerate` (steps 6 and 7);
  - `CoastCurves::degenerate` (the lines' cases plus step 8's);
  - `CoastCurves::degenerateSmooth` (step 4, in the sum too).
- A character spline file with such a point is refused (`BadLayout`).
- A finite input takes exactly the operations it took before, because only the result is tested.
- **Backstops in the rasterisers:**
  - `regionBlocks` refuses (`BadLayout`) a range point that is not finite or lies more than `kRegionPointReach` (16)
    field sizes outside the field, before anything is truncated;
  - `Raster::triangle` refuses a triangle with a vertex that is not finite, or a box wider or taller than the field,
    and counts it in `StripStats::trianglesRefused`.
- **What the server logs.** The producer hands both counts on (`ChangeResult::coastDegenerate`,
  `stripTrianglesRefused`; `ChangeOut` the same). The server logs one LOUD line per change that has any of them
  (`LiveTerrain::takeResult`), and one at the load when the load's curves have any (`Producer::loadCoastDegenerate`,
  the land/water enabler). A refusal by `regionBlocks` makes the change an error: the usual LOUD error line, and the
  change waits for the reload.

**Tests** (`render/tests/liveterrain/coastlines/coastlines_test.cpp` `zero_length_segments`,
`render/tests/liveterrain/coaststrips/coaststrips_test.cpp` `raster_non_finite_vertex`):
- A real island has 0 cases.
- A waterline point doubled in the middle, or at the end, of a line: 2 cases (one per offset line). Every point is
  finite and the region fill is as before (island land, far ocean ocean).
- A NaN, +inf, -inf or 1e9 point in a fill or boundary range is refused in well under 2 s.
- A point two field widths past the seam is not refused.
- In the strips, a NaN or infinite vertex refuses the 3 or more triangles that use it. The rest of the field equals
  the reference, and a far vertex is refused.

**The exact-port numbers did not move [C, offline, 2026-11-21 16:00-16:11 EST, workstation].**
- **Method.** `lt_region_run` (4910) and `lt_strips_run` (4911) were run on the recorded inputs, built once from the
  team branch's c1dc17d2a and once from this fix. The inputs were W1's Tiny and huge folders (`block.bin`,
  `plots.txt`, `m8.raw`, the extracted coast styles) and W2's strip inputs (`*_curves.bin`, `*_styles_w1.bin`,
  `h16.raw`). Seeds as 8.10.2.
- **Result.** Byte for byte the same in both builds: the curves, the region bytes, the strips' heights, materials
  and cover, and the result lines.
  - Tiny: 13 lines, 4,736 draws, coast-adjacent 37 reproduced 47,708 / 47,708. Strips: 157,184 triangles drawn,
    1,590,913 writes.
  - huge: 53 lines, 13,430 draws, 148,700 / 148,700. Strips: 448,000 drawn, 4,528,376 writes.
- **SHA-256 prefixes:** Tiny region 0093e7ddc863aa39, huge region 3fb5e1d9e83f5134, Tiny strips h16
  f21bb791ca74147a, huge strips h16 9eec7130151f25e1.
- Scripts and outputs: `C:/c5-64bit-work/0615/w10/cmp.sh`, `cmp/{base,new}/` (scratch, not committed).
- With the counters printed (the runners' new `degenerate` and `refused` keys, 16:11), both maps show 0 cases
  (`"degenerate": 0`, `"refused": 0`), and the outputs are still the baseline's byte for byte. So the fault has not
  occurred on any recorded input; the guard is for maps and changes not yet seen.
- `render/build.py --test -R liveterrain`: 4 of 4 suites passed (coastlines 7 run, 0 failed checks; coaststrips 16
  run, 0 failed), 2026-11-21 16:09. The whole render ctest is in the hand-back.
- **Not covered by a direct test:** steps 4, 6 and 7's guards. No public entry makes two equal points there; they use
  the same rule as step 8's, which is tested.

### 8.11 Live share before and after, and the pictures of the newly live cases (4908, 2026-11-19 01:20-01:45) [L, the runs named]

The same 403 seeded cases as 8.8 (seed 4908) and 4906's chain, run with `measure.sh`. The before row has 4903's rule;
the second row adds water and marsh kept (`--protect-rivers 0`); the third has all three kept (the defaults).

| Kind of change | Tiny before | Tiny + water, marsh | Tiny + rivers | huge before | huge + water, marsh | huge + rivers |
|---|---|---|---|---|---|---|
| hill raised | 0 / 30 | 2 / 30 | 30 / 30 | 0 / 30 | 9 / 30 | 30 / 30 |
| mountain raised | 1 / 30 | 10 / 30 | 29 / 30 | 0 / 30 | 7 / 30 | 30 / 30 |
| hill flattened | 0 / 30 | 5 / 30 | 28 / 30 | 0 / 30 | 5 / 30 | 30 / 30 |
| mountain flattened | 0 / 13 | 0 / 13 | 13 / 13 | 1 / 30 | 8 / 30 | 30 / 30 |
| natural wonder added | 1 / 30 | 8 / 30 | 29 / 30 | 0 / 30 | 6 / 30 | 30 / 30 |
| **all that draw** | **2 / 133 (2 %)** | **25 / 133 (19 %)** | **129 / 133 (97 %)** | **1 / 150 (1 %)** | **35 / 150 (23 %)** | **150 / 150 (100 %)** |
| feature removed or added | no change (60) | no change | no change | no change (60) | no change | no change |

- With all three kept, the four Tiny cases that still wait are windows that cross the x seam (`Seam`).
- No live case is invisible: every one changes heights (the runner's `visible` count is above 0 in all 279).
- 4906's chain on Tiny with the defaults: steps 1-8 are live (`fh_hills`, `fh_mountain`, `fh_hills_back`,
  `fh_flat`, `gd`, `nw_add`, `nw_remove`, `mw_add`).
  - Steps 9-11 are "no change": `mw_flat` keeps the wonder, whose bit replaces the cell's flags (0554 (a)); the ice
    feature is not a pattern input.
  - `lw` and `wl` wait with `Water` (a plot changes its land or water class).
  - `large` waits with `TooLarge`.
- **Pictures** (DIAGNOSTIC, offline relief renders of the heights; the install has Vibrant Textures):
  - Visual Checks **vc-20261119-014357-workstation-e6cb**: one newly live case per widened reason on the Tiny tap: a
    hill raised at (34,11) next to water, at (43,17) next to marsh, and at (48,9) next to a river.
  - `keptcheck.py` (every kept sample of the three classes compared with the tap): **0** changed in all three cases
    (7,104,282 water, 25,897 marsh and 380,753 river kept samples on Tiny).
  - regioncmp, tol 1, the changed plot / rings 1-4 / outside:

    | Case | inside | ring 1 | ring 2 | ring 3 | ring 4 | outside |
    |---|---|---|---|---|---|---|
    | water | 4,094 | 14,856 | 8,115 | 220 | 0 | 0 |
    | river | 2,648 | 12,466 | 3,442 | 186 | 0 | 0 |
    | marsh | 3,857 | 10,489 | 10,694 | 5,266 | 13,248 | 66,934 |

    The marsh case changes two 512 pieces, whose window reaches ring 8. With `--rings 8`, rings 5-8 hold 20,171 /
    25,608 / 16,197 / 4,958 samples and outside is **0**. The producer's own count of changed samples outside
    w > 0 is 0 in every case.
  - piccmp median / p95 / max: water 0 / 26 / 234, marsh 0 / 60 / 242, river 0 / 17 / 234. Change time 54-130 ms.
- **Not verified.**
  - The river radius (60 samples) is a first choice. The kept samples are bit-equal by construction, but whether
    the EXE's carve reaches farther than 60 samples from a river-edge plot's centre was not measured. The difference
    picture of the river case shows the 32-sample feather band next to the kept river samples, as designed.
  - How the feathered edges look in the game, which shows textures and lighting, not only this relief.

### 8.12 Trees and decals on the new ground (4904) [C for the code read, L for the offline runs named; 2026-11-19]

Scope for 0.1b (MC #6668): the server-drawn trees and decals at the new height after a live window. Ice and coast waves
come after the beta. The code was read on the team branch at 5e2973b1b and on w5-4907 at c689e18af. Branch w7-4904.

**What follows a live window without new code [C].**

- **Trees: the heights.** 4907's hook (`Scene::liveTerrainStep`) writes each window into `Scene::m_grid` in place
  (`ApplyWindow`) and then calls `applyField`. That calls `SceneForest::onField` with the same load number and the same
  hexToWorld. The forest's same-load branch (`srv_forest_scene.cpp`, `onField`) then calls `Placement::invalidateAll`,
  clears its built set and calls `hook(-1)`. Every plot's blocks are built again at its next visibility by `buildPlot`
  (`render/src/forest/placement.cpp`). There each tree's z is `heightAtSample` of the edited grid at the sample the
  build reads, and the water and slope culls run again. So the heights were right before 4904.
- **Trees: the cost of that path.** `hook(-1)` is `dropAllShadows`: every Tree_Shadow decal of the map is dropped
  and becomes a pending decal change, and the area marks of every shadowed tile are added (stock's tile+0x300 bit,
  kept to the next load). A decal is made again only when its plot is next visible. So each live change costs a
  whole-map shadow re-bake, and the off-screen shadows are missing until those plots are seen again.
- **Decals.** These are the EXE's tapped roads, farm fields, improvement grounds and river bed, the server's farm
  fields (epic 0528) and the Tree_Shadow decals (4552). **None carries a height**: a rectangle and half2 positions in
  the field's sample frame (`c5r/decal/decal.h`), drawn into the tile bake's blend targets
  (`terrain-decals.md` 4.1-4.2). At every `onField` the residency hashes each 64-sample tile's heights and materials
  over a 3-sample apron (`render/src/bakeresidency/residency.cpp`, `Residency::onField`). It gives each changed tile a
  new generation and bakes it again with the decal set in force, so each decal on a changed tile lies on the new
  ground. The farm fields' layout reads no heights (`srv_farmfields.cpp`, `render/src/farmfields/`). **Nothing to
  build for the decals.** What changes with the ground is only which trees are drawn, so which shadow decals exist;
  that follows the trees.

**What 4904 adds: the window-local path [C].**

- `server/src/render/srv_liveterrain_derived.h/.cpp`:
  - `DerivedAfterWindow(forest, boxes)`: the entry point for 4907. The hook calls it after `ApplyWindow` and before
    `applyField`, with the inclusive sample rectangles it wrote (from `ApplyStats`, whose x1 and y1 are exclusive:
    W5 passes `x1 - 1`, `y1 - 1`).
  - `BlocksReadBoxes`: does any tree of a plot, drawn or culled, read a sample inside a box widened by
    `kTreeReadMargin` = 3 samples? That margin is 2 for the slope rule's 5 x 5 stencil plus 1 because the build
    rounds the tree's sample position in two ways. The test uses the build's own expression `(ox + x) / sx`,
    truncated; x wraps when the map wraps.
  - `ReheightBuilt`: builds again only the built plots that read a box (or that were built with another cut
    generation) and reports each through a callback.
- **Forest hunk** (`server/src/forest/srv_forest_scene.h/.cpp`, epic 0431's files, about 40 lines, allowed by the
  lead #6719 and named in the hand-back):
  - `SceneForest::expectWindows`: non-pure, no-op by default.
  - `ForestScene::m_liveBoxes`: the boxes pending since the last `onField`.
  - `ForestScene::reheightWindows`: the rebuilt plots go through `hook(x, y, rec, b)`, so `addShadow` keeps a decal
    whose content did not change.
  - The same-load branch of `onField` takes `reheightWindows` when boxes are pending. Every `onField` consumes the
    boxes, whichever path it takes.
  - The whole-map path stays as the fallback in two cases. When no boxes are pending (a device rebuild, a re-applied
    tap). And when any rebuild fails: then every block is built again, so no tree keeps a stale height (D118: never a
    half-updated picture). `buildPlot`'s refusals are structural (layout, params, a value that is not finite) and do
    not depend on the heights, so the fallback is not expected to run.
- **Plots not yet built** are not touched: they are built from the edited field at their first visibility, as before.

**Offline proof [L, 2026-11-19 02:05-02:08, workstation, single runs].** `server/tests/render/liveterrain/derived/`
(`test_derived_build.py`, `test_derived.exe`, `test_derived.py`).

- Inputs:
  - 4903's recorded inputs (`{tiny,huge}`: block.bin, plots.txt, h16.raw). *Corrected 2026-11-20 18:25 by w4-4915
    (ticket 4915): 4903's folder `C:/c5-64bit-work/0465/4903` was removed by the disk clean-up of 2026-11-20 12:30, so
    the test's cases frame, tiny_fh and huge_chain failed. The inputs are made again offline by
    `server/tests/render/liveterrain/derived/make_inputs.sh` (4903's `prep.py` on 4902's recordings in
    `F:/c5-64bit-work/0465/4902/`, then `lt_producer_run` with 4903's rule: all three protections of 8.9 off) into the
    kept folder `C:/c5-64bit-work/keep/liveterrain-derived-4903/` (master copy on F: at the same path; README.txt with
    the commands and the recordings' SHA-256, SHA256.txt), which `test_derived.cpp` now reads by default
    (`C5_4904_DATA` still overrides). Check [L offline, 18:15-18:20]: the huge windows' RegionSha256 equal 4907's record
    of 4903's files (`hookrun_huge.jsonl`), and every per-step number of the table below comes out the same; all four
    cases PASS, pytest 2 passed. With 4908's protections on (the runner's default) the Tiny chain differs (step 1: 818
    stale trees, max |dz| 14.69), so the rule matters. Replaced: "`C:/c5-64bit-work/0465/4903/{tiny,huge}`".*
  - Its chained producer fields and windows: `fh/` is the Tiny FH (50, 16) chain flat->mountain->flat->hills->flat,
    with the clean rule ignored (DIAGNOSTIC, 8.5); `hugechain/` is huge hills->flat at (89, 56), then flat->mountain at
    (90, 56), both live.
  - The install's forests.xml and masks (`C5R_GAME_DIR`, read-only).
  - Placement seed 1234; forest id 5, jungle id 1.
- Method:
  - Every forest and jungle plot gets its record, and every record is built on the field before the step (the worst
    case: the whole map seen).
  - The window path runs against a copy of the placement on which everything was built again on the field after (the
    forest's whole-map path).
- The frame check: the grid constants block's frame (hexToWorld +0x074, waterZ +0x2B8, sx..oy +0x35C..+0x36C, wrap
  +0x000) equals the tap header of 4906's PT01 `load0003.c5hf`.
- Each producer step changed samples only inside its window: 0 outside, on all six steps.

| Step | Built plots | Rebuilt | Drawn trees on stale ground without the re-height | Max stale \|dz\| (world) | Culls changed | Plots differing from the whole-map rebuild | Untouched plots changed | Window path ms (median of 7) | Whole-map tree rebuild ms (median of 7) |
|---|---|---|---|---|---|---|---|---|---|
| Tiny 1 flat->mountain | 180 | 16 | 1,011 | 5.32 | 208 | **0** | **0** | 1.41 | 11.95 |
| Tiny 2 mountain->flat | 180 | 16 | 1,015 | 5.32 | 121 | **0** | **0** | 0.71 | 6.31 |
| Tiny 3 flat->hills | 180 | 16 | 1,030 | 5.32 | 49 | **0** | **0** | 0.75 | 6.07 |
| Tiny 4 hills->flat | 180 | 16 | 993 | 5.32 | 42 | **0** | **0** | 0.71 | 5.92 |
| huge 1 hills->flat | 690 | 6 | 375 | 9.67 | 61 | **0** | **0** | 1.21 | 47.91 |
| huge 2 flat->mountain | 690 | 5 | 261 | 4.65 | 45 | **0** | **0** | 0.72 | 44.46 |

- After the re-height, **every drawn tree's z equals the new field at the sample the build reads** (|dz| = 0; the
  renderer's own placement takes the sample as it is, so the tolerance is exact). Every plot's instances are
  bit-equal to the whole-map rebuild, and the plots the window did not touch are bit-equal to before.
- "Culls changed": trees that the water or slope rule now hides or shows, all inside the rebuilt plots.
- The unit cases:
  - an empty box list;
  - a box far from every tree;
  - a one-sample box over one tree (1 of 180 plots touched);
  - the same box one map width away (the x wrap);
  - the margin edge;
  - a listed plot without a record (counted as failed, with the error text);
  - `DerivedAfterWindow` without a forest.
- **Cost (acceptance (4)).** Single runs on the workstation, an indication only (COMMON 23, PERFORMANCE.md; no timing
  series).
  - The window path costs 0.7-1.4 ms per change on both maps.
  - The whole-map path's tree rebuild costs 6-12 ms on Tiny and 44-48 ms on huge, and this is the worst case, with
    every plot built. In the product the whole-map path builds lazily, so that cost spreads over the next frames.
  - The larger cost of the whole-map path is the shadows: it drops every Tree_Shadow decal of the map (180 and 690
    plots here) and sends them all to the residency as decal changes. The window path changes the decals of the
    5-16 rebuilt plots, and only those whose content changed. The re-bake time of the shadowed tiles was not measured.

- **The forest scene with the hunk (MC #6736)** [L, 2026-11-19 02:20].
  - A new case `live_window` in epic 0431's `server/tests/render/forest/test_forest_scene.cpp` (WARP, the synthetic
    8 x 10 grid, the real ForestShader and install files). After a frame (32 built plots), a 9 x 9-sample box around
    one tree of plot (2, 0) is raised by 5000 u16. Then `DerivedAfterWindow` and the same-load `onField` run.
  - Results:
    - the hook never sees x = -1 (no whole-map drop);
    - 2 of 32 built plots are built again, and the other 30 keep their blocks;
    - the tile marks are unchanged;
    - the decal change holds 2 rectangles, at most twice the rebuilt plots and never `all`;
    - the touched trees stand on the raised ground;
    - the same `onField` without boxes still drops every block and decal (the fallback path).
  - The whole suite (20 cases, `test_forest_scene.py`) passed: 3 passed in 51.5 s.
- **Pictures** (DIAGNOSTIC, D112: offline, heights only, no textures and no renderer;
  `render/evidence/0465/4904/pictures.sh`). Each shows a cross-section and the window from above with the old z and
  the new z minus the new ground. In the top views blue means a tree floats and red means it is buried. In these
  pictures the stale counts include trees that the culls hide after the change, so they are slightly above the
  table's.
  - **vc-20261119-022239-workstation-7392**: Tiny FH flat to hills, a forested neighbourhood raised. 1,069 trees on
    stale ground, up to 5.97 off.
  - **vc-20261119-022241-workstation-87cc**: Tiny FH hills to flat (flattened). 1,003 trees, up to 5.32.
  - **vc-20261119-022242-workstation-ec17**: huge (89, 56) hills to flat (live). 384 trees, up to 9.67.
  - After the re-height, max |dz| is 0.000 in all three.

**The boxes are the window's rectangles (MC #9538 item 3, W10, 2026-11-21) [C for the code; C offline for the
count].**
- **Before.** The hook handed the forest one box per change: the change's `ApplyStats` box, the bounding box of its
  rectangles. A land/water window that crosses the x seam is written as two rectangles, one at each end of the field
  (producer.cpp, 4913's split). Their bounding box is the whole map width, so `ReheightBuilt` rebuilt every built tree
  plot in those rows.
- **Now.** The live apply (`Scene::liveTerrainFinish`) and the offline player (`Scene::liveTerrainReplayTo`) hand over
  one box per written rectangle (`SampleBoxOfRect`).
- **The picture is unchanged.** The rectangles are exactly what was written, and the bounding box only added plots
  whose trees read no written sample: their rebuilt blocks equal the old ones.
- **Count** (test_derived `units`, Tiny, a window of 2 x 40 x 128 samples across the seam): 4 of 180 built plots
  rebuilt from the rectangles, against 68 from their bounding box; 0.3 ms against 2.2 ms. That is one run on a busy
  workstation, an indication only (COMMON 23). On huge the bounding box would cover more plots; not measured.

**Not verified / open.**

- `ForestScene::reheightWindows` and the `onField` branch have run only offline: the `live_window` case on WARP. The
  product path is first run by 4907's live run (D198 video), which carries this hunk (E7).
- The residency's tile re-bake of the decals after a live window is shown from the code, not from a run. 4907's live
  run shows it.
- The shadows' re-bake cost was not measured, on either path.

### 8.13 The EXE field write (4905) [C for the code and the offline runs, L where marked]

The server writes a live window into the EXE's own field, so that the EXE's on-demand readers (Lua `HexToWorld`, and
through it flags, banners and Strategic View picking; a unit at its next move; 2.4) agree with the picture. Code:
`server/src/render/srv_liveterrain_exefield.{h,cpp}` (w6-4905), tests `server/tests/render/liveterrain/exe_field/`.

**Interface (one entry point for 4907).** `OpenExeFieldTarget(pid)` once per load: opens the EXE with VM read, write,
operation and query rights, takes the main module's base (`K32EnumProcessModulesEx`, 32-bit modules), requires the PE
TimeDateStamp 0x546CD0A8, and requires `[base + 0x990FC4] == base + 0x991270` (the EXE's pointer to its
GridHeightmap). (Build 4390913's values. Since 2026-11-20, ticket 4966: the build comes from the stamp and SizeOfImage,
and the addresses from the EXE build table: `cam_gridptr`, `cam_grid`, `cam_wrapx`, `ExeFieldAddressesFor`.
Every `lt_*` field of the grid constants block must sit at its build-4390913 offset. An unknown build is refused.
`research/02-exe-internals/exe-build-table.md` section 13.) `WriteExeFieldWindow(target, before, window)` per live result, where `before` is the server's field
before the window and `window` is 8.1's rectangle list (any type with x0, y0, w, h, heights, materials). It returns
`Written`, `Refused` (nothing written), `RolledBack` (written in part, then restored and read back equal) or
`RollbackFailed` (LOUD: the EXE's field is mixed until the reload). Only `Written` lets the server apply and draw the
window. Any other result means the change waits for the reload. The server's picture and the EXE's field therefore
never disagree (D118). 4907 calls it **before** `ApplyWindow`. This order was agreed with W5 on 2026-11-19 01:10. The
brief had said "after". The read-back check needs the pre-window field, and a failure must keep the server from
drawing the change.

**The EXE runs relocated.** Every EXE address is rebased: VA - 0x400000 + module base. Measured bases were 0x00740000,
0x00F60000 and 0x00EB0000 (`02-exe-internals/camera-and-picking.md`, `disable-systems-and-masks.md`,
`terrain-bake-verification.md`). 4907's first `ReadConstantsBlock` read 0xD90FBC unrebased. This was reported to W5 on
2026-11-19 01:06. W5 answered that it is fixing the read with the module base; check that fix on w5-4907. `ExeFieldTarget::exeBase` carries the base for that read.

**Checks of one call, in order** (each failure is a `Refused` with one LOUD line naming the check):

1. The target is set. The server's field is a whole number of tiles, with both heights and materials present. Every
   rectangle lies inside the field and has w*h heights and w*h materials. The window has at most 64 M samples.
2. The object at `gridAddr` (0xC8 bytes) passes `c5r::heightfield::parseGridObject`: ready, sizes, tile counts. For both
   layers the directory and pool pointers are non-null, the pool size is at most its capacity, and the directory and
   pool end below 4 GB. When `exeBase` is set, `[0xD90FC4]` still points at the grid. The EXE's field must be exactly
   the server's size (another load is refused).
3. Every touched tile's directory entry, in both layers, is read: one read covers the touched tile rows. The entry must
   be pooled (state 0), since a constant tile is never written. Its pool index must lie inside the pool.
4. Every touched run of pool slots is checked before any pool read. The runs are the reads, and the writes are a
   subset of them. Each run must lie inside its layer's pool, `[ptrHigh + pool, + poolSize * tileBytes)`. It must lie
   outside the EXE's image, `[exeBase, exeBase + SizeOfImage)`; `OpenExeFieldTarget` reads SizeOfImage, and an unknown
   size (0) refuses. *Added 2026-11-19 02:25 for MC #6744 (c): the containment held by construction before and is now
   checked.* Then VirtualQueryEx checks every region the run covers. Each region must be `MEM_COMMIT`, `MEM_PRIVATE`,
   protect exactly `PAGE_READWRITE`. That rules out guard pages (the modifier bit makes the value 0x104), write-copy,
   read-only, executable, image pages and mapped views. Because this runs before the reads, a guard page is never
   touched. *Was (until 02:25): checked after the read-back, right before the first write.*
5. Every touched tile is read back from both pools, as runs of slots that follow each other. Whole tiles are compared
   with `before`, so samples outside the window but inside a touched tile also count. Any difference refuses the
   write: the EXE's field is not the one the window was made for.
6. Before each write, the object is read again and its key fields are compared: ready byte, sizes, both layers'
   pointers, pool sizes and tile counts. The write runs pass the containment rule once more; their regions are
   already checked in this call.

**Writes.** Only tiles with a changed sample are written, one call per run of pool slots that follow each other, per
layer. The writes go through `NtWriteVirtualMemory`, which never changes a page's protection. On a read-only page it
fails with STATUS_PARTIAL_COPY 0x8000000D and leaves the page read-only [C, test `rollback`, 2026-11-19]. Kernel32's
`WriteProcessMemory` instead makes a non-writable page writable for its write [I, its documented debugger use; see
8.13.3]. A page that changes between check 4 and the write therefore makes the write fail; it can never be written.
The module calls no `VirtualProtect(Ex)`, no `WriteProcessMemory`, and no `VirtualAllocEx` or `VirtualFreeEx`. A test
scans the source with comments removed (`test_exefield.py`, MC #6744 (b)). After the writes, every written run is read
back and the object is read again. A failed write or read-back restores the runs written so far from step 5's copies;
each restore is region-checked again and read back. When the object
changed (a load began), nothing is restored, because the pools may belong to another load.

**Deviation from the acceptance text, accepted (MC #6744/#6745, 2026-11-19, on four conditions).** Acceptance (1)
asks for a VirtualQueryEx check "before each write". Here each region is queried once per call, before the first pool
read. One query costs time linear in the region's size beyond the queried address. A query at the start of a 127 MB
committed region took 0.84 ms, and 0.44 ms in a second run. At its last page it took under 0.001 ms [C, test `cost`,
2026-11-19 01:20 and 01:30, the mock's single VirtualAlloc region; workstation not quiet, an indication only]. With a
query before each of the 66 writes, an 8 x 8 plot window took 38-135 ms on the huge-sized mock; with one query per
region it takes 4 ms. Live on Tiny the whole call took 0.49 ms (8.13.1).

Under D165, MC #6744 counts this as a heap data write because of four conditions, each in the code and each tested:

- (a) Only PAGE_READWRITE is accepted, exactly, so execute, read-only, write-copy and guard pages all refuse with the
  LOUD line. Tests: `refusals` (read-only, PAGE_EXECUTE_READWRITE, and a guard page, after which the guard is still
  set), `mapped_view` (read-write view, write-copy view), `image_page`.
- (b) The module never calls VirtualProtect(Ex) or WriteProcessMemory. The writes go only through
  `NtWriteVirtualMemory`, which never makes a page writable. Test: the source scan in `test_exefield.py`.
- (c) Every target lies inside its pool's allocation and outside the EXE's image. Tests: `run_inside` (below the pool,
  past it, an empty run, a wrapping run, in the image, over the image's end, image unknown), `image_page` (refused by
  the image rule, and by the region type when the image range is wrong), and the `refusals` case "EXE image size
  unknown".
- (d) Every call queries again; nothing is kept between calls. Test: `requery` makes two calls on one target, flips a
  region to read-only between them, and the second call refuses.

**Offline tests** (`test_exefield.exe`, 11 cases, 193 checks, plain clang-cl x64 /W3 /WX; 2026-11-19 02:25; was 9 cases
and 164 checks at 01:30). The mock
GridHeightmap is built with `heightfield::buildMemoryImage`. It lives in the test process inside the 4 GB window of the
test program's image, with `ExeFieldTarget::ptrHigh` supplying the upper half of its 32-bit pointers. The writer reaches
it through the same Read/NtWrite/VirtualQueryEx calls on `GetCurrentProcess()`. The cases:

| Case | What it shows |
|---|---|
| `write_readback` | Two rectangles are written into a plain mock and into a scrambled one (pool slots in reverse tile order). The mock then equals before plus window, bit for bit, everywhere. The changed counts are exact. The same window again writes nothing. The old `before` against the new field is refused. |
| `wrap_split` | A window across the x seam arrives as [SX-26, SX) and [0, 20). Both parts are written, and the samples next to them are unchanged. |
| `nothing_to_do` | An empty window makes no write and no region check. A window equal to the field makes no write. |
| `refusals` | 20 refusals, each with nothing written and the pools unchanged: no target; not ready; unreadable object; another size; no materials; rectangle outside the field; negative x from a bad cast; short arrays; a constant tile in either layer; pool index past the pool; pool size above capacity; null pool; pool past 4 GB; the EXE differs in a height, and in a material outside the rectangle but inside a touched tile; a read-only page; a PAGE_EXECUTE_READWRITE page; `[0xD90FC4]` not pointing at the grid; a guard page (PAGE_READWRITE \| PAGE_GUARD, protect 0x104; the guard is still set afterwards, so nothing read it); the EXE's image size unknown. |
| `mapped_view` | The u8 pool in a MEM_MAPPED view is refused: read-write by its type (0x40000), write-copy (FILE_MAP_COPY, protect 0x8). |
| `image_page` | The u16 pool on a page of the test program's image (the stand-in EXE image) is refused by the image range rule. With a wrong image range it is refused by its type (0x1000000, protect 0x20). The image is unchanged. |
| `rollback` | A page goes read-only after check 5: the write fails and the earlier write is restored (RolledBack), and the page stays read-only. With the earlier page read-only as well, the result is RollbackFailed (LOUD). An object change before the first write gives Refused with no write. An object change after one write gives RollbackFailed with no restore. |
| `open_target` | Pid 0 is refused. The test program itself (64-bit, no 32-bit main module) is refused. |
| `run_inside` | The containment rule alone (MC #6744 (c)): runs at the pool's start and end pass. These are refused: below the pool, past it, an empty run, a wrapping run, inside the image, over the image's end, the image unknown. |
| `requery` | MC #6744 (d): two calls on one target. A page flipped to PAGE_READONLY between them makes the second call refuse, with nothing written. |
| `cost` | A huge-sized mock (128 x 84 grid plots, 8192 x 5376 samples). An 8 x 8 plot window (1,089 tiles, 66 writes): median 4.0 ms over 7 calls (3.8-4.9). A 16 x 16 plot window (4,225 tiles, 130 writes): median 13.8 ms (11.9-23.8). The mock is a single region, so 1 region check per call. In the EXE the pools are separate regions [I]. Not a performance claim (PERFORMANCE.md): a single series on a busy workstation. |

ASan (COMMON 9): `test_exefield_build.py --asan` (MSVC cl /fsanitize=address) and the pytest wrapper
`test_exefield.py` with `C5_EXEFIELD_ASAN=1` passed 3 of 3, with no ASan report [C, 2026-11-19 01:33]. Rerun after
MC #6744 (11 cases, plus the source scan): plain and ASan pytest 5 of 5 each, no ASan report [C, 2026-11-19 02:27].

#### 8.13.1 The live check, acceptance (3) [setup; results below when run]

Setup: instance **c5h-a** (Goldberg, C:), claimed 2026-11-19 01:48 under epic 0465 for 4905 (MC-B grant 01:41,
#6705; child tickets are not on the queue, so the claim names the epic, MC-B 01:44). Stock route as in 2.0. The pin DLL
is the one `provision.py set-dll` holds (COMMON 18). The mod set is mods548 with Vibrant Textures. The save is
`C5H-0301-4905-LT_0008`, a byte copy of `C5H-0301-4906-LT_0008`, SHA-256 b2ceb663...c29163 (2.0's save). There is no
server process: the write runs through the product writer, built into `exefield_live.exe` (TEST ONLY).

```
W=C:/c5-64bit-work/0465/4905/live ; BIN=C:/c5-64bit-work/0465/4905/ef-tests/bin   # test_exefield_build.py --out
bash render/evidence/0465/4905/live.sh c5h-a $W $BIN
#  runner.py prepare (backup by SHA-256 first, pin, save copy, front end) > launch > in game > dialog check > centres.lua
#  > drive.py field (load.npz) > live.py hz (HexToWorld of (53,15) (51,17) (50,16) (55,20)) > picture
#  > exefield_live raise --plots 53,15 51,17 --dz 30 --radius 30 (OpenExeFieldTarget, WriteExeFieldWindow, its own
#    dumps before.c5hf / after.c5hf compared in and outside the window) > hz again > drive.py field (after.npz)
#  > picture > live.py move --to 53,15 (the first land unit, SetXY) > picture > back
#  > exefield_live restore (the inverse window through the same entry point; whole field = before.c5hf) > hz > restored.npz
# close, by hand, also after a failure:
python -I tools/harness/cli.py -i c5h-a stop --settle 10 --human-turn-hook tools.harness.turns:stop_readiness
python -I tools/scenario/runner.py restore --instance c5h-a --work $W/stock   # front end restored, backup restored and
                                                                             # verified against the pre-prepare manifest
python -I tools/harness/pool.py release c5h-a ...
```

Expected: `OpenExeFieldTarget` finds the relocated base, the stamp and `[0xD90FC4]`. The write returns `Written`. Inside
the window the field equals the window and outside it nothing changes, in the writer's own dump and in `drive.py field`
(an independent reader). HexToWorld z at (53,15) and (51,17) rises by 30.00 at once. (50,16) and (55,20) lie outside
the discs and stay unchanged. The drawn terrain does not change (2.4). The moved unit stands about 30 units above the
drawn ground. After the restore the whole field equals the load's, and so does HexToWorld. Idle units, cities, trees
and resource art do not follow; they wait for their own refresh or for increment 3 (0561) (2.4).

**Results [L, 2026-11-19 01:49-01:56, c5h-a, the setup above; `live.log` and the dumps in
`C:/c5-64bit-work/0465/4905/live/`].** Every step exited 0 and no box appeared (the dialog marker was checked four
times).

- `OpenExeFieldTarget`: the EXE ran at base **0x00510000** (another relocation, after 0x740000, 0xF60000 and
  0xEB0000). The grid was at 0x00EA1270 = base + 0x991270. The stamp and `[0xD90FC4]` were as required. Field 3584 x 2560
  samples (56 x 40 grid plots: Tiny 56 x 36 + 4), wrap 1.
- The write: 2 rectangles of 64 x 73 samples (9,344 samples), 6,514 heights and 3 materials changed, 48 tiles,
  13 writes, **2 region checks** (the u16 and u8 pools are separate regions), **0.49 ms** (prepare 0.27, write 0.19).
  The centre u16 went from 15300 to 53550. `Written`.
- The writer's own compare: 0 differ inside the window, 0 outside. Independent reader (`drive.py field`, load.npz vs
  after.npz): heights differ in 6,514 samples and materials in 3. All lie inside the two rectangles (box x 3298-3485,
  y 1054-1250), none outside.
- Lua `HexToWorld` z, read at once after the write: (53,15) 12.00 > 42.00, (51,17) 12.00 > 42.00; (50,16) 12.0157 and
  (55,20) 7.6596 unchanged.
- Unit: the active player's first land unit (UNIT_WARRIOR id 1002, from (50,13)) was moved by `SetXY` onto (53,15).
  Picture vc-20261119-015530-workstation-8dee (DIAGNOSTIC): the figures stand above the wheat, with their shadows
  slightly offset on the drawn ground, as in 2.4. The drawn terrain at (53,15) does not change after the write (picture
  vc-...-4627, stock route: the EXE's terrain tiles do not follow). The unit was moved back afterwards.
- The restore, by the inverse window through `WriteExeFieldWindow`: `Written` (6,514 heights and 3 materials, 0.43 ms).
  The whole field then equalled before.c5hf (0 samples differ); load.npz vs restored.npz: 0 in both layers. HexToWorld
  read back 12.00 at both plots.
- Not shown live: the product route with the server's own hook (4907). The writer ran in a test process, the same code
  the server links. Idle units, cities, trees and resource art were not re-checked; 2.4 stands for them.

#### 8.13.2 P7 (acceptance 4): the reload finding and the design for after the release

*2026-11-24: (b) is built by ticket 0613 as `[Extras] FixUnknownWonderBase`, option id 14, default 1 (MC #13471); see
8.13.5. The text below is the design it was built from.*

Decision (lead-0465 with MC #6718, 2026-11-19 02:00, on #6715): option (c). P7 waits until after the release and then
comes as (b). No live-only inconsistency ships. 4905's part of 0.1b is acceptance (1)-(3).

- **The finding [C from P1, P7 and 8.1].** The producer could give a natural wonder unknown to the EXE a neutral base.
  That means the plot's own terrain cell value, which Fountain of Youth and El Dorado keep, instead of Fuji's 0x40000
  (`heightfield::patternCellFlags`, the table of 0x60ABB0). It would do so only for what it draws live. At the next
  load the EXE bakes the field again with its own Fuji fallback, the server taps that field (P1), and the plot shows
  Fuji again. Live would be neutral and the reload Fuji, which is a visible inconsistency.
- **(a) Live only** (rejected): the cell-flag rule behind [Extras] option 14 in the producer's `patternCellFlags` call
  (render/src/liveterrain/producer.cpp, the cell grid build). It is used in `create` and `change` alike, so a wonder
  present at load produces no window of its own. The inconsistency above remains.
- **(b) Live plus a load-time pass** (to build after the release): (a), and in 4907's `onField`, right after
  `Producer::create`, one producer call that re-draws every unknown-wonder plot of the load with the neutral base,
  as if the wonder had just been placed. That is a window through the same path: the producer, this file's
  `WriteExeFieldWindow` before `ApplyWindow`, then the scene's re-apply. The EXE's field, the server's field and the
  picture then agree at every load. It costs one extra producer call per load (8.6: 50-234 ms on Tiny, off the render
  thread) and touches 4907's hook. Option id 14 (MC #6298) and the key name are reserved for it. The default is the owner's
  at that merge, with A/B pictures (DIAGNOSTIC, D112).
- A data write before the EXE's own bake (cell flags into the HexPatternCell grid) would also make the reload agree.
  It was not looked into: it is an EXE-side timing question (D132 R2), and (b) needs no EXE knowledge beyond this
  section.

#### 8.13.3 Open (2026-11-19 01:35)

- The live check (3) ran on the stock route through a test process (8.13.1). The write from the server's own hook
  (4907) on the product route has not been run live.
- Kernel32 `WriteProcessMemory` making a read-only page writable is [I], not measured here.
- The EXE's pools: on the Tiny load the u16 and the u8 pool were one region each (2 region checks per call) [L, 8.13.1].
  How they are allocated has not been looked at.

#### 8.13.4 Land/water windows (ticket 4914 acceptance (3), epic 0615; w5-0615, 2026-11-20 19:11 EST)

Question: does the writer cover land/water windows (4913, 8.17)? That means unit placement and picking on the new land
and the new water, and which of the EXE's fields it writes. **Answer: yes. No code change is needed and no gap is left
in the writer** [C for the code and the offline runs named, L where marked].

- **What it writes [C, the code above].** It writes the u16 heights and the u8 materials of the touched tiles, and
  nothing else. The EXE's GridHeightmap has only these two layers (P4). There is no per-sample land/water class and no
  water flag in it.
- **How the EXE tells water.** Water is a height below one constant, the water height 10.1167 world units
  (`[grid+4]` 0xD91274, set by CreateTerrain, `water-system.md`). The path floor (+0x5E90) is the same value as a
  stored constant (`path-models.md` 7.1).
  - A sample lies under water when `u16 / 255 * 0.2 < 10.116731`, that is u16 <= 12,898.
  - The plot's land/water class itself is the DLL's (`CvPlot` plot type), which the DLL has already changed when the
    window is made.
  - So a window whose heights carry the new coast gives every EXE reader the new land and water.
- **The windows carry it [C offline, on 4906's Tiny chain (8.17's runs; W1's windows in `C:/c5-64bit-work/0615/w1/4906/k12..k14`)].**
  The under-water state (u16 <= 12,898) of the live window agrees with the stock reload as follows:

  | Case | window samples agree, live / tap | samples the reload moved across the line | of those, live follows |
  |---|---|---|---|
  | lw | 99.85 % / 95.96 % | 9,243 | 96.60 % |
  | wl | 98.44 % / 91.80 % | 10,337 | 81.29 % (the river keep of (a)) |
  | large | 100.00 % / 93.52 % | 24,551 | 99.96 % |

  Material 37 and the under-water state agree on 99.76 % of the material-37 samples of the whole field. This holds
  alike in the tap, the live fields and the reload.
- **The readers, on new land and new water:**
  - *Mouse picking (normal view):* it intersects a flat plane and reads no field (P3). The picked plot is the DLL's
    plot, so nothing is needed [C].
  - *Strategic View picking, Lua `HexToWorld` (EUI flags and banners):* these read the field at the plot centre at
    once (2.4 [L]). `HexToWorld` has no water clamp (`camera-and-picking.md`, hex to world). On new water it gives the
    new sea-floor height, as it does for a load's water plot. On new land it gives the new ground [C].
  - *Units in the product (ServerUnits):* the server draws them at `z = max(heightAtWorld, waterLevel) + ZOffset` from
    the server's own field (`render/src/units/placement.cpp`), which `ApplyWindow` has already updated.
    - A ship on new water floats at the water level [C code].
    - A land unit on new land stands on the new ground. Units following a live window were seen in 9.9 ("the units
      ride up with it") [L for a land window].
    - The EXE's field is not read for these.
  - *Units the EXE still places (ServerUnits off, the stock route):* the chain 0x42FA80 > 0x530A10 takes the field's
    height at the unit's next move; an idle unit does not follow (2.4 [L]). On new water the EXE's own water rule for
    ships applies, as for a load's water plot [I: not run on a class change].
  - *Path markers:* drawn by the server from the server's field with the constant floor (`path-models.md`) [C].
- **Size and shape [C, the code].** A land/water window is at most 256 plots (1 M samples), far below the writer's
  limit of 64 M samples. A window split at the seam (8.17) arrives as two rectangles, the case the test `wrap_split`
  covers. Huge's Gibraltar window is 347 x 373 samples. The writer's cost test wrote a 16 x 16-plot window in a median
  13.8 ms, an indication only.
- **Not covered by the field write, named elsewhere:**
  - the river water mesh, never ported (MC #8420 (d), 8.17 (a));
  - the coast waves and the ice, through the 0586 gate (10.9);
  - the EXE's terrain drawing, off under D33.
- **Not verified:**
  - a land/water window written live through the server's hook into the EXE (4915's live session);
  - Strategic View picking by mouse on a changed plot;
  - ServerUnits off with a ship on new water.

#### 8.13.5 P7 as built: option (b), [Extras] FixUnknownWonderBase (ticket 0613, 2026-11-24) [C for the code and the tests, L where marked]

*Updates 8.13.2: option (b) is built; the "(b) later" there is this section.* Branch `task-0613-live-terrain-load-pass`.

**The option.** `[Extras] FixUnknownWonderBase`, option id 14 (reserved for 0465 under K4), a bugfix: 1 = on, the default
(MC #13471, D169: bugfix keys are on by default; the owner is told at the merge). In effect only with `EnableBugfixes = 1`
(`Vp64Opt::Extra`); it acts only where the server draws the terrain (`ServerRenderer = 1`). Player text: docs/OPTIONS.md.
The server reads it once per process (`srv_liveterrain_scene.cpp`, `LiveNeutralWonders`, one log line
`live terrain: FixUnknownWonderBase: in effect (...)`).

**The rule [C] (`render/src/liveterrain/producer.cpp`, `unknownToExe`, `cellFlags`).** A plot's natural wonder is unknown
to the EXE when the plot is land (terrain not coast 5 or ocean 6), its featureType is named and is none of
FEATURE_FUJI, FEATURE_FOUNTAIN_YOUTH and FEATURE_EL_DORADO, and the EXE's rule (`heightfield::patternCellFlags`, P7) gives
it Fuji's 0x40000. The neutral base is `patternCellFlags` with the wonder bit cleared: the plot's own terrain value
(flat, hills by terrain, or 0x400 for a mountain), the value Fountain of Youth and El Dorado keep in stock. Not counted,
so the EXE's Fuji stays for them: a wonder on a coast or ocean plot (its neutral base would be water and move the coast
lines; not handled), and an unnamed wonder (`featureType` empty: the server's name comes from the feature's art define,
`srv_liveterrain_features.h`, and an art define of another form gives no name). [I] A mod's wonder that reuses a
stock wonder's art define (for example `ART_DEF_FEATURE_FUJI`) is named after that stock wonder by the server and is
therefore taken as known; with Fuji's art define this agrees with the EXE (Fuji), with another one it does not.
`Producer::Options::neutralUnknownWonders` builds the cells of a change's new table with this rule; `create` always
builds the EXE's rule, because the tap it starts from is the EXE's bake.

**The load-time pass [C] (`srv_liveterrain.cpp`, `LiveTerrain::onField`; `srv_liveterrain_c5r.cpp`).** With the option in
effect, `onField` posts, right behind the create job on the same worker, a change job whose table is the load's own
(journal job without diffs, id = the next job id). The producer, made with the EXE's cells, answers it with the neutral
cells of every unknown wonder of the load: a window when there is one, `NoChange` when there is none. The window takes
the path of every live window (8.14.8): the EXE's field first (`WriteExeFieldWindow`), then the scene's field
(`ApplyWindow`) and the region re-apply; one line per load:
`live terrain: load seq N: load-time pass (job J, FixUnknownWonderBase): live: K plot(s) re-drawn on their own ground;
X ms in the producer's worker, Y ms from the post (off the render thread)` (or "no unknown natural wonder in this load
(no window)", or "not drawn (<reason>): unknown wonders keep the EXE's Fuji base in this load"). The adapter keeps the rule
for the rest of the load when the pass was live or found nothing; when the pass waits, the producer kept the EXE's
cells and the rule stays off for that load (otherwise every later change would carry the wonders' cells and wait with
them). A table that arrives while the create runs merges into the queued pass, as any queued change does (the pass's
line then names the merged job). Counters: `loadPasses`, `loadPassesLive`, `loadPassMsLast`.

**Rule A (the journal).** Journal trailer `"NWON", u32 1` after the river band's and before the seed's, only when the
option was in effect: a journal of a server with the option off is byte for byte the form before 0613. The player
(`Replay::init`) gives `LoadIn::neutralWonders` from the mark, so the first job replays the pass. Test-only switch
`C5_RENDER_LIVE_TEST_UNKNOWN_WONDERS=FEATURE_A,FEATURE_B` (env only, TEST ONLY, one LOUD line when set): the server names
those Types `C5H_TEST_UNKNOWN_<Type>` for the producer, so a stock wonder stands in for a mod's unknown one in a live check
(the mod set has none: Vox Populi adds no natural wonder, checked 2026-11-24 in c5h-a's MODS XML and SQL). The bundle
recorder keeps the feed's own names, so a bundle recorded with the switch does not replay its pass: such runs keep no
bundles.

**Tests [C, 2026-11-24].** `render/tests/liveterrain/producer/producer_test.cpp` (59 cases, 5 new, PASS): `wonder_rule`
(every branch of the rule and its negatives), `wonder_load_pass_redraws_the_unknown_wonder` (Fuji's piece in the tap, max
18,975 u16 in the window on the synthetic map; after the pass 15,300; a second producer on the same tap gives the same
window bit for bit; a second pass is NoChange; a later change does not touch the wonder), `wonder_placed_live_gets_the_neutral_base`,
`wonder_load_pass_without_unknown_wonder_is_no_window` (no wonder, Uluru, Fuji, El Dorado, an unnamed one: NoChange, the
field and the placements unchanged), `wonder_option_off_is_todays_producer` (bit for bit equal to `Options()`).
`server/tests/render/liveterrain/test_liveterrain.cpp` case `unknown_wonder_pass` on the stub (the pass live, journal job
without diffs, the mark's bytes, decode, replay of both generations, a replay without the mark refused; no unknown wonder:
no window; option off: no pass, no mark, today's rule; the pass waiting: the rule off for the load; a merged pass). Options:
`test_options` 1000 passed in the x86 and x64 trees and under ASan (`TestFixUnknownWonderBase`).

**The live check [setup; results below].** Product route, Tiny save copy `C5H-0615-4912-N13PL_0008` (SHA-256
b2ceb663...c29163), a build-26577625 Goldberg instance, this branch's server.exe, proxy and composite on the 0.1b pins,
laid by `lay.py`; every launch with `C5_RENDER_LIVE_TEST_UNKNOWN_WONDERS=FEATURE_ULURU,FEATURE_KILIMANJARO`. Process 0:
setup, save P00; process 1: `nw_add` (Uluru on NW (53,15), flat plains) and `mw_add` (MW (51,18) flat grass to a
mountain with Kilimanjaro), each with the EXE's field and a picture before and 2 s after, save P01; process 2: reload P01
with the option on (the pass); process 3: reload P01 with `FixUnknownWonderBase = 0` (the EXE's own bake, 8.13.2's
finding: the before case). Expected: live, Uluru on flat plains is no change at all (its neutral cell is the plot's) and
Kilimanjaro is drawn as a plain mountain; after the reload with the option on the pass re-draws both and the EXE's field
on both hexes is close to the live one (sample by sample the producer's own pieces and seed against the EXE's, as 2.3 and
the 0717 check); with the option off the reload shows Uluru's and Kilimanjaro's own ground (the before case).
```
bash server/evidence/0613/live_0613.sh build        # gated builds, layout with SHA256.txt
bash server/evidence/0613/live_0613.sh dry
bash server/evidence/0613/live_0613.sh check <inst>  # EXE build 26577625
bash server/evidence/0613/live_0613.sh prep <inst>   # after the claim
bash server/evidence/0613/live_0613.sh run <inst>    # four loads
bash server/evidence/0613/live_0613.sh end <inst>
bash server/evidence/0613/live_0613.sh compare
```

**Results [L, 2026-11-24 23:33-23:48 EST, workstation, c5h-new-b, EXE build 26577625].** Layout of branch
`task-0613-live-terrain-load-pass` built at a884d35f2 on the 0.1b pins (server.exe 62a7767b..., proxy 6f1fbaa0..., composite
4be0ef53..., x64 DLL b84aa3e9...); laid by `lay.py` (backup by SHA-256; restore ok, `provision.py verify --quick` ok,
`leftovers` clean, VP64Options.ini back at its SHA-256 a172959b..., saves put back as snapped). Four loads, every step exit
0, no dialog box, no LOUD line but the switch's own. Captures and pictures in `C:/c5-64bit-work/0613/live/out1/` (outside
git), the server's live lines per process in `.../work1/lines_p{0..3}.txt`.

- **The start lines.** `live terrain: FixUnknownWonderBase: in effect (FixUnknownWonderBase = 1 default; EnableBugfixes =
  1 default) ...` in processes 0-2; in process 3 `off: FixUnknownWonderBase = 0 (from the file)`, and no pass requested.
- **The pass on every load with the option on.** Processes 0 and 1 (no wonder on the map): `no unknown natural wonder in
  this load (no window)`, 0.2 and 0.1 ms in the worker. Process 2 (Uluru and Kilimanjaro in the save, both taken as
  unknown): `live: 2 plot(s) re-drawn on their own ground; 101.2 ms in the producer's worker` (inside 8.6's 50-234 ms for one
  change; 2,086 ms from the post, as it waits behind the create on the same worker, never the render thread), then field
  generation 1: one rectangle 3168,960-3584,1408, 186,368 samples written (55,974 heights changed), the EXE's field first,
  applied in 27.3 ms on the render thread. DIAGNOSTIC numbers, single runs (PERFORMANCE.md: not a performance claim).
- **Live (process 1).** `nw_add`: no change of the field at all (the neutral cell of a flat plot is the plot's own; the
  EXE's field inside NW's hex equal before and live, 0 of 4,096 samples). `mw_add`: change 3 live, the hex a plain
  mountain (mean 35,660 u16, p90 46,524; 112,640 samples written, producer 94.6 ms).
- **Live against the reload, the EXE's field (`compare.json`).**

  | plot | reload with the option on: hex mean / samples within 255 u16 of live | reload with it off (the EXE's own ground: the before case) |
  |---|---|---|
  | NW (53,15), Uluru on flat plains | 15,295 against 15,288 live; 286 of 4,096 samples differ, at most 413 u16 (0.32 world units); 99.3 % within 255 | mean 15,644, max 30,965 (Uluru's raised ground); 4,095 of 4,096 differ, at most 15,665 u16 |
  | MW (51,18), Kilimanjaro on a mountain | 33,634 against 35,660 live (p90 48,440 / 46,524); sample by sample 4,084 differ (3 % within 255) | 24,385 (Kilimanjaro's own crater shape); 4,095 differ, at most 28,860 |

  With the option on, the reload's field on both hexes is what the live change left: on NW to within 413 u16 everywhere;
  on MW the same plain mountain, its pieces re-rolled (the pass draws with a seed made from the reloaded table, so the
  samples differ as every reloaded mountain does: 2.3, and the 0717 check's inland plot 3,994-4,095 of 4,096). With the
  option off the reload shows the EXE's own wonder ground, which the live change had not drawn: 8.13.2's finding, now only
  with the option off.
- **Pictures** (Visual Checks issue 0613, DIAGNOSTIC, D112: the product path laid by `lay.py`, harness shots at half
  scale, camera on the plot): NW and MW each before, live, reload_on, reload_off (`0{1,2}_*_{before,live,reload_on,reload_off}.png`).
  MW reload_on shows a full mountain under the Kilimanjaro model, reload_off the lower crater-topped Kilimanjaro ground;
  on NW the EXE's Uluru model covers its plot in both pictures and no difference is visible: there the difference is in
  the field (the table), which places units and picking.
- **Not shown live:** a mod's own wonder (the switch stands in for one: the EXE bakes the stock wonder's ground, not Fuji's);
  a pass that waits (unit test only); a table merged into the queued pass (unit test only); the huge map.

### 8.14 The live apply off the render frame (ticket 4909) [C for the code and the offline runs, L where marked]

Written by w8-4909 (account B) from 2026-11-19 11:00 EST, branch `w8-4909` off `epic-0465-extras-c-live-terrain`
(1d8aec261, 4907's hook contained). Problem (9.6): the whole live apply ran in one `Scene::poll` on the render thread,
`applyField` re-applying the **whole** field: the server drew no frame for 0.25-0.48 s per change on Tiny.
**Status 2026-11-19 12:30: the region apply is built, bit-equal to the whole path offline (8.14.4) and live-checked on
the product route (8.14.5): 7.4-14.6 ms on the render thread per change on Tiny and 7.4-11.6 ms on huge (whole path
on the same build: 217-419 ms and 1,881-2,016 ms), DIAGNOSTIC numbers; a stale-page defect of the bake found and fixed
(8.14.2).** Status 2026-11-19 23:50 (ticket 0587, 8.14.8): every write in step 2 (no stale frames), the player by
region, region against whole checked live and offline.

#### 8.14.1 What each part of the whole-field path does (code read) [C]

| Part | What it rebuilds for a live edit | Region rule (what a window can reach) |
|---|---|---|
| `terrain::Renderer::setHeightField` (`render/src/terrain/terrain.cpp`) | drops every tile cache and style; builds every EXE height page (1024 x 1024 A8, 11 levels; Tiny 15, huge 70 pages; page (px, py) = samples from px * 896 - 64, both axes wrapping); per tile (1-plot tiles: 2,240 Tiny, 10,752 huge) its z bounds over levels 0-4 of its page, a material texture (S = 70 samples square, `CreateTexture2D` per tile), the land class; the EXE list (`exeTileClass3`, both axes wrapping); the patch index buffers; resets the LOD state | pages whose window holds a changed sample; z bounds of the tiles on those pages; material of the tiles whose 70 x 70 window (x wrapping or clamped, y clamped) meets a rect; land of the tiles whose (P + 8)-sample window meets one; the EXE list of the 64-sample tiles whose 63 x 63 window from + 4 meets one |
| `ResidencySource::onField` -> `Residency::onField` | reads the table again (unless `keepTableOnNextField`), art types, styles; the baker's `setMap`; hashes **every** tile (72 x 72 samples) to find the changed ones; marks every resident tile dirty (the renderer dropped its caches), so the next `beforeDraw` hands every cache back; restarts the tokens, the bake-ahead and the held-frame state; clears the pending decal generations | tiles whose 72 x 72 read window meets a rect; nothing marked dirty (the renderer keeps its caches); scheduling state runs on |
| `WaterScene::onField` -> `water::Renderer::setHeightField` | builds every EXE height page again (the same pages as the terrain's) and marks the currents map stale | the same pages; the currents depend on the plot counts only (`currentsParamsFromGrid`), kept |
| trees (`ForestScene::onField`, 4904) | only the blocks the windows reach (4904's `DerivedAfterWindow`) | unchanged |

Equality rule: the region path leaves every module in the state the whole path leaves it in, except what the whole path
throws away and rebuilds the same (the tile caches, the residency's dirty marks, the currents map) and the scheduling
state of 8.14.1's residency row. The per-tile code of `setHeightField` and of `Residency::onField` was moved into
shared helpers (`Impl::tileBounds`, `tileMaterial`, `tileLand`; `Residency::Impl::scanTile`) so both paths run the same
instructions; the LOD state is reset as `setHeightField` does.

#### 8.14.2 The stale height pages of the bake pipeline (a defect of 4907's live path) [C: test]

The bake pipeline's EXE height pages (`ExePages`, `render/src/bake/pipeline.cpp`, the server's bake) are kept by the
grid's address, sizes and `h.data()`. `ApplyWindow` edits the scene's grid in place, so after a live change every
re-bake of a changed tile read the page of the field **before** the change (the bake's lighting from the old heights)
until the next load. Confirmed by `render/tests/terrain/region_test.cpp` `exe_pages_follow_an_in_place_edit` (the page
after an in-place edit equals the old one, not a fresh build of the edited grid). Fix (ticket 4909):
`HeightPageSource::heightsChanged(g, boxes)` (`ExePages` drops the pages whose window a box reaches, every page without
boxes) through `PipelineBaker::heightsChanged`, which `ResidencySource` calls: with the window's rects after
`onFieldRegion`, and without boxes after every whole `onField` (a whole re-apply, or a new load whose grid lands at the
old address, which had the same staleness). `MapInputs` (baker.h) is unchanged. The test fails without the fix: a copy of
render/ with only the `ExePages` override removed fails `CHECK(h3 == hf)` and `CHECK(h4 == hf)` (2026-11-19 13:17,
`nofix_test.log` of this ticket's work folder); with the fix it passes (plain and ASan, 8.14.4).

#### 8.14.3 The scene's two-step apply (`srv_liveterrain_scene.cpp`) [C]

1. **Step 1, the poll that takes the results** (render thread): per live change 4905's EXE write, then `ApplyWindow`
   into the scene's field and the region hash, as 4907. Then the EXE height pages the windows' rectangles reach
   (`terrain::pagesForRects`) are built by `std::async` from the scene's field (`liveterrain::BuildApplyPages`). While
   the job runs the field is read only: no later live result is taken (they wait in `LiveTerrain`'s queue, a newer
   table merging into them), and `tapPoll` joins the job before a new load's field replaces the grid
   (`liveTerrainJoin`; the changes are then dropped with one line).
2. **Step 2, a later poll once the pages are there**: the trees' boxes (4904), then `Scene::applyFieldRegion`: terrain
   `updateHeightRegion` with the worker's pages, residency `onFieldRegion` (and the pipeline's `heightsChanged`, 8.14.2),
   the tile styles as `applyField`, water `onFieldRegion` with the same pages, the trees' `onField`, the farm's inputs
   (the same values). If any part refuses, one LOUD line and the whole field is applied as before 4909 (D118: never a
   half-updated field; the terrain renderer drops its field when it failed after replacing pages).
3. One line per apply in `server.log`: `live terrain: apply of N change(s) (load seq S): render thread T ms = step 1 a (EXE
   write b, windows c) + step 2 d (region ...: terrain e ms (pages, from the worker, build, upload, tiles: bounds,
   material, land, EXE list), residency f ms (tiles compared, changed), styles, water g ms, trees h ms, farm i ms); pages
   P built on the worker in j ms; k poll(s) between, m ms from step 1 to the end`. The whole-field path's line
   (`terrain: field ...`) now carries its parts too (setHeightField, tile source, styles, water, trees, farm).
4. Switches (test and tool, as `C5_RENDER_LIVE_TERRAIN`; no VP64Options option, K4): `C5_RENDER_LIVE_REGION=0` applies
   the whole field in the same poll (the path before 4909, the live check's arm A); `C5_RENDER_LIVE_ASYNC=0` builds the
   pages on the render thread. The offline player (`liveTerrainReplayTo`) still applies the whole field (bit-equal,
   8.14.4). *Corrected 2026-11-19 by ticket 0587: the player applies by region now, and `C5_RENDER_LIVE_REGION=0`
   switches it too. Items 1 and 2 changed with it: the EXE write and `ApplyWindow` moved from step 1 to step 2, and the
   worker builds its pages from the field before the windows plus the windows (8.14.8).*

What the player sees between the steps [I]: for the poll(s) between step 1 and step 2 (the page build, 5-16 ms
offline) the server draws the old heights while the EXE's field and the scene's grid already hold the new ones; a tile
baked in that gap reads the new heights and is re-baked after step 2 (its generation moves). *Corrected 2026-11-19 by
ticket 0587: no longer so. Step 1 writes nothing and every write is in step 2 (8.14.8). The paragraph describes the
4909 build.*

#### 8.14.4 Offline: region path against the whole path [C for equality, DIAGNOSTIC for the times]

`render/tests/terrain/region_test.cpp` (synthetic 32 x 20 plot fields, wrapped and not, 1- and 2-plot tiles, windows
inside, on the 128-sample page overlap, across the x seam, on row 0 and the last rows, one sample, two windows) and the
bench `region_bench.cpp` on 4903's recorded fields compare a digest of each module's state after the whole path with
the state after the region path: terrain (every page level and every tile's material texture read back from the GPU;
tile page, z bounds, land, style, gray mark; the EXE list; zmaxAll, LOD state, tile grid, patch counts), the same with
the worker's pages, water (every page read back; world block; currents' sizes), residency (per tile class, land, z
bounds, hash, generation, failed generation, cache presence; EXE list, class counts, changed-tile total; the bake
order). **All equal** on every synthetic case (2026-11-19 11:20 and 11:45) and on all five recorded transitions:

| Transition (4903) | rect (x, y, w, h) | pages | terrain whole -> region (with worker pages) ms | water ms | residency ms |
|---|---|---|---|---|---|
| Tiny k1 (41,13) load -> field.0 | 2496, 704, 320 x 384 | 4 | 252 -> 16.4 (9.2) | 60 -> 18 | 49 -> 1.1 |
| Tiny k2 (42,14) field.0 -> .1 | 2304, 896, 544 x 596 | 4 | 222 -> 18.7 (12.0) | 65 -> 17 | 37 -> 2.0 |
| Tiny k3 (42,14) field.1 -> .2 | 2304, 896, 544 x 596 | 4 | 202 -> 26.6 (13.0) | 64 -> 19 | 37 -> 2.0 |
| huge k1 (89,56) load -> field.0 | 5600, 3584, 256 x 256 | 2 | 1,141 -> 9.0 (8.1) | 189 -> 8.3 | 291 -> 1.6 |
| huge k2 (90,56) field.0 -> .1 | 5664, 3584, 256 x 256 | 2 | 1,180 -> 14.2 (7.9) | 331 -> 12.7 | 204 -> 0.9 |

Medians of 3 runs, `c5r_terrain_region_bench` (workstation, RTX 4080, release clang-cl, w8-4909 @276c78755's render),
bench run `bench_diag3` 2026-11-19 11:28-11:38 **DIAGNOSTIC** (D106): a build of this ticket ran at the same time and
another owner's game instance was up, so the times are an indication, not a timing series; the equality is the
result. Page build (the worker's part, both modules' pages built once): Tiny 9-16 ms for 4 pages, huge 4.6-5.1 ms
for 2. Why the times fall: the whole path's cost is linear in the map (huge: 70 pages built twice, 10,752 tiles each
with a material texture made, every tile hashed over 72 x 72 samples, every resident cache handed back), the region
path's in the window (huge: 2 pages, 392 tiles' bounds, 30 tiles' material and land, 30 residency tiles).

#### 8.14.5 Live on the product route (acceptance (2), (3)) [L, see the setup]

**Setup.** Goldberg instance c5h-f (**F:** disk, mods548: CP + VP + EUI 5.4.8, Vibrant Textures), claimed under 0465 by
w8-4909 11:53 (MC-B's grant 11:52). Layout: a copy of 4907's (the batch-4 pin `x64-c29442e8-p58724e0`: the pin's DLL,
CvGameDatabase, lua) with server.exe from w8-4909 @34c8a8ce7 (SHA-256 84c6b1e6...) and the proxy f590f2cb...; the
composite e34f0284... (4907's build of the team branch). `server/evidence/0465/4909/live_prep_4909.sh` (run_3308 prep,
pp_keys, the save copies `C5H-0301-4906-W8PL_0008` of the Tiny library save b2ceb663... and `C5H-0301-4906-W8HG_0350`
of `C5H-VP8P-HUGE_0350`), `live_4909.sh` (one process per arm, F:'s read latency measured over 1 min before each
launch, a dialog check after each step), `live_end_4909.sh`, `collect_4909.py`. Arms on the same build: A = whole field
(`C5_RENDER_LIVE_REGION=0`, today's path), B = region (default). Tiny: 4906's fh_hills .. mw_add in one process; huge:
(89,56) hills -> flat, (90,56) flat -> mountain, (89,56) flat -> hills by `lt.py set`. The frame times are a
**DIAGNOSTIC** A/B under the quiet lock (D106, #6854), on the F: disk.

**Tiny** (2026-11-19 12:10-12:18 EST; F: read latency before the launches max 3.3 / 5.0 ms over 1 min; the quiet-lock
hold DISTURBED by other owners' heavy jobs: a game on c5h-b, render ASan tests, a build_all on F:): 8 of 8 changes live
in each arm, no dialog box, no LOUD line beyond the boot's interface-version line every run has. Per change, the
render thread's apply time from the apply line, and from `render.log`'s `host 1s` counters the cameras the server
skipped and the frames it drew in the second of the change:

| k | transition | A whole: render thread ms (setHeightField / tile source / water) | A: cameras skipped, frames drawn | B region: render thread ms = step 1 + step 2 | B: skipped, drawn |
|---|---|---|---|---|---|
| 1 | fh_hills | 418.5 (257.8 / 94.8 / 60.5) | 12, 39 | 14.6 = 3.1 + 11.5 | 0, 51 |
| 2 | fh_mountain | 379.5 (222.8 / 94.7 / 57.0) | 11, 29 | 13.2 = 2.7 + 10.5 | 0, 58 |
| 3 | fh_hills_back | 375.9 (219.7 / 94.3 / 57.0) | 10, 29 | 7.4 = 1.5 + 5.9 | 0, 56 |
| 4 | fh_flat | 235.3 (141.7 / 54.6 / 35.0) | 6, 46 | 8.3 = 1.6 + 6.7 | 0, 61 |
| 5 | gd | 374.6 (219.3 / 94.5 / 55.9) | 10, 32 | 11.9 = 2.4 + 9.5 | 0, 48 |
| 6 | nw_add | 216.7 (122.8 / 54.9 / 37.4) | 5, 39 | 7.8 = 1.0 + 6.8 | 0, 49 |
| 7 | nw_remove | 389.1 (230.1 / 97.5 / 58.5) | 10, 47 | 11.4 = 1.1 + 10.3 | 0, 51 |
| 8 | mw_add | 375.8 (219.3 / 94.4 / 56.7) | 10, 36 | 9.1 = 2.8 + 6.3 | 0, 50 |

- Arm A reproduces 4907's finding (9.6: 246-475 ms) and splits it: `setHeightField` 123-258 ms, the residency's
  `onField` (tile source) 55-98 ms, water 35-61 ms, trees 0.6-2.6 ms; the whole apply in one poll, 5-12 cameras skipped
  per change.
- Arm B: the render thread's apply is 7.4-14.6 ms per change, in two polls (step 1, the EXE write and the window, 1.0-3.1
  ms; step 2, the region step, 5.9-11.5 ms: terrain 3.1-6.5 ms, residency 1.2-2.3 ms, water 0.2-1.2 ms, trees 0.5-2.3
  ms); the worker built 1-3 pages in 2.3-10.8 ms; 1-2 polls between the steps, 12.7-20.9 ms from step 1 to the end; **no
  camera skipped**.

**Huge** (`C5H-VP8P-HUGE_0350`'s copy, 128 x 84 grid; 2026-11-19 12:19-12:28 EST, F: latency before the launches max
0 / 5.1 ms; hold DISTURBED as above): 3 of 3 changes live per arm, no dialog box, no LOUD beyond the boot line.

| change | A whole: render thread ms (setHeightField / tile source / water) | A: skipped, drawn | B region: ms = step 1 + step 2 (terrain / residency / water) | B: skipped, drawn |
|---|---|---|---|---|
| (89,56) hills -> flat | 1,909.7 (1,155.9 / 450.4 / 300.4) | 57, 1 | 9.1 = 2.8 + 6.3 (4.3 / 1.2 / 0.6) | 1, 7 |
| (90,56) flat -> mountain | 1,880.9 (1,144.8 / 450.6 / 282.6) | 55, 6 | 11.6 = 2.5 + 9.1 (6.0 / 2.1 / 0.7) | 0, 17 |
| (89,56) flat -> hills | 2,016.0 (1,266.5 / 451.5 / 295.1) | 59, 6 | 7.4 = 1.7 + 5.7 (3.8 / 1.2 / 0.5) | 0, 20 |

- Arm A confirms 4907's estimate for huge (9.7: 1.5-2.2 s): 1.88-2.02 s per change, about two seconds without a
  server frame (55-59 cameras skipped).
- Arm B: 7.4-11.6 ms on the render thread per change, the larger step 5.7-9.1 ms; the worker built 2 pages in 4.5-4.9
  ms; 16.8-22.0 ms from step 1 to the end. **Acceptance (3), a hitch under about 50 ms per change on huge: met**
  (DIAGNOSTIC numbers; 1 camera skipped in the second of the first change, 0 after the next two). Few frames drawn in
  both huge arms (1-20 a second): the huge game's own rate in that minute (the camera still, the residency's bake-ahead
  running), not the apply.
- The region step's terrain part (2.5-6.5 ms) is mostly the z bounds of every tile of a rebuilt page (196 per page) and
  the material textures of 30-49 tiles; the copy of the worker's pages (`build` 0.3-0.9 ms) and their upload (0.3-0.8
  ms) are small.

Why the numbers moved (PERFORMANCE.md): today's apply does work linear in the map (every page twice, every tile's
material texture, every tile's hash, and every resident cache handed back at the next draw), the region apply work
linear in the window (1-3 pages built once on a worker, a few hundred tiles' bounds, 30-49 tiles' textures and hashes).
Not a timing series: one process per arm, the quiet-lock holds disturbed by other owners' heavy jobs, F: disk
(D106); the effect (25-270 x) is far outside that noise.

Evidence: `server/evidence/0465/4909/live_server_lines.txt` (every live-terrain, field and LOUD line of the 4 runs,
paths scrubbed), `live_summary.jsonl` (`collect_4909.py`'s per-change summary with the host's counters),
`live_4909_steps.log` (the driver's steps and the F: latencies).

**End.** `live_end_4909.sh` 12:28-12:29: harness stop, `frontend.py restore` (MainMenu.lua, FrontEnd.lua,
LoadScreen.lua original, each equal to the install manifest's hash), `run_3308.py restore` (provision verify rc 0); claim
released 12:29. The end script was started a second time by mistake at 12:29: its `run_3308.py restore` stopped at a
missing `vpproxy.ini` (the first restore had already put the stock layout back, which has none) before changing
anything; the front end was again original.

**Suites** (after the merge of the team branch 33b5c0f3f = main 2008dbd17, 2026-11-19 12:32-13:17): plain and ASan
(MSVC /fsanitize=address) each green: `render/build.py --test -R "terrain|water|residency|bake"` 20 of 20 (ASan: 0
reports; `c5r_terrain_region_test` 8 of 8 among them), `run_all.py --only render-host --only water-scene` 143 + 28 passed
(the rt build compiles the server's `server/src/render/*.cpp` with this ticket's changes; `water-scene` has the new
`field_region` case), the hook's suite `test_liveterrain.py` 3 passed. The ASan build first failed on MSVC C4458 (a
parameter `sz` hid `Impl::sz`), renamed. server.exe itself was last built at w8-4909 @34c8a8ce7 (the live build); after
the merge `build_server.py` refuses the batch-4 fork copy (deps/dll.lock now pins batch 5, 1fd51353e), and no server
or render source changed in the merge.

#### 8.14.6 Open (2026-11-19 12:30)

- *Done by ticket 0587 (8.14.8), 2026-11-19:* the offline player applies by region as the live path does. (Was: "The
  offline player (`liveTerrainReplayTo`) still applies the whole field (bit-equal to the region path, 8.14.4); a bundle
  replay therefore shows no hitch difference.")
- *Done by ticket 0587 (8.14.8), 2026-11-19: removed by design.* Every write now happens in step 2, in the poll that
  replaces the pages. (Was: "Between step 1 and step 2 (one or two polls, 13-22 ms) the EXE's field and the scene's grid
  already hold the new heights while the server draws the old pages; not seen in a picture (one or two frames), not
  measured [I].")
- The LOD state is reset at a live change as the whole path did; keeping it might avoid a tessellation pop, not tried.
- 4907's other open items (9.7) are unchanged.

#### 8.14.7 Batch-5 smoke on the product path (MC #7050, 2026-11-19 13:20-13:27 EST) [L, DIAGNOSTIC times]

The merge check before 0465's MERGE-REQUEST: one short live run after the hook change, on the batch-5 pin.

- **Build.** server.exe from w8-4909 @d89ef78b1, which is the team head ea4c9b83e plus evidence and issue-log lines only;
  repo_dirty 0, dll_pin 1fd51353e, release. It was made by `build_server.py` 13:20:53-13:22:33 through the gate, with a
  plain copy of the batch-5 fork source and its finished `build_all` release output. Both were copied read-only from
  4883's batch-5 build (`build_all.json` rc 0, src_commit 1fd51353e). `build_server.py` checked the source's fingerprint
  against deps/dll.lock and took it. Hashes: server.exe SHA-256 e0a201a7..., proxy 9be5f471...
- **Layout.** A copy of 0551's batch-5 layout `x64-1fd51353-p1e09fc7` (the x64 DLL 78355a75...) with that server.exe and
  proxy. The composite is e34f0284..., 4907's build; no composite source changed on the team branch since.
- **Instance and steps.** Goldberg c5h-c (C:), claimed 13:22, released 13:27. `live_4909.sh ... smoke`: run_3308 prep,
  one process from the copy of the Tiny library save (`C5H-0301-4906-W8PL_0008`), `lt.py setup`, then `apply fh_hills`
  and `apply fh_flat` on the default (region) path. The quiet-lock hold was DISTURBED by another owner's server build.
- **Result.** Both changes were live (field generations 1 and 2, box 3008,1024-3360,1344).
  - fh_hills: the render thread's apply took 14.9 ms (step 1 3.1 ms + step 2 11.9 ms: terrain 6.9, residency 2.3).
  - fh_flat: 11.6 ms (1.5 + 10.1 ms: terrain 5.4, residency 2.3).
  - The worker built 1 page in 2.1 ms each time. No camera was skipped; 50-51 frames were drawn in each change's second.
  - No dialog box. **0 LOUD lines**: the boot interface-version LOUD line of the batch-4 runs is gone on batch 5,
    whose DLL has the version export.
- **End.** Harness stop; the front end was restored by the install manifest's hash (MainMenu.lua, FrontEnd.lua and
  LoadScreen.lua original, before and after). `run_3308.py restore`: the backup verify gave every entry equal or absent
  as before, vpproxy.ini went back to the recorded hash, and provision verify returned rc 0.
- **Evidence.** `server/evidence/0465/4909/batch5_smoke_server_lines.txt`, `batch5_smoke_summary.jsonl` and
  `batch5_smoke_steps.log`.

#### 8.14.8 Ticket 0587: every write in one poll, the player by region, region against whole live [C for the code and the offline runs, L where marked]

Written by impl-0587 (account B) from 2026-11-19 22:13 EST, branch `task-0587-live-terrain-region-checks` off main
5c0402810 (0586 merged). The ticket asks for three things left open by 4909 (8.14.6):
- a live comparison of a region apply against a whole-field apply;
- the one or two frames of old heights between the two render-thread steps, removed or measured;
- the offline player applying only the region.

**1. The stale frames: removed by design [C].** Until 0587, step 1 wrote the EXE's field (4905) and the scene's grid
(`ApplyWindow`), and the worker then built the pages from that grid. For the polls until step 2 (1-2 on the
product route, 8.14.5), the server drew the old pages while the EXE and the grid held the new heights. A tile baked in
that gap read the new heights over the old pages.

Now step 1 writes nothing. The worker builds each page the windows reach from the field **before** the windows, with
the windows written over it. This is `BuildPageWithWindows` (`server/src/render/srv_liveterrain_pages.*`):
- It copies the page's 1024 x 1024 samples from the field.
- It writes the windows' rectangles over them in `ApplyWindow`'s order.
- It holds the result in a 16 x 16 plot scratch grid, laid out so that the scratch's page (0, 0) reads every sample at
  the place where the field's page (px, py) reads it.
- `bake::buildHeightPage` of that page gives the same bytes at every level. Level 0 is the samples' high bytes, and
  the mips read only level 0.

Step 2 is one poll. It writes the EXE's field, runs `ApplyWindow` and the region hash, then runs the region update with
the worker's pages. A poll draws nothing in the middle of that work, so no frame shows the field, the EXE and the pages
disagreeing. Two side effects:
- The gap's bakes now read the old heights over the old pages, which agree.
- The render thread does the same work as before, moved from step 1 to step 2 (the EXE write 1.6-8 ms and the windows
  about 1 ms: 9.6, 8.14.5).

When step 2 cannot write a window (the EXE refused it, or `ApplyWindow` refused it), the worker's pages hold a window the
field lacks. That apply builds its pages from the written field on the render thread (`updateHeightRegion` with no
pages, one plain line). A discarded job (a new load's field) has written nothing. The apply line now reads `render
thread T = step 1 a + step 2 d (written in step 2: EXE write b, windows c; region ...)`.

TEST ONLY switch `C5_RENDER_LIVE_PAGECHECK=1`: in step 2 every worker page is compared, level by level, with
`buildHeightPage` of the written field. It logs `live terrain page check ...: N of N worker page(s) equal`, or one LOUD
line per page that differs. Its time is left out of the apply's times.

**2. The offline player applies by region [C].** `Replay::advanceTo` now also hands back every rectangle of the
windows it wrote (optional out parameter). `Scene::liveTerrainReplayTo` gives those rectangles to `applyFieldRegion`,
as the live scene does, and builds the pages on the player's own thread (`m_liveAsync` is false). The trees take the
windows' boxes first, as live. `C5_RENDER_LIVE_REGION=0` switches the player to the whole field as well. A refused region
step is LOUD, and the whole field is then applied.

**3. Tests [C, 2026-11-19 22:28-22:40].**
- `server/tests/render/liveterrain/pages/` (new; `test_pages_build.py`, `test_pages.py`, `test_pages.cpp`):
  - Six synthetic fields: 32 x 20 wrapped and not, 56 x 40, 10 x 6 and 20 x 12 (narrower than a page, so a page reads a
    sample twice), and 40 x 9.
  - For each field, 11 levels and 1 level, up to nine window sets: inside, on the 128-sample page overlap (only on the
    three fields above 1,000 samples in both axes), the last and the first columns, row 0 with the last rows, one
    sample, two overlapping changes (the later one wins), a window of two rectangles, and the whole field.
  - Every page is compared with `buildHeightPage` of the written field: **in all 102 runs (field x levels x window set),
    every page equal**.
    The field is unchanged after each build, and each window set does move a page, so no case passes on unchanged
    pages.
  - Nine refusals, each with `out` left empty: outside the field (x and y), an empty rectangle, heights not w x h, a
    null window, a page past the count, mip count 5, samples not plots * 64, and a short field.
  - Plain (clang-cl) and ASan (MSVC `/fsanitize=address`): 2 passed each, 0 ASan reports. Run:
    `server/evidence/0465/0587/test_pages_run.txt`.
  - Cost (one Tiny page, 11 levels, best of 5, an indication only): with the window 5.3 ms, plain `buildHeightPage`
    3.9 ms. The copy into the scratch grid adds about 1.4 ms per page, on the worker.
- `test_liveterrain.cpp`, case `bundle_text`: the replay's rectangles equal the live windows' rectangles for both
  generations, carry no samples, and are empty when nothing is written. 3 passed plain and under ASan.
- server.exe and the proxy built release from this branch @1f8e94c69 (`build_server.py`, batch-5 fork source 1fd51353e):
  server.exe 4160b4d9..., proxy da7f6618...

**4. Live: region against whole, the same changes [L, 2026-11-19 23:18-23:45 EST, workstation, c5h-c].**

*Setup.*
- Instance: Goldberg c5h-c (C:, mods548: CP + VP + EUI 5.4.8, Vibrant Textures, D94). Its DX11 EXE was checked before
  each launch: build 4390913, stamp 0x546CD0A8, 16,982,584 bytes. Claimed by impl-0587 on MC #7328.
- Layout: the batch-5 layout `x64-1fd51353-p1e09fc7` with this branch's server.exe (4160b4d9...) and proxy (da7f6618...),
  and 0586's composite (8f384c9f...; `shell/` is unchanged since it was built). All of it is made by
  `server/evidence/0465/0587/build_0587.sh`.
- `live_0587.sh prep`: run_3308 prep (D33 minus Overlay, `[Renderer] ServerRenderer = 1`), the composite keys, and the save
  copy `C5H-0301-4906-RCPL_0008` (SHA-256 b2ceb663..., the Tiny library save).
- One game process per arm, each loaded from that same save. In each: `lt.py setup`, then fh_hills (k 1) and
  fh_mountain (k 2). Each is captured before and after by `scenes_0587.py`: popups are closed in a loop (D114), the EXE's
  field is read, the camera is put on (52,15), and a picture is taken about 6 s after the change.
- The arms:
  - A: `C5_RENDER_LIVE_REGION=0`, the whole field.
  - B: the region path with the TEST ONLY page check.
  - R: the region path with a scene bundle recorded across fh_hills.
  - C: the region path as it ships.
- No dialog box was seen at any step.

*The same change in both arms (the EXE's field, `fielddiff_0587.py`) [L].*
- Before the change the two loads differ in 5 height samples and 3 material samples. This is the load's own noise (P5),
  at (row, column) (752,3427), (838,1347), (839,1347), (1618,3211) and (1800,2715), all outside the window.
- After the change the same 5 samples differ, and no other.
- The change itself (live minus before) is **equal sample by sample in A and B**, heights and materials, for both
  transitions: 47,567 (k 1) and 46,487 (k 2) samples, in rows 1025-1342, columns 3009-3358.

*The worker's pages (arm B, TEST ONLY page check) [L].* For both changes the line reads `1 of 1 worker page(s) equal to
a build from the written field, every level` (7.0-7.3 ms, left out of the apply's times). The bit-equality of 3 holds
on the product path.

*The render thread's apply (`server.log`, DIAGNOSTIC: one process per arm, no quiet-lock hold, D106):*

The last column holds the cameras the server skipped and the frames it published in the second of the apply, from
`render.log`'s `host 1s` totals, the second's line minus the one before (`summary_0587.py`, `live_summary.jsonl`).

| Arm | k | render thread ms | step 1 / step 2 | of step 2: EXE write, windows, update | pages (worker ms) | polls between | skipped, published |
|---|---|---|---|---|---|---|---|
| A whole | 1 | 377.6 | 0.0 / 377.6 | 2.9, 0.6, whole field 373.8 | - | 0 | 10, 30 |
| A whole | 2 | 395.8 | 0.0 / 395.8 | 1.2, 0.5, whole field 394.0 | - | 0 | 11, 34 |
| B region + check | 1 | 21.3 | 0.5 / 20.8 | 5.0, 1.1, region (terrain 8.7, residency 2.4, water 1.0, trees 2.3) | 1 (8.4) | 1 | 0, 47 |
| B region + check | 2 | 19.5 | 0.0 / 19.4 | 3.1, 0.7, region (terrain 9.4, residency 2.6, water 1.0, trees 2.4) | 1 (12.0) | 2 | 0, 45 |
| R region, recording | 1 | 15.4 | 0.0 / 15.4 | 2.8, 0.6, region (terrain 6.6, residency 2.3) | 1 (4.2) | 1 | 28, 3 (the recorder's 16 dumped frames fall in that second) |
| C region (ships) | 1 | 17.4 | 0.0 / 17.3 | 3.7, 0.7, region (terrain 7.5, residency 2.4, water 0.8, trees 1.9) | 1 (8.2) | 1 | 0, 46 |
| C region (ships) | 2 | 8.4 | 0.0 / 8.4 | 1.2, 0.5, region (terrain 3.6, residency 1.4, water 0.4, trees 1.2) | 1 (4.4) | 1 | 0, 51 |

- The whole path skips 10-11 cameras per change, as 8.14.5 found (5-12). The region path skips none outside the
  recording arm.
- Step 1 is now 0.0-0.5 ms; step 2 carries the EXE write (1.2-5.0 ms) and the windows (0.5-1.1 ms) that were step 1's.
  So the render thread's total per change (8.4-21.3 ms) is not lower than 4909's 7.4-14.6 ms (8.14.5): the same work
  in one poll instead of two. The spread is the machine's load: DIAGNOSTIC numbers, not a timing series.
- The polls between the steps (1-2) are still there. They now draw the old field consistently: the EXE, the grid and
  the pages are all still the old ones.

*Pictures (DIAGNOSTIC, D112; Visual Checks issue 0587; `compare_0587.json`) [L].* The pictures are A against B at the
same camera, half scale, 1,277 x 705 (900,285 pixels), max channel difference per pixel. Each row compares the same
state in the two game processes:

| Pair | whole frame: median / p95 / mean, share > 16 | 60-pixel boxes on the changed plots: median / p95 / mean |
|---|---|---|
| k 1 before (the floor) | 4 / 55 / 13.2, 26.3 % | 6 / 63 / 15.3 |
| k 1 after the change | 5 / 55 / 13.4, 26.4 % | 6 / 69 / 17.1 |
| k 2 before (the floor) | 5 / 56 / 13.4, 26.4 % | 13 / 70 / 20.7 |
| k 2 after the change | 4 / 55 / 13.2, 26.2 % | 5 / 91 / 20.2 |

- Two game processes of the same save differ that much by themselves: animated water and units, and the EXE's own
  frame. So the pictures show no difference between region and whole above the before pair's floor; they cannot show a
  small one.
- The exact comparison of the drawn result is offline (item 5): the bundle recorded live in arm R is drawn by the player
  both ways.

**5. The offline player, region against whole, on the bundle recorded live [L offline, 2026-11-19 23:42-23:47].**

Setup:
- The bundle is arm R's `F:/c5-64bit-work/0587/live/bundles/RC01_20261119-232800_workstation`: region path, span from
  frame 1500, 16 frames dumped, fh_hills applied inside the span.
- It is drawn by this branch's `sceneshot.exe` (`build_sceneshot.py`, 22:41) twice, with `--dumped --compare --diff
  --tolerance 1:1`: once by region (the default) and once whole (`C5_RENDER_LIVE_REGION=0`).
- 4907's format-8 bundles (9.8) could not be used. Today's player refuses them (exit 26 OlderVersion), so arm R
  recorded a new one.

Results (`server/evidence/0465/0587/replay_rc01_summary.txt`):
- **The two players' outputs are byte-equal.** All 16 PNGs, all 16 depth files and all 7 diff PNGs are equal (39 of 39
  files). The player's region apply draws exactly what its whole-field apply draws, on live-recorded inputs.
- The player applied generation 1 at frame 2365 with its hash checked: by region in 126.6 ms (terrain 6.7 ms with 1
  page, residency 1.4 ms with 49 tiles compared and 33 changed, water 3.2 ms, trees 1.2 ms), whole in 491.8 ms. Both
  times include the producer's replay of the job, and they are an indication only.
- Against the live dumps (P1), both players give the same result as 9.8:
  - the 9 frames before the change: colour and depth **EQUAL**;
  - the 7 frames after it: depth **EQUAL**, colour differs in 167,962-167,971 of 3,603,694 pixels (median 8, p95 45,
    max 150 over the differing ones). Exit 10.
  - The cause is 9.8's: the live server re-bakes the changed tiles' colour after the change, while the player settles
    each shot. It is the same with both paths, so it is not caused by the region apply.

**6. Not verified / open (2026-11-19 23:50).**
- Huge was not run live in 0587. The page builder's equality is size-independent (tested on 6 field shapes), and 8.14.5
  measured huge's region apply on the 4909 build.
- A window refused in step 2 (the EXE or `ApplyWindow`): the fallback, pages built on the render thread from the
  written field, is covered by code reading only. Neither the tests nor the live run produced a refusal.
- The polls between the steps remain (1-2). Nothing in them is stale any more, but the change reaches the screen one or
  two polls after the producer finished (31-39 ms from step 1 to the end with the page check, arm B; 8.14.5 measured
  12.7-22.0 ms without it).
- The re-bake lag of the changed tiles' colour (9.8) is unchanged by 0587.

### 8.15 The coast strips (step 6) and the lakes, as the port uses them (ticket 4911, epic 0615) [C, decompile of 0x709B50, 0x639D40, 0x626500, 0x6990D0, 0x4B2100, 0x711750, 0x55F9F0, 0x7222E0, 0x4C0560, 0x4BD430, 0x73C170, 0x412680, 0x4954C0, 0x416CF0; constants read from the EXE; 2026-11-20 15:30-16:20 EST]

Written by w2-4911 before the port (D83): our own words, addresses and constants, no decompiler text. The EXE is the old
build 4390913 (stamp 0x546CD0A8), read with headless Ghidra on a scratch copy of the project. Its decompiler output
stays in scratch and is not committed. Everything below is [C] unless it is marked otherwise. Where this section and
`asset-pipeline.md` 4.1 step 6 differ, this section is the detailed one; that step now points here.

**Where step 6 sits in `CreateTerrain` 0x709B50.** The order is:

1. the block blend (`BlendTerrainHeightmap` 0x6BA6F0, 7.4);
2. 0x6D78D0;
3. 0x5C8320;
4. the marsh replay 0x63B0F0;
5. the strip builder 0x4B2100;
6. the strip raster `BlendAllCoastMeshes` 0x639D40;
7. the river mesh 0x6D51D0;
8. the river carve 0x412680.

The strip steps therefore work on the blended field after the marsh replay, and the strips' material writes can
overwrite the marsh stamp. Both 0x4B2100 and 0x639D40 run as methods of one coast object on `CreateTerrain`'s stack.
That object holds:

| Offset | What it holds |
|---|---|
| +0x00 | the TerrainDef table |
| +0x10 / +0x14 | the coast styles (array of 100-byte profiles, count) |
| +0x20 | the five-line records (0x38 bytes each) |
| +0x40 | the coast splines (0x2C bytes each) |
| +0x44 | the coast count |

**Input: five offset lines per coast (made by step 3, W1's ticket 4910, 8.10.1).**
- The job 0x9E0AC0 > 0x57BD70 > 0x73C170, started per coast spline by 0x4BEF10, turns each coast spline of n points into
  five polylines of L = 8 n - 14 points each, all in one point array. Record dwords 0-9 are the five ranges
  [i L, (i + 1) L); dword 13 is the coast's art style.
- A point is 16 bytes:
  - f32 x and f32 y, in samples;
  - f32 t, a blend fraction copied from the source spline point;
  - u8 style A at +0x0C and u8 style B at +0x0D, two coast-style kinds.
- The lines lie along the coast's normal at the style's offsets (CoastStyles.xml, below), blended between the two styles
  by t, with the character splines added on top.

**The coast styles (0x4BD430, `terrain/Coast/CoastStyles.xml`).** The file is in `Resource/Common/TerrainModels.fpk`
(5,477 bytes in the pre-update copy the render tests read), so it is read from the player's install at run time.
- Each `CoastStyle` element becomes one profile of 25 dwords: five lines of four values (offset, weight, height,
  character), four band terrain codes, and the kind.
- The kind is the style's `type` name looked up in the EXE's table of 13 names at 0xB9A064: Default, Grass, GrassHill,
  Plains, PlainsHill, Snow, SnowHill, Desert, DesertHill, Tundra, TundraHill, Mountain, DefaultShallows. The stock XML
  lists them in the same order.

  | Line | Offset | Weight | Height | Character |
  |---|---|---|---|---|
  | 0 | minus `SmoothingOffset@HighSide` (-20) | 0 | `HighBeach@Height` | none (-1) |
  | 1 | minus `HighBeach@Offset` (-1.5) | 1 | `HighBeach@Height` | `HighBeach@Character` |
  | 2 | `Waterline@Offset` (0) | 1 | `Waterline@Height` | `Waterline@Character` |
  | 3 | `LowBeach@Offset` (1.5) | 1 | `LowBeach@Height` | `LowBeach@Character` |
  | 4 | `SmoothingOffset@LowSide` (20) | 0 | `LowBeach@Height` | none |

- The four bands lie between consecutive lines. Their terrain codes are: band 0 none, band 1 `HighBeach@TerrainType`,
  band 2 `LowBeach@TerrainType`, band 3 none. A value of 65535 in the XML means none (-1), for a character or a terrain
  type alike.
- Stock values:
  - every style except Default, Mountain and DefaultShallows: HighBeach 0.2044, Waterline 0.19836, LowBeach 0.19226,
    terrain type 5 on both beaches;
  - Default: HighBeach 0.2244;
  - Mountain: 0.24 / 0.19 / 0.18, HighBeach terrain none;
  - DefaultShallows: 0.11 / 0.1 / 0.09, no terrain.
- **A terrain code becomes a material byte (0x4C0560).** It is looked up in the TerrainDef table, whose row k is
  material k. A row's id is `(0x10 + continent) << 16 | id`, with the CSV's id column read as hexadecimal (loader
  0x61BAE0, `%x`; the prefixes are the table at 0xB99658: 0x100000 for continent 0, then 0x110000 to 0x140000). Two
  lookups are made:
  - first `0x110000 | code`;
  - if that finds nothing, `0x100000 | code`;
  - if neither finds a row, the band has no material.

  The strip builder always asks for continent 1 first (America). Code 5 is absent from continent 1, so it resolves to
  row 3, `Euro_Coast` (continent 0, id 5): the beach material is material 3.
- **A kind that has no style in the XML** gets an all-zero profile: offsets 0, weights 0, heights 0, and band codes 0,
  not none. Code 0 then resolves through `0x110000` to the American marsh row, so the EXE would paint that. The stock
  XML defines all 13 kinds.

**The strip mesh, per coast (0x4B2100 > job 0x9FA43C > 0x711750 > 0x55F9F0).**
- **Columns.** The mesh has L - 1 columns j = 0 .. L - 2. Column j's point on line i is the midpoint of the line's
  points j and j + 1. It comes from the uniform quadratic B-spline 0x7222E0 evaluated at fraction 0, or at fraction 1
  over points j - 1, j, j + 1 for the last column; both give the same midpoint.
  - x, y and t are averaged in f32 as `p[j+1] * 0.5 + p[j] * 0.5`.
  - The two style bytes are copied from point j, except for the last column, which copies them from point j - 1.
  - Every line uses line k's point count for its own end test. The five counts are equal, so this changes nothing.
- **Vertices.** There are eight vertices per column: for each band k = 0..3, an inner vertex on line k and an outer
  vertex on line k + 1. Each vertex holds f32 x, f32 y, a material byte (0xFF for none), f32 height and f32 weight.
- **Profiles.** Each vertex pair is computed from two profiles:
  - profile A, for the inner point's style A;
  - profile B, for its style B.
  - Both are re-read only when the inner point's style A differs from the last one read. A change of style B alone
    does not re-read them.
  - Each re-read flips a parity, which starts so that the first read leaves it "even".
  - With an even parity the fraction is f = t; with an odd one f = 1 - t. t is the inner point's.
- **The values of a pair** (f32):
  - inner height `B.height[k] (1 - f) + A.height[k] f`; inner weight the same with the weights;
  - outer height and weight the same with line k + 1's values;
  - both vertices' material: the band k terrain of B when f <= 0.5, else that of A.
  - The inner vertex's x, y come from line k, the outer vertex's from line k + 1. Their own style bytes are not used for
    the outer vertex.
- **Triangles.** There are L - 2 quads, each joining column j to column j + 1, with eight triangles per quad in band
  order. Band k gives two triangles:
  - (inner j, outer j, inner j + 1);
  - (outer j, outer j + 1, inner j + 1).
- **Edge weights.** Each triangle carries three edge weights e01, e12, e02, the same for every quad:

  | Triangle | Edge weights |
  |---|---|
  | band 0, first | 0, 0, 0 |
  | band 0, second | 1, 0, 0 |
  | bands 1 and 2, all four | 1, 1, 1 |
  | band 3, first | 0, 0, 1 |
  | band 3, second | 0, 0, 0 |

- **Segments.** The 8 (L - 2) triangles are cut into segments of c triangles (the last one shorter):
  - c starts at 64 and grows by 8 while either holds: there is more than one segment and their number is odd, or the
    remainder of the triangle count over c is 1 to 7.
  - The segment count is therefore 1 or even.
  - A segment starts on a quad boundary. Neighbouring segments share the column between them.

**The raster (0x639D40 > job vtable 0x9E4944 > 0x626500 > 0x6990D0).**
- **Jobs.** One job per segment, in two waves:
  - first, every even segment (0, 2, 4, ...) of every coast, in coast order;
  - then the load thread waits (0x81D790);
  - then every odd segment.
  - The jobs of one wave run in parallel. Two segments of one coast in the same wave are at least one segment apart, so
    in practice only two different coasts that come within a strip's width of each other can race [I].
  - So for a sample, the triangles of the even segments act before those of the odd ones, and within a segment they act
    in triangle order.
- **Per triangle**, all in SSE f32 unless said otherwise:
  - **Box.** The first column is `floor(min x + 0.5)`, computed in x87 and double (exact), and likewise the first row
    from min y. The box ends before `trunc(max x) + 1` and `trunc(max y) + 1` (truncation toward zero).
  - **Skip test.** The triangle is skipped when |det| < 1e-5, with det its doubled signed area (the constant is the
    double at 0x9EE840). Otherwise the reciprocal `1.0f / det` is taken once.
  - **Barycentrics.** The vertices are taken relative to the box origin (f32 subtractions). The barycentrics are
    evaluated at the integer sample position, with no half-sample offset, as affine functions of the column and row
    steps. Each step adds the reciprocal to a running f32 sum, so they are not multiplied out.
  - A sample is drawn when all three barycentrics are >= 0, tested in the order b0, b1, b2, and its row is in
    [0, samplesY). x is not range-checked: on a wrapping map it wraps as `(W*100 + x) % W` (0x548980). On a map that
    does not wrap the EXE would write outside the row [I: not reached on the maps seen; the port clips].
  - **Weight.** `w = v0.w b0² + v1.w b1² + v2.w b2² + 2 (e01 b0 b1 + e12 b1 b2 + e02 b0 b2)`, summed in that order, and
    then `w = max(0, w)`. So the weight is 1 inside bands 1 and 2. In bands 0 and 3 it falls off quadratically toward
    lines 0 and 4.
  - **Height.** With `hv = (v2.h b2 + v1.h b1) + v0.h b0`, the new height is
    `trunc((hv w + old * (1/65535) * (1 - w)) * 65535)` taken to 16 bits. The constants are the f32 0x37800080 and
    65535.0f (0x477FFF00), and the conversion is `cvttss2si` (truncation, not rounding as in the blend's write-out).
    old is the sample's current height, which the triangles before it in the wave order may already have changed.
  - **Material.** The sample's current material byte is read first. If the triangle's first vertex has a material
    (not 0xFF), that material is written. Then, if the new height is at most 35,000 (0x88B8), the mountain-top swap
    is applied: for each continent 1-4, a sample whose material **before** this triangle was that continent's row
    `| 0x35` becomes its row `| 0x10`.
    - The rows are looked up in the TerrainDef table at the job's start. A continent without both rows takes no part.
    - With the stock CSV only America has both: row 10 `Amer_Mountain_Top` (id 35) becomes row 9
      `Amer_Mountain_Base` (id 10).
    - The swap is written after the vertex material, so it wins.

**The two `rand()` draws and the "special segment".**
- On the load thread, after the coast object is built and before the first wave, 0x639D40 draws `rand() % coasts` to
  pick one coast. Then, inside the loop over the coasts, when it reaches that coast, it draws `rand() % (segments / 2)`,
  times 2, to pick one even segment.
  - That makes exactly two draws whenever there is at least one coast, and none when there is none (the function
    returns early).
  - No guard is in the code: a picked coast with a single segment would divide by zero. A one-segment coast needs a
    source spline of 3 points (64 triangles); none was seen [I].
- The picked job gets a value: the result of 0x4173D0 (which only calls 0x416CF0, a CEG routine over code tables)
  minus 0xFF. Every other job gets 0.
- **The value has no effect on the field.** The job body 0x626500 only tests it. When it is non-zero, the job takes
  128 bytes from the temporary heap and never uses them, then runs the raster exactly as for every other job [C, the
  disassembly of 0x626500].
- [I] In an intact EXE the CEG routine presumably returns 0xFF, so the value is 0. It looks like a tamper tripwire.
- **What this means for a port:**
  - the special segment needs no model;
  - a reload would pick another coast and segment (the matcher's and the jitter's draws come before it), with no
    visible difference;
  - the draws count only for the rand stream that the river mesh (step 7) reads after them.
  - *Correction of `asset-pipeline.md` 4.1 step 6 and `water-system.md` 3, 2026-11-20: "a special value" was read as a
    height effect by the earlier entries.*

**The blender's three constants (+0x1D0..+0x1D8: 15423, 7650, 13000; set by 0x669920).**
- None of the strip, blend or carve functions read +0x1D4 or +0x1D8. This comes from an operand scan of the whole
  `.text` for those offsets: no hit falls in 0x6990D0, 0x639D40, 0x55F9F0, 0x73C170, 0x412680, 0x46AEF0, 0x7A1FD0,
  0x772510, 0x70B490, 0x608270, 0x463B10, 0x6414D0 or 0x425060 [C].
- 15423 (`0x3C3F`) appears only in that set-up and as a push to 0x639D40, whose raster does not use the argument.
- Their readers, if any, were not found [I: 15423 = 0.23534 x 65535, the blend's offset; 13000 = the stock
  `Waterline@Height` 0.19836 x 65535 rounded; 7650 = 0.1167].

**Lakes.**
- **There is no lake carve.** 0x412680 builds its triangles only from the river records (0x4954C0 reads the per-river
  centre lines and their running minimum, 0556). It has no lake path.
- A lake's basin is the pieces' work: lake plots are coast class in the pattern cells, `asset-pipeline.md` 4.2 point 1,
  so the matcher places coast pieces there and the region fill marks them water. The coast strips then run along its
  shore, because the coast splines follow every land-water edge [I: that the coast grid 0x489CC0 counts lake water as
  water is not read separately; the beach material along the lakes of the taps is the check].
- The 0556 floor (0.1753) belongs to the river carve only. A river leaving a lake meets it there.
- So for this ticket a "new lake" needs the pieces (the producer), the region fill and the strips (W1, 4910) and the
  strips along its shore (this section); rivers stay protected (MC #8420 (d)).

**Checks against recorded data (EFFECTIVENESS 15; the taps of 4902 (`F:/c5-64bit-work/0465/4902/`): Tiny t8, huge
t350; 2026-11-20 16:15).**
- **Beach heights and material.** Material 3 (`Euro_Coast`, the resolved code 5) holds 45,790 samples on Tiny and
  145,311 on huge. Of these, 99.06 % (Tiny) and 98.18 % (huge) lie between LowBeach 12,599 and HighBeach 13,395 (u16,
  0.19226 and 0.2044 x 65535 truncated), where bands 1 and 2 write with weight 1. The rest lie lower, which fits the
  river carve after step 6 [I].
- **Material rows.** Rows 9, 10 and 3 of the pre-update copy's `Assets/DLC/Shared/Terrain/TerrainDef.csv` are as
  stated.
- **Not checked against data:**
  - the 35,000 threshold and the mountain-top swap;
  - the edge-weight pattern;
  - the f32 order of the barycentrics. They are checked by the port's comparison against the taps (8.16), not by a
    separate number.

### 8.16 The coast-strip port as built, and its API for 4913 (ticket 4911, 2026-11-20) [C for the code and the tests]

Written from 8.15 (D83). Namespace `c5r::liveterrain`, header `render/include/c5r/liveterrain/coaststrips.h`, source
`render/src/liveterrain/coaststrips.cpp`, library `c5render`, C++14, no D3D and no I/O.

**Inputs** (all built by the caller at load; the game's files are read at run time):

| Type | What | Source in the server |
|---|---|---|
| `StripLines` | per coast, the five offset lines of `StripPoint` {x, y, t, styleA, styleB} | W1's `CoastCurves::lines[i].mesh[0..4]` (4910), in its order (the EXE's coast order) |
| `StripStyles` | the 13 kinds' weights, heights and band terrain codes, with `present` flags | W1's `CoastStyles` (CoastStyles.xml): weight = its `flag`, height, bands (none, `terrainHigh`, `terrainLow`, none) |
| `StripMaterials` | the TerrainDef rows' ids, row k = material k | `StripMaterials::fromLayerTables` over `c5r::bake::loadTerrainDef` |

**Calls.**
- `buildStripMesh(lines, styles, materials, mesh)`, per coast, once per load. It builds 8 vertices per column and
  8 triangles per quad, cuts them into segments, and records the box. An absent kind takes the all-zero profile.
- `applyStrips(meshes, materials, field, stats)`: the raster over a `StripField` window, which is a dense rectangle of
  the field that already holds the blend. The window must not cross the seam; a seam-crossing window is two calls.
  - The order is the EXE's: every even segment of every mesh, then every odd one.
  - Only window samples are read or written. A sample depends only on its own value and the triangles over it, so a
    window gives the same samples as the whole field. The test `raster_window_equals_whole` checks this.
- `stripTouches(mesh, field)`: whether a mesh's box reaches the window, with the x wrap. Meshes that do not reach it
  may be left out, as long as the others keep their order.
- `specialPick(meshes, randState, pick)`: the EXE's two step-6 draws (MSVCR90's generator). They count for the stream
  only; the pick changes nothing (8.15).

**For 4913 (the producer's change step).** For a change that moves a coast:
1. Make the new curves with W1's `buildCoastCurves` and `buildStripMesh` for every coast.
2. Take the window the producer computes.
3. Fill it with the new block blend, the new region fill included (W1's `regionField`).
4. Call `applyStrips` with the meshes whose box touches the window.
5. Write the window.

Rivers stay protected as before (MC #8420 (d)): the river carve runs after the strips in the EXE, so near a river the
current field's samples are kept. The marsh replay runs before the strips in the EXE (8.15), so the strips' materials
may cover marsh samples there, just as a reload does.

**What a reload would pick instead (acceptance (2)).** The special segment is not modelled because it changes nothing.
A reload draws the coast and the segment again from its own stream position: `rand() % coasts`, then
`rand() % (segments / 2)`. There is no visible difference.

**Tests** (`render/tests/liveterrain/coaststrips/coaststrips_test.cpp`; 2026-11-20 16:30, workstation; 16 run,
0 failed, 0 skipped):
- the material resolution (continent 1 first, then generic) and the swap pairs;
- the TerrainDef rows from `LayerTables`, with negative tests;
- the mesh's shape, the midpoints, the last column and the triangle order;
- the segment rule over L = 3..399;
- the style switch, the parity and the "style A only" re-read;
- an absent kind;
- uneven lines (refused, output untouched) and short lines;
- a straight strip: w = 1 in bands 1-2, the band materials, the band-0 ramp, nothing beyond lines 0 and 4;
- a window equals the whole field;
- determinism, and that the wave order differs from plain order where segments overlap;
- the mountain swap at and above 35,000;
- the x wrap and the clip;
- every refused input leaves the field untouched;
- `specialPick` against the CRT sequence of `srand(1)` (41, 18467), the one-segment case and no meshes;
- `game_terrain_def` on the install's TerrainDef.csv: code 5 resolves to row 3, and the swap 10 -> 9 is America's only
  one.

**Offline runner** (`lt_strips_run`, same folder; inputs from `render/evidence/0615/4911/prep.py`, which reads
CoastStyles.xml from the install's TerrainModels.fpk at run time). It applies the strips of a curves file to a recorded
field and writes the result and a cover mask: 1 where a band 1-2 triangle covers the sample, where the height does not
depend on the field underneath.

**The check against the taps (acceptance (3)) [L offline, 2026-11-20 17:15-17:35 EST, workstation].**
- **Inputs.**
  - W1's `buildCoastCurves` (w1-4910 at 81df89010, 4910's 8.10.2) on 4902's recorded inputs (Tiny t8 and huge t350,
    prepared by `render/evidence/0465/4903/prep.py`), with the saves' `mapRandomSeed` (4912): Tiny 0x3DFC9816, huge
    0x067F5502.
  - The coast styles and character splines are read at run time from the pre-update `pure_vp` copy's
    TerrainModels.fpk (D200), through W1's `CoastStyles` and the adapter of this section. TerrainDef.csv comes from the
    same copy.
- **The run.** The strips were drawn with the tap as the base, and only the covered (band 1-2) samples were compared,
  because those do not depend on the base.
  - The scripts are `render/evidence/0615/4911/run_check.sh`, `compare.py` and `lt_curves_dump.cpp`. The dumper ran
    from a local, unpushed merge of both branches; outputs are in `C:/c5-64bit-work/0615/w2/check/`.
  - Tiny: 13 coast lines, 157,184 triangles; build 2.3 ms, raster 114 ms (DIAGNOSTIC, single run).
  - huge: 53 lines, 448,000 triangles; build 6.6 ms, raster 266 ms (DIAGNOSTIC).

  | Map | Covered samples | within tol 1 | exact | material equal | the tap more than 1 lower |
  |---|---|---|---|---|---|
  | Tiny, all | 119,404 | 99.02 % | 98.93 % | 99.76 % | 0.87 % |
  | Tiny, farther than 100 samples from a river-edge plot | 112,815 | **99.94 %** | 99.86 % | 100.00 % | 0.05 % |
  | huge, all | 339,910 | 98.32 % | 98.22 % | 99.56 % | 1.33 % |
  | huge, farther than 100 samples from a river-edge plot | 307,613 | **99.86 %** | 99.77 % | 99.99 % | 0.11 % |

- **Reading.**
  - Away from rivers, the strips reach 8.3's inland tol-1 shares: Tiny 99.94 % against 99.92 %, huge 99.86 % against
    98.3 %. Most samples are exact, which supports the f32 order of 8.15.
  - Near rivers the tap is lower: the river carve (step 7) runs after the strips and is not ported. Those samples stay
    protected (MC #8420 (d)). Of the misses, 974 of 1,034 (Tiny) and 4,178 of 4,509 (huge) lowered samples lie near a
    river-edge plot.
- **Lakes.** These are the covered samples within 96 samples of each lake plot's centre:
  - Tiny's lake (45, 11): 588 of 588 samples exact (100 % within tol 1).
  - Tiny's lake (34, 16): 520 samples, 100 % within tol 1.
  - Tiny's lake (35, 10): 90.7 %. Every miss is the tap lower, from a river at that lake.
  - huge: 33 lake plots. 25 are at 100 % within tol 1, for example (44, 24) with 510 samples. The rest have a river
    nearby (most misses are the tap lower), for example (104, 27) at 80.0 % with 16.5 % lower.
- **Not verified:**
  - The covered samples where two coasts' strips overlap within one wave (the EXE's race, 8.15) are compared like the
    others. The remaining misses away from rivers (0.06 % Tiny, 0.14 % huge) were not attributed; see the known
    difference below. *Corrected 2026-11-20 17:50: was "0.05-0.25 %".*
  - Band 0 and band 3 samples were not compared, because their values depend on the blend underneath. They need the
    producer's own blend with W1's region fill (4913). *(2026-11-20, 4913, 8.17: in a live window they sit on the
    producer's own pieces, whose variants differ from a reload's (way 3), so they cannot be separated there either;
    8.17 gives the window's tol-1 shares as a whole.)*

### 8.17 The land/water step as built (ticket 4913, epic 0615, 2026-11-20) [C for the code and the tests, C offline for the runs on recorded inputs]

Written by w1-4913 from 17:35 EST. Our own design on 8.10.1 (4910) and 8.16 (4911); nothing here is taken from the EXE.

**Not in server.exe until 4914 (1) (w6-4914b, 2026-11-20 21:35 EST) [C, `git grep enableLandWater` on the team branch
@4118a39fa and on w1-4913, w2-4914, w3-4912, w4-4915, w5-0615].** Every number of this section is `lt_producer_run`'s
(the render test runner), not server.exe's. `Producer::enableLandWater` was called only by `producer_test` and
`lt_producer_run`; the server's adapter (`server/src/render/srv_liveterrain_c5r.cpp`, `C5rProducer::create`) called
`Producer::create` alone, so in server.exe, live and in the scene player (both use the same factory), every land/water
change still waited with `Water`, also with the map seed known. The wiring is 4914 (1), the paragraph "The server's
land/water inputs" below.

**Carriers of the numbers in 8.10.2 and 8.17 (lead3-0615 / MC #8826, 2026-11-20; marked by w6-4914b).** A number is
only as good as the binary that made it:
- `lt_region_run` (render test runner, `render/tests/liveterrain/coastlines/`): 8.10.2's coast-adjacent counts
  **47,708 / 47,708** (Tiny) and **148,700 / 148,700** (huge), the 37 counts, the seed checks and the costs.
- `producer_test` (render unit tests): the test counts of this section (35/35, then 40/40 with `protectWonders`).
- `lt_producer_run` (render test runner, through `render/evidence/0615/4913/run_case.sh`, W5's `item3.sh`,
  `nw_runs.sh`): every row of the offline table below (window samples, the water-mark agreement, the coast-moved
  matches, heights at tol 1, the line counts, samples outside, ms), the per-case cause table, (b) and (b.3), and the
  huge own case (34, 30).
- `test_liveterrain` (server unit tests on the producer stub): the land/water wiring of 4914 (1) only; it says nothing
  about the coast a real producer draws.
- server.exe: nothing of this section yet. The first server.exe numbers come from the land/water smoke of 4914 (1)
  (`server/evidence/0615/4914/smoke_4914.sh`, below) and then I2 and 4915 (11.1).
- live: nothing yet (the I1 runs of 6.11 / 6.12 had the server waiting `Water`).

**The server's land/water inputs (ticket 4914 (1), w6-4914b, 2026-11-20 21:40 EST) [C for the code and the stub tests;
server.exe not built yet at this writing].**
- **Where.** `MakeInstallLandWaterEnabler` (`server/src/render/srv_liveterrain_scene.cpp`) gives the producer factory
  (`MakeC5rProducerFactory(library, enabler)`, `srv_liveterrain_c5r.cpp`) a function that, on the worker right after a
  load's `Producer::create`, turns 4913's step on with `enableLandWater(styles, materials, mapRandomSeed)`. The live
  scene and the offline player pass the same enabler.
- **Inputs, read once per factory from the player's install at run time (D83):** `coaststyles.xml` and the character
  spline files it names, through `c5r::gamefs` (the install's index, by bare lower-case name: in
  `Resource/Common/TerrainModels.fpk`, 8.10.2), and the TerrainDef through `bake::loadTerrainDef` on the same index
  (the residency's own route, `srv_residency_source.cpp`), made into `StripMaterials` (8.16). No mod roots are mounted,
  as for the residency's TerrainDef. The index costs about 1.3-1.5 s (`srv_render_scene.cpp`'s note); it is paid once,
  on the worker, never on the render thread.
- **Which thread, and can it stall the render thread? [C, the code read 2026-11-20 23:10; L for the times].** The
  read runs inside `Producer::create`'s job, on `LiveTerrain`'s own worker thread (`LiveTerrain::run`, kCreate), the
  thread that makes every load's producer. It is once per factory, so once per server process (the smoke's 1.4-2.0 s
  were paid in every process because each process is a new server.exe); a second load in the same process pays only
  `enableLandWater` (about 20 ms on Tiny). The render thread never waits for it in play: `onField` only posts the job,
  `takeResult` takes the lock for a queue pop, and land/water changes posted meanwhile queue behind the create, as
  every change does while a producer is being made (they wait seconds, not frames). The enabler's own mutex is taken
  only by the worker (and the player's thread below). The one wait on the render thread is `~LiveTerrain` (the
  scene's end at shutdown), which joins the worker and so waits out a read in progress (at most the 1.4-2.0 s
  measured). The offline player (`liveTerrainReplayInit`) reads synchronously on its own thread, and only for a
  journal with the land/water mark.

  **Measured (MC #8868 (1)) [L, the smoke's render.log, 2026-11-20 22:49-23:02, c5h-a, Tiny; DIAGNOSTIC].** Per
  process: the field hand-over (the seed line) is the start of the read; the residency's "first drawn frame of load 3"
  comes 0.31-0.45 s after it; the read ends ("land/water inputs read") 1.07-2.38 s after that first frame:

  | process | hand-over | first drawn frame | inputs read (ms) | read ends after the first frame | render-thread draws in the read's seconds (host 1s) |
  |---|---|---|---|---|---|
  | 0 (load) | 22:49:32.670 | 22:49:33.117 (frame 27) | 1971 | 2.38 s | +1 (to 33.18), +16 (34.24), +18 (35.29); then 26, 16, 27 |
  | 1 (lw) | 22:52:40.565 | 22:52:40.871 (frame 47) | 1438 | 1.51 s | +11 (41.23), +39 (42.25); then 32, 26, 28 |
  | 2 (wl) | 22:57:37.737 | 22:57:38.113 (frame 49) | 1615 | 1.73 s | +5 (38.40), +17 (39.43); then 26, 29, 29 |
  | 3 (coast_land) | 23:02:06.311 | 23:02:06.620 (frame 47) | 1133 | 1.07 s | +2 (06.68), +34 (07.71); then 34, 32 |

  (draws = the `host 1s` line's `draws` counter, difference to the line before; 0 in every line before the first
  frame. Scratch: `C:/c5-64bit-work/0615/w6/draws.py`.) The first second after the first frame is low in every
  process (the residency's catch-up after the load), and the seconds the read covers draw 16-39 frames.

  - **Which thread [L].** Not the render thread: it drew 16-39 frames in each second the read covered (39 in process 1,
    more than its 26-32 per second afterwards); a read on the render thread would leave a gap of 1.1-2.0 s with no
    draw. So the read runs beside the render thread, on the worker, as the code says.
  - **Load screen or first frames of play [L, with the harness's timing].** In these runs the read did **not** fall
    inside the load screen: the server's first frame of the load came 0.3-0.45 s after the hand-over, and the harness's
    `drive.py ingame` saw "load screen hidden" at 22:52:40-42 for process 1, while the read ran 22:52:40.57-42.38. The
    read overlaps the first 1-2.4 s of play; it costs no frames (above). What it delays is the producer: "producer made"
    came 1.4-2.9 s after the hand-over, against 0.25-0.52 s in I1 without the step (`live_lines_i1_tiny.txt`), so any
    terrain change (height or land/water) made in the first ~3 s after a load waits that much longer before it is
    drawn. A human load screen is usually longer (the player clicks it away), so in play the read is normally done
    before the first change [I]. Not shown: a run with the load screen held until a click (the harness's auto-load
    dismisses it); an item for I2.
  - **huge, DIAGNOSTIC [C offline, `lt_producer_run` of w6-4914b's render build on 4902's huge tap, the pure_vp
    install copy (build 4390913), W1's extracted coast styles; 3 runs each, busy workstation].** `create` alone 755 /
    800 / 1,009 ms, with `enableLandWater` 1,049 / 1,071 / 1,549 ms: the step adds about 270-540 ms on huge (Tiny live:
    18-22 ms). The input read itself (the install's index, 1.1-2.0 s above) does not depend on the map [I: the index
    scans the install's packages, not the map]. So on huge the worker's extra time per load is about 1.4-2.5 s in the
    first load of a process and 0.3-0.5 s in later loads; I1's huge "producer made" was 0.8-1.0 s without the step.
- **When.** Only with the load's map seed (4912) and `LoadIn::landWater` (the live scene: on unless the **test switch**
  `C5_RENDER_LIVE_LANDWATER=0`; it is not an option and has no VP64Options key, K4; the player: the journal's mark).
  - **The switch-off is complete (MC #9538 item 4, W10, 2026-11-21 16:16) [C, the code and the tests; server.exe link in
    the hand-back, live not run].** Until then, with the switch at 0, two things of the step still ran: the map seed
    read (with its LOUD line and D118 report when the read failed), and the ice refresh of 10.9 on every live window.
    Now both are gated on `LoadIn::landWater`:
    - **The seed.** The scene reads no seed and logs one plain line: `MapSeedNotWanted`, "map seed not read
      (C5_RENDER_LIVE_LANDWATER=0 ...)". There is no LOUD line and no report.
    - **The adapter.** It knows the step is not wanted, so a land/water change reaches the producer. Without
      `enableLandWater` the producer waits with its own `Water`, as before epic 0615, not with 4912's "no map seed" wait.
    - **The ice.** `IceWavesLoad` sets `IceWaves::setFieldRefresh(false)`, so a live window owes no `Ice_Reload`
      (10.9). Height-only changes therefore behave as before 0615.
    - **Tests:** `test_liveterrain` case `land_water_off`; `test_icewaves` case `fieldrefresh_off`.
- **Failure (D118).** Inputs that cannot be read: ONE LOUD line per factory ("the land/water step's inputs cannot be
  read from the install"); `enableLandWater` failing: one LOUD line for that load. Either way the load's land/water
  changes wait with their own reason **`LandWaterInputs`** (not `Water`) and the reason text; height changes go on live;
  no crash path.
- **Log lines.** Once per process: `live terrain: land/water inputs read in N ms (the install's index M ms; coaststyles.xml
  from ...)`. Per load: `live terrain: land/water step on for this load (enableLandWater N ms, map seed S)` and the
  producer line gains `; land/water changes can go live (ticket 4914)`.
- **Tests [C].** `test_liveterrain` case `land_water` (32 checks; suite 229, 0 failed, plain and ASan, 2026-11-20 21:35):
  the enabler called once with the seed; not called without the seed, with the switch off or without an enabler (the
  4912 `Water` wait unchanged); a failure gives `LandWaterInputs` with the reason while a hills change stays live; the
  journal's mark and its trailer (9.4); the replay calls the enabler only on the mark.
- **Cost per factory load on huge (MC #8826 (e)):** offline only, DIAGNOSTIC: `enableLandWater` adds 270-540 ms to a huge
  load's `create` (the bullet "huge, DIAGNOSTIC" below); a live huge load is I2's. On Tiny (the smoke below, DIAGNOSTIC, single loads on the busy workstation): the inputs
  1438-1971 ms per process, of which the install's index 1437-1962 ms; `enableLandWater` 18.3-21.9 ms per load; both
  on the worker, before "producer made" (1409-2912 ms after the field).

**The land/water smoke on the product path (MC #8826; EFFECTIVENESS 14) [L, 2026-11-20 22:40-23:05 EST, workstation,
c5h-a] — the first server.exe and live numbers of this section.**
- **Setup.** Goldberg instance c5h-a (C:, mods548: CP + VP + EUI 5.4.8, Vibrant Textures; EXE build 4390913, stamp
  0x546CD0A8), claimed by w6-4914b for 0615 (MC-B's grant through lead3-0615). Layout `C:/c5-64bit-work/0615/w6/layout`
  from `smoke_4914.sh build` at w6-4914b @521b26d58 on the 0.1b pins (`keep/pins-0.1b`, release; server.exe
  e90c55c1..., proxy 1f014b36..., composite 860e0098..., the pins' x64 DLL b84aa3e9...); `run_3308.py prep` (D33 minus
  Overlay, `[Renderer] ServerRenderer = 1`), 4901's composite keys, the recorder through `server_cfg_extra` (TEST ONLY).
  Save: copy `C5H-0615-4912-W6PL_0008` of `c5h-9029-tiny-t0008` (SHA-256 b2ceb663...c29163, seed 0x3DFC9816).
  Driver `server/evidence/0615/4914/smoke_4914.sh` (prep, run, end, verdict): process 0 (load, `lt.py setup`, save),
  then lw, wl and coast_land, each in a new process from the save before, with a scene bundle per process.
- **Result: every change drawn live, without a reload; SMOKE PASS** (`smoke_4914.py verdict`;
  `server/evidence/0615/4914/smoke_lines.txt` and `smoke_verdict.json`, local paths replaced):

  | case (plots) | render thread (apply line) | EXE field write | residency tiles compared / changed | `Waves_Reload` |
  |---|---|---|---|---|
  | lw (53,13) (55,15) | 52.0 ms (EXE write 7.5, windows 5.0; 2 rects) | 2 rects, 228,928 samples, 46,532 heights, 12,789 materials | 90 / 46 | ok, 31.2 ms |
  | wl (51,12) | 27.5 ms (EXE write 3.8, windows 1.3; 1 rect) | 1 rect, 315,723 samples, 107,012 heights, 16,588 materials | 100 / 58 | ok, 23.9 ms |
  | coast_land (49,11), picked | 22.9 ms (EXE write 4.7, windows 1.7; 1 rect) | 1 rect, 379,500 samples, 114,757 heights, 15,273 materials | 132 / 63 | ok, 23.3 ms |

  - Times are DIAGNOSTIC (one change each, busy workstation, no quiet lock). lw's window is 228,928 samples, the same
    as `lt_producer_run`'s lw row above. wl and coast_land start from other states than the rows above (after lw and
    after wl), so their windows differ.
  - **The gate (0586) on land/water, first live run [L]:** each case logs "the coast waves wait for the reload: N of
    the N plot(s) whose wave class changed wait in the field" while the change waits for its apply, then, after the
    apply, "request posted: waves (272 ice plots, 0 to flip; N plot(s) of another wave class)", "answer: ice not asked
    ..., waves ok (Waves_Reload N ms)" and "new water tap copy requested"; the water tap's copy 2 is read 0.3-0.6 s
    later (hashes ok). No `Ice_Reload` was owed (no window beside the ice in these cases).
  - **Lines.** 0 LOUD in server.log and render.log of the 4 processes (the boot's interface-version line of 8.14.5 is
    gone on the 0.1b pins); vpproxy.log only the proxy's 5 known TEST ONLY `server_cfg_extra` lines. No dialog box at
    any check, no crash. End: the harness's stop, `frontend.py restore`, `run_3308.py restore` (`provision_verify_rc`
    0), `backup.py verify` of the pre-prepare backup rc 0; c5h-a released 23:05:49.
  - **Bundles** (`F:/c5-64bit-work/0615/4914b/smoke/bundles/W6P00..P03`): each journal of P01-P03 holds 1 job and 1
    generation (the live change) and the LWAT mark (`scenes_4912.py seed`: exit 0, `land_water` true). P01 and P02
    hold one water copy of their load; P03 holds two (`load0003-c2.wtap`, the copy after its `Waves_Reload`), so it is
    evidence only (9.4's gap). Their replay is a rule A run (a slot from MC-B through lead-0615): not run.
  - **Picture triplets for the owner (MC #8868 (2); DIAGNOSTIC, D112)** in `F:/c5-64bit-work/0615/4914b/pictures/`
    (`README.txt`; per case the originals and one labelled side-by-side `<case>_triplet_DIAGNOSTIC.png`): before
    (product), live without a reload (product), after a reload (stock, I1's chain); lw also the product's reload. Looked
    at [L]: lw shows the two plots as water live, with the river-edge plot's old bank kept as a straight strip along
    the river (known difference 1, 11.3) where the stock reload draws a smooth shore; wl shows the capital's bay
    narrowed to the new grass and the lake live; coast_land shows the bay's shore moved and the new grass, with the
    re-matched hills next to it (way 3's own piece variants). Limits: wl's stock reload picture sits about one plot off
    (I1's lake camera) and coast_land's about three plots east, with I1's lake (53,16) in its state (I1 ran coast_land
    after lake). Not a measure.
  - **Finding: a ridge of the old shore inside lw's new water (w6-4914b, 2026-11-20 23:24; to the lead and the MC
    the same hour, MC #8770 (1)).** Picture `F:/c5-64bit-work/0615/4914b/pictures/lw/lw_ridge_zoom_DIAGNOSTIC.png`
    (before, live, the product's reload, the stock reload; one camera). Live, a thin raised line runs through the new
    water along (53,13)'s old east shore; neither reload shows it.
    - [C, the smoke's fields and `lt_producer_run`; scratch `C:/c5-64bit-work/0615/w6/ridge.py`] 232 samples east
      of (53,13) (nearest plots (54,14) 88, (54,12) 68, (53,13) 45, (54,13) 30; samples x 3412-3491, y 842-1053) are
      land live where the reload has the water mark, a median 2,843 u16 (about 2.2 world units) above the reload's
      ground; 192 keep the load's heights exactly; all lie inside the written window. server.exe's live field equals
      `lt_producer_run`'s offline lw (W1's 4906 inputs) on 99.5 % of the heights and 99.9 % of the materials, so it
      is the producer's output. These samples are part of the lw row's 98.9 %, whose misses had no attribution.
    - **Cause [C, measured by w7-ridge, 2026-11-21 00:00 EST; offline, workstation]: all 232 are the river keep, made
      wider by the server's river flag.** *Replaces W6's [I] of 2026-11-20 23:24: "67 of the 232 are the river keep, the
      rest lie past the 64-sample ring (weight 0)".*
      - Method. The producer now reports per window sample its weight before and after the feather, why a sample is
        kept and whether it counts as moved coast (`Producer::Options::diagnostics`, offline only, `lt_producer_run
        --diag-out`). The smoke bundle `W6P01`'s journal (lj1) was decoded into `lt_producer_run` inputs:
        - the baseline and job 1, made into plot tables by the same rule as `PlotTableFromFeed`;
        - the journal's constants block and map seed 0x3DFC9816;
        - the smoke's before field as the tap.

        Scratch: `C:/c5-64bit-work/0615/w7/journal2plots.py`, `run_case.sh`, `ridge7.py`, `keepwho.py`. The offline
        run reproduces server.exe's live field **exactly**: 0 of 9,175,040 samples differ, in heights and in materials.
        Binary: `lt_producer_run` of w7-ridge, with the fix switched off.
      - Weights. All 232 have weight 1 before the feather: they lie in the full set, not in the ring.
        - 192 are kept by the river keep (weight 0).
        - 40 are feathered towards those kept samples (weight at most 0.25).
        - A test run with a 128-sample ring (`--ring 128`) leaves the same 232 samples. So widening the ring costs
          time and fixes nothing.
      - Why the product keeps more than W1's offline lw. The server's plot table sets `riverEdges = 7` for every plot
        whose feed record carries the river flag (`PlotTableFromFeed`, `server/src/render/srv_liveterrain.cpp`; flag
        bit 0 = "river on any edge", the DLL's isRiver). *Until ticket 0631 (2026-11-23, 8.18): the table now takes
        each plot's own edge bits from the feed.* W1's chain table carries the EXE's own edge bits (correction,
        2026-11-23, 0631: they are the DLL's bits, read by 4902's `live.py` through Lua `IsWOfRiver` etc.). So in the
        product, (53,13) and (53,12) are river-edge plots too:
        - (53,13) is the changed plot; after the change it is coast, and the DLL keeps its river flag (the smoke's
          `01_lw_apply.json`: `river` true).
        - In W1's table both plots have riverEdges 0.

        The 192 kept samples lie in the circles of (53,13) (129 samples), (53,14) (68) and (53,12) (53). Some lie in two
        circles. All lie in hexes that are water after the change, by the producer's own cell test (reason 5 in the run
        with the fix). By nearest centre they are W6's (54,14), (54,12), (53,13) and (54,13).
        With W1's table (offline, the EXE's edge bits) the same keep gives 67 kept samples and 27 feathered ones:
        94 samples, W1's 95 lw misses.
      - W6's plot centres were right: `centres.txt` (W5's conversion) equals the producer's `hexToSample` (0.00 samples
        apart on all 28 river plots compared).
    - **Fix (w7-ridge, 2026-11-21; MC #8880, lead-0615 #8899, MC #8905) [C offline].** In a land/water change the river
      keep no longer holds a sample whose hex is water after the change. The river carve it protects lies on land;
      in the water the region fill and the strips draw the ground (`producer.cpp`, the keep loop; diagnostics reason 5).
      Land hexes keep the river keep unchanged. In every run below, each sample that lost the keep carries the
      producer's own reason 5 (its hex is water after the change). W6's proposed redraw of the old strip footprint
      was not needed: the samples already had weight 1.
      - The negative: `Options::diagnosticRiverKeepOnWater` (offline only; `lt_producer_run --river-keep-water 1`)
        restores the old keep and reproduces the smoke's field exactly (0 samples differ).
      - With the fix, on the smoke's product inputs: **232 samples become 13**, none kept from the load. The 13 lie in
        water hexes, feathered (weight 0.32-0.49) towards land samples that are still in the river keep. Their material
        stays the old one (the material switches at weight 0.5), and their ground stands a median 149 u16 (about
        0.1 world unit) above the reload, against 2,843 before the fix.
      - On W1's lw (the EXE's edge bits) the misses drop from 95 to 11, all of the same feathered kind (reason 5,
        weight 0.32-0.49, 151 u16).
      - All cases: the table "The river keep fix, on and off" below.
  - **Not shown by the smoke:** a pixel comparison of the look against a reload, build 26577625, huge, the other land/water cases (lake, mouth,
    ice_land, large: I2 and 4915).
  - **Next live runs (W8, 2026-11-21).** Build 26577625: 11.2, "The land/water smoke on build 26577625". I2 on
    4390913 (mouth, huge, Gibraltar h23): 11.5. Both use the w7-ridge fix.
  - **Live: the ridge is gone (W8, w8-sat) [L, 2026-11-21 10:53-11:05 EST, workstation, c5h-a, build 4390913; DIAGNOSTIC:
    other games ran on the machine].**
    - **Setup.** Layout `C:/c5-64bit-work/0615/w8/layout` at w8-sat @6eebdbc53: the team branch @e9b328dab with the
      w7-ridge fix, plus main 2f395cc6d. Built on the 0.1b pins: server.exe
      81f10ca8cc6ee3a18d8b35c56bcf989af332eba860fbe547c1d78c90f0658698, proxy 98a076cc..., composite 39eb8380...
      The prep is run_3308's, as W6's smoke, with copy `C5H-0615-4912-W8PL_0008` of `c5h-9029-tiny-t0008`
      (b2ceb663...).
    - **Run.** `smoke_4914.sh` with `C5_SMOKE_CASES=1`: process 0 (load, setup, save), lw in a new process, then a
      reload of the lw save with a capture at the same camera.
    - **Lines.**
      - lw drawn live: render thread 63.2 ms (EXE write 22.1, windows 4.8; 90 tiles compared, 49 changed).
      - The gate: "waves ok (Waves_Reload 26.4 ms)", then "new water tap copy requested".
      - 0 LOUD in server.log and render.log; no box, no crash.
      - Producer ready 1,552 ms after the field (process 0) and 3,322 ms (the lw process). The harness's load took
        longer than that, so the wait found it ready.
    - **The ridge (W6's method, scratch `C:/c5-64bit-work/0615/w8/post.py`).** Within 128 samples of (53,13) and
      (55,15), live land where the reload has the water mark:
      - **13 samples** (W6's live run: 232); 0 kept from the load; the median 149 u16 above the reload (W6's: 2,843);
      - nearest plots (54,14) 11, (52,11) 1, (54,12) 1;
      - the other way, live water where the reload has land: 3 samples;
      - the water mark agrees on 99.94 % of the near water samples.
    - **Live equals offline.** In the window (19,851 changed samples) the live field equals W7's offline `lt_producer_run` of the smoke's
      inputs with the fix (`C:/c5-64bit-work/0615/w7/runs/smoke_fix`) on 100.0 % of heights and materials.
    - **Pictures (DIAGNOSTIC, D112), `F:/c5-64bit-work/0615/4914b/pictures/w8-ridge-4390913/`.**
      - `lw_ridge_zoom_DIAGNOSTIC.png`: before, live, the product's reload, the stock reload (I1). It uses W6's crop
        box, (600,300)-(1150,705), and the same camera.
      - `lw_ridge_w6_vs_w8_live_DIAGNOSTIC.png`: W6's live picture and this one.
      - Looked at: the raised line in the new water is gone, and the shore runs smooth as in the reload.
    - **End.** run_3308 restore rc 0 (`provision_verify_rc` 0).
    - `smoke_4914.py verdict --cases 1` (labels fixed 11:2x): **SMOKE PASS**. Process 0, lw (step on, 1 apply, 4 gate
      lines), then the reload of lw with no change; 0 LOUD. *The first verdict with the four-case labels read FAIL
      because it took the reload folder for wl.*
    - The reload bundle's seed check exits 1: a process without a change [I: its journal holds no job].

**What changed in the producer** (`render/src/liveterrain/producer.cpp`, `producer.h`):
- `Producer::enableLandWater(CoastStyles, StripMaterials, mapRandomSeed)`, called once after `create`: the load's coast
  curves (`buildCoastCurves`) and every line's strip mesh (`buildStripMesh`; StripStyles from CoastStyles as 8.16
  says: weight = flag, height, bands none / terrainHigh / terrainLow / none). Without it a change of a plot's land/water
  class waits with `Water` as before; every 4903-4908 behaviour and test is unchanged.
- **The change step for a class change** (a changed cell whose class 0 / 1 / 2 changes: land to water, water to land;
  a lake that turns land to coast water is one):
  1. The new curves of the whole map from the new plot table. The **live line set** keeps every current line whose
     pre-jitter shape (`CoastLine::shape`, a hash of type, closed and the extracted points) is unchanged, with its
     current jitter, and takes the lines whose shape changed from the new build, in the new build's order
     (`assembleCurves` remakes the point array and the ranges).
  2. The window: the reach of the removed and re-matched pieces (as before), the changed plots (+-40 samples around
     each centre), and every point of the changed lines (old and new, all five mesh lines) that lies within
     `kCoastNearSamples` = 128 of a changed plot's centre; then the 64-sample ring. A window across the x seam is
     handled (two rectangles in the result, split at the seam); outside this mode the seam rule is unchanged.
  3. The region bytes of the window from the old and from the live curves (`regionBlocks`), the block blend before and
     after with those regions in place of the hex classes, and W2's `applyStrips` over the window (the meshes whose
     box reaches it, in the coast order).
  4. Kept samples: water protection is off in this mode (the region fill and the strips redraw the water); marsh as
     before; rivers as before **except** on the changed plots and on every sample whose region byte the change moves,
     which take the new ground whole (the feather towards kept river samples does not hold them back), and, since
     w7-ridge (2026-11-21), on every sample whose hex is water after the change (the smoke's ridge, below). Natural
     wonders' own hexes are kept, and near them the feather wins (`protectWonders`, (b.2) below; w5-0615,
     2026-11-20).
  5. The result names the coast lines: `coastLinesRedrawn` (shape changed), `coastLinesBeyondWindow` (of those, lines
     whose changed points reach beyond 128 samples: a reload moves them farther, not drawn) and
     `coastLinesReloadMoves` (lines whose shape is unchanged but whose jitter a reload would shift, 8.10.1: not drawn).
     **In the server's log (MC #9538 item 2, W10, 2026-11-21) [C, code and test; not seen live yet].** Until then these
     counts and `samplesWonderKept` stopped at the adapter. Now they reach `ChangeOut` (`landWater`, `coastLinesRedrawn`,
     `coastLinesBeyondWindow`, `coastLinesReloadMoves`, `samplesWonderKept`). A change with any of the last three gets
     one plain line from `LiveTerrain::takeResult`, live or not. It is not LOUD, because naming what a reload would move
     is not a fault (D118 is for faults). The line reads: "live terrain: load seq S change N: coast (live): R line(s)
     redrawn in the window, B of them also reach beyond it; M unchanged line(s) a reload would re-jitter (named, not
     drawn ...); K sample(s) kept on natural wonders' hexes". Test: `test_liveterrain` case `coast_counts`.
     The written window is exact for every sample inside it (samples outside are never changed: `samplesOutside` 0).
- **Why bounded, not whole lines**: a line whose point count changes gets a new jitter along its whole length, and
  a continent's coastline is far larger than a window (8.10.1). Live, the line is redrawn near the change only; at the
  window's edge the 64-sample ring blends it into the load-time line [I: a step of at most the jitter amplitude,
  about 14 samples, softened by the ring; to be seen in the I-session pictures].

**Tests** (`producer_test.cpp`, 2026-11-20 18:10 EST, 35 run, 0 failed; the 4903-4908 tests unchanged): a one-plot
lake goes live (material 37 at its centre, every changed sample inside the window, the same window on a second
producer) and back to land (material 11 again); without `enableLandWater` the class change waits with nothing
written; across the seam (two rectangles inside the field, nothing changed outside them); a change whose coast spans
more than 256 plots waits `TooLarge` with the producer untouched; a plot table of the wrong size is refused
(`BadLayout`); `enableLandWater` before `create` is refused.

**Offline against 4906's stock reloads** (Tiny `LT_0008`, mapRandomSeed 0x3DFC9816; the chain's states rebuilt from
its `_apply.json` files by `render/evidence/0615/4913/prep_chain.py`; the tap = the reload before the step, the
reference = the reload after it; `run_case.sh`, `pack.py`, `compare_case.py`; the piece library and TerrainDef of
c5h-d's install copy, the coast styles of 4910) [C, offline on recorded inputs, 2026-11-20 17:50-18:15 EST, workstation]:

| Case (plots) | live | window samples | water mark (37) agrees with the reload, live / tap | coast-moved samples within 128 of a changed plot: live matches | heights tol 1, live / tap | lines redrawn / beyond / reload moves | samples changed outside the window | ms |
|---|---|---|---|---|---|---|---|---|
| lw (53,13) (55,15), across the seam | yes | 228,928 | 99.81 % / 95.93 % | 8,787: **98.9 %** (99.87 % with the w7-ridge fix, 2026-11-21; the on/off table below) | 81.9 % / 81.7 % | 2 / 2 / 0 | 0 | 174 |
| wl (51,12) land, (51,13) lake | yes | 126,111 | 97.55 % / 90.93 % | 8,727: **81.8 %** (82.7 % with the w7-ridge fix) | 34.0 % / 30.3 % | 3 / 2 / 9 | 0 | 121 |
| large (7 plots) | yes | 378,925 | 99.94 % / 93.25 % | 25,393: **99.9 %** | 96.6 % / 86.1 % | 2 / 0 / 7 | 0 | 227 |
| lake (53,16), 4912 | yes | 92,160 | 99.26 % / 96.95 % | 2,803: **75.9 %** | 72.3 % / 69.3 % | 1 / 0 / 1 | 0 | 148 |
| coast_land (49,11), 4912 | yes | 122,560 | 99.80 % / 96.05 % | 4,573: **99.7 %** | 63.5 % / 79.2 % | 2 / 2 / 0 | 0 | 160 |
| mouth (52,13) to coast; the DLL clears (51,13)'s lake and (53,13)'s river, 4912 | yes | 411,240 | 98.10 % / 95.59 % | 9,118: **79.8 %** (81.0 % with the w7-ridge fix) | 41.5 % / 39.7 % | 3 / 2 / 10 | 0 | 369 |
| ice_land (53,5) to tundra, 6 ocean plots to coast, across the seam, 4912 | yes | 262,144 (2 rectangles) | 99.30 % / 98.28 % | 2,654: **100.0 %** | 95.4 % / 79.1 % | 2 / 0 / 12 | 0 | 180 |
| **huge** lake (101,22) flat plains to a one-plot lake, 4912 huge | yes | 323,456 | 99.59 % / 98.88 % | 2,311: **100.0 %** | 67.1 % / 60.5 % | 1 / 0 / 40 | 0 | 331 |
| **huge** coast_land (103,19) to grass; the DLL turns (104,18) ocean to coast, 4912 huge | yes | 305,152 | 100.00 % (99.998) / 98.65 % | 4,074: **100.0 %** | 72.3 % / 82.2 % | 4 / 4 / 0 | 0 | 383 |

**4912's huge cases (the two huge rows) [C, offline on recorded inputs; w6-4914b, 2026-11-20 about 23:12-23:16 EST, workstation;
carrier `lt_producer_run`].**
- **Inputs.** W3's huge stock recording of 6.12 (`F:/c5-64bit-work/0615/4912/huge/scenes/stock/`; save
  `C5H-VP8P-HUGE_0350`, c499d3c6..., seed 0x067F5502): the tap of step k is its before field, the reference the next
  field (lake: `02_coast_land_before_field.npz`; coast_land: `03_reload_field.npz`). The base table is 4902's huge
  tap of the same save (`C:/c5-64bit-work/keep/liveterrain-derived-4903/huge/plots.txt`; its h16 equals W3's load
  field on all but 24 of 44,040,192 samples, materials on all); wonders from `F:/c5-64bit-work/0465/4902/huge/huge_plots.json`.
- **Runs.** W1's chain unchanged (`prep_chain.py`, `run_case.sh`, `compare_case.py`), `lt_producer_run` built from
  w6-4914b's render tree (`render/build.py --target lt_producer_run`; the producer as on the team branch, with
  `protectWonders`; no wonder in either window), the pure_vp install copy (build 4390913; piece library
  86faac1e...), W1's extracted coast styles. Scratch `C:/c5-64bit-work/0615/w6/huge_rows.sh`, `huge/k1`, `huge/k2`.
- **Outside the window: 0 samples changed** in both. The reload's own re-roll outside the window (tap against
  reload, tol 1): lake 6,406,287 samples, coast_land 1,271,503 (6.12's numbers again).
- **Misses: none.** Every coast-moved sample within 128 of a changed plot has the reload's water mark live (lake
  2,311 of 2,311; coast_land 4,074 of 4,074); no river lies within that span of either plot. The farther measure
  (every coast-moved sample in the window, `coast_moved_matched_live`) is 63.7 % (3,629 samples) for lake and 99.9 %
  (4,128) for coast_land: lake's 40 lines that a reload re-jitters (`coastLinesReloadMoves`) move coast beyond 128
  samples that the live step does not redraw (known difference 5).
- Heights at tol 1 inside the window are low for the same reason as on Tiny (the reload re-rolls the pieces; way 3,
  D194); the water mark is the measure.

**4912's new Tiny cases (rows marked 4912) [C, offline on recorded inputs; w5-0615, 2026-11-20 20:15-20:21 EST, workstation].**

- **Inputs.** W3's I1 stock chain `F:/c5-64bit-work/0615/4912/scenes/stock/`, with the same save as 4906 (SHA-256
  b2ceb663...). The steps are 1 lw, 2 wl, 3 lake, 4 coast_land, 5 mouth, 6 ice_land, 7 large, 8 the reload of 7.
- **Runs.** As above, with `prep_chain.py` (base: the kept folder's `tiny/plots.txt`), `run_case.sh` and
  `compare_case.py`. The tap is step k's before field; the reference is step k+1's before field.
  - Producer: branch w5-0615, `protectWonders` on. No wonder lies in these windows (`wonderKept` 0), so the numbers
    equal W1's producer.
  - Scratch: `C:/c5-64bit-work/0615/w5/item3.sh`, `miss.py`, `i3/k3..k6`.
- **Outside the window: 0 samples changed** in every case.
- **The reload's own re-roll outside the window** (tap against reload, tol 1): lake 1,109,798 samples, coast_land
  242,242, mouth 1,549,980, ice_land 1,763,669.

**Each miss and its cause.** A miss is a coast-moved sample within 128 of a changed plot where the live water mark
differs from the reload's. Samples are given to the nearest plot centre.

- **lake: 675 misses, all of them the river keep (a).**
  - The lake lies beside the river plots (52,14), (53,14), (52,15), (51,15), (51,16) and (52,16).
  - The misses fall on (52,16) 226, (52,15) 188, (51,15) 129, (51,16) 64, (52,14) 53 and (53,14) 15.
  - Every one of them is a sample the producer kept from the load (within 60 of a river-edge plot's centre).
  - 360 keep the old water where the reload has land, and 315 keep the old land where the reload has water.
  - What the player sees: the lake's shore next to the river keeps its load-time line, about one plot wide, until the
    next load.
- **coast_land: 14 misses.** 8 are redrawn samples at (51,12) and (51,11), where the reload's line differs by a
  sample or two (its jitter: the 2 changed lines reach beyond 128 samples). 6 are the river keep at (52,12) and
  (52,13).
  - Heights at tol 1 are lower live than in the tap (63.5 % against 79.2 %): the live re-match places its own piece
    variants on the changed plot's neighbours, where this reload kept the load's (way 3, D194).
- **mouth: 1,843 misses.**
  - 1,545 (84 %) are the river keep. The misses fall on (52,14) 494, (52,15) 316, (53,14) 236, (53,12) 166,
    (52,13) 153, (51,13) 133, (53,13) 114 and (52,12) 53.
  - Of those, 222 lie on (51,13) and (53,13). The DLL changed only the lake and river flags of these two plots, so
    their pattern cell does not change and they are not "changed plots" for the producer. They are kept like any
    plot within 60 samples of a river-edge plot.
  - The remaining 298 (16 %) are redrawn samples where the reload's lines lie elsewhere: 2 changed lines reach beyond
    the 128-sample span, and the reload re-jitters 10 unchanged lines.
  - Input limit [C, the script]: `prep_chain.py` takes no river-edge change from the `_apply.json` (it records only
    `river` true/false per plot, not the edges). So these runs keep the load's river edges, including the one the DLL
    removed at (53,13). In the product, the DLL's own plot table gives the producer the new edges, so the keep there is
    smaller [I: not run]. *Settled 2026-11-23 by ticket 0631 (8.18) [C offline]: the DLL clears `(52,13)`'s own bits
    (the edge it shared with `(53,13)`), and with those edges mouth keeps 56,222 river samples (A 56,395) and misses
    1,675 (A 1,732).*
- **ice_land: 0 misses.** The window crosses the x seam (two rectangles). The 6 ocean plots turning coast and the new
  tundra plot are drawn as the reload draws them.
- **Summary.** Away from rivers the new cases match the reload's coast on 99.7-100 % of the moved samples. Next to
  rivers (lake, mouth) the misses are the named river keep (a), plus in mouth the reload's farther and re-jittered
  lines.
- **Every miss has a named cause, per case** (added 2026-11-20 20:40, lead3-0615, MC #8770):

  | case | misses | river keep (a), known difference 1 | reload's own lines (jitter beyond the 128-sample span, re-jittered lines), known difference 5 | other |
  |---|---|---|---|---|
  | lake | 675 | 675 (100 %) | 0 | 0 |
  | coast_land | 14 | 6 (43 %) | 8 (57 %) | 0 |
  | mouth | 1,843 | 1,545 (84 %), 222 of them on the two flag-only plots | 298 (16 %) | 0 |
  | mouth with the w7-ridge fix (2026-11-21) | 1,732 | at most 1,434: 111 fewer than above; the fix changes only river-keep samples on hexes that are water after the change and the feather next to them [C] | 298 [I: not re-attributed] | 0 |
  | lw (W1's table; w7-ridge, 2026-11-21) | 95 | 94: 67 kept, 27 feathered [C] | 0 | 1 not attributed |
  | lw with the w7-ridge fix | 11 | 11: feathered towards the river keep on land [C] | 0 | 0 |
  | wl with the w7-ridge fix | 1,513 (1,588 without) | 75 fewer, by the same kind of samples [C]; the rest as (a) below | - | - |
  | ice_land | 0 | 0 | 0 | 0 |
  | huge lake (w6-4914b) | 0 | 0 | 0 | 0 |
  | huge coast_land (w6-4914b) | 0 | 0 | 0 | 0 |

  lw (W1's row, 98.9 %) had no attribution. Its live field on server.exe (the smoke) holds 232 samples of the old
  shore as a raised line in the new water east of (53,13). All 232 are the river keep, 192 kept and 40 feathered
  (w7-ridge, 2026-11-21). *Was (w6-4914b, 2026-11-20): "only 67 of them within the river keep".* See the smoke's
  "Finding: a ridge of the old shore" below. With W1's table the lw row's 95 misses are all the river keep (a): 67
  kept and 27 feathered, the 95th not attributed. With the w7-ridge fix 11 remain, all feathered towards the river
  keep on land.

  **The river keep fix, on and off (w7-ridge, 2026-11-20 23:58 to 2026-11-21 00:12 EST) [C, offline on recorded inputs, workstation].**
  - Binary: `lt_producer_run` of w7-ridge, with the fix on and with `--river-keep-water 1` (off).
  - Inputs, read only: W1's (lw, wl, large), W5's (lake, coast_land, mouth, ice_land) and W6's (huge lake, huge
    coast_land) chain folders, against their stock reloads. Scratch `C:/c5-64bit-work/0615/w7/cases.sh`,
    `cmp_onoff.py`, `keepdiff.py`, `cases/`.
  - Each Tiny case runs with two river tables:
    - A: the chain's own table (the EXE's edge bits).
    - P: product-like, riverEdges 7 on every plot with the feed's river flag (from the smoke journal's baseline, the
      same map). P keeps the load's flags in mouth, where the DLL clears (53,13)'s river.
  - Columns:
    - "misses": coast-moved samples within 128 of a changed plot whose water mark differs from the reload
      (compare_case.py's measure).
    - "changed": samples the fix changes.
    - "closer on / off": of those, how many are nearer the reload's height with the fix on, and how many with it off.

  | case | table | near coast-moved | misses off -> on (matched on) | water mark in the window off -> on | heights tol 1 off -> on | changed | closer on / off |
  |---|---|---|---|---|---|---|---|
  | lw | A | 8,787 | 95 -> **11** (99.87 %) | 99.81 % -> 99.85 % | 82.4 % -> 82.4 % | 4,970 | 2,994 / 1,975 |
  | lw | P | 8,787 | 237 -> **16** (99.82 %) | 99.75 % -> 99.85 % | 80.7 % -> 81.3 % | 8,475 | 6,277 / 2,196 |
  | wl | A | 8,727 | 1,588 -> 1,513 (82.7 %) | 97.55 % -> 97.61 % | 34.0 % -> 34.9 % | 10,277 | 9,236 / 1,041 |
  | wl | P | 8,727 | 1,588 -> 1,514 (82.7 %) | 97.43 % -> 97.49 % | 34.0 % -> 34.8 % | 11,564 | 10,529 / 1,034 |
  | large | A, P | 25,393 | 95 -> 95 | 99.93 % both | 96.6 % both | 0 | - |
  | lake | A, P | 2,803 | 675 -> 675 | 99.26 % both | 72.3 % both | 4 | 0 / 0 |
  | coast_land | A | 4,573 | 14 -> 14 (99.7 %) | 99.80 % both | 63.5 % both | 2,046 | 1,582 / 464 |
  | coast_land | P | 4,573 | 14 -> 14 | 99.80 % both | 68.5 % both | 2,046 | 1,582 / 464 |
  | mouth | A | 9,118 | 1,843 -> 1,732 (81.0 %) | 98.10 % -> 98.13 % | 41.5 % -> 41.6 % | 10,997 | 9,904 / 1,093 |
  | mouth | P | 9,118 | 1,961 -> 1,854 (79.7 %) | 98.05 % -> 98.08 % | 40.9 % -> 41.0 % | 12,523 | 11,412 / 1,111 |
  | ice_land | A, P | 2,654 | 0 -> 0 | 99.30 % both | 95.4 % both | 0 | - |
  | huge lake | A | 2,311 | 0 -> 0 | 99.59 % both | 67.1 % both | 0 | - |
  | huge coast_land | A | 4,074 | 0 -> 0 | 100.00 % both | 72.3 % both | 0 | - |

  - **No row drops.** No miss count grows, and no water-mark or height share falls. Every case writes 0 samples
    outside the window, with the fix on and off.
  - **The river carve on land is untouched [C].** `keepdiff.py` compares the producer's own reasons with the fix on
    and off, in mouth A, lw A, the smoke lw and the smoke wl. Every sample that loses the keep (6,557, 7,044, 11,001
    and 12,840) carries reason 5, a hex that is water after the change. No sample gains the keep, and no land hex
    loses one.
  - **The reload agrees with the fix where it changes something.** On the samples that lose the keep in mouth A, the
    median height difference to the reload is 205 u16 with the fix and 379 u16 without it. 5,814 of these samples are
    nearer the reload with the fix and 22 without it. So the reload keeps no river carve in these water hexes that the
    fix drops. The changed land samples next to them change only through the feather (they are kept in neither run):
    650 are nearer the reload with the fix, 142 without it.
  - **The product's river flag (P against A) matters only for lw.** It is the ridge itself: 237 misses with P against
    95 with A without the fix, and 16 against 11 with it. Elsewhere P and A differ by at most 118 misses (mouth), and
    the fix takes 107 of them away.
  - The fix leaves the product's flag as it is (lead-0615). Its follow-up is the named cause in 11.3 item 1. *Since
    ticket 0631 (2026-11-23, 8.18) the product's table is A's own edge bits (with the DLL's land/water clearing), not
    P: the P rows describe server.exe before 0631 and bundles recorded before it.*
  - Timing: the table "The river keep fix, on and off: time" below.

  **The river keep fix, on and off: time (DIAGNOSTIC; w7-ridge, 2026-11-21) [C offline, busy workstation, no quiet
  lock].**
  - What was timed: the producer's `change()` for the smoke's lw (server.exe's own inputs, from the W6P01 journal),
    with the fix on and off, alternating, no diagnostics dump. Also `create` + `enableLandWater`, which the fix does
    not touch. Scratch `C:/c5-64bit-work/0615/w7/timing.sh`, `timing.txt`.
  - Where the time is paid [C, the code; 8.14.3]: the change runs on `LiveTerrain`'s worker thread. The render
    thread's apply takes the finished result from the worker's queue and does not run the producer. That apply is the
    smoke's lw 52.0 ms: the EXE write, the windows and 8.14.3's region step. The fix adds one class test per sample in
    the keep loop, on the worker. It changes the window's values, not its rectangles: lw's two rectangles are the
    same with the fix on and off (228,928 samples), so the apply writes and uploads the same area [I: not timed live]. `enableLandWater` (18-22 ms per Tiny load) is not
    touched.
  - Result (2026-11-21 00:24-00:26 EST, 7 rounds each, alternating).
    - Conditions: the workstation ran two heavy jobs of other teams the whole time (quietlock check: impl-0626's long
      games and w33-0042's ASan suite). The numbers are an order of magnitude only, not a measure of the fix.
    - `change()` with the fix: median 215.8 ms (172.8-245.4). Without it: 255.6 ms (179.2-296.7).
    - `create` + `enableLandWater`: 190-349 ms in both arms.
    - Reading [I]: the fix makes the change no slower. A sample in a water hex now skips the loop over the river
      centres (47 centres in lw's window band), and fewer kept samples mean a smaller feather. That fits the lower
      median, but the spread on this machine is larger than the difference.
    - Nothing is claimed as faster (PERFORMANCE.md); a quiet-machine series was not run.

- The heights at tol 1 inside the window are low where the reload re-rolled the pieces (wl: 1.76 million samples of
  the map differ between the tap and the reload outside the window; P5, 6.7): the live step's own pieces cannot match
  a reload's variants (way 3, D194), so the region (the water mark) is the measure of the coast.
- **(a) Rivers [C offline for wl, I for what the player sees].** Of wl's misses near the change (704 samples within
  128 of (51,12) in one count), 301 lie on (53,12) and 61 on (52,12), 169 on (52,13) and 127 on the lake (51,13): the
  river-edge plots (52,12) and (52,13) and the samples within 60 of their centres are kept, so where the reload moves
  the coast over the river's surroundings the live field keeps the old shore. In 462 samples the old water stays where
  the reload has land, in 242 the old land stays where the reload has water. What the player sees until the next load:
  near a river that meets the changed coast, a band up to about 60 samples (one plot) wide keeps the old shoreline,
  so the new land shows a notch of old water (or the old bank) along the river's last plot, and the river ends where it
  ended at the load. Rivers are named, not ported (MC #8420 (d)). Planned picture triplet for an I-session: before,
  live, reload of wl at (51,12)-(52,13).
- **(b) Natural wonders next to a change [C offline against 4906's recorded stock reload `15_reload_field.npz`, for one reef; I for Gibraltar].** In the large case the reef
  (25,9), two plots from (24,10), lies inside the window: its hex's materials equal the reload's on 99.0 % of the
  samples (the tap 98.7 %), heights at tol 1 75 % (the tap 78 %, the reload re-rolled there too); the second reef
  (26,8) is equal on 100 %. A wonder's piece is one fixed piece (its own flag bit), so the producer's re-match places
  the same piece, and its materials on wet region samples (the 9 huge samples of 8.10.2) come out as in the EXE.
  Gibraltar, offline on 4902's huge tap (2026-11-20 18:30 EST; no reload of it exists, so no reference): coast
  (112, 43) next to it turned to grass goes live, 0 samples outside the window (347 x 373), 437 ms; the wonder's
  own materials (26, 8, 34, 33) cover its hex again (2,643 of 3,136 samples around the centre against 2,985 in
  the tap), but 71 % of its hex's materials and nearly all heights differ from the tap: the producer's re-match
  places the piece by its own seed, so possibly in another of its four transforms (way 3, D194) [C, offline].
  *Verdict replaced 2026-11-20 19:10 by w5-0615 (MC #8641: option (B), the wonder's own hexes protected); was:
  "drawn right, no refusal needed".*

  **(b.1) What a stock reload does to a wonder's hex [C, offline on 4906's recorded stock reloads, w5-0615,
  2026-11-20 18:45 EST].** The check: consecutive fresh loads of 4906's chain, `F:/c5-64bit-work/0465/4906/scenes/stock/`
  (`12_lw_before_field.npz`, `13_wl_before_field.npz`, `14_large_before_field.npz`, `15_reload_field.npz`; each is
  a load of the previous state's save). They were compared with `regioncmp.py --plots <wonder> --tol 1 --rings 1`
  on each natural wonder of Tiny `LT_0008`: the reefs (26,8) and (25,9), Mt Kailash (38,12), Kilimanjaro (51,18) and
  King Solomon's Mines (6,22). Scratch: `C:/c5-64bit-work/0615/w5/nw_reload_check.sh`, `nw_radial.py`.

  | Load pair (the change between them) | reef (26,8) | reef (25,9) | Kailash (38,12) | Kilimanjaro (51,18) | Solomon (6,22) |
  |---|---|---|---|---|---|
  | 12 vs 13 (lw at (53,13) (55,15)) | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
  | 13 vs 14 (wl at (51,12) (51,13)) | 3,446 / 157 | 3,372 / 141 | 1,745 / 706 | 3,928 / 919 | 64 / 103 |
  | 14 vs 15 (large, 7 plots at (22-24, 10-13)) | 903 / 114 | 1,044 / 123 | 4,092 / 1,498 | 3,914 / 433 | 64 / 502 |

  Each cell gives the hex's samples (of 4,096) whose heights differ at tol 1 / whose materials differ.

  - A reload after a land/water change **re-rolls wonders far from the change**: Kailash lies 13 plots from wl, and
    Kilimanjaro 6 plots from it.
  - The material differences reach the wonder's centre (Kilimanjaro: 47 % of the samples within 8 of the centre;
    Kailash: 31 % within 8 and 54 % at 8-16). So the reload places the wonder's own piece differently. These are not
    only the neighbours' blend at the hex edge.
  - The reefs keep their material (7, uniform) while their heights move.
  - The lw reload moved none of them; the whole map differs there in only 246,894 samples.

  Wording, as MC #8641 set it for this result: **live, the wonder keeps its shape; after a reload it may show another
  variant, as it may in stock.**

  **(b.2) As built: `Producer::Options::protectWonders` (on by default; land/water changes only) [C for the code and
  the tests, C offline for the runs; w5-0615, 2026-11-20 19:05 EST].**
  - **The rule.** During a land/water change, every window sample whose hex (`sampleCell`) is a natural wonder's own
    plot keeps the current field: the load's ground, or an earlier live window's.
    - It is kept like a river sample: `prot`, w forced to 0 and the 32-sample feather around it.
    - Only the wonder's own plots are kept. The coast around them is redrawn.
    - A wonder plot that is itself one of the changed plots is redrawn.
    - Changes that keep every plot's class are unchanged (4903-4908: no wonder rule).
  - **The step fix.** Step 4's override, "a sample whose region byte the change moves takes the new ground whole",
    does not apply within `kFeatherSamples` of a kept wonder sample. There the feather wins.
    - Without this, huge's Gibraltar showed a step at the hex border of up to 6,264 u16 (4.91 world units). The load's
      own largest step across the same border is 2,090 u16 (1.64 world units).
    - So the excess was up to 6,093 u16 (4.78 world units), at the samples where the moved coast meets the kept hex.
  - **Output.** `ChangeResult::samplesWonderKept` counts the kept samples. The runner `lt_producer_run` takes
    `--protect-wonders 0` and prints `wonderKept`.
  - **Tests** (`producer_test`, 40 run, 0 failed):
    - `lw_wonder_beside_change_keeps_its_hexes`: a two-plot wonder beside a plot turned to coast keeps both hexes
      bit-equal, and the changed plot carries material 37.
    - Negatives: `lw_wonder_unprotected_is_redrawn` (option off), `lw_wonder_own_plot_changed_is_redrawn` (the
      wonder's own plot turned to water is redrawn, the other wonder hex is kept), and
      `lw_wonder_not_kept_outside_land_water`.

  **(b.3) The seam, offline on recorded inputs [C offline].**
  - Setup: the runs of `C:/c5-64bit-work/0615/w5/nw_runs.sh`, measured with `nw_seam.py`.
    - Hexes by the nearest plot centre.
    - Pairs: 4-neighbour samples across the wonder hex's border, compared live against the load.
    - Install: pure_vp, build 4390913. Piece library SHA-256 86faac1e...ce4c1c7a. Coast styles from W1's assets.
  - Inputs:
    - Tiny: 4906's `LT_0008` chain, the large case, with the reefs at (26,8) and (25,9). mapRandomSeed 0x3DFC9816.
    - huge: `C5H-VP8P-HUGE_0350` from the kept folder `C:/c5-64bit-work/keep/liveterrain-derived-4903/huge`, change
      (112,43) beside Gibraltar (112,42). mapRandomSeed 0x067F5502.
  - With the option off, both runs reproduce W1's windows and counts exactly.
  - Gibraltar is a one-plot wonder in the DLL. The reef is a two-plot wonder.

  | Case | wonder samples kept | hex equal to the load, option on / off | border pairs (outer side redrawn) | largest step across the border, live / load | live excess over the load, p99 / max | material changes across the border, live / load | ms on / off |
  |---|---|---|---|---|---|---|---|
  | Tiny large, two reefs | 8,192 | 100 % / heights 74.7 %, materials 98.1 % | 490 (42) | 403 / 403 u16 | 2 / 2 u16 | 10.8 % / 10.8 % | 326 / 211 |
  | huge, Gibraltar (112,42) | 4,096 | 100 % / heights 0.4 %, materials 28.6 % | 298 (193) | 2,030 / 2,090 u16 | 175 / 222 u16 (0.17 world units) | 18.1 % / 18.1 % | 312 / 275 |

  - "Hex equal, option on": 100 % by construction on the producer's own hex test, since w is 0 there and the window
    takes the current field's value [C code]. The script's nearest-centre hexes give 99.76 % and 98.17 %; the
    difference is samples on the hex edge.
  - The ms figures are single runs on the busy workstation, an indication only (PERFORMANCE.md).
  - **No visible step** [C offline]: the excess at most 222 u16 is 0.17 world units, and the border's material changes
    are those of the load.
  - **What the feather costs** [C offline, I for the look]. Within the feather around Gibraltar the new coast is
    partly held back. 487 samples outside the wonder's hex (all within 65 samples of its centre) have the other
    waterline state than the unprotected run, 446 of them in the water mark (material 37). So the player sees the old
    shore's last few samples bend towards the wonder, like rivers in (a). On Tiny's reefs: 0 such samples.
  - Not verified: the look in the game (an I-session item: one change beside a natural wonder on huge, with a picture,
    sent to W4 for the 4915 runbook on 2026-11-20 19:07).
- **huge, one own case [C, offline on recorded inputs, 2026-11-20 18:20 EST]:** plot (34, 30) of `C5H-VP8P-HUGE_0350` (plains next to
  coast, no river near) to coast, mapRandomSeed 0x067F5502: live, window 359 x 361, 2 lines redrawn (both reach
  beyond 128 samples), 32 lines a reload would re-jitter, 0 samples outside the window, 177 ms. No stock reload of it
  exists, so it shows behaviour and cost only.
- **Not verified:** ~~huge against a reload (4912's huge cases come Saturday)~~ (done 2026-11-20 by w6-4914b, the two
  huge rows above, offline); the mouth case with the DLL's own river
  edges (above); the look of the window edge where a re-jittered line meets the load-time line; the strips'
  band 0 / 3 samples (not separable from piece variants, above).

**Known difference of the strips, for the candidate's list (MC #8589; 2026-11-20 17:50, from the runs above) [L
offline for the numbers, I for what a player sees].** Where the beach band of a changed coast differs from what the
EXE's own load makes:

1. **Near rivers: the river carve.** On the covered samples, the tap is more than 1 lower than the strips on 0.87 %
   (Tiny) and 1.33 % (huge); 94 % (Tiny) and 93 % (huge) of those lie within 100 samples of a river-edge plot.
   - Cause: the EXE's river carve (step 7, 0x412680) runs after the strips and lowers the ground along the rivers. The
     server does not port it (MC #8420 (d)).
   - In the product: `protectRivers` (8.9) keeps the current field within 60 samples of a river-edge plot's centre, so
     a live window near a river shows the load's carved ground there, not the strips' uncarved beach.
   - What the player sees: where a coast moves next to a river, the old shore stays visible inside about one plot
     around the river plots until the next load. The new beach meets the kept ground through the 32-sample feather
     (8.9) [I: not seen; 4912's `mouth` scene will show it].
2. **Away from rivers: a few unattributed spots.** These are 0.06 % (Tiny: 66 samples in 4 blocks of 64 x 64) and
   0.14 % (huge: 440 samples in 12 blocks) of the covered samples. Each spot belongs to a single coast: no second
   coast's line comes within 25 samples of any of them.
   - Size: |difference| median 136 / 521 u16 and at most 852 / 2,235 u16, which is at most 0.67 / 1.75 world units
     (a plot is 64 wide; 1 u16 is 0.0008).
   - Material: the tap's material there is 3 or 37, the beach and the water ground.
   - Cause not found [I candidates: a local difference of W1's curve, or a strip folding over itself at a tight bend,
     where the order of the even and odd segments decides].
   - What the player sees [I]: at most a small dent or bump on the beach, a few samples across, on a coast the player
     has just changed. The player has no reference to compare it with, because a reload of the same table re-rolls
     the pieces anyway (way 3, 8.5 "The stock reload re-rolls").
3. **Rounding.** Every other covered sample is within 1 u16 (0.0008 world units), which is invisible.

### 8.18 The producer gets the plots' own river edges (ticket 0631, 2026-11-23) [C for the code, the DLL read and the tests; C offline for the runs; L where marked]

Written by impl-0631 (account B, branch `task-0631-river-edges`). Replaces the feed's "river on any edge" rule of
8.17 ("Why the product keeps more than W1's offline lw") and 11.3 item 1's planned fix.

**The DLL's river data [C, the fork at the x64 pin 1b87dd185, `CvPlot.cpp`/`CvPlot.h`/`CvDllPlot.cpp`].**
- A plot holds three edge bits of its own: `m_bWOfRiver` (the edge to its east neighbour), `m_bNWOfRiver` (south-east
  edge) and `m_bNEOfRiver` (south-west edge); `CvPlot::IsRiverSide` reads the other three from the neighbours' bits.
  `ICvPlot1::IsWOfRiver` / `IsNWOfRiver` / `IsNEOfRiver` (`CvDllPlot.cpp:163-176`) return the members
  (`CvPlot.cpp:6289-6292, 6341-6344, 6392-6395`): pure reads.
- `isRiver()` (the feed's flags bit 0, `CvPlot.h:452-455`) is `m_iRiverCrossingCount > 0`, a count kept by
  `updateRiverCrossing`, which runs only from the edge setters, and counts an edge only when both plots are land
  (`CvPlot.cpp:11933-11990`). So the flag and the bits can differ: a coast plot keeps its flag when it turns to water
  (8.17's lw, `(53,13)`), and a plot whose only river edge is a neighbour's bit has the flag with no bit of its own.
- **A land/water change clears the changed plot's own three bits.** `CvPlot::setPlotType` calls `erase()` when the old
  or the new plot type is `PLOT_OCEAN` (`CvPlot.cpp:7261-7264`), and `erase()` ends with `setNEOfRiver`,
  `setNWOfRiver`, `setWOfRiver(false, NO_FLOWDIRECTION)` (`CvPlot.cpp:341-344`). No other plot's bits change. This
  is why 4912's `mouth` saw `(53,13)` lose its river: `(52,13)` became coast, its W-of-river bit (the edge to
  `(53,13)`) was cleared, and `(53,13)`'s crossing count fell to 0 [C for the rule; the flag change is the recorded
  `05_mouth_apply.json` [L]].

**As built.**
- `server/src/feed/srv_feed.h` `PlotRecord`: the spare byte `reserved0` (offset 11, always 0 before) is now
  `riverEdges`: bit 0 W, bit 1 NW, bit 2 NE of river, the producer's `PlotInfo::riverEdges` order. `known` bit 2
  (`kPlotKnownRiverEdges`) says the bits were read. Size 20 and every offset unchanged (the rule above `PlotRecord`;
  `static_assert`s on offsets 11 and 12).
- `srv_feed_server.h`: mask bit `kFRiverEdges` (in `kFGetterAll`, not in `kFLayoutAll`); `ApplyMask` keeps only the
  three bits and sets known bit 2. `srv_remote_feed.cpp` `ReadPlot` calls the three getters at every getter read
  (snapshot, turn refresh, dirty and rolling refresh); `rf_river` of `feed-per-turn.tsv` now also counts an edge change.
- The three getters are on the audited purity list (`proxy/model/pure-methods.tsv`, evidence lines at the list's
  citation commit e9d0c4837; `purity-baseline.json` remade, 138 method keys; MC #11870 option A). Side effect (MC's
  condition): the proxy's `purity.tsv` lists them too, so the EXE's own calls of these three (about 680 each per Tiny
  load, `server/evidence/2500/P3/proxy-calls.tsv`) are mirror-pure crossings like `IsRiver` already was.
- `srv_liveterrain.cpp` `PlotTableFromFeed`: a record with known bit 2 gives `riverEdges = record & 7`. A record
  without it (every record of a scene bundle recorded before 0631: `reserved0` was 0 and bit 2 never set) takes the
  old rule, `flag ? 7 : 0`, and is counted (`PlotTableIn::oldRuleRecords`, `oldRuleRiverPlots`). So old bundles replay
  exactly as recorded (rule A).
- `FieldRelevantChange`: with edges on both records, only an edge change is relevant (a flag-only change is not: it
  changes nothing the producer reads); the flag counts only where a record has no edges.
- **The journal says so (rule A).** The live load line reads `live terrain: load seq S: producer requested (...);
  river edges: the plots' own edges (ticket 0631)`; the player's line `live terrain replay: ...; river edges: N of M
  record(s) carry no river edges (recorded before ticket 0631): those take the old rule, all three edges on the K with
  the any-edge river flag` (`RiverEdgeRuleText`, `Replay::riverEdgeRule`). The journal's format is unchanged.
- `render/src/liveterrain/producer.cpp` is unchanged: it already takes the three bits.

**Tests [C].** `test_feed` case `river-edges` (layout, every bit combination through the snapshot, an edge-only
refresh listed, bits above 7 dropped, layout and owner patches keep the bits, a mask without `kFRiverEdges` sets no
bit 2; 318 checks); `test_remote_feed` case `river-edges-0631` (the snapshot reads each plot's bits with three calls,
a flag-only plot gets 0, the rolling refresh finds an edge-only change; 235 checks); `test_liveterrain` case
`river_edges` (the mapping, "a plot that only touches a neighbour's river edge gets riverEdges 0", the old rule and its
counts, the filter, the stub producer's `RiverEdge` reason on the real edges against the old rule, the load and replay
log lines; 35 checks). The existing `known == 1 / 3` checks of `test_feed` became `5 / 7` (bit 2). All
2026-11-23 16:14, plain build. `lt_hook_run` builds its records with the edges (`--river-rule old` for the old form).

**Acceptance (1) offline [C, offline on recorded inputs, 2026-11-23 16:45-16:48 EST, workstation; DIAGNOSTIC as to
time].** `server/evidence/0631/cases_0631.sh` (results `offline_table.md`, `offline_summary.json`, `tables_k*.json`):
4912's Tiny stock chain (`F:/c5-64bit-work/0615/4912/scenes/stock`, save b2ceb663..., seed 0x3DFC9816), steps 1-5,
prepared by 4913's `prep_chain.py` from the kept folder's `tiny/plots.txt`; `lt_producer_run` of this branch (the
w7-ridge fix on, `--diag-out`), the pure_vp install copy (build 4390913, piece library 86faac1e...), the coast styles
extracted with `fpkscan extract` from its `TerrainModels.fpk`. Three river tables per case (`tables_0631.py`):
- **A** = the chain's table: the load's own edge bits (W1's "EXE's edge bits" are in fact the DLL's, read through Lua
  `IsWOfRiver` etc. by 4902's `live.py`), never changed by a step;
- **E** = the product with 0631: A with every plot that changed to or from `PLOT_OCEAN` in steps 1..k set to 0 (the
  DLL's `erase`, above). E equals A in lw, wl, lake and coast_land (the erased plots had no bits of their own); in
  mouth `(52,13)`'s bits 3 (W, NW) go;
- **P** = the product before 0631: 7 on every plot with the DLL's river flag (4902's flag 64, each step's recorded
  `river` applied): 86 plots against A's 49.

| case | river table | river-keep samples (reason 2) | misses / coast-moved within 128 | water mark in the window | heights tol 1 | outside |
|---|---|---|---|---|---|---|
| lw | A = E | 45,852 | 11 / 8,787 | 99.85 % | 84.9 % | 0 |
| lw | P | 49,034 | 16 / 8,787 | 99.85 % | 83.8 % | 0 |
| wl | A = E | 75,577 | 1,513 / 8,727 | 97.50 % | 34.1 % | 0 |
| wl | P | 88,511 | 1,514 / 8,727 | 97.45 % | 34.0 % | 0 |
| lake | A = E = P | 25,960 | 675 / 2,803 | 99.26 % | 72.3 % | 0 |
| coast_land | A = E | 27,204 | 14 / 4,573 | 99.80 % | 63.5 % | 0 |
| coast_land | P | 35,010 | 14 / 4,573 | 99.80 % | 68.5 % | 0 |
| mouth | A | 56,395 | 1,732 / 9,118 | 98.13 % | 41.6 % | 0 |
| mouth | **E** | **56,222** | **1,675** / 9,118 | 98.14 % | 41.8 % | 0 |
| mouth | P | 69,147 | 1,854 / 9,118 | 98.08 % | 41.0 % | 0 |

- **Verdict: acceptance (1) holds** (`summary_0631.py`: PASS). With the feed's edges the river keep holds no more
  samples than with W1's tables in any case (equal in four, 173 fewer in mouth), and the misses of 8.17's cause table do
  not grow (equal in four; mouth 1,675 against A's 1,732 and the old product's 1,854). Against the product before 0631
  the keep is 6-22 % smaller in lw, wl, coast_land and mouth.
- The A and P misses equal w7-ridge's table in 8.17 (lw 11 / 16, wl 1,513 / 1,514, lake 675, coast_land 14, mouth
  1,732 / 1,854): this chain reproduces those runs. lw's heights at tol 1 (84.9 %) differ from W1's 82.4 % because
  W1's lw ran on 4906's chain, this one on 4912's (the same save; another stock reload).
- mouth with E settles 8.17's "Input limit" `[I: not run]`: with the DLL's edges the keep is smaller (56,222 against
  56,395) and 57 fewer samples miss.
- What is left on land is the river keep itself (the carve is not ported): 11.3 item 1 stays a known difference, at
  the real river plots only.

**Read-only trap met on the way [L, 2026-11-23 16:18].** `proxy/tools/test_purity.py`'s seeded-source cases copied
the DLL source with `copy2`; from the read-only shared folder `C:/c5-64bit-work/keep/pins-0.1b` the copies were
read-only and the second case's `rmtree` failed with WinError 5, so `build_proxy`'s full purity test failed as soon as
the purity list changed (on main the test was cached and never re-ran). The same class as `release-builds.md` 6.6;
fixed in `copy_sources` (`copyfile`, and `rmtree` clears the attribute): 66/66 passed, 135 s.

**Live (acceptance (5)) [L, 2026-11-23 18:04-18:24 EST, workstation, c5h-d (F:, mods548 + Vibrant Textures, EXE build
4390913), MC-B grant #11944; DIAGNOSTIC as to times and pictures (D112: the run_3308 product route, not the installer's
layout)].**
- **Setup.** Layout `C:/c5-64bit-work/0631/layout` from `smoke_4914.sh build` at 3506906e0 on the 0.1b pins (server.exe
  869c00e6..., proxy 912e5e6c..., composite eb5a591a..., the pins' x64 DLL b84aa3e9...). `smoke_4914.sh` with
  `C5_SMOKE_PREP=run3308 C5_SMOKE_CASES=1 C5_SMOKE_TAG=Q1 C5_SMOKE_SAVE_SRC=c5h-0631-src-t0008` (a copy of c5h-a's
  `c5h-9029-tiny-t0008`, b2ceb663..., put into c5h-d: the driver's `copysave` copies inside one instance only). Process
  0 (load, `lt.py setup`, save), then lw ((53,13), (55,15) to coast) in a new process with a scene bundle. The
  owner's beta-3 install window (MC-B 18:1x) stopped the run before its after-reload capture; that capture was taken
  in a second short session (prep, reload of the lw save, the capture at the same camera:
  `server/evidence/0631/live_reload.sh`; the first session's `live.sh` and the save copy `copy_save.sh` beside it),
  18:20-18:24.
- **Lines.** Every load: `live terrain: load seq 3: producer requested (56x36 plots, 3584x2560 samples); river edges:
  the plots' own edges (ticket 0631)`. lw drawn live: `apply of 1 change(s)`, render thread 52.6 ms (EXE write 6.6,
  windows 4.9; 2 region rectangles); the gate `waves ok (Waves_Reload 34.4 ms)`; 2 coast lines redrawn. Producer made
  994 ms after the field. 0 LOUD in server.log and render.log of the three processes; no box. End: harness stop,
  `run_3308` restore rc 0, provision verify rc 0, both times.
- **The feed's edges [L].** `server/evidence/0631/journal_edges.py` on the lw bundle (`F:/c5-64bit-work/0631/smoke/
  bundles/Q1P01_20261123-180840_workstation`): 2,016 of 2,016 baseline records carry known bit 2; their `riverEdges`
  equal the 4903 table (`keep/.../tiny/plots.txt`, W1's bits) on every plot; 49 edge plots, 86 flag plots, so 37
  plots that only touch a neighbour's river are no longer river-edge plots. The job's records: (53,13) and (55,15)
  with edges 0 (as before the change) and (53,13)'s flag kept. The same reader on W6's pre-0631 bundle W6P01: 0 of
  2,016 records with bit 2 (the negative).
- **Live equals offline [L + C].** `server/evidence/0631/live_check.sh`: `lt_producer_run` on the live before field and
  the feed's tables (E) gives the live field exactly (0 of 9,175,040 samples differ, heights and materials); the old
  rule (P) differs in 7,665 heights and 62 materials. Against the stock reload after lw (4912's chain), live and E miss
  11 coast-moved samples within 128 of the changed plots, P 16.
- **Against the product's own save-and-reload [L]** (`server/evidence/0631/live_vs_reload.py`, the reload's field and
  picture at the same camera): within 128 samples of the changed plots, 11 samples are land live where the reload has
  the water mark (W6's ridge measure: 232 before w7-ridge, 13 with it on the old rule), none of them kept from the
  load, 0 the other way; the water mark agrees on 99.97 % of those near samples with water in either. In the samples
  the live step changed (57,416) the water mark agrees on 99.41 %; the reload re-rolls 211,780 samples elsewhere at tol
  1 (piece variants, D194). Pictures (DIAGNOSTIC): `F:/c5-64bit-work/0631/pictures/lw_live_vs_reload_DIAGNOSTIC.png`
  (before, live, reload; W6's crop (600,300)-(1150,705)) and its difference image; Visual Checks
  `vc-20261123-182609-workstation-af28`. Looked at: the new water and the shore along the river match the reload's
  outline; no raised line; the reload's beach differs by its re-rolled jitter. Picture difference live against reload
  in the crop: median 6, p95 66, max 217 (whole frame 7 / 57 / 236).

## 9. The server's region hook (child 4907) [C for the code, I where marked]

Written by w5-4907 (account B) from 2026-11-19 00:30 EST, branch `w5-4907` off `epic-0465-extras-c-live-terrain`.
Design: way 3 (route note 3.3, MC #6524, D198). **Status 2026-11-19 04:00: live on the product route, 8 of 8 drawing changes of 4906's chain live in the game (9.6); offline green with 4903's real producer
(the hook reproduces 4903's runner bit for bit on the huge chain, 9.5); the bundle's generations built inside format 9
(9.4); 4905's EXE field write called before each window.** Corrected
2026-11-19 01:55 by w5-4907: was "built against a test stub ... no live run; the scene bundle's field generations not
built yet" (00:30).

### 9.1 Where it sits

| File | What |
|---|---|
| `server/src/render/srv_liveterrain.h/.cpp` | `LiveTerrain`: the worker thread (one producer per load: `create` and `change` never on the render thread), the change filter, the merge of queued changes, held plots, the wait lines, the load's journal; `ApplyWindow` (every rectangle checked before any write); `RegionSha256` (bcrypt); the journal's text form (`EncodeSpanText` / `DecodeSpanText`), `Replay` and `CheckLiveTerrainConfig` (9.4) |
| `server/src/render/srv_liveterrain_c5r.cpp` | the adapter to `c5r::liveterrain` (8.1 at b6cf09570): the piece library opened once per process and shared by the loads' producers, `MapFrame::fromConstantsBlock` with the map size checked against the plot table, `deriveSeed`, the result copied into the hook's own types (`samplesOutside` non-zero is an error) |
| `server/src/render/srv_liveterrain_scene.cpp` | `Scene::liveTerrainStep` and the player's `Scene::liveTerrainReplayInit` / `liveTerrainReplayTo` (Scene members defined here) |
| `server/src/render/srv_liveterrain_bundle.cpp` | the recorder's `noteLiveTerrainFrame` / `takeLiveTerrain` (9.4) |
| `server/src/render/srv_liveterrain_features.h` | feature Type names by id, made from the art define (`ART_DEF_FEATURE_X` -> `FEATURE_X` [I], lead #6640 (f): no new DLL-boundary call); written by `srv_remote_feed.cpp` |
| `server/src/render/srv_liveterrain_exefield.h/.cpp` | 4905's (W6), taken unchanged at 2534847df: the EXE's own field written as data |
| shared, hunks only | `srv_render_scene.h` (include, declarations, members, `liveTerrain()`), `srv_render_scene.cpp` (one call in `Scene::poll`'s residency block), `srv_residency_source.h/.cpp` (`plotRecords`; `keepTableOnNextField`), `srv_scene_recorder.h/.cpp` (members, two calls), `srv_scene_bundle.cpp` (include, one check block), `srv_render_host.cpp` (`setLiveTerrain`), `server/tools/sceneshot/sceneshot.cpp` (the replay's init block and one call per frame), `server/src/remote/srv_remote_feed.cpp` (include, one line) |

The sources sit in `server/src/render/` itself, not in a subfolder: `server/build_server.py` and every suite builder glob
`server/src/render/*.cpp` without recursion, so new units there need no builder change [C, `render_sources()`].

### 9.2 What happens, per `Scene::poll` (render thread)

1. First call: `LiveTerrain` made when the scene has a section (the live server), a residency (`render_tiles=bake`), the
   install, and `C5_RENDER_LIVE_TERRAIN` is not `0` (a test and tool switch, like `C5_RENDER_HELD_BAKE`; live terrain has
   no VP64Options option, K4). Piece library path `<install>\Resource\Common\TerrainPiecesLibrary.fxsb` (8.1).
2. A field of a load not seen, once the bake's table has the field's size (W x (rows - 4)): 4905's
   `OpenExeFieldTarget(exePid)` (it finds the EXE's module base: **the EXE runs relocated**, bases 0x00740000,
   0x00F60000, 0x00EB0000 measured, `camera-and-picking.md`; it checks the PE stamp 0x546CD0A8 and that [0xD90FC4] is
   the rebased 0xD91270), then the grid constants block read as data at `exeBase + (0xD90FBC - 0x400000)` (880 bytes,
   D4; its pointer at offset 8 must be the target's grid), then the producer's inputs to the worker: the field copied,
   the table (named through `srv_liveterrain_features.h`), the block. An input that cannot be had is one LOUD line and
   the load stays as stock draws it. *Corrected 2026-11-19 01:35 (W6's note #6655): the first version read 0xD90FBC
   unrebased.*
3. The table the residency compared last (`plotRecords`), when its generation or the residency's `plotsChanged` moved:
   a plot whose terrain, plot type, natural-wonder bit, wonder feature id or river bit changed posts the newest table.
4. Every finished change of the current load: live -> 4905's `WriteExeFieldWindow` first (while `m_grid` is still the
   field before the window: it refuses when the EXE's field differs from it), then `ApplyWindow` into `m_grid`, then
   **one** `applyField` for all of them, with `ResidencySource::keepTableOnNextField()` before it (9.4); a window the
   EXE refused is not drawn either (`noteApplyError`, one LOUD line), so the server's picture and the EXE's field never
   disagree (D118). Wait for reload -> one line: reason, the producer's text, the plots. `applyField` re-bakes only
   tiles whose field hash changed (`residency.cpp` `onField` [C]); the trees rebuild every block from the edited field
   (`ForestScene::onField`, same load [C]) and the water module takes the field again.

Rules the code adds to 8.1 [C, tests]:

- **Merge.** A change queued behind a running job is replaced by the newer table (the producer compares with its own
  current table, so the newest one carries every change); the journal's entry is replaced with it.
- **Held plots.** On "wait for reload" the producer keeps its old cells (8.1), so every later diff would carry the
  waiting plot and wait too: one coastal change would stop live terrain for the rest of the load. The hook therefore
  sends a waited plot **as the producer has it** until the reload; if that plot changes again (or back), it is released
  and goes out with the next table. Limitation [I]: a clean change merged into the same job as a waiting one is held
  with it.
- **Errors** (`Status` from the producer) are LOUD lines; the change waits for the reload. A producer that cannot be
  made is one LOUD line per load; the picture is stock's (no degraded world, D118).
- **Stale results** of an earlier load are dropped (counted).

### 9.3 Tests

`server/tests/render/liveterrain/`: `test_liveterrain_build.py` (clang-cl C++14 like the server; `--asan`; `--hook-run
C5RENDER_DIR` also builds `lt_hook_run.exe` with the real producer), `test_liveterrain.py` (pytest over
`test_liveterrain.exe`, `C5_LIVETERRAIN_BUILD`), cases `table_and_filter`, `apply_and_hash`, `live_inline`,
`create_failures` (library missing, version 19, grid size, a short constants block, no material layer, inputs
unavailable), `threaded` (create off the calling thread, three changes merged into one, a stale result dropped, the
destructor during a create), `journal` (diffs, generations, a replay through a second producer equal to the live field),
`journal_merged`, `bundle_text` (the text form's round trip and eight refusals, `Replay` with a tampered hash refused as
MISMATCH, the checker's rules). The test stub (`stub/`) mirrors 4903's header so the adapter compiles against either.
2026-11-19 02:00: 176 checks, 0 failed, the pytest wrapper 3 passed plain and under ASan (MSVC /fsanitize=address);
the server's units that include the hook syntax-checked with 4903's real header as C++14 (`/std:c++14 /W3 /WX`, clang-cl).

### 9.4 The scene bundle (rule A) inside format 9 [C]

Lead #6640 (d): format 9 is 0561's; no new format. The pattern of ticket 4852's farm producer: `render_config` keys, no
member.

- `render_live_terrain` `0` / `1`: live terrain on at the span's first frame (written at the span's end by the recorder).
- `render_live_terrain_journal` = `lj1 ` + base64 of: the load's seq, map size, the 880-byte constants block, the
  baseline table (the feed's records), every job as a diff of records against the job before, every generation (number,
  job, box, SHA-256 of the box's heights then materials right after that job's window), the frame -> generation list
  (one entry per change, the span's first frame always), the feature names by id. Sizes: 55 KB of text on Tiny, 275 KB
  on huge (9.5).
  Trailers after the feature names, each only when it applies, so a journal without them is byte for byte the older
  form: the map seed (`SEED`, u32 1, u32 seed; 12 bytes; ticket 4912) and, after it, the **land/water mark** (`LWAT`, u32
  1; 8 bytes; ticket 4914 (1), 2026-11-20): the load's live producer had 4913's land/water step on. The decoder refuses
  any other bytes after the names, the mark without the seed's trailer before it, and a cut mark (`test_liveterrain`
  case `land_water`). `scenes_4912.py seed` reads the seed before the mark and reports `land_water`.
- A journal that cannot be taken (no producer for the load, a new load inside the span) is a version note starting
  `gap: live terrain` instead.
- **The player and the mark (ticket 4914 (1)).** The player's producer gets the land/water step only when the journal
  carries the mark, so a bundle replays what its live server did: every journal recorded before 4914 (1) (the library's
  16 bundles, 4906's and 4912's PT/HT bundles) replays without the step, and their land/water jobs wait as they did
  live [C, the code; `land_water` case: the replay calls the enabler only on the mark]. The 16 library bundles therefore
  stay as they were by construction; the rule A run that shows it is R1 (lead-0615's slot).
- **Gap (rule A), land/water changes drawn by default (ticket 4914 (1), w6-4914b, 2026-11-20 21:50).** From 4914 (1)
  server.exe draws land/water changes live by default. No library member holds such a change yet: 4912's PT01-PT08 and
  HT01-HT02 were recorded while the server waited `Water` (6.11, 6.12), so their journals list no land/water generation
  and carry no mark; replayed they draw the load's field, as recorded. The members come from the first recordings with
  the mark: the land/water smoke of 4914 (1) (`server/evidence/0615/4914/smoke_4914.sh`, Tiny lw / wl / coast_land, a
  bundle per process) and I2. Until such a member is in the library index, this note is the rule A record.
  *(Corrected 2026-11-23, ticket 0622: a span may now hold the `Waves_Reload`; format version 11's `water_frames.bin`
  switches the water copy at the recorded frame (below). Was: "each span holds one water copy of its load (the gap
  above), else it is evidence only".)*
- The checker (`CheckLiveTerrainConfig`, called by `CheckBundle`): the key absent (every older bundle), `0` or `1`; the
  journal only with `1` and decodable (BadValue); `1` without journal or note: MissingMember.
- The player: after the load's field, `liveTerrainReplayInit` rebuilds the producer from the journal and the load's
  field; before each frame's `poll` (and before the trees' restore at the span's first frame) `liveTerrainReplayTo(frame
  id)` runs the jobs up to the generation the live server drew, writes each listed window, checks its box and hash
  (a mismatch is a refusal, not a tolerance) and re-applies the field.
- **The feed's read order is unchanged**: a live edit re-applies the field through `applyField`, whose residency
  `onField` used to read the plot table again (`takeBaseline`, caller 1); in the player a caller-1 record inside the span
  is a field load and is refused. `ResidencySource::keepTableOnNextField()` makes that one `onField` keep the table it
  has, so a live edit makes no feed read and the bundle's feed order is that of a load. Not checked by a recorded
  bundle yet [I].
- **The water copy after `Waves_Reload`: closed by ticket 0622 (format version 11, 2026-11-23) [C code, L live].**
  Land/water windows make `Waves_Reload` live (10.9), and the server then takes a later water copy of the same load,
  which the recorder writes as `water/load<NNNN>-c<copySeq>.wtap`. Since format version 11 the bundle's
  `water_frames.bin` names, per recorded frame, the copy the render thread drew with, and `sceneshot` switches to the
  later copy at that frame through the live `WaterScene::useImage` (a same-load copy keeps the water clock, ticket 0586);
  `research/06-tooling-and-workflows/offline-scene-harness.md` section 42 has the member, the checker and the tests.
  Bundles of formats 9 and 10 replay as before (the load's last copy for every frame). The rule that every 0615 bundle
  member's span holds one water copy of its load is withdrawn.
  - **Live (c5h-a, Tiny, build 4390913, 2026-11-23 20:00-20:40, this branch's server.exe `0bd39b2c...` on the 0.1b pins;
    `server/evidence/0622/live.sh`, `waves40.sh`).** `smoke_4914.sh` (SMOKE PASS; bundles Q2P00-P03) and one 40-frame
    process (Q2W40: the save after wl, coast_land made after 3 dumped frames). Q2W40 draws copy 1 (531 waves) in frames
    1853-1867 and copy 2 (528 waves, read 0.5 s after the gate's `Waves_Reload`) from 1868 to 1892; Q2P03 switches at its
    last frame 1737 (server.log: copy 2 read at 117.796 s, the span drawn at 117.849 s). Q2P01 and Q2P02 hold no switch
    inside the span (Q2P01's copy 2 arrived after its last frame: every record names copy 1, where the old player
    would have drawn copy 2).
  - **Offline (this branch's sceneshot, `--dumped --compare --tolerance 1:1`, 2026-11-23 20:41-20:44).** Every frame
    before the live terrain generation is byte-equal, colour and depth: Q2W40 1853-1863 (11), Q2P03 1722-1732 (11),
    Q2P01 1798-1805 (8), Q2P02 16 of 16. Main's player cannot replay Q2W40 or Q2P03 at all: it takes copy 2 for every
    frame and refuses copy 1's water clock (531 waves given, 528 in the image), W6P03's failure (9.8).
  - **The rest is the re-bake latency gap (9.8, R1), not the water [C].** From the generation frame on (Q2W40 1864, Q2P03
    1733, Q2P01 1810) about 628,000 px differ (any channel above 1; 709,000 above 0), all in the changed coast's land
    tiles, depth equal: live re-baked those tiles 2,184.8 ms after the publication (Q2P03 server.log 119.249 s, after the
    span ended), the player re-bakes them at once. Across the water switch the differing set does not move: Q2W40's frames
    1868-1870 have 369-374 px outside the union of 1864-1867's differing pixels, while the recorded frames change by
    865,333 px from 1867 to 1868 (`C:/c5-64bit-work/0622/switchmask.py`, copy in `server/evidence/0622/`). MC #12190
    accepted 0622's acceptance (3) on this reading: every frame byte-equal up to the re-bake gap, the copy switched at the
    frame live switched it.
  - *Was (2026-11-20 17:53, ticket 4914, MC #8606): "Gap (rule A), the water copy after `Waves_Reload`": the player took
    the load's last copy for every frame, so every 0615 member's span was cut to hold one water copy, and the switch was
    checked only live (I1, I3).*

### 9.5 Offline results through the hook [L offline, workstation; inputs read-only]

Two producer states: **4903's rule** (b6cf09570, runs 2026-11-19 01:45-02:00) and **after 4908** (team branch
b951c33a7: water, marsh and river-edge samples kept from the tap, 8.9; runs 02:05-02:15). *Corrected 2026-11-19 02:20
by w5-4907: the first version of this section had only the 4903 rows and concluded "about 1 % live"; that was the
producer before 4908.*

- **The hook equals 4903's runner** (4903's rule). `lt_hook_run.exe` (the hook's own path: feed records -> adapter ->
  producer -> `ApplyWindow`) on 4903's huge chain (copy of `C5H-VP8P-HUGE_0350`, hills to flat at (89, 56), then flat
  to mountain at (90, 56)): both live, the same windows and counts as 4903's `lt_producer_run` (D 15,226 / 15,829,
  ring 44,228), and the fields after each change **bit-equal** to 4903's `hugechain/field.<k>` files (`cmp`). The
  journal's round trip and a `Replay` from the load's field gave the same field (`equal: true`). Evidence
  `server/evidence/0465/4907/hookrun_huge.jsonl`, `hookrun_tiny.jsonl`, `hookrun.sh`.
- **4906's staged Tiny transitions.** `staged_offline.py` (field = 4906's stock `before` field of each state, table =
  4903's Tiny table of the same save with the earlier transitions applied, one hook per transition):
  - 4903's rule: fh_hills .. mw_add wait with **Water** (material 37 in the window); mw_flat NoChange. Evidence
    `staged_tiny_summary.json`.
  - **after 4908: fh_hills, fh_mountain, fh_hills_back, fh_flat, gd, nw_add, nw_remove, mw_add all live** (one window
    each, D 1,869-49,863 samples, ring 45,314-84,553, 60-206 ms in the producer); mw_flat NoChange (Kilimanjaro's
    pattern flags replace the plot's, P7). Every journal replayed equal. nw_add ran with the name `FEATURE_ULURU` as the
    art-define mapping makes it [I for the mapping in the game]. Evidence `staged_4908_tiny_summary.json`.
- **Live share per kind of change (MC #6636)**, `lt_hook_run --survey 150`: 150 land plots per kind (fewer when the map
  has fewer), every k-th candidate in map order (deterministic, no seed), each changed from the load's table and
  changed back, through the hook (held plots and all):

  | Map | producer | flat -> hills | flat / hills -> mountain | hills -> flat | waits by reason |
  |---|---|---|---|---|---|
  | Tiny 56 x 36 (LT_0008) | 4903's rule | 1 / 150 | 2 / 150 | 0 / 126 | Water 370, Marsh 48, RiverEdge 5 |
  | Tiny | after 4908 | **140 / 150** | **142 / 150** | **119 / 126** | Seam 25 |
  | huge 128 x 80 (HUGE_0350) | 4903's rule | 0 / 150 | 2 / 150 | 0 / 150 | Water 428, Marsh 17, TooLarge 3 |
  | huge | after 4908 | **149 / 150** | **150 / 150** | **150 / 150** | TooLarge 1 |

  With 4903's rule every logged Water wait (the first 50 per run) was material 37 inside the window. After 4908 the
  waits left are windows crossing the x seam (Tiny) and one window above 256 plots (huge). "Feature removed or added"
  (forest, jungle, marsh) makes no field change: NoChange, nothing to draw, as stock (4908's 8.11 counts 60 per map); a
  natural wonder added or removed changes the cell and goes live like a hill. Producer time per change after 4908: mean
  94-107 ms on Tiny, 213-241 ms on huge (single runs, an indication, COMMON 23). Evidence `survey_4908_tiny.jsonl`,
  `survey_4908_huge.jsonl` (with the live plots listed), `survey_tiny.jsonl`, `survey_huge.jsonl` (4903's rule),
  `survey.sh`.
- The live arms (lead4 #6747 (c)): 4906's own steps 1-8 on Tiny; (5, 22) kept as a spare for the video.

### 9.6 Live on the product route (acceptance (4), (5)) [L, 2026-11-19 02:43-03:50 EST, workstation, c5h-a]

**Setup.** Goldberg instance c5h-a (mods548: CP + VP + EUI 5.4.8, Vibrant Textures, D94), claimed under 0465 by
w5-4907 (MC-B's grant 02:42). Layout: a copy of `x64-c29442e8-p58724e0` (the pin's DLL, CvGameDatabase, lua; the pin
directory untouched) with server.exe and the proxy built from w5-4907 @dfdd8dfd4 (team branch 53a987e71 merged: 4904,
4905 @72a7791ac, 4908) by `build_server.py` 02:49:47-02:51:47 (server.exe SHA-256 220c6c38..., proxy 0321b374...; DLL
deps reused from 4812's build_all of c29442e8, copied into this ticket's folder; no link place: no LTO link), and the
composite by `shell\build.cmd` 02:19-02:27 (shell_composite.dll e34f0284...). `run_3308.py prep` (D33 minus Overlay,
`[Renderer] ServerRenderer = 1`), `server_cfg_extra` for the recorder. Save: `C5H-0301-4906-W5PL_0008`, a byte copy
of 4906's `LT_0008` (SHA-256 b2ceb663...; the prefix is 4906's because lt.py and scenes.py accept only those copies).
Chain `server/evidence/0465/4907/chain_4907.sh` (4906's chain.sh with this ticket's owner and saves, steps 1-9) with
`scenes_4907.py` (4906's scenes.py with Visual Checks issue 4907 and a `frontend.py restore` after each stop, MC
#6596): process 0 then one process per state, a scene bundle per process (span from frame 1500, 16 frames), and a
last process for the capture of state 9. 02:58-03:41, **CHAIN OK, no dialog box, no LOUD live-terrain line.** Then the
video (below), then the harness's stop, `frontend.py restore` (ok against its manifest), `server_cfg_extra` unset
(vpproxy.ini back to e1785cdd...), `run_3308.py restore` (9 files equal to the pre-prepare backup, 6 absent as before,
`provision.py verify` rc 0); claim released 03:50. Evidence: `server/evidence/0465/4907/live_server_lines.txt` (every
live-terrain and exefield line of the 11 processes), `chain_4907_run.log`, `live_vs_reload_regioncmp.json`; the
scenes (fields, pictures) under `F:/c5-64bit-work/0465/4907/scenes/`, the bundles under `.../bundles/` (outside git).

**The live share in the game** (the same transitions as 9.5's offline run, one hook per process):

| k | transition | kind | in the game | window (samples written / D / ring) | producer | apply on the render thread (of it: EXE write) |
|---|---|---|---|---|---|---|
| 1 | fh_hills | flat -> hills | **live** | 112,640 / 21,750 / 48,507 | 110 ms | 429 ms (4.2) |
| 2 | fh_mountain | hills -> mountain | **live** | 335,008 / 49,584 / 83,761 | 190 ms | 353 ms (3.9) |
| 3 | fh_hills_back | mountain -> hills | **live** | 112,640 / 21,649 / 48,507 | 122 ms | 288 ms (3.0) |
| 4 | fh_flat | hills -> flat | **live** | 335,008 / 41,168 / 83,761 | 256 ms | 396 ms (8.1) |
| 5 | gd | grass -> desert | **live** | 81,920 / 1,869 / 46,184 | 88 ms | 472 ms (2.7) |
| 6 | nw_add | natural wonder added (Uluru) | **live** | 122,880 / 17,993 / 44,580 | 92 ms | 475 ms (3.6) |
| 7 | nw_remove | natural wonder removed | **live** | 65,536 / 6,865 / 20,270 | 56 ms | 435 ms (1.6) |
| 8 | mw_add | flat -> mountain with Kilimanjaro | **live** | 112,640 / 25,572 / 54,658 | 112 ms | 246 ms (2.6) |
| 9 | mw_flat | mountain -> flat, wonder kept | NoChange (as stock: the wonder's flags replace the cell's, P7) | - | - | - |

8 of 8 drawing changes live; the producer was made per load in 268-546 ms on the worker thread. The window counts of
k = 1-6 and 8 equal 9.5's offline run of the same transition [L]; k = 7 differs (the live state after 4906's reloads
is not the offline one).

**Live equals reload?** (`regioncmp.py`, tol 1, rings 1-8, the EXE's field 2 s after the change (the hook wrote it,
4905) against the next process's load of the saved state; `compare_4907.py`):

| k | live vs reload: inside / ring 1 / ring 2 / ring 3-7 / outside (samples over tol 1) | before vs live (what the live change moved) |
|---|---|---|
| 1 | 4,095 / 18,454 / 20,011 / 9,900 / **0** | 2,678 / 14,042 / 20,011 / 9,900 / 0 |
| 2 | 4,096 / 17,661 / 26,376 / 65,000 / **0** | 2,678 / 13,789 / 26,376 / 64,998 / 0 |
| 3 | 4,096 / 16,019 / 19,207 / 9,941 / **0** | 2,679 / 11,705 / 19,207 / 9,940 / 0 |
| 4 | 4,093 / 16,934 / 23,164 / 63,846 / **0** | 2,679 / 12,695 / 23,164 / 63,846 / 0 |
| 5 | 1,071 / 4,950 / 6,487 / 259 / 1 | 1,071 / 4,950 / 6,487 / 259 / 0 |
| 6 | 3,984 / 9,917 / 5,431 / 5,168 / 1 | 2,104 / 6,394 / 5,431 / 5,130 / 0 |
| 7 | 3,978 / 9,999 / 2,149 / 38 / **0** | 2,111 / 6,507 / 2,149 / 0 / 0 |
| 8 | 4,001 / 9,985 / 11,181 / 3,369 / **0** | 4,096 / 15,090 / 11,181 / 3,369 / 0 |
| 9 | 0 / 0 / 0 / 1 (ring 8) / 0 | 0 everywhere |

- **Outside the window the live field equals the reload** (0 or 1 sample, the reload's own noise of P5).
- **Inside the window it does not**, in two ways [L for the counts, I for the reading]: (a) on the changed plot and in
  ring 1 the live piece is another variant than the reload's (D194 allows that); for gd (k = 5) the reload kept the
  old heights everywhere (its counts equal before-vs-live in every region) while the producer re-matched the plot's
  cells; (b) in rings 2-7 the reload's field
  equals the field **before** the change (the same counts in both columns), while the live window changed them: the
  EXE's own reload re-blends only the plot and its first ring there, the producer's window (the reach of the removed
  and added 512 pieces plus the 64-sample ring) is wider. So the acceptance's "live equals reload at tol 1 except the
  variant choice" holds outside the window and not in rings 2-7; that is a property of the producer's window (4903 /
  4908), not of the hook, which writes exactly the window it is given (the EXE write and the server's field agree:
  the EXE field dump after the change is what the hook wrote).
- Materials: 1,998-15,000 bytes differ live vs reload, all inside the window (the same reading).

**The render-thread apply** (lead4's question, from `render.log` of k = 2; the other changes alike):

| Part | ms (k = 2) | from |
|---|---|---|
| producer (worker thread, not the render thread) | 190 | the live line |
| the EXE field write (4905) | 3.9 | the exefield line |
| `ApplyWindow` + the region SHA-256 | about 1 [I: between the lines, no own stamp] | |
| `applyField`: the renderer's `setHeightField` (every height page of the map rebuilt, 11 mips) | about 180 [I: 257 ms between the EXE line and the residency's line, minus the residency's own 76.2 ms] | timestamps |
| `applyField`: the residency's `onField` (the whole field hashed per tile: 64 tiles changed) | 76.2 | the residency's line |
| `applyField`: water module, trees (`DerivedAfterWindow`: 23 of 37 built plots, 3.4 ms), farm, styles | about 90 [I: from the residency's line to the field line] | timestamps |
| **total in `Scene::poll`** | **347.8 (applyField) + 3.9 + ~1 = 353** | the field line, the live line |

- **The largest part is `applyField`**, inside it the renderer's whole-map height-page rebuild, then the residency's
  whole-field compare and the water module's field.
- **It blocks one frame**: the whole apply runs in one `Scene::poll` on the render thread, so the server draws no frame
  for about 0.25-0.48 s (render.log: frame 2079 dumped at 03:06:27.109, the next drawn frame 2100 at 03:06:27.508, 21
  EXE frames skipped; `host 1s` skipped_cams +33 in that second). The composite shows the server's last frame for that
  time: one long frame, not a stall spread over frames. The 64 changed tiles' re-bakes follow in `idle` within the
  residency's rate (400/s), spread over the next frames [I: no per-tile stamp read].
- To remove the stall: a region-only height-page update in the renderer (`render/src/terrain`: re-upload the pages and
  mips the box touches) and a region compare in the residency; or run `applyField`'s CPU parts off the render thread.
  Not in 4907's files; an offline measurement of `setHeightField` alone (render/tests, about 1 h) would confirm the
  180 ms split [I].

**D198 video** (acceptance (5)): `F:/c5-64bit-work/0465/videos/4907-fh_hills-live-tiny.mp4` (8.0 MB, H.264 by
OpenCV's Media Foundation writer, 960 x 570, 10 fps real time), made by `server/evidence/0465/4907/video_4907.py` after
the chain on the last process (state 9): the camera on FH (50, 16) by `lt.py look`, perf/evidence/0567's
`recorder.py` on the game window, 5 s before, `lt.py apply fh_hills` (flat to hills, the warrior on the plot), 10 s
after; captions name the phase and the time from the change (t = 0 at the Lua call, from its apply record's time
minus its 297 ms round trip [I, about 0.5 s]). The server drew the live result about 1 s after the call (render.log
03:43:11.5). **PrintWindow of the hidden game window returned a flat gray picture for 92 of the 162 frames** (E9's
PrintWindow artifacts); the MP4 drops them and holds the last good frame, so it shows about 4 different frames a
second. DIAGNOSTIC (D112): a picture of the product with the hook, not an acceptance shot of the shipped build; the
change is visible as the hill's shading on the warrior's plot and the trees rebuilt around it.

### 9.7 Open (2026-11-19 04:00)

- The window wider than the reload's change (rings 2-7, 9.6): for 4903 / 4908 to judge (whether the removed and added
  pieces' reach must be blended, or the reload's own reach can bound the window).
- *Done by ticket 4909 (8.14), 2026-11-19 12:30:* the render-thread stall of `applyField` (measured live: 0.22-0.42 s on
  Tiny, 1.88-2.02 s on huge) is replaced by a region apply of 7-15 ms on the render thread. (Was: "a region-only path in
  `render/src/terrain` and the residency, a follow-up".)
- The bundles: checked and one replayed (9.8). Open there: the live server's tile colour lags the field after a change
  (9.8), which an offline replay that settles each shot does not show.
- The art-define names: nw_add (Uluru) went live in the game; whether the EXE's reload took the same wonder flags is
  seen only as "inside differs" (a variant), not checked by the flags themselves [I].
- The headed D198 video on oldpc (MC #6861): a 4907 follow-up item after the hand-back (lead4, 04:05).

### 9.8 The recorded bundles offline (rule A) [L offline, 2026-11-19 03:52-04:20, workstation]

- `recorder_test.exe --check` (built from w5-4907 with the checker rules of 9.4) on all 11 bundles of the live run
  (`F:/c5-64bit-work/0465/4907/bundles/W5PT00..W5PT10_*`): **11 x `scenebundle: OK`**. Each carries
  `render_live_terrain=1` and its journal (55,340-55,776 characters); the eight of processes 1-8 hold 1 generation and 2
  frame entries (the change fell inside the 16-frame span), process 7's 2 jobs (a NoChange and the live one).
- `sceneshot.exe --bundle W5PT01_... --dumped --compare` (fh_hills): the player rebuilt the producer from the journal
  and applied **generation 1 at frame 2070 with its hash checked** (`live terrain replay: frame 2070: field generation 1
  (hash checked) in 554.0 ms`), the trees took the window (16 of 37 built plots, as live), the decal set after the
  change was the live one (651 decals, 21 tiles raised, as live). Result over the 16 dumped frames: the 8 frames before
  the change **colour and depth EQUAL**; the 8 frames after it **depth EQUAL** (the field and every model's geometry as
  live) and **colour DIFFERS** in 172,035-172,058 of 3,603,694 pixels (median 8, p95 42, max 134 over the differing
  ones). Exit 10 (Differs).
- Cause [L for the times, I for the reading]: the live server re-baked the 21 tiles of the decal generation that follows
  the change **3,627.5 ms after its publication** (render.log 03:02:04.504 for the change at 03:02:00.5), so its dumps
  of the next ~3.6 s show those tiles' old colour; the player settles every shot and had them re-baked 93 ms after the
  publication. So the field generation itself replays exactly (depth equal, hash checked); the live picture's colour
  of the changed tiles lags the heights by about 3.6 s on Tiny under the chain's load. Not separated: how much of the
  lag is the residency's rate (400 tiles/s, burst 16) against the 64 field-changed tiles queued first, and how much the
  machine's load.
- So rule A holds for the field generations: a bundle recorded across a live change replays with the generation at its
  frame and its hash; a P1 compare across such a change needs the live tile re-bakes in the bundle (the residency's
  state per frame) or a tolerance for the re-bake lag. Evidence: `server/evidence/0465/4907/replay01_lines.txt`.

### 9.9 The headed D198 video on oldpc (4907 follow-up, MC #6861) [L, 2026-11-19 11:08-11:26 EST]

- **Job**: `server/evidence/0465/4907/video_job.py`, a testbed job built on `tools/testbed/jobs/product_path.py`'s
  prep, stop and restore (unchanged). It ships the layout copy of 9.6 (server.exe 220c6c38..., proxy 0321b374...,
  composite e34f0284...) and the save `C5H-0301-4906-W5PL_0008`, whose SHA-256 the job checks. The window is put on the
  screen as 0567's runs had it (offscreen mode, `--park none`). Then `lt.py setup`, `lt.py look --plot 50,16`, and
  0567's `recorder.py` (BitBlt copy of the screen, 12 fps, 960 wide) around `fh_hills`, then `fh_flat`, each 5 s
  before and 10 s after the Lua call.
- **Three runs, all on MC-B's grants, all rc 0, all restored (`provision verify` rc 0); the first two are spoiled:**
  - 20261119-110821-fb9e and 20261119-111453-360e: `lt.py setup` reveals the map, and the game queues one 'Natural
    Wonder Found!' popup per wonder seen. The popup dims the map in every frame. The first run closed no popups
    (D114 missed). The second ran `popups.lua` once per recording, and the next queued popup came up at once.
  - 20261119-112115-90c3 (w5-4907 @97f1ec2a1): `popups.lua` runs in a loop until two calls in a row find nothing
    open (4 popups closed before recording 1, none before recording 2). The job stops before recording if a popup
    stays open or the loop hits its cap (lead5), and a BitBlt preshot is kept before each recording. **Both
    recordings are clean: 187 and 185 frames, all by BitBlt ('screen'), 12.0 fps.** Both changes went live in the
    game: generation 1 for flat to hills, generation 2 for hills to flat. The render-thread apply took 295 and 267 ms
    on oldpc; it was 308 / 298 and 321 / 283 ms in the first two runs.
- **Outputs (F: only, COMMON 27):** `F:/c5-64bit-work/0465/videos/4907-1-fh_hills-oldpc-headed.mp4` (11.0 MB) and
  `4907-2-fh_flat-oldpc-headed.mp4` (10.6 MB): H.264, real time, captions with the phase and the time from the Lua
  call. The preshots are `4907-<k>-...-oldpc-preshot.png`. The spoiled first run's MP4s are kept, renamed
  `...-run1-SPOILED-popup.mp4`. Evidence: `server/evidence/0465/4907/video_job_run3_summary.json` and
  `video_job_run3_live_lines.txt`.
- At lt.py look's camera height (the load default, zoom level 14.256, camera height 424) the change is small on the
  screen. It shows as the hill shading on the warrior's plot and the trees rebuilt around it.
- **Closer-camera re-run (lead5, MC-B's grant 11:30) [L, 2026-11-19 11:34-11:46 EST]:** `video_job.py --zoom min`. After
  `look`, the job posts 8 wheel ticks at the client centre (`tools/harness/input.wheel`, as `tools/scenario/runner.py`'s
  camera), then runs `look` again. It reads the zoom back with `shell/tools/gt_reader.py` into `change.json` and tries
  once more when the read-back is above 11.0. Zoom 11.0 is the clamp, camera height 261: 1.63 times closer than the
  default, at a steeper pitch. The offline check was a 1.63x centre crop of run 3's preshot.
  - 20261119-113412-0172 (@4142723c8), rc 0, restored, but recording 1 stayed at 14.256. **The wheel ticks went in
    while four 'Natural Wonder Found!' popups were still open (the zoom ran before the popup loop), and the EXE ignored
    them** [L]. Recording 2, with no popup open, reached 11.0.
  - 20261119-114105-b0c5 (@5aca22d82: the popup loop runs, then the zoom, then the popup check again), rc 0, restored.
    **Both recordings at 11.0: 186 / 187 frames by BitBlt at 12.0 fps.** Generations 1 and 2 were live; the apply took
    306 / 262 ms. The plot under the warriors visibly rises and falls, and the units ride up with it. The plot
    tooltip, opened by the posted mouse move at the centre, changes from "Grassland" to "Grassland, Hill" and back.
    The interface stays on.
  - Outputs (F: only): `F:/c5-64bit-work/0465/videos/4907-1-fh_hills-oldpc-headed-close.mp4` (11.0 MB) and
    `4907-2-fh_flat-oldpc-headed-close.mp4` (10.4 MB), with the preshots `...-oldpc-preshot-close.png`. The default-zoom
    pair stays as the fallback. Evidence: `server/evidence/0465/4907/video_job_close{1,2}_summary.json` and
    `..._live_lines.txt`.
  - Rule for later camera work: zoom only once no popup is open; a modal popup swallows the wheel [L].

## 10. Ice and coast waves after a live change (ticket 0586) [C for the code, L offline on recorded inputs, live in 10.6]

Written 2026-11-19 20:54- EST by impl-0586 (account B), branch `task-0586-ice-waves-reload` off main bc512f271. Route: the
Architect's note 5 (`architect-2026-11-18-live-terrain-route.md`): the EXE rebuilds its own ice mesh and coast-wave
chunks through its Lua globals `Ice_Reload` / `Waves_Reload`, called **by name** in the in-game UI context on the EXE's
main thread, with the ice bit written as data first (D4); approved for product by the MC (#6236 (3)), fail-closed on any
EXE but the pre-update DX11 build (PE stamp 0x546CD0A8; MC #7251, D200). Section 4.8 measured the calls and the ice grid
live on the stock route.

### 10.1 Who does what

| Part | File | What |
|---|---|---|
| The section | `proxy/include/vp_livereload.h` | `Local\VP64LiveReload.<EXE pid>`, 16 KiB, made by server.exe: one request at a time (`reqSeq`; never a second before `ackSeq == reqSeq`): flags ice / waves, the DLL's map W x H, the ice the EXE's grid should hold (one bit per plot, y * W + x), `expectFlips` (plots whose bit must change); the answer (`ackSeq`, a result per kind, bits set / cleared, the calls' microseconds, an error text) |
| The decisions | `server/src/render/srv_liveterrain_icewaves.{h,cpp}` (`IceWaves`, `LiveReloadLink`) | per load: what the EXE built from (the bake's table of the field's load) and what the field shows; per changed table: the ice grid to ask for, whether the waves must be rebuilt, the gate (10.2) |
| The scene glue | `server/src/render/srv_liveterrain_scene.cpp` | the load (`IceWavesLoad` next to `LiveTerrain::onField`), the field's table after each live result written or NoChange (`IceWavesDrawn`), every poll (`IceWavesPoll`: an answer, then the next request); after a good `Waves_Reload` the water tap's `requestSeq` + 1. State per scene in a map inside the file (no member in `srv_render_scene.h`, shared with 0561) |
| The proxy | `proxy/src/proxy_livereload.{h,cpp}`; a second frame hook in `proxy_ui_bridge.{h,cpp}` (`SetFrameLuaHook`: the drain passes its Lua state and restores the stack top after it); registered in `proxy_ui_bridge_rt.cpp` `Init` (always: it does nothing until the section exists; one `OpenFileMapping` every 60 frames until then) | on the drain of `Events.LocalMachineAppUpdate` in the InGame context: the EXE checks once per process, the grid write, the calls, the answer (10.3) |
| The water | `server/src/render/srv_water_scene.cpp` `useImage` | another image of the same load keeps the water clock (start time and start values): under D33 the tapped clock fields never advance, so starting again would jump the water's animation back to its load-time phase at every `Waves_Reload` (and at a device rebuild, which re-uses the image) |
| Switches | env of server.exe | `C5_RENDER_LIVE_ICEWAVES=0` turns it off (a test switch; live terrain itself has no VP64Options key, K4); **TEST ONLY** `C5_RENDER_LIVE_ICEWAVES_TEST_UNGATED=1` lifts the waves' gate (10.6's waves arm), with one LOUD line |

The EXE's rules the server mirrors [C, 4.1 and `polar-ice.md` 3; L, 4.8]: a plot is ice when its feature id is 0
(`FEATURE_ICE`; the load hands the grid every plot whose `ICvPlot1` feature is 0); the waves read every plot's terrain,
5 coast and 6 ocean. So what the EXE holds at a load is that load's table's ice plots and wave classes.

### 10.2 The gate (MC #7251, 2026-11-19)

The field the server draws waits for the reload on every land/water change and every change of a water plot's terrain
(4908's option A, 8.9; 4907's hook, 9.2). New surf around a coast the player still sees as before, or ice cleared over a
plot still drawn as frozen sea, would be a half-updated picture (D118). So:

- a plot is **held** while the field does not show its DLL state: `FieldRelevantChange(table the field was last made
  from, DLL's table)` (terrain, plot type, the natural-wonder bit and id, the river bit); the field's table is the load's,
  then the `sent` table of each live result written (`ApplyJob.ok`) or of a NoChange result;
- the ice of a held plot stays what the EXE has; every other plot's ice follows the DLL at once;
- the waves are rebuilt only when no plot whose wave class changed is held; one line names the wait (once, until it ends);
- when the field goes live for those plots (option B, 8.10) or at the reload, the ice and the waves follow. Today that
  means: **an ice feature added or removed on water goes live at once** (it is not a field change: 6.7 k = 10, 11 show 0
  field samples); **the coast waves follow only at the reload** (every wave-class change is a water plot's change).

### 10.3 The proxy's checks and the call [C, unit test]

Per request, on the main thread inside the drain (the InGame context's listener, so `LUA_ENVIRONINDEX` is that context's
environment):

1. Once per process: the EXE's PE time stamp must be 0x546CD0A8 and the bytes at VA 0x77CC42 (inside `Ice_Reload`
   0x77CC30, rebased) must be the push of the grid 0xD91270, the load of the ice object 0xD68F9C as `this` and a call
   (checked in `proxy/src/proxy_livereload.cpp`; D83 edit 2026-11-25, 0772: the byte pattern written out was replaced by
   this description), read only and after `VirtualQuery`. Either failing refuses every request of the process (kRefusedExe / kRefusedPattern), nothing
   read further, written or called.
2. Ice: `Ice_Reload` must be a function (the InGame environment, then the thread's globals) **before** any write; the
   grid's W and H (Ice + 0x94 / + 0x98) must be the request's map; the grid range (Ice + 0xA0, stride 255) must be
   committed PAGE_READWRITE memory inside the EXE's image; the plots whose bit 0x80 differs from the request must number
   exactly `expectFlips` (else kGridMismatch, nothing written: the grid is not the one the server's table describes).
   Then bit 0x80 of each such byte is set or cleared, the other bits kept, and `Ice_Reload()` is called under
   `lua_cpcall`.
3. Waves: `Waves_Reload()` under `lua_cpcall`.
4. Every failure is a `LOUD: livereload:` line in the proxy log and a result code; the server logs the answer (LOUD on a
   failure) and does not ask that kind again in the load (the ice and waves it draws stay as they are: no half-updated
   picture). The server takes the grid as written when bits set + cleared = `expectFlips` (a Lua error after the write),
   else as before. No answer for 10 s: one LOUD line (a proxy without 0586, or no in-game frame); the request stays
   posted.

### 10.4 Tests (2026-11-19) [C]

- `proxy/tests/livereload/test_livereload.py --scratch DIR`: `livereload_test.cpp` + `proxy_livereload.cpp` on stock Lua
  5.1.4, a stand-in EXE image (PE header, the code site, the ice object) in the test's memory and the real section of
  the test's pid: no section; the section found within the look interval; an ice request (1 set, 1 cleared, the other
  bits of a byte kept, `Ice_Reload` once); an answered request not served again; a wrong `expectFlips` (nothing
  written or called, LOUD); waves only; both; `Ice_Reload` missing (grid untouched); the InGame environment looked at
  first; a Lua error (counted bits, the message); a grid of another size; bad requests (0 columns, 255 rows); a
  read-only grid page; another PE stamp (both refused, nothing written or called); another code site; a section of
  another version. **33/33 on the proxy toolchain (clang-cl -m32, VC9) and 33/33 under MSVC x86 /fsanitize=address**
  (the canary caught).
- `server/tests/render/liveterrain/test_icewaves.py` (`test_icewaves.exe`, built by `test_liveterrain_build.py` beside
  `test_liveterrain.exe`): cases `decisions` (ice added / removed live; land to water: nothing asked, the waves' wait
  noted once; an ice plot made land: its ice kept; the field going live: ice + waves; coast to ocean: waves after the
  field; a land change: nothing), `failures` (a refusal is LOUD and latches that kind for the load; waves still asked;
  a new load starts again; a grid mismatch keeps the server's state; a Lua error after the write takes the grid as
  written), `link` (one request at a time, the bitmap, a restarted server re-opens the section and goes on from its
  sequence, sizes refused, another version refused), against a stand-in proxy: **50 checks, 0 failed**; pytest 3 passed
  plain and 3 under ASan, with `test_liveterrain.py` (3 + 3) unchanged.

### 10.5 Offline on recorded inputs: 4906's bundles [L offline, 2026-11-19 21:20 EST]

`server/tests/render/liveterrain/icewaves/icewaves_replay.py` reads the plot tables of a scene bundle's `feed.bin`
(4550's `c5data.feed_tables`), keeps the distinct ones and runs the decisions (`test_icewaves.exe --replay`) with the
field never advanced (every field change of these tables waits or is not a field change, 6.7-6.8). Inputs: the bundles
of 4906's product chain (6.8, `F:/c5-64bit-work/0465/4906/scenes/bundles/PT10..PT14_*`, Tiny, 56 x 36). Result
`server/evidence/0465/0586/replay_4906_bundles.json`:

| Bundle | Transition | Ice plots at the load | Decision |
|---|---|---|---|
| PT10 | `ice_remove` | **272** | ice: 1 flip, **(45, 4) cleared**; waves not asked |
| PT11 | `ice_add` | 271 | ice: 1 flip, **(45, 4) set**; waves not asked |
| PT12 | `lw` (LW1, LW2 to water) | 272 | nothing asked; "the coast waves wait for the reload: 2 of the 2 plot(s) whose wave class changed wait in the field (first (53, 13))" |
| PT13 | `wl` | 272 | nothing asked; the waves wait (1 plot, (51, 12)) |
| PT14 | `large` (7 plots to water) | 272 | nothing asked; the waves wait (6 plots, first (24, 10)) |

The 272 ice plots of the load's feed table are the 272 bits 0x80 that 4.8 read in the EXE's grid of the same save
[L, 4.8], and (45, 4) is the plot whose byte 4.8 cleared by hand: the server's table and the EXE's grid agree on this
save, which `expectFlips` checks live on every request.

### 10.6 Live on the product route [L, 2026-11-19 21:44-22:00 EST, workstation, c5h-c]

**Setup.** Goldberg instance c5h-c (C:, mods548: CP + VP + EUI 5.4.8, Vibrant Textures, D94; its DX11 EXE checked
first: stamp 0x546CD0A8, 16,982,584 bytes, build 4390913, `tools/harness/exe_build.py`), claimed by impl-0586 on MC #7276.
Layout: the batch-5 pin's (`F:/c5-64bit-work/0551/layouts/x64-1fd51353-p1e09fc7`: its DLL and stubs, COMMON 18) with
server.exe (2ffe4c2e...) and the proxy (129eec4d...) built release from this branch @6dbc69aae and the composite
(8f384c9f...) from the same tree (`server/evidence/0465/0586/build_0586.sh`). `live_0586.sh prep` (run_3308 prep: D33 minus
Overlay, `[Renderer] ServerRenderer = 1`; 4901's composite keys; `ice_tap=1`, `water_tap=1`), save `C5H-0301-4906-IWPL_0008`
(byte copy of `c5h-9028-tiny-t0008`, SHA-256 b2ceb663...). Three game processes, no dialog box, no LOUD line other than the
TEST ONLY switch's; `live_0586.sh end` both times: the harness's stop, `frontend.py restore`, run_3308 restore,
`provision_verify_rc 0`; c5h-c released 22:01. Lines: `server/evidence/0465/0586/live_lines.txt` (`collect_0586.py`).

| Process, step | What the server asked | The proxy's answer | Ice tap / server | Server's request to its ice drawn |
|---|---|---|---|---|
| 1, `ice_remove` (45,4) | ice: 271 plots, 1 to flip | ice ok, 0 set 1 cleared, `Ice_Reload` 132.2 ms | generation 2: VB 487,480 B, IB 579,228 B, **96,538 triangles** (247 fewer: 4.8's numbers exactly) | 1.2 s (request 21:46:47.708, generation applied 21:46:48.917) |
| 1, `ice_add` (45,4) | ice: 272 plots, 1 to flip | ice ok, 1 set, `Ice_Reload` 184.3 ms | generation 3: VB 489,112 B, IB 580,710 B, 96,785 triangles (the load's) | 1.3 s |
| 1, `lw` (LW1, LW2 to water) | nothing: "the coast waves wait for the reload: 2 of the 2 plot(s) whose wave class changed wait in the field (first (53, 13))" | - | - | - |
| 2 (TEST ONLY ungated), `lw` | waves: 2 plots of another wave class | waves ok, `Waves_Reload` 18.2 ms | the server raised the water tap's requestSeq: copy 2 of load 3 read (396,272 B against 396,304), the water scene used it: 3 chunks, **531 waves** (533 at the load) | 0.08 s (answer 21:51:04.428, copy read 21:51:04.506) |
| 3, `ice_remove`, `ice_add` (the pictures) | as process 1 | ok, `Ice_Reload` 214.1 / 146.9 ms | generations 2 and 3 as process 1 | 1.3 s each |

- **Ice matches the DLL live**: the EXE's own rebuild, tapped and drawn by the server about 1.2-1.3 s after the change (the
  ice tap's 30 settled frames, `polar-ice.md` 9). The `Ice_Reload` stall on the main thread: 132-214 ms on Tiny (4.8 measured
  80-225 ms over the Lua channel); a huge map is not measured.
- **The waves wait** as the gate says; with the gate lifted (TEST ONLY), `Waves_Reload` costs 18.2 ms on Tiny (the EXE
  reads every plot's terrain through the proxy) and the server takes the new chunks 78 ms later through the water tap.
- **Pictures** (DIAGNOSTIC, D112; popups closed in a loop before each, D114; issue 0586): process 3's `ice_remove` before
  `vc-20261119-215900-workstation-28ab`, live `-215913-...-0ebe`; `ice_add` before `-215933-...-beae`, live
  `-215946-...-a539`. The pole edge loses the plot and is re-laid along its visible length, as 4.8 saw on the stock route;
  `ice_add` brings the load's outline back. Process 1's pictures carry an ARTEFACT note: a 'Natural Wonder Found!' popup
  was open (`lt.py setup` reveals the map; the first `scenes_0586.py` closed popups once, now it loops until two calls in a
  row find none).
- `CvAssert.log` stayed 0 bytes in all three processes.

### 10.7 Not verified / open (2026-11-19 22:05)

- A huge map: the `Ice_Reload` stall (the builder's cost grows with the ice hexes: 8 chunks on huge) and the
  `Waves_Reload` stall (one cross-process plot read per plot) are measured on Tiny only.
- The water clock kept across a same-load re-copy: the code path ran live (process 2), but the tapped clock fields were 0
  in this configuration, so the continuity is shown by the code, not by a measurement of the water's phase.
- A field change that goes live next to ice (a hill raised beside the pole): the ice builder reads the field (Ice + 0xFF14,
  the water height, and the heights it displaces against); the gate asks no `Ice_Reload` for such a change, so the ice
  keeps the old field's edge until the next ice change or the reload [I: not seen; the server draws the EXE's mesh as is].
- Land/water changes (option B, 8.10): when they go live, the gate lets the waves and the ice of those plots follow without
  further code; not exercised.
- ~~The new Steam EXE (build 26577625, D200): the proxy refuses every request there (stamp check, unit test); not run.~~
  Corrected 2026-11-20 (ticket 0619): the gate is on the EXE build table now and runs on build 26577625; section 10.8.

### 10.8 Build 26577625 (ticket 0619, epic 0590; sixth lead-0590, 2026-11-20) [C for the code, L where marked]

**The gate on the build table** [C, unit test]. Until 0619, `CheckExe` (`proxy/src/proxy_livereload.cpp`) compared the PE
stamp with build 4390913's and the `Ice_Reload` site, the Ice object and the grid with that build's fixed addresses
(constants in `proxy/include/vp_livereload.h`), so after 0606's flip every request on build 26577625 was refused (loud).
Now: the build comes from the registry (`ProxyExe::BuildOfImage`, stamp and SizeOfImage); the three symbols are the
build table's `ice_reload`, `ice_ice` and `cam_grid` (`exebuild/symbols/shell.tsv`, rows the ice tap already carries;
no new row); a symbol the build does not carry is `kRefusedExe` naming it; the site is checked with the template
`68 R(cam_grid) B9 R(ice_ice) E8` (the ice tap's row) through `ProxyExe::CheckPattern`; the ice grid is written at the
build's `ice_ice`. The fixed constants are gone from `vp_livereload.h`; the offsets inside the Ice object (W +0x94, H +0x98,
grid +0xA0, stride 255) stay constants: 0582 found the Ice object's layout unchanged, and the grid check (W x H must be the
request's map, the flips must number exactly `expectFlips`) refuses any other layout before a write. Test
`proxy/tests/livereload/test_livereload.py`: 36/36 checks under both toolchains (clang-cl -m32 and MSVC ASan), two cases
new: an ice request served on a stand-in image laid out with build 26577625's table, and a dropped `ice_ice` refused.

**Live on build 26577625** [L, 2026-11-20 15:29-15:49 EST, workstation, Goldberg instance c5h-new-b, mods548 (CP + VP +
EUI 5.4.8), Vibrant Textures]. Layout: the release take of 0606 (`x64-1fd51353`, the flip; server.exe, composite as
taken) with the proxy rebuilt release from the 0619 branch (process 1: SHA-256 025f988d...; process 2, main merged with
0042's stage 3: 60f29ab5...); installed by `research/02-exe-internals/evidence/4967/lay.py prep` + `helper on` (the
installer's config form, no developer switch). Save: copy `C5H-0301-4906-IWPL_0008` of `C5H-0167-TINY_0008` (SHA-256
b2ceb663..., the same file as 10.6's). Transitions by `lt.py` (the game's own Lua). Driver: local
`C:/c5-64bit-work/0590/lead6_0619.sh`.

| Step (process 1) | Result on build 26577625 | Build 4390913 (for comparison) |
|---|---|---|
| boot | `exe build: build 26577625 ... known and supported` | - |
| `ice_remove` (45,4) | request 1: 271 ice plots, 1 to flip; **ice ok, 0 set 1 cleared, `Ice_Reload` 222.4 ms** | 10.6: the same, 132-214 ms |
| `ice_add` (45,4) | request 2: 272 ice plots; **ice ok, 1 set, `Ice_Reload` 540.4 ms** | 10.6: 146-184 ms |
| `lw` | "the coast waves wait for the reload: 2 of the 2 plot(s)" (the gate, as 10.6) | the same |
| `fh_hills` (50,16) | 4905's write: `exefield: Written: 1 rect(s), 112640 samples, 47163 heights and 1831 materials changed`; field generation 1, box 3008,1024-3360,1344, **sha256 d51d186d...** | **the same sha256 d51d186d...** in four runs: `server/evidence/0465/4907/live_server_lines.txt`, `4909/live_server_lines.txt`, `4909/batch5_smoke_server_lines.txt`, `0587/live_lines.txt` |
| `fh_flat`, `gd` | exefield Written (46,576 / 15,262 heights); field generations 2 and 3 | - |

0 LOUD and 0 FALLBACK lines in vpproxy.log, server.log and render.log; no dialog box. The ice draw followed (the shadow
line's `ice=ACTIVE gen=3`: the two reloads' generations, as 10.6). Process 2 (15:50-15:56, the proxy with main merged,
0042's stage 3 in it): `ice_remove` ok (1 cleared, `Ice_Reload` 131.1 ms), `ice_add` ok (1 set, 213.7 ms), `fh_hills`
exefield Written with the same sha256 d51d186d...; 0 LOUD, 0 FALLBACK, no box. Both processes: the harness's stop,
`frontend.py restore`, `lay.py restore` (backup by SHA-256, `provision.py verify` rc 0), c5h-new-b released.

What this settles:
- **4905's EXE field write works on build 26577625** [L]: the target opened (`OpenExeFieldTarget`'s pointer check,
  the constants block read), three windows written.
- **`lt_ginv` is right on build 26577625** [L, by an identical output]: the producer maps samples to plots through
  `Ginv` (`render/src/liveterrain/producer.cpp`, `MapFrame::fromConstantsBlock` and the sample loop) read from the
  build's block; the field window of `fh_hills` on this save hashes the same on both builds. Before: [I] (exe-build-table.md
  13.3, no reference of its own).
- The `Ice_Reload` stall is in the same range on Tiny (222 and 540 ms, one sample each; not a timing measurement).

Not verified on build 26577625: `Waves_Reload` (the waves wait by design until land/water changes go live: epic 0615);
a huge map; the ungated waves path (10.6's TEST ONLY process 2 was not repeated).

### 10.9 Land/water windows and the gate (ticket 4914, epic 0615; w2-4914, 2026-11-20) [C for the code and the tests, L where marked]

Scope: what 0586's gate does once land/water changes can go live (4911-4913 make the windows). Written by w2-4914.

- **The waves and the ice of the changed plots follow without new code** (10.7's last item, now tested). While a
  plot's field change waits, it is held. Once the field shows its table (`onDrawn`), the next request asks
  `Waves_Reload` for the plots whose wave class changed, and the ice bit follows the DLL.
- **An ice plot made land keeps its ice until its feature goes, as a reload does [C].**
  - `CvPlot::setPlotType` and `setTerrainType` clear no feature (the DLL fork's i0 checkout, `CvGameCoreDLL_Expansion2/CvPlot.cpp`, read 2026-11-20; only
    `CvPlot::erase`, the feature-removal paths and `Validate`'s mountain rules call `setFeatureType(NO_FEATURE)`). So a
    plot turned to land by `SetPlotType` alone still carries FEATURE_ICE.
  - The EXE's grid takes every plot whose feature is 0 at a load, with no plot-type test (`polar-ice.md`: 0x60ABB0 >
    0x639850), so a reload of such a table shows ice on that land as well [C for the fill; I that the builder 0x4956F0
    draws ice on a land hex, not seen].
  - The server keeps the EXE's rule (ice = feature 0; lead-0615, 2026-11-20 17:32). The ice clears when the mod or the
    DLL removes the feature (`SetFeatureType(-1)` or `erase`); the feature is not a field change, so the ice then
    follows at once.
  - For acceptance (2) the `ice_land` case "follows the reload".
- **A live window beside ice asks `Ice_Reload` (10.7's open item, built).** The EXE's ice builder reads the field
  around its ice hexes, so the server now asks for an `Ice_Reload` when a written window lies beside the ice.
  - `IceWaves::onDrawn(sent, written)` takes the sample rectangles the window wrote. When one of them reaches within
    `kIceFieldRing` (1) plots of a plot the EXE holds as ice (x widened by one more plot for the odd rows' half-plot
    offset, x wrapped, y clipped), an `Ice_Reload` is owed.
  - The next request then asks for ice with `expectFlips` 0 when no bit changes. The proxy writes nothing and calls
    it: the count check of 10.3 passes at 0 (`proxy_livereload.cpp` `WriteIce`, read 2026-11-20).
  - A request covers the windows written before it was posted. A window written while it is in flight is owed again
    after the answer. A new load clears what is owed. A failed ice kind stays latched for the load (10.3).
  - Counter `iceRefreshes`.
  - The refresh is part of the land/water step (MC #9538 item 4, 2026-11-21). With `C5_RENDER_LIVE_LANDWATER=0`,
    `IceWaves::setFieldRefresh(false)` makes `onDrawn(sent, written)` the same as `onDrawn(sent)`, as before 0615 (8.17's
    switch-off paragraph).
  - **Wired (ticket 4914 Part B, w6-4914b, 2026-11-20 21:20) [C, the code; server.exe build and live: not yet].** The
    apply loop of `srv_liveterrain_scene.cpp` calls `IceWavesDrawn(this, sent, job->live[k].out.window)` for each change
    it wrote: the change's own rectangles (its part of `job->rects`), converted to `IceWaves::SampleBox` (x0, y0, w,
    h), so a change beside the ice counts one `iceRefreshes`, and a job of several changes does not count one per change.
    The "nothing to draw" call (NoChange) keeps `onDrawn(sent)`. *Was: "Not wired yet: the scene glue still calls
    onDrawn(sent) ... W2 makes it once 4912 is on the team branch".*
- **Tests.** `test_icewaves.exe` case `landwater` (2026-11-20 17:35, workstation):
  - land made water, drawn away from ice: waves only;
  - an ice plot made land with FEATURE_ICE kept: ice (no flip) plus waves, and the ice kept;
  - the feature then removed: ice cleared at once;
  - an ice plot made land with the feature removed in the same change: held, then ice plus waves together;
  - a window beside the ice: ice with `expectFlips` 0, the grid untouched;
  - a window far from the ice: nothing;
  - a window written during a request in flight: a second `Ice_Reload`;
  - a window across the x seam;
  - a new load;
  - a failed kind not asked again;
  - a mis-sized table ignored.

  Results: 83 checks, 0 failed, all four cases PASS. pytest `test_icewaves.py` + `test_liveterrain.py`: 6 passed
  plain and 6 under ASan (`test_liveterrain_build.py --asan`).

**Known differences for the candidate's list (MC #8589; 2026-11-20 17:50) [C for the rules, I for the look: not seen
yet; 4912's `ice_land` scene is the check].**

1. **A plot turned from ice-covered water to land.**
   - **The DLL.** `SetPlotType` keeps FEATURE_ICE on the plot, so it is still an ice plot in the game's rules. This is
     not the patch's doing.
   - **While the change waits** (before 4913 makes land/water windows live): the field, the ice and the waves stay as
     loaded. The player sees the frozen sea until the reload.
   - **Once the window is live:**
     - the field shows the new land, and the waves follow (`Waves_Reload`);
     - the EXE keeps the plot in its ice grid, because the feature is still ice, and `Ice_Reload` rebuilds the ice
       over the new field (the scene call is wired since 4914 Part B, 2026-11-20);
     - the ice mesh lies at the water height (its floes and skirts are displaced around it, `polar-ice.md` 3), so on a
       land hex that rises above the water the ice is under the ground and hidden [I];
     - at the shore, where the new beach slopes below the water height (the strips' waterline is at 0.19836 of the
       range), the edge of the ice can show through the beach as white patches [I].
   - **A reload shows the same thing**, because the EXE's grid takes every feature-0 plot. The ice disappears from the
     picture only when the mod or the DLL removes the feature: `SetFeatureType(-1)`, or `erase`. Then the ice follows
     at once, live, which is not a field change.
   - **Mods.** For a mod that turns ice to land, the clean way is to remove the feature in the same change; the patch
     then shows no ice there at once.
2. **The `Ice_Reload` owed beside the EXE's ice.**
   - **Before the scene call was wired (until w6-4914b, 2026-11-20 21:20).** A live window beside the ice left the ice
     mesh built against the load's field (10.7). Where the ground rose under the ice edge, the floes kept their old
     skirts: the ice edge could float above, or sink into, the new ground for about one plot [I].
   - **Wired (the call above; live not run yet).** Each such window costs one `Ice_Reload` on the EXE's main thread: 132-214 ms on Tiny in 10.6, so
     a single hitch per change beside the poles. huge is not measured (8 chunks; probably longer).
   - The ice is redrawn about 1.2-1.3 s after the change (the ice tap's settle, 10.6). During that time the player
     sees the new ground with the old ice edge.

## 11. The live acceptance of land/water changes (ticket 4915, epic 0615) [I, the runbooks; results in 11.4 when run]

Written 2026-11-20 18:50 EST by w4-4915 (account B), branch `w4-4915`, before any run (no instance used). It covers
acceptance items (1)-(4), (6), (7) and the pictures of (8) of epic 0615. The drivers are in `render/evidence/0615/4915/`:

- `cases_tiny.txt`, `cases_huge.txt` and `cases_waves.txt`: the changes, in order.
- `scenes_4915.py`: one case at a time. It is a layer over W3's `scenes_4912.py`, which is imported, not copied.
- `collect_4915.py`: the cost per change and the LOUD, FALLBACK and box checks, read from the logs.
- `accept_4915.sh`: the modes below.
- `test_4915.py`: offline tests, 14 passed at 18:45.

`accept_4915.sh dry` checks everything that needs no game. With W3's 4912 files present (a trial merge at 18:44, then
aborted) it also confirmed that `scenes_4912` loads with issue 4915. **Not run against a game yet.**

The prep and end modes call W3's `chain_4912.sh` modes through the overrides `C5_CHAIN_OWNER`, `C5_CHAIN_WORK`,
`C5_CHAIN_OUT`, `C5_CHAIN_TREE` and `C5_CHAIN_SEED`. W3 adds those after I1 (W3, 18:40). Until then the prep and end
modes refuse to run.

### 11.1 Runbook on build 4390913 (window I3, Sat 2026-11-21 18:00 - Sun 02:00, workstation)

**Instance and build.**
- A Goldberg instance from MC-B, requested by START-REQUEST through lead-0615. Claim it first:
  `python -I tools/harness/pool.py claim --name INST --ticket 0615 --owner w4-4915`.
- `accept_4915.sh check INST` must print EXE build 4390913 (stamp 0x546CD0A8). If it prints anything else, stop (D200).
- c5h-new-a/b are never ours.
- An instance on F: launches only when F:'s read latency is under 50 ms, measured before every launch (the driver does
  it).
- The candidate's server tree: `C5_4915_TREE`, by default `C:/c5-64bit-work/0615/w6`, the 0.1b-pin tree built by
  `server/evidence/0615/4914/smoke_4914.sh build` (server.exe and the proxy from the shared pins
  `C:/c5-64bit-work/keep/pins-0.1b`, MC #8815; the composite; `layout/` with `SHA256.txt`). Rebuild it with that mode at
  the team branch's head before the run. *Corrected 2026-11-20 22:15 (w6-4914b; MC #8815 (3): the old-pin
  `C:/c5-64bit-work/0615/srvtree` is removed); was: "by default the team's srvtree, built by build_4912.sh".*
- `dry` prints the SHA-256 of server.exe, the proxy and the composite. Those hashes go into 11.4.
- `C5_4915_WORK` must be a fresh folder for each prep: `runner.py` and `run_3308` refuse a used one.

**Saves.** The originals are never touched; the copies are made by `scenes_4912.py copysave` in the prep modes.
- Tiny: `c5h-9028-tiny-t0008` (SHA-256 b2ceb663...c29163, 56 x 36, mapRandomSeed 0x3DFC9816, 6.9). Copies
  `C5H-0615-4912-APT00` (product) and `-AST00` (stock).
- huge: `C5H-VP8P-HUGE_0350` (c499d3c6...88c3, 128 x 80, seed 0x067F5502). Copies `-APH00` and `-ASH00`.
- The checkpoint saves are `-APT_S1` .. `_S3`, `-APH_S1` .. `_S3`, `-AST_S1` .. `_S3` and `-ASH_S1` .. `_S3`. They
  use 4912's prefix, which `lt.py` and `scenes_4912` accept.

**The changes (22 on Tiny, 23 on huge, in one process each, each from the state the one before leaves).**

| | Tiny (`cases_tiny.txt`) | huge (`cases_huge.txt`) |
|---|---|---|
| lw | t01 `apply lw` (LW1 river plot (53,13) and LW2 forest plot (55,15)) | h13: the h08 plot back to water |
| wl | t02 `apply wl` (the capital's bay, (51,12)) | h14: the h01 lake back to land |
| lake | t03, t09, t13, t19 (picked; the capital, then `--near` anchors) | h01, h07, h11, h16, h21 |
| coast_land | t04, t10, t14, t20 | h02, h08, h12, h17 |
| mouth | t05, t11, t17 | h03, h09, h15 |
| ice_land | t06, t12, t18 | h04, h10, h18 |
| large | t21 (the largest allowed landmass, 6.7) | h22 (it may wait `TooLarge`, 8.17: that is a named outcome) |
| next to a natural wonder (MC #8589) | t07 (39,12) flat to water beside Mt Kailash (38,12); t08 (26,9) sea to land between the reefs (26,8) and (25,9) | h05 (112,41) sea to land beside Gibraltar (112,42), the coast-class wonder 4913 did not run; h06 (77,35) flat to water beside Lake Victoria (76,35); h23 (112,43) coast to grass beside Gibraltar (MC #8641 condition 4, below) |
| reverts | t15 (t03's lake back to land), t16 (t04's new land back to water), t22 (LW1 back to desert) | h19 (h02 back to water), h20 (h05 back to water) |

- The wonder plots come from 4902's plot tables of these saves (`F:/c5-64bit-work/0465/4902/`, read-only).
- huge has no `lt.py setup`: it would reveal the whole 128 x 80 map and place Tiny's studio warrior. Instead the
  plots within 3 of each case are revealed before its pictures (`--reveal 3`).
- `cap+DX,DY` anchors are taken from the capital of the save that is loaded.
- A case the mod refuses (lt.py exit 4: no candidate, a city, the wrong starting state) is recorded as refused and
  skipped, and the session goes on. Any other failure ends the session.

**For each wonder case, the verdict is one of three (MC #8589):**
- **drawn right:** the piece's materials and heights equal the reload's within 8.10's tolerance;
- **refused:** a wait with its reason;
- **wrong:** the change must then be refused loudly. Report it to lead-0615 at once.

4913 found the reef drawn right offline (8.17 (b)). Gibraltar (h05) is the open one.

**h23, a change beside a natural wonder on huge with pictures (MC #8641 condition 4; W5, 2026-11-20 about 19:05).**
It runs with branch w5-0615's producer option `protectWonders`, which is on by default. The plot is the one of W1's
`huge_gib` table (`C:/c5-64bit-work/0615/w1/huge_gib/after.txt`). What W5 expects, from its offline run [L offline, W5's numbers;
not checked by W4]:
- Gibraltar's own hex keeps the load's ground, and the coast around it is redrawn.
- There is no step at the hex border: the largest |dz| across it is 2,030 u16, against the load's own 2,090.
- About 487 samples of the old shoreline stay inside the 32-sample feather around the wonder.

Every wonder case gets three pictures: before, live and after the reload (`--before` for kind `wonder`). A reload may
show another variant of the wonder, as stock does.

**Order and commands.** Run from the worktree root. Each step ends non-zero at the first failed step or an open box.
A file `STOP` in the output folder ends it before the next step.
```
bash render/evidence/0615/4915/accept_4915.sh dry INST                       # hashes, tests, claim
bash render/evidence/0615/4915/accept_4915.sh check INST                     # build 4390913
bash render/evidence/0615/4915/accept_4915.sh product-prep INST              # run_3308 prep (backup), keys, empty extra.cfg, copies
python -I perf/quietlock/quietlock.py run --take --minutes 90 --who w4-4915 --what "4915 product tiny (cost per change, DIAGNOSTIC)" -- \
  bash render/evidence/0615/4915/accept_4915.sh product INST tiny            # 1 process + 3 reloads; about 60 min [I]
C5_4915_FIRST=0 python -I perf/quietlock/quietlock.py run --take --minutes 150 --who w4-4915 --what "4915 product huge" -- \
  bash render/evidence/0615/4915/accept_4915.sh product INST huge            # about 90-120 min [I]
C5_4915_FIRST=0 bash render/evidence/0615/4915/accept_4915.sh waves INST tiny    # the Waves_Reload frames (below)
C5_4915_FIRST=0 bash render/evidence/0615/4915/accept_4915.sh waves INST huge
bash render/evidence/0615/4915/accept_4915.sh product-end INST               # stop after the newest save settles, restore
C5_4915_SINCE=<yyyymmdd-hhmm of the product start> bash render/evidence/0615/4915/accept_4915.sh collect INST tiny   # and huge
bash render/evidence/0615/4915/accept_4915.sh stock-prep INST                # runner.py prepare (backup, 32-bit pin), launch
bash render/evidence/0615/4915/accept_4915.sh stock INST tiny                # the product's plots replayed; 1 + 3 processes
bash render/evidence/0615/4915/accept_4915.sh stock INST huge
bash render/evidence/0615/4915/accept_4915.sh stock-end INST
# MC #6596: backup.py verify against both routes' pre-prepare backups; then
python -I tools/harness/pool.py release --owner w4-4915 INST
```
If the window runs short, the order of value is: product Tiny, the Tiny waves check, product huge, stock Tiny, stock
huge. The stock route only gives the "stock after reload" picture, which a reload of the product route nearly stands
in for (2.2: the product draws a reload as stock does).

**Cost per change (acceptance (6)), as 4909 measured it (8.14.5).**
- **Render-thread time.** From the scene's apply line: "render thread N ms = step 1 + step 2 (written in step 2: EXE
  write, windows; ...)", in the 0587 form, 8.14.8. The pages built on the worker are reported separately.
- **Frames.** From render.log's `host 1s` counters: the cameras skipped and the frames drawn in the second or seconds
  of the apply.
- **What `collect_4915.py` matches.** Each apply goes to the case whose change time (`<id>_case.json` `at`) comes
  last before it. Each case also gets the gate's answer line (`live ice/waves: ... Waves_Reload N ms`) and the scene's
  per-change lines (a wait names its reason).
- **Nothing is captured for 6 s after a change** (`--wait 6`). The EXE field read and the screenshot would otherwise
  fall into the measured second.
- **The bar.** 4909 measured 7.4-14.6 ms per height change on Tiny and 7.4-11.6 ms on huge, with no camera skipped. Its
  acceptance was "a hitch under about 50 ms per change on huge".
  - A land/water window is as large or larger: 126,111-378,925 samples on Tiny (8.17's table), against 4909's
    65,536 (huge, 256 x 256) to 324,224 (Tiny k2, 544 x 596) (8.14.4's bench rects). The strips are added on top. So
    the render thread's share is expected to grow with the window's pages and tiles [I].
  - The producer runs on the worker (121-227 ms offline in 8.17) and is not render-thread time.
- **The times are DIAGNOSTIC (D106).** They are taken as registered jobs (`quietlock.py run` WITHOUT `--take`) and
  labelled disturbed when other heavy jobs run. (Corrected 2026-11-21 18:22 by lead7-0615 with MC-B: was "under the
  quiet lock (`--take`)"; COMMON 23 allows a hold only in an MC-scheduled window, none was granted for I3, and
  `--minutes` is capped at 30, so the 90/150 in the commands above are not used.)

**Crash, box and LOUD checks (acceptance (7)).**
- **Boxes (D155).** `frontend.py ingame` runs after every reload and every change: exit 8 means a box is open. The
  driver then stops and reports to lead-0615 within the turn.
- **The dialog watcher** (`dialogwatch.py`, COMMON 29) records every box in the run folder as `dialog_open.json`.
  `collect_4915.py` counts those records.
- **The patch's own popups.** Any D118 error popup is a failure, not a pass.
- **Crashes (COMMON 30).** A crash gets a root cause in research/ (D185). Only the owner closes a fatal box, by the
  permitted path.
- **LOUD and FALLBACK lines.** Every `*.log` of the run folders is read. The pass is 0 beyond the known lines:
  - the boot's interface-version line, which every run has (8.14.5);
  - the proxy's `TEST ONLY` lines for `server_cfg_extra`. They are expected on every product process [I: the
    proxy's line for an empty file is not seen yet], because the prep names an `extra.cfg`. It stays empty until the
    waves mode writes the recorder keys into it.

**Pictures (acceptance (8)): a triplet per case.**
- The three pictures of a case are:
  - **stock after reload:** `stock/<id>_reload.png`, from the stock session's checkpoint reloads;
  - **product live:** `product/<id>_live.png`, 6 s after the change;
  - **product after reload:** `product/<id>_reload.png`.
- Every capture, before its picture:
  - closes the EXE's popups (D114, scenes_0586's popup loop);
  - dumps the EXE's field (`<tag>_field.npz`);
  - puts the camera on the case's plot.
- They go through Visual Checks: issue 4915, labelled DIAGNOSTIC. The product layout is a dev layout, not the shipped
  installer's (D112).
- Files go under `F:/c5-64bit-work/0615/4915/<map>/<route>/`. Nothing goes into git (COMMON 27).
- `--before` adds a picture before the change. It is off by default, for time.

**The live check of the frames around a Waves_Reload (MC #8606; 9.4's gap note).** A rule A PASS on a cut span does
not show the switch, so this is checked live.
- **What runs.** `accept_4915.sh waves INST MAP` reloads S1 with a bundle (`--after 1500 --frames 40 --dump all`). It
  waits for 4 dumped frames, then makes the change `wv1` (`cases_waves.txt`: `coast_land` beside the capital).
  - With every frame dumped, the recorder spaces the span's draws about 250 ms apart (offline-scene-harness.md, "Sizes
    and limits" [L]). So 40 frames are about 10 s of game time: about 1 s before the change, the gate's
    `Waves_Reload` (expected about 1-1.5 s after the change [I]) and about 8 s after it.
  - The dumps are the server's own frames of the span, "the live side of offline equals live" (the same file).
  - Size: about 1.2-2.7 GB on F: [I, from 281 MB / 544 MB per 8 frames].
- **What is checked, after the process ends.** All offline, no slot needed (no sceneshot replay):
  1. server.log has the gate's answer line "waves ok (Waves_Reload N ms)", and no LOUD line.
  2. The bundle's `water/` has two copies of the load (`load<NNNN>-c0`, `-c1`), with the second copy made after the
     answer.
  3. The dumped frames before the answer's time show the old coast waves; those after show the new.
  4. `recorder_test.exe --stamp` and `--check` pass on the bundle (6.8).
- **Status of the bundle.** It is evidence, not a library member, and it is not replayed. A replay would draw the
  later copy for every frame (9.4), which is what this check does not trust.
- **Scene players** are not part of this runbook. Any later replay of these bundles is a rule A run, in a slot from
  MC-B through lead-0615. It sets `C5_SCENE_ASSET_ROOT` only for `scenerun` or for `run_all`'s offline-equals-live,
  never globally: with it set, sceneshot_test's own `--install` cases fail (MC-B, 2026-11-20 18:57).
- The same check on build 26577625 is in 11.2.

### 11.2 Runbook for the build-26577625 session (an ordinary pool instance, by START-REQUEST to MC-B)

**Who runs it.** Corrected 2026-11-20 19:14 (MC #8680; replaces "account A runs this session through the MC"): since
0606 (15:01) build-26577625 instances are ordinary pool instances for both accounts. Team 0615 runs this session itself:
lead-0615 asks MC-B by START-REQUEST like any instance, and the worker claims the named instance first (`pool.py claim
--name INST --ticket 0615 --owner <worker>`). c5h-new-a/b are held by lead-0590 until Sat 18:00.
- Never read the Steam install, and never launch through Steam (D200).
- No address or pattern table of this build goes into anything written (D202). The drivers read the EXE only through
  the harness's build-aware readers (`gt_reader`, `tools/exebuild/resolve.py`), whose table stays local.

**Setup.**
- The layout as 10.8 laid it on build 26577625: 0606's release take with the candidate's server.exe, proxy and
  composite, installed in the installer's config form (`research/02-exe-internals/evidence/4967/lay.py prep` +
  `helper on`, no developer switch).
- The EXE build line must read `exe build: build 26577625 ... known and supported`.
- `accept_4915.sh check` prints the build. On this instance it must say 26577625. `chain_4912.sh`'s prep modes assume
  4390913's pin layout, so the prep is `lay.py`'s. The case and capture modes (`scenes_4915.py change`,
  `shots`) are build-independent: they use the game's Lua and the harness's readers.
  *Corrected 2026-11-21 00:55 (W8, w8-sat): the prep is no longer by hand. `smoke_4914.sh` takes
  `C5_SMOKE_PREP=lay` (was: "so the prep is `lay.py`'s, by hand").*

**The land/water smoke on build 26577625 (window Sat 2026-11-21 07:30-08:30, c5h-new-a; MC-B #8872; W8) [I, the
runbook, written 2026-11-21 00:55 before the run; results below when run].** It is 4914 (1)'s smoke (8.17) in the
same shape: process 0 (load, `lt.py setup`, save), then lw, wl and coast_land, each in a new process with a bundle.
It is the first `Waves_Reload` on this build (MC #8527 above).
- **Layout.** `C:/c5-64bit-work/0615/w8/layout`, made by `smoke_4914.sh build` at w8-sat @6eebdbc53. That is the
  team branch @e9b328dab (w6-4914b + the w7-ridge fix + main with 0624) merged into w8-sat. It is built on the 0.1b
  pins (release, no flag).
  - server.exe 81f10ca8...; proxy 98a076cc...; vp64helper 38253a4e...; composite 39eb8380...; the pins' x64 DLL
    b84aa3e9...
  - W7's own build of the same code gave server.exe f01a2df1... A server.exe built in another tree hashes differently.
- **The prep variant (`C5_SMOKE_PREP=lay`), the installer's form.**
  - Saves snapped (`saves_snap.py`), then `lay.py prep` (backup by SHA-256, the D28 layout, vp64helper), then the
    composite with its own `shell_composite.ini` and the manifest, then `lay.py helper on`. There is no developer
    switch.
  - The compositor is laid in because 0624's boot guard (main 2f395cc6d) gives a D33 layout without it NO PLAY.
  - The recorder's `server_cfg_extra` is set. Its LOUD `TEST ONLY` lines are the known ones.
  - Not used: `crashlogs\shell.helper` and `server_config=`, the test hooks that skip 0624's guards.
- **The save.** `c5h-9029-tiny-t0008` is not on c5h-new-a [C, read 2026-11-21 00:40]. The identical file is put in
  under a new name, `C5H-0615-4912-W8PL_0008`, after the claim:
  - SHA-256 b2ceb663...c29163, a local copy taken from c5h-a;
  - never over an existing file; the copy's hash is checked;
  - lead-0615's yes, 2026-11-21 00:4x.
  - At the end `saves_snap.py restore` moves the copy and every save the run made to the work folder. A save it
    overwrote is put back by hash, so the instance ends with its saves as snapped.
- **The producer wait.** After every load the driver waits for `producer made ... land/water changes can go live`
  (`wait_producer.py`), because the input read is not hidden by the load screen (8.17). The ms after the field go to
  `WORK/producer.jsonl`.
- **Commands** (the worktree root, after `pool.py claim --name c5h-new-a --ticket 0615 --owner w8-sat`):
```
export C5_SMOKE_OWNER=w8-sat C5_SMOKE_PREP=lay C5_SMOKE_TAG=W8 C5_SMOKE_TREE=C:/c5-64bit-work/0615/w8 \
  C5_SMOKE_WORK=C:/c5-64bit-work/0615/w8/smoke-a C5_SMOKE_OUT=F:/c5-64bit-work/0615/4914b/w8-26577625 \
  C5_SMOKE_SAVE_FILE=C:/c5-64bit-work/0615/w8/saves/c5h-9029-tiny-t0008.Civ5Save
bash server/evidence/0615/4914/smoke_4914.sh dry                 # DRY rc=0 at 00:50 (all items, the save file's hash)
bash server/evidence/0615/4914/smoke_4914.sh check c5h-new-a     # "EXE build 26577625 as expected" at 00:51 (read only)
bash server/evidence/0615/4914/smoke_4914.sh prep c5h-new-a
bash server/evidence/0615/4914/smoke_4914.sh run c5h-new-a       # ~20 min; F: latency checked before each launch
bash server/evidence/0615/4914/smoke_4914.sh end c5h-new-a
bash server/evidence/0615/4914/smoke_4914.sh verdict c5h-new-a
python -I tools/harness/pool.py release --owner w8-sat c5h-new-a
```
- **Expected [I].**
  - The same lines as the 4390913 smoke: the step on, one apply per case and no land/water wait.
  - The gate's `waves ok (Waves_Reload N ms)` and the water tap's second copy per case.
  - 0 LOUD beyond the known lines; `exefield: Written` per case (10.8).
  - lw: no ridge of the old shore east of (53,13). The picture is cut with W6's own box: (600,300)-(1150,705) of the
    1277 x 705 captures, found by an exact match, 0.0 mean difference. The tool is `C:/c5-64bit-work/0615/w8/zoom.py`.
- **Rescheduled (2026-11-21 11:58).** The 07:30 window was missed: account B's weekly limit ran from 03:52 to about
  10:50. The new window is c5h-new-a from MC-B's GO at about 12:25; the instance must be released by 14:10.
  - The driver now waits a settle (`C5_SMOKE_SETTLE`, 30 s, with a box check) before every capture after a load.
    This follows I2's capture-timing fault (11.5).
  - The frame dump stays `all` (`C5_SMOKE_DUMP`): the step's `--before-dumps 3` needs dumped frames, and Tiny showed
    no recorder stall.
- **Result [L, 2026-11-21 12:18-13:10 EST, c5h-new-a, build 26577625, W8]: process 0 and lw done, wl and coast_land
  NOT run. Functional result only.**
  - Layout: w8-sat @6eebdbc53, server.exe 81f10ca8....
  - Prep: `lay.py prep` + `helper on`, the installer's form, with the compositor.
  - Times are not a measurement (MC #9266). Over the run window (diskwatch 12:45:45-13:07:41, 264 samples of 5 s),
    F:'s read latency had a median of 294 ms and a maximum of 874 ms; 177 samples were above 50 ms. Scratch:
    `C:/c5-64bit-work/0615/w8/smoke-a2/diskwatch_window.jsonl`.
  - **Claim and save.** Claimed 12:18 on MC-B's GO.
    - The save was put in as `C5H-0615-4912-W8PL_0008`, hash checked (b2ceb663... = b2ceb663...).
    - Boot line: "exe build: build 26577625 ...: known and supported: the 64-bit path starts".
  - **First attempt (12:18-12:26), stopped safely before lw.** Process 0 ran. Then the driver's F: latency check
    refused the lw launch: one 5-s sample of 125 ms, at 0.2 reads/s.
    - The end ran in full: `lay.py restore` "restore ok", and the saves restored.
    - Fix: the check measures a new 1-min window up to 4 times. A launch still needs a whole window under 50 ms.
  - **Second attempt (12:27-13:07).**
    - **Process 0:** producer ready 709 ms after the field.
    - **lw drawn live:** render thread 36.0 ms (EXE write 7.5, windows 4.4); producer ready 781 ms after the field.
    - **The first `Waves_Reload` on build 26577625 (MC #8527) is OK:**
      - "the coast waves wait for the reload: 2 of the 2 plot(s) ... (first (53, 13))";
      - "request posted: waves (272 ice plots, 0 to flip; 2 plot(s) of another wave class)";
      - "answer: ice not asked (0 set, 0 cleared, Ice_Reload 0.0 ms), waves ok (Waves_Reload 27.0 ms)";
      - "new water tap copy requested (requestSeq 1)";
      - "water: tap load seq 3 copy 2 read: 21 records (4 VB, 4 IB, 4 draws) ... hashes ok, copied and acknowledged in
        2.7 ms", 96 ms after the answer.
      - 0 LOUD in the lw process. No box.
    - **The ridge on 26577625 is gone** (post.py).
      - 13 samples, 0 kept, median 149 u16; the water mark agrees on 99.94 %.
      - The reference is the 4390913 reload of the same state (8.17's c5h-a run), because this build's reload
        process (wl) did not complete.
      - The live lw field equals the 4390913 live lw field on 99.9998 % of heights and 100 % of materials. The
        0.0002 % is the same as the difference between the two loads' before fields.
      - It also equals W7's offline smoke_fix on 100.0 % of the window.
      - So the same code draws the same field on both builds.
  - **The wl load stopped (the COMMON 30 record).** No crash, and no product hang shown.
    - The launch began at 12:55:51 (pid 34716, after a 1-min F: window of 2.03 ms max).
    - On the stalled F:, the EXE reached the DLL and server.exe started only at 13:00:50. The proxy logged "in game"
      at about 13:01:33.
    - `drive.py ingame` (400 s from 12:57:05) gave up at 13:03:45: "not in game after 408 s: Lua channel silent".
    - The harness's stop waited for the newest save to settle. `C5H-0615-4912-W8P01` had been stable 152 s, the human
      turn was confirmed and **no autosave was in progress**. It then terminated the EXE at 13:06:41: exit code 15,
      "not a crash code", no exe-crash report.
    - The dialog watch saw 0 boxes over 637.8 s. server.exe drew frames until the stop (render.log "host 1s" up to
      13:06:42).
    - Root cause [I]: the F: stall stretched the launch beyond the harness's 400 s in-game window. Not shown: whether
      the Lua channel would have answered later.
  - **End.**
    - `lay.py collect` and `restore`: "restore ok", provision verify rc 0.
    - `saves_snap.py restore`: moved out W8PL_0008, W8P00 and W8P01; put back `AutoSave_0008` and
      `AutoSave_Post_0008`, which the loads had overwritten; verify ok, 58 files.
    - c5h-new-a released 13:10:15. Not retried: F:'s latency broke the 50 ms launch rule most of the time.
  - **Open:** wl, coast_land and the reload picture on 26577625 go to W9's session (lead5, 2026-11-21).

**What to run, in order of value.**
1. Tiny's 22 cases, product route, under the quiet lock: `scenes_4915.py change` per case. The session's loop is
   `accept_4915.sh product`'s, run by hand without the prep, or `C5_CHAIN_*` pointed at a 26577625 layout if
   `chain_4912.sh` learns one.
2. The waves check (`accept_4915.sh waves INST tiny`).
3. The three checkpoint reloads with their shots.
4. If time remains, huge.

**The first Waves_Reload on this build (MC #8527).** It is the gate's first run of `Waves_Reload` there (10.8: never run
before).
- Report at once: the gate's answer line (`live ice/waves: ... waves ok|failed (Waves_Reload N ms)`) and the water tap's
  re-copy (server.log's water copy line and the bundle's `-c1` copy).
- If it is refused or LOUD, tell lead-0590 on thread 0590.
- 4905's field write already works on this build (10.8 [L]). The `exefield: Written` line is expected for each change.

**The same checks as 11.1.** Boxes after every step (D155), COMMON 29 and 30, LOUD and FALLBACK lines 0 beyond the
known ones, and pictures through Visual Checks (DIAGNOSTIC).

**What is stated.** 7's wording: if the session does not happen, the acceptance runs on 4390913 and says so.

### 11.3 Known differences of the candidate (one list, links to where each is written)

What a player sees after a live land/water change that differs from a reload of the same game. Each item names where
the finding is written; nothing is repeated here.

1. **Rivers at a new coast.** The river water mesh is the EXE's, tapped once per load, and is not ported (MC #8420
   (d)). The live field keeps the old shore within about one plot of a river-edge plot, except on the changed plots and
   wherever the change moves the region byte. So near a river that meets the changed coast:
   - a notch of old water, or the old bank, stays along the river's last plot;
   - the river ends where it ended at the load;
   - a plot whose ONLY change is its lake or river flags (4912's `mouth`: (51,13) loses its lake flag, (53,13) its
     river) is not a changed plot for the producer, because its pattern cell does not change. It keeps its load-time
     river ground like any plot within 60 samples of a river edge, until a reload. This is the lead's ruling of
     2026-11-20 20:30, lead3-0615 on thread 0615: the case is part of this item, and the beta gets no code change. The
     product side is now **[L] (I2, 2026-11-21 11:09, c5h-a, W8; 11.5)**. Live, the mouth case's field equals the
     offline product-like model (W7's P_on, with the w7-ridge fix) on 100 % of the window, and its 885 + 971 water-mark
     misses lie on 8.17's river-keep plots. So nothing other than the named keep (and known difference 5) shows.
     *Was: "[I] until the Tiny `mouth` case runs live on the product route with 4913's producer (I2). If that live run
     shows something other than the named keep, it is a finding for the MC."*

   Player's wording for the beta's known issues (MC #8770 (2); ticket 0601 takes it as written): "When a tile changes
   between land and water during play, the shore right beside a river can keep its old outline for about one tile (a
   small notch of water or a strip of the old bank), and the river still ends where it did when the game was loaded.
   Saving and loading the game draws it correctly. A fix is planned for a later beta."

   The planned fix is ticket 0631 (MC #8930, 2026-11-21: name it with this wording; done 2026-11-23, 8.18: it narrows
   the keep to the real river edges, the item stays for those, see "Named cause" below). The ticket id stays out of the
   player's text; ticket 0601 takes the wording with the added last sentence. The wording stays whole after the w7-ridge
   fix [L, W8, c5h-a, build 4390913, 2026-11-21 11:0x-11:1x]: on the live `mouth` case, 971 near samples are live water
   where the reload has land (the notch) and 885 the other way (the old bank), all on the river-keep plots; on lw, 13
   and 3. *Was (01:04): "whether the notch still shows ... is checked by the live runs".*

   Sources: 8.17 (a) (ticket 4913; on branch `w1-4913` until it is merged) and 8.16's "Known difference of the
   strips", item 1.

   - **Since w7-ridge (2026-11-21): land only.** The keep no longer holds hexes that are water after the change (8.17,
     "Finding: a ridge of the old shore", the fix). Only land next to the river keeps its old shore. The bank keeps
     its feather into the new water: up to 16 samples on Tiny lw, about 0.1 world unit high.
   - **Named cause: the keep is wider than the river (w7-ridge, 2026-11-21) [C for the code, C offline for the
     numbers].**
     - What happens. The feed's river flag means "river on any edge" (`SrvFeed::PlotRecord` flags bit 0, the DLL's
       isRiver). `PlotTableFromFeed` (`server/src/render/srv_liveterrain.cpp`) turns it into all edges
       (`riverEdges = 7`). So in server.exe every plot that touches a river is a river-edge plot. That includes a
       plot whose only river edge belongs to a neighbour, and a water plot at the mouth. On Tiny `c5h-9029-tiny-t0008`
       the feed flags 86 plots. W1's table (the EXE's own edge bits) marks fewer of them: in lw, (53,12) and (53,13)
       are river-edge plots only in the feed's table.
     - Effect, measured offline on the product-like tables (8.17's on/off table, rows P against A): with the fix, P
       misses 5 more samples than A on lw, 1 more on wl, 0 more on lake and coast_land, and 122 more on mouth (where P
       also keeps the flag the DLL clears).
     - Ruling: it stays as it is for the first beta (lead-0615, MC #8905, 2026-11-21). The follow-up is ticket 0631
       (`tracker/issues/0631-live-terrain-real-river-edges.md`, backlog p4, after the first beta; MC #8930): the
       producer gets the plots' real river edges. *Was: "the draft is in lead-0615's merge request of w7-ridge".*
     - **Done by ticket 0631 (2026-11-23; 8.18).** The feed carries each plot's own three edge bits and the
       producer's table takes them; a flag-only plot is no longer a river-edge plot. Offline on 4912's chain the keep
       now equals W1's table (lw, wl, lake, coast_land) or is smaller (mouth: 56,222 against 56,395 samples; 1,675
       misses against 1,732). The item itself stays: the river carve and the river water mesh are still not ported, so
       land within the keep of a real river-edge plot keeps its old shore. The player's wording above stays true; its
       "A fix is planned" now refers to the carve, not to 0631.
2. **A change next to a natural wonder.** A wonder's piece is one fixed piece, so the re-match places it again. The
   reef case was drawn right offline (8.17 (b)).
   - **Gibraltar: drawn right live [L, I2, 2026-11-21 11:53, c5h-a, server.exe 81f10ca8...; 11.5].** In h23,
     (112,43) went from coast to grass beside the rock.
     - The wonder's own hex keeps the load's ground: 4,005 of 4,096 samples unchanged; the other 91 lie at the
       approximate hex edge.
     - No new step at the hex border: 1,880 u16 live, 1,880 in the load.
     - Picture: `F:/c5-64bit-work/0615/4915/i2/pictures/h23_gibraltar_triplet_DIAGNOSTIC.png`.
   - h05, (112,41) from sea to land, stays with 11.1 (I3).
   - *Was: "Gibraltar has not been run; 11.1's h05 checks it, and its verdict goes into 11.4 (MC #8589: refuse loudly
     if wrong)."*
3. **The coast strips' remaining misses (C2's band rest).**
   - A few unattributed spots, 0.06 % of the covered samples on Tiny and 0.14 % on huge: at most a small dent or bump
     on the new beach.
   - The rounding: within 1 u16 elsewhere.
   - Bands 0 and 3 were not compared.

   Source: 8.16's "Known difference of the strips", items 2-3, and its "Not verified".
4. **Ice on new land.** An ice plot made land keeps FEATURE_ICE in the DLL, so it is still in the EXE's ice grid, and a
   reload shows the same thing. White patches can show at the shore [I]. The scene call is wired (4914 Part B,
   2026-11-20; 10.9): each change beside the ice costs one `Ice_Reload` hitch, and the ice edge follows the new ground
   about 1.2-1.3 s later [I: not run live yet]. *Was: "Until the scene call is wired, a window beside the ice keeps the
   old ice edge."*

   Source: 10.9, "Known differences for the candidate's list", items 1-2.
5. **Later coastlines that a reload moves.** A change of a line's point count shifts the jitter of every later line in
   the EXE's order (8.10.1). The live step redraws only the changed lines, and only near the change: within 128 samples,
   then a 64-sample ring. It names the rest (`coastLinesBeyondWindow`, `coastLinesReloadMoves`, 8.17). A reload also
   re-rolls the pieces of distant plots (2.3's correction, 6.7). So "live equals reload" holds for land/water changes
   only up to the reload's own re-roll.
6. **FIXED offline (ticket 0622, format version 11, 2026-11-23): the water copy after a `Waves_Reload`.** The player
   now switches to the later copy at the frame live switched (`water_frames.bin`); a span may hold the reload. Source:
   9.4, "The water copy after `Waves_Reload`". *Was: "The offline player draws the last water copy of a load for every
   frame, so the switch itself is seen only live. Every 0615 bundle member is chosen with one water copy, and ticket 0622
   is the fix after the beta. ... The live checks are I1 (Tiny) and 11.1 / 11.2's waves check."*
7. **The EXE's minimap and Strategic View hex colours.** These are stock behaviour and out of the epic's scope: they
   follow a land/water change only after a reload, in stock as in the product. Source: 2.1 (row "Land to water", the
   Strategic View item) and 2.2.
8. **FIXED offline (w7-ridge, 2026-11-21; MC #8880 / #8905): a ridge of the old shore in new water.** A land plot
   turned to water beside a river left the vanished shoreline as a thin raised line inside the new water. It showed
   in lw, east of (53,13), about 2 world units high; a reload does not show it.
   - Cause [C]: it is entirely the river keep of item 1. The keep held hexes that are water after the change, made
     wider by the feed's river flag (item 1, named cause).
   - Fix: the keep no longer holds water hexes. On the smoke's own inputs 232 samples become 13; those 13 are
     feathered, about 0.1 world unit high and not kept.
   - **Live confirmation [L, 2026-11-21 10:56-11:03 EST, c5h-a, build 4390913; W8].** Server.exe 81f10ca8... of
     w8-sat @6eebdbc53, which carries the w7-ridge fix. Tiny lw alone: 13 samples, none kept, median 149 u16. The
     live field equals W7's offline output in the window; the picture shows no ridge. Details: 8.17, "Live: the ridge
     is gone". *Was: "the 07:30 build-26577625 smoke and I2. Not run yet."* The 07:30 and 09:00 windows were missed:
     account B's weekly limit stopped the agents from 03:52 to about 10:50.
   - *Was (2026-11-20 23:24): "OPEN ...; only part of it is the river keep of item 1 ... a fix is being tried (branch
     w6-ridge)".*

   Source: 8.17, the smoke's "Finding: a ridge of the old shore".

9. **A land or water change can appear one turn late (MC #9700, 2026-11-21 18:48; 11.7).** In the player's words: "a
   land or water change can appear one turn late". A plot-type or terrain change that leaves the plot's yields as they
   were reaches the server only at the next turn's full refresh (the feed hears of no notification for it), so the old
   coast stays until then, as stock keeps it until a reload; 0465's height changes have the same limit. Default stays
   ON. Fix forward: a rolling plot refresh in the feed (W11, 4914 log; merged with 0615's second merge 33f50a43a, 11.7);
   the cause-true fix, a DLL notification, is ticket 0656 (backlog, after the beta). *(Ticket id added 2026-11-22,
   4916; was "a follow-up ticket after the beta".)*
10. **FIXED in the second beta candidate (ticket 0667, main 4fa43e3b1, 2026-11-22): the changed area kept its old look
    at a close camera.** In the first beta candidate, a live change seen at a close zoom (the 512 near pool full) drew
    the new ground with the old colours (wl: new grass under a film of water; lw: new water with a sandy band and no
    rocky shore) until the camera moved away; roads, improvements and chopped forests had the same fault. Cause and
    fix: 11.11 (stale 512 near pairs, re-baked in place). The first beta's workaround (zoom out and back in) is in
    `docs/BETA-TESTERS.md`. *(Added 2026-11-22, 4916.)*

### 11.4 Window I3 (Sat 2026-11-21, run 17:46-21:47: c5h-new-a build 26577625, then c5h-a build 4390913; W9, branches w9-i3, w9-i3b) [I for the plan, written 13:03; L and C for the results, marked per item]

11.1's runbook, run by W9 (owner `w9-i3`) on c5h-a in its order of value: product Tiny, the Tiny waves check, product
huge, stock Tiny, stock huge. Plus, on the lead's GO, one product-Tiny session on c5h-new-a (build 26577625) by 11.2.

- **Branch.** w9-i3 = the team branch @a71563ef7 (w7-ridge fix, I2's driver) + main @992f1a135. Main carries 0042
  stage 5 since 502435e15: `[Units] ServerCombat` defaults to 1, so route S is armed where ServerUnits = 1. Every result
  below names the layout's sha and ServerCombat.
- **I3's layout is `C:/c5-64bit-work/0615/w9/t8` [C, laid 2026-11-21 15:38].** MC #9508: main 8be2b28dc changed the
  proxy (`proxy_sound_exec.cpp`, 0042's 4740), so I3 runs a proxy built on it or later. lead6 built the team branch
  @fd06f5941 (main 8be2b28dc merged) on the 0.1b pins; W9 copied those builds read-only into its own tree and laid
  `layout/` as `smoke_4914.sh build` does (`C:/c5-64bit-work/0615/w9/relay8.sh`, every copy cmp-checked):
  - server.exe 952208874f3a7778d763fbec2a5d63f46bb22103a2ea42478413c2b28280b663
  - proxy 28379760f65509f3dbd95c7b31343795dfef6353f63c97f221976b6fe89cf724; vp64helper 1403b128...
  - composite 16c10b111df4d7e573d98356e67d017f32b20f5f76928fd8ac4d97f08ad6a80c; the pins' x64 DLL b84aa3e9... (unchanged)
  - Dry at 15:38-15:39: `smoke_4914.sh dry` rc=0 for prep lay and prep run3308, `accept_4915.sh dry` done.
  - w9-i3 is fd06f5941 (a fast-forward to the team branch, which already held w9-i3's commits).
  *Replaces, for I3, the 13:01 layout below (kept for R1's record).*
- **Layout [C, built 2026-11-21 12:57-13:01].** `smoke_4914.sh build` at w9-i3 @2f960c144 on the 0.1b pins (release, no
  flag) into `C:/c5-64bit-work/0615/w9` (`layout/SHA256.txt`). The script's `layout/HEAD.txt` was corrected by hand to
  2f960c144: it reads HEAD at the end of the build, and a driver-only commit (f8ea8764c) landed during it.
  - server.exe 562ab03e3824cb056c737321f6ba51094f603d81b622f9437d56380462f46e6e
  - proxy 1ae9b5d896b7aaa34c69b9ceaa26afbc3e1a908ca165fabfc3e8b307c3eb2093; vp64helper f0103e2b...
  - composite ae9f5353fb8b89d31368ea03052b2ec21e4898bf247ec741fcbee4b0d4774069; the pins' x64 DLL b84aa3e9...
  - The server.exe differs from I2's 81f10ca8... (another tree, and main's 0042 stage 5 since).
- **Dry checks [C, 2026-11-21 13:02].** `accept_4915.sh dry c5h-a`: both case lists parse (22 Tiny, 23 huge), 14 offline
  tests passed, `scenes_4912` loads. `smoke_4914.sh dry`: DRY rc=0 for prep run3308 (4390913) and for prep lay (26577625,
  tag W9, the save file `C:/c5-64bit-work/0615/w9/saves/c5h-9029-tiny-t0008.Civ5Save`, b2ceb663..., a copy read from
  c5h-a). `check` (read only) waits for the claim.
- **Driver changes before the run (W9, 2026-11-21 13:00) [C].** Found by reading the drivers and c5h-a's saves folder:
  - `accept_4915.sh` hard-coded the owner `w4-4915`. It now reads `C5_4915_OWNER`.
  - Its Tiny source was `c5h-9028-tiny-t0008`, which c5h-a does not hold. c5h-a holds `c5h-9029-tiny-t0008`, the same
    bytes (b2ceb663...c29163, hashed 13:00). The source is now `C5_4915_TINY_SRC`, default the 9029 name. Every copy is
    hashed against its source's hash (Tiny b2ceb663..., huge c499d3c6...) before the run goes on.
  - `chain_4912.sh product-prep` copied `c5h-9029-tiny-t0008` to `C5H-0615-4912-PL_0008`. That name is I1's copy on c5h-a,
    and `copysave` never overwrites, so the prep would have stopped there. `C5_CHAIN_SAVE_AS` set and empty now skips
    that copy; `accept_4915.sh` sets it so (it makes its own `-APT00` / `-APH00`).
  - `accept_4915.sh` waited a fixed 20 s after a load. It now waits for the producer after every product load
    (`wait_producer.py`, as 11.2 and 11.5) and then settles 30 s with a box check (`C5_4915_SETTLE`), the fix of I2's
    capture-timing fault. The stock route has no producer and only settles.
  - The stock route's `--dll pin` refuses [C, 13:05]: the i0 pin is now f33fc46a4 (i0-0.1b) and its instance-aware build
    `c5-i0/dlls/i0ship-f33fc46a-ia` is not laid out; no layout of it exists on C: or F:. As in I1 (6.11), the stock route
    names a C: copy of `F:/c5-64bit-work/0551/layouts/i0ship-5d18d3d6-ia/CvGameCore_Expansion2.dll` (e72f0880..., equal
    to its SHA256.txt): `C5_CHAIN_STOCK_DLL=C:/c5-64bit-work/0615/w9/stockdll/CvGameCore_Expansion2.dll`. Its pictures
    are "stock after reload" only, so the older i0 fixes do not touch what is compared [I].
  - The stock route's copy `C5H-0301-4912-SL_0008` already exists on c5h-a. `runner.py prepare` keeps an existing copy
    and loads it; the session's first reload is the fresh `-AST00` copy, so that does not change the run.
- **Product processes record nothing.** The prep's `extra.cfg` stays empty except in the waves mode, so 11.5's recorder
  stall (huge, `--dump all`) does not touch the product sessions' times; only the waves process records (Tiny).
- **Commands** (worktree root; `C5_4915_OWNER=w9-i3 C5_4915_TREE=C:/c5-64bit-work/0615/w9 C5_4915_WORK=<fresh>`):
  11.1's list, with `--who w9-i3` and `pool.py release --owner w9-i3 c5h-a`.
- **Evening order (lead5, 2026-11-21 13:05; start moved to about 18:05 on MC-B's GO, lead6 15:2x).** First the
  c5h-new-a session (build 26577625): `smoke_4914.sh` with `C5_SMOKE_PREP=lay`, `C5_SMOKE_CASES=3` (lw, wl,
  coast_land) and `C5_SMOKE_RELOAD_CAPTURE=1` (the after-reload capture; W8's run left wl and coast_land undone, 11.2),
  tag W9, as W8's 11.2; about 40 min, then released. Then c5h-a's product Tiny under the quiet lock (`--take`), with no other game of W9's beside it, so its
  times are not disturbed by W9's own games.
- **Addition (lead-0615 7th, MC-B yes 16:4x).** At the end of the c5h-a session, one short Tiny smoke (lw, wl,
  coast_land) with layout t9 (W10's fix-forward, epic-0615-lw-2 @a743b07c1): no LOUD line (a "degenerate"/"refused"
  line of the new guards is a finding), the new plain coast-count line per change, `Waves_Reload` as before.
  `C:/c5-64bit-work/0615/w9/run_t9.sh` (smoke_4914.sh PREP=run3308, tag W9S). It runs after the product end and before
  the stock arms.
- **Run.** MC-B's GO at 17:47 (the quiet window ended early). c5h-new-a (on C:, build 26577625) claimed 17:46 by w9-i3;
  `run_new.sh` started 17:48 with layout t8 (code = main c1dc17d2a incl. 8be2b28dc). Options as logged: `[Renderer]
  ServerUnits = 0 (default)`, `ServerCombat = 1 (default)`, so route S is not armed in these runs.
- **Result, c5h-new-a [L, 2026-11-21 17:46-18:10 EST, build 26577625, installer form (`lay.py`), layout t8]: SMOKE
  PASS.** It completes W8's 11.2 smoke (W8 ran only lw).
  - The save went in as `C5H-0615-4912-W9PL_0008` (b2ceb663... = b2ceb663...). Each load's producer was ready
    1,354-1,382 ms after the field.

  | case | apply (render thread) | gate | `Waves_Reload` |
  |---|---|---|---|
  | lw | 36.8 ms (EXE write 4.9, windows 4.2) | 2 of 2 plots wait (first (53,13)) | ok, 17.8 ms |
  | wl | 31.3 ms (EXE write 6.9, windows 2.1) | 1 of 1 (first (51,12)) | ok, 37.6 ms |
  | coast_land | 33.7 ms (EXE write 7.1, windows 2.0) | 1 of 1 (first (49,11)) | ok, 22.7 ms |

  - Each change: "new water tap copy requested". 0 LOUD in all 5 processes, no land/water wait, no box, no crash.
    The after-reload capture of coast_land's state was taken (process 5).
  - End: `lay.py restore` "restore ok" (provision verify rc 0); `saves_snap.py restore` moved out W9PL_0008 and
    W9P00-P03. Released 18:10:10.
  - Times: DIAGNOSTIC (no quiet lock; other teams' games ran).
  - Pictures (DIAGNOSTIC): `F:/c5-64bit-work/0615/4914b/w9-26577625/scenes/product/`.
- **Bundles of this session, and a limit for R2 [C].** `F:/c5-64bit-work/0615/4914b/w9-26577625/bundles/`:
  W9P01, W9P02, W9P03 and W9P03R are finished (scene.json); W9P00 stays `.partial` (the process-0 rule above). W9P01-P03
  each hold two water copies (`load0003.wtap` and `load0003-c2.wtap`): the `Waves_Reload` fell inside the span. As rule A
  members they would hit 9.4's gap exactly like W6P03 (the water clock refused). Only W9P03R (a reload with no change,
  one copy) is a clean member. A change member needs a span that ends before the `Waves_Reload`, until ticket 0622.
  *(2026-11-23: ticket 0622 is in, format version 11: a span recorded from then on may hold the reload; these format-10
  bundles still replay the old way.)*
  - Not tried tonight (lead-0615, 18:1x: only if under 10 min with no code change): `smoke_4914.sh` takes the change
    after 3 of 16 dumped frames, about 250 ms apart, so about 3 s of span follow it; the apply comes about 0.5 s after
    the `lt.py` call and the `Waves_Reload` 0.2-0.5 s after the apply. A span holding the apply but not the reload is a
    1-2-frame target at that spacing: it needs a driver knob and would still not be reliable [I].
- **c5h-a run.** Claimed 18:16 by w9-i3 (free early). `C:/c5-64bit-work/0615/w9/run_a.sh` from 18:20: check,
  product-prep, product tiny, waves tiny, product huge, waves huge, product end, collect; then the t9 smoke; then the
  stock arms. **No quiet-lock hold** (MC-B 18:19, lead-0615: COMMON 23, no MC window is open): each arm runs through
  `quietlock.py run` without `--take`. Other teams' games ran on the machine, so every time below is DIAGNOSTIC,
  disturbed. *Was (11.1): "quietlock.py run --take --minutes 90 / 150".*
- **Result, c5h-a, product Tiny [L, 2026-11-21 18:22-18:43 EST, c5h-a, build 4390913, layout t8; DIAGNOSTIC,
  disturbed].** One process (`C5H-0615-4912-APT00`, run folder `20261121-182329-38836-1`, a read-only copy in
  `C:/c5-64bit-work/0615/w9/logs_ptiny/`), 22 cases 18:24:42-18:32:09, no box; then the S1-S3 reloads with shots.
  - **18 change lines for 22 cases.** Mapping by time (server t0 about 18:23:28; t01's apply at 74.1 s): changes 1-6 =
    t01-t06 (all live); **t07, t08, t09, t10: no line at all**; changes 7-15 = t11-t19 (all live); change 16 = t20,
    "wait for reload: TooLarge: the changed coast lines span 1655x1265 samples [plots 39,12 14,21 17,28 15,29 16,29]";
    change 17 = t21 (large, live, 5 plots, 380,160 samples); change 18 = t22 (live). 0 LOUD.
  - **FINDING (reported to lead-0615 and MC-B at 18:4x; MC #9694): four changes after an ice change were silently not
    drawn [L].** t07 (39,12 flat to ocean beside Mt Kailash) and t08 (26,9 sea to land between the reefs) were changed
    by the DLL (`t07_apply.json`, `t08_apply.json`: before and after as asked), but server.log has no apply, no wait and
    no LOUD line for them, and their live EXE field equals the before field exactly (h16 and m8: 0 samples differ). t09
    (lake 14,21) and t10 (coast_land 10,11) are silent the same way. The silence begins after t06 (ice_land; change 6 at
    175.6 s, the session's first "ice + waves" request, Ice_Reload 125.6 ms, ice tap generation 2 at 176.8 s) and ends at
    t11 (change 7 at 283.8 s, which carries only its own plot 14,25). The only line in the gap that follows the table is
    "cut: generation 6 applied" at 204.0 s (t07's time). Later, t20's TooLarge wait names t07's plot 39,12 and t09's
    14,21: they were still pending and were swept into t20's job and left to the reload. t08's 26,9 and t10's 10,11
    appear in no line.
  - **So the wonder cases have no verdict of MC #8589's three** (drawn right / refused / wrong): they were not drawn and
    not refused. Root cause: lead-0615 (offline, 18:4x). Two code paths fit the silence [C, read]: the scene calls
    `LiveTerrain::onTable` only when the residency's table generation or `plotsChanged` moves
    (`srv_liveterrain_scene.cpp`, step 3), and `onTable` returns without a line when no plot differs relevantly from
    its last table (`srv_liveterrain.cpp`, `relevant == 0`).
- **Product huge [L, 18:51-19:27].** The first run stopped at h18 before the change (a driver fault: the anchor
  `cap+0,-30` became (101,-5), which `lt.py` answers with a usage error; `scenes_4915.py` now clamps an anchor to the
  map, 19:05); h01-h17 were all drawn live (17 change lines, 0 LOUD; run `20261121-185239-37752-1`). The rerun from
  `APH00` (19:06) crashed server.exe at h18: **11.8** (cause, stack, repro, key-off arm).
- **Cost per change (acceptance (6)) [L, `collect_4915.py` over copies of the run folders, DIAGNOSTIC, disturbed: no
  quiet lock, other teams' games ran].** `C:/c5-64bit-work/0615/w9/collect/` (`*_summary.jsonl`, `*_lines.txt`):
  - Tiny (run `20261121-182329-38836-1`): 17 of 22 cases drawn live; render thread 9.1-38.1 ms, median 16.1 ms;
    cameras skipped 1 in all; `Waves_Reload` 17.6-35.9 ms. Not live: t07, t08, t09, t10 (silent, 11.7) and t20
    (TooLarge). t21 (large) drawn live in 32.9 ms. LOUD 0 beyond 2 known lines; FALLBACK 0; no dialog record.
  - huge (run `20261121-190759-42740-1`, the crashed rerun): h01-h17 drawn live; render thread 8.3-35.0 ms, median
    18.0 ms; cameras skipped 13 in all (at most 2 per change); `Waves_Reload` 105.5-179.0 ms. h05 (Gibraltar) 26.2 ms
    and h06 (Lake Victoria) 12.2 ms, both live. h18: the crash (11.8). LOUD 0 beyond 1 known line.
  - Against 4909's bar (a hitch under about 50 ms per change on huge): every drawn change is under it.
  - The S1 and S2 reload processes of Tiny (`20261121-183412-*`, `-183839-*`) are not in the set: the harness keeps
    the newest 10 run folders, and they were pruned before they were copied. Their pictures exist.
- **Waves (MC #8606) [L].** Tiny `WV_tiny_20261121-184548` and huge `WV_huge_20261121-193732`
  (`F:/c5-64bit-work/0615/4915/i3/bundles/`): each change drawn live, the gate's "waves ok" (Tiny 29.5 ms, huge
  123.8 ms), the second water copy written after the answer, the span drawn with the reload inside (Tiny 1847-2157,
  huge 1604-1974), the bundle finished with 40 dumped frames, 0 LOUD; Tiny's `recorder_test --check`: "scenebundle:
  OK". Not done: the frame-by-frame look at old against new waves (item 3).
- **Product end [L].** 19:57: `run_3308.py restore` rc 0, `provision_verify_rc` 0.
- **The fix arm (t12):** 11.8.
- **The Tiny smoke on t12 [L, 20:34-20:52, c5h-a, build 4390913, layout t12 = W12's 975c48700 (W10's fix-forward,
  W11's feed refresh, the h18 fix), `C:/c5-64bit-work/0615/w9/run_t12.sh`, tag W9Y]: SMOKE PASS.** (It replaces the
  planned t9 smoke, lead8 20:2x.)
  - lw / wl / coast_land drawn live: render thread 40.0 / 18.8 / 48.9 ms (EXE write 5.8 / 3.7 / 10.1 ms);
    `Waves_Reload` 21.7 / 22.4 / 32.6 ms; 0 LOUD, no "degenerate"/"refused" line, no box; restore rc 0.
  - W10's plain line per change: "coast (live): 2 / 3 / 2 line(s) redrawn in the window, 2 / 2 / 2 of them also reach
    beyond it; 0 / 9 / 0 unchanged line(s) a reload would re-jitter".
  - Which feed path found each change is not in the logs: W11's per-turn summary ("feed: rolling refresh in turn N:
    ... found X; the plot-state path found Y") is written when a turn ends, and no turn ended in these processes
    (process 0's line, turn 8: found 0, plot-state path 0). Each apply came 0.5-1 s after the lt.py call [I: the
    immediate notification path].
  - Before it, two launches (the smoke's and stock-prep's, 20:31-20:32) were refused by another team's quiet-machine
    lock; both were cleaned up (the smoke's end restore rc 0; stock-end's runner restore OK; `provision.py verify
    c5h-a` rc 0 at 20:33).
- **Stock arms [L, 20:53-21:46, I1's i0 DLL copy e72f0880..., stock DX11 EXE].** Tiny: all 22 cases replayed at
  the product's plots (20:57-21:08), S1-S3 saved, the three reloads with shots, "SESSION OK (stock tiny)" 21:20:46.
  huge: h01-h18 replayed (21:21-21:33), S1 and S2 saved; it stopped at h19 by design ("no product record of h19":
  the product session ended at h18), then S1 and S2's after-reload shots (`hshots_stock.sh`, 21:34-21:45). No box.
  Stock end: runner restore "RESTORE OK" 21:46.
- **Pictures (acceptance (8); DIAGNOSTIC, D112: a dev layout, not the installer's).**
  `F:/c5-64bit-work/0615/4915/i3/{tiny,huge}/{product,stock}/`: product live per case; after-reload pictures for
  Tiny 22 (product) and 22 (stock), huge 14 and 14 (S1 and S2's cases); wonder cases also `_before`.
  Not sent through Visual Checks as triplets yet (the shots carry their own `visual` ids).
- **Wonder verdicts (MC #8589) [L for the fields, `C:/c5-64bit-work/0615/w9/wonder_check.py`, W8's gib.py
  method].**
  - h05 (112,41) sea to land beside Gibraltar (112,42): **drawn right**: the wonder's hex keeps the load's ground
    (4,005 of 4,096 samples unchanged; the 91 others are heights at the approximate hex edge, as in I2's h23); the
    largest step across its border is 2,059 u16 live against 2,090 in the load.
  - h06 (77,35) flat to water beside Lake Victoria (76,35): **drawn right** [I: by the same two measures]: 91 edge
    samples, border step 3,587 live against 3,556 in the load (+31 u16, about 1 %).
  - t07 and t08 (Tiny): **no verdict**: not drawn and not refused (11.7: the feed never heard of them in the turn).
    t07's field is unchanged (0 samples in the 400 x 400 box). W11's feed refresh (t12) covers them; its live check
    is in 11.7.
- **Release [C, 21:47].** Restore check (`C:/c5-64bit-work/0615/w9/restore_check.py`) against the session's first
  backup (run_3308, 18:19, right after the 18:16 claim; 15 files) and the stock route's backup (runner.py, 20:32;
  19 files incl. `instance.json`, the user and install manifests, `steam_api.dll`, `c5_wrapper.json`): every file
  hash-equal or absent as then; `provision.py verify c5h-a` rc 0. c5h-a released 21:47:54. The save copies
  (`C5H-0615-4912-APT*`, `-APH*`, `-AST*`, `-ASH*`, `-W9X*`, `-W9Y*`) stay on the instance, as I1's and I2's did.
- **Bundles of I3** (none added to the library; none stamped):
  - one water copy, but NOT a member (R2 below: the player refuses a build-26577625 bundle against the only install
    outside the pool; corrected 2026-11-22 00:2x, was "member candidate"): `F:/c5-64bit-work/0615/4914b/w9-26577625/bundles/W9P03R_20261121-180610_workstation`
    (build 26577625, layout t8, a reload with no change);
  - two-copy evidence (the `Waves_Reload` inside the span, 9.4's gap): W9P01-P03 (same folder), W9YP01-P03
    (`F:/c5-64bit-work/0615/4914b/w9-t12/bundles/`, t12), `WV_tiny_20261121-184548` and `WV_huge_20261121-193732`
    (`F:/c5-64bit-work/0615/4915/i3/bundles/`);
  - `.partial` (process 0): W9P00, W9YP00.
- **Not verified.** The waves check's frame-by-frame look at old against new coast waves (item 3); the Tiny S1/S2
  reload processes' logs (pruned before they were copied); stock huge h19-h23 and S3.

**R1: rule A run before the window (MC-B's GO 13:07; W9) [C offline; results below when run].**
- **What.** lead5's `r1.sh` (W9's copy `C:/c5-64bit-work/0615/w9/r1.sh`, owner w9-i3), at w9-i3 @2f044d840:
  1. `run_all.py` with W6's seven suites: render-host, overlay-scene, shadow-ice-scene, fow-scene, forest-scene,
     scenebundle-programs, scenebundle-py. GAME is the pure_vp install outside the pool (read only; EXE stamp
     0x546CD0A8 checked by the script). `C5_SCENE_ASSET_ROOT` = `scene-asset-root/mods548-vt`; run_all passes it only to
     offline-equals-live.
  2. A sceneshot build.
  3. `sceneshot --dumped --compare --tolerance 1:1` replays of the 9 `PT0*` bundles (I1, `F:/c5-64bit-work/0615/4912/bundles`)
     and the 4 `W6P0*` bundles (the 4390913 smoke, `F:/c5-64bit-work/0615/4914b/smoke/bundles`).
- **F: latency.** At 13:05 F:'s read latency was max 657 ms over 1 min, with three games of other teams on F:. The
  replays read their bundles from F:, so W9's copy measures 1 min before each replay and starts it only under 50 ms (up
  to 40 tries). The suites run on C:. A diskwatch of F: covers the whole run
  (`C:/c5-64bit-work/0615/w9/r1/diskwatch_r1_F.jsonl`).
- **Known gap.** A replay draws the last water copy of a load for every frame (9.4's rule A gap). A P1 difference only
  in frames after a `Waves_Reload` is that gap, not a failure; the frames are named below. *(Closed for bundles of
  format version 11 by ticket 0622, 2026-11-23: 9.4; R1's bundles are format 10 and replay as here.)*
- Started 13:06:52.
- **Suites [C, 2026-11-21 13:06-13:45, w9-i3 @2f044d840]: `run_all: OK: 7 suite(s) passed (plain)`.**
  render-host 164 passed (558 s); fow-scene 3; overlay-scene 3; shadow-ice-scene 6; forest-scene 3;
  scenebundle-programs 4 (560 s); scenebundle-py 22. 0 failed, 0 skipped. Logs: `C:/c5-64bit-work/0615/w9/r1/ra/logs/`.
  sceneshot build exit 0 at 13:50:42. All of it ran on C: (F: not read), with other teams' games and builds on the
  machine: the times are not a measurement.
- **The replays waited for F: [L, 13:51-14:04].** Twelve 1-min windows before the first bundle had a max read latency of
  194-1,064 ms (c5h-new-b, c5h-d and an ASan build on F:). lead6 (14:0x, MC-B's plan): no bulk copy of the bundles to C:
  while a game runs from F:; the replays run in F:'s quiet span from MC-B's GO (about 15:50) to 17:15, W6P01 and W6P02
  first, nothing after 17:15 (0637's quiet pairs from 17:20). The loop: `C:/c5-64bit-work/0615/w9/r1_replay.sh` (a
  bundle starts after a window under 50 ms, or after the GO with the window's max recorded; none starts after 17:05;
  output on C:).
- **Finding: PT00 and W6P00 are not bundles [C, 2026-11-21 14:04-14:10].** Both folders hold only `.partial/` (feed.bin,
  heightfield/, water/) and no `scene.json`; sceneshot refuses them: "FAILED 21 NoManifest: ... (scene.json missing)"
  (PT00, 14:04:53; it started during the change of loops, with F: at 624 ms, and read nothing beyond the folder).
  - Cause: the recorder's span begins at published frame 1500 (`--after 1500 --frames 16`). A bundle is finished only
    when its span is drawn ("span drawn: 16 frames, 2340 to 2468", then "bundle ... finished") or at the render
    thread's own end (`FinishRecorder`, `srv_render_host.cpp`). Process 0 (load, `lt.py setup`, save) is stopped by the
    next process's reload before frame 1500: W6's process 0 (`C:/c5-64bit-work/0615/w6/smoke/product_logs/20261120-224837-35644-1`)
    logged its last `host 1s` at published=1038 and no finish line, so the writer's `.partial` was never moved into place.
    Process 1 of the same run drew its span at frames 2340-2468 and finished.
  - Effect: the process-0 bundles of 4912's chain and of the smoke carry no frames; nothing in them was ever a rule A
    member. 11 of the 13 named bundles are replayable (PT01-PT08, W6P01-W6P03).
  - A driver that wants a process-0 bundle must keep that process past frame 1500 + the span (on Tiny about 2 min
    here), or start its span earlier [I].
- **The replays [C, 2026-11-21 15:04-17:05, sceneshot of w9-i3 @2f044d840 (r1/ss), C:/c5-64bit-work/0615/w9/replay].**
  MC-B's GO came at 15:05 (no game on F:). A bundle that started after a window above 50 ms says so in the table.
  Logs: `C:/c5-64bit-work/0615/w9/r1/replay_<bundle>.log`, the loop's `r1/replay.out`.

  | bundle (case) | F: window before (max) | exit | result |
  |---|---|---|---|
  | W6P01 (smoke lw) | 6.08 ms | 10 Differs | frames 2340-2448 (13) EQUAL; 2450, 2459, 2468 colour differs, about 35,860 px, depth EQUAL |
  | W6P02 (smoke wl) | 13.3 ms | 8 ReplayFailed | frame 2265: "live terrain replay: generation 1 MISMATCH: box 2913,685-3486,1236 sha256 a933115b..., recorded 2e78af63..." |
  | PT01 (I1 lw, step off) | 4.89 ms | 10 Differs | frames 1763-1816 (8) EQUAL; 1821-1870 (8) colour differs, about 34,445 px, depth EQUAL |
  | PT02 | 10.4 ms | 0 | 16 shots byte-equal |
  | PT03 | 4.78 ms | 0 | 16 shots byte-equal |
  | PT04 | 0 ms (no read in the window) | 0 | 16 shots byte-equal |
  | PT05 | 601 ms (ABOVE 50 ms; started under MC-B's GO, no game on F:) | 0 | 16 shots byte-equal |
  | PT06 | 0 ms | 0 | 16 shots byte-equal |
  | PT07 (I1 large, step off) | 4.76 ms | 10 Differs | frames 2165-2326 (5) EQUAL; 2328-2403 (11) colour differs, about 134,530 px, depth EQUAL |
  | PT08 | 0 ms | 0 | 16 shots byte-equal |
  | W6P03 (smoke coast_land) | 0.29 ms | 8 ReplayFailed | "water clock: water_clock.bin: refused by the water scene (531 waves ...)": "setClockState refused: 531 waves given, the image in use has 528" |

  The loop ran 15:04-15:26:14 (end minute 15:26). 6 of 11 replays pass; the 5 others are explained below, none by a
  fault of today's code.

  F: over the spans (the background diskwatch, 5-s samples, `r1/diskwatch_r1_F.jsonl`): suites 13:07-13:45 (on C:, F:
  not read by them) median 70.5 ms, max 1,078 ms, 239 of 462 samples above 50 ms (other teams' F: games and builds);
  replays 15:04-15:27 median 0 ms, max 601 ms, 18 of 276 samples above 50 ms, all in short bursts at the replays' own
  starts (15:04:52-15:05:12 at W6P01's, 15:17:08-15:18:29 around PT05's, single samples at 15:13:43, 15:15:43,
  15:18:49-15:19:04, 15:20:09), i.e. the replays' own bundle reads [I]; the A/B 15:27-15:38 max 18.4 ms. A replay's
  pass or fail does not depend on timing: these are records, not a measurement.
- **W6P03: 9.4's rule A gap [C].** The bundle holds two water copies (`load0003.wtap`, and `load0003-c2.wtap` written
  inside the span after the `Waves_Reload`, 110.985 s). The player draws the last copy for every frame (528 waves), while
  the water clock recorded at the span's start belongs to copy 1 (531 waves), so the water scene refuses the clock. This
  is known difference 6 (11.3), the reason 0615's members are chosen with one water copy (ticket 0622 is the fix).
  *(2026-11-23: fixed for format-11 bundles by 0622, 9.4; W6P03 itself is format 10 and still fails this way.)*
- **PT07: the same re-bake timing as W6P01 and PT01 [C].** The `large` case's new land loses its forest and jungle; live
  keeps the old forest-floor ground until the 14-tile decal re-bake lands, 3,328.9 ms after the raise (I1's server.log
  117.188 s raise, 120.517 s done; the span ended at 119.689 s); the replay re-baked in 121.0 ms. Picture:
  `F:/c5-64bit-work/0615/4915/i3/r1/PT07_2328_diff_DIAGNOSTIC.png` (recorded | replay | mask), box x 648-1436, y 202-781.

- **W6P02: the refusal is right [C, offline, 15:15].** The recording predates the w7-ridge fix (W6's smoke ran at 22:55
  on 2026-11-20 with server.exe without it). W6P02's own journal through W7's `journal2plots.py` and a copy of W7's
  `lt_producer_run` (`C:/c5-64bit-work/0615/w9/wl_ridge.sh`, inputs from the bundle and `02_wl_before_field.npz`):
  - fix on (today's code): the box 2913,685-3486,1236 hashes a933115b1537f7ff..., the replay's hash exactly;
  - fix off (`--river-keep-water 1`): 2e78af63399b4111..., the recorded hash exactly;
  - 11,574 samples of the field differ between the two.
  So the player refuses a stale recording, as it should. The wl case is recorded again tonight (I3's c5h-new-a smoke,
  tag W9, with the fix).
- **W6P01 and PT01: a re-bake timing difference, not the land/water step [C, 15:10-15:17].** PT01 was recorded with
  the land/water step OFF (I1; its replay says "land/water step off (no mark: as recorded)"), and it differs the same
  way, in the same screen box (x about 1920-2212, y 575-837 of 2554 x 1411), so the step is not the cause.
  - What happens at the first differing frame (W6P01 2450, PT01 1821): the lw case's change has reached the DLL, which
    removes the forest of LW2 (the forest plot). Frame 2448 shows the forest. In frame 2450, live and replay alike
    have dropped the trees, but live still draws the old forest-floor ground (dark blotches) on the plot, and the replay
    draws plain grass. Picture: `F:/c5-64bit-work/0615/4915/i3/r1/W6P01_2448rec_2450rec_2450rep_DIAGNOSTIC.png`
    (recorded 2448 | recorded 2450 | replay 2450); masks in `W6P01_2450_diff_DIAGNOSTIC.png`, `PT01_1821_diff_DIAGNOSTIC.png`.
  - The logs: the same decal generation in both ("1 changed rectangles raised 2 tiles", the set going from 651 to 650
    decals). Live finished re-baking those 2 tiles 1,541.1 ms after the publication (W6P01; server.log 124.766 s raise,
    126.307 s done) and 2,506.3 ms after (PT01), both after the span's last frame. The replay re-baked them in 57.4 ms
    (W6P01) and 74.6 ms (PT01) and drew them at once.
  - So the bundle does not record when a tile re-bake lands in live, and the player bakes without waiting: a named gap
    of rule A (re-bake latency), not missing content. Live ran under `--dump all`, which stalls the render thread
    (11.5), so the live latency here is longer than a player's [I].
  - Player-visible side note [L, recorded]: after a forest is removed, its old forest-floor ground stays for the tile
    re-bake time (1.5-2.5 s here, recorded).
  - *Added 2026-11-23, ticket 0622 (9.4) [C]:* the same gap, larger, on land/water changes with the step on. Q2P03
    (coast_land): the decal generation raised 25 land tiles at 117.064 s; live re-baked them 2,184.8 ms after the
    publication (119.249 s, after the span's end at 117.849 s); until then live draws the new coast's land with the old
    tiles (blotchy water colours on the new land, picture `C:/c5-64bit-work/0622/diag_1866_rec_rep_DIAGNOSTIC.png`,
    Q2W40 frame 1866, recorded | replay). Offline about 628,000 px differ (channel above 1) from the generation frame on,
    depth equal (Q2W40 1864-1892, Q2P03 1733-1737, Q2P01 1810-1813; Q2P01 also the 34,000 px forest case from 1806).
    Still a named gap, no ticket of its own named here.
- **MC #9487's A/B: main's player against the merged head's [C, 2026-11-21 15:29-15:38].** A = sceneshot of main
  44bd842f0 (no 0615 code; lead6's build, SHA-256 390f58fc...), B = sceneshot of the merged head f92ac9508 (8a7986af...),
  both with R1's flags. Scripts: `C:/c5-64bit-work/0615/w9/ab.sh`, `ab2.sh`, `abcmp.py` (per frame, pixels differing by
  more than 1 in a channel); logs and frames under `C:/c5-64bit-work/0615/w9/ab/`.
  - **A cannot read any 0615-era bundle as recorded.** It refuses PT01, W6P01 and PT07 before a frame: "scenebundle:
    REFUSED BadValue scene.json: render_config's render_live_terrain_journal: the journal has bytes after its end"
    (exit 42). The journals carry 0615's trailers (the map seed's "SEED", ticket 4912; and on W6P01 the land/water mark
    "LWAT", ticket 4914; `ParseJournal` in `srv_liveterrain.cpp`), which main's reader does not know.
  - **So the step-OFF bundles were replayed on A from C: copies with the SEED trailer cut** (`strip_seed.py`: only the
    journal's last 12 bytes removed from the copy's `scene.json`; no member hash covers it, the checker accepted the
    copies). W6P01 needs LWAT and has no main equivalent. The copies were deleted at 15:38.
  - **Result: A == B, byte for byte.** PT01: 16 of 16 frames, PNG and depth, identical between A and B; PT07 the same.
    Against the recording both differ by the same pixels in the same box: PT01 frames 1821-1870, 32,704-32,717 px,
    x 1959-2212 y 598-831; PT07 frames 2328-2403, 124,650-124,684 px, x 648-1437 y 201-781; the frames before are 0 for
    both. B also equals W9's R1 replays (w9-i3's sceneshot) on PT01, W6P01 and PT07, every frame.
  - **Conclusion.** On step-OFF scenes 0615's code draws exactly what main draws. The recordings' difference is the
    re-bake timing above (live lands the tile re-bake 1.5-3.3 s later than the player draws it), present in main's
    player as well; it is not a change made by 0615.

**R2: rule A run at the merged head (MC-B's slot 23:40-01:30, no game; W9, branch w9-r2) [C offline; the plan,
written 2026-11-21 21:55; results below when run].**
- **What.** `C:/c5-64bit-work/0615/w9/r2.sh` at epic-0615-lw-2 @3bf820adf (code frozen since 0a27573f2; it carries the
  h18 fix 9326f6782):
  1. `run_all.py` with R1's seven suites, R1's flags (GAME = the pure_vp install outside the pool, stamp checked;
     run_all passes the asset root only to offline-equals-live).
  2. A sceneshot build.
  3. `sceneshot --dumped --compare --tolerance 1:1` of the 16 library bundles (`C:/c5-64bit-work/scene-library/bundles`;
     any exit other than 0 stops R2 and is reported at once; a frame within 1:1 but not EQUAL is noted), then I3's
     member candidate W9P03R, then the two-copy evidence W9P01-P03, WV_tiny and WV_huge (each read from F: after a
     1-min window under 50 ms; their expected answer is the water clock's refusal, 9.4's gap).
- Nothing starts after 01:20; a stop file at 01:28 ends what is left; end minute to lead9 and MC-B at once.
- lead9's relink (`quietlock run`) runs beside it from 23:40: times are not a measurement.
- **Beside R2: two Tiny smokes on c5h-b with the final code (MC-B 22:0x; 23:40-00:40, one game at a time).** Layout t13
  = epic-0615-lw-2 @0a27573f2 (lead9's links9 builds, 0.1b pins), copied to `C:/c5-64bit-work/0615/w9/t13`
  (`sha256sum -c` OK at 22:03; server.exe c8b71600..., proxy 4566dea5..., vp64helper d87bdf1f..., composite
  11948d3a...; the composite's ini from lead9's `links/shell/bin`). Driver `C:/c5-64bit-work/0615/w9/b13.sh` (prep as
  the smoke's run3308 prep, the Tiny save put in by `saves_snap.py` as `C5H-0615-4912-W9KPL_0008`, restored at the end):
  - (a) key OFF (`C5_RENDER_LIVE_LANDWATER=0`): lw, wl, coast_land in one process must each wait for the reload, 0
    LOUD, no box; per W10's item 4 (8.17's switch-off) the scene logs "map seed not read (C5_RENDER_LIVE_LANDWATER=0
    ...)" and owes no `Ice_Reload`;
  - (b) key ON: `smoke_4914.sh run` (lw, wl, coast_land live, `Waves_Reload` ok, the "coast (live)" lines).
- **R2 result: run_all `OK: 7 suite(s) passed (plain)` at 3bf820adf (00:07-00:40; render-host 164, fow-scene 3,
  overlay-scene 3, shadow-ice-scene 6, forest-scene 3, scenebundle-programs 4, scenebundle-py 22); the 16 library
  bundles byte-equal; the replays of I3's bundles as in the table below. R2 ended 00:42.** Every F: window before a
  replay from F: had a max of 0-3.3 ms.
- **DIAGNOSTIC: W9P03R with `--allow-other-exe` [L for this scene, 00:41-00:42; not P1].** exit 0, "OK 16 shot(s), each
  byte-equal to the server's own dump": with the pure_vp install's (build 4390913) shaders the player draws this
  build-26577625 scene byte for byte as server.exe drew it live. It does not make the bundle a member (the check is by
  design), but it says the two builds' shaders agree for this scene.
- **R2 run [C, 2026-11-21 23:40 - 2026-11-22 00:42, w9-r2 @de667d9dc = code 3bf820adf].** The order was changed at the start (lead9's
  yes): the sceneshot build, the replays, then run_all last, so that an overlap with another team's rule A run could
  only cut suites. Logs: `C:/c5-64bit-work/0615/w9/r2/`, `r2.out`.

  | bundle | F: window before (max) | exit | result |
  |---|---|---|---|
  | the 16 library bundles (`scene-library/bundles`: FOG_0008, HUGE_0350, SCNB_0009 x4, TINY_0009, the nine 4980 scenes) | on C: | 0 each | 8 of 8 frames byte-EQUAL each: the library stays 0:0 |
  | W9P03R (26577625, a reload) | 0 ms | 9 Install | "the EXE '<pure_vp>/CivilizationV_DX11.exe' has SHA-256 04cfdc71..., the bundle's pins.exe_sha256 is a7b4f520... (its shaders may differ; --allow-other-exe draws anyway, not P1)" |
  | W9P01 / W9P02 / W9P03 (26577625) | 0.29 / 1.29 / 3.27 ms | 9 Install | the same refusal |
  | WV_tiny (4390913) | 0.61 ms | 8 ReplayFailed | "water clock: water_clock.bin: refused by the water scene (558 waves ...)": the two-copy gap (9.4) |
  | WV_huge (4390913) | 2.23 ms | 10 Differs | 0 of 40 frames EQUAL: the player drew copy 2's waves for every frame (its clock was accepted: the same wave count in both copies [I]); 57-59k px differ before the reload, 173-175k px at the end; depth EQUAL throughout |

  - **Named gap: a build-26577625 bundle cannot be a member yet [C].** The player checks the bundle's EXE hash against
    the install it reads shaders from, and the only install outside the pool is pure_vp at build 4390913. A 26577625
    install outside the pool does not exist (D200: never the Steam install; D202), so W9P03R is not a member candidate
    (lead9, 2026-11-22 00:0x). *Was (11.4, 21:4x): "W9P03R the one-copy member candidate".*
  - WV_huge's later frames differ more than its early ones [I: the live re-bakes of the change's tiles land later than
    the player draws them (R1's finding), on top of the waves; not separated].
- **The c5h-b smokes [L, 23:41-00:09, c5h-b claimed 23:41 by w9-i3, build 4390913, layout t13].**
  - The first prep (23:41) stopped on a file the copied tree lacked (`shell-build/bin/shell_composite.pdb`, which
    `run_3308.py prep` copies); the end restored (rc 0). With the pdb copied from lead9's `links/shell/bin` it ran.
  - **(a) key OFF: PASS** (run `20261121-234610-40516-1`, 23:45-23:48): "land/water changes wait for the reload
    (C5_RENDER_LIVE_LANDWATER=0)"; "map seed not read (C5_RENDER_LIVE_LANDWATER=0: the land/water step is off ...)";
    lw, wl, coast_land each "wait for reload: Water: plot X,Y is or was water (the coast lines move)"; no Ice_Reload and
    no waves request posted; 0 LOUD; no box.
  - **(b) key ON: PASS by the logs** (23:49-00:07): lw / wl / coast_land each applied live (render thread 30.6 / 17.9 /
    38.8 ms; EXE write 4.5 / 3.7 / 7.8 ms), "waves ok" 19.9 / 25.2 / 24.6 ms, "coast (live): 2 / 3 / 2 line(s)
    redrawn ...", 0 LOUD, no box. `smoke_4914.sh verdict` printed "SMOKE FAIL: lw: no apply line" only because its
    collect took every run folder since the claim's prep, including the key-off process, which shifted every name by
    one (its "extra 4" is coast_land; all three have an apply) [C, read from the folders].
  - End: run_3308 restore rc 0, provision verify 0, `saves_snap.py restore` (the W9K copies moved out); restore check
    against the 23:4x backup OK. c5h-b released 00:09:41.

### 11.5 Window I2 (Sat 2026-11-21 09:00-11:00, c5h-a, build 4390913; W8) [I, the runbook, written 2026-11-21 01:00 before the run; results below when run]

I2 checks, live on the product route with server.exe's land/water step and the w7-ridge fix, the three items that the
smoke and I1 left open. It runs on save copies only (`C5H-0615-4912-AW8*`; the originals and earlier copies are not
touched).

- **mouth (Tiny).** 4912's `mouth` case (`cases_tiny.txt` t05, picked nearest the capital). It starts from W3's
  product chain state after coast_land: a copy of `C5H-0615-4912-PT04`, as 8.17's mouth row starts.
  - Pictures: before, live and after a reload (`scenes_4915.py change --before`, then `shots`).
  - Each picture has its EXE field, `*_field.npz`.
  - The question (MC #8770 (1)): does anything other than the named river keep of 11.3 item 1 show? Anything else
    goes to the lead the same hour.
- **huge.** W3's recording (`F:/c5-64bit-work/0615/4912/huge/`, made 2026-11-20 20:19-20:46 while server.exe never
  called `enableLandWater`) is run again in `huge_4912.sh`'s product sequence, with a bundle per process:
  - lake;
  - coast_land after a reload;
  - the reload's capture.
  The land/water changes should now be drawn live. In W3's run they waited `Water`.
- **Gibraltar h23 (MC #8709, #8641 condition 4).** `cases_huge.txt` h23, (112,43) from coast to grass. It runs in the
  third huge process, from the state after coast_land, with pictures before, live and after a reload (a fourth
  process). Expected (W5 offline, 11.1):
  - Gibraltar's own hex keeps the load's ground;
  - no step at the hex border;
  - about 487 samples of the old shoreline in the 32-sample feather.
- **Driver.** `render/evidence/0615/4915/i2_4915.sh` (dry, check, prep, mouth, huge, end, verdict). It uses the
  layout of 11.2's smoke (server.exe 81f10ca8..., w8-sat @6eebdbc53), with `run_3308` prep and 4901's composite keys
  as in the 4390913 smoke. After every load it waits for the producer (`wait_producer.py`).
- **Commands** (after `pool.py claim --name c5h-a --ticket 0615 --owner w8-sat`):
```
bash render/evidence/0615/4915/i2_4915.sh dry c5h-a        # DRY rc=0 at 00:53 (sources PT04 and HUGE_0350 present, no copies yet)
bash render/evidence/0615/4915/i2_4915.sh check c5h-a      # "EXE build 4390913 as expected" at 00:52 (read only)
bash render/evidence/0615/4915/i2_4915.sh prep c5h-a
C5_I2_FIRST=1 bash render/evidence/0615/4915/i2_4915.sh mouth c5h-a     # 2 Tiny processes, ~10 min
bash render/evidence/0615/4915/i2_4915.sh huge c5h-a       # 4 huge processes, ~40 min [I]
bash render/evidence/0615/4915/i2_4915.sh end c5h-a
bash render/evidence/0615/4915/i2_4915.sh verdict c5h-a
python -I tools/harness/pool.py release --owner w8-sat c5h-a
```
- The outputs go to `F:/c5-64bit-work/0615/4915/i2/` (`tiny_mouth/`, `huge/product/`, `huge/gibraltar/`, `bundles/`).
  The pictures are DIAGNOSTIC (D112).
- **Run.** I2's planned window (09:00-11:00) was missed: account B's weekly limit ran from 03:52 to about 10:50. It
  ran on lead5's new plan, on c5h-a claimed 10:52 for w8-sat (build 4390913), after the lw ridge run (8.17, "Live:
  the ridge is gone").
  - The layout is the one of 11.2's smoke: w8-sat @6eebdbc53, server.exe 81f10ca8....
  - Prep: run_3308 at 11:06. Copies `C5H-0615-4912-AW8M04` of PT04 (2ee3be1e...) and `-AW8HL_0350` of
    `C5H-VP8P-HUGE_0350` (c499d3c6...).
  - Other games ran on the machine: every time is DIAGNOSTIC, disturbed.

**Result: mouth [L, 2026-11-21 11:07-11:13 EST].**
- **The change.** The pick is (52,13), distance 2 from the capital, as in I1. The DLL also cleared (51,13)'s lake flag
  and (53,13)'s river.
- **Drawn live.**
  - The apply: render thread 42.0 ms (EXE write 8.4, windows 2.1).
  - The gate: "the coast waves wait ...: 1 of the 1 plot(s)", then "waves ok (Waves_Reload 18.2 ms)" and "new water
    tap copy requested".
  - 0 LOUD in server.log and render.log; no box, no crash.
  - Producer ready 822 ms after the field; 1,348 ms in the reload process. Both were ready before the wait began.
- **Against the reload** (post.py, within 128 samples of (52,13), (51,13) and (53,13)):
  - 885 samples are live land where the reload has the water mark, 545 of them kept from the load.
  - 971 samples are live water where the reload has land.
  - Their nearest plots are (52,14), (52,15), (53,14), (53,12), (52,13), (51,13) and (53,13): the plots of 8.17's
    mouth river-keep list.
- **Against offline.** In the window (39,287 changed samples) the live field equals W7's offline `lt_producer_run`
  (`C:/c5-64bit-work/0615/w7/cases/mouth/P_on`) on 100.0 % of heights and materials. P_on is the product-like river
  table (every edge of a river plot) with the fix, and its offline counts are the same 885 / 971.
- **Verdict.** The product draws exactly the offline model whose misses 8.17 attributes to the river keep (known
  difference 1) and the reload's own lines (known difference 5). Nothing else shows, so there is no finding for
  MC #8770 (1).
- **Pictures** (DIAGNOSTIC): `F:/c5-64bit-work/0615/4915/i2/pictures/mouth_triplet_DIAGNOSTIC.png`, cut from
  `tiny_mouth/t05_{before,live,reload}.png`.
  - Live, the bay joins the sea, and the EXE's river mesh still runs to its load-time end across the new water.
  - The reload picture's camera sits about a plot off.
  - In the first reload picture the capital area is drawn darker and without units. It is not fog: the capture came
    too early (the driver's capture-timing fault, below). The retake `t05_reload2.png` shows the full frame.
    *Corrected 11:40; was: "[I: the fog of war after the reload; not checked.]"*

**Result: huge and h23 [L, 2026-11-21 11:13-11:34 EST; DIAGNOSTIC, disturbed].**
- **Picks.** The same as W3's recording: lake (101,22), coast_land (103,19), which also moved (104,18).
- **Every change drawn live:**

  | case | render thread (apply) | `Waves_Reload` | producer ready after the field |
  |---|---|---|---|
  | lake | 34.1 ms (EXE write 8.3) | ok, 110.3 ms | 2,232 ms |
  | coast_land | 16.3 ms (EXE write 4.1) | ok, 112.3 ms | 2,473 ms |
  | h23 beside Gibraltar | 22.3 ms (EXE write 4.0; 185,472 samples written) | ok, 136.0 ms | 2,198 ms |

  - h23 was **not refused**.
  - The 4 huge processes: `i2_4915.sh verdict` gives **"I2 PASS (6 process folders)"** (with the two mouth
    processes). Every change folder has the step on, an apply and no land/water wait; 0 LOUD; no box; no crash.
  - Huge loads' producer: 2.1-2.7 s after the field.
  - End: run_3308 restore rc 0, `provision_verify_rc` 0.
- **FINDING: the apply came about 10 s after the change in 2 of the 3 huge cases.** In server.log:
  - coast_land: the gate's "coast waves wait" at 208.55 s, the apply at 218.65 s;
  - h23: 228.41 s, then 238.13 s; the producer line says "producer 242.2 ms, queued 242.3 ms";
  - lake: 0.85 s; Tiny lw and mouth: 0.76-0.77 s.
  - The gate and `LiveTerrain::onTable` run in the same poll (`srv_liveterrain_scene.cpp` 434-441). So the job
    reached the worker about 9.5 s after the table moved. Between the two, server.log has only the recorder's frame
    dumps (`--dump all`, 2554 x 1411, colour and depth).
  - **Cause [L]: a stall of the scene recorder, TEST ONLY, not a player path.** It was decided by the run without
    the recorder, below.
    - In the recorded processes (`--after 1500 --frames 16 --dump all`; on huge each frame is 2554 x 1411 colour and
      depth), render.log's `host 1s` lines have gaps of 4-27 s during the dumped span:
      - h23's process: 26.6, 15.8 and 16.7 s;
      - coast_land's: up to 17.8 s.
    - h23's change was seen 17.6 s after the lt.py call and applied 27.3 s after it.
    - Without the recorder the lines come about 1.0 s apart, and the same changes are applied about 2 s after the call.
    - So **a recorded huge run's live timings, and any capture taken within about 30 s of a change, are not the
      player's**. Recorded huge runs need `--wait` of 30 s or more before a live capture. Tiny is not affected (lw and
      mouth: 0.76-0.77 s, recorded).
    - *Corrected 11:56; was: "The run without the recorder, below, decides ..."*
- **The live captures raced that delay.**
  - coast_land's live capture is 2 s after the change (`scenes_4912 step`), h23's 10 s after
    (`scenes_4915 change --wait`).
  - Both live fields are byte-identical to their before fields. So `02_coast_land_live` and `h23_live` show the state
    BEFORE the apply.
  - lake's live field differs from its before field in 70,830 samples.
- **A capture-timing fault of this driver (named, fixed).** The first run took its after-reload pictures
  (`t05_reload`, `03_reload`, `h23_reload`) as soon as the producer was ready. At that moment the EXE's UI was not up,
  the city and the units were not drawn, and part of the residency's tiles were still gray.
  - `t05_reload` showed it; the lead's question was whether it was fog. Answer: no, the picture was taken too early.
  - The fix: `i2_4915.sh` has `settle`, 30 s and the in-game/box check, before every capture after a load. It mirrors
    `accept_4915.sh`'s 20 s.
  - Retake (11:35-11:43, mode `retake`, prep without copies): `tiny_mouth/t05_reload2.png` and
    `huge/gibraltar/h23_reload2.png`. They show the full frame: UI, city and units.
  - The fields of the first run are data and are unaffected.

**Result: huge coast_land and h23 in the player's configuration [L, 2026-11-21 11:43-11:55 EST, c5h-a, the same
layout; DIAGNOSTIC, disturbed].**
- **Setup.** Mode `relive`: prep without copies; `server_cfg_extra` unset, so there is no recorder.
  - `AW8HT01`, then cases_huge h02: coast_land, picked (103,19) again.
  - `AW8HT02`, then h23.
  - Each with the 30 s settle, then `--before --wait 20`.
- **From the lt.py call to the apply** (render.log; the call time from `<id>_case.json`):

  | case | call | the gate sees it | applied | apply (render thread) | `Waves_Reload` |
  |---|---|---|---|---|---|
  | h02 coast_land | 11:48:29.509 | 11:48:30.987 (+1.48 s) | 11:48:31.517 (+2.01 s) | 35.7 ms (EXE write 9.4) | ok, 159.7 ms |
  | h23 | 11:53:49.760 | 11:53:51.237 (+1.48 s) | 11:53:51.570 (+1.81 s) | 29.3 ms (EXE write 6.1) | ok, 96.9 ms |

  - 0 LOUD, no box, no crash.
  - The live fields changed: h02 114,012 heights, the same count as the recorded run's apply.
  - End: run_3308 restore rc 0 (`provision_verify_rc` 0).
  - c5h-a released 11:55:19.
- **Gibraltar (MC #8709, #8641 condition 4) [L].** Scratch: `C:/c5-64bit-work/0615/w8/gib.py`, on
  `huge/relive/h23_{before,live}_field.npz`. The hex is taken as the nearest plot centre, an approximation of the
  producer's hex.
  - Gibraltar's own hex (112,42) keeps the load's ground: 4,005 of its 4,096 samples are unchanged. The other 91 are
    heights only, within 2.8 samples of the approximate edge, at most 238 u16.
  - No new step at the hex border: the largest |dz| across it is 1,880 u16 live and 1,880 in the load. W5 offline:
    2,030 against 2,090.
  - Within 32 samples outside the hex, 2,090 of 10,733 samples keep the load's heights. This is not W5's "about 487
    old-shoreline samples": that count is of shoreline samples only, and it is not re-measured here.
  - Verdict (MC #8589): **drawn right**.
- **Pictures** (DIAGNOSTIC), in `F:/c5-64bit-work/0615/4915/i2/pictures/`:
  - `h23_gibraltar_triplet_DIAGNOSTIC.png`: before, live, after the reload (the settled retake). It shows the new grass
    hex beside the rock and the rock's own hex as loaded.
  - `huge_coast_land_pair_DIAGNOSTIC.png`: before and live.
  - `mouth_triplet_DIAGNOSTIC.png`. The mouth reload, settled: `tiny_mouth/t05_reload2.png`.

**I2 summary (2026-11-21 10:52-11:55, c5h-a, build 4390913, server.exe 81f10ca8...).**
- The lw ridge is gone live (8.17).
- The mouth shows only the named keep (11.3 item 1 is now [L]).
- huge lake, coast_land and h23 are drawn live; Gibraltar is drawn right.
- 0 LOUD, no box, no crash in 10 processes.
- One test-only limit: the recorder's full dump stalls the render thread on huge.
- One driver fault, fixed: after-reload captures were taken too early.

### 11.6 The load-time "render loop did not run" lines are not live terrain (MC #9651; lead7-0615, 2026-11-21 18:06 EST) [L, logs of 0042's runs on c5h-new-b, build 26577625]

Question (MC #9651): 0042's run S9 (c5h-new-b, build 26577625, layout of main eca6442f5 with 0615's milestone, live
land/water on by default) logged `models mode: the server's render loop did not run for 1708 ms` and then `... 333 ms`
about 2 minutes in, before any combat case. Is that the live-terrain path?

Answer: no. Both stalls belong to the load's terrain hand-over, which every run of the day shows, with or without 0615:

- **Where they end [L].** The 1708 ms line is written at server time 51.629 s, 6 ms after `terrain: field 56x40 plots
  ... in 352.0 ms (setHeightField 253.6, tile source 56.7, water 40.0, ...)` (51.623); before it, from 51.169, the
  terrain tap read (102.3 ms) and the residency classify (56.5 ms); before that, from 49.82 (the units' load begin),
  the wait for the EXE's terrain tap. The 333 ms line (52.222) follows the first drawn frame's catch-up bakes
  (`91 bakes in 269.0 ms before the draw`, ticket 0424) and the forest's Tree_Shadow marks (14.9 ms).
- **Live terrain runs after the first stall [L].** 0615's first lines of the load come after it: `live terrain: load
  seq 3: waiting for the bake's plot table` at 51.629, the map seed read at 51.898, `producer requested` at 51.898.
  No live change was applied in S9 before the stalls (the cases began later).
- **The same stalls without 0615 [L].** 0042's runs S8, S10, S11 and S12 of the same afternoon on layouts without the
  milestone (no map-seed line in their server.log) show the pair at the same place, larger: S8 2488 / 438 ms, S10
  2883 / 516 ms, S12 2591 / 411 ms on the same 56x40 map, S11 3235 ms then 2768 ms at a 128x84 load (field build
  2337.9 ms); each first stall ends within 17 ms of the `terrain: field ...` line. Evidence:
  `C:/c5-64bit-work/0042/w38/s4779/live-S*-r1/out-*/server.log` (0042's W38 folder, read only).
- **What it costs [C, city-renderer.md, 0641's arms].** A stall of the server itself counts at most 250 ms toward the
  models mode's bounds; 0641's arms A1/A2 suspended the render thread for 3 s and 12 s with no loss and no end.
- **Not one of the fix-forward items (MC #9538) and not a sixth:** it is the load's field build on the render
  thread (0424's catch-up and the terrain field hand-over), owned outside 0615. [I] which thread waits for the tap
  between 49.82 and 51.17 was not read from the code; a render-thread profile of one load would confirm it.

### 11.7 Changes the server never hears of within the turn: the feed's plot notifications (I3 finding; lead7-0615, 2026-11-21 18:45 EST) [L for the run, C for the code]

Seen in I3 (W9, c5h-a, build 4390913, layout t8 = main c1dc17d2a, product Tiny, run 20261121-182329-38836-1, all
cases in game turn 8): t07 (39,12 flat to ocean), t08 (26,9 sea to land), t09 (lake 14,21) and t10 (coast_land 10,11)
were changed in the DLL (lt.py's apply json) but neither drawn nor refused: no live-terrain or ice/waves line, the EXE's
live field equal to the field before. Changes 1-6 (t01-t06) and 7-18 (t11-t22) were drawn; plots 39,12 and 14,21
turned up later inside t20's TooLarge wait; 26,9 and 10,11 in no line.

- **Where it stops [L].** The server never received the changed plots. The render host's feed counter
  (`host 1s ... feed changed=`) is 3990 after t06 (18:26:25), 3991 after t07 (18:26:53), and stays 3991 through t08,
  t09 and t10 (18:27:18, 18:27:38, 18:27:55) although events arrive; it moves again at t11 (3993, 18:28:13). The
  residency's `plots_changed` stays at 14 over the same span. So `LiveTerrain::onTable` and `IceWavesPoll` had no new
  table to look at; the silence is upstream of 0615's code. The ice reload at t06 is a coincidence: the ice reloads at
  t13 and t15 (300 s, 339 s) were followed by live changes.
- **Why [C].** Between turns the feed re-reads a plot only when a notification marks it dirty
  (`server/src/remote/srv_remote_feed.cpp`: `MarkDirty` from `OnFeatureChanged` and `OnPlotOnly`, which the proxy
  object raises for `GameplayNaturalWonderRevealed`, `GameplayWallCreated` and `GameplayYieldMightHaveChanged`;
  `OnPlotStateChange` carries resource, improvement and route only). A plot-type or terrain change raises none of them
  by itself: the DLL sends `GameplayYieldMightHaveChanged` only from `CvPlot::updateSymbols`, which
  `updateYieldFast` calls only when one of the plot's yields changed, and it returns early while the plot has no area
  (`getArea() == -1`). A change whose yields stay the same (or that is made while the plot has no area) is read only
  at the next turn change (the full refresh in `AtLeaveUpdateBody`) or when a later notification marks the plot.
  [I] which of the two held for t08-t10 is not read; the outcome is the same.
- **What the player sees [I].** Nothing wrong is drawn: the old coast stays, as stock keeps it until a reload, and the
  change is drawn at the next turn's full refresh (not confirmed in this run, which stayed in turn 8), unless that
  refresh batches it with other changes into a TooLarge wait (then at the reload). The same holds for 0465's height
  changes (a hill raised or flattened with the same yields), which were on main before 0615.
- **Not one of MC #9538's items; not limited to the ice path.** It is the feed's coverage of plot changes.

**The fix as built (ticket 4914, MC #9700; W11, branch `w11-feed`, 2026-11-21 18:50-19:10 EST) [C for the code and the
offline tests, I for the cost and the delays until the lead's live smoke].** A second way to find a changed plot, beside
the notifications: the **rolling refresh** in `SrvRemoteFeed::AtLeaveUpdateBody`
(`server/src/remote/srv_remote_feed.cpp`). At every LeaveUpdate that is neither a snapshot nor a turn change, after the
dirty plots, it reads the next K plots of the grid round robin (a cursor kept across calls, wrapping at W x H, reset to
plot 0 by every snapshot, so also by a new grid) through the same `ReadPlot` + `SrvFeed::UpdatePlot(kFGetterAll)` as a
dirty plot: a record it finds changed is marked changed exactly as a dirty refresh marks it and reaches every reader
through the next Drain's `changedPlots`. A plot a notification named in the same Update is read by the dirty refresh
first and so is not counted again. If the map's width (`ICvMap1::PlotNum(0, 1)`) is no longer the table's, the pass
is skipped (no plot of the old grid read) until the next snapshot or turn change. It runs whenever the feed is on
(`render=1` or `feed=1`), whatever live terrain's switch says.
- **The switch**: `server.cfg` `feed_rolling_plots` = K (default 256; 0 or negative: off, for comparisons), the feed's
  own pattern for server.cfg switches (`SrvCfgInt`, as `feed`, `render`, `render_units`); read at each snapshot; a test
  sets it through vpproxy.ini `server_cfg_extra` (TEST ONLY), no rebuild.
- **Log lines (all plain, never per plot)**: at each snapshot `feed: rolling refresh: K plots per Update
  (server.cfg feed_rolling_plots); a sweep of the N plots takes S Updates` (or `... off`); per LeaveUpdate that found
  any `feed: rolling refresh found N plots changed that no notification marked (x,y: up to four plots) at turn T; k plots
  read in X ms; totals: passes, read, found, ms`; once per turn change after a turn with passes `feed: rolling refresh
  in turn T: P passes, R plots read, F found, X ms (Y ms per pass, longest Z ms); K k` (the cost numbers of MC #9700
  (2)). The rolling reads also count in `feed-per-turn.tsv`'s `plot_reads` / `read_ms` and the session summary's plot
  reads (the code of both is unchanged).
- **How fast a change is found**: within ceil(W x H / K) Updates, the worst case being the plot read just before it
  changed. The game-core thread runs one `Update` per request of the EXE's frame, at most one per 0.1 s tick
  (`research/08-performance/turn-time-study.md`, "The EXE asks for an Update only on a 0.1 s tick" [C]); I3 counted 4,235
  `ICvGame1::Update` in 532 s of play (proxy-calls.tsv of run 20261121-182329-38836-1, snapshot at 38.4 s, exit at
  570.4 s) = 7.96 per second [L]. So Tiny (56 x 36 = 2,016 plots): 8 Updates, about 1.0 s; huge (128 x 80 = 10,240): 40
  Updates, about 5.0 s [I]. In AI turns with `AiUpdateTickMs = 0` (the default since 0566) the Updates follow the
  frames, so the sweep is shorter there.
- **Cost [I until measured live]**: the extra reads run on the DLL's (core) thread inside the dispatch. Recorded full
  read rates: 2,016 plots in 0.42 ms (I3's snapshot, feed-per-turn.tsv, 0.21 us per plot), Tiny full refresh median
  0.21 ms and huge 1.5 ms (renderer-in-server-design.md 2.2: 0.10 and 0.15 us per plot). So K 256 costs about 0.03-0.05
  ms per LeaveUpdate on any map size (K bounds it, not the map), plus the feature-info look-ups of the pass's distinct
  features (`FeatureCache`, a few calls per pass): under MC #9700's 0.1 ms bar on huge [I]; about 0.2-0.4 ms per second
  at 8 Updates a second. Offline on stand-ins (the feed's own share, trivial getters, workstation under load):
  12-22 us per LeaveUpdate at K 256 on Tiny and huge sizes (`TestRollingTiming`; under ASan 108-147 us) [C offline].
- **What else now updates earlier within the turn** (the fields `ReadPlot` reads; layout fields such as route,
  resource, pillage bits and the revealed improvement come only from `GameplayPlotStateChange` and are unchanged):
  terrain and plot type (live terrain's land/water and height; the residency source's `BakeRelevantChange` re-bake;
  the forest scene for plot-type changes), feature (forest scene, re-bake), the river and natural-wonder flags (re-bake),
  owner, fog bits (the fog scene re-reads the table on any changed plot; the DLL defers some fog notifications to
  `updateDeferredFog`, which the EXE mostly calls inside the same unit callback [C, CvPlot::updateFog, CvDllMap], so a
  fog bit found ahead of its notification is the exception [I]), the continent art type, and the getter's improvement
  for a plot no `GameplayPlotStateChange` has named yet (farm fields re-read the table on any changed plot). The roads
  extra re-reads its table on any changed plot (its output depends on routes only). Any of these becomes visible up to
  ceil(W x H / K) Updates after the change instead of at the next turn.
- **Tests [C]**: `server/tests/feed/test_remote_feed.cpp` (run by `run_remote_feed_test.py`, which now adds `ICvMap1`
  to the generated stand-ins): 215 checks, 0 failed, plain and ASan (2026-11-21, 0.1b pin headers). The round robin
  covers every plot once per cycle in order on 35 plots with K 8 and with K above the plot count; a type and terrain
  change with no notification is found in exactly 5 = ceil(35 / 8) LeaveUpdates in the worst case and within 5 for each
  of the 35 plots; a snapshot resets the cursor; no GetPlot past the grid; K 0 reads nothing (not even GetMap); no map
  and an unreadable grid keep their outputs.
- **Open**: the live numbers (ms per LeaveUpdate on the Tiny and the huge save, one huge AI turn with and without, K
  lowered if 256 costs over 0.1 ms on huge), the looks (a pillage and an AI-built road: nothing drawn twice, no flicker)
  and I3's t06-t10 again are the lead's live smoke. The cause-true fix, a DLL notification on a plot-type or terrain
  change, is the follow-up ticket `tracker/issues/0656-feed-plot-type-notification.md` (after the beta); the rolling
  refresh then stays as the safety net. The ticket also names a server-only alternative [C for the DLL path, I for its
  coverage]: a setter called with `bRebuildGraphics` true (the test mod's calls) flags the plot's layout, the EXE's
  `ICvMap1::UpdateLayout` pass then sends `GameplayPlotStateChange` for it (CvPlot::updateLayout, unconditional), and
  `OnPlotStateChange` does not mark the plot dirty today; one MarkDirty there would re-read it after that pass.
  *Built 2026-11-21 19:20 (MC #9712, below).*

**The plot-state path (MC #9712; W11, `w11-feed`, 2026-11-21 19:15-19:30 EST) [C for the code, the tests and the DLL
reading at pin 1b87dd185; L open].** `SrvRemoteFeed::OnPlotStateChange` now also marks its plot dirty (one call,
`MarkDirtyByState`), so the next LeaveUpdate re-reads the plot's getter fields (plot type, terrain, feature, ...) through
the dirty refresh. The rolling refresh stays as the net (default K 256). Which path found a change is logged per plot in
one plain line per LeaveUpdate that found any: `feed: plots found changed at turn T: by the plot-state path N (x,y: up to
four), by the rolling refresh M (x,y: up to four); rolling k plots read in X ms; totals: plot-state S, rolling P passes,
R read, F found, Z ms` (this replaces the rolling-only "found" line above); the per-turn cost line ends with `; the
plot-state path found N`. A plot both paths read in the same Update is counted once, by the plot-state path (the dirty
plots are read first). Tests: `TestStatePath` in `test_remote_feed.cpp` (229 checks in all, 0 failed).
- **Which changes reach `OnPlotStateChange` [C, the DLL at 1b87dd185].** The DLL sends `GameplayPlotStateChange` from
  one place, the end of `CvPlot::updateLayout` (CvPlot.cpp 14022-14164, unconditional). `updateLayout` runs for a plot
  when the EXE calls `ICvPlot1::UpdateLayout` for it (CvDllPlot.cpp 307) or `ICvMap1::UpdateLayout`, which runs
  `CvMap::updateLayout` (CvMap.cpp 1010) over every plot whose layout flag is set and clears the flags. The flag is set
  by `setLayoutDirty(true)`, from: `setPlotType` when `bRebuildGraphics` is true and graphics are initialised
  (CvPlot.cpp 7469; the argument defaults to true, CvPlot.h 482), `setResourceType`, `setImprovementType`,
  `SetImprovementPillaged`, `SetRoutePillaged`, `setRevealed`, `setRevealedImprovementType`, `setRevealedRouteType`,
  `updateVisibility`, and callers in CvCity, CvCityConnections, CvPlayer, CvTeam and CvUnit (24 sites). I3 counted
  23 `ICvMap1::UpdateLayout` and 28 `ICvPlot1::UpdateLayout` calls and 2,295 `GameplayPlotStateChange` in 532 s [L].
- **What still reaches the server only through the rolling refresh (or a yield, feature or turn change) [C]:**
  a terrain change alone: `setTerrainType` never sets the layout flag (it calls `setPlotType` only when the terrain's
  water flag differs from the plot's, CvPlot.cpp 7542-7546), so e.g. grass to desert with equal yields, or on a plot
  without an area, raises nothing; `setPlotType` with `bRebuildGraphics` false (a Lua `Plot:SetPlotType(t)` without
  the third argument goes through `BasicLuaMethod`, which [I] reads a missing boolean as false; the DLL's own false calls are map generation, before the
  first snapshot, CvMap.cpp 961, and the world-builder loader); any change before graphics are initialised; and a
  change between the DLL's setter and the EXE's next UpdateLayout pass (the EXE's timing of that pass after the UI's
  `PlotData_DIRTY_BIT` is not read here [I]). The test mod's changes (`liveterrain.lua` 449-451, `SetPlotType(t, true,
  true, true)` and `SetTerrainType(t, true, true)`) set the flag through `setPlotType`; its terrain-only cases (`gd`,
  `fh_hills_back`'s first step) do not.

**Live check on c5h-c (W11, 2026-11-21 from 19:30 EST; build 4390913, product route, layout t11 = w11-feed d627180cc on
the 0.1b pins, server.exe 4440dba9...; Tiny copy AWT00 of c5h-9028-tiny-t0008, b2ceb663...; DIAGNOSTIC: workstation busy)
[I, the plan, written before the run; results below when run].** Driver in the work folder
`C:/c5-64bit-work/0615/w11` (accept_w11.sh: a copy of accept_4915.sh's functions; w11_live.py, w11_join.py,
RUNBOOK.txt; not in git). Steps: I3's t01-t10 (cases_tiny.txt's first ten) in one process with the S1/S2 reload shots;
then a pillage (Lua `SetImprovementPillaged` on a revealed improvement) before / live / a 1 s burst, four AI end-turns,
each new revealed road live + burst, a save and its reload with the same plots; then the feed lines joined per change
(which path found each case's plot first) and the per-turn cost lines. Expected: t07-t10 named within the turn (8 Updates
at most at K 256), no TooLarge wait unless batched, 0 LOUD; the pictures live = after the reload; the burst pictures
equal.

*Results [L, DIAGNOSTIC (the workstation busy), 2026-11-21 19:30-20:04 EST, c5h-c, runs 20261121-193233-28736-1 (the
t01-t10 process) to 20261121-200004-29448-1; claim 19:25, release 20:04; stop after the newest save settled, run_3308
restore provision_verify_rc 0, no dialog box, no server crash, 0 LOUD in five server processes (collect_4915: loud 0,
known_loud 5, fallback 0)].*
- **(5) I3's t01-t10: all ten drawn live in the same turn** (collect_4915: live true for each, render thread 8.8-41.1 ms,
  Waves_Reload ok each). I3's four unseen changes now reach the server within the Update: t07 (39,12 flat to ocean),
  t08 (26,9 sea to land) and t09 (lake 14,21) by the **plot-state path** (the found line 0.0-0.1 s after the change),
  t10 (coast_land 10,11) by the **rolling refresh** (within 1 s; the change's time and the line's are known to +-1 s,
  the run folder's name). Among the multi-plot changes the rolling refresh also named one plot of t05 (53,13, 0.5 s) and
  one of t06 (54,4, 0.2 s); t01-t04 were read through another notification first (the yield path: no found line).
  Totals: 6 found lines, 3 plots by the plot-state path, 3 by the rolling refresh (`w11_join.py`, F: summary).
- **(3) The look.** Pillage: a farm laid by Lua on (51,15) beside the capital, then `SetImprovementPillaged`: the farm
  turns from green to the pillaged brown with the smoke; live, in the three burst pictures 1 s apart, four AI turns
  later and after the reload it is the same (crops side by side: no green farm over the brown, no flicker; the burst
  differences are the units' idle animation, the smoke and the water). Road: **no AI-built road existed anywhere on the
  map by turn 33** (24 AI end-turns on Tiny; a Lua count over all plots: 0 routes outside cities), so the road was laid
  by Lua (`SetRouteType(ROUTE_ROAD)`) on (50,13): drawn live at once, the same in the bursts and after the reload. An
  AI-built road stays unchecked live. Both layout changes made no found line (their getter fields did not change).
  Pictures: Visual Checks, issue 4914, `F:/c5-64bit-work/0615/w11/tiny/look/` (p_*, q_*, the strips).
- **(2) Cost on Tiny**: 25 per-turn cost lines (4,143 passes, K 256): ms per pass median 0.063 (0.045-0.077 per turn),
  the longest single pass 0.27 ms; the t01-t10 process 1,987 passes in about 250 s of play = 7.9 Updates a second (the
  sweep of 2,016 plots about 1 s). Higher than the 0.03-0.05 ms estimate from the full-read rate (busy machine,
  `DIAGNOSTIC`), under the 0.1 ms bar at the median, over it for single passes. Huge and the huge AI turn with K 256
  against 0: the quiet window (RUNBOOK B), below.

**The huge AI turn, K 256 against 0 (MC #9700 (2); W11, MC-B's GO 01:41, quiet window 2026-11-22 01:45-02:25 EST, c5h-a,
build 4390913 (stamp 0x546CD0A8), product route, layout t11 = w11-feed d627180cc on the 0.1b pins, server.exe 4440dba9...;
the save copy AWH00 of C5H-VP8P-HUGE_0350, c499d3c6..., turn 350, 128 x 80) [L, under the quiet lock (`quietlock.py run
--take`, two holds 01:42-01:59 and 01:59-02:12); n small: an indication, grade C].** Each arm: `feed_rolling_plots` = 256
(ON) or 0 (OFF) through `server_cfg_extra`, a reload of AWH00 in a new process, the producer's ready line, 30 s settle,
one end turn, the wait for the next human turn (wall seconds from the end-turn call to the wait's return). Order ABBA then
BAA (ON OFF OFF ON, OFF ON ON; the last OFF not started: the 02:12 cut-off). Claim 01:41, release 02:13; stop after the
newest save settled, run_3308 restore provision_verify_rc 0, no dialog box, no server crash. Every arm logged the same
81 known `remote: LOUD: stale Unit instance` lines (ticket 0576's class, in both arms alike) and no other LOUD.

| arm | start | AI turn wall s | rolling line of turn 350 (from the snapshot to the turn change, about 60 s) | C: read latency median / max ms |
|---|---|---|---|---|
| ON | 01:42:21 | 30.21 | 351 passes, 15.21 ms, 0.0433 ms per pass, longest 0.0924 | 0.20 / 1.27 |
| OFF | 01:46:30 | 29.92 | (off) | 0.19 / 6.22 |
| OFF | 01:50:55 | 29.24 | (off) | 0.16 / 5.14 |
| ON | 01:55:21 | 28.59 | 350 passes, 14.51 ms, 0.0414 ms per pass, longest 0.0811 | 0.16 / 0.59 |
| OFF | 01:59:49 | 29.55 | (off) | 0.20 / 0.82 |
| ON | 02:04:08 | 29.70 | 351 passes, 14.77 ms, 0.0421 ms per pass, longest 0.0962 | 0.23 / 1.71 |
| ON | 02:08:27 | 29.01 | 342 passes, 14.48 ms, 0.0423 ms per pass, longest 0.1086 | 0.17 / 1.23 |

- **Cost on huge**: 0.041-0.043 ms per pass (K 256: the cost is the same as Tiny's at the same K, as expected), the
  longest single pass 0.081-0.109 ms: under the 0.1 ms bar on average, one pass in four arms just over it. About 5.8
  Updates a second in these processes (350 passes in about 60 s), so a sweep of 10,240 plots (40 Updates) takes about
  7 s; the worst-case delay of a change no notification names is 40 Updates. K stays 256.
- **The AI turn**: ON 29.38 s mean (n 4), OFF 29.57 s (n 3), ON - OFF -0.19 s, Welch's 95 % interval
  [-1.28, +0.90] s (sd 0.72 and 0.34 s, df 4.5): no difference resolved; the smallest the design could detect is about
  1 s (3 % of the turn). The
  rolling refresh's own measured time is about 15 ms per 60 s (0.025 %), two orders below what the pair can see.
- C: latency per arm from `tools/diskwatch/diskwatch.py run --drive C: --interval 5` over the window (out on F:): read
  median 0.16-0.23 ms, the maxima 0.6-6.2 ms (two OFF arms), writes under 0.6 ms; disk idle at least 75 %.
- Files: `F:/c5-64bit-work/0615/w11/huge/` (arms.jsonl, summary.json, diskwatch_C.jsonl); driver and scripts in
  `C:/c5-64bit-work/0615/w11/` (accept_w11.sh huge-*, arms.sh, arms2.sh, huge_summary.py; not in git).

### 11.8 The server.exe crash at huge h18 (I3, W9, 2026-11-21 19:18 EST): an unguarded plot row in the producer's window check [C for the cause; L for the crash]

**The crash [L].** c5h-a, build 4390913, product huge (rerun of `accept_4915.sh product c5h-a huge`), layout t8
(server.exe 952208874f3a7778..., code = main c1dc17d2a), case h18 (`apply ice_land --near cap+0,-30`, picked (103,2),
7 plots changed, at the pole of the 128 x 80 map), 19:18:43:
- server.exe: access violation 0xC0000005, "read of 0x000001e12d4e2a34", RVA 0x46a05f (`crashes.log` "Location (in
  file)" server.exe+0x46945f), about 23 s after the gate's "the coast waves wait for the reload: 2 of the 2 plot(s)
  ... (first (103, 2))" (643.586 s). The EXE then ended by design (proxy "on_server_death=exit", exit code 3). No box.
- Evidence (copied before any restore): `F:/c5-64bit-work/0615/w9/crash_h18/20261121-190759-42740-1/` (moved from C: by lead9, 21:4x, hash-checked) (server.log,
  render.log, vpproxy.log, `crashlogs/CvMiniDump_20261121_191843_5.4.8-80-g1b87dd185_Release.dmp`, `crashes.log`).
- The first huge run of the evening never reached h18 (a driver fault stopped it before the change, 11.4), so this was
  h18's first run.

**The stack [C]** (llvm-symbolizer on t8's server.exe and server.pdb; RVAs, innermost first):
`c5r::liveterrain::Producer::change(PlotTable const&, ChangeResult&, Options const&)` (0x46a05f; the c5r library
carries no line info, so the line is read from the disassembly below) <- the same function (0x465546, 0x465507) <-
`Producer::change(PlotTable const&, ChangeResult&)` (0x112b68) <- `C5rProducer::change`
(`server/src/render/srv_liveterrain_c5r.cpp:150`) <- `LiveTerrain::run` (`srv_liveterrain.cpp:519`) <-
`LiveTerrain::worker` (`srv_liveterrain.cpp:467`), the producer's worker thread.

**The line [C].** The faulting instruction and the ones before it load a set node's y (`+0x20`), subtract 2, multiply by
`after.w`, scale by 0x38 (sizeof `PlotInfo`), add the node's x times 0x38, and compare the byte at `+0x34` with 0
(`p.lake`); the next test reads `opt.protectWater`. That is `render/src/liveterrain/producer.cpp` at main c1dc17d2a,
lines 840-846, the loop over `wcells`:
`const int py = gy - 2; const PlotInfo& p = after.plots[size_t(py) * after.w + gx]; if (p.lake && ...`,
with no check that `py` lies in 0 .. after.h - 1.

**How a frame cell gets there [C].** Line 822 counts a sample as water when its cell row is outside the map
(`py < 0 || py >= after.h`, the map frame's margin rows); line 824 protects (skips) water only when `!classMode`. In a
land/water change (`classMode`) a window that reaches the frame inserts those margin cells into `wcells` (line 828),
and lines 843-844 then index `after.plots` with a negative row (or one past the last): an out-of-bounds read before (or
after) the vector. h18's window at the pole reached the top margin; on huge the address was unmapped, so it crashed.
- Where such a read lands in mapped memory it does not crash but reads garbage, which can raise a false `Lake` or
  `RiverEdge` wait [I: not observed; Tiny's ice_land t06 at (53,5) ran live without a wait].
- t9 (epic-0615-lw-2 @a743b07c1, W10's guards): the same loop, unguarded (its lines 847-848) [C, read]; so t9 is
  expected to crash the same way [I until its arm runs].
- The fix: in that loop, skip a cell whose `py` is outside 0 .. after.h - 1 (it is frame water, already handled by the
  water rules above it). Not made here (a product change: the team's code owner) [I].

**Repro arms (lead8 / MC-B, 19:2x) [L, c5h-a, build 4390913, layout t8].** h15-h18 from `C5H-0615-4912-APH_S2`
(`C:/c5-64bit-work/0615/w9/repro_h18.sh`):
- **t8, key on (default): the crash reproduces.** h15, h16, h17 live; h18 at 19:26:40, server.exe AV at the same RVA
  0x46a05f (read of 0x1c9ceaa9a34) at 19:26:43; the EXE ended by design; no box. Run `20261121-192200-7052-1`, copied
  to `F:/c5-64bit-work/0615/w9/crash_h18_repro/`.
- **t8, `C5_RENDER_LIVE_LANDWATER=0`: no crash.** server.log: "land/water changes wait for the reload
  (C5_RENDER_LIVE_LANDWATER ...)". h15-h18 (19:32-19:35) each answered "wait for reload: Water: plot X,Y is or was
  water (the coast lines move)" (h18: "[plots 103,2 104,2]"); 0 LOUD, no box, the game alive after h18. With the key
  off the producer is not in class mode, so the frame's water cells are protected before they reach the loop.
  Run `20261121-192921-43192-1`.
- The t9 arm was dropped (lead8, 19:2x): t9 has the same loop.
- **The fix layout t12 (W12, 975c48700; server.exe 6e307458..., proxy 814f9600..., composite c68bfff9...; copied to
  `C:/c5-64bit-work/0615/w9/t12`, `sha256sum -c` against W12's SHA256.txt OK), run 1 of 3 [L, 20:01-20:04]: no
  crash.** APH_S2 + h15-h18; h18's job drawn live as changes 4 and 6 ("2 plots, box 6432,64-6880,448" and "5 plots,
  box 6368,0-6848,512": the window reaches row 0); the plain "coast (live): N line(s) redrawn ..." line per change;
  0 LOUD; no box. Run `20261121-195927-15148-1` (copied to `C:/c5-64bit-work/0615/w9/runs/`).
- **t12 runs 2 and 3 and the row edits [L, 20:14-20:27]: no crash.** (The run-2 reload was first refused by the
  quiet-machine lock, another team's timed pairs; it ran when the lock was free, 20:14.) Runs `20261121-201529-24920-1`
  and `-202140-19848-1`:
  - h18 in both: drawn live as changes 4-6 ("2 plots, box 6432,64-6880,448", "2 plots, box 6400,0-6848,375",
    "3 plots, box 6368,64-6848,512").
  - The class edits at x=40 in run 3 (`C:/c5-64bit-work/0615/w9/fixarm2.sh`; lt.py `set --plot 40,0 --pt PLOT_LAND
    --ter TERRAIN_GRASS`, back with `--pt PLOT_OCEAN`, the same at 40,79): all drawn live. Row 0 to land: changes 7-8
    (box 2400,0-2784,320; 4 plots, 2336,0-2848,384); back: change 9 (2464,0-2720,256). Row 79 to land: changes 10-11
    (2432,4992-2816,5318; 4 plots, 2368,4928-2880,5318); back: change 12 (2496,5056-2752,5312).
  - 0 LOUD, no "degenerate"/"refused" line of the new guards, no box, in all 3 processes.
  - So with the fix: h18 three times clean on build 4390913, and a class edit in the first and the last row of huge
    drawn live in each direction.

### 11.9 The fix of 11.8 (h18 edge-row read) (W12, branch w12-h18, 2026-11-21 19:20-) [C for the code and the offline runs, L where marked]

The crash facts are in 11.8: huge h18 (`ice_land --near cap+0,-30`), an access violation in `Producer::change` on the
worker thread, 2026-11-21 19:18 EST, c5h-a, build 4390913, layout t8. This subsection covers the fix, its tests, the
sweep, and an audit of every index the producer and the coast code compute from a sample or grid coordinate.

- **Cause [C, ASan].** The fault is in `Producer::change`, in the clean check's loop over the window's cells
  (`for (const auto& cxy : wcells)`).
  - The loop read `after.plots[size_t(py) * after.w + gx]` with `py = gy - 2` and no bounds check.
  - The pattern grid has two margin rows above the map and two below (`gridH = plotsH + 4`). A window sample whose
    nearest hex is a margin cell gives `py` = -2, -1, h or h + 1.
  - Such a sample counts as water (`py < 0 || py >= after.h`). Water is kept out of `wcells` only when the change
    keeps every plot's land/water class (`!classMode`).
  - So in a land/water change within about 3 plot rows of the top or bottom edge, the margin cells reach `wcells`. The
    read then falls up to 2 x w plots before the table or past its end.
  - Offline under ASan on the recorded huge inputs, unfixed head c91ba272e, 19:27, two cases: one edit at (103,2),
    ocean to flat tundra (h18's plot), and one edit at (101,78). Both give `heap-buffer-overflow READ ...
    producer.cpp:849` (the `p.lake` read), called from `lt_producer_run.cpp:200`.
- **The silent half [C].** Where the read landed in mapped memory, the loop decided on whatever bytes it found.
  - In a land/water change only the river-edge check can act on those bytes, and only with `protectRivers` off. The
    lake check runs only outside land/water changes.
  - The server passes the default options, with every protect on (`srv_liveterrain_c5r.cpp` 153). So in the product
    the read could crash but could not change a decision [C].
  - Offline and in tests with `protectRivers` off, it gave false waits. In the plain Release build of the unfixed head,
    `lw_edge_row_no_false_river_wait` waits "river edge at plot 7,17" (row 17 is the margin row h + 1 of a 20 x 16
    map), and `lw_edge_row_rivers_unprotected` failed too.
- **Fix [C].** Commit 30aac843c on `w12-h18`, `producer.cpp`, also cherry-picked alone to main (branch `h18-main`,
  MC #9764): in that loop, a cell with `py < 0 || py >= after.h ||
  gx < 0 || gx >= after.w` is skipped.
  - A margin cell is frame water with no plot, so it has no lake and no river edge.
  - Skipping it gives exactly the result of reading a plot with neither flag, so the skip is the faithful choice.
  - The alternative, treating the cell as a plot with no lake and no river, gives the same decision with more code.
- **Tests [C].** In `render/tests/liveterrain/producer/producer_test.cpp`, on a synthetic 20 x 16 map with land/water
  on.
  - Each of these tests makes a class change and then reverts it, and expects both steps live, reason none, and
    nothing changed outside the windows:
    - `lw_edge_row_top`: rows 0 and 1.
    - `lw_edge_row_bottom`: rows 15 and 14.
    - `lw_edge_row_rivers_unprotected`: the same edits with `protectRivers` off.
    - `lw_edge_row_seam_corners`: a wrapping map, the four corners at x 0 and 19.
  - `lw_edge_row_no_false_river_wait`: puts plots with river edges and the lake flag in the table's spare capacity,
    just past its end, where the bottom margin rows' reads landed. The read can never land inside the table itself,
    because `change` refuses a table whose size is not w x h.
  - Results on 2026-11-21 (the unfixed head is the fix reversed):

| Build | Unfixed | Fixed |
|---|---|---|
| Release | 48 run, 10 failed checks (the false waits above) | 48 run, 0 failed checks |
| ASan (RelWithDebInfo, MSVC 14.43) | `heap-buffer-overflow` at producer.cpp:849, from `lw_edge_row_top` | 48 run, 0 failed checks, 0 ASan reports |

- **The recorded inputs [C, ASan, fixed build].** `lt_producer_run` on `C:/c5-64bit-work/keep/liveterrain-derived-4903`.
  - Maps: huge is 128 x 80 with wrapX; Tiny is 56 x 36.
  - Options are the product's: land/water on, the four protects on, map seed 0x067F5502. Tiny ran with the same seed,
    which only moves the coast jitter.
  - The live run's four row edits ran here first, as one chain on one producer: (40,0) to grass, back to ocean, then
    (40,79) to grass, back. All four went live with reason none and gave 0 ASan reports.
  - The sweep (`sweep.sh`, 19:35-19:51) had 48 cases, each a fresh producer with one water-to-land class edit
    (tundra, feature removed):
    - huge rows 0, 1, 2, 77, 78 and 79, at x 0, 40, 103 and 127;
    - two h18-like seven-plot blobs, (102-104, 0-2) and (100-102, 77-79);
    - six-plot edits across the x seam, at the top and at the bottom;
    - Tiny rows 0, 1, 2, 33, 34 and 35, at x 0, 28 and 55;
    - four-plot Tiny edits across the seam, top and bottom.
  - Result: **0 ASan reports**. 44 cases went live with reason none. The 4 edits across the seam waited too-large:
    - huge: "the changed coast lines span 8326x173 samples" (top) and "8305x169" (bottom);
    - Tiny: "window 3871x384 samples" (top) and "3847x386" (bottom).
  - Those 4 waits are not a fault. The changed lines' sample box is taken in unwrapped x, so plots on both sides of
    the seam span the whole map width, and the change waits for a reload. Single-plot edits at x 0 and x 127 (huge)
    and at x 0 and x 55 (Tiny) went live [C].
- **Audit of the computed indexes (MC #9725 item 1) [C, read on w12-h18 @975c48700].** The table lists every index into
  a plot table, a per-cell vector or a per-sample vector that is computed from a sample, a grid cell or a cell/plot
  mapping, with its guard:

| Where | Index | Guard |
|---|---|---|
| producer.cpp `buildCells` (263-265) | plot (x, y) loops, cell from `plotToCell` | loops 0..h-1, 0..w-1; the table size is checked by `create` (526) and `change` (563); the cell range is checked (261, refused as BadLayout) |
| `tryPlace` (347-356), `forUsedCells` (488) | cell of a piece's used cell | `gx`/`gy` range test; cells outside the grid are skipped |
| `change`: changed plots (571-573), the coast-plot check (837-838) | cell from `plotToCell` of a plot | the same mapping `buildCells` accepted for every plot (567) |
| re-match anchors (616-617) | anchors from freed cells | only fed to `tryPlace` (guarded) |
| `(*d.meshes)[fromCur[i]]` (708) | index into the load's lines | `d.meshes` is built with one mesh per line of `d.curves` (515), and both are replaced together (982-983) |
| river centres (777) | plot loop | 0..h-1, 0..w-1 |
| class-mode wonder keep (798-799) | `gy0 - 2` from `sampleCell` | `py0 >= 0 && py0 < after.h` (4913 (b)) |
| `w.flags[c0]`, `isChanged[c0]` (793-808), `w.flags[gx * gh + gy]` (826) | cell from `sampleCell` | `sampleCell` clamps `gy` to 0..gh-1 and wraps (or clamps) `gx` |
| marsh read (827) | `gy - 2` | read only when `!water`, and water includes `py < 0 \|\| py >= after.h` |
| **the clean check (852)** | `gy - 2`, `gx` | **none before 30aac843c (the h18 fault); now cells outside 0..h-1, 0..w-1 are skipped** |
| field `fk` (823-824, 949, 963) | window sample | `wy0 >= 0` and `wy1 <= sy` (722); x through `wrapX` on a wrapping map, otherwise the window is clamped to 0..sx (723) |
| `rB`/`rA`, `dist`, `wt`, `prot` (802, 911) | window index | all sized WW x WH; `windowRegion` writes W x H |
| `blend` (411-449) | `sampleCell`; window `k`; piece `tk` | the cell is clamped; rows and columns are clipped to the window (424, 432-436, 441) |
| `windowRegion` (118-131) | block rectangle, `r[...]` | the window lies inside the field (120); `regionBlocks` refuses a rectangle outside the map (coastlines.cpp 1022) |
| `windowStrips` (135-160) | `sg.wc + x` | `segments` splits the window at the seam into runs inside 0..W-1 |
| coastlines.cpp `buildCoastCurves` (850), `regionBlocks` (1056) | plot loops, cell from the G matrix | loops 0..h-1, 0..w-1; the cell range is checked, refused as BadLayout |
| `Grid::at` (114-115): `buildEdges` (171), `window6` (256, 263), `trace` (359) | neighbour cell | `exists()` (y in 0..gh-1, x wrapped or in 0..gw-1); otherwise the grid's `none` cell |
| `trace` start (344), smoothing (282) | cell loops | 0..gh-1, 0..gw-1 |
| `listRanges` (964-981), seeds (1059-1072) | block from a point | points are finite and within kRegionPointReach (MC #9538); x and y are wrapped into the field; seeds are tested 0 <= bx < nbx, 0 <= by < nby |
| `Fill::set` / `blocked` (991-993) | 64 x 64 block | the `inside()` test and `& 63` |
| coaststrips.cpp rasteriser (126-141), `applyStrips` (311-312) | field row, column | rows are clipped to the field and the window; `column()` returns -1 outside; a window outside the field is refused |

  - One finding with no fault: `trace` follows a link to a cell outside the grid through `Grid::at`. That returns the
    grid's own `none` cell, and `trace` then marks `none`'s slot as used. This writes a member, not out of bounds [C].
  - No other unguarded index was found.
  - Every guard in the table is also on main at d1afab27d; there the producer's line numbers are 1 to 4 lower. The
    team branch adds W10's non-finite-triangle refusal in the strips (MC #9538, 8.10.3); the table does not rely on it.
- **Repro commands.** Run from the worktree root; the scripts are in `C:/c5-64bit-work/0615/w12/`, outside git.
  - ASan build: `python -I render/build.py --asan --config RelWithDebInfo --target lt_producer_run --build-dir <dir>`,
    and the same with `--target c5r_liveterrain_producer_test`, both through `perf/quietlock/quietlock.py run`. The
    MSVC bin folder (the ASan runtime DLL) must be on PATH.
  - One case (`run_cases.sh` makes `after.txt` and runs it):
    - `after.txt` is `plots.txt` with line `103 2 ...` set to terrain 3 (tundra) and feature -1.
    - Run it under `shell/tools/memcap.py` with `ASAN_OPTIONS=log_path=...`:

```
lt_producer_run --lib <install>/Resource/Common/TerrainPiecesLibrary.fxsb --block huge/block.bin \
  --plots huge/plots.txt --h16 huge/h16.raw --m8 huge/m8.raw --change after.txt --result res \
  --coast <coast styles folder> --game-dir <install> --map-seed 0x067F5502
```

  - The tests before and after the fix: `before_after.sh`. It reverses the fix, builds and runs both variants, then
    restores the fix.
- **Live, both EXE builds [L].** Layout t12 = w12-h18 @975c48700, built on the 0.1b pins: server.exe 6e307458...,
  proxy 814f9600..., composite c68bfff9.... Each run is h15-h18 from a copy of `C5H-0615-4912-APH_S2`, with h18's
  anchor clamped to (101,0), which picks (103,2) and changes 7 plots, as in the crash run.
  - Build 4390913, c5h-a (W9): three runs, then the row edits at x=40. h18 had no crash in any run, and the four row
    edits were all drawn live (changes 7-12). 0 LOUD lines, no box. Details in 11.8, "Repro arms".
  - Build 26577625, c5h-new-b (W12, `C:/c5-64bit-work/0615/w12/run_w12.sh`; check, prep and end are `smoke_4914.sh`'s
    with PREP=lay). The run was from 20:15 to 20:47 EST.
    - h18 had no crash in any of the three runs. Each time it drew 3 applies of one change (render thread 12-23 ms).
    - Then one more load and the four row edits: (40,0) to grass, back to ocean, (40,79) to grass, back. All four were
      drawn live as changes 1-6, with 2, 1, 2 and 1 applies (render thread 9-35 ms) and 2-4 coast lines redrawn each.
    - In all five game processes: 0 LOUD lines, 0 "wait for reload" lines, 0 dump files, no box.
    - End: the stop waited for the newest save to settle; `lay.py restore` reported "restore ok"; the saves restore
      verified 56 files.
- **Rule A.** The fix only stops a crash; nothing new is drawn by default. The windows of changes away from the edges
  are unchanged, because the skipped cells were never inside the map.

### 11.10 The headed video for the owner and live against reload on oldpc (ticket 4916; lead-0615, 2026-11-22 08:24-08:56 EST) [L, oldpc, build 4390913, DIAGNOSTIC (D112: the product path of `product_path.py`, not the installer's layout)]

- **Build:** the first beta candidate's own layout `x64-1b87dd18-p06d9e82` (commit 06d9e82, which contains the 0615
  merge 33f50a43a), copied read only: server.exe 54d99b8e..., proxy 9245a061..., shell_composite.dll cb781d20..., each as
  the layout's SHA256.txt. Save: 4906's copy of `c5h-9029-tiny-t0008` (SHA-256 b2ceb663...). Live land/water default on.
- **Jobs** (`server/evidence/0615/4916/`, on testbed `oldpc`, headed, slots 2; all rc 0, restored, `provision verify` ok,
  0 LOUD lines in server.log, render.log and vpproxy.log, no box):
  - `video_job_4916.py` = 4907's `video_job.py` (9.9) with 0615's steps: `lw` (LW1 (53,13) and LW2 (55,15) land to
    ocean), then `wl` (WL (51,12) coast to grass), zoom 11.0 (`--zoom min`). Run 1, 20261122-082404-7974: one camera
    on (53,14); WL sat in the lower-left corner under the unit panel. Run 2, 20261122-083201-54ab (`--per-step-camera`):
    lw on (54,14), wl on (51,12). Both changes were live within about 1 s: render thread 45.7 / 12.8 ms,
    `Waves_Reload` ok 18.2 / 18.3 ms, a new water tap copy each (copies 2 and 3, water z 10.117). lw's field
    generation 1 sha256 24adf31b... equals W9's final-code key-on smoke (b13b, 11.4).
  - `reload_job_4916.py`, 20261122-084447-cfc2: lw and wl, a still per camera, `lt.py save C5H-0301-4906-LT4916R`, the
    harness's safe stop, a new launch on that save, the same stills; the save removed at the end.
- **Outputs (F: only, COMMON 27):** `F:/c5-64bit-work/0615/videos/4916-1-lw-oldpc-headed-close.mp4` (11.0 MB) and
  `4916-2-wl-oldpc-headed-close.mp4` (11.7 MB): H.264, real time, 12 fps, 960 wide, captioned, 5 s before and 10 s after
  the Lua call; run 1's pair in `videos/run1-camera-53-14/`; the pairs `4916-{lw,wl}-live-vs-reload-DIAGNOSTIC.jpg`
  (left live, right after the reload); the fetched jobs in `F:/c5-64bit-work/0615/4916/job-*`. Made by
  `make_videos_4916.py` (4907's `make_mp4`). OpenCV's ffmpeg back end cannot seek in these files (odd height 567:
  "Slice parameters 0, 567 are invalid"); Media Foundation (`cv2.CAP_MSMF`) decodes all frames.
- **The recorder on oldpc used PrintWindow, not BitBlt,** in the first process of every job ("another window over the
  centre", 186 of 186 frames); the pictures are whole. After the relaunch BitBlt worked ('screen').
- **Finding: two live-only looks [L, by eye, same camera and zoom in both phases; cause I].**
  1. **wl:** live, the new grass plot (51,12) is drawn under a murky film of water, with no shoreline; after the
     reload it is grass with the EXE's beach and cliff, and the bay is a closed lake.
  2. **lw:** live, the new water beside the old west shore shows a light, sandy shallow band with a straight edge
     and no rocky coast at the new shoreline; after the reload the new shore has the rocky beach and deep water up to
     it. (A straight diagonal line in the upper right of the lw picture is in both phases: not live-only.)
  - The field matched the accepted runs (the sha256 above), so the difference is in what is drawn over or from it.
    *Corrected 2026-11-22 09:30 by ticket 0667 (11.11): the cause is stale 512 near pairs, not the water. Replaced:
    "Hypothesis [I, not checked]: the water surface's own data (the water tap copy taken after `Waves_Reload`) does
    not follow the live field".*
  - Reported to the MC on thread 0615 (#10157). The defect is ticket 0667 (MC #10168, p8 under 0615: its fix goes into
    the next beta build; the MC adds a known-issue line to `docs/BETA-TESTERS.md`). The owner gets the videos with 0667's
    before/after, not before.

### 11.11 Ticket 0667: the live-only looks are stale 512 near pairs, and the fix (lead-0615, 2026-11-22 09:02-) [C for the code and the unit test, L where marked]

- **11.10's hypothesis does not hold [C, the code; L, 4916's logs].** The ocean is not a tapped mesh: the server's water
  module builds it every frame from the camera (`water-system.md` 10.2) and shades it from its own height pages, and
  those pages follow every live change (`WaterScene::onFieldRegion` -> `c5r::water::Renderer::updateHeightRegion`;
  4916's render.log: "water 2.3 ms (6 pages)" for lw, "water 0.4 ms (2 pages)" for wl). The water tap copy taken again
  after `Waves_Reload` feeds only the rivers, the coast waves and the Water object's fields (`WaterScene::useImage`).
  The field is right too: W8's workstation lw smoke with the same field (sha256 equal, 11.10) looks right live at the
  load's zoom (`F:/c5-64bit-work/0615/4914b/pictures/w8-ridge-4390913/lw_ridge_zoom_DIAGNOSTIC.png`), and offline the
  wl plot (51,12) is raised above the water (W7's `smoke_wl_on` field: median 15,924 u16 against the water's 13,000;
  before the change 9,766).
- **Cause [L, oldpc job `20261122-091640-d16a`, 09:16-09:26 EST; candidate layout 06d9e82 (the tenth lead's copy), save
  b2ceb663..., build 4390913; rc 0, restored, 0 LOUD in render.log, server.log and vpproxy.log, no box].** Job
  `server/evidence/0667/diag_job_0667.py` (written and its question stated in #10182 before the run): 4916's lw and
  wl, stills at zoom 11.0 ("live"), then the camera out by 12 wheel ticks ("far"), a rest of 10 s, and back to 11.0
  ("back"), all in one game. "live" shows both looks of 11.10 again; "back" shows wl as grass land with the cliff shore
  along the new coast and lw as deep water up to a rocky shore, as the reload of 11.10 does. The render.log's `near 1s`
  lines: before and after the changes `held512=32 free512=0` (the 512 pool of 64 textures full, array mode 1) and
  `short` rising by about 60 per second; at "far" `held512=0 free512=64` (the tiles want 256, the 512 pairs went back);
  at "back" `held512=32 free512=0` again, the pairs baked anew from the current field. Stills (DIAGNOSTIC, F: only):
  `F:/c5-64bit-work/0615/0667/job-20261122-091640-d16a/out/still_{live,far,back}_{lw,wl}.jpg`.
- **The mechanism [C, `render/src/bakeresidency/nearcache.cpp` before the fix; the EXE's rule, `terrain-bake-passes.md`
  3.5.1].** A live window raises the generation of every residency tile it reaches (`scanTile` ->
  `NearCaches::onContentChanged`). A tile holding a near pair whose content generation is ahead asks for a new pair
  (request 2), and the up-res pass tries to take a NEW pair of the wanted size before it gives the old one back. In
  array mode there is no fallback to a smaller pool, so with the 512 pool empty (the usual case at a close camera: the
  32 pairs fill it) the take fails every frame and the tile goes on drawing its old pair: the new heights painted with
  the old colours. wl: the raised grass plot drawn with the old sea-floor bake (the "murky film", no shoreline); lw: the
  lowered plots drawn with the old desert and flood-plain bake under the new water (the "sandy band"; its straight
  edge is a 64-sample tile edge between a stale pair and a re-baked tile). It lasts until the camera moves the pair
  away. A camera farther out (256 pairs, a pool with free textures) re-bakes at once, which is why the smokes and I3's
  pictures at the load's zoom showed nothing. The EXE has the same rule; stock never changes a tile's content this way
  during play, except [I] its own road and improvement invalidations (ordinary changes: below).
- **Fix (branch `epic-0615-0667` @e6834ff50; our rule, not the EXE's) [C].** In `NearCaches::frame`, when the take
  fails for a held pair whose content is behind, the pair is baked again into its own textures at its own size (an
  up-res entry with its own pool entries; `NearFrameResult::inPlace`); when it lacks a texture the tile needs now (no
  t8 entry while the tile's bake has one), it is given back and the tile draws its BC1 pair, which the residency bakes
  again from the new content (`NearFrameResult::dropped`). A failed bake of an in-place pair falls back to the BC1 pair
  as any failed up-res. Pools, sort order, budget and every other request stay the EXE's. Unit test
  `near_content_change_with_an_empty_pool_rebakes_in_place` (`render/tests/bakeresidency/residency_test.cpp`): the
  in-place re-bake, the next frame quiet, a failed in-place bake back to BC1, a pair short of its t8 given back;
  `c5r_bakeresidency_test` 42 run, 0 failed, 0 skipped with `C5R_EXE` = the pure_vp copy (build 4390913), workstation,
  2026-11-22 09:20.
- **Workaround until the fix ships [L]:** zooming out and back in draws the changed area right.
- **Ordinary changes are hit too [L, oldpc job `20261122-092625-83d3`, 09:26-09:36 EST, the same candidate layout,
  save and checks; `server/evidence/0667/ordinary_job_0667.py`].** Camera on Honolulu (51,14) at zoom 11.0; through the
  game's Lua: a road at (50,13), a farm at (51,15), the forest at (50,15) removed, the farm pillaged, a still 12 s after
  each. Without the fix no "after" still shows the road and the chopped forest's baked tree shadows stay; after the zoom
  cycle the road is drawn and the shadows are gone. So every change that re-bakes a terrain tile (roads, improvement
  decals, tree shadows, live heights and land/water) kept its old look at a close camera while the 512 pool was full.
  The rule came with issue 0371's port of the EXE's near pairs (`render_bake_near=1`), so every build with near pairs
  had it; [I] stock has the same rule. The MC named it with the zoom workaround in `docs/BETA-TESTERS.md` (3d7eb7cd1).
- **After the fix [L, oldpc, layout `fix1` = the candidate layout with server.exe 452c8279... of `epic-0615-0667`
  @e6834ff50 (0.1b pins, the candidate's fork commit 1b87dd18), each job rc 0, restored, 0 LOUD, no box].**
  - `20261122-093047-688c` (4916's `reload_job_4916.py`): at zoom 11.0 right after the changes wl is grass land with the
    cliff shore and lw deep water up to a rocky shore, as after the reload; the 512 pool full as before (`held512=32
    free512=0`); render-thread apply 43.2 / 10.2 ms (before the fix 41.7 / 10.4 ms: no change in the apply).
  - `20261122-093724-d34d` (`ordinary_job_0667.py`): the road is drawn in its own still, the forest's trees and shadows
    are gone in theirs, no zoom cycle needed.
  - `20261122-093119-e04b` (4916's `video_job_4916.py --per-step-camera --zoom min`): the AFTER videos for the owner,
    `F:/c5-64bit-work/0615/videos/0667-after/4916-{1-lw,2-wl}-oldpc-headed-close.mp4` (PrintWindow frames, 12 fps);
    the BEFORE pair is 4916's `F:/c5-64bit-work/0615/videos/4916-{1-lw,2-wl}-oldpc-headed-close.mp4`.
  - Stills (DIAGNOSTIC, F: only): `F:/c5-64bit-work/0615/0667/job-<id>/out/still_*.jpg`.
- **Rule A (run_all, plain, `epic-0615-0667` @aac3bb5ff, workstation, 2026-11-22 09:44-10:32 EST; asset root
  `mods548-vt`) [L offline].** 11 of 14 suites passed; fow-scene and overlay-scene red were main's (an `[Extras]`
  default the test programs did not link; green on main since 0857e703b). offline-equals-live: 15 passed, 2 failed:
  `C5H-0301-HUGE_0350_20261119-214920` and `C5H-4980-C4PORT_0009_20261120-034655`, colour only (about 1,045 of 3,603,694
  pixels per frame, largest channel difference 70; depth equal). They are the fix itself: the same head with only the
  fix's three files taken back (sceneshot built from it, the same `--dumped --compare --tolerance 1:1`) replays C4PORT
  with 0 differing frames. Both bundles' `near.bin` hold pairs whose content was behind when they were recorded
  (C4PORT 24, huge 36 textures "from recorded texels"): the live server of 2026-11-19/20 drew those tiles from stale
  pairs (the same bug in a plain load: tiles re-baked after their 512 pair was taken), and the fixed player re-bakes
  them. A recording made with the fixed server.exe does not carry them [C since 0670]. The two bundles were recorded
  again by ticket 0670 (2026-11-22 11:15-11:38, server.exe of main b391a7a22): both replay byte-equal at 0:0 with no
  texture from recorded texels; the old ones are in the library's `retired/` (offline-scene-harness.md 38; corrected
  2026-11-22, was: "are to be recorded again (the MC's decision, #10245)").
