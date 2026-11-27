# Landmarks and their model files (`ArtDefine_Landmarks`, `.fxsxml`, Granny `.gr2`)

Written 2026-11-10 by one-off 9012 (offline file work only: no game launched, no process touched, nothing fetched).
Verifies the research session's landmark finding of 2026-11-10 (the basis of decisions D31 and D33) and records
the first byte-level facts about Civ 5's `.gr2` files. Evidence, queries and the scripts used are in
`evidence/9012/` (all scripts are standard-library Python written in this session).

**Result in short.**
- `ArtDefine_Landmarks` has 667 rows, 659 with a model, 514 distinct model strings (508 distinct file names);
  six layout handlers with the counts of the finding [C]. World wonders are **not** in this table (correction,
  section 1.4).
- Every landmark row leads to one `.fxsxml` (an XML list) that names exactly one Granny mesh, optional Granny
  animation files and DDS textures [C]. The files sit in 12 `.fpk` archives, two loose DLC folders and the Vox
  Populi mod folder (143 of 508) [C].
- **Every `.gr2` on this install (12,761 files; 12,760 in the first version of this file, which left the big-endian
  file out; corrected 2026-11-11 by epic 3100) stores every data section compressed with Granny compression type
  2, Oodle1** [C]. No data section anywhere is stored raw. Without a decompressor a `.gr2` gives up only its
  header, section table, pointer-fixup and marshalling tables (section 4.2-4.4).
- **With an open-source Oodle1 decoder (Boost licence, ported to Python here) and an own reader, the files read
  completely without the Granny runtime** [C, 2026-11-10, after D34]: all 21 little-endian sample files parse,
  83 meshes pass an index-range check, vertex counts match the raw marshalling tables exactly, and names, vertex
  formats, skeletons, bone bindings, materials with shader sets and texture file names, and animation curve formats
  come out (section 4.5-4.8). Names in most files are CRC hashes resolved through a per-archive Granny string
  database (`*_stringdatabase.gsd`, one per `.fpk`) [C]. Open: animation curve decoding and playback, one
  big-endian file, and the cost of a production reader in the server.
- The one `[DisableSystems]` switch `LandmarkSystem` covers five objects: landmark models, RoadLayout (route and
  bridge layout), the farm-field builder and city-field manager, and DynamicGeometryVisSystem (section 3, from
  existing files). D33 keeps it on in increment 2.

## 0. Corrections to the finding as received (2026-11-10)

| # | Finding as received | Verified | Status |
|---|---|---|---|
| 1 | 667 rows, 514 distinct models; SNAPSHOT 541, ANIMATED 62, RANDOM 27, FARM 24, SPECIAL 9, ANIMATED_ROTATED 4 | identical on two database copies (section 1.1) | **confirmed** [C]. Addition: 8 rows have a NULL model (659 rows with a model) |
| 1 | "covers improvements, world-wonder and resource models" | no world-wonder row exists; wonders are `WonderArtInfo` entries in `civ5artdefines_wonders.xml` and five DLC files (section 1.4) | **corrected**: improvements, resources and features only |
| 1 | "23 ART_DEF_FEATURE_* rows (natural wonders, fords, atolls) with SNAPSHOT or RANDOM layout" | 31 rows have a ResourceType `ART_DEF_FEATURE_*` (23 distinct feature types) and 3 more rows have an ImprovementType `ART_DEF_FEATURE_HADRIANS_WALL_1..3`: **34 feature rows, 26 feature types** | **corrected** (23 is the count of feature types in the ResourceType column, not of rows) |
| 2 | ANIMATED covers camp, fishing boats, pasture, well, polder and 13 resources; ANIMATED_ROTATED horse and pasture | ANIMATED: camp, fishing boats, pasture (cow, sheep), well (oil, heavy oil), polder (18 of its 36 rows) and resources bison, coral, cow, crab, deer, fish, furs, furs-hill, ivory, lake fish, pearls, sheep, truffles, whale. ANIMATED_ROTATED: horse and super-horse, as resource and as pasture (4 rows, 2 models) | **confirmed**, made exact [C] |
| 3 | SPECIAL covers Moai, Feitoria, pontoon bridge; road bridges are not landmark rows but RoadLayout 0x2B735E8 fed by the bake 0x5DE910 and ToggleBridgeState | SPECIAL rows: Moai 3, Feitoria 3, pontoon bridge 3 (one model for all three states). No model string contains "bridge" except `Pontoon_Bridge.fxsxml` [C]. The RoadLayout facts are in `research/sources/shell-gates/stubs/coupling.md` finding 7 and section 2.3 (ToggleBridgeState is event B47, a Landmark listener) and `stubs/landmark.md` section 3; not re-verified here (no decompile run) | **confirmed** for the table; addresses cited, not re-checked |
| 4 | placement inputs are local to the server; the work is the six layout handlers and TerrainContour | `TerrainContour` is a per-row flag (627 rows 1, 39 rows 0, one row 0.75) [C]; the handlers' code is in the EXE and was not read | plausible [I]; the handlers' code was not read. Reading the model files is solved offline (section 4); drawing them is the larger job |
| 5 | fallback: LandmarkSystem kept on, 30.8 s load cost, depth check | the 30.8 s and -53 MB are measured in `02-exe-internals/load-sequence.md` section 1.1 and `disable-systems-and-masks.md` (2026-11-10 live table); see section 3 here | **confirmed** from the cited files; chosen by D33 for increment 2 |

## 1. `ArtDefine_Landmarks`

### 1.1 Database and method

- Database: `Civ5DebugDatabase.db` of test instance c5h-b,
  `%USERPROFILE%/c5-instances/c5h-b/Documents/My Games/Sid Meier's Civilization 5/cache/Civ5DebugDatabase.db`,
  6,816,768 B, modified 2026-11-10 16:51:49, SHA-256
  `c72d02060586531b8742108d0da742aea25b78da357864fb3833fa4f79dd543d` (built by the game with CP + VP + EUI-compat
  of that instance). Copied to scratch and opened with `file:...?mode=ro`.
