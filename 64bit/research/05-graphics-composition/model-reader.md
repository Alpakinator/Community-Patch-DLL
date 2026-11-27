# The C++ model reader (`models/`, epic 3100): format facts, sweep, pictures, playback notes

Started 2026-11-10 by epic 3100 (team-3100). Builds on `landmarks-and-models.md` (one-off 9012), which holds the
first byte-level facts; this file holds what the C++ library established and what the playback epic needs.
Evidence in `evidence/3100/`. Offline work only: no game instance was run.

**Result in short (2026-11-11).**
- `models/` is a standalone x64 static library (`c5models`, C++14, clang-cl + MSVC 14.4x STL, `/MD`, `/W4 /WX`)
  that reads every little-endian `.gr2` of the install without the Granny runtime: 12,760 of 12,761 files, and
  its canonical summary equals one-off 9012's Python reader on **all 12,760** (section 4) [C].
- The one big-endian file is refused with a clear error; its decoding is not solved (section 2.3) [C].
- The art rows resolve to files for landmarks (659 rows), wonders (76 types) and unit members (367): every `.fxsxml`
  and every mesh found; the few misses are missing files in the game data, each named (section 3) [C].
- 11 required rest-pose pictures plus 6 checks, all recognisable; the C++ path and the Python path give
  pixel-identical pictures (section 5) [C]. Fidelity to the game's own rendering is **not** reproduced or verified.

## 1. Library and build

Build from a fresh checkout: `python models/build.py --test` (build tree `D:/c5-64bit-work/3100/build/Release`;
COMMON rule 16 gate inside). Layout and the canonical-summary format: `models/README.md`. Public API:
`models/include/c5models/gr2.h` (reader) and `art.h` (art resolution). The library never opens archives: every
entry point takes bytes (`FileProvider` for art resolution). **Join point with epic 2900:** the server implements
`c5m::FileProvider` (lower-case basename -> bytes, origin, duplicates) over 2900's FPK reader and hands the bytes
to `read_gr2()`; nothing else in `models/` touches files.

| Layer | Source | What it does |
|---|---|---|
| 1 header | `src/gr2_file.cpp` `parse_header` | magic (LE/BE), header and section table, ranges of every section / fix-up / marshalling table, **file CRC** |
| 2 Oodle1 | `src/oodle1.cpp` | port of nwn2mdk `gr2_decompress.cpp` (Boost 1.0 notice and origin kept; Arbos/nwn2mdk b6e67050, derived from xoreos-tools), every read, back-reference and write bounds-checked |
| 3 objects | `src/gr2_file.cpp` `RawFile`, `Object`, `Array` | fix-ups looked up (sorted per section, not written into data), type tree cached with cycle check, typed access with byte swap for BE, string slots through `StringDatabase` (union of `.gsd`), `walk_all()` for the unresolved-string count |
| 4 model | `src/gr2_model.cpp` `read_gr2` | meshes (vertex format, vertex bytes LE, indices 16/32, groups, material and bone bindings), skeletons (Transform, InverseWorldTransform), models (InitialPlacement, mesh bindings), materials (maps, texture basenames, ExtendedData incl. ShaderSet), track groups and curves kept raw (format, degree, scalars, arrays), animations |
| 5 checks | `src/checks.cpp` `check_geometry` | vertex bytes = count x stride, index count multiple of 3, indices < vertex count, groups inside the index buffer, finite positions and normals, bone weights sum to 255 +- 2, weighted bone indices < bindings, bindings name bones of the model's skeleton, parent index < own index |
| summary | `src/summary.cpp` | the canonical JSON line shared with `models/tools/pyref/gr2canon.py` |
| art | `src/art_*.cpp` (worker 3104) | section 3 |

Tests (`ctest`, 2026-11-11 01:33, all pass): `test_gr2` (synthetic Granny file built byte by byte + real files when
`C5M_SAMPLES` is set; output `evidence/3100/sweep/test_gr2.out.txt`), `test_art` (177 checks), `test_modelshot`
(39 checks, renders on a desktop the test creates). Negative tests, one per check, each failing with its own code:
truncated header and truncated body (`Truncated`), magic (`BadMagic`), version (`BadHeader`), one flipped byte
(`BadCrc`), section outside the file (`BadSectionTable`), compression 1 (`UnsupportedCompression`), a fix-up target
outside the file (`FixupOutOfRange`), a wrong fix-up that makes an array overrun its section (`ObjectOutOfRange`),
an unknown member type (`TypeTreeCorrupt`), a model file given as `.gsd` (`BadStringDatabase`), an index past the
vertex count, a NaN position, wrong vertex byte count (`GeometryCheck`); Oodle1: a stream shorter than its
parameter blocks, sub-stream ends out of order, 300 random streams (all refused, none crashes), 40 one-byte
corruptions of the warrior's section 0 with the CRC recomputed (40 of 40 refused as `DecompressCorrupt`), a wrong
expanded size (`DecompressCorrupt`/`DecompressSize`).
Independent verification (one-off 9021, 2026-11-11, `model-reader-verification.md`): the claims hold, but crafted
files still crash the reader above the decoder (stack overflow in the type tree, null read and out-of-bounds read in
`check_geometry`, uncaught `bad_alloc`; defects V1-V4 there).

Tools: `gr2sweep` (sweep, section 4), `modelshot` (pictures, section 5), `artresolve` (section 3).

## 2. Format facts established by the C++ reader

### 2.1 The header CRC [C, 2026-11-11]

Header field 0x28 is zlib's CRC-32 (polynomial 0xEDB88320) of the file's bytes from **0x68** (the end of the
header block) to the total size. Found on `warrior.gr2` and `tank.gr2` (nwn2mdk's `check_crc` uses the same CRC from
`sizeof(Header)`, which is 0x58 there and does not match these files); then checked on every file: `parse_header`
refuses a mismatch and all 12,761 files pass it, the big-endian one and the 24 `.gsd` included. A reader therefore
detects any corruption of compressed data before decoding. The CRC does not cover the header block itself (bytes 0-0x67: section
count, root references, string-database field); those are guarded only by range checks (one-off 9021,
2026-11-11, `model-reader-verification.md` 3).

### 2.2 Member names the code depends on [C]

From the files' own type trees (warrior.gr2, printed with 9012's reader): `Bone {Name, ParentIndex, Transform,
InverseWorldTransform Real32[16], LODError, ExtendedData}`; `Model {Name, Skeleton, InitialPlacement, MeshBindings,
ExtendedData}`; `BoneBinding {BoneName, OBBMin[3], OBBMax[3], TriangleIndices}`; `TriTopology {Groups, Indices,
Indices16 {Int16 Int16}, ...}` (16-bit indices are declared **signed** Int16; the C++ reads them unsigned);
`Mesh {Name, PrimaryVertexData, MorphTargets, PrimaryTopology, MaterialBindings, BoneBindings, ExtendedData}`.
A first C++ version used the names `LocalTransform` / `InverseWorld4x4` and left every bone at identity without an
error; the canonical summary does not carry bone transforms, so the sweep could not see it, the pictures did (rest-
pose deviation 0 instead of ~1e-6). `test_gr2` now checks bone transforms of the warrior.

### 2.3 The one big-endian file is not decodable (2026-11-11) [C]

`MiscData.fpk/cityruins_improved.gr2` (9,287 B, magic `b595110e...`, type tag 0x8000002E, header 0x58 = 0):
- Its header, section table, fix-up tables (big-endian) and its file CRC are valid.
- Its Oodle1 parameter blocks are big-endian words: read so, they give sensible parameters (decoded_value_max 256,
  backref_value_max 262,143); `oodle1_decompress(..., big_endian)` reads them so.
- But the stream does not decode cleanly with the open decoder: section 0 starts with back-references reaching
  before the section start, and blocks of sections 0 and 1 end 7-10 bytes past their sub-stream ends. Read
  permissively (zeros before the start, output cut to size, as 9012's Python port silently does), section 6 (the type
  tree) and the first sub-stream of section 0 look right (BE member types 2, 2, 8, 4 ... of the file-info root;
  root arrays at the 32-bit offsets 12-84 with counts 1), but everything after `stop0` comes out as zeros: the member
  names (fix-ups point at 0:2948-0:4406) are empty, so no member can be found.
