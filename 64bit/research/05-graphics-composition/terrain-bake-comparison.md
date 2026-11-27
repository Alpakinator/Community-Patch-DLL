# The bake pipeline in the server against the EXE's own caches, tile by tile (issue 3404, acceptance B)

Written 2026-11-13 from 05:15 EST by W4 of team-3400 (epic 3400, bake stage 1). Marks: `[C]` from a static reading of
the DX11 EXE (the objdump listing `D:/c5-64bit-work/2900/2903/dx11.asm` and the EXE's own `.rdata` strings, read with
scratch scripts in `D:/c5-64bit-work/3400/w4/scripts/`), `[L]` measured on this machine against the EXE's own caches,
`[I]` inferred. Addresses are VAs at ImageBase 0x400000. D83: no decompiler output, EXE bytes or game data in this
repository; the comparator's outputs (reports, difference images) are game-derived and stay on D.

## 1. Answer first

- **gamefs now resolves `TerrainDef.csv` as the EXE does** (section 2): the EXE's "Common" content kind mounts every
  package folder (minus the directories the packages name for the other kinds), so the Shared DLC's loose
  `Assets/DLC/Shared/Terrain/TerrainDef.csv` replaces the older copy in `Resource/Common/TerrainModels.fpk`. The two
  workarounds of 3401/3402 and the duplicate CSV parser are gone.
- **The pipeline** (`render/src/bake/`, `render/include/c5r/bake/pipeline.h`, section 3): `c5r::bake::PipelineBaker`,
  the `TileBaker` of `baker.h`, joins 3401's lists, 3402's vertex fill and blend pass, 3403's post passes, 3405's grid
  normals and slope classes and 3406's height pages, bake constants and tile style (wave 3, 2026-11-13 08:15: the
  EXE's rules are the defaults; the wave-1 stand-ins stay selectable). Edge tiles now use the EXE's (ex+2) x (ey+2)
  grids in `bakepost` too.
- **Acceptance B** (section 5, run 3, the committed code): on the caches that two EXE sessions bake identically, the
  server is **bit-equal for 1,798 of 1,882 (Tiny) and 3,146 of 3,960 (huge)**, 0.991 / 0.952 of the BC1 blocks; over
  every cache the decoded texel-equal share is 0.923 / 0.859 against the EXE's own session-to-session 0.943 / 0.923.
  Every remaining difference has a cause outside the epic (section 6: decals of bake stage 2); none inside was found.

## 2. Which TerrainDef.csv: the EXE's package-folder rule [C, L consequence]

The facts before this issue (asset-pipeline.md 1.2, terrain-bake-records.md 2.3): the EXE's live texture-set tables
equal `Assets/DLC/Shared/Terrain/TerrainDef.csv` [L, 3401/3402], `c5r::gamefs` resolved the bare name to the base pack
`Resource/Common/TerrainModels.fpk:terraindef.csv` (an older table: 12 of 41 scales differ, other slope flags), and
the Shared DLC's `Upgrade1.Civ5Pkg` names no `<Directory>`.

The rule, from the package mounter 0x630E50 (called with kind 0, 1, 2 = "Common", "Gameplay", "UISkin", table
0xBA31B4) [C]:

- For the kind it mounts, it first collects every package's directories of the **other two kinds**
  (`Skin/Directory`, `GameplaySkin/Directory`, `Directory`, `MapDirectory`; strings 0xA39DB0, 0xA39DC0, 0xA39DD8,
  0xA39DE4) into one exclusion list, each formatted with `";%s\*"` (0xA39DF4), after the fixed
  `"*\Sounds\Speech\*;*\Scenarios\*"` (0xA39D90). The list runs over all packages (0x6DB220 collects across packages).
- It then collects the kind's own `Directory` / `MapDirectory` entries (0xA39E0C, 0xA39E18). **When that list is empty
  and the kind is 0 ("Common"; 0x631149..0x631179) it registers one folder with flags 7** through 0x8219F0 (loose
  files first, recursively, then the `.fpk` files in the folder itself), with the exclusion list and the include
  pattern `"*.xml;*.ggxml"` (0xA39DFC; matching files get flags 2). Otherwise it registers each listed directory the
  same way (0x631180..0x6311D1). Then the `File` entries (0xA39E28) with flags 7.