- Cross-check: the owner's own cache `Documents/My Games/Sid Meier's Civilization 5/cache/Civ5DebugDatabase.db`
  (6,854,656 B, 2026-11-10 02:05, SHA-256 `c48dbe2e...c7432f`) gives the same totals and handler counts [C].
- Queries and full output: `evidence/9012/landmark_queries.py`, `landmark_queries.out.txt`.
- Schema [C]: `ArtDefine_Landmarks(Era TEXT DEFAULT "Any", State TEXT DEFAULT "Any", Scale FLOAT DEFAULT 1,
  ImprovementType TEXT NOT NULL, LayoutHandler TEXT NOT NULL, ResourceType TEXT NOT NULL, Model TEXT,
  TerrainContour INTEGER DEFAULT 0, Tech TEXT)`; `ArtDefine_LandmarkTypes(Type, LandmarkType, FriendlyName)` has 178
  rows: 66 Improvement, 104 Resource, 8 LayoutHandler (SNAPSHOT, FARM, PASTURE, SPRING, ANIMATED, ANIMATED_ROTATED,
  RANDOM, SPECIAL). PASTURE and SPRING are declared but no row uses them.

### 1.2 Counts [C]

| Handler | Rows | Distinct models |
|---|---|---|
| SNAPSHOT | 541 | 440 |
| ANIMATED | 62 | 20 |
| RANDOM | 27 | 27 |
| FARM | 24 | 18 |
| SPECIAL | 9 | 7 |
| ANIMATED_ROTATED | 4 | 2 |
| **total** | **667** (659 with a model) | **514** |

By what the row is (classification in the query file):

| Class | Rows | Distinct models | Handlers |
|---|---|---|---|
| improvement on a specific resource (mine, quarry, plantation, camp, pasture, ...) | 335 | 263 | SNAPSHOT 292, ANIMATED 23, FARM 18, ANIMATED_ROTATED 2 |
| improvement, generic (`RESOURCE_ALL` / `NONE`) | 219 | 170 | SNAPSHOT 165, RANDOM 21, ANIMATED 18 (polder), SPECIAL 9, FARM 6 |
| resource without improvement (`IMPROVEMENT_NONE`) | 79 | 71 | SNAPSHOT 56, ANIMATED 21, ANIMATED_ROTATED 2 |
| feature (natural wonders, Hadrian's wall, fallout, reef, oasis, atoll, ford, ...) | 34 | 29 | SNAPSHOT 28, RANDOM 6 (5 atoll, 1 ford) |

The largest improvement groups: plantation 126 rows (120 models), mine 108 (71), quarry 54 (42), polder 36 (7),
farm 30 (18). 8 rows have a NULL model: farm on wheat / maize / rice (`Any` and `UnderConstruction`) and Moai
`SNAPSHOT` (`Constructed`, `UnderConstruction`); those states draw no model [I].

Variants [C]: Era `Ancient` 228, `Industrial` 205, `Any` 234; State `Constructed` 175, `Pillaged` 170,
`UnderConstruction` 158, `Any` 164. A typical VP improvement has 3 rows (UC / built / pillaged), a base improvement
with era art 6 (2 eras x 3 states). `Tech` is set on 12 rows (`TECH_ECONOMICS`). `TerrainContour` 1 on 627 rows,
0 on 39 (oil and others), 0.75 on one.

Data quirks seen [C]: Victoria Falls uses `Feature_Sri_Pada.fxsxml`; the four duplicated feature rows (Kailash,
Sinai, Sri Pada, Uluru) and several duplicated resource rows are exact duplicates; `polder_blank.fxsxml` names an
animation file `Polder_Blank_BlankAnimation.gr2` that exists nowhere on the install.

### 1.3 Animated rows [C]

22 distinct models are ANIMATED or ANIMATED_ROTATED; every one of their `.fxsxml` files lists `<Animation>` entries
(41 + 8 references), and no FARM, RANDOM or SPECIAL model does; one SNAPSHOT model (`polder_blank`) does
(`evidence/9012/landmark_assets.out.txt`). The full row list is in `landmark_queries.out.txt`. Models with 4
animations: bison, cow, deer, elephant (ivory), sheep, horse, super-horse, ANC/IND polder; with 1: coral (2 eras),
crab, fish (2 eras), furs, oil rig, super oil rig, pearls (2 eras), truffles, whale (2 eras).

### 1.4 World wonders are not landmark rows

- No `ArtDefine_Landmarks` row and no model path contains "wonder" [C].
- `Buildings.ArtDefineTag` holds the wonder art key (`BUILDING_STONEHENGE` -> `STONEHENGE`) [C], which matches a
  `<WonderArtInfo><Type>` in `civ5artdefines_wonders.xml` (inside `Resource/Common/WonderModels.fpk`, 53 entries,
  each one state `Any`, `fScale` 0.05, `<Granny>` = an `.fxsxml` path) and in five loose DLC files (DLC_03 3,
  DLC_06 3, DLC_07 1, Expansion 8, Expansion2 8 + 9 "Inherited") [C]. The mods folder has no `WonderArtInfo` [C].
- Which system draws wonders is **not** settled by this: `stubs/landmark.md` section 4 says "wonders and
  improvements are" landmarks and lists the DLL producer `GameplayWonderCreated` among Landmark's inputs; that is
  the only source and it was static analysis [I]. If true, wonders also vanish with LandmarkSystem off, but through
  a second art path, not this table. (Note 2026-11-15, 0319: the City system's per-city record holds the display
  slots of wonders and display buildings, and the DLL sends `GameplayWonderCreated` for every building with
  `DisplayPosition > 0`, 58 rows of which 12 are not wonders; which system owns the drawn wonder object is still not
  read. [cities-and-landmarks.md](cities-and-landmarks.md) 1.6.)

## 2. From an art row to files on disk

Chain [C for the file contents, I for the engine's lookup rule]:

1. `ArtDefine_Landmarks.Model` is a path or a bare file name (`Assets/Buildings/Resources/Cow.fxsxml`,
   `Bison.fxsxml`). The engine resolves by file name: the `.fpk` name tables hold bare lower-case names only, and
   all 508 distinct model file names resolve by basename (`evidence/9012/landmark_assets.out.txt`).
2. The `.fxsxml` is a small XML `<Asset>` with exactly one `<Mesh file="X.gr2">`, zero or more
   `<Animation file="Y.gr2" name=... ec=...>`, `<Texture file="Z.dds">` entries (typically 6-25), and optionally
   `<BoneUsage>`, `<DecisionTree file="LandmarkDecisionTree4.ddtxml">` and `<TimedTrigger file="FX_Triggers_*.ftsxml"
   state="3">` (pillaged smoke and similar effects). Example `anc_academy.fxsxml`: 1 mesh, 6 textures.
3. The `.gr2` and `.dds` files are looked up by name the same way. DDS textures are in the `*Textures.fpk`
   archives under `Resource/DX9` (GATE2: shareable / parseable, D13).

Where the 508 landmark `.fxsxml` live (first match; 62 names exist in more than one place):

| Origin | Models |
|---|---|
| `Resource/Common/ImprovementModels.fpk` | 200 |
| `MODS/(2) Vox Populi/Assets/...` (loose) | 143 |
| `Resource/Common/MiscData.fpk` (animated resources and others) | 44 |
| `Expansion1Models.fpk` / `Expansion2Models.fpk` | 28 / 24 |
| loose `Assets/DLC/Shared` / `Assets/DLC/DLC_04` | 15 / 6 |
| `HutsBarbarianFortModels.fpk`, `ConquestDeluxeModels.fpk`, `FeatureModels.fpk`, `Upgrade1Models.fpk`, `PolynesiaModels.fpk` | 14, 13, 9, 8, 4 |

Duplicates matter: `resource_deer.gr2` exists in MiscData (182,620 B), Expansion1Models and Expansion2Models
(180,556 B each) [C]. Which copy wins is the EXE's load order (not established here).

### 2.1 The `.fpk` archive (format re-derived 2026-11-10, matches GATE2's description) [C]

Note 2026-11-10 (task 2901): verified on all 147 packs (`a` is a file time in 115 entries) and the game's lookup order for duplicate names: [asset-pipeline.md section 1](asset-pipeline.md#1-game-files-fpk-packs-loose-file-lookup-dds-textures-task-2901).

`u32 version (6)`, `"FPK_"`, `u16 0`, `u32 count` at offset 10, then per entry `u32 name_len`, the name, padding to
a multiple of 4 (pad bytes are not zero), `u32 a, u32 b` (0 in the files looked at), `u32 size`, `u32 offset`;
payloads stored raw. `evidence/9012/fpk.py` (list / extract). On c5h-b's install: 147 archives, 30,557 entries,
2,373,988,644 B; 12,412 `.gr2` entries (593,884,391 B), 1,388 `.fxsxml`, 10,004 `.dds`
(`fpk_summary.out.txt`). Loose: 106 `.gr2` under `Assets/` (DLC and UI overlays) and 243 under the three mods.

## 3. What the one `LandmarkSystem` switch covers, and what keeping it on costs

Collected from existing files (added 2026-11-10 at the MC's request after D33); nothing here was re-measured.

**What the bit (0x2) covers** [C, `02-exe-internals/world-systems-and-stubs.md` section 3,
`disable-systems-and-masks.md` row 1, `sources/shell-gates/stubs/landmark.md` sections 3-4]:

| Object | What it does | Key addresses |
|---|---|---|
| 0x2D70310 cvLandmarkSimSystem | landmark sim, ~23 queue listeners (farm types, groups 0x582970 / 0x592670 / 0x52D800) | Startup via 0x75CC70, per-frame 0x5524B0 |
| 0xDB13D8 cvLandmarkVisSystem | loads `ArtDefine_Landmarks` (0x636590 -> 0x560340, `cvLandmarkLibrary.cpp:475`, 3.9 MB), instances, render 0x7A6680, shadow pass "LandmarksShadow" 0x559DF0 | art library +0xB58 = 0xDB1F30 |
| 0x2B735E8 RoadLayout listener object | 8 listeners: 0x5DE910 plot route bits, **0x5EC250 route builder -> 0x583CE0 TerrainDecal route curves**, 0x5FD600, 0x49E290, 0x710ED0, 0x450A70, 0x4A3BE0, 0x521B70; fed with river edges by the bake (coupling.md finding 7) and by events RiverTileAdded B67, road created/destroyed B45/B46, ToggleBridgeState B47 | GameView Startup 0x661D10 -> 0x758440 |
| 0xBA20D0 / 0xBA2224 / 0xBA2D40 + 0xB9DDA0 | **farm-field builder 0x748FC0** (`ART_DEF_IMPROVEMENT_FARM` -> layout 0x6016F0), city-field manager init 0x432220 (City needs it), "RoadLayout::GameView::Startup()" 0x4AB360 | GameView Startup 0x465600 |
| 0xC8F908 DynamicGeometryVisSystem | dynamic geometry; per-frame function empty | GameView Startup 0x429F60 |

So "LandmarkSystem" = landmark models + road and railroad decals + bridges + farm fields + city-field stamps +
dynamic geometry; one bit, no finer switch is known [C]. (Note 2026-11-15, 0319: the "city-field stamps" are
direction fields that orient farm fields and city objects, not a terrain flattening; the layout handlers, the library
entry and the city assembly are in [cities-and-landmarks.md](cities-and-landmarks.md).) With `ReloadLandmarkSystem = 1` (CP/VP/EUI) mod activation
restarts the app-level parts regardless of the bit (world-systems-and-stubs.md section 3).

**Cost of keeping it on** [L, from the cited files]:
- Load time: **30.8 s** of a 148.2 s launch (B4 vs B4 + Landmark, unsampled, same night 2026-11-10, huge save
  turn 350; `load-sequence.md` section 1.1 and the levers table: startups -5.7 s, queue-B gap 19.92 -> 1.06 s, tail
  8.02 -> 0.03 s). The bulk is the farm-field layout (0x748FC0 -> 0x6016F0, serial, 121-235 ms per farm group),
  then TerrainDecalSystem geometry; "farm fields, not route curves or improvement models, are the bulk".
- Memory: **-53 MB** private after load with it off (1,676 -> 1,623 MB, B4 vs B4 + Landmark with City off;
  `disable-systems-and-masks.md` 2026-11-10 live table). Largest free block 1,500 -> 1,542 MB.
- Off requires City off too (crash 0x79C33D through the city-field manager) unless the DLL calls 0x432220.

**Can roads go to the server while the models stay in the EXE?** What the files say:
- Not by configuration: roads, railroads, bridges and farm fields are produced by Landmark's own GameView
  listeners (0x5EC250, 0x748FC0) behind the same bit; the separate `Decal` bit (0x10) changed nothing visible
  ("farm fields and rails still drawn", `disable-systems-and-masks.md` row 4) [C/L].
- The decals themselves are stored and drawn by the **TerrainDecalSystem, which lives at `TerrainSystem+0`**
  (`sources/shell-gates/stubs/terrain.md` item 4 and its object table) [C]. Landmark only produces them. In
  increment 2 the EXE's terrain is kept but not drawn (stub c1, view-mask bit 0 cleared). Whether TerrainDecalSystem
  decals are drawn inside the terrain pass, and therefore vanish with it, is **not established** [I: likely, since
  they are terrain decals]. If they vanish, the server has to draw roads, railroads and farm fields in increment 2
  anyway, from DLL route and improvement data it already hosts, while LandmarkSystem keeps producing (unseen)
  decals and drawing the models. If they do not vanish, they draw on the EXE side against the EXE heightmap.
  *Answered 2026-11-11 by epic 3000 (3003) [L], recorded 2026-11-15 by epic 0166: they vanish with the terrain pass
  (roads, railroads and farm fields are not drawn with the EXE's terrain off; the models stay), because they are drawn
  only inside the tile bake (terrain-decals.md 4.1). The server takes them by route (a), a tap of the EXE's own decal
  records (terrain-decals.md 0.2, 9-11), not from DLL route data: their layout is random per session (P6), and
  LandmarkSystem keeps producing them under D33 (P1: every class but Tree_Shadow is in the D33 records). Replaced: "is
  **not established** [I: likely ...]".*
- Bridges: RoadLayout takes river edges from the bake and ToggleBridgeState; how a bridge is drawn (decal or mesh)
  is not established. *2026-11-15, epic 0166 (terrain-decals.md 2.2, 5.3, P9) [L]: **not a decal**: TerrainDecalSystem
  has no bridge key (huge t350: 188 road edges across rivers carry only the route-curve key over the river bed; a live
  road across a river added only route curves).* *2026-11-18, epic 0561 (4871 [L], 4877 [C]): **a model**, drawn
  only when the active team has bridge building: the EXE's fixed file `Bridges.gr2` (ANC / IND End1, Mid, End2 pieces),
  three pieces CPU-built per crossing into one material-3 draw (`landmark-renderer.md` 8, `city-renderer.md` P6).
  Replaced: "Whether stock draws a bridge there as a model is still open."*
- Cheapest check: one live run of the increment-2 configuration (B4 + River + Arrows, Terrain c1) on a save with
  roads and farms, looking for road decals. That is package I2-A's question (D33).

## 4. The `.gr2` files (model-file spike, part B)

### 4.1 Inputs (extracted read-only from c5h-b's install copy to scratch; SHA-256 in `evidence/9012/sample_files.sha256.txt`)

| Role | Files | Archive |
|---|---|---|
| SNAPSHOT world wonder (wonder art, not a landmark row, section 1.4) | `stonehenge.gr2`, `pyramid.gr2` | WonderModels.fpk |
| natural wonder (SNAPSHOT feature) | `feature_fuji.gr2` | FeatureModels.fpk |
| ANIMATED resource | `resource_deer.gr2` + `resource_deer_deeridle01.gr2`, `resource_deer_idle.gr2` | MiscData.fpk |
| improvement with era / state variants | `anc_academy.gr2`, `ind_academy.gr2`, `pl_anc_academy.gr2` | ImprovementModels.fpk |
| RANDOM row | `barbarian_village1.gr2` | HutsBarbarianFortModels.fpk |
| units (mesh, skeleton and animations in separate files) | `warrior.gr2` (+ `_idlea`, `_run`), `spearman.gr2`, `musketman.gr2`, `tank.gr2` (+ `_idlea`), `antiaircraftgun.gr2` (+ `_idlea`, `_run`) | UnitModels.fpk |
| odd one | `cityruins_improved.gr2` (big-endian) + `_idle` | MiscData.fpk |

### 4.2 Header and section table (own parser `evidence/9012/gr2hdr.py`; dumps `gr2hdr.out.txt`) [C]

Field names are this spike's labels; each field was checked against the bytes (total size equals the file size in
every file; the region map tiles the file exactly).

| Offset | Field | Value in these files |
|---|---|---|
| 0x00 | magic, 16 B | `29de6cc0 baa4532b 25f5b7a5 f666e2ee` (little-endian); one file `b595110e 4bb5a56a 502828eb 04b37825` with all fields big-endian |
| 0x10 | header size | 0x1C8 (456) |
| 0x14 | header format | 0 |
| 0x20 | file-format version | **7** |
| 0x24 | total file size | = file size |
| 0x28 | CRC | per file: zlib CRC-32 of bytes 0x68..total size (found 2026-11-11, `model-reader.md` 2.1; checked on all 12,785 files by one-off 9021) |
| 0x2C | section-table offset (from 0x20) | 0x48 (table at 0x68) |
| 0x30 | section count | **8** |
| 0x34 | root type reference (section, offset) | always section 6 |
| 0x3C | root object reference | always section 0, offset 0 |
| 0x44 | type tag | **0x80000031** (12,758 files); **0x8000002E** (3 files: the big-endian `cityruins_improved.gr2` and the little-endian `cityruins_improved_idle.gr2` and `_worked.gr2`, all MiscData.fpk; corrected 2026-11-11 by epic 3100, was "2 files, one of them the big-endian one") |

Section entry, 44 B: compression, data offset, stored size, expanded size, alignment (4), two split offsets, fixup
offset, fixup count, marshalling offset, marshalling count.

Pointer size: **32-bit** [C]: fixup entries are 12 B (`u32 offset in section, u32 target section, u32 target
offset`); every source offset and target in the files tested lies inside the expanded section sizes; pointer slots
are 4 B apart. Marshalling entries are 16 B (`u32 count, u32 offset, u32 type section, u32 type offset`).
Fixup and marshalling tables are **stored raw** (their byte length is exactly count x 12 / 16).

The EXE links **Granny 2.8.21.0** [C: string `2.8.21.0` next to the DLL/header version check, file offset 0x76BEEC
of `CivilizationV_DX11.exe` in c5h-b's install, SHA-256 `04cfdc71...8814c3`], with `granny_oodle0_compression.cpp`,
`granny_oodle1_compression.cpp`, `granny_bink0/1_compression.cpp`, `granny_file_compressor.cpp` and the strings
"Oodle1Decompress unable to alloc %d bytes" and "Substituting Oodle1 compression for obsolete Oodle0 compression".
It also says "File has run-time type tag of 0x%x, which doesn't match this version of Granny (0x%x). Automatic
conversion will be attempted." (the three 0x8000002E files).

### 4.3 Compression: everything is behind it [C]

Survey of every `.gr2` (`evidence/9012/gr2survey.py`, `gr2survey.out.txt`):

| Set | Files | File bytes | Sections stored with compression 2 | Stored bytes | Expanded bytes | Sections stored raw |
|---|---|---|---|---|---|---|
| fpk archives | 12,412 | 593,884,391 | all non-empty sections of 12,411 files (the 12,412th is the big-endian file, also type 2) | 415,941,203 | 916,037,364 | **0** |
| loose install `Assets/` | 106 | 7,292,996 | all | 6,151,523 | 17,032,096 | **0** |
| MODS (CP, VP, EUI-compat) | 243 | 15,266,132 | all | 12,508,821 | 49,958,568 | **0** |

- The compression field is 2 in every section entry, even empty ones and an 8-byte section stored as 48 B.
- Number 2 = Oodle1: first inferred from the EXE's strings (it has an Oodle1 decompressor and treats Oodle0 as
  obsolete), then **confirmed 2026-11-10 by decoding every sample section with an Oodle1 decoder** (section 4.5);
  was [I] in the first version of this file.
- Share of each file behind compression: 86-97 % for meshes, 29-46 % for small animation files (whose fixup and
  marshalling tables are a large part of the file); see the table below.

### 4.4 Per file (`evidence/9012/gr2table.py`, `gr2table.out.txt`) [C]

Section usage pattern seen [C, roles confirmed by the decoded objects of section 4.7]: 0 = main
objects (root object always here), 1/2 = rigid vertex / index data (static landmarks and wonders), 3/4 = deformable
(skinned) vertex / index data (units, the deer), 6 = type definitions (root type always here, its fixups point only
into section 6), 5/7 unused.

| File | Bytes | Behind compression | Sections used, stored / expanded B | Fixups (s0 / s6) | What the raw tables give |
|---|---|---|---|---|---|
| stonehenge.gr2 | 126,240 | 95.4 % | s0 1,062/7,264; s1 110,361/210,360; s2 7,406/14,004; s6 1,572/10,880 | 319 / 129 | rigid mesh, 210 KB vertex data; no counts (no marshalling) |
| pyramid.gr2 | 71,196 | 94.0 % | s1 56,481/285,240; s2 8,324/14,556 | 209 / 109 | same |
| feature_fuji.gr2 | 3,948 | 48.8 % | s1 128/180; s2 48/8 | 61 / 69 | a tiny mesh: 180 B of vertices, 8 B of indices; the mountain itself is not in this model [I: it is terrain] |
| resource_deer.gr2 | 182,620 | 86.7 % | s1 61,817/180,600; s2 4,670/10,304; s3 69,424/165,172; s4 8,757/16,728 | 1,504 / 470 | rigid camp props + skinned animals: 3 marshalled vertex arrays, 2,429 vertices |
| resource_deer_deeridle01.gr2 | 130,556 | 85.9 % | s0 111,852/152,856 | 963 / 240 | animation only (no vertex sections); 222 marshalled arrays [I: curve data] |
| anc_academy.gr2 | 59,276 | 90.2 % | s1 47,348/155,700; s2 3,420/10,356 | 376 / 71 | rigid |
| ind_academy.gr2 | 104,744 | 94.0 % | s1 87,050/302,700; s2 8,461/24,744 | 414 / 71 | rigid |
| pl_anc_academy.gr2 | 77,224 | 92.7 % | s1 63,629/207,360; s2 5,206/13,908 | 362 / 71 | rigid |
| barbarian_village1.gr2 | 159,688 | 97.3 % | s1 141,178/419,340; s2 11,634/26,136 | 253 / 71 | rigid |
| warrior.gr2 | 31,048 | 91.3 % | s3 21,537/58,684; s4 2,627/4,044 | 116 / 70 | **863 vertices x 68 B** (one marshalled array); index section 4,044 B |
| spearman.gr2 | 33,396 | 91.6 % | s3 23,404/60,180; s4 2,752/4,488 | 124 / 70 | **885 vertices x 68 B** |
| musketman.gr2 | 35,816 | 92.3 % | s3 25,596/69,292; s4 2,924/4,584 | 120 / 70 | **1,019 vertices x 68 B** |
| tank.gr2 | 52,168 | 94.5 % | s3 43,036/104,992; s4 3,898/7,472 | 128 / 70 | 2 vertex arrays, 1,544 vertices |
| antiaircraftgun.gr2 | 127,508 | 95.6 % | s3 104,233/283,288; s4 9,401/17,612 | 358 / 70 | 3 vertex arrays, 4,166 vertices |
| warrior_idlea / _run.gr2 | 6,120 / 7,448 | 36.3 / 45.7 % | s0 only | 160 / 64; 170 / 66 | animation files: 47 marshalled arrays each |
| tank_idlea.gr2 | 4,628 | 40.1 % | s0 only | 94 / 60 | 29 marshalled arrays |
| antiaircraftgun_idlea / _run.gr2 | 12,284 / 14,884 | 28.6 / 38.6 % | s0 only | 455 / 64; 481 / 66 | 130 / 132 marshalled arrays |
| cityruins_improved.gr2 | 9,287 | 29.8 % | s0, s1, s6 | 222 / 283 | big-endian, type tag 0x8000002E; the TempModels.fpk copy (4,604 B) is a different file |

The 68-byte stride (expanded section size / marshalled vertex count, exact for all three single-array units) is
the only geometric number recoverable without decompression [C]. Index counts would need the index format (16-bit
is likely, [I]: warrior's 4,044 B would be 2,022 indices). Unit meshes carry no animation; animations are separate
files with no vertex sections (the "skeleton and animations in separate files" case) [C].

### 4.5 Decompression without the Granny runtime (after D34, 2026-11-10)

The owner allowed cloning open-source Granny readers (D34). Cloned into scratch only (`%USERPROFILE%/AppData/Local/Temp/
claude/o9012/clone/`, deleted at the end of the session, nothing committed):

| Repository | Commit | Licence | What it gave |
|---|---|---|---|
| `https://github.com/Norbyte/lslib` | `6f5f6987d10d8485876b8368431c948e4dea4f83` (2026-03-31) | MIT | **No decompressor**: `LSLibNative/granny2wrapper.cpp` loads `granny2.dll` and calls `GrannyDecompressData` for any compressed section. Used only as a reference for the member-type numbers and element sizes (`LSLib/Granny/GR2/Format.cs`). Not built, not run. |
| `https://github.com/Arbos/nwn2mdk` | `b6e67050be87b72403916aab891b4d571c2d7032` (2026-01-14) | repository Apache-2.0; the file used is Boost 1.0 | `nwn2mdk-lib/gr2_decompress.cpp` (330 lines) is an independent Oodle1 decoder, "derived from https://github.com/berenm/xoreos-tools/blob/wip/granny-decoder/src/decompress.cpp", Boost Software License 1.0. Its own `GR2_file` can also call `granny2.dll` (behind `USE_GRANNY32DLL`); that path was not used. Not built, not run. |

