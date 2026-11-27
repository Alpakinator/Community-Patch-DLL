# The move-order path's models drawn by the server: c5r::pathmodels (ticket 0300, part 1)

Written 2026-11-16 (19:25-) by the queue implementer of ticket 0300 (account B, branch `task-0300-path-models`), part 1
of the ticket: the offline module and its tests (increment 3 groundwork, D143). Part 2 (the server wiring, the feed of
the path nodes, MovementPath back on the D33 key list, the live comparison) waits for epic 0431's merge.

Sources: the EXE side is 3802's `research/02-exe-internals/arrows-and-movement-path.md` section 2; stock's capture of
the path is 3801's `arrows-and-path-line.md` section 4 (the follow-up note is its section 9). New here: the capture's 54
draws read draw by draw, matched to the install's files and to the module's own output. Marks: [C] confirmed by the
capture's records or the files, [L] verified by running the module, [I] inferred. EXE behaviour is described, never
quoted (D83); nothing of the game is committed: the module reads meshes, textures and the two programs from the
player's install and EXE at run time, and the committed reference holds only digests, counts, states and world numbers.

**Capture used:** 3801's stock capture `c5h-c_20261114-193435_frame6496.rdc` (2026-11-14 19:34, c5h-c, 32-bit pin
`i0ship-26234371-ia`, stock config, Tiny save copy `C5H-0179-TINY_0009V` turn 9, VT present; archived in
`D:/c5-64bit-work/0179/3801/captures/`), copied to `C:/c5-64bit-work/0300/`, converted again with `cli.py rdc-summary`
(RenderDoc 1.47's zip.xml, no replay) and draws 775-828 exported with 3601's `xml_export.walk`
(`render/evidence/0300/export_0300.py`). `render/evidence/0300/fixture_0300.py` writes the committed reference
`render/tests/pathmodels/f6496_reference.h`. Install for the module's runs: `%USERPROFILE%\Desktop\pure_vp\Sid Meier's
Civilization V` (render/build.py's default copy), EXE `%USERPROFILE%\tools\ghidra-projects\bin\CivilizationV_DX11.exe`
(SHA-256 b2ce96ae..., build 4390913).

## 1. Summary

- The path's 18 assets are each **two rigid meshes** (a disc or chevron body and its frame); every draw is one mesh
  with UIShader, three textures and a world matrix [C].
- The EXE's vertex buffers are the `.gr2` vertices **byte for byte** as (Position, Normal, the first two
  TextureCoordinates0 floats), stride 32, and the index buffers the `.gr2` indices as 16 bits: all 7 distinct captured
  buffers of the preview match by SHA-256 [C, L].
- The material of each mesh names its textures: `BaseTextureMap` = t0, `SREFMap` = t1, `IrradianceCubeMap` = t2 (the
  32 x 32 cube `cubelight_generic_32.dds`). The captured textures are the install's DDS payloads with red and blue
  swapped (A8R8G8B8 in the file, R8G8B8A8 on the GPU) [C].
- The bone's rest transform is **not** applied to a static mesh: the world matrix is the instance's (e.g. a pure
  translation for the start disc), although the `.gr2` bones carry translations up to 1.3 units [C]. For the animated
  SelectedAni* meshes the mesh matrix is the bone's animated world times its inverse bind (World x
  InverseWorldTransform), which is the identity at rest; the ring's captured rotation is reached by the clip [L].
- The placement rule of 3802's 2.3 holds with two precisions (section 3): the chevrons are spaced by (B - A) / 7 on
  every step, so the 3 chevrons of the last step are the first three of six; and the node 2 of the preview is scaled
  1.5, not 0.75 (3802's text applies the "next turn greater" rule to node 2 wrongly: 120 -> 121 is greater).

## 2. The assets [C files, C capture]

### 2.1 Meshes

Each of the 18 `.fxsxml` names one `.gr2`; each `.gr2` holds two models, each with one skeleton of one bone and one mesh
bound to that bone (the canonical summaries of `models/tools/pyref/gr2canon.py`, 2026-11-16):

| Assets | Mesh 0 (vertices, indices) | Mesh 1 (vertices, indices) |
|---|---|---|
| Center* | `CenterOneTurnCenter` 193, 1056 (the disc) | `CenterOneTurnFrame` 128, 384 (its rim) |
| Connector* | `Box01` 4, 6 (the chevron quad) | `Tube01` 78, 90 (its rim) |
| SelectedAni* | `CenterThreeTurnCenter` 128, 576 (the destination's centre) | `Tube04` 512, 1392 (the rotating ring) |

The vertex format of every mesh is Real32 Position:3, Normal:3, Tangent:3, Binormal:3, TextureCoordinates0:3 (60 bytes).
The EXE uploads Position, Normal and the first two texture coordinates, 32 bytes, in `.gr2` order, without flipping v;
SHA-256 of our repack equals the capture's for the disc, the rim, both connector meshes and both SelectedAni meshes
[C: `vbcheck.py` in the scratch, 2026-11-16 19:5x; L: `test_models` below]. Draw order inside an asset = the `.gr2`'s
model order (mesh 0 then mesh 1) [C].

### 2.2 Textures

The material names `UIShader` as its shader set and three maps. For the preview's assets (all on
`Resource/DX9/UITextures.fpk` except the cube on `MiscTextures.fpk`; the EXE's own lookup decides which pack wins, the
bytes are equal in both [C digests]):

| Mesh | t0 (BaseTextureMap) | t1 (SREFMap) | t2 |
|---|---|---|---|
| CenterOneTurn mesh 0 | `colorthreeturns.dds` | `centeroneturnspec.dds` | `cubelight_generic_32.dds` (cube, 6 x 32 x 32) |
| CenterThreeTurns / ConnectorThreeTurns mesh 0 | `coloroneturn.dds` | `centeroneturnspec.dds` | the cube |
| every rim (mesh 1 of Center*, Connector*) and SelectedAni's ring | `centeroneturnframe.dds` | `centeroneturnframespec.dds` | the cube |
| SelectedAniThreeTurns mesh 0 | `centeroneturncenter.dds` | `specgray.dds` (8 mips) | the cube |

The art's names cross over (the one-turn disc uses the three-turn colour and the reverse); the module takes them from
the materials as they are. All 128 x 128 textures are uncompressed A8R8G8B8 with one mip; the captured contents equal
the file's payload with red and blue swapped [C, `texmatch.py` in the scratch].

### 2.3 Shader and states [C capture]

UIShader at SM5: VS SHA-256 `7ce2cf44...` (build 4390913 VA 0x067FC9E8), PS `7d85ec9f...` (0x067FEBA0); full digests in
`render/src/pathmodels/draw.cpp`. What the programs read (from their signatures and resource declarations, described,
not quoted): the VS reads cb0 rows 0-3 (the world matrix, row vectors) and cb1 rows 3 (the eye position) and 4-7 (the
view-projection); its inputs are POSITION (w = 1 from the 3-float element), TEXCOORD0 and NORMAL. The PS reads t0 and
t1 as 2D textures and t2 as a cube, three samplers, no constants. So cb0's other 192 bytes (row 4 holds (0, 832, 64,
64), row 5 (1/66, 1/66, 0, 0), row 6 (2, 2, 1, 0), the rest stale values) are not read by these programs [C].
States, identical in all 54 draws: back buffer R8G8B8A8_UNORM with the scene's D24S8; blend SRC_ALPHA / INV_SRC_ALPHA,
alpha ONE / ONE, write all; depth LESS_EQUAL, write all, no stencil; solid, cull back, FrontCounterClockwise true,
depth clip on; samplers MIN_MAG_MIP_LINEAR, wrap in u, v and w; input layout POSITION R32G32B32_FLOAT @0, NORMAL
R32G32B32_FLOAT @12, TEXCOORD0 R32G32_FLOAT @24.

## 3. The placement, draw by draw [C capture]

The preview: unit 1002 (Warrior) at (50, 13), served nodes (50,13,22), (51,14,21), (52,14,120), (52,13,121), (53,13,220).
The 54 draws are 27 instances of two meshes each:

| Draws | Instance | World matrix |
|---|---|---|
| 775-776 | node 0, CenterOneTurn | translation (1471.9, -277.06, 19.524), no scale |
| 777-778 | node 1, CenterThreeTurns | scale 1.5, at (1503.9, -221.63, 12.443) |
| 779-790 | step 0 -> 1, 6 x ConnectorThreeTurns | rows (0.866, -0.5, 0), (0.497, 0.861, -0.110), (0, 0, 1); positions A + k (B - A) / 7 |
| 791-792 | node 2, CenterThreeTurns | scale 1.5 at (1568, -221.6, 12.02) |
| 793-804 | step 1 -> 2, 6 connectors | |
| 805-806 | node 3, CenterThreeTurns | scale 1.5 at (1600, -277.1, 12.04) |
| 807-818 | step 2 -> 3, 6 connectors | |
| 819-820 | node 4, SelectedAniThreeTurns | mesh 0 identity at the node; mesh 1 rotated about z (cos -0.540, sin 0.842) |
| 821-822 | node 4, CenterThreeTurns | scale 1.5 at (1664, -277.1, 12.02) |
| 823-828 | step 3 -> 4, 3 connectors | at A + k (B - A) / 7, k = 1, 2, 3 |

### 3.1 Order

Node 0's disc; then for each node i >= 1: its SelectedAni (last node only), its disc, then the chevrons of the step from
node i - 1 to node i [C]. 3802's 2.4 says the EXE sorts instances into two lists by screen box; in this frame all
instances fell into one, in list order [I: the two-list split matters only when the path crosses the map's wrap].

### 3.2 The chevron frame

Row 0 (the mesh's x axis) = normalise(d.y, -d.x, 0), row 1 (y) = normalise(d) including the height difference, row 2 =
(0, 0, 1) (not made orthogonal to row 1), row 3 = the position; d = B - A between the two nodes' ground positions. The
chevrons sit on the straight line between the nodes, z interpolated (A.z + k (B.z - A.z) / 7): they do not follow the
ground between the nodes [C]. 3802's 2.3 named the first axis "along B - A": the capture shows the side first.

### 3.3 Scale

Node 0 unscaled; a node is scaled 1.5 when it is the last node or the next node's turn value is greater, else 0.75 (3802
2.3). In the preview every node 1-4 is 1.5 (21 < 120 < 121 < 220) [C]. **Correction to 3802's 2.3 "VP note"** (its
text, not its rule): it says node 2 gets 0.75; by the rule and in the capture it gets 1.5. SelectedAni and the chevrons
are unscaled [C].

### 3.4 Ground positions

The disc translations are the plot centres (x step 64, row step 55.43, odd rows shifted by 32, plus the 0.05-0.12 shift
of the tap's HexToWorld that `unit-formations.md` 12.3 found) at the height field's height: node 0 at 19.52, the others
at 12.02-12.44 [C]. No node of this preview reached the floor rule.

### 3.5 SelectedAni's clip

`SelectedAniThreeTurns_SelectedAniThreeTurns.gr2`: one animation of 6.667 s (step 1/60 s), two track groups:
`CenterThreeTurnCenter` (orientation identity, position and scale constant) and `Tube04` (orientation curve
D4nK16uC15u degree 2, position constant). The centre bone's rest has scale (6.598, 6.598, 0.756); the capture draws the
centre mesh with the identity matrix, so the EXE applies World(t) x InverseWorld, not the rest pose [C]; with c5models'
sampler the ring's captured rotation is reached at a clip time found by search (result in section 5) [L].

## 4. The module `c5r::pathmodels` [L]

Code: `render/include/c5r/pathmodels/pathmodels.h`, `render/src/pathmodels/` (placement.cpp, models.cpp, draw.cpp),
tests `render/tests/pathmodels/`. Uses `c5models` (0041's reader, its sampler) and `units::plotCentre` /
`units::heightAtWorld` / `units::sha256Hex` read-only; no change under `render/src/units/` or `models/`.

- `placePath(PathInput)` -> instance list (asset, world matrix, node): the rule of section 3 and 3802's 2.3, with the
  list's cap of 256 instances (a longer path drops the rest and counts them [I: the EXE stops adding; not seen]); the
  no-path marker at the cursor (Center + SelectedAni of Inaccessible, Embark or Attack; scale 1 [I]); `show = false`
  clears the list. The DLL-side answers the rule needs are inputs: the last node's attack test, the per-node embark
  tests, and for `nodePositions()` the fog state (revealed, not visible) and a missing plot.
- `nodePositions(HeightGrid, nodes, fog, missing, floor)` -> ground positions: `units::plotCentre`, z from
  `units::heightAtWorld`, the floor when lower, fogged or missing. The floor is the EXE's +0x5E90 value [I: the water
  level; part 2 reads it or takes `HeightGrid::waterZ`].
- `PathModels::load(GameFiles | FileReader)`: the 18 assets, meshes repacked (section 2.1), textures from the materials,
  SelectedAni's clips bound with `c5m::bind_clip_all_groups` (each skeleton has its own track group).
  `meshMatrix(asset, mesh, t)`: identity for a static asset; World(t) x InverseWorld for an animated one, the clip
  looped.
- `buildDraws(models, instances, t)`: one draw per mesh, cb0 = mesh matrix x instance matrix.
- `PathRenderer`: UIShader from the EXE by digest, the input layout, states and samplers of 2.3, one immutable VB / IB
  per mesh, the textures (t2 must be a cube), cb0 per draw (rows 0-3, the rest zero) and cb1 = `frameConstants(camera
  world, view-projection)`; draws into whatever target and depth the caller binds.

Failure behaviour: every step returns a `c5r::Status`; validation paths and their codes are listed in the header and
each has a negative test.

## 5. Tests and results

Suite `render/tests/pathmodels/` (3 executables, `python render/build.py --test -R pathmodels`; game files from
`C5R_GAME_DIR` / `C5R_EXE`, render/build.py's defaults above). First full pass 2026-11-16 19:49 EST (clang-cl Release,
branch commit after 2623b76bc): 3 of 3 passed, 0 skipped [L].

- `test_placement` (no game files): the recorded preview's 27 instances in the captured order, each instance's matrix
  within 2e-4 (rows 0-2) and 2e-3 world units (row 3) of its first draw's cb0; the scales; turn values 1 / 2 / other,
  0.75 in mid-turn, the attack and embark variants; the chevron spacing and frame; the no-path marker and the hide; a
  one-node path; the 256-instance cap (64 nodes: 184 dropped) and zero-length steps; `nodePositions` on a synthetic
  height field (plot centre, height, floor, fog, missing plot); every validation path with its code.
- `test_models` (game files): the 18 assets load (2 meshes each, SelectedAni* animated); **all 54 captured draws
  match**: index count, vertex buffer and index buffer SHA-256, t0 / t1 / t2 SHA-256 (the DDS payload with red and blue
  swapped), and the world matrix within 2e-3, SelectedAni's ring included; the clip time that gives the captured ring
  is **2.272 s** of 6.667 s, max difference 5.3e-5 (scan at 1/60 s, then 1/6000 s); the centre mesh is the identity at
  that time. Loader validation paths (missing file, malformed or meshless `.fxsxml`, a `.gr2` that is not one, the
  `.gr2` missing) and the repack's (no Normal, Real16 position, a non-UIShader material, no BaseTextureMap, no
  IrradianceCubeMap, two groups, an index out of range, no indices, short vertex bytes, a material out of range, 70,000
  vertices) on synthetic inputs.
- `test_draw` (game files, EXE, WARP): the 54 draws with UIShader from the EXE at the capture's camera (cb1 of the
  capture) into a 1277 x 705 target: 10,302 pixels covered, each node's disc covers the pixel of its projected centre,
  the corner stays clear; `frameConstants` reproduces cb1; the program digests are the capture's; every validation
  path of the renderer (no programs, no device, models not loaded, a texture missing, t2 not a cube, a texture that is
  no DDS, an item out of range, a non-finite clip time).
- **AddressSanitizer** (MSVC, `python render/build.py --asan --test -R pathmodels`, 2026-11-16 19:55 EST): canary
  reported, 3 of 3 passed, 0 ASan reports, peak commit 352 MB [L].
- **The stand-alone DIAGNOSTIC picture** (`render/evidence/0300/pathshot_0300.sh`, 2026-11-16 19:57 EST; Visual Checks
  `vc-20261116-195816-workstation-7427`). 3801's own pictures have no path-off picture at the capture's camera (every
  candidate differs from `s_path1` in 1.8-2.2 M pixels: the game panned), so the picture uses task 3804's stock pair
  at camera p1 (`D:/c5-64bit-work/0179/3804/captures/tiny/stock/p1__off_stock_1.png`, `p1__on_stock_1.png`,
  `p1__on_stock_2.png`; c5h-c, 2026-11-14 21:30, same unit 1002 (50,13) -> (53,13), camera within 0.01 world units of
  frame6496's). That preview took another route: (50,13), (51,14), (52,14), **(53,14)**, (53,13) (read from the
  picture's discs projected through the camera); (53,14)'s ground is 12.0 from 3804's height-field dump (raw 15300 x
  0.2 / 255; the same formula gives the five captured node heights to 0.001). Our 54 draws on WARP at the capture's
  cb1 and clip time 2.272 s over the path-off picture, **no scene depth**, against the path-on picture
  (`render/evidence/0300/compare_p1.json`): in the 18,373 pixels our models change, |ours - stock| median 1, p95 125,
  max 234, 19.0 % above 16; stock against itself (on_2 vs on_1) in the same pixels median 0, p95 54, 10.7 % above 16.
  By eye on a full-resolution crop: discs, chevrons and the destination ring at stock's places and sizes, the same
  grey and rim. What differs: the units stand in front of stock's first disc but behind ours (no depth), the ring's
  rotation (clip time), and stock's UI on top (turn labels, the river's blue highlight, the tooltip). The "stock" mask
  class (181,647 px: everything that changes between stock's off and on pictures, the river highlight and labels
  included) is not a measure of the models. Not an acceptance picture (D112): the live comparison is part 2's.
- **SelectedAniAttack has no SREFMap** in its centre material (all other 35 meshes name the three maps) [C files]. The
  module accepts it and binds a 1 x 1 black texture with alpha 1 as t1 (no specular term; an alpha of 0 would give the
  PS a power of 0 and an undefined log(0) x 0) [I: what the EXE binds there needs a capture of an attack preview].

## 6. What part 2 needs, and what is not verified

- **Wiring (part 2, after epic 0431):** the served path (the five calls of 3802's section 4: `TEMPCalculatePathFinderUpdates`
  as the start mark, `GetX`, `GetY`, `GetTurnNumber` per node, the end at the first false `MoveNext`), the show / hide
  bool of `DisplayMovementIndicator` (bridge kind-4 records, `arrows-and-path-line.md` P8), the attack test of the last
  node and the embark tests per step, the fog state per node, the floor value, the clip clock (dt summed per frame), and
  the draw at the end of the server's world pass with the scene's depth. MovementPath then goes back on the D33 key list
  and the live comparison against stock at 0179's cameras is made (definition of done of the ticket).
- **Not verified:** the two-list screen split (a path across the wrap); the no-path cursor marker's scale; the instance
  cap's behaviour; the embark and attack variants' textures against a capture (the rule is 3802's [C], the textures
  come from the materials); the white fog mask over the path (0179's 11.6); a path across a load.
  *(2026-11-18, part 2: the floor, the fog test, the two lists and the DLL tests are settled by section 7.)*

## 7. Part 2: the EXE's tests read again, and the server's wiring (ticket 0300 part 2, 2026-11-18)

Written by the part-2 implementer (account B, branch `task-0300-path-models-p2`, from main ef52fef55). Section 7.1 is
from a read-only headless Ghidra pass over a copy of the analysed project (`reverse-engineering-setup.md` 2; scratch
scripts in `C:/c5-64bit-work/0300/ghidra/`, never committed, D83), described in words. The DLL interface slots come from
`proxy/model/interfaces.json` (x86 slot x 4 = the vtable offset the EXE calls). [D] = design, not yet built or measured.

### 7.1 What the placement asks, read from the EXE [C decompile, 2026-11-18 00:1x]

- **The floor** (+0x5E90 of the placement's object) is a **constant**: the object's constructor (0x5D0D30) stores the
  `.rdata` float at 0xA7A040, **10.116731** (a single-precision float; its bytes were dropped, 2026-11-18, D83 audit), and nothing else writes it (a scan of every
  instruction with the scalar 0x5E90: the constructor writes it; the placement 0x5F8B50 and one other reader,
  0x4F9790, read it). Section 4's "[I: the water level]" is replaced: it is not read from the terrain. The other
  constants of the placement: the scales 1.5 (0x9DFDF0) and 0.75 (0xA0A4AC), 1.0 (0x9F72F4).
- **The fog test** (0x70B790, per node, from the node's hex coordinates): the plot from `ICvMap1::GetPlot` (+0x3C);
  no plot -> the floor; else the floor when `ICvPlot1::GetActiveFogOfWarMode` (+0x0C) returns 1. In the DLL, 1 is
  `FOGOFWARMODE_UNEXPLORED` (`CvEnums.h`: OFF 0, UNEXPLORED 1, NOVIS 2; `CvPlot::GetActiveFogOfWarMode` gives
  UNEXPLORED when the plot is not revealed to the active team). **Correction (2026-11-18) to 3802's 2.3 and to this
  file's section 4 / the header's `fogRevealed`:** the floor applies to a plot the active team has **not revealed**,
  not to a "revealed, not visible" one. The node's z is also the floor when the height field there is below it.
- **The unit** of every test is the EXE's head selected unit (its interface object's `GetHeadSelectedUnit`, +0x0C), and
  each test returns false unless that unit's `GetOwner` equals `ICvGame1::GetActivePlayer` (+0x0C); the team in the
  tests is `ICvGame1::GetActiveTeam` (+0x10).
- **Embark** (0x768E50, called with (this node, previous node)): A = the previous node's plot, B = this node's plot
  (no plot: false); true when `B->IsRevealed(activeTeam, false)` (+0xA0) and `unit->CanEmbarkOnto(A, B, true)` (+0x6C;
  the third argument is `bOverrideEmbarkedCheck`) and **not** `A->IsWater()` (+0x60).
- **Disembark** (0x5DEA50, tried when embark fails): the same with `unit->CanDisembarkOnto(A, B, true)` (+0x70) and
  `A->IsWater()` true. Either test gives the Embark variant (CenterEmbark, connector kind 16; SelectedAniEmbark on the
  last node).
- **Attack** (0x48B290 -> 0x6D8BF0, last node only, and the no-path cursor): with the active player's unit of the
  selected id and T = that unit's `GetTeam` (+0x24): false unless `P->IsVisible(T, false)` (+0x24); true when
  `unit->GetDeclareWarMove(P)` (+0xA0) is not -1 (in the DLL: entering a revealed foreign territory that needs a war, or
  a city or enemy unit the move would attack, `CvUnit::GetDeclareWarMove`), or `P->IsEnemyCity(unit)` (+0x2C), or, for
  some unit V on P (the plot's unit list), the team T is at war with V's owner's team (`ICvTeam1::IsAtWar` +0x28) and
  `unit->CanMoveInto(P, 1)` (+0x9C; 1 = `MOVEFLAG_ATTACK`).
- **The no-path marker** (node count 0, show): at the mouse plot (the EXE's interface object +0x90, wrapped or clamped
  as the path query does, 3802 3), z by the same height / floor / fog rule; `unit->CanSwapWithUnitHere(P)` (0x6182B0,
  +0x68) gives CenterEmbark + SelectedAniEmbark (3802's 2.3 called this test "embark": it is the swap test), else the
  attack test gives CenterAttack + SelectedAniAttack, else CenterInaccessible + SelectedAniInaccessible; all unscaled.
- **The two lists** (pre-pass 0x70BE30, render 0x519B90): the EXE takes the number of views (+0x874, at most 2) and
  for each view a world-space x/y box (from the object's +0x798.. per-view values, widened by 64.0 world units,
  0x9E0AA8). Each instance goes into the list of the **first** view whose box holds its translation, and is **not
  drawn** when no box holds it; render pass i draws list i with view i's camera ("Movement Path", presets 8, 4, 2). So
  across the wrap each instance is drawn once, by the wrap copy whose view holds it.
- **The clocks** (0x42C510, per frame): +0x5E88 (the SelectedAni clip time handed to the pose) and +0x5E8C (a shader
  time) both add the frame's dt; the pre-pass resets nothing. Show / hide (0x734D90) runs the placement 0x5F8B50 with
  the event's bool, which first clears the list (so a hide leaves nothing).

### 7.2 The chain [D]

| Step | Where | What |
|---|---|---|
| 1. Nodes | proxy, `proxy_ui_bridge.cpp` | a listener of the InGame context's `Events.UIPathFinderUpdate` (the EXE raises it with the type-0x51 record's nodes; 3801 saw the served x / y / turn in it, `arrows-and-path-line.md` 6.2) posts **kind 8** records: one per node (at most 64, as the record), or one with count 0 for an empty path |
| 2. Tests | proxy, `proxy_ui_bridge_rt.cpp` (glue) | for each update, the tests of 7.1 through the DLL interfaces (`DllGetGameContext`, as `proxy_leaderscene.cpp`), the unit from Lua's `UI.GetHeadSelectedUnit()` (owner, id): embark / disembark per step, attack on the last node, and for an empty path the cursor's swap / attack tests at `UI.GetMouseOverHex()`; the answers ride in the kind-8 records' flags |
| 3. Show / hide | proxy | the generic listener of `DisplayMovementIndicator` (kind 4, a[0] = the bool), installed by the path install |
| 4. Transport | proxy -> server | `kRt_UiRecord` as today; `SrvRemote::UiRec` pushes kinds it does not know into the feed unchanged (counted as "other"): no change in `server/src/remote/` |
| 5. The record | server, `Scene::pathFeed` | kind 8 assembles the path (by its update number), kind 4 sets the shown flag, `kLoadBegin` clears both; the instance list is rebuilt at the next draw when either changed (stock rebuilds on each show; VP raises a show after every update) |
| 6. Placement | `Scene::pathDraw` | `nodePositions` (plot centre, height field, floor 10.116731 [C], unexplored per the fog scene's plot states, a plot outside the grid = missing) and `placePath` with the tests' flags |
| 7. The draw | `Scene::pathDraw` | at the **end of the world pass**, after the cloud planes (stock: FluidFOW 0x602B10 runs before 0x519B90, `fog-of-war.md` 3.5), into the scene's colour and depth; each instance with the first of view 0 / view 1 (`numViews`, at most 2) that holds it on screen; cb1 per view; the clip clock = the sum of the frames' dt (clamped to 0.25 s per frame); the scene stays dirty while a path with a SelectedAni is shown (the ring turns) |
| 8. The EXE's path off | proxy, `proxy_shell.cpp` | with `ServerPath = 1` and the composite loaded, the view rule clears **MovementPath 0x800000** (config.ini keeps the key on, so the system is started: the same safe direction as Arrows in 0199) |

The option: `VP64Options.ini [Renderer] ServerPath` = 0 (default) or 1, id 6 (the next free one after ServerArc 5),
read by the proxy; with 1 and the server's render lines written, server.cfg gets `render_path=1`, the bridge its path
install, and the view bit is cleared. The server's key `render_path` (default 0) goes into the bundle's
`render_config` (rule A's key route; the offline player does not draw the path yet).

**The D33 key list stays as it is (MC, 2026-11-18, the ticket's thread #4918 answer):** with `ServerPath = 1` the EXE's
path is switched off by clearing MovementPath's **view bit** (the system stays started, config.ini keeps the key on), as
0199 did for Arrows; moving MovementPath back into `proxy_session_keys.h`'s `VP64_D33_KEYS_A2` (the key off in
config.ini, so the system is not even started) belongs to the default-on follow-up, because with the option at its
default 0 the key list must keep the EXE's own path. The tests of 7.1 then run twice per show in such a session (the
EXE's placement still makes them for its undrawn list, and the bridge's glue for the server) [I: harmless, read-only
calls; the parity pair of 7.5 checks it].

### 7.3 As built (2026-11-18 00:2x-00:4x EST) [C code, tests]

- **Bridge** (`proxy/src/proxy_ui_bridge.{h,cpp}`): `ConfigurePath`, `SetPathTests`, `GetPathStats`; record kind 8
  (`kKindPathNode`) and the `kPath*` bits of a[2] (index, count, tested, embark, attack, swap, truncated); the listener
  `PathListener` reads the path's array in a protected call, then `UI.GetHeadSelectedUnit():GetOwner()` / `:GetID()` and,
  for an empty path, `UI.GetMouseOverHex()`. **A trap found by the unit test:** `UI` must be read from the C function's
  own environment (`LUA_ENVIRONINDEX`, the InGame context's, which the listener got at the install), not with
  `lua_getglobal`: `LUA_GLOBALSINDEX` is the globals of whatever thread the EXE raises the event on (in the test, the
  engine's main state), where `UI` is absent. The listener's time on the main thread is summed (`costUs`, `costMaxUs`;
  the stats line's "listener time") for the MC's condition (0518: UI listeners on the EXE's Lua lock matter).
- **Glue** (`proxy_ui_bridge_rt.cpp`): `RequestPath()` sets the tests hook `PathTests`, which makes 7.1's calls through
  `DllGetGameContext()` (`ICvGame1::GetActivePlayer`, `GetPlayer(owner)->GetUnit(id)`, `ICvMap1::GetPlot`, and per step
  `IsRevealed`, `IsWater`, `CanEmbarkOnto` / `CanDisembarkOnto(prev, this, true)`; the last node's attack test with
  `IsVisible`, `GetDeclareWarMove`, `IsEnemyCity`, the plot's units through `GetNumUnits` / `GetUnitByIndex` [I: the same
  set as the EXE's head-node walk], `ICvTeam1::IsAtWar`, `CanMoveInto(P, 1)`; the cursor's `CanSwapWithUnitHere`). Every
  interface object is released (`Destroy`). The embark test asks `CanEmbarkOnto` only when the previous plot is land and
  `CanDisembarkOnto` only when it is water: the EXE asks both in turn, but each of its tests is false on the other
  branch (the IsWater condition), so the answers are the same [C reasoning from 7.1].
- **Shell** (`proxy_shell.cpp`): `PathLines` (as `ArcLines`), `MOVEMENTPATH_BIT` 0x800000 in `ClearBits` while
  `g_bPath` (the composite loaded and `render_path=1` written), the active line names it.
- **Server** (`srv_render_host.cpp`: `render_path` parse, the scene option, `render_config`, `pathFeed` after `arcFeed`;
  `srv_render_scene.{h,cpp}`: `pathInit` (the 18 models once, kept across a device rebuild; `PathRenderer` per device),
  `pathFeed` (assembly by update number, a broken sequence dropped and LOUD), `pathDraw` (7.2 steps 6-7; floor
  10.116731; unexplored from the fog scene's plot states; per instance the view among the first `min(numViews, 2)` whose
  clip-space position is nearest the screen's middle within 1.25, else not drawn [I: the EXE tests world boxes widened
  by 64 units; not compared yet]; cb1 per view with the camera world of view 0, or the rigid inverse of view 1).
- **Options**: `kServerPath = 6`, its text and `TestServerPath`. `docs/OPTIONS.md` is ticket 0504's: the player text is
  `server/evidence/0300/OPTIONS-ServerPath.md`, for the MC to add at the merge (the test's `TestDocs` needs it: the
  suite was run against a scratch copy of `docs/OPTIONS.md` with that text added).
- **Tests** (`server/evidence/0300/tests_0300.sh`, logs in `C:/c5-64bit-work/0300/logs/`, 2026-11-18 00:35-00:41):
  `proxy/tests/ui_bridge` 147/147 under the proxy's toolchain and 147/147 under MSVC ASan (canary caught), of them the
  0300 blocks: path only (install, 3801's 5-node preview in order with VP's show first, the tests hook's query, an empty
  path with the mouse plot and the swap bit, a 70-node path cut to 64, 16-bit edges, six unreadable paths, a failing /
  absent / id-less unit, another thread, no hook, a silent-drain re-install with the old listener stale), path with the
  whole bridge, and a missing `UIPathFinderUpdate`; options 241/241 on x86 and x64 and 241/241 under ASan with the
  scratch docs (240/241 with the repository's docs: only `TestDocs` for ServerPath, as expected); `render/tests/pathmodels`
  3/3 plain and 3/3 under ASan (0 reports, canary caught).

### 7.4 Stock's path is not hidden by trees: the depth it is drawn against [L, c5h-c, 2026-11-18 01:00-01:13]

The first joined picture round (build server.exe 2def13d1, Visual Checks of 01:07-01:13, pictures kept in
`F:/c5-64bit-work/0300/captures/tiny/joined-try1/`) drew the path with the scene's depth at the end of the world pass,
as 7.2 planned. On open ground (p1, pi) the server's markers matched stock by eye (side-by-side crops). On forest plots
they did not: at pw (destination in a forest) and pa (the attack ring on a barbarian in a forest) the server's ring and
disc were almost entirely hidden by the server's trees, while **stock draws the ring and the disc over the trees** (the
trees show through the translucent ring; the disc covers the tree tops), and over the units' feet. Only the terrain cuts
stock's ring (3804, 11.2: the ring is cut by the ground where a hill rises). So in stock the depth the "Movement Path"
pass tests against holds the terrain but not the trees [L picture; I: why — the forest draw's depth write is [C, L] in
`trees.md` 1, so either the path pass binds an earlier depth, or the presets 8, 4, 2 of its pass begin (7.1) select a
depth state or target that the capture of frame6496 (open ground only) could not show; a RenderDoc capture of a path
over a forest settles it]. **Change (2026-11-18 01:15):** `Scene::pathSaveDepth` copies the scene's depth after the
water and the ice and before the forest stage and the units (only while a path is shown; one `CopyResource` of the
depth texture per frame), and `pathDraw` binds that copy. The EXE's own units are composited over the server's picture
under D33 in any case (the EXE draws them after the composite), so a unit standing on a path plot covers the server's
disc where stock's disc covers the unit's feet: a D33 difference, not one of this module [L by eye at pa and p1].

### 7.5 The placement rule read again: SelectedAni on embark nodes, the order, the chevron count [C decompile, L]

The joined pe picture of 01:38 (build server.exe 2e7c3c42) drew node 1 of a land-to-water path as CenterEmbark only
(the server's list: assets 0 4 16x6 10 2 14x3, `render.log` "path: list 56"; the glue's crossing log "(52, 14) -> (51, 13)
land to water: embark marker"), while stock drew a large ring around that node's disc. Read again in 0x5F8B50 (7.1),
the branches add, per node i >= 1:

| Node's variant | Instances, in order | Chevrons of the step into it |
|---|---|---|
| attack (last node only) | CenterAttack (scaled), then SelectedAniAttack (unscaled) | 3 |
| embark or disembark (any node) | CenterEmbark (scaled), then SelectedAniEmbark (unscaled) | **6, also on the last node** |
| turn 1 / 2 / other, last node | SelectedAni* (unscaled), then Center* (scaled) | 3 |
| turn 1 / 2 / other, middle node | Center* (scaled) | 6 |
| no path (cursor) | Center (unscaled), then SelectedAni (unscaled) | - |

**Corrections (2026-11-18) to 3802's 2.3 ("the SelectedAni instance is added only on the last node": not for the embark
variant) and to part 1's `placePath` (embark nodes had no SelectedAni; attack, embark and cursor markers had the
SelectedAni first; an embark destination had 3 chevrons).** `render/src/pathmodels/placement.cpp` follows the table since
2026-11-18 01:4x; `test_placement` checks the order and counts (`attack_and_embark`, `embark_destination`,
`no_path_marker_and_hide`); 3/3 plain and ASan. The preview of 3801's capture is turn nodes only, so its 54 draws are
unchanged.

A setup note for pe: the run's first promotion with `AllowsEmbarkation` is PROMOTION_ALLWATER_EMBARKATION, with which
only deep water needs embarking (`CvPlot::needsEmbarkation`), and the Tiny save's civilization already embarks at turn 9
(pw's path crossed a water plot at (0, 14) with embark markers on both crossings). The run used the same unit and
promotion in both arms, so the pair compares like with like; the driver gives PROMOTION_EMBARKATION with C5_0300_PLAIN_EMBARK=1 (both arms alike), the first promotion otherwise.

### 7.6 The server's path against stock (DIAGNOSTIC, D112) [L, c5h-c, stock 00:56-01:04, joined 01:49-01:58, 2026-11-18]

Arms: **stock** = the stock EXE on the 32-bit pin `i0ship-39f17f75-ia` (`darkred --arm stock`, no proxy, no server; the
EXE draws the path); **joined** = the D33 product path as 0199 ran it (3308's prep, the A'' form, the shipped composite
ini, ServerRenderer = 1, WhiteFogMask default) with `ServerPath = 1`, build of branch `task-0300-path-models-p2`
(server.exe 517b1fc9, proxy 14459bad, composite 0b189f42; x64 DLL pin layout `x64-4f4daac7-pf8e9f42`). Save copy
`C5H-0300-TINY_0009V` of `C5H-3600-TINY_0009V` (27420A41). Cameras and stimuli (`server/evidence/0300/run_0300.py`, all
player input, no Lua fallback in the final runs): **p1** = 3804's path (Warrior 1002 (50, 13) -> (53, 13), the same
five served nodes as 3801's capture); **pi** = the same unit, cursor on the mountain (49, 14) (no path: Inaccessible);
**pa** = Lua-made Warrior 1039 (50, 14) -> a barbarian Warrior at (49, 16) in a forest (attack); **pw** = Warrior 1041
(1, 14) -> (55, 15) across the wrap seam (the path crosses the water plot (0, 14): embark markers); **pe** = Warrior 1042
(52, 14) -> coast (51, 12), given PROMOTION_ALLWATER_EMBARKATION (7.5). Method: 0199's `compare_0199.py` (copied as
`compare_0300.py`): the item mask = stock on vs off > 12 on visible ground minus the UI boxes; item_cut minus what moves
between stock's two on shots; **item_strong** = item_cut where the path changes stock by more than 48. Largest channel
difference, median / p95 / max (share > 12):

| Camera | Strong px | Stock vs stock | Joined vs stock | Joined off vs stock off (the D33 floor) | Joined on vs joined off |
|---|---|---|---|---|---|
| p1 | 23,038 | 0 / 2 / 12 (0.0 %) | 0 / 58 / 200 (13.6 %) | 2 / 87 / 213 (21.2 %) | 83.5 / 156 / 246 (90.7 %) |
| pi | 16,921 | 0 / 2 / 11 (0.0 %) | 0 / 56 / 204 (13.9 %) | 1 / 61 / 222 (15.1 %) | 89 / 155 / 205 (94.3 %) |
| pa | 57,588 | 0 / 7 / 12 (0.0 %) | 7 / 66 / 248 (41.8 %) | 14 / 92 / 208 (51.3 %) | 33 / 137 / 252 (52.2 %) |
| pw | 26,289 | 0 / 7 / 11 (0.0 %) | 25 / 82 / 200 (66.2 %) | 25 / 94 / 168 (69.4 %) | 80 / 149 / 244 (83.6 %) |
| pe | 37,965 | 0 / 7 / 11 (0.0 %) | 9 / 65 / 174 (45.0 %) | 20 / 94 / 186 (57.5 %) | 55 / 136 / 239 (59.6 %) |

(All masks in `C:/c5-64bit-work/0300/cmp-joined.md`; difference images `F:/c5-64bit-work/0300/captures/tiny/diff/
<cam>__joined__item_diff.png`, side-by-side crops `diff/side_<cam>.png`; game data, never committed, D83.)

- **The server draws the path at every camera** (joined on vs off: 52-94 % of the strong pixels change), and **on the
  path's pixels the joined picture is no further from stock than the D33 ground without the path** (median and p95 at
  or below the floor at every camera), the criterion 3804 and 0199 accepted on.
- **By eye** (the side-by-side crops, Visual Checks `vc-20261118-015842-workstation-9f31` p1, `-015844-...-d6d0` pi,
  `-015845-...-e940` pa, `-015846-...-c0d2` pw, `-015847-...-d412` pe, all DIAGNOSTIC): the same discs, chevrons, turn-end
  scale and destination ring at stock's places; the red Inaccessible marker on the mountain cut by the mountain as in
  stock; the red attack ring and disc on the barbarian, over the forest; the embark ring around the first water node
  (pe) and on both crossings of the seam path (pw), the seam path's nodes on both sides of the seam where stock has them.
  What differs: the destination ring's rotation (the clip clock's phase), the EXE's units drawn over the server's discs
  (D33), and the D33 ground itself (no Decal; the server's trees).
- Three earlier joined rounds found the defects fixed before this one: 01:07-01:13 (7.4, trees hid the path; pictures in
  `joined-try1`), 01:21-01:27 (the depth fix confirmed), 01:32-01:39 (7.5, no SelectedAni on embark nodes; `joined-try3`).
- A picture trap met on stock too: a share of the harness shots come back as stale UI-less frames (E9's PrintWindow
  artifact); the first stock round kept three of them (p1 on 1 and off 1, pa on 1). The driver now keeps only a shot
  whose top-left corner shows the interface (mean grey < 90 there; UI-less frames measured 168-191, frames with the
  interface 31), from up to 10 candidates; the stock round was taken again (00:56-01:04).

### 7.7 The parity pair and 30 turns with a path preview before each [L, c5h-c, ON 01:58-02:10, OFF 02:11-02:23, 2026-11-18]

The same instance, prep, save copy and build as 7.6; ON = `ServerPath = 1`, OFF = `ServerPath = 0`, 30 AI turns each
(turn 9 to 38), before every turn a path preview (Lua select and move mode, the cursor onto the destination by the
harness: 30 of 30 turns in move mode in both arms). `server/evidence/0300/parity_0300.py`:

| | ON (ServerPath = 1) | OFF (ServerPath = 0) |
|---|---|---|
| `eq_stats` after the load / after the last turn | 362 / 434 keys | 362 / 434 keys, **identical: 0 differing in both** |
| turns, end, rc, crash, dialog box | 30 (9 -> 38), max turns, 0, none, none | 30, max turns, 0, none, none |
| LOUD lines: render.log, server.log, vpproxy.log, the composite's log | 0, 0, 0, 0 | 0, 0, 0, 0 |
| the bridge (the stats line after 3,600 Updates) | path updates 124 (empty 72), node records 331, tested 124, embark 33, bad arguments 0, stale 0 | no path install |
| EXE private, first / last turn (MB) | 1,332.2 / 1,360.0 | 1,322.0 / 1,364.3 |
| server private, first / last turn (MB) | 903.6 / 922.8 | 885.8 / 905.5 |
| turn wall time, median (s) | 18.3 | 19.9 (indications only: the workstation, COMMON 23) |

- **Parity: met.** The glue's DLL calls (7.3) are read-only interface methods the EXE itself makes for the same path
  (its placement still runs in a ServerPath session, 7.2), and they change no game result in this pair.
- **The listener's cost on the EXE's main thread** (the MC's condition, 0518): 124 path updates took 38.1 ms in all,
  **mean 307 us, max 1.33 ms per update** (the bridge's own clock around its listener: reading the path and the unit in
  Lua, the glue's DLL tests through the proxy, the posts), with node paths of up to 8 nodes and the empty-path cursor
  tests [L, ON arm]. An update comes only when the hovered plot changes (124 in 30 turns of previews).
- The server is about 17-18 MB larger with the option on at both ends (one run each; the 18 models, their textures and
  the depth copy; within the run-to-run noise of a single pair [I]).

### 7.8 The EXE's path is off under ServerPath = 1 (TEST arm x) [L, c5h-c, 02:25-02:35, 2026-11-18]

The same prep with `ServerPath = 1` and the TEST ONLY `vpproxy.ini server_cfg_extra=` file `render_path=0` (appended
after the proxy's `render_path=1`; `render.log`: "render_path=0: no move-order path markers"); the proxy still cleared
the view bit (`vpproxy.log`: "MovementPath (0x800000) cleared"). At all five cameras, move mode on and the cursor on the
destination, **no path model is drawn**: only the UI's turn labels ("1", "<2") and the plot highlights stay
(side-by-side crops `F:/c5-64bit-work/0300/captures/tiny/diff/side_x_<cam>.png`; Visual Checks
`vc-20261118-023732-workstation-1e8b` pe, `-023734-...-f964` pa, `-023736-...-f9ed` p1, DIAGNOSTIC). So the cleared view
bit stops the EXE's own path models (the system stays started: config.ini keeps the key, 7.2), and the ON arm's markers
are the server's. On the strong mask the x arm is far from stock where the markers are opaque (p1 p95 109 against the
ON arm's 58, floor 66-87; pi 83 against 56) (`C:/c5-64bit-work/0300/cmp-x.md`).

### 7.9 The default, and what is not verified (2026-11-18 02:4x)

**`[Renderer] ServerPath` stays 0 by default** (the ticket's rule, as ServerArc in 0199): the live checks of 7.6-7.8
passed, but a draw the server makes by default needs rule A's default-on path (format 9, the library recorded with
the option on) and D118's failure route for the path, and the D33 key list's MovementPath entry moves with that
follow-up (7.2). Rule A for default 0: the key `render_path` is in the bundle's `render_config`; the offline player
(`sceneshot`) does not draw the path yet (it reads no such key: a bundle recorded with the option on replays without
the path).

Not verified:
- **Why stock's path is not hidden by trees** (7.4): the server draws against the depth before the trees and units; a
  RenderDoc capture of a path over a forest would show which depth stock's pass binds. Over hills and the terrain the
  copy keeps stock's cut (pi's marker cut by the mountain, as stock's).
- **The two lists' rule** is approximated (each instance with the view whose clip-space position is nearest the screen's
  middle within 1.25; the EXE tests world boxes widened by 64): the seam camera pw matched by eye, no camera put an
  instance at the edge of a view box.
- **The clip's phase**: the destination ring turns on the server's own clock (the frames' dt summed from its start), the
  EXE's from its start; the rotation differs between the arms in every picture.
- **The attack test's unit walk** uses `GetNumUnits` / `GetUnitByIndex` for the EXE's head-node walk [I: the same units in
  the same order]; GetDeclareWarMove (a move into foreign land without open borders) was not staged live.
- **SelectedAniAttack's centre** still binds the black stand-in for its missing SREFMap (part 1, 5); at pa the attack ring
  and disc looked as stock's by eye, no draw-level comparison.
- **A path across a load** (0179's 11.6): `kLoadBegin` clears the path and the shown flag [C code]; not run live.
- **The huge save**, fogged path nodes (Tiny is fully revealed: the floor for unexplored plots was not exercised live),
  and the white fog mask over a path on fogged plots.
- **A bundle recorded live with ServerPath = 1 and replayed**: not made (the player draws no path).