- No package of the install names a Directory for the Common kind, so this branch always runs. The folder it
  registers is built before the branch (0x6310C6..0x6310EF); **that it is the package folder (or the DLC root, which
  gives the same names when every DLC folder holds a package) is [I]**: the operand was not traced to its source.
  Supporting facts [L]: every DLC package lists files in its own folder outside any named directory by bare name
  (`Upgrade1.Civ5Pkg`'s `<GameData>Civ5ArtDefines_SV_Shared_Features.xml</GameData>` lives in `Shared/StrategicView/`;
  `Mongol.Civ5Pkg`'s `CIV5Units_Mongol.xml` in `DLC_01/Gameplay/XML/`), which the EXE could not find otherwise, and the
  live tables equal the Shared loose CSV.

**gamefs** (`GameFiles::Options::mountPackageRoots`, on by default; `render/src/gamefs/gamefiles.cpp`): in the package
layer, after the packages' packs (flags 4) and before their named directories, each package folder (once, also when
several packages share it, like UI_bc1's EUI_0/1/2) is scanned with flags 7, skipping the subtrees of every directory
any package names for Gameplay (`Directory`, `MapDirectory`) or any UISkin (active or not), plus Sounds/Speech and
Scenarios. `MapDirectory` is now parsed (`Civ5Pkg::gameplayMapDirs`) and mounted as a Gameplay directory, as the
evidence of 2901 says the EXE does. The relative order of the Common, Gameplay and UISkin file systems stays [I] (one
package layer). Tests: `render/tests/gamefs/gamefiles_test.cpp` (`gamefiles_package_folder_is_mounted_like_the_shared_dlc`,
`negative_gamefiles_package_folder_exclusions`: a Shared-like package beats the base pack and a package pack; Speech,
Scenarios, an inactive skin's directory, another package's inactive skin directory in a shared folder and a DLC folder
without a package stay out; with the option off the old resolution comes back).

What the rule changes on the install copy `%USERPROFILE%/Desktop/pure_vp` (2026-11-13, `pkgroots.py`, scratch) [L]:
2,832 loose files under 19 package folders become resolvable; 67 of them have a name that a `Resource/DX9` or
`Resource/Common` pack also holds and now resolve to the loose copy: `TerrainDef.csv` (the target), 27 `.gr2` and 26
`.fxsxml`/`.ftsxml` of the Shared trading-post models (Upgrade1's replacements of `ImprovementModels.fpk`), 10 `.wav`
of DLC_02/DLC_07/Expansion, 2 `.xml` of DLC_01, 1 `.xml` of VT. **No `.dds` changes resolution**, so no texture the
server's terrain draws changes.

Call sites: `loadTerrainDef(fs, tables, &origin)` reads through gamefs (3401's loose-first read by path removed; the
old signature with an install root stays for callers and ignores the root); 3402's test reads the CSV through
`loadTerrainDef` and its own `parseTerrainDefNames` is removed, so 3401's `parseTerrainDef` (42 sets with the appended
Checkerboard set) is the only parser. The origin is `Assets/DLC/Shared/Terrain/TerrainDef.csv`.

## 3. The pipeline (`c5r::bake::PipelineBaker`)

Per `bake(BakeRequest)` (one tile, size class 64/128/256/512), as of wave 3 (2026-11-13 08:15, `ae1390f`):

1. `buildLayerList` (3401) and `tileWindow`; t8 when `needsSecondCache` says so with the spec switch 0xD90F28 = 1 and
   tile+0x300 = `BakeRequest::areaChanged` (a listed layer's set has a spec map, or the area changed, 0x4387D0).
2. Blend (3402) on the tile's (ex+2) x (ey+2) grid: slope classes from the `SlopeClassSource`, `fillBlendVertices`,
   `buildBlendIndices`, `fillOwnBlock` (largest cache 512, the page offset of step 3), `BlendTextureCache::bind`
   (layer textures by the CSV's names through gamefs), `BlendPass::run` (draw + GenerateMips).
3. Height page from the `HeightPageSource` (SRV + tile+0xE4/+0xE8).
4. Post (3403) on the same grid: the grid normals from the `GridNormalSource`, the tile style's cubes and LUT
   (`loadStyleInputs`, cached per style), constants from the `BakeConstantsSource`, `BakePost::run` -> t0, t8.
5. When `BakeRequest::t0Target` / `t8Target` name a BC1 array, the result is copied into that slice (all mips,
   `CopySubresourceRegion`) and no texture is returned; `BakedTile::hasT8` says whether a t8 was made;
   `BakedTile::gpuMs` is measured with timestamp queries around steps 2-5 (the read waits for the GPU;
   `PipelineConfig::gpuTiming` switches it off). E_OUTOFMEMORY from D3D11 is reported as `Err::OutOfMemory` (the
   pipeline's own calls and `bakepost`'s).

| Input (owner) | Interface | Default since wave 3 | Stand-in (wave 1, still selectable) |
|---|---|---|---|
| CacheBake grid normals, tile+0x308 (3405) | `GridNormalSource` | `makeExeGridNormals`: 3405's `gridNormals` | `makeApproxGridNormals`: 3403's central differences of the vertex heights |
| slope classes of the blend vertices (3405) | `SlopeClassSource` | `makeExeSlopeClasses`: 3405's `tileSlopeClasses` | `makeFlatSlopeClasses`: all 0 |
| height page tile+0xEC and offset (3406) | `HeightPageSource` | `makeExeHeightPages`: 3406's `buildHeightPage` + `createHeightPageTexture`, one texture per page, kept while the grid is the same | `makeFittedHeightPages`: 3403's fitted rule, one page per tile |
| HalfVector, AuxDir (3406) | `BakeConstantsSource` | `makeStockBakeConstants`: 3406's `stockBakeLight` | `makeCaptureConstants`: the load-screen values of 3023 run 3 |
| terrain style, tile+0x138 | `StyleSource` or `MapInputs` | `MapInputs::plotArtTypes` through 3406's `tileStyle`, else `MapInputs::style` | - |

**Edge tiles** (last column / row, extent 64; wave 3): the EXE draws every pass of such a tile on the (ex+2) x (ey+2)
grid (66 on the short side, 25,350 indices for 66 x 66; uv step 1/65). `bakepost` took a fixed 67 x 67 grid; it now
takes `PostInputs::gridW` / `gridH` (2..67, default 67) and builds the macro-shadow grid, the CacheBake grid and the
indices for that size (`buildGridIndices(gw, gh)`, `buildShadowGrid(..., gw, gh)`, `buildCacheGrid(..., gw, gh)`; the
67 forms unchanged and checked equal; test `edge_tile_grids` and an edge run in `game_init_and_input_validation`). Run 2
had 12 Tiny edge textures with 1-2 LSB differences; run 3 has none (section 6).

API and checks: `render/include/c5r/bake/pipeline.h`. Tests: `render/tests/bake/bake_test.cpp` (15 tests; the wave-3
contract in `game_contract_targets_area_changed_and_plot_style`, the stand-ins in `game_stand_in_sources_bake`).

## 4. The comparator (`render/tools/bakecompare`)

`bakecompare --ref <save dir> --game-dir <install> --exe <EXE> --out <dir> [--arm armB] [--limit N] [--tiles X,Y;...]
[--diff-images N] [--style-probe] [--t8-rule exe|list] [--sources exe|standin] [--plot-art FILE]` (wave 3: `--sources`
picks the EXE's rules (default) or the wave-1 stand-ins; `--plot-art` takes the plots' continent art types, "W H
v,v,v..." row-major as 3406's live probe writes them, and gives the baker `MapInputs::plotArtTypes`; `--t8-rule exe`,
the default, sets `BakeRequest::areaChanged` from the record's tile+0x2FC) reads 3401's reference set (manifest, `<arm>/caches/raw_index.json` with the raw
BC1 files and the tile records, `<arm>/heightfield.c5hf` of the same session, and the EXE-vs-EXE files
`equal_between_sessions.json`, `decoded_<save>.json`), bakes every tile that has a cache at the record's size class,
and compares texel by texel:

- BC1: the share of 8-byte blocks equal, mip 0 and all mips.
- Decoded: both sides decoded with the reference BC1 palette of 3401's `cache_decoded_diff.py` (so the server-vs-EXE
  and the EXE-vs-EXE figures are measured the same way): per channel R, G, B the share equal, median, p95 and max of
  |d|; per texel the largest channel |d| (median, p95, p99, p99.9, max, histogram 0/1/2/3/4-7/8-15/16+), mip 0 and
  all mips.
- Scopes: all compared textures, t0, t8, the **fair set** (caches byte-identical between two EXE sessions), and each
  class of difference; beside each scope the EXE-vs-EXE ceiling over the same textures (mip 0 decoded texel-equal
  share; known for textures present in both EXE sessions).
- Classes, first match wins: equal; decals (the texture differs between the two EXE sessions: bake stage 2's random
  layout, out of scope); layer list differs; t8 flag differs; edge tile; size class not 256; style (with
  `--style-probe`: another of the five styles gives more equal mip-0 blocks); t8 only (t0 equal); then "stand-in
  inputs" while any source is a stand-in, else "remaining".
- Outputs: `bakecompare_<save>.json` (totals, classes, ceiling, skips, the 20 worst), `bakecompare_<save>.csv` (one
  row per texture), `diff/<save>_<x>_<y>_t<cache>.bmp` (server | EXE | |d| x 8, mip 0) for the worst N.
- Exit codes: 0 report written; 2 usage; 3 an input missing or unreadable (also the output folder and the plot-art
  file); 4 unparsable (JSON, tap, TerrainDef, plot art); 5 inconsistent (no texture listed, no material bytes, a tile
  beyond the height field, plot art not matching the grid); 6
  device, shaders or pipeline set-up. Per-tile problems (record fields missing, raw file missing or of the wrong size,
  a bake failure, the server making no t8 where the EXE has one) are counted by reason in the report, never silent.
  Each exit code and each skip reason has a product test (`bakecompare_*` in `c5r_bake_test`).

## 5. Results (acceptance B) [L]

**Setting.** 2026-11-13, this workstation (RTX 4080, the driver of that day), Release builds of the branch
`worktree-agent-ab3fd37fd7f4c3211`, install copy `%USERPROFILE%/Desktop/pure_vp` (CP/VP/EUI), EXE copy
`%USERPROFILE%/tools/ghidra-projects/bin/CivilizationV_DX11.exe` (b2ce96ae..., build 4390913, shaders read from it).
Reference: 3401's set, **arm B** (`i2-shell-test` b92373aeb, old pin, stock config; caches read through the bake tap),
saves `C5H-3401-TINY_0008` (Tiny 56 x 36, turn 8) and `C5H-3401-HUGE_0350` (huge 128 x 80, turn 350), every cached
tile: Tiny 1,307 tiles = 1,307 t0 + 1,245 t8, huge 3,083 tiles = 3,083 t0 + 3,051 t8, all size class 256, 0 skipped.
t8 made where the EXE's record has tile+0x2FC = 1. Decoded = the reference BC1 palette, mip 0 unless "all"; "max-ch" =
per texel the largest channel |d| (8-bit). The ceiling is 3401's EXE-vs-EXE figure over the same textures (two
sessions of the same save, same DLL and machine).
*Asset set (D94, one-off 9049, 2026-11-13):* server side `pure_vp`, EXE side c5h-e: crossed installs, both with the full
Vibrant Textures folder (hash 45b3b433...), so the figures stand; asset-pipeline.md 1.7 has the table and the rule.

**Run 3, the committed pipeline (current figures)** (2026-11-13 08:17 EST, `ae1390f`; grid normals and slope classes
3405, height pages and constants 3406, the tile style from the plots' continent art types of W6's live probe of the
same saves, `D:/c5-64bit-work/3400/w6/live/{tiny,huge}_art.txt` (3406's save copies `C5H-3406-*` of the same
originals; the art types are map data [I: static after map creation]), edge tiles on (ex+2) x (ey+2) grids):

| Save, scope | Textures (bit-equal) | BC1 blocks equal mip 0 / all mips | Decoded texel-equal mip 0 / all | max-ch median / p95 / p99 / max | EXE-vs-EXE ceiling |
|---|---|---|---|---|---|
| Tiny, all | 2,552 (1,869) | 0.9185 / 0.9167 | 0.9234 / 0.9218 | 0 / 28 / 74 / 255 | 0.9429 |
| Tiny, t0 | 1,307 (835) | 0.8474 / 0.8447 | 0.8561 / 0.8535 | 0 / 52 / 84 / 231 | 0.8935 |
| Tiny, t8 | 1,245 (1,034) | 0.9931 / 0.9924 | 0.9941 / 0.9936 | 0 / 0 / 0 / 255 | 0.9949 |
| **Tiny, fair set** | 1,882 (**1,798**) | **0.9907 / 0.9906** | **0.9922 / 0.9921** | 0 / 0 / 0 / 255 | 1 |
| Tiny, fair t0 / t8 | 861 (804) / 1,021 (994) | 0.9814 / 0.9986 (mip 0) | 0.9844 / 0.9988 | 0 / 0 / 14 / 231; 0 / 0 / 0 / 255 | 1 |
| huge, all | 6,134 (3,212) | 0.8494 / 0.8463 | 0.8591 / 0.8564 | 0 / 58 / 113 / 255 | 0.9226 |
| huge, t0 | 3,083 (1,322) | 0.7399 / 0.7368 | 0.7553 / 0.7525 | 0 / 82 / 124 / 244 | 0.8552 |
| huge, t8 | 3,051 (1,890) | 0.9601 / 0.9569 | 0.9640 / 0.9613 | 0 / 0 / 49 / 255 | 0.9907 |
| **huge, fair set** | 3,960 (**3,146**) | **0.9522 / 0.9512** | **0.9571 / 0.9563** | 0 / 0 / 66 / 255 | 1 |
| huge, fair t0 / t8 | 1,752 (1,301) / 2,208 (1,845) | 0.9164 / 0.9805 (mip 0) | 0.9250 / 0.9826 | 0 / 16 / 85 / 244; 0 / 0 / 16 / 255 | 1 |

Per channel (run 3, all textures, mip 0: equal share / p95 / max): Tiny R 0.928 / 24 / 255, G 0.926 / 23 / 247,
B 0.937 / 5 / 255; huge R 0.870 / 46 / 255, G 0.867 / 36 / 247, B 0.871 / 33 / 255. Mip 0 and all mips agree within
0.003 everywhere: the server's `GenerateMips` of the scratch targets matches the EXE's (as 3403 found for one tile).
Run time 50 s for both saves (bake, read-back and comparison of 8,686 caches).

**By terrain style (run 3, fair set):** Generic tiles are equal almost everywhere (Tiny t0 609 / 609, t8 610 / 610;
huge t0 1,075 / 1,090, t8 1,085 / 1,093); the tiles of the four continent styles much less (Tiny America t0 84 / 101,
Asia 111 / 151; huge Africa t0 65 / 191, Europe 158 / 453). A run with the style probe on top (`--style-probe`,
08:20) makes **no** further texture equal (2 huge textures gain a few blocks): the style rule is right, and the
differences of those tiles have another cause, which the pictures name (section 6: rivers, floodplains, forest
shadows, roads: decals on the land the continents carry).

**Earlier runs, for the record** (same reference): run 1 (06:10, `6b5f81e`, the wave-1 stand-ins: approximate
normals, slope classes 0, fitted per-tile page, load-screen constants, Generic style): fair set Tiny 830 / 1,882
bit-equal, 0.6939 of blocks; huge 1,582 / 3,960, 0.6892; no t0 equal at all. Run 2 (06:14, 3405 at `062ec1c` and 3406
at `a5f9f4c` copied in as an experiment, style probed, 67 x 67 post grids): fair set Tiny 1,785 / 1,882, 0.9894;
huge 3,131 / 3,960, 0.9408.

Reports (game-derived, D only): `D:/c5-64bit-work/3400/w4/cmp/wave3a/<save>/` (run 3), `.../cmp/wave3probe/`
(run 3 + probe), `.../cmp/standin2/` (run 1), `.../cmp/wave2exp1/` (run 2): `bakecompare_<save>.json`, `.csv` (with
the style per texture), `diff/*.bmp` (12 worst per save; more for chosen tiles in `.../cmp/look1..3/`).

## 6. Causes of the differences (run 3 classes; counts are textures t0 + t8)

| Class (first match) | Tiny | huge | Cause | Owner (D67) |
|---|---|---|---|---|
| equal (bit for bit, all mips) | 835 + 1,034 | 1,322 + 1,890 | - | - |
| decals: the cache differs between the two EXE sessions | 407 + 180 | 1,310 + 798 | farm, road and blob decals laid out at random per session (3401, records 2.4): bake stage 2 [L pictures; **cause L since 2026-11-15**: `rand()` in the decal producers, terrain-decals.md 3.1, and the same session's decals drawn by the server make these tiles equal, 13.3] | epic 0166 |
| tile+0x300 (area changed since load, 0x4387D0) | 13 + 7 | 186 + 121 | t8 is made as the EXE does (areaChanged); the pictures show decals the server does not draw (cracked ground, dirt patches, improvement grounds) that are the same in both sessions [L pictures; **cause L since 2026-11-15**: improvement grounds and other decals, drawn by the server from the same session's records in 4106, terrain-decals.md 13.3-13.4]. The Tiny rows of tiles (55, 17)-(55, 20) t0 are stage 1's east edge column of the wrapping map (with t8), not decals: as large without decals as with them (terrain-decals.md 13.4) | epic 0166; the edge column stage 1's |
| remaining (none of the classes above) | 52 + 24 | 265 + 241 | 10 tiles looked at: river beds and floodplain vegetation where the server shows the channel's water material, forest-shadow blobs, and pieces of neighbouring decals at the tile border, the same in both sessions [L pictures; **cause L since 2026-11-15**: the river bed is the Riverbank decal, flood plains their decal, the blobs Tree_Shadow decals (terrain-decals.md 2.2, 4.4); with the decals drawn, 39 of 39 river t0 of 9047 4.2 cached in 4106's session are bit-equal and every texture of a decal-free tile is bit-equal, 13.3]. Almost all are on tiles of a continent style (Tiny 76 of 76, huge 482 of 506), where the land with rivers, forests and improvements is; the probe finds no better style for any of them | epic 0166 |

*Corrected 2026-11-15 (issue 4107, from 4106's same-session comparison, terrain-decals.md 13) [L]: the three decal classes
above were [I] from pictures; they are now measured. With the server drawing the decals of the same EXE session (4106:
one stock session per save, the decal tap's image, `bakecompare --decals`, small decals newest first): Tiny 2,543 of 2,568
textures bit-equal (t0 1,295 / 1,318, t8 1,248 / 1,250; without decals 1,891), huge 5,330 of 6,136 (t0 2,439 / 3,085,
t8 2,891 / 3,051; without decals 3,217); every texture of a tile without decals bit-equal on both saves. What still
differs has its cause named in terrain-decals.md 13.4: the order of equal-priority decals (Tiny 15, huge 725 textures),
the stone quarry ground (huge 59), stale EXE caches (huge 18), stage 1's east edge column (Tiny 8), decal slivers at a
tile edge (5) and one farm tile. 4106's reference is another session than run 3's (the same saves), so these are
4106's counts, not run 3's rows re-labelled. Replaced: "[L pictures, I cause]" in the three rows and "bake stage 2,
outside epic 3400" as their owner.*
| t8 only (t0 equal) | 0 | 0 + 1 | one t8 of tile (110, 30) with 0.9993 of its blocks equal | open (one texture) |
| layer list, t8 flag, edge tile, size class, style | 0 | 0 | lists equal on every tile (acceptance A); t8 from the record; edge tiles fixed in wave 3 (none left; run 2 had 11 + 1); all size class 256; the style rule leaves nothing to the probe | - |

**Inside the epic, no cause is left** that the comparator can see: every different cache is either different between
two EXE sessions, or a tile whose picture shows decals, or (one t8) within 0.07 % of its blocks. ~~The [I] on the
decal causes is settled only by drawing decals (stage 2) or by a reference made with the decal pass switched off.~~
*Done 2026-11-15 (epic 0166, 4106): settled by drawing the decals of the same session; the causes are [L] (the note
under the table, terrain-decals.md 13).*

## 7. Open points

- *2026-11-15 (MC's drafter, from the code on main; ticket 0341):* the live server bakes every tile with the Generic style [C]: `srv_residency_source.cpp` `onField` passes no `plotArtTypes`, so continent art types never reach the bake. The Shadowlight files for America, Asia and Europe differ from Generic's, so on huge about 3,221 of 10,752 tiles get the wrong cube [I]; how much shows in the picture is not measured. 0341 (priority 5) feeds the art types.

- The folder 0x630E50 registers for the Common kind (package folder or DLC root) is [I] (section 2).
- ~~Edge tiles: 66 x 66 grids in `bakepost`~~ Done in wave 3 (2026-11-13 08:15, section 3): no edge difference left.
- tile+0x300 [C, 2026-11-13 06:25, dx11.asm]: written 1 by **0x4387D0** (this = TR), which converts a world-space
  rectangle to tile columns and rows (TR+0x1AC4/+0x1AC8 steps, TR+0x2310/+0x2314 tile counts) and, for every tile in
  it, sets **tile+0x1CC = the frame counter `[[0x3010570] + 0x33F74]`** (the content generation of records 2.1, whose
  writer was open), **tile+0x1F8 = 0** (the layer count, so the list is rebuilt) and **tile+0x300 = 1**, then calls
  0x657CD0 with TR+0x1AD8 (the tile queue [I]). Callers 0x49B925, 0x67915E, 0x68F233, 0x6B5C65 are not traced [I: the
  places that add or change decals, roads and improvements over an area]. So a tile with tile+0x300 = 1 has had an
  area change since load and always gets t8 (decal spec), which fits the pictures. For issue 3407 (invalidation):
  0x4387D0 is the EXE's "area changed" entry; the contract carries it as `BakeRequest::areaChanged` (wave 3). With the
  list rule alone the server makes no t8 for 182 (Tiny) / 827 (huge) of the EXE's t8 caches, and their t0 come from
  CacheBake pass 0 instead of pass 1.
  *Corrected 2026-11-15 (issue 4107, from 4106, terrain-decals.md 13.5) [L]: which tiles carry +0x300 = 1 at load is
  set by the decals: every non-river decal class (routes, farm fields and tracks, city and model grounds, tree shadows)
  sets it on every tile its square floor(min / 64) .. floor(max / 64) covers (0 exceptions on both saves), river and
  flood-plain decals only on some of theirs (Tiny 99 of 298 and huge 87 of 1,995 river tiles keep 0), so the final decal
  records do not determine the bit; 4111 reads the EXE's own bit. The "CacheBake pass 0 instead of pass 1" sentence does
  not hold for the 56 tiles where the server's decal rule makes a t8 and the EXE does not: their t0 are bit-equal in
  both runs (13.5), so a t8 decided differently changed no t0 there. Replaced: nothing; the sentence above stays as the
  wave-3 statement for the list rule without decals.*
- Style: settled in wave 3 for the comparator with 3406's live dump of the plots' continent art types; the server
  must pass `MapInputs::plotArtTypes` from the DLL's plots (3406: the EXE's style for 10,748 of 10,752 huge tiles;
  the 4 others list a fifth plot, not traced). *Corrected 2026-11-16 (0383): with the fifth plot traced and x not
  wrapped, `tileStyle` gives the EXE's style for all 10,752 huge and all 2,240 Tiny tiles (`terrain-bake-passes.md`
  5.3).*
- ~~The decal causes of section 6 are [I] from 10 pictures; a decal source (stage 2), or a reference baked with the
  decal pass off, would settle them by drawing.~~ *Done 2026-11-15 (epic 0166, 4106): settled by drawing; section 6's
  note and terrain-decals.md 13.*
- `BakedTile::gpuMs` waits for the GPU after every bake (three `GetData` loops); a residency manager that bakes many
  tiles per frame may want to read the queries a frame later instead (not measured; no timing claim here).

## 8. The server's live picture against stock at the same cameras (issue 3408, D66, 2026-11-13) [L]

Written by team-3400 W8. The camera script of terrain-bake-residency.md 6.1 (huge: start, 4 pans, 24 jumps), one
screenshot per camera in each arm (2554 x 1411, PrintWindow):
- **stock**: current pin, stock configuration, 08:17-08:20 EST (terrain-bake-residency.md 6.2);
- **server**: D33 on the proxy route, `shell_server.exe --scene terrain --bake pipeline --bake-radius -1` (this branch with
  W7's f6dd9a9 merged; **the PipelineBaker of wave 2 with the stand-ins inside it that sections 3 and 5 list: not yet
  W4's exact-input defaults**), 08:55-09:00 EST, each camera after the bake-ahead (every tile cached except the 4
  Gibraltar tiles); per camera also a passthrough shot (the composite's mode switched by its ini, same camera) and the
  plot list within 12 plots (`bake_live.py --composite-ini`).

*Asset set (D94, 9049):* both arms on c5h-b's install (the server's `--game-dir %USERPROFILE%/c5-instances/c5h-b/install`,
`D:/c5-64bit-work/3400/w8/server_series.sh`), Vibrant Textures hash 45b3b433... (asset-pipeline.md 1.7).

Tool: `shell/evidence/3408/compare_3408.py` -> `shell/tools/terrain_compare.py` per step with epic 3200's mask (the
passthrough's clear pixels minus 16 px around models and UI, minus water, unrevealed, route, farm and forest plots of the
list, minus the magenta of uncached tiles), `--geometry none`. Same camera in both arms: largest difference of the
projection and view matrices 0.001-0.081 (world units; the default-zoom camera is about 1,000 units away). Per-step
figures (largest channel difference per pixel, 0-255): `shell/evidence/3408/compare_pipeline_huge_r-1.csv`; difference
images `D:/c5-64bit-work/3400/w8/cmp_pipeA_HUGE/evidence/step_NN_colourdiff_512.png` (game-derived, not committed).

| Huge, 29 cameras | Median of the largest channel difference | 95th percentile | Maximum | Share within 16 on all channels |
|---|---|---|---|---|
| per camera, range (median camera) | 9-84 (48) | 66-133 (98) | 150-230 (206) | 0.02 %-75.6 % (8.0 %) |
| all 13.3 M mask pixels pooled (16-wide bins) | **48-63** | **96-111** | 230 | 19.2 % |

Mean absolute difference per channel (median over cameras): R 43.6, G 33.8, B 18.9. Decals: every camera's plot list
held 33-203 route plots and 45-238 improvement plots; those plots are outside the mask (reported, not judged; bake stage
2). Step 18 (the Gibraltar tiles uncached in the server): 94.5 % of its mask cached.

**What the difference is made of [L for the observation, I for the shares]:**
- **Fog of war**: stock darkens revealed plots that are not visible (FOW), D33 switches FOW off and the server draws
  none; on this turn-350 save large parts of most views are fogged (pair pictures, e.g. step 7, top left). With
  terrain_compare's diagnostic `--exclude-fogged` (fogged plots also removed; run 10:50-10:53 EST,
  `compare_pipeA_huge_r-1_nofog.csv`): 12 of the 29 cameras keep no pixel (their whole view is fogged land, water or
  models), the other 17 give

  | Huge, fog excluded, 17 cameras | Median | 95th percentile | Maximum | Within 16 on all channels |
  |---|---|---|---|---|
  | per camera, range (median camera) | 9-66 (18) | 66-143 (87) | 150-230 (199) | 11.9 %-75.6 % (48.0 %) |
  | all 4.86 M pixels pooled (16-wide bins) | **16-31** | **80-95** | 230 | 49.0 % |

  and the per-channel mean differences fall to R 23.4, G 19.4, B 18.6: **most of the median difference and the red
  bias of the full mask is the fog of war** [L], not the bake.
- **The stand-ins inside the pipeline** (grid normals, height page, style, bake constants: sections 3 and 5): what
  remains after the fog (median 16-31) includes them. One camera with W4's exact-input defaults (pipeB, d597edf: the
  start view at the capital, 10:45 EST; the rest of that run was lost to an EXE crash, terrain-bake-residency.md 6.6)
  gave the same figures as the wave-2 pipeline at that camera (median 16, p95 76, within 16: 50.8 % against 50.3 %)
  [L, one camera].
- Borders, the hex grid and units' shadows on the terrain are drawn by the EXE in stock and not by the server (D33).
So this is **not yet a measure of the bake**: section 5 (tile against cache, same inputs) is that measure; this figure is
the live end-to-end picture, to be repeated with W4's exact inputs and with fog excluded.

**Tiny: not comparable.** At turn 9 nearly every camera of the Tiny script looks at unrevealed land (black in stock, the
script had to use any land plot, terrain-bake-residency.md 6.1), and the plot outlines of terrain_compare come from the
ground-truth folder `D:/c5-64bit-work/i2-ground-truth/tiny`, which is another Tiny map: all 469 listed plots were
"unrevealed" yet 2.2 M pixels stayed in the mask (median 132). The Tiny figures (`compare_pipeline_tiny_r-1.csv`) are
kept as a record of that, not as a result; a Tiny comparison needs this save's own grid constants and height map and a
later save with revealed land.

## 9. A texture name the install lacks: Generic_Gibraltar_H.dds (2026-11-13, 11:09-11:20 EST)

Reported by 3407 (offline) and 3408 (live): the pipeline failed the four huge tiles (111..112, 43..44) with
`NotFound: Generic_Gibraltar_H.dds not in the install`; stock bakes them.

- **The name.** `Assets/DLC/Shared/Terrain/TerrainDef.csv` row 8 (Rock of Gibraltar, slope-projected, layers 10-12):
  diffuse `Generic_Gibraltar_D.dds`, heightmap `Generic_Gibraltar_H.dds`, specmap `Euro_Mountain_Base_S.dds`. A sweep of
  the install copy and the MODS folder (every loose file and every FPK entry, `findname.py`, scratch) finds the diffuse
  (`Resource/DX9/TerrainTextures.fpk` and the Vibrant Textures copy `Assets/DLC/VT/Terrain/generic_gibraltar_d.dds`) but
  **no file named `Generic_Gibraltar_H.dds` anywhere**, in no other extension or folder either [L].
- **What the EXE holds for it** [L, 3401's probe of the huge save, arm B, `layers.json` `tex_908`]: the height-texture
  handle of set 8, TR8+0x908[8], is **0**; every other set has a non-zero handle there. So the CSV loader 0x61BAE0
  leaves handle 0 when the name does not resolve; it does not substitute the Checkerboard set or another file.
- **What the bake does with handle 0** [C, dx11.asm]: the blend's draw object (the loop at 0x69553A..0x6955C4) copies
  TR8+0x708[k] (diffuse) and TR8+0x908[k] (height) of each listed layer's set unchanged into its 16-bit texture slots;
  only the spec slot is checked (TR8+0x808[k] = 0 -> the dummy TR8+0xB08). The binder (0x8BD1F0) turns each slot's
  handle h into the view at `[0x414C348 + 12 * h]` with no check for 0. That entry 0 of the view table holds no view
  (so D3D11 samples 0, exactly like the all-zero 32 x 32 dummy) is **[I]**: the allocator of handles was not traced, and
  the reference set cannot check it, because none of the four tiles was in the EXE's cache pool when 3401 copied it
  (no reference cache of a Gibraltar tile exists). A live capture of one of those tiles (3408's tools) would settle it.
- **The server's rule** (`BlendTextureCache`, `render/src/bakeblend/blendpass.cpp`, `blend.h`): a name gamefs does not
  have binds the dummy texture (the EXE's handle 0) and is recorded in `missing()` (`PipelineBaker::missingTextures()`);
  `MissingTexture::Fail` keeps the old error. A file that exists but does not parse or create stays an error. In the
  blend shader the height texture feeds only the height target (o1), so the Gibraltar layers blend with height 0.
- **Checks** (`render/tests`, 2026-11-13 11:17): `game_every_layer_of_every_set_binds` binds every layer of the install's
  48 layers / 42 sets: all bind, and the names bound as handle 0 are exactly those gamefs lacks: **only
  `generic_gibraltar_h.dds`** [L]; with `Fail` the same sweep fails with NotFound. `game_gibraltar_tile_bakes` bakes a
  synthetic tile of material 8. `game_negative_missing_texture_name` (bakeblend): the EXE rule, the Fail rule, and a
  non-DDS file that must still fail.

**`BakedTile::gpuMs`** (asked by the lead for 3407): wave 2's baker returned 0; since wave 3 it is filled from timestamp
queries. `PipelineConfig::gpuTiming` = `Blocking` (default: this bake's time; the CPU waits at the end of every bake
until the GPU has finished it), `Deferred` (never waits: the context is flushed and the newest finished earlier bake's
time is returned from a ring of 16 query sets, 0 until one has finished) or `Off`. Observation, not a timing claim
(PERFORMANCE.md): in `game_gpu_timing_modes` (synthetic 4 x 4 map, RTX 4080) the GPU time of one bake was 0.18-0.20 ms
and 23 of 24 deferred bakes reported a time. Whether the blocking wait explains the ~7.7 ms of CPU per tile 3407 saw
was not measured; with a GPU time this short the wait is unlikely to be most of it [I] (the vertex fill, the normals and
first-use texture and page creation are CPU work). A residency that bakes several tiles per frame should use
`Deferred`.
