# The renderer's unit module `c5r::units`: input, file form, layout, passes, premises (epic 0041, U1-U3)

Written 2026-11-15 from 14:04 EST by lead-0041 (epic 0041 on account B, branch `epic-0041-units`, child 4351) as the
C1 note of `docs/briefs/epic-0041-units-in-server.md`: the renderer half of the unit feed contract
(`unit-feed-contract.md`, N5) and the design of the module that U6 (the server scene) wires to the feed. **Status:
accepted by the MC at the C1 review, 2026-11-15 14:08 (queue #1845), with section 9 as the definition of done.** Sections 10-13
are reserved for the workers' results (10 U1 / 4352, 11 U2 / 4353, 12 U3 / 4354, 13 the oracle run / 4355) and are
written only by them.

Marks: `[C]` read in this repository's code, data or the EXE's decompile as recorded in the files named; `[L]`
measured by the issue named; `[I]` inferred or proposed.

## 0. In short

- The module takes **one frame's unit list** (`UnitScene`, section 2) and a **pass description** (camera, targets;
  section 5) and produces the EXE's own draw inputs for it (28-byte and 20-byte vertex streams, 98-row palette blocks,
  the per-instance id stream, cb1 per pass, the textures and the EXE's unit programs read from the install) and issues
  the draws of the three passes on its own D3D11 device. It reads no EXE memory and assumes nothing of the EXE's unit
  system (D144, D147): every input comes from the list, the install, the active mods and the game database.
- The list has a **file form**, the unit-scene file (`.c5units`, section 3): a strict line-oriented text that the
  offline tool `render/tools/unitscene` reads and that U6 can write from the bundle's records 3-5 (UNITS, PATHS,
  COMBATS) plus the position records, with no change to either (section 4).
- Layers L0-L3 of the sheet's 3.2 are the module's stages: art (L0 load), draw inputs and draws (L1), pose (L2),
  placement (L3). The controller (L4, U5) is outside: **clip and clip time per figure are inputs** until U5 exists.
- Every unit that cannot be drawn exactly gets a **named status** and is not drawn at all (no placeholder, no partial
  unit; D118, D125, R6/D132). A **pre-check** over every unit type of the active database runs the load stages without
  drawing, for U6 to call at game start (section 7).

**Results at the end of the epic (lead-0041, 2026-11-15 20:19 EST; sections 10-13 hold the numbers) [L]:**
the module exists in `render/src/units` and runs offline on 0301's catalogue (231 units: 3907's 229 and 3901's 2) with one
command, `render/tools/unitscene/oracle.py` (13.5). Draw inputs (DoD B): art rows 231/231, all 231 units load; vertex
buffers byte-equal on 434 of 530 meshes plus 12 that differ only at named weight ties, 69 one-step meshes classified
with the cause open (10.8; **cause found 2026-11-16 by ticket 0379, 10.9: the EXE's basis conversion, cofactor
inverse and float weight renormaliser reproduce all 515 drawn meshes byte for byte**); index buffers, textures (by the material's `UnitShaderTextures` parameters, 10.8) and
programs equal on 515/515 drawn meshes; header rows 5,134/5,170; cb1 63/63; figure-major ids 306/318 models. Draws
(DoD C, the module's own draws against the captured draws, GPU): 199 of 228 units bit-equal in all three passes (scene
200, reflection 208, mask 221), 15 within one 8-bit step, 2 within 3, 12 more with named causes (one-step meshes, the
battering ram's shared crew, the submarines' shared ice/plain buffer). Placement (D): 129/138 multi-figure units within
0.002 world units with no fitted shift, heights 1,083/1,135 within 0.001, the rest by named class. Pose (E): K3 holds
on the whole catalogue. Failure statuses and the pre-check (F) with a negative test each; suites plain and MSVC ASan
with 0 reports (G).

## 1. Where the code lives (decision 1) [C for the build facts, read 2026-11-15 14:00]

- `render/src/units/` (module sources), `render/include/c5r/units/` (public headers), `render/tests/units/`,
  `render/tools/unitscene/`. `models/` stays the format library: `.gr2` reader, curve decoders and sampler
  (`anim.h`), the 28-byte repack (`unit_mesh.h`), the art tables and chain 3 (`art.h`, `resolve_unit`).
- **Linking `c5models` without touching `render/CMakeLists.txt` (0169's):** both projects are clang-cl x64, C++14,
  `CMAKE_MSVC_RUNTIME_LIBRARY` `MultiThreaded$<$<CONFIG:Debug>:Debug>DLL` (both CMakeLists, read 2026-11-15) [C]. The
  render root globs `src/*/CMakeLists.txt` and `tests/*`, `tools/*` [C]. So `render/src/units/CMakeLists.txt` builds
  `models/src/*.cpp` (globbed, so new `models/src` files are picked up) into a static library `c5r_models` with
  `models/include` public and **models' own warning set** (clang-cl `/W4 /WX -Wno-unused-parameter`; under the MSVC
  ASan variant `/W3` without `/WX`, as `models/CMakeLists.txt` does), adds the module's sources to `c5render`, and
  links `c5r_models` to `c5render` (`target_link_libraries` across directories is allowed from CMake 3.13, CMP0079;
  the root requires 3.21). `add_subdirectory(models)` is not used: it would register models' tests in render's ctest.
  `tests/units/CMakeLists.txt` and `tools/unitscene/CMakeLists.txt` glob their sources, so workers never edit a build
  file. The ASan flags of the render root (`add_compile_options`) reach the library through directory scope [C].
- The ASan variant: render's `build.py --asan` (MSVC) builds models' sources too; models' own `build.py --asan` keeps
  its suite (DoD G runs both).

## 2. The input: `UnitScene` (renderer terms)

One frame's units. All strings are database `Type` names (ASCII); all positions world units, z up (the server's and
the EXE's world frame, `unit-draws.md` 2).

```
struct PlayerInput {            // one per player that owns a unit in the scene
    int32_t  player;            // 0..63 (DLL PlayerTypes)
    std::string colour;         // PlayerColors.Type; the module takes its SecondaryColor (unit-draws.md 5)
    std::string art_style;      // the owner's ArtStyleSuffix, civilization Type, or ArtStyleType (see 4)
    std::string era;            // the owner's current era Type (era variation of the art row)
};
struct FigureInput {            // optional per figure; absent = the unit's default clip at t 0 (section 6.2)
    uint16_t    figure;         // index into the unit's figures in member order (section 6.3)
    std::string clip;           // animation name as the .fxsxml names it (EC code resolved by the module), until U5
    float       t;              // clip time in seconds (wrapped by clip_time for looping clips)
};
struct UnitInput {
    int32_t  player;            // owner; (player, id) keys a unit (contract premise F3)
    int32_t  id;
    std::string unit_type;      // Units.Type
    std::string art_row;        // ArtDefine_UnitInfos.Type, or "" = resolve from unit_type and the owner (6.1)
    int32_t  plot_x, plot_y;    // the unit's plot
    bool     has_root;          // true: root_x/y/z given (position source exe); false: plot centre (source own)
    float    root_x, root_y, root_z;
    int8_t   facing;            // DirectionTypes 0..5; yaw = 150 - 60 d degrees (unit-formations.md 11.2);
                                // -1..7 in the .c5units file (4464): 6, 7 (the fork's NO_DIRECTION) and -1
                                // give 1 rad (facingYaw), as the EXE's switch
    uint32_t formation_seed;    // the EXE's art record index (unit-formations.md 12.1); U6 keeps that allocator
    int16_t  damage, max_hp;    // figure deaths by the 0x4B4B80 rule (unit-formations.md 7)
    int16_t  alive;             // -1 = from damage; else the number of living figures (oracle override)
    uint8_t  flags;             // bit 0 embarked, 1 garrisoned, 2 fortified, 3 visible to the active team,
                                // 4 invisible, 5 delayed death, 6 dead (the contract's UnitRecord flags)
    float    fade_from, fade_to, fade;   // palette header row 1 x, y, z; (1, 0, 0) = opaque (unit-draws.md 4)
    std::vector<FigureInput> figures;
};
struct UnitScene {
    uint32_t format = 1;
    std::string position_source;        // "exe" | "own" (contract section 4)
    float    water_level;               // the height field's floor (unit-formations.md 12.4); also cb1[8].x
    std::vector<PlayerInput> players;
    std::vector<UnitInput>   units;     // draw order = this order (the EXE's id order is not sorted, unit-draws.md 6)
};
```

The module's entry points (`render/include/c5r/units/units.h`, sketched; exact signatures are U1's):

```
class UnitRenderer {
    Status open(ID3D11Device*, const gamefs::GameFiles&, models::TableSource& db, const std::wstring& exe_path);
    Status prepare(const UnitScene&, const heightfield::HeightField&, FramePlan& out);  // L0-L3, CPU only
    Status draw(ID3D11DeviceContext*, const FramePlan&, const PassSet&);                // L1 uploads and draws
    Status precheck(std::vector<UnitTypeStatus>& out);                                  // section 7
};
```

`FramePlan` holds per unit a `UnitStatus` (section 7) and, for drawable units, the meshes, figures, palette blocks,
id ranges and textures; it is what the oracle compares before any draw (DoD B, D, E).

## 3. The file form: the unit-scene file (`.c5units`, format 1)

UTF-8 text, LF or CRLF, one record per line, fields separated by one tab, `#` starts a comment line, blank lines
ignored. Strict: an unknown record kind, a wrong field count, a number that does not parse completely, a value out of
range, a duplicate `(player, id)`, a `figure` before its `unit`, or a missing `c5units 1` header is refused with the line
number and a named code (`BadHeader`, `BadRecord`, `BadValue`, `Duplicate`, `Orphan`, `TooLarge`), and nothing of the
file is used. Limits: 64 players, 4,096 units, 64 figures per unit, line length 4,096 bytes.

```
c5units	1
scene	<position_source exe|own>	<water_level>
player	<player>	<colour Type>	<art_style>	<era Type>
unit	<player>	<id>	<unit_type>	<art_row or ->	<plot_x>	<plot_y>	<root x|->	<root y|->	<root z|->	<facing>	<seed>	<damage>	<max_hp>	<alive|->	<flags>	<fade_from>	<fade_to>	<fade>
figure	<figure>	<clip>	<t>
```

A `figure` line belongs to the last `unit` line. Floats are written with `%.9g` (round-trip exact). `<facing>` is an
integer **-1..7** (corrected 2026-11-16 by W-4470 from issue 4464, `render/src/units/scene_file.cpp` [C]: 0..5 are the six
DirectionTypes; 6 (NUM_DIRECTION_TYPES), 7 (the fork's NO_DIRECTION, which a Lua `InitUnit` without a facing leaves,
`unit-formations.md` 12.7.7 (4)) and -1 (stock's NO_DIRECTION, 3907's catalogue) give 1 rad through `facingYaw`, as the
EXE's switch; the reader refused the DLL's 7 before, 4458; was 0..5). The writer and the
reader are one source file (`scene_file.cpp`); a positive test round-trips every field and a negative test per refusal
code checks the message and that the scene stays empty.

## 4. From the feed to the input (what U6 maps)

| Input field | From (`unit-feed-contract.md`) | Notes |
|---|---|---|
| `player`, `id`, `unit_type` | UnitRecord `owner`, `id`, `unitType` (index -> `Units.Type` through the database) | [C] contract 2.2 |
| `art_row` | "" (the module resolves, 6.1); the UnitRecord's `artStyle`, `era` fill `PlayerInput` | the contract carries them per unit; they are the owner's, so per player here [I: equal for all units of one owner at one moment] |
| `plot_x`, `plot_y` | UnitRecord `plot` | |
| `root_*`, `has_root` | mode exe: the newest `kKindUnitPosition` (contract 3.3); mode own: has_root false | the arrival snaps to the plot |
| `facing` | UnitRecord `facing` | -1..7 in the .c5units file (4464): 6, 7 (the fork's NO_DIRECTION) and -1 give 1 rad (`facingYaw`); the feed takes it only at a creation and a move (`unit-server-scene.md` 2.2, 4463) |
| `formation_seed` | not in the feed: U6 keeps the EXE's allocation rule over `kUnitCreated` / `kUnitDestroyed*` in feed order (unit-formations.md 12.1) | **contract gap**, added to N5 as its section 8a by this note [I]; *closed 2026-11-16 by U6: `srvunits::SeedAllocator`, `server/src/units/srv_units_seed.*` (4453, 4462), `unit-server-scene.md` 4* |
| `damage`, `max_hp`, `alive` | UnitRecord `damage`, `maxHP`; `alive` -1 | |
| `flags` | UnitRecord `flags` (same bits, contract 2.2 order) | |
| `fade_*` | from the last `kUnitVisibility` (blendTime) | U5/U6; offline (1, 0, 0) |
| `figures` | absent until U5 | U5's controller fills clip and t |
| `PlayerInput.colour` | the player's colour Type: not in the feed | **contract gap** [I]: one PLAYERS line per player in the bundle, or from the save's player slots; U6 names it; *closed 2026-11-16 by U6: the player table, `feed.bin` record 6 PLAYERS (`SrvFeed::PlayerRecord::colour` -> the `PlayerColors` row's Type; MC decision 2, #2105), `unit-server-scene.md` 2.4, 5* |
| `water_level` | the bundle's height field (C5HF) | |

Two contract gaps (seed, player colour) are named for U6; neither changes N5's accepted kinds or records. *(2026-11-16,
recorded by W-4470: both closed by U6, epic 0375, without a new event kind; `unit-feed-contract.md` 8a. U6 writes
`PlayerInput.art_style` = the owner's civilization or minor-civilization Type, as the lead decision below asks, and `era` = the player row's
era Type; the art row stays "" for the module to resolve. The map from the feed to this input as built is
`unit-server-scene.md` 3.2.)*

**Lead decision 2026-11-15 14:20 (queue #1858/#1859, minor):** the cultural suffix belongs to a civilization, not to
an `ArtStyleType` [C, database, read by 4352: `ARTSTYLE_ASIAN` civilizations carry `_ASIA` and `_MED`,
`ARTSTYLE_EUROPEAN` `_EURO` and `_MED`, `ARTSTYLE_MIDDLE_EAST` `_AFRI` and `_MED`]. So `PlayerInput.art_style` takes the
owner's `ArtStyleSuffix`, or a `Civilizations` / `MinorCivilizations` Type (its suffix is used), or an `ArtStyleType`
only when all its civilizations share one suffix (else `BadInput`). U6 writes the owner's civilization Type: the
UnitRecord's `artStyle` (contract 2.2) alone does not determine the cultural variation.

## 5. The passes (U1)

Per pass the module needs a `PassDesc`: the pass kind (scene, reflection, shadow mask), the render target and depth
views (none for the mask), the viewport, and cb1's twelve registers: camera rows 0-2 (not read), camera position (3,
11), view-projection rows (4-7), cb1[8].x the height threshold (the water level in every capture, `unit-draws.md` 3),
fog factors (9) and fog colour (10). States per pass as `unit-draws.md` 1: mask = no depth, blend off, cull front,
programs VS `8cb52b96` / PS `72c27aea`; reflection = depth, src-alpha / inv-src-alpha, cull front, VS `29a0b061` / PS
`60d270e1`; scene = depth, src-alpha / inv-src-alpha, cull back, same programs. Material 89 (alpha) and 87
(not skinned, `UnitShader_CODE`) have their own programs (K9, `unit-playback.md` 6); 87's pairs and 20-byte layout are
read by U1 from the L06, L09 and S04 captures.

Per mesh-instance the palette block of 98 float4 rows: row 0 team colour (rgb, a 1), row 1 `(fade_from, fade_to,
fade, 0)` (w not read by any program [C]), rows 2-97 the 32 bones' 3 x 4 skinning matrices
`[Rz(yaw) R_k | s Rz(yaw) t_k + T]` (`unit-playback.md` 1.1). Blocks are numbered per frame from 0, a figure's meshes
consecutive, 167 blocks per 262,144-byte buffer, a second buffer after it (`unit-draws.md` 4, 6). One
`DrawIndexedInstanced` per mesh of a model per unit, `InstanceCount` = that member's figures, ids from an 8,192-byte
R32_UINT stream at a per-draw offset, `StartInstanceLocation` 0 (as the EXE). Rows of bones a mesh does not weight
are written as identity (the EXE leaves stale values; no program reads them for that mesh [C]) and excluded from the
comparison as `unit-draws.md` 4 says.

PS slots t0-t5: t0 the member's base texture (from the `.fxsxml`), t1 `SREFMap`, t2 the irradiance cube, t3 and t4
the dull and sharp environment cubes, t5 the distance-fog ramp (`unit-draws.md` 1). Where t1-t5 come from (install
file or made by the EXE each frame) is **open premise P1**; U1 answers it from the capture's resource names and the
install, and until then the oracle binds the captured ones and says so.

## 6. Load path and stages

### 6.1 The art (L0) [C chain, I for the variation names]

`Units.Type` -> `UnitArtInfo` (or the era variation `<base>_<era>` when `UnitArtInfoEraVariation` is set and a row
exists for the owner's era prefix; the cultural variation `<base><ArtStyleSuffix>` when `UnitArtInfoCulturalVariation`
is set) -> `ArtDefine_UnitInfoMemberInfos` (member, NumMembers) -> `ArtDefine_UnitMemberInfos` (Model, Scale,
Domain, ZOffset, material) -> `.fxsxml` -> `.gr2`, textures, animations (`scene-catalogue.md` 3, `model-reader.md` 3,
`models/` chain 3 `resolve_unit`). Files through `render/src/gamefs` `GameFiles` (install packs, DLC, and the active
mods as `extraLooseRoots`), adapted to `models::FileProvider`. The database is a `models::TableSource` (offline: the
game's `Civ5DebugDatabase.db` copy written with the mods applied, read-only; in the server: U6's). The resolution of
`art_row` is checked against 3907's `index.csv` `art_row` for all 229 placed rows (DoD B).

### 6.2 Models and draw inputs (L0, L1)

Per member model, once: `.gr2` read, meshes repacked by material (90/89: 28-byte stream, `repack_unit_mesh`, with the
weight-tie rule; 87: the 20-byte stream), positions times the member's `Scale`, VP's FBX-exported models turned y-up to
z-up first (open premise P4), 16-bit indices, the base texture decoded from DDS, kept in a model cache with its GPU
buffers. The unit programs' bytes are taken from the EXE file in the install by their hashes (`render/src/shaders`
`shadersection`), never shipped (D83). The default clip of a figure (no `FigureInput`) is the member's idle, or the
fortify idle with flag 2 (contract 8) [I: by name, decision 6].

