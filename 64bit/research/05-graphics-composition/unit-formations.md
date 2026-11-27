# Unit formations: where each member of a unit stands (4153, premise K5)

Written 2026-11-15 02:57-03:30 EST by worker W3 of epic 0327 (task 4153, N4 of the Architect's increment 3 sheet,
branch `task-4153-formations`), wave 1. Offline only: no game was run.

**Routes and marks.** `[C]` read in the EXE (read-only headless Ghidra 12.1.4 on a private copy of project `civ5`,
DX11 EXE SHA-256 `b2ce96ae...c88e`, build 4390913; llvm-objdump of the same copy; constants read from its `.rdata`
and `.data`), in the install's files or in the DLL's source. `[L]` measured: 3909's per-figure positions in 3907's
stock captures (below), compared with this rule by `models/tools/formations/formations.py`. `[I]` inferred.
Everything about the EXE is described in my own words (D83): no decompiler output, listing or EXE bytes are in this
file or in the repository; the scratch outputs stayed in `C:/c5-64bit-work/0327/4153/` and were deleted at the end.
Addresses are VAs at ImageBase 0x400000. No table of the XML's values is copied here; values appear as examples only.

**Inputs of the measurement.** 3909's `F:/c5-64bit-work/0301/3909/out/figures.json` (SHA-256 `b17f5a83...8e06`;
the per-figure version of `shell/evidence/0301/3909/units.csv` on branch `epic-0301-scene-harness`: per placed unit
the figure offsets from the plot centre, heading and height; a figure's position is the median of its weighted bones'
palette translations, `unit-draws.md` section 4). They come from 3907's catalogue: 26 RenderDoc exports, workstation,
c5h-c, 32-bit DLL `i0ship-26234371-ia`, CP/VP/EUI, stock `config.ini`, save `C5H-0301-CAT_0009` (Tiny, turn 9),
Vibrant Textures present, all units placed by 3907 with facing -1 (`scene-catalogue.md`, `unit-draws.md` section 7).
Install file: c5h-c's `Assets/Units/UnitFormations.xml` (SHA-256 `bf164b8a...df0b`). Database: the read-only copy
`F:/c5-64bit-work/0327/db/Civ5DebugDatabase.db` (SHA-256 `27fc81c6...19b4`, 2026-11-15 02:54).

## 0. Results in one screen

Since wave 2b (section 12, 2026-11-15): **the rule is exact.** With the figures' exact world positions from the
palettes (4151's relation), section 3's rule with facing 30 deg and the seed below places 134 of the 138
multi-figure units of the catalogue within 0.002 units per figure (the four others hold cross-fading figures,
<= 0.20) [L, 12.3]. **The seed is the index of the unit's art record** in the EXE's free-list table [C, 12.1], and it
matches all 138 units [L]. **Member heights:** z = the height-field sample at the member's slot with truncated
coordinates, raised to the water level, plus the z offset: within 0.001 units for 1083 of 1135 figures, including
197 on steep slopes; figures stay upright [C, L, 12.4]. The member scale is `ArtDefine_UnitMemberInfos.Scale`
exactly; T is the model origin; crew models' bone medians lie up to 56 units from it; VP's FBX-exported models are
drawn after an axis change (y up -> z up) without unit scaling [L, 12.2]. The Field Gun is explained, the Mamluk's
missing horse is not (12.5). Wave 1's figures below are kept; where wave 2b corrects them it says so.

- **K5 holds [C, L].** The formations are data in the install, and the rule that turns them into member positions is
  now known (section 3). With only the facing rotation and a translation fitted per unit, the rule's grid places
  the figures of the 122 multi-figure units that 3909 could count (969 figures, 19 formations) with a median error
  of 0.27-4.17 world units per formation, which is the size the formation's own `UnitRandomness` predicts
  (section 5.1: e.g. Phalanx 0.68 measured, 0.70 expected; Barbarian 2.77 and 2.87). With the random part reproduced
  too (the generator's seed fitted per unit from 64 candidates), the median error per figure is **0.00-0.40 units**
  and the p95 **0.09-1.13** for every formation except the gun crews (ThreeBigGuns p95 6.5) and Samurai (median 0.76,
  its random part is below the measurement noise) (section 5.2). The 58 single figures without a formation land
  within 0.03 units (median) of the Default record's slot. A plot is 64 units wide; a figure is about 10-20.
- **The 24 formations [C].** `UnitFormations.xml` holds 24 `<Formation>` entries (not 25; the 25th `<Formation` match
  is the root `<Formations>`, and "Melee Front" is one name, not two). The database's `ArtDefine_UnitInfos.Formation`
  uses 23 of them; "Melee Front" is unused. 64 rows (43 empty, 21 NULL) name none and get the EXE's built-in
  "Default" record (section 2). So the sheet's "24 formations" are 23 from the file plus the built-in one.
- **The facing [L, C for the default].** All placed units (DLL default facing `DIRECTION_SOUTHEAST`, set in
  `CvUnit` init) show the grid rotated by 30.0 degrees (median of 122 units; 28-32 for 97 of them, the rest are
  two- and three-part models and some cavalry at 25-27 and 33-39). The rows then run
  along 30 / 210 degrees and the files along 120 / -60 degrees, i.e. along the SE-NW axis: a unit facing SE looks
  towards its slots' -y side. Since wave 2 all six facings are known from the EXE: yaw = 150 - 60 x direction
  (section 11.2) [C].
- **Scale [L].** Fitting a scale as well gives a per-formation median of 0.997-1.004 for every formation: the
  spacings are used as written, global unit scale 1.0.
- **Members to slots [C].** The slots are always laid for all members of the unit; living members take the slots in
  member order, dead ones are skipped, so survivors close up into the first slots (front centre). Members die from
  the last one backwards.
- **Random part [C, L].** Each slot gets a uniform offset of up to `0.4 x UnitRandomness x spacing` on each axis,
  from a per-unit seed. In the catalogue the seeds are small integers (20-31) that follow the order in which 3907 made
  the units; the seed is the index of the unit's art record (section 12.1, wave 2b; replaced "what the EXE takes the
  seed from is not traced"). `BlockRandomness` is read but not used by the slot code.

## 1. Where the formations are defined [C]

- `Assets/Units/UnitFormations.xml`, loose in the install (13,710 bytes on c5h-c). An older copy with 21 entries
  (without OffsetPikeman, ThreeInLine, TheeInLineStag; the 21 shared entries have identical values) is inside the
  `UnitModels` pack (`F:/c5-64bit-work/0327/extract/fpk/UnitModels/unitformations.xml`, SHA-256 `4edcac45...96ea`).
  The EXE opens the loose path; which copy wins if both are visible is the general loose-file order of
  `asset-pipeline.md` 1.x [I: the loose one, same values for the units that exist in both].
- No VP mod in c5h-c's extraction overrides it (no other `*formation*.xml` under `F:/c5-64bit-work/0327/extract/`
  besides the pack copy). The three `CIV5MultiUnitFormations.xml` files are gameplay (armies), not this.
- Each `<Formation>` has 16 child elements: `Name` and the 15 parameters of section 4. Integers: `SpacingX`,
  `SpacingY`, `PerRowInc`, `YOffsetCurved`, `Stagger`, `TotalWidth`, `XBlockSize`, `XBlockSpacingCount`,
  `YBlockSize`, `YBlockSpacingCount`, `CenterWidth`. Floats: `SpacingMultiplier`, `UnitRandomness`,
  `BlockRandomness`, `YOffsetInc` (e.g. Melee Front's 8.8268) [C: the reader stores them with an integer and a
  float reader respectively, and the slot code loads them as integer and float].
- Units name their formation in `ArtDefine_UnitInfos.Formation` (VP database: DefaultCavalry 42 rows, UnFormed 32,
  TwoBigGuns 24, HonorableGunpowder 16, Archer 12, ...).

## 2. How the EXE reads them [C]

- At start, 0x79B200 reads `Assets\Units\UnitVisConfig.ini` (section `Debugging`, key `GlobalUnitScale`, default 100;
  the file is not in c5h-c's install), multiplies it by 0.01 into a global unit scale (0xBA2FF0) and calls the reader
  0x5560E0 on `Assets\Units\UnitFormations.xml` (string at 0xA21344). A second loader, 0x63CDF0 (from 0x6CAA50, the
  "LoadUnitLibraries via Database" path), calls the same reader with the path at 0xA263E4 when its flag is set and
  takes the unit scale from its caller. 0x649CA0 writes the table back as XML (a tool path; not used in play).
- The table is a global object at 0xBA2FC0: an array of 0x44-byte records with count at +4, a parallel array of
  32-byte names at +0xC, the formation generator's state at +0x18..+0x2C, the unit scale at +0x30 (= 0xBA2FF0) and the
  seed for the next layout at +0x34 (0xBA2FF4).
- The reader first adds a record named "Default" with built-in values: spacing 6 x 7, multiplier 1.0, unit and block
  randomness 0.2, `YOffsetInc` 1.2, curved, no stagger, width 4, block sizes -1, no centre width. Then one record per
  `<Formation>` in file order; fields missing in the file keep these defaults. Each record carries a hash of its name.
- Lookup is by that hash (0x6AFDB0 with a hash, 0x664410 with a string). **A name not in the table returns the first
  record, "Default"** [C]; 0x583090 also sets "Default" when a unit has no formation hash. So empty and NULL
  `Formation` values get Default.

## 3. The slot rule (0x4CB0A0) [C; checked against the captures in section 5]

The function takes the record, the member count n and an output array of n (x, y) pairs. In the slot frame x runs
across the rows and y along the files; the slots are rotated and moved afterwards (3.7). With sx = `SpacingX` x unit
scale x `SpacingMultiplier` and sy likewise from `SpacingY`:

### 3.1 Rows

- Row width W = `TotalWidth`; if it is below 1 or above 11, W = ceil(sqrt(n)) + 1 instead.
- Row r has W + 2 x `PerRowInc` x r slots, plus `Stagger` on odd rows (with `Stagger` 1 the odd rows change parity,
  which shifts their slots by half a spacing: DefaultCavalry's rows of 3, 4, 3, ...).
- Slots fill row 0 first, then row 1, and so on; row r sits at y = r x sy, plus (r div `YBlockSize`) x
  `YBlockSpacingCount` x sy (a block size of 0 means one block).

### 3.2 Order within a row: centre out, alternating sides

- Slot k (0-based within the row) lies at distance |x| from the centre line: for an odd row width 0, 1, 1, 2, 2, ...
  spacings for k = 0, 1, 2, 3, 4, ...; for an even width 0.5, 0.5, 1.5, 1.5, ... spacings.
- The side alternates with every member of the unit, **counted over the whole unit, not restarted per row**: the
  first member goes to +x, the second to -x, and so on. In an even-width formation every row starts on +x; in an
  odd-width one the rows alternate (HonorableGunpowder's second row of 7 is the mirror of the first) [C, L].
- Blocks: with `XBlockSpacingCount` = c > 0, the slots of a row are counted in groups of 2 x `XBlockSize` (that is
  `XBlockSize` per side, the sides alternating), and each group lies c x sx further out than the one inside it.
  `CenterWidth` = w makes the first w slots a centre group of their own: the first gap opens after them (group index
  +1 for k >= w). Without a centre width every slot also moves out by half a gap, which opens a gap of c spacings on
  the centre line itself. Example: Samurai (width 6, spacing 8, block size 2, gap count 1, centre width 2) puts its
  first row at +-4, +-20, +-28 [C, L: measured +4.1/-4.4, +20.5/-18.9, +28.8/-27.4 before the random part].

### 3.3 Row bend

- `YOffsetCurved` = 0: each slot moves along y by (its column distance index) x `YOffsetInc`, a straight V.
- `YOffsetCurved` != 0: the slot moves by (1 - cos(pi/2 x |x| / x_outer)) x ceil(W_r / 2) x `YOffsetInc`, where
  x_outer is the outermost slot's |x| in that row (with blocks: computed with the current slot's own block gap):
  zero in the centre, the full ceil(W_r / 2) x `YOffsetInc` at the
  ends, a smooth bow. Only zero / non-zero matters; the value itself (1 or 30 in the file) is not used [C].
- The bend is always towards +y (the offset's absolute value is taken). With the files pointing backwards from the
  front (3.7), the centre of each row leads and the wings trail.

### 3.4 Random part

- Per slot, two numbers u1, u2 in [0, 1) from the generator; x += (u1 - 0.5) x 2 x 0.4 x `UnitRandomness` x sx and
  y += (u2 - 0.5) x 2 x 0.4 x `UnitRandomness` x sy. So the offset is uniform within +-0.4 x `UnitRandomness`
  spacings on each axis (Warrior, 0.4 at spacing 13: +-2.08 units) [C].
- The generator: the 32-bit multiply-add step of the C runtime's `rand` (x 214013 + 2531011, mod 2^32), then the state
  is reduced modulo 0xFFFFCFEF and u = state / 0xFFFFCFEF (constants at 0xBA2FE4 / 0xBA2FE8, static data) [C].
  A consequence seen in the data: from a small seed the first u is below 0.004, so a single figure's x part sits at
  its lower bound (section 5.3) [C, L].
- `BlockRandomness` is stored in the record but the slot function does not read it; no other user was found among
  the table's readers [C for the slot function, I for the rest].

### 3.5 Centring

- After all slots, the function takes the bounding box of all slots **together with the origin** (its running
  minimum and maximum start at 0) and subtracts the box's centre from every slot. For a symmetric formation that is
  its middle; for one member it puts the member halfway between its slot and the unit's centre (Default's single
  figure ends 1.25-1.5 units from the plot centre, not 3) [C, L].

### 3.6 The seed

- Before each layout the caller copies a per-unit value (field +0x30C of the EXE's 0x344-byte per-unit art record)
  to 0xBA2FF4; the slot function resets the generator to it unless it is -1, in which case the generator runs on from
  the previous layout [C].
- Measured [L]: for 93 of the 121 multi-figure units with a random part, one seed in 0..63 reproduces the figures
  with less than half the error of any other seed. In each catalogue frame these seeds are integers 20-33 that follow
  3907's object number: ascending (seed = object + 19) in L01, L03, L05, L07, L09, L11, L13, descending (seed = 33 -
  object, the first unit often 20) in L02, L08, L10, L12, L14, with a few exceptions
  (`models/evidence/0327/4153/seeded64/per_unit.csv`).
- What the EXE stores there: the index of the unit's art record in its free-list table [C], section 12.1 (wave 2b;
  replaced "not traced ... the index part of the DLL unit id or the EXE's own record index are the candidates [I]").
  The "few exceptions" above came from the bone-median positions; with exact positions there are none (12.1).

### 3.7 Into the world

- 0x7758E0 rotates every slot about +z by the unit's facing angle (D3DX rotation about z) and adds the unit's
  position [C]. Measured rotation for the DLL's default facing (south-east): 30.0 degrees [L], so local -y points
  to -60 degrees (south-east, y north) and local +x to 30 degrees. Units therefore face their local -y: row 0 is the
  front row, the files run backwards, and the yaw is the hex direction's compass angle + 90 degrees [C since wave 2:
  the EXE's direction-to-angle function, section 11.2; the figures' own x axes lie at 20-40 degrees, `unit-draws.md`
  7, parallel to the rows].
- Heights: 0x6C67C0 gives each member the terrain height sampled at its own slot (0x530A10, a lookup in the EXE's
  height field by grid cell), raised to a floor value of the unit layer, plus the member's z offset (member info
  +0x144, `ArtDefine_UnitMemberInfos.ZOffset` [I]) [C]. So members on a slope stand at different heights. Wave 2b
  (12.4): the lookup truncates the sample coordinates, the floor is the water level, and this reproduces the
  catalogue's figure heights within 0.001 units [C, L].