- So the decoder differs for this (older, 0x8000002E, big-endian) variant after the first sub-stream [I: the
  range coder may restart per sub-stream there]. The library refuses the file with `DecompressCorrupt ... (big-endian
  file: not decodable ...)`. Its namesake `TempModels.fpk/cityruins_improved.gr2` (4,604 B, little-endian, 1 mesh of
  4 vertices, `SimpleShader`) reads; which of the two the EXE loads is not known.
- Type tag 0x8000002E is carried by **3** files, not 2 (correction to `landmarks-and-models.md` 4.2): this one and
  the little-endian `MiscData.fpk/cityruins_improved_idle.gr2` and `cityruins_improved_worked.gr2`, which read.

### 2.4 Other facts from the sweep [C]

- 12,761 `.gr2` on c5h-b's install copy: 12,412 in 26 archives, 106 loose under `Assets/`, 243 under MODS. 9012's
  "12,760" left the big-endian file out (worker 3102). No Oodle1 block of any little-endian file crosses its
  sub-stream end and no back-reference reaches before its section start.
- 24 `.gsd`, 29,214 strings in their union, **0 CRC collisions**; EffectData.fpk and MiscData.fpk have no `.gsd`
  and their 141 readable files keep inline strings.
- 16 curve formats occur (not 8): table in section 6.2 / `evidence/3100/sweep/cpp_counts.txt`; 13 vertex formats,
  one of them the empty format of meshes without vertex data (note by one-off 9021, 2026-11-11);
  16-bit indices in 14,353 meshes, 32-bit in 258; no morph targets anywhere; largest skeleton 1,097 bones
  (`huns_atilla.gr2`).
- `mods:(2) Vox Populi/Assets/Models/Units/Dhanurdhara/EthiopiaCompBow.gr2` has 15 vertices with NaN or infinite
  positions (mesh `Material.001`), and its index buffer uses them.

## 3. The art resolution: art rows to files

Worker 3104, 2026-11-11 00:00-00:17. Code `models/include/c5models/art.h`, `src/art_*.cpp`, tool `artresolve`,
evidence `evidence/3100/art/` (commands, the three examples, `--all`, the `.fxsxml` survey, a texture cross-check).
Database: copy of c5h-b's `Civ5DebugDatabase.db` (SHA-256 `c028ca2b...`, 2026-11-10 22:29; same row counts as 9012's
`c72d0206...`), opened read-only and immutable through the system's `winsqlite3.dll` (`LoadLibraryEx` from
System32); a TSV fallback (`dump_tables.py`) gives a byte-identical result. Roots in the order extraction (all
archives, alphabetical), install `Assets/`, MODS: an assumption [I].

Chains [C]: landmark `ArtDefine_Landmarks(ImprovementType, ResourceType, Era, State) -> Model -> .fxsxml`; wonder
`Buildings.ArtDefineTag -> WonderArtInfo.Type` (7 XML files: `civ5artdefines_wonders.xml` in WonderModels.fpk, the
loose DLC_03/06/07, Expansion, Expansion2 and Expansion2 "Inherited") `-> <Granny> .fxsxml`; unit
`Units.UnitArtInfo -> ArtDefine_UnitInfoMemberInfos(UnitInfoType, UnitMemberInfoType, NumMembers) ->
ArtDefine_UnitMemberInfos.Model -> .fxsxml`; then `.fxsxml -> <Mesh>, <Animation file ec>, <Texture>,
<DecisionTree>, <TimedTrigger>, <AnimGraph>, <StateMachine>`.

| Example | Result |
|---|---|
| `ART_DEF_IMPROVEMENT_ACADEMY` / `ART_DEF_RESOURCE_ALL`, Ancient Constructed | `anc_academy.fxsxml` + `.gr2` (ImprovementModels), 6 textures (5 ImprovementTextures, `generic_grey_8.dds` MiscTextures), SNAPSHOT, scale 0.75 |
| `BUILDING_STONEHENGE` | tag `STONEHENGE` -> `civ5artdefines_wonders.xml` (fScale 0.05) -> `stonehenge.fxsxml` + `.gr2` (WonderModels), 6 textures, `wonder_decisiontree.ddtxml` |
| `UNIT_WARRIOR` | `ART_DEF_UNIT__WARRIOR` -> 3 member infos (`_V2` x3, `WARRIOR` x6, `_V4` x3, scale 0.14), each `.fxsxml` + `.gr2` in UnitModels, 31 animations each, `fx_triggers_warrior.ftsxml` |

`--all`: landmark rows 659 (638 complete), wonder types 76 (74), unit member infos 367 (354); **no `.fxsxml` and no
mesh missing anywhere**. Misses, all absent from every archive and loose folder: animation
`polder_blank_blankanimation.gr2` (9012) and `u_assyrian_siege_tower_charge_attack.gr2`; textures named `.psd`
(`el_dorado.psd`, `uluru_decal_diff.psd`; a `.dds` of the same stem exists for the latter), `olive_trees_pil_diff.dds`,
`sunlight_africa.dds`, `ufizzi_grnd_h.dds`, 10 unit textures (e.g. the typo `155mmdebangesref.dds` for
`155mmdebangsref.dds`); trigger `fx_triggers_resource_whale_breach.ftsxml`. `.dge`/`.fsmxml` were not extracted
(274 / 67 in the archives) and are reported as unchecked, not missing.

