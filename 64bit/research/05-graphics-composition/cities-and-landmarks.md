# Cities and landmarks: how the EXE assembles a city, the landmark layout handlers, terrain contour (0319)

Written 2026-11-15 by the implementer of queue ticket 0319 (account B), increment 3 research items N6 and N7 of
`research/07-architecture-decisions/architect-2026-11-15-increment-3-decision-sheet.md` (section 1a; package D1 of
section 7). Knowledge base only: no product code, no game launched.

**Method and inputs.**
- The EXE: thirteen read-only headless Ghidra 12.1.4 passes on a scratch copy of the analysed project `civ5`
  (`C:/c5-64bit-work/0319/ghidra/`, copied from `%USERPROFILE%/tools/ghidra-projects/civ5`; input EXE copy SHA-256
  `b2ce96ae...70c88e`, the installed DX11 EXE), each started through `quietlock.py run` (owner 0319). Scripts:
  `StrXrefDecomp.java` of task 2904 (string xrefs and function decompiles), an own `OperandScan.java` (instructions
  whose operands hold a given value). Decompiler output stayed in `C:/c5-64bit-work/0319/out/` and is not committed
  (D83); this file describes behaviour in its own words. Addresses are DX11 VAs at ImageBase 0x400000.
- Data: `Civ5DebugDatabase.db` of instance c5h-c (written by the game with CP + VP + EUI-compat, 2026-11-15 01:34,
  copy SHA-256 `27fc81c6...2fd19b4`, opened read-only); files read from the install copy of instance c5h-a with
  9012's `fpk.py`, `gr2summary.py` (Python reader) and own scripts.
- DLL behaviour: the source of the Community-Patch-DLL checkout `%USERPROFILE%/Documents/GitHub/Community-Patch-DLL`
  (read only, `CvCity.cpp`, `CvPlayer.cpp`, `CvGame.cpp`, `CvBuildingClasses.cpp`, `CvUnitCombat.cpp`).
- Stock captures: the four city frames of 0301's catalogue task 3907 (branch `epic-0301-scene-harness`, not on main;
  captures `F:/c5-64bit-work/0301/3907/captures/c5h-c_20261115-005741_frame756.rdc` (Ancient),
  `..._005741_frame1322.rdc` (Renaissance), `..._010805_frame691.rdc` (Industrial), `..._010805_frame1271.rdc`
  (Modern); exports `F:/c5-64bit-work/0301/3907/exports/cities/c_*`). Setup of those captures: 3907's
  `research/06-tooling-and-workflows/scene-catalogue.md` on that branch (c5h-c, `i0ship-26234371-ia`, CP/VP/EUI,
  Vibrant Textures present, Tiny save `C5H-0301-CAT_0009`). 3701's overlay captures were not needed. 3910's
  landmark catalogue was running on 2026-11-15 and is not on main; nothing here uses it.
- Scripts of this task (own code, scratch only): `C:/c5-64bit-work/0319/scripts/` (`texmatch.py`, `citydraws.py`,
  `passes.py`, `ddsstat.py`, `dbq*.py`, `peread.py`, the pass scripts).