## 4. What each parameter does (summary)

| Parameter | Effect | Route |
|---|---|---|
| `SpacingX`, `SpacingY` | slot spacing across rows / between rows, world units (integers) | C, L (scale 1.000) |
| `SpacingMultiplier` | multiplies both spacings (EarlyGreatMerchant's 1.39252) | C |
| `TotalWidth` | slots per row; outside 1-11 -> ceil(sqrt(n)) + 1 | C, L |
| `PerRowInc` | each further row is 2 x this wider (0 in every stock entry) | C |
| `Stagger` | odd rows one slot wider, which shifts them half a spacing | C, L (DefaultCavalry) |
| `YOffsetInc` | depth of the row bend at the row's ends | C, L |
| `YOffsetCurved` | 0: straight V bend; non-zero: cosine bow (value otherwise unused) | C, L |
| `XBlockSize`, `XBlockSpacingCount` | split the row into blocks of 2 x size slots with gaps of count spacings | C, L (Samurai) |
| `YBlockSize`, `YBlockSpacingCount` | the same between groups of rows | C |
| `CenterWidth` | first w slots form a centre block; the gaps start after it | C, L (Samurai) |
| `UnitRandomness` | uniform jitter +-0.4 x value x spacing per axis, per member | C, L (5.1) |
| `BlockRandomness` | read, not used by the slot code | C |
| `Name` | looked up by hash; unknown or empty -> built-in Default | C |

## 5. The rule against the captures [L]

Command (2026-11-15 03:22): `python -I -X utf8 models/tools/formations/formations.py compare --xml
<c5h-c>/Assets/Units/UnitFormations.xml --db F:/c5-64bit-work/0327/db/Civ5DebugDatabase.db --figures
F:/c5-64bit-work/0301/3909/out/figures.json --out models/evidence/0327/4153/seeded64 --fit-seeds 64` (and
`--fit-seeds 0` into `grid_only/`). Per unit: the rule's slots for the unit's member count, the best assignment of
slots to figures (Hungarian, needed where a unit's members use several models and 3909's figure order follows the
draws), then the best rotation and translation; error = distance per figure in world units. Of 229 placed units:
122 multi-figure units compared, 80 single figures, 22 whose figure count differs from the members (3909's cases,
section 6), 5 without figures.

### 5.1 The grid alone (rotation and translation fitted, no random part)

| Formation | Units | Figures | Median | p95 | Max | Median expected from `UnitRandomness` alone |
|---|---|---|---|---|---|---|
| DefaultMelee | 6 | 72 | 0.78 | 1.44 | 1.54 | 0.83 |
| Phalanx | 6 | 72 | 0.68 | 1.10 | 1.20 | 0.70 |
| Pikeman | 3 | 36 | 0.66 | 1.02 | 1.28 | 0.67 |
| Warrior | 1 | 12 | 1.43 | 2.31 | 2.35 | 1.66 |
| Samurai | 1 | 12 | 0.53 | 1.47 | 1.82 | (0.6) |
| OffsetPikeman | 1 | 12 | 0.76 | 1.28 | 1.36 | (0.7) |
| Archer | 12 | 140 | 1.47 | 2.67 | 3.65 | 1.67 |
| Barbarian | 4 | 46 | 2.77 | 5.08 | 5.19 | 2.87 |
| HonorableGunpowder | 11 | 154 | 0.43 | 0.86 | 1.76 | 0.33 |
| DefaultCavalry | 30 | 136 | 1.02 | 1.95 | 3.69 | 1.14 |
| Scout | 4 | 32 | 2.30 | 3.72 | 3.88 | 2.38 |
| UnFormed | 24 | 162 | 2.04 | 3.74 | 5.68 | 2.15 |
| LooseCivilian | 2 | 16 | 4.17 | 5.13 | 5.25 | 3.33 |
| Vehicle | 5 | 14 | 0.76 | 1.48 | 1.68 | 0.93 |
| TwoBigGuns | 9 | 17 | 0.27 | 1.97 | 1.97 | |
| ThreeBigGuns | 7 | 21 | 1.07 | 6.99 | 10.41 | |

ChariotElephant, FighterWing and EarlyGreatArtist (1-7 units, 6-13 figures) are in `grid_only/per_formation.csv`.

The expected median is that of a uniform +-0.4 x `UnitRandomness` x spacing offset per axis (Monte Carlo, 20,000
draws); the fit absorbs a little of it, so the measured medians lie slightly below. The residual is the random part.

### 5.2 With the random part (seed fitted per unit from 0..63)

| Formation | Median | p95 | Max |
|---|---|---|---|
| DefaultMelee | 0.16 | 0.52 | 1.26 |
| Phalanx | 0.17 | 0.31 | 0.54 |
| Pikeman | 0.25 | 0.42 | 1.68 |
| Warrior | 0.16 | 0.19 | 0.19 |
| Samurai | 0.76 | 1.02 | 1.12 |
| OffsetPikeman | 0.37 | 0.70 | 0.80 |
| Archer | 0.40 | 0.79 | 1.16 |
| Barbarian | 0.28 | 0.55 | 0.58 |
| HonorableGunpowder | 0.28 | 0.72 | 1.20 |
| DefaultCavalry | 0.17 | 1.13 | 1.39 |
| Scout | 0.15 | 0.43 | 0.47 |
| UnFormed | 0.35 | 1.04 | 4.67 |
| LooseCivilian | 0.13 | 0.22 | 0.22 |
| Vehicle | 0.00 | 0.15 | 0.16 |
| FighterWing | 0.04 | 0.19 | 0.20 |
| ChariotElephant | 0.07 | 0.15 | 0.15 |
| TwoBigGuns | 0.03 | 0.09 | 0.09 |
| ThreeBigGuns | 0.28 | 6.48 | 8.27 |

The remaining error is the measurement's own (a figure's position is a bone median, a few tenths of a unit off its
root for most meshes). Samurai's and HonorableGunpowder's random part (0.2 at spacing 8 x 13; 0.1) is below that
noise, so their seed search picks by chance (no clear winner); their grid-only figures are the honest ones.
UnFormed's 4.67 is FCompany (7 member types, a mixed draw order). Rotation fitted: 29.9-30.3 degrees median per
formation (ChariotElephant 36, ThreeBigGuns 34: two- and three-part models). Scale fitted (seeded): 0.997-1.004.

### 5.3 Single figures (fixed facing 30.0 degrees, no translation fitted)

- 58 units without formation (ships, missiles, spaceship parts, great people, Default record): median error 0.03,
  p95 10.7, max 36.5 units (grid only, without the random part: median 0.32). Ships sit 1.25 units along the rows
  from the plot centre and -0.4 to +0.35 across: the Default
  slot (+3, about 0) halved by the centring (3.5), minus the random part's lower bound in x (3.4) [C, L].
- The outliers are models whose bone median is far from their root, not formation: generals (11.8), late scientist
  and merchant (12.6, 10.3), modern general (36.3), great bombard (13.7) [I].
- 4 bomber wings 0.06 median, 1 fighter 0.10, 1 merchant 0.80, the DefaultCavalry singles (cargo ships, admirals)
  0.42 median [L].

## 6. 3909's open cases

- **Mamluk (L06) [L, explained]:** DefaultCavalry, 8 members; the 15 figures are 8 riders (mesh 694) and 7 horses
  (mesh 693). The 8 riders fit the 8-slot grid (median 1.22 without the random part), the 7 horses fit slots 0-6
  (median 0.86); the last member's horse is not drawn in that frame. Why one horse is missing is not explained.
- **Huns battering ram (L11) [L, explained]:** one member drawn as two "figures" 0.1 units apart (meshes 549 and
  550-554): one figure of six meshes that 3909's mesh-chaining rule split in two.
- **Field Gun (L03):** explained in wave 2b (12.5): 12 mesh-instances, 3 figures, the rule fits exactly; the "8
  mesh-instances spread over 56 units" were bone medians of the crew meshes. (Was: "not explained".)
- **Gun crews in general [L]:** the crews' bone medians lie 22-56 units from the model origin (12.2); with exact
  positions ThreeBigGuns fits to 0.001 units. The cavalry "shift of about 11 units" does belong to the models, as
  supposed: their horse meshes' bone medians lie 10.7-13 units from the origin (12.2) [L, was I]. On top, the
  five-member layout of seed 31 (Knight, Black Tug, Qizilbash, Hakkapelitta, camel archer, Lancer) was mis-fitted by
  the free rotation; with the facing fixed they fit to 0.000 (12.3). The Mamluk's missing horse stays open (12.5).
- Not compared: ThreeInLine and TheeInLineStag (the caravans: 3 members drawn as one chained "figure" by 3909's rule),
  the other single-figure mixtures of 3909's case (a), and the units without figures (Marine, Mech, Mechanized
  Infantry, Merc, modern cargo ship). *(2026-11-17, ticket 0502 [L], `unit-server-scene.md` 18.2: a modern caravan on a
  trade route is drawn by the EXE with its three members (box van, sack truck, plank trailer) in `TheeInLineStag` at the
  DLL's facing, the same as off a route; that formation's `UnitRandomness` 1 at spacings 22 / 23 moves each member by up
  to about 9 units, so a caravan shows a wrong seed more than any other unit. 12.3 compared TheeInLineStag exactly.)*

## 7. Members, deaths and re-layout [C]

- 0x6C67C0 (whole unit, from 0x583090) and 0x42D1E0 (a second caller with the same pattern) look up the record by
  the unit's formation hash, set the seed, call the slot function for **all** n members, rotate and move the slots,
  then walk the members in order: each living member (health field non-zero) takes the next slot, dead ones are
  skipped and keep their place. A single member can also be placed alone (the facing-only branch).
- 0x4B4B80 handles a health change: the number of dead members becomes round(n x percent / 100) (at most n - 1 unless
  the percent is 100); extra deaths are taken from the last member backwards, revivals from the first dead one
  forward; then 0x583090 lays the unit out again if anyone lives. So survivors close up into slots 0..m-1: the front
  row's centre first, still on the n-member grid [C structure, I for the percent being the damage].

## 8. What the server needs, and wave 2

The server can place members from the install's file with this rule (`formations.py` `slots()` is a reference
implementation of section 3, written from this description). Inputs per unit: formation name (art define), member
count, alive flags in member order, position, facing angle, and the seed. Still open for wave 2:

1. The seed's source (3.6): one static trace of who fills the art record's +0x30C at unit creation (1-2 h). Without
   it the server would pick its own seeds: same look, different jitter (+-0.4 x `UnitRandomness` spacings).
2. Facing for the other five directions and the angle convention (only SE observed): a catalogue frame with units
   turned, or the EXE's facing-to-angle code (1-2 h static, or one capture).
3. Heights on sloped plots: the per-slot terrain sample (0x530A10) against the EXE's height field, rule R4 / D132,
   with the catalogue's hill plots (2-4 h offline once the height field reader of `asset-pipeline.md` is used).
