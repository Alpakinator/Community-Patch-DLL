# Unit playback offline: premises K1-K9 tested on stock captures, the curve census, the pose sampler (epic 0327)

Epic 0327 (increment 3 units groundwork, N1-N4 of the Architect's increment 3 sheet,
`research/07-architecture-decisions/architect-2026-11-15-increment-3-decision-sheet.md` sections 1a, 3.2, 7). Started
2026-11-15 by lead-0327 (account A). Each section is written by the task named in it; the lead writes section 0.

Marks: `[C]` read in a capture's export, a file of the install or the database; `[L]` measured on stock captures;
`[I]` inferred. Nothing from the EXE or the game's files is in this repository (D83): captures, exports, palettes and
replays stay under `F:/c5-64bit-work/0327/` (D141) or the 0301 catalogue folders on F:, read in place.

Inputs:
- 3901's capture (0301): `F:/c5-64bit-work/0327/captures/c5h-a_20261114-131137_frame1762.rdc` (copy of
  `D:/c5-64bit-work/0167/captures/`, SHA-256 db058c39...4aabf3 equal), Tiny `C5H-0167-TINY_0008`, 18 skinned figures.
- 3907's catalogue exports (0301): `F:/c5-64bit-work/0301/3907/exports/` (`units_land/L01..L16`, `units_sea/S01..S04`,
  `rdc-export --select all`), placed objects in `shell/evidence/0301/3907/index.csv` (branch `epic-0301-scene-harness`).
- 3908's pose sequences (0301): `F:/c5-64bit-work/scene-library/poses/P01..P05` (captures and `*.bones.json`).
- 3909's analysis of the unit draws (0301): `research/05-graphics-composition/unit-draws.md` (branch
  `epic-0301-scene-harness` until 0301 merges).

## 0. Results in short

**Since then (epic 0041, 2026-11-15 20:19 EST, lead-0041):** U1-U3 are code in the renderer's unit module (`unit-renderer.md`): K1 and K2 hold on the whole catalogue (434 + 12 of 530 meshes byte-equal, 199 of 228 units
bit-equal in all three passes, the rest named), K3 on every catalogue figure, K5 to 0.002 units with no fitted shift.
Format 3 in the orientation slot is confirmed on a capture; degree 3 on several bones is still not [L].

**At the end (lead-0327, 2026-11-15 04:47 EST; waves 1 and 2 of 4151-4153, 3.4 agent-h of workers):**

- **K1 holds on 4 units, 11 meshes [L]** (sections 3, 6): warrior, tank, anti-aircraft gun and trireme byte-equal in
  vertices and indices except warrior_v2's two weight-tie vertices. Wave 2 corrected the rounding: float32 products,
  then half with ties away from zero.
- **K2 holds and the units draw offline [L]** (sections 4, 7): through `render/tools/terrainreplay` (four additive
  directives, existing outputs SHA-256-equal base vs task), our meshes give the captured pixels: tank 19,019 of 19,019,
  anti-aircraft gun 7,089 of 7,089, trireme 2,971 of 2,971, warrior 14,711 of 14,724 (max 1 level). Against the
  previous frame: median 0-5, p95 2-59 (one frame of animation [I]). DIAGNOSTIC, scene pass only.
- **K3 holds with the C++ sampler [L]** (section 5): `models/` decodes formats 1-5, 8-11, 17, 18 (3 and 17 from LSLib,
  MIT notice kept), de Boor degree 0-3; equal to the Python fits (t within 2.7e-5 s, rows within 1e-5) and to the
  captured palettes, incl. format 3, format 17 and degree 3 on catalogue units. 3901's second unit is the Shoshone
  Pathfinder, fortified.
- **K5 holds exactly [C, L]** (`unit-formations.md`): with exact figure positions from the palettes, the slot rule, the
  facing `yaw = 150 - 60 d` and the seed = the EXE's art record index, 134 of 138 multi-figure units fit within 0.002
  world units per figure; heights = max(height-field sample at the slot, water level 10.1167) + z offset, 1,083 of 1,135
  figures within 0.001 incl. all 197 on slopes. VP's FBX-exported models need a y-up to z-up axis change.
- **K8 [L]:** no world model has more than 32 bones (167 such meshes, all leader scenes): no split is needed.
- **K9 [C, L]:** six unit shader pairs, materials 90 (colour and mask), 89 (alpha) and 87 (not skinned, 20-byte
  vertex) (section 6).