### 6.3 Pose (L2)

Per figure: clip by name from the member's `.fxsxml` animation list, `bind_clip` once per (model, clip) and the decoded
control points kept in a **clip cache** (sheet 3.1), `sample_local` at t, `world_from_local`, skinning matrices with
the mesh's bone bindings, `palette_rows(scale, yaw, T)`. Figures are numbered in member order: member infos in
`ArtDefine_UnitInfoMemberInfos` row order, each repeated NumMembers times [I: the EXE's order; the oracle's id pairing
checks it].

### 6.4 Placement (L3)

The unit's formation from `UnitFormations.xml`, the slot rule (rows, centre-out order, bend, random part seeded by
`formation_seed`, centring; `unit-formations.md` 3, 12.3), rotated by the facing's yaw and moved to the root; each
living figure takes the next slot, dead ones are left out (section 7 there); z = max(height field at the slot,
truncated sample, `water_level`) + the member's `ZOffset` (12.4, R4/D132); no tilt. The root in mode own is the plot
centre from the height field's plot relation.

## 7. Failure statuses and the pre-check (DoD F)

`UnitStatus` per unit, one code, the first that applies, with a text naming the row, file or mesh:
`Ok`, `NoArtRow` (no row for the type and owner), `NoMembers` (an art row with no member infos), `ModelNotFound` (the
`.fxsxml` or `.gr2` in neither install nor mods), `BadModel` (the reader refused it), `TooManyBones` (a mesh weighting
more than 32 bones; K8 found none in world models), `UnknownLayout` (a vertex layout outside 87/89/90),
`CurveUnsupported` (a curve format the sampler does not decode), `ClipNotFound` (a clip name the member does not have),
`TextureNotFound`, `ProgramNotFound` (a unit program hash not in the EXE file), `PlacementFailed` (a formation name not
in `UnitFormations.xml`, a plot outside the height field). A unit not `Ok` is not drawn at all; the frame's other units
are. `precheck` runs 6.1-6.2 and binds every clip of every member for every `Units.Type` of the active database (and
every era and cultural variation row it can reach) and returns one status per type. What U6 does with it is the MC's
decision 7 (recommended: a failure at game start takes D125's route, one in play is an R6 fault, logged, counted,
that unit not drawn). *(2026-11-16, recorded by W-4470: as built by U6, epic 0375, `unit-server-scene.md` 3.3 and 10:
the scene runs `precheck` on a worker thread with its own CPU-only `UnitRenderer` at each new unit database; an entry
not `Ok` is `PrecheckFailed` by D125's route (shown live on the shipped pack layout, oldpc, 2026-11-16 [L], 10); a
warning (`ClipMissingInInstall`, `TextureFallback`) is logged and the unit drawn; in play a unit the module refuses is
`UnitRefused`, LOUD once per type and code, counted, not drawn. On VP's database: 233 art rows, 0 failed, 2 warnings,
14-28 s on the workstation, 13.9 s on oldpc, 3.4.)*

