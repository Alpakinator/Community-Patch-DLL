# Unit draws in the EXE's frame: shaders, constants, bone palette, team colour, instancing, formations, cities (3909)

Written 2026-11-15 02:15-02:42 EST by the implementer of task 3909 (account B, branch `task-3909-kb-unit-draws`, epic
0301 W7) for lead-0301. It extends 3901's P5 (`research/06-tooling-and-workflows/offline-scene-harness.md` section 8,
one capture) to 3907's whole stock catalogue (`research/06-tooling-and-workflows/scene-catalogue.md`). The city part
stands beside 0319's `cities-and-landmarks.md` (merged on `main` 2026-11-15, MC #1408), which owns the city assembly;
this file adds the draws' constants, instancing and per-city counts.

**Inputs.** 3907's 26 exports made with `rdc-export --select all` (`F:/c5-64bit-work/0301/3907/exports/`: `units_land/L01..L16`,
`units_sea/S01..S04`, `cities/c_ancient|c_renaissance|c_industrial|c_modern`, `p4_shown_all`, `p4_hidden_all`), read in
place; no new conversion, no replay, no game instance. Their setup (3907, 2026-11-15 00:11-01:16): workstation, c5h-c,
32-bit DLL `i0ship-26234371-ia`, CP/VP/EUI, stock `config.ini`, frame cap 30, window 2554 x 1411, Vibrant Textures
present (D94), RenderDoc 1.47, save `C5H-0301-CAT_0009` (Tiny 56 x 36, turn 9; copy of `c5h-9028-tiny-t0008`, SHA-256
b2ceb663...). The placed objects (type, owner, plot) are 3907's `shell/evidence/0301/3907/index.csv`. Database: a
read-only copy of c5h-c's `Civ5DebugDatabase.db` (2026-11-15 02:15). Shader names: SHA-256 of the capture's DXBC
against the EXE's shader model 5.0 blobs at the `50:` offsets of `evidence/2900/2903/material_table.txt` (VA =
0x6000000 + offset; read at run time from c5h-c's `CivilizationV_DX11.exe`, 1,374 blob references, 432 distinct
hashes). Register use: the capture's DXBC disassembled with the system's `d3dcompiler_47.dll` into scratch on C: and
not kept (D83); this file states what the programs do in its own words.