No project was built, no package was restored, no binary was downloaded, and no Granny runtime was used anywhere.
The decoder was ported to Python (`evidence/9012/gr2_oodle1.py`, Boost notice kept; Boost 1.0 is compatible with
GPL-3.0-or-later). Everything else is own code: `gr2read.py` (sections, fixup lookup, type tree, generic object
reader, string database), `gr2summary.py`, `gr2summary_all.py`, `gr2checks.py`, `gr2sample_run.py`.

Facts established by running it [C]:
- Compression type 2 is Oodle1: the decoder reproduces every section's exact expanded size, and the decoded type
  trees and objects are internally consistent (below). Corrects the [I] of section 4.3.
- The two "split" fields of a section entry are the ends of the first two of Oodle1's three sub-streams
  (`stop0`, `stop1`, then the section end); each sub-stream has its own 12-byte parameter block at the start of the
  stored data.
- Speed of the pure-Python port: 0.2-0.9 s per mesh file of 30-180 KB on this machine. A C++ port of the same 330
  lines is a few hours of work and should be roughly 100 times faster [I]. Done 2026-11-11 by epic 3100
  (`models/src/oodle1.cpp`); one sweep of all files took 73 s of summed per-file time in C++ against 5,454 s in
  Python (single runs, trust grade C, `model-reader.md` section 4).

