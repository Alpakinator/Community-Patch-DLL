# Cities, landmarks, bridges and wonders drawn by the server: the plan and the renderer interface (epic 0561, C1)

Written 2026-11-18 16:30-16:42 EST by lead-0561 (account B) as the C1 milestone of epic 0561 (increment 3 package I3-D,
0037). Inputs: the MC's brief `docs/briefs/epic-0561-cities-and-landmarks.md`, the Architect's switch answer
`research/07-architecture-decisions/architect-2026-11-18-cities-landmarks-switch.md` (route A, the tap, two bits,
P1-P7), the increment 3 sheet sections 4.5-4.7, 6, 7, `cities-and-landmarks.md` (0319/0337/0338),
`unit-draw-filter.md` 0-3, `unit-feed-contract.md`, `unit-draws.md` 8, `scene-catalogue.md` 11 (3910). Nothing was built
and no game was run for this file; the only new measurement is the program tally of section 5 (offline, on existing
exports). Marks: `[C]` read in code, data or an export; `[L]` a live run of the team's; `[I]` inferred.

**State: C1 accepted with conditions by the MC, 2026-11-18 16:43 EST (queue #5962, #5963; issue log; main b6e8f4bf7).**
Sent as REVIEW-REQUEST #5959 at 53f95df4c. The answers:
- Q1 yes: the `own` layout is parked, 4880/4881 as backlog children.
- Q2 yes, the two bits split by the tap's identity set, **on two conditions**: P3b identifies every class's mesh, and
  P7 measures the per-draw identity lookup in the filter (its cost per frame on a huge save). If either fails: one
  option and one bit (the Architect's fallback).
- Q3 granted (touches widened, v3), except `docs/OPTIONS.md` (epic 0504 holds it): the OPTIONS.md text goes into the
  MERGE-REQUEST and the MC writes it. Option ids renumber at the merge, after 0437 and 0504. The new shared headers are
  named `shell/common/shell_model_tap.h` and `shell/common/shell_model_scene.h` in the touches (not `city_programs.h`).
- Q4 the tolerance of section 6 accepted.
- Q5 yes, 4870 may start.
- Q6 animated landmarks take the EXE's palette from the tap in state (ii): the faithful choice (D66), no decision of
  the owner's needed; the pose layer stays for the `own` mode (offline scenes without tap records, later true absence).
- R1 (edge popping while panning): measured in 4873's harness, the fix named before 4878.

## 0. In short

- **Route A as the Architect answered:** the EXE's City and Landmark systems stay on; the compositor's draw filter (the
  units' recogniser, extended) drops their scene draws **and taps them**: per dropped draw the mesh identity, the
  instance matrices of vertex slot 1 and the per-draw constants the server needs. The server draws its own copy of the
  model with those matrices. Position source `exe` (the tap, the product) and `own` (layout from the knowledge base,
  offline scenes without tap records, later true absence).
- **Offline first (D137):** the 3907/3910 captures already hold every input of the `exe` source (index buffers,
  slot-1 instance buffers, constant buffers, bound textures). A tap record can therefore be made from a capture
  offline, and the server's draw of it compared with the captured target at the same camera, before any live tap
  exists. The offline acceptance needs no layout code.
- **The layout code (lattice, density field, wall spline, the six handlers, TerrainContour, SPECIAL placers, the farm
  buildings) is off this epic's path** to the first accepted picture. Proposed: it leaves this epic's definition of
  done and is kept as two parked children for the `own` source (section 1, Q1).
- **P1 (answered by 4870, 2026-11-18, section 3.1 and 4; its marks):** the rigid classes split by VS: city blocks
  material 5 (PS `c704c80b` / `73daa800`), landmarks material 4 (VS `7ec4e9a5` / `88c2b7d4`), wonders and display
  buildings material 3 (VS `c4bb25da` / `0a3f2230`), never the other way on 3910; only the skinned class (material 6)
  serves both cities and landmarks. *(Corrected 2026-11-18 by lead-0561 from 4870's hand-back. Was: "it does not split
  by program ... wonders, walls and display buildings very likely not separable from landmarks [I strong]", from the
  lead's first tally, which read PS only.)* The two bits keep the **tap's identity set** (C1 Q2, MC #5962): it splits
  the skinned class and gives the safety rule "never drop what the server cannot draw"; CPU-built meshes with no file
  (the Great Wall, likely walls) are outside it and stay with the EXE until 4879.
- **Sizes:** the path to acceptance is children 4870-4878, **54-93 h raw**; the `own` layout 4880-4881 parked,
  27-50 h raw.

## 1. Breakdown (children 4870-4899)

Sizes are raw agent-hours `[I]`. "Offline" = on recorded inputs (3907/3910 exports on `F:/c5-64bit-work/0301/`, the
install read at run time, the scene library); no game client in the loop until 4878.

| Id | Child | Hours | Needs | After | Offline? | Oracle |
|---|---|---|---|---|---|---|
| 4870 | **Premises P1-P4, P3b, P3c, P8 offline** (section 4) on the 83 exports of 3907/3910: every draw of the BuildingShader programs named by material (shaderdump's index), per target, per object of 3910's `index.csv`; slot-1 content; the identity rule (index-buffer content hash and range against the server's own repack of the `.gr2`); the per-draw constants; walls in 3907 (none walled: names the gap); the filter table draft `city_programs` (digests only, D83). Evidence scripts in `render/evidence/0561/4870/` | 4-6 | none | C1 | yes | the exports |
| 4871 | **Live captures P6, P4-walls** on one Goldberg instance: one capture of a road and a railroad across a river (P6), one capture of a walled, damaged city (P4, and 0337's tower count `floor(length / 12)`), exported with `rdc-export`. *(P5 moved to 4874 on 2026-11-18 by lead-0561: the filter's path 2 checks each program's create site and handle global (`unit-draw-filter.md` 3.2), so the test key `unit_filter_table` cannot show P5 without code.)* | 2-4 | 1 game instance when a place is free (0398 / 0559 first) | none | no | the captures |
| 4872 | **The model library**: every BuildingShader model the EXE can draw, read from the install at run time: `ArtDefine_Landmarks` rows (667 rows, 514 models) and their `.fxsxml`, wonder art (`WonderArtInfo`, 76 types, cultural/era/random forms), the seven wall files plus the Great Wall pieces, the 25 city sets (7 cultures x 4 era slots, blocks by class, the ground plane meshes); meshes repacked to the EXE's slot-0 vertex forms (32, 20 or 28 bytes, section 2.1), the identity key of each mesh (4870's rule), textures by material; lazy residency by key | 6-10 | model reader (`models/`) | 4870 (key rule) | yes | the exports' index and vertex buffers, byte-equal |
| 4873 | **The draw layer and the offline harness**: the four programs (materials 3, 4, 5, 6; VS/PS from the EXE by digest, as terrain does, D13) with the captures' states; `ModelScene` -> draws; the tap-frame file form `.mtap` and the resolved scene `.c5models`; `render/tools/cityscene/`: capture export -> tap frame -> server draw -> compare with the captured scene target on the object's pixels (median, p95, max, difference image, COMMON 25) | 8-14 | 4872 | 4872 | yes | 3907/3910 captures at their own cameras |
| 4874 | **The compositor's recogniser, filter and tap** (shell, outside touches, Q3): P5 first (the create sites and handle globals of the BuildingShader programs from the create-site table, offline, then one live load with path 2's counters); the units' filter generalised to one table per mode bit (one set of draw hooks, so it must be one recogniser); per class the drop rule (PS + VS pair, then the identity set of 4875); the tap: Unmap copy of the slot-1 instance buffers (deferred contexts included), index-buffer identity at creation (or the 4870 fallback), the constants of P3c; the per-frame section `Local\VPShellModelTap.<exe pid>` with a seqlock and an ack, as the decal tap; `filterState` per bit (section 3.4); stand-in tests (`unit_filter_host` extended) for every refusal and the D118 path | 11-17 | none (stand-in) | 4870 (table) | yes (stand-in) | the stand-in; 4878 live |
| 4875 | **The server's side**: the tap reader (copy-then-check, refusals counted, last good frame kept), the scene source for `exe` and `own` (own empty until 4880/4881), the identity-set publication to the compositor, the mode writer for `VPMODE_CITIES` / `VPMODE_LANDMARKS` (ready per module, latched per frame, D118 per bit through `filterState`), the two options, the bundle member *(feed kind 34 `kWonderCreated`, section 2.3, moved 2026-11-18 by lead2-0561 to the parked `own` children 4880/4881, MC noted #6134: the `exe` source does not need it)* (the tap frames' file form, D137; scene-bundle rule A), fog from the server's own fog texture in the draws (ends D136's white mask for these models) | 10-16 | 4873, 4874 interfaces | 4873 | yes (bundle replay) | the bundle replays; 4878 live |
| 4876 | *Closed 2026-11-18 by the MC (#6446), absorbed: 4874 taps the skinned palette and 4873 draws material 6 with it (3910 Lanc07 and Sanc01 at 1.00000); the live checks went to 4878.* **Animated landmarks** (22 models; VS `7a9e17f6`, palette buffer): either the EXE's palette taken by the tap (exact, cheap) or the units' pose layer (Q6) | 3-6 | 4873; Q6 | 4873 | yes | captured palettes (3910) |
| 4877 | **Bridges**: road and rail bridges (what stock draws at a road across a river, P6); pontoon bridges are landmark rows and come with 4872/4873 | 2-8 (sized after P6) | 4871's capture | 4871, 4873 | yes after P6 | the P6 capture |
| 4878 | **Acceptance**: the offline catalogue compare at the C1 tolerance (section 6); live on Goldberg instances, D112 acceptance pictures with D114 popups closed; the mixes all off, all on, each option alone, ServerUnits on and off for one (Architect 4); shadows between server- and EXE-drawn models looked at; memory and load time with the options on against off; P7 the filter's and tap's cost on oldpc (PERFORMANCE.md, S3's method, driver named, D175); no crash or box in the acceptance loads (D155, COMMON 30); `docs/OPTIONS.md` rows | 8-12 | game instances, oldpc | all above | no | stock catalogue, live stock |
| 4879 | **CPU-built meshes** (added 2026-11-18 by lead-0561 after 4870 #6040): the Great Wall and, if 4871 confirms, the walls are world-space meshes built by the EXE per frame or per city, with no file to key; the tap copies their geometry at run time from the player's process (D83: nothing committed), or they stay with the EXE (outside the identity set) | sized after 4871 | 4874 | 4871, 4874 | yes (captures) | W01 draws 469/617; 4871's walled city |
| (lead) | **Bundle re-record at format 9** (added 2026-11-18 18:45 by lead2-0561 on MC #6158): 4875's `modeltap/` member raises `kFormatVersion` 8 -> 9 (rules A and B, `offline-scene-harness.md` 24), which refuses every v8 bundle; the epic re-records the offline library once on the epic branch after the last bundle-changing child has merged and before the MERGE-REQUEST; if main reaches 9 first, the number moves to main's + 1 at the rebase and the record is made again | 1-3 | the recorder, an instance only if the library's recipe needs one | 4875 and every later bundle change | per the library's recipe | the library's own checks |
| | **Path total** | **54-93** (+1-3 re-record) | | | | |
| 4880 | *Parked (Q1):* the `own` city layout: blocks (lattice, density field, relaxation, random pick seeded and recorded; 0337 1.12), city field and slot positions (1.9, 1.10), walls (1.13), ruins and damage anchors (1.14) | 14-25 | 0337's N6c reads | - | yes | 3907 block positions (exact) |
| 4881 | *Parked (Q1):* the `own` landmark layout: row choice (2.5), variants, TerrainContour (2.4), SPECIAL placers, farm buildings (the server's own field outlines, Architect 2) | 13-25 | 0338's N7c reads | - | yes | 3910 per object |

**Order and waves (lead plus at most 3 workers):** wave 1 after the C1 answer: 4870 (worker A, offline) and 4872's
install-reading part (worker B, offline; the identity key added when 4870 reports), 4871 (worker C) only when a game
place is free. Wave 2: 4874 (A, after 4870), 4873 (B, after 4872). Wave 3: 4875, then 4876, 4877 as P6 allows. 4878
last. The lead keeps the interfaces (section 2) and merges `main` at every milestone (COMMON 28).

**Where the D2-D6 items of the brief land:** D2 landmark models -> 4872/4873 (construction and pillaged states are the
EXE's choice, carried by the tap), animated idles -> 4876, the six handlers and TerrainContour -> 4881 (parked); D3 ->
4877 (pontoons with D2); D4 assembly, walls, damage, capture, ruins -> the tap draws the EXE's own result (4872-4875),
the `own` assembly -> 4880 (parked); the city-field stamps in the bake: nothing to do in `exe` (the EXE keeps its
city-field manager and its ground decals come through 0166's decal tap); D5 wonders and display buildings ->
4872/4873 (half-built is the EXE's model choice); D6 fog, feed, switch, acceptance -> 4874, 4875, 4878.

**Damage smoke** is an effect drawn by the EXE's effect systems (on since 0368, D147) at the EXE's anchors; it stays
the EXE's. **Roads and rails** (brief D3): today the EXE's RoadLayout lays the route curves as terrain decals and the
server draws them from 0166's decal tap in its bake (route (a)) `[C, terrain-decals.md 2.2]`; this epic changes
nothing there (Architect 3).

## 2. The renderer interface (item 2)

### 2.1 Modules

Both kinds of model use the same family of programs (section 5) and the same instanced draw, so the shared layers live
in one module and the city-only parts in the other. **Vertex forms (corrected 2026-11-18 17:02 by worker-4872, #5998
[C, input layouts of every BuildingShader draw in 3910 W01 and 3907's four city exports]; was "the same 32-byte
vertex"):** city blocks 32 B (POSITION R32G32B32, NORMAL R32G32B32, TEXCOORD0 R32G32); every other single model 20 B
(POSITION and NORMAL R16G16B16A16_FLOAT, TEXCOORD0 R16G16_FLOAT); skinned parts 28 B (POSITION half4, BLENDWEIGHT
R8G8B8A8_UNORM, BLENDINDICES R8G8B8A8_UINT, NORMAL half4, TEXCOORD0 half2) with **slot 1 of stride 4** (R32_UINT
BLENDINDICES1), not the 64-byte matrices: the skinned tap takes the palette, not slot 1. Index buffers R16 except 4
draws with R32.

- `c5r::landmarks` (`render/include/c5r/landmarks/`, `render/src/landmarks/`): the model library of every
  BuildingShader model (landmark rows, wonder art, walls, city sets as meshes), the identity keys, the draw layer, the
  tap frame and the scene, their file forms. Later (4881) the landmark layout.
- `c5r::cities` (`render/include/c5r/cities/`, `render/src/cities/`): the city art catalogue (culture and era slot to
  a set, the conversions of 1.10, walls by culture, wonder forms by culture and era), used by the library and by
  acceptance labels; later (4880) the block and wall layout.

### 2.2 The types (renderer terms; names may change in review, the fields not without the MC)

    namespace c5r::landmarks {
      enum class ModelClass : uint8_t { CityBlock, CityPiece /* wonder, display building, wall, ruins */,
                                        Landmark /* improvement, resource, feature, pontoon */, Unknown };
      struct MeshKey  { uint64_t indexHash /* from index 1 */, vertexHash; uint32_t firstIndex, indexCount; int32_t baseVertex; };   // 4870 #6015; as built by 4872 (landmark-renderer.md); shared keys resolved with t0/t1 by candidates()
      struct TapDraw  {                     // one dropped EXE draw (file form .mtap, one record)
        uint32_t program;                   // index in the filter table (material 3/4/5/6, pass 0)
        MeshKey  mesh;                      // or the fallback key of P3b
        uint32_t instanceCount;             // 1..n
        uint32_t firstMatrix;               // into TapFrame::matrices, 16 floats per instance, row-major, row vectors
        uint32_t constants;                 // into TapFrame::constants, the P3c values (lighting set, tint, time)
        uint32_t palette;                   // skinned only: into TapFrame::palettes, else ~0u
      };
      struct TapFrame { uint64_t exeFrame, seq; std::vector<TapDraw> draws; std::vector<float> matrices,
                        constants, palettes; };
      struct DrawItem { const Model* model; uint16_t mesh; ModelClass cls; uint32_t firstMatrix, count; ... };
      struct ModelScene { uint64_t exeFrame; std::vector<DrawItem> items; uint32_t unknownDraws; };
      Status resolve(const ModelLibrary&, const TapFrame&, ModelScene*);   // source `exe`
      Status buildOwn(const ModelLibrary&, const OwnInput&, ModelScene*);  // source `own` (4880/4881; empty until then)
      class ModelRenderer { Status init(ID3D11Device*, const ProgramSet&);
                            Status draw(ID3D11DeviceContext*, const ModelScene&, const ViewInput&, const FogInput&); };
    }

Every function returns a `c5r::Status`; nothing throws; an unknown mesh is counted (`unknownDraws`) and drawn by nobody
only if the compositor dropped it, which the identity set prevents (section 3.2).

### 2.3 The feed and the other inputs

| Input | Today | This epic |
|---|---|---|
| The EXE's choice of model and placement | not carried | **new: the tap frame** from the compositor (4874/4875), file form `.mtap` in the bundle |
| Plot state (improvement, state, resource, route, pillaged, feature, natural wonder) | feed `PlotRecord`, kinds 1, 2, 7 `[C, srv_feed.h]` | unchanged; used by acceptance labels and by `own` |
| Cities: created, destroyed, captured, damage, population | kinds 4-6, 26, 27 `[C]` | unchanged |
| Walls | kind 8 `kWallCreated(plot)` `[C]` | unchanged |
| Wonders and display buildings | not carried | **new kind 34 `kWonderCreated`**: `GameplayWonderCreated(player, plot, building, state)` (slot 189, `cities-and-landmarks.md` 1.6): plot = the call's plot index, a[0] player, a[1] building type, a[2] state 0/1, a[3] 0; pushed before the call is forwarded, as every feed kind. For acceptance labels and `own`; kind number the MC's |
| Owner's art style and era | `PlayerRecord` (record 6) `[C]` | unchanged (culture and era slot by 1.10's conversions) |
| A city table (plot, population bucket, damage, walls art) | not carried (no pure getter gives a city's plot, `unit-feed-contract.md` 2.1) | **not needed for `exe`**; a record 7 CITIES is named for 4880 and only drafted then |
| Fog | the server's own fog (`srv_fow_scene`) `[C]` | used by the model draws (4875) |

### 2.4 File forms (D137)

`.mtap`: one tap frame (header with magic, version, EXE frame, seq, counts; the four arrays), byte-checked as the decal
tap's images are. In a bundle: `modeltap/frame<NNNNNN>.mtap` for the frames recorded (every frame, or the frames whose
tap content changed with a reference to the last equal one; 4875 decides by size). Made offline from a capture export by
`render/tools/cityscene/` (4873), so the catalogue's 83 exports are the first scene library for this module.

## 3. The switch (item 3)

### 3.1 Programs (P1, section 5)

| Class | Colour pass (scene, reflection) | Shadow pass (target 13383) |
|---|---|---|
| City blocks (material 5 `BuildingShader_CODE_Instanced_Opaque`) | VS `7ec4e9a5` / PS `c704c80b` | VS `88c2b7d4` / PS `73daa800` |
| Single models, instanced (material 4) | VS `7ec4e9a5` / PS `f0e1f0a6` | VS `88c2b7d4` / PS `e12e9eaf` |
| Single models (material 3 `BuildingShader_CODE`; VS `c4bb25da` also material 1 `BuildingShader`) | VS `c4bb25da` / PS `f0e1f0a6` | VS `0a3f2230` / PS `e12e9eaf` |
| Animated parts (material 6 `BuildingShader_Skinned_CODE`) | VS `7a9e17f6` / PS `f0e1f0a6` | VS `34b98e63` / PS `e12e9eaf` |

**Which objects each class draws (4870, 2026-11-18, [C] on the 55 3910 exports placed by `objects.py`, section 5.2):**
landmarks (improvements, resources, animated resources) are material 4 (6,139 scene instances on catalogued objects) and
material 6 (140); **never material 3 or 5**. Wonders are **material 3** (VS `c4bb25da`, one non-instanced draw per mesh,
world matrix in VS cb0 rows 0-3: 47 instances in W01-W06) plus material 6 for an animated part (W06's United Nations,
1); none uses material 4. City blocks are material 5 only. So **city pieces and landmarks are told apart by VS**
(`c4bb25da` / `0a3f2230` against `7ec4e9a5` / `88c2b7d4`), contrary to the C1 expectation, **except the skinned class**
(material 6), which serves both and must be split by the identity set. (Display buildings, walls and ruins: none in these
exports as catalogued objects; the save's own cities add 92 material-3 instances in the land frames [I: their display
buildings].)

**The programs 4870 named** (DXBC checksum -> task 2902's index -> task 2903's material table, `programs.csv`; [C]): none
is a BuildingShader program. VS `ec8fba0f` 0x67712A8 = ForkDefaultShader / ForkGlowShader / ForkBrightnessShader /
ForkMaskShader (materials 45-48, passes 0-1) with PS `70ed01e8` 0x6774468 = ForkDefaultShader pass 0; VS `da2382af`
0x677F838 = FXShader / FXShader_CODE (54 and 56, every pass) with PS `a095c878` 0x6788FF0 = FXShader_CODE pass 1 (and
`8d853988` 0x678A7E0 = FXShader_CODE pass 2); VS `e76abcd1` 0x67802B8 = FXShader_Skinned / FXShader_Skinned_CODE (55, 57);
`121efb79` 0x6772B50 = ForkFakeVolumeShader (49); `3c39dd7e` 0x67D2950 = SimpleShader (71). The tree-cut candidate is
wrong: **LandmarkShader, LandmarkShader_Shadow and LandmarkShader_Stencil (materials 60-62) are bound by no draw of the
83 exports**, and no draw enables the stencil test (four exports read in full). *(Replaced 2026-11-18 by 4870: "Not yet
named (4870): ... the tree cut `LandmarkShader_Stencil` is a candidate [I]".)*

### 3.2 What is dropped

- **Only the colour-pass draws into the scene target and the water reflection target** (Architect P2). 4870 (P2,
  section 4): nothing later reads 2419, 2429 or 832; 13415 is read only by the WaterShader; the mask 13383 feeds the
  ShadowShader and then terrain and water, so the shadow-pass draws stay; no draw uses the stencil (LandmarkShader 60-62
  bound by none).
- **The drop rule per draw:** the bound PS is a DROP PS of the class table, the bound VS is its pair's VS (the units'
  second check), the frame's latch has the class's bit, and **the draw's mesh key is in the server's identity set for
  that class** (published by the server through the tap section's control block: the keys its library holds and has
  resident). A draw that fails any test passes and is counted. So an EXE model the server cannot draw (a mod's art, a
  model not yet loaded) is still drawn by the EXE: no hole, at the price of a mixed picture, which the counters name.
- P3b holds (4870: byte-equal for blocks, a landmark, a wonder and two skinned models). Had it failed, the split would
  have been by program only (now known to separate the rigid classes, P1).

### 3.3 Options and mode bits

- `[Renderer] ServerCities` (blocks, walls, world wonders, display buildings, ruins) and `[Renderer] ServerLandmarks`
  (improvements, resources, natural wonders, pontoon bridges), both 0/1, default 0, effective only with
  `ServerRenderer = 1`, read by the proxy and passed to server and compositor as `ServerUnits` is (`proxy_shell.cpp`).
  Ids reserved by the MC (#6034, auditor cycle 10 K4; branches never renumber): **12 `ServerCities`, 13 `ServerLandmarks`**. (Was: "the next free ones at the merge, today 9 and 10".)
- Bits `VPMODE_CITIES` (bit 1) and `VPMODE_LANDMARKS` (bit 2), as reserved in `shell_shared.h`; each set by the server
  only while its module's scene is ready (library loaded, tap frames accepted, renderer OK), latched per frame at
  Present by the compositor; a cleared bit returns the EXE's draws of that class only.

### 3.4 D118 per bit

`VPShellMode` has 20 bytes left (offsets 44-63). Proposed: offset 44 `filterState2` (bits 0-15 the cities state and
reason, bits 16-31 the landmarks', with the units' encoding of `filterState`), 48 `droppedCities`, 52
`droppedLandmarks`, 56 `passedSetCities`, 60 `passedSetLandmarks`. The server's mode writer reads them as
`srv_units_mode.cpp` does: CANNOT_DROP while the bit is held and latched is the D125 step (exit 5, report, popup). The
tap's own failures (a refused frame, no frame within a bound while the bit is set) clear the bit (the EXE draws again)
and, past a bound, take the same step.

### 3.5 Order

None at run time against `ServerUnits` or `ServerFarmFields` (Architect 4): depth decides any mix. `ServerFarmFields`
stays its own mechanism; with it on, the server may place FARM rows' buildings itself later (4881).

## 4. Risks and premises (item 4)

| # | Premise or risk | How it is checked | Decides | State |
|---|---|---|---|---|
| P1 | Programs a small set by hash, disjoint from units, effects, decals, terrain, UI; city and landmark sets disjoint | 4870 (tally done in part, section 5) | the table; the split rule | **holds by VS for rigid models: blocks (PS `c704c80b`/`73daa800`), city pieces (VS `c4bb25da`/`0a3f2230`, material 3) and landmarks (VS `7ec4e9a5`/`88c2b7d4` with PS `f0e1f0a6`/`e12e9eaf`, material 4) disjoint; skinned (material 6) shared by both: split by identity; no unit, effect, tree or UI program bound with them `[C, 4870, 5.1-5.2]`** |
| P2 | Targets; nothing kept reads a dropped draw | 4870 | what is dropped | **holds for scene and reflection: no later draw samples 2419's colour, its depth 2429 or the reflection depth 832 as a resource; 13415 is read only by the WaterShader (t4); the mask 13383 is read by the ShadowShader (into 13386, read by TerrainShader t3 and WaterShader t13), so the mask draws stay; no stencil anywhere `[C, 4 exports, 5.3]`** |
| P3a | Slot 1 holds the world matrices | `unit-draws.md` 8: the instanced VS takes four float4 rows from slot 1 (stride 64) as `x r0 + y r1 + z r2 + r3` | the tap | **holds for materials 4/5 `[C]`; not for the others: material 3 takes its matrix from VS cb0 rows 0-3, material 6 from VS t27 (96-row blocks, row 96 id + 3 bone) through a slot-1 R32_UINT id `[C, 4870, 5.4]`** |
| P3b | A draw's mesh is identified: the index buffer's content (hash at creation with initial data, or at first sight) and the draw's range equal the server's repack of the file | 4870 offline (exports' buffers vs the server's repack); 4871 one live counter | the identity set; two bits | **holds for every class with a file: block set, single landmark, wonder and skinned part equal byte for byte after the pad index (5.5); key = IB hash from index 1 + VB hash + range + t0 (and t1) identity, because two model files can share IB and VB (euro/medit Ancient sets) or IB alone (asia/poly); no file key for the CPU-built meshes (W01's Great Wall, R32 IB) `[C, 4870]`** |
| P3c | The per-draw constants beyond view-projection (lighting set by culture, fog and screen projections cb1[8..12], tints, time) are known or tapped | 4870 (the exports' constant buffers per draw) | the tap's constants | **known: VS cb1 rows 0-3 and 8-12, PS cb1 rows 16-21 (mask PS row 20), one cb1 per (class, pass) and frame for materials 4/5/6; material 3: VS cb0 rows 0-3 per draw and cb1 rows 19-21 per draw `[C, 4870, 5.6]`** |
| P4 | Walls: CPU-bent per city or instanced straight pieces | 4871's walled capture (3907 has no walls) | geometry copy for walls | **CPU-built [L, 4871 at c5h-b, i0ship-e96457fd-ia]: one draw per pass per city on an IMMUTABLE buffer already bent into world space (96,000 B, 3,000 vertices of 32 B, 6,156 R32 indices, identity instance row), on the BLOCK programs (7ec4e9a5 / c704c80b; shadow 88c2b7d4 / 73daa800); the same city refounded gets byte-identical wall content; 12 towers counted by height (fits floor(length / 12) - 1 for a loop of 156-167 units, length not read exactly). So the identity set must keep walls out of the block class until 4879 copies them (the buffer is immutable: a copy at creation) |
| P5 | Path 2 finds these programs' objects | 4874: the create sites offline, one live load (moved from 4871, 2026-11-18) | the recogniser | **offline holds [C, 4874, 10.1]: each of the ten programs has exactly one create site of the 1,417 (all in the 5_0 set of the init 0x4e5c20), push / call / store bytes and the blob's SHA-256 checked in the EXE file; resolved on the stand-in's fake image [L stand-in]; **holds live [L, 10.8]: `model_filter: path 2: 10 of 10 programs resolved` in both loads of 2026-11-18 on c5h-c** |
| P6 | What stock draws at a road across a river | 4871's capture | 4877's size | **a bridge model [L, 4871]: material 3 (VS c4bb25da / PS f0e1f0a6; shadow 0a3f2230 / e12e9eaf), one draw per pass, 270 R32 indices, InstanceCount 1, an IMMUTABLE 3,200 B half-float model-space VB; road and railroad get the same bridge (identical buffers and textures); drawn only when the active team has bridge building (VP TECH_ENGINEERING), else nothing at the crossing. Its file [C, 4877, 2026-11-18]: `Bridges.gr2` (MiscData.fpk), the EXE's fixed name; the draw is its three IND pieces (End2, Mid, End1) CPU-built per crossing into one R32 buffer without the pad element and bent along the crossing, so no library file holds it: child 4879, bit cities (landmark-renderer.md 8). *(Was: "Its file: open (4877)".)* |
| P7 | The filter's and tap's cost per frame on oldpc | 4878, S3's method | acceptance | open |
| P8 | The textures of a draw follow its mesh (no per-instance texture choice) | 4870 | the library | **holds per model file: t0-t9 fixed per mesh range within a frame (1,001 ranges, 4 exports); but 58 of 527 mesh keys are drawn with two t0 from two files of identical geometry, so the texture follows the file, not the mesh bytes `[C, 4870, 5.6]`** |
| R1 | The tap's frame lags the server's frame: a model entering view while panning could be missing for a frame at the screen edge, because the EXE culls before it draws | 4878 live (pan series, counted pixels at the edges); mitigation: keep each tapped instance until its plot leaves the view by a margin | the picture in motion | open `[I]`; the likely cause of 0709's farm-building flashes on drag pans (2026-11-23: under exact lockstep 15,315 of 15,524 server frames drew the previous Present's tap, pans moving 150-370 px per frame; [pan-coherence.md](pan-coherence.md) 5.11) |
| R2 | The Unmap copy on deferred contexts costs more than the units' filter | P7 | acceptance | **measured in part [L, 4874, 10.8, Tiny, workstation]: copying whole buffers cost 2.8 ms per frame (upload memory at about 158 MB/s); with the high-water copy 0.28 ms; the whole tap about 0.58 ms per frame with every family draw tapped; P7 on oldpc and a huge save still 4878's** |
| R3 | A model the server lacks (mods) | the identity set (3.2): passed, counted | holes | designed out |

## 5. P1 in part: the program tally of 2026-11-18 [C]

Command (lead-0561, 2026-11-18 16:33 EST, scratch script `C:/c5-64bit-work/0561/scripts/p1_tally.py`, to be committed
by 4870 into `render/evidence/0561/4870/`): every `draws.json` under `F:/c5-64bit-work/0301/3910/exports` and
`F:/c5-64bit-work/0301/3907/exports`, read in place: 83 exports, 86,276 draws, all `DrawIndexedInstanced`. Material
names from `shell/evidence/0301/3909/shader_programs.csv`. Counts of the BuildingShader programs:

| VS / PS | Draws | Exports | Targets (draws) |
|---|---|---|---|
| `7ec4e9a5` / `f0e1f0a6` | 22,657 | 83 | scene 2419: 21,392; reflection 13415: 1,265 |
| `88c2b7d4` / `e12e9eaf` | 15,820 | 79 | 13383 (shadow mask) |
| `7a9e17f6` / `f0e1f0a6` | 2,324 | 66 | scene 1,873; reflection 451 |
| `88c2b7d4` / `73daa800` | 2,048 | 76 | 13383 |
| `7ec4e9a5` / `c704c80b` | 2,048 | 76 | scene |
| `34b98e63` / `e12e9eaf` | 1,654 | 66 | 13383 |
| `c4bb25da` / `f0e1f0a6` | 245 | 76 | scene 204; 13383 34; reflection 7 |
| `0a3f2230` / `e12e9eaf` | 164 | 76 | 13383 |

- PS `c704c80b` and `73daa800` are bound with no other VS: the city blocks are separable by PS.
- PS `f0e1f0a6` is bound with three VS, and VS `7ec4e9a5` with both `c704c80b` and `f0e1f0a6`: the drop rule must be
  by PS with the VS pair check (as units), and by program alone everything that is not a city block falls in one class.
- 34 draws of PS `f0e1f0a6` (pass 0) go into the shadow mask target 13383 with VS `c4bb25da`: not explained (4870).
- No unit program (`29a0b061`, `82e59838`, PS `60d270e1`, `5def4f99`, `0e7a3566`) is bound with a BuildingShader
  program in any of these draws.

### 5.1 The tally repeated and named (4870, 2026-11-18 16:58-17:45 EST) [C]

Worker-4870 (account B, Opus 5.5), offline, no game run, scripts and outputs in `render/evidence/0561/4870/` (its
`README.md` has the command lines). F: was at its I/O limit (lead-0561), so the full state of every draw was read for
four exports only (3910 `landmarks_land/Fanc01`, `landmarks_land/Lanc07`, `wonders/W01`, 3907 `cities/c_ancient`:
4,460 draws); the 83-export counts come from `draws.json` alone, the placement from a few `state.json` and the stream
files per export.

- `tally.py` gives the lead's counts above exactly (83 exports, 86,276 `DrawIndexedInstanced`, `pairs.csv`).
  `programs.py` names all 102 programs bound in the 83 exports from committed evidence only (the blob's DXBC checksum in
  2902's `shader_index.csv`, its VA in 2903's `material_table.txt`; no EXE read): the BuildingShader digests are the
  materials of section 3.1; the seven programs left open are effect and tree programs (3.1).
- Frame order (median position of a pair's first / last draw in its frame): the mask pass 0.03-0.27, the ShadowShader
  draw that reads it about 0.34 (Fanc01 draw 554 of 1,619), the reflection pass 0.30-0.32, the scene pass 0.56-0.92.
- The 34 colour-program draws into the mask (`c4bb25da` / `f0e1f0a6`, material 3 pass 0, into 13383 with blending on)
  are in 5 exports (3907 `units_land` L07, L08, L10, L12; 3910 W06): in W06 it is one mesh of Broadway (object 1), drawn
  also twice in the scene pass (draws 506 and 649). Why a pass-0 program draws into the mask is not known (a material
  flag of those meshes [I]); 4874 keeps every mask draw, so it changes nothing there.

### 5.2 Every BuildingShader draw placed on 3910's objects [C]

`objects.py` places each draw instance: materials 4/5 from slot 1 (stride 64; the slot's offset is the running sum of
InstanceCount x 64 over the stream's draws in frame order, checked against the first and last draw's `state.json` in all
55 exports: no mismatch); material 6 from VS t27 (below, 5.4); material 3 from VS cb0 rows 0-3. Each instance goes to
the nearest catalogued plot centre (`x = 64 gx + 32 (gy & 1) + 32 - 32 W`, `y = (64 gy + 128) 0.866 - 32 (H + 4)
0.866`, Tiny 56 x 36) within 64 units (128 for a wonder frame's non-block draws: a wonder stands on a plot next to its
city, 22-74 units from the city's plot centre). Result over the 55 exports (`objects_by_class.csv`,
`objects_per_object.csv`): 53,550 draw instances of the family in all passes; 15,935 on catalogued objects, 37,615
the save's own content (its cities and improvements in the revealed area). Scene pass on catalogued objects:

| Objects | material 4 (`7ec4e9a5` / `f0e1f0a6`) | material 3 (`c4bb25da`) | material 6 (`7a9e17f6`) | material 5 (blocks) |
|---|---|---|---|---|
| landmarks, land (SNAPSHOT, RANDOM, SPECIAL, FARM, ANIMATED, ANIMATED_ROTATED) | 6,124 | 0 | 89 | 0 |
| landmarks, sea | 15 | 0 | 51 | 0 |
| wonders (46, one per city) | 6 (3-index draws at exactly 64 units: not the wonder [I]) | 47 | 1 (United Nations) | 2,338 (the cities' blocks) |

- 579 of the 604 objects are drawn. The 25 without a draw: the natural wonders and the reef set by Lua (15: Fanc02-04
  and Sanc01; 3910 knew these were not drawn), the four atolls of Sanc02 (another system: 0389), two oil resources
  (Lanc08 11, 12), the two Moai states (Lanc13, SPECIAL) and the two hidden antiquity sites (Lanc15, Lanc17) [C: no
  family draw within 64 units; what draws them, if anything, not looked at].
- 31 of the 6,248 landmark scene draws carry more than one instance; the rest are one mesh of one model each (a model is
  one draw per mesh, each with its own matrix).
- 3907's four city frames (`objects.py --index shell/evidence/0301/3907/index.csv`): 497 block instances per frame on the
  eight cities, 26 on the save's own; one material-4 instance per frame near a city; no material 3 (no display
  buildings). The African, Polynesian (`CITY_ASIAN_*`), European and Mediterranean Ancient sets are the IB/VB pairs res
  5581, 5700, 5616 and 5648 of c_ancient.

### 5.3 P2: who reads what the family draws [C, the four exports read in full]

Read as a shader resource by a later draw: the mask 13383 by the ShadowShader (material 70, PS t0, one draw into 13386);
13386 by TerrainShader t3 (242-286 draws per export, two terrain passes) and WaterShader t13 (4); the reflection 13415 by WaterShader t4 (5).
Never read: the scene target 2419, its depth 2429, the reflection depth 832. No draw of the four exports enables the
stencil test. So dropping the colour-pass and reflection draws (3.2) removes nothing any later draw reads; the mask draws
must stay (they make the buildings' shadows on terrain and water), as for units.

### 5.4 P3a and the placement inputs per class [C]

- Materials 4/5: slot 1, four float4 rows per instance, as `unit-draws.md` 8 (one stream buffer per frame, res 850,
  131,072 B, appended in frame order). Landmark matrices have scale 1.0; block matrices 0.038 (Fanc01).
- Material 3: no slot 1; the VS reads cb0 rows 0-3 only (2902's reflection: `cb0[0-3]`), a 256-byte per-draw buffer whose
  other rows are not read.
- Material 6: slot 1 is an R32_UINT id (stride 4, a stream shared with the units' skinned draws, res 857); VS t27 (res 860
  for the building parts, not the units' palette) holds blocks of 96 rows: 32 bones x 3 rows of a 3 x 4 world matrix,
  translation in w, **no header rows** (the units' blocks have 98); bone b of id i is rows 96 i + 3 b .. + 2 [C, the VS's
  addressing read 2026-11-18 from task 2902's dump; placements on Lanc07's cow and deer agree]. The tap needs, per
  skinned draw, the palette rows of its ids.

### 5.5 P3b: the identity rule, byte for byte [C]

`identity.py` repacks a `.gr2` (one-off 9012's reader) and compares with the export's buffers; `p3b_all.sh` runs every
check, results `p3b_*.json` (meshes named by their index in the file; no names, export ruling J3). The rule:

- **One index buffer per model file**: one pad index, then the Indices16 of every mesh whose material's shader set is
  BuildingShader or BuildingShader_Skinned, **in the order the file's Models bind them** (`Models[i].MeshBindings` in
  turn; the Meshes array's order fails on `fish.gr2`, whose meshes 3 and 4 are bound in the other order) (LandmarkShader and LandmarkShader_Stencil meshes, e.g. tree
  cuts and decal quads, are in no buffer). **One vertex buffer per set**: rigid 20 B (materials 1/3/4) or 32 B (material
  5 city sets), skinned 28 B. A mesh's draw is (StartIndex = 1 + the indices before it, count, BaseVertex = the vertices
  of its own set before it).
- Vertices: 20 B = half(Position x S), w 0; half(Normal / S), w 0; half(u, v). 32 B = float32 Position, Normal, u, v
  (S 1). 28 B = the units' K1 stream (`k1_probe.py`: three-weight rule, bone indices as stored). Arithmetic: x S and / S
  in double, then float32, then to the nearest half **with ties away from zero** (ties to even misses 4-31 vertices per
  sample). S = 1 for the mine; the WonderArtInfo `fScale` for wonders (Colossus 0.05); 0.12 for the deer (its landmark
  scale [I: which column; the value fits exactly]).
- Equal byte for byte (IB after the pad, the whole VB): PL_MED mine iron heavy (Fanc01 object 9, 18 meshes, 36 draws), the fish with its seagulls (Sanc01 objects 1 and 5, S 0.07, all five meshes
  skinned),
  the Colossus (W01 draw 156, 6 draws), the deer's three animals (Lanc07 object 12, 12 draws; the Expansion2Models copy:
  the MiscData copy of `resource_deer.gr2` differs in 761 vertices, so the library must take the archive the game
  loads), the African, European, Mediterranean and Polynesian Ancient city sets (c_ancient, 40-88 draws each).
- **The pad index is not stable**: two loads of one model carry different pads (5 of the 24 buffer sizes seen more than
  once in the four exports); hash the IB from index 1.
- **Collisions** (`collisions.py`, `texture_variants.py`, on the four exports): 106 index buffers, 61 distinct whole,
  54 distinct from index 1, 56 (IB, VB) pairs, 527 scene mesh keys (IB, VB, StartIndex, count, BaseVertex). Two IB
  hashes meet two VBs (asia_anc and poly_anc share every index); 58 mesh keys are drawn with two different t0 from two
  buffer pairs of identical content (20 the European / Mediterranean Ancient sets, 38 other material-4 models). So the
  key is (IB from 1, VB, range) **plus the t0 and t1 identity** (a content hash of the texture at creation); with it
  every sample class is identified. How many of the library's models the key tells apart over the whole catalogue is
  4872's count (the library holds every file).
- No file key: the four R32 draws of W01 (70 / 469 block programs, 3,048 vertices already in world space at the Great
  Wall city; 134 / 617 material 3, 54,876 vertices, a box of +-226 x +-174 at (1087, -417)): CPU-built meshes (5.7);
  the identity set leaves them to the EXE.
- worker-4872's report (#6038, its marks): `fish.gr2`'s second seagull "differs from file x S", the EXE's first
  vertices close to the other seagull's. Checked here [C]: the two seagull meshes have identical index lists (510 each);
  in Meshes order the VB misses 3,013 vertices, in the Models' binding order (mesh 1 before mesh 0, mesh 4 before mesh 3)
  the IB and the whole 28-B VB of Sanc01's fish (IB res 39872, 20 draws) are equal byte for byte at fScale 0.07. Not a
  bone-space problem: the order rule above.

### 5.6 P3c and P8: constants and textures [C, the four exports]

- Constants read (2902's reflection): VS `7ec4e9a5`, `c4bb25da`, `7a9e17f6` cb1 rows 0-3 (view-projection) and 8-12;
  mask VS cb1 rows 0-3; PS `f0e1f0a6` and `c704c80b` cb1 rows 16-21; mask PS `e12e9eaf` row 20; `73daa800` none;
  material 3 also cb0 rows 0-3.
- In one frame there is **one cb1 per (program pair, pass)** for materials 4, 5 and 6. Blocks and material 3 differ from
  material 4 in rows 19-21 only (components 19.w, 20.yzw, 21.yzw on 3910; 19.w, 20.z, 21.w on c_ancient); passes differ
  in rows 0-3 too. Material 6 shares material 4's cb1. Material 3 varies per draw (W01: 8 cb0 and 2 cb1 versions among its mask
  draws). Between frames rows
  3, 7, 8, 9, 11, 12, 16, 17 and 19-21 change. So the tap carries per frame the cb1 of each (class, pass) and, per
  material-3 draw, its cb0 rows 0-3 and its cb1; what rows 19-21 mean is not read [I: material constants].
- Textures: within a frame a mesh key binds the same t0-t9 in every draw (1,001 scene ranges; the only variation is a
  stale t27 that PS `f0e1f0a6` does not read). t5-t9 are one per frame, t2-t4 4-7 per frame, t0/t1 per model file; no
  per-instance or per-player texture (instanced draws bind one set for all instances; no team colour, `unit-draws.md`
  8). P8 holds per file, not per mesh bytes (5.5).

### 5.7 P4 as far as 3907/3910 go [C positions; I meaning]

Neither catalogue has a walled city. The only CPU-built family meshes in the 83 exports are W01's four R32 draws: draws
70 (mask) and 469 (scene) with the block programs, a 3,048-vertex 32-B buffer in world coordinates (identity instance
matrix) spanning x 1186-1250, y -526 to -470, z 8.7-20.7 around plot (46, 9), the city holding BUILDING_GREAT_WALL;
draws 134 / 617 with material 3, cb0 translation (1087, -417), local box +-226 x +-174, z 8.4-34 (about 7 x 6 plots):
the Great Wall itself [I]. So walls-like geometry is built on the CPU per city in world space with the block programs
(as `cities-and-landmarks.md` 1.13 expects) [I]; the tap would have to copy its buffer, or the identity set leaves it to
the EXE. 4871's walled city decides.

### 5.8 Not checked by 4870

- P2, P3c, P8 and the collision counts rest on the five exports read in full (Fanc01, Lanc07, Sanc01, W01, c_ancient);
  the other 78 exports gave counts and placements only. The pad index is never drawn (every R16 BuildingShader draw of
  those exports starts at index 1 or later), but no draw of the other 78 was checked for that.
- The placement's plot-to-world formula and the 64 / 128-unit radii are a nearest-plot rule, not the EXE's own
  assignment; 6 wonder-frame instances at exactly 64 units are probably not the wonder.
- The meaning of cb1 rows 19-21 and of rows 8-12, why material 3's pass-0 program draws into the mask (5.1), which
  database column gives the deer its 0.12, what (if anything) draws the 25 objects without a family draw, and that the
  R32 meshes of W01 are walls-system geometry and the Great Wall [I].
- The catalogue-wide count of models the key tells apart (4872's library holds every file).
- 3907's four era frames draw the same block counts per city (as 0337 found); whether they draw the era sets was not
  looked at here.

## 6. Definition of done: the tolerance (proposed)

- **Offline** (4873/4878, every BuildingShader object of 3910's 55 frames and 3907's four city frames): the server's
  draw from the capture's own tap frame against the captured scene target at the same camera, on the object's pixels:
  median <= 2, p95 <= 8 (8-bit), max and a difference image reported; >= 97 % of pixels within 16 per frame and >= 75 %
  per object (the drawn-whole numbers of COMMON 25).
- **Live** (4878): `tools/compare/model_checks.py` drawn-whole gate on the joined frame against its passthrough at every
  camera; the `models` class reported beside stock's two-session ceiling; D112 acceptance pictures for each mix.

## 7. Questions for the MC (C1)

- Q1: the `own` layout (4880, 4881) leaves this epic's definition of done and is parked as children for the `own`
  source and true absence. Recommended: yes (Architect 2: off the path).
- Q2: the split of the two bits by the tap's identity set (3.2), one option and one bit only if P3b fails. Recommended:
  yes.
- Q3: files outside touches to add: `shell/composite/unit_filter.{cpp,h}` (one recogniser: the draw hooks exist once),
  `shell/composite/shell_composite.{cpp,ini}`, `shell/common/shell_shared.h` (mode block 3.4), new
  `shell/common/city_programs.h` and `shell/common/shell_model_tap.h`, `shell/tests/` (new test files and
  `unit_filter_host`), `shell/build.cmd` if a file is added, `options/` (two options), `proxy/src/proxy_shell.{cpp,h}`
  (the option lines), `docs/OPTIONS.md`, `server/src/feed/srv_feed.h` and the feed's writer (kind 34),
  `server/src/render/srv_scene_bundle.{cpp,h}`, `srv_render_scene.{cpp,h}`, `srv_render_options.cpp`,
  `srv_render_host.cpp` (the mode writer's fatal step), `server/CMakeLists.txt` / `render/CMakeLists.txt` for the new
  folders, `tools/compare/` only if model_checks needs a class, and `research/05-graphics-composition/README.md`
  (one line).
- Q4: the tolerance of section 6. Recommended as written.
- Q5: may 4870 (offline premises, evidence scripts only, no product code) start before the C1 answer, since the
  Architect wants P1-P4 answered before C1 is accepted? Recommended: yes.
- Q6: animated landmarks (4876): the EXE's palette from the tap (exact, 2-3 h, keeps the EXE's animation as a part of
  the product while state (ii) lasts) or the units' pose layer (3-6 h, the brief's text, needed for true absence).
  Sheet 4.6 calls the lasting use of the EXE's animation the owner's decision. Recommended: the palette in state (ii), the
  pose layer with `own`; put to the owner if the MC agrees.

## 8. Not verified

- Everything in sections 1-4 is a plan; only section 5 is measured.
- ~~That wonders, walls and display buildings bind PS `f0e1f0a6`; that the server's repack equals the EXE's buffers
  (P3b).~~ Answered by 4870 (sections 3.1, 4, 5; 2026-11-18).


## 9. Captures of 4871 (P4, P6), folded in by lead-0561 2026-11-18

Setup and numbers: `render/evidence/0561/4871/README.md` (worker-4871; c5h-b on C:, the pinned DLL
`i0ship-e96457fd-ia`, save copy `C5H-0301-4871_0009` of `CAT2_0009`; captures kept on
`C:/c5-64bit-work/0561/4871/`, 1.3 GB, not committed). Results as in the P4 and P6 rows of section 4, and:

- Damage smoke: draws of VS `da2382af` / PS `fd6f5c00`: 3 at damage level 1, 13 at level 3, none without damage
  [L, 4871; that these are the smoke effects is I]. They are effects, not BuildingShader draws: they stay the EXE's.
- Trap for every picture round: a road or farm field set by Lua is drawn only after a zoom re-bakes the terrain
  [L, 4871]; the first take at minimum zoom drew no route (superseded). Also for `scene-catalogue.md` 11.
- Not verified: which pieces are exactly the towers; the loop length; bridges on another player's plot, pillaged or
  in other eras; gates; walls of other cultures; a grey patch at (39,10).


## 10. The compositor's filter and tap (ticket 4874)

Written 2026-11-18 by worker-4874 (account B, Opus 5.5) on `task-4874-model-filter-tap`. Code:
`shell/composite/unit_filter.{cpp,h}` (the units' recogniser, generalised), `shell/composite/shell_composite.cpp`
(four hooks, the keys), `shell/composite/vtable_index.c` (four slots), `shell/common/shell_model_scene.h` (the program
table), `shell/common/shell_model_tap.h` (the sections, the frame image = the `.mtap` file form, the hash; the one
definition, lead2-0561 #6099), `shell/common/shell_shared.h` (`VPShellMode` offsets 44-63). Stand-in:
`shell/tests/unit_filter_host.cpp --models`, `shell/tests/test_model_filter.py`, `shell/tests/test_model_programs_table.py`.
Evidence scripts and outputs: `render/evidence/0561/4874/`. Marks: `[C]` read in code or data, `[L stand-in]` the
stand-in host, `[L]` the game, `[I]` inferred.

### 10.1 P5 offline: the create sites [C]

`p5_sites.py` (2026-11-18 18:05 EST) reads `render/src/shaders/create_sites_table.inc`, the llvm-objdump listing of build
4390913 (`<work folder>/2900/2903/dx11.asm`) and the EXE file read-only (a Goldberg copy whose `.text` equals the
Steam EXE's, `../02-exe-internals/addresses.md`), output `p5_sites.txt` (numbers and digests only, D83):

It finds the create sites of the 10 programs of the model classes: six vertex and four pixel programs for
materials 1 and 3 to 6, five in the colour pass and five in the mask pass (the materials per program are in
`shell/common/shell_model_scene.h`). All ten sites lie in the 5_0 part of the shader set-up function 0x4E5C20,
between 0x4EBB91 and 0x4EBCF1, and each stores its 16-bit handle into one of ten handle globals in 0x2DE1580-0x2DE15AC.
The blob VAs and the push, call and store sites of each are in the generated create-site table
(`render/src/shaders/create_sites_table.inc`), in path 2's site rows (`shell/composite/unit_filter.cpp`, symbols in
`exebuild/symbols/shell.tsv`) and in the stand-in's table (`shell/tests/unit_filter_host.cpp`); the program digests are in
`shell/common/shell_model_scene.h`. (D83 edit 2026-11-25, 0772: a 10-row create-site table from `p5_sites.py` was
replaced by this description, as 4718 did for unit-draw-filter.md.)

Each program has exactly one create site among the table's 1,417 rows (its digest at no other site of any set); at
those VAs the EXE pushes the blob's address, calls its create function 0x8BBC40 (VS) / 0x8BBBF0 (PS) and stores the
16-bit result into the handle global (D83 edit 2026-11-26, 0773: the instruction bytes of these three steps were
replaced by this description in words, as 0772 did); the blob's SHA-256 (DXBC size at +24) equals the 3910 capture's shader file of the same name. The full digests are in
`shell_model_scene.h`, generated from this output; `test_model_programs_table.py` checks the header, the site rows of
`unit_filter.cpp` and 4870's tables against each other (and 8 mutations). Path 2 checks these ten as a **group of its
own** (unit-draw-filter.md 3.2's checks, all or nothing): a refusal turns the model filter off and never the units' (and
the units' refusal never the models'); the shared checks (instruction patterns, shader level 10, device, renderer)
refuse both.

### 10.2 The identity premise: what the EXE creates with initial data [C, 4 exports]

`family_creates.py` over the capture XML of Fanc01, W01, Lanc07 (3910) and c_ancient (3907), for the resources the
family's colour draws bind (output `family_creates.txt`):

- Every index buffer and every slot-0 vertex buffer is `IMMUTABLE` and created with initial data (Fanc01 41 / 42, W01
  20 / 20, Lanc07 36 / 37, c_ancient 9 / 9). Every PS t0 and t1 is a `DEFAULT` texture created with initial data
  (Fanc01 44 / 34, W01 22 / 22, Lanc07 40 / 30, c_ancient 10 / 10). So the key's hashes are taken at `CreateBuffer` /
  `CreateTexture2D` and never need a read-back. (4871, section 9: the CPU-built walls are `IMMUTABLE` too, so they get a
  key at creation; they stay out of the set until 4879.)
- What the tap copies is `DYNAMIC` and written by `Map(WRITE_DISCARD)` only: slot 1 (850 the 64-B stream, 857 the skinned
  ids), VS t27's buffer and the four to six cb1 buffers **once per frame on the immediate context** (Fanc01 chunk 1643:
  res 850, 131,072 B written whole); VS cb0 is **one 256-B buffer mapped about 300 times per frame on the deferred
  contexts** (Fanc01 325, W01 297, Lanc07 323, c_ancient 315 Unmaps), once before each draw that reads it. No
  `UpdateSubresource` of them.
- Not checked: whether other saves or the live game create a family buffer without initial data (the live load counts
  `buf_noinit` / `tex_noinit`, 10.8).

### 10.3 The drop rule (as built)

A draw whose bound (VS, PS) pair is one of `kVPModelDropPairs` (block, single, instanced, skinned), through
`DrawIndexedInstanced` or `DrawIndexed`, is dropped when all of these hold, else passed and counted:

1. a depth view is bound (the scene 2419/2429 and the reflection 13415/832 have one; the shadow mask 13383 has none
   [C, Fanc01 state.json], so 4870's 34 colour-program draws into the mask pass);
2. the mesh key is complete: IB R16 or R32 at an offset that is a multiple of the index size, slot-0 VB at a multiple of
   its stride, both with an identity (hashed at creation or read back once, 10.5), t0 and t1 too (or not bound);
3. the key is in the live server's identity set, for the class's bit: block and single = cities, instanced = landmarks,
   skinned = the set entry's one bit (an entry's `cls` other than "any" must match too);
4. the latch of this frame holds that bit (`model_filter=on`: both bits, TEST ONLY);
5. the tap record is complete (10.5): every buffer it copies has a copy covering its range, the bounds hold, the frame's
   budget has room (lead2-0561 #6099 (c): no drop without a record);
6. the bound shaders read back equal the tracked pair (the units' rule: a stale track may pass a draw, never drop one).

`Draw`, `DrawInstanced` and the indirect entries never drop a model draw (no index range: no key; counted `entry`).
Counters per context (`modelfilter 1s:` / `totals:`): seen per class, dropped and passed-while-latched per bit,
`tapped`, `not_in_set`, `no_dsv`, `unhashed`, `no_shadow`, `budget`, `nomem`, `mismatch`, `entry`, `bound`, `idle`
(nothing latched or wanted: no work but the counter), `keep` (mask pass or a foreign pairing); per model program
`c/m/r/f/s/d/p` as the units'; the creation counts `buf_init`, `buf_noinit`, `tex_init`, `tex_noinit`, `tex_noid`, the
hash, copy, draw and publish times.

### 10.4 The latch and the mode block

Bits 1 and 2 are latched with bit 0, from the same word and the same liveness (`unit-draw-filter.md` 3.1, ring or v3);
the control thread clears them with bit 0 when no Present came for 1 s. `VPShellMode` offsets 44-63 stay
`reserved[0..4]`, read and written through accessors (`VPMODE_FILTERSTATE2(m)` and four more; 4761's test addresses
`reserved[4]`, and a nameless union broke render's MSVC `/W4 /WX` build with C4201, #6270; corrected 2026-11-18, was a
union): 44 `filterState2` (cities bits 0-15, landmarks 16-31, `filterState`'s encoding), 48 `droppedCities`, 52
`droppedLandmarks`, 56 `passedSetCities`, 60 `passedSetLandmarks`, written at every Present before `latchedWord`. Per bit, in this order: `model_filter=off` -> cannot
(5); the model filter off for the process -> cannot (its reason: 1 its path 2 refused, 3 an exception, 6 the test table,
7 the mark table, 10 the tap failed); `hook_models=0`, `hook_units=0` or no hooked call after two Presents -> cannot (8);
no model DROP PS recognised -> cannot (2 path 2 not run, 4 none); **the bit asked (latched, or `model_filter=on`) and no
identity set of this server applied -> cannot (9)**; not reported before the first hooked call; else can drop. A cannot
while the latch asks is LOUD once per bit (`model_filter: LOUD: the latch asks for drops ...`) and counted
(`frames_unable_c` / `_l`). Reasons 9 and 10 are `VPMFILTER_R_*` (`VPFILTER_R_COUNT` stays 9 for the units' reader).
`passedSet*` is expected above 0 (a model the server lacks is drawn by the EXE, section 3.2), unlike the units'.

For 4875: set a bit only after `VPShellModelTapControl::setGenerationApplied` equals the generation just published (else
reason 9 is reported in the frames between, which the mode writer's D125 step would read as fatal).

### 10.5 The tap (as built)

- **Identity at creation** (hooks `CreateBuffer`, `CreateTexture2D`, kill switch `hook_models`): an IMMUTABLE buffer
  bound as VB or IB created with initial data gets VB = `VPModelHash64` of all bytes, IB = the hash from byte 2 (R16) and
  from byte 4 (R32) (the index size is known only at the bind); a DEFAULT or IMMUTABLE shader-resource texture without
  RT, depth or UAV binding created with initial data gets `VPModelTextureHash` of the first mip with both sides <= 64 (or
  its last mip), array slice 0, tight rows, the bytes as passed (#6146; BGRA rule #6270). Only content that cannot change
  gets an identity (#6273): DYNAMIC and DEFAULT buffers never; an `UpdateSubresource`, `CopyResource` or
  `CopySubresourceRegion` into a texture with an identity clears it (hooks of their own, `write_cleared`). A table of
  65,536 entries keyed by the object's address, overwritten at every creation (a creation without identity at a known
  address clears it), read lock-free (a sequence per entry).
- **The read-back** (#6273; the ticket's "else once per resource at first sight"): a key part not in the table at all
  (created before the composite's hooks: load 2 of 10.8 found textures and some meshes so) is AddRef'd and queued at the
  draw, which passes (`uh_ib_miss`, `uh_vb_miss`, `uh_tex_miss`); at the next Present the immediate context copies it to
  a staging resource (a buffer whole, a texture only its identity mip of slice 0), a later Present maps it with
  `DO_NOT_WAIT`, hashes it with the creation path's functions, enters it and releases both; at most 64 copies and 32 MB
  per Present, never retried, never a stall (`rb_new`, `rb_hashed`, `rb_failed`, `rb_notkeyable`, `rb_kb`). Same
  identity as at creation by construction (stand-in: `test_read_back_of_parts_created_before_the_hooks`, 18 of 18).
- **The Unmap copy** (hooks `Map`, `Unmap`): a buffer a tapped draw read is marked (slot 1, VS cb0, VS cb1, PS cb1, VS
  t27's buffer) with a high-water mark of the bytes the draws read; from then on every `WRITE`, `WRITE_DISCARD` or
  `WRITE_NO_OVERWRITE` map of it on any context copies `[0, mark + mark/4 + 1 KB)` (at most the buffer) at its `Unmap`,
  before the call, into a copy per (context, buffer); a draw past the copy passes (`no_shadow`) and raises the mark
  (live load 1 copied whole buffers: 2.8 ms per frame; load 2 with the mark: 0.28 ms). A draw reads its own deferred context's copy
  when that context mapped the buffer since its last `FinishCommandList`, else the immediate context's: at once on the
  immediate context, and **at the list's `ExecuteCommandList`** on a deferred one (so a list recorded before the
  immediate context's Maps still gets that frame's contents; `test_deferred_list_recorded_before_the_maps`). The first
  frame after a buffer is marked has no copy, and the skinned palette's mark is known a frame after the ids: those draws
  pass (`no_shadow`). The copy reads the mapped memory the EXE wrote (upload memory: about 158 MB/s in load 1, R2).
  (Corrected 2026-11-24, ticket 0719: since 0719 a `WRITE_DISCARD` map of a DYNAMIC non-constant buffer hands the EXE a
  cached staging whose written pages the `Unmap` forwards to the driver, and the copy reads the staging; the pages a map
  did not write below the draws' mark still come from the driver. Was: every copy read the mapped driver memory.
  frame-cost-exe-side.md 5.)
- **Records**: one list per deferred context while it records, moved to its command list at `FinishCommandList`, placed
  into the frame at `ExecuteCommandList` (the immediate context's records first); never-executed lists are freed after
  8 Presents. Constants deduplicated per frame by content.
- **Publication** at the Present: `Local\VPShellModelTap.<pid>` (control, created by the compositor) and
  `Local\VPShellModelTapData.<pid>` (3 slots of `model_tap_slot_kb`, default 4,096 KB), frame n in slot n % 3 through a
  short view; frames are published while something is tapped. The budget per frame is half a slot, reserved at each
  record, so a frame always fits; a draw beyond it passes (`budget`).
- **The identity set**: `Local\VPShellModelSet.<pid>`, created by the server; read when the control block's
  `setGeneration` changes, whole or not at all (seq, magic, version, sizes, generation, server pid, hash), into a table
  swapped at the Present; any failure leaves no set (no drop) and is named once (`model_filter: identity set generation N
  of server P: <result>`); a set of another server or generation is dropped at once.
- **Skinned deferred draws** reading the immediate context's ids and palette: the ids are checked against the palette's
  copy as it is at the record, and again at the placement; a miss there writes zero rows and counts `place_miss` (LOUD
  once) [I: not staged on the stand-in; the EXE writes ids and palette once per frame before executing, 10.2].

### 10.6 Keys (`shell_composite.ini` and the built-in defaults; rows in `test_config_defaults.py`)

`model_filter=auto|on|off` (on/off TEST ONLY, logged; a bad value is a CONFIG ERROR and auto), `hook_models=1`,
`model_tap_passed=0` (TEST ONLY), `model_tap_slot_kb=4096` (a multiple of 64 in 256..16,384, else CONFIG ERROR and
4,096). Test-only keys read by `unit_filter.cpp`: `model_filter_fault=draw|map|create|present|execute`,
`model_filter_set=all` (every fully hashed key counts as in the set: the live check without a server; LOUD), and
`unit_filter_table` may carry indices 12..21 (all ten or none, else refused whole).

### 10.7 Stand-in results [L stand-in]

Final builds 2026-11-18 (`shell/build.cmd` and `shell/build.cmd asan`, `C5_SHELL_BUILD` under
`C:/c5-64bit-work/0561/4874/`, the sources of commit 8f6ca81ec; the plain composite SHA-256 `199da0d6...`, the one of live
load 2). Runs (`PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 python -I -m pytest ...` through `quietlock.py run`):

- ASan, 20:05-20:11 EST: `test_model_filter.py`, `test_unit_filter.py`, `test_config_defaults.py`: **95 passed**
  (`conftest.py` fails a case on any ASan report, so 0 reports).
- plain, 20:12-20:19 EST: the same plus `test_tagged.py` and `test_model_programs_table.py`: **116 passed**. The units'
  suite is unchanged and green (one of its cases caught a first version whose model reason text "path 2 not run" also
  matched the units' line count; renamed). Earlier runs on the 19:07 build: plain 113, ASan 93, all passed.

What `test_model_filter.py` (38 cases) shows, each with the cells read back, the host's record check, `filterState2`,
the counters, the log lines and the host's exit code:

- each bit alone and both, with the units' bit too: exactly the latched classes' cells drop on both contexts; every
  frame's DROPPED records equal the dropped cells one for one and carry the drawn instance rows (2 block instances, 1
  landmark), cb0 rows of the drawing context (immediate and deferred differ), the palette rows of each skinned id, the
  cb1 content and the t2-t4 identities; cells 5 (not in the set), 6 (same buffers and range, another t0: the euro/medit
  case), 7 (no depth view) and 8 (mask pass) always pass; the rigid cells drop from frame 2 (frame 1's draws mark the
  buffers, frame 2's Unmaps are copied), the skinned ones from frame 3 (the palette's high-water mark is known once the
  ids are), a frame earlier with `model_filter=on` or a deferred list recorded before the Maps; the mode block's per-bit
  counts equal the dropped cells read back;
- the word cleared mid-frame: the drops stop at the next frame;
- the read-back (`--early`: meshes and textures created before the composite): the first draws pass (`uh_*_miss`), 18
  of 18 parts read back and hashed, then every latched cell drops with complete records; a later `UpdateSubresource`
  into t0: its identity cleared (`write_cleared`), its cells pass from that frame, nothing read back again;
- `wantBits` without the mode bit and `model_tap_passed=1`: records, no drop;
- the budget (`model_tap_slot_kb=256`, a block cell of 4,000 instances): that cell passes (`budget`), every other drops;
- the identity set: none, torn, bad hash, bad magic, another server's pid, another version, bounds, another generation:
  nothing dropped, `filterState2` 0x09020902, two LOUD lines (one per bit), the result named; published late: drops
  from the frame after;
- `model_filter=off|on`, bad values of `model_filter` and `model_tap_slot_kb` (CONFIG ERROR, defaults used),
  `hook_models=0` (reason 8, no creation hashed), `model_filter_set=all`;
- an access violation in each model path (draw, map, create, present, execute): one LOUD exception line, both filters
  off (reason 3 in `filterState` and both halves of `filterState2`), the composite not disabled;
- a test table with nine of the ten model digests: refused whole (reason 6 for both filters);
- path 2 on the fake image: 10 of 10 resolved and 12 of 12 for the units; a model blob or a model handle store broken:
  the models refused (LOUD once per bit), the units still resolved and dropping; the shader level broken: both refused.

Costs on the stand-in (an indication only, PERFORMANCE.md: the workstation, other jobs running; `test_each_bit` with
both bits, 8 frames plus the 1.5 s hold): 84 tapped draws took 0.25 ms in all (about 3 us each, the COM reads of the
bound state included); 71 publications 4.24 ms (about 60 us each: a view of the 4-MB slot, the copy, the hash); 49 KB
hashed at creation in 0.09 ms; 33 KB copied at Unmap in 0.11 ms. The live load (10.8) and 4878's P7 on oldpc give the
numbers a port can use.


### 10.8 P5 live (c5h-c, 2026-11-18) [L]

Setup: the claimed Goldberg instance c5h-c (workstation, `mods548`, Vibrant Textures present, D94), the product path of
3308's prep through `render/evidence/0561/4874/chain_4874.sh` (4757's driver: layout installed and backed up by hash,
VP64Options.ini `[Renderer] ServerRenderer = 1, ServerUnits = 0`, config.ini the product's D33 list), layout = a copy of
the pinned `x64-c29442e8-p58724e0` (x64 DLL `2B56A1CD...`, server.exe `FC73E60E...`, proxy `3DFD7521...`, never edited) with
this branch's composite; the Tiny copy `C5H-4874-TINY_0018` of `C5H-3000-TINY-T0018` (E337244C, turn 18); composite
TEST-ONLY keys `model_filter=on`, `model_tap_passed=1` (load 2 also `model_filter_set=all`). Restored by SHA-256 after
each load (`backup_restore_ok` true, `provision verify` 0); no box, no dump.

**Load 1** (19:28-19:32 EST, composite `21a5a38e...` of 19:07, stopped by the stop file after the load picture because
that build lacked `model_filter_set`; 2,412 frames in about 95 s):

- **P5 holds live**: `model_filter: path 2: 10 of 10 programs resolved from the EXE's renderer (image 0x005C0000, shader
  level 10, ...)` at the first Present after the EXE validation, beside the units' `12 of 12`. So the create sites and
  handle globals of 10.1 are what the running game uses (the code bytes compared read as on disk).
- Seen per class over the run: block 35,835, single 2,389, instanced 64,600, skinned 4,778, mask pass 71,767; every one
  tapped (`model_tap_passed=1`) and passed; no mismatch, no place miss, nothing lost.
- **Every tapped draw had an incomplete key** (`unhashed` 107,562 for 107,095 tapped) although the creation hooks saw
  2,763 vertex or index buffers and 1,908 textures with initial data (25 buffers and 75 textures without; 310 textures
  whose format has no row size in `VPModelFormatBlock`). Which part was missing was not counted in this build: load 2
  counts it per part and cause and runs the read-back (10.5).
- **R2 is real**: copying whole buffers at Unmap read 1,062,432 KB in 6,732 ms over the run: about 440 KB and 2.8 ms per
  frame, about 158 MB/s (upload memory read back by the CPU). Fixed by the high-water copy (10.5).
- Other costs: 107,095 tapped draws in 318 ms (3.0 us each, the COM reads of the bound state included: about 0.13 ms
  per frame at 45 draws); 2,412 publications in 254 ms (0.105 ms per frame); 181,713 KB hashed at creation in 87 ms during
  the load (about 2.1 GB/s).

**Load 2** (19:55-20:00 EST, composite `199da0d6...` of commit 8f6ca81ec: the read-back, the high-water copy, the per-part
counters, `model_filter_set=all`; load to in-game 139.5 s; 3,756 frames; 2 AI turns, 18 -> 20, 6.9 s and 6.2 s;
lead2-0561's grant #6264 under MC-B's of 19:17):

- path 2 again `10 of 10` (image 0x00F00000) and the units' `12 of 12`; `filterState2` 0x10001 (both bits can drop) from
  the first Present; LOUD lines: only the test key's own.
- **Which parts were missing**: the creation table lacked textures (`uh_tex_miss` 187 draws) and some meshes (`uh_ib_miss`
  32, `uh_vb_miss` 32), i.e. resources created **before the composite's hooks** (the first three logged keys: a landmark's
  t1 `BC1` 256 x 256 9 mips and a t0 `BC2` 128 x 128 8 mips "miss", their IB, VB and the other t0 "in"); no part was in the
  table without a hash (`uh_*_other` 0, `uh_tex_noid` 0). The read-back closed them: 31 resources read back once (307 KB,
  0 failed, 0 not keyable), after which only 58 of 184,038 tapped draws had an incomplete key. One identity was cleared by
  a later write (`write_cleared` 1). Creation counts this load: 2,615 IMMUTABLE vertex or index buffers and 1,929 textures
  hashed at creation (8 shader-resource textures without initial data; 314 with a format without a row size), 170 MB in
  83 ms.
- **The drops with the test keys**: 184,038 family colour draws tapped and, with every hashed key counted as in the set,
  dropped 59,536 under cities and 124,492 under landmarks; passed while latched 514 (the warm-up frames, the 58 incomplete
  keys, `no_shadow` 553 over the run); no mismatch, no place miss, no budget refusal, nothing lost. The DIAGNOSTIC picture
  (`vc-20261118-195853-workstation-ef54`, `C:/c5-64bit-work/0561/4874/live/pics/tiny-base-r1/base_load.png`, not committed)
  shows Madrid without its city blocks and no improvement or resource models (the resource icons are the UI's), terrain,
  forests, units and the interface as stock: what the keys ask for. Per program: VS `7ec4e9a5` seen 157,219 dropped 156,712;
  PS `c704c80b` 55,995 / 55,815; PS `f0e1f0a6` 128,596 / 128,213; VS `c4bb25da` 3,733 / 3,721; VS `7a9e17f6` 23,639 /
  23,595; the five mask programs seen and never dropped.
- **Costs (an indication: one workstation load, other jobs running; PERFORMANCE.md: P7 is 4878's on oldpc)**: the Unmap
  copy 321,308 KB in 1,056 ms over 3,756 frames = **85 KB and 0.28 ms per frame** (load 1: 440 KB and 2.8 ms); the draw
  path 668 ms for 184,038 tapped draws = 3.6 us per draw, 0.18 ms per frame; the publication 433 ms = 0.115 ms per frame;
  together about 0.58 ms per frame on this Tiny save with everything tapped.
- Not verified live: the identity set itself (no server publishes one yet: 4875); a deferred skinned draw's placement
  (`place_miss` 0 here, but no deferred skinned list was told apart); a huge save (4878).


## 11. The server's side (ticket 4875, worker-4875, 2026-11-18)

Branch `task-4875-server-model-side` (team branch `epic-0561-cities-and-landmarks` at 93759fd86 merged). Layout:
`shell/common/shell_model_tap.h` (4874, 72e32aefe). The model side: 4873's `c5r/landmarks/tap.h` and `draw.h`
(task-4873-draw-layer-harness, read at 450af292e and 23aa402db, not merged into this branch: #6145). Everything here
is offline; no game ran. [C] below = checked by worker-4875 in the stand-in suites of 11.10; [I] = not run.

### 11.1 Files

| File | What |
|---|---|
| `server/src/render/landmarks/srv_model_tap.{h,cpp}` | `ModelTapReader` (the frames), `ModelSetPublisher` and `BuildIdentitySet` (the identity set), `CheckModelSet` (the compositor's check, for tests) |
| `server/src/render/cities/srv_models_mode.{h,cpp}` | `ModelModeWriter`: bits 1 and 2 of the mode word, D118 per bit |
| `server/src/render/landmarks/srv_models_scene.{h,cpp}` | `ModelsScene` (what `Scene` calls) and the `ModelLayer` seam; both made by factories registered at static initialisation (the units' pattern), so every program that links `srv_render_scene.cpp` without these folders builds as before and refuses `render_cities=1` |
| `server/src/render/landmarks/srv_model_layer_c5r.cpp` | the layer on `c5r::landmarks` (library, `WireIndex`, `readTap`, `resolve`, `ModelRenderer`); compiles to nothing until `tap.h` and `draw.h` are on the branch (`__has_include`), so until 4873 merges a server with `render_cities=1` takes the D118 route ("no model layer") |
| `srv_render_scene.{h,cpp}`, `srv_render_host.cpp` | `Scene::configureModels`, poll, idle, the draws, `ModelsFatalStep`; the server.cfg keys of 11.5 |
| `srv_scene_bundle.{h,cpp}`, `srv_scene_recorder.{h,cpp}`, `srv_scene_sources.{h,cpp}`, `server/tools/sceneshot/sceneshot.cpp` | the bundle member (11.6) |
| `options/`, `proxy/src/proxy_shell.cpp` | options 12 / 13 and their server.cfg lines (11.5) |
| `server/src/feed/srv_feed.{h,cpp}`, `server/src/remote/srv_remote_main.cpp` | the database copy for the model library (11.4) |
| `server/tests/render/cities/` | `models_test.exe` (19 cases) and `test_models.py` |

### 11.2 The tap reader (copy-then-check)

The reader maps the control block read-write (the consumer fields are the server's) and the frame section read-only,
whole, once. Per loop iteration: a copy of the control block under its seqlock (16 tries; odd or changing = `busy`,
read again next time, never acked); state not RUNNING = nothing read; a `frameSeq` not newer than the last acked =
nothing. Then, in this order, each refusal counted by reason and acked with its code (`ackServerPid`, `ackResult`,
`ackFrameSeq` last): block magic / version (-4); `exePid`, `slots` = 3, `slotBytes` 256 KB..16 MB and a 16-byte
multiple (BadLayout); `slot` = `frameSeq % slots` and `headerBytes <= frameBytes <= slotBytes` (BadLayout); the frame
section opens and holds `slots x slotBytes` (-2); the slot is copied; **torn**: the block names `frameSeq + 2` or later
once the copy is done (the producer writes frame n + 3 into our slot only after publishing n + 2; the slot of frame m is
written while the block still names m - 1, so this covers a publication in progress) (-3); the copy's header
`frameSeq` equals the block's (-3); the copy's `fnv1a64` equals the block's (-1); then the **content checks, once, in
4873's `readTap`** (lead2-0561 #6146), with its `c5r::Err` as `ackResult` > 0. A refused frame leaves the last good
frame in use. Frames skipped between polls are counted (`skipped`). Before the layer is open the reader only opens the
control block and writes `wantBits` (`openOnly`): the compositor taps from then on, and no frame is refused for want of
a check. Stand-in suite [C, 2026-11-18, workstation, Release]: a 256 KB slot of one frame copied and checked in 0.01 ms
mean, 0.07 ms max (`copy_ms`, an indication, not a timing claim); a producer thread publishing as fast as it can is
caught as torn within 2,874 polls, and no accepted image was torn.

### 11.3 The identity set

`BuildIdentitySet` (the rule of 3.2 and the ticket's item 3): a CityBlock file's part -> bit cities, class BLOCK; a
CityPiece file's -> cities, SINGLE; a Landmark file's -> landmarks, INST; a skinned part -> exactly one bit by the
source of the model that uses the file (Wonder, CitySet: cities; LandmarkRow: landmarks), class SKINNED; sources Wall and
GreatWall are CPU-built in the EXE (P4) and never published (4879). Keys with ib or vb 0 are left out; the same key
twice with the same entry is kept once; the same key with another bit or class among the wanted bits is left out
whole (the EXE keeps drawing it); only the bits of the options on are published; sorted by key bytes. The layer
publishes the keys of files it has made drawable (`ModelRenderer::prepare(file)`, 4873's answer to #6160: GPU buffers
and every part's textures), a few files per `Scene::idle` budget, so the set grows while the game runs; generations are
written at most every 500 ms. The section is created with `VPSHELL_MODELSET_MAX_BYTES` (4 MB, pagefile-backed); a
section that already exists (a server restarted under the same EXE) continues its generations. Write order: seq odd,
keys, header, hash, seq even, then the control block's `setGeneration`. The compositor's verdict is read from
`setGenerationApplied` / `setResult`; a refusal is counted once per generation and only after `setReads` moved past our
publish (an older generation's refusal still standing is not taken for the new one's) [C].

### 11.4 The mode writer (D118 per bit), its readiness, the database copy

Own writer on the render thread, the units writer's store order at a change: `writerPid` (only when not already ours:
a server without the units writer still needs it, the compositor latches the word only from its live server), `word`
(bit 0 and bit 3 kept as they are, bits 4-31 written 0), `changedQpc`, `changes + 1`; never `readyLossTotal`, the
consumer part or the reserved words. `srv_units_mode` is unchanged (lead2-0561 #6137, MC #6134); the case
`mode_sets_and_keeps_units` shows the units' bit, `readyLossTotal` and bit 3 unchanged by every model write [C].
Per bit: ready = the option on with source `exe`, the layer open, its renderer made, and a generation of ours with keys
of that bit applied by the compositor with `VPMSET_OK`. **To set a bit** (start-up, after a loss or a load) the
compositor's `setGenerationApplied` must equal the generation published last (4874's rule, 10.4: in the frames between,
`filterState2` reports reason 9, which the D118 step would read as fatal); a bit already live stays on an older applied
generation while a newer one waits (the older set is a subset) [C, case `scene_set_rule`, which fails with the earlier
rule]. Fresh (corrected 2026-11-18 21:00 by worker-4875 after 4874's 10.5; was "an accepted tap frame within
`render_models_no_frame_ms`", which a view without models would have broken, since the compositor publishes only while
something is tapped): the tap's state is RUNNING and every frame it published has been read and accepted, within
`render_models_no_frame_ms` (500); published frames that are refused or not read make it stale [C, `scene_full_path`].
The bit is set at a drawn frame when ready and fresh. Lost (not ready, or not fresh) = the bit cleared at once, the EXE draws that class again; back within
`render_models_hold_ms` (30000 **of step time**) = set again, LOUD; not back = the D125 step (`HostConfig::fatal(5, ...)`).
(Corrected 2026-11-21 by impl-0641, section 16: both `render_models_no_frame_ms` and the hold count step time, a step
adding at most 250 ms; was "hold 2000 ms of wall time", which a 2.4 s stall of the server's own render thread turned
into the end of a game.) Never set
within `render_models_install_ms` (30000, 0 = none) of the later of the load's first drawn frame and the end of the
model layer's open = the D125 step (an option the player turned on must not stay off silently); the bound does not run
while the open runs, and the open has bounds of its own (11.13). (Corrected 2026-11-19 by worker-4885; was "of the
load's first drawn frame", which turned a disk-bound open of 18-32 s into the fatal step, 11.13.) `filterState2`'s half of the bit = `VPFILTER_CANNOT_DROP` while the bit
is held and latched = the D125 step at once, the bit cleared first. A load (`kLoadBegin`) clears both bits and ends a
loss. The offline player has no mode block: no bits, no bounds [C].
The database copy (MC #6166): the model library needs the game database; the copy is the units' one
(`srv_remote_main.cpp` UnitDatabaseAtLoad), now made once per load when `SrvFeed::DatabaseCopyWanted()` (units or
`SetModelDatabaseWanted`, set by the model module's configure); without the unit feed the load mark is the module's
own (the first `ICvPlayer1::SetupGraphical` of a load: a player id not above the previous call's, the units' rule).
The unit writers stay off (`feed_flag` [C]; the hook itself is not linked by any test [I]). A failed copy leaves the
layer unopened, so the install bound takes the same D118 route as with units on [I].

### 11.5 Options and server.cfg keys

`[Renderer] ServerCities` id 12, `[Renderer] ServerLandmarks` id 13 (K4), 0/1, default 0. Id 11 is not on this
branch (epic 0504's `GameEventsOneCrossing`): a reserved row with an empty key that `FindKey` never returns and that
the default file and the documentation test leave out; the processes' option lists print it as `[Performance]  = 0`.
The proxy (`ModelLines`) writes `render_cities=1` / `render_landmarks=1` only when this boot writes the render lines,
else one `shell: [Renderer] ServerCities = 1: ...` line says why. No compositor line: 4874's `model_filter` defaults to
`auto` (the mode word decides). Server keys (`ParseHostConfig`): `render_cities`, `render_landmarks` (0/1),
`render_models_source` (exe | own: `own` is an empty scene with its reason, the bits never set; 4880 / 4881 parked),
`render_models_no_frame_ms` (16..600000), `render_models_hold_ms` (1..600000), `render_models_install_ms`
(0..3600000), `render_models_open_stall_ms` (0..3600000, default 60000) and `render_models_open_max_ms` (0..3600000,
default 600000) (ticket 4885, 11.13; 0 = none), `render_models_pair_wait_ms` (0..100, R1: 11.8), `render_test_models_draw_all` (TEST ONLY). The module draws **only the records the compositor
DROPPED** (`VPMTAP_DRAW_DROPPED`), i.e. exactly what is missing from the EXE's picture, so a bit not latched draws
nothing twice; `draw_all` draws every resolved record (frames made offline from a capture carry no DROPPED flag).
Draw place: after the trees, before the units (stock's pass B: Forest, City, Unit), into the scene's colour and depth;
the reflection records through the water's reflection-models hook with the units'.

### 11.6 The bundle member (format version 9)

MC #6158: a new member kind raises the format (8 -> 9; replaced the optional-v8 proposal of #6151); a version-8 bundle
is refused as OlderVersion naming version 9's change; the library's re-record is the epic's own step.
`modeltap/frame<NNNNNN>.mtap` = the module's frame image as is; NNNNNN = the frames.bin index from which it is in
force; a member is written only when the image's hash differs from the last one written. **Why by change**: an image is
up to the slot size (4 MB default) and the cb1 constants carry the view, so a pan changes every frame (one member per
frame) while a still span records one; every frame would cost the slot size times the frame count even when nothing
moves. Checks on read as the decal member's (header, sizes, hash; then `readTap` where the build has it) and the
cross-checks against render_config (`srv_scene_bundle.h`, the block at `ModelTapFile`). Rule A: the replay
(`LoadModelTapMembers`, `ApplyModelTapFrame` in sceneshot) draws the models only when the bundle's render_config turns
cities or landmarks on (MC #6263); a v9 bundle without the member replays as before. The player needs the game
database for the library: the units_db member when the bundle has one, else `C5R_LANDMARK_DB`.

### 11.7 Fog

The model draws take the server's own fog in the BuildingShader PS's frame slots: t7 = the fog-of-war mask
(`FowScene::terrainInputs().mask`), t8 = the cloud base (`.cloudBase`) (fog-of-war.md 2: "the mask in PS slot t7 ...
t8 = the cloud base"). What differs from the capture's: the EXE's own model draws read the white mask (D136) or the
mask handed over by 0339; the server's draws read the server's mask of the same frame directly, which ends D136's white
mask for the models the server draws. *Corrected 2026-11-19 by ticket 4882 (11.12; replaced: "t5, t6 and t9 and the
environment cubes t2-t4 have no server source yet: they are unbound and counted (`env_missing`)", which drew every model
black live, s3 point 8): t5 is the install's `fogramp.dds`, t6 the Buildings ColorKey volume, t9 the TerrainFOW volume
and t2-t4 the install's cube maps by the tap's identity (`c5r::landmarks::InstallInputs`); without the server's fog t7 /
t8 are a white mask and a black cloud base, never unbound. The VS cb1 rows 0-3 (view-projection) and, with the server's
fog, 8-12 (OverlayProj, FOWAnim) are this frame's, written into the tapped records (`applyFrameCamera`); PS cb1 stays the
EXE's, tapped with the draw.*

### 11.8 Notes for later

- **Live terrain (MC #6254, the Architect)**: tapped instance rows carry the height the EXE placed the model at; the
  tapped matrices are used unchanged. The one-line add (current height field minus the field at the last tap, on the
  translation row of each instance) goes in `srv_model_layer_c5r.cpp` `C5rLayer::accept`, after `resolve`, on
  `ModelScene::rows` of each item's instance rows.
- **R1 (landmark-renderer.md 7.7; lead2-0561): each server frame draws the tap of its own Present.** The server's frame
  for the EXE's Present P (`SceneFrame::cam.frameStamp`) is paired with the tap frame whose `VPModelTapFrame::
  presentCount` is P: its records carry P's camera in VS cb1, so the models are drawn at the camera the terrain is drawn
  at, and the EXE's cull (models from about 21.6 px outside its view) matches the frame. `ModelsScene::beginFrame(P)`
  (before the reflection and scene passes) looks in the last four accepted taps; if P's is not there it polls the reader
  for up to `render_models_pair_wait_ms` (default 4 ms, 0..100; the compositor publishes tap P at the Present that ends
  EXE frame P, so the server's frame for P can start before it), stopping early once a newer tap shows; else it draws
  the newest older tap (one or more Presents behind: `pair older`, `max_lag` in Presents) and only when nothing older is
  held the oldest newer one (`pair newer`). The chosen tap is put back into the layer (readTap + resolve again) when it is
  not the one the layer holds. **Latency**: the model pass of frame P waits at most the bound for the EXE's frame P to be
  tapped; within it there is no lag and no hole at any pan speed; past it the picture is one frame behind for the
  models only (counted). Not measured live (4878 / P7). Stand-in [C, case `scene_pairing`]: exact, put back, older after
  a 4.0 ms wait, newer, and a tap published during the wait found after about 12 ms with a 200 ms bound. Assumed [I]: the
  tap's `presentCount` equals the camera block's `frameStamp` of the same Present (both are the camera block's
  `presentCount` at that Present); the live smoke checks `pair exact` against `pair older`.
  *Corrected 2026-11-19 by ticket 4882 (11.12): the numbering holds [C], but under the product's lockstep the tap of P is
  published only after the composite took the server's frame P at its insertion, so the exact tap is never there in time
  and the 4 ms wait made the composite's lockstep give up for the whole of s3 [L]. The default wait is 0 and the records
  are drawn at the frame's own camera (rows 0-3, 8-12 of VS cb1), so the one-Present-old tap costs no misregistration;
  the paragraph above is kept for the rule's history.*
- The scene target is taken as the tap target with the largest area, the reflection as the largest other one [I].

### 11.9 Not verified

Nothing ran against a live compositor or the product's `server.exe` (server.exe builds clean, Release, with the c5r
layer compiled against the merged 4873 headers [C, 2026-11-18]). Not verified: the identity set's acceptance by 4874's
compositor; the pairing's `presentCount` equality (11.8); the c5r layer
against a real install (it compiles against 23aa402db); the scene and reflection target choice; the database copy
without the unit feed; the reserved option row in the x86 proxy build.

### 11.10 Tests

`server/tests/render/cities/` (`build_models_tests.py`, `test_models.py`, C5_MODELS_BUILD): 19 cases, each refusal and
D118 path with its counter, ackResult and fatal text, a producer thread for the torn case, the module end to end with a
stand-in layer and a fake compositor under the product's section names; 21 pytest items passed 2026-11-18 (Release and MSVC ASan) [C].
`options/tests/test_options.cpp` `TestModelOptions`: 412 passed x64 plain and ASan (MSVC) against OPTIONS.md plus the
hand-back's text [C]. `server/tests/render/scenebundle/bundle_format_test.cpp` `TestModelTapMember` and the v8 refusal:
`build_scenebundle_tests.py --run` (C5R_EXE and C5R_GAME_DIR set) 2026-11-18 20:1x EST: bundle_format_test 1020,
recorder_test 247, sceneshot_test 203, sources_test 24 passed, 0 failed, 0 skipped [C]. The models suite also under
MSVC ASan: 19 passed [C].

### 11.11 The live smoke of 4875 (ticket 4879 step 0, worker-4879, 2026-11-18) [L]

Setup: claimed Goldberg instance c5h-c (workstation, `mods548`, Vibrant Textures), the product path of 3308's prep through
`render/evidence/0561/4879/chain_4879.sh` / `run_4879_smoke.py` (4874's pattern: 4757's driver, layout installed and
backed up by SHA-256, restored by hash after the run), VP64Options.ini `[Renderer] ServerRenderer = 1, ServerUnits = 0,
ServerCities = 1, ServerLandmarks = 1`, config.ini the product's D33 list, no composite test key (`model_filter=auto`);
layout = the pinned `x64-c29442e8-p58724e0` x64 DLL (`2B56A1CD...`) with the team branch's server.exe, proxy and composite;
the Tiny copy `C5H-4879-TINY_0018` of `C5H-3000-TINY-T0018` (E337244C, turn 18). The driver checks the eight points of
thread 0561 #6452 from the logs (point 8, the pictures, by eye).

**Run s1 (22:34-22:39 EST; server `2C18A5C5...`, proxy `9070698E...`, composite `6312522E...`, team branch c6726fc01):
the server ended at the install bound, exit code 5.** render.log 22:37:21.700 `LOUD: models: cities: the option is on
but the module was not ready within render_models_install_ms=30000 of the load's first drawn frame ... the server ends
(exit code 5)`. What held: the proxy's two option lines (point 1); the database copy without the unit feed, the layer
open after 22,360 ms (624 files, 621 ok, 4,448 keys, wire index 6.9 s) and the renderer made (point 2); the control
section opened with want bits 0x6 and identity set generations 1-6 published (5,539 keys: cities 633, landmarks 4,906;
29 CPU-built parts left out) (point 3, in part). What did not: the compositor never read a set (`set_gen=0 set_reads=0`),
never tapped (`tap_bits=0x0 published=0`), so `frames accepted=0`, `acked=0`, both bits `phase=starting` to the end.

**Root cause [L, C]**: the compositor uses the server's `wantBits` and identity set only while it treats the server as
live (`unit_filter.cpp` `ModelOnPresent`: `want` and `setGen` are 0 unless `live`), and liveness needs the mode block's
`writerPid` to equal the server's pid (the latch's `UFL_PID` check). The composite log said so from the first Present:
`unit_filter: latched word 0x0 (raw 0x0, server pid)`. With `ServerUnits = 0` the units writer never writes `writerPid`,
and the model writer (`ModelModeWriter::write`, `server/src/render/cities/srv_models_mode.cpp`) stored it only when the
bits change; a bit is set only after the compositor has applied the set, which it does only for a live server: a
deadlock that ends at the install bound (D118's step, correct as designed). 4875's stand-in compositor in `models_test`
read the set without the liveness rule, so its suite passed. With `ServerUnits = 1` the units writer claims `writerPid`
when its bit is set, which needs no compositor action; that is why 4757's runs never met it.

**Fix (ticket 4879, 2026-11-18)**: `ModelModeWriter::attach` claims the block (`writerPid` = the server's pid) when a
bit's option is on; the word is written at the first change as before; with `ServerUnits = 1` both writers store the
same pid. Test: `models_test` case `scene_liveness_without_units` with a stand-in compositor that applies the liveness
rule (no tap, no set read while `writerPid` is not the server's); it fails on the old writer at its first check.

Instance note (not the product): c5h-c's `install/Assets/UI/FrontEnd/MainMenu.lua` was `8A1CCDFA...` before this run's
prep (provision's baseline differs: `provision.py verify c5h-c` rc 1, `install: size ...MainMenu.lua`; 4874's restore
at 20:00 had `5C525C54...`); restored by hash to what was found.


### 11.12 The server's model draws live: the PS inputs, the pairing, the load begin (ticket 4882, worker-4882, 2026-11-19)

Branch `task-4882-model-draw-live`. The three failures of 4875's smoke s3 (11.11, thread 0561 #6609) [L]: the models drawn
in place but solid black (point 8), the tap paired one Present behind (point 4), no load begin with `ServerUnits = 0`
(point 7).

**(1) Black models: PS t6 unbound** [C, 2026-11-19 01:00-01:45 EST]. The BuildingShader colour PSs `c704c80b` (blocks) and
`f0e1f0a6` (3, 4, 6), disassembled with `d3dcompiler_47!D3DDisassemble` from 4871's capture export (scratch only, D83),
compute the lit colour, gamma-encode it (pow 1/2.2), and **sample the t6 volume at that colour (s6, clamp)**: every
pixel's colour is the volume's texel. Then `c = lerp(t8 cloud base, c, t7.x)`, `c = lerp(c, t9(c), t7.x (1 - t7.y))`
(fog-of-war.md 2.8), then the distance fog `lerp(c, cb1[18], f)` with `f` scaled by `t5.x` at `(v3.y/2 + 1/2, cb1[17].w)`.
An unbound t6 reads (0, 0, 0, 0), so the model is black whatever else is bound; 4875's draws left t2-t6 and t9 unbound
(11.7) and drew black live. The offline harness (12) passed because it bound the capture's own textures.

The captures' resources, matched by SHA-256 against the install (`frame_slots.py` / `env_fmts.py` in the work folder;
4871's exports P4_w3 (725 draws) and P6_road (324 draws), every draw the same t5-t9):

| Slot | Capture | The server's source (`c5r::landmarks::InstallInputs`, `render/src/landmarks/inputs.cpp`) |
|---|---|---|
| t5 | 128 x 8, 8 mips, R8G8B8A8_UNORM view, `f9ed79fe...` | `fogramp.dds`, uploaded as the EXE does (the A8R8G8B8 file as R8G8B8A8, red and blue swapped) |
| t6 | 32^3 R8G8B8A8_UNORM, `a62b4351...` | `Buildings_Generic_ColorKey.tga` by the TGA rule of asset-pipeline.md 3.5; equal to all five Buildings and all five Terrain files of the workstation's Steam install [C] (*2026-11-20, ticket 0585, 15.1: the EXE binds Buildings Generic for cities, wonders and landmarks and the record's style 1-4 for the walls [C]; was "which style the EXE binds is not read [I]"*) |
| t9 | 32^3, `49245115...` | `TerrainFOW_Generic_ColorKey.tga` (the five TerrainFOW files are equal) |
| t7, t8 | the EXE's mask and cloud base | the server's own fog (11.7); without it a 1 x 1 white mask and a 1 x 1 black cloud base (the fog-off look), never unbound |
| t2-t4 | cube maps, BC1 or R8G8B8A8, always through `_SRGB` views | per draw by the tap's identity: every install `.dds` whose name holds `cube`, `refl`, `environment`, `irradiance` or `light` and that is a cube map, keyed by `textureIdentity` (the BGRA rule); made at first use with the sRGB form. Steam install: 576 names, 499 cubes, 481 identities, read and hashed in 310 ms |

Offline (`cityscene run --inputs capture|install|none`, the harness case of this ticket; `cityscene.py --inputs`), 3910's
Fanc01, render build of 2026-11-19 01:30 EST:

| Inputs | Reflection 1024 x 1024 (596 px) | Scene 2554 x 1411 (296,049 px) |
|---|---|---|
| install (the server's sources, t7 / t8 the capture's) | median 0, p95 0, max 0, within 16 1.0000 | median 0, p95 0, max 0, within 16 1.0000 |
| none (s3's live state) | median 103, p95 195, max 235, within 16 0.0101 | median 98, p95 197, max 251, within 16 0.0036 |

The same on this branch after the team branch a1c0a8858 merged (4879's CPU-built meshes drawn too), 02:20-02:32 EST:
W01 (505 / 334,010 px) install = capture inputs exactly (both: reflection max 2, within 16 1.0000; scene median 0, p95
0, max 81, within 16 0.9927, the rest on draw 617, the Great Wall's CPU-built material-3 draw with the EXE's blend off,
7.3, 4879's), none = scene median 126, within 16 0.0004; Lanc07 (1,105 / 124,666 px) install = median 0, max 4 / 0,
within 16 1.0000. Every env identity of the five frame runs (6 in each reflection, 10 or 11 in each scene) was found
among the install's cubes; t6 and t9 are byte-equal to the capture's. t5: the uploaded texture is the file's levels
(swapped); the harness's byte check first compared the file with its 128-byte header (5,628 against 5,500 B, a check
slip, fixed 2026-11-19 02:40: `rampBytes()` now holds the levels only); rerun 02:50: t5 byte-equal to the capture's, all 8 levels (5,500 B, SHA-256 `f9ed79fe...`), so the EXE's red-blue swap of the A8R8G8B8 file holds for the ramp [C].

**(2) The pairing one Present behind: timing by design under lockstep, and the wait broke the lockstep** [C code, L s3].
Numbering: the compositor increments `presentCount` at the start of its Present (`CamOnPresent`, shell_composite.cpp)
before `UnitFilter_OnPresent` publishes the tap, so tap P carries P; with `lockstep=1` (the product's
`shell_composite.ini`, bound 3,400 us) the camera of frame P is published at UpdateViews with `F = max(F' + 1,
presentCount + 1)` = P. The two numbers agree. Timing: the composite takes the server's frame F at its insertion (after
the scene clear, before the EXE's own world draws) and waits for it at most the bound; the tap of P is published at
Present P, after that insertion. So the server can only have tap P for its frame P when its frame is late (a lockstep
miss). s3's composite log (`work-s3/out-base-r1/shell_composite.log`, `lockstep 1s:` lines): exact 31 per second until
23:15:58; the landmarks bit was set at 23:15:58.77 (render.log); at 23:15:59 `giveups=1 gave_up=1` and the lockstep
stayed given up for the rest of the session (`wait_off` every frame, `ring_exact` 501 against `ring_older` 29,097 at
the end): **the 4 ms pair wait of 11.8 delayed every server frame past the 3.4 ms bound**, so from then on the whole
server picture (terrain, water, units, models) was shown one Present late. The 62 exact pairs are those frames that
waited until the EXE's Present (`waited=55`).

The fix: `render_models_pair_wait_ms` defaults to 0 (the key stays, 0..100), and the drawn records are drawn at the
frame's own camera (`c5r::landmarks::applyFrameCamera`): VS cb1 rows 0-3 = the frame's view 0 x projection (the
reflection pass: the reflection camera, as the units'), and with the server's fog placed rows 8-11 = the OverlayProj of
view 0 and row 12 = FOWAnim that the terrain's fog uses this frame (the VS reads rows 8-11 .xy and 12); PS cb1 row 16 =
view 0's eye of this frame in both passes, rows 17-21 left as tapped (*2026-11-20, ticket 0598, 15.8; before it row 16
was left as tapped, a frame old (0585, 15.2); first written as "[I: row 16 the eye position, a frame old]"*). So a tap one Present old costs no misregistration; what
is left is the content one frame old (a model appearing at the leading edge of a pan faster than about 20 px per frame,
7.7 R1). The module counts the shift avoided (each instance origin of materials 3-5 projected by the tap's own rows and
by the frame's, pixels of the target): `cam frames= exact_n= exact_max_px= older_n= older_mean_px= older_max_px=
win_max_px= refl_win_max_px=` on the `models:` line (`win_*` = the largest since the previous line). A still camera gives
about 0 (the check of the rows' convention). Live numbers: see the live run below (the shift was 0.000 px on every exact pair and 0.00 px in the still window, so the convention holds [L]).

**(3) The load begin without the unit feed** [C]. `SrvFeed::PushModelLoadBegin(player)` appends `kLoadBegin` (a[0] the
unchanged unit table generation, a[1] the player) when the feed is on and the unit feed is off; the model load mark
(`ModelDatabaseLoadMark` in `srv_remote_main.cpp`, the first `ICvPlayer1::SetupGraphical` of a load) calls it before the
database copy and logs `feed: models: a load begins at ICvPlayer1::SetupGraphical(N): kLoadBegin pushed`. With the units
on it pushes nothing (their `ResetUnitsAt` does: never twice). The render side's rules for that event then run without
the units too: the model bits cleared and the layer's scene cleared (`models mode: a load begins`), and the arc's, the
arrows' and the path's clears. A recorded bundle with `ServerUnits = 0` now holds the event in `feed.bin` (same format).
Live: both loads of each live arm logged `models mode: a load begins` (arm base 02:34:27 and 02:36:23 with `feed: models: a load begins ... kLoadBegin pushed (seq 5)` and `(seq 50)` in server.log; arm filter 02:40:40 and 02:42:40 with no `feed: models` line: the unit feed's mark pushed them), and both bits were set again after the reload [L].

**Tests** [C]: `render/tests/landmarks/test_inputs.cpp` (`cube_names`, `frame_camera_rows`: the rows replaced once per
constant, only the target's, rows 8-12 only with the fog, the shift in pixels against the tap's own rows;
`inputs_negative`; `inputs_from_install`; `unbound_inputs_draw_black`: one block drawn with the EXE's programs is black
on every covered pixel with the inputs unbound and lit with the install's); `server/tests/render/cities/models_test.cpp`
`feed_model_load_begin`, `scene_camera_rows`, and `scene_pairing` adjusted to the default 0 (with a 4 ms case kept).
Runs 2026-11-19 (workstation, Release, after the team branch a1c0a8858 merged): `render/build.py --test -R c5r_landmarks` 7 of 7 passed, 0 skipped (02:05-02:20 EST); `test_models.py` 25 passed (models_test plain); server.exe and the proxy release built (`build_server.py`, 243 s); the shell plain built; the live driver's selftest 8 of 8.

**The live run** (ticket 4882, c5h-c, 2026-11-19 02:32-02:45 EST, released 02:46) [L]: `server/evidence/0561/4882/
chain_4882.sh` / `run_4882_live.py` (4879's and 4757's product path: layout and options backed up by SHA-256 and restored
by hash, `backup_restore_ok` true, `provision verify` 0, `frontend.py restore` 0 and status MainMenu original
`5C525C54...`); layout = the pinned `x64-c29442e8-p58724e0` x64 DLL (`2B56A1CD...`) with this branch's server.exe
(`4D6A7FF5...`), proxy (`A3FAB1CD...`) and composite (`E1623606...`), head af7d90583 (team a1c0a8858 + 4882); the Tiny
copy `C5H-4882-TINY_0018` of `C5H-3000-TINY-T0018` (E337244C, turn 18); options 12 / 13 = 1. Two arms, one launch each:

| | arm base (ServerUnits 0) | arm filter (ServerUnits 1) |
|---|---|---|
| bits set after the load's first frame | landmarks 21.5 s, cities 23.1 s | 18.2 s, 19.8 s |
| frames drawn, `env_missing`, `env_unknown`, draw errors | 5,617, 0, 0, 0 | 5,702, 0, 0, 0 |
| pairing | exact 11, older 5,606, waited 0 | exact 15, older 5,687, waited 0 |
| camera rows: shift on exact pairs / still window / camera steps | 0.000 / 0.00 / up to 166.96 px | 0.000 / 0.00 / up to 166.93 px |
| older taps: mean shift avoided per frame | 3.36 px (max 195.6) | 3.26 px (max 319.4) |
| composite lockstep exact / older, still 12 s | 395 / 1 | 205 / 165 |
| the same, camera steps 20 s | 635 / 8 | 297 / 345 |
| lockstep give-ups after the bits | at the reload and the AI turn only, each recovered in 1-4 s | about every 5-10 s throughout, each recovered |
| server `submit_ms` (render.log 1 s lines) | about 2.0-2.5 ms | 1.5-1.7 ms before the bits, 2.6-3.9 ms after |
| load begins / bits again after the reload | 2 / yes | 2 / yes |
| box, crash, dump | none, rc 0 | none, rc 0 |

- **(a) no black model**: DIAGNOSTIC pictures `vc-20261119-023514-workstation-03d4` (base, load), `-023704-...-2347`
  (base, reload), `-024124-...-1140`, `-024321-...-4990` (filter): Madrid's blocks and the improvements shaded as the
  EXE's. The presented frame against its passthrough frame (the composite's `mode=passthrough` for one frame: the EXE's
  own models over the clear colour), base load camera: Madrid's blocks outside the capital's light column equal the
  passthrough pixels (median difference 0 over 2,542 px). model_checks' drawn-whole figures without stock sessions
  (`drawn_mask` + `drawn_whole` on the compare's screen area): base 314,068 px median 56, p95 135, max 203, within 16
  24.42 %; filter 323,341 px median 56, p95 136, max 204, within 16 23.87 %; one blob of 266k / 275k px is the capital's
  translucent light column (drawn over the clear colour in passthrough, over the world when presented), the next ones
  copper chunks that stand partly under the server's terrain (whole over the clear colour) and model parts under fog
  where passthrough uses D136's white mask: so the gate's verdict is not a model fault here; the gate needs the stock
  sessions (not run).
- **(b) the lockstep holds with the models live and the units off** (s3: given up for the whole session); the lag of one
  Present is drawn without misregistration (shift 0 while still, so the rows' convention holds; the shift avoided while
  the camera steps, up to 167 px, is what s3's drawing at the tap's own camera cost). **New finding [L], not fixed here:
  with the units on as well, the server's frame passes the composite's 3,400 us bound often** (`submit_ms` 1.5-1.7 ms
  before the model bits, 2.6-3.9 ms after them; lockstep older 45-54 % in the two windows, short give-ups every 5-10 s):
  the model pass costs about 1.5-2 ms of the server's frame on this Tiny save; with both modules on, the whole server
  picture is one Present late in about half the frames. For 4878's P7 (the cost series) and the lead: where the time goes
  (per frame the layer re-resolves the new tap and the renderer creates its row and constant buffers per pass, 7.8) is
  not measured. *Measured and cut 2026-11-19 by ticket 4884 (section 14): the scene pass's item loop (13 context calls
  per item), resolve's allocations and the buffers made per pass; was "is not measured".*
- **(c)** as above: kLoadBegin at both loads in both arms, once each.

**Not verified**: the drawn-whole gate of `tools/compare/model_checks.py` with stock sessions (no stock arm ran: the figures below are the presented frame against its own passthrough frame only); which ColorKey style the EXE binds as t6 (all five files equal here); PS cb1 row 16 as the eye position; a huge save; the reflection pass's shift (`refl_win_max_px` stayed 0: the reflection held no material 3-5 record with a shift, or the counter does not see one).

### 11.13 The install bound counts from the layer's open (ticket 4885, worker-4885, 2026-11-19)

Written 2026-11-19 by worker-4885 (account B, Opus 5.5) on `task-4885-install-bound`, offline only. Marks: `[C]` this
ticket's code and its own `models_test` cases, `[L]` the game (not run here), `[I]` inferred.

**The fault [L, 4878's live run e1, thread 0561 #6913; 4883 #6844].** The install bound counted from the load's first
drawn frame, while the model layer's open (the library's 624 files, the art catalogue, the wire index, the programs and
inputs; one worker thread, disk-bound) took 18.5 s / 25.5 s / more than 32 s in 4878's arms on / cities / landmarks
(c5h-e, F: under a headless load), 18.3 s on c5h-a (4883) and 14.0 s on oldpc (4878's P7 job). The landmarks arm took
the D118 step (`... not ready within render_models_install_ms=30000 of the load's first drawn frame` -> server exit 5,
proxy FATAL exit 3) although nothing had failed. Root cause: the bound's start, not the open; the open was making
progress the whole time [I from the open times; the open's own log line is 4878's].

**The rule now [C, `srv_models_mode.cpp/.h`, `srv_models_scene.cpp`].** The module tells the mode writer when the
layer's open begins (`onOpenBegin`, as the worker starts), makes progress (`onOpenProgress`) and ends (`onOpenEnd`,
ok or failed with its reason). The open's state belongs to the layer (it opens once per process): a load or a
reconfigure keeps it.

| Stage | Bound | Fatal text names |
|---|---|---|
| before the open begins (no game database copy yet) | `render_models_install_ms` from the load's first drawn frame, as before | "the model layer's open has not begun ... (no game database copy, or no model layer)" |
| the open runs | no install bound; **no progress** for `render_models_open_stall_ms` (60000) and a **ceiling** `render_models_open_max_ms` (600000) from its begin, 0 = none | "the model layer's open ... made no progress for N ms" / "has not ended N ms after it began" |
| the open fails | at once | the module's "models: the model layer cannot open: <reason>" (the writer's own line: "the model layer's open ... failed after N ms: <reason>") |
| after the open | `render_models_install_ms` from the **later** of the load's first drawn frame and the open's end (identity set, compositor's verdict, tap frames, renderer) | "... within render_models_install_ms=N of the later of the load's first drawn frame and the model layer's open (open after N ms; ...)" |

**Progress** = the open's worker thread ran since the last poll: its `QueryThreadCycleTime` count grew (sampled each
render-thread poll while the open runs). Reading and parsing files keeps it growing; a worker blocked for good (a
read that never returns, a lock) does not. Process-wide I/O counters were not used: other threads' reads would hide a
hung open. A cycle count that cannot be read is said once (LOUD) and counted as progress, so only the ceiling bounds
that open. No hook was added to the layer (the library code is 4878's): a later ticket may add a file count from the
open for the log; the cycle count needs none.

**Log lines [C].** `models mode: the model layer's open begins: the install bound waits for its end (open_stall_ms=..
open_max_ms=..)`; `models mode: the model layer is open after N ms (longest time without progress N ms): the install
bound (N ms) counts from here`; the bit line keeps its text and adds `(N ms after the model layer's open)` when the
open ended after the load's first drawn frame (the 4879/4882 drivers match `" bit 1 after "`, unchanged); the writer's
start line names `open_stall_ms` and `open_max_ms`; the models line gains ` | open state=not_begun|running|open|failed
open_ms= max_gap_ms= progress= stall_fatal= max_fatal= fail_fatal=` after the two bits.

**Choices.** The stall default (60 s) is about twice the slowest open seen live (more than 32 s, all of it making
progress) and far above any single file read; the ceiling (10 min) exists only for an open that "progresses" forever
(a spin). Both are server.cfg keys only (no option id, K4). The pre-open wait keeps the old bound because the database
copy is the one step before the open that can hang and has no worker to sample.

**Tests [C, 2026-11-19, `models_test` built by `build_models_tests.py`].** In the case `mode_install_bound` (stand-in
clock): (a) an open of 60 s with progress every second while frames are drawn: no fatal, the bits set 20 s after it;
the install bound counts from the open's end (fatal at 30.002 s after a 40 s open, not before); (b) no progress:
fatal past 60 s naming `render_models_open_stall_ms`; progress forever: fatal at 601 s naming
`render_models_open_max_ms`; a failed open: fatal with its reason; both bounds 0: none, and still no install bound
while the open runs; (c) the install fatal after a 100 ms open; a later load (and a reconfigure) keep the open, the
bound from the new load's first drawn frame. In the case `scene_failures` (real time, a stand-in layer): an open of
1.5 s in bursts (2 ms work, 10 ms wait) with `install_ms=300`: no fatal, both bits set; an open blocked on an event:
fatal past `open_stall_ms=400`, no install text. Mutation check: with the open ignored in the install bound
(`opening` and the later start forced off), both cases fail. Results 2026-11-19: `test_models.py` 25 passed plain
(11:19) and ASan (11:31, no AddressSanitizer report); server.exe release built (`build_server.py --config release
--deps`, OK in 189 s, 11:39, the pin x64-c29442e8's source and deps; script `server/evidence/0561/4885/build_4885.sh`).

**Not verified.** Live (no game run here; lead-0561 routes it to 4878's or 4883's next run): that the slow opens of
e1 now pass, the cycle count's growth on a real disk-bound open (expected: it grows at every poll while files are
parsed), the new log lines in render.log. (Added 2026-11-19 by worker-4883 [L]: the new lines are in all seven live
runs of ticket 4883 on c5h-a, batch-5 build, 12:39-13:22 EST; e.g. huge `models mode: the model layer's open begins:
the install bound waits for its end (open_stall_ms=60000 open_max_ms=600000)` 13:19:15.655, `models mode: the model
layer is open after 16398 ms (longest time without progress 109 ms): the install bound (30000 ms) counts from here`
13:19:32.053, both bits 13:19:47.252; opens 15.8-17.5 s; no install fatal in any run. offline-scene-harness.md 31.)

### 11.14 The bundle and the CPU-built meshes: a gap (ticket 4883, worker-4883, 2026-11-19)

The scene bundle (format 9, 11.6) records the model tap frames but **not the geometry section of 13.2**
(`Local\VPShellModelGeom.<pid>`): the CPU-built meshes (walls, the Great Wall, the road and rail bridge) reach the layer
only live (`ModelGeomReader` -> `ModelLayer::addGeometry`), so the offline player draws the models without them. On the
huge save the live layer held 4 such buffers (`cpu held=4 kb=252 keys=2 resolved=2`) and the replay differs from live in
about 10,558 colour pixels per frame (a wall at the view's left edge, drawn nearer live); the six Tiny-save bundles hold
none and replay byte-equal [L, C]. Until a member exists, the scene recorder writes the note `gap: models: CPU-built
meshes: ...` (`sb::kModelGeomNote`) whenever the layer holds such buffers in the span (`ModelsScene::heldGeometryCount`),
`sceneshot` prints a NOTE naming it, and the standing check reports such a bundle as an expected failure (pytest xfail,
keyed on the note). Options 12 / 13 are 0 in the default file and 1 in the release configuration (the 0.1b installer
turns them on, MC #6924): the gap affects only the offline library, not what the product draws. The member (the
section's entries at the span's first frame, handed to the layer by the player before the first draw) is a follow-up
child of epic 0561 that lead-0561 opens after the epic's merge.

(Corrected 2026-11-19 by ticket 4888; the paragraph above described the state before it.) **The member exists:**
`model_geom` (`modelgeom/frame<NNNNNN>.mgeo`, format version 9 unchanged, optional): the buffers the layer held (its
`CpuMeshStore`, what the live frame drew from) with strides and bytes, written by the recorder at the first frame of the
span where the layer holds any and at every change of the held set; the player hands the member in force to
`ModelsScene::loadGeometry` before the frame's tap (`ApplyModelTapFrame`). `kModelGeomNote` is now written only when such
a member cannot be written. The huge bundle was recorded again with it. File form, checks, tests and the re-recording:
`research/06-tooling-and-workflows/offline-scene-harness.md` section 32.

## 12. Acceptance (4878)

Written 2026-11-18 from 23:29 EST by worker-4878 (account B, Opus 5.5) on `task-4878-acceptance` (team branch
e27412385 merged). Marks: `[C]` checked by this ticket's own run on the data named; `[I]` inferred; others' marks kept.

### 12.1 Item 1: the offline catalogue at the C1 tolerance [C, 2026-11-18 22:07-22:56 EST]

**Inputs.** 4873's harness (`render/tools/cityscene/cityscene.py`, landmark-renderer.md 7.5) on the render Release tree
of this branch (clang-cl, `C:/c5-64bit-work/0561/4878/render-build/Release`), the workstation's RTX 4080 (driver
610.88, hardware device for both sides); the install copy `render_game_dir` (pure VP 5.4.7 as DLC, Vibrant Textures,
D94), the EXE copy `render_exe` (build 4390913), the database copy `Civ5DebugDatabase.db` SHA-256 27fc81c6... (c5h-c, CP +
VP + EUI). Exports read in place: 3910's 55 (`F:/c5-64bit-work/0301/3910/exports/{landmarks_land,landmarks_sea,wonders}`,
c5h-d, DLL `i0ship-26234371-ia`, save `C5H-0301-CAT2_0009`), 3907's four city frames, and 4871's P4_w3 (walled city,
damage level 3) and P6_road (road bridge) re-exported with their texture dumps from the captures
`C:/c5-64bit-work/0561/4871/captures/c5h-b_20261118-173609_frame{10724,3010}.rdc` into
`C:/c5-64bit-work/0561/4878/exports/` (4871's own exports were kept without textures). Placement on the catalogued
objects: 4870's `objects.py` re-run (3910 and 3907's indexes); 4871's two frames have no catalogue: all their pixels are
the object `save`. Commands:

    W=C:/c5-64bit-work/0561/4878
    python -I perf/quietlock/quietlock.py run --who worker-4878 --what "..." -- \
        python -I render/evidence/0561/4878/catalogue.py $W --build $W/render-build/Release     # 61 runs, 49 min
    python -I render/evidence/0561/4878/summarize.py render/evidence/0561/4878 $W/runs         # the CSVs below
    python -I render/evidence/0561/4878/record_pictures.py $W/runs render/evidence/0561/4878/frames.csv

**Result: every scene target of the 61 frames meets section 6** (median <= 2, p95 <= 8, >= 97 % within 16 per frame,
>= 75 % per object), and every reflection target with object pixels too. Tables:
`render/evidence/0561/4878/frames.csv` (one row per frame and target), `objects.csv` (per catalogued object),
`states.csv`, `left_to_exe.csv`, `totals.txt`. Scene target 2419 (2554 x 1411) per group:

| Group | Frames | Listed colour draws (both targets) | Resolved | Object pixels | Median (max over frames) | p95 (max) | Max | Within 16 (lowest frame) | Catalogued objects | Lowest object within 16 |
|---|---|---|---|---|---|---|---|---|---|---|
| 3910 land (Fanc01-04, Find01-02, Lanc01-27, Lind01-13) | 46 | 23,117 | 23,104 | 10,998,412 | 0 | 0 | 164 (Lind02) | 0.99998 | 494 | 0.99928 |
| 3910 sea (Sanc01-02, Sind01) | 3 | 683 | 683 | 265,686 | 0 | 0 | 0 | 1.00000 | 19 | 1.00000 |
| 3910 wonders (W01-W06) | 6 | 1,000 | 997 | 1,236,603 | 0 | 0 | 116 (W04) | 0.99998 | 44 | 0.99994 |
| 3907 cities (ancient, renaissance, industrial, modern) | 4 | 693 | 693 | 459,385 | 0 | 0 | 96 (c_modern) | 0.99998 | 32 | 0.99984 |
| 4871 (P4_w3 walled city, P6_road bridge) | 2 | 1,049 | 1,046 | 581,454 | 0 | 0 | 129 (P6_road) | 0.99999 | - (save only: 0.99999 / 1.00000) | - |
| **All** | **61** | **26,542** | **26,523** | **13,541,540** | **0** | **0** | **164** | **0.99998** | **589** | **0.99928** |

*(Updated 2026-11-19 00:23 EST after item 6 (12.4): Lanc18-21 rerun with the library's R16 rule. Was, from the first
run of 2026-11-18 22:07-22:56: 3910 land 22,961 resolved, 10,607,035 pixels, 461 objects; all 26,380 resolved,
13,150,163 pixels, 556 objects. The verdicts did not change.)*

- Reflection target 13415 (1024 x 1024): 57 of 61 frames have object pixels (65,793 in all): median 0, p95 0, max 0-144
  (Sanc01's 144, as 4873 found), lowest frame within 16 0.99914 (Sanc01). The four 3907 city frames list the target but
  no resolved draw drew a pixel into it (`frame_pass` n/a).
- Per object over both targets (590 rows; the save's own content apart): no median or p95 above 0, lowest within 16
  0.99884; the object `save` 116 rows, lowest 0.99914. The few pixels above 16 are 4873's open item (landmark-renderer.md
  7.6: equal inputs, cause not found).
- Ambiguous keys (several library parts, equal textures, the first drawn): 6,434 draws over the 61 frames.
- States: every listed draw equals the draw layer's table (`states.csv`) except the Great Wall's W01 617 (blend off, as
  4873).
- Pictures: Visual Checks, issue 4878, 61 entries (ours, the replayed capture as reference `same`, the difference x 8),
  all DIAGNOSTIC (D112: offline, not the player's picture).
- 4873's sixteen frames, run again here on the merged tree, give the same verdicts (their numbers in landmark-renderer.md
  7.6 are 4873's run).

### 12.2 The draws left with the EXE (162 of 26,542 in the first run; 19 after item 6, 12.4) [C for the counts and the samples named]

The table is the first run's; 12.4 gives what item 6 changed (the R32 row is gone, 8 texture rows came in its place).

A draw the library holds no part for is outside the identity set, so the compositor passes it and the EXE draws it
(section 3.2): a mixed picture, never a hole. `left_to_exe.csv` names each (frame, draw, material, index size, key):

| Why | Draws | Frames | Who |
|---|---|---|---|
| CPU-built, no file: the Great Wall (W01 469, 617) | 2 | W01 | 4879 (pending) |
| CPU-built wall (P4: material 5, R32, one instance, 6,156 indices) | 1 | P4_w3 | 4879 (pending) |
| CPU-built road bridge (P6: material 3, R32, 270 indices; the same key in both frames: the crossing of take two was still on the map in the walls' process) | 2 | P6_road, P4_w3 | 4879 (pending) |
| EXE-built 32-byte copy of a Broadway mesh (W06 649, as 4873) | 1 | W06 | open (stays the EXE's) |
| **The library holds the file with R32 indices, the EXE draws it with R16** | 151 | Lanc18 26, Lanc19 47, Lanc20 14, Lanc21 64 | **new: the library (4872)** |
| The library refused the file: `schloss_hb.gr2` (> 65,536 vertices; the EXE draws it with R32) | 1 | Lanc20 | the library (4872, landmark-renderer.md 3.2) |
| **Skinned: index bytes equal, vertex bytes differ** (`ind_polder.gr2`, the animated polder) | 4 | Lind08 | **new: the library's skinned repack** |

- **R32 against R16 [C for the library's forms and two samples; I for the other 149 draws].** `c5r_landmark_report
  --files` lists 38 of the library's 621 files with an R32 index buffer (`ib32`), all with fewer than 65,536 vertices
  (embassy, Eki, hacienda, Kuna, siheyuan, Mediterranean villa, yurt camp, castro / Oppidum, Lalibela, schloss, Water
  Village, Tersane, persian garden, isibaya, aplekton ...: VP's unique improvements, catalogued only in Lanc18-Lanc21).
  The EXE draws those meshes with R16 indices: Lanc18 draw 799 (2,739 indices from index 1: `embassy.gr2`, 2,740
  indices with the pad) and Lanc20 draw 722 (366: `lalibela.gr2`, 367) are R16 draws whose key the library misses with
  no candidate of the same index-buffer size. So the EXE converts a file's 32-bit indices to 16 bits when the vertices
  fit, and the library's key (the IB hash from element 1, which depends on the element size) never matches. Fix (the
  library's, not this ticket's): repack such files' indices to R16 as the EXE does (or key the index values, not the
  bytes); then the 151 draws should resolve. Until then these VP improvements stay the EXE's under ServerLandmarks.
- **The polder [C, one draw].** Lind08 draw 982 (`ind_polder.gr2` mesh `berm2`): the index bytes equal the library's,
  the whole buffers have equal sizes (IB 10,400 B, VB 71,064 B), and 873 of 8,792 vertex bytes differ, the first at byte
  16 of vertex 0 (the half-float normal: `10 2d f7 36` ours against `94 26 f5 36` captured). So the EXE's skinned
  vertices of this file carry other normals than the library's repack (normalised or recomputed [I]); the four skinned
  draws of this object stay the EXE's. The 4873 skinned samples (Lanc07 deer, Sanc01 fish) matched byte for byte.
- None of the 162 is a texture miss or a material miss (resolve: texture miss 0, material miss 0 in every frame).

### 12.3 Not verified (item 1)

- The R32 -> R16 cause for the 149 draws not sampled (each misses with no same-size candidate; the frames' catalogued
  objects are those 38 files' improvements) [I strong].
- Why the polder's normals differ (the bytes differ; the EXE's rule was not read).
- The pixels above 16 (4873's open item); MSAA and terrain depth (both sides draw single-sampled into a cleared depth,
  landmark-renderer.md 7.5).
- The CPU-built rows (the wall, the Great Wall, the bridge) join when 4879 merges: they are counted above as left with
  the EXE, not compared.

### 12.4 Item 6: the R16 rule in the library, the polder's normals, the EXE's default t1 [C, 2026-11-18 23:40 - 2026-11-19 00:23 EST]

**(a) The index size is the EXE's.** `layoutFile` (`render/src/landmarks/layout.cpp`) gave a file R32 indices as soon as
one packed mesh was stored with 32-bit indices (the `.gr2`'s `Indices` instead of `Indices16`); the EXE draws such a
file with R16 indices when every index fits 16 bits (12.2: Lanc18 draw 799 `embassy.gr2`, Lanc20 draw 722
`lalibela.gr2`). Changed: R32 only when an index is above 0xFFFF. Checked against the exports: every library-file draw
of the 61 frames is R16 (the only R32 draws are the CPU-built meshes of 12.2 and `schloss_hb`, whose mesh of more than
65,536 vertices `repack_unit_mesh` refuses), and no file of the library has 65,536 or more vertices or indices
(`c5r_landmark_report --files`: 0 files R32 after the change, was 38; 621 laid out, 3 refused, as before). Tests:
`test_layout` (a 32-bit mesh whose indices fit is R16; an index of 0x10000 makes R32; 0xFFFF stays R16) and two new
`test_oracle` samples (Lanc18 d799 embassy at scale 1.2, Lanc20 d722 Lalibela at 0.75: the key found and the bytes
equal), `render/build.py --test -R "c5r_(landmarks|cities)_"` 6 of 6 passed, 0 skipped (Release, 2026-11-19 00:0x
EST; samples in `C:/c5-64bit-work/0561/4878/samples`). Item 1 rerun on Lanc18-21 (00:02-00:11 EST): 1,692 of 1,701
listed draws resolve (was 1,549), every target median 0, p95 0, max 0-35, within 16 1.00000; 47 catalogued objects of
these frames compared over both targets (33 more on the scene target than in the first run), lowest within 16 0.99995.
`c5r_landmarks_test_oracle` run alone: "byte-equal: 13 of 13 sampled draws", the two new ones included. MSVC ASan
(`render/build.py --asan`, own tree, 2026-11-19 00:3x-00:5x EST): `c5r_landmarks_test_layout` and `_oracle` 2 of 2
passed, the canary caught, 0 ASan reports.

**What is left of the 151 draws: 9.** One is `schloss_hb` (refused, R16 draw of 68,889 indices). The other **8 are a
texture rule, new [C]**: the geometry now resolves (each names its file: `eki_pl`, `eki_uc`, `hacienda_hb`, `schloss`,
`schloss_pl`, `iya`, `iya_pl`), the t0 identity is equal, but the file's material names no `BuildingSREF`, so the
library's part has no t1, while the EXE binds its own texture there: identity `a61a52924ecfca51`, the same in all 8
draws, an 8 x 8 R8G8B8A8 (sRGB view) texture of one mip (Lanc18's res 910: one texel non-zero). It is not a file of the
install [I: made by the EXE]. Fix (the library and draw layer's, not done here): a part without `BuildingSREF` takes
the EXE's default t1 identity in its key, and the draw layer makes that texture; until then these 8 draws stay the
EXE's under ServerLandmarks.

**(b) The polder's normals: the EXE recomputes them [C for this mesh].** `render/evidence/0561/4878/polder_normals.py`
on Lind08 draw 982 (`ind_polder.gr2` mesh `berm2`, 314 vertices, the captured index and vertex buffers against the
file's vertices from `c5r_gr2_facts`): the captured normal equals the file's at 137 vertices and differs at 177 (median
6.5 degrees); it equals, at **all 314 vertices within 0.02 degrees**, the normalised sum of the unit normals of the
triangles that use the vertex (no weld of equal positions). Area-weighted sums fit 261, angle-weighted 261, welded
sums 5. So the EXE gives this skinned mesh recomputed, unweighted face-average normals; the 137 equal ones are where
the file's normals already were that. Not known: when the EXE does this (the deer and fish skinned samples of 4873 were
byte-equal with the file's normals; a flag of the file, of the material or of the mesh is likely [I]); the other three
polder draws were not checked. Until the condition is known the four polder draws stay the EXE's (identity set).

### 12.5 Items 3 and 4 (P7): the cost series on oldpc (2026-11-19; grants #6659, #6809)

(Note 2026-11-19, ticket 4887: these figures are of the build before 4884's model-pass cut, on the batch-4 pin. The
product's current figures are 12.8: the server's side moved from +2.66 to +0.20 ms CPU per frame, the EXE's side did
not move in substance.)

Method first (written before the runs), results below. The section names the method, the inputs and what is expected, so the numbers can be read against
it. Driver `render/evidence/0561/4878/cost_4878.py` = 4756's `cost_4756.py` (unit-draw-filter.md 4.1, the S3 series)
with the kinds, options, mode check and filter parse of the model options (offline checks: `test_cost_4878.py`, 6 of 6;
`--dry-run` plan 13 arms, about 78 min); build `build_4878.sh` (server.exe, proxy and composite of
`task-4878-acceptance` with the team branch 9befb6df9 merged, on the pin layout `x64-c29442e8-p58724e0`'s x64 DLL);
submit `submit_4878.sh` (oldpc, quiet and headed, huge save copy TBPP-HUGE_0350, camera 1 (85, 31)).

| Kind | `[Renderer]` | What it is |
|---|---|---|
| a | ServerRenderer 1, ServerUnits 0, ServerCities 0, ServerLandmarks 0 | the product today |
| b | ServerCities 1, ServerLandmarks 1 | the compositor drops and taps the EXE's BuildingShader colour draws, the server draws them (black until 4882; the work is the same) |

Warm-up b, then a,b / b,a turned over six rounds (6 counted arms each). Per arm: time to the map (load time), memory
after the load and at the end (server and EXE private bytes, the server's peak, video memory), the EXE's frame time idle
(45 s) and panning (20 s), the server's CPU and GPU per drawn frame, 2 AI turns. **P7** comes from two sides: b - a of
the EXE's frame time (the whole cost on the EXE's threads), and the compositor's own timers per EXE frame of the same
seconds (`modelfilter 1s:` draw_ms = the drop and tap path with the identity lookup, copy_ms / copy_kb = the Unmap copy,
publish_ms; divided by the `unitfilter 1s:` presents). Expected from 4874's Tiny loads (10.8, workstation, an
indication): 0.18 ms (draw path) + 0.28 ms (copy, 85 KB) + 0.12 ms (publication) per frame; a huge save draws more
family draws per frame, so the draw path should grow with them and the copy with the instance count. Kind b refuses a
window whose `modelfilter` lines show no drops of either bit or a frame unable; kind a refuses any drop.

Two jobs (grants #6659, #6809): job 1 `20261119-011000-2901` on the build of 9befb6df9 (before 4879's CPU-mesh copy
and 4882's fixes: black models and the 4 ms pair wait, which broke the composite's lockstep, 11.12), so its b arms
measure the filter and the tap without the CPU-mesh copy and with a server that waits; job 2 on the build of team
ca5a6ee7c (server.exe 0fcb39b5, proxy e0cf4668, composite 8e70c73f, the same x64 DLL 2b56a1cd): the product's P7 and the
baseline for 4884.

**Results [L, oldpc, quiet testbed, both jobs exit 0, 12 counted arms each, every arm restored and verified]**: job 2
`20261119-035744-1f09` (04:2x-05:5x EST; the product build) is the result; job 1 `20261119-011000-2901` (01:5x-03:48)
agrees on the EXE side. oldpc's RTX 2080, NVIDIA driver 32.0.16.1088 [I: from quiet-testbed.md 25, not read by this
job]; every arm an uncapped timing launch (`frame_cap.capped` false); huge save TBPP-HUGE_0350, camera 1. Means of 6 per
kind, b - a paired by repetition with the 95 % t interval (`render/evidence/0561/4878/cost-summary-job2.txt`,
`-job1.txt`):

| Metric (job 2) | a (options off) | b (ServerCities + ServerLandmarks) | b - a [95 %] | Job 1 b - a |
|---|---|---|---|---|
| EXE frame time, idle (ms) | 5.207 | 7.240 | **+2.033 [+1.848, +2.219]** (+39 %) | +2.267 |
| EXE frame time, pan (ms) | 5.536 | 7.756 | +2.221 [+2.110, +2.332] | +2.599 |
| compositor per EXE frame, idle: draw path (drop + tap + identity lookup) | 0 | 0.728 ms | +0.728 [+0.721, +0.734] | +0.747 |
| ... Unmap copy | 0 | 1.166 ms (594 KB) | +1.166 [+1.150, +1.182] | +1.180 (593 KB) |
| ... publication | 0 | 0.431 ms | +0.431 [+0.424, +0.438] | +0.443 |
| ... sum (P7) | 0 | **2.325 ms** | +2.325 [+2.302, +2.347] | +2.369 |
| family draws dropped / tapped per EXE frame | 0 | 449 / 449 | | 432 / 448 |
| server CPU per drawn frame (ms) | 7.788 | 10.451 | +2.662 [+2.501, +2.824] (+34 %) | +3.263 |
| server GPU per drawn frame (ms) | 1.093 | 1.282 | +0.189 [+0.174, +0.204] | +0.087 |
| server frames per second | 193.6 | 139.0 | -54.7 [-59.0, -50.3] | -58.2 |
| server private MB after the load / at the end | 1,614 / 1,868 | 1,829 / 2,161 | +216 [+178, +253] / +293 [+282, +304] | +97 / +286 |
| EXE private MB after the load / at the end | 1,430 / | 1,437 / | +7.3 [+0.2, +14.5] / +4.3 [-3.7, +12.2] | +8.6 / +4.1 |
| video memory (MB) after the load / at the end | | | +93 [+55, +132] / +42 [+16, +69] | -1 / +157 |
| **load time: launch to the map (s)** | 135.05 | 135.10 | **+0.05 [-0.15, +0.25]** (none) | -0.02 |
| model layer open after the load (s, on a worker) | | 14.04 (sd 0.20) | | 14.03 |
| AI turn wall time (s) / server CPU per AI turn (s) | 28.88 / | 29.24 / | +0.36 [-0.39, +1.10] / -1.3 [-2.6, +0.1] | +1.22 / +8.9 |

**Why the numbers moved (PERFORMANCE.md):** the EXE's frame grows by about what the compositor's own timers count on its
threads (2.33 ms of the 2.03 ms; the timers include time the EXE's render thread would otherwise spend waiting), so the
EXE-side cost is the model filter and tap, not the server: (1) the Unmap copy is half of it: about 594 KB per frame read
back from mapped upload memory (the slot-1 instance stream, the cb0/cb1 constants and the skinned palettes of 449 tapped
draws: about 510 MB/s, the slow read 4874 named as R2); (2) the draw path is 1.62 us per family draw on oldpc (0.728 ms
/ 449, the bound-state reads, the key's table lookups including the identity-set lookup, the record); the identity
lookup's share is not timed apart [I: small, one hash probe per part]; (3) the publication of a 3-slot frame image, about
0.43 ms. On 4874's Tiny workstation load the same path was 0.58 ms per frame (10.8), so it scales with the draws in view.
The server's model pass costs 2.66 ms CPU per drawn frame (its frame rate falls 28 % but stays near 139 per second,
above the EXE's 138); the 4 ms pair wait and the black models of job 1's build cost 0.6 ms more server CPU there. Load
time is unchanged because the model layer opens on a worker after the load (14.0 s), and the EXE does not wait for it.
Memory: the server holds the library (about 51 MB resident, landmark-renderer.md 3.4), the draw layer's GPU buffers and
textures, and the tap frames: about +290 MB private at the end of the huge save. **For the MC's Q2 condition (C1, MC
#5962):** the per-draw path including the identity lookup is 0.73 ms per frame on a huge save at 449 dropped draws
(1.6 us each); the larger cost is the Unmap copy. Not measured: the cost with ServerUnits = 1 (4884's DIAGNOSTIC A/B);
the per-frame cost while the EXE draws far more family draws (a bigger city view).

### 12.6 Item 2: the live mixes (c5h-e, c5h-b, 2026-11-19; DLL pin batch 4, before batch 5's MC #6989) [L]

Method (driver `render/evidence/0561/4878/run_4878_live.py` on 4882's, chain `chain_4878.sh`; grant #6809): the
Goldberg instance c5h-e (F:, `mods548`, Vibrant Textures; `provision.py align548` and `verify` rc 0 at 03:26-03:27; F:'s
read latency 03:27:27: median 0.83 ms, p90 1.69 ms over 40 random 64 KB reads, `f_latency.py`), the product path of
4882's live run (layout = the pin `x64-c29442e8-p58724e0`'s x64 DLL 2b56a1cd with server.exe 0fcb39b5, proxy e0cf4668
and composite 8e70c73f of `task-4878-acceptance` at the team branch ca5a6ee7c), one prep of the Tiny copy
C5H-4878-TINY_0018 (of C5H-3000-TINY-T0018, E337244C), one launch per arm: off1 and off2 (the product today, twice),
on, cities, landmarks, onu (all on with ServerUnits 1). Per arm: camera A (the capital) and B (3 east, 2 south): a
harness picture (DIAGNOSTIC) and the composite's presented and passthrough frames; two presented frames 1 s apart at A;
12 s still and 20 s of camera steps; one AI turn. What is compared: (1) drawn whole: each arm's presented frame against
its own passthrough frame on the EXE's opaque model pixels (what the EXE still draws is not clipped by the server's
models); (2) J (the arm against off1) beside C (off2 against off1, the two-session ceiling) on off1's EXE model pixels
(its passthrough frame's drawn mask: every model the EXE draws today) and on the whole kept screen area (shadows on the
terrain included); (3) motion: the share of off1's model pixels that change by more than 16 between the two frames 1 s
apart. **No ACCEPTANCE picture (D112)**: options 12 and 13 are not the release's default, so the full shipped system does
not draw them; every picture is DIAGNOSTIC.

**Run e1 (2026-11-19 03:28-04:02 EST; c5h-e released 11:00 after the usage-limit stop, restored by hash 04:01:39,
`backup_restore_ok` true, `provision verify` 0, `frontend.py restore` 0, status original) [L]:**

| Arm | Launch to map (s) | Model module | Compositor (totals) | Lockstep idle / steps (exact / older) | Box, crash |
|---|---|---|---|---|---|
| off1 | 140.3 | no `models:` line | nothing dropped | 399 / 0, 642 / 1 | none |
| on | 138.4 | layer open after 18.5 s, keys 5,539, env_missing 0, draw errors 0, drawn 5,565 frames | dropped cities 89,437, landmarks 148,525; passed while latched 255 / 596 (not in the set: 851); frames unable 0, place miss 0 | 396 / 1, 669 / 5 | none |
| cities | 235.7 | layer open after 25.5 s, keys 637 | dropped cities 114,591, landmarks 0; passed 16,833 (not in the set) | held (exact > older, no give-up) | none |
| landmarks | 442.6 | **never ready: D118 fatal step** (below) | set generation 0 | - | **the session ended (server exit 5, proxy FATAL exit 3)** |
| onu, off2 | not run (the chain stops at a failed arm) | | | | |

- **Arm landmarks took the D118 path [L for the timings; root cause below]:** the server's model layer opened on a worker at
  03:57:51 and was not open 30 s after the load's first drawn frame (03:57:53), so the mode writer's install bound
  (`render_models_install_ms` 30000, 11.4) took the D125 step: `LOUD: models: landmarks: the option is on but the module
  was not ready within render_models_install_ms=30000 ...`, server exit 5, the proxy's `FATAL: the server failed a call
  (on_server_death=exit): the process ends with exit code 3`. The open (library 624 files, wire index hashing every t0/t1)
  took 18.5 s in arm on and 25.5 s in arm cities of the same run, and the loads 138, 236 and 443 s: during arms cities and
  landmarks 0551's D10 save test was writing to `F:/c5-64bit-work/0551/d10/` (quietlock check of 03:50; 4883 measured F:
  at 120-286 ms read latency then, #6860), and the instance's install, the database copy and the library's files are on
  F:. **Root cause [L, I for the share of each]: the install bound counts from the load's first drawn frame, while the
  layer's open is disk-bound work of 14-26 s (14.0 s on oldpc's huge save, 12.5) that runs after it; on a slow or busy
  disk it passes 30 s and the option's working path is turned into the fatal one.** It is the D118 failure path working as
  built (no degraded picture, a report), but on a cause that is not a failure. Fixed by child 4885 (11.13: the install
  bound counts from the later of the first drawn frame and the layer's open end, with the open's own bounds); not rerun
  live by this ticket on 4885's build. Still open: a cheaper open (the wire index's texture hashes cached by file
  identity); on C: the open took 16.9-19.3 s in every arm of b1/b2.
- Pictures (Visual Checks, issue 4878, DIAGNOSTIC): off1, on, cities at cameras A and B. The compare of e1
  (`compare-r1.json`) has no ceiling (off2 did not run), so its J figures (on against off1 on off1's model pixels:
  capital within 16 0.830, camera B 0.626; cities 0.932 / 0.623) are not read yet; drawn whole without stock sessions
  fails on off1 itself (25 % within 16: the capital's light column and the translucent parts, as 4882 found), so the
  gate needs the SNAPSHOT rule with two sessions (off1 and off2 as stock sessions 1 and 2).

**Runs b1 + b2 on c5h-b (C:, the same build and pin: server 0fcb39b5, proxy e0cf4668, composite 8e70c73f of
`task-4878-acceptance` at team ca5a6ee7c; x64 DLL 2b56a1cd of the pin `x64-c29442e8-p58724e0`, batch 4; 2026-11-19 b1
11:14-11:25 off1, on; b2 11:58-12:22 cities, landmarks, onu, off2; two preps of the same save copy; every restore by hash
rc 0, `provision verify` 0, `frontend.py restore` 0; c5h-b released 12:22) [L]:** every arm's run and check rc 0, no box,
no crash, no dump. b1's chain stopped after `on` because 4884's quiet hold refused the next launch (arm `on` overlapped
4884's first window, which 4884 discarded, #6952); b2 ran the other four after 4884's series.

| Arm | Map (s) | Layer open (s) | Dropped cities / landmarks (totals) | Passed while latched (not in the set) | Lockstep idle / steps exact:older |
|---|---|---|---|---|---|
| off1 | 132.0 | - | 0 / 0 | - | 397:0 / 608:2 |
| on | 114.2 | 18.2 | 83,419 / 135,609 | 216 | 398:0 / 670:1 |
| cities | 117.3 | 19.3 | 101,201 / 0 (set keys 637) | 12,172 | 399:0 / 610:1 |
| landmarks | 109.9 | 18.8 | 0 / 144,720 (set keys 4,910) | 198 | 398:0 / 615:1 |
| onu (units on too) | 110.2 | 16.9 | 81,885 / 142,515 (set keys 5,539) | 153 | 397:0 / 643:2 |
| off2 | 110.6 | - | 0 / 0 | - | 398:0 / 613:1 |

(Per arm `C:/c5-64bit-work/0561/4878/live/work-b{1,2}/check-<arm>-r1.json`; every check's points m, f, l, d hold: only the
arm's own bits dropped, frames unable 0, place miss 0, env_missing 0, draw errors 0, exact > older with no give-up in
the still and step windows. Arm cities alone passes 12,172 draws while latched: the family draws whose key the cities-only
set does not hold (with both options the same run passes 216) [I: the skinned parts and keys shared with the landmarks
class, left out of a one-bit set by 11.3's rule].) **Arm landmarks alone holds on C:** (its layer open 18.8 s, under the 30 s bound
that failed it on a busy F: in e1), so the e1 failure is the install bound's, not the arm's.

**The pictures against the two-session ceiling** (`compare-r1.json`, off1 and off2 as stock's two sessions; within 16 as a
share of the region's pixels):

| Camera A (capital) / B (near) | models region: within 16, median, p95 | whole kept screen: within 16 | drawn whole (SNAPSHOT rule): within 16, gate |
|---|---|---|---|
| C = off2 vs off1 (the ceiling) | A 0.785, 6, 36 / B 0.768, 7, 41 | A 0.784 / B 0.895 | off1, off2: 100 %, pass |
| on | A **0.898**, 3, 40 / B 0.784, 7, 64 | A 0.852 / B 0.947 | A 96.4 % **fail** (one blob) / B 98.6 % pass |
| cities | A 0.795, 5, 39 / B **0.675**, 10, 69 | A 0.833 / B 0.909 | A 99.7 % pass / B 98.9 % pass |
| landmarks | A 0.797, 6, 41 / B 0.806, 6, 40 | A 0.765 / B 0.876 | A 96.8 % **fail** (the same blob) / B 99.7 % pass |
| onu | A 0.796, 6, 39 / B 0.751, 8, 43 | A 0.768 / B 0.893 | A 97.3 % (the same blob under 75 %: fail) / B 99.7 % pass |

- **Models class [L]:** every arm's J is within the two sessions' own spread or above it (the ceiling C is 0.77-0.79 on the
  model pixels: idle units, water and the random layouts differ between two loads of the same save), except camera B in
  arm cities (0.675) and, slightly, arm onu (0.751). Read as reported, not as a gate (COMMON 25, `model_checks.py`: the
  class is not a gate until a few acceptance runs show its spread); the pixel-faithful proof stays the offline one (12.1:
  median 0, p95 0 on every frame). Not checked: what in camera B of arm cities differs (a random block layout of the
  capital's district is the likely cause [I]: the EXE picks blocks per load, the tap carries the EXE's pick).
- **Drawn whole: one failing blob, in every arm with ServerLandmarks on and in no other [L]:** 919 px at x 536-591, y
  1151-1234 of camera A, 20-21 % within 16: EXE-drawn grain (opaque in both off sessions and in the passthrough frame) is
  covered by a white haze in the presented frame of arms on, landmarks and onu (Visual Checks vc-20261119-122449-
  workstation-9a82, DIAGNOSTIC: presented, passthrough, off1). *Corrected 2026-11-19 by ticket 4886 (12.7): the cause is
  known and it is not a clip. The grain is the wheat resource model, a landmark: the server draws it in these arms, and
  the server's cloud planes are drawn over it, as stock does. off1 and off2 stood in for stock sessions, and they carry
  the deviation of fog-of-war.md 5.4 (the EXE's models drawn over the clouds). Against two real stock sessions the
  gate passes at camera A in on, landmarks and onu (99.58 / 99.88 / 99.74 %) [L]. No product fix is needed. Replaced: "Cause
  open [I: a translucent model the server now draws ... is composited over an EXE-drawn grain pixel that should lie in
  front of it]".*
- **Motion (animated models) [L, a weak check]:** the share of off1's model pixels that change by more than 16 between two
  presented frames 1 s apart: off1 0.048, off2 0.071, on 0.051, cities 0.095, landmarks 0.102, onu 0.072. Every arm moves
  within the off sessions' spread or more; this does not tell the 22 animated landmarks apart from units and water, and
  a deferred skinned draw's placement is shown only by `place_miss` 0 in every arm (10.5). [I] that the animated models
  move right; a per-model check needs the skinned models' pixel masks (the id pass of the offline harness).
- **R1 (pan series) [L]:** in every model arm the lockstep stays exact while the camera steps (exact 670 against older 1
  in arm on), the server's camera rows shift 0 px while still and up to 235 px are avoided while stepping (4882's
  frame-camera rows, 11.12), so the models are drawn at the frame's own camera; the edge-pixel count of R1 was not taken
  separately.
- **Shadows** between server- and EXE-drawn models: looked at once, camera A, arms off1, cities and landmarks side by side
  at reduced scale (900 x 600 px of the centre at 2/3; `C:/c5-64bit-work/0561/4878/live/shadows.png`, scratch): the city
  blocks' and the improvements' shadows on the ground are there in all three and no doubled shadow shows; the block layout
  differs per load (the EXE's random pick) [L, by eye only, not measured, not at full scale].
- **The identity set live [L]:** every model arm drops only keys the server published (set keys 5,539 with both options,
  637 with cities alone) and passes the rest while latched (`passed_set` > 0: 216 in arm on, the draws left with the EXE).

### 12.7 The white haze over the grain at camera A: stock's cloud planes over a server-drawn landmark (ticket 4886, 2026-11-19)

Written 2026-11-19 12:32-13:10 EST by worker-4886 (account B, Opus 5.5) on `task-4886-landmarks-haze` (team branch
58039ef12). The cause was found offline. Two real stock sessions then ran on c5h-c (grant MC-B 12:38; the stock
result is at the end of this section). Inputs: 4878's kept frames and logs of b1/b2 (`F:/c5-64bit-work/0561/4878-keep`:
`pics/tiny-<arm>-r1/<arm>_capital_{presented,pt}.png`, `logs/<arm>/shell_composite.log`; batch-4 pin c29442e8, Tiny copy
C5H-4878-TINY_0018, camera A = the capital (8, 23), 2554 x 1411). Scripts: `render/evidence/0561/4886/crop_blob.py` (the
crops) and `haze_fit.py` (the fit below). Marks: `[C]` checked by this ticket in code or on those frames, `[L]` the game
as cited, `[I]` inferred.

**What the blob is [C on the frames, I for the model's name].** The 919 px of 12.6 lie on the grain of the wheat resource
on a plot at the edge of the unexplored clouds, west of the capital. The plot shows the wheat resource icon, and the
grain is a BuildingShader landmark (`wheat_decal.gr2` and its kin, landmark-renderer.md 7.4) [I: from the icon and the
look; no per-draw capture of camera A exists]. It is a landmark-class draw, so the server draws it whenever
ServerLandmarks is on. That fits the arms: the haze is in on, landmarks and onu, and not in cities (12.6: camera A
passes at 99.7 % there). In off1 and off2, the grain at the western edge stands opaque over the white clouds. In the
three landmark arms, the same grain fades into them.

**The haze is an alpha blend of one cloud colour [C, `haze_fit.py`, 2026-11-19 12:36 EST].** Per pixel of the box (x
536-591, y 1151-1234), each landmark arm's presented frame was modelled as `arm = (1 - a) * off1 + a * C`: off1's
presented frame is the same grain unhazed, C is one colour for the whole box and a is fitted per pixel.

| Arm | Against off1 as is: median, within 16 | Fitted C | Fit residual: median, p95, within 16 | a by 8-px column, west to east |
|---|---|---|---|---|
| on | 20, 0.465 | (208, 212, 216) | 1.1, 4.2, 1.000 | 0.55 0.37 0.26 0.19 0.08 0.05 0.02 |
| landmarks | 18, 0.487 | (212, 217, 219) | 1.2, 5.1, 0.999 | 0.51 0.34 0.24 0.17 0.08 0.05 0.02 |
| onu | 19, 0.478 | (212, 217, 220) | 1.1, 5.0, 1.000 | 0.53 0.35 0.25 0.18 0.08 0.05 0.02 |

off2 against off1 in the same box: median 1, p95 4, within 16 1.000 (the two sessions' own spread). So under the haze
the server draws the grain as the EXE does, to within the two sessions' spread. Over it lies one translucent, near-white
layer whose opacity falls smoothly from about 0.5 at the western (unexplored) side to 0 within about 55 px. That is the
profile of the cloud planes' edge (fog-of-war.md 3.5: alpha = mask.y x cloud alpha, falling to 0 about 0.4 of a plot
beyond the unexplored hexagons). No model is translucent there.

**Why the server's grain is covered and the EXE's is not [C code, L stock].**

- Stock draws the models first. Then come FOW CULL, the three FOWBaseLayer cloud planes (SRC_ALPHA / INV_SRC_ALPHA,
  depth test LESS_EQUAL, no depth write) and the particles, all over the finished world, models included
  (fog-of-war.md 2.3 [L], 3951's captures). So stock's cloud edges cover models near unexplored plots.
- The server keeps that order. `Scene::draw` (`server/src/render/srv_render_scene.cpp`) calls `m_models->drawScene`
  (and the units' `drawScene`) before `m_fow.afterWorld`, which draws the cloud planes. A model the server draws is
  covered by the cloud edge as in stock [C].
- The EXE's own models are drawn after the server's frame. The compositor inserts that frame after the scene clear,
  before the EXE's world draws (11.12 (2)). Every model the EXE still draws is therefore drawn over the server's clouds.
  This is the deviation already named in fog-of-war.md 5.4 ("models stay uncovered under cloud edges"). Before this
  ticket it was not measured, because no model stood near an unexplored edge at that ticket's cameras. The grain here is
  such a model in off1 and off2.
- The passthrough frame cannot show this [C, `unit_filter.cpp`]. With `mode=passthrough` the composite passes
  `compositeOn = 0`, so the latch reads `UFL_NOTCOMPOSITE` and drops nothing. The passthrough frame is the EXE drawing
  every model, the wheat included, over the clear colour, with no clouds at all. Its "opaque grain" is right for that
  frame, and says nothing about who draws the grain in the presented frame.

**Root cause [C for each link above; L that stock hazes this grain, the stock sessions below].** The drawn-whole gate's blob is
a false failure. With ServerLandmarks on, the server draws the wheat resource itself and covers it with its cloud
planes in stock's order. The gate compared that against substitutes for stock sessions (off1 and off2) that carry
fog-of-war.md 5.4's deviation, and against a passthrough frame without drops. 4878's guess (a translucent server model
over EXE grain) is replaced: the translucent layer is the cloud planes, and the grain is the server's. The landmark arms
are closer to stock here than the product without them.

**Fix: none in the product (cost 0).** For the gate (not done here):

- (a) Run two real stock sessions at camera A (done by this ticket, below). A stock session that shows the haze takes
  the blob out of the gate. `snapshot_opaque` counts a pixel as opaque only where the passthrough frame equals both
  stock sessions within 8, and the passthrough's unhazed grain differs from the hazed stock. `drawn_whole` can also read
  `min(diff to passthrough, diff to stock session 1)` when given `stock1`, which 4878's compare does not pass. With off1
  and off2 as the "stock" sessions, the passthrough grain equalled theirs, so the blob was gated.
- (b) Or let `drawn_whole` leave out the pixels of models the server draws (the arm's dropped keys). That needs a
  per-pixel id the live run does not record today.

The deviation of 5.4 remains for every model the EXE still draws: cities with ServerCities off, the draws left with
the EXE (12.2), and, with options 12 and 13 off, all of them. Removing it would need the cloud planes drawn after the
EXE's world draws, a second insertion point. That is not proposed here.

**The two stock sessions [L, c5h-c, 2026-11-19 12:51-12:56 EST].** Setup:

- Driver and chain: `render/evidence/0561/4886/stock_4886.py` and `chain_4886.sh`, built on 4107's stock arm.
- The standard files were backed up by SHA-256 first. The 32-bit pin's instance-aware DLL i0ship-5d18d3d6-ia (batch 5,
  `E72F0880...`, read from 0551's layout on F:) went into the CP slot, with darkred's stock config, no proxy and no
  server.
- The save copy C5H-4886-TINY_0018 of C5H-3000-TINY-T0018 (E337244C, turn 18) was loaded, and the load screen left by
  `frontend.ensure_in_game`.
- Camera A: popups closed, `UI.LookAt` the capital (8, 23), the cursor over the minimap, then two PrintWindow shots 4 s
  apart (2554 x 1411, Visual Checks issue 4886, `info`).
- Per session: stop, `frontend.py restore` rc 0, the standard files restored by hash (all `equal` or `absent`) and the
  save copy removed. `provision verify` rc 0 before and after. The guard check rc 0, no box, the D155 marker clear at
  the end. Claimed 12:44, released 12:57.
- A first try (12:44-12:50) shot the leader's load screen still fading, because the driver did not leave it. Discarded;
  `ensure_in_game` added.

Pictures, not committed (D83, COMMON 27): `F:/c5-64bit-work/0561/4886/stock{1,2}/`. Figures from
`stock_compare.py <4878-keep> F:/c5-64bit-work/0561/4886` (written beside them as `compare-4886.json`):

| Camera A | off1 | on | landmarks | onu | stock2 (stock's own spread) |
|---|---|---|---|---|---|
| blob box against stock1: median, p95, within 16 | 27, 104, 0.271 | 11, 39, 0.758 | 10, 39, 0.754 | 11, 39, 0.723 | 2, 7, 0.999 |
| drawn whole, off1 and off2 as stock (4878's form) | 100 %, pass | 96.4 %, fail (919 px) | 96.8 %, fail (913 px) | 97.3 %, fail (919 px) | - |
| drawn whole, the real stock sessions | 99.89 %, pass | **99.58 %, pass** | **99.88 %, pass** | **99.74 %, pass** | - |
| the same with drawn_whole's stock1 exemption | 99.91 %, pass | 99.59 %, pass | 99.89 %, pass | 99.74 %, pass | - |

- **Stock hazes the grain [L].** By eye, stock's wheat at the western edge fades into the clouds as in the landmark arms,
  and the off arms' does not. In the blob box the landmark arms are about 3 times closer to stock than off1. The
  `stock_compare.py` self-test, with off1 and off2 copied in as "stock", gave 4878's numbers again (96.4 / 96.82 / 97.27,
  the blobs of 919 / 913 / 919 px).
- **With real stock sessions the gate passes in every landmark arm.** Under the SNAPSHOT rule only 56 px of the box stay
  opaque, against about 1,010 with off1 and off2 as stock.
- **Not equal to stock either [L figures, I cause].** The haze over the grain is stronger in stock: `haze_fit` against
  off1 gives a mean a of 0.46 in both stock sessions against 0.20-0.22 in the landmark arms, still 0.3 at the box's
  eastern edge, with a warmer fitted colour (about (206, 195, 160)) and a worse fit (residual median 5, p95 14). This
  is consistent with the cloud phase (FOWAnim T from the server's clock) and the particles' look-alike, both named in
  fog-of-war.md 5.4 and 10. Not separated here. Over the whole kept area every product arm is at 0.65-0.66 within 16 of
  stock1, against stock's own 0.74. The 4878 arms ran on another instance (c5h-b) and the batch-4 pin, so that figure is
  not a gate.

**Not verified.** The draw's identity (no per-draw capture of camera A). Which of the cloud phase and the particles
makes stock's haze stronger. The other EXE-drawn models next to cloud edges in the off arms (not counted).

### 12.8 P7 rerun after 4884, batch 5 (ticket 4887, worker-4887, 2026-11-19; MC #7034: the 0.1b release notes' figures)

Method first (written before the run). The series of 12.5 repeated unchanged on the merged team build, so the two read
side by side; only the build and the pin differ. Driver `render/evidence/0561/4887/cost_4887.py` = `cost_4878.py` with
the ticket labels changed (offline checks `test_cost_4887.py` 6 of 6; `--dry-run` plan 13 arms: W0-b warm-up, then R1-a
R1-b R2-b R2-a ... R6-a, 6 counted per kind, about 78 min); build `build_4887.sh` (server.exe, proxy and composite of
`task-4887-p7-rerun` = the team branch fdbb60f7a, which has 4884's cut of section 14 and everything merged through 4883;
server.exe and the proxy built with `--deps` of a release `build_all` of the fork at the batch-5 pin 1fd51353e (MC #6989,
deps/dll.lock x64-batch5), the layout's bin = ticket 0551's validated take of the same commit
`F:/c5-64bit-work/0551/layouts/x64-1fd51353-p1e09fc7`, x64 DLL `78355A75...`, read only); submit `submit_4887.sh`
(oldpc, quiet and headed, huge save copy TBPP-HUGE_0350, camera 1 (85, 31), 45 s idle, 20 s pan, 2 AI turns per arm).
Kinds a and b as in 12.5. Statistic: per kind the mean of 6 arms with sd and 95 % t interval; b - a with Welch's 95 %
interval and the smallest difference the series could detect (PERFORMANCE.md 2.3); 4878's paired-by-repetition
interval is given beside it where the two differ. Trust grade A when the job exits 0 with 6 counted arms per kind and the
quiet lock held (RELEASE-GRADE in the release notes' sense); otherwise labelled DIAGNOSTIC.

Expected against 12.5 (job `20261119-035744-1f09`, batch 4, before 4884): (1) **the server's CPU per drawn frame** falls:
4884's cut took 0.3-0.57 ms off the model path per frame offline on the workstation (14.4: Fanc01b 0.853 -> 0.284 ms);
on oldpc with 449 dropped draws per frame the model pass cost +2.66 ms before, so a fall of about 1-2 ms is plausible
[I], and the server's GPU per frame should fall too (14.2: most of arm A's GPU interval came with the per-pass buffers);
(2) **the EXE side should not move**: 4884 changed only the server; the compositor's draw path, Unmap copy (about 594
KB) and publication are the same code (12.5's +2.03 ms idle stays within its interval) unless the merges after 4878
(4879's CPU-mesh copy was already in job 2; 4882, 4885, 4883) changed the tap; (3) memory and load time about as before
(the cut keeps one DYNAMIC row buffer instead of per-pass IMMUTABLE buffers: video memory may fall a little [I]).
The batch-5 DLL changes the game after its first AI turn only through the finder fix (deps/dll.lock); the AI turn
times are compared within this job only.

**Results [L, oldpc, RELEASE-GRADE: PERFORMANCE.md trust grade A]**: job `20261119-145345-9cc3`, 2026-11-19 14:54-16:12
EST, exit 0, 13 arms (W0-b warm-up + 6 a + 6 b counted), every arm's mode check and filter check passed, none frame-capped
(`frame_cap.capped` false), server alive at every arm's end, restore by SHA-256 ok (`backup_restore_ok`, frontend restore
rc 0); no crash, dump or box (D155). Inputs: server.exe `8A83AE71...`, proxy `C5D1C7C0...`, composite `AA005CEE...` (built
2026-11-19 14:50-14:53 EST from team fdbb60f7a, whose product files equal main 6e8d5006f's, the 0561 merge), x64 DLL
`78355A75...` (batch 5, dll_pin 1fd51353e). oldpc's RTX 2080 with NVIDIA driver 32.0.16.1088 (read by `wmic` on oldpc
after the job [C]; D175: unchanged since 4878's job). Machine during the series: testbed quiet summary `alone=True`,
total CPU mean 53.1 %, max 86.8 %; foreign cores mean 0.625 (limit 0.1, so the runner marks it `undisturbed=False`), of
which tvnserver.exe (the headed session's VNC server) 2,102 of 2,937 foreign CPU s, then svchost 356, MsMpEng 211: the
same picture as 4878's jobs (0.623 / 0.665), so the two series are comparable; free commit 7.0-7.1 GB at each arm's start,
2.5-2.9 GB at its end. Every arm logged about 150 LOUD lines of ticket 0576's stale-unit guard (`remote: LOUD: stale Unit
instance ...`, Lua calls with dead units on the huge save, refused before the DLL; in both kinds alike; 4878's build had
no such guard): not a fault of this series. Per-kind means of 6, b - a with Welch's 95 % interval
(`render/evidence/0561/4887/cost-summary.txt`, the job's own; `welch-vs-4878-job2.tsv` by `welch_4887.py`); 4878's job 2
(12.5) beside it, and new - old of b - a with Welch's interval:

| Metric | a | b | **b - a [Welch 95 %]** | 4878 b - a | new - old of b - a [95 %] |
|---|---|---|---|---|---|
| EXE frame time, idle (ms) | 5.188 | 7.055 | **+1.866 [+1.823, +1.910]** (+36 %) | +2.033 | -0.167 [-0.327, -0.007] |
| EXE frame time, pan (ms) | 5.536 | 7.760 | **+2.224 [+2.057, +2.392]** (+40 %) | +2.221 | +0.004 [-0.185, +0.192] |
| compositor per EXE frame, idle: draw path (drop + tap + identity lookup) (ms) | 0 | 0.716 | +0.716 [+0.710, +0.722] | +0.728 | -0.012 [-0.020, -0.004] |
| ... Unmap copy (ms / KB) | 0 | 1.102 / 596 | +1.102 [+1.089, +1.114] | +1.166 / 594 | -0.064 [-0.082, -0.046] |
| ... publication (ms) | 0 | 0.406 | +0.406 [+0.403, +0.410] | +0.431 | -0.025 [-0.032, -0.018] |
| ... sum (P7) (ms) | 0 | **2.224** | +2.224 [+2.214, +2.233] | +2.325 | -0.101 [-0.124, -0.079] |
| family draws dropped / tapped per EXE frame | 0 | 448 / 448 | | 449 / 449 | |
| **server CPU per drawn frame, idle (ms)** | 7.714 | 7.918 | **+0.204 [+0.072, +0.335]** (+2.6 %) | +2.662 | **-2.459 [-2.630, -2.288]** |
| server CPU per drawn frame, pan (ms) | 7.569 | 8.824 | +1.255 [+0.987, +1.523] | +2.450 | -1.195 [-1.546, -0.844] |
| server GPU per drawn frame (ms) | 1.096 | 1.143 | +0.047 [+0.038, +0.056] | +0.189 | -0.142 [-0.161, -0.123] |
| server frames per second, idle | 194.2 | 142.8 | -51.4 [-53.5, -49.4] | -54.7 | +3.3 [-1.2, +7.7] |
| server CPU per AI turn (s) | 46.22 | 40.69 | -5.53 [-5.96, -5.10] | -1.27 | -4.26 [-5.31, -3.21] |
| AI turn wall time (s) | 29.10 | 29.26 | +0.16 [-0.22, +0.54] | +0.36 | -0.20 [-0.96, +0.56] |
| server private MB after the load | 1,596 | 2,061 | +465 [+444, +486] | +216 | +250 [+212, +287] |
| server private MB at the end / peak at the end | 1,869 / 1,910 | 2,143 / 2,179 | **+274 [+268, +280]** / +269 [+262, +275] | +293 / +292 | -19 [-30, -8] / -24 [-32, -16] |
| EXE private MB after the load / at the end | 1,428 / 1,523 | 1,439 / 1,526 | +10.3 [+6.4, +14.1] / +3.1 [-4.4, +10.6] | +7.3 / +4.3 | +3.0 [-4.4, +10.3] / -1.2 [-10.5, +8.2] |
| video memory MB after the load / at the end | 1,061 / 936 | 1,353 / 908 | +292 [+262, +321] / -28 [-136, +80] | +93 / +42 | +199 [+158, +239] / -70 [-179, +38] |
| **load time: launch to the map (s)** | 134.90 | 134.90 | **+0.00 [-0.24, +0.24]** (none) | +0.05 | -0.05 [-0.35, +0.25] |
| model layer open after the load (s, on a worker) | | 14.06 (sd 0.20) | | 14.04 | |

The smallest differences this series could detect (80 % power, Welch): EXE idle 0.061 ms, pan 0.230 ms; server CPU per
frame 0.182 ms idle, 0.373 ms pan; server private at the end 7.8 MB; load time 0.33 s (`welch-vs-4878-job2.tsv`, `det`).
The job's paired-by-repetition intervals (the driver's own, as 12.5 gives) agree: EXE idle +1.866 [+1.820, +1.913],
server CPU +0.204 [+0.013, +0.394].

**Why the numbers moved (against 12.5):**
- **The server's model pass is now nearly free per frame [L for the numbers, C for the code change, I for the per-call
  split]:** +0.20 ms CPU per drawn frame idle instead of +2.66 ms (-2.46 [-2.63, -2.29] ms), +1.26 instead of +2.45
  panning, and +0.05 instead of +0.19 ms of GPU. That is 4884's cut (14.3): the scene pass's context calls fell from
  13.3 to 3.3 per item, the per-item texture and map lookups went, the frame's row and constant buffers are kept DYNAMIC
  buffers written with `Map(WRITE_DISCARD)` instead of new IMMUTABLE buffers per pass, and resolve / checkFrame lost their
  per-draw vectors and strings. Offline on the workstation the cut took 0.57 ms off Fanc01b's 534 scene items (14.4);
  live on oldpc's huge save, 448 tapped draws plus their reflection-pass copies, it took 2.46 ms off, about 4x the
  offline figure. Why the live gain is larger is not separated [I: oldpc's slower CPU, about twice the items per frame
  with the reflection pass, and the driver's work for the per-pass `CreateBuffer` / `Release` of the row block and each
  tapped constant inside a running game, which the offline bench saw on an otherwise idle device]. The pan's larger rest (+1.26 ms) is the work that changes with the view [I: new tap draws to
  resolve each frame and files prepared on first draw]. Every number on the server's side that 4878 named as the model
  pass's cost moved; the a arms did not (new - old of a: server CPU -0.07 [-0.17, +0.03], GPU +0.003 [-0.015, +0.022]).
- **Server CPU per AI turn fell further (-5.5 s against a, was -1.3 s) [L, I for the split]:** the server draws at the
  EXE's present rate, which drops with b (during the two AI turns 102 against 137 frames per second in both jobs), so b
  draws about a quarter fewer frames; with the model pass no longer adding 2-3 ms to each of them, b's whole turn costs
  less server CPU (81.4 s against a's 92.5 s over the 61 s turn window; 4878: 89.6 against 92.1). The AI turn's wall time
  does not move (+0.16 [-0.22, +0.54] s).
- **The EXE side did not move in substance [L; C: `shell/` has no change between 4878's build ca5a6ee7c and fdbb60f7a,
  `git diff --stat`].** The EXE's frame time grows by +1.87 ms idle (+36 %) and +2.22 ms panning, the compositor's own
  timers count 2.22 ms per frame on its threads (draw path 0.72, Unmap copy 1.10 for 596 KB, publication 0.41), so the
  cost on the EXE is still the model filter and tap, not the server. Against 4878 the compositor's sum is 0.10 ms lower
  (-0.124 to -0.079) and the EXE's idle b - a 0.17 ms lower (-0.327 to -0.007), with the same code and the same draws
  (448 against 449 per frame, 596 against 594 KB): between-day variation, or less contention for oldpc's cores and memory
  bandwidth now that the server spends 2.5 ms less CPU per frame beside the EXE [I: not separated; the copy, which reads
  mapped upload memory, moved most]. The pan figure is equal (+0.004 [-0.185, +0.192]).
- **Memory: the same at the end, earlier after the load [L numbers; I cause].** At the end of an arm the options cost
  +274 MB of server private bytes (4878: +293; new - old -19 [-30, -8]) and the server's peak is 24 MB lower than with
  4878's build; the EXE's own memory is unchanged (+3 MB). After the load (the sample about 143 s into the server's run,
  77-79 s after the layer opened in both jobs, the model stats line identical in state: layer 1, bits 0x6, about 10,800
  frames drawn) b's server holds +465 MB and +292 MB of video memory over a, against +216 / +93 MB in 4878: b's memory
  now stands near its end value right after the load (2,061 -> 2,143 MB over the arm) where 4878's grew during the arm
  (1,829 -> 2,161 MB). (Corrected 2026-11-19, ticket 0579, 12.9: the cause is the whole-map bake-ahead, which ends 25 s
  sooner in b after 4884's cut (74 s after the load's big step against a's 99 s; before the cut 103 s against 98 s), so at
  the sample, 60 s after the step, b is near its end value and a still about 300 MB below its own; the model layer's own
  memory is +245 / +275 MB from before the step on (its open and prepare: +255 MB private with nodraw on the workstation),
  the frame buffers are a few KB plus one row buffer under 1 MB, and 4878's per-pass buffers cost 28 MB more. Replaced:
  "[I: the kept DYNAMIC buffers and per-file caches of 4884 reach their size at the first frames, where the per-pass
  IMMUTABLE buffers of 4878's build were released and their memory reused by the driver over time; not profiled]".) Video memory at the end is noisy in this job (sd 83-85 MB in both kinds against
  8-19 in 4878's), so its b - a (-28 [-136, +80]) says only that the end difference is within about 140 MB.
- **Load time unchanged (+0.00 s [-0.24, +0.24])**: the model layer still opens on a worker after the load (14.06 s,
  4878 14.04 s), the EXE does not wait for it.

**For the release notes (MC #7034), measured on oldpc (RTX 2080) on the huge save at camera 1:** with cities and
landmarks ON the EXE's frame time rises by 1.9 ms idle (5.2 -> 7.1 ms, about 193 -> 141 frames per second) and 2.2 ms
panning; the cost is the compositor's capture of the EXE's 448 city and landmark draws per frame (2.2 ms on the EXE's
threads, half of it the read-back of about 600 KB per frame); the server's own drawing of them costs 0.2 ms of CPU per
frame idle (1.3 ms panning) and 0.05 ms of GPU; memory +274 MB in the server at the end, nothing in the EXE; the time
to load the map does not change. Not measured: ServerUnits = 1 together with the model options; a view with many more
family draws than camera 1's 448; other GPUs.

### 12.9 The server's memory after the load, by allocation site (ticket 0579, impl-0579, 2026-11-19)

Why: 12.8 found b's server memory near its end value right after the load (+465 MB private, +292 MB video local over a,
against 4878's +216 / +93) and named the cause only as [I] (4884's kept DYNAMIC buffers and per-file caches).

**Method (written before the runs).** A DIAGNOSTIC server.exe, never shipped: `render/evidence/0579/diag0579.patch` on
this branch's tree (main b581b5ceb, which carries 0561 and 4884's cut) adds
- counters at the model layer's allocation sites: `ModelLibrary::resident` (the CPU copies of each prepared file's vertex
  and index bytes, kept for the session), `ModelRenderer::fileBuffers` (the files' IMMUTABLE GPU buffers),
  `ModelRenderer::texture` (the t0 / t1 textures, all subresources), `uploadFrame` (the frame's constant buffers held, their
  bytes, the row buffer's size, CreateBuffer and Map(WRITE_DISCARD) calls), the passes drawn, the CPU-built mesh store, and
  the layer's stages (the open's steps: files, art catalogue, library, wire index, programs, PS inputs; the renderer made;
  the PS inputs made; every file prepared);
- a sampler in the render host's loop: process private bytes, working set, peak private, and DXGI
  `QueryVideoMemoryInfo` local and non-local `CurrentUsage`, once a second before the load mark, then at every drawn frame
  (at least every 10 ms while the loop runs) for `seconds` after it, into a reserved buffer (no growth of its own in the
  window), written once as a TSV. The load mark is the first `ICvPlayer1::SetupGraphical` call of a load, set in every
  arm (kLoadBegin itself is pushed only with the unit feed or the model options on, `srv_remote_main.cpp`);
- two modes for the attribution, chosen by `VP64\diag0579.txt` (absent: the product's behaviour): `nodraw` (the layer opens,
  prepares every file and accepts the tap; `C5rLayer::draw` returns before the renderer, so no frame buffer and no draw)
  and `perpass` (`uploadFrame` makes new IMMUTABLE row and constant buffers with the frame's data in every pass and
  releases the previous pass's: 4878's lifetime before 4884's cut, 14.3).
Built 2026-11-19 17:44-17:46 EST (release, `build_server.py --deps`): server.exe `3143FAC3...`, with the product proxy
`FCC3F674...` and composite `D6A5CADA...` of the same tree (shell/ and proxy/ unchanged by the patch); x64 DLL batch 5
`78355A75...` from 0551's layout; the fork source at the batch-5 pin 1fd51353e; the stub libraries of batch 4's release
build_all, whose three stub DLLs are byte-equal to the batch-5 layout's (`setup_0579.sh deps` checks the SHA-256).
Live: Goldberg c5h-a (C:, `mods548`, Vibrant Textures; granted by MC-B #7143), the product path of 4884's driver
(`run_0579_live.py`, `chain_0579.sh`: prep and restore by SHA-256, `frontend.py restore` after the prep, D155 watcher),
the huge save copy C5H-0579-HUGE_0350 of C5H-VP8P-HUGE_0350 (`c499d3c6...`, turn 350; the save of 4887's TBPP-HUGE_0350),
camera at plot (85, 31) (4887's camera 1), ServerUnits 0, no AI turn, the sampler's window 240 s after the mark; arms off
(options 0), on (options 1), nodraw, perpass, interleaved, two runs each. Memory is not timing (no quiet-lock hold); the
workstation's numbers are like-for-like within this set only (other GPU and driver than oldpc's 12.8).

Expected [I]: the open and the prepare are the same code in 4878's and 4887's builds (`ModelRenderer::prepare` and the
library's resident copies are unchanged by 4884), so if 4884 moved memory to the load, nodraw sits below on, perpass
below on right after the first draws and on's surplus is in the driver's memory behind the kept DYNAMIC buffers (renamed
on every WRITE_DISCARD, about two passes per frame); if perpass equals on, 12.8's [I] is wrong and the cause lies in what
the two builds share.

**Results [L, workstation c5h-a, 2026-11-19 18:17-19:12 EST; DIAGNOSTIC: diagnostic server, the harness's 30 fps frame
cap (`frame_cap.capped` true, the EXE's Present held to 30 per second), NVIDIA GeForce RTX 4080; like-for-like within this
set only].** Chain `chain_0579.sh` (first prep 18:16 refused at set-proxy's argument check for a missing proxy PDB in the
layout after its backup only: restored by hash, provision verify ok, layout fixed), arms in the order off, on, nodraw,
perpass, on, off, perpass, nodraw, every run rc 0, no box, crash or dump, restore by hash ok, provision verify rc 0. The
sampler's files and the tables: `render/evidence/0579/summary.txt`, `per-second.tsv` (`analyze_0579.py`; the TSVs
themselves stay on C:). Per run about 6,350 drawn frames in the 240 s window (the cap).

The model layer's allocation sites, the same in all six runs with the layer (on, nodraw, perpass; counters [C] in the
diagnostic build, memory [L]):

| Stage (s after the load mark, on r1 / r2) | private MB | video local MB | what the counters show |
|---|---|---|---|
| open (worker; 0.3 -> 19.4 / 19.1 s) | **+95 to +100** | +2 to +5 | the library's CPU copies of every file's vertex and index bytes: **50.3 MB, made resident already by the open**, in the wire index's build (on r1: 0.4 MB resident and 440 MB private when the library was loaded at 13.7 s, 50.3 MB and 494 MB when the index and the programs were done at 19.1 s); before it the game files and the art catalogue +26 MB (0.3 -> 1.4 s) and the library's load +14 MB (-> 13.7 s); the PS inputs' CPU data about +1 MB |
| renderer and PS inputs made | 0 | 0 | |
| prepare, every file (5 s: 24.9 / 24.1 s) | **+147 to +152** | **+134 to +137** | the files' IMMUTABLE GPU buffers 50.3 MB and 721 t0 / t1 textures 68.7 MB (119 MB asked); the private part [I: the driver's system-memory side of the 721 + 1,400 resources with initial data; not split further] |
| the frame buffers (4884's cut), at the window's end | about 0 | about 0 | **3-6 constant buffers of 1.0-2.1 KB in all and one row buffer of 576-704 KB** (DYNAMIC, 8 CreateBuffer calls in the run, about 75,000 Map(WRITE_DISCARD) in 12,600 passes) |
| per-file caches of 4884 (`fileGpu_`, `partSrv_`) | about 0 | | pointer arrays (624 files) |

Per arm, the mean of 2 runs, aligned on the load's big step (the first sample with video local above 500 MB; the arms'
steps fall 28-38 s after the mark; 4887's oldpc sample was step + 60 s), and minus off:

| Arm | private step+10 | step+30 | step+60 | end (240 s after the mark) | - off at step+60 | - off at the end | video local step+30 (- off) | bake-ahead end after the step |
|---|---|---|---|---|---|---|---|---|
| off | 1,641 | 1,949 | 1,949 | 1,949 | | | 1,352 | 25.1 / 23.1 s |
| on | 1,884 | 2,212 | 2,212 | 2,213 | **+263** | **+263** | 1,489 (+137) | 26.1 / 27.1 s |
| nodraw | 1,841 | 2,204 | 2,204 | 2,204 | +255 | +255 | 1,491 (+139) | 28.6 / 26.2 s |
| perpass | 1,941 | 2,239 | 2,240 | 2,240 | +291 | +291 | 1,515 (+163) | 24.0 / 23.0 s |

(Runs: on 2,209 / 2,216, off 1,945 / 1,953, nodraw 2,197 / 2,211, perpass 2,237 / 2,243 MB at the end. Video local falls in
every arm 90-200 s after the step, to 340-375 MB at the end, with nothing of ours changing [I: the OS lowering the
process's residency while the hidden game idles]; the video figures are read at step+30.)

What this gives:
- **The options' memory is the model layer's open and prepare, made before the first draw** (the open ends about 10-15 s
  before the load's big step, the prepare 5 s later): about +100 MB private at the open (half of it the library's CPU
  copies, resident for the session until ticket 0581 released them after the upload, 12.10) and +150 MB private / +136 MB video at the prepare (the files' GPU buffers and
  textures); +255 MB private in all with nodraw. With the bake-ahead finished before step + 30 s in every arm here (the
  cap leaves the render thread idle), on - off is the same right after the load as at the end: +263 MB.
- **The draw path adds about +8 MB private and nothing measurable in video** (on - nodraw), so 4884's kept DYNAMIC buffers
  are not the cause of 12.8's early level.
- **4878's lifetime costs more, not less**: perpass - on +28 MB private and +26 MB video local (new IMMUTABLE buffers in
  each of about 60 passes per second, released a pass later). 4887's end figure being 19 MB below 4878's (12.8) fits.

**Why the oldpc figure after the load was +465 MB [L for the course, I for the mechanism].** 4887's and 4878's job 2
render.logs (fetched from oldpc with `tb.py fetch`; `course_4887.py`, `course-4878.txt`, `course-4887.txt`) give the
server's memory every 10 s (`mem periodic`) and the residency's 1 s lines. Aligned on the load's big step (the first line
with video local above 500 MB), the cost driver's "after the load" sample fell 60 s after the step in every arm of both
jobs (a and b alike), so the sample's time is not the cause. What moves is the whole-map bake-ahead (D88, radius -1),
which fills the tile cache (410.1 MB at its end in every arm) and the server's private memory by about 420 MB from step +10
to step +100 s in a. Per kind, the mean of 6 arms:

| oldpc job | kind | private step+10 | step+60 (the sample) | step+100 | end | bake-ahead end after the step (ahead_ms) | server frames/s, step..+60 | render thread CPU ms/s |
|---|---|---|---|---|---|---|---|---|
| 4878 job 2 (before 4884) | a | 1,423 | 1,522 | 1,813 | 1,868 | 97.5 s (106.0 s) | 221 | 985 |
| | b | 1,668 | 1,728 | 2,063 | 2,161 | 103.3 s (110.2 s) | 181 | 974 |
| | b - a | **+245** | +206 | +250 | +293 | +5.8 s | -40 | |
| 4887 (after 4884) | a | 1,394 | 1,500 | 1,814 | 1,869 | 99.0 s (107.1 s) | 220 | 983 |
| | b | 1,669 | 1,919 | 2,081 | 2,143 | 74.1 s (80.7 s) | 182 | 977 |
| | b - a | **+275** | **+419** | +267 | +274 | **-24.9 s** | -38 | |

(Interpolated between the 10 s lines; the driver's own sample, psutil a few seconds later, read +465.) So b - a is the model
layer's own memory (+245 / +275 MB already at step +10, the layer having opened about 17.7 s before the step) plus,
around step +60, the difference in how far the bake-ahead has got. The render thread is busy about 98 % of each second
during the bake-ahead in both kinds (`rt_cpu_ms`), shared between frames and bakes; b draws about 18 % fewer frames
(the EXE presents fewer, 12.8), and before 4884 b's model pass (+2.66 ms per frame, 12.5) used that time and more, so b's
bake-ahead ended 6 s later than a's; after 4884's cut the pass is nearly free (+0.20 ms), so the freed frame time goes to
bakes and b's bake-ahead ends 25 s earlier: at the sample b is near its end value, a still about 300 MB below its own.
The end values (+293 / +274) and the peaks are the model layer's own cost. [I: the share of the render thread between
frames and bakes is read from the counts above, not timed per bake; the workstation runs below show the same end cost with
the bake-ahead finished in every arm before step +30 s.]

12.8's [I] (the kept DYNAMIC buffers and per-file caches of 4884, reaching their size at the first frames) is therefore
corrected: the frame buffers are a few KB of constants and one row buffer under 1 MB, the draw path adds about 8 MB, the
per-file caches of 4884 (`fileGpu_`, `partSrv_`) are pointer arrays, and 4878's per-pass buffers cost 28 MB more. The
options' memory is the model layer's open and prepare (+255 MB private, +136 MB video local), present before the first
drawn model; the higher figure right after the load on oldpc is the bake-ahead finishing 25 s sooner in b after 4884's cut.
No fix is warranted for the timing (the end and the peak are what a player pays); if the +100 MB of the open matters, the
library's CPU copies (50.3 MB, made resident by the wire index's build and kept after the GPU buffers are made) are the one
site that could be dropped. (Done 2026-11-20, ticket 0581, 12.10: released after each file's upload, server private -52.4
MB at the end; replaced "[I: not tried; a separate ticket if the MC wants it]".)

Not verified: the split of the prepare's +150 MB private between the driver's allocations per resource kind; the
workstation figures at an uncapped frame rate (the cap was on; oldpc's job ran uncapped); video local at the end (the OS's
trim); the mechanism that the render thread's time is shared between frames and bakes is inferred from the counts, not
timed per bake.

### 12.10 The library's CPU copies dropped after the GPU upload (ticket 0581, impl-0581, 2026-11-20)

**Who reads the copies [C, the code at main 386eeeb47].** `ModelLibrary::resident(file)` (render/src/landmarks/library.cpp)
makes a file's `FileBuffers` (rigid vertices, skinned vertices, indices: the EXE's buffer pair, repacked from the .gr2) on
first use and kept them for the session. Every caller in render/ and server/:
- `WireIndex::build` (render/src/landmarks/tap.cpp), at the layer's open on the worker: hashes each file's index and
  vertex bytes into its wire keys. This is what makes all 621 files resident (50.3 MiB) before the first frame (12.9).
- `ModelRenderer::fileBuffers` (render/src/landmarks/draw.cpp), from `prepare(file)` and, for a file not prepared, from the
  draw: creates the three IMMUTABLE buffers from the copy. After that the renderer draws only from its `FileGpu`.
- Nothing else in the product. The scene recorder's `modeltap/frame<N>.mtap` member writes the compositor's tap image
  (`srv_model_tap.h`), not library bytes; 4888's `model_geom` member reads `CpuMeshStore` (the CPU-built meshes of 13,
  `C5rLayer::geometryBytes` -> `m_cpu`), not the library; `resolve` and `drawableKeys` read the catalogue
  (`files()`, `layout.parts`) only. The tests and tools (`test_draw`, `test_inputs`, `test_oracle`, `library_report`)
  call `resident()` themselves, after a prepare, and get a copy made again.
So no reader needs a file's copy after its upload.

**The change.** `ModelLibrary::release(file)` drops one file's copy and lowers `residentBytes()` / `residentFiles()`;
`ModelRenderer::fileBuffers` calls it right after the upload (one call; MC #7940 granted the touch). The wire index still
makes every copy at the open (its hashes need the bytes), so the open's peak is unchanged; the copies go one by one as
the prepare uploads them (about 5 s after the open, 12.9). A renderer made again (setDevice after a lost device, D213)
reads and repacks a released file again: `resident()` was already lazy. Releasing in the layer instead, right after the
wire index, would have made the prepare read every file from disk again on the render thread.

**Tests [C, 2026-11-20, the fast configuration, workstation].** `render/tests/landmarks/test_library_memory.cpp`, 3
cases pass, and the 7 other landmarks tests pass (ctest `c5r_landmarks`, 8 of 8): release lowers the counts and leaves the
other file resident, no-op on a released, out-of-range or never-resident file, the copy made again is byte-equal; the
wire index rebuilt from copies made again has the same keys (6,664 parts, 0 differ; 621 files, 50.3 MiB); `prepare`
releases the copy, the part draws from the GPU buffers alone, and a second renderer (the re-init path) re-reads and
uploads the same bytes (FNV-1a equal) and draws byte-equal colour and id pixels. What a rebuild costs on the CPU [an
indication, not a timing claim]: every released file read and repacked again 5.9 s (621 files, 50.3 MiB), one file
(`euro_anc_city.gr2`, re-read, repack, upload) 11.6 ms; the prepare runs in budgeted slices (`C5rLayer::prepare`), so
on a device rebuild the models come back over a longer prepare, not in one frame.

**Rule A [C].** The scene library (`F:/c5-64bit-work/scene-library`, 16 bundles, index `393194ee...`) replays at
tolerance 0:0 against its own live dumps with this branch's sceneshot (`d6c1a58c...`): 16 of 16 bundles, 128 of 128
frames byte-equal; 7 bundles open the model layer (render_cities = render_landmarks = 1). The merge base's sceneshot
(`e393ab8f...`): 16 of 16 byte-equal, one bundle (SCNB_0009 131111) on a rerun: its first replay at about 08:29 failed the
units pre-check offline (`ModelNotFound: ... .fxsxml not found` for every VP unit, the bundle's install and MODS folder
being c5h-a's) while this ticket's live A/B was loading a game on c5h-a; alone at 08:33 it passed 8 of 8 [I: the replay
reads the claimed instance's MODS folder and the launch was changing it; not root-caused, the player and the bundle are
unchanged by 0581]. No recorded picture changed (`render/evidence/0561/0581/replay_0581.txt`).

**The A/B [L, workstation c5h-a, 2026-11-20 08:10-09:01 EST; DIAGNOSTIC (D112: options 12/13 are not the release
default); memory, not timing].** Builds: release `build_server.py --config release` of the merge base 386eeeb47 (before,
server.exe `ae1ce68f...`) and of this branch 8be199182 (after, `9b25faa0...`), the same fork source at the batch-5 pin
1fd51353e and the same stub libraries; one layout for both (x64 DLL `78355a75...` of 0551's batch-5 layout, the merge
base's proxy `a1110711...`, byte-equal to the after build's, composite `5f36cf3e...`); only VP64\server.exe swapped per run
(SHA-256 checked). Options ServerRenderer 1, ServerUnits 0, ServerCities 1, ServerLandmarks 1; the save copy
C5H-0581-HUGE_0350 of C5H-VP8P-HUGE_0350 (`c499d3c6...`, turn 350, 0579's), camera at plot (85, 31), no AI turn; per
run 240 s idle after the camera with the server's private bytes from psutil every 5 s and render.log's `mem periodic`
(10 s). Interleaved before, after, after, before, before, after; 6 of 6 rc 0, no dump, crash or box; restore by hash ok,
provision verify rc 0. Driver `render/evidence/0561/0581/run_0581_live.py` (0579's without its diagnostic sampler),
`chain_0581.sh`, `analyze_0581.py`; table `summary.txt`.

| server private MB (MiB), the mean of the window's last 30 s | runs | mean | sd | 95 % interval |
|---|---|---|---|---|
| before | 2,196.6 / 2,192.9 / 2,202.2 | 2,197.2 | 4.7 | 2,185.6 .. 2,208.9 |
| after | 2,146.2 / 2,147.6 / 2,140.7 | 2,144.8 | 3.6 | 2,135.8 .. 2,153.9 |
| after - before | | **-52.4** | | Welch -63.3 .. -41.5 (df 3.8); detectable about 9.6 |

render.log's last line gives the same (-54.3, -65.4 .. -43.2); the highest `mem periodic` of each run is its end value in
both arms (before 2,196-2,205, after 2,141-2,148), so the run's peak after the load moves by the same amount. Video local
at the end -4.5 MB (-16.0 .. +6.9: no difference shown; nothing on the GPU side changed). Both arms drew the models
(keys 5,565-5,580 in both, the CPU-built meshes coming and going; draw_errors 0).

Why it moved: the after build frees the 621 files' copies (50.3 MiB of vertex and index bytes, 12.9's counter) as the
prepare uploads them, and the drop, 52.4 MiB, is that plus about 2 MiB of the 1,863 vectors' heap overhead: the heap
gives it back [I: the copies average 81 KiB, so most are heap blocks, not VirtualAlloc'd; the drop shows the segments are
decommitted, not kept]. Nothing of the ~50 MB is left: no allocator retention shows, and the tap's own frame copy and the
compositor's image are separate allocations that neither arm changes. What is NOT changed: the open still makes every
copy (the wire index needs the bytes), so the transient at the open is as in 12.9, now released within about 5 s; no
timing claim (the prepare does the same work; a device rebuild re-reads the files, 5.9 s of CPU for all of them in the
module test, in budgeted slices).

Not verified: the transient at the open itself (the 10 s log lines do not resolve it); a device-lost rebuild live (the
module test covers the path); n 3 per arm (grade B for memory; the ticket asked for 3); other machines.

## 13. CPU-built meshes: walls, the Great Wall, the road and rail bridge (ticket 4879)

Written 2026-11-19 by worker-4879 (account B, Opus 5.5) on `task-4879-cpu-built-meshes`. Marks: `[C]` checked in code or
by this ticket's own tests, `[L stand-in]` the shell stand-in host, `[L]` the game, `[I]` inferred.

### 13.1 The class

A family colour draw on the block programs (VS `7ec4e9a5` / PS `c704c80b`, class BLOCK) or the material-3 programs (VS
`c4bb25da` / PS `f0e1f0a6`, class SINGLE) whose index buffer is bound as **R32** is a mesh the EXE builds itself: every
library file packs R16 indices behind one pad element (5.5), while the walls (P4: one world-space 32-B mesh per city,
identity instance row), the Great Wall (W01 70 / 469 and 134 / 617) and the road and rail bridge (P6, landmark-renderer.md
8: Bridges.gr2's three IND pieces bent per crossing, 270 R32 indices, a 3,200-B 20-B-vertex buffer) are R32 without a
pad. No file key can name them; their two buffers are IMMUTABLE and created with initial data (P4, 10.2), so the
compositor already hashes them at creation (the key's `ib` from byte 4, `vb` whole). Bit: **cities** for all of them
(walls and the Great Wall are city art; road and rail bridges by lead2-0561 #6368); copied only while the server wants
cities.

### 13.2 The geometry section (`shell_model_tap.h`, `VPModelGeom*`)

`Local\VPShellModelGeom.<pid>`, made by the compositor at the first CPU-built draw it sees while cities are wanted:
a 64-B header (seqlock, magic `C5MG`, version 1, generation), 512 entries of 32 B (hash, kind INDEX32 / VERTEX, stride,
offset, bytes, generation), data from offset 65,536 (32 MB, pagefile-backed). Writer: the compositor's render thread at
a Present (seq odd, data, entry, generation + 1, seq even), the data through a short-lived view (no address space kept in
the 32-bit process). Reader: the server (`srv_model_geom.cpp`), whole header and entries under the seqlock, each new
entry's bytes copied and the seq checked again (torn: read again later), then the hash checked by the store.

### 13.3 The compositor (`unit_filter.cpp`)

- **Recognise** (in `ModelDraw`, after the key): a complete key with R32 indices on class BLOCK or SINGLE while the
  frame's tap or drop bits have cities: `cpu_seen`. Both buffers are noted (`GeomNote`, by kind and content hash, the
  buffer AddRef'd until copied).
- **Copy** (`GeomPresent`, at each Present after 4874's read-back): at most 8 copies and 16 MB started per Present (a
  staging buffer and `CopyResource` on the immediate context); a later Present maps it `DO_NOT_WAIT`, checks the bytes
  against the creation hash (`changed` if they differ: the draw passes for good), finds room (first fit; when full the
  least recently named entry not named for 120 Presents is freed; else `full`, tried again after 600 Presents) and
  writes the entry. (Since 0597, 13.10: when the free bytes suffice but no hole does, the data is compacted first; every
  `full` is logged once per buffer.) Refused for good: `oversize` (over 8 MB), `notkeyable` (not IMMUTABLE), `failed` (a staging buffer
  or Map failure).
- **Drop rule** (added to 10.3): such a draw is dropped only while its key is in the server's set AND both its buffers
  are entries of the section; else it passes (`cpu_nogeom` counts the ones in the set and latched). Its tap record is the
  class's (BLOCK: slot-1 rows; SINGLE: VS cb0 rows 0-3), so the server draws it from the record and the copied buffers.
- **Release**: an entry not named by any draw for 18,000 Presents (about 5 minutes at 60 fps) is freed; the server then
  releases its copy and its keys leave the set at the next generation. A freed buffer that is drawn again is copied
  again (same content, same hash). The EXE's own release of a buffer is not seen (no Release hook): the age rule stands
  for it.
- Counters: `cpu_seen`, `cpu_nogeom` in the `modelfilter` lines; one `modelgeom totals:` line (section, entries,
  generation, wanted, held, pending, evicted, oversize, changed, full, failed, notkeyable, map_fail, table_full,
  copied_kb, copy_ms). TEST ONLY key `model_geom_test` = `full` | `changed` | `oversize:<bytes>` (LOUD when set).

### 13.4 The server (`srv_model_geom.{h,cpp}`, `srv_model_layer_c5r.cpp`; c5r `cpumesh.{h,cpp}`)

- `ModelGeomReader` (made with the tap reader when cities are on) polls the section each loop once the layer and its
  device are up; new entries go to the layer's `CpuMeshStore` (`addGeometry`: oversize Unsupported, layout and
  "content differs from its hash" BadLayout, each counted and not offered again for that entry generation); held
  buffers that are no longer entries are released.
- `resolveCpu` (after `resolve` on each accepted tap): every R32 draw of material 5 or 3 that no library file resolved,
  whose two buffers the store holds, whose vertex stride fits the material (5: 32 B, 3: 20 B) and whose t0 / t1
  identities name a file of the install (`WireIndex::textureName`; the bridge's `bridges_diff.dds` / `bridges_sref.dds`
  entered by `addCpuTextureNames`, the walls' and the Great Wall's maps are their library files') becomes a
  `CpuDrawItem`. A key whose textures the renderer can make joins the layer's CPU keys (`KeySource::CpuCopied`,
  family CityBlock or CityPiece): the identity set is built again and published (bit cities, class BLOCK or SINGLE).
- `ModelRenderer` draws `cpuItems` after the files' items with the same programs, states and samplers, the copied
  buffers (made on the device at first draw, released when the store drops them) and the tapped rows (BLOCK: slot 1;
  SINGLE: VS cb0).
- The models line gains ` geom open= gen= entries= added= released= torn= refused=<oversize>/<layout>/<hash>/<bounds>
  kb= cpu held= kb= keys= r32= resolved= nogeom= stride= unnamed= notex=`.

### 13.5 Tests [C, L stand-in]

- Shell stand-in (`shell/tests/test_cpu_meshes.py`, host `unit_filter_host.exe --models --cpu`: C0 an R32 cell on the
  block programs with a 32-B vertex buffer and one slot-1 instance, C1 an R32 cell on the material-3 programs with a
  20-B one and VS cb0; the host reads the geometry section as the server does, compares each entry's bytes with what it
  uploaded, and publishes the two keys once the three buffers are held), 2026-11-19 00:0x EST, plain build of this
  branch: **6 passed** - dropped from the frame after the keys are published, every DROPPED record checked; never in the
  set (copied, passed); section full (`model_geom_test=full`: in the set, `cpu_nogeom`, passed); oversize
  (`oversize:32`: both vertex buffers refused, the index buffer held, passed); a copy that differs from its creation
  hash (`changed`: refused, never entered); cities not wanted (no copy, no section). Regression with
  `test_model_filter.py`, `test_unit_filter.py`, `test_config_defaults.py`, `test_model_programs_table.py`: 110 passed
  (00:24, 430 s).
- c5r (`render/tests/landmarks/test_cpumesh.cpp`): the hash rule, add / get / remove / clear, every refusal (oversize,
  layout, a write after the copy, no hash), resolveCpu (not held, held, stride against material, R16 and other
  materials ignored, an unnamed texture, a range past the held buffer, checkFrame): `c5r_landmarks_*` 6 of 6 passed.
- Server (`server/tests/render/cities/models_test.cpp`): `geom_reader` (no section yet; entries added with their bytes;
  same generation not read again; a hash mismatch refused once and not offered again; an entry past the data; a write in
  progress read only after it ends; a freed entry released; a header that does not hold) and `set_build`'s CPU-copied
  keys (BLOCK / SINGLE under cities; landmark or skinned families left out): 23 passed Release.

### 13.6 Offline compare at the C1 tolerance [C, 2026-11-19 00:56-01:20 EST]

`render/evidence/0561/4879/offline_4879.sh` (cityscene, 4873's harness: the tap made from the capture's own buffers, the
CPU-built draws' R32 index and vertex buffers taken from the capture as the compositor copies them at run time; the
install `pure_vp` (VP 5.4.7 DLC, Vibrant Textures), the database copy of 4872, the EXE copy; reference = the captured draws
replayed by terrainreplay). 4871's kept exports have no texture dumps (its README), so P4_w1 and P6_road were exported
again from its captures with `rdc-export --select all` into `C:/c5-64bit-work/0561/4879/exports/` (725 MB, not committed);
the first P4_w1 run on the kept export could not draw a reference (rc 1: no textures).

| Frame | Draws | Resolved by a file | CPU-built (resolved) | Scene target: pixels, median, p95, max, within 16 | The CPU-built draws (scene) |
|---|---|---|---|---|---|
| W01 (3910, the Great Wall city) | 149 | 147 | 2 (2): #469 block (`poly_wall_*`), #617 material 3 (`great_wall_*`) | 343,662, 0, 0, 81, 0.9929 | #617: 123,496 px, within 16 0.9803; #469: no visible pixel |
| P4_w1 (4871, walled city, damage 1) | 685 | 683 | 2 (2): #949 wall (block, `poly_wall_*`), #1610 a bridge (material 3, `bridges_*`) | 389,474, 0, 0, 156, 0.9999 | #949: 11,119 px, within 16 1.0; #1610: 0.9715 |
| P6_road (4871, road across a river, Postmodern) | 324 | 323 | 1 (1): #838 the bridge (material 3, `bridges_*`) | 203,270, 0, 0, 129, 0.9999 | #838: 1,209 px, within 16 0.9884 |

Reflection targets: within 16 1.0000 in all three. Every frame and every CPU-built draw is within the C1 tolerance
(section 6: median <= 2, p95 <= 8, >= 97 % per frame, >= 75 % per object); unknown draws went from 2 / 2 / 1 to 0.
Texture names found by identity: the walls' `poly_wall_diff.dds` / `poly_wall_sref.dds` (library wall parts), the Great
Wall's `great_wall_diff.dds` / `great_wall_sref.dds`, the bridge's `bridges_diff.dds` / `bridges_sref.dds` (entered by
`addCpuTextureNames`): so 4877's open question is closed by content [C]: the bridge's t0 / t1 are those two files. The
material-3 CPU-built draws (Great Wall 0.98, bridges 0.97-0.99) are below the block wall (1.0) [I: material 3 blends
(captureStates), and the CPU-built items are drawn after the files' items instead of in the tap's order; not measured].
The pontoon rows (4877's open step) were not part of these frames.

### 13.7 Live smoke (c5h-c, 2026-11-19 01:07-01:12 EST) [L]

`render/evidence/0561/4879/chain_4879.sh` with `DRIVER=run_4879_live.py` (the product path of 11.11: server `2F7E4FA2...`,
composite `A2D94ABC...`, proxy `2C90BB6F...` of this branch on the pinned x64 DLL `2B56A1CD...`; ServerRenderer, ServerCities,
ServerLandmarks = 1, ServerUnits = 0; the Tiny copy; restored by SHA-256, provision verify rc 0, the front-end files
original). A city founded by Lua at (7,20), population 6, `BUILDING_WALLS` and then `BUILDING_GREAT_WALL` set by
`SetNumRealBuilding`. `GetNumRealBuilding(BUILDING_WALLS)` read back 0. **Why [C, the DLL source at the pin c29442e8,
read 2026-11-19 01:25]:** the Great Wall's row has `FreeBuildingThisCity = BUILDINGCLASS_WALLS` (the game database of the
run); setting it calls `CvCity::SetNumFreeBuilding(walls, 1)` (CvCity.cpp, the building's processing: "Free building"),
which **replaces** a real copy (`SetNumRealBuilding(eBuilding, real - replaced)`, with a refund) and sets the free count
1; `CvCityBuildings::SetNumFreeBuilding` then calls `GameplayWallCreated` for a city-wall building, so the EXE builds the
wall mesh as for real walls. So the walls were there (free, not real), the Lua call was not refused and the type name is
right; the read-back asked the real count only. The live block-program R32 key is therefore that wall mesh [I: by its
class and R32 form, the same form as W01's #469 at the Great Wall city, which resolves to `poly_wall_*` offline; the
texture names of the live key were not logged].

- Composite: `cpu_seen` 6,048, `cpu_nogeom` 0; geometry section made at the first CPU-built draw, 4 entries held (two R32
  index buffers, two vertex buffers: the Great Wall city's block-program wall mesh and the Great Wall's material-3 mesh),
  no refusal of any kind; 636 KB copied, **2.36 ms in all for the copy of the whole walled city** (an indication: one
  workstation run; P7 on oldpc is 4878's); `dropped_cities` 100,733, no LOUD line.
- Server: `geom added=4 torn=0 refused=0/0/0/0 kb=636`, `cpu keys=2 r32=2 resolved=2`; identity sets with `CPU-built
  copied` 1 and then 2 published and applied by the compositor (generation 7, 5,541 keys).
- The 2 CPU keys published and dropped (line added by lead4-0561, 2026-11-19, MC #6699; from the counts above and the
  entries held): key 1, the city's block-program R32 wall mesh (the walls the Great Wall gives free, see above; texture
  names not logged, so [I]); key 2, the Great Wall's own material-3 R32 mesh (the ring around the city). Each key covers
  one index buffer and one vertex buffer of the 4 entries held; both were drawn by the server in place (black, 4882) and
  absent from the EXE's picture while the set held them.
- Picture (DIAGNOSTIC, `vc-20261119-011055-workstation-414d`, not committed): the Great Wall ring and the city drawn once,
  by the server, in place; black like every server-drawn model of this build (11.11 point 8, child 4882), not a CPU-built
  matter.
- Found: with entries held the compositor scanned its 4,096-record table at every Present (`copy_ms` 2.36 -> 119 over
  about 3,000 Presents, about 0.04 ms each); corrected the same night: with nothing pending the scan runs once every 600
  Presents (the idle rule's granularity).
- The Tiny load before the city: no CPU-built draw, so the copy costs nothing there (no section made).

### 13.8 Not verified

(Updated 2026-11-20 by ticket 0584, 13.9: a city with real walls only, the Great Wall's free walls (read back live), a
road and a rail bridge live, and the idle release rule live are now [L]; the LRU when full stays open, see 13.9's "Open
after 0584". The list below is 4879's, kept as written.)

- That the live block-program key was the walls' mesh (inferred in 13.7: walls given free by the Great Wall; its
  texture names were not logged; P4_w1 offline, 13.6, is the walls' evidence); a city with real walls only; a road or rail
  bridge live; walls of other culture sets; a pillaged route's bridge.
- The release rule over a long game (18,000 Presents unnamed), the LRU when the 32 MB section fills (many walled cities on
  a huge map), the 4,096-record table's 3/4 bound; the cost on oldpc (P7, 4878).
- The draw order: CPU-built items are drawn after the files' items; material 3 blends, which is the likely cause of the
  0.97-0.99 within-16 of the Great Wall and the bridges offline [I].
- Pontoon bridges' rows in the library (4877's open step) and the ANC bridge pieces at earlier eras: not looked at.

### 13.9 Live checks of 13.8's open points (ticket 0584, impl-0584, 2026-11-19)

Branch `task-0584-cities-walls-bridges-lru-live` (main 825f072f1 / d74e7212e: 4879, 4882, 4884, 4885, 4888 merged). Build
`render/evidence/0561/0584/build_0584.sh` (pin batch 5: the fork's 1fd51353e by `git archive`, build_all release, the composite,
server.exe + proxy release; the layout = 0551's `x64-1fd51353-p1e09fc7` bin with this build's server.exe, proxy and composite).
Driver `render/evidence/0561/0584/run_0584_live.py` through `chain_0584.sh` (4879's prep / arm / restore: layout and options
backed up by SHA-256 and restored by hash; ServerRenderer, ServerCities, ServerLandmarks = 1, ServerUnits = 0; the Tiny copy
`C5H-0584-TINY_0018` of `C5H-3000-TINY-T0018`). Instance **c5h-d on F:** (MC #7374: functional only; no timing or load-time
claim from it), EXE stamp 0x546CD0A8 (the pre-update build). Sequence: city A founded by Lua with `BUILDING_WALLS` only; city
B with `BUILDING_GREAT_WALL` only (free walls, 13.7); a road bridge (both plots of the river crossing nearest the capital set
to `ROUTE_ROAD`, `IsRiverCrossingToPlot`); a rail bridge (another crossing >= 3 plots away, `ROUTE_RAILROAD`); the four
places again; the camera on open water until the server's `released` grows and its entries reach 0 (the 18,000-Present
idle rule); back to each place (copied again); AI turns with the camera on city B. After each step a snapshot of the
composite's `modelgeom totals` / `modelfilter totals` / `presents=` and the server's `models:` line's geom and cpu parts.

**The section size is fixed [C, code read 2026-11-19]:** `shell_model_tap.h` declares `VPSHELL_MODELGEOM_KB_DEFAULT 32768u /*
model_geom_kb */` with a min (1,024) and a max (131,072), but `unit_filter.cpp` never reads a `model_geom_kb` key:
`g_geomDataBytes` stays 32 MB. So the LRU-when-full path (`GeomEvictOne`) cannot be reached live on a small map by
configuration; it has only the stand-in test with `model_geom_test=full` (13.5), which skips eviction (`!g_geomTestFull`).
The section's two bounds are 32 MB of data and 512 entries (two per mesh: 256 meshes named within the idle window).

**Run d1 (2026-11-19 23:42-00:04 EST; server 6AB5752F, proxy CB7BC27B, composite 8DB915ED, x64 DLL 78355A75; record
`render/evidence/0561/0584/check_d1.json`) [L]:**

| Step (turn 18) | Lua read-back | server CPU keys | entries | KB | composite |
|---|---|---|---|---|---|
| baseline at the capital (8,23) | - | 0 | 0 | 0 | no section yet (no CPU-built draw) |
| city A at (7,20), `BUILDING_WALLS` real | walls real 1, total 1, free 0 | 1 (+1) | 2 (+2) | 105 (+105) | held 2, cpu_nogeom 0 |
| city B at (10,21), `BUILDING_GREAT_WALL` real | walls real 0, total 1, **free 1** (`GetNumFreeBuilding`), Great Wall real 1 | 3 (+2) | 6 (+4) | 1,095 (+990) | held 6, cpu_nogeom 0 |
| road on both plots of the river crossing (9,23)-(10,23) | routes -1/-1 -> 0/0 | 3 (+0) | 6 | 1,095 | no new CPU-built draw |
| railroad on (11,20)-(11,21) | routes -1/-1 -> 1/1 | 3 (+0) | 6 | 1,095 | no new CPU-built draw |

- **A city with real walls only** is one CPU-built block-program key (two buffers, 105 KB), copied, published (identity set
  `CPU-built copied` 1), dropped from the EXE's frame and drawn by the server; no refusal, no `nogeom`, no `notex`, no
  LOUD line. This closes 13.8's "a city with real walls only" [L].
- **The Great Wall's walls are free walls [L]:** the read-back of city B gives walls real 0, total 1, free 1: 13.7's
  reading of the DLL source is confirmed live. City B adds two keys: its own wall mesh (block programs) and the Great Wall
  ring (material 3); the ring is the larger, about 885 KB of the 990 [I: by subtracting a wall mesh of city A's size; the
  per-key sizes are not logged]. The ring follows the whole empire's border (picture `base_road_bridge`, DIAGNOSTIC).
- **Bridges need the owner's BridgeBuilding tech [L, negative]:** roads and railroads laid by `SetRouteType` on both plots of a
  river crossing at turn 18 (no Engineering) made no CPU-built mesh and no bridge in the picture (the road stops at the
  river). Run d2 gives the team every `Technologies.BridgeBuilding` tech first.
- **The EXE draws every CPU-built mesh off screen [L counters; why: I]:** with the camera on the open-water plot (36,34), 18
  plots from every city, the server's tap still held `r32=3 resolved=3` and the composite counted `cpu_seen` 90-93 per
  second (3 per Present at about 31 Presents/s) for 11 minutes; so all three meshes were named every Present and **the
  idle rule never freed anything: 21,405 Presents at the water (23:50:18-00:02:22, presents 6,363 -> 27,768), `evicted`
  0, `released` 0, entries 6** [L]. The EXE does not frustum-cull the walls and the Great Wall [I: or culls them with
  bounds that cover the map, or draws them in a pass whose camera is not the view's; which pass names them was not
  looked at]. (Corrected 2026-11-20 by ticket 0597, 13.10 r3: on a huge map the EXE culls these meshes by the view
  [L]; what follows holds for a Tiny map only.) Consequence
  [I]: the section holds every CPU-built mesh of the map that the EXE has built for the session, not the ones in view;
  the idle rule frees only meshes the EXE no longer draws (a wall removed, a razed city, a route removed), and on a huge
  late map with more than 256 walled cities and bridges, or more than 32 MB, the LRU finds nothing older than 120
  Presents to evict, the newest meshes are refused `full` (retried every 600 Presents) and the EXE draws them itself
  (the drop rule: no missing mesh, but not drawn by the server). Measured sizes: a wall about 105 KB (two buffers), the
  Great Wall ring about 885 KB [I].

**Run d2 (2026-11-20 00:06-00:28 EST; the same build and instance; `C5_0584_RELEASE=unwall`; record
`render/evidence/0561/0584/check_d2.json`) [L]:** the same two cities (same plots, same read-backs: city B walls real 0,
total 1, free 1), then the bridge steps with the team given `TECH_ENGINEERING` (the only `Technologies.BridgeBuilding`
tech of the run's database; `IsBridgeBuilding` true after it):

| Step | server CPU keys | entries | KB |
|---|---|---|---|
| after city A (walls) | 1 (+1) | 2 | 105 |
| after city B (Great Wall) | 3 (+2) | 6 | 1,099 |
| road on (9,23)-(10,23) | 4 (+1) | 8 (+2) | 1,108 (+9) |
| railroad on (11,20)-(11,21) | 6 (+2) | 11 (+3) | 1,119 (+11) |

- **A road bridge and a rail bridge live [L]:** each crossing became CPU-built material-3 keys that resolved (the
  bridge's textures named, `notex` 0, `unnamed` 0, `stride` 0), were published and dropped; `nogeom` 0 at every snapshot.
  The road bridge is one key, two buffers, 9 KB (4879's P6 bridge: 270 R32 indices and a 3,200-B vertex buffer, about 4 KB
  [C, 13.1]; this one is larger [I: a longer crossing or more pieces]). The railroad added two keys over three new buffers
  [I: the rail joins city B on (10,21), next to the same river, so a second crossing got a bridge too, and the two share one
  buffer; the per-key buffers are not logged]. Pictures (DIAGNOSTIC) `base_road_bridge` and `base_rail_bridge` of
  `pics/tiny-base-r2`: the bridge over the river east of Madrid, the rails across the river at Seville.
- **The idle rule frees a mesh the EXE stops drawing [L]:** city A's walls removed by Lua at 00:13:37 (`SetNumRealBuilding`
  0, read back real 0, total 0; Presents 6,286): the tap's `r32` went 6 -> 5 at once (the EXE no longer draws the wall);
  at 00:23:58 (Presents 24,905: **18,619 Presents later**, the rule's 18,000 plus its 600-Present scan granularity) the
  composite had `evicted` 2 and the server `released` 2, entries 11 -> 9, CPU keys 6 -> 5, held 1,119 -> 1,014 KB; the
  other five meshes stayed (named every Present, though the camera was on city A only). Present rate 30.0 per second
  (the instance's frame cap): the 18,000-Present rule is about 10 minutes here, 5 at 60 fps.
- **Copied again [L]:** the walls given back at 00:23:58 (real 1): within 21 s the composite copied two new entries
  (`added` 11 -> 13), CPU keys back to 6, held back to 1,119 KB (the same size as the first copy), the key published (the
  identity set's `CPU-built copied` 5 -> 6) and dropped; `cpu_nogeom` 0 and the server's `nogeom` 0 at every snapshot, so no
  frame lost the wall to a missing copy.
- **Three AI turns (18 -> 21, 7.5-9.1 s each) [L]:** after turn 20 the composite copied two more buffers (entries 11 -> 13,
  held 1,247 KB, CPU keys 7) while the tap's `r32` stayed 6: the EXE built a new CPU-built mesh and stopped drawing an old one
  [I: a city's wall or the Great Wall ring rebuilt when a border grew; which one is not logged]; the old one stays held,
  unnamed, until the idle rule frees it about 10 minutes later. So over a game the section holds the meshes in use plus the
  ones the EXE replaced in the last idle window; nothing grew past that in this run (max entries 13, max 1,353 KB copied).
- Checks of the run (`check_d2.json`): ok; no refusal of any kind (oversize, changed, full, failed, notkeyable, map_fail,
  table_full all 0), no LOUD line, no crash, no dialog; the instance restored by SHA-256 (`backup_restore_ok` true,
  `provision_verify_rc` 0) at 00:28.

**Open after 0584:**
- The LRU when the section is full was not reached live: the section size is fixed (no `model_geom_kb` read), a Tiny map
  holds 13 entries / 1.3 MB, and every live mesh is named every Present (d1), so on a map past 256 meshes or 32 MB the LRU
  has nothing to evict and refuses the newest (`full`): those draws pass to the EXE (drawn, not by the server). How many
  walled cities, Great Walls and bridges a huge late game holds (against 256 meshes / 32 MB) is not counted [I: a walled
  city about 105 KB and 2 entries, so the entry bound (256 meshes) comes before the data bound]. Reading `model_geom_kb` (a
  1,024 KB section) would make the full path testable on the Tiny save.
- Which pass names the CPU-built meshes off screen (d1); the per-key texture names (not logged); the two keys of the rail
  step; walls of other culture sets; a pillaged route's bridge; pontoon bridges.

(Updated 2026-11-20 by ticket 0597, 13.10: `model_geom_kb` is read now, the full section was reached live on the Tiny
with a 1,024 KB section, and the huge t350 game was counted against the bounds.)

### 13.10 The cache when it is full (ticket 0597, impl-0597, account B, 2026-11-20)

Branch `task-0597-cpu-mesh-cache-full` (main 17f6e8afd: 0590 stage 1, 0585 merged). Marks: `[C]` this ticket's code and
stand-in tests, `[L]` its live runs on c5h-d.

**The compositor (`unit_filter.cpp`, the Geom* functions) [C]:**
- `model_geom_kb` (shell_composite.ini `[composite]`, or `VPSHELL_MODEL_GEOM_KB`) is read at the filter's init: KB of
  data, 1,024..131,072 (out of range clamped, not a number = the default 32,768); the section's line names the size in KB
  and bytes and how it was set (`model_geom_kb=N`, `... clamped`, `... is not a number: the default`, `default`).
  *Replaces 13.9's "the composite never reads `model_geom_kb`".*
- **A logged refusal, never a silent loss (D118/D125):** the first `full` refusal of each buffer is one line
  `model_filter: REFUSED full: a CPU-built <index|vertex> buffer of N B (hash ...) does not fit the geometry section: E of
  512 entries, U of S B in use, no entry unnamed for 120 Presents to evict ...; the EXE draws this mesh (not lost); tried
  again every 600 Presents; model_geom_kb sets the section (...)`. When such a buffer fits later (an entry freed by the
  LRU or the idle rule) one line `... refused full N time(s) is held now (...)`. A full record table (3,072 of 4,096
  records) is one `REFUSED table_full` line per episode. The draw of a refused buffer passes (13.3's drop rule): the EXE
  draws the mesh, so nothing is missing from the picture; the server does not draw it.
- `modelgeom totals:` gains `section_b` (data bytes), `used_b` (bytes in use, 16-aligned), `max_entries`, `max_used_b`
  (high-water marks), `evicted_room` (entries the LRU freed to make room; `evicted` counts these and the idle rule's),
  `full_logged` (buffers with a logged refusal) and `records` (records of the 4,096-record table in use).
- TEST ONLY `model_geom_test=bytes:<n>`: the section holds n bytes of data (below the key's minimum; LOUD line).

**Stand-in tests (`shell/tests/test_cpu_meshes.py`, host `unit_filter_host.exe --cpu`, new `--cpu-stop F`: from frame F
the host no longer draws the cell whose vertex buffer is held) [C]:**
- `test_cpu_section_full_logged_nothing_to_evict`: `bytes:128` holds the index buffer (24 B, 32 aligned) and one vertex
  buffer (96 or 60 B); the other is refused, logged once, nothing evicted (both cells drawn every frame: the live case
  of 13.9 d1), the cell with both buffers held is dropped, the other passes (`cpu_nogeom`).
- `test_cpu_section_full_lru_evicts_unnamed`: the same, the held cell stopped at frame 8; at the refused buffer's retry
  (600 Presents after its refusal) the LRU frees the stopped cell's buffer (unnamed > 120 Presents): `evicted_room` 1, one
  `REFUSED full` line, one `is held now` line, the other cell dropped from then on (660 frames).
- `test_cpu_model_geom_kb[1024|5|999999|abc]`: the section's size and its note for each form.
- `test_cpu_section_fragmented_compacts` (r1's case below): a 320-B section, cell 0's 112-B vertex buffer at 0 (`--cpu-frag`
  picks its pad bytes so the compositor copies it first), the index buffer at 112, cell 1 from frame 10 (`--cpu-late`)
  with a 200-B vertex buffer (`--cpu-pad 140`) refused; cell 0 stopped at frame 20; at the retry the LRU frees cell 0's
  buffer (288 B free in a 112-B hole and a 176-B tail), the data is compacted (24 B moved) and the buffer is held: cell 1
  dropped, the host's byte check of every entry holds after the move.
- Plain build, 2026-11-20 03:25 EST: **13 passed** (40.8 s). Final source (04:16-04:28 EST): with
  `test_model_filter.py` and `test_unit_filter.py` **95 passed** (331.8 s); `shell\build.cmd asan` (MSVC
  `/fsanitize=address`): `test_cpu_meshes.py` **13 passed** (48.1 s), no AddressSanitizer report (the conftest fails a
  test on one).

**Live run r1: the Tiny with a 1,024 KB section (c5h-d, 2026-11-20 03:02-03:11 EST; record
`render/evidence/0561/0597/check_tiny_r1_before_compaction.json`) [L]:** build of this branch before the compaction
(server 9788B32F, proxy E37F578A, composite FDBAE67A on the pin batch 5 x64 DLL 78355A75), 0584's sequence with
`model_geom_kb=1024` in the composite's ini (`run_0597_live.py geomkb`), `chain_0597.sh`.

| Step | entries | used B | full | evicted (room) | server CPU keys / r32 / nogeom |
|---|---|---|---|---|---|
| city A, walls | 2 | 108,160 | 0 | 0 | 1 / 1 / 0 |
| city B, the Great Wall | 5 | 904,192 | 2 | 0 | 2 / 3 / 1 |
| 40 s later | 5 | 904,192 | 4 | 0 | 2 / 3 / 1 |
| city A's walls removed | 3 | 796,032 | 6 | 2 (2) | 1 / 2 / 1 |
| city A's walls given back | 5 | 904,192 | 8 | 2 (2) | 2 / 3 / 1 |
| one AI turn (18 -> 19, 8.4 s), end | 5 | 904,192 | 11 | 2 (2) | 2 / 3 / 1 |

- **The full path live [L]:** the section line named `1024 KB of data, 1048576 B; model_geom_kb=1024`; at city B one
  `REFUSED full: a CPU-built index buffer of 226380 B ... 5 of 512 entries, 904192 of 1048576 B in use` (the Great Wall
  ring's index buffer: the only buffer of that size [I: by size; its vertex buffer and city B's wall were held]); the ring's
  draw passed (the server's `nogeom` 1: a tapped CPU-built draw it holds no buffers for; the key never published), so the
  EXE drew the ring: not missing (picture `base_full`, DIAGNOSTIC). Retried every 600 Presents (`full` 2 -> 11), logged
  once. No LOUD line, no crash, no box; restored by SHA-256 (`backup_restore_ok` true, `provision verify` 0).
- **Fragmentation, not size, kept the ring out [L]:** with city A's walls removed (its mesh no longer drawn) the LRU freed
  both of its buffers at the next retry (`evicted_room` 2), leaving **252,544 B free in two holes, 108,160 B at the start
  (city A's) and 144,384 B at the end**, neither of which holds 226,384 B; the buffer stayed refused, and city A's walls
  given back took the hole at the start again. First fit with no move cannot use free bytes split between holes. Fixed
  the same hour by `GeomCompact` (below) and rerun (r2).

**The compaction (`GeomCompact`, `GeomMoveDown`) [C]:** when no hole fits but the free bytes and a free entry suffice,
once per fit: every valid entry, in offset order, has its bytes moved down to the lowest free offset and then its offset
changed, all inside one seq-odd window (the header's generation + 1); the entries' generations stay, so the server, which
keys its copies by kind and hash, copies nothing again, and a server read that overlaps the window sees the seq move and
reads again. A move under 1 MB of distance is one view (at most 9 MB mapped) and `memmove`; a longer one goes in 1-MB
pieces through two views each (the 32-bit process keeps no large view). A view that cannot be made frees that entry
inside the window (its draw passes) and ends the pass (`map_fail`). One line per compaction (`geometry section compacted
for a CPU-built buffer of N B: F B were free but in no hole that fits; M B moved in T ms`); totals `compactions`,
`compact_kb`.

**Live run r2: the same with the compaction (c5h-d, 2026-11-20 03:27-03:34 EST; composite 013E39F7, the rest as r1;
record `render/evidence/0561/0597/check_tiny_r2.json`; check ok) [L]:**

| Step | entries | used B | full (logged) | evicted (room) | compactions | server CPU keys / r32 / nogeom |
|---|---|---|---|---|---|---|
| city A, walls | 2 | 108,160 | 0 | 0 | 0 | 1 / 1 / 0 |
| city B, the Great Wall | 5 | 900,848 | 2 (1) | 0 | 0 | 2 / 3 / 1 |
| 40 s later | 5 | 900,848 | 4 (1) | 0 | 0 | 2 / 3 / 1 |
| city A's walls removed | 4 | 1,017,776 | 4 (1) | 2 (2) | 1 | 2 / 2 / 0 |
| city A's walls given back | 5 | 1,038,896 | 6 (2) | 2 (2) | 1 | 2 / 3 / 1 |
| one AI turn (18 -> 19, 8.6 s), end | 5 | 1,038,896 | 9 (2) | 2 (2) | 1 | 2 / 3 / 1 |

- City B's ring index buffer (225,084 B this run) refused at 03:30:27 (logged); city A's walls removed; at the next retry
  the LRU freed city A's two buffers (255,888 B free, no hole of 225,088), the data was compacted (**792,676 B moved in
  0.97 ms**, one workstation run: an indication) and the buffer held at 03:31:47 (`refused full 4 time(s) is held now`);
  the server then held the ring (CPU keys city B's wall + the ring, `nogeom` 0): drawn by the server, dropped from the EXE's
  frame.
- City A's walls given back: its index buffer fitted (21,120 B), its 87,040-B vertex buffer did not (9,680 B free):
  refused and logged (the second `REFUSED full` line), so the EXE drew city A's walls (server `nogeom` 1). The held index
  buffer alone is of no use until its partner fits [I: 21 KB kept idle; the idle rule does not free it while the EXE
  draws the mesh].
- No LOUD line, no crash, no box; restored by SHA-256 (`backup_restore_ok` true, `provision verify` 0).

**Live run r3: the huge late game, the default 32 MB section (c5h-d, 2026-11-20 03:47-03:55 EST; C5H-0597-HUGE_0350, a
copy of C5H-VP8P-HUGE_0350, SHA-256 C499D3C6..., removed at the restore; the build of r2; record
`render/evidence/0561/0597/check_huge_r3.json`; check ok) [L]:** load, a Lua count, a tour of 24 LookAt points (a 6 x 4
grid over the map, 4 s each, a snapshot at each), back to the capital, one AI turn. (r2's huge part had a Lua error,
`Map.GetGridWidth` does not exist: no count, no tour; `check_huge_r2_no_count.json`.)

- **The map (Lua, turn 350):** 128 x 80; **124 cities, 121 with a walls-class building** (`BUILDINGCLASS_WALLS`, 3 types
  in the database; 120 with real walls, 1 with free walls only), **1 Great Wall**, every city revealed to the active team;
  **188 river crossings with a route on both plots** (189 after the turn).
- **What the cache held:** at the load 20 entries, 2,833,152 B (10 CPU keys); after the tour **251 entries of 512 (49 %),
  11,710,656 B of 33,554,432 (35 %), 195 CPU keys**, 11,436 KB copied in 59.1 ms in all (an indication: one workstation
  run); after the AI turn (350 -> 351, 35.0 s) unchanged. `full` 0, `evicted` 0, `compactions` 0, no `REFUSED` line, no
  LOUD line, no crash, no box; restored by SHA-256.
- **The EXE does not draw every CPU-built mesh on a huge map [L]:** the tap's `r32` (CPU-built draws in a frame) was 0
  at 10 of the 24 tour points and at most 28 (at (96,30)); the entries grew only where the camera went (23 -> 45 -> 78 ->
  99 -> 136 along the row y = 30, x = 10 to 96). So the EXE culls these meshes by the view on this map; 13.9 d1's "named every Present
  even off screen" holds on the Tiny (the whole map near the camera [I: a cull by distance or by map chunk that a Tiny map
  never leaves]). *Corrects 13.9's consequence "the section holds every CPU-built mesh of the map": it holds the meshes
  drawn in the last idle window (18,000 Presents, about 10 minutes at the instance's 30 fps).*
- **Against the bounds [I]:** the tour did not see every city (251 entries for 121 walled cities and up to 188 bridges).
  At about 2 entries a wall mesh and 1-1.5 a bridge (r2 and 0584's d2), a player who looks at the whole map within one
  idle window would need about 242 + 2 + 190-280 = 430-525 entries and, at r3's 46.7 KB an entry, 20-25 MB: the
  512-entry bound is the one a huge late game can reach, the 32 MB one not. Past it the LRU frees the meshes not drawn
  for 120 Presents, which on this map are the ones out of view (r4 checks this with a small section), so a refusal needs
  more than 512 buffers drawn within 2 s at 60 fps, which r3's per-frame counts (at most 28 draws) are far from.

**Live run r4: the huge tour with a 4,096 KB section (c5h-d, 2026-11-20 04:01-04:09 EST; the build of r2; record
`render/evidence/0561/0597/check_hugesmall_r4.json`; check ok after the judge read a torn server read as a fact) [L]:**
r3's sequence with `model_geom_kb=4096`, so that the tour's 11 MB pass through a section a third that size.

- **The LRU under the EXE's own culling:** from the tour's second row on the section stayed near full (`used_b` 2.7-4.19
  MB of 4,194,304) while the meshes in view came and went: **229 entries freed for room** (`evicted_room`; none by the
  idle rule), 295 buffers wanted, at most 115 entries at once; the server released what the compositor freed (CPU keys
  10 -> 71 -> 40, as the camera moved) and drew what was held (`nogeom` 0 at every tour point).
- **Refusals only at the jump back to the capital:** 5 buffers refused at 04:07:11 (logged, 5 `REFUSED full` lines),
  when the meshes around the capital were all needed at once while the tour's last meshes had been drawn less than 120
  Presents before; 3 of them held 20 s later at their retry (`is held now`); the other 2 were not drawn again by then (no
  retry: `full` stayed 5) [I: they left the view]; the server's `nogeom` was 2 at the snapshot 15 s after the jump (the
  EXE drew those meshes), 0 after the AI turn.
- **The compaction live:** 29 compactions; each whole pass moved 0.5-4.1 MB (96.4 MB in all) in 0.4-8.2 ms (median
  3.5 ms, 113 ms in all), up to 11 in one second (04:07:11, the jump back) on the render thread. **Changed after r4:** a
  pass now stops at the first hole that holds the wanted buffer (`GeomCompact(need, ...)`), so it moves only the entries
  below that hole. The server saw one torn read in the run (`torn` 1: the seqlock at work, read again at the next poll),
  no refusal of any kind.
- One AI turn (350 -> 351, 32.6 s); no LOUD line, no crash, no box; restored by SHA-256.

**Live run r5: r4 again on the final composite (CA999B80: the compaction stops at the first hole that fits; c5h-d,
2026-11-20 04:30-04:38 EST; record `render/evidence/0561/0597/check_hugesmall_r5.json`; check ok) [L]:** 229 entries
freed for room again, 293 buffers wanted, at most 114 entries; 4 refusals (logged; 1 at (96,30) during the tour, 3 at
the jump back), 3 held at their retry; server `nogeom` 1-2 at those two snapshots, 0 after the AI turn (350 -> 351,
35.6 s); `torn` 1; no LOUD line, no crash, no box; restored by SHA-256. **The compaction, r4 -> r5:** 29 -> 29 passes,
96.4 -> 59.2 MB moved (a pass 0.5-4.1 MB -> 0.02-3.9 MB, median 2.3 MB), 113 -> 66 ms in all (median 3.5 -> 2.1 ms,
max 8.2 -> 5.3 ms), at most 11 -> 10 in one second (one workstation run each: an indication, no timing claim). In a
section this full the hole that fits is usually near the top, so the early stop saves about 40 %, not more.

**So (2026-11-20):**
- The section's defaults stay (32 MB, 512 entries): a huge late game held 11.7 MB and 251 entries after a tour of the
  map, and on a map that large the EXE culls these meshes, so when the entries run out the LRU frees the ones out of
  view (r4) and a refusal needs more buffers drawn within 120 Presents than any frame showed. Where the section does
  fill (a Tiny map with a small section, or the jump back in r4) the refusal is logged and the EXE draws the mesh.
- The fix the live runs called for was the fragmentation (r1), not the size: the compaction (r2, r4).
- `model_geom_kb` is read (not removed); a smaller value is for tests, a larger one for maps beyond huge.

**Not verified (13.10):** a game with more than 512 CPU-built buffers in view within 120 Presents (none seen); the
compaction's render-thread cost with a full 32 MB section (r4/r5 measured a 4 MB one; a pass moves at most the section,
so up to about 8x r5's 5.3 ms [I]) and any timing on a quiet machine; the 43-civ maps (left out by the MC, #7588); the idle rule over a whole long session on a huge map;
which of r1's buffers were the ring's (by size only); the 4,096-record table's 3/4 bound over a long session (r3 used
251 records, r4 251: a record stays for every buffer ever seen, so a session that sees more than 3,072 distinct
CPU-built buffers stops copying new ones, logged once as `REFUSED table_full`, the EXE then draws them).

### 13.11 "Bridges displaced, not joined to the roads": a RoadLayout effect, not the bridge draw (ticket 0806, impl-0806, account B, 2026-11-26)

The beta 4 player report (relayed by the owner, no save or picture) [reworded for privacy]: bridges sit out of place and do not
join the roads properly. Branch `task-0806-bridges`. Marks: `[C]` code read, `[L]` the live runs below, `[I]` inferred.

**The code path has no bridge-specific transform [C, 2026-11-26].** `drawCpuItems` (`render/src/landmarks/draw.cpp`)
draws a material-3 CPU-built item exactly as a library material-3 item: VS cb0 = the tapped instance's 64 bytes of rows
(`scene.rows[instRow]`), VS cb1 = the tap's camera constant with the frame's view rows written by `applyFrameCamera`
(wrap copy k, 17.2), the EXE's own index and vertex buffers from the geometry section (13.2), keyed by content. Nothing on
the server bends, moves or lifts a bridge (the live-terrain height add of landmark-renderer.md 7.8 is not built). So a
bridge can only land where the EXE's own draw of the same frame put it.

**Live runs (c5h-new-a, EXE build 26577625, mods cp,vp,eui, Vibrant Textures; 2026-11-26 13:09-13:48 EST; MC-B
#17478) [L]:** the beta 4 hotfix 1 tier build (`C:/c5-64bit-work/cand7/tier/build`: server.exe `58b0f449...`,
shell_composite.dll `40a94ed7...`, laid by lay.py and restored by SHA-256, verify ok), `[Renderer]` ServerRenderer,
ServerUnits, ServerCities, ServerLandmarks, ServerFarmFields, ServerCombat = 1; a copy `C5H-0806-HUGE_0350` of
`C5H-VP8P-HUGE_0350` (huge, turn 350, the active team has bridge building), removed after. Driver
`server/evidence/0806/b0806.sh` + `bridge_look.lua` (the crossings visible to the active team with the same route on both
plots, two per axis NE / E / SE and route nearest the capital: 7 found, 6 rail, 1 road), `cmp0806.py` (crops). One hidden
client at a time; four sessions: stock, product (defaults), roads (product + `[Extras] EnableVisualAdditions = 1`,
`RoadLayout = 1`), stock2 (stock again). Shots in Visual Checks (issue 0806, DIAGNOSTIC; local copies
`F:/c5-64bit-work/0806/live/shots`, not committed).

- **Defaults: the server's bridges sit on the server's routes and join them.** At crossings 2, 4, 5 and 6 (1, 3 and 7
  under trees or a city) every bridge deck the server drew starts and ends on a rail or road the server drew from the
  decal tap, the same session's EXE layout. Server counters at the end: `cpu held=57 keys=41 r32=16 resolved=16 nogeom=0
  stride=0 unnamed=0 notex=0`, no refusal.
- **Stock against product differs at most crossings, and so does stock against stock.** The EXE lays its route curves at
  random per session (extras.md X0, 14 sessions) and bends each bridge along its session's curve (landmark-renderer.md
  8.2): crossing 2 shows two straight vertical decks in session stock, a diagonal deck along the bank in product; crossing
  5 has **no bridge at all over two route crossings in session stock** (the rails run through the river) and two joined
  bridges in stock2 and in product. Centre crops (640 x 420) within 16 against session stock: stock2 0.06-0.95, product
  0.29-0.86, roads 0.31-0.82 (the same spread; stock2's crossing 4 shot is a grey capture frame). So a picture of stock
  is no reference for a single bridge; the reference is the EXE's own draw in the same session.
- **With RoadLayout = 1 the report reproduces.** The server lays its own routes (extras.md X3) in place of the EXE's
  route decals, but the bridges stay the EXE's CPU-built meshes, bent along the EXE's curves of that session: decks with
  no road at one end or both (crossing 4's upper-left deck, crossing 5's right deck), decks slanted across our straight
  track (crossings 2 and 4). This is the open point of extras.md X0.2 ("whether the generator's own bridges must follow
  its own curves", X4 `FixRoadBridges`, reserved id 19).

**Root cause:** `RoadLayout` (a visual addition, default 0, needs `EnableVisualAdditions = 1`) replaces the EXE's routes
but not the EXE's bridges, so every crossing where our route differs from the EXE's curve shows a deck off our road. With
the defaults the bridge draw is the EXE's own (same buffers, same placement); no displacement was seen at the 7 crossings.

**What changed (MC's choice (c)+(b), MC-B #17646):** no draw change; the `RoadLayout` help text (`vp64_options.cpp`, the
default file's comment) and `docs/OPTIONS.md` say that with 1 or 2 bridges keep the game's placement; options tests
1170/1170 in the x86 and x64 trees, installer schema 27/27 (2026-11-26). Moving the EXE's bridges onto our roads is ticket
0808. The scene library needs nothing (no server drawing changed: rule A not touched).

**Not verified:** the reporter's options (asked on thread 0806); pontoon bridges; the ANC (earlier era) bridge pieces;
other saves and the 43-civ layout; a same-session overlay of the EXE's and the server's bridge (no live switch draws
both; the offline compare of 13.6, P6_road within 16 0.988, is the evidence for the placement).

## 14. The model pass's cost on the server (ticket 4884, worker-4884, 2026-11-19)

Branch `task-4884-model-pass-cost` (team branch ca5a6ee7c merged). Why: 4882's live run (11.12 (b)) found the server's frame
past the composite's 3,400 us lockstep bound about half the time with the units and the models on (`submit_ms` 1.5-1.7
ms before the model bits, 2.6-3.9 ms after); where the time went was not measured. The reader's accept (copy, readTap,
resolve) was already cheap live: `copy_ms` 0.12 ms per frame, max 0.62 (4882's filter arm, render.log), so the cost was
in the draw.

### 14.1 The stage timers (offline, `cityscene run --bench N`)

`cityscene run ... --bench N` (render/tools/cityscene/cityscene.cpp `runBench`) repeats, after the pictures, the server's
per-frame path on the recorded tap N times: the reader's copy of the frame and the poll's compare with the previous one
(`copy`), `readTap` (every check and the hash, `read`), `resolve` and `resolveCpu` (`resolve`), the layer's DROPPED
filter and `keepTapCamera` (`accept_rest`), then per target as `C5rLayer::draw`: `InstallInputs::fill` (`inputs`),
`applyFrameCamera` at the tap's own camera rows (`camera`), `ModelRenderer::draw` without the id pass (`draw`, split by
`DrawReport::uploadMs` = the frame's row and constant buffers on the device and `itemsMs` = the item loop's state, binds
and draws; `contextCalls` = the context calls the colour pass issued), with a GPU timestamp pair around it (`gpu`). Each
frame is flushed and waited for; the first 10 frames are not counted; `DIR/bench.tsv` has a row per frame. Inputs: 4882's
recorded frames (`frame.tsv` / `frame.mtap` of its runs Fanc01b-install, W01-install, Lanc07-install: 3910's exports on
F:, the workstation's Steam install with Vibrant Textures, the database copy of 4882), `--inputs install` (the server's
PS sources). The summary: `render/evidence/0561/4884/bench_summary.py` (per run the median of each column; per arm the
mean of the run medians with a t interval; B - A with Welch's interval and the smallest detectable difference).

### 14.2 Where the time went (arm A = the team branch's draw layer with the timers only)

Series s1 (14.4: 6 runs x 300 frames per arm and frame, workstation, DIAGNOSTIC indication, D106), arm A, per frame, ms,
the mean of the run medians [C]. Items per frame: Fanc01b 30 reflection + 534 scene, Lanc07 29 + 348, W01 6 + 105 (and
4879's CPU-built items).

| Stage (arm A) | Fanc01b | Lanc07 | W01 |
|---|---|---|---|
| copy (the reader's copy, the poll's compare) | 0.017 | 0.014 | 0.006 |
| readTap | 0.062 | 0.049 | 0.021 |
| resolve (+ resolveCpu) | 0.203 | 0.109 | 0.043 |
| accept rest (DROPPED filter, keepTapCamera) | 0.006 | 0.004 | 0.003 |
| inputs + camera, both passes | 0.025 | 0.016 | 0.010 |
| reflection pass draw (upload / items) | 0.084 (0.033 / 0.046) | 0.070 (0.028 / 0.039) | 0.058 (0.033 / 0.023) |
| scene pass draw (upload / items) | 0.446 (0.031 / 0.411) | 0.309 (0.024 / 0.281) | 0.117 (0.033 / 0.083) |
| **CPU, the whole path** | **0.853** | **0.587** | **0.262** |
| GPU, both passes (timestamps) | 0.592 | 0.438 | 0.263 |
| context calls, reflection + scene | 437 + 7,128 | 419 + 4,720 | 109 + 1,420 |

So on the CPU the scene pass's item loop dominated (13.3 context calls per item, 0.77 us per item on Fanc01b), then
resolve (two vectors per tap draw and, with readTap, a string per draw in `checkFrame`), then the buffer creation (about
0.03 ms per pass, twice per frame). The accept stages together (0.29 ms on Fanc01b) are 4882's live `copy_ms` (0.12
ms on the Tiny save, fewer draws). The GPU interval of a pass was 4-25 times the GPU time of the same draws after the
cut (14.4); the draws themselves take about 0.01-0.03 ms of GPU, so most of arm A's GPU interval came with the buffers
made per pass [I: the new IMMUTABLE buffers' initial data uploaded and made resident inside the interval; not profiled].

### 14.3 The cut (commit on the branch; no picture change)

- **The frame's buffers** (`ModelRenderer::uploadFrame`): the row block (vertex slot 1 of materials 4 / 5, t27 of
  material 6) and each tapped constant were new IMMUTABLE buffers made by `CreateBuffer` in every pass (scene and
  reflection, and the id pass of the harness) and released at its end (landmark-renderer.md 7.8). Now one DYNAMIC row
  buffer kept by the renderer (grown in 64 KB steps, 1.5x the need) and one DYNAMIC constant buffer per constant index
  of exactly the constant's size (as before: a shader never sees a larger buffer), written with `Map(WRITE_DISCARD)`.
- **The pipeline state** (`ModelRenderer::Bound`): every item set the input layout, both programs, the three states, PS
  t0-t1 and t2-t4, VS / PS cb0-cb1 and the index buffer, then the vertex buffers: 13-15 calls before its draw. Now each
  setter is called only when its value differs from what the pass bound last (the first item of a pass sets everything;
  the vertex slots are compared per slot, so material 3's one-slot set does not hide slot 1); the id pass binds its b13
  once. The calls, their order and their values are those of the full set minus repeats, so the GPU sees the same state
  at every draw.
- **Lookups per draw**: `texture(part.t0 / t1)` (a lower-cased base name built and a `std::map<std::string>` searched,
  twice per item and pass) is looked up once per file and part (`partTextures`; a name's view never changes once made,
  a failed one stays null as `texture()` keeps it); the file's buffers once per file (`fileGpu_`).
- **resolve / checkFrame** (tap.cpp): `resolve` built two `std::vector`s per tap draw (the index's hits and the fitting
  ones); it now walks the index's range in place (`WireIndex::findRange`, `hasGeometry`) with the same first-fit rule;
  `checkFrame` built a `std::string` naming the draw for every draw of every frame (twice per frame: in `readTap` and in
  `resolve`), now only for a refusal.

Picture [C, 2026-11-19 03:44-03:58 EST]: `cityscene.py frame ... --inputs install --keep` with each arm's tool on 3910's
Fanc01, W01 (with 4879's CPU-built items) and Lanc07: every `ours_<t>.rgba` and `ours_<t>_id.u32` of both targets is
byte-equal between the arms (SHA-256), and the C1 figures are 4882's (11.12): Fanc01 median 0, p95 0, max 0, within 16
1.0000 on both targets; W01 reflection max 2, scene max 81, within 16 0.9927 (draw 617, 7.3); Lanc07 max 4 / 0, within
16 1.0000.

### 14.4 Offline series: A against B

Series s1, 2026-11-19 11:04-11:14 EST, workstation (DIAGNOSTIC indication, D106: timing belongs off the workstation; the
release-grade cost is 4878's oldpc P7). Arm A = `cityscene.exe` of commit 1 on the branch (the timers on the team
branch's draw layer, SHA-256 `8bbf1bed...`), arm B = the cut (`0f1db7d9...`); 6 rounds, arm order rotated per round
(A B, B A, ...), each run 300 timed frames on each of Fanc01b, W01, Lanc07, `--inputs install`. Quiet-machine lock taken
with `--overlap`, **DISTURBED**: one sceneshot of worker-4883 11:04:45-11:04:50 (round 1's first run); total CPU mean
27.1 %, max 43.1 % (the lock's summary). Statistic: per run the median of the column, per arm the mean of the 6 run
medians, the difference with Welch's 95 % interval, "detectable" the smallest difference the series could detect
(`bench_summary.py`; the whole table `render/evidence/0561/4884/s1_summary.tsv`; the series' scripts `series.sh` /
`bench_inner.sh` beside it). [C]

| Per frame, ms (mean of run medians, sd) | A | B | B - A (95 %) | detectable |
|---|---|---|---|---|
| Fanc01b CPU, the whole path | 0.853 (0.053) | 0.284 (0.014) | -0.569 (+-0.058) | 0.063 |
| Fanc01b scene pass draw | 0.446 (0.027) | 0.116 (0.002) | -0.331 (+-0.028) | 0.031 |
| Fanc01b resolve | 0.203 (0.015) | 0.045 (0.009) | -0.158 (+-0.017) | 0.021 |
| Fanc01b GPU, both passes | 0.592 (0.056) | 0.036 (0.001) | -0.555 (+-0.058) | 0.064 |
| Lanc07 CPU, the whole path | 0.587 (0.023) | 0.260 (0.052) | -0.326 (+-0.057) | 0.065 |
| Lanc07 GPU, both passes | 0.438 (0.044) | 0.029 (0.001) | -0.409 (+-0.046) | 0.050 |
| W01 CPU, the whole path | 0.262 (0.031) | 0.096 (0.020) | -0.166 (+-0.035) | 0.042 |
| W01 GPU, both passes | 0.263 (0.010) | 0.026 (0.001) | -0.237 (+-0.010) | 0.011 |

B's stages on Fanc01b: copy 0.015, readTap 0.045, resolve 0.045, reflection draw 0.032 (upload 0.007, items 0.021), scene
draw 0.116 (upload 0.005, items 0.109); context calls 127 + 1,769 (Lanc07 122 + 1,033, W01 58 + 344). Without round 1
of Fanc01b (the overlap) the CPU difference is -0.572 (+-0.075, 5 runs per arm): the disturbance did not move it.

**Why the numbers moved** [C for the counts, I for the per-call costs]: the scene pass's context calls fell from 13.3 to
3.3 per item (what is left per item: the vertex slots, whose slot-1 offset is the item's own rows, the draw, and the
textures and index buffer when the part changes), and the per-item `texture()` and map lookups went, so the item loop
fell from 0.77 to 0.20 us per item; the buffers per pass went from `CreateBuffer` to `Map(WRITE_DISCARD)` (0.03 ->
0.005-0.007 ms per pass) and with them most of the GPU interval (14.2); resolve lost its two vectors per draw and
`checkFrame` its string per draw (readTap -0.017, resolve -0.158 ms on Fanc01b).

### 14.5 Live: the DIAGNOSTIC A/B on c5h-c [L, 2026-11-19 11:19-11:55 EST]

`server/evidence/0561/4884/chain_4884.sh` / `run_4884_live.py` (4882's driver copied; `live_table.py` prints the table):
Goldberg c5h-c (C:, `mods548`, Vibrant Textures), the product path of 4882 (layout and options backed up by SHA-256 and
restored by hash; `frontend.py restore` and `status` after the prep and at the end; D155 watcher per launch); the pinned
`x64-c29442e8-p58724e0` x64 DLL (`2B56A1CD...`), the proxy (`63F24332...`) and composite (`DB0DDD04...`) of the team
branch ca5a6ee7c; VP64Options.ini `[Renderer] ServerRenderer = 1, ServerUnits = 1, ServerCities = 1, ServerLandmarks =
1` in both arms; Tiny copy `C5H-4884-TINY_0018` of `C5H-3000-TINY-T0018` (E337244C, turn 18). The arms differ only in
`VP64\server.exe`: **before** `4ACB6DD5...` (ca5a6ee7c's sources), **after** `33213869...` (this branch: ca5a6ee7c and
the cut), swapped between launches with SHA-256 checked; one launch per arm and tag, each with the 12 s still and 20 s
camera-step windows of 4882, a reload and one AI turn; the quiet-machine lock taken per launch with `--overlap`. Every
window was **DISTURBED**: other owners' builds and tests ran in all six (total CPU mean 23-30 %, one sample at 99 %),
and in before r1 also worker-4878's game on c5h-b (lead6 #6950): r1 of before is excluded below.

| Arm, tag (order) | still: exact / older (share) | camera steps: exact / older (share) | submit_ms still / steps (max) |
|---|---|---|---|
| before r1 (excluded) | 394 / 2 (0.995) | 600 / 9 (0.985) | 2.14 / 2.25 (6.50) |
| after r1 | 396 / 0 (1.000) | 602 / 9 (0.985) | 1.91 / 2.01 (5.70) |
| after r2 | 397 / 4 (0.990) | 599 / 11 (0.982) | 2.06 / 2.17 (6.44) |
| before r2 | 367 / 1 (0.997) | 626 / 18 (0.972) | 2.13 / 2.26 (5.45) |
| after r3 | 370 / 0 (1.000) | 633 / 9 (0.986) | 2.03 / 2.11 (6.29) |
| before r3 | 395 / 1 (0.997) | 604 / 11 (0.982) | 2.19 / 2.22 (6.47) |

(Launch order: before r1, after r1, after r2, then a fresh prep, before r2, after r3, before r3: 11:36 the driver refused
its own swap back to before, so the first chain's restore found the after server in VP64 and provision's `set-proxy
--original` refused; repaired by hand by hash at 11:37-11:38, `provision verify c5h-c` rc 0; the driver fixed, 14.7.)

- **The acceptance's lockstep** (exact pairs >= 95 % in the still and the camera-step windows with ServerUnits 1): held
  in every arm of both servers (the lowest 0.972, before r2's steps). Every other point of 4882's check held in all six
  (a1 inputs, a2 pictures taken, b no give-up in the windows and the pair wait 0, c the load begins, d no crash, dump or
  box).
- **Server `submit_ms`** (the mean of the window's 1 s means): before 2.16 / 2.24 ms (still / steps, n 2), after 2.00 /
  2.10 ms (n 3): B - A -0.16 (Welch 95 % +-0.18) and -0.15 (+-0.2) ms: the direction of the offline series, but not
  separable from 0 with these few disturbed launches.
- **4882's finding did not reproduce today with the before server**: its submit was 2.1-2.3 ms, not 4882's 2.6-3.9 ms,
  and its lockstep held (4882's filter arm: exact / older 205 / 165 still). Not established why [I: the team branch's
  server after 4882 (ca5a6ee7c, not af7d90583), the machine's load, the scene the AI turn left]. So on this Tiny save the
  bound was not passed in this session in either arm, and the cut's measured effect is the offline one (14.4); the
  release-grade cost is 4878's oldpc P7 (12.5; lead #6913: with the options and Units 0 the server's CPU +2.66 ms per
  frame on oldpc, before this cut; a P7 rerun on this branch would give the cut's release-grade number). (Done
  2026-11-19, ticket 4887, 12.8: +0.20 ms per frame idle with the cut, oldpc, grade A.)

### 14.6 Tests

Runs 2026-11-19 on the branch with the team branch 5f26e0c2b merged (workstation): `render/build.py --config Release
--test -R c5r_landmarks` 7 of 7 passed, 0 skipped (12:0x EST); `render/build.py --asan --test -R c5r_landmarks` 7 of 7
passed, 0 skipped, 0 ASan reports (peak commit 564 MB). `server/tests/render/cities` (`build_models_tests.py`,
`test_models.py`): plain 25 passed (12:2x EST), ASan (`--asan`, `C5_MODELS_ASAN=1`) 25 passed, 0 ASan reports (12:3x
EST); that suite drives the module with a stand-in layer, so it does not reach the draw layer the cut changed. server.exe
and the proxy built clean (release, `build_server.py`) with the cut before the merge of 5f26e0c2b (the after arm of 14.5).
No test was added for the cut: what it must keep is the picture, and the byte-equal pictures of 14.3 (both targets, three
frames, the id buffers too) are that check; `test_draw`'s block drawn with the EXE's programs and `test_inputs`'
`unbound_inputs_draw_black` run through the changed draw path.

### 14.7 Not verified

- Any number off the workstation: the release-grade cost is 4878's oldpc P7 (D106); both series here are DIAGNOSTIC
  indications on a shared, busy machine. (Corrected 2026-11-19, ticket 4887: the cut's release-grade number is now 12.8,
  oldpc: the server's CPU per drawn frame with the model options fell from +2.66 to +0.20 ms idle, -2.46 [-2.63, -2.29].)
- The GPU interval's cause in arm A (14.2: the buffers' upload and residency inside the interval) is inferred, not
  profiled; the GPU timestamps of the live server were not read (the server's `submit_ms` is CPU time to the sink's end).
- A bigger map than the Tiny save: the cost grows with the dropped draws (about 0.20 us of CPU per scene item after the
  cut on the workstation, 14.4); a huge late-game city view was not measured.
- The id pass of the harness (cityscene) shares the cut but is not timed; the reflection pass of the server was timed only
  offline.

## 15. 0561's open checks (ticket 0585, impl-0585, account B, 2026-11-20)

Written 2026-11-20 by impl-0585 on `task-0585-model-checks-stock-sessions-asan` (main 0afdb9394). The five points 0561's
hand-backs left open (11.12 "Not verified", 12.6, 12.7): the drawn-whole gate with real stock sessions, the ColorKey style
of t6, PS cb1 row 16, an ASan run of `models_test`, a huge save. Scripts: `render/evidence/0561/0585/`. Marks: `[C]` this
ticket's own read of a disassembly, a dump or a capture; `[L]` this ticket's live runs.

### 15.1 t6: which Buildings ColorKey style the EXE binds [C, 2026-11-20 01:05 EST]

Read with `llvm-objdump -d` (LLVM's, scratch only, D83) of the pre-update EXE copy of c5h-d (`CivilizationV_DX11.exe`,
PE stamp 0x546CD0A8; never the Steam install), the three builders that copy the BuildingShader's texture handles:

| Builder | t6 (draw object +0x24) | t9 (+0x2a) | So |
|---|---|---|---|
| `0x5a24a0`, the city-building template (cities and wonders, 2.x) | `movzx edx, word [0xd90e88]` at `0x5a25c0` | `[0xd90e60]` at `0x5a25f9` | Buildings style 0 = **Generic**, fixed |
| `0x7b2150` (landmark / improvement draw objects) | `[0xd90e88]` at `0x7b2660` | `[0xd90e60]` at `0x7b26ae` | **Generic**, fixed |
| `0x75a220` (the walls' material entries, cities-and-landmarks.md 1.1) | `movzx edx, word [4*ecx + 0xd90e88]` at `0x75a5f5`, ecx = draw object `+0x4d4` | `[4*edx + 0xd90e60]` at `0x75a64a` | the style of the record's `+0x24` value (clamped 1-4 at creation: America, Asia, Africa, Europe in the table order of asset-pipeline.md 3.5); the TerrainFOW volume of the same style as t9 |

The table is `0xd90e4c + type * 0x14 + style * 4` with Buildings = type 3 (asset-pipeline.md 3.5), so `0xd90e88` is
Buildings Generic. **Bytes**: on c5h-d's install (Goldberg copy, Vibrant Textures present) the five
`Assets/colorkeys/Buildings_<Style>_ColorKey.tga` files are byte-equal (SHA-256 `baf5e23ad8d86009...`, also equal to
`Terrain_{Generic,America,Asia}`), and the five TerrainFOW files are byte-equal (`fc9b869406264f62...`), as 4303 found on
ten installs (terrain-surface.md 12.2). So the server's `InstallInputs` (t6 = `Buildings_Generic_ColorKey.tga`, t9 =
`TerrainFOW_Generic_ColorKey.tga`, 11.12) is the EXE's own choice for every city, wonder and landmark draw, and for the
walls it equals the EXE's on every install read; it would differ only for walls on an install whose Buildings or
TerrainFOW style files differ (none known). *Replaces 11.12's "(which style the EXE binds is not read [I])".*

### 15.2 PS cb1 row 16 is the main camera's eye position [C, 2026-11-20 01:10 EST]

`eye_row16.py` and `eye_row16_groups.py` over 4871's six stock exports (P4_w3, P4_w1, P4_nowall, P6_road, P6_rail,
P6_none; RenderDoc exports of stock frames on c5h-b, read only in `C:/c5-64bit-work/0561/4871/exports`): every draw of
the colour programs `f0e1f0a6` and `c704c80b` (3,075 draws). VS and PS bind one and the same cb1 (352 B, 22 rows). For
the 2,808 draws whose rows 0-3 are a perspective projection (render target 2419, VS `7ec4e9a5`, `7a9e17f6`, `c4bb25da`),
the eye E solved from rows 0-3 (the point sent to x = y = w = 0) equals row 16 .xyz to at most 9.7e-5 (relative
7.7e-8, float rounding) in every draw, in the transposed convention; row 16 .w = 1. The 267 draws on render target 13415
(1024 x 1024 R8G8B8A8, the reflection target of 11.12's table) have an orthographic rows 0-3 (no finite eye) and carry the
same row 16 as the frame's perspective draws: row 16 is the main camera's eye in both passes, the reflection pass included. Eyes seen: (991.95, -794.19, 473.70) on P4, (735.95, -782.05..-782.08, 350.73) on P6.

For the server (11.12): rows 16-21 are left as tapped, so the view-dependent terms of the colour PS use the eye of the
tapped frame, one Present old under lockstep (11.12 (2)), while rows 0-3 and 8-12 are the frame's own. While the camera is
still the two agree exactly; while it moves the eye lags by one frame's camera step. Writing row 16 = the eye of the frame's
main view 0 in both passes (the reflection pass too, as stock) in `applyFrameCamera` would remove that lag; not done here
(no product change in this ticket; files outside its touches). *Done by ticket 0598 (15.8, 2026-11-20): row 16 is now the
frame's eye; the paragraph above describes the server before it.* *Replaces 11.12's "[I: row 16 the eye position, a frame
old]".*

### 15.3 The drawn-whole gate with real stock sessions, cameras A and B (method)

12.7 ran two stock sessions at camera A only and read them against 4878's arms of another instance (c5h-b) and pin
(batch 4). Here all frames come from one instance (c5h-d, F:, functional only: no timing claim, MC grant #7477) and one
build: the layout = 0551's batch-5 bin `x64-1fd51353-p1e09fc7` (x64 DLL never edited) with server.exe, proxy and
composite of this branch (`build_0585.sh`, own `git archive` of the pin and own `build_all`), on the Tiny copy of
C5H-3000-TINY-T0018 (E337244C, turn 18).

- Product arms (`run_0585_live.py` = 4878's driver and phases): `on` (ServerCities 1, ServerLandmarks 1, ServerUnits 0) and
  `onu` (all three 1); per camera the composite's presented frame and its passthrough frame.
- Stock (`stock_0585.py` = 4886's session plus camera B): two sessions, the 32-bit pin i0ship-5d18d3d6-ia in the CP slot,
  darkred stock config, no proxy and no server; PrintWindow shots at camera A (UI.LookAt the capital) and camera B (the
  plot 3 east and 2 south of it), popups closed first (D114).
- Compare (`compare_0585.py`; its self-test on 4878's and 4886's frames gives 4886's camera-A figures again: on 99.58 /
  99.59 %, landmarks 99.88 / 99.89 %, onu 99.74 / 99.74 %, without / with the stock1 exemption): `drawn_mask` of the
  arm's passthrough frame, `snapshot_opaque` with stock sessions 1 and 2, `drawn_whole` with the stock1 exemption on
  those pixels (the gate), the same without the exemption, `drawn_whole` on all drawn pixels without stock (4882's form,
  not a gate), and the `models` class against stock's two-session ceiling.
- Chain: `chain_0585.sh c5h-d tiny|stock|huge`.

### 15.4 The gate's verdicts with real stock sessions [L, c5h-d, 2026-11-20 01:18-01:37 EST]

Runs: Tiny arms `on` 01:18-01:24 and `onu` 01:24-01:29 (one prep, restored by SHA-256: `backup_restore_ok` true,
`provision verify` 0); stock sessions 1 and 2 01:30-01:37 (each restored by hash, `frontend.py restore` 0, save copy
removed). No box, no crash, no dump in any run. Both product arms hold 4878's checks (`check-<arm>-r1.json`): m (layer 1,
`env_missing` 0, `draw_errors` 0, 5,766 / 5,614 drawn), f (cities and landmarks dropped, `frames_unable` 0, `place_miss`
0, mismatch 0), l (lockstep exact / older still 396 / 0 and 392 / 5, steps 641 / 1 and 570 / 44, no give-up), d, one AI
turn 18 -> 19 each. `compare_0585.py` (`F:/c5-64bit-work/0585/compare-0585.json`; pictures on F:, not committed):

| Camera | Arm | Drawn px (passthrough) | Gate: opaque px, within 16, verdict | No stock1 exemption | Without stock (4882's form) | Models vs stock1: within 16 (ceiling stock2 vs stock1) |
|---|---|---|---|---|---|---|
| A (capital) | on | 324,968 | 31,524 px, **99.90 %, pass**, no failing blob (median 0, p95 4, max 64) | 99.89 %, pass | 24.22 %, fail | 0.758 (0.810) |
| A | onu | 322,321 | 31,634 px, **99.80 %, pass** (p95 3, max 87) | 99.80 %, pass | 24.26 %, fail | 0.770 (0.811) |
| B (near) | on | 95,542 | 2,288 px, **98.95 %, pass** (p95 7, max 57) | 98.78 %, pass | 21.52 %, fail | 0.657 (0.792) |
| B | onu | 94,075 | 4,058 px, **99.93 %, pass** (p95 6, max 27) | 99.83 %, pass | 20.88 %, fail | 0.706 (0.792) |

Whole kept screen against stock1: on 0.640 (A) / 0.708 (B), onu 0.658 / 0.718, against stock's own 0.705 / 0.749.

- **The gate passes with real stock sessions at both cameras in both arms [L]**; no blob of >= 150 px fails. The same
  gate with off1/off2 as stand-ins failed at camera A (12.6: 96.4 %, the haze blob of 12.7); without stock sessions it
  fails everywhere on what the passthrough frame cannot show (the capital's light column, terrain cover, D136's white
  mask: 11.12), as before. So the COMMON 25 gate is read with two real stock sessions, never with off arms.
- **What the gate covers is small at camera B**: the SNAPSHOT rule keeps a pixel only where the passthrough frame equals
  both stock sessions within 8, so 2.4-4.3 % of the drawn pixels at camera B (9.7 % at A) are gated. A clip outside
  them would not be seen by this gate; the `models` class (not a gate) is the wider check.
- **The `models` class stays below stock's ceiling** (A 0.76-0.77 against 0.81; B 0.66-0.71 against 0.79). Not a gate
  (COMMON 25). At camera B the arm `on` (units drawn by the EXE) is the lowest; the part units, animation phase and fog
  phase (fog-of-war.md 5.4, 12.7) take of it is not separated here [I].
- Stock frames are PrintWindow shots of the whole window, the arms' the composite's backbuffer: as 12.7, so the same
  read. That the two capture routes give the same pixels for the same frame was not checked here [I]; the gate's 0
  median on the opaque pixels says they agree there.

### 15.5 A huge save, live [L, c5h-d, 2026-11-20 01:37-01:45 EST]

Arm `on` (ServerCities 1, ServerLandmarks 1, ServerUnits 0) on C5H-0585-HUGE_0350, a copy of C5H-VP8P-HUGE_0350 (C499D3C6,
turn 350, the instance's own save folder; copy removed at the restore), the same build and instance as 15.4, 4878's
phases and checks (`check-huge-on.json`); restored by SHA-256 (`backup_restore_ok` true, `provision verify` 0).

- Load to in game 181.7 s; one AI turn 350 -> 351 in 38.6 s; no box, no crash, no dump.
- m: layer 1, `env_missing` 0, `draw_errors` 0, 7,307 drawn in the last second, 5,561 keys; no LOUD model line.
- f: dropped 563,904 city and 2,538,862 landmark draws of 3,103,389 tapped; `frames_unable` 0, `place_miss` 0, mismatch
  0; `passed_set_cities` 374 and `passed_set_landmarks` 961 (0.04 % of the tapped draws: draws of the identity set the
  filter let through to the EXE; when in the run was not separated [I: the frames around the bits' set and the reload]),
  `not_in_set` 238.
- CPU-built meshes (`modelgeom totals`): 37 entries held, `full` 0, `failed` 0, 3,189 KB copied (this camera and this
  save do not reach 0597's full cache).
- l: lockstep exact / older still 280 / 88, steps 461 / 181, no give-up; pairing exact 315 of 7,307, `max_lag` 131.
  A busy shared machine (other teams' suites ran, `qcheck-on.txt`): an indication, no timing claim (functional instance).
- Memory: EXE private 1,459.5 MB after the load, 1,555.5 MB at the end (largest free block 1,722 MB); server private
  2,136.6 -> 2,278.6 MB.
- Pictures (Visual Checks, issue 0585, DIAGNOSTIC): Thebes with its wonders, the Great Wall, railways and improvements
  shaded as the EXE's at camera A; no black model (seen by eye on the presented frame).

### 15.6 `models_test` under ASan [C, 2026-11-20 01:46-01:50 EST]

`models_asan_0585.sh` at fd0d36965 (the product source of main 0afdb9394): `build_models_tests.py` plain (clang-cl x64)
and `--asan` (MSVC `/fsanitize=address`), each through `quietlock.py run`; `test_models.py` (a case fails on a non-zero
exit or any "AddressSanitizer" text): **plain 27 passed (48.8 s), ASan 27 passed (59.6 s)**, no AddressSanitizer report in
any log. (4885 and 4884 had run it under ASan on the team branch with 25-26 items; this is the run on `main` after the
merge, 27 items.)

### 15.7 Not verified

- The gate at other cameras and saves than 4878's two Tiny cameras; the huge save ran the product arm only (no stock
  session on the huge save, so no gate there; the check there is 4878's m/f/l/d and the picture by eye).
- The `models` class gap to stock's ceiling (15.4) is not separated into units, animation phase and fog phase.
- When the huge run's 1,335 `passed_set` draws happened (15.5).
- ~~Row 16 of the server's draws is still the tapped frame's eye (15.2); the fix is named, not made.~~ Made by ticket 0598
  (15.8, 2026-11-20).
- The walls' style volumes on an install whose style files differ (none known, 15.1).

### 15.8 PS cb1 row 16 = the frame's eye in the server's model draws (ticket 0598, impl-0598, account A, 2026-11-20)

Branch `task-0598-models-eye-constant` (from main 876eedeab). Scripts and acceptance: `render/evidence/0561/0598/`.

**What the server does now [C, code and tests].**
- `srv_render_scene.cpp` (draw(), the models camera block): `FrameTextures::haveEye / eye[3]` = `srvmodels::FrameEye`:
  the camera-to-world matrix's row 3 (VPShellCamera `camWorld`, the FCamera position; the unit scene's eye too) when it
  is finite and not all zero, else `EyeOfViewProj` (the point view 0's view-projection sends to clip x = y = w = 0, in
  the row-vector convention that 0585's `eye_row16.py` calls "transposed", 15.2). The reflection pass copies
  `FrameTextures` and replaces only `viewProj`, so it carries **view 0's eye**, not a mirrored one: that is what stock
  binds (15.2: the 267 draws on the reflection target 13415 carry the frame's perspective row 16, [C] from 4871's
  exports). Orthographic and singular matrices give no eye (the reflection camera's own view-projection has none).
- `applyFrameCamera` (`render/src/landmarks/inputs.cpp`): `FrameCamera::eye`; for every PS cb1 constant the target's
  records use (each once, file and CPU-built items), row 16 = **the tap's own row 16 while that constant's tapped rows
  0-3 equal the frame's view-projection** (every element within 1e-6 of the matrix's largest; a pan step moves the
  translation row by about 1e-3 of it), **else the frame's eye** (x, y, z, 1). Rows 17-21 stay as tapped.
  `TapCameraRows` keeps row 16 beside rows 0-3 (`[4]`), so a still frame after a moving one gets the tap's value back.
  `CameraShift::eyeConsts` / `eyeKept` count the two cases (not printed in the models line).
- Why the tap's own value while still [C, replays below]: neither of the server's eyes is bit-equal to the EXE's row 16.
  Writing the solved eye in every frame changed the library's still-camera replays at 0:0 (FOG: 1 pixel per frame, HUGE:
  14, one level each; one Tiny city bundle stopped at the units' pre-check, not separated); writing `camWorld` row 3
  changed HUGE by 6 pixels per frame and FOG by 1. With the tap's own row 16 at a still camera the frames are bit-equal
  to before; the frame's eye is used only once the camera has moved, which is where the tap's eye is a Present old.
- Which PS programs read row 16: the colour programs `f0e1f0a6` and `c704c80b` (15.2); the mask PS `e12e9eaf` reads row 20
  only and `73daa800` no cb1 (5.6), so writing row 16 in their constants changes nothing they draw [C, 5.6's read].

**Tests [C, 2026-11-20, workstation, plain clang-cl unless said].**
- `server/tests/render/cities/models_test.cpp` `scene_camera_rows` (calls `CaseSceneEyeRow16`): `EyeOfViewProj` of a
  look-at x perspective built as `srv_render_scene.cpp` builds view 0's gives P4's eye (991.95, -794.19, 473.70) within
  0.05; the tapped frame's (one pan step, 6 units) is rejected by the same check; orthographic, all-zero and NaN
  matrices give none; `FrameEye` takes `camWorld` row 3 exactly, falls back on all-zero, null and non-finite; the module
  hands the eye to the layer in both passes (the reflection copy with an orthographic `viewProj` keeps view 0's eye).
  `test_models.py`: **27 passed plain, 27 passed under MSVC ASan**, no AddressSanitizer report.
- `render/tests/landmarks/test_inputs.cpp` `frame_camera_eye`: without an eye row 16 keeps the tap's (the frame's-eye
  check fails); with it exactly the target's PS cb1 constants with 17 or more rows get it, once; the reflection target's
  constant gets the same eye; a still camera keeps the tap's row 16 bit for bit (`eyeKept` 1) although the eye given
  differs in the last bits, a pan step writes the frame's eye, a still frame again restores the tap's.
  `c5r_landmarks_test_inputs`: passed (ctest 1 of 1, 0 skipped; C5R_GAME_DIR = the workstation's `pure_vp` copy, read only).

**Rule A: the scene library at tolerance 0:0 [C, 2026-11-20, RTX 4080].** `sceneshot --dumped --compare --no-pictures
--tolerance 0:0` on every bundle of `F:/c5-64bit-work/scene-library/bundles/` (`index.json`, 16 bundles: 4980's nine
and the seven older), player built from this branch and from main 876eedeab (`build_replay.sh`): **after 16 of 16 bundles exit 0, 8 of 8 frames byte-equal
each** (colour and depth, 05:59-06:13 EST); before (main) the same, 16 of 16 (06:13-06:27).
**No recorded picture changes; nothing re-recorded.**

**The `models` class against stock on 0585's cameras.** Before: 15.4's figures (Tiny T18 on c5h-d; A: on 0.758, onu
0.770 against stock's 0.810 / 0.811; B: on 0.657, onu 0.706 against 0.792). After: **the same figures by construction
[I]**: 0585's pictures were taken with the camera still (4878's phases; lockstep pairing exact or "older still", 15.4),
where the tap's rows 0-3 are the frame's and the server now keeps the tap's row 16, the bit-equal case the 0:0 replays
above show. The arms' pictures are no longer on disk and no live run was made (none needed: the score cannot move at a
still camera). So **row 16 explains none of the still-camera gap** of 15.4 (the ticket's hypothesis is refuted for those
figures); the fix acts only while the camera moves, where the view-dependent terms of the colour PS (specular,
reflection, rim) used the eye of a Present before.

**Not verified (0598).** No live run: the moving-camera case is checked by the tests only (no live picture or score while
panning); the 1e-6 same-camera bound was not read against live pairs (the replays show still frames meet it, as the
bit-equal output requires); no ASan run of `c5r_landmarks_test_inputs`; the reflection pass's eye rests on 0585's export
read (15.2), not on a capture of the server's reflection draws.

**Link break and fix [C, 2026-11-20 07:50 EST, fix-0598].** The merge b7df11ad6 made `srv_render_scene.cpp` call
`srvmodels::FrameEye`, defined in `landmarks/srv_models_scene.cpp`; every builder that compiles `server/src/render/*.cpp`
without `landmarks/` (render_host_test, test_scene_units, unitfeed, the fow/forest/farmfields/overlay/shadow scene tests)
stopped linking (`lld-link: error: undefined symbol: bool __cdecl srvmodels::FrameEye(...)`). Fix (branch
`fix-0598-frame-eye-link`): `EyeOfViewProj` and `FrameEye` are `inline` in `srv_models_scene.h`, bodies unchanged. **Rule:
`srv_render_scene.cpp` takes nothing from `landmarks/` but that header** (it is built without the folder by design:
`MakeModelsScene()` is null there); anything it calls from the model module is inline in the header or reached through the
registered factory.

## 16. The model and unit holds count step time; the server's reason in the D119 report (ticket 0641, impl-0641, account B, 2026-11-21)

Ticket 0641 (D118, D223, D185; first beta). Seen by 0637's implementer on 2026-11-21 11:39 EST: c5h-new-a, EXE build
26577625, ServerCities 1, the machine overloaded (the load took 4 min). The server ended a running game:
`models: cities: the readiness or the tap frames were lost in play for 2371 ms, beyond render_models_hold_ms=2000: the
server cannot draw them (D118)`, server exit 5, proxy `FATAL: the server failed a call (on_server_death=exit)`, EXE exit
3, no popup, no report folder. Raw logs (read-only copy): `F:/c5-64bit-work/0641/raw/` (render.log, server.log,
shell_composite.log, vpproxy.log; `run-folder/` with server-report.txt).

### 16.1 What was lost: the server's own render thread [C, the raw logs]

- 11:39:02.99-07.969 render.log: one terrain bake, `pipeline: slow bake: tile (104, 34) ... total 4848.5 ms: ... post
  4845.2 (21 decal textures loaded) ... read 4835.2 ms`, on the render thread (`host 1s: ... submit_max_ms=4865.037`).
- The compositor saw the server stop: `unit_filter: latched word 0x0 (raw 0x13, server stale) at present 114` (04.094),
  `fow_models: the server's mask is late (its heartbeat still for 2034 ms ...)` (05.115), the model tap stopped
  (`modelfilter 1s: ... tap_bits=0x0 ... published=0`), and `late episode 1 ended after 4906 ms` (07.986). The EXE kept
  presenting 15-26 frames a second the whole time (`stats ... frames=15..26`).
- 07.982, the first frame after the bake: the models' freshness stamp was 4.9 s old, so `LOUD: models: cities: no accepted
  tap frame while the bit was set (at a drawn frame)`: bit 0 (the EXE draws the cities).
- 07.98-10.35: more bakes on the same thread (540, 677, 229 ms) and one 779.5 ms render.log write; the server drew 2
  frames in 2.37 s (`drawn=29 -> 31`) while the compositor published 31 tap frames a second again (`published=31,
  unacked 28`). Freshness at a frame was judged after the frame's own bake, so it never came back; at 10.353 the hold of
  2000 ms of WALL time ended the game.

Root cause: the bounds `render_models_no_frame_ms` and `render_models_hold_ms` counted wall time across a stall of the
server itself. Nothing on the EXE side was lost. The synchronous decal reads on the render thread are ticket 0642. Why
the reads took seconds: F:'s latency at that time (diskwatch, MC #9266; 16.6). *(2026-11-23, ticket 0642: the decal
textures are now read by the decal pass's loader thread and a set is used once its names are in;
`terrain-bake-slow-first-use.md` 7. The step-time rule here stays wanted: layer textures, styles and the driver still
can stall the thread.)*

### 16.2 The rule now (srv_models_mode.h, srv_units_mode.h) [C]

- **Step time** (ticket 4730's rule for route S's arming window, unit-combat-server.md): every `onPoll` and `onFrame` of
  the mode writers is a step; a step adds the wall time since the previous step, at most **250 ms**
  (`kModelsStepGapMs`, `kUnitsStepGapMs`). While the render loop runs (16-250 ms per step) step time equals wall time; a
  stall of any length costs 250 ms; with every step a stall the bound still ends, after 30000 / 250 = 120 steps.
- The models' **freshness** is step time since the newest accepted-frame stamp (`lastFrameQpc` greater than any seen; a
  stamp's wall age counts in full when it is first seen, 0 in the product: ModelsScene stamps at the poll that passes it).
  Stale after `render_models_no_frame_ms` (500) of step time.
- **The bound**: `render_models_hold_ms` and `render_units_ready_hold_ms` default **30000 ms of step time** (was 2000 ms
  of wall time, breakdown O5's 2 s). One number per writer (`kModelsHoldMsDefault`, `kUnitsReadyHoldMsDefault`);
  `ModelsOptions` (srv_models_scene.h) carries 30000 and a test checks it equals the writer's constant; the host takes
  the units' default from the constant. Why 30 s: tens of seconds of frames that ran tell a lost tap or readiness from a
  hitch on a busy PC (0637's stall added up to 1.7 s of step time with the new rule, test `mode_stall_steps`), and the
  wait costs the player no wrong picture (next point). The install bound (30 s from the later of the load's first drawn
  frame and the layer's open) and the open's bounds stay wall time: they are start-up bounds, not "lost in play".
- **What the player sees while a loss waits**: the bit is 0, so the compositor passes the EXE's own draws of that class:
  the stock cities (landmarks, units) are drawn by the EXE [C: srv_models_mode.h, unit-draw-filter.md 2.4; live check:
  16.5]. The server's model pass keeps drawing the layer's last tap (`drawPass` does not test the bit), so during a loss
  the cities are drawn by both at the same places (city models do not move). During a stall of the server itself the
  compositor already sees the server stale and passes every draw (unit_filter's `server stale` latch). Lines: one LOUD
  line when a loss starts, one when it recovers (`back after N ms of step time (M ms wall; bound ...)`); a stall of the
  loop is a plain line (`the server's render loop did not run for N ms ...`), the first 20 and then every 100th.
- The writers' status lines add `max_loss_wall_ms`, `step ms= stalls= max_gap_ms=` (models) and `max_loss_wall_ms
  step_ms stalls max_gap_ms` (units). The fatal texts say `lost in play for N ms of frames that ran (M ms wall; a stall
  of the server counts at most 250 ms), beyond render_..._hold_ms=30000`.

### 16.3 The report and popup on a deliberate end (srv_remote_main.cpp, srv_exe_crash.*, proxy_rt.cpp) [C]

- **Why 0637's run showed nothing**: its layout was the harness's (`provision.py set-proxy`: vpproxy.ini **pack=0**,
  vpproxy.log line 3). The proxy writes the D119 report and starts vp64helper only with pack=1 (`CallFailed`); the shipped
  ini has pack=1 (tools/release/README.md). On pack=1 the popup was already there with the server's technical reason as
  its first line (ticket 0511: the proxy reads server.log's `result: FAILED: <why>`; 0600's live arms of 2026-11-20 [L],
  renderer-failure-modes.md). (Corrected 2026-11-21 12:35 by impl-0641; its first root-cause note said "a generic
  popup".)
- **Added**: `SrvRemote::ReportAndExit(code, why)` writes `<run>\server-fatal.txt` first (exit code, a code token, the
  player's words, the reason; scrubbed; one line each) for every code but 0, 3 and the save-variant codes; the proxy's
  `ReportFailure` (pack=1) reads it when its exit code is the server's: the reason goes into [failure]
  (`the server's reason (server-fatal.txt): exit 5, code render-models: ...`), and for a death in a game the token is
  the report's code and the player's words are popup-summary.txt (the popup's `Details:` line, under 0511's reason).
  One report, the proxy's; the server starts no helper on this path. A record whose exit code is not the server's is
  not used (one log line).
- **The quick exit on the shipped layout** (added 2026-11-21 13:40 after MC #9332): live arm B (16.5) took 26 s from the
  fatal step to the server's exit, all of it WriteReport creating six files on a loaded F: (2-8 s each: proxy-lua-calls.tsv
  13:19:55, proxy-lua-names.tsv 13:19:59, gameevents-raises.tsv 13:20:01, server-report.txt 13:20:07, proxy-calls.tsv
  13:20:10, proxy-calls-per-turn.tsv 13:20:12; exit about 13:20:13; the proxy's report and popup at 13:20:23) [C, the
  files' times]. Meanwhile the EXE presented at 30 fps (shell_composite stats) after the compositor dropped the server's
  ring (13:19:51): a world without the server's picture that still takes input, then the end [I: no picture of those
  seconds]. Now `ReportAndExit` on the shipped layout (`SrvExeCrash::ShippedLayout`: the report switch's rule,
  exe_crash_report auto = vpproxy.ini pack=1) for every code but 0 and 3: server-fatal.txt, the FAILED line, server.log's
  result line (`ReportHead`, which 0511's popup reads), one line `server-report.txt and the call tables are not written;
  the server ends at once`, exit. The player's report attaches server.log's tail, never those files. Development
  layouts (pack=0) keep the full report.
- The exit codes and their words (`SrvExeCrash::FatalWords`):

| Exit | Token | Player's words (popup-summary.txt) |
|---|---|---|
| 5 | render-models, render-units, render-fog, render-decals, render-shadows, render-ice, render-overlay, render-forest, render-terrain, render-start, render | The 64-bit server could no longer draw <the cities / the landmarks / the cities and landmarks / the units / the units' shadows / the terrain / the fog of war / the marks on the terrain (roads, farms, improvements) / the shadows / the ice / the map's overlays / the forests and jungles / the game's world>, so it ended the game. |
| 1 | server-error | The 64-bit server met an error it cannot go on from, so it ended the game. |
| 2 | server-exception | The 64-bit server crashed, so the game had to end. |
| 4 | database | The 64-bit server's copy of the game's database went wrong, so it ended the game. |
| 6 | engine-exception | The game's engine failed inside a call from the 64-bit server, so the game had to end. |
| other | exit-N | The 64-bit server ended the game (exit code N). |
| 0, 3, 0x2056SSRR | none | (no record: a clean end; the engine gone first, the server's own exe-crash report of 0418; the save-variant words of 0354) |

### 16.4 Tests [C, 2026-11-21, workstation]

- `models_test` (build_models_tests.py, clang-cl): 28 of 28 cases; new `mode_stall_steps` (0637's shape: 4848 ms stall,
  then 540/677/229/779/146 ms iterations without a new frame: one loss of 1658 ms step time / 2134 ms wall, recovered,
  one LOUD line each way; stalls of 2.4 s and 10 s with the frames back: no loss; a readiness loss across a 10 s stall:
  recovered; a lasting loss at 30 frames/s: fatal after 901 iterations (30 s of step time) with the reason; every step a
  stall: fatal after 120-121 steps; the no-frame rule at 500 ms of step time; the status line; `ModelsOptions()` equals
  the writer's defaults). `mode_stale_and_hold` and `mode_load_ends_loss` were rewritten to step in <= 250 ms steps (the
  old single 1500 ms / 5000 ms steps now count 250 ms).
- `test_units_mode` (build_units_tests.py + server/evidence/0040/3/build_mode_test.py): 25 of 25; new `stall_steps`;
  `loss_recovers`, `loss_fatal`, `load_during_loss`, `options`, `scene_defaults`, `host_keys` follow the step clock and
  the 30000 default.
- `test_exe_crash_report.py`: 135 of 135 with `--build-out` (the real server.exe): the new `fatal` unit cases (every exit
  code's token and words, the record's lines, the scrub, no record for 0, 3, 0x20561622 or without a run folder) and the
  e2e cases `render5` (pack=1: exit 5 at kRt_Boot by a refused render_size, server-fatal.txt code render-start, the result
  line in server.log, NO server-report.txt, the "not written" line) and `render5-pack0` (the full report as before).
  `test_dll_fatal_report.py` 45 of 45 after the change. `models_test` case (h): landmarks alone (gaps of 300-1200 ms
  without a new frame: at most one loss, recovered; a lasting loss ends at 30 s naming the landmarks).
- `test_proxy_session.py`: 183 of 183 (new `death-server-fatal`: one report, stage server-death, code render-models,
  the reason in [failure], popup-summary.txt = the server's words, the helper once, exit 3; `death-server-fatal-stale`:
  a record saying exit 5 from a server that ended with 0xC0000005 is not used, the generic report). `test_proxy_rt.py`
  219 of 219; `test_dll_fatal_report.py` 45 of 45; render host `test_render_host.py` 76 of 76; units scene
  (`test_units_runner.py`) 7 of 7; `models_test` under MSVC ASan 28 of 28, no report.
- Builders of the changed shared sources, linked on the merged state (main af65acf80 with 4733 + this branch, 2026-11-21
  12:25-12:40 EST; main's later fea58538a changes only docs and the tracker): `server/build_server.py --config release --deps keep/pins-0.1b/deps-x64-release` (server.exe and the
  proxy), `build_models_tests.py` (+ `--asan`), `build_units_tests.py` + `server/evidence/0040/3/build_mode_test.py`,
  `build_render_tests.py`, `build_scenebundle_tests.py` (the offline player's programs), `shadow/build_scene_shadow.py`,
  `extras/units/build_units_extras_tests.py`: all OK.
- **Rule A**: no offline scene draws differently. The offline player has no mode block, so the mode writers (the only
  code whose behaviour changed) do not run there (11.4); `ModelsOptions`' hold default is read only by them. No bundle
  member and no `render_config` key changed; the scene library was not re-run [C, by construction].

### 16.5 Live (c5h-d, F:, Goldberg, EXE build 4390913 stamp 0x546CD0A8; 2026-11-21 13:04-13:23 EST) [L]

Grant MC-B 12:42 (window until 13:45). Layout: `server/evidence/0600/lay.py` prep with this branch's release server.exe
(`cb193a97...`) and proxy (`0b099198...`), the stage layout's composite (`x64-1b87dd18-pa4c1c84`, `f8f44aa2...`); a TEST
pack (`tools/release/pack.py`, release `0641-test`, x86_dlls = c5h-d's own `3c2f47da...`) with vpproxy.ini `pack=1`
(checked before the launch); `vp64helper on`; VP64Options.ini `ServerCities = 1`; save a copy of `C5H-3000-TINY-T0018`
(Tiny, turn 18, Madrid on screen) under its own name, removed after. F: was loaded throughout (diskwatch
`%USERPROFILE%/c5-ops/diskwatch/diskwatch-20261121.jsonl`, disk `2 F:`: 13:08:20-13:09:30 idle 0-33 % (mean 33.5),
queue up to 6.3, read latency up to 714 ms, write up to 501 ms; 13:17-13:20:30 idle mean 16 %, latency up to 904 / 807
ms). Tools in `F:/c5-64bit-work/0641/live/stall.py` (by exact ids; not committed); evidence `F:/c5-64bit-work/0641/
live-d/` (armA, armB, the report folder copy, lay.json).

| Arm | What | Result |
|---|---|---|
| A1 | `SuspendThread` on the server's render thread (tid 33436 from render.log, its owner pid 34976 = server.exe checked) for 3000.6 ms, idle on the map, no autosave running | `the server's render loop did not run for 3011 ms (a stall of the server itself, 14 so far): it counts 250 ms`; no loss, no end; the compositor re-applied the identity set at the resume |
| A2 | the same for 12000.9 ms | `did not run for 12023 ms`; no loss, no end. The compositor: `unit_filter: latched word 0x0 (raw 0x2, server stale) ... every draw passes`, `fow_models: late episode 2 ended after 12000 ms`, `server live` at the resume. Picture during the stall: Madrid and its units drawn by the EXE (`vc-20261121-130857-workstation-c38d`, info) |
| A3 | control: the EXE suspended (`NtSuspendProcess`, pid 29072) for 3000.7 ms | the tap frames stop (accepted 2079 flat), no loss: suspending the EXE does not reproduce 0641 (the freshness rule counts a tap that publishes nothing as fresh, 11.4) |
| (all A) | | zero LOUD lines in render.log over the whole run; six natural stalls of the render loop of 280-650 ms (F:'s latency) counted 250 ms each |
| B | relaunch with vpproxy.ini `server_cfg_extra` (TEST ONLY) `render_models_hold_ms=1`, `render_models_no_frame_ms=16` | cities bit 1 at 13:19:45, lost at 13:19:47 (no accepted tap frame), the fatal step: server exit 5; `server-fatal.txt` written (`code: render-models`, `summary: The 64-bit server could no longer draw the cities, so it ended the game.`). The server took about 35 s on F: to write server-report.txt before it exited; then the proxy's report `VP64/reports/20261121-132023` (stage server-death, code render-models, `the server's reason (server-fatal.txt): ...` and 0511's `server's reason: ...` in [failure]; popup-summary.txt = the server's words) and the helper (a D33 session: `failed`); the EXE ended |

The popup (`vc-20261121-132027-workstation-ed73`, the dialog watcher's picture, `seen_by_art` false): "The game's 64-bit
server stopped with this error: models: cities: the readiness or the tap frames were lost in play for 1 ms of frames
that ran (1 ms wall; a stall of the server counts at most 250 ms), beyond render_models_hold_ms=1: the server cannot draw
them (D118)", "The 64-bit part of the patch failed, so the game was ended. Your config.ini is back to your own settings
and the patch is switched off: the next start is the normal game. "Try again next start" switches the patch on again.",
"Details: The 64-bit server could no longer draw the cities, so it ended the game.", the report's path, the hand-in line.
Closed with Close by the claim's owner after the game had ended (`--coordinator-override "MC-B: 0641 planned forced-end
test"`). Restore: backup by SHA-256 every file `equal`, `set-proxy --original`, `frontend.py restore`, `provision.py
verify c5h-d --quick` ok; released 13:23:39.

### 16.6 A second data point: landmarks on c5h-new-mpk (impl-0624's run of ticket 0645, 2026-11-21) [C, its logs]

`C:/c5-64bit-work/0624/mpk/ev/case2-stall/20261121-125700-41556-1` (read only; EXE 26577625, modpack form, pack=1):
`models: landmarks: the readiness or the tap frames were lost in play for 2233 ms, beyond render_models_hold_ms=2000`,
server exit 5 at server time 306.8 s (13:02:07). The same shape: the render thread's own gaps (render.log 13:02:04.792
-> 13:02:05.499, 707 ms between two lines of one identity-set publish; earlier slow bakes of 3.0-5.7 s whose time is in
texture reads, and slow render.log writes of 110-740 ms), F: at idle 0 %, queue 6-12, latency 326-543 ms 13:01:27-13:02:12
(MC-B #9266, `C:/c5-64bit-work/0624/mpk/ev/case2-diskwatch.txt`). On that pack=1 layout the popup showed the reason
(0511). The step-time rule covers it (landmarks is the writer's second bit; one bound for both).

**Why the render thread stalls** (MC #9266): F:'s latency (diskwatch: under 5 % idle, over 100 ms per request for 25.6 of
111 min, episodes up to 260 s, latency up to 1.1 s; `../06-tooling-and-workflows/d-drive-load.md` 13) reaches the
render thread through its synchronous file reads (the bakes' textures, ticket 0642) and its log writes. A player's slow
disk does the same, so the step-time rule stays wanted after 0642.

### 16.7 Not verified

- The units' hold live (ServerUnits was off in 16.5; the rule is the models' rule, covered by `test_units_mode`
  `stall_steps`) [I for live].
- A live stall on EXE build 26577625 (16.5 ran on 4390913; the change is in the server and the proxy, not build-keyed)
  [I].
- The modpack form and a stock (non-D33) session's `popup` verb with the record (the proxy test covers the stock popup
  path offline, `death-server-fatal`) [I for live].
- The quick exit (16.3) live: tested with the real server.exe and a stand-in EXE only (`render5`), not in a game [I for
  live]; what the screen shows in the seconds between a deliberate end and the popup was not pictured.
- The install bound and the open's bounds still count wall time (start-up bounds, 16.2); a start-up on a disk as slow as
  16.5's took 59.5 s for the layer's open, inside the open's own bounds, then 1.5 s to the bit [L].

## 17. Models missing at the wrap seam: the records tapped through a wrap view (ticket 0652, impl-0652, account B, 2026-11-21)

Branch `task-0652-seam-missing-models` (from main ba3d12897). Found by impl-0649 (`terrain-decals.md` 21.5): on
`HI-0580-LARGE-M43-SCENE` (Large Communitu, 87 columns, wrapping), EXE build 26577625, the installed 43-civ layout, the
camera at the wrap seam near plot (0, 22): the city model of Salem and several moai are absent in the product frame and
present in stock (Visual Checks `vc-20261121-162511-workstation-38ab` against `vc-20261121-162509-workstation-4ff5`,
not in git). Pictures read again for this ticket [L, 0649's frames]: what is missing is **every server-drawn model west
of a line just left of the camera's centre** (Salem's blocks, the moai and the improvement models on the west half of
both islands, the fishing-boat and pasture models), while the models east of it are drawn; the units and the decals
are drawn on both sides.

### 17.1 The cause [C code; L the server's own counter]

- **The EXE draws its wrap views' models in draws of their own.** On a wrapping map the EXE renders the world per view
  (`g_NumViews`; the trees' render setup writes one cb1 block per view, view 1's rows 0-3 = view 0's with the world
  shifted by the wrap period, `trees.md` 11.1 [C]). The City and Landmark systems' BuildingShader draws follow the
  same rule: a model the EXE sees through wrap view k is drawn with a VS cb1 whose rows 0-3 are view 0's
  view-projection moved by k x W in world x (row 3 += k W row 0; W = the camera block's `wrapPeriod`, 64 x columns - 1
  = 5567 here) [L by the counter below; I for the EXE's code path: no capture at a seam camera was read]. The tap
  (section 10.5) copies that constant with the draw, so the record carries view k's camera.
- **The server replaced it with view 0's.** Since ticket 4882 (11.12, 2026-11-19) `applyFrameCamera`
  (`render/src/landmarks/inputs.cpp`) writes the frame's **view 0** view-projection into rows 0-3 of every VS cb1 the
  target's records use (and view 0's OverlayProj into rows 8-11, and since 0598 view 0's eye into PS row 16). A record
  tapped through wrap view k was therefore drawn one world width away from where the EXE drew it: off screen. The
  compositor had dropped the EXE's own draw (the record is in the identity set), so the model was drawn by nobody.
  Before 4882 the records were drawn at the tap's own camera, which kept each view's rows (4875's live smokes had no
  wrap camera).
- **Live sign in the server's own counter** (`models:` 1 s line, `win_max_px` = the largest screen shift of an instance
  origin between the tap's rows and the rows written, section 11.12): 0649's product run (c5h-new-b, render.log of run
  `20261121-160807-12508-1`): `win_max_px=0.00` every second until the camera reached the seam, then from 16:12:48 to
  the end (16:14:42) **`win_max_px=36601.45 refl_win_max_px=7511.34` in every 1 s line with the camera still**, and the
  running `older_mean_px` rising from 1.4 to 5,343 px [L]. A still camera gives 0 for view-0 records (11.12); 36,601 px is
  an instance seen through the wrap view projected by view 0 instead [I for the exact figure: it depends on the
  instance's depth]. The reflection pass shows the same (7,511 px): its wrap-view records were lost too.
- **Scope.** Cities (blocks, single pieces, wonders), landmarks (improvements, resources, natural wonders) and the
  CPU-built meshes (walls, the Great Wall, bridges: 13.4) go through the same `applyFrameCamera`, scene and reflection
  pass alike: all affected, at any seam camera on any wrapping map. Not build-keyed: the code is the server's and the
  EXE's wrap views are the same on builds 4390913 and 26577625 (so the fault exists on 4390913 too, from ticket 4882's
  merge on 2026-11-19, i.e. in every build that drew cities or landmarks in the server) [C code; I live on 4390913 until
  17.4 runs]. The units (`srv_units_core.cpp`: three wrap copies of their own cull and draw, `wrapPeriod`), the trees
  (`srv_forest_scene.cpp`, per wrap copy), the terrain, water, ice and overlays (wrap copies of their own) do not use the
  tapped camera and are not affected [C].

### 17.2 The fix (`render/src/landmarks/inputs.cpp`, `inputs.h`; the server passes the period) [C]

- `wrapCopyOf(tap rows, frame rows, W)`: d = (tap row 3 - frame row 3) / frame row 0, taken at the largest member of the
  frame's row 0 (clip x for the scene and the reflection cameras alike), in world units; k = round(d / W). **k = -1, 0
  or +1 when |d - k W| <= W / 4, else `kWrapUnfit`**. The bound is loose against a pan step (a few world units per
  frame; 6 in the tests) and tight against the copies (a whole W apart), so the choice is never between two copies.
  Without a period (<= 0 or not finite) or a usable row 0: k = 0, the rule before 0652.
- `applyFrameCamera`, per VS cb1 constant (each once): rows 0-3 = the frame's view-projection **moved by k W** (rows
  0-2 as the frame's, row 3 + k W row 0; the server's `MoveX` rule); rows 8-11 = the **OverlayProj of copy k**
  (`FrameCamera::overlayProjWrap`, the server's `OverlayCopies` of the same frame, the EXE's own wrap view when matched;
  else view 0's moved by k W); row 12 (FOWAnim) the same for every copy. PS row 16 (0598): the tap's own row 16 while
  the tap's rows equal the frame's **copy k** (a still camera: the EXE's exact value), else the frame's eye **moved by
  -k W in x** (the eye of view k; the trees' row 12 of view 1 is view 0's eye - W, `trees.md` 11.1 [C]; that the
  BuildingShader's row 16 follows it is [I], and matters only while the camera moves).
- *(Corrected 2026-11-22 by ticket 0659, 17.9: such a constant is now drawn at the nearest copy with a plain note, and
  LOUD with nothing drawn only beyond two periods. The 0652 rule it replaced:)* **A constant that fits no copy** (a
  camera step of more than W / 4 between the tap and the frame: a jump by `LookAt`
  or a minimap click; which EXE view the record came from cannot be told then) is **not placed by guess**: its rows 0-3
  are written zero (clip w = 0 at every vertex: its records draw nothing this frame), counted
  (`CameraShift::unfitConsts`), and the layer names it LOUD (`models: N tapped constant(s) of the scene|reflection pass
  fit no wrap copy of the frame's camera ...`, the first time and then at most every 10 s, with totals). The tap is a
  Present old under lockstep (11.12), so this is one frame after each jump. Before 0652 such records were drawn at the
  frame's view 0 (the right place for view-0 records, the set a frame old). Only maps with a period take this rule.
- The first record drawn through a wrap copy is noted once per pass (`models: the scene pass draws N tapped
  constant(s) through a wrap copy ...`). `LayerDrawStats::wrapConsts` / `unfitConsts` carry the counts per draw (not yet
  printed in the 1 s line: `srv_models_scene.cpp` was outside this ticket's touches).
- The server (`srv_render_scene.cpp`, the models camera block): `FrameTextures::wrapPeriod` = the camera block's
  `wrapPeriod` (the units' value), `overlayProjWrap[0 / 1]` = `op.overlay.proj[0 / 2]` (copies -1 / +1) with the fog.
  The reflection pass copies `FrameTextures` and replaces only `viewProj` (the reflection camera), so its records get
  the same rule against the reflection camera (the reflection's wrap view = the reflection camera moved by k W: the
  mirror in z and the shift in x commute) [C].

### 17.3 Tests [C, 2026-11-21, workstation]

`render/tests/landmarks/test_inputs.cpp` (`render/build.py --test -R c5r_landmarks_test_inputs`, C5R_GAME_DIR the
`pure_vp` copy read only, build tree `C:/c5-64bit-work/0652/render-build`): **8 run, 0 failed checks, 0 skipped**
(16:46 EST; the mutation run 16:47). New cases:
- `wrap_copy_of`: copies 0, -1, +1; a pan step beside a copy; 0.24 W -> 0, 0.26 W -> unfit, 0.76 W -> +1, -0.5 W and 2 W
  -> unfit; no period (0, negative, NaN) -> 0; the reflection's orthographic form with small scales -> -1; a zero row 0 or
  a NaN translation -> 0.
- `frame_camera_wrap`: four constants tapped through view 0, copy -1, copy +1 and half a period off; (a) the frame one
  pan step away: rows 0-3 = the frame's copy k, rows 8-11 = the given OverlayProj of copy k (z / w kept as given), row 12
  equal for all, row 16 = the frame's eye moved by -k W, the unfit constant's rows 0-3 zero and the rest untouched,
  counts 3 / 2 / 1, and an instance at x = W - 40 seen through copy -1 shifted 4.18 px for a 6-unit pan; (b) without
  the EXE's copies: view 0's OverlayProj moved by k W; (c) a still camera: the EXE's row 16 kept bit for bit in all three
  fitted constants and the instance shift < 0.01 px (the live `win_max_px` check); (d) **the camera on the seam** (the
  EXE's camera wrapped from x W - 3 to x - 3 between the tap and the frame): tapped view 0 -> the frame's copy +1, tapped
  copy -1 -> the frame's view 0, the others unfit; (e) no period: everything at view 0, nothing unfit, and the copy -1
  instance shifted **3,884 px**: the fault, offline.
- Mutation: `wrapCopyOf` forced to 0 (the rule before 0652): `wrap_copy_of` and `frame_camera_wrap` fail (30 failed
  checks); restored.

### 17.4 Rule A: no library bundle holds a wrap-copy record [C, 2026-11-21 16:54 EST]

`scan_mtap.py` (scratch `C:/c5-64bit-work/0652/`, read only) over every `modeltap/frame*.mtap` member of
`F:/c5-64bit-work/scene-library/bundles/` (`shell_model_tap.h` layout: per target the VS cb1 constants its draws use,
each constant's row 3 against the target's most-used constant along row 0's largest member, in world units): 7 bundles
carry the member (FOG_0008, HUGE_0350, four SCNB_0009, TINY_0009; the nine of 4980 have none), 56 frames, 32 / 24
constants per bundle, **max |d| = 0.00 world units in every bundle**: every record of every recorded frame was tapped
through view 0, none through a wrap view. With the tap's rows within a pan step of the frame's view 0, `wrapCopyOf`
gives 0 and the rows written are the same as before 0652, so no recorded picture changes by construction [C]; what is
left open until the replay (17.8) is a recorded camera jump of more than a quarter period between a tap and its frame
(then the records draw nothing that frame instead of drawing at view 0).

### 17.5 Live on build 4390913: the fault and the fix (c5h-c, 2026-11-21 17:52-18:24 EST) [L]

Instance c5h-c (C:, Goldberg, mods form, EXE build 4390913 stamp 0x546CD0A8, Vibrant Textures), MC-B grant #9611. Save:
copy `C5H-0652-L43` of `HI-0580-LARGE-M43-SCENE` (`96b54368...`). Driver: scratch `C:/c5-64bit-work/0652/live.sh`
(0649's newb.sh method). Stock: the instance DLL set to the 43-civ instance-aware `i0ship-f33fc46a-m43-ia`
(`ba520010...`), put back to `e589306e...` after. Product: the 0.1b rehearsal 43-civ TEST installer
(`VP64-Patch-0.1b-rehearsal-for-VP5.4.8-TEST.exe`, `42497629...`; "install 0.1b-rehearsal (m43, i3, mods form)", it
accepts build 4390913), player DLL `01451bd7...`; in both product arms the installed composite (`db4bcd44...`, without
0649's decal fix, which this save needs) replaced by one built from this branch (main with 0649, `a1b6639d...`) and
`shell_composite.sha256`'s composite line pointed at it; arm **before** = the installed `VP64/server.exe` (`f946bd10...`,
repo f3a66933e, pre-0652), arm **after** = this branch's release server.exe (`924bca7f...`, built by
`server/build_server.py` from the pins-0.1b source and deps; the m43 x64 DLL is the installed one: the server takes no
major-civ define, G1). Every replaced file backed up and put back by hash; removal exit 0, the DLC tree identical
(8,623 rows), My Games identical but for the run outputs and caches (the removal also took the older `Logs/VP64/run`
folders of 2026-11-20); `provision.py verify --quick` ok. Cameras: `UI.LookAt` on plot (0, 22) (the seam seen from
the west: the islands of Salem and Tonga), again after 30 s, on (86, 22) (the seam from the east), on (42, 33)
(Nineveh, far from the seam), and back on (0, 22).
**Camera note for drivers [L]:** on this save `UI.ClearSelectionList()` or putting the units to sleep starts the
next-unit cycle, which moved the camera back to the army within seconds, so the shots repeated one view; with
`cli.py pretend-active on` and `UI.LookAt(plot)` alone (a unit left selected) every camera held.

| | before (pre-0652 server) | after (this branch) |
|---|---|---|
| seam from the west (0, 22) | Salem's city and the moai west of the seam absent; the rest drawn (`vc-20261121-182550-workstation-cb12` against stock `...-182548-...-0142`) | Salem and every moai drawn as stock (`...-182551-...-eb71`) |
| seam from the east (86, 22) | Salem drawn (now view 0), the moai and Tonga's city east of the seam absent (`...-182553-...-aa72` against `...-182551-...-fb80`) | all drawn as stock (`...-182553-...-b655`) |
| far (42, 33) | as stock (`...-182555-...-1695` against `...-182554-...-4db9`) | as stock (`...-182556-...-eb30`; it came back without the interface: a capture artifact) |
| after 30 s / after a move away and back | the same models absent (not a loading matter) | the same models drawn |
| `models:` 1 s line, still camera at the seam | `win_max_px` 36,588-39,206, `refl_win_max_px` 7,511-7,572 in every line from 18:16:22 (0.00 at the far camera) | **0.00 / 0.00** in every still second; 1,557-2,604 px only in the one or two seconds of each camera move |
| notes / LOUD | none of 0652 | `models: the scene pass draws 1 tapped constant(s) through a wrap copy ...` and the same for the reflection pass, once each; **0 `fit no wrap copy` lines**; the only LOUD lines are two unit pose programs (swordsman, warrior), as in 0649's run |

So the fault exists on build 4390913 exactly as on 26577625 (it is the server's, not the build's), it is
placement and not loading (still absent after 30 s and after moving away and back), the reflection pass had it too,
and the fix draws the models from both sides of the seam. (The far views' models are the same in all three frames.)

### 17.6 The acceptance pair on build 26577625 (c5h-new-b, 2026-11-21 18:25-18:38 EST) [L]

Instance c5h-new-b (C:, Goldberg, mods form, Steam build 26577625, Vibrant Textures), MC-B's (C) window. The same save
copy, driver and installer as 17.5 (the installer laid m43 / i3 / mods form, exit 0; installed `VP64/server.exe`
`f946bd10...`, composite `db4bcd44...`, manifest `747dfb01...`, all three backed up and put back by hash); the after arm
only (the before state is 0649's run of 21.5 in `terrain-decals.md`, the same layout and server): server.exe
`924bca7f...` and composite `a1b6639d...` swapped in by hash. Stock: the 43-civ instance-aware DLL `ba520010...`, put
back to `ff0f6e86...`. Removal exit 0; the DLC tree identical (8,623 rows); My Games different only in caches and logs;
`provision.py verify --quick` ok; c5h-new-b released 18:37.

- Pictures (Visual Checks, issue 0652, not in git; all frames with the interface, no popup open): **ACCEPTANCE**
  `vc-20261121-183830-workstation-4817` (the seam from the west, plot (0, 22): Salem's city and the moai drawn as stock;
  crop pair attached) against stock `...-183828-...-41bc`; `...-183835-...-5a48` (from the east, (86, 22): the moai
  and Tonga across the seam drawn) against `...-183833-...-8591`; `...-183837-...-7ee2` (Nineveh, far from the seam,
  unchanged) against `...-183836-...-9fa8`. Differences seen by eye: the farm field styles (random per load, decals 2.4)
  and the clouds' drift; no model missing or extra at the seam [L].
- The models line: `win_max_px=0.00 refl_win_max_px=0.00` in every still second (18:34:43-18:36:23), 1,994-2,610 px only
  in the seconds of a camera move; the two wrap-copy notes once each; 0 `fit no wrap copy` lines; no LOUD line but the
  two unit pose programs [L].
- D112: the full shipped system as the player runs it except the two files swapped by hash (the server of this fix and
  the composite of 0649's fix, which the save needs on this build), named in each picture's text.

### 17.7 Not verified (0652)

- The EXE's code path that draws a wrap view's models (17.1) is inferred from the server's counter and the pictures,
  not read in a decompile or a capture at a seam camera [I]; the eye of a wrap-view record (row 16 moved by -k W) is
  inferred from the trees' per-view block and matters only while the camera moves [I].
- A camera jump of more than a quarter wrap period between the tap and the frame: those records draw nothing for one
  frame (by design, 17.2); not seen live (0 lines in both after arms: `UI.LookAt` pans over several frames) [C tests].
  *(2026-11-22: seen live by 0662 (18.1) and 0659 (17.9); since 0659 drawn at the nearest copy, a plain note.)*
- The modpack form, other wrapping maps (any map size: the rule has no size in it) and four-view cameras (the EXE has up
  to four views; only copies -1 and +1 are taken) [I].

### 17.8 The shared sources' builders and suites, and rule A [C, 2026-11-21, workstation]

`c5render` (`landmarks/inputs.cpp`) and `srv_render_scene.cpp` are compiled by several builders (the merge check "all
builders of a shared source"). Scratch driver `C:/c5-64bit-work/0652/builders.sh` (each step through `quietlock.py run`,
C5R_EXE the 4390913 EXE, C5R_GAME_DIR the `pure_vp` copy, both read only).
- **Head 66cce000c** (main of 17:45 merged; 18:38-19:35 EST): `render/build.py --test` (all of c5render's ctest) **106 of
  106 passed**, 0 skipped; `build_models_tests.py` + `test_models.py` **28 passed**; `build_units_tests.py` +
  `test_units_runner.py` **7 passed**; `build_unitfeed.py`, `test_farmfields_build.py`, `build_scenebundle_tests.py`,
  `build_scene_shadow.py`, `build_units_extras_tests.py` each built and linked OK; `server/build_server.py --config
  release` (pins-0.1b source and deps) and `shell/build.cmd` OK; `run_all.py` with render-host (164 passed), fow-scene
  (3), overlay-scene (3), shadow-ice-scene (6), water-scene (29), forest-scene (3), scenebundle-programs (4: recorder,
  bundle format, sceneshot and sources programs) and scenebundle-py (22): **8 suites passed, 0 failed, 0 skipped**.
- **Head c65e13eef** (main with a792a7d68 merged, which brought `srv_farmfields.*` and render units changes; 19:35-19:56
  EST): c5render ctest **108 of 108 passed**, 0 skipped; `test_models.py` 28 passed; `test_units_runner.py` 7 passed;
  unitfeed, farmfields, scenebundle, scene-shadow and units-extras builders linked OK; `server/build_server.py --config
  release` OK (server.exe `1443eb1b...`). Rule A on head 4d068aa26 (= c65e13eef + notes; MC-B's rule A slot, it also
  serves as ticket 0653's owed library replay): `run_all.py --only offline-equals-live` with `C5_P1_TOLERANCE=0:0` and
  `C5_SCENE_ASSET_ROOT` = the frozen root `C:/c5-64bit-work/scene-asset-root/mods548-vt`, the master library
  (`F:/c5-64bit-work/scene-library/bundles`, 16 bundles): **17 passed, 0 failed, 0 skipped, every frame byte-equal**
  (20:46-21:04 EST). So no recorded picture changes, as 17.4 predicted; nothing re-recorded. A first run (20:27-20:45)
  without the asset root failed 6 bundles with the player's exit 9 (their install lies in pool instance c5h-a, which was
  claimed): a set-up error, not a picture result. Note for drivers: every offline-equals-live run sets
  `C5_SCENE_ASSET_ROOT` (ticket 0614). The run_all suites were not run again on this head (they passed on 66cce000c;
  the merge changed no file of this ticket).

### 17.9 Far camera jumps: drawn at the nearest copy, a plain note (ticket 0659, impl-0659, account B, 2026-11-22)

Branch `task-0659-far-jump-loud-note` (from main 0e5b19d42). 0652's rule (17.2) left a tapped VS cb1 constant more than
W / 4 from every wrap copy of the frame's camera undrawn for that frame and logged LOUD. 0662 saw it live (18.1: one
LOUD line per far jump, one frame each). A far jump is a player's action, not a fault.

**Why the choice cannot change the picture much, by construction [C].** The tap is one Present old (11.12), so the
records of a jump's first frame are the old view's models. Every wrap copy is a true image of the wrapped world, so
a record drawn at any copy is in the right place: it shows only where the old and the new view overlap. With a jump
of more than W / 4, that overlap exists only if the view spans more than W / 4 of the map. The new view's own models
are missing from that frame under any rule, because the tap does not hold them yet. This lag is the same one-frame
lag as at the edge of any pan.

**The change [C].**
- `nearestWrapCopyOf` (`render/src/landmarks/inputs.cpp`, `inputs.h`): k = round(d / W), from -2 to +2, with d the
  camera step of 17.2. It gives `kWrapUnfit` when |k| > 2 (no camera position on this map is that far from the tap:
  a fault) and 0 without a period. `wrapCopyOf` is unchanged.
- `applyFrameCamera`: a constant that `wrapCopyOf` finds unfit takes the nearest copy (`CameraShift::farConsts`).
  Rows 0-3 are moved by k W, rows 8-11 are the given OverlayProj of copy ±1 (otherwise view 0's moved by k W), and row
  16 is the eye moved by -k W. The constant counts its instances (`farInstances`) and the instance origins inside the
  frame at that copy (`farOnScreen`) and at view 0 (`farOnScreenView0`, the rule before 0652). A far constant's records
  are left out of the pan shift (`win_max_px`). Only `kWrapUnfit` (|k| > 2) still writes rows 0-3 zero and counts
  `unfitConsts`.
- `srv_model_layer_c5r.cpp`: a far jump writes a plain note, at most every 10 s per pass: `models: camera jump of more
  than a quarter wrap period (wrap period W): the scene|reflection pass draws N tapped constant(s) at the nearest copy,
  X of their Y instance origins in the frame (at view 0: Z) (totals: ...)`. LOUD is kept for the fault only: `LOUD:
  models: N tapped constant(s) ... lie more than two wrap periods from the frame's camera ... no camera of this map,
  their records are not drawn this frame`.

**Tests [C, 2026-11-22].** `c5r_landmarks_test_inputs`: 10 run, 0 failed checks, 0 skipped. New cases:
- `nearest_wrap_copy_of`: 0.26 W gives 0, 0.74 W gives +1, -0.6 W gives -1, 1.6 W gives +2, -2.4 W gives -2,
  2.6 W and -3 W give unfit; no period and a zero row 0 give 0.
- `frame_camera_wrap` (a) and (d): the half-period constant is drawn at copy 0 / +1 and the two-period one at +2,
  instead of unfit.
- `frame_camera_wrap` (f): the frame 2.9 W from the tap. Two constants are unfit (rows zero, row 16 as tapped) and two
  are far at copy -2.
- `frame_camera_far_jump`: a wide view and a jump of 0.3 W. The copy -1 record lands in the new frame
  (`farOnScreen` 1), and at view 0 it would not (0).

At the tests' own view (half-width about 717 world units, W / 4 = 1,392), a far jump always moves the old models off
screen.

**Live on build 4390913 (c5h-a, C:, Goldberg, mods form, Vibrant Textures; MC-B GO #10158, 2026-11-22 09:17-09:33)
[L], DIAGNOSTIC.**
- Save: 4985's Tiny kit copy `C5H-4985-SMKK_0009` (`74fc7428...`, 56 columns, W = 3,583, W / 4 = 896).
- Product layout: this branch at 76f18b115 on the 0.1b pins (server.exe `8e67e742...`, composite `b439ed3b...`), laid
  by `lay.py prep` + `helper on`, not the installer. Restored by hash (`restore ok`, saves put back, `provision.py
  verify --quick` 0).
- Option set: the first beta's (`ServerCities = 1 (from the file)`, landmarks, farm fields and units 1).
- Stock: the instance's own VP DLL, with the save put in and taken out by hash.
- Driver: `server/evidence/0659/live_0659.sh` and `live_0659.py`. Output and logs are in `C:/c5-64bit-work/0659/`, not
  in git.
- Plan, from camera A (45,13) at zoom min-2 with the interface shown, 12 s apart:
  - four minimap clicks (0.03, 0.97, 0.5 and 0.03 of the panel's width; camera x 1152 -> 244 -> -973 -> 1427 -> 244:
    jumps of 908-2,400 world units);
  - four notification jumps: `Player:AddNotification(NOTIFICATION_GENERIC, ..., x, y)` then
    `UI.ActivateNotification(id)`, which is the EXE's own activation, as when a notification is clicked (camera x 244
    -> -1665 -> 1345 -> 1152 -> -1665).
- After each action four captures: back-buffer dumps in the product arm, raw PrintWindow in stock, the first 0.09-0.84 s
  after the action.

| | product (this branch) | stock |
|---|---|---|
| far-jump notes (render.log of run `20261122-092105-34520-1`) | **4, one per minimap click** (09:22:50, 09:23:03, :15, :27): scene pass 3 / 1 / 1 / 4 constants, reflection 1 / 0 / 0 / 1; **4 frames in all, one per click**; **0 of 1,134 instance origins in the new frame at the nearest copy, 0 at view 0** | - |
| notification jumps | **no note**: the activation pans over several frames, like `UI.LookAt` in 17.5 (no step of W / 4 between two frames) | - |
| LOUD lines from 0652 / 0659 | **0** | - |
| dumps after each action | city and landmark models drawn wherever the land is revealed, from the first dump on | the same |

**Answers.**
1. **Does the line appear on a plain jump?** Yes, on every minimap click to the far side: 0662 saw 5 LOUD lines from 4
   far jumps on 26577625 (18.1), and this run saw 4 notes from 4 clicks on 4390913. A notification jump on this save
   pans and gives none. 0662's LookAt Tahiti -> A did step (18.1); that LookAt pans in some cases and steps in others is
   [I].
2. **How long are the tapped models absent?** One frame per jump (4 frames for 4 jumps; 33 ms at 30 fps). That frame
   lacks the new view's models under any rule (the tap lag above).
3. **Nearest copy or nothing, with numbers.** The old view's records landed in the new frame in 0 of 1,134 origins at
   the nearest copy and 0 at view 0. On this save, drawing at the nearest copy and drawing nothing give the same
   picture. The nearest copy is kept because it is never wrong (a true image) and it is better wherever the views
   overlap: a zoomed-out view wider than W / 4, as in the far-jump test. The cost is one frame of draws that are mostly
   clipped.
4. **Visible in a capture against stock?** No dump caught the jump's own frame. The dump trigger is polled, and the
   first dump came 0.42-0.84 s after a click. From that dump on, the models were drawn as in stock.

**Not the models: a cloud lag after a minimap jump [L].** After click 2 (onto revealed land around (45,13)), the
product's first dump at 0.52 s shows that land under the clouds, with only the units drawn over them
(`vc-20261122-092319-workstation-803b`, frame 0). Stock's first PrintWindow at 0.44 s shows the same land clear
(`vc-20261122-093103-workstation-19c9`). The product's next dump (1.36 s) is clear. This is 0662's "fog or clouds of
the old view over the new one" (18.1), now compared against stock: it is the product's own. It belongs to the fog
layer, not this ticket; it is a follow-up for the MC.
*(Corrected 2026-11-23 by ticket 0668, `fog-after-camera-jump.md` 7: not a lag and not the old view's clouds; it is the
far jump's frame with an empty fog mask (0663 section 2, ticket 0666), fixed by 0674's camera window; live on main
81baa8966 no such frame after the far jumps, the positive control with the old window shows it.)*

**Rule A [C].** No library bundle holds a record tapped more than a pan step from view 0 (17.4: max |d| = 0.00 in
every bundle). So `wrapCopyOf` gives 0 there, no constant is far or unfit, and the rows written are the same as before
this change. No recorded picture can change, and offline-equals-live was not run.

**Builders of the changed sources (head 76f18b115, workstation, 2026-11-22 08:45-09:50) [C].**
`server/evidence/0659/builders_0659.sh`, every step through `quietlock.py run`:
- render ctest whole: **108 of 108 passed**, 0 skipped.
- `build_models_tests.py` + `test_models.py`: **28 passed**.
- `build_units_tests.py` + `test_units_runner.py`: **7 passed**.
- `build_unitfeed.py`, `test_farmfields_build.py`, `build_units_extras_tests.py` and `build_trigger_scene.py`: linked OK.
- `run_effect_routes_test.py`: 275 passed.
- `server/build_server.py --config release` (0.1b pins) and `shell/build.cmd`: OK (`smoke_4985.sh build`).
- `run_all.py` (14 suites, 09:00-09:43): 11 passed: render-host 169, cut-tap 1, decal-tap 13, ice-reader 11,
  overlay-reader 36, shadow-reader 40, shadow-ice-scene 6, water-scene 29, forest-scene 3, scenebundle-programs 4,
  scenebundle-py 22, each with 0 failed and 0 skipped.
- `offline-equals-live` (selected by default) failed 9 of 17 on a set-up error: sceneshot exit 9, because the bundles'
  install lies in the claimed pool instance c5h-c, and the run set no `C5_SCENE_ASSET_ROOT`. 0652's first run (17.8)
  hit the same error. It is not a picture result. It was not run again, because rule A needs no replay here (above);
  the run read the F: library outside a rule A slot (read only).
- Two suites fail for a cause outside this change:
  - `fow-scene` and `overlay-scene` each fail 2 cases on the LOUD line `extras: an [Extras] option is in effect but
    this program does not link server/src/extras/`.
  - Since one-off 9081 (ea8878286) `[Extras] FixAirBases` defaults to 1, and these two test programs do not link the
    extras.
  - This is a break on main, not from this branch [C, the line's condition in `Scene::configureExtras`].

**Not verified.**
- The acceptance run on build 26577625 with the installed layout (the MC's note #10134: c5h-a/b/c are 4390913, so this
  run is diagnostic).
- The jump's own frame in a picture.
- A save or zoom where the old and new views overlap across a far jump (`farOnScreen` > 0 live).

## 18. 4985's "city missing at the wrap seam" shots are harness capture artefacts, not presented frames (ticket 0662, impl-0662, account B, 2026-11-22)

**Question (MC-B #10032, ticket 0662).** 4985's FixAirBases live smoke (c5h-new-a, EXE build 26577625, the product
layout of main 328fd71f3, which carries 0652's fix; `extras-units.md`, 4985 live smoke results, "Seen, not this
extra's") had, in several shots of every arm, a city's model missing at Tahiti (1,13) or Tonga (48,17) on the Tiny map,
near the wrap seam, while other shots of the same plot drew it, and one shot with a grey checkerboard at its right edge.
0 LOUD lines, so 0652's no-fit line (17.2) did not fire. Is it a fault a player sees, and is it 0652's path?

**Answer: no and no.** Offline reading only (2026-11-22 01:37-01:50 EST: no build, no game), of 4985's pictures
(`C:/c5-64bit-work/4985/out1/`, 44 shots), shot records (`pair_*.json`, `shots_*.json`), render.log / server.log of the
three runs (`C:/c5-64bit-work/4985/smoke1/pp/arm-*/runs-Logs-VP64/run/`) and the composite's own logs of the three
runs (`<c5h-new-a user>/Logs/shell_composite.20261122-010811.log` (on), `-012054.log` (off), `shell_composite.log`
(reload); copied read only to `C:/c5-64bit-work/0662/complogs/`):

- **0652's path was not running [C, the run's server.log].** Every run logged `options: [Renderer] ServerCities = 0
  (default)` and `ServerLandmarks = 0 (default)` (`options/src/vp64_options.cpp`: both default 0). With both 0 the
  proxy writes no `render_cities` / `render_landmarks` line (`proxy/src/proxy_shell.cpp` `ModelLines`), the server
  draws no city or landmark model, render.log has no `models:` line at all, and **the cities are the EXE's own draws**,
  composited over the server's world. `wrapCopyOf`, the tap rows and the no-fit rule (17.2) run only with
  `ServerCities = 1` or `ServerLandmarks = 1`. **The first beta ships those at 1** (the `i3` option set,
  `docs/RELEASE-0.1b.md` 1; MC #10040: ServerCities, ServerLandmarks, ServerFarmFields and ServerUnits 1), so the smoke's
  configuration was not the beta's: its shots say nothing about the server's model path, which step 2 (18.1) runs.
- **Not the seam, not one city: every EXE model of the frame is gone [L, the pictures].** In each "missing" shot all of
  the EXE's models are absent at once (both cities in view, farms, mines, the fishing boats, the resource models),
  while everything the server draws is there (terrain, trees, units, borders, the fog). The same happens at camera A
  (45,13), far from the seam (`on_turns_a_src.png`, vc-20261122-010438, and `reload_load_a_src.png`, -012406). Tahiti and
  Tonga were simply three of the four cameras.
- **The camera was still [C, `server/evidence/0455/4985/arm.py`].** The driver sets the camera, waits 3 s, then
  shoots (pairs: before 1, 3 s, before 2, the Lua kill, 4 s, after 1, 3 s, after 2). No shot follows a camera jump
  within its frame; the "far camera jump" of -010743 (pair c1 after 1) is 13 s of still camera after the carrier
  camera was set. Not 0659's form.
- **Every presented frame of those seconds had the EXE's models and the server's world in place [L, the composite's
  1 s lines].** `server/evidence/0662/shotcheck.py` sums, for each of the 44 shots, the composite's `stats`, `lockstep`
  and `modelfilter 1s` lines over the 12 s before its Visual Checks stamp (`shotcheck.out` beside the script's run):
  in every window `nodraw + missed_ins + ins_wrong = 0` (the world inserted after the back-buffer clear and before the
  3D pass, every frame), `cam=OK`, lockstep `gray_frames = 0`, 364-400 frames presented. The compositor's model
  filter counts the EXE's model draws it sees per frame: **at a still camera `seen_single` is an exact multiple of the
  frames presented** (5.0, 6.0 or 4.0 per frame by camera) in every second, also in the seconds of the shots that show
  no models (on pair b after 1 and 2 = vc-010550 / -010559: 5.0 in all 13 s; off pair c2 after 1 = -011920: 4.0; on pair
  c1 after 1 = -010743: 6.0). A frame without the EXE's model pass would break the multiple. The mixed ratios are only
  in windows that include the camera's move to the view.
- **The grey is the D33 clear colour on top of everything [L, pixels; C, render.log].** The checkerboard of -010743,
  -010559, -011809/-011821, -011920 and -012406 is exactly 84,84,84 (159,954 of the crop's pixels in -010559) and lies
  **over units, borders and trees**: a back buffer caught part-way, not terrain. Unbaked tiles would lie under them,
  and there were none: `terrain 1s` gives `resident=1352` of `land=1352`, `pending=0`, `gray_frames=0`,
  `gray_tile_frames=0` in every second of the three runs after the first seconds of the load
  (`BakedTileCameraRadius = -1`, the whole map baked; 819 + 605 + 190 lines read).
- **The harness's own record flags the regime [C, `pair_*.json`].** `cli.py shot` retakes a flat 84,84,84 capture up
  to 4 times (ticket 0401): **30 of the 44 shots needed retakes** (`flat_retakes` 1-4; e.g. on pair b: 1, 0, 3, 4), so
  PrintWindow returned the back buffer's intermediate states all through the smoke. With the interface hidden by Lua
  (4985's shots), a capture taken after the world insertion and before the EXE's models looks like a normal frame
  without models, and the top-band test of `run-and-drive-the-game.md` cannot tell it.

**Mechanism [I, as in `terrain-surface-verification.md` 13].** `PrintWindow(PW_RENDERFULLCONTENT)` on the game window
reads the swap chain's buffer while the frame is being drawn. On the product path the frame's order is: clear to
84,84,84 (D33), the composite inserts the server's world, the EXE draws its models, then its interface, then Present.
A capture can land after any step: flat grey, a part-copied world (the checkerboard: the insertion caught mid-draw),
the world without the EXE's models, the frame without the interface. 0401 measured the first two kinds against the
composite's back-buffer dump (taken inside Present): 8 + 2 of 30 PrintWindow captures, 0 of 30 dumps. The stock route
shows the UI-less and model-less kind too (`../02-exe-internals/community-coastal-city-tiny.md` 2).

**Side observation [C, the shot records].** The EXE camera's `target` read by 4985's driver sits one wrap period
(3,583 world units on the Tiny map, 56 columns) from the camera `position` in some reads (Tonga: target x 4926.9 for
position 1343.9; Tahiti: -5247.0 for -1664.0) and not in others at the same view; the view matrices follow the
position. Drivers that compare cameras should compare the position or the view rows, not the target.

**What follows.**
- From these shots: no player-visible fault in the configuration they ran (the EXE drawing the cities). The beta's
  configuration (the server drawing them) is step 2's question (18.1).
- For drivers: a picture that must show whether a model is drawn comes from the composite's back-buffer dump
  (`tools/hi/capture.py` `grab(..., route="product")`, trigger `shell_composite.shot`), not from `cli.py shot`, above
  all with the interface hidden. 4985's kit (`server/evidence/0455/4985/arm.py` through the scenario runner's
  `Session.shot`) uses PrintWindow.
- Not verified here: the live repeat with back-buffer dumps at the same cameras, with plain camera moves and minimap
  jumps (ticket 0662 step 2; 18.1 when run).

### 18.1 Live: what the player is shown at the seam, the first beta's option set and the smoke's (c5h-new-a, 2026-11-22 04:25-04:41 EST) [L]

> **Correction (MC at 0663's merge, 2026-11-22 08:50):** the purple blocks and thin clouds in the first dump after a
> far jump, described below, are explained in [fog-after-camera-jump.md](fog-after-camera-jump.md) (ticket 0663 [C]):
> that frame shows NO fog at all (its fog mask is empty), not the old view's fog; the purple is open-sea tiles in the
> terrain's material fallback colour. Fixes: tickets 0665 (F1) and 0666 (F2).

Instance c5h-new-a (C:, Goldberg, mods form, Steam build 26577625, Vibrant Textures), MC-B's GO of 04:23 (grant #10043).
Layout: 4985's build of main 328fd71f3 on the 0.1b pins (server.exe `9286e3e5...`, composite `f6c73a9f...`; main
9bbad438a differs only in the road extras, default off), laid by `lay.py prep` + `helper on` (not the installer), backed
up and restored by hash (`restore ok`, saves put back, `provision.py verify --quick` 0, released 04:41). Save: the 4985
kit copy `C5H-4985-SMKK_0009` (`74fc7428...`, Tiny, 56 columns, wrap period 3,583). Driver:
`server/evidence/0662/live_0662.sh` (prep and end through `smoke_4985.sh`) and `live_0662.py`; output
`C:/c5-64bit-work/0662/out1/{beta,exe}/` (`pairs_*.json`, `jumps_*.json`, half-size PNGs; not in git), logs
`C:/c5-64bit-work/0662/live1/pp/arm-*/`.

Arms: **beta** = `ServerUnits`, `ServerCities`, `ServerLandmarks`, `ServerFarmFields` 1 (the first beta's set, MC
#10040; server.log `ServerCities = 1 (from the file)`); **exe** = the smoke's (`ServerCities`, `ServerLandmarks`,
`ServerFarmFields` 0: the EXE draws cities and landmarks). Per arm: at the smoke's four cameras (Tonga (48,17),
Tahiti (1,13), A (45,13), the carrier (1,11); zoom min-2; interface hidden by Lua; popups closed; 3 s still) six pairs
of one raw PrintWindow capture and the composite's back-buffer dump right after it (`tools/hi/capture.py`); then,
interface shown, eight camera actions from A: `UI.LookAt` to Tonga, Tahiti, A, minimap clicks at 0.03, 0.5, 0.97, 0.5 of
the panel's width (the fallback rectangle 2225,1217,320,190; they landed at world x 244, 1427, -973, 1427: jumps of
900-1,200 world units, a quarter period or more), LookAt to Tonga, each followed by six dumps back to back (the first
0.05-0.6 s after the action, then about every 0.5 s). Visual Checks (issue 0662, DIAGNOSTIC, one contact sheet per
view or action; row 1 PrintWindow, row 2 dumps): beta views vc-20261122-042902-...-52b1 (Tonga), -042924-...-8f66
(Tahiti), -042944-...-741d (A), -043007-...-a90c (carrier); beta actions -043102 ... -043208 (eight); exe views
-043645-...-d285, -043708-...-df90, -043737-...-64e8, -043759-...-ffc3; exe actions -043853 ... -043957.

| | beta (the server draws cities and landmarks) | exe (the smoke's set) |
|---|---|---|
| dumps at the four still cameras | **24 of 24 with every city and landmark model**, seam views included | 24 of 24 with every model |
| raw PrintWindow at the same moments | 5 of 24 flat 84,84,84; the other 19 equal their dump (mean abs difference 0.1-2.0 of 255); none model-less | 0 flat, 24 equal their dump (0.3-1.3); none model-less this time |
| dumps after the eight camera actions | models drawn in every dump at every destination (Tonga and Tahiti at the seam, A, the minimap destinations) | the same |
| server's `models:` line | `drawn` = `accepted` every second; `win_max_px = 0.00` / `refl_win_max_px = 0.00` in every still second at every view; 3-732 px only in the seconds of a camera move | (no server model layer) |
| 0652's no-fit rule | **5 LOUD `fit no wrap copy` lines**, one per far jump (LookAt Tahiti -> A at 04:31:20, the minimap jumps at 04:31:31, :40, :51): totals scene 9 constants in **4 frames**, reflection 2 constants in 2 frames: one frame per far jump without the tapped models | - |
| LOUD lines, boxes, crashes | only those 5; no box; crash collect clean | none |

**Answer for the player [L].** At still cameras and during plain moves at the seam, **a player does not see a city
vanish**, in the beta's configuration or the smoke's: every presented frame read had them. With the beta's set the one
exception is 0659's case, now seen live: after a far jump (a minimap click, a jump by LookAt across a quarter of the
map) the tapped city and landmark records of **one frame** (33 ms at 30 fps) fit no wrap copy and are not drawn, and the
layer logs a LOUD line for what is no fault. That frame is the first frame of the new view, so the player sees the new
place without its city models for one frame. No dump caught it (the first dump came 0.05-0.6 s after the action); its
count is from the server's totals. Known issue A4.7 of the first beta's list; 0659 (p5) is the ticket. *(2026-11-22,
0659, 17.9: such constants are now drawn at the nearest copy with a plain note; the LOUD line is gone for jumps.)*

**PrintWindow in the beta's set [L].** The server draws the cities into the world image the composite inserts, so a
mid-frame capture cannot drop them; the flat captures remain (5 of 24 here).

**New finding, not the seam's [L, exe arm only, cause not examined].** After two of the four minimap jumps into
unrevealed land in the exe arm (actions 3 and 5, vc-20261122-043920-...-9e2c and -043939-...-0045, marked `fail` with
a note), the **first dump** (0.59 s / 0.42 s after the click) shows opaque purple (150,4,222) blocks with screen-aligned
edges over the fog, and the clouds over the unrevealed land thin enough to show its rivers and relief; the next dump
(1.2-1.5 s) shows the normal clouds. The composite's counters of those seconds are clean (nodraw, missed insertions,
wrong insertions 0, `cam=OK`, `gray_frames` 0); the server's terrain line counts 31 uncached visible tiles there (the
unrevealed ones, normally under the clouds) and its fog line a new hex mix (6 / 33 / 195). In both arms the first dump
after a far jump also shows the fog or the clouds of the old view over the new one for that moment (beta action 1,
exe action 4). Not seen in the beta arm's same jumps; the colour is in none of this repository's sources; stock not
compared. A follow-up for the MC: stock against product with dumps right after minimap jumps into unrevealed land.

**Not verified:** the jump's one frame in a picture (the counters only); the installed layout (lay.py's); the 43-civ
build; stock at the same jumps.

## 19. Unconfirmed report: coastal cities that looked nearly empty (lead-0673, 2026-11-23) [I]

The reporter (D237 playtest) saw, in an earlier session, coastal cities drawn with only a few buildings near the water
and the rest of the city empty. Not reproduced: the reporter's beta 3 save did not show it (2026-11-23 21:3x EST), so
it may come from an older build; the build is not known. Not the 0709 keep (pan-coherence 5.14: it only adds instances,
never drops one). No ticket: the reporter files a new bug if beta 4 shows it. A future report should name the save,
the build and the city.

## 20. The garrisoned unit's flag ring: the same as stock, no offset at 2554 x 1411 (ticket 0679, lead-0673, 2026-11-24) [L]

The report (D237 playtest, 2026-11-22): in cities the garrisoned unit's flag ring is drawn a few pixels up and left of
its icon disc; flags outside cities are fine. Unit flags are the EXE's and EUI's interface, never drawn by the server.

Stock comparison, 2026-11-24 07:15-07:25 EST, c5h-new-a (EXE build 26577625, mods cp,vp,eui, client 2554 x 1411, the
instance's default UI scale), save copy `C5H-0679-HUGE_0350` (huge, turn 350), hidden, frame-capped, one client at a
time. STOCK: the instance's own DLL, nothing laid. PRODUCT: lay.py with server.exe `eec53dde4f953778...` (0709's
aae608ee7) and shell_composite.dll `a424edcf633fdd4d...`, the reporter's [Renderer] set (`setopts.py` as d237.sh).
`server/evidence/0673/garrison_look.lua` centres the k-th visible garrisoned city (the active player's first);
`server/evidence/0673/g0679.sh` runs both arms (runbook 4.21; corrected 2026-11-24 from "4.20"). Shots stay local (`C:/c5-64bit-work/0673/g0679/shots`).

- Thebes (101,25, machine gun) and Heliopolis (100,35, machine gun): the flag sits at the same place in both arms. Inside
  a radius of 18.5 px around the disc centre (1085 pixels) stock and product differ by at most 10 of 255 levels
  (the ring's blend over a background the server draws differently).
- Geometry, Thebes, both arms the same: the disc fill spans x 1214-1244, y 530-560 (centre 1229, 545); the ring's light
  band reaches x 1210 and 1248 on the centre row and y 526 and 564 on the centre column: 4 px outside the disc on
  every side. The ring is centred on its disc within 1 px. No offset of a few pixels in either arm.
- Memphis (100,30): unusable; the stock shot caught a frame without the interface.

Result: the patch does not move the garrison flag or its ring; the offset was not reproduced at this resolution and
UI scale on either arm. Not checked: the reporter's own resolution and UI scale (not known), and other flag states
(a damaged unit, a selected unit, an embarked or air unit in the city). Ticket 0679 proposed closed as not reproduced
and not the patch's; a new report should name the resolution, UI scale and the unit.