- **N5:** `unit-feed-contract.md`, accepted by the MC (#1467).

At the K1-K3 gate (lead-0327, 2026-11-15 03:35 EST; wave 1 of tasks 4151-4153, about 0.5 agent-h each):

- **K1 holds on the warrior [L]** (section 3): one fixed repack rule (position x Scale as half, weights copied with a
  renormalisation when a fourth weight exists, bone indices = BoneBindings slots, normal / Scale as half, UV as half)
  makes 2,486 of 2,488 vertices and every index byte-equal to 3901's capture; the 2 others are an exact rounding tie
  in a weight.
- **K2 holds on the warrior [L]** (section 4): our mesh with the captured palette and the EXE's shader bytes gives
  the captured draw's pixels: 22,296 of 22,309 equal, the rest 1 level, median 0, max 1; 0 with the tie patched.
- **K3 holds [L]** (section 1): 99 of 100 idle figure blocks (warrior and horseman) are one clip at one t to float
  precision (rotation element error <= 1.1e-4, translation <= 1.3e-6 relative); the 100th is a cross-fade. The
  palette is `Rz(yaw) [R_k | s t_k] + T`, s = the member's Scale. The 3901 warriors are fortified, not idle.
- **N2** (section 2): of the seven unchecked curve formats only 3 (`DaConstant32f`) and 17 (`D3I1K16uC16u`) occur in
  unit clips (86 of 367 members); degree 3 in 19 members.
- **K5 holds [C, L]** (`unit-formations.md`): `UnitFormations.xml` (24 formations) and the EXE's slot rule reproduce
  3909's figure positions to a median of 0.00-0.40 world units once the per-unit random seed is fitted.
- Seen on the way (K6, not tested): clip time advanced 0.200 s per captured frame while the captures were 1.25-1.47
  s apart [L]; a clamp of the frame step at 0.2 s fits [I].

## 1. K3: an idle figure's palette from a clip at some t (task 4151)

Written 2026-11-15 02:57-03:30 EST by W1 of epic 0327 (task 4151, wave 1, branch `task-4151-pose-sampler`). Offline,
Python, no game run.

**Result: K3 holds [L].** Every captured idle figure block of the warrior (60 blocks) and of the horseman (40 blocks:
rider and horse meshes) is reproduced by one clip of its member at one t, to float precision, except one warrior
block caught in a cross-fade (1.4 below). Over the 99 single-clip blocks: rotation element error at most 1.1e-4
(warrior 6.2e-5), rotation error at most 0.0064 deg (warrior 0.0048 deg), translation error at most 1.3e-3 world
units, relative to the translation's size at most 1.3e-6 (warrior 3.8e-7; the float32 step at x = 940 is 6e-5). The
best clip is unambiguous: the runner-up clip's squared error is at least 19,000 times the best one's. This checks at
once the decoders of formats 2, 4, 5, 8, 9, 11, 18 (the clips' formats, 1.2), N's de Boor evaluation with clamped
knots (model-reader.md 6.3), the hierarchy by `ParentIndex`, `InverseWorldTransform`, the member scale and the world
placement.

### 1.1 The relation between a clip and the captured palette [L]

For bound bone k of a mesh (the k-th entry of the mesh's `BoneBindings`), the captured rows 2 + 3k .. 4 + 3k of the
figure's t27 block (`unit-draws.md` 4, branch `epic-0301-scene-harness`) are the 3 x 4 matrix `[R | p]` with

    R = Rz(yaw) * R_k(t)          p = s * Rz(yaw) * t_k(t) + T

where `[R_k | t_k]` = the top three rows of `World_b(t) * InverseWorldTransform_b` in the `.gr2`'s own units (inches
for the warrior, `UnitsPerMeter` 39.37), `World_b` = the product of local transforms (track curves, or the
skeleton's rest `Transform` for a bone without a track) along `ParentIndex`, column-vector convention, the file's
`InverseWorldTransform` transposed (it is stored for row vectors). Equivalently the palette matrix is
`A * D(s) * Skin_k * D(1/s)` with A the figure's rigid placement and D(s) a uniform scale: it carries no scale
because the EXE stores the vertices already multiplied by s (3909's finding, confirmed here).
- **s** = the member's `ArtDefine_UnitMemberInfos.Scale`, solved freely per block: 0.140000 on all 60 warrior blocks,
  0.120000 on all 40 horseman blocks (6 digits).
- **Placement** A: a pure rotation about z (tilt at most 0.0006 deg; ground slope does not tilt the figure on these
  plots) with yaw 30.000 deg on all 100 blocks (P01: units placed with facing 2; 3901: facing not recorded),
  and T = the figure's world position (z 11.94-12.66 on P01, the terrain's height). Rider and horse of one figure
  share T within 0.01 units. T itself (formation offsets) is K5, task 4153.
- No other transform: `Model.InitialPlacement` is identity in `warrior.gr2` and `horseman.gr2` [C], the warrior
  clips' track-group `InitialPlacement` too (model-reader.md 6.1); the warrior's root `Point_WORLD` and
  `CHARACTER_REORIENT` come from the clips like any other bone. Rest pose check: World(rest) x
  `InverseWorldTransform` = identity within 5.4e-5 for every bone of the three warrior skeletons [C].

### 1.2 Inputs and method

- Palettes: (a) 3908's P01_idle, four captures of c5h-f (`F:/c5-64bit-work/scene-library/poses/P01_idle/`, frames
  4677, 4679, 4681, 4683, SHA-256 6cbfe293e6fe75b4..., 0cf6920e2caf06e5..., 8743f1d7b60eba00..., 27c7c70052d092a9...;
  32-bit DLL `i0ship-26234371-ia`, stock config, frame cap 30, VT present; one warrior and one horseman of Polynesia
  on plots (42,12) and (43,12)); 3908's `*.bones.json` read in place. (b) 3901's capture
  `F:/c5-64bit-work/0327/captures/c5h-a_20261114-131137_frame1762.rdc` (SHA-256 db058c3933f412ba...), scanned with
  3908's `tools/scenario/bones.py scan` (branch `task-3908-poses-scenes`, run from a scratch copy) on W2's conversion
  `F:/c5-64bit-work/0327/xml/3901/` into `F:/c5-64bit-work/0327/4151/bones3901/` (20 skinned draws, 62 instances).
- The art: `F:/c5-64bit-work/0327/extract/fpk/UnitModels/` (c5h-c's archives, 2026-11-15 02:55): `warrior.gr2`
  (9d34effba92d5dcc...), `warrior_v2.gr2` (6101693ea454cbdf...), `warrior_v4.gr2` (524fe9979ce361e8...), the 33
  `warrior_*.gr2` clips (idle A 7a6bde9dbb15f091..., idle B 3205852fc8de8034..., fortify idle 0f43b19c09a0634a...,
  fortify idle fidget 2f13db02025c280d..., fidget A a5f5a16aa914b7a6...), `horseman.gr2` (b52d22435979dad8...,
  meshes `Horseman` 21 bones and `HorsemanHorse` 30 bones), `horseman_idlea/idleb.gr2` (856a1533139c447a...,
  436bcb10211cba4c...). The three warrior members (`WARRIOR` x6, `_V2` x3, `_V4` x3, Scale 0.14) share their 33
  clips; their meshes are told apart by index count (2022, 1962, 1914) and bind the same 20 bones in the same
  order. Database copy `F:/c5-64bit-work/0327/db/Civ5DebugDatabase.db` (27fc81c6466cc4a1...).
- Tool: `models/tools/posefit/posefit.py` (own code; decoding and de Boor are 3100's `curves.py`, imported). Per
  block and clip: t on a grid of 0.01 s over [0, Duration], then a golden-section refine to 1e-7 s; per t, R_A by
  orthogonal Procrustes on the 20-30 rotations, (s, T) by least squares on the translations. `track.py` follows
  figures across frames by T, `blendfit.py` fits a two-clip cross-fade, `summarize.py` writes the evidence.
- Clips tried: P01 the six idle-state clips (idle A/B, fidget A/B, the two A <-> B transitions); 3901 all 33 warrior
  clips (the six idle ones gave 72 deg: the 3901 warriors are fortified).
- Evidence (numbers only): `models/evidence/0327/4151/k3_p01_warrior_{blocks,bones}.csv`,
  `k3_p01_horseman_{blocks,bones}.csv`, `k3_3901_warrior_allclips_{blocks,bones}.csv` (per block: clip, t, s, yaw,
  tilt, error maxima, runner-up; per bound bone: rotation error deg, translation error, maximum element error).

### 1.3 Results per input [L]

| Input | Blocks | Clips that fit | Rotation element max | Rotation max (deg) | Translation relative max | s | yaw (deg) |
|---|---|---|---|---|---|---|---|
| P01 warrior, 4 frames x 12 figures | 47 of 48 | idle A 26, idle B 20, fidget A 1 | 6.2e-5 | 0.0048 | 2.8e-7 | 0.140000 | 30.000 |
| P01 horseman, 4 frames x 5 figures x 2 meshes | 40 of 40 | idle A 16, idle B 24 | 1.1e-4 | 0.0064 | 1.3e-6 | 0.120000 | 30.000 |
| 3901 warrior, 12 figures | 12 of 12 | fortify idle 11, fortify idle fidget 1 | 4.5e-5 | 0.0029 | 3.8e-7 | 0.140000 | 30.000 |

- The 3901 warriors (unit on plot (50,13)) are in the fortified state: 9 of 12 at the same t (1.07629 s of the 4 s
  `warrior_fortify_idle`), so a unit's figures enter a state together and keep one clock unless something (a fidget)
  separates them.
- Quaternion blend inside de Boor: nlerp (the reference), slerp (N) and plain lerp with a final normalisation give
  the same fits within the noise (element maxima 5.4e-5 / 5.8e-5 / 5.6e-5 on frame 4677's first three blocks); the
  data does not tell them apart.
- The residual is not uniform in t: blocks at t < 3 s have rotation errors of 0.0005-0.0064 deg, those at
  5.0-5.9 s 0.0001-0.0004 deg (idle clips have only 5-20 knots over 6 s, e.g. warrior idle B pelvis knots 0, 0,
  1.506, 3.012, 4.494, 6.0). Cause not established [I: quantisation of the earlier controls, or a difference in
  Granny's blend]; it is below the criterion and the C++ sampler (wave 2) can look at it per bone.

### 1.4 The one block that is a cross-fade [L]

P01 frame 4681, warrior figure at (917.5, -323.3) (`Warrior4`): no single clip fits (best fidget A at 0.148 s, 1.28 deg).
Followed across frames (`track.py`): 4677 idle A 5.0342 s (with its group), 4679 idle A **5.2339 s** (0.2 s behind
the group, which reads 5.4340), 4681 the blend, 4683 fidget A 0.6000 s (single clip, 0.0048 deg). `blendfit.py`
(per-bone lerp of positions and nlerp of orientations between two clips at t1, t2 with weight w) gives idle A at
t1 = 5.2387 s and fidget A at t2 = 0.1980 s with w = 0.687, rotation error 0.055 deg, element error 9.3e-4. So the
idle-to-fidget change (a state change, `RandomRoll` socket, model-reader.md 6.4 (b)) is a cross-fade in which the
old clip is held at its exit time (5.234 s, frozen) and the new one runs from 0; w = 0.687 after 0.2 s of the new
clip. The exact blend rule (weight curve, blend space; residual 0.055 deg) is not established [I]: K4's matter.

**Corrected 2026-11-15 (4407, epic 0374; unit-controller.md 7.3) [L]:** refitted with a multi-start search, the block
is fidget A at t2 = 0.1995-0.2000 with w = 0.680 (0.679-0.687 over every variant), rotation error 0.025 deg. The old
clip is best at t1 = 5.40 (5.33-5.46). The 5.234 above is a local minimum of the 4151 search: with t2 = 0.2 its cost
is 5.4 times the best, and running with the group (5.834) is 46 times. So the old clip is held, but at about its
value one frame after 4679 (about 5.434), not at the 4679 value. Local-space blending fits 100 times better than
blending the skinning matrices. (Replaced: "held at its exit time (5.234 s, frozen) ... w = 0.687".)

### 1.5 K6 on the side (one line) [L]

Within each group the clip time advances by exactly 0.400 s between captures two frames apart (0.200 s per frame:
5.0342, 5.4340, 5.8340, then 0.2340 after the wrap of the 6 s loop) while the Present times are 1.25-1.47 s apart
(the captures slow the frames): the animation clock is not wall time here; a clamp of 0.2 s per frame fits [I].

**Corrected 2026-11-15 (4407, epic 0374; unit-controller.md 7.2) [L]:** over P01-P04 every step between captures is
0.400 or **0.200** s within 0.0004 s. In P04 (captures 0.97 s apart) the attacking warrior's figures advance 0.200
while, over the same frames, the defending barbarian's advance 0.400. A per-frame `min(dt, 0.2)` cannot give
exactly 0.200 over two frames. So each figure advances 0.2 s or 0 s per presented frame in the capture regime, and
which units skip a frame is open. Fitted clip times lie on a 1/30 s lattice (65 of 72 groups outside the shuffle within 0.001 s), which
points to a fixed step of 1/30 s with at most 6 steps per frame [I]. (Replaced: "a clamp of 0.2 s per frame fits",
as the whole rule.)

**Corrected 2026-11-16 00:54 (lead-0374; unit-controller.md 7.2, 8.10, 9; MC #2089) [L]:** the 1/30 s lattice does not
hold. 4405 B's single captures at frame caps 30 and 60 (SR30, SR60) give clip times off the lattice, consistent with a
per-unit clock t += min(dt, 0.2) per presented frame; the 0.2 / 0 steps are a per-unit skip, not a fixed step. The skip
belongs to the unit given the action (Q4a: the attacker's figures advance 0.200 s, the defender's and a bystander's
0.400 s over the same two frames); every idle bystander advances 0.400 in every pair. The skip rule is kept as data in
the controller (`ActionSkip`); which frame it falls on is scored in 4406 round 3. (Replaced: "points to a fixed step
of 1/30 s with at most 6 steps per frame [I]".)

### 1.6 What more captures would tell

K3 needs no more captures. Open items that need data: a member with degree-3 orientation curves (19 members, 1.2;
none of the fitted clips has one) and a member with the `DaConstant32f` or `D3I1K16uC16u` formats, both in 3907's
catalogue frames if their units are among the 86 members of 1.2; a short burst of consecutive frames (not every other
frame) across an idle-to-fidget change gives the cross-fade's weight curve (K4).

## 2. N2: curve formats of the unit clips (task 4151)

Written 2026-11-15 03:05-03:30 EST by W1 (task 4151). Tool `models/tools/curvecensus/curvecensus.py` (own code; 9012's
reader and `gr2canon`'s string-database union, imported). Chain: `ArtDefine_UnitMemberInfos.Model` -> `.fxsxml` ->
`<Animation file ec>` -> `.gr2`, by lower-case basename over the extraction's archives, then c5h-c's `Assets`, then
c5h-c's MODS folder (artresolve's order, model-reader.md 3). Database copy as 1.2. Evidence:
`models/evidence/0327/4151/census_counts.txt`, `census_members.tsv`, `census_summary.json`.

**Result [C].** 367 unit members (366 used by `ArtDefine_UnitInfoMemberInfos`), all 367 `.fxsxml` found, 6,401
distinct clip files read without error, one clip missing (`U_Assyrian_Siege_Tower_Charge_Attack.gr2`, as 3104
found). Of the seven formats whose decoders were not checked (model-reader.md 6.2: 3, 12, 13, 14, 15, 16, 17),
**only two occur in unit clips**:

| Format | Slot | Curves | Clips | Members | Of which in idle-state clips (ec 1000/2000/1020/2020/1040/2040), curves |
|---|---|---|---|---|---|
| 3 `DaConstant32f` | scale/shear | 1,265 | 319 | 18 | 253 |
| 3 `DaConstant32f` | orientation | 20 | 9 | 2 | 5 |
| 17 `D3I1K16uC16u` | position | 168 | 113 | 73 | 18 |
| 12-16 (`D9I*`, `D3I1K32fC32f`) | | 0 | 0 | 0 | 0 |

- 86 members need at least one of the two (`census_members.tsv`, column `unchecked_formats`); 29 of them in an
  idle-state clip. `D3I1K16uC16u` is format 18 with 16-bit words (L `D3I1K16uC16u.cs`); `DaConstant32f` is one control
  of any dimension (L `DaConstant32f.cs`). Both are a few lines; the C++ sampler should have them rather than reject.
- The 18 members with a constant scale/shear (e.g. galleys, caravans, the atomic bomb, airships) are where a palette
  can carry scale: this matches 3909's column norms up to 2.23 on a few meshes (unit-draws.md 2) [I for the match].
- **Degree 3 occurs in 19 members** (orientation curves of formats 8, 9 and 1: 491 + 61 + 41 curves; tanks, armoured
  cars, mechs, rocket artillery, landships, the caravan late, colonists). N's de Boor code and `curves.py` handle any
  degree, but no degree-3 clip has been compared with a capture yet.
- The rest of the counts (curves / clips / members): scale/shear identity 245,205 / 6,401 / 367; `D3Constant32f`
  205,176 / 6,393 / 367; `D4Constant32f` 89,703 / 6,290 / 366; `D4nK8uC7u` deg 2 83,965 / 5,530 / 366; `D4nK16uC15u`
  deg 2 46,273 / 4,817 / 359; `D3I1K8uC8u` 14,140 / 4,653 / 364; `D3K8uC8u` 9,977 / 3,785 / 333; `D3K16uC16u` 407 / 278
  / 159; `DaK32fC32f` deg 2 125 / 107 / 88.

## 3. K1: the EXE's 28-byte vertex stream from the `.gr2` mesh (task 4152)

**Superseded 2026-11-16 (tickets 0379, 0447):** the exact rule (basis conversion with k = fl(fl(UnitsPerMeter x Scale) /
UnitsPerMeter), cofactor-inverse normals, Granny's half conversion, a float weight renormaliser on every skinned vertex)
is `unit-renderer.md` 10.9; `models/` and the unit module follow it since 0447 (515 / 515 drawn meshes byte-equal, the
warrior_v2 ties included). The rule below is the earlier fit, kept for its numbers.

Written 2026-11-15 02:57-03:35 EST by W2 of 0327 (task 4152, wave 1). **Result: K1 holds on the warrior. The EXE's
unit vertex stream is a fixed repack of the `.gr2` mesh: 2,486 of 2,488 vertices of the warrior's three meshes and
all 5,898 indices are byte-equal to the capture under the rule below [L]; the 2 others differ by one weight step at an
exact rounding tie.**

**Inputs.** 3901's capture `F:/c5-64bit-work/0327/captures/c5h-a_20261114-131137_frame1762.rdc` (SHA-256 db058c39...
4aabf3), Tiny `C5H-0167-TINY_0008`, 32-bit `i0ship-26234371-ia`, stock config, VT present; converted once on
2026-11-15 02:58 with `tools/harness/cli.py rdc-export ... F:/c5-64bit-work/0327/exports/3901 --xml-dir
F:/c5-64bit-work/0327/xml/3901 --select all` (618 draws, 63.5 s; shared with 4151). Draws 369, 370, 371 (scene pass)
= `warrior_v2.gr2` x 3, `warrior.gr2` x 6, `warrior_v4.gr2` x 3: the unit `ART_DEF_UNIT__WARRIOR` (members `_V2` 3,
`WARRIOR` 6, `_V4` 3, `Scale` 0.14 as a float, 0.14000000059604645; database copy `F:/c5-64bit-work/0327/db/`) on plot
(50,13). The `.gr2` files from the 2026-11-15 extraction `F:/c5-64bit-work/0327/extract/fpk/UnitModels/` with the
archive's `unitmodels_stringdatabase.gsd`. Tool: `models/tools/unitmesh/k1_probe.py` (one-off 9012's Python reader);
numbers in `models/evidence/0327/4152/k1_warrior*.json`.

**The layout [C, the export's input layout].** Elements 2-5 are declared with `D3D11_APPEND_ALIGNED_ELEMENT`; resolved,
the offsets are POSITION 0 (8 B), BLENDWEIGHT 8 (4), BLENDINDICES 12 (4), NORMAL 16 (8), TEXCOORD 24 (4): 28 bytes,
nothing overlaps or is packed. (3909's `unit-draws.md` 2 states the same offsets.)

**The rule [L, 3 meshes].** Vertex v of the mesh's `PrimaryVertexData`, in `.gr2` order, becomes:

| Bytes | Element | From the 68-byte `.gr2` vertex | Equal / 2,488 |
|---|---|---|---|
| 0-7 | POSITION half4 | `Position` x `Scale`, w = 0 | 2,488 |
| 8-11 | BLENDWEIGHT unorm8 x 4 | `BoneWeights` as they are when the fourth is 0; else the first three renormalised to 255 (w0' = round(255 w0 / s), w1' = round(255 w1 / s), w2' = 255 - w0' - w1', s = w0 + w1 + w2) and the fourth 0 | 2,486 |
| 12-15 | BLENDINDICES uint8 x 4 | `BoneIndices` as they are (all four, the fourth kept even when its weight is dropped) | 2,488 |
| 16-23 | NORMAL half4 | `Normal` / `Scale` (= x 7.142857), w = 0; not renormalised | 2,488 |
| 24-27 | TEXCOORD half2 | `TextureCoordinates0` u, v (the third component dropped; no flip) | 2,488 |

- Index buffer: the mesh's `Indices16` as they are, R16_UINT, `StartIndexLocation` 0, `BaseVertexLocation` 0; the
  three buffers are byte-equal (2,022 + 1,962 + 1,914 indices) [L].
- Bone indices are the mesh's `BoneBindings` indices, used as palette slots without a remap: slot k of the mesh's
  palette block is binding k (all three meshes bind 20 bones, the 20 filled bones per block of 3901's P5; largest
  index used 19) [L for the bytes; the slot meaning I, shown by K2's picture].
- **Rounding (corrected 2026-11-15 03:45, wave 2).** The products are float32 (`Position` x `Scale`, `Normal` /
  `Scale`), and every float goes to the nearest half with **ties away from zero** [L: all 11 drawn meshes of the
  warrior, tank, anti-aircraft gun and trireme, section 6]. Replaced: "rounded once from a wider value, ties to even
  [I: x87]": that fitted the warrior (float32 then ties-to-even leaves 5 of its normals one step off), but leaves 3
  positions of `trireme.gr2`'s `Oars_L` one step low (z = 0.6467285 lies 2.6e-9 below a tie in double and exactly on
  it in float32). `Normal` / `Scale` and `Normal` x (1/`Scale`) in float32 give the same bytes on these meshes.
- **The normal is not unit length**: its length is 1/Scale (7.14 for the warrior). The vertex program applies the
  bone's 3 x 3 rows to it (3909, `unit-draws.md` 2); the pixel program must normalise it [I].
- **Weights.** All three meshes' weights sum to 255 per vertex. `warrior.gr2` has no vertex with a fourth weight;
  `warrior_v2` has 48, `warrior_v4` 8. Copied as they are, those 56 differ (by up to 55 levels); under the rule 54
  match. The two others (vertices 428 and 442 of `warrior_v2`, weights 119, 62, 57, 17: w0 exactly 127.5) are captured
  as (127, 66, 62) where the rule gives (128, 66, 61). Three other exact ties of the set, (122, 78, 44, 11), (122, 87,
  35, 11) and (91, 85, 62, 17), are captured rounded up. No integer tie rule fits all four; about 400 float32 formulations
  of the renormalisation (weights divided by 255 or multiplied by its inverse, the sum of the three in any order, the
  sum of four minus the fourth or 1 minus the fourth, divide or multiply by the inverse, four rounding modes, the third
  rounded or taking the rest) reach at most 54 of 56 [L, 2026-11-15]. The rule here rounds
  ties up; the EXE's own float arithmetic decides the tie [I]. Effect: K2's 13 pixels at one level (section 4).
- **Not tested here:** a fourth weight with a third weight of 0 (never seen; `unit_mesh.cpp` keeps w1' <= 255 - w0'
  there [I]). Meshes above 32 bones and other units: section 6 (wave 2).

**Library [C++].** `models/include/c5models/unit_mesh.h`, `models/src/unit_mesh.cpp`: `repack_unit_mesh(mesh, scale,
out, err)`, `unit_weights`, `half_from_double`; refuses a mesh without the five components, above 32 bones (K8),
above 65,536 vertices, with an index out of range, a weighted bone index not below the bone count, a vertex with only
a fourth weight, or a scale that is not finite and > 0. `models/tests/test_unit_mesh.cpp` (82 checks): half rounding
(every finite half round-trips; the double-rounding case), the weight rule on 100,000 random quadruples, a hand-made
mesh, every refusal, and the three warrior meshes against FNV-1a 64 fingerprints of the captured buffers: the vertex
buffers of `warrior` (0x185b675f25084168) and `warrior_v4` (0xc368ed5b584dc4cd) and all three index buffers equal the
capture's; `warrior_v2`'s rule output is 0xded26cc25d5c8467 (captured 0xa06d5cd8b80e2ab7, the two tie vertices).
`python -I models/build.py --out C:/c5-64bit-work/0327/4152/build --test` with `C5M_SAMPLES=F:/c5-64bit-work/0327/extract`:
5 of 5 tests passed, none skipped (2026-11-15 03:24); the same with `--asan` (cl.exe AddressSanitizer): 5 of 5.

## 4. K2: the draw replayed with our mesh, the captured palette and the EXE's shader bytes (task 4152)

Written 2026-11-15 by W2 of 0327 (task 4152, wave 1). **Result: K2 holds on the warrior. Replaying the scene pass's
four unit draws with our three meshes instead of the captured vertex and index buffers, everything else as captured,
gives the same pixels: 22,296 of 22,309 unit pixels equal, 13 at one level (median 0, p95 0, max 1), all in draw 369
and all from K1's two tie vertices; with those two vertices' weights set to the captured ones, 22,309 of 22,309 [L].**

**Tool.** `models/tools/unitreplay/` (issue 4152): `unitreplay.exe`, a copy of 0182's
`render/tools/terrainreplay/terrainreplay.cpp` at main f98c6449 with five changes listed in its header (instanced
draws through `call ... INSTANCES [START_INSTANCE]`; `bufsrv` for buffer SRVs such as t27; `tex` DIM 4 for cube
textures; `background`; the name), built by the models CMake (`models/tools/unitreplay/libs.txt`);
`unit_manifest.py` (manifest from an `rdc-export` export, `--vb N=` / `--ib N=` to replace a draw's buffers;
sampler descriptions collected over all draws, because the export writes each only in the first draw that binds it);
`frame_start.py` (a target's frame-start contents from the zip.xml conversion); `k2_compare.py` (statistics and
difference images). Test: `test_unitreplay.py` on WARP, a hand-made instanced skinned draw (28-byte vertices, id
stream at a byte offset, palette blocks, three blended bones, start instance, a cube, a background) and every input
check: 48 of 48 cases (2026-11-15).
**Moved (wave 2, 2026-11-15 03:50):** with the MC's grant (queue 1446, 1463) the four directives are now in
`render/tools/terrainreplay/terrainreplay.cpp` itself (`bufsrv` renamed `buf`; README section "Directives added by
issue 4152"; its test's `unit_draw_cases`), outputs of existing manifests bit-equal before and after; the copy
`unitreplay.cpp` is removed and `unit_manifest.py` writes terrainreplay manifests. The numbers of this section were
measured with the copy, whose replay code is the same.

**Inputs [C].** Draws 368-371 of the export (VS `29a0b061`, PS `60d270e1`, cb1 192 B, t0/t1 BC3 128 x 128, t2
R8G8B8A8 cube 32 x 32, t3/t4 BC1 cubes 32 x 32 with 6 mips, t5 128 x 8 ramp, VS and PS t27 the palette buffer 858,
sampler 878 linear wrap on s0-s5, blend src-alpha / inv-src-alpha, depth LESS_EQUAL, cull back, front
counter-clockwise), the shader bytes read from the export at run time. Target: the swap chain's back buffer, R8G8B8A8_UNORM, 2554 x 1411,
**sample count 1** [C: `IDXGISwapChain::GetBuffer` in the conversion; its view says TEXTURE2DMS]. Replays on the
workstation's GPU (RTX 4080, driver 610.88) and on WARP. Output under `F:/c5-64bit-work/0327/4152/k2/`; numbers in
`models/evidence/0327/4152/k2/`.

| Comparison (unit pixels = pixels draws 368-371 cover, 22,309) | Pixels equal | Median | p95 | Max |
|---|---|---|---|---|
| (a) captured buffers vs (b) ours, GPU | 22,296 | 0 | 0 | 1 |
| (a) vs (b), WARP | 22,293 | 0 | 0 | 1 |
| (a) vs (b) with the two tie vertices patched, GPU | 22,309 | 0 | 0 | 0 |
| (a) vs (b) with draw 399 composited, all pixels written (273,554) | 273,541 | 0 | 0 | 1 |
| (a) GPU vs (a) WARP | 3,138 | 1 | 5 | 36 |

**How faithful the replay is [L].** The capture holds no picture of its own frame's end; the conversion holds the back
buffer's contents at the frame's start (resource 2419, `Internal::Initial Contents`), i.e. the previous frame, one
frame earlier in the idle animation. Replayed over that picture as background,
with draw 399 composited and the 1,479 unit pixels under the interface (draws 400-617) left out: 20,830 pixels,
median 2, p95 38, max 211; 2 px inside the silhouettes (13,301 pixels) median 2, p95 25; the group on (51,15) (draw
368, nothing drawn over it) median 1, p95 10. The differences lie on silhouettes, weapons and feet, where one frame
of animation moves the figures [I]. Against the harness's live shot of the launch (`live_stock_stimuli.png`, another
moment): median 9, p95 78.

- **Draw 399 [C, L].** VS `da2382af` / PS `8d853988`, 2,124 indices, R32 float position, normal and uv, three sets of
  (256 x 4 ramp, 4 x 4, 512 x 512) textures, blend ONE / SRC_ALPHA (premultiplied), depth on: vertical translucent
  streaks over the selected unit's plot [I: a selection effect; not named in `material_table.txt`]. It covers 12,746
  of the 22,309 unit pixels; without it the warrior group compares at median 7-28 against the previous frame.
- **Negative result (cube textures).** The first replay drew the units black: terrainreplay creates every texture with
  ARR 6 as a 2D array, and the PS's TextureCube registers (t2-t4) read 0 from it. `tex` DIM 4 (now in terrainreplay) fixes it. Any
  replay of `UnitShader_Skinned_CODE` (and every material binding the environment cubes) needs it.
- **The replay's own limits:** the depth buffer is D32_FLOAT_S8X24 cleared to 1 (the capture's is D24_UNORM_S8_UINT
  with the terrain in it), so units are not hidden by terrain or trees; GPU and WARP differ by up to 36 levels (median
  1) on the same inputs, so comparisons are made on one device.

Commands (2026-11-15; E = the export, K = `F:/c5-64bit-work/0327/4152`; our buffers from `k1_probe.py --write-vb
--write-ib`): `frame_start.py <xml> <zip> 2419 K/k2/frame_start_2419.rgba`; `unit_manifest.py E K/k2/run_hardware
--draws 368-371 --exe <render build>/tools/terrainreplay/terrainreplay.exe --name a_captured [--background ...]`, the same with `--name b_ours --vb
369=... --vb 370=... --vb 371=... --ib ...` (and `--draws 368-371,399`, `--draws 400-617 --name ui` for the
interface's coverage); `k2_compare.py K/k2/run_hardware a_captured b_ours K/k2/cmp_hardware`; fidelity
`k2_compare.py ... a399_bg ref --ref K/k2/frame_start_2419.rgba --cover-from a_captured_bg --exclude-touched ui
[--erode 2]`.

Pictures (DIAGNOSTIC, Visual Checks): `vc-20261115-032051-workstation-940e` (K2, ours vs captured, difference image),
`vc-20261115-032052-workstation-2f8b` (fidelity against the frame-start picture).

## 5. The pose sampler in `models/` (task 4151)

Written 2026-11-15 03:32-03:55 EST by W1 (task 4151, wave 2, branch `task-4151-pose-sampler`). Offline, no game run.

**Result.** The C++ pose sampler (`models/include/c5models/anim.h`, `src/anim_curves.cpp`, `src/anim_pose.cpp`)
reproduces the Python reference and the captured palettes:
- on synthetic data it equals `curves.py` / `posefit.py` within float tolerance [C];
- on all 100 blocks of 1.3 it picks the same clip as the Python fit, at the same t within 2.7e-5 s, and its skinning
  rows differ from Python's at the Python t by at most 1.04e-5 (5.1e-7 relative to the translation) [L];
- on the catalogue it fits a second fortified unit and five more unit types, including every format the census said
  is needed (1.2 and 2), except format 3 in the orientation slot, which no drawn unit of the catalogue uses in idle
  (5.4) [L].

### 5.1 What the library does [C, the code]

- `decode_curve()`: formats 1, 2, 3, 4, 5, 8, 9, 10, 11, 17, 18 from the reader's raw `Curve` (format, degree, scalars,
  arrays) into knots in seconds and controls. It refuses 0, 6, 7, 12-16 and anything else with
  `AnimErr::UnsupportedFormat`; also checked: degree above 3 (`BadDegree`), a missing scalar or array or a wrong
  element type (`MissingMember`), a length that is not a multiple of 4 or 2 or controls that are not n x dim
  (`ArrayMismatch`), a curve dimension that is not the slot's (`DimensionMismatch`), a decreasing or non-finite knot
  (`KnotsNotMonotonic`), a non-finite control or knot scale (`NonFinite`). On failure the output is cleared.
  Formats 3 and 17 are translated from LSLib (`DaConstant32f.cs`, `D3I1K16uC16u.cs`, commit 6f5f6987, MIT, notice
  and origin in the source's header, D34 / D22); the others are written from model-reader.md 6.2.
- `eval_curve()`: de Boor of degree 0-3 with nwn2mdk's padded knot vector (model-reader.md 6.3), span by binary
  search, quaternions blended by nlerp with the hemisphere fix at every step (as `curves.py`), the result normalised;
  t is clamped to [0, last knot]. `clip_time()`: loop (t mod Duration) or clamp.
- `bind_clip()`: the first track group bound by bone name; bones without a track keep the rest `Transform`; tracks
  naming no bone are counted (`unbound_tracks`), not an error. `sample_local()`: `R(q) * SS^T` and the translation.
  `world_from_local()`: by `ParentIndex`, a parent at or after its child is `ParentOrder`. `skin_rows()`: World x
  `InverseWorldTransform` (transposed from its row-vector storage) per mesh `BoneBinding`, an unknown bone name is
  `UnboundBone`. `palette_rows()`: `[Rz(yaw) R_k | s Rz(yaw) t_k + T]` (1.1).
- Tool `models/tools/posesample` prints a model's skinning (or, with `--placement s,yaw,T`, palette) rows at given
  times; exit 1 on usage errors, 2 on unreadable or undecodable files. `posefit.py fit --sampler cpp --exe ...`
  runs the K3 search on its output (grid 0.01 s, then three zoom rounds of 41 points to about 1e-6 s);
  `compare.py` sets a C++ fit beside a Python one per bone.

### 5.2 Tests [C]

- `models/tests/test_anim.cpp` (ctest), 1,490 checks, 0 failures:
  - 20 synthetic curves (every decoded format; degrees 0, 2 and 3; 2-12 knots; t below 0, at every knot, at the
    midpoints, past the end) and a 5-bone skeleton with a clip of 5 tracks (one naming no bone, one bone without a
    track, formats 1, 2, 3, 4, 5, 8 deg 3, 9, 10, 11, 18) bound to a 4-bone mesh at 5 times. The expected values come
    from `curves.py` and `posefit.py` (`models/tools/posefit/make_anim_reference.py` writes `tests/anim_reference.h`;
    random data, seed 4151, no game bytes).
  - Largest |C++ - Python|: curve values 2.4e-3 on controls of size up to 1.3e4 (relative 2e-7); skinning rows 9.8e-4
    on translations up to 9.7e3; palette rows 1.2e-4.
  - Negative tests, each checking the error code, a non-empty message and the cleared output: formats
    0, 6, 7, 12, 13, 14, 15, 16, 19; a 15-word array; 13 controls for 4 knots; bytes where words belong; a missing
    scalar; a missing curve; decreasing and NaN knots; a NaN control; a position curve in the orientation slot; a
    4-control constant in the scale/shear slot; degree 4; an unbound mesh bone; a parent after its child and out of
    range; a clip with a format 13 track (whole clip refused); no track group; no such animation; `clip_time`'s edges.
- `python -I models/build.py --out C:/c5-64bit-work/0327/4151/build --test`: 6 of 6 ctest tests passed, none skipped
  (2026-11-15 03:40). The `--asan` run is in 5.5.
- `models/tools/posefit/test_posefit.py` (pytest, `POSESAMPLE` = the built tool): 8 passed, including posesample's
  usage errors (exit 1, empty stdout), unreadable or invalid files (exit 2) and the Python driver raising on a failed
  tool call with nothing cached.

### 5.3 The C++ sampler against the Python fits [L]

Same inputs as 1.2; the C++ fit searches t itself (`posefit.py fit --sampler cpp`). Evidence:
`models/evidence/0327/4151/k3cpp_p01_warrior_{blocks,bones}.csv`, `k3cpp_p01_horseman_*`,
`k3cpp_3901_warrior_allclips_*` (columns: both fits' clip, t and errors per bone, and the C++ - Python difference
of the skinning rows at the Python t, the translation in world units).

| Input | Blocks | Same clip | max abs(t_cpp - t_py) (s) | Skinning rows, C++ - Python max (relative) | C++ fit: rotation element max / translation relative max |
|---|---|---|---|---|---|
| P01 warrior | 48 | 48 | 2.7e-5 | 5.9e-6 (5.0e-7) | 6.3e-5 / 2.8e-7 (47 blocks; the cross-fade as in 1.4) |
| P01 horseman | 40 | 40 | 2.4e-5 | 1.0e-5 (5.1e-7) | 1.1e-4 / 1.3e-6 |
| 3901 warrior, 33 clips | 12 | 12 | 1.7e-5 | 7.9e-6 (4.8e-7) | 4.5e-5 / 3.8e-7 |

### 5.4 More units, with the C++ sampler only [L]

Palettes scanned with 3908's `bones.py` from 3907's conversions (`F:/c5-64bit-work/0301/3907/xml/`) into
`F:/c5-64bit-work/0327/4151/bonescat/` (frames L02 1098, L06 3084, L09 1678, L11 2705; save `C5H-0301-CAT_0009`,
32-bit DLL `i0ship-26234371-ia`); art from the extraction (1.2). Clips tried: the member's six idle-state clips
(SS booster: all 16). Evidence `models/evidence/0327/4151/k3cpp_cat_*` and `k3cpp_3901_pathfinder_all_*`.

| Unit (member, Scale) | Frame, blocks | Clip and t | Rotation element max | Translation relative max | Formats exercised |
|---|---|---|---|---|---|
| 3901's second unit, on (51,15): art `ART_DEF_UNIT_U_SHOSHONE_PATHFINDER` (6 x member `U_SHOSHONE_PATHFINDER`, 0.14; `UNIT_SHOSHONE_PATHFINDER` in this database), mesh 3708 indices, 24 bones | 3901 1762, 6 figures | fortify idle (5), fortify idle fidget (1) | 6.9e-6 | 1.4e-7 | 8, 9, 18 |
| Caravan (`CARAVAN_F1`, `_F2`, 0.12) | L02, 1 + 1 | idle B 1.83435 s, idle A 1.83439 s | 2.4e-6, 5.5e-6 | 6.3e-8, 1.0e-7 | **3 scale/shear** (bones `bone_Sack_L01/L02`, a constant mirror diag(1, 1, -1): per-bone rotation error 4e-5 deg) |
| SS booster (`SS_BOOSTER`, 0.10), truck mesh | L09, 1 figure | idle A 3.90202 s | 1.1e-6 | 4.7e-8 | **17 position and 9 degree 3** (bone `Bone_Cab_02`: 6e-5 deg, element 2.3e-5) |
| Huns horse archer (`U_HUNS_HORSE_ARCHER`, 0.12), rider and horse | L11, 5 figures x 2 meshes | idle A (8), idle B (2), all at 3.1668 s | 9.8e-6 | 1.5e-7 | 17 is on `Arrow_Attach`, which no mesh binds: not exercised |
| Tank (`TANK`, 0.12), hull and treads | L09, 3 figures x 2 meshes | idle A / B 1.48006 s | 1.8e-7 | 1.0e-7 | degree 3 only in the fidgets (turret): not exercised in this frame |

- s is the member's Scale wherever the bound bones move apart from each other (0.12000, 0.10000 solved). For rigid
  meshes it is not determined: the tank's tread mesh (5 bones that do not move in these clips) and the booster's
  trailer and bracket give any s and any t (runner-up ratio 1) with element errors of 1e-9. For the tank and the
  booster the table therefore uses s fixed to the member's Scale (`--scale`); the free fit gave the same clips and t
  for the moving meshes.
- The truck and trailer meshes of the booster also match blocks of other units on the frame (ids 17, 18, 67, 68,
  71, 72, at x 833-1089): they share the booster's meshes and play other clips (0.13-0.34 deg with the booster's
  clips) [I: the other spaceship-part units]. 3909's `units.csv` counts 1 figure (3 instances) for the booster.
- Mech (L06) is the one member whose idle clips use format 3 in the orientation slot, and its art is not drawn in
  L06 (3909: 0 figures for mech and mechanized infantry); format 3 in the orientation slot is checked on synthetic
  data only.

### 5.5 ASan [C]

`python -I models/build.py --out C:/c5-64bit-work/0327/4151/build_asan --asan --test` (cl.exe /fsanitize=address,
2026-11-15 03:50): 6 of 6 ctest tests passed, test_anim included, none skipped. The ASan build of `posesample`
also ran without a report on real files exercising formats 17 and 9 degree 3 (`ss_booster_idlea`), 3
(`caravan_early_f2_idlea`, with `--loop` past the end) and 8 / 9 degree 3 (`tank_fidgetb`); the ASan runtime DLL is
taken from the MSVC tools' `bin/Hostx64/x64` folder.

### 5.6 What is not done here

- The sampler evaluates one clip; cross-fades (1.4) and the blend-DAG and state machine (model-reader.md 6.4)
  are not in it.
- Granny's own quaternion blend inside de Boor (nlerp, slerp or plain lerp) is not decided by the data (1.3); the
  sampler uses the reference's nlerp.
- Loop seams: `clip_time` wraps; the clamped-knot evaluation at both ends is what the captures fit (1.3), no
  periodic continuation.
- **Added 2026-11-15 (epic 0041, 4353; unit-renderer.md 11):** the sampler bound only a clip's first track group and
  posefit/posesample used only a file's first skeleton, and no file basis was converted. Now `bind_clip_all_groups`
  binds every group (first track of a bone wins; tracks naming no bone are not decoded), the renderer's pose layer
  (`render/src/units/pose.cpp`) poses each mesh with its own skeleton and brings FBX-basis files to the stock basis,
  posefit and posesample pose each mesh with its own skeleton, and `read_gr2` reads a track group an animation alone
  references (VP's `flagbearer_idle.gr2`). Still not here: cross-fades, the blend-DAG and state machine (U4/U5).

## 6. K1 on more units, K8 and K9: meshes above 32 bones, the shader pairs (task 4152)

Written 2026-11-15 03:32-04:00 EST by W2 of 0327 (task 4152, wave 2). Inputs as section 3, plus 3907's catalogue
exports read in place (`F:/c5-64bit-work/0301/3907/exports/`, `--select all`; c5h-c, 32-bit `i0ship-26234371-ia`,
stock config, VT present, save `C5H-0301-CAT_0009`): `units_land/L09` (frame 1678; `ART_DEF_UNIT_TANK` on (38,9)),
`units_land/L01` (frame 600; `ART_DEF_UNIT_ANTI_AIRCRAFT_GUN` on (40,13)), `units_sea/S03` (frame 633;
`ART_DEF_UNIT_TRIREME` on (30,9)); members from the database copy: `ART_DEF_UNIT_MEMBER_TANK` x 3 and
`_ANTIAIRCRAFTGUN` x 3 at `Scale` 0.12, `_TRIREME` x 1 at 0.07 (floats). Numbers in `models/evidence/0327/4152/wave2/`.

**K1 holds on four units, eleven meshes [L].** `models/tools/unitmesh/k1_scan.py` repacks every mesh of a `.gr2` and
compares it with every draw of the export whose 28-byte vertex buffer has its size:

| Unit (export) | `.gr2` mesh: vertices, bones | Scene / reflection / mask draws | Vertices and indices byte-equal |
|---|---|---|---|
| tank (L09) | `Tank_Treads` 104, 5; `Tank` 1,440, 11 | 571-572 / 118-119 / 39-40 | all; both buffers equal |
| anti-aircraft gun (L01) | `Artillery_Grunt` 710, 20; `Artillery_Captain` 1,041, 24; `AntiAircraftGun` 2,415, 20 | 457-459 / 75-77 / 18-20 | all |
| trireme (S03) | `Oars_R` 792, 18; `Oars_L` 864, 18; `Trireme_Undamaged` 1,499, 5 | 364-366 / 81-83 / 33-35 | all (with the corrected rounding) |
| warrior (3901) | `Warrior` 863, `Warrior2` 829, `Warrior4` 796, 20 each | 369-371 / 58-60 / 13-15 | all but `Warrior2`'s two tie vertices (section 3) |

- A `.gr2` with several meshes is drawn one draw per mesh, each with its own vertex and index buffer (no shared
  buffer, base vertex 0) [L]. Meshes the unit does not show are not drawn: the trireme's `Oars_L_DMG`, `Oars_R_DMG`
  and `Trireme_HeavyDMG` (damage states) have no draw in S03 [L].
- The C++ repack follows (`half_from_float_away`, float32 products); `test_unit_mesh` fingerprints all eleven meshes:
  146 checks.

**K8: no unit mesh has more than 32 bones [L, 2026-11-15].** `gr2sweep` over the whole extraction (12,761 files,
`F:/c5-64bit-work/0327/extract/gr2_manifest.tsv`, 10.9 s): 167 meshes bind more than 32 bones, and every one is a
leader-scene model (`LeaderModels`, `Expansion1/2LeaderModels`, and leader files in `ConquestDeluxe`, `Mongol`,
`Spanish`, `Polynesia`, `Incan`, `Babylonian`, `Denmark`, `Korea`; up to 50 bones). The largest elsewhere: 32 in
`UnitModels.fpk` and in Vox Populi's mods, 30 in the install's loose `Assets`, 29 in the Community Patch, and 32 in
one wonder (`hititte_war_chariot.gr2`). So the 32-bone palette block always holds a unit mesh, and no split into
several draws exists to be copied for the world renderer; leader scenes stay in the EXE (D80). `unit_mesh` keeps
refusing a mesh above 32 bones (its test checks it) [I: should a mod add one, the EXE's split is not known].

**K9: the unit programs of the frames [C, L].** `models/tools/unitmesh/k9_pairs.py` over the 20 unit exports of 3907
and 3901's, names from 3909's `shader_programs.csv` and its run-time `blobnames.json`
(`F:/c5-64bit-work/0301/3909/out/`):

| Material, pass | VS (EXE VA) | PS (EXE VA) | Draws | Vertex input |
|---|---|---|---|---|
| 90 `UnitShader_Skinned_CODE`, pass 0 (scene and water reflection) | `29a0b061` 0x6803140 | `60d270e1` 0x6813630 | 1,194 | 28 B + per-instance id |
| 90, pass 1 (shadow mask) | `8cb52b96` 0x6804E48 | `72c27aea` 0x6815328 | 596 | 28 B + id |
| 89 `UnitShader_SkinnedAlpha_CODE`, pass 0 | `29a0b061` | `5def4f99` 0x6811108 | 2 (S04) | 28 B + id |
| 89, pass 1 | `8cb52b96` | `246527ed` 0x6812DA0 | 1 (S04) | 28 B + id |
| 87 `UnitShader_CODE` (not skinned), pass 0 | `82e59838` 0x68005D0 | `0e7a3566` 0x680CC38 | 6 (L06, L09, S04) | 20 B: POSITION half4, NORMAL half4, TEXCOORD half2; slot 1 bound, VS t27 bound, no BLENDINDICES1 in the layout |
| 87, pass 1 | `59af33ff` 0x6802938 | `9a5991d7` 0x680E7F0 | 3 | 20 B |

- Every skinned unit draw uses one of the four material 89/90 pairs; the two VS programs serve both materials. The
  materials 83-86 (`UnitShader`, `_Alpha`, `_Skinned`, `_SkinnedAlpha` without `_CODE`) have no draw in these 21
  frames [L].
- Draws of `BuildingShader_Skinned_CODE` (material 6; 26) also carry the per-instance id and the 28-byte stream
  (3909, `unit-draws.md` 8), as do `TerrainShaderScreen` draws (stride 48, 28 draws): the per-instance id does not by
  itself mark a unit draw.

## 7. The units drawn offline, comparison per COMMON 25 (task 4152)

Written 2026-11-15 by W2 of 0327 (task 4152, wave 2). `models/tools/unitreplay/draw_units.py` per unit: our meshes
from the `.gr2` (k1_probe's rule) replace the captured vertex and index buffers of the unit's scene-pass draws; both
replays run on terrainreplay (GPU) over the back buffer's frame-start contents (the previous frame, `frame_start.py`);
(a) captured against (b) ours on the unit's pixels; (b) against the frame-start picture on the unit's pixels that no
later draw into the back buffer writes (water, effects, interface; replayed for their coverage), and 2 px inside.

| Unit | Unit pixels | (a) vs (b): equal, median, p95, max | (b) vs frame start: pixels, median, p95, max | 2 px inside: median, p95 |
|---|---|---|---|---|
| tank (L09, 571-572) | 19,019 | 19,019, 0, 0, 0 | 19,019, 1, 3, 93 | 1, 3 |
| anti-aircraft gun (L01, 457-459) | 7,089 | 7,089, 0, 0, 0 | 7,089, 0, 2, 144 | 0, 1 |
| trireme (S03, 364-366) | 2,971 | 2,971, 0, 0, 0 | 470, 5, 59, 164 | 4, 22 |
| warrior (3901, 369-371) | 14,724 | 14,711, 0, 0, 1 | 2,664, 3, 59, 209 | 3, 31 |

- **Our meshes draw the captured pixels**: bit-equal for the tank, the gun and the trireme; the warrior's 13 pixels
  at one level are section 3's two tie vertices [L].
- **The replay against the game's own previous frame**: median 0-1 and p95 1-3 for the units standing still (tank,
  gun; the gun's crew 457/458 median 1, p95 15 and 5); the trireme and the warrior move (oars, the idle animation)
  and are mostly under later draws (the trireme's hull under the water surface: 2,501 of 2,971 pixels; the
  warrior's under the selection effect, section 4) [L for the numbers, I for the cause].
- Pictures (DIAGNOSTIC, Visual Checks; ours over the frame-start picture next to it, the (a)-(b) difference image):
  tank `vc-20261115-035149-workstation-a678`, gun `vc-20261115-035150-workstation-5b51`, trireme
  `vc-20261115-035151-workstation-da5e`, warrior `vc-20261115-035152-workstation-4aea`.
- Command per unit: `python -I -X utf8 models/tools/unitreplay/draw_units.py --export <export> --xml <xml> --zip <zip>
  --gsd <UnitModels gsd> --exe <render build>/tools/terrainreplay/terrainreplay.exe --out F:/c5-64bit-work/0327/4152/s7/<unit>
  --scale <member Scale> --map <draw>=<gr2>:<mesh> ...`.
- **Not done here:** the shadow-mask and reflection passes (same meshes and programs, other cb1 and targets; their
  draws are listed in section 6), the non-skinned material 87 units, a whole frame's composition with terrain depth.

## 8. Section 7 of the sheet re-scored (lead)

Raw hours of the Architect's section 7 against what this epic leaves (lead-0327, 2026-11-15 04:47). The epic spent
about 3.4 worker agent-hours and 2 lead hours on what the sheet put at 30-52 h raw for U1-U3 groundwork.

| Package | Sheet | Re-scored | Why |
|---|---|---|---|
| U1 mesh, textures, draw from a captured palette (L0, L1) | 12-20 | 4-8 | K1, K2, K8, K9 done offline; left: material 87's 20-byte repack, the shadow-mask and reflection passes, VP's FBX basis for vertices, the weight-tie rule |
| U2 pose sampler, census, pose fitting (L2) | 10-18 | 1-3 | done in `models/`; left: format 3 orientation and multi-bone degree 3 against captures |
| U3 formations, placement, scale, facing, slopes (L3) | 8-14 | 1-3 | done to 0.002 units; left: the 0.07-unit common shift, the Mamluk's missing horse, which EXE code applies the FBX basis |
| U4 pose tap with A1 | 4-7 | 4-7 | unchanged |
| U5 controller (L4) | 16-30 | 16-30 | unchanged; the cross-fade rule (1.4) and K6 (the clock: 0.2 s steps seen) are its premises; one cross-fade block measured (1.4) |
| U6 server scene | 14-24 | 14-24 | unchanged; its feed is `unit-feed-contract.md` |
| U7 live comparison | 6-10 | 6-10 | unchanged |
| **Units total** | **70-123** | **46-85** | |

The project's measured ratio on this epic was about a tenth of the raw estimate for offline premise work; it is not
applied to U5-U7, which are design and live work.

**Corrected 2026-11-15 14:11 (lead-0041, epic 0041 C1, MC #1845):** the U5 row said "K4 (cross-fades)"; in the
Architect's sheet (3.2) K4 is the palette header rows of L1 (row 0 team colour, row 1 not known), not cross-fades.
The cross-fade rule is a U5 premise without a K number. Homes after 0041's C1: U1-U3 = epic 0041, U4 with 0040, U5 =
ticket 0374, U6 + U7 = ticket 0375. (Replaced: "K4 (cross-fades) and K6 ... are its premises".)

**Re-scored again 2026-11-15 20:19 (lead-0041, at epic 0041's end; U1-U3 done there, 6.4 agent-h against 25-44 h
raw):** U4 4-7 unchanged (with 0040). U5 16-30 -> 15-28: the clip cache, clip lookup by name and EC code, every track
group bound and per-mesh skeletons are in the module (`unit-renderer.md` 11); left: state machines, blend graphs,
cross-fades, the clock K6, and multi-bone degree 3 against a capture (ticket 0374). U6 14-24 -> 10-18: the module's
input (`UnitScene`, `.c5units`), its draws of all three passes, the pre-check and the failure statuses exist; left:
the feed's records 3-5 with the seed allocator and the player colour Type (`unit-feed-contract.md` 8a), the draws in
the server's frame with depth, fog of war and the composite, residency and the cost on a late huge game (ticket
0375). U7 6-10 unchanged. Units total 46-85 -> 35-63 for U4-U7. Ratio measured on 0041: about a sixth of the raw
figure for new module code with offline oracles [L, tracker hours].

**Re-scored 2026-11-16 01:05 (lead-0374, at epic 0374's MERGE-REQUEST; U5 built there, `unit-controller.md`):**
U5 15-28 raw -> done in about 14.5 agent-h (workers 9.75 h over 4402-4410, tracker hours; the two leads about 4.7 h).
What is left of U5 is listed in `unit-controller.md` 16 (the type-3 ease at a second point, K6's skip in combat, the
From side of a fade beyond one point, fortify / embark / aim captures). For **U6** (ticket 0375) the epic adds four
inputs the feed must carry or the server must time: the damage event's frame (the removal reaches the picture 1-3
frames after the Lua call, `unit-controller.md` 15.9), the order's frame for `ActionSkip`, path options 7 and 8 for
`steer` (`unit-feed-contract.md` 8b), and the per-figure run-start phase (4405 B's `runstart.csv`); U6 14-24 -> 10-18
unchanged in size, with these named. **U7** 6-10 unchanged: the live comparison now has a scripted capture path
(`tools/scenario` additions of 4405, `unit-controller.md` 8.4) and an offline oracle (`oracle_anim.py`) to compare
against. For **0042's C1-C3** (combat choreography): the move's shuffle / charge choice, the attacker's per-unit
skip (12 steps of 0.2 and 9 of 0.0 in combat not yet reproduced), the defender's death in combat (not caught), and
the run-start phases are its inputs. Units total for U4-U7 35-63 -> 20-35 (U5 done).

**U6 and U7 at epic 0375's last wave (2026-11-16 ~11:30 EST, recorded by W-4470 for lead-0375; the epic's own re-score
and hours are its merge's) [C, L as cited]:** U6 is built (`unit-server-scene.md` 0-11): the 21 calls as feed kinds
11-31 plus 32 / 33 with the unit, path, combat and player tables, records 3-6 in `feed.bin` (format version 4), the seed
allocator and the colour rule (the contract's 8a gaps closed), the scene pass and the reflection pass in the server's
frame, the pre-check by D125's route, the controller wired (one controller frame per drawn frame), the fog filter of
the drawn list, and `unitfeed` replaying a recorded game into `.c5units` and the controller's timeline with one
command (DoD I). U7 ran as DIAGNOSTIC (12.1, 12.2): the drawn set, placement and the seed against stock on Tiny, the
huge save at blob level. Of lead-0374's four U6 inputs above: the damage event's frame and the run-start phase are not
in the feed (the scene's damage is at the drain); the order's frame for `ActionSkip` and path options 7 / 8 are 0042's.
Left after 0375 (`unit-server-scene.md` 16): ~~the controller's cost per frame with every held unit (+30.5 ms on the huge
save, 3.4)~~ *(corrected 2026-11-16 by 0375's 4468: the controller steps only the drawn set; +5.33 ms [5.04, 5.62] per
frame on the huge save, oldpc job 20261116-151820-f8bb, grade A; `unit-server-scene.md` 3.4)*, units the controller fails that the EXE draws (`civilian_state_01` fortify 0, work without a work table,
6.1), pixels with forced poses and depth against the EXE's cities (DoD E). Worker hours recorded so far in the
children's `actual_h`: 19.75 h over 13 issues (tracker, 2026-11-16 11:30).

## 9. Not verified

- ~~(4151) Format 3 `DaConstant32f` in the orientation slot against a capture~~ **checked 2026-11-15 (4353, the
  fresh capture of epic 0041, unit-renderer.md 11.4) [L]:** a mech block fits `mech_idlea` at 3.6000 s on all 32
  bound bones with 0.00032 deg, four of them format 3 in the orientation slot; the mechs are drawn there. (Replaced:
  "only the mech uses it in idle and the catalogue frame does not draw the mech; synthetic tests only".)
- (4151) Degree 3 against a capture: one bone only (`Bone_Cab_02` of the SS booster, format 9); the tank's degree-3
  turret curves are in its fidgets, and the tank was idle in L09 (5.4). **2026-11-15 (4353):** still one bone per
  capture (`Mech01Pelvis` in mech idle A); the fresh capture's other mech blocks are idle B / fidget B cross-fades
  (idle B has 7 degree-3 bones) and its tanks were idle, so multi-bone degree 3 stays synthetic only.
- (4151) The ScaleShear transpose (`R * SS^T`): the only non-identity scale/shear met in a fit is a diagonal mirror
  (5.4), for which the transpose makes no difference.
- (4151) The cross-fade rule (1.4), the clock (1.5) and the blend-DAG / state machine are K4 / K6 matters.
  **2026-11-15 (4407, unit-controller.md 7.2-7.3):** partly measured on the old recordings. The clock steps 0.2 or 0 s
  per frame per unit under capture, on a 1/30 s lattice. The idle -> fidget cross-fade is local space with the old
  clip held, w = 0.680 at 0.2 s into the new clip. Idle -> run and combat-ready -> charge run show no fade on their
  first frame. The weight curve, the step rule outside the capture regime and the mech blocks wait for 4405's live
  sequences.
  **2026-11-16 00:54 (lead-0374, unit-controller.md 7.3 corrected, 8.10-8.11, 15.8):** the 4353 mech blocks are idle B
  under the aim layer (4410's track mask), not idle B / fidget B cross-fades: id 51 reproduced to 1e-6 [L]; id 46 is
  0.012 off in translation (open). In 4405 B's 51 live captures no switch was caught mid-fade: every two-clip blend fit
  converged to weight 1, so the From clip's time during a fade (X1, MC #2117) stays determined only on P01 4681 [L, one
  point]. The clock: see 1.5's correction.
