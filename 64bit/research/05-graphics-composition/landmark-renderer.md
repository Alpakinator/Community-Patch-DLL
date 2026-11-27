# The model library of the BuildingShader models: inputs, buffer layout, keys, residency, capture checks (4872)

Written 2026-11-18 16:51-18:20 EST by worker-4872 (account B, Opus 5.5), ticket 4872 of epic 0561 (package I3-D), from
the plan `city-renderer.md` 2 (C1, MC #5962). Code: `render/include/c5r/landmarks/library.h`,
`render/src/landmarks/{layout,library}.cpp`, `render/include/c5r/cities/catalogue.h`, `render/src/cities/catalogue.cpp`;
tests `render/tests/landmarks/`, `render/tests/cities/`; tools (not tests) `c5r_gr2_facts` (one file's models, meshes,
materials) and `c5r_landmark_report` (the library's counts and the byte check of captured draws), both in
`render/tests/landmarks/`. No game was run. Nothing of the game is committed (D83): the install, the database copy and
the capture buffers are read at run time.

Marks: `[C]` checked by this ticket's own code against the data named (the report tool or the tests); `[4870 #n]` a
fact from worker-4870's notes on thread 0561, entry n, with 4870's mark, not yet re-checked here (MC #6035, K5);
`[I]` inferred.

**Inputs of every measurement here.** The install copy the render tests read (`render_game_dir`, a pure-VP install
with VP 5.4.7 as DLC and Vibrant Textures; its file system rebuilt by `c5r::gamefs`); the database copy
`Civ5DebugDatabase.db` of instance c5h-c (CP + VP + EUI, SHA-256 `27fc81c6466cc4a1...`, the copy 0319 used, the units
tests' `F:/c5-64bit-work/0327/db/`, copied to `C:/c5-64bit-work/0561/4872/db/`); the 3910 catalogue's exports (c5h-d,
32-bit DLL `i0ship-26234371-ia`, CP/VP/EUI, Vibrant Textures; scene-catalogue.md 11), of which the draws named in 4.1
were copied once to `C:/c5-64bit-work/0561/4872/samples/` (F: was at its I/O limit, lead note MC #5993). *(Corrected
2026-11-20, ticket 0621: that folder went with epic 0561's trees in the disk clean-up of 2026-11-20; the samples now live
in the kept folder `C:/c5-64bit-work/keep/landmark-oracle-samples/`, see 4.3. The database copy under
`C:/c5-64bit-work/0561/4872/db/` went too; the tests' default is the F: copy.)*

## 1. What the library holds

- **`c5r::cities::ArtCatalogue`** (`load(GameFiles, TableSource)`): the city-set files (`civ5artdefines_citybuildings.xml`
  first, then every other `civ5artdefines_citybuildings*.xml` by name; a later culture and slot replace an earlier one
  [I]), the seven culture names, positional era slots and the lighting rule (cities-and-landmarks.md 1.2); art style to
  culture and era to slot (1.10, `cultureFromArtStyle`, `eraSlots`: CityArtType, else ArtPrefix, else the era before);
  the wall and Great Wall file tables (1.7); the wonder art of `c5m::kWonderArtFiles` through the model reader's
  `ArtTables::load_wonders` (the last entry of a Type in force); the buildings with `DisplayPosition > 0` and their
  art forms (2.3).
- **`c5r::landmarks::ModelLibrary`** (`load(GameFiles, TableSource, ArtCatalogue)`): one `Model` per thing the EXE
  draws by name (an `ArtDefine_Landmarks` model at its row's scale, a wonder art model at its `fScale`, a wall file,
  a Great Wall file, a city-set file), one `ModelFile` per (.gr2, scale) with the EXE's buffer layout (section 2), one
  `Part` per draw (a tri group of a packed mesh) with its range, material, texture names and `MeshKey`; the
  `ArtDefine_Landmarks` rows kept as data for 4881 (handler enum, TerrainContour byte, era, state, tech).
  `find(key)`, `candidates(key)` (every part that holds a key), `keys(class)` (the sorted identity set per class,
  city-renderer.md 3.2), `resident(file)` (the buffers, made on first use).
- Class: city-set files `CityBlock`; wonder, wall and Great Wall files `CityPiece`; landmark rows `Landmark`; a file's
  class is that of the first source that names it.

## 2. The buffer layout per model file

### 2.1 The three vertex forms [C, input layouts of every BuildingShader draw in 3910 W01 and 3907's four city exports, 2026-11-18 16:55]

| Draws | Slot 0 | Slot 1 |
|---|---|---|
| City blocks (PS `c704c80b`, `73daa800`) | 32 B: POSITION R32G32B32, NORMAL R32G32B32, TEXCOORD0 R32G32 | 64 B per instance |
| Every other single model (VS `7ec4e9a5` + PS `f0e1f0a6`, VS `c4bb25da`, shadow `88c2b7d4` / `e12e9eaf`, `0a3f2230`) | 20 B: POSITION R16G16B16A16_FLOAT (w 0), NORMAL R16G16B16A16_FLOAT (w 0), TEXCOORD0 R16G16_FLOAT | 64 B (instanced) or none (`c4bb25da`, `0a3f2230`: world matrix in cb0) |
| Skinned parts (VS `7a9e17f6`, `34b98e63`) | 28 B: POSITION half4, BLENDWEIGHT R8G8B8A8_UNORM, BLENDINDICES R8G8B8A8_UINT, NORMAL half4, TEXCOORD0 half2 | 4 B, R32_UINT (BLENDINDICES1) |

So the city-renderer.md 2.1 statement "the same 32-byte vertex" held for blocks only (corrected there by the lead,
aae463078). The 20- and 28-byte forms are the units' two streams: the library calls the model reader's
`c5m::repack_unit_mesh` with `c5m::unit_basis(file, S)` (unit_mesh.h: Position x S, Normal through the inverse,
Granny's half conversion with ties away from zero) and copies nothing of it; the 32-byte form is the same basis at
scale 1 in float32 without halves. S is the row's `Scale` for landmarks, the `fScale` of the wonder art entry, 1 for
city sets; byte-equal results in 4.1 at S = 0.05, 0.07, 0.08, 0.12, 0.94 and 1 [C]. (4870 #6025 describes the scale as
"x S and / S in double, then to float32"; the model reader's float rule gave the same bytes on every sample here.)
The units' `pathmodels::repackMesh` was not usable: it refuses materials that are not UIShader and meshes of more than
one tri group.

### 2.2 Which meshes, in which order, in which buffer [C for the samples of 4.1]

- **One index buffer per (.gr2, scale)**: one leading element no draw reads (its value differs between captures:
  28864 and 3225 seen; 4870 #6015: it varies between loads), then the indices of every packed mesh as stored (16-bit
  values for Indices16 files), each mesh's own indices (the draw adds BaseVertex). Packed: a mesh whose every tri group's
  material has a shader set beginning `BuildingShader` (`BuildingShader`, `BuildingShader_Skinned`). Not packed: the
  `LandmarkShader` ground planes and decals (`CityGrdTexPlaneNN`, `HB decal`) and the `LandmarkShader_Stencil` tree
  cuts; they are drawn by other programs and listed in `FileLayout::unpacked`.
- **Order: the models' mesh bindings**, models in file order, each model's `MeshBindings` in turn, a mesh taken once
  (`meshOrder`). In fish.gr2 model 0 binds mesh 1 (`Model_Seagull_02`) and model 1 mesh 0, so the file's mesh order put
  the two seagulls the wrong way round and its whole-buffer hash missed; under the model order the fish of W01 and the
  industrial fish of Lanc07 are byte-equal [C, 2026-11-18 17:45; found the same hour by 4870, #6051]. In every other
  sampled file the two orders agree.
- **Two vertex buffers**: the rigid meshes' (32 B in a city set, else 20 B) and the skinned meshes' (28 B, the
  `BuildingShader_Skinned` meshes), each mesh's BaseVertex counted within its own buffer (resource_deer: the camp's rigid
  meshes and the three deer in different buffers; draw 1008's BaseVertex 979 is the first deer's vertex count) [C;
  4870 #6025].
- A draw: `StartIndexLocation` = 1 + the packed indices before its tri group, `IndexCountPerInstance` = 3 x the group's
  triangles, `BaseVertexLocation` = the vertices before its mesh in its buffer [C].
- R32 index buffers: the library uses 4-byte indices when a packed mesh stores 32-bit indices (38 files, all VP mod
  files, e.g. `siheyuan_neighborhood*.gr2`, `schloss*.gr2`) and keeps the leading element there too [I: no capture of
  such a file was looked at]. The three R32 buffers seen in W01 have **no** leading element and match no file of the
  library: draws 70 and 469 (block programs `88c2b7d4`/`73daa800`, `7ec4e9a5`/`c704c80b`; 32-byte vertices, 3,048
  vertices, 6,264 indices) and 617 (`c4bb25da`; 20-byte, 54,876 vertices, 91,728 indices) [C]. W01 holds the Great
  Wall, whose building has no wonder art (`BUILDING_GREAT_WALL`, the one display building without art, 2.3) [C].
  4870 #6040: both are CPU-built meshes of that frame's Great Wall (70/469 in world space around the wall's city, 617
  in a local box of about 7 x 6 plots), the only R32 BuildingShader draws in the 83 exports; no file holds them, so no
  key of this library can name them; their run-time geometry copy is child 4879 (lead, #6055).
- `schloss_hb.gr2` (VP) has a mesh of more than 65,536 vertices; the model reader's unit repack refuses it, so the file
  is refused (3.2) and its draws stay the EXE's [C for the refusal; what the EXE uploads for it is not seen].

### 2.3 Textures and the forms of a wonder

- Per part the material's maps by usage and lower-case name, and `t0` / `t1` = the files the material's
  `BuildingTextures` parameter set names as `Diffuse` / `BuildingSREF`, else its `Diffuse` / `BuildingSREF` map
  [C, 4873: the captures' bound textures by identity, section 7.4]. *(Corrected 2026-11-18 by worker-4873. Was: "t0 =
  the Diffuse map, t1 = the BuildingSREF map ... the parameter sets were not read here (open for 4873)": VP's
  `resource_tea.gr2` keeps cotton's maps and names `tea_diff.dds` in its parameter set, and the EXE binds the latter.)*
- Wonder forms by name [C for the names in the install's wonder art files; I for the EXE's rule]: cultural forms
  `<culture adjective> <tag>` (AMERICAN, ASIAN, AFRICAN, EUROPEAN, MEDITERRANEAN, POLYNESIAN: CASTLE, PALACE,
  COLESSEUM; `ArtInfoCulturalVariation` on BUILDING_CASTLE, PALACE, COLOSSEUM, PITZ_COURT), era forms `<era> <tag>`
  (ANCIENT, MEDIEVAL, INDUSTRIAL, MODERN HARBOR; ANCIENT, INDUSTRIAL LIGHTHOUSE; `ArtInfoEraVariation` on HARBOR,
  LIGHTHOUSE, RUNESTONE), random forms `<tag> <ONE|TWO>` (STADIUM; `ArtInfoRandomVariation`). `cities::wonderForms`
  groups the art types so.

## 3. Counts, refusals, keys, residency (2026-11-18 17:55, the test suite's run) [C]

### 3.1 Counts against the knowledge base

| What | This library | Knowledge base |
|---|---|---|
| `ArtDefine_Landmarks` rows / with a model | 667 / 659 | 667 / 659 (landmarks-and-models.md 1.2) |
| distinct landmark models | 508 `.fxsxml` base names | 514 distinct Model values; 508 base names (2, "all 508 distinct model file names") |
| landmark models placed in their files | 627 models in all (a model per name and scale) | |
| wonder art types / entries / models | 76 / 85 / 74 `.fxsxml` | 76 (model-reader.md 3) |
| world wonders / display buildings / display world wonders | 76 / 58 / 46 | 76, 46 drawn (scene-catalogue.md 11); 58 (cities-and-landmarks.md 1.6) |
| display buildings without wonder art | 1 (`BUILDING_GREAT_WALL`) | |
| wonder types that are a display building's form | 74 of 76 | |
| city-set files read / culture x slot entries / distinct set models | 2 / 28 / 25 | 25 entries (24 sets plus Ruins, 1.2) |
| wall files + Great Wall files | 7 + 2 | 7 + 2 (1.7) |
| model files (.gr2 x scale) / laid out / refused | 624 / 621 / 3 | |
| parts (draws) / distinct keys | 6,664 / 4,448 | |
| keys held by more than one file / of those with different textures | 1,027 / 200 | |
| keys per class: CityBlock / CityPiece / Landmark | 416 / 161 / 3,871 | |
| vertex forms of the laid-out files' rigid buffers: Float32 / Half20 | 25 / 596 | |

"Distinct landmark models": the knowledge base's 514 counts distinct `Model` strings (paths), which resolve to 508 base
names (landmarks-and-models.md 2); the library keys models by base name. The acceptance line's "514 models" is the
path count.

### 3.2 Refused files (each draw of theirs stays the EXE's, city-renderer.md 3.2)

- `oil1.gr2`, `oil_heavy2.gr2`: no `BuildingShader*` mesh (what the EXE draws for the oil rows is another program [I]).
- `schloss_hb.gr2`: a mesh of more than 65,536 vertices (2.2).

### 3.3 Shared keys

A key (index buffer after the pad, the draw's vertex buffer, the range) can be held by several files that differ only
in their textures: `euro_anc_city.gr2` and `medit_anc_city.gr2` (equal index and vertex buffers, different `t0`/`t1`)
[C; 4870 #6015], the four `pl_med_mine_*_heavy.gr2` (aluminium, coal, iron, uranium), cotton / tea / perfume [C]. Of
the 1,027 shared keys 200 have parts with different texture names: the tap (4874) must carry the bound textures'
identity for those, and the library gives every candidate (`candidates`) with its `t0`/`t1`. In the other 827 every candidate names the same
textures, so any of them draws the same picture [I: the texture parameter sets were not read, 2.3].

### 3.4 Load time and memory (a workstation indication, COMMON 23; not a timing claim)

- `load` of the whole catalogue: 6.6 s on the workstation with games running (every file read, decoded, laid out and
  repacked once to compute its keys, then dropped); the catalogue keeps about 4.4 MiB (names, parts, keys; an estimate,
  `catalogueBytes`).
- Every file resident: 621 files, 51.3 MiB (rigid and skinned vertices and indices), 5.7 s more.
- Residency is per file on first use (`resident`), counted in `residentBytes`; `evictAll` drops it. GPU buffers and
  textures are 4873's.

## 4. The checks against the captures

### 4.1 Samples and result [C, test_oracle.cpp, 2026-11-18 17:55]

The key of the captured buffers is computed from the capture (`meshKey` over the exported IB and VB of the draw) and
looked up; the expected file must be among the candidates, and its resident buffers must equal the captured ones byte
for byte (the whole index buffer after the pad element, the whole vertex buffer the draw binds).

| Sample (3910 export, draw) | File, scale | Candidates | Bytes |
|---|---|---|---|
| W01 443 (block colour pass; 44 is its shadow pass) | `euro_anc_city.gr2`, 1 | 2 (euro, medit) | equal |
| W01 156 (wonder, `c4bb25da`) | `colossus.gr2`, 0.05 | 1 | equal |
| W01 528 | `resource_cotton.gr2`, 0.94 | 3 | equal |
| W01 99 (shadow pass) | `ruins4.gr2`, 1 | 1 | equal |
| W01 158 (skinned) | `fish.gr2`, 0.07 | 1 | equal |
| Fanc01 969, 975, 986 (object 9, PL_MED iron heavy mine) | `pl_med_mine_iron_heavy.gr2`, 1 | 4 | equal |
| Lanc07 1007 (skinned, deer) | `resource_deer.gr2` (Expansion2Models copy), 0.12 | 1 | equal |
| Lanc07 1134 (skinned) | `superhorse.gr2`, 0.08 | 1 | equal |
| Lanc07 765 (skinned) | `fish_industrial.gr2`, 0.07 | 1 | equal |

11 of 11 byte-equal. The samples overlap 4870's (#6002: a block set, Fanc01 object 9, W01 156, Lanc07 object 12).
Not compared here: 4870's other block sets (afri, poly) and Sanc01 (4870 #6015, #6051).

### 4.2 Which copy of a file

`resource_deer.gr2` exists in MiscData (182,620 B), Expansion1Models and Expansion2Models (180,556 B each); the capture
equals the Expansion2Models copy [C here; 4870 #6025]. `c5r::gamefs::GameFiles` already resolves the name to that copy
by the EXE's mount order (asset-pipeline.md 1.2: the package layer, DLC packs by Priority, searched before the base
packs), so the library needs no precedence code of its own. fish.gr2 and fish_industrial.gr2 exist only in MiscData.

### 4.3 How to repeat

    python -I render/build.py --test -R "c5r_(landmarks|cities)_"     (C5R_LANDMARK_DB = the database copy,
                                                                       C5R_LANDMARK_SAMPLES = the copied samples)
    c5r_landmark_report [--files] [--residency] <ib>,<2|4>,<vb>,<stride>,<first>,<count>,<base> ...

**Where the samples are (ticket 0621, 2026-11-20).** The default of `C5R_LANDMARK_SAMPLES`
(`render/tests/landmarks/landmarks_testkit.h`) is the kept folder `C:/c5-64bit-work/keep/landmark-oracle-samples/`
(COMMON's disk rule: a clean-up never removes `C:/c5-64bit-work/keep/`); the variable still overrides. The folder holds,
per export, `draws/<nnnn>_draw/state.json` and every `buffers/*.bin` that state names (the IB and VB the oracle reads,
plus the draw's constant buffers): 68 files, 5,379,224 bytes, with `SHA256SUMS.txt` (SHA-256, path, size, original per
file; the list's own SHA-256 `36439bc3018ca0fd...43d05741`), a `README.txt` naming the test and the originals, and
`copy_samples.py` that re-creates it (`python -I copy_samples.py <destination>`, copy only). The originals, all found
2026-11-20 under `F:/c5-64bit-work/0301/3910/exports/`: `wonders/W01` draws 44, 70, 99, 156, 158, 443, 469, 528, 617;
`landmarks_land/Fanc01` 969, 975, 986; `landmarks_land/Lanc07` 765, 1003, 1007, 1134; `landmarks_land/Lanc18` 799 and
`landmarks_land/Lanc20` 722 (4878's two R16 samples, city-renderer.md 12.4; they are 3910 exports too). A master copy with
the same hashes is `F:/c5-64bit-work/0301/3910/landmark-oracle-samples/`, beside the originals. D83: the samples are
captures of the running game and are never committed.
*(Was: copied from W01, Fanc01 and Lanc07 by the scratch script `C:/c5-64bit-work/0561/4872/scripts/copy_samples.py` to
`C:/c5-64bit-work/0561/4872/samples/`, with 4878's Lanc18/Lanc20 added there 2026-11-19 01:55; the clean-up of
2026-11-20 15:26 removed it and `c5r_landmarks_test_oracle` skipped, which build.py counts as a failure. Restored there
17:41 EST (pass), then moved to the kept folder; `c5r_landmarks` passes from the new default, see ticket 0621's log.)*

## 5. Facts for the next tickets

- The FNV-1a 64 of the key uses the standard offset basis 14695981039346656037; `c5m::fnv1a64` (unit_mesh.h) uses
  1469598103934665603 (a digit short), so its values are not standard FNV-1a [C, the two sources]. The key does not
  use it.
- `MeshKey` gained `vertexHash` (4870 #6015: index buffer from element 1 + vertex buffer + range); city-renderer.md 2.2
  shows the older three-field form.
- The install's `.gsd` files hold 29,214 strings; the city-set files keep their names there (read_gr2 without them
  yields no models for `poly_anc_city.gr2`) [C]. The library loads them once through `units::loadStringDatabases`.

## 6. Not verified

- The R32 buffers without a pad (2.2) as the Great Wall's CPU-built meshes: 4870's reading (#6040), not checked here.
- R32 files (VP mods): the leading element and the layout are assumed as for R16 files.
- The wall files' scale (1 used; `civ5artdefines_walls_babylon.xml` names `fScale` 0.05 for Babylon's walls) and the
  walls' and Great Wall's vertex form (Half20 used): no walled city was captured (4871's P4 capture).
- ~~The texture slots (`t0`/`t1` from the material maps, not the parameter sets) and P8 (textures follow the mesh).~~
  Settled by 4873 (section 7.4): t0 / t1 come from the parameter sets; the wire key's texture identity tells the
  shared keys apart on every resolved draw of the 16 frames.
- The order of the city-set files beyond the two in the install, and a culture defined twice.
- 4870's afri, poly and Sanc01 samples were not re-checked by this code.

## 7. The draw layer, the tap frame and the offline harness (4873)

Written 2026-11-18 18:23-19:xx EST by worker-4873 (account B, Opus 5.5), ticket 4873 of epic 0561, from the plan
`city-renderer.md` 2.2, 2.4, 3.1, 5.4-5.6 and 6 and 4874's frame image (`shell/common/shell_model_tap.h`, thread 0561
#6098 / #6099). No game was run. Marks as in this file's head: `[C]` checked by this ticket's code or its harness on
the data named; `[4870 #n]` / `[4874 #n]` another worker's note, not re-checked; `[I]` inferred.

Inputs of every measurement in this section: the install copy `render_game_dir` (pure VP install, VP 5.4.7 as DLC,
Vibrant Textures; D94), the EXE copy `render_exe` (SHA-256 b2ce96ae...0670c88e, build 4390913), the database copy
`Civ5DebugDatabase.db` SHA-256 27fc81c6...82fd19b4 (as section 1), the 3907 / 3910 exports on
`F:/c5-64bit-work/0301/` (c5h-d, DLL `i0ship-26234371-ia`, CP/VP/EUI, Vibrant Textures, Tiny save
`C5H-0301-CAT2_0009`; scene-catalogue.md 11), read in place, each buffer once. Release render tree of this branch on
the workstation's GPU (hardware device), the same device for the reference replay.

### 7.1 Code

- `render/include/c5r/landmarks/tap.h`, `render/src/landmarks/tap.cpp`: `TapFrame` (the parsed form of 4874's
  `VPModelTapFrame`: targets, draws, the float4 row block, the constant table), `readTap` / `writeTap` (the `.mtap`
  file form = the frame image byte for byte, written through the header's structs and `VPModelHash64`), `checkFrame`,
  the wire key (`WireKey` = `VPModelKey`; `wireIndexHash` from element 1; `textureIdentity` =
  `VPModelTextureHash` of the level `VPModelTextureMip` picks), `WireIndex` (every library part's wire key, the
  install's DDS hashed as the compositor hashes the EXE's upload), `resolve` (tap -> `ModelScene`, unknown draws
  counted), `.c5models` (`writeScene` / `readScene`: the scene with each file named by (gr2, scale)).
- `render/include/c5r/landmarks/draw.h`, `render/src/landmarks/draw.cpp`: `BuildingPrograms` (the five pass-0 programs
  of `shell_model_scene.h`'s `kVPModelPrograms` found in the EXE's `.shader` section by SHA-256), `inputElements`,
  `captureStates`, `captureSampler`, `ModelRenderer` (`init`, `prepare(file)` / `prepared(file)` for the identity set
  (4875 #6160), `draw(ctx, scene, ViewInput, EnvInput, DrawReport*)`, an optional id pass for the harness).
- `render/src/landmarks/params.cpp` (`materialParams`): the materials' FireGrafix parameter sets (7.4); the library
  takes `t0` / `t1` from them (`library.cpp`, `layout.cpp`: `Part::materialIndex`).
- `render/tools/cityscene/`: `cityscene.exe tap | run | params | texid` and the driver `cityscene.py` (7.5).
- Tests `render/tests/landmarks/test_tap.cpp` (file forms, every check, hashes, identities, resolve of unknown keys,
  `.c5models` refusals; synthetic) and `test_draw.cpp` (programs and renderer refusals, the state table, one block of
  `euro_anc_city.gr2` drawn with the EXE's programs on the device).
- Evidence: `render/evidence/0561/4873/` (`r1.py`, the result tables).

### 7.2 Programs and input layouts [C]

- The ten BuildingShader blobs at 2902's file offsets in the EXE copy hash (SHA-256) to digests whose first 8 hex
  digits are 4870's names (`programs.csv`), and equal `shell_model_scene.h`'s table [4874 #6088 for the create
  sites]. The draw layer looks up the five pass-0 programs by digest only (VS `7ec4e9a5`, `c4bb25da`, `7a9e17f6`; PS
  `c704c80b`, `f0e1f0a6`); nothing but digests is in the repository (D83).
- Input layouts as the captures bind them (4870's per-draw summary of five exports: 1,202 material-4, 219 material-5,
  169 material-6, 16 material-3 draws, one layout per material): material 5 POSITION / NORMAL R32G32B32, TEXCOORD0
  R32G32 (32 B); materials 3 and 4 POSITION / NORMAL R16G16B16A16_FLOAT, TEXCOORD0 R16G16_FLOAT (20 B); materials 4 and
  5 add slot 1 per instance TEXCOORD1-3 and COLOR0 R32G32B32A32 (64 B: the world rows, `x r0 + y r1 + z r2 + r3`);
  material 6 is the units' 28-byte stream with slot 1 BLENDINDICES1 R32_UINT. Material 3 has no slot 1 (VS cb0 rows
  0-3).
- The VS reads cb1 rows 0-3 as the view-projection in the same row form (`[x y z 1] x M`): checked by projecting every
  tapped instance's box (r1.py) against the captured id buffer: on c_renaissance 117 draws inside the view all have
  captured pixels, the 44 wholly outside have none, 2 inside have none (covered).

### 7.3 States and samplers [C, every listed draw of the 16 frames against `captureStates`]

| Material | Blend (colour) | Raster | Depth |
|---|---|---|---|
| 5 (blocks) | off | scene cull back, reflection cull front; front faces counter-clockwise | less-equal, written, no stencil |
| 3, 4, 6 | SRC_ALPHA / INV_SRC_ALPHA add; alpha ONE / ONE add | as 5 | as 5 |

Samplers s0-s9: MIN_MAG_MIP_LINEAR, wrap, except s6 and s9 clamp (MaxAnisotropy 0 in the capture, 1 here: not read by
a linear filter). One material-3 draw per W01 differs (blend off): W01 draw 617, the Great Wall's CPU-built mesh
(not in the library, so never drawn by the server). The per-frame check is `states.json` of each run (7.6).

### 7.4 Textures: what the EXE binds in PS t0-t9 [C for t0 / t1 and the sizes; I for the t2-t9 roles]

- **t0 / t1 come from the material's parameter set, not its maps.** A BuildingShader material's FireGrafix
  `ExtendedData` holds a parameter set `BuildingTextures` with, in order, `Diffuse`, `BuildingSREF`, `Irradiance`,
  `SpecSharp`, `SpecSoft`, `DistanceFogRamp`, `FOWColorKey`, `FOWMask`, `FOWBaseLayer` (file names, often a full
  artist path: the base name counts). VP's `resource_tea.gr2` keeps cotton's geometry and cotton's maps
  (`cotton_diff.dds`) and names `tea_diff.dds` in `Diffuse`; the capture binds tea's texture. With the maps, W01's 38
  draws of tea / perfume and of two farm-crop decals (`wheat_decal.gr2` / `corn_decal.gr2` / `rice_decal.gr2`, one
  geometry) missed on t0; with the parameter set all resolve (7.6). The units found the same rule for their materials
  (unit-renderer.md 10.8). In the files read so far only `Diffuse` and `BuildingSREF` hold names; the other seven are
  empty, so the EXE supplies those slots itself.
- The slots by size in the captures and the parameter order: t2-t4 cubes (128 x 128 BC1 sRGB in the scene, 32 x 32 in
  the reflection; `Irradiance`, `SpecSharp`, `SpecSoft`), t5 128 x 8 R8G8B8A8 (`DistanceFogRamp`), t6 a 32^3 R8G8B8A8
  volume (the Buildings ColorKey volume [C, 4882, 2026-11-19: matched by SHA-256 against the install; the PS maps the
  lit colour through it, so unbound it draws every model black; city-renderer.md 11.12]; was "`FOWColorKey` [I]" by
  the parameter order), t7 1024 x 1024 R8G8B8A8 (`FOWMask`: the fog of war mask), t8 1024 x 1024 BC1
  (`FOWBaseLayer`), t9 a second 32^3 volume (not named by the block). The names of t2-t9 follow the parameter order
  [I]; the server supplies t5-t9 from its own fog (4875) and t2-t4 by the tap's per-draw identity (`env[3]`).
- t0 and t1 are sampled through sRGB views (BC1 / BC2 / BC3 `_UNORM_SRGB`, on all 149 W01 draws); the draw layer
  creates the install's DDS with the sRGB form of its format.
- Texture identity (`VPModelTextureHash`, 4874's rule): the level whose width and height are first <= 64, tight rows,
  slice 0. The install's DDS gives the capture's identity on every resolved draw (7.6: no texture miss left). A DDS
  in A8R8G8B8 is hashed after the swap to R8G8B8A8 (the EXE uploads it so, unit-renderer.md 10.3); whether any
  BuildingShader t0 / t1 is such a file is in 7.6.

### 7.5 The harness (`render/tools/cityscene`)

    B=C:/c5-64bit-work/render-build/<key>/Release
    python -I render/tools/cityscene/cityscene.py frame <export> C:/c5-64bit-work/0561/4873/runs/<name> \
        --tool $B/tools/cityscene/cityscene.exe --replay $B/tools/terrainreplay/terrainreplay.exe \
        --game <install> --db <Civ5DebugDatabase.db> --exe <CivilizationV_DX11.exe> \
        --objects C:/c5-64bit-work/0561/4870/objects_3910.csv --export-name <name>      (3907: objects_3907_cities.csv)
    python -I render/evidence/0561/4873/summarize.py <out> <run dirs>                    (the tables of 7.6)
    python -I render/evidence/0561/4873/r1.py <out>/r1.json <run dirs>                   (7.7)

1. **The draws**: every `DrawIndexedInstanced` of the four DROP pairs (`shell_model_scene.h`) with a depth view bound
   (the scene target 2419 and the water reflection 13415; the mask pass has none and stays the EXE's), in frame order,
   from `draws.json`; each draw's `state.json` once. Its buffers, constants and textures are named in `frame.tsv`.
2. **The tap** (`cityscene tap`): the frame image 4874's compositor will write, made from the capture's own buffers:
   per draw the wire key (IB from element 1, the whole slot-0 VB, t0 / t1 identity, range), the instance rows (slot 1
   at its offset + StartInstance x 64; VS cb0 rows 0-3; per skinned instance the 96 rows of VS t27 at the slot-1 id),
   VS / PS cb1 deduplicated by content, PS t2-t4 identity, the target. Written as `.mtap`, read back by the module's
   reader, compared field by field.
3. **Resolve and draw** (`cityscene run`): library load (`load` of 621 files), `WireIndex::build` (all 621 files
   resident and hashed, every t0 / t1 read: 5.9-6.3 s on the workstation, an indication), `resolve`, `.c5models`
   written and read back, the scene drawn per target into an R8G8B8A8 target of the capture's viewport size with a
   D32_S8 depth (terrainreplay's), cleared to 0 / 1, with the EXE's programs, the library's buffers and textures, the
   states of 7.3, and PS t2-t9 from the capture's own textures (like for like; the server's own are 4875's). An id
   pass (depth equal) writes 1 + the item index.
4. **The reference**: the exports hold no contents of the scene target (`textures/` has no 2419), so the reference is
   the captured draws themselves replayed by terrainreplay (`models/tools/unitreplay/unit_manifest.py`, the units'
   method of 4355): the capture's buffers, textures, programs, states and constants, in frame order, into the same
   kind of cleared target, with its id buffer. Only the draws the library resolves are replayed: a key the library
   does not hold is outside the identity set, so the compositor passes it and the EXE draws it (city-renderer.md 3.2);
   those are listed per frame (`unknown.tsv`).
5. **The compare** (COMMON 25): on the object pixels (either side's id set), the per-pixel largest RGB difference:
   median, p95, max, share within 16, per frame and target and per catalogued object (4870's placement: a draw belongs
   to the object of its first placed instance; the save's own content is the object `save`); a difference image (x 8)
   and both pictures per target, recorded in Visual Checks as DIAGNOSTIC (D112), never in git.

What the comparison covers and what it does not: it checks the tap -> library -> draw chain (identity, instance rows,
constants, meshes, textures, programs, states, order) against the EXE's own draw of the same frame. It does not check
the MSAA resolve of the real target (both sides draw single-sampled), the terrain and other systems' depth (both sides
draw into a cleared depth), the mask pass (the EXE's), and the server's own t5-t9 (4875).

### 7.6 Results: 16 frames against the C1 tolerance [C, 2026-11-18 18:52-19:27 EST]

Every frame and target meets city-renderer.md 6 (median <= 2, p95 <= 8, >= 97 % within 16 per frame, >= 75 % per
object). Object pixels = the union of both id buffers on the draws the library resolves; "max" is the largest per-pixel
RGB difference (8-bit). Tables: `render/evidence/0561/4873/frames.csv`, `objects.csv`, `states.csv`.

| Frame (scene target 2419, 2554 x 1411) | Resolved / listed | Unknown (left to the EXE) | Pixels | Median | p95 | Max | Within 16 | Objects (>= 75 %) |
|---|---|---|---|---|---|---|---|---|
| 3907 c_ancient | 174 / 174 | - | 118,878 | 0 | 0 | 24 | 0.99999 | 8 / 8 |
| 3907 c_renaissance | 169 / 169 | - | 115,558 | 0 | 0 | 91 | 0.99999 | 8 / 8 |
| 3907 c_industrial | 166 / 166 | - | 107,712 | 0 | 0 | 15 | 1.00000 | 8 / 8 |
| 3907 c_modern | 184 / 184 | - | 117,237 | 0 | 0 | 96 | 0.99998 | 8 / 8 |
| 3910 W01 | 147 / 149 | 469, 617 | 222,235 | 0 | 0 | 61 | 0.99998 | 8 / 8 |
| 3910 W02 | 194 / 194 | - | 212,394 | 0 | 0 | 60 | 0.99999 | 8 / 8 |
| 3910 W03 | 152 / 152 | - | 205,585 | 0 | 0 | 0 | 1.00000 | 8 / 8 |
| 3910 W04 | 180 / 180 | - | 214,048 | 0 | 0 | 116 | 0.99999 | 7 / 7 |
| 3910 W05 | 159 / 159 | - | 214,824 | 0 | 0 | 31 | 1.00000 | 7 / 7 |
| 3910 W06 | 165 / 166 | 649 | 167,517 | 0 | 0 | 103 | 0.99999 | 6 / 6 |
| 3910 Fanc01 | 615 / 615 | - | 327,273 | 0 | 0 | 0 | 1.00000 | 12 / 12 |
| 3910 Find01 | 632 / 632 | - | 321,647 | 0 | 0 | 85 | 1.00000 | 12 / 12 |
| 3910 Lanc01 | 464 / 464 | - | 237,175 | 0 | 0 | 123 | 0.99999 | 12 / 12 |
| 3910 Lanc07 (skinned deer) | 428 / 428 | - | 155,911 | 0 | 0 | 0 | 1.00000 | 12 / 12 |
| 3910 Lind01 | 472 / 472 | - | 236,983 | 0 | 0 | 76 | 0.99999 | 12 / 12 |
| 3910 Sanc01 (skinned fish) | 240 / 240 | - | 101,142 | 0 | 0 | 0 | 1.00000 | 9 / 9 |

- The water reflection target 13415 (1024 x 1024): 21-2,901 pixels per 3910 frame (none in the four city frames),
  median 0, p95 0, max 0-5, except Sanc01 (max 144, 0.99966 within 16; its worst draw 210 at 0.9988).
- **Per object** (158 catalogued objects with pixels; the save's own content apart): the lowest share within 16 is
  0.99884; no object has a median or a p95 above 0.
- **What differs at all**: in 11 of 16 scene targets a few pixels exceed 16 (max 24-123), at most 0.002 % of a
  frame's object pixels, always in a few draws (Lanc01: draws 1160 and 1172, one mesh of `barbarian_village3.gr2`,
  0.9987 / 0.9992 within 16). The draw's inputs are equal (index and vertex bytes by the wire key, textures by
  identity, the rows and constants copied); the cause is not found [open: one candidate is a texture's levels above
  the identity level, which the key does not cover].
- **Resolve**: 4,541 of the 4,544 listed colour draws resolve. The three left are not files: W01 469 and 617 are the
  Great Wall's CPU-built meshes (4870 #6040; child 4879); **W06 649** is a 32-byte-vertex copy of a Broadway mesh in
  its own buffer (IB res 12920, VB res 12921: 1,199 vertices; the index count 1,977 and the textures of the file draw
  506, which resolves) [C for the buffers; I that the EXE builds it]: the identity set keeps it with the EXE.
- **Ambiguous keys**: 1 to 87 draws per frame name a key that several parts hold with equal textures (Lanc01: 87);
  the first part is drawn; equal bytes, equal picture.
- **The t2-t4 sets are per draw, not per class and pass** (`env_sets.txt`; every env identity of the 16 frames equals
  one install cube's [C]): blocks bind the owner culture's cube light (`asia_cubelight.dds` 494, `europe_cubelight.dds`
  401, `africa_cubelight.dds` 129 draws) with `hiconreflmap_soft.dds` / `hiconreflmap_sharp.dds`; instanced
  landmarks `improvements_cubelight.dds` with sharp / soft (t3 / t4 the other way round, 1,923 + 145 reflection),
  resources `resource_irradiance.dds` / `resource_environment_sharp.dds` / `resources_environment_dull.dds` (688), some
  the units' `cubelight_generic_32.dds` / `unit_environment_sharp.dds` / `unit_environment_dull.dds` (323) or
  `unit_light.dds` (4); skinned parts mostly the units' set (350); single models `improvements_cubelight.dds` (75) or
  the culture's (1). So the tap's per-draw `env[3]` (4874's record) is needed, and the server loads these cubes from
  the install and matches them by identity.
- **The BGRA rule** (lead2 #6147): the EXE creates an A8R8G8B8 `.dds` as R8G8B8A8 with the bytes swapped (W03
  `kremlin_diff.dds`: res 5997, R8G8B8A8_TYPELESS, view _SRGB); hashing the swapped bytes gives the capture's identity:
  `kremlin_diff.dds` (2 draws) and `resource_bison.dds` (21 draws over 6 frames) resolve on their t0 (`bgra_t01.txt`).
  One rule: the identity of the initial data as CreateTexture2D receives it.
- **States**: every listed draw of the 16 frames (4,544) equals 7.3's table except W01 617 (the Great Wall, blend
  off).
- Indications (workstation with other work running, never a timing claim, COMMON 23): draw and read-back of one scene
  target 23-120 ms on the hardware device (the first draws include texture creation); GPU buffers and textures made
  per frame 2.6-9.6 MB; per frame run 1.3-2.4 min, mostly the library load and the F: reads.
- Pictures: Visual Checks, issue 4873, 16 entries (ours; the replayed capture as reference; the difference x 8), all
  DIAGNOSTIC (D112).

### 7.7 R1: the tap of frame N drawn at the camera of frame N+1 [C for the counts; I for the fix's cost]

`r1.py` projects every tapped instance's box (the 8 corners of its draw's vertex range through its world rows and the
frame's view-projection) into its own frame's screen and into other frames' (`r1_land.json`, `r1_cross.json`).

- **The EXE draws far beyond the view**: 10-51 % of the tapped instances (materials 3-5, scene target) lie wholly
  outside the 2554 x 1411 viewport: up to 1,570 px outside in the 3910 land frames (218-232 of 451-599 instances;
  median distance 155-222 px), up to 309 px in the wonder frames (32 %), up to 307 px in the 3907 city frames (10 %).
- **But it does cull, close to the edge in places**: the one pair of one save at two cameras with shared content near
  the edge is the wonder camera (W01) against the land camera (Lanc01), shifted 182 px (94.7, -155.7), on the save's
  own content (4870's kind `save`; 67 instances in both). Of Lanc01's save instances visible in Lanc01, 54 are absent
  from W01's tap and lay outside W01's view, 21.6 to about 150 px out (p10 50.9, median 92.8 px); their draws cover
  22,448 px of Lanc01, 9.5 % of the frame's family pixels. (29 more are absent though inside W01's view: content that
  differs between the two processes, not culling.) The 3910 land frames share one camera (shift 0) and the sea and city
  frames are 450-2,800 px away, so no pair gives a small shift; the nearest culled instance, 21.6 px out, is the bound
  measured.
- **Meaning**: with one frame of lag a pan faster than about 20 px per frame can leave models missing at the leading
  edge; slower pans are covered by the EXE's own margin in these frames. The trailing edge has no hole (the tap holds
  what leaves the view).
- **The fix named (for 4875 / 4878)**: draw each EXE frame's models from that frame's own tap at that frame's camera
  (the compositor publishes tap N at the Present that ends frame N; the server's model pass for frame N waits for
  it): no hole at any pan speed and no memory, at the price that the model pass cannot start before the EXE's frame is
  drawn (latency inside the frame, for 4878 / P7 to measure). A keep-alive margin ("keep each tapped instance until it
  leaves the view by M px") fills only the trailing edge, where there is no hole, and helps only when the camera pans
  back (a cache of earlier taps: 64 B per instance for a few thousand instances, with eviction on the plots' change
  events). If 4875 must draw a frame ahead, the rule is: lag the tap by one frame only while the camera moves less than
  about 20 px per frame, else wait for the frame's own tap.
- **The wrap views (ticket 0652, 2026-11-21; `city-renderer.md` 17)**: on a wrapping map the EXE draws the landmarks and
  cities it sees through a wrap view with that view's camera in VS cb1 (view 0's moved by k x W). Drawing every record at
  the frame's view 0 (4882's camera rows) put those one world width off screen: at the seam the moai and Salem's city
  were missing. Each constant now takes the wrap copy of the frame's camera nearest its tapped rows; a constant that fits
  no copy draws nothing that frame and is named LOUD. Cause, fix and tests there.

#### 7.7.1 The EXE's landmark cull: what decides that an instance near the edge is not drawn (ticket 0709, worker-0673v, 2026-11-23 13:00-13:12 EST) [C, EXE build 4390913, headless Ghidra read of a scratch copy of the project, D83: behaviour only]

*Replaces the reading above that the EXE culls "with a margin of 21.6 px": 21.6 px was the smallest distance measured on
one frame pair; the rule is a box in world units, below.*

- **Where**: "Render Landmarks" 0x6187D0 (the landmark vis system 0xDB13D8, reached from 0x7A6680) walks the instance
  list (+0xB20, 0x4B0-byte entries) and submits an instance only when its flag byte has bit 0x40 and one of bits 0-1,
  and its **origin's world x, y** (instance + 0x478 / + 0x47C) lies strictly inside the axis-aligned box of view 0 or
  view 1 (at most the first two views) widened by **64.0 world units** on every side. Only the origin is tested, not
  a bound of the model.
- **The box**: the system's own copy at +0x82C (min x, y, z of view v at +0x82C + 0xC v) and +0x85C (max at
  +0x85C + 0xC v), i.e. `.data` 0xDB1C04 / 0xDB1C34 (bss). It is written every frame by the system's listener
  0x6025F0 on the views snapshot, which copies the whole 0x84C-byte snapshot into +0xFC. The snapshot is built by
  0x617F30 (called at the end of UpdateViews 0x51DF70 and of the other view writers, `../02-exe-internals/camera-and-picking.md` 4)
  and handed to the A manager's list at `[0x1A0E518 / 0x1A0E51C + 0x600 x channel]` (lock 0xCBA9F0, init bit
  0xCC733C, the lock of `../02-exe-internals/farm-builder-guard.md` 13.4). The landmark vis system registers its
  listener (object 0xDB13D8, method 0x6025F0) from its startup 0x6711A0 through 0x4C5A70, which first removes an older
  registration (0x84C3E0). The snapshot's box fields (+0x730 min, +0x760 max) are the globals **0xDA1B60 / 0xDA1B90**
  (+0xC per view): the bounding box of the four frustum side edges cut at z = 0 and z = 127.5, as the water's footprint
  0x5D0370 computes it (`water-system.md` 10.2), **not** clamped to the world box (0xDA1B00 / 0xDA1B30 are the clamped
  pair the water uses).
- **The 64.0**: a literal in `.rdata` (0x9E0AA8, f32 64.0), read by 39 instructions in about 30 functions (the trees'
  plane test uses its own -64 at 0x9D939C). It is no landmark setting.
- **So the margin is not a constant in pixels**: the box of a trapezoid holds the screen's near corners with a lot of
  room (the 155-1,570 px of 7.7) and the far edge's side with 64 units plus the z = 127.5 cut; 21.6 px was the closest
  culled instance in one pair. Fast pans outrun it whatever its value in pixels.
- **Build 26577625** [C, bytes of the golden copies; addresses in the local table only, D202]: the landmark vis
  system's startup (0x6711A0's bytes with operands and calls masked, one hit), the views method it registers (its 24
  bytes the same as 0x6025F0's), the 0x6187D0 head with the same field offsets (+0x82C, +0x85C, +0x90C) and the 64.0
  literal (same value), and the snapshot builder's copy of the two box globals (one hit each) were found. The addresses
  are in `C:/c5-64bit-work/0709/out/new_build_local*.txt` (scratch, never committed), for the build table's catalogue.
- **The server-side fix built instead (2026-11-23, ticket 0709)**: the server keeps the re-entering instances from an
  older drawn tap, with this box rebuilt from the tapped rows as its test (`pan-coherence.md` 5.14); it covers
  re-entries, not first entries, which only the widening below (or tap P) can cover.
- **What can be widened, and how** [I, design; nothing built]:
  - The literal at 0x9E0AA8 is `.rdata` and shared by about 30 functions. A write there is outside the base product
    (D228, Extras only) and changes them all. Not a route.
  - The globals 0xDA1B60 / 0xDA1B90 are `.data` but rewritten every frame, and the shadow view matrix 0x615950 and about
    seven other functions (0x416B00, 0x4FB320, 0x57BAB0, 0x59F8C0, 0x5FACB0, 0x643B60, 0x674AD0) read them. Not a route.
  - **The route (the base product's own tools)**: swap the landmark vis node's method word on the views list for a
    pass-through, under the A manager's lock, as 0655's area probe swaps a node on the type-33 list
    (`farm-builder-guard.md` 13.4). The pass-through copies the snapshot, widens the copy's box for views 0 and 1
    (+0x730 min x, y down, +0x760 max x, y up), then calls 0x6025F0 with the copy. Only the landmark vis system
    sees the wider box: other listeners get the snapshot unchanged, and no other function of the system reads
    +0x82C or +0x85C (operand scan; [I] that nothing reads them through another base).
  - The widening can follow the camera: grow the box only on the side the camera moves, by the last frame's motion
    plus a margin, so it costs nothing when the camera is still.
  - Costs: every extra instance is submitted by the EXE, tapped, copied and dropped by the compositor
    (`modelfilter` copy_kb / copy_ms grow with the count) and drawn by the server off screen. A fixed margin large
    enough for 0709's p90 pan (370 px per frame, pan-coherence.md 5.11) could triple the landmarks in the box [I, not
    measured].
- **How to verify** [I]:
  - Offline: a stand-in A list as in `shell/tests/farm_guard_test.cpp` section 16: the swap under the lock, the copy
    widened, the method called once per snapshot, the original bytes back when the key is off.
  - Live: the recorder check of pan-coherence.md 5.11 with the swap off and on. The holes in the leading band should
    go to about zero, and `modelfilter` `tapped`, `copy_ms` and the server's `models` stage should be measured before
    and after.
  - **The live read the route needed first: done** (lead-0673, 2026-11-24 02:15-02:19 EST, c5h-a, EXE 4390913, stock VP
    with nothing laid, the huge save copy C5H-VP8P-HUGE_0350 at T350, hidden, frame-capped; ReadProcessMemory only,
    `server/evidence/0709/listread0709.py`, runbook 4.20) [L]. (Replaces: "walk the views list ... they should be equal", the open
    read until 2026-11-24.)
    - The EXE loads relocated (module base 0x009B0000 in this launch): every VA below is rebased on the module base, and
      the node's object and method words hold rebased addresses too, so a swap writes a rebased pass-through address.
    - Channel index [0x1A0E400] = 0. Handler word [0x1A0E518] = VA 0x63E530, the list pointer [0x1A0E51C] = a heap list:
      nodes +0x08, size +0x0C = 1771, capacity +0x10 = 2048, count +0x14 = 1771, head +0x18 = 1. Node words: next +0x00,
      +0x04 = 0x80000000 | a serial, object +0x0C, function +0x10 (the type-59 walk of `farm_guard.cpp`).
    - **The node exists once**: index 5, the 8th in walk order, object 0xDB13D8, function 0x6025F0. Seven fixed system
      listeners come first or near it (functions 0x5814D0, 0x57E750, 0x43F830, 0x718570, 0x645ED0, 0x52A570, 0x6154A0,
      0x71CF00); the other 1762 nodes are heap objects with function 0x63B950 (one per object of one kind; not identified).
    - **The system box equals the snapshot box**: camera still, 400 samples 5 ms apart, all 24 floats of 0xDB1C04..0xDB1C63
      equal to 0xDA1B60..0xDA1BBF in 400 of 400. During drive0709.py drag pans (132 distinct view-0 boxes), equal in
      3995 of 4000; the 5 others differ by at most 11.05 units, one frame's motion, where a frame fell between the
      reader's two reads (not atomic). So the listener copies each frame's snapshot box before the frame's landmark walk
      reads it, and widening the box handed to 0x6025F0 is what the walk tests.
    - View 0 and view 1 are the two wrap copies, 8191 units apart in x (e.g. min x 2596.8 and -5594.2), the same y; views
      2 and 3 are all zero. A widening must widen both.
    - The node's risk against 0655's pattern: the same list form, lock and walk as the type-33 area probe, on another
      type slot: type 33's list word is 0x1A0E514 (`farm-builder-guard.md` 13.4), this list's is 0x1A0E51C, the next
      8-byte handler/list pair [I]; whether the handler VA 0x63E530 is the same listener walk as type 59's is not checked
      (a byte compare of the two, offline, before the swap is built). [I] No greater risk;
      one difference: this listener runs every frame, the area probe's only on posts, so a pass-through costs a
      0x84C-byte copy per frame.

### 7.8 What the server supplies, and where the live-terrain height add goes

- The server supplies (4875): the targets and views (`ViewInput`); the t2-t4 cubes by the tap's identity
  (`EnvInput::env`, the install files of 7.6); t5-t9 (`EnvInput::frame`: t5 the distance-fog ramp, t6 / t9 the two
  32^3 volumes, t7 the fog-of-war mask, t8 its base layer: the server's own fog, city-renderer.md 2.3).
- `ModelRenderer::draw` writes the frame's row block (one buffer for vertex slot 1 and the t27 views) and one constant
  buffer per tap constant per call into DYNAMIC buffers the renderer keeps, by `Map(WRITE_DISCARD)` (ticket 4884,
  2026-11-19, city-renderer.md 14.3; was: "creates ... one immutable buffer ... per call; a server drawing every frame
  may keep them dynamic [I]", which 4884 measured at about 0.03 ms of CPU per pass and most of the pass's GPU interval).
- The Architect's live-terrain note (MC #6254): the tapped matrices carry the EXE's placement height; the later add
  (the current height field minus the field at the last tap) goes on the rows before `draw()` uploads them: r3.z of
  each instance's four rows (materials 4 and 5, slot 1), r3.z of material 3's cb0 rows 0-3, and the w of rows 0-2 of
  each bone in a skinned instance's 96 palette rows (3 x 4 rows, translation in w, city-renderer.md 5.4). Not built:
  `resolve()` and the draw use the tapped rows as they are.

### 7.9 Not verified

- Nothing here ran against a live tap (4874) or in the server (4875): the `.mtap` files are the frame image as 4874's
  header defines it, made from captures.
- The few pixels above 16 of 7.6 (cause open); the MSAA of the real scene target (both sides draw single-sampled);
  depth against terrain (both sides draw into a cleared depth).
- R1 at small shifts (no pair below 182 px); the EXE's cull rule itself (not read); the threshold of about 20 px rests
  on one pair.
- The names of t5-t9 (parameter order and sizes) [I]; walls (CPU-built, 4879; 4871's capture not run here).
- 4872's shared-key counts of 3.1 (1,027 / 200) used the maps' names; with the parameter sets they were not recounted.
- ASan ran on the landmarks test targets alone (7.10): the full MSVC ASan tree stopped at tools/bakecompare
  (shell_shared.h C4201, #6269) until 4874's fix (8f6ca81ec, team branch 594c0583e); after that merge the whole ASan
  tree builds (2026-11-18 20:0x EST), but the other modules' suites were not run under it here.

### 7.10 Tests and the sanitizer run [C, 2026-11-18]

    python -I render/build.py --test -R "c5r_(landmarks|cities)_"        (C5R_LANDMARK_DB = the database copy)
    python -I render/build.py --asan --target c5r_asan_canary
    python -I render/build.py --asan --target c5r_landmarks_test_<t> --test -R "c5r_landmarks_test_<t>$"   (t = tap, draw, library)

- Release: 6 of 6 (`test_tap`, `test_draw`, 4872's `test_layout`, `test_library`, `test_oracle`, `test_catalogue`),
  none skipped. `test_tap`: the frame image round trip byte for byte; every reader check (Truncated x 4, BadMagic,
  BadVersion, header and record size, counts, alignment, overlap, past the end, image size, hash, unknown class, rows
  per instance) and every `checkFrame` refusal (also through the writer); the wire index hash's refusals; the texture
  identity (the level, the skipped top level, the pitch, a format without a row size, a missing or short level); the
  classes; `resolve` of unknown keys; the `.c5models` refusals (Truncated x 3, BadMagic, BadVersion, header size, a
  file the library lacks [NotFound], an unknown material, a target, a constant and rows out of range, a string past
  its table). `test_draw`: the programs' and renderer's refusals, the layout and state tables, one block of
  `euro_anc_city.gr2` drawn with the EXE's programs (1 item, id pass covers > 100 px), `prepare` / `prepared`.
- ASan (MSVC): the first run of `test_draw` reported a stack-use-after-scope: `ModelRenderer::release` released the
  `BuildingPrograms` it had borrowed, which the test had already destroyed. Root cause: the renderer released what it
  does not own. Fixed: the renderer never releases the programs (draw.cpp, draw.h's ownership comment). Rerun
  2026-11-18 19:43-19:51 EST: `test_tap`, `test_draw`, `test_library` 3 of 3 passed, 0 ASan reports; Release 6 of 6
  again after the fix.

## 8. Bridges: the road and railroad bridge model (4877)

Written 2026-11-18 20:21-20:50 EST by worker-4877 (account B, Opus 5.5), ticket 4877 of epic 0561, step 1 of the
ticket (find the file). No game was run. Marks as in this file's head: `[C]` checked by this ticket's own scripts on the
data named; `[4871]` worker-4871's capture notes (`render/evidence/0561/4871/README.md`), not re-run; `[I]` inferred.

Inputs: 4871's P6 export `bridges2_4871/P6_road` on `C:/c5-64bit-work/0561/4871/exports/` (c5h-b, DLL
`i0ship-e96457fd-ia`, CP/VP/EUI, Vibrant Textures, save copy `C5H-0301-4871_0009`, the active team in the Postmodern
era), its scene draw #838 (IB res 228143, VB res 228142), read in place; the install copy `render_game_dir` (pure VP,
VP 5.4.7 as DLC, Vibrant Textures; its 147 `.fpk` archives listed with `evidence/9012/fpk.py`); the EXE copy
`render_exe` (SHA-256 b2ce96ae..., build 4390913), read for strings only; `c5r_gr2_facts` of this branch's Release tree.
Scripts: `render/evidence/0561/4877/cap_bridge.py` (decodes the draw's IB and VB), `fit.py` (the file-to-capture fit).

### 8.1 The file: `Bridges.gr2`, by the EXE's own fixed name [C]

- **Not data-driven.** No art define, XML, SQL or Lua file of the install names a bridge model; the `ArtDefine_Landmarks`
  rows name only the pontoon bridge (`Pontoon_Bridge.fxsxml`, SPECIAL, 3 states; `evidence/9012`). The EXE's
  `.rdata` holds the fixed name `Bridges.gr2` (VA 0xA2B0F0), then six model names at 16-byte steps (0xA2B0FC-0xA2B14C:
  `ANC_BRIDGE_END1`, `ANC_BRIDGE_MID`, `ANC_BRIDGE_END2`, `IND_BRIDGE_END1`, `IND_BRIDGE_MID`, `IND_BRIDGE_END2`) and
  the message `CvlDBridge gr2 file %s missing or malformed.` (VA 0x9ED16C) [C, a string search of the EXE copy; who
  reads these strings was not decompiled].
- **The file**: `bridges.gr2` in `Resource/Common/MiscData.fpk` (15,680 B, with `bridges.fxsxml`, 165 B); its textures
  `bridges_diff.dds` (128 x 128 BC1, 8 levels, 11,064 B) and `bridges_sref.dds` (64 x 64 BC2, 7 levels, 5,616 B) in
  `DecalTextures.fpk`. The draw's PS t0 and t1 are 128 x 128 BC1 (8 levels, sRGB view) and 64 x 64 BC2 (7 levels, sRGB
  view): the same forms [C for the forms and sizes; the texture contents were not dumped in 4871's export, so the
  identity is by form, not by bytes].
- **Content**: 6 models with one mesh each, one material `Bridges` (shader set `BuildingShader`, maps Diffuse
  `bridges_diff.dds`, BuildingSREF `bridges_sref.dds`), 60-byte float vertices (Position, Normal, Tangent, Binormal,
  TextureCoordinates0 x 3), 16-bit indices, units/m 39.37:

  | Mesh | Vertices | Indices |
  |---|---|---|
  | IND_Bridge_End2 | 64 | 108 |
  | IND_Bridge_Mid | 32 | 54 |
  | ANC_Bridge_End2 | 84 | 144 |
  | ANC_Bridge_Mid | 28 | 42 |
  | ANC_Bridge_End1 | 84 | 144 |
  | IND_Bridge_End1 | 64 | 108 |

### 8.2 The captured bridge is the three IND pieces, CPU-built per crossing [C]

- **Which meshes**: the draw's 160 vertices and 270 indices are IND_Bridge_End2 + IND_Bridge_Mid + IND_Bridge_End1
  (64 + 32 + 64; 108 + 54 + 108), in that order: all 160 texture coordinates equal the file's in that order (0 of 160
  differ by more than 0.002, the half-float step); the index ranges per piece are 0-63, 64-95 and 96-159 (each piece's
  own indices plus its base vertex), the first indices of each piece equal the file's (`0 1 2 2 3 0 4 5 6`).
- **Not the file's buffer**: one R32 index buffer of exactly 270 elements (1,080 B) **without** the leading pad element
  of every library file (2.2), and the base vertex 0 with the pieces' offsets inside the indices. Like the Great Wall's
  meshes (2.2, 4870 #6040) this is the R32-without-pad form of a buffer the EXE builds itself.
- **The vertices are bent per crossing, not one transform of the file**: a least-squares affine map from the file's
  positions to the captured ones misses by up to 0.257 world units over the whole buffer (median 0.112); per piece the
  best affine map still misses by 0.021-0.061, 3-15 times the half-float step at these magnitudes (0.004 below 8, 0.008
  at 12.5). The pieces' fitted maps differ: across the road the file's x is scaled by 0.0998-0.0999 (0.1 [I]), along it
  the file's y by 0.113-0.125 (stretched to the crossing [I]), each piece turned by its own small angle (about 1-4
  degrees, End2 the least, End1 the most), z scaled by 0.100 and lifted by 12.50 (the deck at file z 0 is at model z 12.50 on every piece). The
  captured outline is not a rectangle (the deck's corners at y -1.44 / -1.52 and 1.10 / 1.02): the span follows a
  curve, most likely the crossing's route curve [I: the route builder 0x5EC250 and the bridge code were not read].
- **One model per crossing in model space**: the positions are centred on the crossing (x -5.8..5.8, y -1.85..1.85, z
  9.52..13.45), the placement in the VS constants (material 3: world rows in VS cb0, landmark-renderer.md 7.2)
  [4871]; road and railroad share the same buffers [4871]. The IND pieces were drawn with the active team in the
  Postmodern era; that ANC is the earlier eras' set is [I] from the names.

**Consequence for this epic (2026-11-18, lead2-0561 #6368, agreed)**: no library file can hold the bridge's buffers,
so a wire key of the library never names it and the identity set keeps the bridge with the EXE, as for W01 469 / 617
(7.6). The bridge goes to child **4879** (CPU-built meshes: copy the immutable buffers at creation, keyed by content).
Bit: the bridge is a material-3 draw (class SINGLE); `shell_model_scene.h` gives SINGLE the fixed bit cities (0x2) and
`shell_model_tap.h` passes a key whose bits lack the class's bit, so a road or rail bridge can only be dropped under
**ServerCities** unless SINGLE took its bit from the identity set as SKINNED does. Decided (lead2-0561 #6368): road and
rail bridges under ServerCities, as their own source (Source Bridge, class CityPiece) when 4879 builds them; the
compositor unchanged; pontoon bridges stay landmark rows under ServerLandmarks.

### 8.3 Without bridge building [4871]

With the active team lacking the bridge-building tech (VP `TECH_ENGINEERING`) the EXE draws nothing at the crossing:
no bridge draw and no other new geometry (P6_road_nobb against P6_none); the route curves end short of the river.

### 8.4 Not done here, not verified

- Steps 2-4 of the ticket (library entry, the cityscene compare, the pontoon rows' check) were not done: the ticket
  stops after step 1 for a CPU-built bridge (4879). The pontoon bridge's three SPECIAL rows were not re-checked in the
  library here.
- The bend rule (how End1 / Mid / End2 are stretched and turned along the crossing, whether Mid repeats on a longer
  span, the deck height 12.50) [I]; the ANC set at earlier eras; a crossing on another player's plot; a pillaged
  route's bridge.
- The textures by content (4871's export has no texture dumps; re-export the capture to compare t0 / t1 bytes).