### 4.6 Names: the Granny string database [C]

- Files inside a `*Models.fpk` keep no strings. Every string slot (member names in the type tree included) holds a
  32-bit value equal to **`zlib.crc32(s) ^ 0xFFFFFFFF`** (CRC-32 without the final inversion) of the string, and the
  header field at 0x58 holds the string database's CRC (2 in every such file).
- Each of those 24 archives carries `<archive>_stringdatabase.gsd`: an **uncompressed** Granny file (compression 0)
  whose root is `{Strings: ReferenceToArray of String, DatabaseCRC: UInt32 (= 2), ExtendedData}`; e.g.
  `featuremodels_stringdatabase.gsd` 10,956 B, 276 strings; `unitmodels_stringdatabase.gsd` 160,004 B.
- Files in `MiscData.fpk` (112), `EffectData.fpk` (30), part of `Expansion1/2Models.fpk` (61 + 76), the loose DLC
  files and the MODS files have header field 0x58 = 0 and keep their strings inline (pointer fixups to C strings).
- With the right database every string in the sample resolves (0 unresolved in 21 files); six VP mod files need a
  game archive's database (section 4.9).

### 4.7 What each input gives up (`evidence/9012/gr2summary.out.txt`, `gr2checks.out.txt`) [C]

Proof of geometry: in all **83 meshes** of the sample the largest index is exactly vertex count - 1 and the index
count is a multiple of 3; the decoded vertex counts equal the raw marshalling counts of section 4.4 (warrior 863,
spearman 885, musketman 1,019, tank 104 + 1,440 = 1,544, anti-aircraft gun 710 + 1,041 + 2,415 = 4,166, deer
979 + 707 + 743 = 2,429 skinned) and the vertex section sizes (863 x 68 = 58,684 B).