**Lead decision 2026-11-15 15:10 (queue #1893/#1894, minor): the EXE's own fallbacks are reproduced, not refused.**
Where the EXE itself draws a unit with something other than its named input (first case: Tadodaho's `firekeeper.gr2`
names an SREF map `black_4x4.dds` that no install or mod file holds, and the EXE binds an 8 x 8 texture of its own,
4356 [L, one data point]), the module does the same: that is the stock picture (D66), not a placeholder of ours,
which D118 bars. Such a unit stays `Ok` with a named warning (the file it stood in for) that the pre-check lists and U6
logs. A missing input for which the EXE's behaviour is not known stays a failure status.

## 8. Open premises

| # | Premise | Mark | Answered by |
|---|---|---|---|
| P1 | PS slots t1-t5 (SREF map, three cubes, fog ramp) come from install files, not made by the EXE per frame | **answered [L]** (4352, 10.3): t1 is the member's SREF map; t2-t5 are `unit_irradiance.dds`, `unit_environment_dull.dds`, `unit_environment_sharp.dds`, `fogramp.dds` from the install (the two A8R8G8B8 files uploaded with red and blue swapped) | done |
| P2 | Material 87's 20-byte vertex layout is a fixed repack of the `.gr2` mesh like 89/90 | **answered [L]** (4352, 10.2): half4 position x Scale, half4 normal / Scale, half2 uv; byte-equal on all 3 material 87 meshes; its cb0 rows 0-4 are world matrix, colour and fade | done |
| P3 | Header row 1 is (1, 0, 0) for every unit at rest; damaged, embarked, fading and a second owner change only row 0 or row 1's x, y, z (K4's rest) | **answered [L]** (4353 on the fresh capture, 2026-11-15 14:54): row 1 = (1, 0, 0) for a damaged unit (barbarian warrior, 8 of 12 figures), an embarked one (2 instances) and a second owner; row 0 = the owner's colour (Polynesia 0.851, 0.345, 0; Carthage 0.8, 0.8, 0.8; barbarian 0, 0, 0); a warrior fading in had row 1 = (0, 1, 1), i.e. alpha from 0 to 1 at fade 1 | done; the fade's time course is U5/U6's |
| P4 | VP's FBX-exported models need y-up to z-up for vertices and placement, and the EXE applies it at load | **placement: no basis change [L]** (4354, section 12, 2026-11-15); skeletons: M (x, y, z) -> (x, -z, y), axes only (4353, section 11); vertices **[L]** (4352, 10.2): the same M on position and normal before the repack, 39 of 42 turned meshes byte-exact (rest one-step differences); the EXE code that applies it not found. **Found 2026-11-16 (0379, 10.9) [C, L]:** Granny's basis conversion at model load (0x8ECC50 -> 0x9C0C60, desired right (1,0,0), up (0,0,1), back (0,-1,0)), on vertices, skeleton and clips; with it all 43 drawn turned meshes of the report are byte-exact (45 turned, 2 absent) | done |
| P5 | Curve format 3 in the orientation slot and degree 3 on several bones decode as the synthetic tests say | **format 3 answered [L]** (4353, 11.4): a mech fits idle A at 3.6000 s with 0.00032 degrees on 32 bones, 4 of them format 3. **Multi-bone degree 3 open [C synthetic only]:** no capture holds it: the fresh capture's tanks were idle, no warrior, horseman or barbarian clip of P01-P05 has degree 3 on bound bones, and none of the 1,535 fitted catalogue instances uses a clip with degree 3 on two or more bound bones (4355, 11.4). **Multi-bone degree 3 answered 2026-11-16 [L] (0379, 11.4):** the fresh capture does hold it: mech block 51 (frame 2676) is `Mech_IdleB.gr2` at 4.8985 s under the aim layer, with degree-3 orientation curves on 6 of the Mech mesh's 32 bound bones, and the module's pose program reproduces its rows to rotation element 1e-6 and translation 5.6e-5 | done |
| P6 | The 0.07-unit common shift of figure positions is a fixed offset of the plot centre or of the slot rule | **answered [L]** (4354, 2026-11-15): the map's hex-to-world matrix carried by the C5HF tap (not the nominal 64/32/128 lattice) gives every unit's shift to 0.0004 median; `plotCentre()` uses it | done |
| P7 | The Mamluk's eighth horse is skipped by a rule (per-mesh culling or a limit) that can be named | **not reproduced [L]** (4353, 2026-11-15 14:54): on the fresh capture the Mamluk draws 8 horses and 8 riders in all three passes at two angles (ids paired horse k, rider k + 1); damaged to 7 figures it draws 7 + 7 (the last member gone with both meshes). A "member index 8" rule and a "last living figure loses its horse" rule are both ruled out; L06's case stays unexplained [I: transient]. Frustum, clip, per-draw limit and passes ruled out by 4354 (12.5) | closed as not reproducible; the module draws every member's meshes |
| P8 | Figures are numbered in member-info row order, each member repeated NumMembers times | **holds [L]** (4355, 13.5): 205 units agree, none contradicts, 24 not checkable (cause per unit); the first round's 8 "differ" were an artefact of grouping ids. Within a figure the EXE numbers the blocks in the first Model's MeshBindings order, not the Meshes list (13.5; the module follows it) | done |
| P9 | Clip time t and clip from `posefit` reproduce every catalogue figure (K3 on the whole catalogue) | **holds [L]** (4355, 13.6): 1,479 of 1,482 comparable within 0.05 degrees and 0.01 units (3 = their fits' residuals); the 51 instances 0327 left unfitted all fit with the module's C++ sampler (19 unambiguous within 0.0036 degrees, 32 rigid parts posed by the figure's body); 53 tilted fits not compared | done |
| P10 | Era and cultural variation row names as `scene-catalogue.md` 3 states | **answered [L]** for the catalogue (4352, 10.2): 231 / 231 rows; the suffix is per civilization; era rule beyond the catalogue's rows [I] | done for the catalogue |

A premise that fails on a unit 0327 did not cover is reported to the MC at once (brief, Reporting).

## 9. Definition of done, made final at C1 (the brief's A-I with these changes)

- **A** as the brief. This file is A's design; the MC's answer is quoted here and in the issue's log.
- **The MC's C1 answer (2026-11-15 14:08, queue #1845), in short:** accepted, workers may commit. (1) linking
  `c5models` from `src/units/CMakeLists.txt` agreed; (2) `UnitScene` and `.c5units` confirmed as the input U6 fills,
  the two gaps (seed allocator by the EXE's rule, player colour Type) carried by U6 (ticket 0375); (3) 0346 folded in
  and closed as split; (4) shadow-mask and reflection passes proven offline here, product wiring later; (5) the fresh
  capture: one pool instance claimed under 0041, RenderDoc on Tiny as 0301, at most 6 captures and 2 GB under
  `F:/c5-64bit-work/0041`, harness stop only and never during an autosave, SHA-256 backups and restores, pictures
  DIAGNOSTIC; (6) no controller, clip and t are inputs, default clips by name (U5 = ticket 0374); (7) for U6: a
  pre-check failure at game start takes D125's route; **the pre-check must cover every art row the game can reach**
  (all unit types for the players' art styles and eras present), so a failure in play is a bug: logged LOUD, counted,
  listed in the session's diagnostic report, that unit not drawn, never a silent gap; (8) U4 with 0040, U5 = 0374,
  U6 + U7 = 0375; `unit-playback.md` 8's K4 naming corrected with a dated note. DoD A-I of this section accepted.
- **B** as the brief; the 229 placed rows' art resolution (P10) added; units whose clip cannot be fitted (cross-fade
  blocks, `unit-playback.md` 1.4) are listed with that cause and compared for L0/L1 only.
- **C** as the brief. Targets set up from the capture's descriptions; the scene pass compared over the captured depth,
  the reflection and mask passes from their captured clear state. DIAGNOSTIC pictures only.
- **D** as the brief.
- **E** as the brief; format 3 and multi-bone degree 3 against the fresh capture **if decision 5 is granted**, else
  named open with the synthetic evidence.
- **F** as the brief with the statuses of section 7; each status has a negative test.
- **G** as the brief: `render/tests/units/` and `models/tests/` plain and once under MSVC `/fsanitize=address` with 0
  reports; skipped counts as failed; terrainreplay's existing outputs SHA-256-equal base against branch.
- **H** as the brief.
- **I** as the brief: `unitscene` runs the whole oracle set with one command and writes a report per unit and pass.

## 10. U1: art, draw inputs and passes (4352)

Written 2026-11-15 14:11-15:00 EST by the 4352 worker (account B, branch `task-4352-u1`). Offline, workstation; no game
run of its own (P3 reads 4353's fresh capture). **Result: the module builds every draw input of the catalogue from the
install, the mods and the database; its own draws equal the captured ones bit for bit in all three passes for the five
units tried (the warrior's two weight-tie vertices aside); premises P1, P2, P4 (vertices), P10 answered; P3 answered for
two owners, damage, embarking and a fade-in.**

Inputs (all read only): 3907's 20 unit exports and 3901's export (`F:/c5-64bit-work/0327/exports/3901`), the frame
records `shell/evidence/0301/3907/index.csv` (229 placed units) plus 3901's warrior and pathfinder; c5h-c's install and
its MODS (`(1) Community Patch`, `(2) Vox Populi`, `(3a) VP - EUI Compatibility Files`) as `extraLooseRoots`; database
`F:/c5-64bit-work/0327/db/Civ5DebugDatabase.db` (c5h-c's, mods applied); EXE c5h-c's `CivilizationV_DX11.exe` (build
4390913). Capture setup as `unit-draws.md` (32-bit `i0ship-26234371-ia`, CP/VP/EUI, VT present).

### 10.1 The code [C]

| File | What |
|---|---|
| `render/include/c5r/units/units.h` | `UnitRenderer` (`open`, `prepare`, `draw`, `precheck`, `loadUnit`), `FramePlan` / `UnitPlan` / `MeshDraw`, `IdStream`, `PoseSource` and `PlacementSource` (the L2 / L3 steps behind interfaces; the oracle and tests inject their own). *Since 4464 (2026-11-16, epic 0375; `unit-server-scene.md` 3.4): L0 + L1 cached per art row (`loadRow`, `rowOf`; `setArtCache(false)` gives the rule as before), dropped at `open()`; `UnitPlan::art` a `shared_ptr<const UnitArt>` into that cache with `unit_type` "" (the unit's type is the scene's)* |
| `status.h`, `src/units/status.cpp` | `UnitCode`: section 7's codes plus `BadInput` (owner not among the scene's players, a colour Type not in `PlayerColors`, an art style the database does not know, a figure index past the unit's figures) |
| `art.h`, `src/units/art.cpp` | `GameFilesProvider` (GameFiles as models' `FileProvider`), `ArtResolver` (tables once; art row rule; members; team colour). *Since 4464: results cached per art row, a failure included (`resolve_shared` hands the cached art without a copy, `resolve_uncached` is the rule, `fxsxml_parses()` counts `resolve_fxsxml` calls, `clear_cache()`); the team colour's rounding is `trunc(float32(v) x 255) x float32(1/255)` since 4453 (10.3)* |
| `draw.h`, `src/units/model.cpp` | `ModelCache` (per (gr2, Scale): repacks, textures, IMMUTABLE buffers), `repackSkinned` / `repackStatic`, `meshMaterial`, `applyBasis`, `widenReal16`, `loadStringDatabases`. *Since 0447 (2026-11-16): the vertices by the EXE's load rule (10.9) through models' `unit_basis` / `repack_unit_mesh`; `applyBasis` / `widenReal16` / `positiveZeros` are off the load path* |
| `src/units/programs.cpp` | `UnitPrograms`: the 12 program references (6 distinct VS / PS pairs of materials 90, 89, 87 x pass 0 / 1) found in the EXE's `.shader` section by SHA-256 (digests only in the source, D83); input layouts per VS; `sha256Hex` (Windows CNG) |
| `src/units/draw.cpp` | `prepare` (L0-L3 per unit, all or nothing), `buildIdStream`, `draw` (states of `unit-draws.md` 1, palette buffers, id stream, cb1, material 87's cb0), the environment textures |
| `src/units/draw_steps.cpp` | the module's own steps: `ModulePose` (4353, `bindAll` as the clip check) and `placeUnit` (4354). *Since 4466 (2026-11-16, epic 0375, MC grant #2395 of `pose.cpp` / `pose.h`; `unit-server-scene.md` 3.4): the pose memo: the default clips found once per pose model (`PoseModel::idle` / `fortify`, `defaultClipIndex()`), the model and clip binding through the last request's member / model pair and a (model, skeleton, clip) map, and one sampling per figure for all its meshes (a ring of the last 8 world poses); its rows byte-equal to `poseFigure` / `poseFigureProgram` (test `memo_rows_byte_equal`); counters `worldHits()` / `worldSamples()`* |
| `src/units/precheck.cpp` | `precheck` |
| `src/units/draw_check.cpp` | `makeCb1` / `cameraFromCb1` and the DoD B checks, one function each: `compareVertices`, `compareIndices`, `differOnlyAtTies`, `headerRowsEqual`, `cb1ReadLanesEqual`, `idsFigureMajor` (for 4355) |
| `render/tests/units/test_art.cpp`, `test_model.cpp`, `test_draw.cpp`, `test_precheck.cpp`, `test_draw_oracle.cpp` | ctest; `test_draw_oracle` is also the catalogue checker (`--manifest ...`) and the DoD C drawer (`--draw ...`) |
| `render/tests/units/units_mods.h`, `test_mod_roots.cpp` | *Since 0443 (2026-11-16):* the mod roots of the install the tests read, for `test_model`, `test_art_cache` and `test_pose_catalogue`: `C5R_UNITS_MODS` (';'-joined) when set, else, when `<C5R_GAME_DIR>/../instance.json` exists (a pool instance of `tools/instances/provision.py`), every folder of `<instance>/<user_dir>/MODS` sorted by name (the server's every-folder rule), else none (VP as DLC in the install). The user's own Documents is never read. Each test prints `mod roots (<branch>): N` and the folders |
| `render/evidence/0041/4352/` | `oracle_manifest.py`, `draw_compare.sh`, `draw_compare_table.py`, `record_checks.py`, `header_rows_scan.py`, and the numbers |

### 10.2 The art rows, models and the vertex streams

- **The art row rule (P10) [L 231/231].** Cultural (`UnitArtInfoCulturalVariation`): `<base><ArtStyleSuffix>` when that row
  exists, else the base. Era (`UnitArtInfoEraVariation`): `<base>_<Eras.ArtPrefix>` for the owner's era and then each
  earlier era, then `<base>_LATE` from the Industrial era on, else the base [C code; I: the EXE's own order of tries, only
  the 15 era rows and 1 cultural row of the catalogue confirm it]. The suffix is per civilization, not per
  `ArtStyleType` (Asian civs carry `_ASIA` or `_MED`): `PlayerInput.art_style` takes a suffix, a civilization or minor
  civilization Type, or an ArtStyleType whose civilizations share one suffix (lead's answer to queue #1858: U6 writes
  the owner's civilization Type). All 231 rows (229 + 3901's 2) resolve to `index.csv`'s `art_row`; and the meshes of
  those rows are the drawn ones (10.5), which checks the rule against the EXE, not only against 3907's script.
- **Since ticket 0447 (2026-11-16) the module loads every mesh by the EXE's rule of 10.9** (basis conversion, cofactor
  normals, Granny's halves, float weights; the stream chosen by bone bindings); the stream, FBX, Real16, tie and
  one-step paragraphs below describe the earlier rule and what it left, kept for their numbers.
- **28-byte stream** = models' `repack_unit_mesh` (unit-playback.md 3). **20-byte stream (material 87, P2) [L]:** a fixed
  repack like 89/90: POSITION half4 (Position x Scale, w 0), NORMAL half4 (Normal / Scale, w 0), TEXCOORD half2;
  float32 products, halves to nearest with ties away from zero. Byte-equal on all three material 87 meshes of the
  catalogue (Mech_GunBarrel L06, SS_Booster_Brackets L09, Cargo_Ship S04). Material 87 is a mesh with no BoneWeights;
  89 a skinned mesh whose material's `ShaderSet` contains "Alpha" (Workboat_Net, S04: exact) [L].
- **FBX basis (P4, vertices) [L]:** files whose ArtToolInfo axes are not the stock ones (pose.h `fileBasis`: VP's
  FBX exports, y up / back +z, M = (x, y, z) -> (x, -z, y)) take M on Position and Normal before the repack. 42 such
  meshes in the catalogue (Mamluk, Black Tug, Chewa, Cacador, Commando, Grenzer, Klepht, Goedendag, Inti maceman, Heavy
  skirmisher, Dhanurdhara, Otomi, Pancerny, ...): all byte-exact except 2 single one-step differences of the kind below
  and the Cruiser (16 vertices). So the EXE turns the vertices at load as the pose layer turns the skeleton.
- **Real16 sources [L].** Seven DLC unique units (Babylonian bowman, Danish ski infantry, Incan slinger, Korean
  Hwacha, Mayan atlatlist, Mongolian Keshik, Polynesian Maori warrior; Korean turtle ship, Danish longboat,
  Polynesian canoe at sea) store Normal, Tangent, Binormal and TextureCoordinates0 as Real16. They are widened to
  float32 exactly (Tangent and Binormal dropped) and repacked; a -0 normal is stored by the EXE as +0 (9 of 9 cases;
  applying the same to Real32 meshes broke 2 exact ones, so it is limited to Real16 sources). These meshes still
  differ by one half step in 14-65 normals each [I: the EXE's half-to-float or the 1/Scale product]; not solved.
- **Meshes not drawn [L]:** damage states (name containing `dmg` or `_damaged`, or ending `_dam` / ` dam`: trireme
  `*_DMG`, `Portuguese_Carrack_DAM`, `Venetian Galleass DAM`, `U_Longboat_Damaged`, `PolynesiaCanoe_Damaged`) and
  meshes with no vertices or indices (`bombard.gr2`'s empty MilkShape object). [I: the EXE picks damage meshes by state;
  only undamaged units are in the catalogue.]
- **Weight ties.** `ModelMesh::tie_vertices` lists every vertex whose renormalised w0 or w1 is an exact half
  (unit-playback.md 3's rule rounds them up). The EXE rounds some ties down: 12 meshes differ only there, 1-6 vertices
  each, always w0 one lower and w2 one higher (`80>7f` = 128 -> 127, `d5>d4`): `archer_v3` / `barbarian_archer_v3`
  474, 484; `barbarian_swordsman_v2` / `swordsman_v2` 478; `u_austrian_hussar` (Horse) 224, 233; `u_polish_winged_hussar`
  51, 62, 231, 251; `warrior_v2` 428, 442; `settlers_afri_f2` 855; `great_artist_late` 257, 264, 270, 286, 328, 343;
  `great_artist_late_f1` 326, 340, 398, 407, 409, 419; `great_musician_late_f2` 798, 934. Other ties of the same meshes
  are captured rounded up (warrior_v2: 11 ties, 9 up), so no integer rule decides it (as 0327 found) [L].
  **Explained 2026-11-16 (ticket 0379, 10.9) [C, L]:** the EXE renormalises in float, w' = floor(fl(fl(255 / s) x w)
  + 0.5), on every vertex of a skinned mesh; fl(255 / s) decides each tie.
- **Other one-step differences [L, cause I].** Real32 meshes with 1-9 vertices one half step off in a position (42
  values) or normal component, and 21 weights off by one outside exact ties. 39 of the 40 position cases are matched
  when Scale is taken one to four float ulps larger; normals fit no single variant (n / s, n x (1/s), double) [I: the
  EXE scales through a 3 x 3 matrix built in float, e.g. Granny's basis conversion with units per metre]. One vertex of
  `ethiopiacompbow.gr2` holds a NaN position: we write 0xFE00, the capture 0xFFC0. **Explained 2026-11-16 (ticket
  0379, 10.9) [C, L]:** the inference held: Granny's basis conversion with desired units per metre = UnitsPerMeter x
  Scale gives the factor fl(fl(UnitsPerMeter x Scale) / UnitsPerMeter) and a cofactor inverse for the normals; the
  NaN keeps its payload through Granny's half conversion. (The Real16 units' half-step normals above and their -0 -> +0
  are the same rule, 10.9.)
- **Textures (t0, t1) [L].** **Corrected 2026-11-15 15:20 (task 4356, 10.8):** the EXE binds the textures the
  material's FireGrafix `UnitShaderTextures` parameters name; the 3ds Max maps and the `.fxsxml` list are only
  fallbacks (515 / 515 drawn meshes equal by SHA-256). (Replaced: "the mesh's `.gr2` material maps, each replaced by
  the member's `.fxsxml` `<Texture>` of that role when the `.fxsxml` does not list the map's name", which reached
  386 / 413 of 477.) Equality is by SHA-256 of all subresources against the capture.
- **Loose-over-pack** (4354's 12.4 case): no art lookup in the catalogue met it (every mesh, texture and program
  matched by bytes or by SHA-256) [L]; GameFiles' order is used unchanged.

### 10.3 Environment textures (P1), team colour, header rows (P3)

- **P1 answered [L]:** t2-t5 are install files: t2 `unit_irradiance.dds` (32 x 32 cube, A8R8G8B8), t3
  `unit_environment_dull.dds`, t4 `unit_environment_sharp.dds` (both `Expansion2UnitsTextures.fpk`), t5 `fogramp.dds`
  (128 x 8, 8 mips, A8R8G8B8, `MiscTextures`). The EXE uploads the two A8R8G8B8 files as R8G8B8A8 (channels swapped
  on load): their payloads with B and R swapped equal the capture's by SHA-256; the cubes are equal as stored. The
  module loads them at `open()` and binds them unless `PassSet::env` overrides. Nothing is made by the EXE per frame.
- **Row 0 [L, 6 owners + barbarian]:** `trunc(float32(v) x 255) x float32(1/255)` per channel of
  `PlayerColors.SecondaryColor`, alpha 1 (the current rule; restated here 2026-11-16 by W-4470 from the correction
  below, which this line still contradicted: it read `trunc(float32(v) x 255) / 255`). Morocco's 0.831372549 -> 212
  shows the float32 product (a double gives 211).
  **Corrected 2026-11-16 01:12 (lead-0374 for the MC, #2168; from 0375's worker 4453, live capture of 2026-11-16, 228
  instances, 6 owners; `unit-draws.md` 5.1 on 0375's branch `task-4453-seed`, not yet on main) [L]:** the rounding is
  `trunc(float32(v) x 255) x float32(1/255)`, a float32 product, not a division by 255: the division misses by one ulp
  for 3 of the 6 owners (channel values 57, 118, 229). The code fix in `render/src/units/art.cpp` is 4453's (the MC's
  grant). (Was: "`trunc(float32(v) x 255) / 255`".)
- **P3 (K4's rest) from 4353's fresh capture** (c5h-d, frames U01-U04, scene pass; `header_rows_scan.py`) [L]:
  two owners (U01: warrior of player 0 and of player 1, Carthage): only row 0 differs (0.851, 0.3451, 0 against 0.8,
  0.8, 0.8); embarked warrior (U02, the embarked model's meshes): row 0 the owner's, row 1 (1, 0, 0); damaged barbarian
  warrior and Mamluk (U03, U04, after a ranged attack): row 0 (0, 0, 0) (the barbarian colour), row 1 (1, 0, 0), and
  fewer instances (8 of 12, 14 of 16): damage removes figures, not header values; a city-state warrior that had just
  come into view (U03, U04): row 1 = (0, 1, 1), i.e. from 0 to 1 at fade 1, alpha 1. So row 1 carries the
  visibility fade (from, to, progress), and nothing else changed it in these frames. Not seen: a unit fading out or
  mid-fade, row 1 w (not read).

### 10.4 Passes, ids, cb1

- **States (`unit-draws.md` 1) [C, the export]:** mask: depth off, blend off, cull front; reflection: depth LESS_EQUAL
  with write, src-alpha / inv-src-alpha (alpha ONE / ONE), cull front; scene: the same with cull back; front
  counter-clockwise; six samplers MIN_MAG_MIP_LINEAR wrap.
- **Material 87 draws [C, L]:** one `DrawIndexed` per figure (InstanceCount 1) with a per-draw 256-byte cb0 of which
  the programs read registers 0-4: rows 0-2 the mesh's 3 x 4 world matrix, row 3 the team colour, row 4 the fade
  triple (VS `82e59838` reads cb0[0..2], PS `0e7a3566` cb0[3], cb0[4]). The id stream stays bound but is not advanced
  (L09: draw 28 at the offset of draw 27, which reads 3 ids from there).
- **Id stream [L]:** the 8,192-byte stream holds one region per pass, in pass order (L06: mask at byte 156, reflection
  560, scene 964 for the same draw); within a pass a draw's ids follow the previous draw's. Figure-major blocks
  (a figure's meshes consecutive) hold for 290 of 307 models in the scene pass; the 17 others are draws of one model
  shared by two members or units (Cossack v2/v3, battering ram, submarines, SS parts) where the checker paired the
  wrong draw [I]. The order of ids within a draw differs per pass (depth-sorted [I]); the module writes member order.
- **cb1 [L]:** `makeCb1(cameraFromCb1(captured))` equals the captured cb1 in every lane the programs read, 63 of 63
  (21 frames x 3 passes); r8.x = 10.1167316 in all, the water level.

### 10.5 DoD B and C numbers [L, 2026-11-15]

DoD B, the catalogue run (`c5r_units_test_draw_oracle --manifest`; report `render/evidence/0041/4352/oracle_report.tsv`).
**Corrected 2026-11-15 15:20 (task 4356, 10.8):** numbers of the run after 4356's texture rule; the report file is that
run's. (Replaced, 4352's run: 228 units loaded, 492 meshes, 399 + 12 exact, 66 differing, t0 386 / t1 413 of 477,
header rows 4,942 / 4,978. Mesh by mesh none of 4352's results got worse: `render/evidence/0041/4356/regression.py`.)

| | Count |
|---|---|
| units (3907's 229 + 3901's 2), art row rule equal to `index.csv` | 231 / 231 |
| units whose art loads (all members, models, textures) | 231 (Tadodaho with one `TextureFallback` warning, 10.8) |
| meshes of the loaded units' members | 530 |
| vertex buffer byte-equal | **515 since ticket 0447 (2026-11-16, 10.9; `render/evidence/0447/oracle_report.tsv`).** Was: 434 (+ 12 differing only at weight ties, 10.2) |
| one-step differences (10.2, 10.8) | 0 since 0447. Was: 69 meshes, none by more than one half step or one weight level |
| meshes with no draw in the frame | 15: the 5 L06 units 3909 found undrawn (Marine, Mech, Mechanized infantry, Merc x 2), the Spanish conquistador and tercio's 6 meshes in L13, Mayan atlatlist, Dutch_Flute2 (S03, a second hull variant [I]) and 3 damage meshes not caught by the name rule |
| index buffers equal | 515 of 515 drawn |
| t0 / t1 equal by SHA-256 | 515 / 515 |
| programs (VS and PS of material and pass) | 515 / 515 |
| header rows (row 0 within one 8-bit step, row 1 x y z) | 5,134 / 5,170 (the 36 others: L15's save-owned warrior of another player drawn from the same buffers) |

DoD C, the module's own draws (`draw_compare.sh`, `render/evidence/0041/4352/draw_compare.tsv`; GPU; our meshes,
textures, programs, states and environment textures; the captured palette, ids and cb1; target cleared to 0, depth to
1; reference: the captured draws replayed by terrainreplay):

| Unit | Scene: pixels, equal, max | Reflection | Mask |
|---|---|---|---|
| tank (L09 571-572 / 118-119 / 39-40) | 19,019, 19,019, 0 | 1,837, 1,837, 0 | 6,688, 6,688, 0 |
| anti-aircraft gun (L01 457-459 / 75-77 / 18-20) | 7,089, 7,089, 0 | 993, 993, 0 | 3,293, 3,293, 0 |
| trireme (S03 364-366 / 81-83 / 33-35) | 2,971, 2,971, 0 | 403, 403, 0 | 1,161, 1,161, 0 |
| warrior (3901 369-371 / 58-60 / 13-15) | 14,724, 14,711, 1 | 1,548, 1,547, 1 | 4,382, 4,382, 0 |
| SS booster brackets, material 87 (L09 560 / 107 / 28) | 1,387, 1,387, 0 | 248, 248, 0 | 767, 767, 0 |

Median and p95 are 0 everywhere; the warrior's 13 + 1 pixels are the two tie vertices. Mask coverage is the union
of the pixels either picture wrote (terrainreplay's id buffer stays empty without depth). Pictures, all DIAGNOSTIC:
Visual Checks `vc-20261115-144526-workstation-7f55` to `vc-20261115-144536-workstation-39c2` (15 entries, issue 4352).

### 10.6 Failure statuses and the pre-check [C, tests]

Every code of 7 is reached by a test: `test_art` (NoArtRow, NoMembers, ModelNotFound, BadModel, BadInput),
`test_model` (TooManyBones, UnknownLayout, BadModel, ModelNotFound, TextureNotFound against the install with the
texture hidden), `test_draw` (prepare: BadInput x 2, NoArtRow x 2, NoMembers, ModelNotFound, PlacementFailed,
ClipNotFound, CurveUnsupported; ProgramNotFound with no EXE; a draw on WARP of all three passes), `test_precheck`
(Ok with 4353's real clip binding, ModelNotFound, NoMembers, NoArtRow, CurveUnsupported from a clip check). A unit not
Ok has no draws and adds no palette block. A missing program does not stop `open()`: the units of that material get
ProgramNotFound. `precheck` walks every `Units.Type` x every art style suffix x every era, one entry per distinct art
row reached.

**ClipMissingInInstall (issue 4460, epic 0375, 2026-11-16; MC ruling of 2026-11-16, `render/src/units/precheck.cpp`
granted to 0375).** VP's database (the catalogue copy `F:/c5-64bit-work/0327/db/Civ5DebugDatabase.db`, 215 units, 233
distinct art rows reached) has one row whose member's `.fxsxml` names an animation file that no package ships:
`ART_DEF_UNIT_U_ASSYRIAN_SIEGE_TOWER`, member `ART_DEF_UNIT_MEMBER_U_ASSYRIAN_SIEGE_TOWER`, file
`U_Assyrian_Siege_Tower_Charge_Attack.gr2` [C: not loose in the install (only `U_Assyrian_Siege_Tower3DScripts.xml`
exists), not in the CP / VP / EUI mod folders, and the module's file system finds it in no package]. The pre-check made
it `ClipNotFound`, a failure, so `render_units=1` ended every VP game by D125's route. **Rule since 4460:** an animation
file the art file names but the install does not ship is the named warning `ClipMissingInInstall: <art row>: member
<m>: animation file <f> is named by the art file but shipped by no package (the unit is drawn)` on a row that stays
`Ok`; the clip binding then checks the clips that exist. Every other missing file (a model, an `.fxsxml`, a texture
without the EXE's fallback) stays a failure. The server's pre-check line counts the warnings by kind (`units: pre-check
of generation G: N art rows, F failed, W warnings (ClipMissingInInstall 1, TextureFallback 1), T ms`). Tests:
`test_precheck` `precheck_clip_missing_in_install_is_a_warning` (the stock siege tower row Ok with one warning naming
the Charge_Attack file, beside `UNIT_BADMODEL` still `ModelNotFound`) and `server/tests/units/test_scene_units.cpp`
`precheck_clip_missing_in_install` (the scene's pre-check passes and logs `1 warnings (ClipMissingInInstall 1)`).
**At run time [I]:** what the EXE does when the unit's charge attack is asked for is not known (MC: a capture of the
siege tower's charge attack is added to 0395); the rule for the server is that a request for that clip invents no
default: the controller keeps the unit's current state and logs one LOUD line per occurrence (unit-controller.md 5,
same date). Today the server's scene asks only for the default clip (no controller wired yet, unit-server-scene.md 6),
so the missing clip is never requested.

### 10.7 Not verified

- The era rule beyond the catalogue's rows (an era with no row of its own between two that have one); the cultural
  rule for `_AMER` / `_ASIA` (no such player in the save).
- ~~The 69 one-step meshes and the tie direction (10.2, 10.8).~~ Answered 2026-11-16 by ticket 0379 (10.9) [C, L].
  (Corrected 2026-11-15 15:20 by 4356: the texture
  mismatches and the 3 TextureNotFound units are solved, 10.8; replaced "the 91 t0 and 64 t1 mismatches of VP art;
  3 TextureNotFound units".)
- The fallback texture for a missing file is one data point (Tadodaho's t1, 10.8); t0 never needed it.
- Palette buffers past the first (L11 needs 182 blocks; the module splits a draw across buffers, the EXE's rule for a
  draw at the boundary is not known); the per-pass id order.
- The whole-frame id equality (needs every unit of the frame in the scene: 4355).

### 10.8 Textures by material parameters, the EXE's fallback texture, the one-step meshes (4356)

Written 2026-11-15 14:58-15:20 EST by the 4356 worker (branch `task-4356-u1-followup`), same inputs as section 10; scripts
and numbers in `render/evidence/0041/4356/`. **Result: t0 and t1 equal the capture on all 515 drawn meshes; the three
units 4352 refused load and match; the one-step vertex differences are not reproduced by a units-per-metre rule and
stay named.**

- **Which texture the EXE binds [L, 515 / 515].** A unit `.gr2` material's `ExtendedData` (`typeName`
  `FireGrafixMaterial`) holds `ParameterSetCount` structs `ParameterSet0..N-1`. The set whose `ParamBlock` is
  `UnitShaderTextures` lists `ParamNames` (`BaseTextureMap`, `SREFMap`, `IrradianceCubeMap`,
  `DullEnvironmentCubeMap`, `EnvironmentCubeMap`, `DistanceFogRamp`) and per name a `ParamStoreIdx` into `ParamDataT`,
  an array of `{String}` holding file names (4 slots per parameter). The EXE binds `BaseTextureMap` as t0 and `SREFMap`
  as t1 from there. VP art that reuses a stock `.gr2` changes only these strings: the 3ds Max `Maps` keep the stock
  names (`ambassador.gr2`'s mesh "Archeologist": maps `archeologist_diff.dds`, parameter `ambassador_diff.dds`, bound
  `ambassador_diff.dds`), and FBX exports have no `Maps` at all (`Knight_Turkey.gr2`: parameters `Knight_Turkey.dds`,
  `Knight_Turkey_gloss.dds`). Found by naming every texture the capture bound: `tex_index.py` hashes every `.dds`
  payload of the install extraction, the install's loose `Assets` and the three mod folders (11,670 files), and
  `tex_names.py` names each captured t0 / t1 of a mismatched mesh. Code: `render/src/units/model_params.cpp`
  (`materialTextureParams`, over models' typed object view, nothing in `models/` changed); `ModelCache` takes the
  parameter, else the 3ds Max map, else the `.fxsxml` texture of the role.
- **4352's oracle artefact.** Two units of one frame can draw one mesh with their own textures (L01's archer and
  barbarian archer share `Archer_MOD`); 4352's checker failed a texture when any draw of the shared buffer differed.
  It now counts a mesh's texture equal when one of its draws binds it.
- **The three TextureNotFound units [L].** Bandeirante and VP Explorer (`bandeirante_1.gr2`) and SUEA MOP
  (`debang3.gr2`) name their textures in the parameters; all their meshes' textures now equal the capture (the
  Bandeirante's and Explorer's 30 meshes are byte-exact; SUEA MOP's 3 meshes are one-step meshes).
- **The EXE's fallback texture [L, one data point; lead's decision #1893].** Tadodaho's `firekeeper.gr2` names
  `SREFMap` = `black_4x4.dds`, which no install or mod file holds. The EXE binds an 8 x 8 R8G8B8A8 texture with one
  mip: texel 0 = (255, 0, 0, 255), every other texel 0 (L09 resource 910; no other draw of the 21 frames binds a
  texture outside the install and mods). The module builds the same texture in code (`kFallbackTexture`) for a
  texture in no file, keeps the unit `Ok` and names it in `UnitStatus::warnings` ("TextureFallback: firekeeper.gr2
  medicine_man: black_4x4.dds"), which `precheck` lists. Its t1 then equals the capture by SHA-256. A texture that
  exists but does not parse stays `TextureNotFound`. Whether t0 takes the same fallback is not seen [I].
- **The one-step meshes [L for the classes, cause I].** 69 meshes differ from the capture in 1-65 vertices, every
  difference one half step of a position or normal component or one weight level (`one_step_meshes.tsv`, per mesh):
  normals only 49 (the seven Real16-source units among them), positions only 13, both 1, a normal and a weight 1,
  weights only 4, and `ethiopiacompbow.gr2`'s NaN position. The units-per-metre hypothesis does not reproduce them
  (`rounding_hyp.py` over the listed values; the files' `ArtToolInfo` is 39.37 units per metre for 361 exact and 59
  differing meshes, 1.0 for 33 exact and 10 differing): positions as fl(fl(x / u) x fl(s x u)) fix 11 meshes and break
  11 exact ones, adding normals as fl(fl(n x u) / fl(s x u)) fixes 25 and breaks 22 (catalogue runs with the formula
  in the loader, 2026-11-15; the experiment is not kept). No formula tried (x s, n / s, n x 1 / s, double, the three
  units-per-metre forms, ties to even or away) explains more than 37 of 42 positions and 41 of 101 normals. The cause
  stays open [I: the EXE's own conversion at load, e.g. a 3 x 3 product with off-diagonal terms or x87 precision]; a
  decompile of the EXE's load path would decide it. **Corrected 2026-11-16 (ticket 0379, 10.9):** the cause is the
  basis conversion's factor fl(fl(u x s) / u) applied once (not the per-vertex forms tried here), the cofactor inverse
  for the normals and the float weight renormaliser; all 515 drawn meshes are reproduced. (Was: "The cause stays
  open".) Effect on the picture: the warrior's ties (one level on 14
  pixels) are the only such difference measured (10.5).

Commands (2026-11-15): `python -I render/evidence/0041/4352/oracle_manifest.py ... --out manifest.tsv`;
`python -I render/evidence/0041/4356/tex_index.py tex_index.tsv F:/c5-64bit-work/0327/extract <install>/Assets <MODS>`;
`c5r_units_test_draw_oracle --manifest manifest.tsv --game <c5h-c install> --mods "<(1)>;<(2)>;<(3a)>" --db
F:/c5-64bit-work/0327/db/Civ5DebugDatabase.db --exe <install>/CivilizationV_DX11.exe --out oracle_report.tsv`;
`tex_names.py oracle_report.tsv tex_index.tsv`; `regression.py <4352 report> oracle_report.tsv` (0 regressions);
`one_step_classes.py oracle_report.tsv`; `rounding_hyp.py oracle_report.tsv`.

### 10.9 The one-step meshes and the weight ties explained: the EXE's load path (ticket 0379)

Written 2026-11-16 20:52-22:30 EST by the 0379 implementer (account B, branch `task-0379-units-vertex-step`). Inputs as
10.8 (4356's `oracle_report.tsv`, 3907's exports, 3901's export, the 0327 extraction and database copy); the static pass
is a read-only headless Ghidra 12.1.4 run on a scratch copy of project `civ5` (the DX11 EXE, build 4390913, SHA-256
`b2ce96ae...`; `reverse-engineering-setup.md` 2). No decompiler output is kept or committed (D83); what follows is
behaviour. Addresses are DX11 VAs. **Result: one rule reproduces the EXE's vertex buffer byte for byte on all 515
drawn meshes of the catalogue, the 69 one-step meshes and the 12 tie meshes included; the meshes where it differs from
the module's rule are exactly those 81 and none of the 434 exact ones [L]. There was no rounding mystery: the EXE scales
the mesh through Granny's basis conversion with a factor one or two ulps off Scale, inverts that matrix by cofactors
for the normals, and renormalises every vertex's weights in float.**

**The load path [C, static pass].**
1. **Basis conversion per model file and member Scale.** 0x8ECC50 (per model of the file; the Scale is the object's
   float at +0xBC) calls 0x8ECBA0, which calls Granny's basis conversion (0x9BC790 -> 0x9C0C60) with desired units per
   metre = fl(`ArtToolInfo.UnitsPerMeter` x Scale) (39.37008 x Scale, the constant at 0xA9B35C, if the file has no
   ArtToolInfo), desired origin (0, 0, 0), right (1, 0, 0), up (0, 0, 1), back (0, -1, 0). 0x9C0C60 builds the 3 x 3
   basis product of the desired and the file's axes, multiplies every element by k = fl(desired / UnitsPerMeter),
   and gets the inverse from 0x99E800 (cofactors, each times fl(1 / det), det expanded along the first row); the
   affine part is desired origin - file origin. Then 0x9BCDA0 -> 0x9C1840 transforms the model's skeleton and every
   mesh (0x9C1130 -> 0x9BECE0 per vertex, flags 0: no renormalisation): Position' = L p + A, Normal' = I^T n, Tangent
   and Binormal by L, in float32 SSE, written back into the mesh's own vertex format (a Real16 normal is rounded to
   half there). 0x8ECCC0 applies the same conversion to the file's track groups (the clips).
2. **The copy to the draw layout.** The mesh uploaders (0x59BF10 and 0x51CB30 among five; which one serves units is
   [I], both behave alike) take EXE vertex format 4 for a mesh with two or more bone bindings and format 2 otherwise
   (0x9BBD90: BoneBindingCount < 2). The formats are (usage, format, offset) lists in a table at 0xCA0108, turned into
   Granny type definitions by 0x8E2EB0 / 0x8E32E0: format 4 = Position Real16 x 4 @0, BoneWeights NormalUInt8 x 4 @8,
   BoneIndices UInt8 x 4 @12, Normal Real16 x 4 @16, TextureCoordinates0 Real16 x 2 @24 (the 28-byte stream);
   format 2 = Position, Normal Real16 x 4 @0 / @8, TextureCoordinates0 Real16 x 2 @16 (the 20-byte stream). Granny's
   copy (0x9BCCA0) converts member by member (0x9A1580): equal types are copied as they are (Real16 uv and normals,
   the weight and index bytes), Real32 to Real16 goes through 0x999C10.
3. **Float to half (0x999C10).** Add 0x1000 to the 23-bit mantissa and shift right by 13 (ties away from zero); a
   carry out of the mantissa raises the exponent; below the normal range the mantissa (with its hidden bit) is shifted
   first and then rounded the same way; exponents above 15 give infinity; a NaN keeps its sign and its top ten
   mantissa bits (1 if they are all zero).
4. **Weights (0x5EC070, format 4 only, every vertex after the copy).** s = w2 + w1 + w0 as floats; if s > 0:
   k = fl(255 / s), w0' = floor(fl(k x w0) + 0.5), w1' = floor(fl(k x w1) + 0.5), w2' = trunc(fl(255 - fl(w1' + w0'))),
   fourth byte 0. With a fourth weight of 0 and a sum of 255, k = 1 and nothing changes.

**What this explains [L].**
- **Positions:** k = fl(fl(UnitsPerMeter x Scale) / UnitsPerMeter) is Scale only when the division gives it back. For
  the stock files (UnitsPerMeter 39.370079 = 1 / 0.0254) k is one ulp above Scale for 0.12 and one below for 0.11, and
  equal for 0.08-0.10, 0.13-0.20 (`render/evidence/0379/vstep_numbers.py`, 2026-11-16); every position difference of the report is of a
  39.37 file at Scale 0.12 (4356's "Scale one to four ulps larger" was this one ulp). FBX files (1.0 units per metre)
  get k = Scale exactly.
- **Normals:** I = cofactor x fl(1 / det) is fl(fl(k x k) x fl(1 / fl(k x fl(k x k)))) on the diagonal. It differs from
  the module's n / Scale where k is not Scale (0.12, 0.11) and, through the cofactor rounding, where k is Scale
  (one ulp above fl(1 / k) for 0.13 and 0.088): the 49 normal-only meshes, the seven Real16-source units included
  (their widened half times I, rounded back to half in the mesh).
- **Signed zeros:** each output component is a sum of three products, two of them zero; it is -0 only when all three
  are. That is why a -0 normal of the Real16 units came out +0 while some Real32 meshes keep -0 (10.2's special case
  for Real16 sources is the general rule seen on one class).
- **Weight ties and the "21 weights off by one":** fl(255 / s) rounds below or above the exact quotient, so an exact half
  lands on either side (warrior_v2 vertex 428, weights 119, 62, 57, 17: 255 x 119 / 238 = 127.5 exact,
  fl(fl(255 / 238) x 119) = 127.499992, so 127); a vertex whose first three weights do not sum to 255 with a fourth of 0
  is renormalised too, which the module's rule leaves alone (`licorne.gr2` 5 such vertices per mesh, `dopp_a.gr2` 1,
  `elder_statesman.gr2` 2; `knight.gr2`'s weight differences are among its 303 / 117 fourth-weight vertices). (10.2's
  "no integer rule decides it" stays true: the rule is a float one.)
- **NaN:** `ethiopiacompbow.gr2`'s NaN position becomes NaN in all three coordinates (0 x NaN in the off-diagonal
  terms) and keeps its payload: 0xFFC0, as captured.

**The check [L, 2026-11-16].** `render/evidence/0379/vstep_check.py` emulates the four steps in float32 (numpy, one
rounding per operation, the EXE's order) from the `.gr2` files, picks each mesh's captured draw by equal index buffer
(the best-matching file and draw where several share indices: swordsman.gr2 is in UnitModels and Upgrade1Models with
other vertices; ambassador.gr2 and archeologist.gr2 share a mesh at other Scales), and compares bytes. Result
(`vstep_variants_summary.txt`, `vstep_meshes.tsv`): **515 / 515 meshes byte-equal under the EXE rule**; the module's
rule as re-implemented agrees with 4356's report on 515 / 515; **the meshes where the two rules differ are 81 = the
report's 69 differ + 12 ties, 0 of the 434 exact**; 15 not compared (the report's absent meshes). Negative controls
(the same run with one step replaced, `--variant`): k = Scale 465 / 515; I = M^T / k 495; the module's integer weight
rule 498; IEEE nearest-even halves 422; without the affine +0 still 515 (the catalogue has no position that tells the
affine's +0 apart: that step is [C] only). Command: `python -I render/evidence/0379/vstep_check.py --report
render/evidence/0041/4352/oracle_report.tsv --index shell/evidence/0301/3907/index.csv --extra
3901=F:/c5-64bit-work/0327/exports/3901 --gr2-manifest F:/c5-64bit-work/0327/extract/gr2_manifest.tsv --db
F:/c5-64bit-work/0327/db/Civ5DebugDatabase.db --out <tsv> [--variant ...]` (about 3 minutes, CPU only; exit 0 = PASS).

**For the module (a follow-up for the MC; render/src is not changed by 0379).** `render/src/units/model.cpp` and
`models/src/unit_mesh.cpp` reach byte equality on the catalogue by: k and I as above (from the file's ArtToolInfo and
the member Scale, which also replaces `fileBasis` / `turnYUp` and the Real16 `positiveZeros` case: the general basis
product gives the FBX turn), Position' = A + ((L0 x + L1 y) + L2 z) etc. and Normal' x = (ny I3 + nz I6) + nx I0,
y = (ny I4 + nz I7) + nx I1, z = (nz I8 + nx I2) + ny I5 in float32, the half conversion of step 3 (it differs from
`half_from_float_away` only below the normal range and for NaN), the weight rule of step 4 on every vertex of a
skinned mesh, and the 28 / 20-byte split by bone bindings (in the catalogue it equals the module's BoneWeights test
[L]). `tieVertices` and `differOnlyAtTies` then have nothing left to name. [I] The pose layer: the clips and the
skeleton go through the same L with k, not Scale; at the palette's precision that is below the fits' residuals (P9).

**Done in the module (ticket 0447, 2026-11-16 22:03-23:05 EST, account B, branch `task-0447-units-mesh-load-exact`).**
Suites the same day, plain and MSVC ASan, 0 skipped: models 9/9, render `c5r_unitanim|c5r_units` 29/29 (0 ASan reports),
server `test_scene_units` 42/42 and its runner 7 (`render/evidence/0447/acceptance.txt`, `suites-summary.txt`).
Result: **the catalogue run gives vertex buffers byte-equal to the capture for 515 / 515 drawn meshes, 0 ties, 0 differ
(15 absent as before) [L]**; t0 / t1 / programs / index buffers 515 / 515 and header rows 5,134 / 5,170 unchanged;
mesh by mesh against 4356's report: 0 regressions, 69 differ -> exact, 12 ties -> exact
(`render/evidence/0447/oracle_report.tsv`, `regression.txt`). The code [C]:
- `models/include/c5models/unit_mesh.h`, `models/src/unit_mesh.cpp`: `UnitBasis` and `unit_basis(FileInfo, Scale)` (step
  1, the reference's operation order; also `unit_basis(u, right, up, back, origin, Scale)`, `unit_basis_stock` = 3ds Max
  files' 39.370079 units per metre and stock axes, `unit_basis_fbx` = 1.0 and right +x / up +y / back +z);
  `repack_unit_mesh(mesh, basis, skinned, out)` (step 2 in float32 with contraction off, Position / Normal / uv read as
  Real32 or Real16, Real16 uv copied, the NaN rule of the reference, the 28- or 20-byte stream); `half_from_float_granny`
  (step 3); `unit_weights` is now step 4 (replaced: 4152's integer rule "only when the fourth is not 0, exact halves
  up"); `repack_unit_mesh(mesh, Scale)` = the stock basis's 28-byte stream. A vertex with only a fourth weight is no
  longer refused (s = 0: kept as it is, its bone index checked).
- `render/src/units/model.cpp`: `ModelCache::build` takes `unit_basis` of the file and the member's Scale and calls the
  rule on the mesh as read (no `widenReal16` / `positiveZeros` / `applyBasis` on the load path any more; kept as
  functions since `draw.h` declares them); `meshMaterial` chooses the stream by bone bindings (two or more: 28-byte;
  two or more without NormalUInt8 BoneWeights / UInt8 BoneIndices: `UnknownLayout` [I: what the EXE draws for such a
  mesh is not seen]; one binding with weights: the 20-byte stream, posed by binding 0 as before); `repackSkinned` /
  `repackStatic(mesh, Scale, y_up)` use the stock or the FBX basis. `ModelMesh::tie_vertices` stays empty; the oracle's
  `differOnlyAtTies` is left as it is and can no longer name a tie (a difference now reports `differ`).
- Tests: `models/tests/test_unit_mesh.cpp` (345 checks with samples: Granny halves incl. NaNs, the weight rule on the
  four captured ties, k and I0 at seven Scales, the FBX turn, signed zeros, NaN positions, Real16 sources, both
  streams, refusals; warrior_v2's vertex buffer now the captured 0xa06d5cd8b80e2ab7); `render/tests/units/test_model.cpp`
  (material by bindings, the tie vertex (119, 62, 57, 17) -> (127, 66, 62, 0), the three warriors at the captured
  fingerprints). Expected values come from `vstep_check.py`'s functions (`exe_matrices`, `half_granny`).
- [I] not seen: a unit file without ArtToolInfo (none in the catalogue; the module takes the stock axes at 39.37008
  units per metre); weights such as (1, 1, 0, x) whose third comes out -1 (stored as its low byte, 255, as the
  reference does). `render/include/c5r/units/draw.h`'s comment on `repackStatic` (line 159, "Normal / Scale ... ties
  away") still describes the earlier rule: the header was not this ticket's to change.
- Commands (2026-11-16): `python -I render/evidence/0041/4352/oracle_manifest.py --index shell/evidence/0301/3907/index.csv
  --db F:/c5-64bit-work/0327/db/Civ5DebugDatabase.db --out manifest.tsv --extra "3901=F:/c5-64bit-work/0327/exports/3901|units_3901.tsv"`
  (the two 3901 rows of `render/tools/unitscene/oracle_draws.py`); `c5r_units_test_draw_oracle --manifest manifest.tsv
  --game %USERPROFILE%/c5-instances/c5h-c/install --mods "<MODS>/(1) Community Patch;<MODS>/(2) Vox Populi;<MODS>/(3a) VP -
  EUI Compatibility Files" --db F:/c5-64bit-work/0327/db/Civ5DebugDatabase.db --exe <install>/CivilizationV_DX11.exe
  --out oracle_report.tsv` (about 1 minute); `python -I render/evidence/0041/4356/regression.py
  render/evidence/0041/4352/oracle_report.tsv render/evidence/0447/oracle_report.tsv`.

## 11. U2: the pose layer (4353)

Written 2026-11-15 14:11-15:00 EST by the 4353 worker (branch `task-4353-u2-oracle`, which also carries 4355). The
fresh capture (decision 5) ran 14:36-14:47 EST; everything else offline. Marks as at the top of this file.

### 11.1 What is built [C]

- `render/include/c5r/units/pose.h`, `render/src/units/pose.cpp`, `clipcache.cpp`. The module's pose step is
  `ModulePose : PoseSource` (units.h): `ModulePose(c5m::FileProvider&, const c5m::StringDatabase* = nullptr)`,
  `rows(PoseRequest, Float4[96])` gives the block's 32 x 3 bone rows (slots the mesh does not bind are identity),
  `bindAll(member, model)` binds every clip of a member to every skeleton its meshes use (the pre-check's clip binding,
  section 7). The pieces are usable alone: `buildPoseModel` (once per model file and `.fxsxml`; copies skeletons, keeps
  no pointer), `ClipCache` (decoded control points per (model, clip, skeleton), a failed bind is cached with its
  status), `poseFigure` (rows of every requested mesh of one figure), `findAnimation`, `defaultClip`, `fileBasis`.
- **Clip names.** `FigureInput.clip` is matched, ASCII case ignored, against an animation's `name` attribute, then its
  file with or without directory and `.gr2` (`Warrior_IdleA`, `warrior_idlea.gr2`), then `ec:<code>` or a plain
  integer against its ec codes. No stock `.fxsxml` gives a `name` attribute (warrior.fxsxml: `file` and `ec` only), so
  the file name is the practical name. **Default clip** (decision 6, "by name"): ec 1000 (idle A), with flag 2
  (fortified) ec 1520 (fortify idle) falling back to 1000 [I: the ec codes are the only names the files carry].
- **Clip time.** Every clip wraps (`clip_time(t, duration, loop = true)`) until U5's controller says which clips loop.
- **Three things the sampler lacked, each a work-around in 0327's tools before:**
  1. Every track group of a clip, first track of a bone name winning: `c5m::bind_clip_all_groups` (new,
     `models/src/anim_groups.cpp`, declared in `anim.h`; additive). Tracks that name no bone of the skeleton are
     counted, not decoded, so an attachment point with a format the sampler lacks no longer refuses the clip.
  2. Each mesh posed with the skeleton of the model entry that binds it, else the first skeleton holding all its
     bound bones (not the file's first skeleton); a mesh no skeleton holds gives `BadModel` when posed.
  3. Files in another basis (ArtToolInfo axes other than right +x, up +z, back -y) brought to the stock basis, axes
     only, no unit scale: X' = M X M^-1 for rest transforms, inverse world transforms (model file) and clip tracks
     (clip file's own basis). For VP's FBX files (y up, back +z) M takes (x, y, z) to (x, -z, y). **The vertices must
     take the same M** (v' = M v) for these rows to fit them: 4352's repack (`UnitModel::y_up`, premise P4).
  4. **Reader fix (models/src/gr2_model.cpp, lead's go-ahead 14:56).** `read_gr2` resolved an animation's track
     groups only through the file's root `TrackGroups` list. VP's FBX clip `flagbearer_idle.gr2` (Bandeirante) leaves
     that list empty and references its one group (32 tracks) only from `Animations[0].TrackGroups`, so the clip had
     no group and the pose step refused the unit (`BadModel`, NoTrackGroup; 5 catalogue mesh-instances in the
     oracle). A group the root list does not hold is now read from the animation's reference and appended after the
     root ones (`FileInfo::animation_only_track_groups` counts them; the canonical summary still lists the root list
     only, as the Python reference does). Test `models/tests/test_gr2_groups.cpp` (synthetic files): the root-list
     form unchanged, the animation-only form read, a null reference and no reference still give no group
     (NoTrackGroup); 31 checks.
- **Statuses** (section 7): `ClipNotFound` (name not in the `.fxsxml`; also no default clip), `CurveUnsupported`
  (`UnsupportedFormat` or `BadDegree` on a bound track), `ModelNotFound` (clip file in no source), `BadModel` (reader
  refused the file, broken curve data, unresolvable mesh, parent order, singular axes, more than 32 bound bones),
  `BadInput` (non-finite t, scale, yaw or T; mesh index out of range; no model in the request). `poseErrUnitCode`
  maps them to `UnitCode` one to one.
- Not thread-safe (one `ModulePose` per thread). Nothing throws.

### 11.2 Tests [C, L 2026-11-15]

- `render/tests/units/test_pose.cpp` (`c5r_units_test_pose`, synthetic data only), 9 tests: equal to models' own
  pipeline (`bind_clip_all_groups`, `sample_local`, `world_from_local`, `skin_rows`, `palette_rows`) within 1e-4 at
  five times incl. a wrapped one, for meshes of two skeletons and a mesh without bindings; a model, a clip and both
  written in the y-up basis give the stock rows within 2e-4; a front group with an undecodable track; every look-up
  form, the default clip, loop and clamp; `ModulePose::rows` (96 rows, identity padding, default fortify clip,
  `bindAll` refused by the curve the member's sixth clip lacks); negative tests per status (ClipNotFound,
  CurveUnsupported incl. the cached second answer with one file read, ModelNotFound incl. no loader, BadModel: broken
  file, unresolvable mesh, parent order, singular axes, 34 bound bones; BadInput: NaN t, infinite T, mesh 7 of 4, a
  skeleton out of range, an empty request), each asserting code, message and an empty output.
- `models/tests/test_anim.cpp` + `test_all_groups` (additive): one group equal to `bind_clip`; a front group wins for
  its bone, its unbound undecodable track does not refuse the clip, `bind_clip` does; a bound unsupported track
  refuses (output cleared, message names the track); no group, group index out of range, no animation. 1,518 checks,
  0 failures.
- Commands: `python -I perf/quietlock/quietlock.py run --who 0041 --what ... -- python -I render/build.py --test -R
  c5r_units` (4 of 4 passed, 0 skipped, 2026-11-15 14:53); `python -I models/build.py --out
  C:/c5-64bit-work/0041/4353/models-build --test` (6 of 6). The ASan runs are in the issue's log.

### 11.3 The fresh capture (decision 5) [L, 2026-11-15 14:36-14:47 EST]

- Pool instance `c5h-d` (F:) claimed under 0041; 32-bit DLL `i0ship-26234371-ia` (deps/i0.lock 26234371e, SHA-256
  3C2F47DA..., named on the command line); CP/VP/EUI, stock `config.ini`, frame cap 30, VT present, RenderDoc 1.47
  preloaded by the 0302+ wrapper; no-focus launcher. Save copy `C5H-0041-CAT0301_0009` of `c5h-9028-tiny-t0008`
  (SHA-256 b2ceb663...dcc29163; the runner requires 0301 in the name). Scenario
  `tools/scenario/scenarios/units_0041.json`, frames U01-U04; four captures, 1.7 GB with exports under
  `F:/c5-64bit-work/0041/4353/` (`captures/c5h-d_20261115-143637_frame{2676,5287,7817,11720}.rdc`,
  `exports/units_0041/U01..U04` with `--select all`, `runs/units_0041/` frame records and DIAGNOSTIC shots).
- Stopped through the harness (newest autosave settled), restored by SHA-256 (19 of 19 files), `provision.py verify
  c5h-d` ok, the two save copies removed, the instance released. `runner.py prepare` crashed in `provision.py set-dll`
  (KeyError `kept_sha256` at its return, after writing the same DLL: old = new = 3C2F47DA...); the remaining prepare
  steps ran from a script calling the same functions (lead #1861); the bug went to its owner through the lead.
- Analysis: `models/evidence/0041/4353/fresh_capture.py` (draws of the three passes, ids from the per-instance stream,
  blocks of t27, meshes by index count; exact_t fits of the tank blocks), `p5_mech.py` (the mech blocks fitted with
  posefit on the C++ sampler), `p5_pose_list.py` (the renderer's pose layer against the captured rows through
  `unitscene pose`). Numbers in `models/evidence/0041/4353/`.

### 11.4 P5: format 3 in the orientation slot and degree 3 [L for format 3; L for multi-bone degree 3 since 2026-11-16]

**Answered 2026-11-16 by ticket 0379 [L]: multi-bone degree 3 against a capture.** The fresh capture held it after all:
mech block 51 of frame 2676 (`c5h-d_20261115-143637_frame2676`, below "cross-fades") is idle B under the aim layer
(4406 r2, `unit-controller.md` 15.8). `Mech_IdleB.gr2` has degree-3 orientation curves on 7 tracks (`Mech01LLeg1`,
`Mech01LLegPalm`, `Mech01Pelvis`, `Mech01RLeg1`, `Mech01Ribcage`, `Mech01Spine` format 8; `Mech01RLegPalm` format 1);
the `Mech` mesh binds 32 bones, 6 of them among these (all but `Mech01Spine`, which is also the only bone the aim
mask gives weight 1). Re-run by 0379 through the module's pose program (`unitscene program`, models' C++ decoder and
sampler; binary of build tree `C:/c5-64bit-work/render-build/74063e0d99`, 4406 r2's), placement solved as posefit:
| Program | rotation max (deg) | rotation element max | translation max |
|---|---|---|---|
| idle B at 4.8985 s under the aim layer | 0.000043 | 1e-6 | 5.6e-5 |
| the same at 4.9185 s (t + 0.02, control) | 0.00054 | 9e-6 | 8.8e-5 |
| idle B at 4.8985 s without the aim layer (control) | 1.47 | 0.022 | 0.127 |
| block 46: idle B at 0.3205 s under the aim layer | 0.023 | 0.00032 | 0.012 (open, 4406's) |
So the decoder and sampler give the EXE's pose on a 32-bone mesh with six bound degree-3 bones within the fits'
noise (11.2's single-clip residuals are 0.0003-0.0016 deg). Commands: `python -I -X utf8
render/evidence/0379/degree3_scan.py --pair mech.gr2,Mech,Mech_IdleB.gr2` (the bone count); `python -I
render/evidence/0379/mech_idleb_check.py <unitscene.exe> <work>` (the table). Other recordings searched, none with
such a block: 4405's 1,324 fitted blocks (72 model / mesh / clip pairs incl. the archer's) and 0395's 121 (25 pairs,
battleship aim and fortify), `degree3_scan.py --table ...`, `render/evidence/0379/degree3_tables.txt`. **How common
it is [C, the files]:** over the 301 members of the report's units and every clip of their `.fxsxml` (7,638 member /
clip pairs; `degree3_scan.py --catalogue render/evidence/0041/4352/oracle_report.tsv --out <csv>`, 13.5 minutes,
2026-11-16), 29 pairs of 12 members have degree 3 on two or more bound bones of one mesh
(`render/evidence/0379/degree3_catalogue_multi.csv`): the mech's fidgets, transitions, combat-ready and bombard-defence
clips (up to 32 bones), the late caravans' and colonists' run and stop-run, the SS booster's run (17), tank, modern
armour, rocket artillery, mobile SAM and German panzer fidgets and bombard-defence (2-4); only two are idle-state clips,
both the mech's (idle B, 6 bones, the one captured; fortify idle, 2). Tank fidgets and the run clips stay uncaptured;
they take the same decoder path. (Replaces the heading's "open for multi-bone degree 3" and the
"Degree 3 on several bones: not verified" item below, which predate 4406 r2's aim-layer reading of the mech blocks.)

- **Mechs are drawn here** (3 mechs, U01, settle 8 s): `unit-draws.md` 7's L06 "not drawn" was not a property of the
  mech [I: art still loading in L06].
- **Format 3 (`DaConstant32f`) in the orientation slot [L].** Mech block id 50 (U01 draw 381) fits `mech_idlea.gr2` at
  t = 3.6000 s on all 32 bound bones: rotation error 0.00032 deg, translation 1.6e-4, runner-up (`mech_fidgeta`) 1,106
  times the cost (posefit `--sampler cpp`, s fixed 0.12). Four bound bones carry format 3 orientation curves
  (`Mech01LLegDigit01`, `Mech01LLegDigit21`, `Mech01RLegDigit01`, `Mech01_Piston_01_Toe_LR`); `Mech01Pelvis` is
  degree 3. The renderer's rows at that fit against the captured rows: rotation 0.026 deg (the fit's own tilt, 0.025
  deg; the module places with Rz only), element 4.5e-4.
- **Degree 3 on several bones: not verified. (Superseded 2026-11-16 by 0379, top of 11.4: the "cross-fades" below are
  idle B under the aim layer, and block 51 answers it.)** The other two mech blocks (ids 46, 51) are cross-fades: best
  `mech_idleb` at 4.513 s with 1.32 deg, runner-up `mech_fidgetb` at 1.04 times the cost (idle B has 7 degree-3 bones:
  LLeg1, LLegPalm, Pelvis, RLeg1, RLegPalm, Ribcage, Spine); the cross-fade rule is K4's. The 4 tanks and the panzer
  were idle, not mid-fidget (tank idle A/B at 4.3008, 1.8796, 0.6599 s; the degree-3 turret curves are in the
  fidgets). So multi-bone degree 3 stays checked on synthetic data only (unit-playback.md 5.2) plus one bone each
  on two captures (SS booster `Bone_Cab_02`, mech `Pelvis`).
  **Searched further 2026-11-15 15:40 (4355, no new capture) [L]: still open.** (a) The pose sequences P01-P05
  (`F:/c5-64bit-work/scene-library/poses`: warrior, horseman, barbarian warrior idle, walking, attacking): no clip of
  the warrior, the horseman or the three barbarian EURO members has a degree-3 curve on a bone their meshes bind
  (curve headers of every clip of the three `.fxsxml`), so those captures cannot show it. (b) The whole catalogue:
  among the 1,535 fitted mesh-instances (0327's K3 table, reproduced by the module's sampler in 13), none is posed by
  a clip with degree-3 curves on two or more of the mesh's bound bones
  (`render/evidence/0041/4355/p5_degree3_scan.py`). The idle states the catalogue and P01-P05 hold do not use them;
  attack, fidget and run clips of other units do (mech idle B, tank fidgets). Evidence for the decoder stays the
  synthetic tests (degree 3 on 2-12 knots, models/tests/test_anim.cpp) and the two one-bone captures.
- **Rows equal on the fresh capture [L].** 27 tank, tread and panzer blocks: the renderer's rows against the captured
  ones at the exact_t fits: rotation at most 1.4e-5 deg, translation at most 2.5e-4 world units.

### 11.5 P3: palette header rows on the fresh capture [L]

Scene pass, every instance (`fresh_capture.py` summary): row 0 = the owner's colour, alpha 1 (Polynesia 0.85098,
0.345098, 0; Carthage 0.8, 0.8, 0.8; barbarians 0, 0, 0); row 1 (x, y, z) = (1, 0, 0) for units at rest, for the
**damaged** barbarian warrior (8 of 12 figures drawn after the ranged attack, U03), the damaged Mamluk (7 of 8, U04)
and the **embarked** warrior (U02: drawn as a 7,722-index embark model, 2 instances); a third-party warrior that came
into view during U03/U04 (team colour 0, 0.635, 0.71) has row 1 = **(0, 1, 1)**: fading in. Row 1's w differs per
instance and is not read (section 5). So P3 holds as stated: only row 0 and row 1's x, y, z change.

### 11.6 P7: the Mamluk's eighth horse at a second angle and damaged [L]

(Asked by the lead at 14:50 after 4354's hand-back; questions of 12.5.) Mamluk mesh-instances per pass, paired by id
(horse even k, rider k + 1, as 12.5 pairs them):

| Frame | Camera, unit plot | Mask | Reflection | Scene | Pairs |
|---|---|---|---|---|---|
| U01 (angle A, rest) | (41,11), (40,11), facing 2 | 8 horses + 8 riders | 8 + 8 | 8 + 8 | ids 30-45, all 8 paired |
| U02 (angle B, rest) | (36,11), (37,12), facing 2 | 8 + 8 | 8 + 8 | 8 + 8 | ids 0-15, all 8 paired |
| U04 (damaged, barbarian) | (41,11), (42,9), facing 4 | 7 + 7 | 7 + 7 | 7 + 7 | ids 12-25, all 7 paired |

- The eighth horse **is drawn** at both angles and at another idle time: L06's missing horse is not reproduced.
- Damaged to 7 figures, the last member (figure 8) goes with **both** its meshes; the 7 living figures all have their
  horse. So neither a "member index 8 has no horse" rule nor a "last living figure loses its horse" rule holds here.
- The L06 case stays unexplained [I: a transient of that frame]; the product draws 8 horses (12.5's default), and the
  oracle keeps L06's Mamluk as a known difference.

### 11.7 posefit and posesample (0346 item 5) [C, L]

- `posefit.py`: `Clip` reads every track group (first track of a name wins; a track that does not decode is an error
  only when a model's bone uses it); `Model` records per mesh the skeleton holding its bones and carries the first
  skeleton that holds every mesh's bones (else 0), with `view(j)` / `for_mesh(mesh)` for the others; `fit`,
  `blendfit.py` and `compare.py` pose each mesh with its own skeleton. `posesample` binds every group
  (`bind_clip_all_groups`) and poses each mesh with its model entry's skeleton.
- `test_posefit.py`: the three posesample tests skip with "POSESAMPLE not set to the built posesample.exe ..." (they
  failed before); three new tests on synthetic roots (every group, first wins; a bad track refuses only when used;
  skeleton per mesh). 11 passed with POSESAMPLE set; 8 passed and 3 skipped (with the message) without.

### 11.8 Clip cache memory and sampling cost (indications, workstation, outside a timing window; COMMON 23)

From `unitscene pose` over the catalogue (section 13): 469 (model, clip, skeleton) bindings kept, 6.58 MB of decoded
curves and track tables (14 KB each on average), 1.04 s to read and bind them all (2.2 ms each, files from F:);
sampling 18.7 us per figure-mesh on average over 1,473 (bind time excluded; sample, world, skin and palette rows
of one mesh). On the fresh capture: 9 bindings, 78,868 bytes, sampling 4-30 us per figure-mesh (13 us mean).

### 11.9 Not verified

- Degree 3 on several bones against a capture (11.4).
- Which clips loop (U5); the cross-fade (K4).
- The ASan run of the merged module with 4352's code (only this branch's sources were built).
- The basis conversion against a VP unit's captured rows inside the module: the oracle's pose comparison covers the
  Mamluk (FBX) through the same code (section 13); the module's vertex side is 4352's.

## 12. U3: placement (4354)

Written 2026-11-15 14:11-14:38 EST by the 4354 worker (branch `task-4354-u3`). Offline: no game was run. Marks as at
the top of this file.

### 12.1 What is built [C]

- `render/include/c5r/units/placement.h`, `render/src/units/formations.cpp`, `placement.cpp`: L3 as 6.4 designs it.
  `FormationTable::load(files, installRoot)` reads `UnitFormations.xml` (12.5) and `UnitVisConfig.ini`'s
  `[Debugging] GlobalUnitScale` (absent in the stock install: scale 1.0); `parse()` starts with the built-in Default
  record. `formationSlots()` is unit-formations.md 3 (rows, centre-out sides counted over the whole unit, blocks and
  centre width, straight or cosine bend, the random part with the generator of 3.4, centring with the origin);
  `facingYaw()` 150 - 60 d deg, 1 rad for any other value (11.2); `livingFigures()` the 0x4B4B80 rule (section 7
  there: dead = round(n x percent / 100), at most n - 1 below 100 percent, percent = damage x 100 / max HP in
  integers [I], members die from the last backwards, `alive` >= 0 overrides); `plotCentre()` HexToWorld (12.2);
  `heightAtWorld()` the truncated sample (12.4 there); `placeUnit()` puts it together: slots for all n members, the
  living figures 0..m-1 take slots 0..m-1, rotated by the yaw about +z, moved to the root (`has_root`: the given
  root, else the plot centre), z = max(truncated sample at the slot, `water_level`) + the member's `ZOffset`, yaw =
  the unit's, no tilt. `loadPlacementColumns()` reads the two columns c5m::ArtTables does not load
  (`ArtDefine_UnitInfos.Formation`, `ArtDefine_UnitMemberInfos.ZOffset`).
- **Failures.** Every function returns a `c5r::Status`; 4352's prepare step maps a failed one to `PlacementFailed`
  with its text. NotFound: a non-empty formation name not in the table (decision below), no formation file. BadLayout:
  a plot outside the height field's map (checked in both position sources), a figure count outside 1..64, `alive`
  above n or below -1, `max_hp` <= 0, negative damage, a non-finite root, `ZOffset` or water level, seed 0xFFFFFFFF
  (the EXE's "continue the running generator", not reproducible per unit), an empty height field or an all-zero
  hex-to-world matrix, a slot that is not finite (a one-slot row with a curved bend: 0 / 0 in the EXE too).
- **Decision (logged in the issue):** an unknown non-empty formation name is `PlacementFailed`. The EXE would use its
  Default record (unit-formations.md 2), but no art row of the catalogue's database names an unknown formation, so a
  name that does not resolve means a broken mod or table, which R6 wants loud. Empty / NULL gives Default as in the EXE.
- `render/include/c5r/units/placement_check.h`, `render/src/units/placement_check.cpp`: the DoD D check, callable by
  4355's oracle tool: `parseCatalogue()` (text from `models/tools/formations/catalogue_tsv.py`), `checkCatalogue()`
  (places every unit with `placeUnit()`, pairs placed and measured figures of the same member type by least total
  distance, Hungarian), `catalogueReportText()`.
- Tests: `render/tests/units/test_placement.cpp` (synthetic only; slots against `formations.py` on an invented
  formation file, every check with its negative case; 19 tests) and `test_placement_catalogue.cpp` (DoD D on the
  oracle set, below). Python: `models/tools/formations/catalogue_tsv.py`, `seed_fit.py`, `test_catalogue_tools.py`
  (4 tests; the folder's 38 pass).

### 12.2 P6 answered: the 0.07-unit common shift is the EXE's hex-to-world matrix [L, C for the formula]

- The plot centre the EXE places a unit at is HexToWorld 0x6851C0: `(hx, hy, 0, 1) x M_h2w`, then `x sx - ox`,
  `y sy - oy`, with hx = x - (y >> 1), hy = y (`camera-and-picking.md` "Hex -> world"). The C5HF tap carries M_h2w
  (`hexToWorld`). In bundle `C5H-0301-TINY_0009_20261115-070309_workstation` it is **not** the nominal lattice
  (64, 0 / 32, 64 / 32, 128): row 0 = (63.9986, 0.000938), row 1 = (32, 64.00188), row 3 = (32.00071, 128.0033).
  `heightfield.h`'s nominal relation (x = 64 px + 32 (py odd) + 32 - ox, y = (64 py + 128) sy - oy), which 3909 and
  4153 used as the plot centre, differs from it by (-0.03..-0.08, +0.005..+0.12) on this map: unit-formations.md
  12.3's common shift.
- Check (2026-11-15 14:14, own scratch script on 4153's `exact_fixed30/per_unit.csv` and `figures_exact.json`): the
  138 multi-figure units' fitted shifts minus the HexToWorld-minus-nominal difference: median 0.0004, p95 0.0045 (the
  csv rounds to 0.001; the larger residuals are the cross-fade and aircraft units of 12.3), against a raw shift of
  median 0.0708, maximum 0.144. With the tapped matrix, DoD D below needs **no fitted shift at all**.
- So the cause is neither the slot rule's centring nor the hex geometry: it is a fixed property of the map's
  hex-to-world matrix, which the EXE builds with float rounding [I: how 0x535BF0 arrives at 63.9986 not traced; it
  does not matter, the tap carries the matrix]. `plotCentre()` fails with BadLayout on a tap without a matrix rather
  than falling back to the nominal lattice (R6). Note for other modules: anything that places at a plot centre and
  wants the EXE's position to better than 0.12 units must use the tapped matrix, not `heightfield.h`'s nominal
  relation (`fow` already does, `fog-of-war.md` 930).

### 12.3 DoD D: the catalogue placed by the C++ rule [L]

**Inputs** (all read only, 2026-11-15): the install `%USERPROFILE%/Desktop/pure_vp/Sid Meier's Civilization V`
(render/build.py's default; `Assets/Units/UnitFormations.xml` SHA-256 `bf164b8a...df0b`, the file 4153 read on c5h-c);
height field `F:/c5-64bit-work/scene-library/bundles/C5H-0301-TINY_0009_20261115-070309_workstation/heightfield/
load0003.c5hf` (SHA-256 `5fb03aec...165b`; its `scene.json` names save `C5H-0301-TINY_0009`, SHA-256
`b2ceb663...dcc29163`, the catalogue's save, checked before use; 56 x 40 plots, water level 10.11673, wrap x); database
`F:/c5-64bit-work/0327/db/Civ5DebugDatabase.db` (SHA-256 `27fc81c6...19b4`); 4153's exact positions
`F:/c5-64bit-work/0327/4153/w2b/all/figures_exact.json` (SHA-256 `f0d9468e...e2dc`) turned into
`models/evidence/0041/4354/catalogue_figures.tsv` (numbers only) by
`python -I -X utf8 models/tools/formations/catalogue_tsv.py --figures <json> --db <db> --out <tsv> --seed-overrides
models/evidence/0041/4354/sea_seeds.csv`. Facing 2 (south-east) for every unit (3907 placed with facing -1, which the
DLL makes south-east); seeds by unit-formations.md 12.1's allocation for the land frames (object + 19 in L01, L03,
...; 20 and 33 - object in L02, L04, ...), for the sea frames from `seed_fit.py` (below).

**Command:** `python -I perf/quietlock/quietlock.py run --who 0041 --what ... -- python -I render/build.py --test -R
c5r_units_test_placement` (C5R_UNITS_PLACEMENT_REPORT set to `models/evidence/0041/4354/placement_catalogue_report.tsv`;
one line per figure, summary at the end). Tolerances: 0.002 world units in x, y; facing 0.002 rad (0.115 deg: the
palette fits' own yaw noise reaches 0.08 deg on good blocks, so 0.002 deg would measure the fit, not the rule); z
0.001.

| Result (2026-11-15 14:33) | This rule, nothing fitted | 0327 (4153 12.3), common shift fitted per unit |
|---|---|---|
| Multi-figure units within 0.002 and 0.002 rad | **129 of 138** | 134 of 138 (positions only) |
| ... outside, Air domain (FighterWing: Fighter, SPAD, WW1 fighter, Zero) | 4 | 1 (the Zero; the other three within after their fitted shift) |
| ... outside, cross-fade blocks (F-Company, Huns horse archer, Samurai, Cossack) | 4 | 3 (the Cossack within after its fitted shift) |
| ... outside, other: Paratrooper (one figure 0.0023, its fit's rotation error 0.028 deg) | 1 | 0 |
| Single figures within | 60 of 71 (land: 5 Air bombers and the gunship; sea: 6 of class fit or sea) | median 0.02, p95 0.05 (Default slot, facing fixed) |
| Heights within 0.001 (all 1135 figures) | **1083 of 1135** | 1083 of 1135 |
| ... outside: Air 17, Sea hulls 17, cross-fade 6, grouping 8 (Machine gun, Missionary, XCOM: their measured figures are callout meshes), other 4 (Armored car +0.0045 both figures, WW1 tank +0.025 both figures) | 52 | 52 |
| Formation file entries | 24 (the loose file, 12.5) | 24 |

- Classes: "air" = a figure of an Air- or Hover-domain member (few-bone hulls whose T the palette fit cannot separate
  from the flight idle's root motion, 4153 12.2); "fit" = a figure whose palette fit is a cross-fade (rotation error
  >= 0.05 deg or its meshes' T more than 0.002 apart: `part_spread` in `figures_exact.json`); "sea" = a Sea-domain
  hull; "grouping" = 3909's figure grouping does not match the members. The Paratrooper and the two vehicles are named
  as fit noise [I]: one figure, rotation error 6x the fit's p95, and two-member vehicles whose two figures sit
  equally high, which points at the model's root, not at the slot.
- The 129 within have a per-figure median of 0.0001 and p95 0.0005 units. The member pairing (least total distance
  within a member type) was needed only because the measured figures come in draw order.
- **Sea frames' seeds.** The land rule does not hold for S01-S04 (their runs held other record counts). `seed_fit.py`
  finds, for each sea single, the one seed in 0..299 that places it within 0.002 (32 of 39 have exactly one; the 7
  others are the fit / hull cases): S01 31-k for most objects (k 2 and 3 swapped), S02 k + 19 (11 and 12 swapped),
  S03 k + 21, S04 35 - k (object 1: 22). They are integers in 17..33 as the art-record allocator hands out, which
  supports 4153 12.1 on 32 more units; the exact creation order of 3907's sea runs was not traced.
- The test asserts these numbers as floors (129, at most 1 "other", 1083, 0 failed) and that the loose file gives 24
  records. Run time of the whole check: under 1 s on the workstation (indication, COMMON 23).

### 12.4 The formation file in effect: the loose 24-entry file, not the pack's [L, C]

- The install holds two copies: loose `Assets/Units/UnitFormations.xml` (24 entries) and `unitformations.xml` in
  `Resource/Common/UnitModels.fpk` (21 entries, without OffsetPikeman, ThreeInLine, TheeInLineStag; 4153 section 1).
  `gamefs::GameFiles`' name lookup returns the **pack** copy (Resource/Common packs are registered with flags 0 in the
  base layer, loose Assets with flag 1, which does not replace a pack entry; `gamefiles.h` 4) [L: the test prints
  "name lookup alone: 21 formations from Resource/Common/UnitModels.fpk:unitformations.xml"].
- The game uses the loose one [L]: the catalogue's OffsetPikeman and modern caravan units fit to 0.000 with records
  only the loose file has; with the pack copy they get Default and fail. Consistent with the EXE opening the path
  `Assets\Units\UnitFormations.xml` (string 0xA21344, unit-formations.md 2) rather than a bare name [C for the string].
- So `FormationTable::load()` takes the install root and reads the loose path first, the file-system lookup only when
  the loose file is absent [I for that fallback: whether the EXE's path open would find a pack copy at all was not
  traced]. A mod cannot replace the formations through the loose path [I]. **4352:** pass the install root to `load()`.

### 12.5 P7: the Mamluk's eighth horse [L for what is ruled out, I for the rest]

What 3907's L06 capture already rules out (3909's `units_land_L06.json`, own scratch scripts, 2026-11-15 14:20-14:40):

- **Not frustum culling:** with the frame's scene-pass view-projection (cb1 `323c00f5`, camera (896, -850, 474)) the
  8th figure's origin (819.3, -365.6, 16.3) projects to pixel (963, 628) of 2554 x 1411, depth 0.937: inside, and
  close to its seven companions (pixels 933-1097, 628-775).
- **Not the clip:** the 8th figure plays idle A at t 1.967 s, as figures 4 and 6 do (ids 63/64, 69/70), whose
  horses are drawn.
- **Not a per-draw budget:** the horse draw (index count 1722, 22 bound bones) has InstanceCount 7; elsewhere in the
  catalogue 22-bone meshes are drawn 12 per draw (crossbowman, `instances.csv`), and meshes of 17-27 bones up to 14.
- **Not a pass:** none of the three passes (mask, reflection, scene) draws it (4153 12.5).
- **Unique to this model:** of the 138 compared units, the Mamluk is the only one whose meshes of one model are
  instanced unequally (horse 7, rider 8); the other uneven units are multi-model or share meshes (chariots,
  elephants, bandeirantes) with even counts per figure.
- **Where it happens:** the frame's palette blocks pair horse and rider per figure (57/58 ... 69/70) and give the
  8th figure only a rider block (71): the horse is left out when the EXE builds that figure's mesh list, before any
  draw [L for the numbering, I for "when built"].
- Candidates left [I]: (a) the last member of a unit loses its horse mesh by a rule of the model (VP's FBX
  `Mamluk.gr2`, the only FBX cavalry model in the catalogue; its `.fxsxml` names the stock Hakkapeliitta textures
  while the draws bind the model's own two), e.g. a mesh-to-member binding the EXE reads from the file; (b) a rule on
  the member index (the 8th, slot 7) for models with a separate horse mesh.
- **What the fresh capture must show (for 4353's capture, at most two frames):** (1) the Mamluk from a second camera
  angle and at a different idle time: are there again 7 horse instances, and is the horseless figure again the 8th
  (in L06 it is slot 7, the third row's single slot at the rear centre, slot frame (0.24, +25.7) with seed 27; the
  check pairs it with the measured rider 71)? (2) the same Mamluk damaged to
  7 living figures (alive override or about 12 percent damage): does figure 7 then lose its horse (a "last living
  member" rule), or do all 7 have one (a "member index 8" rule)? (3) if any other FBX cavalry model of VP is in the
  catalogue's units (Hakkapelitta's stock Max model is not), the same count for it. Until then: the product draws
  what the rule says (8 horses) and the oracle (4355) lists the Mamluk as a known difference with this section.

### 12.6 P4 for placement: no basis change in placement [L]; the EXE code not found [I]

- Placement is the same for VP's FBX-exported models and the stock Max ones: the Mamluk (FBX, y up) and the other
  FBX units of 4153 12.2 (Black Tug, Chewa, Cacador, Commando, Grenzer, Klepht, Goedendag, Inti maceman, Heavy
  skirmisher, Dhanurdhara; the Emissary is a single figure, also within) are inside the 129 with yaw 150 - 60 d and
  T at their slots, like the stock models. The y-up to z-up change (X' = M X M^-1 on rest and inverse world transforms, no unit scale) belongs to the
  model and pose layers (4352 vertices, 4353 skeleton), not to L3. `placeUnit()` has no basis input.
- Which EXE code applies the conversion: not found in the knowledge base (no address in `research/` converts a
  Granny file's basis; the import table of the EXE was not examined in this issue) [I: Granny's own basis
  conversion at load]. Not needed for placement.

### 12.7 For 4352, U5 and U6

- 4352 (prepare): load the table once with `FormationTable::load(files, installRoot)` and the columns with
  `loadPlacementColumns(db, cols)`; per unit build `PlacementInput` from the art row's `Formation`, the figures' member
  `ZOffset` in member order (6.3), the unit's plot, root, facing, `formation_seed`, damage, max HP, alive and the
  scene's `water_level`; `placeUnit()`; a failed Status is `PlacementFailed`. The figure's T is `FigurePlacement::x,
  y, z`, its yaw `yaw` (the palette rows' `Rz(yaw)` and `T` of section 5).
- U6: the seed allocator of unit-formations.md 12.1 is confirmed again on the sea frames (12.3 above).
- U5: `alive` and damage drive which figures stand; the death animation itself is the controller's.

### 12.8 Not verified

- The death rounding (round half up of n x percent / 100) and percent = damage x 100 / max HP [I]; no catalogue unit
  is damaged. The fresh capture's damaged Mamluk (12.5) would check it.
- Facings other than south-east are from the EXE's switch (unit-formations.md 11.2), not measured here.
- `ZOffset` as member info +0x144 [I, unit-formations.md 12.4]: no catalogue member has one; the code adds it.
- Mods that ship their own `UnitFormations.xml` (12.4).
- `has_root` (position source exe): members take their heights from the field at their slots, not from `root_z`
  (the EXE's 0x6C67C0 does the same at layout [C]); during a move the controller places figures (U5).

### 12.9 Files

Code: `render/include/c5r/units/placement.h`, `placement_check.h`; `render/src/units/formations.cpp`,
`placement.cpp`, `placement_check.cpp`; tests `render/tests/units/test_placement.cpp`,
`test_placement_catalogue.cpp`; tools `models/tools/formations/catalogue_tsv.py`, `seed_fit.py`,
`test_catalogue_tools.py`. Evidence `models/evidence/0041/4354/`: `catalogue_figures.tsv`, `sea_seeds.csv`,
`placement_catalogue_report.tsv`, `README.txt`.

## 13. The oracle run (4355)

Written 2026-11-15 14:16-15:10 EST by the 4355 worker (same worker and branch as 4353, `task-4353-u2-oracle`).
Offline: no game instance. Marks as at the top of this file.

### 13.1 The scene file (DoD I, section 3) [C, L]

`render/include/c5r/units/scene_file.h`, `render/src/units/scene_file.cpp`: `parseScene` / `writeScene` /
`readSceneFile` / `writeSceneFile`, a `SceneFileStatus` (code, 1-based line, message). Codes as section 3 plus `Io`.
Rules beyond section 3's text, decided here: string fields must be non-empty without control characters (bytes >= 0x80
allowed); `-` is the only empty art row; a unit's `player` must have a `player` record (anywhere in the file:
`Orphan` at the unit's line otherwise); `id` 0..2^31-1, plot 0..32767, `max_hp` 1..32767, `damage` 0..`max_hp`,
`alive` `-` or 0..64, `flags` 0..127 (bits 0-6), `t` finite and >= 0; one `scene` record required (`BadRecord`, line
0, when missing; a second is `Duplicate`); a UTF-8 BOM is skipped; a file over 64 MiB is `TooLarge`. The writer
refuses any scene the reader would refuse (it reads its own text back), so every file it writes reads back equal.
Tests `render/tests/units/test_scene_file.cpp`: every field round-trips bit-exactly (text and file, CRLF, BOM,
comments); one negative test per code asserting code, line, message and an empty scene (10 tests).

### 13.2 The tool and the one command [C]

- `render/tools/unitscene/unitscene.cpp` (the C++ tool): `scene FILE... [--write DIR]` (read, round trip),
  `pose --list --rows --out [--gsd-union]` (the module's pose layer against captured palette rows, per line a
  status and rotation, translation and element errors, plus the clip cache's and the sampling's indications),
  `placement` (4354's `checkCatalogue`, DoD D, and the height field's water level).
- `render/tools/unitscene/oracle.py` (the one command, no game instance):
  `python -I -X utf8 render/tools/unitscene/oracle.py --out C:/c5-64bit-work/0041/4355/oracle --exe
  <render build>/tools/unitscene/unitscene.exe`. Inputs default to the oracle set (worker-common.md): 3907's
  index, frame records and exports, 3909's per-frame unit records, 0327's K3 fits of every catalogue mesh-instance
  (`F:/c5-64bit-work/0327/4153/w2b/all/instances.csv`), 0327's extraction with c5h-c's install and MODS, the
  catalogue's database copy, bundle `C5H-0301-TINY_0009_...`'s height field, 4354's catalogue text. Writes
  `placement_report.tsv`, `scenes/*.c5units` (one per catalogue frame) and their round trip, `pose_list.tsv` /
  `pose.tsv`, `report_units.tsv` (one row per placed unit: B, C per pass, D, E, P8, causes), `summary.json`. Exit 0
  report written, 1 a step failed, 2 usage, 3 an input missing, 4 OUT outside the work folders.
- Tests `render/tools/unitscene/test_unitscene.py` (UNITSCENE = the built tool; skips with a message without it):
  usage errors (exit 2), a scene round trip, six refusals with their line (exit 1, the good file beside still read),
  a missing file, the pose command's unreadable list, wrong field count, odd rows file and a missing model (row
  `BadModel`, exit 0), the driver's guards (4, 3, 3). 11 passed.
- Run 2026-11-15 15:05 (whole oracle set, about 2 min on the workstation). Numbers in
  `render/evidence/0041/4355/` (`oracle_summary.json`, `report_units.tsv`, `oracle_pose.tsv`, `scenes/`).

### 13.3 Results per DoD item [L]

- **Scenes:** 20 `.c5units` files (16 land, 4 sea frames, 229 units, players with their PlayerColors, art style as
  the civilization Type (the lead's decision), era; facing and seed from 4354's catalogue (12.1's rule); figures with
  clip and t from the K3 fits, numbered by the id order (P8)); all 20 read, written and read back equal; the negative
  file (a figure before any unit) refused `Orphan at line 3`, exit 1.
- **B (draw inputs) and C (draws per pass): not run** (first round; run in the second, 13.6). 4352's `UnitRenderer` (art, repacks, palette blocks, ids,
  cb1, the three passes) is not in this branch's build (its headers are; no `units.cpp`/`draw*.cpp`). Every unit's
  row says so. The tool and the scenes are ready for it: `prepare` takes the scene files as they are.
- **D (placement):** 4354's check through `unitscene placement`: 24 formations, 229 units, 138 multi-figure units,
  heights 1,083 of 1,135 within 0.001, 0 failed (section 12 has the classes); water level 10.1167316.
- **E (pose) and P9 (K3 on the whole catalogue):** 1,586 fitted mesh-instances in 0327's K3 table; 51 not listed
  (27 "no fit": every idle clip of the member holds a curve format the Python reference does not decode, e.g. the
  caravans' camels, early great scientist, SS booster, battering ram, B29, five sailing ships; 24 a swordsman
  attachment mesh of no member model). 1,535 posed by the module: all `Ok` (after the reader fix of 11.1, which turned
  5 Bandeirante flag-bearer `BadModel` into `Ok`). 53 have a fit whose placement is not a rotation about z (tilt >=
  0.05 deg: rigid callout meshes whose free rotation absorbs any pose, runner-up ratio 1, and aircraft); they are not
  compared. Of the other **1,482, 1,479 are within 0.05 deg and 0.01 world units** of the captured rows: rotation
  median 0.00034 deg, p95 0.0044, translation median 0.00014, p95 0.00092 (0327's figures, now through the renderer's
  code). The 3 outside (cannon captain 0.061 deg, SS capsule truck 0.064, English longbowman 0.061) equal their own
  fit's residual (0.056, 0.057, 0.061): the fit, not the pose. Per unit: 192 units with compared instances, 189 all
  within.
- **P8 (figures in member-info row order, each member NumMembers times) [L for 184 units].** From the id order of
  each unit's blocks (a figure's meshes consecutive): 184 units as P8 says; 37 not checkable (the instances do not
  make the member count: single-figure mixtures of 3909's case (a), units with unfitted instances, no instances);
  8 differ (atomic bomb, caravan, missionary, pioneer, Russian cossack, great admiral, dromon, sea beggars), each a
  unit with unfitted or attachment instances or several meshes per member that the id-run rule groups wrongly [I:
  not inspected one by one].
- **Not covered, with cause (report_units.tsv):** 21 units with a "no fit" instance (formats the Python fit lacks;
  the C++ sampler decodes them, a C++ fit of those clips is the next step), 32 with a tilted fit, 2 with the
  attachment mesh, 5 units without instances (not drawn in their frame, unit-draws.md 7).
- **Terrainreplay's cube views (0347's open point):** `manifest.py` now writes a TEXTURECUBE / TEXTURECUBEARRAY view as
  DIM 4 (square faces, ARR a multiple of 6), as 4152's `unit_manifest.py` did by hand. Base (7dcd110d) against
  branch, `render/evidence/0041/4355/base_vs_task.py` / `base_vs_task.json`, all ten variants `--no-run` on 0347's ten
  exports plus a hardware replay: SHA-256-equal on 0327's terrain export (1,737 files; replay exe, nooverlay, nofow:
  18 files) and both 4109 bake exports (93, 229); on the other seven (stock city, coast, river, highlights, 3901, P4
  shown and hidden) only the `tex` lines of a 128 x 128 cube bound at PS t0 (and t2 in the P4 exports) change from
  DIM 2 to DIM 4, every other byte equal. Tests: `test_terrainreplay.py` 80 cases, 0 failed (5 new: cube and cube
  array views DIM 4, a 6-slice 2D array and a non-square cube view stay DIM 2, six sub lines).

### 13.5 Second round (2026-11-15 15:09-, after 4352's module and 4356 were merged) [L]

The rows of 13.3 for B, C and P8 are replaced by this section (13.3 kept as the first round's record).

- **The one command now covers B to E.** `oracle.py` additionally (a) writes 4352's manifest
  (`render/evidence/0041/4352/oracle_manifest.py`, 3907's 20 unit frames and 3901's export) and runs 4352's catalogue
  checker (`c5r_units_test_draw_oracle --manifest`) for B, parsed per unit (`render/tools/unitscene/oracle_draws.py`);
  (b) runs the module's whole CPU path on the 20 scenes (`unitscene prepare`: `UnitRenderer::open` without a device,
  `prepare` with its own pose (11) and placement (12)) and reports per unit its status, block count and warnings,
  and compares its palette blocks with the captured ones (blocks paired by member row, figure, mesh and index count);
  (c) for C, per unit and pass, the module's own draws (`c5r_units_test_draw_oracle --draw`) against the captured
  draws replayed by terrainreplay (scene and reflection: one captured replay per frame and pass, the unit's pixels
  from its id buffer; mask: one replay per unit, the pixels either picture wrote), statistics per COMMON 25 and per
  frame and pass one DIAGNOSTIC picture set in Visual Checks (issue 4355). `--reuse-c` takes C from an earlier run
  (`OUT/bc/c_results.json`); `--no-c` skips it. Report `report_units.tsv` columns B, C_scene, C_reflection,
  C_mask, D, E (K3 fits through the pose layer), module (status, blocks, warnings), E_module (the prepared palette
  against the capture), P8.
- **The module's mesh order (block and draw order within a figure) [L, C].** For 5 units the prepared blocks came in
  another order than the captured ones (horse and rider swapped; the chariot archer's driver and archer after the
  chariot). Rule found in their files: **the EXE numbers a figure's blocks in the file's first Model's MeshBindings
  order**, not in the Meshes list's: VP's FBX cavalry files bind horse then rider (Knight_Turkey, Mamluk) or rider
  then horse (Horseman_Turkey, Horseman_Viking, Keshik_Mongol) in the order opposite to their Meshes lists, and
  `chariotarcher.gr2` binds Driver, Archer, Chariot x3 while its Meshes list starts with the three chariot meshes.
  `render/src/units/model.cpp` now builds `UnitModel::meshes` in that order (meshes no Model binds after them, in file
  order; `ModelMesh::index` keeps the .gr2 index); test `test_model.cpp` `mesh_order_follows_mesh_bindings`
  (chariotarcher: driver, archer, chariot x3, .gr2 indices 3, 4, 0, 1, 2) and `material_parameter_textures` finds
  the rider by name. 4352's id check (figure-major ids per model), rerun on the catalogue: **301 -> 306 of 318**
  models; the 5 fixed are those units; none newly failing. The 12 left: the Mamluk (L06: 8 instances, 7 figures, the
  missing horse of 12.5), free company (2 instances, 3 figures), SS engine and stasis chamber and the battering ram
  (models shared between members, the checker pairs the wrong draw), Russian cossack v2/v3 (meshes shared between
  variant models), the akula, nuclear submarine and submarine (their ICE and plain meshes share one vertex buffer and
  index count, so no pairing can tell them apart), the workboat. Pixels do not depend on this order (C).
- **P8 [L] as a rule:** a unit's blocks in id order, cut into figures in member-info row order (each member
  NumMembers times) taking as many blocks as its model draws meshes (4352's B report); P8 holds where every block
  the K3 fit attributed to one member model alone lies in a figure of that member. **0 units contradict P8** (the
  first round's 8 "differ" were artefacts of its id-run grouping: four single-figure units whose only block has no
  fit (atomic bomb, great admiral, dromon, sea beggars), the caravan and pioneer with unfitted camel blocks, the
  missionary _01 / _02 and the Russian cossack whose variant models share meshes, so the fit's member is a guess).
  Counts of this round in 13.6.
- **P5:** 11.4 (searched the pose sequences and the catalogue; multi-bone degree 3 stays open; answered 2026-11-16 by
  0379 on the fresh capture's mech block 51, 11.4).

### 13.6 Second-round numbers (2026-11-15, whole oracle set: 3907's 229 units and 3901's 2) [L]

Run: `oracle.py --out C:/c5-64bit-work/0041/4355/oracle3 --exe <render build>/tools/unitscene/unitscene.exe` (B-E,
C on the hardware device, 15:22-16:20 EST, through `quietlock.py run`), then `--reuse-c` on the build with the mesh
order of 13.5, then `--reuse-c --c-passes mask` with the id-set pairing (below). Numbers in
`render/evidence/0041/4355/` (`oracle_summary.json`, `report_units.tsv`, `c_results.json`).

- **B (4352's checker on this build):** units 231, art row rule equal 231/231, all 231 load (Tadodaho with the 8 x 8
  texture fallback: the one unit with a `UnitStatus` warning, shown in the report's `module` column); meshes 530:
  vertex buffers exact 434, ties only 12, one-step differences 69 (4356's classes), absent 15; index buffers 515/515
  drawn; t0, t1 and programs 515/515; header rows 5,134 / 5,170; figure-major id models 306/318 (13.5); cb1 read lanes
  63/63; environment textures 4/4 found in the install.
- **The module's prepare on the 20 scenes:** 229/229 `Ok`, 1 with warnings (Tadodaho TextureFallback); about 0.3 s
  per frame of 12 units (workstation indication, COMMON 23). **Its palette against the captured blocks** (blocks
  paired by member row, figure, mesh and index count; bound bones' rows, row 0, row 1 x y z): 216 units compared,
  1,472 blocks; **182 units equal** (rotation element < 1e-3 and translation < 0.01 world units); the 34 others by
  class: 25 units whose K3 fit is tilted or missing (aircraft, ships, rigid parts: the EXE's placement there is not a
  rotation about z alone, or no fit gives clip and t), 3 units with a cross-fade fit (cannon, SS capsule, English
  longbowman), 3 placement-only differences (WW1 tank 0.025, the two submarines 0.05-0.06 world units; section 12's
  sea and other classes), 3 other (SS engine, stasis chamber: models shared between space-ship parts; modern great
  general 0.10). Row 0 within one 8-bit step and row 1 x y z equal in every unit with a compared block (208; the 8
  others have no block with a fit to pair, so nothing was compared).
- **P8:** 205 units as P8 says, **0 against**, 24 not checkable, each with its cause in `P8_note`: 11 units whose
  blocks no fit attributes to one member model alone (single-figure units with an unfitted block, the cossack's and
  missionary's shared meshes), the Mamluk (L06: 15 blocks for 16), 5 units not drawn in their frame (L06's marine,
  mech, mechanized infantry, merc; the modern cargo ship), the Mayan atlatlist, conquistador and tercio (members whose
  model draws a mesh 4352's B report counts as absent), the attack submarine and the workboat (blocks of meshes shared
  or not counted), the SS booster (3 blocks, 4 meshes: the material 87 bracket draws no block).
- **C (the module's own draws against the captured draws, per unit and pass):**

  | Pass | Units with pixels | Bit-equal units | Pixels | Equal pixels | Max |
  |---|---|---|---|---|---|
  | scene | 228 | 200 | 2,051,032 | 2,040,775 | 252 |
  | reflection | 228 | 208 | 235,790 | 235,086 | 189 |
  | mask | 228 | 221 | 737,090 | 728,599 | 255 |

  228 units have captured draws (3907's 229 and 3901's 2, less the units not drawn in their frame). 199 are
  bit-equal in every pass. Of the 29 others (per unit in `report_units.tsv`): 15 differ by at most one 8-bit step
  (the named weight-tie vertices and one-step meshes of 4356, 0327's floor), 2 by at most 3 (Spanish conquistador,
  modern great admiral: one-step meshes), and 12 more, each with a vertex-buffer or pairing cause in its B column: Licorne, SUEA MOP, Korean hwacha, Siamese war elephant, Landsknecht, Ottoman sipahi, the
  Renaissance great diplomat and the postmodern great artist (meshes of 4356's one-step class "differ", up to 252);
  the battering ram (its crew models are shared by the ram's members; 4352's id check names it); the attack, nuclear
  and plain submarines (their ICE and plain meshes share one vertex buffer, so the module draws the one its pairing
  finds, with the other's texture: max 23-35). Not drawn by the module (no captured draw to compare): the 5 L06 units
  and the cargo ship above. 3901's warrior: 14,711 of 14,724 scene pixels equal, max 1 (its 13 tie pixels, as
  4352's 10.5); the pathfinder bit-equal.
- **Mask pairing fixed (13.5's id rule):** the first C run paired a scene draw with its reflection and mask draws by
  their id sequence; the ids within a draw come in another order per pass (depth-sorted), so 45 units had no mask
  draw. Pairing by the id set (`oracle_draws.id_set`) finds them; the mask pass was run again (`--c-passes mask`).
- **Visual Checks:** one DIAGNOSTIC entry per frame and pass (ours, the captured replay as reference, the difference
  x8 as comparison), issue 4355: 63 from the full run (16:06), 63 from the mask rerun (16:46; it ran and recorded
  all three passes again, and the scene and reflection numbers came out identical to the first run, a repeat of the
  GPU comparison), 3 from a stopped earlier run of 3901 (15:19); `visualcheck.py list --issue 4355 --result
  undescribed` lists none.
- **P9 (C++ fits of the 51 instances 0327 left unfitted), `render/evidence/0041/4355/p9_cpp_fits.py` [L]:** all 51
  fitted through posefit's search on the C++ sampler (`posesample`: every curve format the module decodes, every
  track group, each mesh with its own skeleton), every clip of the owning member, s fixed to the member's Scale.
  **19 are unambiguous** (runner-up at least 1.5 times the cost; caravan and pioneer camels, Fusta, galleass, Venetian
  galleass, great admiral, dromon, sea beggars, early great scientist, SS booster truck, battering ram crew and
  captain): all within 0.05 deg, at most 0.0036 deg. **32 are rigid** (one or few bones that move together: any clip
  fits with a free rotation, runner-up ratio < 1.5): the 24 swordsman "Shovel" blocks (one bone; tilt 94 deg in the
  free fit), the ram's beam (2) and one crew block (118 deg: the free rotation's minimum lies elsewhere), the B29, the
  scientist's cart, the booster and its trailer, one galleass block; their pose follows the figure's clip from its
  body mesh, which the scenes take (`oracle.py` picks a figure's clip from its most telling fit, 13.5). So **K3 holds
  on the whole catalogue**: every catalogue mesh-instance is either fitted to one clip and t within 0.05 deg by the
  module's sampler (1,479 + 19 + 3 fits within their own residual, 13.3) or is a rigid part, a tilted callout or
  aircraft (53) or a cross-fade (3, 0327's 1.4).
  Two findings on the way: (a) posesample prints mesh names that contain spaces ("Hun_battering ram ", "Venetian
  Galleass") and two meshes of one file can share a name (`galleass.gr2`): posefit's C++ path now keys its rows by
  (name, index count). (b) **0327's file look-up took another copy of some models than the game does:**
  `swordsman.gr2` and `barbarian_swordsman.gr2` exist in UnitModels.fpk (one mesh) and Upgrade1Models.fpk (with the
  78-index "Shovel"); exact_t's `ArtFiles.find` takes the first of a sorted walk (UnitModels), the game and the module's
  GameFiles the later pack's (B: Shovel drawn, vertex buffer exact). So 0327's "24 instances of a mesh of no member
  model" (unit-formations.md 12.2) are the Upgrade1 swordsman's Shovel. Which other models have such duplicates was
  not checked [I: fits that passed used a copy identical in the bones they bind].

### 13.4 Not verified

Updated 2026-11-15 20:20 after the second round (13.5, 13.6). Closed: B and C (run), the 27 "no fit" instances (P9),
the first round's 8 P8 "differ" (artefacts). Still not verified:
- ~~Multi-bone degree 3 against a capture (11.4: no capture of the oracle set or the pose sequences holds it).~~
  Answered 2026-11-16 (0379, 11.4): the fresh capture's mech block 51.
- The 53 tilted fits and the 32 rigid P9 blocks: their clip and t cannot come from their own block; the scenes take
  the figure's body mesh's fit.
- The 12 units whose draws differ by more than 3 (13.6): their causes are named (4356's one-step meshes, shared
  meshes); the meshes are not repaired here.
- The EXE's reason for the 12 models left in the figure-major id check (13.5) and for L06's missing Mamluk horse.
- Which other model files have duplicates whose copy 0327's look-up took differently from the game (13.6 P9).
- The 3,500 figures of a real game (U6); the terrainreplay replays of the seven exports whose cube lines changed
  (only their manifests were compared).

## 14. Not verified

Sections 2-7 are carried out as sections 10-13 report; what stays open at the epic's end (lead-0041, 2026-11-15):

- ~~The cause of the 69 one-step meshes (10.8): needs a read of the EXE's model-load path; the units-per-metre float
  hypothesis was tested and rejected.~~ **Answered 2026-11-16 by ticket 0379 (10.9) [C, L]:** Granny's basis
  conversion at load with desired units per metre UnitsPerMeter x Scale (factor fl(fl(u x s) / u)), the cofactor
  inverse for normals, Granny's half conversion; `render/evidence/0379/vstep_check.py` reproduces 515 / 515 drawn
  meshes and predicts exactly the 81 not-exact ones. The fix of `render/src/units` is a follow-up ticket (10.9's last
  paragraph).
- ~~The EXE's choice of weight-tie direction (12 meshes, 10.2): no integer rule decides it.~~ **Answered 2026-11-16
  (0379, 10.9) [C, L]:** a float renormaliser on every skinned vertex, floor(fl(fl(255 / s) x w) + 0.5).
- ~~Degree 3 on several bones of one mesh against a capture (P5): no capture holds it.~~ **Answered 2026-11-16 (0379,
  11.4) [L]:** the fresh capture's mech block 51, idle B with six bound degree-3 bones, to rotation element 1e-6.
- The 12 units with larger draw differences (13.6), the 12 models outside the figure-major id check, L06's missing
  Mamluk horse (P7, not reproducible).
- The era rule beyond the catalogue's rows and the `_AMER` / `_ASIA` cultural rows (10.7).
- Palette buffers past the first: the EXE's rule for a draw at the 167-block boundary (10.7).
- Everything of U4-U7: the module has not drawn into the server's frame, has no feed input yet (ticket 0375), and no
  controller (ticket 0374); clip and t are inputs.
- Corrections owed to files outside this epic (listed in the merge request): GameFiles' name lookup returns
  `UnitModels.fpk`'s 21-entry `UnitFormations.xml` where the game uses the loose 24-entry `Assets/Units` copy
  (12.4, for `asset-pipeline.md`'s owner); 0327's file lookup took `UnitModels.fpk`'s `swordsman.gr2` where the game
  takes `Upgrade1Models.fpk`'s (13.6; `unit-formations.md` 12.2's "mesh of no member model" is the Upgrade1 Shovel).

### 14.1 The controller behind `PoseSource` (epic 0374, 4409, 2026-11-15)

Additive to this module: `ModulePose::rowsProgram(req, program, out)` and `poseFigureProgram` (pose.h) pose a figure from a pose program (samples and per-bone blends, `c5r/unitanim/pose_program.h`); a program with one Sample goes through `poseFigure` itself and is bit-identical to `rows()`. `ModulePose::clipDuration` gives a clip's duration. The controller's `ControllerPose` (`c5r/unitanim/controller_pose.h`) is the `PoseSource` that answers `rows()` from the program set for `(player, id, figure)` and refuses a figure without one. `units.h`, `PoseRequest` and `.c5units` format 1 are unchanged; 0041's suites and the oracle's pose rows are byte-equal before and after (`unit-controller.md` 13.2).

## 15. The open and the first rows off the render thread (ticket 0704, 2026-11-23)

Ticket 0704 (account B, branch `task-0704-units-pass-load-cost`), from 0689's split (`pan-coherence.md` 5.8: once per load
one server pass with the `units` stage on top, 226-338 ms on the workstation, the composite's 244/364 ms wait under D242).

### 15.1 Where the once-per-load cost goes [L: render.log lines of earlier runs, read 2026-11-23 by this ticket]

The server's units scene (`server/src/units/srv_units_scene.cpp`, `UnitsHost`) opened its drawing renderer
(`UnitRenderer::open` on the render device: the art tables, the unit string databases, the four environment textures, the
unit programs read from the EXE, the formation file, the device's state objects) at the first frame after the pre-check
passed, on the render thread, and that frame's `prepare` then loaded every art row it drew (L0 + L1: the members' art,
the `.gr2` models and their `.dds` textures, `ModelCache::get`) and its first `draw` uploaded them (`ModelCache::upload`).
The controller's first frame (`AnimRunner::apply` with every held unit's create) runs in the same frame. Split from the
`units: ... is open` line's time after the pre-check's line and the `slow frame` line (`frame > 250 ms`: build and fog +
the controller + prepare + the scene pass; the open is before it and not in it):

| run (save, machine) | open (log time after the pre-check) | controller's first frame | first prepare (art rows) | scene pass |
|---|---|---|---|---|
| L1 (Tiny t18, workstation, `C:/c5-64bit-work/0673/live-0692/L1.render.log`) | 151 ms | 40.6 ms (35 held, 2 drawn) | about 35 ms (no slow line: frame < 250 ms) | - |
| 0687 (reporter's Tiny game, workstation) | 183 ms | 79.4 ms (67 held, 3 drawn) | 192.0 ms (3 rows) | 0.2 ms |
| hold-nofield 12:35 (Tiny, workstation, c5h-new-b) | 142 ms | 81.2 ms (44 held, 12 drawn) | 232.3 ms (10 rows) | 0.1 ms |
| live-0692 16:14 (a 790-unit save, workstation) | 142 ms | 372.7 ms (790 held, 33 drawn; 18 clip files read) | 604.1 ms (19 rows) | 0.3 ms |
| D237 12:23 (huge t350, workstation) | 165 ms | 388.2 ms (853 held, 29 drawn; 26 clip files) | 274.8 ms (11 rows) | 0.2 ms |
| laptop acc0691b (TB-HUGE_0350, 5.9) | 195 ms | 382.7 ms (853 held, 29 drawn; 26 clip files) | 293.3 ms (11 rows) | 0.4 ms |

So the pass is three parts of similar size on the huge save: the open (140-200 ms), the controller's first frame (80-390
ms, it scales with the held units and the clip files read on the render thread) and the first prepare (190-600 ms, the
art rows' model and texture loads). `ana0704.py` on L1 reproduces 5.8's figure: the units pass 246.1 ms busy (units
stage 226.18 ms, frame 5398), the composite's longest wait 244.3 ms, one present over 100 ms.

### 15.2 The change (step 1: the open and the art rows) [C: code and test]

`UnitsOptions::openAsync` (`srv_units_scene.h`), set only by server.exe's factory (`MakeLive`; no configuration key):
at the first frame after the pre-check passed, `openDraw` starts `OpenRun` (`srv_units_scene.cpp`) on its own thread
instead of opening: the passed pre-check's environment (kept by `PrecheckRun` after its run and handed over in
`evaluate`; a second worker, after a device change, opens its own through the host's `EnvOpener`), `UnitRenderer::open` on
the render device (an `ID3D11Device` is free-threaded; the server's is not created `D3D11_CREATE_DEVICE_SINGLETHREADED`,
`srv_render_host.cpp`), then `UnitRenderer::loadUnit` for one unit per distinct (Units.Type, art style, era) of the table
at that moment (the owner's art by `UnitsCore::playerArt`, the rule `buildList` uses), then `ModelCache::upload` on the
device. The frames until it is done are not drawn and not ready (as during the pre-check: the mode word's start-up
transition, O6 of `unit-server-scene.md` 8.1, where the EXE draws its units). The render thread joins it when `done`,
takes the renderer (`m_draw`) and keeps the worker's environment as long as the renderer (it points into it), then sets
the pose and placement sources and says the same `is open` line as before. A result for another device or database
(`setDevice`, `startDatabase`, both set its cancel flag) is dropped and the open starts again; an environment or open
failure is D125's route as the open on the render thread; an upload failure is LOUD and left to the scene pass's upload.
Lines: `units: the drawing renderer opens on a worker thread (ticket 0704): K art keys of the table's N units loaded
first` and `units: the drawing renderer was opened on a worker thread (ticket 0704): environment [the pre-check's] .. ms, open .. ms, K art
keys loaded (F failed, S skipped) in .. ms, upload .. ms; worker .. ms, taken .. ms after its start, F frames not drawn
meanwhile; R art rows cached`. Not warmed: an embarked unit's row (`EmbarkedRow`) and units created after the start;
prepare loads such a row at its first frame, as before.

The offline tools (sceneshot, unitfeed) and the tests keep `openAsync` false: their first built frame must be the
recorded one, and the recorder's units history starts at the frame before the first one drawn with `ready()` set, which
with the worker is the frame that takes the renderer and builds (`srv_scene_recorder.cpp` `noteUnitsFrame`), as before.
TEST ENV `C5_RENDER_UNITS_OPEN_SYNC=1` (server.exe, LOUD once) opens on the render thread as before: the before arm of
one build.

Test `open_async` (`server/tests/units/test_scene_units.cpp`): the first frame starts the worker and draws nothing;
the frame that takes it draws the warrior byte-equal to the render-thread open's target, with its one row loaded by the
worker (`1 art keys loaded`, `1 art rows cached` at the take) in the pre-check's environment; a device released and given
again opens again (its own environment); a device change during the worker's open drops its result; a worker whose own
environment does not open fails the scene (D125); a host destroyed during the worker's open joins it.
Plain and MSVC ASan: 81 of 81 cases (2026-11-23 16:43, ASan build with `CL=-wd4456` before main's a0a56d99b fixed
motion.cpp's C4456, which had broken every `--asan` c5render build since 4982).

### 15.3 Before and after, live [L, 2026-11-23; one run per arm: an indication, not a timing claim (COMMON 23)]

One server.exe per machine (`server/evidence/0704/build0704.sh`, branch at `b3880cde2`: server.exe `3bcd2292...`,
composite `3a9df5ee...`, proxy `1ffd4c3e...`); the before arm is the same binary with the TEST ENV
`C5_RENDER_UNITS_OPEN_SYNC=1`. Workstation: c5h-c (EXE 4390913, mods548, hidden, MC-B #11923), 16:44-17:08 EST, copies of
`C5H-0167-TINY_0008` and `C5H-VP8P-HUGE_0350`, `server/evidence/0704/live0704.sh` (load, the units' first frame, one key
and drag series of drive0691.py), with eight to ten other teams' builds and suites running (`quietlock check` 16:33):
loaded. Laptop: quiet testbed jobs (each alone) `20261123-164742-d8cf` (before) and `20261123-161949-fd24` (after),
TB-HUGE_0350 tb1, EXE 4390913, `server/evidence/0704/lap0704.sh` (capture_job.py product arm, case `wide` only, not
recorded), test-only `render_pass_long_ms=8`. Reader: `server/evidence/0704/ana0704.py` (the pass with the largest `units`
stage between the pre-check's end and 3 s after the controller's first frame; the composite's largest wait and present
interval there from the lockstep CSV). Logs: `C:/c5-64bit-work/0704/live/` (workstation), `F:/c5-64bit-work/0704/lap-*`
(laptop); not in git.

| arm | units pass busy (units stage) | composite's longest wait | longest present | controller's first frame | first prepare (rows cached) | open on the render thread |
|---|---|---|---|---|---|---|
| workstation Tiny, before | 530.8 ms (490.3) | 526.5 ms | 534.9 ms | 166.8 ms | 65.4 ms (2) | 234 ms (log) |
| workstation Tiny, after | **67.1 ms (55.5)** | **57.6 ms** | 66.0 ms | 44.4 ms | < 250 ms frame (no slow line) | 0 |
| workstation huge, before | 1,171.7 ms (1,103.1) | 1,043.0 ms | 1,130.1 ms | 493.5 ms | 359.6 ms (11) | 287 ms |
| workstation huge, after | **610.7 ms (594.3)** | **589.7 ms** | 739.6 ms | 512.3 ms | 71.9 ms (43) | 0 |
| laptop huge, before | 813.8 ms (783.3) | 753.3 ms | 826.7 ms | 378.0 ms | 258.1 ms (11) | 153 ms |
| laptop huge, after | **475.0 ms (468.5)** | **486.9 ms** | 525.1 ms | 393.6 ms | 66.4 ms (43) | 0 |
| laptop huge, 5.9's run (`acc0691b`, build of ec19daa90) | 910.6 ms (836.3) | 877.6 ms | 891.3 ms | 380.0 ms | 293.3 ms (11) | 195 ms |

The worker (after arms): environment 659 ms (laptop) / 2,747-2,815 ms (loaded workstation), open 228-276 ms, art keys
261 of 853 units (huge; 43 art rows) in 1,158-1,492 ms, 14 of 22 (Tiny) in 108 ms, upload 5-47 ms; taken 2.1 s (laptop),
3.2 s (Tiny) and 4.6 s (huge) after the pre-check's end, 153-371 frames not drawn meanwhile (the EXE draws its units then,
O6). **Why the numbers moved:** the open (150-290 ms) and the first prepare's row loads (190-290 ms less: the 11-row
prepare of 258-360 ms became 66-72 ms with every row cached) left the render thread; what is left of the pass is the
controller's first frame (378-512 ms on the huge save, 44 ms on Tiny), which this step does not touch and which did not
move (378 -> 394 ms laptop, 494 -> 512 ms workstation), and 66-72 ms of the first prepare (one row was still loaded at the
frame: 42 rows cached at the take, 43 after; the rest is the first frame's pose work, not split). The Tiny before arm ran
slower than 5.8's L1 (226 ms) on a machine with eight other heavy jobs.

**Follow-up in this ticket:** the worker takes the passed pre-check's environment (`PrecheckRun::env`, handed over in
`evaluate`) instead of opening a second one, which was the largest part of the worker's time (0.7-2.8 s) and so of the
window with the EXE's units; tests `open_async` (the first take says `environment the pre-check's`, the open after a
device change opens its own and its failure is D125's) pass. Live on the laptop (quiet job `20261123-172233-9fd9`,
server.exe `f6b5e679...` of `8d63669ef`, same shape as above): `environment the pre-check's 0 ms`, open 167 ms, 261 keys in
1,262 ms, upload 36 ms, worker 1,464 ms (was 2,084), taken 1.5 s after the pre-check's end with 69 frames not drawn (was
153); units pass 426.4 ms (units stage 420.3: the controller 361.8 + prepare 55.0), composite's longest wait 365.8 ms.

**Step 2 (not in this step):** the controller's first frame (`AnimRunner::apply` with every held unit's create, 26 clip
files read on the render thread on the huge save) is now 85-90 % of the remaining pass; it lives in `srv_units_anim` (epic
0042's W99 holds it until 5057 merges). What it does per create [C, `render/src/unitanim/controller.cpp`
`Controller::create`, read 2026-11-23]: the unit's art resolved (`art_->resolve`), each member's machine (`member`), then
for every living figure `enterState` into the machine's default state, which takes a graph instance from the clip graph
factory (`graphs_`; a clip the factory has not bound is read then: the 26 files), for all 853 held units, drawn or not
(824 are quiet). Candidates [I, not measured]: the factory's binds and the members' machines prepared on the worker for
the table's rows (as this step does for the models), or the not-drawn units' creates deferred to their first drawn frame
if the picture stays byte-equal (rule A; 5056's B and C were kept out for that reason, unit-server-scene.md 20.5). The
Architect's frame-jobs ticket 0711 J1 ("the units' begin on a worker", MC #12111) lands after 0704 and covers the
per-frame begin, not this once-per-load create burst [I: from its title].