**Records.** Numbers only, in `shell/evidence/0301/3909/`: `shader_programs.csv` (hash prefix, stage, EXE VA,
material, pass), `program_pairs_by_target.csv`, `frames.csv` (per export: draws per pass, instances, palette buffers,
figures), `units.csv` (229 placed units: figures, database members, offsets, heading, height, header rows, colour
distance), `cities.csv`. The scan scripts (`units.py`, `figures.py`, `cities.py`, `blobnames.py`, `make_evidence.py`;
own code, no game data) are archived with the scratch outputs in `F:/c5-64bit-work/0301/3909/`.
(Added 2026-11-15 04:56 after queue #1497 (epic 0327, `unit-playback.md` 6, K9): `shader_programs.csv` and
`program_pairs_by_target.csv` lacked the non-skinned unit programs, because the scan kept only skinned and building
draws. Four rows are added from 3907's exports L06 (frame 3084), L09 (1678) and S04 (1141), one mesh per pass in each:
VS `82e59838` 0x68005D0 (materials 83/85/87/88, pass 0) with PS `0e7a3566` 0x680CC38 (`UnitShader_CODE`, pass 0) into
the scene (2419) and the reflection (13415); VS `59af33ff` 0x6802938 (87/88, pass 1) with PS `9a5991d7` 0x680E7F0
(87, pass 1) into the shadow mask (13383). Replaced: the files' silence on material 87, and "L06 frame 3084: one draw
per pass" in section 0.)

Marks: `[C]` read in the export (buffers, state, the DXBC's register use) or the database; `[L]` a count over the 26
stock frames; `[I]` inferred. Capture, frame and draw ids are given as `export frame/draw` (frame = the capture's frame
number; draw = the export's frame-order index `n`).

## 0. Results in one screen

- **Shader sets [C].** Every skinned unit is drawn by `UnitShader_Skinned_CODE` (material 90) in **three passes** with
  the same instances: pass 1 (VS `8cb52b96` at 0x6804E48, PS `72c27aea` at 0x6815328) into a 2554 x 1411
  R16G16B16A16_FLOAT target without depth that the ShadowShader turns into the terrain's shadow map; pass 0 (VS
  `29a0b061` 0x6803140, PS `60d270e1` 0x6813630) into the water's 1024 x 1024 reflection target; pass 0 again into the
  scene target with depth. One ship's mesh used the alpha set (material 89, PS `5def4f99`). Non-skinned unit art uses
  `UnitShader_CODE` (material 87; L06, L09 and S04: one draw per pass each, corrected 2026-11-15 04:56, see Records). City blocks use `BuildingShader_CODE_Instanced(_Opaque)`
  (materials 4/5), animated building parts `BuildingShader_Skinned_CODE` (material 6).
- **Constants [C].** One 192-byte dynamic cb1 (12 float4) per pass: the VS reads cb1[3] (camera position) and cb1[4..7]
  (view-projection, row-vector form: `pos.x*c4 + pos.y*c5 + pos.z*c6 + c7`); the PS reads cb1[8].x (a height below which
  the unit is drawn at 0.33 alpha), cb1[9] (distance-fog factors and the fog ramp's row), cb1[10] (fog colour) and
  cb1[11] (camera position). cb1[0..2] (the camera's rotation rows) are read by neither.
- **Bone palette [C].** VS t27 is a 262,144-byte dynamic buffer of 16,384 float4 = 167 blocks of 98 rows. A block is one
  **mesh of one figure**: row 0 = team colour (rgb, a = 1), row 1 = (alpha from, alpha to, fade, w), then 32 bones x 3
  rows of a 3 x 4 world-space skinning matrix (translation in w). The matrices carry no scale: the EXE stores the mesh's
  vertices already multiplied by the member's `Scale`. Rows of bones the mesh does not weight hold stale values. A
  frame with more than 167 mesh-instances continues in a second buffer (resource 860, ids from 0 again), the same pool
  the skinned building parts use.
- **Team colour [C, L].** Header row 0 equals the owner's `PlayerColors.SecondaryColor` (the `*_BACKGROUND` colour)
  for all six owners in the catalogue, within 0.004 (8-bit rounding). The PS tints the base texture by it squared,
  masked by the base texture's alpha (alpha 1 = untinted). It enters as a per-instance buffer row, not a constant,
  texture or vertex attribute.
- **Header row 1 [C, L].** x, y, z = (1, 0, 0) on all 1,534 mesh-instances of the placed units: the PS's alpha is
  `x + z * (y - x)` = 1. Its w takes 1,420 distinct values from -692 to 1,118 and no program reads it.
- **Instancing [C, L].** One `DrawIndexedInstanced` per mesh of a unit model, `InstanceCount` = that model's figures in
  the unit; the per-instance R32_UINT id comes from an 8,192-byte dynamic vertex buffer (slot 1, its offset per draw),
  and selects the block (`id * 98`). A figure's meshes get consecutive ids (mesh j of figure f = base_f + j).
- **Formations [L].** On 202 of the 224 units drawn, the figures counted from the draws equal the database's
  `ArtDefine_UnitInfoMemberInfos` sum; the 22 others are explained in section 7. A 12-figure formation spreads up to
  20-30 world units from the plot centre (the hex's corner is 36.9 away), mean offset within 2.6 units of the centre;
  most figures face 20-40 degrees (854 of 1,094; units placed with the default facing); land figures stand at z 10.6-29.5
  (median 12.3), ships at z about 10.
- **Cities [C, L].** Every block is an instance of a 64-byte per-instance world matrix in vertex slot 1; the colour
  pass and the shadow-mask pass each draw 523 block instances in every city frame, and a city's block count depends on
  its culture and size, not its era (section 8). No building program binds t27 or a team-colour input.

## 1. The three passes and their targets [C, `units_land_L01` frame 600]

| Pass | Draws (L01) | VS / PS (hash prefix, EXE VA) | Target | Depth | Blend, cull | Read by |
|---|---|---|---|---|---|---|
| 1 shadow mask | 16-64 (49) | `8cb52b96` 0x6804E48 / `72c27aea` 0x6815328 | res 13383, 2554 x 1411 R16G16B16A16_FLOAT | none | off, front | ShadowShader pass 0 (draw 72, t0 `ShadowMap`) -> res 13386, 2048 x 2048 R16_UNORM -> TerrainShader pass 0 t3 (draws 155-173 ...) |
| 0 reflection | 73-121 (49) | `29a0b061` 0x6803140 / `60d270e1` 0x6813630 | res 13415, 1024 x 1024 R8G8B8A8 (the export's `water_t4`) | res 832, D24_UNORM_S8_UINT | src-alpha / inv-src-alpha, front | WaterShader passes 3 and 4, t4 (draws 267-271) |
| 0 scene | 455-503 (49) | `29a0b061` / `60d270e1` | res 2419 (the scene's colour target; the export holds no description of it) | res 2429 | src-alpha / inv-src-alpha, back | (the frame) |

- The pass-1 PS writes (1, 1, 1, alpha) and nothing else: a coverage mask. Its view-projection (cb1[4..7]) has
  (0, 0, 0, 1) as its w column, an orthographic projection, as has the reflection pass's; the scene pass's is a
  perspective one [C, the cb1 values of L01 draws 16, 73, 455]. City blocks and other buildings draw into the same
  mask target 13383 with their own pass 1 (section 8). So unit and building shadows on the terrain go through one
  mask [I for "shadow": by the ShadowShader and TerrainShader slot names].
- **The two orthographic matrices are the EXE's ShadowMatrix0 and ShadowMatrix1 (added 2026-11-16 by 4455, epic 0375,
  [C] on the 24 exports with units, read in place; `F:/c5-64bit-work/0375/4454/r2/r2check.py`):** the mask pass's cb1
  r4-r7 are byte-equal to the same frame's WaterFrame 0x1A0 (`ShadowMatrix0`, 0xDA1790 + 0x40v) and the reflection
  pass's to WaterFrame 0x160 (`ShadowMatrix1`, 0xDA1890) in 24 of 24 exports; cb1 r9 / r10 are equal in the three
  passes of every export. Same x / y extents (proportional to the camera's height) and depth scale (1.66667e-5) in both;
  ShadowMatrix0 looks along (0.4330, -0.25, 0.8660), ShadowMatrix1 along (0.1228, 0.6964, 0.7071) at the catalogue's one
  camera pitch. Consequence for the server: `unit-server-scene.md` 7 (R2).
- Every one of the 26 exports draws the same instance set in all three passes (S03 frame 633: the scene pass has 2
  more mesh-instances, 19 against 17) [L, `frames.csv`]. There is no per-pass culling of units in these frames.
- The pass-0 PS binds t0 `BaseTextureMap` (BC2/BC3/BC1, 128-512 px), t1 `SREFMap`, t2 irradiance cube (32 x 32
  R8G8B8A8), t3 dull and t4 sharp environment cubes (32 x 32 BC1), t5 distance-fog ramp (128 x 8), and t27 (the same
  palette buffer as the VS). It binds no fog-of-war mask, no overlay and no shadow map: units are not darkened by the
  fog of war in the shader, and do not receive shadows [C, the bindings; slot names `slot_names.txt` 90].
- **Correction (2026-11-15) to 3901's section 8:** "VS 0x6804E48 / PS 0x6815328 into 2554 x 1411 R16G16B16A16_FLOAT
  (section 5's pass A)" and "pass 0 into 1024 x 1024 R8G8B8A8 (format 27)" are this file's shadow-mask and reflection
  passes; the 1024 x 1024 target is the water's reflection (t4), not a second scene target.

## 2. The vertex input and the vertex program [C]

- Slot 0, stride 28 B, per vertex: POSITION R16G16B16A16_FLOAT at 0, BLENDWEIGHT R8G8B8A8_UNORM at 8, BLENDINDICES
  R8G8B8A8_UINT at 12, NORMAL R16G16B16A16_FLOAT at 16, TEXCOORD R16G16_FLOAT at 24. Slot 1, stride 4, per instance
  (step rate 1): BLENDINDICES1 R32_UINT. No tangent. Mesh vertex and index buffers are IMMUTABLE, 16-bit indices.
- **Positions are pre-scaled [C].** L01 draw 464 (2,022 indices, 863 vertices: `warrior.gr2`'s one mesh) spans
  (-13.34, -1.75, 0.00) .. (7.46, 1.87, 16.50); the `.gr2`'s box (-95.3, -12.5, 0.0) .. (53.3, 13.4, 117.9)
  (`landmarks-and-models.md` 4.7) times the warrior member's `Scale` 0.14 is (-13.34, -1.75, 0) .. (7.46, 1.88, 16.5).
  The bone matrices' 3 x 3 columns have norm 1.000 (median over 1,094 figures; maximum 2.23 on a few meshes). So the
  member scale is applied once at load, into the half-float positions [I for "at load"]. **Confirmed and made exact
  2026-11-16 (ticket 0379, `unit-renderer.md` 10.9) [C static pass, L 515 / 515 meshes]:** at model load the EXE runs
  Granny's basis conversion with desired units per metre = the file's UnitsPerMeter x Scale (desired axes right
  (1,0,0), up (0,0,1), back (0,-1,0)), so positions are multiplied by fl(fl(UnitsPerMeter x Scale) / UnitsPerMeter),
  one ulp off Scale for some Scales, and normals by the cofactor inverse of that matrix; Granny's copy writes the 28-byte
  layout (meshes with two or more bone bindings) or the 20-byte one (fewer), its float-to-half rounds ties away from
  zero, and a float renormaliser rewrites every skinned vertex's weights (floor(fl(fl(255 / s) x w) + 0.5), fourth 0).
- The VS blends up to three bones: weight x always, y if > 0, z if > 0 (the fourth weight is not read); bone k's rows
  are t27 elements `id*98 + 2 + 3k`, `+3`, `+4`; the normal uses the same 3 x 3 rows. Outputs: clip position, the
  normal with u, the eye vector (world - cb1[3]) with v, the world position, the clip position's x, y, w (for the fog
  ramp lookup), and the instance id (to the PS). There is no world matrix: the palette is in world space.

## 3. cb1 (192 B, dynamic) [C, values from L01]

| Register | Read by | Meaning | L01 value (scene pass, draw 455) |
|---|---|---|---|
| 0-2 | neither program | camera rotation rows [I] | (1, 0, 0), (0, 0.707, 0.707), (0, -0.707, 0.707) |
| 3 | VS | camera position (eye vector) | (895.95, -849.6, 473.7, 1) |
| 4-7 | VS | view-projection rows, applied as `x*c4 + y*c5 + z*c6 + c7` | per pass (section 1) |
| 8.x | PS | height threshold: world z below it gives alpha x 0.33 [I: the water level; ships stand at z about 10] | 10.117 |
| 9 | PS | x, y distance-fog factors (exp2 with 1.442695 = log2 e); w = the fog ramp texture's row | (-0.05, -5.17e-14, 0, 0.42105) |
| 10 | PS | fog colour | (0.5725, 0.7137, 0.9529, 1) |
| 11 | PS | camera position (fog distance) | (895.95, -849.6, 473.7, ~0) |

cb1[8].yzw and cb1[11].w differ between passes and are not read. The same cb1 is bound to VS and PS; its contents
change per pass, not per draw (three hashes in L01: `c00c59ea` pass 1, `fe225339` reflection, `636997f7` scene).

## 4. The bone palette, block by block [C]

- VS and PS t27: an SRV R32G32B32A32_FLOAT over 16,384 elements of a 262,144-byte `D3D11_USAGE_DYNAMIC` buffer
  (resource 858; a second one, 860, follows when 858 is full), written once per frame by `Map(WRITE_DISCARD)`/`Unmap`.
  The export holds it as written (the same SHA-256 in every draw of a frame), with no replay.
- Block of instance id i = elements `98 i .. 98 i + 97`: 167 blocks per buffer. In L11 (frame 2705) 182 mesh-instances
  were drawn: ids 0-166 in buffer 858, then ids 0-14 in buffer 860 [L]. In city and sea frames 860 holds the
  BuildingShader_Skinned_CODE parts (S01 draws 43, 75, 267) [L].
- Row 0: team colour (section 5). Row 1: x = alpha at start, y = alpha at end, z = the fade between them (the PS's
  alpha is `x + z (y - x)`, then x 0.33 below cb1[8].x); w is not read by the material 90 or 89 programs [C]. Rows
  2 + 3k .. 4 + 3k: bone k, k = 0..31 (`SkinBoneCount` 32).
- **Stale rows [L].** Only the bones a mesh's vertices weight are written meaningfully; the others carry values left
  in the mapped memory (translations of 0 and of other figures, and 1e34 in L08's block 7). A reader must take the
  bone set from the mesh (the vertex buffer's BLENDINDICES over the draw's index range), not from the non-zero rows.
- **Where a figure stands [L].** The translations of a figure's weighted bones scatter around its root: their median
  is the figure's position within a few units for most meshes; for gun crews (`ThreeBigGuns`) parts sit up to 56 units
  from the body's median. The root transform itself is not in the palette (a skinning matrix is world x bone x
  inverse bind); an exact root needs the `.gr2`'s inverse bind matrices [I].
  **Corrected 2026-11-15 03:34 (after epic 0327, queue #1462, `unit-playback.md` 1.1-1.3 on branch
  `epic-0327-units-groundwork`, task 4151) [L]:** the figure's placement is recoverable from the palette with the
  model's `.gr2` and its clips. Bone k's rows are `Rz(yaw) [R_k | s t_k] + T`, where `[R_k | t_k]` = World x
  `InverseWorldTransform` of bone k in the file's units at one clip time t, and s = the member's `Scale` exactly (0.140000
  warrior, 0.120000 horseman). yaw is 30.000 degrees on all 100 idle blocks fitted (warrior and horseman), and T is the
  figure's world position; rider and horse of one figure share T within 0.01 units. 99 of 100 blocks fit one clip at
  one t to float precision. The bone median used here stays an approximation of T: it is good for most meshes and off
  by 13-36 units for gun crews and some cavalry models (`unit-formations.md` 6 on that branch). Checked against this
  file's records: the 30-degree heading and the matrices' unit column norms (section 2) agree. (Replaced: "an exact
  root needs the `.gr2`'s inverse bind matrices [I]".)
  **Corrected 2026-11-15 04:56 (epic 0327 merged on `main` 7f25199a, queue #1497; `unit-formations.md` 12.2 and
  12.5, task 4153) [L]:** the relation `[Rz(yaw) R_k | s Rz(yaw) t_k + T]` gives every figure's exact position T, and
  T is the model's origin. It was fitted to 1,520 of 1,586 main-pass mesh-instances of the 20 unit frames, with median
  errors of 0.0003 degrees and 0.0001 units; s equals the member's `Scale` (median relative deviation 4e-9) and yaw is
  30.0001 degrees. The bone median used in this file lies 0.69 units from T in the median, p95 15 and at most 56
  units. The large offsets are gun crews (36-56 units for the Parrott crews, 22-27 for the cannons) and the horse
  meshes of several cavalry models (10.7-13 units). So "13-36 units" above is "up to 56", and this file's positions
  (sections 7 and `units.csv`) are bone medians, not figure positions: for exact positions use 0327's
  `models/tools/formations/exact_t.py`. Checked against this file's records: the Field Gun's largest part spread is
  56.2 units (`units.csv`), and its crews are the parts that fell outside the 64-unit plot radius (section 7).
  (Replaced: "off by 13-36 units for gun crews and some cavalry models", and "the figure's position within a few units
  for most meshes" now reads with p95 15.)

## 5. Team colour [C database, L 6 owners]

| Owner (player) | Header row 0 (rgb) | `PlayerColors` secondary | distance | primary |
|---|---|---|---|---|
| Polynesia (0) | 0.851, 0.3451, 0.0 | 0.851, 0.349, 0.0 | 0.004 | 1.0, 1.0, 0.294 |
| Carthage (1) | 0.8, 0.8, 0.8 | 0.804, 0.804, 0.804 | 0.007 | 0.318, 0.0, 0.537 |
| Greece (2) | 1.0, 1.0, 1.0 | 1.0, 1.0, 1.0 | 0.000 | 0.255, 0.553, 0.996 |
| Portugal (3) | 1.0, 1.0, 1.0 | 1.0, 1.0, 1.0 | 0.000 | 0.016, 0.078, 0.49 |
| Msoura (22, city-state) | 0.8314, 0.6863, 0.2157 | 0.831, 0.686, 0.216 | 0.000 | 0.0, 0.0, 0.0 |
| Ragusa (24, city-state) | 0.0, 0.6353, 0.7098 | 0.0, 0.639, 0.71 | 0.004 | 0.0, 0.0, 0.0 |

(`Civilizations`/`MinorCivilizations.DefaultPlayerColor` -> `PlayerColors` -> `Colors`; the row-0 values are 8-bit
fractions, e.g. 88/255 = 0.3451.) *(2026-11-16, issue 4453 [L], 5.1: the player's colour (`GetPlayerColor`), which is
the default only when the setup leaves it; and the fraction is k x float32(1/255), a float32 product, not k / 255.)* Every figure of one owner carries the same row 0. The PS: `tint = lerp(c^2, 1,
a^2)` with c = row 0 and a = the base texture's alpha, then `albedo = base^2 * tint` (the program squares its texture
reads: gamma 2 lighting, square root at the end). The alpha variant (material 89, S04 draws 82 and 351) reads row 1
only, no team colour [C]. **Corrects** 3901's "header row 0 = (0.851, 0.345, 0, 1) on all 18 [I: team colour]": it is
team colour, the owner's secondary colour [C]; "header row 1 ... meaning not known": section 4.

### 5.1 Which colour set: the player's, not the civilization's default (C1-C3, issue 4459, 2026-11-15)

Reads of the DLL fork at the x64 pin 04b1211b8 (`CvGameCoreDLL_Expansion2/`, line numbers at that commit) and of the
proxy's call logs; no game run. The six owners of the table above all had their civilization's default colour, so the
table cannot tell the two sources apart.

- **C1, the EXE takes row 0 from the player's colour** [C for the DLL's values, L that the EXE never asks the
  civilization's default; I that row 0 comes from `GetPlayerColor` rather than from the EXE's own database read].
  - The player's colour lives in the pregame: `CvPlayer::getPlayerColor` returns `CvPreGame::playerColor(id)`
    (`CvPlayer.cpp` 34776, `CvPreGame.cpp` 1670), the array `s_playerColors` (`CvPreGame.cpp` 236), saved and loaded
    with the pregame (1852-1855, 3640). `ICvPlayer1::GetPlayerColor` (`CvDllPlayer.cpp` 248) and
    `ICvPreGame1::playerColor` (`CvDllPreGame.cpp` 475) both return that value.
  - At game start `CvGame::InitPlayers` (`CvGame.cpp` 634-860) sets it: for a major civilization (players below `MAX_MAJOR_CIVS`, 22,
    or 43 in the m43 build) the pregame colour if the setup set one, else the civilization's `DefaultPlayerColor`
    (737-744); a colour equal to the barbarians' or **already taken by an earlier major civilization** is dropped
    (746-765) and the player gets **the lowest `PlayerColors` ID not yet used** by a major (782-796). So a second player of the same
    civilization gets the first free colour row, not its civilization's default; a colour chosen at setup (the
    pregame value, `ICvPreGame1::setPlayerColor`, `CvDllPreGame.cpp` 890) is kept unless taken or the barbarians'.
    A scenario map sets it from the map's team colour by name (`CvWorldBuilderMapLoader.cpp` 325-336, 410-419).
  - What the EXE reads (proxy call logs, 2026-11-10, `server/evidence/2500/P1/proxy-calls.tsv`: a Tiny save loaded
    through the proxy and 10 AI turns): `ICvPlayer1::GetPlayerColor` 241 calls, `ICvGameContext1::GetPlayerColorInfo`
    241, `ICvPlayerColorInfo1::GetColorTypeSecondary` 239, `ICvGameContext1::GetColorInfo` 471, `ICvPreGame1::playerColor`
    3,989, and **`ICvCivilizationInfo1::GetDefaultPlayerColor` 0** and `ICvMinorCivInfo1::GetDefaultPlayerColor` 0
    (other civilization getters are logged in the same file, so the getter would have shown). The chain player colour
    -> `PlayerColors` row -> secondary `Colors` row is what section 5 found as row 0. Both getters are mirrored on the
    proxy route (`proxy/model/policy-table.tsv`: `GetPlayerColor` mirror:players, `playerColor` mirror:pregame).
  - Open [I]: whether the EXE reads `Civilizations.DefaultPlayerColor` through its own database connection (not a DLL
    call, so not in the log). The live check of epic 0375's DoD D settles it (below).
- **C2, city-states and the barbarians** [C]: `InitPlayers` gives every city-state slot its `MinorCivilizations`
  row's `DefaultPlayerColor` (`CvGame.cpp` 853, also for closed slots that may become free city-states later, 833),
  and the barbarians the barbarian civilization's `DefaultPlayerColor` (638, 775); the pregame value is overwritten
  for both. So for them "the player's colour" and "the default" are the same row, and the same rule (the player's
  colour, secondary colour as row 0) covers them; 3909's two city-states and 4353's barbarian (row 0 = (0, 0, 0),
  `unit-renderer.md` 10.3) agree [L].
- **C3, can it change in a running game** [C]: only through `CvGame::addPlayer` (`CvGame.cpp` 11249-11303: a new
  player gets its civilization's default, or the first colour row no player 0-62 uses) and
  `CvGame::changeMinorPlayer` (11306-11313: the new city-state type's default), both reached only from Lua
  (`Game.AddPlayer`, `Game.ChangeMinorPlayer`, `Lua/CvLuaGame.cpp` 1856-1865), and the engine's
  `ICvPreGame1::setPlayerColor`. No Lua, XML or SQL file of the fork at the pin calls any of them (`git grep` for
  `ChangeMinorPlayer`, `AddPlayer`, `SetPlayerColor`: no hit). A team change, a liberated civilization, a vassal or a
  revolution keeps the player and so the colour. **No `Gameplay*` call announces a colour change** (none of the engine
  interface's colour members is a DLL-to-engine notification). So in VP as shipped the colour is fixed after game
  start; a mod that calls those Lua functions would change it silently, which the server sees only by reading the
  player record again (the brief's decision 2: re-read at every check, flag a change).
- **Live check for C1 (epic 0375 DoD D, 4453):** a Tiny game set up with (a) one major civilization twice (the second
  gets the first unused colour row, by the rule above) and (b) a colour set at setup through the front end's
  `PreGame.SetPlayerColor` for one slot; capture each owner's unit draw and compare row 0 with the
  `SecondaryColor` of `GetPlayerColor`'s row and with the civilization's `DefaultPlayerColor` row. Under C1 every
  row 0 equals the player's row (bit-equal after `trunc(float32(v) x 255) / 255`); under the alternative the
  duplicate and the set colour show their civilization's default.
- **Result of the live check (issue 4453, 2026-11-16 00:55-01:05 EST) [L]: C1 and C2 hold; the rounding is a
  multiplication by float32(1/255), not a division.** Pool instance c5h-c, stock path (32-bit pin `i0ship-26234371-ia`,
  CP + VP + EUI, Vibrant Textures present), a new Tiny Continents game started by the harness's front end with the
  setup keys of the instance's patched `MainMenu.lua` (civilizations by `PreGame.SetCivilization` and
  `SetLeaderType`, the colour by `PreGame.SetPlayerColor`): player 0 Rome (default colour), players 1 and 2 both
  Greece, player 3 Egypt with `PLAYERCOLOR_DARK_PINK` set at setup, one city-state (22, Veligrad), the barbarians.
  The game gave (Lua `GetPlayerColor`): 0 `PLAYERCOLOR_ROME`, 1 `PLAYERCOLOR_GREECE`, **2 `PLAYERCOLOR_BLACK`** (ID 0,
  the lowest row: C1's "first unused colour row" [L]), **3 `PLAYERCOLOR_DARK_PINK`** (the setup's, not Egypt's
  default), 22 `PLAYERCOLOR_MINOR_VELIGRAD` (its `MinorCivilizations` default, not `CIVILIZATION_MINOR`'s
  `PLAYERCOLOR_MINOR_WHITE`), 63 `PLAYERCOLOR_BARBARIAN`. One unit of a distinct type per owner placed next to the
  human's start and made visible, popups and leader screens closed (D114), one RenderDoc capture
  (`F:/c5-64bit-work/0375/4453/captures/c5h-c_20261116-005505_frame11661.rdc`, kept), exported with `cli.py
  rdc-export --select all`; `server/evidence/0375/4453/colour_rows.py` read header row 0 of every instance of every
  skinned unit draw (VS `29a0b061`, the t27 palette, the vb slot 1 ids): **228 instances, 6 distinct row-0 values,
  each equal bit for bit to exactly one owner's `SecondaryColor` row, all six owners matched** (table:
  `server/tests/units/colour_cases.tsv`, Types and counts only, D83) with the rule
  **`trunc(float32(v) x 255) x float32(1/255)`** (float32 product), alpha 1. The rule as written so far,
  `trunc(float32(v) x 255) / 255` as a correctly rounded float32 division, misses by one ulp for 3 of the 6 owners
  (Rome's blue channel 118, `PLAYERCOLOR_BLACK`'s 229, Veligrad's 57: `server/evidence/0375/4453/colour_rule_div.txt`);
  the two agree for 0, 43, 128 and 255, which is why 0041's six owners and 3909's rows did not show it.
  *(Fixed 2026-11-16 by 4453 on the MC's grant: `render/src/units/art.cpp` `ArtResolver::team_colour` now multiplies by
  `1.0f / 255.0f` (it divided by 255.0f); `render/tests/units/test_art.cpp` `team_colour_reciprocal_rule` pins it with
  synthetic colours of the live bytes 57, 118, 229 / 70, 128, 52 (bit-exact, and unequal to the division where the two
  differ), and the old division constants of `test_art.cpp` / `test_draw.cpp` (217, 88, 212, 175, 55) were changed to
  the product. 0041's unit suites (`python -I render/build.py --test -R c5r_units_`) 9 of 9 before and after.
  The catalogue-placement suite's default height field (bundle `C5H-0301-TINY_0009_20261115-070309_workstation`) is now
  under `F:/c5-64bit-work/scene-library/retired/`: without `C5R_UNITS_HEIGHTFIELD` pointing there the suite skips,
  which `build.py` counts as a failure. `unit-renderer.md` 10.3 (0374's) is corrected by lead-0374 on the MC's request.)*
  C3 not tested live (no colour change in a running game is reachable in VP, above).

## 6. Instancing and the id buffer [C, L]

- One draw per mesh of a model per unit: L01 draws 24-26 are the warrior's three member models (1,962 x 3, 2,022 x 6,
  1,914 x 3 instances; 12 figures), draws 18-20 one model of three meshes with ids (99, 100, 101), (102, 103, 104),
  (105, 106, 107). The order of the ids within a draw is not sorted (draw 61: 22, 24, 18, 26, 20, 28).
- The id buffer: resource 857, 8,192 B, DYNAMIC, slot 1 with a per-draw byte offset (L01 draw 74: offset 496 = ids from
  element 124); `StartInstanceLocation` is 0 in every unit draw.
- Ids run from 0 per frame and per palette buffer, a figure's meshes consecutive. Rule used to count figures (and
  checked by the 202 matches of section 7): two draws are meshes of one model when their sorted ids differ by exactly
  one and each id pair stands within 64 units; a model's figure count is its draws' `InstanceCount`.
- Whether two units of one art row share a draw is not visible in the catalogue (each frame has each row once) [not
  verified].

## 7. Formation placement [L, 224 drawn units of 229 placed]

Plot centres on this map: x = 64 px + 32 (py odd) + 32 - 32 W, y = 0.866 x 64 (py + 2) - 0.866 x 32 (H + 4) (W 56, H 36;
3901's relation from `heightfield.h`). Each figure is assigned to the nearest placed plot within 64 units; the 12-33
other figures per unit frame (18 in the city and P4 frames) are the save's own units elsewhere on the map.

| Formation (`ArtDefine_UnitInfos.Formation`) | Units | Figures | Largest offset from the plot centre | Mean offset |
|---|---|---|---|---|
| Phalanx, DefaultMelee, Pikeman, Warrior, Samurai, OffsetPikeman | 18 | 12 | 20.3-30.0 | 0.1-1.3 |
| Archer, Barbarian | 16 | 10 or 12 | 18.6-25.7 | 0.3-2.4 |
| HonorableGunpowder | 11 | 14 | 25.3-26.3 | 0.1-2.6 |
| DefaultCavalry | 37 | 1-15 | 0.5-34.2 | 0.1-14.2 |
| ThreeBigGuns, TwoBigGuns | 18 | 1-3 | 1.7-59.1 | 1.1-46.7 (parts of crews, section 4) |
| Vehicle | 6 | 1-3 | 9.4-20.2 | 0.7-9.4 |
| None, empty (ships, single figures) | 58 | 1 | 0.4-36.3 | 0.4-36.3 |
| others (UnFormed, Scout, ChariotElephant, LooseCivilian, EarlyGreat*, FighterWing, BomberWing, ThreeInLine, TheeInLineStag) | 60 | 1-14 | 0.3-27.4 | 0.1-23.5 |

- The 12-figure grid is the same for every unit of a formation, rotated by the facing: the Roman legion (L13 frame 624,
  plot (38,11)) and the swordsman (L09 frame 1678, (42,11)) have offsets (10,-8), (0,-12), (19,1), (-10,-16), (5,3),
  (-5,-2), (13,10), (-15,-6), (0,13), (-11,6), (7,19), (-21,3) within 1-5 units of each other (the swordsman's grid slightly wider; `units.csv`).
  **Corrected 2026-11-15 03:34 (epic 0327, queue #1462, `unit-formations.md` 9 on branch `epic-0327-units-groundwork`,
  task 4153) [C, L]:** the two units are different formations: the legion is Phalanx (`UnitFormations.xml` spacing
  11), the swordsman DefaultMelee (spacing 13). That is why the swordsman's grid is wider: about 1.2 times by this
  file's offsets (e.g. (22.85, 0.29) against (18.55, 0.68)), close to 13/11 = 1.18. The grid is the formation's slot
  rule (rows from the centre out, per-unit seeded jitter, centred, rotated by the facing), reproduced to a median of
  0.00-0.40 units with the seed fitted. (Replaced: "The 12-figure grid is the same for every unit of a formation ...
  the Roman legion ... and the swordsman", which compared two formations as one.)
- Facing: the body mesh's x axis lies at 20-40 degrees in 854 of 1,094 figures and at 10-20 degrees in 117 more (all
  units placed with facing -1); 63 lie at -155 to -140 degrees, 180 degrees from the main group [I: meshes modelled
  the other way round].
- Height: land figures z 10.6-29.5 (median 12.3; the studio is flat land with hills at the edges), ships 0.5-12.1
  (median 10.0), next to cb1[8].x = 10.117.
- **Figures against the database.** 202 of the 224 drawn units have as many figures as the members of
  `ArtDefine_UnitInfoMemberInfos`. The 22 others: (a) 19 units whose members include single figures of different models
  (great people, missionary, inquisitor, caravans, colonist, settlers, pioneer, archaeologist, Bandeirante, explorer):
  such draws (one instance each) chain into one "model" under the id rule, so the count reads 1-4 where the database
  says 3-9 - their mesh-instances do match the members' meshes [I]; (b) Mamluk (L06):
  15 = 7 + 8 draws of two meshes that do not pair (8 members); (c) Field Gun (L03): 2 figures of 4 meshes for 3 members;
  (d) Huns battering ram (L11) 2 for 1. (b)-(d) are not explained.
  **Corrected 2026-11-15 03:34 (epic 0327, queue #1462, `unit-formations.md` 6 on branch `epic-0327-units-groundwork`,
  task 4153) [L]:** (b) the Mamluk's 15 are 8 riders (mesh draw 694, 8 instances) and 7 horses (draw 693, 7
  instances). The riders fit the 8-slot grid and the horses slots 0-6, so the last member's horse is not drawn in that
  frame (why is not known). (d) The battering ram is one figure of six meshes (draws 549 and 550-554) that the id rule
  above split in two; this file's records give the two parts 0.1 units apart ((-0.33, -1.56) and (-0.35, -1.66)).
  (c) the Field Gun stays unexplained. Checked against `units.csv` / `figures.json`: the instance counts 7 and 8 and
  the two ram positions are this file's own. (Replaced: "(b)-(d) are not explained".)
  **Corrected 2026-11-15 04:56 (epic 0327 merged on `main` 7f25199a, queue #1497, `unit-formations.md` 12.5, task
  4153) [L]:** (c) the Field Gun is explained. It has 3 figures of `union_artillery_parrott.gr2` (a cannon and three
  crew meshes): 12 mesh-instances, ids 114-125 in four draws of 3. With the exact T of section 4 they match the
  ThreeBigGuns rule with seed 29. The "2 figures, 8 mesh-instances" counted here came from bone medians: crew meshes
  whose medians lie 36-56 units from the model origin fell outside the 64-unit plot radius, which broke the id chain
  of one figure. Checked against this file's records: L03 draws 608-611 (2,718, 3,030, 2,601 and 3,300 indices) each
  hold 3 instances, ids 114-125. (b) Mamluk: 0327 confirms with exact positions that the 8th member's horse is drawn
  in none of the three passes (rider id 71 alone). Counted this way (the ram as 1 figure, the Field Gun as 3, the Mamluk as its 8
  riders), 205 of the 224 drawn units have the database's member count. Case (a) (19 single-figure mixtures, a
  counting artefact of the id rule) and the Mamluk's missing horse remain.
  (Replaced: "(c) the Field Gun stays unexplained".)
- **Not drawn [L].** Marine, Mech, Mechanized Infantry and Merc (L06 frame 3084) and the Modern Cargo Ship (S04 frame
  1141) have no unit draw on their plot (no skinned draw; L06 has one `UnitShader_CODE` draw), although 3907 placed
  them `ok`. Not explained [I: their art not yet loaded when the frame was captured].

## 8. Cities in the same terms [C, L; the four city frames]

| Family | VS / PS | Target | Draws / block instances per frame (Ancient) |
|---|---|---|---|
| city blocks, shadow mask | `88c2b7d4` 0x61A0320 (material 4/5 pass 1) / `73daa800` 0x61A9C98 (5, pass 1) | 13383 (the units' mask target) | 133 / 523 |
| city blocks, colour | `7ec4e9a5` 0x619F1E8 (4/5 pass 0) / `c704c80b` 0x61A7D00 (5, pass 0) | 2419, depth 2429 | 133 / 523 |
| single buildings | `7ec4e9a5` / `f0e1f0a6` 0x61A56E8 (3/4/6, pass 0); mask `88c2b7d4` / `e12e9eaf` 0x61A76F8 | 2419; 13383 | 34; 33 (one instance each) |
| animated parts | `7a9e17f6` 0x61A0CB0 / `f0e1f0a6` (6, pass 0); `34b98e63` 0x61A2BB8 (6, pass 1) | 2419, 13415; 13383 | 4 + 2 + 2 skinned, palette buffer 860 |

- **Per-instance world matrix [C].** The instanced VS takes four float4 rows from vertex slot 1 (stride 64) and
  applies them as `x r0 + y r1 + z r2 + r3`, then cb1[0..3] as view-projection; it also projects the world position
  through cb1[8..11] and cb1[12] into two texture coordinate pairs (the fog-of-war and screen lookups [I]). Matrix
  translations put every block within 64 units of a placed city except 59 per frame (the save's own cities); the
  matrices' scale is 1.0 (a few 0.038).
- **Blocks per city [L]**, the same in all four eras (Ancient frame 756, Renaissance 1322, Industrial 691, Modern 1271):
  African pop 1 / 20: 33 / 98; European 25 / 98; Mediterranean 34 / 83; Polynesian 32 / 95. (3907's index names the
  Polynesian cities `CITY_ASIAN_*`: by 0319 they draw the `Poly_*` set.) The colour pass's largest instance count per
  draw is 15-26.
- The block PS reads cb1[16..21] and binds t0-t8 (base, SREF, three cubes, fog ramp, a 3D colour correction, the
  fog-of-war mask and base layer); no t27, so no team colour on buildings [C].
- With the cities, the save's own units (18 mesh-instances in 4 draws per pass, in every city and P4 frame) are drawn
  as in the unit frames.

## 9. What a server-side unit renderer needs from this

1. Per frame: one palette of 98-row blocks per mesh-instance (team colour, alpha triple, up to 32 skinning matrices
   in world space), the per-instance id stream, and cb1 per pass. The EXE's own programs can be driven with exactly
   these inputs (D13): VS 0x6803140 / PS 0x6813630 for colour and reflection, 0x6804E48 / 0x6815328 for the mask.
2. Mesh data: the `.gr2` vertices scaled by the member's `Scale` and packed to the 28-byte layout (half positions and
   normals, unorm8 weights, uint8 indices, half uv). (Exact form 2026-11-16, ticket 0379: the scale is Granny's basis
   conversion factor, the normals take its cofactor inverse and the weights a float renormaliser; section 2 and
   `unit-renderer.md` 10.9.)
3. Placement: the formation grid per `Formation` row, rotated by the unit's facing, at the plot centre, on the terrain
   height; the poses (epic 0041's sampler, `model-reader.md` 6) give the skinning matrices.
4. Team colour from `PlayerColors.SecondaryColor` of the owner's colour set.
5. Shadows: the mask pass into an R16G16B16A16 target that the ShadowShader reduces for the terrain; reflections: the
   same draw into the water's t4 with the reflection pass's cb1.

**Added 2026-11-15 15:00 (epic 0041, task 4352, `unit-renderer.md` 10) [L]:** all five are built by the renderer's unit
module and its own draws equal the captured ones bit for bit in all three passes (tank, anti-aircraft gun, trireme,
warrior, a material 87 unit; the warrior's two weight-tie vertices aside; the tie rule found 2026-11-16 by ticket 0379,
`unit-renderer.md` 10.9). What this list lacked: (6) material 87
(`UnitShader_CODE`, not skinned) draws one `DrawIndexed` per figure with a per-draw 256-byte cb0 (rows 0-2 the world
matrix, 3 team colour, 4 the fade triple) and a 20-byte stream (half position x Scale, half normal / Scale, half uv),
no palette block and no id; (7) PS t1 is the member's SREF texture, t2-t5 are install files (`unit_irradiance.dds`,
`unit_environment_dull.dds`, `unit_environment_sharp.dds`, `fogramp.dds`; the two A8R8G8B8 files uploaded as
R8G8B8A8), nothing made per frame; (8) the id stream holds one region per pass (mask, reflection, scene); (9) VP's
FBX-exported models take the basis change (x, y, z) -> (x, -z, y) on their vertices; (10) team colour row 0 =
trunc(float32(v) x 255) / 255. Item 3's placement and poses are the module's L3 and L2 (`unit-renderer.md` 11, 12).

## 10. Not verified

- The meaning of header row 1's w; why it varies (stale memory or a value the EXE writes for another program).
  (Row 1's x, y, z: answered 2026-11-15 by task 4352 on epic 0041's fresh capture, `unit-renderer.md` 10.3 [L]: the
  visibility fade (from, to, progress); a unit just come into view carries (0, 1, 1); two owners, embarking and damage
  leave rows 0-1 as for a unit at rest except the owner's colour, damage removes figures.)
- **Corrected 2026-11-15 (task 4352):** "Marine, Mech, Mechanized Infantry and Merc ... have no unit draw" (section 7):
  the Mech's material 87 gun barrel (`Mech_GunBarrel`, 45 vertices) is drawn in L06 (draws 39 / 167 / 683, all three
  passes); its skinned body and the other four units are not. (Replaced: "no skinned draw; L06 has one
  `UnitShader_CODE` draw" left the 87 draw unattributed.)
- The orthographic mask pass's light direction, and the ShadowShader's reduction (not opened here).
- The root transform of a figure (needs the inverse bind matrices); the 145-150 degree headings.
- The five units without draws (section 7) and cases (b)-(d) of the figure counts.
- Whether two units of the same art row are drawn in one draw.
- cb1[8].x as the water level (it equals no recorded water height here).