| File | Meshes / vertices / triangles | Vertex format (stride) | Skeletons | Materials -> shader set, textures | Example bounding box (file units) |
|---|---|---|---|---|---|
| stonehenge.gr2 (wonder) | 5 / 3,506 / 2,334: the building 2,498 v, its under-construction variant 972 v, two hex ground decals (4 v each), a tree-cut mesh | rigid (60 B) | 3 single-bone | the building BuildingShader (diffuse and SREF maps); the decals LandmarkShader (base, height and spec maps); the tree cut LandmarkShader_Stencil | the building (-298.7, -293.0, -51.3)..(299.3, 296.3, 202.8) |
| pyramid.gr2 (wonder) | 3 / 4,754 / 2,426: the building 336 v, its under-construction variant 4,408 v, a tree-cut mesh | rigid (60) | 3 | BuildingShader | |
| feature_fuji.gr2 (natural wonder) | 1 / 3 / 1: a single triangle | rigid (60) | one skeleton, 2 bones (the model and an effect bone) | one material (named after another wonder) with a decal height texture | a point at z = -32.3: the model is only an effect anchor; the mountain is terrain [C for the file, I for the terrain] |
| anc_academy.gr2 | 15 / 2,595 / 1,726 | rigid (60) | 14 single-bone + a hex ground decal template | the building BuildingShader (diffuse and SREF maps), the decal LandmarkShader (diffuse, height and spec maps), the tree cut LandmarkShader_Stencil | |
| ind_academy.gr2 | 17 / 5,045 / 4,124 | rigid (60) | 16 | the same shader sets, the era's own textures | one part (-3.5, -4.0, -1.1)..(3.9, 4.2, 4.0) |
| pl_anc_academy.gr2 (pillaged) | 14 / 3,456 / 2,318 | rigid (60) | 14 | | |
| barbarian_village1.gr2 (RANDOM) | 9 / 6,989 / 4,354 | rigid (60) | 8 | three materials: the huts, a camp decal, a tree cut | one hut (-13.3, -11.6, -0.8)..(12.5, 5.9, 8.0) |
| resource_deer.gr2 (ANIMATED) | 11 / 5,439 / 4,505: three skinned animals (buck 979 v, 28 bones bound; does 707 and 743 v, 30 bones) + rigid camp, decal and tree-cut meshes | skinned (68) and rigid (60) | 8, up to 36 bones | animals `BuildingShader_Skinned` (diffuse, SREF, irradiance and two specular maps); camp parts BuildingShader / LandmarkShader | buck (-21.2, -73.6, 0.0)..(21.1, 60.0, 121.2) |
| warrior.gr2 | 1 / 863 / 674 | skinned (68) | one skeleton, 23 bones; 20 bound | UnitShader_Skinned (diffuse and SREF maps, irradiance and two environment cube maps) | (-95.3, -12.5, 0.0)..(53.3, 13.4, 117.9) |
| spearman.gr2 | 1 / 885 / 748 | skinned (68) | 26 bones; 18 bound | UnitShader_Skinned | |
| musketman.gr2 | 1 / 1,019 / 764 | skinned (68) | 24 bones; 21 bound | UnitShader_Skinned | |
| tank.gr2 | 2 / 1,544 / 1,245 (body and treads) | skinned (68) | 14 bones | UnitShader_Skinned | the body (-56.8, -176.2, 0.2)..(56.8, 105.2, 100.9) |
| antiaircraftgun.gr2 | 3 / 4,166 / 2,935 (gun + two crew) | skinned (68) | one skeleton, 72 bones | UnitShader_Skinned | |
| *_idlea, *_run, deer idles | 0 meshes: animation only | | | | see 4.8 |
| cityruins_improved.gr2 (MiscData) | not read: the one big-endian file; the reader handles little-endian only. The C++ reader of epic 3100 also fails on it after the first Oodle1 sub-stream (`model-reader.md` 2.3, 2026-11-11) | | | | |