Marks: `[C]` read in the decompile, in data files or in the DLL source; `[L]` seen in the stock captures (a live
game's frame); `[I]` inferred.

## Result in short

- **A city is drawn from one art set per (culture, era slot)** [C]: `Assets/Buildings/City/Civ5ArtDefines_CityBuildings.xml`
  in `CityModels.fpk` plus DLC files of the same shape. Seven cultures (America, Asia, Africa, Europe, Mediterranean,
  Polynesian, Ruins) by four era slots; each set is one `.gr2` that holds 20-21 building blocks in four density
  classes (Sparse, Small, Mid, Tall) with a ground decal under each block (section 1.2).
- **The culture is the owner's `ArtStyleType`, not its `ArtStylePrefix`** [C handler + L capture]: Polynesia
  (`ARTSTYLE_POLYNESIAN`, prefix `ASIAN`) draws `Poly_*` textures in all four stock city frames, and no `Asia_*`
  texture is bound. This corrects 3907's `[I]` in `scene-catalogue.md` section 4 (section 1.3).
- **Era slot** = the era's `Eras.CityArtType`, else its `ArtPrefix`, matched against ANCIENT, RENAISSANCE, INDUSTRIAL,
  MODERN, falling back to earlier eras [C]: with VP's data Ancient/Classical/Medieval -> Ancient set,
  Postmodern/Future -> Modern set. The DLL sends the owner's current era at city creation and at game load (1.4, 1.10).
- **Size**: the DLL reports the population and the EXE keeps a size bucket from the DLL's
  `GetVariableCitySizeFromPopulation` (Fibonacci thresholds 1, 2, 3, 5, 8, 13, 21, 34, 55) [C]; a bucket above 6
  (population 21 and more) switches the city's field stamp from small to large [C for the comparison, I for the
  field's identity] (sections 1.5, 1.9).
- **Blocks** (0337) stand on a 45-degree square lattice of pitch 6.0 around the city, clipped to the size stamp,
  cleared around walls and display buildings, relaxed, then classed Sparse/Small/Mid/Tall/none by the **City Density
  Field**: 70 install files `DF_<shape>_A1..A10.dds` (seven coast shapes x ten size buckets, grey 50/100/150/200);
  each kept point gets a random block of its class with a random quarter turn. Positions are deterministic (identical
  in 3907's four frames from two processes; measured pitch 6.01-6.03), mesh and quarter turn are random; everything
  is redone at every population change [C, L] (section 1.12).
- **Wonders and display buildings** are separate models placed in display slots of the city record; the DLL sends
  `GameplayWonderCreated(player, plot, building, state)` for every building with `DisplayPosition > 0` (58 rows: 46
  world wonders and 12 others such as Palace, Harbor, Lighthouse, Castle, Colosseum) [C] (section 1.6).
- **Walls** are seven per-culture wall files with three pieces each (`WALL`, `GATE`, `TOWER`), plus the Great Wall's
  own pieces [C]; the DLL announces them with `GameplayWallCreated(plot)` [C] (section 1.7). The ring (0337) is the
  convex hull of the density map's built-up area within 40 units, smoothed by a closed cubic B-spline and pushed out
  2 units; WALL pieces are bent along it in runs of up to 6 points, a TOWER stands every `length / floor(length / 12)`
  units, and no code that places a GATE was found [C] (section 1.13).
- **Damage** becomes one of four levels in the EXE; levels 1-3 spawn smoke effects
  (`ART_DEF_VEFFECT_CITY_DAMAGED_SMOKE_FIRST/SECOND/THIRD_*`) at anchors of the city [C]. The thresholds (0337) are on
  the raw hit-point damage: 0 -> 0, 1-7 -> 1, 8-15 -> 2, 16+ -> 3, so a Vox Populi city (200+ hit points) shows the
  full level from 16 damage on; 3, 4 or 6 randomly chosen blocks smoke [C] (section 1.14). A destroyed city becomes the
  Ruins culture (`City_Ruins.gr2`, Mid and Tall blocks only) [C] (section 1.8).
- **The "city-field stamps" are not a height change**: `TF_Small_City.dds` and `TF_Large_City.dds` are 128 x 128
  fields of unit 2D vectors (R, G; |RG| = 0.99-1.00 everywhere) [C], and the city-field manager also loads
  `TF_BLEND.dds` and the City Density Field (`DF_*_A1..A10.dds`, 1.12; 0319 called the `DF_flat` files decals,
  corrected 2026-11-15 by 0337). It is a direction field for the layout of farm fields and city objects [I, strong]. Corrects the "flattens/blends terrain" reading of `sources/shell-gates/stubs/landmark.md` 4, which
  renderer-in-server-design.md 4.16 had already shown does not change the heights (section 1.9).
- **The landmark layout handler is an enum of eight names** (SNAPSHOT 0, FARM 1, SPECIAL 2, PASTURE 3, SPRING 4,
  ANIMATED 5, ANIMATED_ROTATED 6, RANDOM 7; an unknown name gives 0) [C]. Six are used by the 667 rows. **It is read
  once, when the landmark sim starts up, to sort the rows into the lists of two layout managers** (0338, 2026-11-15)
  [C]: the generic manager takes SNAPSHOT rows (a deterministic best match) and RANDOM rows (a `rand() % n` pick
  among the matches, used only when no SNAPSHOT row matches); the animated manager takes ANIMATED and PASTURE rows
  (best match) and ANIMATED_ROTATED rows (random fallback). FARM and SPECIAL rows go into no list: two further
  managers pick their rows by improvement name (`ART_DEF_IMPROVEMENT_FARM`; Moai, pontoon bridge, Feitoria) and place
  them with their own code (section 2.5). ANIMATED_ROTATED rotates nothing [C]. *(Corrected 2026-11-15 by 0338; was:
  "at load the EXE only counts rows per handler; no other reader was found". The reader is in code Ghidra had not
  disassembled.)*
- **`TerrainContour`** (`bTerrainContour` in the XML) is one byte per library entry and per instance [C]. **Its
  only effect: when an instance is created, every mesh part of a model with more than one part is hidden if a 8 x 8
  unit square around the part's origin touches a cell of the plot mask grid with bit 8 set, the river footprint; if
  every part would be hidden, part 0 stays** [C] (0338, 2026-11-15; section 2.4). It is not passed to the draw. Rows
  with 0 (forts, Chateau, Kasbah, Feitoria's land part, oil, ...) keep all their parts next to rivers.

## 1. City assembly (N6)

### 1.1 The systems and the draw [C, L]

- The City system is the `[DisableSystems]` bit 0x20 (`disable-systems-and-masks.md`). Its GameView startup is
  0x662C50, reached from the GameView StartupGame 0x4686C0 under "CityBuildingSystem::StartupGame()" [C]. Its
  per-frame update is 0x537BF0 (one dirty city per frame from a queue at system+0x1624C8, entries of 16 bytes: cube
  hex coordinates and a flag byte) [C]. Render passes "CityBuildings" (0x4A2960) and "CityBuildingsShadow"
  (0x63BBE0) [C]. Allocator tag `.\App\Graphics\cvCityVis.cpp` at 0x611A00; sim object methods
  `cvCityBuildingSim::OnRuinsCreated` and `OnWallCreated` (mangled names in `.rdata`) [C].
- Per city the system keeps a record of 0x6F4 bytes (array at system+0x23C, index from a hex grid of u16 at
  system+0x83FD8, 0x7FFF = no city) [C]. Fields read in this task: +0x18 owner, +0x1C culture index, +0x20 era slot,
  +0x24 a value 1-4 (default and maximum 4; its uses: 1.12), +0x28 the population size bucket, +0x2C/+0x2E hex
  coordinates, +0x30 a flag byte, +0x34 the wall object (-1 = none), +0x532 the damage level, +0x53C..+0x58C and
  +0x6D8 (four) display-object slots, +0x598/+0x630 a list of placed objects with its count at +0x694, +0x6F0 the
  handle of the city's draw object [C for the offsets, I for the names where marked in the sections below].
- The draw object (cvCityVis, 0x4F0 bytes per city) takes culture, era slot, the +0x24 value and the +0x30 byte; the
  model is `table[culture] + era slot` where the table is the per-culture base index the art loader wrote (1.2) [C].
- **Stock draws** [L, 3907's four city frames]: every city block is an instanced `DrawIndexedInstanced` (1 to 13
  instances per draw) with two vertex buffers, slot 0 stride 32 (the mesh, repacked from the file's 60-byte rigid
  vertex) and slot 1 stride 64 (per instance, presumably a 4 x 4 matrix [I]). Each block draw appears twice: vs
  `88c2b7d4` / ps `73daa800` into one target without depth (draws 23-155 of 916 in the Ancient frame) and vs
  `7ec4e9a5` / ps `c704c80b` into the scene target with depth (draws 537-669). The first pair is the shadow pass
  ("CityBuildingsShadow"), the second the colour pass [I by order and target]. 250-286 draws per frame bind a city
  texture (16 cities in the frame plus the save's own cities). The shader pair `7ec4e9a5` is the "21 draws not
  looked at" of 3907's section 7.

### 1.2 The art data [C]

- File: `Assets\Buildings\City\Civ5ArtDefines_CityBuildings.xml` (the path string at 0xA2A6C0; loader 0x46EB90).
  `<BuildingArtInfos>` holds `<Culture name=... [lighting=...]>`, each with four `<Era name=...>` holding
  `<PopulationBuildings><Granny>X.gr2</Granny></PopulationBuildings>`. Base file in `Resource/Common/CityModels.fpk`
  (4,245 B, SHA-256 `61e1db0e...7e81`): America, Asia, Africa, Europe, Mediterranean, Ruins. DLC_03 adds
  `Assets/DLC/DLC_03/Buildings/Civ5ArtDefines_CityBuildings_Polynesia.xml`: `Polynesian`, `lighting="Asia"`, models in
  `PolynesiaModels.fpk`. So the stock install has **24 culture sets plus Ruins** (25 entries, `City_Ruins.gr2` for all
  four Ruins eras), not the 20 of 3907's section 4.
- **Culture index** [C]: the loader matches the name (case-insensitive) against a table of seven names at
  0xB9A2E0: America 0, Asia 1, Africa 2, Europe 3, Mediterranean 4, Polynesian 5, Ruins 6, and writes the culture's
  first model index to its table. The **lighting** index is the culture's own unless the `lighting` attribute names
  another culture; Mediterranean and Ruins are forced to 3 (Europe) when the attribute is absent [C].
- **Era slots are positional** [C]: the `name` of an `<Era>` is read but never compared; the 1st to 4th
  `<Era>` of a culture are slots 0-3. The file's spelling "Rennaisance" therefore does not matter.
- **Blocks inside a set** [C, file contents]: every set file holds one model per block, named
  `<Class>_<Culture>_<Era>_City<NN>`, class Sparse, Small, Mid or Tall, each model with two meshes: the block
  (material `BuildingShader`: `Diffuse` and `BuildingSREF` textures of the set) and a ground plane
  `CityGrdTexPlaneNN` (material `LandmarkShader`: `CityBase_Diff_<era>.dds`, `CityBase_HT.dds`, `CityBase_SREF.dds`).
  Counts read: Afri_Anc 3/6/6/6 (Sparse/Small/Mid/Tall), Euro_Anc and Euro_Mod 3/5/6/6, Asia_Ren 4/5/6/6, Poly_Anc
  3/6/6/6 (its blocks are named `*_Asian_*`), City_Ruins 0/0/6/6 (`Mid_Early_Ruin*`, `Tall_Early_Ruin*`,
  `CityRuins_*` textures). Block meshes are 42-300 triangles; the ground planes are 4-vertex quads of about 220 x 320
  file units. The EXE sorts blocks into the four classes by name prefix (strings at 0xB9A2CC, `Sparse_` 0, `Small_`
  1, `Mid_` 2, `Tall_` 3, in a draw-key builder at 0x7A6FF0 that packs class, culture and era) [C].
- Textures: `CityTextures.fpk` (`*_city_diff.dds`, `*_city_sref.dds`, `cityruins_*`), `MiscTextures.fpk`
  (`citybase_*`), `PolynesiaTextures.fpk` (`poly_*_city_*`) [C].

### 1.3 Which set a city gets: culture from the art style [C, L]

- `GameplayCityCreated(ICvCity1*, EraTypes)` (DLL->engine slot 162, handler 0x58E880) asks the owner `IsMinorCiv`;
  for a major civilization it takes `ICvPlayer1::GetArtStyleType`, for a minor one
  `ICvMinorCivInfo1::GetArtStyleType` of its minor civ type, and converts it (0x765AA0) [C]. The culture order of
  1.2 is the order of `ArtStyleTypes` (SOUTH_AMERICA 0, ASIAN 1, MIDDLE_EAST 2, EUROPEAN 3, GRECO_ROMAN 4,
  POLYNESIAN 5, BARBARIAN 6), and 0x765AA0 is the identity on it with Europe as the default (1.10) [C].
- **Stock check** [L, 2026-11-15, `texmatch.py`]: the payload (all mips) of every city, city-base, ruins and wall DDS
  in the install's packs was hashed and compared with the textures RenderDoc stored in the four city exports. Matched:
  `afri_*`, `euro_*`, `medit_*` and `poly_*` sets of each frame's era; never `asia_*` or `amer_*`. The owners
  3907 placed were Msoura (city-state, its minor art style `ARTSTYLE_MIDDLE_EAST`/`AFRICAN` row of
  `MinorCivilizations`), Polynesia (`ARTSTYLE_POLYNESIAN`, prefix `ASIAN`), Portugal (`EUROPEAN`) and Greece
  (`GRECO_ROMAN`, prefix `MEDITERRANEAN`). Polynesia getting `Poly_*` decides between the two readings: the art
  style, not the prefix. (Msoura's minor civ row was not looked up by name here.)
- The same handler also asks the civilization for its unique building of the walls class
  (`ICvGameContext1::GetWALLS_BUILDINGCLASS`, `ICvCivilizationInfo1::GetCivilizationBuildings`) and takes that
  building's art tag, so Babylon's walls art reaches the city record at creation [C for the calls, I for their use].

### 1.4 Era [C, L]

- The DLL calls `GameplayCityCreated(city, owner's current era)` from `CvCity::setupGraphical` (`CvCity.cpp`
  1832-1857), which runs at city creation (`CvCity::init`) and for every city of a player in
  `CvPlayer::setupGraphical` (game load) [C, DLL source].
- The EXE turns an era into a slot in 0x7A1480 [C for the call site] and has a second writer of the era slot: a
  handler (0x720A80) that sets the slot of every non-ruins city of one owner from an era it receives [C]. Which event
  feeds it was not identified.
- **Stock behaviour** [L, captures]: in the Renaissance, Industrial and Modern frames, the Polynesian cities
  placed for that era bind `poly_ren`, `poly_ind`, `poly_mod`, and the save's own Polynesian cities still bind
  `poly_anc` (26 draws each frame), after 3907's runner had set Polynesia's era with `Teams[t]:SetCurrentEra`. So
  in that run an era change did not re-dress existing cities until a reload [L for what was drawn; I for whether
  the 0x720A80 path is ever taken by Vox Populi].
- The mapping of the game's eight eras onto the four slots (section 1.10): Ancient, Classical and Medieval share
  the Ancient set; Postmodern and Future the Modern set (from `Eras.CityArtType`, else `ArtPrefix`) [C].

### 1.5 Size and population [C]

- The DLL sends `GameplayCityPopulationChanged(city, newPopulation)` from `CvCity::setPopulation` (`CvCity.cpp`
  16698-16701). The handler 0x632B10 reads the population and asks the DLL's `ICvGame1::GetVariableCitySizeFromPopulation`
  (`CvGame.cpp` 13923: the index of the first of 1, 2, 3, 5, 8, 13, 21, 34, 55 the population is below, 0-9), and the
  consumer 0x6E3780 stores that bucket at record+0x28 and marks the city dirty [C]. The creation handler does the same
  for the initial population [C].
- Record+0x28 above 6 selects the large city stamp (0x79C2D0, 0x569720) [C]. How the bucket chooses which blocks of a
  set stand where: 1.12 (0337, 2026-11-15; this line said "not read" until then).

### 1.6 Wonders and display buildings [C]

- `CvCity::setupWonderGraphics` (`CvCity.cpp` 1859-1941) sends `GameplayWonderCreated(owner, plot, building, 1)` for
  every existing building whose `GetPreferredDisplayPosition() > 0`, and state 0 (half built) for a world wonder in
  production or a league project the player contributes to (capital only). The same call follows a building's
  completion (`CvBuildingClasses.cpp` 6064). Handler 0x4BFDB0 (plot getters; borrows its argument, proxy-design.md).
- Database [C, c5h-c copy]: 58 buildings have `DisplayPosition > 0`, as bit values 1 (12 rows), 2 (26), 4 (11),
  8 (4), 16 (2), 32 (1), 64 (2). 46 are world wonders; the other 12 are BUILDING_PALACE (32), HARBOR (16),
  LIGHTHOUSE and RUNESTONE (8), CASTLE and MUGHAL_FORT (1), COLOSSEUM, PITZ_COURT, STADIUM, BROADCAST_TOWER,
  MILITARY_BASE, PARTHENON (2). Every one has an `ArtDefineTag`. The bit is a slot of the city's layout [I].
- The tag resolves to a `<WonderArtInfo><Type>` in `civ5artdefines_wonders.xml` (WonderModels.fpk, 53 entries, each
  one `<WonderArtState state="Any">` with `fScale` and a `<Granny>` `.fxsxml`) and DLC files (model-reader.md 3: 76
  wonder types). Buildings carry `ArtInfoCulturalVariation`, `ArtInfoEraVariation`, `ArtInfoRandomVariation`; the
  DLC_03 file shows the cultural form of a type name (`POLYNESIAN CASTLE`, `POLYNESIAN PALACE`,
  `POLYNESIAN COLESSEUM`, `fScale` 0.2) [C].
- In the city record the display objects sit in slots +0x53C, +0x544..+0x58C and four at +0x6D8, and a list at
  +0x598/+0x630 with its count at +0x694; a remove handler (0x462620) clears whichever slot holds a given object [C].
  The four +0x6D8 objects get their position from the city stamp by slot number (0x79C2D0 calls a stamp-position
  function per slot and turns the stamp's direction into a facing angle) [C for the calls, I for "position from the
  stamp"]. The list objects get the terrain height at their hex and a facing from a direction (0x760DF0) [C].

### 1.7 Walls [C]

- `CvBuildingEntry::IsCityWall` buildings (`CityWall = 1`: BUILDING_WALLS, BUILDING_WALLS_OF_BABYLON,
  BUILDING_LAMASSU_GATE) send `GameplayWallCreated(plot)` (`CvBuildingClasses.cpp` 6121-6124 and 6281-6284;
  `CvCity::setupBuildingGraphics` at load). Handler 0x484E70 (plot getters).
- The loader 0x64E4D0 opens seven wall files from a table at 0xB9A5B0 indexed by culture: `Amer_Walls.gr2`,
  `Asia_Walls.gr2`, `Afri_Walls.gr2`, `Euro_Walls.gr2`, `Medit_Walls.gr2`, `Poly_Walls.gr2`, `Wall_of_Babylon.gr2`
  (the last one also named by `civ5artdefines_walls_babylon.xml`, a `<WallArtInfos>` file in BabylonianModels.fpk:
  `ART_DEF_BUILDING_WALLS_OF_BABYLON`, `fScale` 0.05). From each it takes three models by name, `WALL`, `GATE`, `TOWER`
  (Euro_Walls: `Gate_euro` 146 vertices, `Wall_euro` 30, `Tower_euro` 167; `BuildingShader`, `Euro_Wall_DIFF/SREF`).
  It also opens `HB_Great_Wall.gr2` and `Great_Wall.gr2` with pieces `WALL1`, `WALL2`, `TOWER1`, `TOWER2` and their
  `*_TREECUT` stencil quads (the Great Wall is drawn by this system, half built and finished) [C].
- A walls message stores a wall object at record+0x34 and marks the city dirty with flags 0x3D (0x79ED00); the
  per-city stamp user 0x569720 walks that wall object's point list [C]. How the pieces are laid around the city:
  1.13 (0337, 2026-11-15; this line said "not read" until then).

### 1.8 Damage, capture, destruction and ruins [C]

- `GameplayCitySetDamage(city, damage, previousDamage)` is sent at creation and on every damage change
  (`CvCity.cpp` 1845, 27860-27863), and with damage 0 just before a destruction "to stop the fires"
  (`CvUnitCombat.cpp` 2613-2617, `CvPlayer.cpp` 11875-11879). The handler 0x66F040 compares the raw damage (hit points) with
  four thresholds (0, 7, 15, 20) and posts a level 0-3 [C, 1.14; corrected 2026-11-15 by 0337 from "the damage
  fraction ... threshold values not read"]. The consumer 0x641DF0 stores it at
  record+0x532 and marks the city dirty (flag 2) [C].
- The update 0x6A0340 walks the city's anchor list (entries of 0x28 bytes with a level byte); for every anchor whose
  level is below the city's damage level it posts an effect at the anchor's position (terrain height added) chosen
  from a 3 x 3 table filled at startup (0x6DEF50): `ART_DEF_VEFFECT_CITY_DAMAGED_SMOKE_FIRST_01..03`, then
  `..._SECOND_01` three times, then `..._THIRD_01` three times, by `rand() % 3 + 3 * (level difference - 1)`; it
  removes the previous effect first [C]. The effect models are `fxm_city_damage_state_{first,second,third}_0{1,2,3}`
  in `EffectData.fpk` [C, file list]. Effects are drawn by the Particle/effect systems, which the product kept off
  (`VP64_D33_KEYS_A2`) until 2026-11-15; since issue 0368 (D147) they are on in the EXE, so the EXE may draw this
  smoke over the server's picture (not checked for city damage) [I]; the server drawing it needs the effect work of
  0042's C4 [I].
- `GameplayCityCaptured(city, newOwner)` (`CvPlayer.cpp` 3965, handler 0x4C13D0) and `GameplayCityDestroyed(city,
  NO_PLAYER)` (`CvPlayer.cpp` 11879, handler 0x5E3970, slot 163) [C]. A culture-change consumer (0x479D40) rewrites
  record+0x1C and marks the city dirty [C]; a captured city taking its new owner's culture is [I].
- **Ruins** [C]: a ruins consumer (0x7A59C0, the `CityRuinsCreated` event / `cvCityBuildingSim::OnRuinsCreated`)
  sets culture 6 (Ruins), era slot 0, the +0x24 value to 4 and damage 0, or creates a record for a new ruins hex. The
  Ruins set has no Sparse or Small blocks. `ArtDefine_StrategicView` has `ART_DEF_IMPROVEMENT_CITY_RUINS`
  (`SV_CityRuins.dds`); MiscData.fpk's `cityruins_improved*.gr2` (one of them the undecodable big-endian file,
  model-reader.md 2.3) are not used by this path (no reference found) [I].
- **Razing** has no own art in the files read: a city being razed is drawn by its damage and population [I].

### 1.9 The city field (city-field manager 0xB9DDA0): direction stamps, not heights

Correction 2026-11-15 to `sources/shell-gates/stubs/landmark.md` section 4 (which said the manager "flattens/blends
terrain under city footprints"; that was an inference, and `renderer-in-server-design.md` 4.16 had already measured
that the drawn heights do not change).

- Init 0x432220 (only from the Landmark branch of GameView Startup, as recorded) fills 50 random entries, allocates
  a 128 x 128 grid (0x4000 bytes plus a header with width and height 0x80) and loads `assets\LayoutSystem\TF_BLEND.dds`
  and the City Density Field maps (0x663FC0, 70 files `DF_<shape>_A1..A10.dds`, 1.12), then the two city stamps (0x7812D0) into 64 x 64 x 2-byte stamps with a header
  (+0x2000 width, +0x2004 height, +0x2008 rectangle, +0x2018 scale) [C].
- The files [C, `MiscTextures.fpk`, `ddsstat.py`]: `tf_small_city.dds` and `tf_large_city.dds` are 128 x 128 A8R8G8B8
  (65,664 B, SHA-256 `b6f10cee...c768c9` and `2e5fbc7a...fd3ed57`) with B = 0 and A = 255 everywhere and (R, G) a
  unit vector at every texel (|RG| 0.99-1.00 at p5, p50 and p95); the large stamp's vectors turn around its edge
  (top (149, 253), left (253, 108), bottom (141, 254), right (254, 112)). `tf_blend.dds` is a grey radial blend (255 in
  the middle, 0 at the corner); `df_flat_a1..a10.dds` are density maps, not alpha decals (1.12; corrected
  2026-11-15 by 0337). `MiscData.fpk` also holds `large_city.ftbxml`, a
  `tangent_buddy type="FTensorBuilderBasic" width="128" height="128"` file of widgets (`FWidgetLinearRegion`,
  `FWidgetRoundy`, `FWidgetDir`): the authoring form of such a field [C].
- Users [C]: the City update writes the city's hex rectangle and scale into the stamp of its size (0x79C2D0,
  0x569720) and derives positions and facing angles for the city's slot objects from it; 0x569720 also gathers the
  city's walls, display objects and list objects (up to 32) with their positions. The farm-field layout 0x6016F0
  reaches the manager only through its direction field 0x44DA00, which reads the manager's tail (a base angle and a
  per-map noise offset), never the city stamps: the farm fields follow a simplex-noise direction field and **cities do
  not bend them** [C, `farm-fields-layout.md` 7]. The city stamps orient city objects only, as far as is known.
  *Corrected 2026-11-18 by lead-0528 (epic 0528, from ticket 4850's reading). Was: "The farm-field layout 0x6016F0
  (the only other known user) reads the manager. So the field orients farm fields and city objects around a city
  [I, strong]."*

### 1.10 The three conversions [C]

- **The art style to culture** (0x765AA0): 0 -> 0, 1 -> 1, 2 -> 2, 4 -> 4, 5 -> 5, 6 -> 6, anything else (3 included)
  -> 3. So the culture index is the `ArtStyleTypes` ID with Europe as the default: SOUTH_AMERICA America, ASIAN Asia,
  MIDDLE_EAST Africa, EUROPEAN Europe, GRECO_ROMAN Mediterranean, POLYNESIAN Polynesian, BARBARIAN Ruins. No
  `MinorCivilizations` row of the c5h-c database uses BARBARIAN (their styles: ASIAN, EUROPEAN, GRECO_ROMAN,
  MIDDLE_EAST, POLYNESIAN, SOUTH_AMERICA).
- **Era to slot** (0x7A1480, called by the creation handler and by the era handler 0x720A80): the EXE runs
  `Select CityArtType from Eras where Type = ? LIMIT 1` and, if that is empty, takes the era's `ArtPrefix`; it
  matches the string against `ANCIENT`, `RENAISSANCE`, `INDUSTRIAL`, `MODERN` (table at 0xB9D5EC, slots 0-3); with no
  match it tries the era before, down to the first. In the c5h-c database (CP + VP) `CityArtType` is NULL in all eight
  rows, so: **Ancient, Classical, Medieval -> Ancient; Renaissance -> Renaissance; Industrial -> Industrial; Modern,
  Postmodern, Future -> Modern**. A mod can change the mapping through `Eras.CityArtType`.
- **Slot position** (0x57D3F0, for the four display slots of 0x79C2D0): the city hex's world position plus a fixed
  per-slot offset (a table of four 2D offsets at system+0x162494, filled at startup by 0x6DEF50 from two constants),
  then the stamp of the city's size is sampled there and its 2D vector becomes the slot's facing. So a slot object
  stands at a fixed offset from the city centre and faces along the field [C for the arithmetic, I for "facing"].

### 1.11 What the renderer needs from the DLL, per part

| Part | DLL call (slot) | What the EXE takes from it | Where it comes from in the DLL |
|---|---|---|---|
| City set | `GameplayCityCreated(city, era)` (162) | id, owner, hex, population, size bucket (`GetVariableCitySizeFromPopulation`), owner's art style (major: player; minor: minor civ info), era, the civilization's walls building art tag | `CvCity::setupGraphical` at creation and load |
| Size | `GameplayCityPopulationChanged(city, pop)` (166) | population, size bucket | `CvCity::setPopulation` |
| Damage | `GameplayCitySetDamage(city, dmg, prev)` (165) | raw damage -> level 0-3 (thresholds 0/7/15, 1.14) | `CvCity::setDamage`, creation, destruction |
| Wonders, display buildings | `GameplayWonderCreated(player, plot, building, state)` (189) | building type (-> `ArtDefineTag`, `DisplayPosition`), state 0/1, plot | `setupWonderGraphics`, building completion |
| Walls | `GameplayWallCreated(plot)` (176) | the plot; the walls art came with the city | `setupBuildingGraphics`, building completion |
| Owner change | `GameplayCityCaptured(city, newOwner)` (164) | the city, the new owner | `CvPlayer::acquireCity` path |
| Destruction | `GameplayCityDestroyed(city, NO_PLAYER)` (163) | the city; ruins follow | raze completion, combat kill |
| Fog | (plot visibility, already in the feed) | | |

All of these arrive inside the server first (the DLL runs there); by D137 each needs a file form for offline replay.
The getters each handler calls are the ones listed in `research/03-dll-engine-boundary/proxy-design.md`'s borrowing
table (all borrow their argument).

### 1.12 Block placement by city size (0337) [C, L]

Read 2026-11-15 by 0337 (read-only headless Ghidra 12.1.4 passes on a scratch copy of project `civ5`, input EXE
SHA-256 `b2ce96ae...70c88e` as in the method above; checked against the instance buffers of 3907's four stock city
frames). The pipeline has four stages; the first three run in 0x569720 (City update, flags 0x0D) and the fourth in
0x5BAC00 (flags 0x05). A population change marks the city with flags 5 (0x6E3780), so **all four stages run again when
the city grows or shrinks** [C].

1. **Candidate points: a rotated square lattice around the city** [C 0x74CEB0, L]. A point set (entries of 0x24 bytes,
   bucketed in a 32 x 32 grid of 4-unit cells) is filled with a square lattice turned 45 degrees over a 128 x 128-unit
   window at the city: per axis `n = int(sqrt(32768) / S)` points (sqrt(32768) = 128 x sqrt 2) with step `128 / n`,
   where **S = 6.0** is the spacing at point-set +0x4848 (0x79FD70 writes 0xA04FC4 = 6.0) [C]: `n = 30`, step 128/30 =
   4.267, nearest-neighbour pitch 4.267 x sqrt 2 = 6.03. **Stock: the blocks of every studio city stand on a 45-degree
   lattice with nearest-neighbour distance 6.01-6.03 world units (median) and 5.3-5.7 (10th percentile)**, neighbour
   directions only +-45 and +-135 degrees (n = 25 to 98 blocks per city) [L, `cityinst.py`/`cityclusters.py` on the
   four exports]: the measured pitch equals the computed one.
2. **Clip to the size stamp and take its direction** [C 0x507CD0]. Points outside the window, or outside the rectangle
   of the city's direction stamp (small for size bucket 0-6, large for 7 and more, 1.9), are deleted; each kept point
   samples the stamp (0x42E4A0, bilinear) and stores the 2D direction as its facing (+0x10/+0x14, x 1/128).
3. **Obstacles and relaxation** [C 0x569720, 0x6EF590, 0x7B43B0]. Points are deleted along the city's wall outline
   (1.13, three passes over its segments, 0x4E16C0) and inside the footprint of every display object of the city
   (palace, wonders and other `DisplayPosition` buildings, up to 32, footprint = the model's bounds turned by its
   facing, 0x7AFAE0); then 40 rounds of a neighbour and obstacle push (0x6EF590, called by 0x6E26C0 with flags
   bit 1 on rounds not divisible by 10 and bit 0 on rounds not divisible by 5: with bit 0 a neighbour closer than 5.0
   in the field-aligned distance (point-set +0x484C, 0xA0B0B4) is removed, otherwise the pair is pushed apart; with
   bit 1 a point on an occupied cell of a 128 x 128 occupancy grid is removed; neighbours are searched within 2
   bucket cells and occupied cells within 4 cells push points away, both from 2.75 at 0x9E2D20) [C for the calls,
   flags and constants, I for "relaxation" as the purpose; the step that applies the accumulated push was not read].
   The stock 10th-percentile spacing of 5.3-5.7 (above 5.0) fits [L]. The survivors whose four corners at +-2 units
   are all free of flag bit 8 of the plot flag map (0x4365E0; *2026-11-15, 0338: the mask grid 0xBA392C, bit 8 = the
   river footprint written by the river mesh build, section 2.7; was "the bit's meaning was not read"*) become the city's
   anchors (0x28 bytes each, list at record+8) with class 4 ("none") [C].
4. **Class from the City Density Field, then terrain checks** [C 0x5BAC00, 0x663FC0]. The city-field manager 0xB9DDA0
   (the same object as 1.9) holds the "City Density Field", loaded at startup by 0x663FC0 from **70 files in
   `MiscTextures.fpk`: `DF_<shape>_A1.dds` .. `_A10.dds`, one per size bucket 0-9, for seven shapes `flat`, `0`, `1`,
   `05`, `15`, `045`, `135`** [C for the names: "DF_flat", "DF_" + the set bits of the masks {0x01, 0x02, 0x21, 0x22,
   0x31, 0x2A} at 0xA38984, suffixes `_A1.dds`.. at 0xA389BC; file list from `fpk.py`]. Each file is 128 x 128
   A8R8G8B8 with mips (87,508 B), grey (R = G = B) in levels 0/50/100/150/200 plus a few in-between values
   [C, `dfstat.py`]. The loader turns the first byte of each texel into a **class with the thresholds {49, 99, 149,
   199} (0xA3897C): below 49 -> 4 none, 49-98 -> 0 Sparse, 99-148 -> 1 Small, 149-198 -> 2 Mid, 199 and more -> 3
   Tall** (0x6BF2F0; rows flipped, row 127 first), stores the rows run-length coded (pairs count/value, 0x420030 /
   0x77AEB0; decoded by 0x5BEBC0 into the grid at manager+0x28, 0x60D3D0: `(shape x 10 + bucket) x 0x200` selects the
   row table) [C]. **Layouts**: one entry for `flat` and four for each of the six other shapes (symmetry indices 0,
   2, 6, 9 of a matrix table at 0xC8FCD0), 25 in all, each holding the map-set index, a 6-bit key (the shape's mask
   moved by that symmetry, 0x667230) and the matching 4 x 4 transform into map space (0x48 bytes, manager+0x1C). A city
   takes the entry whose key equals a 6-bit mask built for its hex by 0x53FA50 from its plot record and three
   neighbours; with no match the first (`flat`) [C for the arithmetic; I for "the bits are the hex edges that touch
   water", from the names' digits 0-5 and the coastal shapes]. Each anchor is transformed into map texels and takes
   the map's class (the class names are the model prefixes of 1.2) [C]. An anchor
   with a class below 4 is then dropped (class 4) when the terrain value at its plot cell is outside 13,001-35,000, or
   when its terrain normal has z < 0.85 (about 32 degrees of slope) [C for the tests, I for "height" as the meaning of
   the 16-bit value]. Every kept anchor gets one block: **a random block of that class from the city's set**
   (0x4448E0 -> 0x77A7A0: the blocks of the set whose class byte matches, `rand() % count`), placed at the anchor
   (0x55EB70) [C], facing along its point's direction **plus a random quarter turn** [L: between frames the same
   block position is turned by exactly 0, 90, 180 or 270 degrees, about a quarter each; the code of the turn was not
   located, I: like the landmarks' `rand() & 3` x 90 degrees in 0x4BE160]. Neither the pick nor the turn is stored;
   a replay needs them or the seed.
- **What changes with size** [C, L]: only the density map (the bucket picks one of the layout's ten maps) and, from
  bucket 7, the large stamp's rectangle and directions. Stock, `C5H-0301-CAT_0009`, the eight studio cities (2026-11-15
  captures of 3907, `i0ship-26234371-ia`, CP/VP/EUI, Vibrant Textures): **population 1 (bucket 1): 25-34 blocks over
  about 34 x 34 units; population 20 (bucket 6, still the small stamp): 83-98 blocks over about 68 x 64 units**
  (Africa 32/98, Polynesia 32/95, Europe 25/98, Mediterranean 34/83) [L]. The plot pitch in those frames is about 63
  units per column [L, from the city spacing], so a size-6 city covers about one plot [I]. The `flat` maps grow
  steadily from bucket 0 to 9 (class texels 1,159 / 1,492 / 1,753 / 2,345 / 2,835 / 3,382 / 4,378 / 5,616 / 7,607 /
  10,952; extent 40 x 39 to 121 x 125 texels) [C, `dfclass.py`]. If the studio cities use `flat`, bucket 1 (`_A2`,
  1,492 class texels) for 25-34 blocks and bucket 6 (`_A7`, 4,378) for 83-98 blocks both give about 0.85 world units
  per map texel at 36.4 square units per lattice point [I: the studio plots' shape and the map scale were not read].
- **Determinism** [L, `cityinst.py` rows compared]: all 557 instance positions (x, y and z) and their scale are
  identical in the four frames, which came from two game processes (`..._005741` Ancient/Renaissance, `..._010805`
  Industrial/Modern) and four eras; what differs is the mesh (12-21 distinct block meshes per city) and the facing
  (by quarter turns, 376-415 of 557 instances). So positions are a function of the city's plot, size and
  surroundings, and the per-block mesh and quarter turn are the random part [L, consistent with C].
- **Instance data** [L]: the colour draw (vs `7ec4e9a5`) takes per instance three float4 rows of a 3 x 4 matrix
  (rotation about z times a uniform scale of 0.038 for blocks; translation in the fourth column of each row: x, y,
  z) and a float4 colour (1, 1, 1, 1 seen); some instances of scale 1.0 in the same draws are not blocks (other city
  objects, not identified). Block z is the terrain height at the anchor (11.1-13.7 in the studio).
- **The record's +0x24 value** (1.1) [C for the uses, source not traced]: record creation (0x617B50) clamps it to
  1-4 (4 otherwise), ruins set it to 4, and it is copied to the draw object (+0x4D4). There it picks **which group of a
  block's meshes is used** (0x55EB70 starts at `value x model+0x60`) and two material entries of the walls (0x75A220
  indexes 0xD90E88 and 0xD90E60 with it). Its source in the creation event was not traced.
- **Correction** (2026-11-15, 0337): until this date 1.9 called the `DF_flat` files "alpha decals"; they are the `flat`
  shape of the City Density Field. Their alpha channel holds one pattern, the same in all six files read; its use was
  not read.
- **Open**: the map-space transform (the runtime matrix at 0xD90FF0 times the layout's matrix), so the exact world
  scale of a texel; the 6-bit mask's bits (plot record bytes +6..+8 with mask 0x0A, the neighbour table at 0xC8FB08);
  the meaning of plot flag bit 8 and of the 16-bit terrain value; the exact step of 0x6EF590 that moves the points;
  the 8 x 8 jitter table of the walls (1.13).

### 1.13 Wall layout: outline, WALL runs, TOWERs, GATE (0337) [C]

Read 2026-11-15 by 0337 (same method as 1.12). Walls are rebuilt by 0x738780 whenever the city is marked with any of
the flags 0x3D (the walls message sets 0x3D, a population change 5), so **the wall ring grows with the city** [C].

- **Which art set** [C 0x4C1BB0, at draw-object creation]: the city's walls art tag (the civilization's building of
  the walls class, read at creation, 1.3) is compared with the tag hash each of the seven wall sets carries (table
  0xB9A5CC); a match takes that set (this is how Babylon gets `Wall_of_Babylon.gr2`); otherwise the set of the city's
  culture through the same table; otherwise set 3 (Europe). The sets are, in table order, `Amer_`, `Asia_`, `Afri_`,
  `Euro_`, `Medit_`, `Poly_Walls.gr2`, `Wall_of_Babylon.gr2`, each with the three pieces in the order **WALL, GATE,
  TOWER** (names table 0xB9A5E8) [C]. Index 6 is Babylon here, not Ruins.
- **Outline** [C 0x451DF0]. The city's density map of 1.12 (same layout and bucket) is sampled on a 64 x 64 grid
  (every second texel) with a per-sample jitter from an 8 x 8 offset table of the wall system. A sample is inside the
  city when its class is: **bucket 0-3: any class but 4; bucket 4-5: Small, Mid or Tall (not Sparse); bucket 6-9: Mid
  or Tall only**. It must also pass the block tests of 1.12 (terrain value 13,001-35,000, normal z >= 0.85, plot flag
  bit 8 clear) and lie **within 40.0 units of the city centre** (0x9F15A0). The kept samples are reduced to their
  **convex hull** (0x868E90: lowest-x start, angle sort, cross-product test). If the hull's area measure is below 200
  (0x9E3954) the outline becomes a **hexagon of six points at 0.6 of the hex's corner directions** (table 0xB9D7D8)
  around the centre. The closed outline is then smoothed with a **closed uniform cubic B-spline** (0x868720, basis
  tables 0xB9D8A4-0xB9D8E8) sampled at unit parameter steps, and each sample is pushed **2.0 units** along the local
  normal [C for the arithmetic, I for "outward"]. The result is the wall object's point list (record+0x34 -> wall
  table at system+0x214, 0x20 bytes per wall).
- **Pieces** [C 0x738500, 0x5E9A60, 0x751CC0, 0x6892D0]: with four or more outline points, the loop is walked twice
  (the whole loop, then the closing stretch across the start). **WALL**: the polyline is cut into runs of up to six
  points, each new run starting with the last two points of the previous one; every run of four or more points is one
  WALL piece **bent along the curve** (0x4CAA20). **TOWER**: placed at equal arc-length steps, `count =
  floor(length / 12)` (0x9FFB10 = 1/12), step `length / count`, the first position skipped, each facing along the
  local tangent (`atan2`) at the terrain height. Limits per city: 10 runs (draw object +0x4A8, 0x4C bytes each) and
  20 towers (+0x4A9; position at +0x304, facing at +0x3F4) [C].
- **GATE is loaded but no writer was found**: the draw object has a list of up to 6 gates (+0x444 positions, +0x48C
  facings, count +0x4AA) which the draw reads, but an operand scan of the whole EXE for 0x4AA and 0x444 finds only
  the reset (0x51FEE0, 0x560E70), the copy (0x609400) and the reader (0x75A220) [C for the scan; I for "never
  placed", since a write through a computed pointer would not show].
- **Drawing** [C 0x75A220, 0x401E50]: the pieces of one city are baked on the CPU into one vertex and index buffer
  (draw +0x4AC/+0x4B0/+0x4B4): each TOWER (and GATE) is the piece mesh scaled by the draw object's wall scale (+0x4A4,
  set to **0.05** by 0x642980 from 0x9E28EC; the Babylon art XML's `fScale` is also 0.05), turned about z by its facing
  and moved to its position; each WALL run is the WALL mesh deformed along the run. The material/texture pick reads
  the culture (+0x4D0, capped at 3) and the era slot (+0x4CC) [C for the reads, I for which texture they choose].
- **Stock**: no city in 3907's catalogue has walls, so nothing here is checked against a picture [not verified]. The
  Great Wall's own pieces (`WALL1/2`, `TOWER1/2` and their `TREECUT` quads, 1.7) were not read.

### 1.14 Damage levels: the thresholds (0337) [C]

Read 2026-11-15 by 0337 (read-only Ghidra pass on a scratch copy of `civ5`, disassembly of the handler).

- The handler of `GameplayCitySetDamage(city, damage, previousDamage)` (0x66F040) compares the **raw `damage`
  argument** (the DLL's hit-point damage, converted to float) with four constants built on its stack: **0.0, 7.0
  (0x9DAF70), 15.0 (0x9DEE88), 20.0 (0x9F39EC)**. The level is the index of the first threshold that is greater than or
  equal to the damage, capped at 3: **damage 0 -> level 0, 1-7 -> 1, 8-15 -> 2, 16 and more -> 3**. The fourth
  threshold (20) cannot change the result (index 3 and "none found" both give 3). It is not a fraction of the city's
  hit points [C, corrects the "damage fraction" of 1.8].
- The DLL sends the damage in hit points (`CvCity::setDamage`, `CvCity.cpp` 27850-27863; `MAX_CITY_HIT_POINTS` 200 in
  `CvGlobals.cpp` 1495, plus building hit points). So with Vox Populi (and with the stock BNW rules) every city that has
  lost 16 or more hit points shows the full level 3, and the three levels cover only the first 16 of about 200+ points.
  The constants fit the original game's 20-point city scale [I: the history is inferred, the values are C]. A faithful
  renderer keeps the EXE's thresholds; a "better" scaled version is a deliberate deviation (D66 names it as such).
- **Which buildings smoke** [C, 0x5BAC00, at every block placement, 1.12]: every placed block gets a smoke level byte
  (anchor +0x1C). All candidate points start at 4 ("never"); the list of placed blocks is **shuffled** (0x907A00, a
  Fisher-Yates shuffle on `rand()`), and the first eight of the shuffled list get level 0 for ranks 0-2, 1 for rank 3,
  2 for ranks 4-5 and 3 for ranks 6-7, from the table {3, 4, 6, 8} at 0xA3800C (the rank is compared with entries
  1-3). The update 0x6A0340 (1.8) posts a smoke effect on
  every block whose level byte is below the city's damage level, with the effect chosen by `rand() % 3 + 3 x (city
  level - block level - 1)`. So: **level 1 smokes the first 3 blocks of the shuffled list** with `FIRST_0x`; **level 2 the first 4**
  (3 with `SECOND_01`, 1 with `FIRST_0x`); **level 3 the first 6** (3 `THIRD_01`, 1 `SECOND_01`, 2 `FIRST_0x`) [C for
  the arithmetic; the effect table of 0x6DEF50 fills the SECOND and THIRD rows with one name each, 1.8]. Ranks 6-7
  (level 3) never smoke, since no city level exceeds 3 [C].
- So **which blocks smoke is random** (3, 4 or 6 of the city's blocks) and is drawn again at every block placement,
  that is at every population change (1.12) [C]; a replay needs the shuffle or its seed.
- The effects themselves are Particle/effect-system work (0042's C4, `effects-formats.md`); the renderer needs from
  this part only the level and the anchor positions.

## 2. Landmarks: the layout handlers and terrain contour (N7)

`landmarks-and-models.md` 1 has the table (667 rows, 514 models) and section 3 the system's objects. Facts added here:

### 2.1 Parsing [C]

- 0x560340 (art reload, `cvLandmarkLibrary.cpp`) runs `select ImprovementType, ResourceType, Era, State, Scale,
  TerrainContour, Model, LayoutHandler, Tech from ArtDefine_Landmarks` and builds one library entry of 0x1800 bytes
  per row (0x5CC9E0): improvement type string at +0x1640, resource type at +0x16C0, era at +0x175C (0x723A40),
  state at +0x1760 (0x43E830: `UnderConstruction`, `Pillaged`, ...), scale at +0x174C, TerrainContour as one byte at
  +0x1750, layout handler at +0x1768 (0x7304E0), tech at +0x1764, "has an improvement" at +0x176E, "is a resource,
  not a feature" at +0x176F, resolved type ids at +0x1754/+0x1758 (`ART_DEF_RESOURCE_ALL` -> -2, none -> -1).
- The handler string becomes an enum (0x7304E0): **SNAPSHOT 0, FARM 1, SPECIAL 2, PASTURE 3, SPRING 4, ANIMATED 5,
  ANIMATED_ROTATED 6, RANDOM 7**; anything else 0. PASTURE and SPRING have no rows (`ArtDefine_LandmarkTypes` declares
  them). The loader increments a counter per handler in the library (library+0xB4 + 4 x handler, library =
  0xDB1F30, so the counters are 0xDB1FE4..0xDB2000) [C]. The counters size the managers' lists and the handler is
  read by the list builder 0x64B540 (section 2.5) [C]. *(Corrected 2026-11-15 by 0338; was: "reads the field nowhere
  else".)*
- `BindAsset` (0x77DE00) loads the `.fxsxml` with the row's scale, then looks for a Granny extended-data block
  (tag 0x6D746464) whose tracks give **layout variants**: the number of variants is the largest track count of type 1,
  capped at 4 (entry+0x176C); for each mesh part (up to 16, 0x15C bytes each) it samples one transform per variant
  [C for the code, I for "variants = alternative arrangements of the model's parts"].
- An operand scan of the whole EXE for 0x1768 finds, apart from the loader and the setter, only stack-frame offsets
  of unrelated functions (seven); a scan of the absolute addresses of the counters finds nothing [C]. *(Corrected
  2026-11-15 by 0338: an operand scan sees only instructions Ghidra has disassembled. A raw byte scan of the
  executable sections (0338's `b:` mode, `evidence/0338/Dec.java`) finds `CMP [EDI+0x1768],ECX` at 0x64B569 and the
  counter reads at 0x4734D3 (0xDB1FE4), 0x4F8D40 (0xDB1FF8), 0x4F8D46 (0xDB1FF0), all in code outside any function
  of the project. Every "no reader" result of an operand scan is therefore [I] until a raw scan agrees. Was: "so 'the
  handler changes nothing at run time' is [I]".)*

### 2.2 The generic instance [C]

- The landmark sim keeps a record of 0x90 bytes per landmark (hex coordinates at +0x10/+0x12, flags at +0xC: bit 0x100
  "has a drawn instance", bit 10 "disabled", bits 11-15 a 5-bit type) and creates the drawn instance through 0x655640
  from the listeners 0x5307A0 and 0x603000 [C]. *(Narrowed 2026-11-15 by 0338: these records and listeners belong to
  the farm manager at sim+0x24 only; the 5-bit type is the farm-row slot and 0x5307A0 is its era change (2.5).
  Generic, animated and SPECIAL landmarks have no such record; they are instances in slot arrays of 0xC98 bytes.)*
- 0x754DB0 initialises an instance from its library entry: with no variants it copies every part's matrix; with
  variants it picks `rand() % variants` once and multiplies every part's matrix by that variant's transform. It
  copies the part count, the TerrainContour byte (instance+0xC54) and a class (1 improvement, 2 resource, 0 feature)
  [C]. **The random variant pick is per instance and not stored in the game state**, so a faithful replay needs the
  same pick or its seed [I].
- Listener 0x5307A0 maps a value of the event (0-2 -> 1, 3 -> 2, 4 -> 3, more -> 4) to one of five model levels per
  sim type and falls back to the next lower level that has a model [C]; which event and which value (era index is the
  likely one, `Era` `Ancient`/`Industrial` rows) is [I].

### 2.3 The six handlers in use: what each changes at run time (rewritten 2026-11-15 by 0338) [C]

What the handler decides is **which layout manager may use the row, and how a row is chosen among several that
match** (section 2.5). The manager then fixes the placement (2.6). Row counts from the database copy of 2.4.

| Handler | Rows | What the rows are | Manager and row choice | Placement | Mark |
|---|---|---|---|---|---|
| SNAPSHOT | 541 | improvements, resources, features, natural wonders; contour 1 on 506, 0 on 34, 0.75 on 1 | generic manager, list A: the best match (2.5) | plot centre at z = 0, the model's random variant (2.2), TerrainContour culling (2.4) | C |
| RANDOM | 27 | barbarian camps (3), goody huts (Ruins1-10, 10), Kasbah (5, contour 0), Oppidum (3), atolls (5), ford (1) | generic manager, list B: only if no SNAPSHOT row matches (Oppidum has SNAPSHOT rows for some states, which then win), `rand() % n` over all matching RANDOM rows (0x6029B0) | as SNAPSHOT | C |
| ANIMATED | 62 | camp, fishing boats, pasture (cow, sheep), well, polder, 14 animated resources | animated manager (sim+0x207e8), list A': best match | plot centre at z = 0, the random variant, **no** contour culling (0x42BD10 -> 0x554450); the idles come from the model (`<Animation>`, `LandmarkDecisionTree4.ddtxml`, model-reader.md 6.4) | C; idles I |
| ANIMATED_ROTATED | 4 | horse and super-horse, as resource and pasture | animated manager, list B': only if no ANIMATED/PASTURE row matches, `rand() % n` (0x51B170 sets a flag that 0x6459F0 passes on and 0x42BD10 never reads) | as ANIMATED: **no rotation** of any kind | C |
| FARM | 24 | 6 generic farm buildings (`RESOURCE_NONE`, Medieval_Farm1-3 Ancient, Modern_Farm1-3 Industrial) and 18 wheat/maize/rice rows | in no list. The farm manager (sim+0x24) takes rows by name: improvement `ART_DEF_IMPROVEMENT_FARM`, resource none, era 1-4, state 1, up to 3 per era (0x539370) | its own records and placer 0x4BE160 (2.6) | C |
| SPECIAL | 9 | Moai statues, Feitoria dock, pontoon bridge (3 states each, resource none) | in no list. The SPECIAL manager (sim+0x208d8) takes them by improvement name and state (0x621230) | one placer per improvement (0x67E6D0 Moai, 0x685BE0 Feitoria, 0x477EA0 pontoon bridge), seeded by the plot (2.6) | C; their detail I |

Consequences [C unless marked]:
- The 18 wheat/maize/rice FARM rows (12 with a model, `Medieval_Farm.fxsxml` and so on) are in no manager's list and
  are not resource-none, so none of the four managers can choose them. The farm manager also resolves
  `ART_DEF_RESOURCE_WHEAT` (farm+0x4A8), and where that value is used was not read. Until it is, "these rows are
  never drawn" is [I]. On wheat/maize/rice farms the generic manager finds SNAPSHOT rows with model `None` (-2, draws
  nothing).
- Moai and Feitoria have SNAPSHOT rows too (resource ALL, contour 0). The generic manager draws those: Moai `None` when
  constructed or under construction and `PL_Moai_Statues_FX` when pillaged, and Feitoria's land building
  (`Feitoria.fxsxml`). The SPECIAL manager draws the statues and the dock. [I] that both managers act on the same
  plot event; the listeners of both were read, not a capture.
- PASTURE (3) rows would join the ANIMATED list; there are none. SPRING (4) is collected by no manager, so a SPRING
  row would never be drawn.
- **Atolls (ticket 0389, 2026-11-15)** [C files, L pictures]: each of the five RANDOM atoll rows is one flat textured
  quad (Scale 2.4, about 84 world units wide) whose texture alpha holds the atoll, and the five differ in shape:
  `Atoll_3` is an open C (islets on the left arc only), `Atoll_2` an open square. A C-shaped atoll is therefore stock's
  own picture whenever `rand() % 5` gives variant 3, on the product path as in stock; the variant is not stored and
  differs per session, so two sessions of one save rarely show the same atolls. Details and pictures:
  `landmarks-and-models.md` 7.

Natural wonders are SNAPSHOT feature rows whose models are effect anchors only; the mountain is terrain
(`landmarks-and-models.md` 4.7). Roads, railroads and farm-field decals are epic 0166's; the bridge models of the
road layout are package D3 (not read here).

### 2.4 TerrainContour: cull the parts that touch a river (rewritten 2026-11-15 by 0338) [C]

- **Storage.** Column 5 of the query is read with a column getter that is not the float getter used for `Scale`
  (import slot 0x9D6090 against 0x9D6088) and stored as one byte at entry +0x1750 (0x5CCB3F); the XML loader names the
  attribute `bTerrainContour` (0x5BD970) [C]. A new entry and a new instance default to 1 (0x44C2C0, 0x740A90).
  0x754DB0 copies the byte to instance+0xC54 [C]. How the one 0.75 row (`ART_DEF_IMPROVEMENT_MOUNTAIN_CITY`,
  `MachuPicchu.fxsxml`) converts was not determined (0 under an integer read, 1 under a boolean one).
- **The one reader** is 0x6919E0 (`CMP byte [EBX+0xC54],0` at 0x691A46), called by the generic manager's two
  placement paths only: create 0x66ADF0 and re-place 0x64C2D0 [C]. An operand scan and a raw byte scan for 0xC54 find
  only it, the copy 0x5C3CF0, the initialiser 0x740A90 and 0x754DB0; the draw record built by 0x7B6050 does not carry
  the byte [C].
- **What it does** [C]. For each mesh part (count at instance+0xC50, parts at +0x84 with a stride of 0xC4 bytes, the
  visible flag at part+0x40, so part 0's flag is instance+0xC4): multiply the part's matrix by the placement matrix.
  If the model has exactly one part, stop. Otherwise, if the contour byte is set, take the part's origin (the matrix
  applied to (0, 0, 0, 1)), build the four corners (x +- 4.0, y -+ 4.0) of a square around it (0x9F7B40 = 4.0), and
  test each corner with 0x4365E0 against bit 8 of the plot mask grid (2.7). If any corner hits, set the part's visible
  flag to 0 and count it. Afterwards: if the hidden count is at least `n/4 + n/2` (integer halves of the part count
  n), call 0x9D57D0, which runs an EXE-file check (0x474DF0 opens the EXE by file id) and changes nothing visible; if
  every part is hidden, set part 0 visible again. 0x7B6050 then gives hidden parts the index -1 in the draw record.
- **Meaning**: "contour" = keep the model's clutter off the river. Bit 8 of the grid is written only by the river mesh
  build (2.7), so the test asks "does this part stand within 4 units of a river triangle". It never moves a vertex or
  a part, and it does nothing on a one-part model or on a contour-0 row.
- Rows with 0 [C, database copy below]: 39 of 667. Brazilwood camp (under construction), Chateau (3), Customs House
  (2), Feitoria's land rows (3), Fort and Wajo (6 each, two eras), Kasbah (5, RANDOM), Lumber Mill (under
  construction), Moai's SNAPSHOT rows (3), oil (4: `Oil1`, `Oil_Heavy2`, `Oil_water`, `Oil_water_heavy`), Tersane (2),
  Tipis (3). *(Corrected 2026-11-15 by 0338; was: "and 5 of the 27 RANDOM rows (atolls)"; the five RANDOM rows with
  0 are Kasbah, and the atolls have 1. The [I] reading "0 = rigid, 1 = bend to the terrain" is replaced by the bullet
  above.)*
- The animated manager (0x42BD10 -> 0x554450), the farm manager (0x4BE160) and the SPECIAL placers never call 0x6919E0:
  their contour byte has no effect [C].
- Database: copy of c5h-c's `Civ5DebugDatabase.db` (CP + VP + EUI-compat, written 2026-11-15 01:34, SHA-256
  `27fc81c6...2fd19b4`, the copy 0319 used), opened read-only; queries in `evidence/0338/dbq.py`.

### 2.5 The four layout managers of the landmark sim (0338) [C]

The landmark sim object 0x2D70310 (cvLandmarkSimSystem) is set up by 0x473440 (code outside any function of the
Ghidra project). It holds four managers, each with its own pointer to the library 0xDB1F30. The handler is read by one
helper, 0x64B540 (`collect(list, h)`): append (entry, index) for every library entry whose handler (+0x1768) equals h.

| Manager | Object | Set up by | Rows | Lists |
|---|---|---|---|---|
| generic | sim itself (library at +0x60BC8) | 0x473440 | handler 0, handler 7 | A at +0x60C7C sized by counter 0 (0xDB1FE4); B at +0x60C88 sized by counter 7 (library+0xD0) |
| animated | sim+0x207E8 (library at +0x24) | 0x4F8D00 | handlers 5 and 3; handler 6 | A' at +0xD8 sized by counters 5 + 3 (0xDB1FF8 + 0xDB1FF0); B' at +0xE4 sized by counter 6 (library+0xCC) |
| farm | sim+0x24 (library at +0x6D4) | 0x74CDC0; rows 0x539370 | `ART_DEF_IMPROVEMENT_FARM`, resource none, era 1-4, state 1 | table at +0x4B0: 3 slots x 5 era levels of entry indices, count per era at +0x4EC |
| SPECIAL | sim+0x208D8 (library at +0x228) | 0x4D8FD0; rows 0x621230 | Moai statues, pontoon bridge, Feitoria, resource none | per improvement, the entry for each of three states (state codes 2, 4, 8 at +4/+8/+0xC); a 255 x 255 per-plot table of 4-byte cells from +0x2C8 |

0x505400 (called from the startup 0x4686C0, "Landmark&DynamicGeometrySystem::StartupGame()") fills the SPECIAL and
farm tables. The generic and animated managers listen to the same landmark events (event fields: +4 resource, +8
improvement, +0x18 plot, +0x20 era, +0x24 state, +0x28 the rest of the key); for a resource-only event the improvement
is passed as -1 [C for the listeners 0x47D980, 0x443F70, 0x796770; I that every landmark event reaches both].

**Row choice** [C]:
- Best match 0x73B8E0 over a list, scanning from the end: same improvement (or, for improvement -1, same resource
  only); resource equal or the row's resource `ALL` (-2); the row's era <= the asked era, or either is 0; the row's
  state equal, or either is 1 (`Any`). Among matches, the highest score of 0x5259E0 wins (1 without a tech, 2 when the
  row's tech is known, 0 when it is not, so a row whose tech is unknown never wins); ties keep the later row of the
  list (the scan goes backwards and needs a strictly higher score). A winner whose model file name is empty returns -2:
  nothing is drawn.
- Random match 0x6029B0: collect every match with the same tests (0x553480; if any match has a tech, only rows with a
  tech are kept), then `rand() % count`. The CRT `rand` is the process-wide one.
- Each caller tries the best list first, then the random list. The create listeners (0x4740C0 generic, 0x51B170
  animated) ask once; 0x700B90 and 0x6551A0 (and 0x608AF0, whose decrement the decompile does not show) repeat with
  each lower era down to 0. Since the era test already accepts every row of an earlier era, an Ancient and an
  Industrial row of the same key both match an Industrial ask, both score 1, and the later one in library order wins.
- At an era change the generic manager keeps an instance's row if it is still among the candidates for the new era
  (0x6551A0), else picks again and re-places the instance (0x537490 -> 0x608AF0 -> 0x64C2D0). A RANDOM landmark can
  therefore change its model at an era change.

### 2.6 Placement per manager [C unless marked]

- **Generic** (0x66ADF0 create, 0x64C2D0 re-place): the placement matrix is a translation to the plot centre
  (0x6851C0 of the hex coordinates) at z = 0; then 0x754DB0 (variant pick, 2.2), 0x6919E0 (contour, 2.4), 0x7B6050
  (draw record). The instance's first byte gets the state bits (event +0x24 = 2 -> 0x08, 4 -> 0x10, 8 -> 0x20, else
  0x04) and two bits from event +0x1D. No rotation.
- **Animated** (0x42BD10, also reached by 0x693390): the same translation, variant pick and draw record, with 0x554450
  in place of 0x6919E0 (parts multiplied, nothing hidden). No rotation, also for ANIMATED_ROTATED.
- **Height**: both write z = 0 into the placement. The terrain height is added later, where was not read in this
  pass (`sources/shell-gates/stubs/coupling.md` finding 5: Landmark reads the heightmap) [I].
- **Farm** (0x4BE160, for the farm manager's 0x90-byte records): start at the plot centre; if the record lists curves
  (count at record+5, ids at record+0x20, a table of 0x2C-byte entries at farm+0x6F4 whose points are 0xFC-byte
  segments), move to the **centroid of the curve whose centroid is nearest the plot centre**, among the curves whose
  value 0x867D20 is at least 100.0 (0x86F940 is the area-weighted centroid, not a nearest point; corrected 2026-11-22 by
  ticket 0657, 2.6.2; replaced "move to the nearest point on those curves (0x86F940, kept when 0x867D20's value is at
  least 0x9E7230)");
  clamp into the map's bounds; read a direction from the field 0x44DA00 at that point, normalise it, and add a random
  quarter turn (`rand() & 3` times 0x9FD008, read as pi/2 [I]); the matrix is that rotation about z plus the
  translation (x, y, terrain height from 0x530A10). Every part gets the same matrix; no variant multiply, no contour
  test. The listener 0x5307A0, for the records whose owner byte (+8) equals the event's +8, maps the event's value at
  +4 (the era [I]: 0-2 -> level 1, 3 -> 2, 4 -> 3, more -> 4, as the SPECIAL listener does with the event era) to the slot's
  row, falling back to the next lower level that has one; 0x603000 re-creates the instance of a plot, and 0x754330
  starts the `ART_DEF_VEFFECT_FARM_PILLAGED_SMOKE_01` effect when the record's byte +4 is 8 (pillaged [I]). The curves are the farm manager's, written
  into the mask grid as bit 1 and an outline pushed out by 0xA0B0B4 as bit 0x10 (0x543D10) [C]; that they are the
  farm-field outlines of 0166's farm-field builder is [I].
- **SPECIAL** (listener 0x59D550): for an event on a plot whose improvement is one of the three, take the entry for
  the event's state, the era level as above, and call the improvement's placer: 0x67E6D0 (Moai, 3107 bytes), 0x685BE0
  (Feitoria), 0x477EA0 (pontoon bridge). Each placer first calls **`srand(q + r)`** with the plot's two hex
  coordinates (it reseeds the process-wide CRT generator), reads six neighbour bytes of the per-plot table at +0x2CA
  through the hex direction table 0xC8FB10, builds points along the plot's edges where a neighbour byte is set,
  jitters them by `rand() % 1000` steps, drops points closer than 0xA0888C to each other, and gives each element its
  own facing (`atan2` of the edge direction plus 0x9FD008) and the terrain height (0x530A10). The element count is
  the entry's part count. [C for the calls and the seed; I for "neighbour byte = water", the constants' values were
  not read, and the pontoon and Feitoria placers were read only for these calls.]

### 2.6.1 The farm buildings without the farm-field builder: can the farm manager make them itself? (ticket 9078, 2026-11-21) [C unless marked]

Read only, EXE build 4390913 (the analysed DX11 image, ImageBase 0x400000), headless Ghidra 12.1.4 on a scratch copy of
the `civ5` project (`C:/c5-64bit-work/9078/`, nothing of it committed, D83), 2026-11-21 18:05-18:12 EST. No game was run.
Question (the Architect's note of 2026-11-21, item 5, `../07-architecture-decisions/architect-2026-11-18-cities-landmarks-switch.md`):
with `[Renderer] ServerFarmFields = 1` the builder 0x748FC0 is never called and no farm building is drawn
(`../02-exe-internals/farm-field-double-free.md` 4.4 [L]); can the farm manager still make each farm's building?

**Verdict: YES, by one existing post per farm event, not by any event the EXE already sends.** The building is made only
by the farm manager's farm-end listener 0x603000, which only a type-5 record of the 4 MB queue reaches, and only the
builder posts type 5. The record and its "building wanted" entry exist without the builder; what is missing is the
builder's last step. That step needs no field record: with no curves the placer keeps the plot centre.

**(1) Callers** [C, Ghidra references and a scan of every 4 MB queue poster]:
- The placer 0x4BE160 has two direct callers: 0x603000 (at 0x603178) and the era listener 0x5307A0 (at 0x5308B0).
- 0x603000 has no direct caller. It is a listener only: registered on A 5 by 0x582970 (at 0x5829E3, through the thunk
  0x870380) and named again in 0x656C20 (called by 0x6D38A0; a second list of the same nine listeners through other
  thunks, read as the removal at shutdown [I]).
- The farm manager's listeners in 0x582970, in order: B59 0x7040D0, B60 0x4349F0, B48 0x5307A0, 0x587170 (updates a
  record byte +6 and the instance's bits through 0x4A86E0, read as a visibility update [I]), A5 0x603000, A3 0x7AD6C0,
  A6 0x6DEC60, 0x582FD0 (copies a 332-byte block, the A36 settings block, as the builder's 0x4C2CA0 does), 0x785C50
  (copies 0x84 bytes).
- **Who posts type 5**: the poster 0x48E810 (allocates 0x80 bytes in the double-buffered 4 MB queue with an interlocked
  add, writes type 5, returns the payload for the hex x and y) has one caller, the builder at 0x749105, which passes
  `ECX = 0x1A0F080 + [0x1A0E400] x 0x800180`. Of the 160 sites in the image that compute a 4 MB queue slot
  (a multiply by the slot size 0x400080), 154 store a constant type within the next 14 instructions; type 5 is stored only in 0x48E810
  (at 0x48E842). The other six are the queue's set-up 0x423910, two reads of the buffer offsets in 0x490DF0, a pump step
  in 0x459D20, a clear in 0x580C50, and 0x6F0822 (type 0x72, stored a few instructions later; checked). So no other code
  posts a farm end.
- The builder posts type 4 (farm begin, 0x503820, also only called from the builder) before its layout and type 5 after
  it, on both of its paths (first layout and "already laid out"), whenever its three tests pass: the event's flag byte
  +0x1C is non-zero, the plot is on the map (0x42B440) and the event's art id (+8) is `ART_DEF_IMPROVEMENT_FARM`
  (`farm-fields-layout.md` 1.2). No listener of type 4 is in the farm manager's list.

**(2) What the two functions do for a record with no curves** [C]:
- The B59 listener 0x7040D0 makes the 0x90-byte record (0x6D4580) with the row already chosen (record +0, from the
  slot's era table through 0x6E54A0, which steps down to the next lower era with a row; -1 when none) and **appends the
  record's index to a pending list at farm manager +0x458** (0x5AC7B0, call at 0x7042B6; count at +0x45C). It creates no
  instance.
- 0x603000 (event payload: one dword, hex x in the low and hex y in the high 16 bits): if the manager is deferring
  (+0x207C0 bit 0) it queues the event for later and returns. Else it walks the pending list: an entry whose record is
  freed (record +0xC bit 0x400) is dropped; for the record whose hex x, y (+0x10, +0x12) equal the event's it frees an
  existing instance (flag 0x100, handle at +0x84, 0x5B29C0), and **if the record has a row (+0 != -1)**: posts an A33
  record (type 0x21, poster 0x462360) carrying the plot's rectangle (0x418B60, the 128 x 110.85 square of
  `farm-fields-layout.md` 1.3), calls the placer 0x4BE160, and **when the placer reports a part count other than 0**
  creates the instance (0x655640, handle into +0x84, flag 0x100), then runs the pillaged smoke 0x754330 (state 8), and
  drops the entry. A record without a row stays pending. **There is no test of the curve count**: the only conditions
  are the deferral bit, the coordinates, the row and a non-zero part count of the row's library entry.
- The placer 0x4BE160 with a curve count (record +5) of 0 skips the curve loop: the position stays **the plot centre**
  (0x6851C0 of the hex), clamped into the map's bounds; the height is the terrain's at that point (0x530A10); the facing
  is the direction field 0x44DA00 at that point plus `rand() & 3` quarter turns, as with curves. Every part gets the
  same matrix; the part count is the library entry's (+0x1740). It also writes the position into the record
  (+0x14..+0x1C), where the pillaged smoke reads it.
- The era listener 0x5307A0 acts only on records that already have an instance (flag 0x100): it writes the era level
  (+7) and, when the row changes, frees and re-creates the instance through the placer. **It never makes a first
  instance.**

So the reason no building is drawn with the option on is exact: every farm record waits in the pending list for a type-5
record that only the builder posts. (This replaces the inferred mechanism of `../02-exe-internals/farm-field-double-free.md`
4.7, "the instance must be created on the field path, which only the builder's type-3 records start": the field records
play no part, the farm end does. It also corrects `farm-fields-layout.md` 10.3 "A5 from other posters still re-places
buildings": there is no other poster of A5. Both statements are superseded by this subsection from 2026-11-21.)

**(3) The route, its thread and state, and what could go wrong** [C for the code, I for the risks]:
- **The call**: where the composite's builder hook (`farm_guard.cpp` `FgHook`, `farm-fields-layout.md` 14.1) returns
  before the builder, do the builder's last step and nothing else: under the builder's own three tests, call the EXE's
  poster 0x48E810 with `ECX = 0x1A0F080 + [0x1A0E400] x 0x800180` (the queue object the builder uses) and write the
  event's hex x (+0x18) and y (+0x1A) into the two 16-bit fields it returns. That is a call of the EXE's own function
  from a listener node the composite already owns, with no instruction byte changed (D136, D144, D228). Posting type 4
  as well is not needed.
- **Thread and state**: the hook runs inside queue B's dispatch of the B59 event, on the thread that pumps the queues, at
  the moment the builder would post. The poster is interlocked, so the post itself is safe; the 4 MB queue is pumped
  after queue B's batch (`load-sequence.md` 1.1), so 0x603000 runs after 0x7040D0 has made the record, whatever the
  listener order in B59. 0x603000 needs no lock and no pointer beyond its `this` (the farm manager, landmark sim + 0x24),
  which the queue supplies, and it handles the deferral itself. A direct call of 0x603000 is also possible (thiscall,
  `ECX` = the farm manager, argument = pointer to the packed hex dword) but needs the pump's thread, a record already made
  and the deferral bit clear; the post avoids all three and is the recommended form.
- **The mask bits**: 0x603000, 0x4BE160, 0x5B29C0, 0x754330 and 0x655640 call none of the mask rasterisers (0x62AE50,
  0x68FCB0) and none of the curve functions that stamp bits 0x01 and 0x10 (0x543D10, 0x46BFE0, 0x86F690). **Bits 0x01
  and 0x10 stay unwritten.** The instance is the landmark sim's ordinary visualizer (created through the deferred command
  0x66FEE0 -> 0x64E930); that shared path reaches 0x85EB80 -> 0x85E7F0 -> 0x700030, the bit 0x02 footprint setter of
  2.7, so a building whose model has footprint parts cuts the trees under itself as in stock [C for the call edges, I
  that farm models have such parts].
- **The track strip builder: the route does reach it, through the A33 that 0x603000 posts.** The builder's A33 listener
  0x79D040 stays registered with the option on (`farm-fields-layout.md` 14.1); it marks the graph grid cells under the
  plot's rectangle dirty and re-lays them through 0x6E27D0, which calls 0x55D0C0 for each dirty cell. With the option on
  the graph holds only the terrain bake's lines and no cell has the margin bit 0x10, so no strip can be made (14.8 there
  [L: 0 track records in 22 huge loads]). The same path already runs today in play: 0x421280 posts an A33 over seven
  plots whenever a farm record is removed (every B59 on a farm plot that has a record, and every B60). What the route
  adds is one A33 per farm event at load (373 on the huge save) where today there are none. 0537's write fault needs a
  0-point edge from the builder's layout [I, 14.8 there], which the bake's lines are not. If the MC wants the strip
  builder at zero calls, the composite can also skip the builder object's A33 listener 0x79D040 with the option on, the
  same kind of node change as `FgHook` [I: 0x421280's A33 is then dropped too, which the option allows since the server
  draws the tracks; other users of A33 not read].
- **Other effects** [I]: one CRT `rand()` per placement (the quarter turn), as in stock; the EXE's `rand` is not game state
  (D164 identity held with the builder skipped, `farm-fields-layout.md` 14.7 stage 3). The building stands at the plot
  centre on the server's fields, not where stock puts it (stock moves it to the centroid of one of the plot's fields, 2.6.2;
  "stock snaps it to the nearest curve point" corrected 2026-11-22 by 0657). The pillaged
  smoke comes back with it (0x754330, state 8). Later B59 events on the plot (pillage, repair, rebuild) wipe and re-make
  the record (0x7040D0 -> 0x421280) and the hook posts again, as stock does. With `ServerLandmarks = 1` the building
  reaches the server through the model tap like every other landmark; with 0 the EXE draws it.
- **Size of a live trial** [I]: 1-2 h of code in the composite (the post under the three tests, a count in
  `farm-builder-calls.log`), then one huge load with the option on (1 h): one building per farm with a row in the
  picture pair of `farm-field-double-free.md` 4.4 (or in the farm landmark instance count); still 0 field and 0 track
  records in the decal tap; still 0 cells with bit 0x10 in the mask grid; one post per farm event in the composite's log;
  no fault in 0x55D0C0.

**(4) Does an event the EXE already handles reach 0x603000 without a new call? No** [C]. The era listener 0x5307A0
re-places only existing instances; the B59 listener 0x7040D0 only queues the record; the A3 field listener 0x7AD6C0 only
adds curves and decals; the farm end comes only from the builder's type 5 (above). The re-post path behind the CEG thunk
0x42B4F0 is itself inside the builder.

**(5) The live trial (ticket 0655, 2026-11-21 18:37-20:45 EST, both EXE builds; `../02-exe-internals/farm-builder-guard.md` 13)
[L]**: the route of (3) built as described (the post under the builder's three tests, nothing else of the builder) and
run on copies of the huge save `C5H-VP8P-HUGE_0350` (373 farm plots) with the first beta's option set and
`ServerFarmFields = 1`, 5 launches and one in-session load on 4390913, 15 launches on 26577625 (10 huge, 5 on the
oversize 43-civ save), 0 crashes, 0 boxes, 0 LOUD lines from the farm path. What it confirms and corrects of (1)-(4):
- **Confirmed**: one farm-end post per farm plot at load (373; the other 827 type-59 events fail the builder's flag test);
  the farm manager's pending list (+0x45C) is 0 after the load with the post and 373 without it (4390913, read only):
  the records wait for exactly this farm end. Every farm plot gets its building **at the plot centre**, in the owner's
  era style, drawn by the EXE (`ServerLandmarks = 0`) or by the server from the model tap (`= 1`); pillage brings the
  pillaged smoke and the burnt model, repair, removal and rebuilding work, a second load in one process starts clean.
- **Confirmed**: the A33 of (3) reaches the builder object's area listener once per farm end (373 calls at the huge load,
  measured by a diagnostic timing probe on its node), 44.8 us mean, flat over the 373 (no growth with the number of
  posts); 0 farm-field and 0 farm-track records in the EXE's decal set; 0 field and 0 margin (0x10) cells in the EXE's cut
  grid; the layout graph keeps the bake's 754 nodes after load, play and turns.
- **Answered (was not verified)**: farm building models do have footprint parts: the cut grid's bit 0x02 cells rise from
  864,596 (post off) to 925,918 (post on) on the huge save (+61,322, about 164 cells per building), so a building cuts the
  trees under it as in stock [L, 26577625].
- **Refined**: the era change re-styles the building (with the city) when the turn ends, not at the moment the tech is
  given (techs given by Lua mid-turn, tiny save) [L]; 0x5307A0 runs then.
- **Refined**: a removal (the improvement set to none) reaches the farm manager without a type-59 hook call (B60 0x4349F0
  in (1)) [L: no event line for it].
- Size: the code took about 2 h as (3) estimated; the trial added the switch `farm_end_post` and the diagnostic
  `farm_area_probe`.

**Not verified** (9078, 2026-11-21 18:12; items run since by 0655 are answered in (5)): nothing was run; the instance count with the route is not measured; that 0x656C20 is the unregistration
was not read; whether farm building models have footprint parts (bit 0x02); the 4 MB queue scan reads 14 instructions
after each slot computation, so a poster that stores its type later would be missed (one such, type 0x72, was found and
checked); no address of EXE build 26577625 was read (D202), so the route's addresses on that build must come from the
build table, not from this note.

### 2.6.2 Where the farm placer puts the building, and what a curve in the record costs (ticket 0657, 2026-11-22) [C unless marked]

Read only, EXE build 4390913, headless Ghidra 12.1.4 on a scratch copy of the `civ5` project (`C:/c5-64bit-work/0657/`,
nothing of it committed, D83), 2026-11-22 08:10-08:40 EST; constants read from the golden copy's `.rdata`. Question
(ticket 0657, the Architect's idea of 2026-11-21): with `ServerFarmFields = 1` and the farm-end post of 0655 the placer keeps
the plot centre (2.6.1 (2)); can one curve point from the server's own fields, written into the farm record before the
farm end, make it put the building where stock does? **Build 26577625 was not read (D202); the rule below must be checked
against that build's placer before anything is built on it (the MC, queue #10136).**

**(1) The placer's rule** (0x4BE160; 2.6 corrected by this): with the record's curve count (+5) above 0 it walks the curve
ids at +0x20 (uint16 each, at most 50). For each curve it computes
- `F` = 0x867D20 = 0.5 x sum over the closed point loop of `x_prev x (y - y_prev)` (constant 0x9FCF90 = 0.5) [C]. This is
  not the polygon's area: for a polygon of area A it is `0.5 A` plus a quarter of `sum dx dy` (exactly `0.5 A` for an
  axis-aligned rectangle);
- `c` = 0x86F940 = the exact area-weighted centroid: `sum (x + x_prev) cross / (3 x sum cross)` with `cross = y x_prev -
  x y_prev` (constant 0xA0A144 = 3.0) [C];
- the curve takes part only when `|F| >= 100.0` (0x9E7230); among those, the one whose centroid is **strictly nearest the
  plot centre** (squared distance, start 0xA07094 = FLT_MAX; the first wins a tie) gives the position. Without such a
  curve the position is the plot centre (0x6851C0 of the record's hex x, y; ECX = [farm + 0x64C], the terrain object:
  hex matrix 0xD91030 row vector, then `x x [T+0xA8] - [T+0xB4]`, `y x [T+0xAC] - [T+0xB8]`, i.e. the world frame of
  `heightfield.h` `sampleToWorld`).
- Then: the height 0x530A10 at that (unclamped) point; the point clamped into the map box (0xBA1E14..0xBA1E20, inset by
  1.0) only for the direction field 0x44DA00; the facing = that direction + `rand() & 3` quarter turns (0x9FD008 = pi/2);
  the record's +0x14..+0x1C get the unclamped point and height. One `rand()` per call whatever the curves.

So stock puts each farm building **in the middle of one of its own fields** (the field nearest the plot centre), not on an
edge and not at the nearest outline point; the "edge" look of the stock pictures (0655, 13.7) is [I] the model's own
ground decal (`Farm_Decal` mesh of each farm model, `farm-fields-layout.md` 16.1) over the field. Ticket 0653's record
(`render/src/farmfields/buildings.cpp`, `farm-fields-layout.md` 16.3: nearest outline point, no threshold) follows the old
reading and is not stock's rule.

**(2) The curves' frame**: world x, y (the frame of 0x6851C0's result), the same points the field decal is made from
(`farm-fields-layout.md` 4.2 step 4: sample x = origin x + px, sample y = (origin y + py) x 1.1547344). A server field
corner (cx, cy) in the sample frame is the world point (cx x sx - ox, cy x sy - oy) of the height field (sx 1, sy 0.866).

**(3) Is one synthetic point enough? No.** A one-point loop has F = 0 and a zero cross sum: it fails the `|F| >= 100`
test (and its centroid would be 0/0). The least that works is a closed polygon with `|F| >= 100` whose centroid is the
wanted point, e.g. an axis-aligned square of side >= 14.15 centred on it (side 16: F = 128).

**(4) Who else reads the curves of a record** [C]:
- The era listener 0x5307A0 (B48): when an owner's era level changes the row, it calls the placer again on every record
  of that owner that has an instance. A record without curves goes back to the plot centre then.
- The record removal 0x421280 (from B60 0x4349F0 and from the B59 listener 0x7040D0 on every later event of a farm plot
  that has a record: pillage, repair, state change): for each curve of the removed record 0x51D5A0 (its decals, handles
  at curve +0x24 / +0x28) and **0x543D10 with 0 (clears mask bits 0x01 and 0x10 over the curve)**, then frees the curve
  slot (flag byte +0 = 1, free list at farm + 0x6EC, count farm + 0x6F0; the segments are not freed there); then for the
  six neighbours of the plot **0x543D10 with 1 on every curve of every neighbour record: the curve's triangles stamped
  with bit 0x01 and the polygon pushed out by 5.0 (0xA0B0B4) from its centroid with bit 0x10**. So a curve left in a
  record puts the field and margin bits back the first time a neighbouring farm changes; with the margin bit the track
  strip builder 0x55D0C0 can make strips again (`farm-fields-layout.md` 5.2). The only way 0x543D10 writes nothing is a
  failed ear clipping (0x86FAD0 returns 0; the whole body is inside that test).
- The A6 listener 0x6DEC60 (by field id at curve +0x1C; only the builder posts type 6) and the A3 listener 0x7AD6C0 (adds
  curves, then 0x5865B0 = masks and decals; only the builder posts type 3).
- 160 instructions in the image use the offset 0x6F4 (most of other objects); the farm manager's own readers found:
  0x421280, 0x4BE160, 0x51D5A0, 0x543D10, 0x5865B0, 0x6DEC60, 0x7AD6C0. Not every hit was sorted.
- The curve entry (0x2C bytes) [C]: +0 free flag, +4 next free, +8 the point store (its +8 = segment array), +0x10 first
  segment (low 24 bits; 0x0FFFFFFF none), +0x14 last segment (the centroid's loop starts from the last point through
  0x867CB0), +0x1C field id, +0x20 plot (uint16), +0x24 / +0x28 decal handles (-1 none). Segment (0xFC bytes): +0
  previous, +4 next, +8 point count (& 0x7FFFFFFF), +0xC up to 30 x, y float pairs. A curve slot comes from 0x6F69A0 ->
  0x7124F0 (a push on the table; whether the table exists at all with `ServerFarmFields = 1`, where no type 3 ever arrives,
  was not read).

**(5) What this means for the ticket's route** [I, design; nothing built]:
- A curve written into the record and left there is not a data write of one point: it needs the EXE's slot allocator, a
  point store, and it brings the mask bits back through (4). Either the polygon must fail ear clipping (not read in full),
  or the curve must exist only while the placer runs.
- **Transient form**: the curve exists only inside the two calls that place: a pass-through on the farm manager's A5 node
  (0x603000, the farm end, the 4 MB A manager's type-5 list) and on its B48 node (0x5307A0, the era), as 0655's area probe
  does on the type-33 node. Inside the pass-through: point farm + 0x6F4 at a table of the patch's own (one entry and one
  segment per record placed, a square of side 16 about the server's point), set the record's +5 and +0x20, call the EXE's
  function, then put +5, +0x20 and +0x6F4 back. Nothing outside the call ever sees a curve, so (4)'s mask paths never meet
  one. Neither listener nor anything it calls was found to read +0x6F4 except the placer (0x603000's callees 0x5B29C0,
  0x462360, 0x418B60, 0x655640, 0x754330; 0x5307A0's 0x4CCA80 was not read). Both functions defer themselves while
  farm + 0x207C0 bit 0 is set by queuing a call of their own address (0x8676E0): such a deferred call bypasses the node and
  places at the plot centre; the pass-through has to hold those events itself.
- **Timing**: the farm ends of a load arrive in the EXE in one burst (`farm-fields-layout.md` 1.4); the server lays the
  load's fields out 0.75-0.8 s after the first plot table (13.2 there). The pass-through has to hold a farm end until the
  server's point for that plot is there (re-post the type-5 record from inside the dispatch, as the builder and 0x603000
  themselves post from inside a dispatch), with a time limit after which it places at the plot centre (as today).
- **The server's point**: the rule of (1) on the server's own fields of that plot (world frame of (2), `|F| >= 100` with F
  computed as the EXE does), carried to the proxy in a named section beside `.Trig` (`<VPSHELL_SECTION>.<suffix>`), one
  entry per plot with a load identity so that a second load in one process never takes the first load's points.
- Size [I]: the server's rule and section 3-4 h; the proxy's two pass-throughs with their patterns on both builds, the
  hold and its tests 8-12 h; live runs on both builds 3-4 h. The ticket's 3-6 h assumed a one-point data write.

**(6) Live check of the rule on stock** [L, 2026-11-22 08:42-08:49 EST, workstation, c5h-b, EXE build 4390913, the patch
off (no proxy), mods cp, vp, eui; a copy `C5H-0657-HUGE0350` of `C5H-VP8P-HUGE_0350` (turn 350), loaded, load screen
dismissed, no turn played; harness stop; the copy moved out and the saves verified by hash (`saves_snap.py restore`
`verify_ok`), front end restored, `provision.py verify` ok; MC GO queue #10136]. Probe
`shell/evidence/0657/farm_records.py <game pid>` (ReadProcessMemory only): the farm manager's 373 live records, each with
an instance and with curves (5,115 curves), the placed position at record +0x14 against the three rules:

| Rule | within 0.05 | 0.5 | 1 | 5 | above 5 | median error |
|---|---|---|---|---|---|---|
| (1): centroid of the nearest-centroid curve with `|F| >= 100` | 218 | 351 | 357 | 359 | 14 | 0.033 |
| the same with the exact area `|A| >= 100` | 148 | 232 | 236 | 237 | 136 | 0.097 |
| the nearest point on the outlines (the old reading) | 0 | 0 | 0 | 0 | 373 | 9.386 |

The old reading fits no record; rule (1) fits 357 of 373 within one world unit. The residue (errors of 0.05-0.5, and 14
records above 5) is [I] the curves changing after the farm end placed the building: a neighbour's later layout re-cuts
a farm's faces (type 6, then new type-3 curves, `farm-fields-layout.md` 4.1), while the building stays where it was put.
Of the curves, 2,469 of 5,115 have `|F| < 100` (577 have `|A| < 100`); `|F| / |A|` has median 0.500, as (1) says. World
frame of this load: s = (1, 0.866), o = (4096, 2327.81).

**(7) Build 26577625** [C, bytes only, no address of that build written anywhere, D202; 2026-11-22 08:50 EST]: the
placer's body (1,473 bytes, the 200 bytes of absolute operands and call targets masked) occurs exactly once in the new
EXE, and the values behind its operands are the same (100.0, FLT_MAX, 1.0, pi/2); the bodies of 0x86F940 (centroid),
0x867D20 (F) and 0x867CB0 (the loop's start) each occur twice there (identical look-alikes), 0x603000, 0x5307A0 and
0x543D10 once each (0x543D10's 5.0 equal); 0x421280's body does not occur as bytes (compiled differently, not read).
Scripts outside git (`C:/c5-64bit-work/0657/scripts/findfn.py`, `constcheck.py`). So the rule of (1) holds on both
builds as far as bytes can say; a live probe on 26577625 needs that build's addresses from the local table.

### 2.6.3 The building at the server's field centroid: the transient curve, built (ticket 0657, 2026-11-23) [C unless marked]

The design of 2.6.2 (5), built and tested; the guard's side is `../02-exe-internals/farm-builder-guard.md` 14, the server's
`farm-fields-layout.md` 16.3 and 16.6.

**(1) The second read** (4390913, headless Ghidra on the 0657 scratch copy, 2026-11-23 15:40-15:50 EST, nothing committed, D83):
- The placer 0x4BE160 reads per curve id k only the table entry `[fm + 0x6F4] + k x 0x2C` at +8 (a pointer to the point
  store, whose +8 is the segment array) and +0x10 (the first segment, low 24 bits); its two iterators start at (store,
  count of the first segment, first segment, 0) and end at (store, 0, 0x0FFFFFFF, 0); the step back 0x867CB0 from the end
  iterator takes the last segment from the store pointer's +0xC, i.e. the entry's **+0x14**, and that segment's last point.
  Segment links: +4 next (0x0FFFFFFF none), +8 count (& 0x7FFFFFFF), points from +0xC. Nothing else of the entry (+0 free
  flag, +0x1C field id, +0x20 plot, +0x24 / +0x28 decal handles) is read there [C].
- The farm-end listener 0x603000 and the era listener 0x5307A0 both test the deferral bit `fm + 0x207C0` first and then
  queue a call of their OWN address (0x8676E0), bypassing the node; 0x5307A0's callee 0x4CCA80 only initialises the
  instance description (no curve read) [C].
- The registers: A5 0x870380 takes the A manager's lock (0xCC733C / 0xCBA9F0, the same as type 33's) and the list slot
  `[0x1A0E400] x 0x600 + 0x1A0E434` (slot offset 0x30, list +4); B48 0x867370 the B manager's lock (0xCBC56C / 0xCBFC70)
  and `[0x2A10200] x 0x600 + 0x2A1038C` (slot 0x188). The group 0x582970 registers each listener with the manager itself
  (ESI = this) as the object [C].
- The 4 MB queue's pump 0x459D20 flips (0x490DF0: flip + 1, the new write buffer cleared) and then dispatches the OTHER
  buffer, whose size it read once: a post from inside a dispatch lands in the buffer pumped the next time, never in the
  one being walked [C]. So a re-post cannot loop inside one pump.

**(2) What runs** (`farm_guard.cpp`, the owner is the proxy): with farm_builder=0, farm_edge=1 and the post's sites
matched, the farm manager's two listener nodes call the pass-throughs `EdgeEnd` / `EdgeEra` (stubs of the shared page that
fall back to the EXE's listener while no owner is attached). For a farm end of plot (x, y): the view shut down -> dropped
(as F1); a stale node (the list also holds the EXE's own listener) -> nothing; the manager deferring, or the server's
section not DONE for the camera's grid seq, or the plot missing -> **held**: the same farm end posted again by the EXE's
poster and the call skipped (the record stays pending), at most 20 s (5 s for a plot missing from a DONE list), then the
EXE's listener as before 0657 (the plot centre); a CENTRE entry or no section -> the EXE's listener as is; a POINT entry ->
each live record of the plot gets curve k of the patch's own table: one segment of four corners, the square of side 512
centred on the point, counter-clockwise (|F| = 131,072, far above 100; its centroid is the point), the record's +5 = 1 and
+0x20 = k, then the manager's +0x6F4 = the patch's table; the EXE's listener runs (outside any `__try`); the table word,
+0x20 and +5 are put back. The era pass-through does the same for every live record with an instance whose plot has a
POINT entry, around one call of the era listener; while the manager defers it lets the listener queue itself (that later
direct call places at the plot centre, counted `eraDeferred`).

**(3) Why a square of side 512**: the EXE computes F and the centroid in float on world coordinates up to about 4,100. The
cross products of a polygon at that distance from the origin are about 1e7, each off by up to about one unit; the
centroid's error grows as (distance from the origin) x (summed error) / (3 x 2 x area). With side 16 (area 256) that is
several units; with side 512 (area 262,144) about 0.005. The test placer, which repeats the EXE's float loop from 2.6.2,
put the building 0.00024 from the point (`farm_guard_test` section 17) [L offline].

**(4) Not changed**: no curve is left in any record or table outside the EXE's call, so 0x421280's re-stamp of the
neighbours' curves (2.6.2 (4)) finds none, and the mask bits 0x01 / 0x10 stay unwritten; the farm-end post of 0655 is
unchanged; nothing is posted that the builder would not post (the re-post is the same farm end again).

**(5) Live** [L, 2026-11-23, build 26577625, c5h-new-b, the huge T350 copy, the first beta's set with `ServerFarmFields = 1`;
`../02-exe-internals/farm-builder-guard.md` 14.4]: 373 of 373 farm buildings at the server's points (max 0.0020 world
units), also after a pillage, a removal and rebuilding, an era change with one turn and a repair; 0 records left with a
curve; 0 EXE field and track records, 0 field and margin cells. The buildings appear about 6 s after the server's
production of the load is in force (the held farm ends).

### 2.7 The plot mask grid 0xBA392C [C]

A map-wide byte mask that the landmark placers test with 0x4365E0 (point, bits). Cells are 2 world units in x and
`2 / 1.1547` in y (y is multiplied by 0x9F3498 = 1.1547 = 2/sqrt(3) before the halving), offset by the map origin at
`[0xD90FC4]+0xB4/+0xB8`; storage is 16 x 16-cell tiles, each either one shared byte or 256 bytes (0x4A0A50). All
writers found go through two rasterisers, each with one caller: 0x62AE50 (triangles, bits, set or clear, called
by 12 functions) and 0x68FCB0 (called only by the river mesh build 0x6D51D0). Each change posts an event (type 0x20)
with its bounding box and bits.

| Bit | Written by | Meaning |
|---|---|---|
| 0x01 | river mesh 0x6D51D0 (with 0x08); farm manager curves 0x543D10 -> 0x86F690 | river and farm-curve footprint |
| 0x02 | per-element footprints of a visualiser: 0x85E7F0 -> 0x700030 sets, 0x85E900 -> 0x45B2A0 clears; 0x748DC0 (model parts of vertex type 3, clears), 0x65A150 / 0x69F250 (set), 0x680F80 (clear), 0x5EC600, 0x449C30 (squares of +- 0xA0B0B4, set) | object footprints; the model's `tree cut` meshes are the likely source [I] |
| 0x04 | 0x402C40 / 0x678EB0 -> 0x617E20 (set and clear) | not identified |
| 0x08 | **only the river mesh build 0x6D51D0** (0x68FCB0 with bits 9) | river footprint |
| 0x10 | farm manager 0x543D10 -> 0x46BFE0 (an outline pushed out from the curve) | farm-curve margin [I] |

The terrain bake 0x709B50 also replays a stored list of rasters with their own bits (0x709B50 at 0x70A0CC); its
entries were not read, so "only the river mesh writes bit 8" is [C for the direct writers, I for the replay list].
Readers: the contour cull (bit 8), 0x585490 (bit 8; on the farm-field chain of `load-sequence.md`), 0x7B43B0 (bit 8,
the city anchors of 1.12), 0x770EE0 and 0x6D7560 (bits 7), 0x55D0C0 (bit 0x10), 0x451DF0.

### 2.8 Open in this part

- Where the height of generic and animated landmarks is added (z = 0 at placement).
- The pontoon and Feitoria placers in full, the constants of the Moai placer (0xA0888C, 0x9EC9B8, 0x9FCF90,
  0xA0B0B4), and what the per-plot byte at SPECIAL+0x2CA holds and who writes it.
- The farm manager's curves (who fills farm+0x6F4) and the direction field 0x44DA00 (`[this+0x4068..0x4070]`;
  whether it is the city field of 1.9).
- The use of farm+0x4A8 (`ART_DEF_RESOURCE_WHEAT`) and so whether the 18 resource FARM rows are ever drawn.
- Bit 0x04 of the grid, the terrain bake's replay list, and who consumes the type-0x20 mask events. *Answered in part 2026-11-16 (4553, `trees.md` 14.1, 14.5): the Forest's listener `0x715720` consumes the 0x20 events (bits & 7); bits 0x01, 0x02, 0x04, 0x08 and 0x10 all seen set live, 0x20-0x80 never on the tested saves [L].*
- Nothing here is checked against a stock picture: 3910's landmark catalogue is not on main (2026-11-15), and no game
  was run. A capture of a multi-part landmark beside a river (parts missing), of a Moai coast and of a horse pasture
  (no rotation) would confirm the three findings that change the picture.

## 3. Breakdown for packages D2-D5 (sizes are raw agent-hours, [I])

Premise for all: the server has the model reader (`models/`, epic 3100), the units' mesh and draw layers (0041's U1)
and, for animated landmarks, the units' pose layer (U2). The EXE's shaders are used as the terrain does (D13).

| Package | Part | Hours | Depends on | Oracle |
|---|---|---|---|---|
| D2 landmarks | D2a library from `ArtDefine_Landmarks` (rows, states, eras, the handler enum, scale, contour byte) and `.fxsxml` binding, variants from the extended-data tracks | 3-5 | model reader | the counts above; 3910's catalogue |
| | D2b instance placement from plot state (improvement, resource, feature, pillaged, under construction, era), random variant and RANDOM row pick (seeded, recorded in the feed) | 4-8 | D2a, feed plot records | 3910's catalogue per object |
| | D2c terrain contour (2.4): rasterise the river mesh triangles into a 2-unit mask (bit 8), the 4-corner +-4.0 test per part of multi-part generic models, part 0 kept; placement height from the EXE's field (R4) once its point of application is read (2.8). Re-sized 2026-11-15 by 0338 from 2-5 | 2-4 | the river mesh triangles (water work, `water-system.md`); a short read of where z is added | **exact**: which parts are missing on multi-part landmarks beside a river (a catalogue capture) |
| | D2d animated landmarks: 22 models' idles through the pose layer | 3-6 | 0041 U2 | captured palettes of animated resources |
| | D2e SPECIAL placers (2.6): Moai, Feitoria dock and pontoon bridge, each seeded `srand(q + r)` per plot, edge points from six neighbour bytes, jitter, per-element facing, terrain height; ANIMATED_ROTATED needs nothing beyond D2b's random fallback (no rotation). Re-sized 2026-11-15 by 0338 from 2-4: the two placers not yet read in full (2.8) and the per-plot neighbour byte | 5-9 | a full read of 0x477EA0 and 0x685BE0 and the constants (N7c below); the CRT `rand` sequence (seeded per plot, so exact) | **exact** once read: element positions of a Moai coast, a dock, a pontoon bridge in a capture |
| | D2f farm buildings (2.6; added 2026-11-15 by 0338): the farm manager's slots by era, placement on the nearest farm curve, facing from the direction field 0x44DA00 plus a random quarter turn, terrain height, pillaged smoke | 2-4 | 0166's farm curves; the direction field (N7c) | farm buildings in a capture |
| D3 bridges | bridge models of the road layout (RoadLayout, `ToggleBridgeState`), pontoon bridge | 4-8 | 0166's routes; a short read of 0x5EC250 | stock capture of a road over a river |
| D4 cities | D4a city-set library (cultures, era slots, lighting, blocks by class) | 2-4 | model reader | 1.2 counts |
| | D4b city record from the six city calls (culture from art style, era slot, size bucket, damage level, walls art) | 3-5 | feed | the handler reads above |
| | D4c block placement (1.12): lattice, stamp clip and facing, obstacle clearing, the 40-round relaxation, the density field from the 70 `DF_*.dds` files with the 25 layouts, terrain tests, random block pick (seeded, recorded); re-run on population change. Re-sized 2026-11-15 by 0337 from 4-8 | 6-10 | D4b, D4e (stamps); the leftovers of 1.12 (map transform, mask bits, relaxation step) | **exact**: block positions and counts of the 8 studio cities in 3907's four frames (deterministic, 25-98 per city); meshes only after a recorded pick |
| | D4d walls (1.13): outline from the density map (hull, B-spline, 2-unit push), WALL runs bent along it (0x4CAA20 not read), TOWERs every 12 units, CPU-baked buffer, art set by walls tag; Great Wall not read. Re-sized 2026-11-15 by 0337 from 3-6 | 5-9 | D4c (density field); a read of 0x4CAA20 and the jitter table | a walls capture (none yet; 3907's cities have no walls) |
| | D4e the city field (stamps by size, slot positions, facing) | 3-6 | D4b | slot positions in captures |
| | D4f damage smoke and ruins (1.14): levels from the raw damage (0/7/15), smoke on 3/4/6 shuffled blocks (seeded, recorded), ruins set; the effects themselves wait for 0042 C4. Re-sized 2026-11-15 by 0337 from 2-4 | 2-3 | D4c (blocks are the anchors); C4 for the smoke | a damaged-city capture (none yet) |
| D5 wonders | display slots by `DisplayPosition`, `WonderArtInfo` with cultural/era/random variation, half-built state | 4-8 | D4e for slot positions | 3910's wonder catalogue |
| **Sum D2-D5** | | **49-90** | | |

The decision sheet's 16-28 (D2), 4-8 (D3), 16-30 (D4) and 4-8 (D5), 40-74 in all, stands in its order of magnitude.
Recomputed 2026-11-15 by 0337: the rows as 0319 left them add to 39-77 (D2 14-28, D3 4-8, D4 17-33, D5 4-8), not the
48-91 and D4 24-39 written here before; with D4c, D4d and D4f re-sized, D4 is 21-37 and the sum 43-81. 0337 (N6b)
came before D4c/D4d. Recomputed 2026-11-15 by 0338 (N7b): D2c 2-5 -> 2-4, D2e 2-4 -> 5-9, D2f 2-4 added (the farm
buildings were not in D2 before), so D2 is 20-37 (was 14-28) and the sum 49-90 (was 43-81). D2b keeps 4-8 and now
carries the row choice of 2.5 (best match with the tech score, random fallback, era change keeps or re-picks).

## 4. Proposals for what is left (COMMON 26)

- ~~`NEW-inc3-city-blocks-walls-research.md`~~: numbered 0337 and done 2026-11-15 (sections 1.12-1.14) except the
  +0x24 value (partly: 1.12) and the event behind the era handler 0x720A80, which move to the proposal below.
- Proposal of 0337 (COMMON 26, for the MC to number), N6c, 2-4 h, knowledge base only: the map-space transform of the
  density field (runtime matrix 0xD90FF0 x layout matrix; a memory read in a running EXE settles it), the bits of
  0x53FA50's mask, plot flag bit 8 and the 16-bit terrain value of 1.12, the moving step of 0x6EF590, the wall bend
  0x4CAA20 and the walls' 8 x 8 jitter table, the Great Wall pieces, the source of the +0x24 value in the creation
  event, the event behind 0x720A80; plus one stock capture of a walled, damaged city (walls, smoke, a tower count to
  check `floor(length / 12)`).
- ~~`tracker/issues/NEW-inc3-landmark-handlers-research.md`~~: numbered 0338 and done 2026-11-15 (sections 2.3-2.7),
  except the check against 3910's catalogue (not on main) and the leftovers of 2.8, which move to the proposal below.
  0338 also answers 0337's "plot flag bit 8" (river footprint, 2.7).
- Proposal of 0338 (COMMON 26, for the MC to number), N7c, 3-5 h, knowledge base only: read the pontoon and Feitoria
  placers in full and the Moai placer's constants; the writer and meaning of the SPECIAL per-plot byte (+0x2CA); where
  z is added to generic and animated landmarks; the farm manager's curves (farm+0x6F4) and the direction field
  0x44DA00; the use of farm+0x4A8 (wheat); grid bit 0x04 and the bake's replay list. Then, once 3910's catalogue is on
  main, check three pictures: parts missing on a multi-part landmark beside a river, a Moai coast, a horse pasture.

## 5. Not verified

- The draw passes' names (shadow vs colour) are by order and target, not by the EXE's pass function.
- The era-slot conversion 0x7A1480 did not decompile (Ghidra address-range error); it was read from its disassembly
  (llvm-objdump), the query string and the name table, not line by line.
- 0337 (2026-11-15): the block positions are checked against 3907's captures (pitch, counts, determinism); the
  density-field scale (about 0.85 units per texel) is an estimate from counts; walls, gates, towers and damage smoke
  are read in the code only, with no stock picture (3907 has no walled or damaged city); the "relaxation" in 0x6EF590
  is read up to its forces, not the step that moves points; that GATE is never placed rests on an operand scan.
  The +0x24 value is traced to its uses (1.12), not to its source.
- The city field's role in layout is inferred from its data and its users, not from a capture with and without it.
- ~~Whether the landmark handler value is read anywhere through a sub-structure pointer.~~ Answered 2026-11-15 by
  0338: read directly by 0x64B540, in code that is outside Ghidra's functions (2.1, 2.5). A raw byte scan of the
  executable sections for 0x1768 finds three instructions, all accounted for. A read whose displacement is computed
  (not an immediate) would still escape it.
- 0338 (2026-11-15): sections 2.3-2.7 are code reads only (decompile and disassembly), with no stock picture and no
  game run. The undisassembled regions (0x473440, 0x4F8D00, 0x64B540, the listeners around 0x47D980) were read as
  disassembly, line by line. The constants named in 2.6 by address were not all read as values.
- Nothing here was measured live; the stock facts are from 3907's existing captures.

## Sources

EXE: read-only Ghidra passes on a copy of project `civ5` (functions named above), the EXE copy
`%USERPROFILE%/tools/ghidra-projects/bin/CivilizationV_DX11.exe` (SHA-256 `b2ce96ae...70c88e`). DLL:
Community-Patch-DLL `CvCity.cpp` 1832-1961, 16698-16701, 27860-27863; `CvPlayer.cpp` 3962-3965, 11875-11879;
`CvGame.cpp` 13923-13935; `CvBuildingClasses.cpp` 6064, 6121-6124, 6281-6284; `CvUnitCombat.cpp` 2613-2617.
Install files of c5h-a: `CityModels.fpk`, `PolynesiaModels.fpk`, `BuildingModels.fpk`, `BabylonianModels.fpk`,
`MiscData.fpk`, `MiscTextures.fpk`, `WonderModels.fpk`, `EffectData.fpk` (file list), DLC_03 Buildings XML.
Database copy above. Stock captures and exports of 3907 (branch `epic-0301-scene-harness`). 0337 (2026-11-15):
twelve read-only headless Ghidra passes on a scratch copy `C:/c5-64bit-work/0337/ghidra/civ5` (removed at hand-back),
own scripts in `evidence/0337/` (`Dec.java`, `gh.sh`; `cityinst.py`/`cityclusters.py` for the instance rows of the
city exports, `dfstat.py`/`dfclass.py` for the `DF_*.dds` maps read from c5h-a's `Resource/DX9/MiscTextures.fpk`);
DLL `CvCity.cpp` 27850-27863, `CvGlobals.cpp` 1495. 0338 (2026-11-15): 36 read-only headless Ghidra passes on a
scratch copy `C:/c5-64bit-work/0338/ghidra/civ5` (removed at hand-back), each through `quietlock.py run` (owner
0338); own scripts in `evidence/0338/` (`Dec.java` with the new `b:` raw dword scan, `c:` raw call scan and `y:`
symbol modes; `gh.sh`; `dbq.py` for the database copy of 2.4); decompiler output stayed in scratch (D83). Knowledge base:
`landmarks-and-models.md`, `model-reader.md` 2-6, `renderer-in-server-design.md` 4.16,
`research/sources/shell-gates/stubs/landmark.md`, `research/03-dll-engine-boundary/proxy-design.md` (borrowing
table), `proxy/model/interfaces.json` (slot names), the decision sheet of 2026-11-15.