Texture binding [C for the counts, I for the rule]: over 940 (`.fxsxml`, mesh) pairs, 655 agree exactly; 176 meshes
use a material texture the `.fxsxml` does not list (all 154 names exist); 234 list textures no material uses. The
renderer should bind by the `.gr2` material maps; the `.fxsxml` list is a preload hint.
**Corrected 2026-11-15 15:20 (epic 0041, task 4356, `unit-renderer.md` 10.8) [L]:** neither the material maps nor
the `.fxsxml` decide: the EXE binds the textures named in the material's `ExtendedData` FireGrafix parameter set
`UnitShaderTextures` (`ParamNames` -> `ParamStoreIdx` -> `ParamDataT`), which VP art changes while the maps keep stock
names (and FBX exports have no maps). 515 of 515 drawn catalogue meshes' t0 and t1 equal the capture under that rule.
(Replaced, 4352's note of the same day: "a map name the member's `.fxsxml` does not list is drawn with the
`.fxsxml`'s texture of that role", 386 / 413 of 477; and before it "bind by the `.gr2` material maps; the `.fxsxml`
list is a preload hint".) The module's loader (`render/src/units/
model.cpp`) reads unit `.gr2` files with the union of every `*.gsd` of the install and mods (`loadStringDatabases`),
takes Real16 Normal / TextureCoordinates0 (seven DLC unique units) by widening them, and applies the file's basis
(pose.h `fileBasis`) to Position and Normal. (Replaced: "the `.fxsxml` list is a preload hint" as the whole rule.)

Duplicates (same basename in several places) are common: 1,731 names fpk-over-fpk (98 of different size), 52
fpk-over-loose-install (51 different), e.g. `cow.gr2` MiscData 137,524 B vs `Assets/DLC/Shared` 139,012 B. The
order chosen here makes the larger, probably newer loose copies lose; `research/03-dll-engine-boundary/server-host.md`
(script VFS: DLC by package priority, mods over DLC) suggests the opposite. **The EXE's mount order is not known**
and decides which model is drawn.

## 4. The sweep over every `.gr2` file

Inputs: extraction of c5h-b's install copy by worker 3102 (`models/tools/pyref/extract.py`, 98 archives, 24,063
entries, 1,636,207,025 B to `D:/c5-64bit-work/3100/extract`, manifest of 12,761 `.gr2`). Reference: one-off 9012's
Python reader through `models/tools/pyref/gr2canon.py` (12 processes, 324 s wall, 2026-11-10 23:51-23:56).
C++: `gr2sweep --manifest ... --jobs 12` through `quietlock.py run`, 2026-11-11 01:31 (second run; warm file cache:
6.3 s wall, 73 s summed per-file time; the first, cold run took 86 s wall). Single runs on a shared machine, trust
grade C (`docs/briefs/PERFORMANCE.md`); not a speed measurement.

| | Python reference | C++ |
|---|---|---|
| files | 12,761 | 12,761 |
| parsed | 12,760 | 12,760 |
| errors | 1 (big-endian, "not supported") | 1 (big-endian, `DecompressCorrupt`, section 2.3) |
| canonical summaries equal (`compare.py`, floats as float32) | | **12,760 of 12,760** |
| meshes / vertices / triangles | 14,656 / 5,812,896 / 5,348,659 | identical |
| unresolved strings | 0 | 0 |

The count tables (`evidence/3100/sweep/cpp_counts.txt`, `py_counts.txt`) are identical line for line except the
error text. Each line compares section sizes and CRC-32 of every decompressed section (so the two Oodle1 decoders
agree byte for byte), materials and maps, skeletons and parents, models, meshes (count, stride, format, indices,
largest index, groups, bone bindings, materials, bounding box) and track groups with curve-format counts. Not in
the summary: vertex and curve bytes beyond the section CRC, bone transforms (section 2.2).

Geometry checks (`evidence/3100/sweep/cpp_checks.tsv`): **every game file passes every check** (12,517 readable
archive and loose-install files). 18 files fail, all VP mod files: 16 bind meshes to a bone name that is not in
their model's skeleton (`Point01`, `BONE`, `FX_firepit_UpperLeft`, `Decal` ...: Aplekton, Eki, Embassy, Kampong,
Kuna, Lalibela, Villa), `Embassy.gr2` has 359 vertices whose weights do not sum to 1, `EthiopiaCompBow.gr2` has 15
non-finite positions. These are data faults of the mod files, reported, not reader faults.

## 5. Rest-pose pictures (`modelshot`)

Worker 3103, 2026-11-11 00:00-01:17; regenerated with the C++ reader by the coordinator 01:31. Code
`models/tools/modelshot/` (D3D11 **WARP** offscreen, no window, no swap chain; own HLSL: diffuse x (ambient +
Lambert), alpha test; own DDS reader; WIC PNG; every run on a hidden desktop through `run_hidden.py`). Pictures
and `pics.txt` (per picture: file, meshes drawn and skipped, textures, triangles, rest-pose deviation, command) in
`evidence/3100/pics/`.

| Required | File | Recognisable |
|---|---|---|
| improvement x3 | `anc_academy.gr2`, `ind_mine_iron.gr2`, `lumbermill_mid.gr2` (ImprovementModels) | yes |
| resource x2 | `resource_deer.gr2` (skinned buck and doe), `resource_elephant.gr2` (MiscData) | yes |
| wonder | `stonehenge.gr2` (finished state, HB* skipped) | yes |
| natural wonder | `feature_uluru.gr2` (Expansion1Models); every FeatureModels.fpk model is a 2.5-11 KB effect anchor like `feature_fuji` | yes |
| units x3 | `warrior.gr2`, `tank.gr2`, `knight.gr2` | yes (bind pose: weapons on their own bones) |
| city | `euro_anc_city.gr2`, model `Mid_Euro_Anc_City02` (the file holds 20 blocks) | yes |

**Both readers give pixel-identical pictures** (all 17, SHA-256 of the decoded pixels, `make_pics.py --compare`):
the full chain - Oodle1, fix-ups, vertex formats including Real16, skinning data, materials and texture names -
agrees between the C++ library and 9012's Python reader on these files [C].

Rules established by the pictures (full text at the top of `pics.txt`) [C for the data, by picture for the rule]:
bone world `W = L x W[parent]` (row vectors; L = scale-shear, rotation, translation) with the model's
`InitialPlacement` as the root's parent; rigid meshes are in their bound bone's space (`vertex x W[bone]`); skinned
meshes are in model space (`InverseWorld x W` is the identity at rest: largest deviation 2.1e-6 in 3x3 and 1.9e-4
units, knight 1.0e-4 / 0.0117 units); `LandmarkShader_Stencil` (tree cut) and `LandmarkShader_Shadow` groups are not
drawn as colour; one file can hold several states (HB*/PL* meshes) or variants (city blocks); the rest pose of
animated resources (sheep, whale) is not a usable picture because their places come from the idle animation.

Comparison with the game: by eye against epic 3000's stock screenshot `D:/c5-64bit-work/i2-ground-truth/tiny/
shot_v2_city_zoomin.png` (2026-11-11 00:46): the warriors there (yellow tunic, headband, club) and the industrial
mine on the snowy hill (headframe, conveyor, ore piles) match `unit_warrior.png` and `improvement_ind_mine_iron.png`
in shape and texture; the Pyramids are in the shot but were not drawn here. No pixel comparison: the EXE's shaders
(SREF/specular, irradiance and environment maps, team colour, decal blending, fog of war), its lighting and its
camera are not reproduced.

Why stock models could come out untextured dark red with fog textures missing (MC question of 2026-11-11, from the
file formats only) [I]: no unit, improvement or wonder material in the `.gr2` files names a fog texture (map usages,
`evidence/3100/sweep/cpp_counts.txt`: `FOWColorKey` occurs only in 20 leader-scene materials). The fog-of-war and
lighting inputs of `UnitShader_Skinned`, `BuildingShader` and `LandmarkShader` are therefore bound by the engine as
global samplers, outside the model files; a shader that multiplies its diffuse result by such a sampler while the
slot is empty or bound to a stale target would lose the texture and keep only a constant term. The `.gr2` side
cannot say which slot; the shaders' parameter blocks in the materials' `ExtendedData` (register numbers, 9012 4.7)
are where to look next.

## 6. Playback: what the next epic needs

Written 2026-11-10 (23:36-23:52 EST) by worker 3105 of team-3100. Offline: files extracted read-only from c5h-b's
install copy (`%USERPROFILE%/c5-instances/c5h-b/install/Resource/Common/*.fpk`) to `D:/c5-64bit-work/3100/samples-playback/`;
database `Civ5DebugDatabase.db` of c5h-b (same file as `landmarks-and-models.md` 1.1) copied there and opened
`mode=ro`. Scripts and outputs in `evidence/3100/playback/` (own Python, standard library, reusing 9012's
`gr2read.py` and `fpk.py`). No game run, nothing built, no Granny runtime anywhere.

Open readers read (D34, neither built nor run; no new repository cloned):

| Short | Repository, commit, licence | Files used |
|---|---|---|
| **L** | `https://github.com/Norbyte/lslib` `6f5f6987d10d8485876b8368431c948e4dea4f83`, MIT, clone `D:/c5-64bit-work/3100/clone/lslib` | `LSLib/Granny/Model/CurveData/*.cs`, `LSLib/Granny/Model/Animation.cs` |
| **N** | `https://github.com/Arbos/nwn2mdk` `b6e67050be87b72403916aab891b4d571c2d7032`, Apache-2.0, clone `D:/c5-64bit-work/3100/clone/nwn2mdk` | `nwn2mdk-lib/gr2.h`, `nwn2mdk-lib/gr2.cpp`, `nw2fbx/export_gr2.cpp` |

Marks: [C] read in a reader's source and checked against the game's bytes by our scripts; [I] inferred; "reader
only" where a reader states it and the game's files did not exercise it.

**Result in short.**
- Every curve format met so far decodes with the readers' rules, and the decoded values pass every check on 13
  animation files of 3 units (warrior, tank, anti-aircraft gun): unit quaternions, monotonic knots ending at
  `Duration`, constant bone translations equal to the skeleton's rest `Transform` (bone lengths) [C].
- Evaluation must be a real degree-2 B-spline (de Boor), not "controls as keyframes": the two differ by up to 63.6
  degrees on `warrior_attacka.gr2` [C for the difference]. The open readers disagree here (L exports controls as keys,
  N evaluates de Boor); Granny's own sampler is closed. The knot convention of N fits the data (6.3).
- **Playback logic outside the `.gr2` files is a three-layer Granny/FireEngine system** [C for the files, I for
  the semantics]: per-unit `.fsmxml` state machines (`FGrannyStateMachine`, 14 unit machines) whose states each play
  one `.dge` **AnimGraph = a Granny blend-DAG file in the same `.gr2` container** (123 unit graphs, 8 node types:
  animation sources, selection, blend, block curves, callbacks, track masks), driven by the `ec` event codes of the
  `.fxsxml`, plus `.ftsxml` timed triggers (sound, effect, action, transfer) and, for landmarks and wonders only,
  `.ddtxml` decision trees. The `.dge` files read with 9012's reader unchanged (0 unresolved strings in 123 files).
- Correction to `landmarks-and-models.md` 4.8 (not edited here, owned by the team coordinator): the format list
  misses **`DaK32fC32f` (format 1, float knots and controls)**, found in `warrior_deatha.gr2` (one orientation
  curve). The full count by format is worker 3102's sweep (6.2).
- Re-estimates in 6.6: playback **28-55 h raw, 10-22 h expected**; I3-B **55-112 h raw, 18-40 h expected**;
  I3-D **39-78 h raw, 14-30 h expected**.

### 6.1 Animation, track group and transform track

Member names as the type tree of the files gives them (9012 reader) [C]; the same members in L `Animation.cs`
(`TransformTrack` 554, `TrackGroup` 791, `Animation` 824).

- `Animation`: `Name` (source `.max` path), `Duration` (s), `TimeStep` (s), `Oversampling`, `TrackGroups`
  (array of references); L also lists `DefaultLoopCount`, `Flags` for format version 0x80000011+. Seen:
  `TimeStep` 1/60 s with `Oversampling` 2, or 1/90 s with 3 (`tank_idlea`) [C].
- `TrackGroup`: `Name` (= the skeleton name, e.g. `Point_WORLD`), `VectorTracks`, `TransformTracks`,
  `TransformLODErrors`, `TextTracks`, `InitialPlacement` (Transform), `AccumulationFlags`, `LoopTranslation[3]`,
  `PeriodicLoop`, `RootMotion`, `ExtendedData`. In the warrior files: no vector or text tracks,
  `AccumulationFlags` 0, `InitialPlacement` identity [C].
- `TransformTrack`: `Name` (bone name; tracks are bound to bones by name), `Flags` (0 in all warrior tracks),
  `OrientationCurve`, `PositionCurve`, `ScaleShearCurve`, each an inline `{CurveData: variant reference}`. The
  variant's first member is named `CurveDataHeader_<FormatName>` and is `{UInt8 Format, UInt8 Degree}` (L
  `AnimationCurveData.cs` 96-99, 170-185; N `gr2.h` 128-133) [C].
- `ScaleShearCurve` is `DaIdentity` in every track decoded (138 + 28 + 216 tracks of the three units) [C], so a
  first playback can ignore scale/shear; Bone `Transform.Flags` bits (1 position, 2 orientation, 4 scale/shear) are
  9012's reading [I].

### 6.2 The curve formats: layout and decoding

Format numbers (L `AnimationCurveData.cs` 65-94; N `gr2.cpp` 136-147 names only 0-11). Quaternions are stored and
returned as (x, y, z, w) (L `D4Constant32f.cs` 23-26, OpenTK order; N `gr2.cpp` 90-95). "n" = number of knots.

| # | Format | Members after the 2-byte header (byte layout, 32-bit file) | Knots | Controls | Seen in |
|---|---|---|---|---|---|
| 1 | `DaK32fC32f` | `Int16 Padding; ReferenceToArray Real32 Knots; ReferenceToArray Real32 Controls` (L `DaK32fC32f.cs` 6-14; N `gr2.h` 322-330) | float seconds | `Controls[i*dim + c]`, dim = len(Controls)/n = 3 position, 4 quaternion, 9 scale/shear (L 16-26) | `warrior_deatha` orientation, 1 curve; deg 2; quaternion norms up to 1 +- 0.0114, **not normalised** [C] |
| 2 | `DaIdentity` | `Int16 Dimension` (9 in scale/shear, also used for position and orientation) (L `DaIdentity.cs` 9-10) | none | identity: zero translation, unit quaternion, identity matrix | 6 + 3 + 9 position, 6 + 6 + 39 orientation, all scale/shear [C] |
| 4 | `D3Constant32f` | `Int16 Padding; Real32 Controls[3]` (L `D3Constant32f.cs` 8-12; N `gr2.h` 139-144) | one, t = 0 | the vector | positions; equal to the bone's rest `Transform.Translation` within 0.003 units in the warrior and tank tracks printed, one exception 0.10 units (warrior idle, left upper arm) [C] |
| 5 | `D4Constant32f` | `Int16 Padding; Real32 Controls[4]` (L `D4Constant32f.cs` 8-12) | one | quaternion x, y, z, w | orientations [C] |
| 8 | `D4nK16uC15u` | `UInt16 ScaleOffsetTableEntries; Real32 OneOverKnotScale; ReferenceToArray UInt16 KnotsControls` (L `D4nK16uC15u.cs` 22-27; N `gr2.h` 203-209) | n = len/4; `knot[i] = KC[i] / OneOverKnotScale` (L 35-43) | 3 words per knot after the knots, below | orientations, deg 2 [C] |
| 9 | `D4nK8uC7u` | as 8 with `UInt8` array (L `D4nK8uC7u.cs` 22-27; N `gr2.h` 231-237) | n = len/4, same rule | 3 bytes per knot, below | orientations, deg 2 [C] |
| 10 | `D3K16uC16u` | `UInt16 OneOverKnotScaleTrunc; Real32 ControlScales[3]; Real32 ControlOffsets[3]; ReferenceToArray UInt16 KnotsControls` (L `D3K16uC16u.cs` 8-16; N `gr2.h` 153-160) | n = len/4; `knot[i] = KC[i] / f(Trunc)` where `f` = the 32-bit float whose high 16 bits are `Trunc` and low 16 bits 0 (L `AnimationCurveData.cs` 201-207; N `gr2.cpp` 380-384) | `P[i][c] = KC[n + 3i + c] * ControlScales[c] + ControlOffsets[c]` (L 34-49) | 9012's sample only (not in the 13 files here) |
| 11 | `D3K8uC8u` | as 10 with `UInt8` array (L `D3K8uC8u.cs`; N `gr2.h` 178-185) | as 10 | as 10 | positions, deg 2 [C] |
| 18 | `D3I1K8uC8u` | as 11 (L `D3I1K8uC8u.cs` 8-16) | **n = len/2** (L 18-21) | **one byte per knot**: `P[i][c] = KC[n + i] * ControlScales[c] + ControlOffsets[c]`, the same byte for all three components (L 34-49), i.e. a point moving on a line | positions (upper arms, pelvis), deg 2; values within 0.4 units of the rest translation [C]. **N does not handle it** (no case in `GR2_curve_view`, `gr2.cpp` 541-605) |

Not met in any file here, decoders exist in L only: 0 `DaKeyframes32f`, 3 `DaConstant32f`, 6/7 `DaK16uC16u` /
`DaK8uC8u`, 12-15 `D9I1/D9I3` (scale/shear), 16/17 `D3I1K32fC32f` / `D3I1K16uC16u`.
**Full-sweep counts by format and degree** (all 12,760 readable files, 58,819 track groups; C++ and Python sweeps
identical, `evidence/3100/sweep/cpp_counts.txt`, 2026-11-11) [C]. Key = curve slot (P position, O orientation, S
scale/shear) : format : number : degree.

| Curves | Key | Curves | Key |
|---|---|---|---|
| 1,456,761 | S:DaIdentity:2:0 | 11,965 | S:DaConstant32f:3:0 |
| 750,816 | P:D3Constant32f:4:0 | 6,944 | O:DaConstant32f:3:0 |
| 668,101 | O:D4Constant32f:5:0 | 6,687 | O:D4nK8uC7u:9:3 |
| 564,104 | P:DaIdentity:2:0 | 5,780 | O:DaK32fC32f:1:3 |
| 407,295 | O:DaIdentity:2:0 | 1,868 | P:D3I1K16uC16u:17:2 |
| 190,177 | O:D4nK16uC15u:8:3 | 168 | O:DaK32fC32f:1:2 |
| 118,159 | O:D4nK8uC7u:9:2 | 133 | S:D9I1K8uC8u:14:2 |
| 74,533 | P:D3I1K8uC8u:18:2 | 59 | S:D9I1K16uC16u:12:2 |
| 65,635 | O:D4nK16uC15u:8:2 | 47 | P:DaK32fC32f:1:2 |
| 64,806 | P:D3K8uC8u:11:2 | 33 | P:D3I1K32fC32f:16:2 |
| 12,739 | P:D3K16uC16u:10:2 | 22 / 6 | S:D9I3K16uC16u:13:2 / S:D9I3K8uC8u:15:2 |

So 16 formats occur, not 8: besides the table above, `DaConstant32f` (3), `D3I1K16uC16u` (17), `D3I1K32fC32f` (16)
and the four scale/shear formats `D9I1K8uC8u` (14), `D9I1K16uC16u` (12), `D9I3K16uC16u` (13), `D9I3K8uC8u` (15),
whose decoders exist in LSLib only; and **degree 3** occurs (orientation curves of formats 1, 8, 9: 202,644 curves),
so the evaluator must handle degree 3 as well as 2. Formats 0, 6, 7 do not occur.

**The D4n quaternion encoding** (identical in L `D4nK16uC15u.cs` 45-115 / `D4nK8uC7u.cs` 44-115 and N `gr2.cpp`
25-134) [C]:
1. `ScaleOffsetTableEntries` is four 4-bit selectors: `sel[j] = (S >> 4j) & 15`, j = 0..3 (one per quaternion
   component x, y, z, w).
2. `scale[j] = ScaleTable[sel[j]] * q`, `offset[j] = OffsetTable[sel[j]]`, with q = 1/32767 = 0.000030518509 for
   15-bit and 1/127 = 0.0078740157 for 7-bit values. Tables (16 entries each): ScaleTable = {sqrt2, 1/sqrt2,
   1/sqrt8, 1/sqrt8, 1/sqrt8, 1/sqrt32, 1/sqrt32, 1/sqrt32} then the same negated; OffsetTable = {-1/sqrt2,
   -1/sqrt8, -0.53033006, -1/sqrt32, 1/sqrt32, -1/sqrt32, -1/sqrt128, 0, 1/sqrt2, 1/sqrt8, 0.53033006, 1/sqrt32,
   -1/sqrt32, 1/sqrt32, 1/sqrt128, -0} (N `gr2.cpp` 25-41 names the constants).
3. Per knot three values a, b, c (16-bit words or bytes). Top bits: `G` = top bit of a (sign of the dropped
   component), `s1 = (topbit(b) << 1) | topbit(c)` = index of the dropped component; the others are
   `s2 = (s1+1)&3, s3 = (s1+2)&3, s4 = (s1+3)&3`.
4. `q[s2] = (a & 0x7fff) * scale[s2] + offset[s2]`, likewise b -> s3, c -> s4 (mask 0x7f for bytes);
   `q[s1] = sqrt(1 - q[s2]^2 - q[s3]^2 - q[s4]^2)`, negated when G is set.
Checked on the game's data [C]: 0 cases of a negative square-root argument; control norms within 1.1e-7 of 1
(`curves_*.out.txt`); 0 adjacent control pairs in opposite hemispheres.

**Knot scales seen** (`OneOverKnotScale` = 255/Duration for 8-bit and 65535/Duration for 16-bit knots, e.g.
42.5 / 382.5 / 255.0 for 6.0 / 0.667 / 1.0 s with 8-bit knots, 65535.0 for 1.0 s with 16-bit) [C]. The
`Trunc` variants lose the low mantissa bits (42.25 instead of 42.5, 382.0 instead of 382.5), so the last knot
can lie up to 0.6 % past `Duration` (measured up to 0.0118 s on a 6 s clip) [C]: clamp t to the last knot.
Knot counts per curve: up to 34 (1 s clips), 115 (`tank_idlea`, 6 s at 1/90 s, Oversampling 3) [C].
The first knot is 0; the second is 0 or one knot quantum (1/OneOverKnotScale, e.g. 0.0026 s) [C].

### 6.3 Evaluating a curve at time t

What the readers do:
- **L does not evaluate B-splines.** It exports control i as the key value at knot i (`AnimationCurveData.cs`
  255-283 `ExportKeyframes`); its writer only stores degree 0 or 2 (`Animation.cs` 619, 666). [reader only]
- **N evaluates with de Boor** (`nw2fbx/export_gr2.cpp` 163-253), sampled every 1/30 s (line 11, 304):
  the stored knots K[0..n-1] become the clamped knot vector `T = [0, 0, 0, K2, ..., K(n-1), K(n-1), K(n-1)]` for
  degree 2 (`padded_knots` 163-178: T[j+1] = K[j], the first `degree` entries forced to 0, K(n-1) appended
  `degree` times; length n + degree + 1); span search `while (i < len(T)-k-1 && T[i] <= t) i++; i--` (185-188);
  standard de Boor on controls `P[i-k..i]` with `alpha = (t - T[j+i-k]) / (T[j+1+i-r] - T[j+i-k])`, alpha 0 (positions)
  or 1 (rotations) when the denominator is 0 (198-206, 237-243); positions blended linearly, quaternions with
  `FbxQuaternion::Slerp` pairwise (244); a curve with one knot or fewer knots than degree + 1 returns P[0]
  (211-217, 248-253). Degree 0 = constant P[0] (same rule), degree 1 would be linear between neighbouring
  controls under the same code path [reader only, no degree-1 curve met].
- Granny's own sampler (`granny_bspline.cpp`, `granny_track_group_sampler.cpp` in the EXE's string table,
  `evidence/3100/playback/exe_animation_strings.out.txt`) is closed; neither reader says how Granny treats
  the ends of a looping curve, and neither normalises quaternions after blending.

What the data says (`curves.py`, `seam_check.py`, outputs `curves_warrior.out.txt`, `curves_tank.out.txt`,
`curves_aagun.out.txt`, `seam_check.out.txt`) [C for the numbers]:
- de Boor (N's rule) against controls-as-keys (L's reading), every 1/60 s: up to 3.9 deg (warrior idle),
  32.5 deg (run), 63.6 deg (attack A), 24.6 deg (anti-aircraft gun run); 0.03 / 0.9 deg on the tank. So the
  reading of L is wrong for playback of fast clips, and frame-to-frame jumps under it are about twice those of
  de Boor (24.2 against 10.5 deg on the warrior run).
- Clamped (N) against a periodic continuation across the loop seam (P[-1] = P[n-2], K[-1] = K[n-2] - Duration):
  at most 1.15 deg apart on the warrior run, 0.000 deg on the idle. The edge rule is therefore a detail, not a
  risk. Neither variant makes the seam velocities match on the run clip (median relative mismatch 0.71 / 0.65),
  so the seam test does not decide the edge rule.
- Position at t = 0 against the rest pose: constant tracks equal to rest; animated bones move as expected
  (pelvis 11.7 units in the run, the club 68 units: it is held in the hand). Orientation at t = 0 differs from the
  rest (bind) pose by up to 128 deg: the bind pose is not the animation's first frame, as usual [I].
- Loop clips end where they start: |v(0) - v(Duration)| at most 0.05 deg / 0.03 units for idle and run; one-shot
  clips (death, fortify, transitions) do not (up to 165 deg) [C].

Rules for the server's sampler (degree 2 as N; [I] where marked):
1. Clamp t to [0, K(n-1)] for one-shot clips; for looping clips t mod Duration [I: Granny's `ClampedLoop` and
   `LoopCount` parameters are on the `.dge` animation sources, 6.4].
2. Find the span by binary search on K (n up to 115).
3. Positions: de Boor on 3-vectors.
4. Quaternions: make each control the same hemisphere as its predecessor (no flip was needed in the data, but
   blending and `DaK32fC32f` need it), de Boor with nlerp or slerp per step, normalise the result [I: the 1.15 deg
   periodic/clamped and the nlerp/slerp differences are both below what a unit at Civ 5's zoom shows].
5. Degree 0 or one knot: P[0]; `DaIdentity`: identity.
6. Cost: per bone per frame two de Boor evaluations of 3 lerps each; 72 bones x 1,000 units is about 0.4 M
   lerps per frame [I], trivial on the CPU; GPU skinning then needs one 4x3 matrix per bound bone,
   at most 32 per draw (`SkinBoneCount` 32, `landmarks-and-models.md` 4.7).

### 6.4 Logic outside the `.gr2` files

Census over all 147 archives (`logic_census.py`, `logic_census.out.txt`; `ec_names.py`, `ec_names.out.txt`)
[C for the counts]: 1,388 `.fxsxml` (568 with `<Animation>`; 477 with `<StateMachine>`; 215 with
`<DecisionTree>`; 45 with animations but neither), 69 `.fsmxml`, 666 `.ftsxml`, 9 `.ddtxml`, 288 `.dge`
(123 in `UnitModels.fpk`, the rest leader scenes). The EXE carries the matching runtime [C, strings in
`exe_animation_strings.out.txt`]: Granny's `granny_dag2_*.cpp` (blend, blockcurve, callback, poseanimsource,
scalarcallback, scalarselect, scalarsource, scalarswitch, scaleoffset, selection, sequence, subgraph, timeshift,
trackmask), `granny_dag_pose_cache.cpp`, `granny_track_group_sampler.cpp`, `granny_bspline.cpp`, and
FireEngine2's `FGrannyStateMachine.cpp`, `FGrannyStateMachineInstance.cpp`, `FGrannyDecisionTreeMgr.cpp`,
`FTimedTriggerMgr.cpp`, `FTriggerTimelineSetMgr.cpp`, the condition classes `FFSMCondition*` (18 names) and
`FTimedTrigger{Sound,StopSound,Effect,Action,Transfer}`. The pose pool of `world-systems-and-stubs.md`
(0x5F63958, 32 local / world poses and 32 DAG pose caches per thread) is this runtime's working memory [I].

**(a) `ec` event codes.** `<Animation file=... ec="N[, M]">` binds an animation file to one or more codes; the code,
not the file name, is what the state machine, DAG sources, triggers and decision trees refer to. 133 distinct codes;
the unit vocabulary is shared by every unit (counts = `.fxsxml` files using the code) [C for names and counts,
I for the meaning taken from the file names]:

| Codes | Meaning (from the animation file names) | Files |
|---|---|---|
| 1000 / 2000 | idle A / idle B | 406 / 334 |
| 1020 / 2020 | idle A -> B / B -> A transition | 334 / 334 |
| 1040 / 2040 | fidget A / B | 334 / 334 |
| 1100 / 2100 | attack A / B (1100 also `attack_air`) | 240 / 216 |
| 1120, 1140 | charge run, charge attack | 216, 187 |
| 1160 / 1180, 2160 / 2180 | attack city / bombard (A, B) | 216 / 228, 41 / 41 |
| 1130 | anti-air attack | 5 |
| 1190 / 1195 | captured / captured idle | 3 / 3 |
| 1200 / 1220, 2200 / 2220 | death A / dead idle, death B / dead idle | 350 / 350, 228 / 228 |
| 1280 / 1285 / 1290 | bombard defence / its idle / back to idle A | 334 each |
| 1400, 1420, 1440, 2440, 1450 | run, start run, stop run, stop run into idle B, shuffle | 350, 3, 336, 163, 163 |
| 1460 / 1480 | run turn right / left | 35 / 35 |
| 1500 / 1520 / 1540 / 1560 / 1580 | fortify / idle / fidget / -> combat ready / -> idle A | 335, 334, 216, 216, 334 |
| 1600 / 1620 / 1640 | combat ready / its idle / -> idle A | 226 each |
| 1700 / 1705 / 1710 | aim left / right / centre (layered aiming) | 21 each |
| 1800 | victory | 216 |
| 1910-1960, 1914 | worker: dig, rake, axe, pickaxe, hammer, railroad; stop dig | 6-17 |
| 1990 / 1995 | build or activate (great people, workers) / its idle | 115 / 120 |
| 1000-1003 | landmark / resource idles 1-4 (cow, deer, ...) | 10-11 |
| 100 / 101 | feature / improvement `idle` / `worked` (TempModels, FeatureModels, MiscData) | 19 / 19 |
| 1, 4001-10120 | leader scenes: rigging, camera, intro (6000), moods 7000-7071, defeated 8000/8001, war and hate 9000-9071, scene effects 10001-10120 | |

*Note 2026-11-15 21:30, task 4408 (`unit-controller.md` 6.1, 6.6):* binding by code **confirmed** [C] for the unit
files the active VP database reaches (357 `.fxsxml`): every `ECComplete` code is a source of its state's graph (223 /
223), and graph sources resolve through the model except in 6 models (4 spaceship parts through ec 0 sources, `dopp_a`
1020 / 2020, the Hun ram 1460 / 1480). Meanings stay [I] from the names.

**(b) State machines (`.fsmxml`, units and leaders).** Root `<statemachine fsm="FGrannyStateMachine"
default_state="N">`, `<states>` of `<state id type="FGrannyState" name asset="X.dge" action="ec" sockets>` and
`<sockets>` of `<socket from too bidirectional order conditions>` each holding an ordered `<conditions>` list [C].
14 unit machines (`1_omni_state_01` 29 states / 72 sockets used by 117 `.fxsxml`; `civilian_state_01` 97;
`ships_state_01` 37; `1_omni_mounted_state_01` 35; also airplane, missile, mechanized aim, modern ship aim, roman
legion, great general, worker, work boat, ship transport, aim civilian). The warrior's `.fxsxml` names
`1_OMNI_STATE_01.fsmxml` and its 24 AnimGraphs. Condition types and counts over all 69 machines [C]:
`EventSetPathOption` 2,062, `CheckPathOption` 1,938, `OrOperator` 843, `EventRandomPathOption` 634 (with
`<weights><weight v w/>`), `ECComplete` 445, `CheckStateTime` 107, `RandomRoll` 17 (e.g. value 0.0007 for the
idle-to-fidget socket). The EXE names five more that the files do not use (`AnyComplete`, `EventCopyVariable`,
`EventSetVariable`, `Variable*` comparisons, `True`/`False`).
Reading [I]: a machine holds 7 integer "path options" (indices 0-6 used). **Path 4 is the requested action**:
`CheckPathOption method=1 path=4 value=<ec>` is how a socket fires on a game request (in `1_omni_state_01` the
values are exactly the unit action codes 1100, 1120, 1160, 1180, 1200, 1280, 1400, 1450, 1500, 1600, 1800, ...;
leader machines use small values 0-41 there). The other paths are set on the transition
(`EventSetPathOption`, `EventRandomPathOption` with weights, e.g. death A or B 50/50) and read inside the target
state's DAG to pick variants. Conditions between `OrOperator`s are AND-ed [I]. `ECComplete` waits for the current
clip to finish; `CheckStateTime` for a time in the state; `RandomRoll` is a per-update chance. State `action` is
the ec the state reports as "current action" (`10000` on the run-start states) [I].
*Notes 2026-11-15 21:30, task 4408 (`unit-controller.md` 6.2, 6.6), over the 15 machines the VP database reaches
(13 of UnitModels' 14 unit machines, not `ship_transport_state`, plus Expansion2's `engineer_state` and VP's `edited_modern_ship_aim_state_01`):*
path 4 as the request **confirmed by the files** [C] and refined: a level the EXE writes that the machine itself
clears (`EventSetPathOption path=4 value=0` when a one-shot action or run starts) or sets (1000 or 1400 in default
states); 2100 and 2200 are never requested, the machine draws the A/B variant. **Corrected:** the machines use path
options 0-6, but the graphs read 0-8; 7 and 8 are written by the EXE (turn and aim, 6.3 there). `CheckPathOption`
method 1 is "equal" and 6 "not equal" (the only two used); `CheckStateTime` method 5 is "at least"; `ECComplete` may
carry a fraction (0.98, missile). The state's `sockets` attribute is the number of its outgoing sockets (299 / 299),
not a reference list. Conditions and sockets are written in their `order` (0 exceptions), and AND within OR groups
fits every socket read (stays [I]).

**(c) AnimGraphs (`.dge`) = Granny blend DAGs** [C for structure, `dge_dump.py`, `dge_census.out.txt`,
`dge_warrior_examples.out.txt`]. Same header and magic as `.gr2`, sections stored raw (compression 0); root
`{DAGs: [{RootNodeIndex, InstanceDataSize, Nodes[{Name, ParentIndex, NodeXPos, NodeYPos, ExternalInputEdgeCount,
InputEdges[{ParameterType, InputNodeIndex, ConnectedOutput, EdgeName, Internal}], ExternalOutputEdgeCount,
OutputEdges[...], InstanceOffset, NodeData: variant}], Sources[{EventCode}], TrackMasks[{BindingName}]}],
ExtendedData}`. **`Sources[k].EventCode` is how a graph names its animations: by ec code, resolved through the
model's `.fxsxml`.** Node types over the 123 unit graphs: PoseAnimSource 698, ScalarCallback 360, Selection 276,
Blend 182, Subgraph 123 (one per file, the output node), BlockCurve 59, Trackmask 31, ScalarSource 31. Parameters:
PoseAnimSource edges `{SourceIndex, TrackGroupIndex -1, Speed 1.0, Offset, LoopCount 0 (loop, 486) or 1 (once,
212), ClampedLoop 1, ContinueOffset 0/1}`; Selection `{DefaultEdge 1}` with a `Selector` input and N pose inputs;
Blend inputs `Param, From, To, Mask`; BlockCurve `{Blocks[{BlockType, FlipX, FlipY, Duration}], Scale}` (an
ease curve: ramp 1.0 s, flat, ramp flipped); ScalarCallback `{CallbackBindHint 7 (275) / 5 (83) / 6 (2),
CallbackValue}`; Trackmask `{BindingIndex 0, DefaultWeight 1, TargetWeight 0}`. `ForwardIndex` / `InstanceOffset`
hold run-time garbage in the files. Example `idle_graph.dge`: sources ec [1000, 2000], Selection between them,
selector = ScalarCallback hint 7 value 0. `melee_attacks.dge`: sources [2100, 1160, 1100, 1140, 1120, 1180], three
Selections, one Blend whose parameter is a BlockCurve fed by a ScalarCallback hint 5 value 0.15.
Reading [I]: hint 7 callbacks return the state machine's path option number `CallbackValue` (selects the variant);
hint 5 callbacks return a time in seconds (0.1 / 0.15 = the cross-fade length) that drives a BlockCurve ramp,
which is the Blend weight: **blending and transitions are cross-fades of 0.1-0.15 s inside the DAGs**, and a state
change in the machine swaps graphs (whether the EXE also cross-fades between states is not in the files).
*Notes 2026-11-15 21:30, task 4408 (`unit-controller.md` 6.3, 6.6), over the 112 reached unit graphs:* hint 7 =
path option **confirmed by structure** [C] for paths 0-6 (the start-run graph's selections follow exactly what the
sockets into that state write to paths 1 and 2; the death graph's follow paths 3 and 6); hint-7 values 7 and 8 are
channels no machine writes (turn amount and side in `mounted_running_loop.dge`, broadside side in `ship_attacks.dge`,
aim in the 52 aim graphs) [I: written by the EXE]. Hint 5 refined [I]: a 0..1 fade over `CallbackValue` seconds
(0.1-0.333; 100 and 1000 in 4 aim uses) through a BlockCurve; the machine leaves the state at the fade's end in 36 of
74 states, not in 38. **Refined:** the cross-fade between states is carried by the transition graphs themselves (a
"From" side selecting the previous clip by a path the incoming socket wrote, with `ContinueOffset` 1, faded into the
new clip); whether the EXE adds one is still open (X1, live). Hint 6 is used by no reached graph; Trackmask only in
the aim graphs (52), always the aim layer's binding.

**(d) Timed triggers (`.ftsxml`).** `<trigger_system type="FGrannyTimedTriggerSystem">` with `<event_tracks>`
(`<event_track ec>` of `<track id name>`: Sound 1, Effect 2, Transfer 3, Action 4) and `<triggers>` of
`<trigger type id time duration repeat ec track tier ...>` [C]. Types over all 666 files: Sound 18,737 (attribute
`event` = an audio script name with `$(WEAPON)` / `$(TERRAIN)` substitutions), Effect 7,042 (`event` =
`ART_DEF_VEFFECT_*`, `bone` = attachment bone, `duration`), Action 1,278 (`action="1"`: e.g. ec 1100 at 0.4889 s,
ec 2100 at 0.4741 s in the warrior: the hit moment that the combat visualisation waits for [I]), Transfer 603
(`refid` of an effect, `speed`, `tangent.x/y/z`: a projectile hand-off, e.g. the torch thrown in attack-city)
[C]. `time` is in seconds from the clip start [I: max 0.68 s in the warrior's 1 s clips, 44.65 s over all files,
which only fits seconds]. The `.fxsxml` `<BoneUsage>` lists the bones these triggers attach to
(warrior: `bone_CLUB`, `Base HumanSpine2`, `Base HumanRPalm`).
*Notes 2026-11-15 21:30, task 4408 (`unit-controller.md` 6.4):* over the 268 reached unit trigger files Action
triggers occur only on attack codes (1100, 2100, 1140, 1160, 1180, 2160, 2180, 1130, 2140, 1192); a melee unit's city
attack and bombard (1160 / 1180) carry a Transfer instead (warrior 0.576 / 0.578 s, horseman 0.602 s), so "the
Action trigger is the hit moment" holds for attacks only [I]. `repeat` is 0 on every trigger; the track number is
not fixed (Action is track 5 in 30 event tracks), so a trigger is typed by its `type`. `time` in seconds stays [I]
(max 9.56 s). One VP file (`edited_fx_triggers_missilecruiser.ftsxml`) is not well-formed XML (a nested comment).

**(e) Decision trees (`.ddtxml`, landmarks and wonders only).** `<DecisionTree><Selector><Layers>` with two
`<Layer>`s, "ModelAnimationSet" and "TriggerTimelineSet"; each holds one `<Item type="selector">` per landmark
state in the order RawResource, UnderConstruction, Constructed, Pillaged (comments in the files), and each of
those a list of `<Item type="sequence">` with `<AnimationSequence ec>` or `<TriggerTimelineSequence ec>` and
`<SequenceInfo duration speed loop>` [C]. `LandmarkDecisionTree4.ddtxml` offers ec 1000-1003 (four 6 s loops) in
every state; `LandmarkDecisionTree.ddtxml` only 1000; the static trees only a trigger timeline of duration 0.
Use: `staticlandmarkdecisiontree` 136 `.fxsxml`, `wonder_decisiontree` 29, `animatedmodeleffect` 20,
`landmarkdecisiontree` 15, `landmarkdecisiontree4` 9, others 6 [C]. Choice among several sequences of a state is
random [I]. The landmark state index matches `<TimedTrigger state="3">` (pillaged smoke) and `state="0,1,2"` in
the `.fxsxml` [C for the files].

**(f) Database side** [C, c5h-b database]. `ArtDefine_UnitInfos` 274 (`Formation`: 24 named formations plus
NULL / empty), `ArtDefine_UnitInfoMemberInfos` 406 (1,391 members in total, 1-14 per unit art, mean 3.4; warrior =
6 `WARRIOR` + 3 `WARRIOR_V2` + 3 `WARRIOR_V4`), `ArtDefine_UnitMemberInfos` 367 (`Scale` 0.14 for the warrior,
`Model` = the `.fxsxml`, `MaterialTypeTag` for sounds), `ArtDefine_UnitMemberCombats` 364 (`EnableActions`, e.g.
warrior "Idle Attack RunCharge AttackCity Bombard Death BombardDefend Run Fortify CombatReady Walk AttackCharge";
`MoveRate`, `TurnRate*`, radii and `Has*` flags: the movement and combat choreography),
`ArtDefine_UnitMemberCombatWeapons` 377. `UnitVisConfig.ini` (UnitModels.fpk): `GlobalUnitScale 100`,
`GlobalUnitMemberMultiplier 100`, `UnitRenderMultiThreaded 1`.

**(g) What the EXE side adds (from `research/`, not re-checked).** The DLL drives unit visuals through 20
`ICvEngineUtility::GameplayUnit*` methods at 84 call sites (`sources/shell-gates/stubs/unit.md` 3c): Moved,
UnitCombat / CityCombat, Fortify, Activate, Work, Embark, SetDamage, Destroyed / DestroyedInCombat,
ResetAnimationState and others. *(Corrected 2026-11-15, 0316 and 0321. This sentence said: "Visualised combat
waits for the engine's combat-end callback (the `RunCombatSim` listener lives in the Unit system; `coupling.md`
2.3)". Vox Populi's DLL resolves at once and waits for nothing. The waiting happens inside the EXE: its event queue
holds the unit's later messages behind the running combat simulation,
`combat-choreography.md` 3.)* Mapping these calls onto the path-4 action
codes (Moved -> 1400 run, UnitCombat -> 1100/2100 or 1160/1180, Fortify -> 1500, death -> 1200/2200,
Work -> 1910-1990) is [I] and is the bridge the playback epic has to build; the files fix everything below it.
*Note 2026-11-15 21:30, task 4408 (`unit-controller.md` 6.5):* refined [I]: the requests are 1000, 1100, 1120, 1130,
1140, 1160, 1180, 1200, 1280, 1400, 1450, 1500, 1600, 1800, 1190 / 1195, 1910-1960 and 1990 (never 2100 / 2200: the
machine draws A or B); 1990 is activate, 1910-1960 work; each `EnableActions` word names one code (Idle 1000, Attack
1100, RunCharge 1120, AttackCharge 1140, AttackCity 1160, Bombard 1180, Death 1200, BombardDefend 1280, Run 1400,
Walk 1450, Fortify 1500, CombatReady 1600, AttackSurfaceToAir 1130); an attack request on an idle unit first plays
"enter combat ready". Which DLL call requests which code stays [I] (table in 6.5 there).
*Note 2026-11-16 00:55, epic 0374 (lead-0374; `unit-controller.md` 8.10, 14.1, 15):* from the live captures (4405) and
the built bridge (4404 B): Moved -> run 1400, then stop run 1440 / 2440 -> idle A / B at the move's end, [L] (Q3a, Q3b);
UnitCombat on the attacker -> charge run 1120, charge attack 1140, attack 1100 / 2100, combat-ready idle [L] (Q4b,
Q6); SetDamage outside combat -> the falling figures are removed with **no** death clip [L, one unit, Q7], which
corrects the death -> 1200 reading for that case (premise change 5 to the MC, #2141; death in combat not captured);
Fortify on another player's unit did not show 1500 within the active turn (Q2: not determinable from it, stays [I]);
Work -> 1910-1960 is refused by the bridge as `BadEvent` (no source table for which work code), open.

### 6.5 What the next epic has to build (and what it does not)

| Part | Needed | Source of truth |
|---|---|---|
| curve decoders | 9 formats met (1, 2, 4, 5, 8, 9, 10, 11, 18) + reject the rest | 6.2, `curves.py` as reference |
| sampler | de Boor degree 0-2, quaternion hemisphere + normalise, clamp / loop | 6.3 |
| pose pipeline | local pose per bone (track bound by bone name) -> world pose by `ParentIndex` -> skinning matrix = world x `InverseWorldTransform` -> per-mesh `BoneBindings` palette, <= 32 per draw | 9012 4.7; section 2 here |
| GPU skinning | 4 weights (`NormalUInt8`) + 4 indices (`UInt8`) per vertex, 68-byte stride; palette of <= 32 bones | 9012 4.7 |
| blend-DAG evaluator | 8 node types above; cross-fade, selection, track masks, loop / once | 6.4 (c), `.dge` read by the existing reader |
| state machine | 7 condition types, 7 path options, random weights; 14 unit machines | 6.4 (b) |
| triggers | Action (hit moment) needed for combat timing; Sound / Effect / Transfer need the audio and particle systems, which are not in the server | 6.4 (d) |
| landmark trees | state x random 6 s loop | 6.4 (e) |
| not in the files | which DLL event requests which action code; formations and member placement; movement paths and turning (`MoveRate`, `TurnRate*`) | EXE side, I3-A / I3-C |

### 6.6 Re-estimates (Opus-session-hours, 2026-11-10)

Basis: the static reader of this epic exists (sections 1-5: sections, Oodle1, fix-ups, type tree, string databases,
meshes, skeletons, bindings, track groups parsed); 9012's figures (`landmarks-and-models.md` section 5: playback
+25-45 h, static + rest pose 25-45 h); GATE2's 6-12 h per shader family for driving the EXE's shaders; the brief's
observation that epics finished at 3-30 % of raw figures. "Expected" applies roughly 35-40 % to the raw range: the
formats are now known and have Python references, but the semantics of 6.4 (b)-(c) are [I] and need checking
against footage.

**Playback (units and animated landmarks), on top of the static reader:**

| Item | Raw h |
|---|---|
| curve decoders, 9 formats, tests against `curves.py` outputs | 2-4 |
| sampler (span search, de Boor, quaternions, clamp / loop) | 2-4 |
| local -> world pose, skinning matrices, 32-bone palettes per draw (mesh split if a mesh binds more) | 3-6 |
| GPU skinning in the server's own shader (EXE shader parity is GATE2's per-family cost, not counted) | 3-6 |
| blend-DAG evaluator, 8 node types, `.dge` loading through the existing reader | 6-12 |
| state machine interpreter (`.fsmxml`) and ec resolution through `.fxsxml` | 5-10 |
| timed triggers: Action events to the combat visualisation; Sound / Effect hooks stubbed | 2-4 |
| landmark decision trees | 1-2 |
| validation against stock footage (epic 3000's ground truth) and fixes | 4-7 |
| **total** | **28-55 raw; expected 10-22** |

Up from 9012's 25-45 because the DAG and state-machine layers are now known to be real, file-driven systems
(123 graphs, 14 machines), not a lookup of one clip per action.

**I3-B (units and cities in the server)** = playback above + per-unit instancing (formations of
`ArtDefine_UnitInfos`, 1-14 members, scale, facing, terrain height) 8-15 h + unit materials with
`UnitShader_Skinned` parameters (team colour, alpha, damage) 6-12 h + city models (`civ5artdefines_citybuildings.xml`,
`CityModels.fpk`; not examined here) 8-20 h + the DLL-event-to-action bridge (6.4 g) 5-10 h:
**55-112 h raw, 18-40 h expected**. Not in it: I3-A (unit system absence, 14-21 h) and I3-C (movement and combat
visualisation), which consume the bridge.

**I3-D (landmarks in the server)**, the static reader done: D3D11 drawing of landmark meshes with
BuildingShader / LandmarkShader / LandmarkShader_Stencil parity 6-12 h, the six layout handlers and
`TerrainContour` (EXE code not read) 10-20 h, animated landmarks through the decision trees reusing the playback core
2-4 h, state changes and pillage triggers 2-4 h, roads / railroads / bridges / farm fields (RoadLayout, farm-field
builder; formats not read) 15-30 h, verification 4-8 h: **39-78 h raw, 14-30 h expected**, if the playback core of
I3-B exists (else add 10-18 h of it for the 22 animated landmark models, which use only idles: no state machine,
no DAG).

### 6.7 Not verified (playback part)

- Granny's own edge and loop handling of B-splines and whether it slerps or nlerps: closed source; the clamped
  (N) and periodic variants differ by at most 1.15 deg on the warrior run (6.3).
- Every semantic reading in 6.4 marked [I]: path 4 as the requested action, the meaning of callback hints 5 and 7,
  AND/OR grouping of conditions, trigger times in seconds, random choice in decision trees, and the DLL-event to
  action-code mapping. Checking them needs the game (footage or a debugger on the EXE's `FGrannyStateMachine`).
- Only 13 animation files of 3 units were decoded; formats 10 (`D3K16uC16u`) and the full format census wait for
  worker 3102's sweep. Formats 0, 3, 6, 7, 12-17 were not met.
- `.dge` and `.fsmxml` of leaders and scenes (165 graphs, 55 machines) were counted, not read.
- City models, formations and member placement were not examined.

## 7. Not verified

- **Rendering fidelity against the game.** The pictures use an own plain shader; the EXE's shader sets
  (`UnitShader_Skinned`, `BuildingShader`, `LandmarkShader`, ...), their parameter blocks, team colour, SREF,
  irradiance and environment maps, decal blending, fog of war and lighting are not reproduced. The comparison with
  the stock screenshot is by eye on two objects (section 5); winding order, handedness and units against the EXE's
  world camera are not checked.
- The big-endian file is not decoded (section 2.3); which `cityruins_improved.gr2` the EXE loads is not known.
- Which copy of a duplicated basename the EXE loads (mount order of archives, loose DLC and mods) [I].
- Bone transforms, OBBs, curve bytes and vertex bytes are compared between the two readers only through the section
  CRCs and the 17 pictures, not field by field in the canonical summary.
- Whether the engine maps `.psd`/`.png` texture names to `.dds`; `.dge` and `.fsmxml` existence (not extracted).
- The decoder's behaviour on corrupt input is tested by synthetic and random cases and 40 targeted corruptions, not
  fuzzed at scale.
- Playback: section 6.7.