(The per-model mesh, skeleton, material and texture names of this table were replaced by descriptions, 2026-11-18,
export ruling J3; counts, formats and bounding boxes stay. The names are in the player's own files.)

Vertex formats seen [C]: rigid `Position f32x3, Normal f32x3, Tangent f32x3, Binormal f32x3, TextureCoordinates0
f32x3` (60 B, 72 meshes); skinned the same plus `BoneWeights NormalUInt8 x4, BoneIndices UInt8 x4` after the
position (68 B, 11 meshes). Indices are 16-bit (`Indices16`) in all 83 meshes. All files: exporter "Granny Standard
Exporter, SDK version 2.8.21.0", art tool 3D Studio MAX 12.0, 39.37 units per metre (inches), Z up.

Textures: the root `Textures` arrays are empty; each material map names its texture by the artist's original path
(`D:\projects\artdev\Civilization5\...\Warrior_DIFF.dds`), whose file name matches a DDS in the `*Textures.fpk`
archives and an entry of the `.fxsxml` [C]. Materials also carry the engine's `FireGrafixMaterial` block:
`ShaderSet` (UnitShader_Skinned, BuildingShader, BuildingShader_Skinned, LandmarkShader, LandmarkShader_Stencil),
alpha and Z modes, `SkinBoneCount` 32 and parameter blocks with register numbers (e.g. `UnitShaderSkinnedDynamicData`:
`BoneMatrices` 1,536 B, `TeamColorS`, `AlphaValuesS`) [C]. That is the material-to-shader binding the server needs
to drive the EXE's own shaders (D13).

### 4.8 Animations; rest pose versus playback [C for the file contents]

- Animation files hold `Animations` (name = source .max path, `Duration`, `TimeStep` 1/60 s or 1/90 s,
  `Oversampling` 2 or 3) and `TrackGroups` with one `TransformTrack` per bone: warrior idle 6.0 s, run 0.667 s, 23
  tracks; tank idle 6.0 s, 14 tracks; anti-aircraft gun 72 tracks; deer idles 48.0 s, 9 / 13 track groups (one
  per animal plus camp parts, up to 36 tracks).
- Curve formats used in the sample (header name, `Format` number, degree): `DaIdentity` 2 (deg 0),
  `D3Constant32f` 4, `D4Constant32f` 5, `D4nK16uC15u` 8 (deg 2), `D4nK8uC7u` 9 (deg 2), `D3K16uC16u` 10 (deg 2),
  `D3K8uC8u` 11 (deg 2), `D3I1K8uC8u` 18 (deg 2). The full sweep of 2026-11-11 (`model-reader.md` 6.2) finds 16
  formats, among them `DaK32fC32f` 1, `DaConstant32f` 3, `D3I1K16uC16u` 17, `D3I1K32fC32f` 16 and four scale/shear
  formats, and degree 3 on 202,644 orientation curves. Scale/shear is identity in every track seen. Constant and identity
  curves decode trivially; the five quantised degree-2 B-spline formats need a knot/control decoder and a B-spline
  evaluator. Reading the curve data is done (the arrays are in the object tree); evaluating it is not.
- **Frozen rest pose** needs only the mesh file: skeleton bones (local `Transform`, `InverseWorldTransform`), the
  mesh's `BoneBindings` and the per-vertex bone indices and weights, i.e. one CPU skinning at bind pose, or the
  bind-pose vertices drawn as they are. All of that is read above [C].
- **Playback** adds: the 5 quantised curve decoders, sampling and blending per frame, GPU skinning with up to 32
  bones per draw (`SkinBoneCount`), and the selection logic outside the `.gr2` (`.fxsxml` animation `ec` codes,
  `LandmarkDecisionTree4.ddtxml`, `FX_Triggers_*.ftsxml`).

### 4.9 Systematic sample over all archives

Runs of `evidence/9012/gr2sample_run.py` (full object read plus the index check per mesh; each archive with its own
string database; MODS files with none), 2026-11-10 [C]:

| Run | Files parsed | Failed | Index check failed | Meshes | Vertices | Animations | Unresolved strings |
|---|---|---|---|---|---|---|---|
| every 60th `.fpk` file + every 8th MODS file (`gr2sample_run.out.txt`) | 249 | 0 | 0 | 716 | 272,390 | 182 | 0 |
| every 20th + every 4th (`gr2sample_run20.out.txt`) | 692 | 0 | 0 | 1,411 | 457,613 | 556 | 0 |
| every 5th + every 2nd (`gr2sample_run5.out.txt`, 638 s) | 2,614 | 0 | 0 | 3,606 | 1,304,191 | 2,210 | 5,421 |

The 5,421 unresolved strings are 6 VP mod files that were copied out of game archives and still name string
database 2 (header field 0x58) while the mod ships no `.gsd`: `Figs/grapes_improved*.gr2` (TempModels.fpk's
database), `Figs/pl_plantation_ind|mid_grapes.gr2` (ImprovementModels.fpk's), `Armada/mod_spanish_galleon.gr2`
(ConquestDeluxe or Expansion1). With the union of all 24 databases every one of them resolves (0 unresolved) [C].
All 24 databases carry the same CRC (2), so a reader should load the union of all of them, as the engine presumably
does [I]. 237 of the 243 MODS files keep inline strings. Targeted every-5th runs of Expansion1/2Models, MiscData and
EffectData (885 files) and of the three leader archives (438 files) also had 0 failures and 0 unresolved strings
and of the other 19 archives (1,169 files) also had 0 failures and 0 unresolved strings (`gr2sample_run_exp.out.txt`,
`gr2sample_run_lead.out.txt`, `gr2sample_run_rest.out.txt`); together these cover every `.fpk`. The one big-endian
file was not drawn by any sample.

## 5. The routes for landmark (and unit) models in increment 3

Revised 2026-11-10 after the reader worked. Opus-session-hours, C++ in the server.

| Route | What it needs | Estimate | Main risk |
|---|---|---|---|
| (1) **Parse the files in the server** (recommended) | C++ port of the Oodle1 decoder (Boost) 2-4 h; reader (sections, fixups, type tree, string databases, big-endian swap or a skip list) 6-12 h; meshes / materials to D3D11 buffers and the five shader sets' parameter blocks 10-20 h (shader cbuffer work is GATE2's separate 6-12 h per family); rest pose 3-6 h | **static models + rest pose 25-45 h**; playback +25-45 h (5 curve decoders, sampling, blending, skinning, decision trees and timed triggers) | low for reading (done here on 21 files and the 4.9 sample); remaining risk is in rendering fidelity (the EXE's shaders and their parameters) and in playback logic outside the `.gr2` |
| (2) Read back from the EXE at run time | GATE2's route: staging copies of the buffers plus the EXE's Granny instance metadata | 30-60 h (GATE2), unchanged | metadata reverse-engineering; needs the EXE's model systems running, which increment 3 wants to switch off. Now dominated by route 1 |
| (3) Keep LandmarkSystem on in the EXE | D33's choice for increment 2 | 0 h model work; depth agreement with the server's terrain 3-8 h [I] | 30.8 s load and 53 MB stay; road and farm decals may vanish with the EXE's terrain pass (section 3) |

Route 2b of the first version of this file (copy decompressed sections out of the EXE's Granny) is withdrawn: the
open decoder makes it unnecessary.

**Which part needed an outside open-source reader:** only the Oodle1 decoder (`gr2_decompress.cpp`, Boost) and,
as a cross-reference, LSLib's member-type table. The container, fixups, type tree walk, string database and every
recovered number are own code.

## 6. Not verified

- The big-endian file (`cityruins_improved.gr2` in MiscData.fpk) is not read (nor by the C++ reader, 2026-11-11); the TempModels.fpk copy of the same
  name is a different, little-endian file. Which one the EXE loads is not known.
- Which copy of a duplicated file name the EXE loads (e.g. `resource_deer.gr2` in three archives); the engine's
  basename lookup rule [I].
- That wonders are drawn by LandmarkSystem (only `stubs/landmark.md` says so) [I].
- Animation curves were read but not evaluated; no mesh was rendered, so winding order, units and axis conventions
  against the EXE's world are not checked.
- The EXE addresses in section 3 were copied from the cited files, not re-checked by decompile in this task.
- No game run, so nothing here is [L] except what the cited files measured.

Sources: `evidence/9012/` (queries, outputs, scripts); `research/02-exe-internals/world-systems-and-stubs.md`
section 3; `disable-systems-and-masks.md` (row 1, row 4, 2026-11-10 live table); `load-sequence.md` section 1.1 and
the levers table; `research/sources/shell-gates/GATE2.md` sections 3-4; `sources/shell-gates/stubs/coupling.md`
findings 7-10 and section 2.3; `sources/shell-gates/stubs/landmark.md` sections 3-4;
`sources/shell-gates/stubs/terrain.md` item 4; decision log D2, D13, D31, D33, D34. Outside code: LSLib and nwn2mdk at
the commits above.

## 7. The atoll and the other feature models on the product path (ticket 0389, 2026-11-15)

The owner's report (2026-11-15 22:35 EST, Visual Checks card of 4311, huge save turn 350, camera `s24_jump_90_16`): on the
product path the two atolls at (90,15) and (90,17) are open C shapes, in both stock sessions full rings.

### 7.1 What an atoll is [C]

- Five `ArtDefine_Landmarks` rows, `ART_DEF_IMPROVEMENT_NONE` / `ART_DEF_FEATURE_ATOLL`, handler **RANDOM**, Era and
  State `Any`, **Scale 2.4**, TerrainContour 1, no tech; models `assets/DLC/Shared/Features/Feature_Atoll/Atoll_1..5.fxsxml`
  (database copy of c5h-c, 2026-11-15; `ArtDefine_LandmarkTypes`: `LandmarkType` `Resource`). The RANDOM handler picks
  one of the five rows per landmark with `rand() % 5` (cities-and-landmarks.md 2.3, 2.5), so the variant differs per
  plot and per session.
- Each `.fxsxml` (`Upgrade1Models.fpk`, 128 bytes) names `Atoll_<n>.gr2` and two textures, `FX_Blk_Blk4x4.dds`
  (`BuildingSREF`) and `FX_Atoll_<n>_Base.dds` (`Diffuse`, `Upgrade1Textures.fpk`, 256 x 256 RGBA, alpha 0..255).
- Each `Atoll_<n>.gr2` (about 4 KB) is **one flat quad**: one mesh `Plane01`, 4 vertices, 6 indices (2 triangles), one
  material `Standard_14` with shader set `BuildingShader`, positions x, y = +-17.436, z = 0.25, normal +z, UV (0,0) at
  (-x, +y) and (1,1) at (+x, -y) (the texture's top row lies to +y, north, up the screen at the game's camera). Skeleton
  `World` with bone `Plane01` (translation z 0.25, ScaleShear 0.8718 in x and y). With Scale 2.4 the quad is about 84
  world units wide (a plot is 64), so the atoll reaches over the neighbouring plots. (`models/tools/pyref/gr2canon.py`
  and its reader on the extraction `D:/c5-64bit-work/3100/extract`, read-only.) One part, so TerrainContour culling
  (cities-and-landmarks.md 2.4) never applies: "if the model has exactly one part, stop".
- **The ring is in the texture's alpha, and the variants differ in shape** [C, the five textures looked at]: variant 1
  a reef ring with islets on the left, top right and bottom, the right side reef only; **variant 2 an open square ("R"
  shape) with islets at the top, the left and the bottom right, open on the right**; **variant 3 an open C: islets only
  on the left arc and one white rock at the top right, the right half only a faint lagoon tint**; variant 4 a closed
  ring with islets all round; variant 5 islets at the top, left and right, the bottom faint. So stock itself draws a
  C-shaped atoll whenever it picks variant 3, and an open one with variant 2.

### 7.2 the owner's picture: variant 3, drawn whole [L, 4311's own pictures, 2026-11-15]

`evidence/0389/atoll_census.py` lays the quad (centre = the plot centre, half size 17.436 x 2.4 = 41.85, on the water
plane z 10.1167; the half size with the bone's 0.8718 fits worse, correlation 0.75 against 0.84) over every revealed atoll
plot in a camera's view, unprojects each pixel of its screen box onto that plane, and correlates each variant's alpha
with the picture: in the passthrough frame "differs from the clear colour 84,84,84", in a presented picture R + G - 2B
(sand and green islets against blue water). On 4311's huge `s24_jump_90_16` (build `x64-04b1211b-p0b0dc14df-0169`,
c5h-h, VT `45b3b433...`), atolls (90,15) and (90,17):

| Picture | (90,15) | (90,17) |
|---|---|---|
| stock session 1 (4309) | v4 (0.30, next v5 0.19) | v4 (0.46, next v5 0.18) |
| stock session 2 (4309) | v2 (0.37, next v3 0.31) | v4 (0.46, next v5 0.18) |
| joined, presented (4311 ACCEPTANCE) | v3 (0.22, next v1 0.14) | v3 (0.33, next v2 0.17) |
| joined, passthrough frame (the EXE's own drawing) | **v3 (0.84, next v2 0.72)** | **v3 (0.81, next v2 0.68)** |

- **The EXE draws the whole quad on the product path**: the passthrough frame shows the complete Atoll_3 texture on
  both plots (crops looked at).
- **Nothing of it is lost in the presented frame**: on the quad's opaque pixels (variant 3's alpha >= 0.98; 4,881 and
  3,271 px) the presented joined frame equals the passthrough frame: median 0, p95 2 / 4, max 40 / 35, **99.6 % / 99.1 %
  within 16**. No clip by the composite, its depth write or a mask.
- So the owner's [reworded for privacy] incomplete-looking ring is Atoll_3 itself, picked for both plots in that session. Stock picks per session too
  (session 2 drew the "R" variant 2 at (90,15)). The ACCEPTANCE compare of 4311 masked the model (the passthrough frame
  + 16 px), so neither this nor any other model difference was counted; see 7.5.

### 7.3 Own sessions: stock draws the C as well, the product draws every variant whole [L, 2026-11-15 23:02-23:53 EST]

`evidence/0389/run_0389.py` (4309's driver imported unchanged, own plan: one camera per revealed atoll plot plus the owner's
`s24_jump_90_16`, and one per other feature model) and `chain_0389.sh`: two stock sessions (the 32-bit pin
`i0ship-26234371-ia`, stock config.ini, no proxy, no server) and one joined product session (4311's build
`x64-04b1211b-p0b0dc14df-0169`, checked against its manifest, shipped settings, presented frame plus passthrough frame).
Instances c5h-c (tiny9v stock) and c5h-d (the rest), both VT `45b3b433...` (D94); saves 4309's copies. Pictures, census
and camera blocks: `F:/c5-64bit-work/0389/runs/captures/<map>/<arm>/` (game data, not committed). Variants by
`atoll_census.py` (passthrough frame: correlation 0.53-0.91 with the runner-up at least 0.04 lower; presented pictures
with a score under 0.15 were decided by eye on the crops).

Huge save (turn 350), all 12 atolls (V = visible to the active team, else revealed and fogged):

| Plot | V | stock 1 | stock 2 | product (joined, passthrough) | opaque quad pixels, joined = passthrough within 16 |
|---|---|---|---|---|---|
| (90,15) | V | 2 | **3** | **3** | 99.6 % (4,894 px) |
| (90,17) | V | **3** | 2 | 5 | 99.5 % |
| (94,17) | V | 5 | 1 | **3** | 99.0 % |
| (38,15) | | 1 | 2 | 1 | 97.3 % |
| (14,47) | | 5 | 5 | 5 | 99.8 % |
| (13,51) | | 1 | 1 | **3** | 99.4 % |
| (16,53) | | 2 | **3** | 5 | 100.0 % |
| (19,52) | | 5 | **3** | 2 | 99.6 % |
| (43,59) | | **3** | 4 | 2 | 99.6 % |
| (114,60) | | **3** | 1 | 1 | 97.0 % |
| (118,60) | | **3** | **3** | 2 | 99.7 % |
| (118,64) | | 4 | 2 | **3** | 99.0 % |

- **Stock draws the open C**: in stock session 1 at the owner's own camera (`s24_jump_90_16`, atoll (90,17)), and on 4 of
  the 12 huge atolls in that one session; Tiny9v (21,11) in stock session 2 (session 1 drew Atoll_1 there). Visual Checks
  `vc-20261115-235352-workstation-5805` (stock crops), `vc-20261115-235344-workstation-80aa` (the owner's camera, four columns
  and the five textures).
- **The product's pick changes per session too**: 4311's session drew 3 / 3 at (90,15) / (90,17), this one 3 / 5. So the
  product does not always pick the same variant; nothing on the product path fixes or skews the pick (the EXE's own
  `rand()` in the EXE process, as in stock).
- **Every variant the product picked is drawn whole**: on the opaque quad pixels of all 12 atolls the presented joined
  frame equals the EXE's passthrough frame, 97.0-100 % within 16, medians 0 (the two at 97 % have p95 12-13: the
  quad's edge over the composite's water). The fogged atolls are fully lit on the product path and darkened in stock:
  the white mask's intended difference (fog-of-war.md decision 3 (i)), not this ticket.
- Stock sessions 1 and 2 agree on 3 of the 12 (all fogged plots); with five equally likely variants about 2.4 agreements
  are expected by chance. Whether the stock pick at load is partly reproducible was not examined.

Tiny9v (turn 9, every plot visible; stock on c5h-c, product on c5h-d at 2026-11-15 23:52-2026-11-16 00:00 EST): (25,7)
stock 2 / 4, product 4; **(21,11) stock 1 / 3**, product 4; (24,14) stock 5 / 1, product 2. Opaque quad pixels joined =
passthrough 97.2-99.8 % within 16 on each atoll's own camera; 81.3 % for (21,11) seen near the screen top from the
(25,7) camera, where the presented frames (stock and joined alike) carry a distance haze over the models that the
passthrough frame lacks (looked at; the joined picture there looks like stock's).

### 7.4 The other feature models, checked the same way [L, the same sessions]

`evidence/0389/model_check.py` per revealed feature-model plot in view (forest, jungle, marsh, ice and flood plains are
not landmark models and are skipped): region = the plot's pixels plus the column a 24-unit model can cover; A2 =
presented joined frame against its own passthrough frame on the opaque model pixels (EXE-drawn, and equal within 8
between the passthrough frame and stock session 1); J = joined against stock 1 on the region, C = stock 2 against stock 1
(stock's ceiling). Model rows from 7.1's query: all are SNAPSHOT except the atolls and the ford (RANDOM, one row, so no
choice of model).

| Feature (plot, map) | visible | A2 opaque px, within 16 | J within 16 | C within 16 | Reading |
|---|---|---|---|---|---|
| Mt Kailash (38,12) tiny9v | yes | 550 px, 100 % | 95.2 % | 93.1 % | whole, at stock's level |
| Solomon's Mines (6,22) tiny9v | yes | 29,410 px, 100 % | 63.8 % | 62.2 % | whole, at stock's level (the region is mostly animated water) |
| Oasis (48,9), (43,10), (45,12) tiny9v | yes | 2,555-4,465 px, 100 % | 91.2-100 % | 86.3-100 % | whole, at stock's level |
| Reef (26,8), (25,9) tiny9v | yes | no opaque pixels | 92.3 / 82.6 % | 98.5 / 98.6 % | the reef's ground is the server's terrain and equals stock by eye; the EXE's reef model is only faint animated streaks (passthrough frame), different in every frame; the lower J is that animation and the water phase, not a missing part [I by eye] |
| Kilimanjaro (82,22) huge | yes | 1,522 px, 100 % | 75.8 % | 76.7 % | whole, at stock's level |
| Gibraltar (112,42), Lake Victoria (76,35), Mt Sinai (44,28), Crater (94,46), Volcano (63,40) huge | no | 102-290 px, 96.3-100 % (Volcano: nothing drawn by the EXE) | 74.9-98.2 % | 75.8-99.6 % | whole, at stock's level |
| Potosi (30,33), Oasis (43,23), (49,30), (51,31) huge | no | few opaque px (fog) | 74.1-86.5 % | 86.3-98.7 % | lower J: the white mask (fully lit models on fogged plots, intended, fog-of-war.md decision 3 (i)) |

No feature model shows a missing part. Not covered: the ford (no ford on these saves), Fallout, Hadrian's Wall, El Dorado,
Fountain of Youth, Fuji, Everest, Mesa, Sri Pada, Titicaca, Uluru, Victoria Falls (not on these saves).

### 7.5 The acceptance compare and models

4309's `compare_4309.py` (used by 4311's ACCEPTANCE tables) removes every pixel the EXE draws in the passthrough frame
plus 16 px from every class, so a model drawn wrongly on the product path is never counted; that is how this report came
from the owner's eye, not from a table. Two checks would cover models without being confused by stock's random picks
(proposal posted on 0389's thread, 2026-11-16):
1. **Drawn whole**: on each joined camera, the presented frame against its own passthrough frame on the opaque model
   pixels (A2 above; for RANDOM rows the variant's alpha >= 0.98, as `atoll_census.py`), pass at >= 97 % within 16. It
   catches a clip by the composite, its depth write or a mask, independently of variants and of the white mask.
2. **A `models` class** beside the terrain classes: J against C on the model plots' regions, leaving out plots whose
   row is RANDOM (atolls, ford, goody huts, barbarian camps, Kasbah, Oppidum), fogged plots (the white mask) and P6
   plots; reported, not a gate, until a few acceptance runs show its spread.

Both are in the compare since ticket 0397 (2026-11-16; the paragraph above was the proposal): 7.5.1-7.5.4.

#### 7.5.1 What the compare does now (ticket 0397, 2026-11-16) [C for the code]

`render/evidence/0169/4309/compare_4309.py` calls `tools/compare/model_checks.py` (15 unit tests on synthetic pictures:
`python -I -X utf8 -m unittest discover -s tools/compare -p "test_*.py"`). New options `--drawn-whole gate|report|off`
(default `gate`; `--self-test` sets `off`), `--atoll-tex DIR` (the five `fx_atoll_<n>_base.dds` of an extraction of the
player's install, read at run time; default `D:/c5-64bit-work/3100/extract/fpk/Upgrade1Textures`, read only), `--db FILE`
(a copy of `Civ5DebugDatabase.db`: the RANDOM rows from `ArtDefine_Landmarks`; default a built-in list, the same six:
barbarian camp, goody hut, Kasbah, Oppidum, atoll, ford; `FEATURE_FORD` has no `Features` row in the VP database, so no
plot carries it). Exit **5** when the gate fails. Output: a `models` column in every class table and a "drawn whole" table
per map (per camera: gated opaque pixels, figures, failing blobs with their plots, the atoll variants found, RANDOM 3D
plots in view, the not-gated pixels' figures, the result); per camera in `compare_<arm>.json` under `drawn_whole`.

**Drawn whole (the gate).** Opaque model pixels: the passthrough frame differs from the clear colour by more than 2 in a
channel, eroded 2 px, outside the interface (the compare's rectangles, plus the whole right strip 216 px wide: the
notification icons reach to x 2380 and y 1135, below the compare's right strip), and equal within 8 to **both** stock
sessions (SNAPSHOT models; a part that animates differs between the sessions); plus, for each atoll whose quad lies wholly
on screen, the variant the passthrough frame shows (correlation of the five alphas with "drawn", as `atoll_census.py`) where
its alpha >= 0.98. **Not gated** (their figures are reported): the cover of plots with units or a pillaged improvement
(24 world units) and of city plots up to 80 units (the city banner). Difference per pixel: the smaller of (joined against
the passthrough pixels within 2 px) and, outside the atolls' pixels, (joined against stock session 1) (7.5.3). Pass: >= 97 % within 16 per camera, and >= 75 %
per blob (opaque pixels joined by a 3 px closing) of at least 150 px, so one clipped model is not diluted by the rest.

Why each rule (all from 4311's sets, looked at on crops, 2026-11-16 03:05-04:10 EST; none of them was a clip) [L]:
- **The two frames are taken at different moments.** On tiny9v w2 a warrior at (50,13) has the same pose in stock 1,
  stock 2 and the passthrough frame and another in the presented frame (club swung down; 88 % within 16 pixel for pixel,
  98.4 % with 1 px of movement, 99.1 % with 2 px); a sparkle over the gems moved. With the ticket's rule alone (pt = stock 1
  within 8, pixel for pixel) 4 of 6 tiny9v cameras failed.
- **Units animate by more than 2 px** (cavalry on huge s15 (92,33): 87-89 % per blob with 2 px), and the smoke over a
  pillaged mine (s23 (82,31), 73 %) is an animated particle effect: both left out of the gate.
- **The city banner is translucent interface**: on tiny_0008 c3 the Honolulu banner shows the world through it; the
  presented frame has no forest there (D33) where stock has one, so the banner's pixels differ (73 % per blob).
- **Overlays over models**: the civilisation border line crosses the atoll at (90,15) on s24 in the joined frame and in
  stock alike, not in the passthrough frame (96 % per blob before the stock-1 rule).
- **Sparkles over gems** (tiny_0008 c2 (50,14)) still reach 82.6 % per blob with every rule above: hence 75 % per blob.
  A planted clip of half a blob gave 29-55 % on the same blobs (a scratch probe, not kept).

**Models class.** The model plots' regions (each plot's pixels plus the screen column a 24-unit model covers), revealed
and visible plots with a feature model (not forest, jungle, marsh, ice, flood plains), an improvement, a resource, a city or
a unit; a pixel that also lies in a left-out plot's region (RANDOM row, fogged, P6) is left out. Not masked by the
EXE-drawn pixels and not by the tree mask (stock's own ceiling carries the trees' sway).

#### 7.5.2 4311's sets, the final arm (2026-11-16 03:52-04:24 EST, `F:/c5-64bit-work/0397/final2`; huge again after the atoll fix of 7.5.3, 04:31-04:56, `final3`, the same figures; logs in `F:/c5-64bit-work/0397/logs`) [L]

Command (from the worktree; data: 4311's `F:/c5-64bit-work/0169/4309`, build `x64-04b1211b-p0b0dc14df-0169`, read only):

    python -I render/evidence/0169/4309/compare_4309.py --arm final --maps tiny9v,tiny_0008,huge \
      --atoll-tex <copy of the five textures> --db <copy of c5h-h's Civ5DebugDatabase.db> --out <scratch>

Exit 0: **drawn whole PASS on all 33 cameras.**

| Map | cameras | gated opaque px per camera | pooled median / p95 / max, within 16 | lowest camera | not-gated px, lowest camera |
|---|---|---|---|---|---|
| tiny9v | 6 | 2,025-51,515 | 0 / 3 / 107, 99.9 % | 99.8 % (w2, x1) | 99.5 % (x1) |
| tiny_0008 | 5 | 6,029-17,373 | 0 / 2 / 115, 99.6 % | 99.3 % (c2) | 93.8 % (c3: the banner) |
| huge | 22 | 3,220-311,477 | 0 / 0 / 179, 100.0 % | 99.6 % (s08) | 99.3 % (s23) |

Atolls on the owner's camera `s24_jump_90_16`: (90,15) and (90,17) found as variant 3 (correlation 0.84 / 0.81, runner-up v2
0.72 / 0.68), 8,152 opaque atoll pixels gated, the camera 99.9 % within 16; (94,17) is not wholly on screen. These are
0389's figures (7.2: 4,881 and 3,271 px) through the new code.

Models class, pooled over all revealed plots (J = joined vs stock 1, C = stock 2 vs stock 1; median / p95 / max, within 16):

| Map | pixels | J | C (stock's ceiling) |
|---|---|---|---|
| tiny9v | 2,036,358 | 1 / 63 / 247, 80.9 % | 0 / 46 / 245, 85.9 % |
| tiny_0008 | 2,088,845 | 5 / 79 / 239, 77.5 % | 1 / 44 / 241, 82.9 % |
| huge | 7,261,612 | 3 / 85 / 255, 70.2 % | 1 / 53 / 248, 78.4 % |

J lies 5-8 points under stock's ceiling. Not named yet: the regions take in the ground around each model (the terrain
classes' own differences, among them the missing forest under D33 and the shadows (0312)) and every unit's animation; a
reported class, as the ticket says, until a few acceptance runs show its spread.

#### 7.5.3 Planted cases (`evidence/0397/plant_0397.py`) [L]

One camera, the owner's `s24_jump_90_16` on huge, copied per case to `C:/c5-64bit-work/0397/planted/<case>/` (scratch, removed
at hand-back; the driver's JSON kept in `F:/c5-64bit-work/0397/planted/`), one change, then `compare_4309.py --only s24_jump_90_16` (2026-11-16 04:57-05:01 EST):

    python -I research/05-graphics-composition/evidence/0397/plant_0397.py --map huge --cam s24_jump_90_16 \
      --atoll-box 1234,426,1619,671 --model-box 2201,959,2338,1126 --compare-args "--atoll-tex <dir> --db <db copy>"

| Case | changed | exit (expected) | drawn whole, camera | failing blobs (px, % within 16) |
|---|---|---|---|---|
| none (control) | 0 px | 0 (0) | 42,597 px, 0 / 1 / 28, 99.94 % | - |
| clip_atoll: the left half of what the EXE draws in atoll (90,17)'s box replaced by the box's water | 16,257 px | **5 (5)** | 0 / 43 / 162, 88.28 % | six: 2,411 px 3.2 %, 1,006 px 0 %, 661, 458, 207 px 0 %, 426 px 46.7 % |
| clip_model: the right half of the mine at (93,14) (SNAPSHOT) replaced by the world | 8,388 px | **5 (5)** | 0 / 18 / 94, 94.65 % | 446 px 50.0 % |
| variant: stock 1's atoll box replaced by stock 2's (another RANDOM pick) | 94,325 px | **0 (0)** | 42,758 px, 99.93 % | - |

Plant driver exit 0 (4 of 4 as expected). The models class (J within 16) moved 78.60 -> 78.65 % in the variant case (the
atoll's box overlaps neighbouring model plots' regions; the atoll plot itself is left out) and to 77.96 / 76.69 % in the
two clips. `compare_4309.py --self-test --maps tiny9v,tiny_0008` (05:02): "2 maps, 11 cameras compared, 0 problems", exit 0
(the `models` column of the joined arm equals the ceiling's, as the other classes). Output of every step:
`evidence/0397/acceptance.txt`; the crops looked at are in Visual Checks (issue 0397, eight `info` entries, 04:28) and
`F:/c5-64bit-work/0397/look/`.

Two things the planted cases taught [L]:
- **The stock-1 rule must stay off the atoll's pixels.** In the first version the stock-1 rule (7.5.1) applied
  everywhere; a clip of atoll (90,17) passed at 99.65 %, because stock session 1 drew another variant whose water lies
  where the product's islets were. Since 04:40 the rule is limited to SNAPSHOT pixels (`stock1_where`; unit test
  `Atolls.test_variant_found_and_drawn_whole`).
- **A clip of a translucent part is not seen.** Clipping the right half of the box (Atoll_3's right half is only a faint
  lagoon tint, 7.1) changed 7,595 px and passed at 99.4 %: only opaque pixels (alpha >= 0.98) are gated. So the driver clips
  the islet half (`--atoll-side left`, the default).

#### 7.5.4 Limits of the gate [I unless marked]

- A unit, a pillaged improvement or a city banner that the product clips is not gated (its figures are in the table's
  "not gated" column). The same composite, depth and mask paths draw them as the gated models, so a clip there would be
  expected to show on gated models too.
- Only opaque pixels are gated: a clip of a model's translucent parts (an atoll's lagoon tint, edges, glows) passes
  (7.5.3) [L].
- A clip of less than a quarter of one model blob passes the blob gate; it fails only through the camera's 97 % when it
  is large against all opaque model pixels of the camera.
- RANDOM 3D rows (goody huts, barbarian camps, Kasbah, Oppidum) count only where stock session 1 happens to show the
  same model (listed per camera as "RANDOM 3D plots").
- A joined arm without a passthrough frame at a camera fails the gate ("not checked"): acceptance chains must keep taking
  it (4309's `run_4309.py` does).

### 7.6 Not verified

- Which stock `rand()` call order gives a variant; whether stock's pick at load is partly reproducible (7.3, last point).
- The ford, and the feature models not on these saves (7.4).
- The distance haze over models near the screen top (7.3, Tiny9v): who draws it on the product path was not traced; the
  joined frame looks like stock's there.
- The drawn-whole gate (7.5) has caught only planted clips; its thresholds (2 px movement, 75 % per blob, 80 units of
  banner) come from one build's 33 cameras (4311). The 0389 Tiny9v haze case (81.3 % for an atoll near the screen top) is
  not among 4311's cameras: no camera there shows a whole atoll near the top.