4. Scale and facing for the rest of the catalogue: member `Scale` and `ZOffset` from `ArtDefine_UnitMemberInfos`
   against the palette's scale (3909's figures carry it), the model-specific root offsets of section 6 (crews,
   cavalry) from the `.gr2` skeletons rather than bone medians (3-5 h).
5. The Field Gun case and the missing Mamluk horse (1 h, may need the capture's draw list).

Estimate for wave 2: 8-12 h raw, 4-7 h expected. (All five done in waves 2 and 2b: sections 11 and 12. The server's
inputs per unit are now complete: formation name, member count, alive flags, position, facing direction, the art
record index as the seed, and the height field with its water level.)

## 9. Corrections to other files (for their owners)

- (Done in wave 2 with the MC's leave, section 11.3.) `combat-choreography.md` (0321) section 6 and its README line said 25 `<Formation>` entries and list "Melee" and
  "Front" separately: there are 24 entries, one of them named "Melee Front"; `ArtDefine_UnitInfos.Formation` uses 23
  names, and the 64 rows without one get the EXE's built-in Default record (this file, sections 0-2).
- `unit-draws.md` (0301) section 7: the Mamluk and battering-ram cases are explained here (section 6); the "12-figure
  grid ... the swordsman's grid slightly wider" is DefaultMelee (spacing 13) against the legion's Phalanx (11).

## 10. Not verified

- Only one facing (SE) was measured (the other five are read from the EXE, 11.2). Slopes: measured in wave 2b (12.4).
- The meaning of the health percent in 7, the member z offset's database column (no catalogue member has one).
  (The seed source is traced since wave 2b, 12.1.)
- The code that converts VP's FBX-basis models (12.2); why the Mamluk's last horse is not drawn (12.5).
  *(2026-11-15, issue 4354: the common shift of about 0.07 units is explained, `unit-renderer.md` 12.2: the EXE's
  hex-to-world matrix; was "The common shift of about 0.07 units between the plot centre and the unit's position
  (12.3); ...".)*
- Whether 0x42D1E0's caller differs in what it does with the result (it follows the same pattern).
- BlockRandomness: unused in the slot function; a use elsewhere was not searched exhaustively.

Files: tool `models/tools/formations/formations.py` (+ `test_formations.py`), since wave 2b also `exact_t.py` and
`heights.py` (+ `test_exact_heights.py`; 34 tests in the folder), evidence `models/evidence/0327/4153/` (wave 1:
`grid_only/`, `seeded64/`; wave 2b: `exact_t/`, `exact_fixed30/`, `exact_free64/`, `exact_grid_only/`, `heights/`;
`README.txt` lists them).

## 11. Wave 2 (2026-11-15 03:32-03:41 EST, partial; see 11.4 for what is left)

### 11.1 The seed [L, I; static trace not completed]

- The seeds do not restart per owner: in frame L15 (owners 0, 1, 2, 3, 22, 24 mixed) the warrior and worker of owner
  0 have seeds 21 and 22 (objects 2, 3), and in L16 the modern worker of owner 3 (object 3) has seed 30, which
  continues the same frame-wide sequence as owner 0's units elsewhere (seed = object + 19 or 33 - object). The DLL's
  unit ids are not small per-owner numbers (*corrected 2026-11-15, issue 4459 [C]: VP keeps units in a `TContainer`
  per `CvPlayer` whose ids come from one game-wide counter, `CvGame::GetNextGlobalID`, starting above 1000 and saved
  with the game, `TContainer.h` 181-190, `CvGame.h` 766; replaced "per player (`FFreeListTrashArray` per `CvPlayer`,
  id = 8192 x k + index)"*), so the seed is **not the DLL's unit id or its index part**; it is an index from one table shared by all owners, handed out in creation order
  with freed entries reused by the free rule of 12.7.2 (*2026-11-15, issue 4459; was "last-freed-first"*) [L for the
  pattern, C for the table since 12.1]. The likeliest table is the EXE's own
  array of 0x344-byte unit art records (manager +0xD0, the index the layout calls take as their second argument),
  which the EXE fills on `GameplayUnitCreated` (0x5579C0).
- The static trace of the first write to the record's seed field (+0x30C) did not finish in wave 2: the stores found
  (0x707540, called from 0x4737B0 and 0x501CD0) only copy an existing value back. Finished in wave 2b: 12.1 (the
  record initialiser 0x5B03E0 writes the record's own index) [C].
- **What the server must feed to get the same jitter:** an integer per unit that follows the EXE's allocation: one
  counter for all owners' units, fresh values ascending from the number of unit records the EXE holds after loading,
  freed values reused by the free rule of 12.7.2 (*2026-11-15, issue 4459; was "last-freed-first"*). A server that
  owns its own unit table can reproduce that exactly only if it
  creates and removes art records in the EXE's order; otherwise any per-unit integer gives the same look with a
  different jitter (+-0.4 x `UnitRandomness` spacings per axis).

### 11.2 The six facings [C, L]

- 0x503880 turns a DLL `DirectionTypes` value into the yaw in radians with a six-way switch: NE (0) 150 deg, E (1) 90,
  SE (2) 30, SW (3) 330, W (4) 270, NW (5) 210; any other value gives 1.0 rad (57.3 deg). It is called from
  `GameplayUnitCreated` (0x5579C0). So yaw = 150 - 60 x d (mod 360) [C].
- This matches every measurement: SE (2, the DLL's default facing) gives 30.0 deg in 3909's 122 units (section 5) and
  30.000 deg in 4151's palette fits of 3908's P01 (units placed with facing 2, `unit-playback.md` 1.1) [L].
- With section 3.7's rotation, a unit's local -y (row 0, the front) then points to the compass angle of its direction
  (NE 60, E 0, SE -60, SW -120, W 180, NW 120 deg, y north) for all six: units face their direction and the files run
  away from it [C for the angles, I for "front" = the side the models face, consistent with the figures' x axes
  lying along the rows]. In the tool: `facing_angle(d)`.

### 11.3 Correction made (granted by the MC)

- `combat-choreography.md` section 6 and its README line corrected in place (24 entries, "Melee Front" one name, 23
  used, empty / NULL -> Default), with dated notes and the replaced wording.

### 11.4 Left for a further session (not started here; the session was near its context limit)

1. The seed's first write (11.1): find the handler that allocates the art record after `GameplayUnitCreated` and
   check whether the seed equals the record's index (1-2 h static).
2. Heights on sloped plots (rule R4 / D132): each slot's terrain sample (0x530A10, a height-field lookup by grid cell,
   raised to the unit layer's floor, plus the member's z offset) against the height field
   (`render/include/c5r/heightfield/heightfield.h`, 3901's plot relation) on the catalogue's hill plots (2-4 h).
   *(2026-11-15, issue 4354: for plot centres use HexToWorld with the tap's hex-to-world matrix, not
   `heightfield.h`'s nominal relation; the two differ by up to 0.12 units, `unit-renderer.md` 12.2.)*
3. Exact figure positions T from the palettes with 4151's relation `[Rz R_k | s Rz t_k + T]` (pose sampler in
   `models/tools/posefit`), replacing 3909's bone medians: this removes the crew and cavalry root offsets of section 6
   and gives member `Scale` (s) and `ZOffset` per catalogue unit (3-5 h).
4. Field Gun and the missing Mamluk horse with exact T (at most 1 h; needs item 3).

(All four done in wave 2b, section 12.)

## 12. Wave 2b (2026-11-15 03:47-, worker W3b, branch `task-4153b-formations`)

Offline. Routes as at the top of this file; the static reading of the EXE this time was an `llvm-objdump` listing of the
analysed EXE copy (`%USERPROFILE%/tools/ghidra-projects/bin/CivilizationV_DX11.exe`, SHA-256 `b2ce96ae...c88e`) in
`C:/c5-64bit-work/0327/4153/` (deleted at the end); nothing of it is in the repository (D83).

### 12.1 The seed is the index of the unit's art record [C] (closes 3.6 and 11.1)

- The unit system keeps its per-unit art records in a free-list array, the container at unit manager +0xC8: free-list
  head +0xC8 (0x0FFFFFFF = empty), live count +0xCC, array +0xD0, size +0xD4, capacity +0xD8, then a list of the
  live records in creation order (count +0xDC, first +0xE0, last +0xE4). An element is 0x344 bytes: +0 the next
  free index, +4 bit 31 "in use", +8 the record proper (0x33C bytes; the seed is its +0x304, element +0x30C).
  *(2026-11-16, issue 4453 [L], 12.7.6: the container is the UnitSim static object's (VA 0x2D6F5F8, rebased with the
  EXE's load address: 0x4A0000 in a pool instance); the unit's key is at element +0x2F4 (unit id) and +0x2F8 (owner),
  both int32.)*
  *(2026-11-15, issue 4459 [C]: while a record is live, +0 is its next and the low 31 bits of +4 its previous index
  in the live list; +0 becomes the free-list link when it is freed.)*
- Allocation (0x7AF700): with an empty free list the new record is appended (index = the array's size; the array
  grows when full), otherwise the free list's head is taken. Fresh indices therefore ascend from the number of records
  already held. *(Corrected 2026-11-15, issue 4459, section 12.7.2 [C]: a freed record goes to the head **only when
  its index is lower than the current head**, otherwise it is linked in right behind the head (0x546560); replaced "a
  freed record goes to the head ... freed ones come back last-freed-first".)*
- 0x56F510 builds a default record, allocates it through 0x7AF700, appends it to the live list and returns its index.
  Its caller 0x5B03E0, the handler that fills a new unit's record from the creation message (owner, unit id, art
  type and so on; *corrected 2026-11-15, issue 4459, 12.7.1 [C]: 0x5E527C and 0x6BCC0C are where the Unit system
  subscribes and unsubscribes this handler for the unit-created message; the busy path is inside 0x5B03E0 itself
  (bit 0 of +0x128 set: the call is deferred to the queue at +0x118); replaced "its address is also queued from
  0x5E527C and 0x6BCC0C for when the system is busy"*), writes that
  same index into the record's seed field (+0x304 of the record proper). The other writers do not set it at creation:
  0x583090 (the re-layout) replaces it only when its caller passes a non-zero seed (its callers pass 0, or the
  record's own value back), and 0x707540 copies records.
- So **seed = the index of the unit's art record in the EXE's table** [C; L by the reader, 12.7.6]. The catalogue
  agrees [L]: *(2026-11-16, issue 4453: a fresh load of the save gives 22 records, its 22 units, 12.7.6 (1); the "0-19"
  below is the fit's, not the save's count)* after loading `C5H-0301-CAT_0009` the table holds records 0-19, 3907's units of a frame take 20, 21, ... in creation order
  (seed = object + 19), and in the frames whose predecessor's units were removed first the freed indices come back
  in the order the free rule of 12.7.2 leaves them (seed = 33 - object), section 3.6's observation.
- For the server: one table for all owners' units with this allocation (append when no slot is free, else take the
  free list's head; a freed index becomes the head only when it is lower than the head, else it goes right behind
  it, 12.7.2), records created and removed in the order the EXE **dispatches** the unit-created and unit-removed
  messages (not always the DLL's call order, 12.7.4), and the index passed as the seed. Which index each of a save's
  own units gets at load: 12.7.3 (*2026-11-15, issue 4459: settled from the DLL fork's load path [C] and the EXE's
  posting [C]; replaced "follows the order in which they are created at load [I: not traced]"*).
- With the exact positions of 12.3 every one of the 138 multi-figure units' best seed is the predicted index, no
  exception [L]: in L01, L03, L05, L07, L09, L11, L13, L15 seed = object + 19; in L02, L04, L06, L08, L10, L12, L14,
  L16 object 1 gets 20 and object k >= 2 gets 33 - k. With the free rule of 12.7.2 that is exactly what follows when
  3907's runner removes a frame's units in object order 1-12 (`tools/scenario/lua/clear.lua` kills the ids in the
  order `runner.py` lists the frame's objects), and the next frame's removal in object order gives the ascending
  numbers back [C for the rule, L for the fit]. *(2026-11-15, issue 4459: replaced "With last-freed-first reuse that
  is what follows when 3907's runner removes a frame's units in the order objects 2-12, then object 1 [I for the
  removal order]".)* (Wave 1's "few exceptions" came from the bone medians.)

### 12.2 Exact figure positions from the palettes [L] (item 1)

- **Tool** `models/tools/formations/exact_t.py` (own code; imports 4151's `models/tools/posefit` unchanged). For each
  instance of each main-pass unit draw (3909's selection: unit program, pass 0, not the 1024 x 1024 reflection
  target) it reads the instance's t27 block from 3907's RenderDoc export (`buffers/<res>_<sha>.bin` of the draw's
  t27 view, the hash 3909 recorded; the same numbers 3908's `bones.py scan` reads from a zip.xml conversion, which
  exists for only 7 of the 26 catalogue captures), finds the placed unit (3907's index, nearest plot centre within
  100 units of 3909's bone-median origin, members of that unit whose model has a mesh of the draw's index count),
  and fits unit-playback.md 1.1's relation: every idle clip of the member (ec 1000/2000/1020/2020/1040/2040; all clips
  if the best leaves more than 0.05 deg), t on a 0.02 s grid, golden-section refine, rotation by Procrustes, s fixed
  to the member's `Scale` (free s reported as a check), T by least squares. T must lie within 64 units of the unit's
  plot. Meshes of one or two bound bones get the placement yaw fixed at 30 deg (with a free rotation one bone fits any
  pose). Run (4 processes, about 10 min): `exact_t.py --figures F:/c5-64bit-work/0301/3909/out/figures.json --units-dir
  F:/c5-64bit-work/0301/3909/out/units --db <db copy> --manifest F:/c5-64bit-work/0327/extract/gr2_manifest.tsv
  --fpk-root F:/c5-64bit-work/0327/extract/fpk --install <c5h-c>/install --mods <c5h-c MODS> --out DIR --step 0.02
  [--only FRAME,...]`; then `formations.py compare --figures DIR/figures_exact.json --fixed-facing 30`.
- **Fit quality** (2026-11-15 04:30, all 20 unit frames of the catalogue, 1586 mesh-instances): 1535 fitted, 1520
  with rotation error below 0.05 deg; over those the median rotation error is 0.0003 deg and the translation error
  0.0001 units (p95 0.005 deg, 0.0009 units). The 15 others are cross-fades (1.4 of unit-playback.md) and a few
  aircraft and hulls. Not fitted: 24 instances of a 78-index mesh of the swordsmen (an attachment not in the members'
  models) and 27 whose idle clips all hold a curve format that posefit's decoder rejects (4151's unchecked formats:
  the caravans' camels, the early great scientist, SS booster, battering ram, B29 and five sailing ships;
  `instances.csv` names the clip and bone).
- **Scale [L].** The free s equals the member's `ArtDefine_UnitMemberInfos.Scale` on every block with enough lever
  (1374 blocks whose bones spread more than 5 units): median relative deviation 4e-9, maximum 2.1e-4.
- **Placement [L].** Yaw 30.0001 deg median (|yaw - 30| p95 0.0007 deg), tilt 0 (the figures stay upright on every
  slope; maximum tilt among the 1520 is from few-bone callout meshes). T is the model's origin: the bone median 3909
  used lies 0.69 units from T in the median, p95 15, at most 56: the gun crews (Parrott crews R and F 36-56 units,
  Gatling crew F 36, Assyrian siege tower crew 36, the cannons and their captains 22-27) stand far from their model's
  origin, which was wave 1's "13-36 unit shift" of the crew models; the horse meshes of several cavalry models
  (Hakkapelitta, Mamluk, Lancer, Qizilbash, Knight_Turkey, Sipahi: 10.7-13 units) give the "about 11 units" of
  section 6 (3909 took a figure's position from its mesh with the most bones, often the horse). Separately, the
  five-member DefaultCavalry layout of seed 31 made formations.py's free-rotation fit land in a wrong minimum
  (angle 25.6 deg instead of 30, 12.3).
- **Files in another basis [L].** VP's FBX-exported models (e.g. `Mamluk.gr2`: art tool "FBX", y up, back +z,
  `UnitsPerMeter` 1) are drawn with the stock clips (Max: z up, back -y, 39.37 units per metre) once the model's axes
  are brought to the stock ones **without** the unit scale: with that, Mamluk's rider fits idle B at 2.634 s with
  rotation error 0.000 deg, s 0.1200; with no conversion the best is 125-172 deg off, with axes and units 6-177 deg.
  The tool does this (`--basis axes`, the default: X' = M X M^-1 for rest transforms and inverse world transforms,
  M = the axis change). Bone-to-track matching is by exact name (ignoring case and spaces gives 0.19 deg, worse).
  This concerned the VP models of Black Tug, Chewa, Cacador, Commando, Grenzer, Klepht, Goedendag, Inti maceman,
  Heavy skirmisher, Dhanurdhara, Emissary and Mamluk (255 blocks that failed before). Which EXE code converts is not
  traced [I: Granny's basis conversion at load, with the art tool's unit scale ignored or set to the stock value].
- **Clips with several track groups [L].** The hakkapelitta clips hold a one-track group `Point01` before the
  skeleton's group; posefit reads the first group only, exact_t.py reads all (first match wins).

### 12.3 The formation rule on exact positions [L]

`formations.py compare --fixed-facing 30` (new option: the facing is known, only the slot order (Hungarian) and one
common shift are fitted; seeds 0-63): evidence `models/evidence/0327/4153/exact_fixed30/` (and `exact_free64/`,
`exact_grid_only/` with wave 1's free rotation for comparison). Error per figure in world units after the common shift:

| Formation | Units | Figures | Grid only: median / p95 / max | With the seed: median / p95 / max |
|---|---|---|---|---|
| Archer | 12 | 140 | 1.44 / 2.57 / 2.98 | 0.000 / 0.000 / 0.002 |
| Barbarian | 4 | 46 | 2.81 / 5.07 / 5.33 | 0.000 / 0.000 / 0.000 |
| ChariotElephant | 7 | 13 | 1.43 / 2.40 / 2.40 | 0.000 / 0.000 / 0.000 |
| DefaultCavalry | 36 | 164 | 1.10 / 2.25 / 2.53 | 0.000 / 0.006 / 0.204 |
| DefaultMelee | 6 | 72 | 0.83 / 1.23 / 1.44 | 0.000 / 0.000 / 0.000 |
| FighterWing | 5 | 13 | 1.14 / 1.83 / 1.89 | 0.000 / 0.097 / 0.134 |
| HonorableGunpowder | 11 | 154 | 0.32 / 0.49 / 0.56 | 0.000 / 0.000 / 0.000 |
| LooseCivilian | 4 | 34 | 3.16 / 5.21 / 5.31 | 0.000 / 0.000 / 0.000 |
| Phalanx | 6 | 72 | 0.64 / 0.97 / 1.08 | 0.000 / 0.000 / 0.000 |
| Pikeman, OffsetPikeman, Warrior | 5 | 60 | 0.59-1.44 median | 0.000 / 0.000 / 0.001 |
| Samurai | 1 | 12 | 0.64 / 0.97 / 1.14 | 0.014 / 0.077 / 0.154 |
| Scout | 5 | 38 | 2.46 / 3.47 / 3.91 | 0.000 / 0.000 / 0.000 |
| ThreeBigGuns | 7 | 21 | 1.38 / 3.08 / 3.66 | 0.000 / 0.001 / 0.001 |
| TwoBigGuns | 9 | 17 | 1.57 / 2.43 / 2.43 | 0.000 / 0.000 / 0.000 |
| UnFormed | 24 | 162 | 2.07 / 3.45 / 4.02 | 0.000 / 0.013 / 0.145 |
| Vehicle | 6 | 17 | 1.01 / 1.62 / 1.68 | 0.000 / 0.000 / 0.000 |
| EarlyGreatArtist, EarlyGreatMerchant | 6 | 16 | 0.000 / 0.000 / 0.000 | 0.000 / 0.000 / 0.000 |
| TheeInLineStag (modern caravan) | 1 | 3 | 6.75 / 10.09 / 10.46 | 0.000 / 0.000 / 0.000 |

- **K5 is exact [L].** 134 of the 138 multi-figure units are reproduced to within 0.002 units per figure by section
  3's rule with facing 30 deg and the seed of 12.1. The four others (Huns horse archer 0.20, Samurai 0.15, the
  F-Company 0.15, the Zero wing 0.13) contain the cross-fade blocks of 12.2, whose T is a few tenths off. The 71
  single figures land within 0.02 units (median; p95 0.05, max 0.13) of the Default record's slot.
- **Common shift [L].** Every unit's slots sit (-0.05, +0.05) units (median; range x -0.08..+0.08, y +0.005..+0.12)
  from the plot centre as `heightfield.h` computes it. *(2026-11-15, issue 4354: explained [L]. The EXE's plot
  centre is HexToWorld with the map's hex-to-world matrix, which the C5HF tap carries and which is not the nominal
  64 / 32 / 128 lattice (63.9986, 64.0019, 128.0033, ...); it reproduces every unit's shift to 0.0004 (median), and
  the C++ rule then places 129 of 138 units within 0.002 with no shift fitted, `unit-renderer.md` 12.2-12.3. Was:
  "the unit's position is not exactly that centre [I: a rounding in the EXE's plot-to-world conversion or in the
  float centring of 3.5; not traced]".)*
- The formations without a random part beyond the measurement in wave 1 (Samurai, HonorableGunpowder) now give
  their seeds too (HonorableGunpowder 0.000 max).
- Not compared: 5 units whose figure groups differ from the members (Caravan: its 4698/3126-index meshes do not
  fit, see 12.2; Machine gun, Missionary, XCOM squad: few-bone callout meshes grouped as extra figures; Pioneer:
  same meshes as the caravan) and 15 without fitted figures (the 4 of wave 1 without draws, and 12.2's not fitted).

### 12.4 Heights: slots on slopes, rule R4 / D132 [C, L] (item 2)

- **HeightAtWorld 0x530A10 [C]:** column = (x + ox) / sx and row = (y + oy) / sy, each truncated toward zero
  (no rounding, no interpolation), then HeightAtGrid 0x6D8B60 (u16 / 255 x sz; `heightfield.h`). In 0x6C67C0 each
  member's z is this height at the member's own slot position, replaced by the unit manager's floor (+0x5B4) when
  the floor is higher, plus the member info's +0x144 (the z offset).
- **Height field:** 0301's bundle `C5H-0301-TINY_0009_20261115-013528_workstation/heightfield/load0003.c5hf`
  (SHA-256 `d2b40d1a1861d0bb...`): its save `C5H-0301-TINY_0009` is a copy of the same file as the catalogue's save
  (SHA-256 b2ceb663...dcc29163), so the terrain is the catalogue's. 56 x 40 plots, sz 0.2, water level 10.1167.
- **Measured** (`models/tools/formations/heights.py`, own code, reads the C5HF format; evidence
  `models/evidence/0327/4153/heights/`, 1135 exact figure positions of 12.2): z = max(height at the truncated sample,
  10.1167) + 0 reproduces T.z within 0.001 units for **1083 of 1135 figures** (median |dz| 0.0000, p95 0.0003); on
  the 197 figures standing on steep ground (terrain slope >= 0.1 at the figure) the rounding variants are worse:
  nearest sample median 0.20, bilinear 0.12, truncation 0.0000 (p95 0.0003). So the members of one unit on a hill
  stand at different heights, each at its own sample, and stay upright (tilt 0, 12.2).
- The floor equals the height field's water level (10.1167 on this map): ships stand at 10.1167 where the sea floor
  samples are 0-7.7 [L]. No catalogue member has a `ZOffset` (only three VP air units in the whole table have one,
  10 or 12), so the +0x144 = `ZOffset` reading stays [I].
- The 52 others: aircraft and helicopters (flying, -0.32 to +0.42), ship hulls (-0.29 to +0.22, hulls of one or two
  bones whose T rests on the fixed yaw), the callout meshes of Machine gun, Missionary and XCOM squad (wrong
  grouping), and the cross-fade blocks (Samurai -0.22).
- For the server: per member, sample the height field at its slot with truncation, raise to the water level, add
  the member's z offset; no tilt.

### 12.5 Field Gun and the Mamluk's horse (item 3)

- **Field Gun (L03) [L, explained].** ThreeBigGuns with 3 members of `union_artillery_parrott.gr2` (cannon and three
  crew meshes): 12 mesh-instances (ids 114-125, four draws of 3), all fitted exactly; the three figures' T match the
  rule with seed 29 (max error 0.000 after the common shift). 3909's "8 mesh-instances spread over 56 units" came
  from the bone medians: crew R's and crew F's lie 36-56 units from the model origin, so some fell outside 3909's
  64-unit plot radius and the rest gave the spread.
- **Mamluk (L06) [L, not explained].** All 15 mesh-instances fit exactly (12.2's axes conversion); the 8 figures
  match DefaultCavalry with seed 27 (max 0.000). The instance ids pair horse and rider per figure (57/58, 59/60, ...,
  69/70); the 8th figure is the rider id 71 alone: that member's horse mesh is submitted in none of the three passes
  that draw the unit (render targets 13383, 13415 and the main 2419; 7 horse and 8 rider instances in each). What makes the EXE skip it is not found [I: per-mesh culling or a limit; one capture
  of the same unit from another angle would tell whether it is always the same member]. *(2026-11-15, issue 4354,
  `unit-renderer.md` 12.5: frustum culling, the clip, a per-draw instance budget and the passes are ruled out [L];
  the horseless figure is slot 7, the last member; the open candidates and what the fresh capture must show are
  listed there.)*

### 12.6 Corrections and notes for other owners

- 4151 (`models/tools/posefit`, not changed): `Clip` reads only the first track group and `Model` the first
  skeleton; neither converts a file's basis. All three matter for VP's models (12.2); exact_t.py works around them.
- 4152 (`unit_mesh*`): the FBX-exported VP models' vertices presumably need the same axis change (y up -> z up, no
  unit scale) before the 28-byte repack [I: not checked against a capture's vertex buffer].
- 0301 (`unit-draws.md` 4 and 7): the bone-median figure position is up to 56 units off the model origin for crew
  models; the exact position comes from 4151's relation (this section). 3909's Field Gun case is explained (12.5).
- *(2026-11-15, issue 4354)* Readers of the formation file: `gamefs::GameFiles`' name lookup of
  `unitformations.xml` returns the older 21-entry copy in `Resource/Common/UnitModels.fpk`; the game uses the loose
  24-entry `Assets/Units/UnitFormations.xml` (section 1's "[I: the loose one]" is now [L], `unit-renderer.md` 12.4).
- *(2026-11-15, issue 4354)* Plot centres: `heightfield.h`'s nominal relation is off the EXE's HexToWorld by up to
  0.12 units on the catalogue's map; use the tap's `hexToWorld` matrix (`unit-renderer.md` 12.2).
- *(2026-11-15, issue 4354)* 12.2's FBX basis change does not touch placement: the FBX units take their slots with
  the same yaw and T rule as the Max ones (`unit-renderer.md` 12.6).

### 12.7 Premises S1-S6 for the server's allocator (epic 0375, issue 4459, 2026-11-15 22:45-23:20 EST)

Reads only, no game run. DLL: the fork at the x64 pin 04b1211b8 (`deps/dll.lock`), files under
`CvGameCoreDLL_Expansion2/`, line numbers at that commit. EXE: an `llvm-objdump` listing of the analysed copy
(`reverse-engineering-setup.md` 2, SHA-256 `b2ce96ae...c88e`) in the session's scratch folder, deleted at the end;
only behaviour is written here (D83). The engine-utility vtable (`0x9EA718`) gives the handlers: slot 144
`GameplayEraChanged` 0x492980, 146 `GameplayUnitCreated` 0x5579C0, 149 `GameplayUnitDestroyed` 0x68F260, 150
`...DestroyedInCombat` 0x58C9D0, 154 `GameplayUnitParadrop` 0x58C1D0, 156 `GameplayUnitEmbark` 0x408310.

#### 12.7.1 The path from a DLL call to the art record [C]

- The handlers do not touch the record table. Each posts records into event manager B's queue (`0x2A10E08 + 0x4C x
  context`, the queue of `combat-choreography.md` 3.1), one post function per record type: **type 0x45** (0xB0
  bytes, post 0x87DD50) = unit created, **type 0x46** (0x70 bytes, post 0x87E570) = unit removed, type 0x50 (0x80
  bytes, 0x87DE20) follows each 0x45 with the facing from 0x503880.
- Who posts 0x45: `GameplayUnitCreated`; `GameplayUnitEmbark` (both directions); 0x486E90, 0x768710 and 0x775BF0
  (callers not traced). Who posts 0x46: `GameplayUnitDestroyed`; `GameplayUnitEmbark` (before its 0x45);
  `GameplayEraChanged` (12.7.3, S3); `GameplayUnitParadrop`; 0x775BF0 (it posts 0x46 then 0x45: an art refresh, caller
  not traced). `GameplayUnitDestroyedInCombat` posts 0x47 (`combat-choreography.md` 3.1), not 0x46.
- The Unit system (UnitSim, static object 0x2D6F5F8, `addresses.md`) subscribes 0x5B03E0 (creation, from 0x5E527C;
  unsubscribed at 0x6BCC0C), 0x66CC50 (removal, 0x5E52A2 / 0x6BCC32) and 0x68E390 (a third type) to the queue. That
  0x5B03E0 serves 0x45 and 0x66CC50 serves 0x46 is [I] (the message ids are registered at run time by 0x58FE90 and
  0x5C1A80); the handlers' bodies agree: 0x5B03E0 allocates, 0x66CC50 frees.
- **Creation (0x5B03E0):** if bit 0 of UnitSim +0x128 is set, the call is deferred into the queue at +0x118 (template
  0x86DF40) and nothing is allocated now; else it allocates (0x56F510 -> 0x7AF700), maps the key (the message's two
  key words, owner and unit id [I]) to the index (0x5FD0F0) and fills the record. **It does not look the key up
  first**: a creation message for a unit that already has a record allocates a second one (the DLL's own comment at
  `Lua/CvLuaUnit.cpp` 679, "creates ghost units on the map", matches this) [C for the code, I for the ghost].
- **Removal (0x66CC50):** the same busy test, deferred into the same +0x118 queue (template 0x86EC90); else it looks
  the key up in the map at +0xE8 (0x5D7580); **an unknown key does nothing**; a known one is cleaned up, freed through
  0x546560 and erased from the map (0x78D560).

#### 12.7.2 The free rule (0x546560) [C]

- 0x546560(start, end) unlinks the live records from `end`'s predecessor back to `start` (walking the +4 links) and
  frees each: **if its index is below the free-list head it becomes the head; otherwise it is linked in directly
  behind the head**; the free count drops by one. Allocation (0x7AF700) takes the head, else appends.
- So the next allocation after a free gets min(head, freed) and the free list is not a stack. Example: free list
  empty, free 20, 21, ..., 31 in that order gives the list 20, 31, 30, ..., 21, so the next twelve creations take 20,
  31, 30, ..., 21: exactly 3907's even catalogue frames (object 1 gets 20, object k gets 33 - k) after `clear.lua`
  killed the previous frame's objects in object order; freeing those in object order (20, 31, 30, ..., 21) gives
  20, 21, ..., 31 back, the odd frames (12.1) [C rule, L fit].
- For the allocator: keep the free list as a linked list with this insertion rule; not a stack, not a sorted set.

#### 12.7.3 Answers

**S1, which events free a record and when.** [C for the DLL and the EXE's posting, I for the type-to-handler map]
- DLL side: a unit is deleted only through `CvPlayer::deleteUnit` -> `TContainer::Remove` -> `~CvUnit`
  (`CvUnit.cpp` 524-545), which calls `GameplayUnitDestroyed` when `gDLL && !gDLL->GetDone() &&
  GC.IsGraphicsInitialized()`. `kill(true)` (delayed death) only sets `m_bDeathDelay` (2516 -> `startDelayedDeath`
  25579); the deletion comes at the next `doDelayedDeath` (25588: `!isFighting() && !IsBusy()`, i.e. no mission timer
  and not in combat), called from `CvPlayer::updateTimers` (`CvPlayer.cpp` 10874), `CvPlayerAI::AI_doTurnUnitsPre`
  (`CvPlayerAI.cpp` 176) and `CvUnit::doTurn` (`CvUnit.cpp` 3207). `GameplayUnitDestroyedInCombat` (`CvUnitCombat.cpp`
  384, 415, 1251, 1669, 2107, 2139, 4450) is sent at the combat for a loser that is then a delayed death; its
  `GameplayUnitDestroyed` follows from `~CvUnit` (`combat-choreography.md` 2.1 step 12). `kill(false)` deletes at once
  (2717). `IsDelayedDeathExported` is `m_bDeathDelay` (25571).
- EXE side: the record is freed when the 0x46 record (from `GameplayUnitDestroyed`, or from an embark, an era change,
  a paradrop) is dispatched to the Unit system, not at the DLL call and not at 0x47. A unit killed in combat keeps
  its record through `DestroyedInCombat` until its `Destroyed` record is dispatched; that record waits behind any held
  record that names the unit (the combat, `combat-choreography.md` 3.2-3.4), so with Combat on (the product since
  D147) the free comes after the combat has played.

**S2, the records at a load and their order.** [C DLL, C EXE posting, L 64 calls; I for "0-19 are the save's units"]
- The EXE calls `ICvPlayer1::SetupGraphical` for players 0..63 once at a load (64 calls in every logged load,
  `server/evidence/2300/*/proxy-calls.tsv`, `2500/P1`, `2500/P3`; order 0..63 in the headless trace,
  `research/sources/civ5-headless/engine-contract.md` 6). `CvPlayer::setupGraphical` (`CvPlayer.cpp` 2306) does the
  player's cities first (`CvCity::setupGraphical`, no unit record) and then its units in the player's list order
  (`firstUnit/nextUnit` = `TContainer::m_order`, `TContainer.h` 49, 115), which is the saved order
  (`TContainer::Read`/`operator>>` push in stream order, `TContainer.h` 233-243, 266-279; `CvPlayer::Serialize`
  `visitor(player.m_units)`, `CvPlayer.cpp` 45952). Per unit `CvUnit::setupGraphical` (`CvUnit.cpp` 1760-1803) sends
  `GameplayUnitEmbark(unit, true)` for an embarked unit and **no** `GameplayUnitCreated`, otherwise
  `GameplayUnitCreated` then `ShouldDimFlag`, `Fortify`, `SetDamage` if damaged, `Work` if working, and for a
  garrisoned unit `Visibility(false)` + `Garrison(true)`. The embark handler's 0x46 for a not-yet-known key does
  nothing (12.7.1) and its 0x45 allocates, so embarked units take their record in the same sequence.
- So, starting from an empty table (the first load in a process), the save's units take indices 0, 1, 2, ... in the
  order player 0's units in list order, then player 1's, ..., the barbarians (63) last, embarked ones included;
  then the game's new units follow. Cities, plots and the units' flags take no record. The catalogue save's
  records 0-19 after its load are, by this, its own 20 units [I: the unit count of `C5H-0301-CAT_0009` was not
  read]. Callers of 0x486E90 / 0x768710 / 0x775BF0 (other 0x45 posters) were not traced: whether anything else takes
  a record at load stays open (the live check below settles it).

**S3, what else takes or frees a record.** [C DLL; C EXE posting; I where marked]

| Case | DLL calls (pin 04b1211b8) | Record effect |
|---|---|---|
| Embark / disembark of a live unit | `embark`/`disembark` -> `GameplayUnitEmbark(unit, true/false)` (`CvUnit.cpp` 7108-7140); at creation an embarked unit sends only `Embark(true)` (1771-1774); a captured embarked unit: `Created` then `Embark(true)` (2823, 2873) | the EXE posts 0x46 then 0x45: the old record is freed, a new one allocated. With 12.7.2 the unit keeps its index only if that index is below the free-list head at the time; otherwise it takes the head and its old index goes behind it. **An embark can change the seed.** |
| Upgrade | `DoUpgradeTo`: `initUnit(..., bSetupGraphical=false)` (14374), `convert(old)` -> `old->kill(true)` (2105), `pNewUnit->setupGraphical()` (14435) | new unit's 0x45 now; old unit's 0x46 at its delayed death (a later update). Create before remove. |
| Gift (`CvUnit::gift`) | cargo `kill(false)` first (6209), `initUnit(..., false)` (6219), `convert` -> `kill(true)`, `setupGraphical` (6226) | as upgrade; the cargo's removals first |
| Captured unit | `kill(false)`: cargo killed (2207), `setXY(INVALID)` (2642), `deleteUnit` (2717) -> `Destroyed`, then `createCaptureUnit` (2723) -> `initUnit(..., true)` -> `Created` (2823) | remove, then create: the captor's unit can take the freed index if it is the lowest |
| Goody hut, city production, any `initUnit` with the default `bSetupGraphical = true` (`CvPlayer.h` 251) | `Created` at the end of `initWithNameOffset` (`CvUnit.cpp` 1250-1251) | one record |
| Other new-unit paths calling `setupGraphical` on a fresh unit | `CvBeliefClasses.cpp` 5061, `CvPlayer.cpp` 13017, 36178, `CvTraitClasses.cpp` 8018, `CvUnit.cpp` 24609, 24645 | one record each |
| Cargo, aircraft based in a city, garrisoned units | ordinary `Created`; garrison adds `Visibility(false)` + `Garrison(true)` | a record like any unit (hidden, not freed) |
| Units invisible to the active team, other owners | `Created` for every unit of every owner; visibility only by `GameplayUnitVisibility` (`setXY` 20470-20544, 20785-20821) | a record like any unit |
| Teleport, rebase, move | `GameplayUnitTeleported` (29838), `Rebased` (10230), `Moved` (29826) | none of these posts 0x45/0x46 [C: not among the posters] |
| Era change | `CvTeam` 8977: `GameplayEraChanged(player, era)` for each alive player of the team | the EXE walks the player's units; for each unit with a live record it compares a value 0x591930 computes from the unit's type and owner with one read from the record (0x4A1360) and skips the unit when they are equal [C; I that the value is the era's art define]; otherwise it posts 0x46 and calls back `ICvUnit1::SetupGraphical` (slot 33) -> the DLL sends `Created` (or `Embark(true)`: 0x46 again, unknown key by then, then 0x45). Live: `ICvUnit1::SetupGraphical` 5 calls with 2 `GameplayEraChanged` in `server/evidence/2500/P3/proxy-calls.tsv`; 8 nested in 24 in the headless trace. **The server sees a `GameplayUnitCreated` for a unit it already has**, inside an era change: it is a remove + create, not a fault. *(2026-11-16, issue 4469 [L], 12.7.11: the comparison differs for every embarked unit at every era change (re-created through `Embark(true)`, freed together and created after, in walk order); a land unit's `Created` re-creation is freed and created at once; the server's land-row art test is not the EXE's comparison.)* |
| Paradrop (active-visible plot, quick movement off, `CvUnit.cpp` 9010-9016) | `GameplayUnitParadrop` | the EXE posts a 0x46 for the unit (0x58C483) and no 0x45 in that handler; what re-creates its record afterwards was not traced [C post, I effect] *(2026-11-16, 4458 [L], 12.7.7 (3): the record is re-created at once: remove + create)* |

**S4, can the queued paths reorder a creation against a removal?** Yes, two ways [C mechanism, I that it happens in
play]:
1. *The event queue.* 0x45 and 0x46 are ordinary records of event manager B's queue, so the pass/wait rule of
   `combat-choreography.md` 3.2 applies: a record waits only behind an earlier blocking record that shares its key (or
   a barrier). A unit killed in a visible combat during the human's turn has its 0x46 held behind its combat until
   the simulation ends (up to 12 s, 20 s with a projectile), while a 0x45 of another unit created meanwhile passes at
   once. The EXE then allocates before it frees, against the DLL's order (Destroyed before Created). During the AI
   turns the barrier (flags bit 1, `combat-choreography.md` 3.3) holds everything after a visible combat, which keeps
   the order. With quick combat or an invisible combat nothing is held and the order is the DLL's.
2. *The Unit system's own deferral* (bit 0 of +0x128): both handlers defer into the one +0x118 queue in arrival
   order; that the flush keeps that order is [I] (the flush was not read). What sets the bit is open
   (`combat-choreography.md` 10, Q8).
So an allocator that applies the feed's events in DLL order is out of step whenever case 1 happens, unless it models
the hold (0042's visual queue) or learns the EXE's order (the test-only reader of the brief's decision 3). *(2026-11-16, issue 4453 [L]: case 1 measured, 12.7.6 (5): the hold
lasts 3.6-4.9 s on the stock path; and a third way exists, 12.7.6 (3): one frame's creations come after its removals.)*

**S5, a second load in one process and a reload.** [C for the code, I for which load paths reach it]
- ShutdownGame (0x580C50, `addresses.md`) calls the Unit system's reset 0x46A780 (at 0x580EF3, behind the Unit bit
  of the system mask at `[0xCC8E74]+0x940`); 0x6445EA and the per-context reset 0x75E2A9 call it too, and 0x6E8140
  (the reset run on "Changing active DLC and Mods", `sources/shell-gates/stubs/landmark.md` 82) ends in the same
  clear. 0x46A780 empties the deferred queue (+0x118) and calls 0x713EB0, which frees **every live record** through
  0x546560 (walking the live list from the last created back to the first, each by the 12.7.2 rule) and empties the
  key map. **The array's size is not reset**: after the clear every old index is in the free list, so the next
  game's units take indices from the free list in the order the clear left it (then append from the old size), not
  0, 1, 2, ...
- Consequences: a reload in the same process gives the same indices as the first load only when the old live list
  was in ascending index order with no older free entries (then the clear leaves 0, 1, ..., n-1 in order); in general
  the indices depend on the whole previous session. The allocator must keep its table across loads (the server's
  process persists across loads, `shared-texture-and-compositor.md` 680) and apply this clear at the load boundary.
- DLL side of the same moment: `CvPlayer::Read` starts with `reset()` (`CvPlayer.cpp` 46089-46092), which empties
  `m_units` (1931) and so runs `~CvUnit` for every old unit: a `GameplayUnitDestroyed` per old unit **if** graphics
  are still marked initialised (order: the `unordered_map` order of `TContainer::RemoveAll`, `TContainer.h` 156-164,
  not the list order). The EXE's clear has already freed those records, so such removals find no key and do nothing;
  the allocator must not apply them as frees. Whether they are sent at all (whether the EXE clears
  `SetGraphicsInitialized` before a load) was not read: each logged run had one load and one
  `SetGraphicsInitialized` call.

**S6, does the re-layout pass a seed other than the record's own?** No [C]. 0x583090 writes the seed (+0x304) only
when its argument's second word is non-zero; its three callers pass 0 (0x4B4E73 from the health-change path 0x4B4B80,
0x5516C2) or the record's own +0x304 (0x78D548, read at 0x78D525). The seed is written at creation only (12.1).

#### 12.7.4 What changes for the allocator (for the C1 note)

1. The free rule of 12.7.2 replaces "reuse the last freed".
2. The order is the EXE's dispatch order: equal to the DLL's except when a removal waits behind a held combat
   (S4.1), which the product has with Combat on. *(2026-11-16, issue 4453 [L], 12.7.6 (3): a second exception, more frequent:
   inside one frame the EXE applies a creation after the removals of other units posted in the same frame, and a
   removal of the human's selected unit a frame later still.)*
3. Creations for known units are remove + create (era change, S3) and embarks are remove + create (S3); a removal of
   an unknown unit is a no-op in the EXE.
4. At a load: clear every live record by 12.7.2 in reverse live order, keep the array size, then the save's units in
   SetupGraphical order (S2, S5).

#### 12.7.5 Live checks still needed (for 4453 / 4452), each with the test-only reader of the brief's decision 3

- **S2 + S5:** load `C5H-0301-CAT_0009` (copy) in a fresh process; read the table (size, live count, free head, the
  record index per (owner, id)) and the DLL's unit lists through the Lua channel. Expected: indices 0..n-1 in
  player-then-list order, n = the save's unit count (20 if 12.1's "0-19" are its units). Then reload the same copy in
  the same process and read again. Clear-and-keep (this section): indices follow the free list the clear left; a
  table reset would give 0..n-1 again; count the `GameplayUnitDestroyed` calls the server logs during the reload.
- **S3 embark and era:** embark a unit whose index is above the free-list head, read its index before and after
  (expected: it changes to the head); set the active team's era up by one (`Teams[t]:SetCurrentEra`) and read the
  indices of era-variant units (workers, settlers, great people) before and after (expected: remove + create).
- **S4:** in the human's turn, with Combat on, kill an AI unit in a visible melee combat and create a unit
  (`InitUnit`) within the same second; read the indices. In DLL order the new unit takes the freed index only if it
  is the lowest; in the EXE's order (combat held) it takes the head as it was before the death.
- **Paradrop and the other 0x45/0x46 posters:** one paradrop on an active-visible plot with quick movement off; read
  the unit's record before, during and after.

*(2026-11-16, issue 4453: run live, results in 12.7.6. S2 [L], S5 [L] (two reloads), S3 embark, upgrade and capture
[L]; S3 era change: no re-creation seen in six era steps [L negative]; S4 [L] with the hold measured; paradrop not
reached (the DLL's `CanParadropAt` refused every plot); the `GameplayUnitDestroyed` count at a reload not taken (stock
path, no call log). New: the EXE's frame order, 12.7.6 (3).)*

#### 12.7.6 Live results (issue 4453, 2026-11-15 23:49 - 2026-11-16 00:52 EST) [L]

**Set-up.** Pool instance c5h-c (Goldberg, `%USERPROFILE%\c5-instances`), the **stock path**: the 32-bit pin
`i0ship-26234371-ia` installed by `tools/scenario/runner.py prepare` (CP + VP + EUI, Vibrant Textures present),
RenderDoc 1.47 preloaded, frame cap 30 fps; save copy `C5H-0375-CAT0301_0009` (SHA-256 `b2ceb663...dcc29163`, the
Tiny catalogue save of 0301, turn 9, Polynesia human), loaded once at the launch (a fresh process), then reloaded twice
in the same process (`shell/tests/tap_live.py reload`). Not the product path: no product driver takes a Tiny save with
RenderDoc without editing another team's file, and with no feed DRAIN yet (4452) the DLL's call order is reconstructed
from the scripted Lua calls and the DLL's code paths of 12.7.3 ("derived"). The EXE's unit system is the same on both
paths; the hold length (5) depends on the EXE's Combat, Projectile and Particle systems, which D33 sets differently on
the product path: **the product's hold is not measured** (follow-up).

**The reader** (`server/tests/units/live/art_records.py`, test only, Python ReadProcessMemory; refuses any process that
is not the DX11 EXE of a pool instance, exit 3): reads 12.1's container through the UnitSim static object, VA
0x2D6F5F8 **rebased**: the EXE of the pool instance was loaded at **0x4A0000**, not 0x400000 (with the harness's
wrapper and the RenderDoc preload; every fixed VA of this file is then + 0xA0000 in that process). The layout of 12.1
held on every sample (header, live list walked by the +0/+4 links, free list by +0, free + live = size, bit 31 set on
every live element). **The unit key in the element** (the reader's `--discover`, a unique fit over 22 live records
against the Lua unit list): **unit id at element +0x2F4 (int32), owner at element +0x2F8 (int32)** (record proper
+0x2EC and +0x2F0). Lua's unit set equalled the reader's key set at every sample. Driver and Lua chunks:
`server/evidence/0375/4453/` (`live_4453.py`, `lua/*.lua`); every sample, call and poll in
`F:/c5-64bit-work/0375/4453/steps.jsonl`. `pool.py list` takes 30-60 s on the workstation: the reader's pool check
runs once per driver process and the handle is kept.

**(1) S2, the fresh load [L].** 22 records, size 22, capacity 32, free list empty: index i = the i-th unit in player
order 0..63 and each player's list order (`Players[p]:Units()`), the barbarians' list order included (1028, 1030,
1031, 1032, 1033, 1035, 1029, 1036, 1037, 1038: not id order). *The save has 22 units, so 12.1's "records 0-19 after
loading C5H-0301-CAT_0009 are its own 20 units" does not hold as written; why 3907's first frame took 20 is not
explained here (the catalogue's studio removes the units in its rectangle; not checked against 3907's run).*

**(2) S5, reloads in one process [L].** Clear-and-keep confirmed twice. Reload 1, after the human's 1002 and 1003
(indices 0, 1) were killed and two warriors created (taking 0, 1): the save's units came back as 1002 -> 0, 1003 -> 2,
1005 -> 3, ..., 1037 -> 21, **1038 -> 1**, size 22: exactly the free list 0, 2, 3, ..., 21, 1 that the clear of the
live list (..., 1039 at 0, 1040 at 1) leaves by 12.7.2. Reload 2, after 50 more steps (a live list of 52 records in a
scrambled order, size 52): the units of the load took the indices the allocator's clear predicts from the reader's
table before the reload, all equal, the free list equal (12 entries), the size kept at 52. (In reload 2 the
barbarians' 1037 and 1038 were absent from the game itself, Lua and reader alike; not investigated.)

**(3) The EXE's frame order: a creation is applied after the removals of other units in the same frame [L, 6
probes].** One Lua call = one DLL frame; the reader polled every 20-100 ms:

| Probe (one Lua call, DLL order) | Reader | Feed order would give |
|---|---|---|
| create A; kill X (index 21) | X freed, A takes 21, size 22 | A appended (22) |
| create A; kill X; create B; kill Y (owner 0) | X, Y freed first, then A, B take them | A from the old free list |
| AI creations on empty plots, barbarian kills, interleaved | both kills first, then the creations | other indices |
| create K; kill X (index 1); embark K | K at 1, size unchanged, no second record | K appended, then moved to 1 |
| upgrade: the new unit created, the old one converted (its Destroyed at the delayed death) | the new unit takes the old unit's index | the new unit appended |
| capture: worker killed, the captor's worker created | the creation takes the freed index (also feed order) | the same |

With two calls 0.3 s or more apart the DLL's order held (2 probes). The records of one unit keep their order (the
embark probe: K's removal waited behind K's own creation). **Exception:** the removal of the human's head-selected unit
(`UI.GetHeadSelectedUnit`) came a frame later, after the creations (3 of 3 cases). Mechanism [I]: event manager B's
wait rule (`combat-choreography.md` 3.2), a creation record that waits one frame and a held record of the selected
unit; not read in code. For the allocator: within one frame apply the other units' removals before the creations
(`SeedAllocator::SetFrameGrouping`, default on, a group = the events of one drain); the selected-unit delay is not in
the feed and is a named source of difference (seeds equal, the free-list order differs until a creation takes from
it, else the seeds of the next creations differ).

**(4) S3 [L].** Embark of a unit whose index (22) was above the free head (1): its seed changed to 1 (remove +
create). Upgrade (`InitUnit` of the new type + `Convert(old, true)`, the DLL's own steps; the upgrade command refused,
`CanUpgradeRightNow` false): create before remove in the DLL, both in one frame, so the new unit took the old unit's
index. Capture of a barbarian worker by a warrior's move: removal, then the captor's new worker took the freed index.
**Era change: no re-creation observed** for `Teams[0]:SetCurrentEra` 0 -> 1 -> 2 -> 4 -> 5 -> 6 with two workers
(`UnitArtInfoEraVariation` true; art rows `ART_DEF_UNIT__WORKER` and `ART_DEF_UNIT__WORKER_MODERN`), a Great General
and warriors of player 0: indices and live-list order unchanged at 20 ms polls. Whether the EXE's comparison (0x591930
against the record) ever differs in VP is open; the allocator's era window (kind 33) stays as coded, untested live.
*(2026-11-16, issue 4458 [L], 12.7.7 (2): on the PRODUCT path the comparison does differ: era 4 -> 6 re-created
the worker and two embarked warriors, each by a removal immediately followed by its own creation, so every index
stayed and only the live list's order changed; the stock run above saw no re-creation on the same save (cause [I]).)*
**Paradrop: not reached** (the DLL's `CanParadropAt` refused every plot within 5 of the capital for a fresh
paratrooper; quick movement off). *(2026-11-16, issue 4458 [L], 12.7.7 (3): reached on the product path (drop range 9 from
the capital): **the EXE removes and re-creates the record** (a paratrooper at index 27 with the free head 0 took 0).)*

**(5) S4 and decision 14: the hold [L].** Human's turn, Combat on (stock), quick combat off, a warrior or archer of the
human attacking a barbarian warrior at 5 HP on a visible plot next to the human's capital; a warrior created by
`InitUnit` 0.5-4.2 s after the order. Times from the order: the defender seen dying (`IsDelayedDeath`) at 0.23-0.34 s,
deleted by the DLL (kind 14) at 0.37-0.61 s; its record freed at:

| Attack | n | record freed minus dying seen (s) | median |
|---|---|---|---|
| melee (warrior) | 4 | 4.20, 4.45, 4.77, 4.94 | 4.61 |
| ranged (archer) | 4 | 3.61, 3.68, 3.71, 3.81 | 3.70 |

(One more melee attack did not kill; two melee samples were void: a first poll 40 s late, and a poll loop that
stopped before the record was freed; per run: `server/evidence/0375/4453/holds.tsv`.) The creations inside the hold
did not take the dying unit's index (appended or took another free index), and the allocator with the hold model
reproduced the index of all 8 creations (the per-step replay, (6)). The hold table for decision 14 (a), stock path, Tiny, workstation (indications, D106):
**melee 4.6 s, ranged 3.7 s, band 0.4 s** (the measured spread plus the poll step); `HoldParams` takes it, measured
from the kind 25 event. *(Superseded 2026-11-16 by issue 4462: the server's table is now the one that covers this
path and the product path (12.7.7 (1), 4458): **melee 4.3 s with band 0.6 s, ranged 3.9 s with band 0.35 s**, a band
per class (`HoldParams::bandSec[]`, `srv_units_core.h` `kHoldMeleeSec`, `kHoldMeleeBandSec`, `kHoldRangedSec`,
`kHoldRangedBandSec`); the 4.6 / 3.7 / 0.4 of this paragraph remain this path's own measurement.)* One of the 8 creations fell within the band (ranged, 0.24 s after the modelled end) and is
counted `CombatHoldAmbiguous` by the replay. AI turns (the barrier keeps feed order, 12.7.3 S4) were not checked:
without the feed's DRAIN the AI's call order is not known.

**(6) The allocator against the reader** (`live_4453.py replay`: the derived calls of every step through
`test_units_seed.exe --replay` with `H 4.6 3.7 0.4`; outputs `F:/c5-64bit-work/0375/4453/replay7*`):

| Mode | unit x sample comparisons | equal | differences and their causes |
|---|---|---|---|
| per step (each step from the reader's previous table) | 1110 | 1105 | 2: the selected unit's removal a frame late (1 probe; 2 more probes differ only in the free-list order); 2: a hold shorter than the model's 4.6 s (4.20, 4.45 s) with the sample taken in between; 1: a sample inside a hold after a faulty poll |
| cumulative from the fresh load | 1110 | 976 | the first selected-unit difference (5 units) carried to every later sample and through reload 2 (the clear walks a different live list): out of step until the process ends, as decision 4 says |

The fresh load, both reloads, the embark, upgrade and capture, the frame-order probes without a selected unit and all
eight holds are equal on every unit. Not covered: AI turns, an era change with a re-creation, a paradrop, the product
path. *(2026-11-16: all four covered since by 4458 (12.7.7-12.7.10); issue 4462 put their rules into the allocator:
an era re-creation and a paradrop as a removal and a creation at once, a combat death's removal after the group's
creations (AI turns too), stacked and embarked creations after the group's other creations; the replay of 4458's
recorded feeds gives 899 of 899 and 34 of 34 (`unit-server-scene.md` section 4, `server/evidence/0375/4462/`).)*

#### 12.7.7 The product path: the server's allocator against the reader (issue 4458, 2026-11-16 03:15-) [L]

**Set-up.** Pool instance c5h-c (Goldberg, `%USERPROFILE%\c5-instances`, Vibrant Textures present), the **product path**
(`run_3308.py`'s prep through `server/evidence/0375/4460/live_4460.py`: D33 with Overlay on, `ServerRenderer = 1`, the
composite beside the proxy); layout `C:/c5-64bit-work/0375/4458/layout` = the pin layout `x64-04b1211b-pfdee656`'s bin
(DLL 29F58E17...) with server.exe 62E70746... built at `c6ad11c7` (the code of the team branch at d0559d8e), proxy
04A764F6..., composite CAA556BE...; `vpproxy.ini server_cfg_extra=` (TEST ONLY, issue 0307) `render_units=1`,
`render_test_units_table`, `render_test_units_dump`. Save copy `C5H-0375-4458-CAT_0009` of b2ceb663 (the catalogue
save of 12.7.6, turn 9, Polynesia); quick combat and quick movement set off in the game by `PreGame.SetQuickCombat` /
`SetQuickMovement(false)` (the save has both on; `Game.IsOption(GAMEOPTION_QUICK_COMBAT)` false after it). Driver
`server/evidence/0375/4458/live_4458.py` (sequence files `seq*.txt`, Lua chunks of 4453 and `4458/lua/`); every check
in `F:/c5-64bit-work/0375/4458/c1/out/checks.jsonl`. **What is compared:** the reader's index per `(owner, id)`
(12.7.6) against the seed the server's own allocator holds for the unit (the TEST ONLY table file, rewritten within a
second of a change), both read after the step settled (4-5 s), the table file read twice around the reader's sample.
Unlike 12.7.6 nothing is derived: the allocator runs on the feed's real events inside server.exe.

| Check (run 1, then run 2 in a fresh process) | units | equal | differences |
|---|---|---|---|
| fresh load (S2), twice | 22, 22 | 22, 22 | - |
| creations of three owners (player 0, city-state 22, barbarians) in one call | 26, 26 | all | - |
| a unit created on water (Lua `InitUnit` of a warrior on a coast plot: embarked at creation) | 26, 27 | all | - |
| an out-of-combat death (`Kill(false)`), an upgrade (`InitUnit` + `Convert`: the new unit took the old index) | 25, 25 | all | - |
| K created, a low index freed, then K embarked by its move mission (K 23 -> 1, the free head) | 25 | 25 | - |
| era 0 -> 1, 1 -> 4 | 26 | 26 | - |
| **era 4 -> 6** | 26 | 23 | **3: (2) below** |
| **paradrop of a new paratrooper (free list empty), then one above the free head** | 27, 27 | 23, 22 | **2: (3) below** (plus the 3 carried) |
| eight visible human-turn combats (4 melee, 4 ranged), a warrior created inside each hold | 29-43 | all | - (2 counted `CombatHoldAmbiguous`, both resolved as the EXE did) |
| a save, its load in the same process (an embarked unit at the save: `Embark(true)` at the load) | 43 | 43 | - (size 44 kept, free list [43]) |
| the same save loaded again in the same process | 43 | 43 | - |

(1) **The hold on the product path** (`server/evidence/0375/4458/holds.tsv`, `holds_table.py`; seconds; the reader
polled every 50 ms, the defender's state through Lua):

| Attack | n | record freed minus dying seen | median | freed minus the DLL's deletion (n) | median |
|---|---|---|---|---|---|
| melee (warrior on a barbarian warrior at 5 HP) | 3 | 3.76, 4.12, 4.80 | 4.12 | 3.62, 3.97, 4.45, 4.50 (4) | 4.21 |
| ranged (archer) | 3 | 3.73, 3.73, 4.18 | 3.73 | 3.58, 3.58, 4.03, 4.08 (4) | 3.81 |

Against the stock path (12.7.6 (5): melee 4.20-4.94, median 4.61; ranged 3.61-3.81, median 3.70) the product's melee
holds spread lower and wider (one at 3.76 s) and its ranged holds wider (up to 4.18 s); n is small (two samples per
class lost the "dying" poll). With the scene's table (melee 4.6, ranged 3.7, band 0.4 from the kind 25 event) every
creation of this series still got the EXE's index (all eight came before the free; the two latest fell in the band
and were counted). **A table that covers the product's spread** (decision 14 (a): measured, never guessed): melee
4.3 s with band 0.6 (3.7-4.9 covers both paths' 7 melee samples), ranged 3.9 s with band 0.35 (3.55-4.25 covers both
paths' 7); a creation in the band stays `CombatHoldAmbiguous`. Not applied here: the constants are in
`srv_units_core.cpp` (`kHoldMeleeSec` ...), not this issue's file (named for the lead).

(2) **Era change re-creates on the product path, one unit at a time [L].** `Teams[0]:SetCurrentEra(6)` from era 4
re-created exactly the units whose art changes (the worker, the embarked warrior moved there, the warrior created
embarked; not the land warrior of the same type, not the spearman): the reader's live list shows the three moved to
its end in walk order (1042, 1044, 1045), each **with its old index**: the EXE frees a unit's record and allocates its
new one before it handles the next unit (removal, then `ICvUnit1::SetupGraphical` -> the DLL's `Created` / `Embark`,
12.7.3 S3), so the creation takes back the index just freed. The server's allocator re-created the same three
(its art test agreed) but applied the three removals first and the three creations at the next frame (the frame
grouping of 12.7.6 (3) applied to the era window), which permuted the indices (1042: EXE 25, server 1; 1044: 1 vs 23;
1045: 23 vs 25). **Rule for the allocator:** inside the era window a re-creation is a removal and a creation applied at
once, before the next event (not grouped). The stock run of 12.7.6 (4) saw no re-creation for the same EXE and save
(eras 4 -> 5 -> 6 with workers); why the EXE's art comparison differs between the paths is not known [I: a value the
EXE reads through the DLL's interface that the proxy route answers differently]. *(Corrected 2026-11-16 by issue 4469,
12.7.11 [L, recorded feed]: "one unit at a time" holds for a land re-creation (`Created`); the two embarked warriors
here are `Embark(true)` re-creations, which the EXE frees together and creates after, in walk order. With 1044 at 1 and
1045 at 23 that also gives each its own index back (free 1, free 23 behind it, create 1, create 23), so this run cannot
tell the two apart; 12.7.11's five embarked units can. The rule the allocator follows since 4469: a `Created` of a held
key of the era player at once, an `Embark` of one in the frame group, both whatever the art test says.)*

(3) **Paradrop is remove + create [L].** Kind 19 (`GameplayUnitParadrop`, quick movement off, a visible target plot;
the DLL's `CanParadropAt` accepted plots with a paratrooper created in the capital, drop range 9): the EXE keeps a
record for the unit: a first paratrooper (index 26, free list empty) stayed at 26; a second (index 27) with the free
head at 0 moved to 0, its 27 behind. So the EXE frees the record and allocates a new one (from the head), as an
embark does. The server's allocator only frees at kind 19 (12.7.3's table: "a removal and no creation"), so the unit
has no seed (`SeedMissing`, not drawn) and every later creation is out of step. **Rule:** kind 19 = a removal and a
creation applied at once. What posts the creation (the paradrop handler or the `setXY` that follows) is not traced.

(4) **Facing 7 [L, C].** Lua `Player:InitUnit` without its facing argument calls `setFacingDirection(NO_DIRECTION)`
(`Lua/CvLuaPlayer.cpp` 1686-1691 at the pin), and in the fork's `CvEnums.h` `NO_DIRECTION` follows
`NUM_DIRECTION_TYPES` (value 7, not -1): every unit a Lua script creates without a facing has facing 7 until it moves.
The feed records 7; the module's `facingYaw` gives 1.0 rad for it, as the EXE's switch (11.2); the server draws such
units (`refused=0`), but the `.c5units` writer refuses facing 7 ("BadValue: an integer 0..5"), so the TEST ONLY dump
(13,000+ LOUD lines in run 1) and, by the same writer, `unitfeed`'s output fail for any frame holding such a unit
(VP's own Lua creates units this way). For the owner of `scene_file.cpp`: accept 0..7 (or any int8) for facing.

The AI-turn half (30 AI turns, a check per turn) is in 12.7.8.

#### 12.7.8 30 AI turns on the product path (issue 4458, 2026-11-16 03:45-04:00 EST) [L]

Set-up as 12.7.7 with the save copy `C5H-0375-4458-TINY_0018` of `C5H-3000-TINY-T0018` (E337244C; turn 18, Spain; the
latest Tiny copy that loads with the instances' CP / VP / EUI mods: the later Tiny saves on the drives, D10's T100 and
0116's T200, are VP-as-DLC saves), quick combat and quick movement set off by `PreGame` after the load, 30 AI turns
(`tools/harness/turns.py end` / `wait`), the reader against the server's seeds after every turn
(`F:/c5-64bit-work/0375/4458/c2/out/checks.jsonl`, tags `AI-T019` .. `AI-T048`). The feed counted 114 creations, 25
removals, 100 combats (kind 25), 16 deaths in combat (kind 15), 1 embark in the run.

- Turns 18-28: **equal** on every unit (35 -> 53 units).
- **Turn 28 -> 29: 2 differences.** A barbarian at index 48 died (the removal) and five units were created in the
  AI turn (ids 1073-1077 in DLL order). The EXE gave 1073 and 1074 appended indices 53, 54 and 1075 the freed 48; the
  server's allocator gave 1074 the 48 and 1075 the 54. So the EXE freed 48 after 1074's creation and before 1075's,
  while the server applied the removal before 1074's creation.
- **Turn 34 -> 35: 3 more.** Three barbarians died (indices 45, 60, 65) and units 1089 (player 3), 1090, 1091
  (barbarians) were created: the EXE appended 1089 (67) and gave 1090, 1091 the freed 45 and 60, freeing 65 later still
  (free list [65] at the check); the server freed all three first (1089 -> 45, 1090 -> 60, 1091 -> 65). Turn 36's
  creation took the EXE's free 65 where the server appended: one more difference carried. Turns 37-48: no new
  difference (6 carried to the end, 83 of 89 equal at turn 48).
- **So in AI turns the EXE also lets creations pass a removal it holds [L]:** removals of units that died in the AI
  turn's combats (quick combat off) come after creations the DLL made later. 12.7.3 S4's "during the AI turns the
  barrier keeps the order" does not hold here; the allocator follows decision 14 only in the human's turn (combat
  game bits 9-10), so it applies these removals at once. Measuring the AI-turn hold needs the feed's event times
  against the reader's (a recorded bundle with a fast reader poll): not done here. *(Done the same session, 12.7.10: 0.11-0.30 s, behind deaths in combats not shown; a rule replayed equal.)* Until then an allocator that
  applies decision 14's hold to every visualized combat (AI turns included) is [I]; with quick combat on (the setting
  of the T0018 save as made), nothing is held (12.7.3 S4).
- **Feed side [L]:** 3 `FeedOutOfOrder` LOUD lines in render.log ("table record (63, 1031) has seq 3097 above the last
  drained 3096 (CopyUnits ahead of Drain)"): the DLL's game-core thread pushes between the render thread's `Drain` and
  its `CopyUnits`, so the copy is newer than the batch (unit-server-scene.md 3.1's "exact" claim does not hold under
  load); 3 in 30 AI turns, no seed effect seen.

#### 12.7.9 One DLL frame of ten creations: the EXE's order is not the DLL's for stacked and embarked units (issue 4458, 2026-11-16 04:01-04:39 EST) [L]

Set-up as 12.7.7 (`C5H-0375-4458-CAT_0009`, fresh process each time). One Lua call (`server/evidence/0375/4458/lua/
scene_e.lua`, every unit with facing 2) creates for player 0, in this order: a worker and a warrior on land plot
(50,14), a warrior and a worker in the capital (51,14), a warrior and a worker on water (51,13), a warrior on water
(51,12), and lone warriors on (49,15), (52,15), (52,16): ids 1039-1048. Done three times (E-B with RenderDoc, E-A,
E-R with 0364's recorder keeping the feed): the reader's indices were the same each time, and the server's allocator
differed the same way each time (6 units).

| id (DLL order) | case | the DLL's calls for it in the frame (feed seq, `server/evidence/0375/4458/er_feed_scene_events.txt`) | EXE index | allocator |
|---|---|---|---|---|
| 1039 | worker, first on (50,14) | Teleported, Visibility(1), ShouldDimFlag, **Created**; later Visibility(0) (the warrior took the centre) | 22 | 22 |
| 1040 | warrior, second on (50,14) | Teleported, Visibility(1), *Visibility(0) of 1039*, ShouldDimFlag, **Created** | **27** | 23 |
| 1041 | warrior in the city | Visibility(0), Teleported, Visibility(0), ShouldDimFlag, **Created**, Visibility(0), Garrison(1) | **23** | 24 |
| 1042 | worker in the city, second on the plot | Visibility(0), Teleported, *Visibility(0) of 1041*, Visibility(0), ShouldDimFlag, **Created** | **28** | 25 |
| 1043 | warrior on water | Teleported, Visibility(1), ShouldDimFlag, **Embark(1)** (no Created) | **29** | 26 |
| 1044 | worker on water, second on the plot | Teleported, *Visibility(1) of 1043*, Visibility(0), ShouldDimFlag, **Embark(1)** | **30** | 27 |
| 1045 | warrior alone on water | Teleported, Visibility(1), ShouldDimFlag, **Embark(1)** | **31** | 28 |
| 1046, 1047, 1048 | lone warriors | Teleported, Visibility(1), ShouldDimFlag, **Created** | 24, 25, 26 | 29, 30, 31 |

So inside one DLL frame the EXE allocated first, in DLL order, the creations by `Created` whose unit had no other
unit's call on its plot between its own first call and its creation (1039, 1041, 1046-1048), then, again in DLL
order, the others: the `Created` of a unit after a call for another unit on the same plot (1040, 1042: the DLL's
`CvPlot::updateCenterUnit` sends the earlier unit a Visibility call while the new unit is being set up) and every
creation by `Embark(1)` (1043-1045). The allocator applies all ten in feed order. Mechanism [I]: event manager B's
wait rule (`combat-choreography.md` 3.2): the earlier unit's Visibility record (a 0.01 s blend) blocks the records
that share its plot until it ends, and the embark handler's creation waits behind its own removal record of a key
that had no record. **Rule for the allocator [L on this frame, I as a rule]:** within a drain, after the removals,
apply the creations by `Created` that have no earlier event of another unit on the same plot in the drain, then the
rest (creations after such an event, and `Embark(1)` creations of unknown keys) in feed order. Not covered: whether
such a creation can slip by more than a frame (then a creation of the next drain would pass it), several plots'
blocks interleaved, AI turns.
*(2026-11-17, ticket 0502 [L], `unit-server-scene.md` 18.3: the rule does not hold inside a load's setup: in two loads
of the huge t350 save (853 units, the setup in two drains) the EXE allocated every setup creation in feed order,
`Embark(1)` creations and second units on a plot included. The allocator's load exemption (4462) covered only the
setup's first drain; the server's core now keeps the whole setup in feed order.)*

The same frame answers what the EXE **draws** on shared plots (unit-server-scene.md 13, P1; 12 below this file's
scope): the visible flag the DLL sends before a unit's creation does not reach the EXE (1042 and 1044 got only
pre-creation Visibility(0) and are drawn by the EXE), a Visibility after the creation does (1039 hidden, the
garrisoned 1041 hidden).

#### 12.7.10 The AI-turn delay measured, and both new rules replayed on recorded feeds (issue 4458, 2026-11-16 04:55-05:08 EST) [L]

Run c3 (`F:/c5-64bit-work/0375/4458/c3`): the T0018 copy again (fresh process, quick combat off), 18 AI turns, 0364's
recorder keeping the feed (`render_test_record`, its frame span placed after the run so that only the feed is
written; the file's last 4 KB were lost at the stop), the reader polled every 30 ms on `time.perf_counter()` (QPC ms,
the same clock as the drains' `drainMs`, `srv_render_common.cpp` QpcToMs), a check per turn. **The AI turns are
deterministic:** the unit counts and both difference episodes of 12.7.8 came back at the same turns with the same ids.
`server/evidence/0375/4458/ai_order.py` (table `ai_order_c3.tsv`): all 9 removals of the run were deaths in combat
(kind 15 before kind 14) in combats the human was not shown (combat flags 0x483 / 0x484: bit 6 visualize clear, quick
combat off, not the human's turn); **the EXE freed each record 0.106-0.297 s after the drain that carried the DLL's
removal** (median 0.19 s), and the creations the DLL made in that window allocated first (turn 28: 1067's removal,
seq 3262, and 1074's creation, seq 3335, came in one drain; the EXE allocated 1074 0.15 s before freeing 1067).

`server/evidence/0375/4458/sim_alloc.py` models the allocation (12.7.2's free rule, the frame grouping of 12.7.6 (3),
the load clear) on a recorded feed and compares it with the reader at every check (`sim_alloc_results.txt`):

| Feed | Rules | unit x check comparisons | equal |
|---|---|---|---|
| c3, 18 AI turns | removals first, then the drain's creations (the server's allocator) | 899 | 882: exactly the server's 17 differences |
| c3 | **a combat death's removal (kind 15 seen for the key) applied after the drain's creations** | 899 | **899** |
| E-R, the one-frame scene of 12.7.9 | the server's | 34 | 25 |
| E-R | **creations after an event of another unit on their plot in the drain, and `Embark(1)` creations, after the drain's other creations** | 34 | **34** |
| c3 | both rules | 899 | 899 |

So the two rules are consistent with every check of both recorded runs; the human-turn hold of decision 14 (a visible
combat held for seconds, 12.7.7 (1)) is a longer case of the first rule. Not covered: quick combat on (the first rule
for an unshown combat with quick combat on is [I]), a drain that ends before the EXE's free (a creation of the next
drain passing too: the window is 0.1-0.3 s, about 3-9 drawn frames at 30 fps) [I].


#### 12.7.11 The era change recorded: embarked re-creations freed together, land ones at once; the art test is not the EXE's (issue 4469, 2026-11-16 11:19-11:37 EST) [L]

Run r1 (`server/evidence/0375/4469/run.sh c5h-e plan1 ... r1`, `live_4469.py plan1`): pool instance c5h-e (F:,
Goldberg, VT present, claimed by W-4469), the product path, the team branch head 8c920dc0 (server.exe 9E7D6829, proxy
20AB447B, composite 142EBAE7, the pin layout `x64-04b1211b-pfdee656`'s bin), a copy `C5H-0375-4469-CAT_0009-r1` of
`REL548-TINY_0008` (b2ceb663, 4458's CAT_0009; Polynesia, turn 9, quick combat and quick movement on as saved), 4465
J2's scripted state (load camera, P1 scene, seam, water units, kill: 34 units, free list [34, 37, 36, 35]), then with
the reader polled every 10 ms (`time.perf_counter`, the QPC clock of the feed's `drainMs`) and the feed recorded
(0364's recorder): `Teams[0]:SetCurrentEra` 1, 4, 6, a player 1 warrior on water, a player 0 warrior created on water
and moved onto land (4466's "created embarked, then disembarked"), player 1's eras 1..7, player 0's eras 7, 5, 6 with
the camera away. A reader check after every step. Restore by SHA-256 ok, `provision.py verify` 0. Evidence:
`server/evidence/0375/4469/r1/` (`reader_changes.jsonl`, `marks.jsonl`, `run-summary.txt`), the replay
`server/evidence/0375/4469/r1_replay.txt`.

- **J2's era case came back exactly** (the same 4 differences at era 6 as 4465's J2, on the team head's allocator).
- **What the DLL sends at an era change (the recorded feed, kinds 33, 11, 21):** era 0 -> 1 and 1 -> 4: kind 33, then
  `Embark(true)` for each of Polynesia's five embarked units (1043, 1044, 1045, 1049, 1050) in id order, nothing else.
  Era 4 -> 6: kind 33, then `Created` for the two land workers 1039 and 1042 (each followed by its setup calls 23 and
  17), then the five `Embark(true)`. All in one drain. The DLL sends these from `ICvUnit1::SetupGraphical`, which only
  the EXE's era walk calls here, so they are the set the EXE removed (12.7.3 S3).
- **What the EXE does (the reader at 10 ms):** the five embarked records are **freed in one frame** (one sample), then
  **created one by one in walk order, 30-200 ms apart** (era 1: 1043 at +33 ms, 1044 +232, 1045 +417, 1049 +582, 1050
  +727 after the frees). With the free list [34, 37, 36, 35] the free rule (12.7.2) then gives 1043 -> 29, 1044 -> 33,
  1045 -> 32, 1049 -> 31, 1050 -> 30 (eras 1 and 6) and 29..33 back (era 4): exactly the reader. The land workers'
  `Created` re-creations at 4 -> 6 were never seen freed by the reader and kept their indices (22, 28): the removal
  and the creation in one frame, as 12.7.7 (2) found for a land unit.
- **Every embarked unit of the era player is re-created at every era change** (5 era changes of Polynesia, its
  embarked graphic override (the war canoe) unchanged across them), while the land workers only at 4 -> 6, where their
  land row changes (`ART_DEF_UNIT__WORKER` -> `_WORKER_MODERN` from the Modern era). So the EXE's comparison
  (0x591930 against the record, 12.7.3 S3) differs for every embarked unit at every era [I why; the embark handler's art
  choice, `unit-server-scene.md` 3.2, depends on the era only from LAST_EMBARK_ART_ERA on]. The disembarked warrior of
  player 0 (created embarked, then moved onto land) was **not** re-created at eras 7, 5, 6: 4466's great war infantry
  case did not come back in this setting (why that unit was re-created stays [I]).
- **The team head's rule against it:** its art test (the land row through 0041's resolver) took 1044 (an embarked
  worker, land row changing at era 5) into the era window and re-created it at once, and the next unit's `Embark` closed
  the window: 4 differences from era 6 on. Its art test also left out every embarked warrior (land row constant), which
  only matched at eras 1 and 4 because their `Embark` swaps fell into the frame group by accident.
- **The rule the allocator follows since 4469** (`srv_units_seed.cpp`, `unit-server-scene.md` 4): inside kind 33's
  window, a `Created` of a held key of the era player is a removal and a creation at once, an `Embark` of a held key is
  the frame group's embark swap (all the group's removals, then its creations in feed order), whatever any art test
  says; nothing is freed for a unit the DLL did not re-create. The recorded feed replayed in the C++ allocator
  (`test_units_seed.exe --replay`, 11 checks through the load, the scene, three era changes and the embark / disembark):
  **369 of 369** equal to the reader; the team head's allocator on the same replay (no art data in the replay) 336 of
  369, live (with the art test) 4 differences from era 6 on; 4462's 899 / 899 and 34 / 34 unchanged.
- **Not covered:** a land `Created` re-creation after the same drain's embarked removals (here the workers came first:
  the at-once creation would take the free head, an embarked unit's index, if it came after) [I]; a re-creation inside a
  combat hold; an era change of a player with units in both the walk and an unrelated creation within the EXE's 0.7 s
  of spaced creations [I]. The recorded feed ends at 11:31 (the recorder's stdio buffer is written in 4 KB blocks and
  the bundle never seals with `render_test_record_after=1000000`, so the tail is lost at the harness stop; 4465's c30
  `.partial` was not empty, as 4465 wrote, but held `feed.bin` 548,864 B up to 07:55 of a run to 08:09): player 1's
  eras and player 0's last three era steps are in the reader's checks and `reader_changes.jsonl` but not in the replay.
- **The reader's `--art` scan (negative):** no ASCII `ART_DEF_` name is reachable from a record's element through one
  pointer or a pointer in the pointed-to 0x80 bytes (`art_before-eras.txt`: "-" for all 34 records); the record names
  its art another way (an index or a deeper chain), not followed.