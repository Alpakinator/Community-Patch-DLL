# The stock scene catalogue: P4, the scenario runner, units and cities (issue 3907, epic 0301)

Written by the implementer of 3907 (account B), 2026-11-15. Code: `tools/scenario/` (README there: commands, the
scenario file format, what one frame does). The plan: `research/07-architecture-decisions/architect-2026-11-14-offline-scene-harness.md`
4.5; the bundle contract and P2/P5/P7 are 3901's `offline-scene-harness.md`. Captures and their mechanics:
`research/05-graphics-composition/renderdoc-capture.md` 10.

Setup of every measurement below unless a line says otherwise: workstation, instance c5h-c (`%USERPROFILE%/c5-instances`),
32-bit DLL `i0ship-26234371-ia` (deps/i0.lock 26234371e, SHA-256 3C2F47DA...), CP/VP/EUI, stock `config.ini`, frame
cap 30 (0157), window 2554 x 1411, asset set: Vibrant Textures present (`install/Assets/DLC/VT`; D94), RenderDoc 1.47
preloaded by a wrapper of 0302 or later (installed for the run, restored by hash after). Save `C5H-0301-CAT_0009`,
a copy of `c5h-9028-tiny-t0008` (SHA-256 b2ceb6632d9766bd817819261d70b168556cc6219ce709326deaa101dcc29163; Tiny 56 x 36,
turn 9, active player 0 Polynesia).

## 1. P4: the interface hidden by Lua leaves the world picture unchanged [L, 2026-11-14 23:51-23:57]

- **How:** `ContextPtr:SetHide(true)` in the `InGame` Lua state (`tools/scenario/lua/hideui.lua`); `SetHide(false)`
  shows it again. Everything below the InGame context goes: top panel, unit and city panels, minimap, notifications,
  and the Lua-drawn world labels (unit flags, city banners, plot-help tooltip, resource and yield icons). What the
  engine draws stays, including the selection highlight of a selected unit, so the runner clears the selection.
- **Draws (the decisive test):** captures of the same view with the interface shown (frame 2083) and hidden (frame
  2314), both exported with `rdc-export --select all` and compared draw by draw on (vertex shader, pixel shader, index
  count, instance count, depth bound) by `tools/scenario/drawdiff.py`: shown 554 draws, hidden 333. The interface pass
  (223 back-buffer draws without depth; one command list fewer, 51 vs 50) is gone. Of the world draws 327 of 333 are
  equal; the other 6 are FOWParticleShader (2 draws, index count 3210 vs 3228) and a dynamic depth-bound shader
  (vs `ec8fba0f`, ps `70ed01e8`, 4 draws of changing index counts). Two shown captures 5 minutes apart differ in the
  same draws (4 and 6), and two shown captures 1.1 s apart (one multi-frame request) in one of them: animation, not
  the interface.
- **Pixels:** shots A shown, B hidden, C shown, D hidden at one camera (Honolulu (51,14), zoom 11) a few seconds
  apart; `tools/scenario/compare.py p4`. Largest channel difference on the world pixels (all pixels outside the
  static interface mask): A-B median 2, p95 101, max 248; C-D median 2, p95 112. Baselines with nothing changed:
  A-C (both shown) median 1, p95 59; B-D (both hidden) median 2, p95 25. Water, clouds, the fog's particles and the
  city's light shaft animate, so a pixel comparison of two moments is no sharper than this; the draws above are.
  The interface mask (1.1 % of the frame) falls on the top panel, the unit panel, the minimap, the city banner, the
  plot-help tooltip and the unit flags. Visual Checks: shots A-D (issue 3907) and the difference image
  `vc-20261115-000240-workstation-5e2a`.
- **Consequence:** catalogue shots are taken with the interface hidden; a cell of the picture holds only world
  draws. A reference that needs the interface shown is a separate shot.

## 2. Things that move the game away from the scenario (each cost a retry) [L, 2026-11-14/15]

- **First contact stops the Lua channel.** Revealing plots (`SetRevealed`, `ChangeVisibilityCount`) where another
  civilization's unit or territory is seen makes that civilization meet the active player; its leader greets with a
  leader screen, which holds `CvGame::update` and with it the Lua channel (a request is never picked up: `lua: no
  answer within 60 s`). Seen at 23:58 with Carthage. Closed with Esc posted by the harness (D114). The runner arms
  the harness's diplomacy handler (`tools/harness/diplo.py`, issue 1116) at the start, which closes such screens in
  the game.
- **The camera follows an auto-selected unit.** `UI.LookAt(plot)` then `UI.ClearSelectionList()` left the camera on
  the capital's warrior: with the selection cleared the game selects the next unit that wants orders and the camera
  goes to it. The runner puts every unit of the active player to sleep with its moves used (`quiet.lua`) before
  `UI.LookAt`, and never clears the selection after it.
- **Lua `SetPlotType` / `SetTerrainType` change the state, not the picture.** Flattening a studio this way left the
  hills and mountains drawn and turned the texture to a plain beige; the state record would then not match the
  picture. The runner removes features, improvements, resources and routes only and records the plot type.
  Whether the drawn terrain follows after a reload or a camera move was not checked. [L, 2026-11-15 00:01; I for
  why: the terrain heights and the tile bake are not rebuilt for these setters]
- `PreGame.GetLoadFileName()` is empty after the harness's auto-load; the runner reads the loaded save from the
  front end's own record (`run/frontend.json`: the save filter and the pid that used the load token).
- The address-space guard of 0302 ends a capture series; the runner caps captures at 6 per game process and
  restarts the game between batches (`tools/scenario/batch.sh`).

## 3. The art rows and placeable unit types [C database, L placement]

Database: a copy of c5h-c's `cache/Civ5DebugDatabase.db` (written by the game at load, mods applied; 2026-11-14 23:01),
read-only. 274 rows in `ArtDefine_UnitInfos`, 406 rows in `ArtDefine_UnitInfoMemberInfos`, 368 in
`ArtDefine_UnitMemberInfos` (model-reader.md 2.4 counts 367 member infos resolved), 215 unit types.

How the EXE picks a unit's art row: the DLL hands it `UnitArtInfo`, `UnitArtInfoCulturalVariation` and
`UnitArtInfoEraVariation` of the unit type (`CvDllUnitInfo`) and the owner's art style and era (`CvDllPlayer`) [C];
the name the EXE builds from them is [I]: rows exist as `<base><civ ArtStyleSuffix>` (cultural: only the settler,
`_AFRI`, `_AMER`, `_ASIA`; a `_EURO`/`_MED` owner gets the base row [I]) and `<base>_<era>` (era: `_RENAISSANCE`, `_MODERN`, `_POSTMODERN`, and `_LATE`, which is
not an era's `ArtPrefix`; tried at the Industrial era).

`tools/scenario/make_catalogue.py` builds the reach table (`reach.csv`, committed in `shell/evidence/0301/3907/`):

| | Rows |
|---|---|
| used directly by a unit type (`Units.UnitArtInfo`) | 213 |
| reached only through era variation, placed for a player set to that era (`_RENAISSANCE` 2, `_MODERN` 7, `_POSTMODERN` 3, `_LATE` 3 at the Industrial era) | 15 |
| reached only through cultural variation (`SETTLER_AFRI`; `SETTLER_AMER`, `SETTLER_ASIA` need a civilization the save lacks) | 1 of 3 |
| **placeable in this save** | **229** |
| not placeable: no unit type uses the row (`ARTIST`, `ARTIST_LATE` (the artist unit uses `GREAT_ARTIST`), Civil War and Smokey scenario units, unused units such as `GALLEY`, `GALLEON`, `INFANTRY`, `EXPLORER`, `TRANSPORT`, older unique units `U_DANISH_LONGBOAT`, `U_HITTITE_WARCHARIOT`, ...) | 43 |
| not placeable: no player with that art style suffix in the save (`SETTLER_AMER`, `SETTLER_ASIA`) | 2 |

Placement: `Players[p]:InitUnit(type, x, y, -1, facing)`; any unit type for any player, unique units included
(placement results per object in the frame records; section 5).

## 4. City sets [C file, L placement]

`civ5artdefines_citybuildings.xml` (CityModels.fpk, read from 3102's extraction): 5 cultures (America, Asia, Africa,
Europe, Mediterranean) x 4 eras (Ancient, Renaissance, Industrial, Modern), one `PopulationBuildings` model each:
**20 city sets**, plus `City_Ruins.gr2` for all four eras (21 models, 20 + 1 distinct). The culture is the owner's
`ArtStylePrefix` (AMERICAN, ASIAN, AFRICAN, EUROPEAN, MEDITERRANEAN) [I: by name]; the era mapping of the 8 game eras
onto the 4 art eras is [I]. Size: one model per culture and era; how the population chooses the buildings shown is
not known [I]; the catalogue places each set at population 1 and 20.
This save has no AMERICAN-prefix player, so the 4 America sets cannot be placed here; 16 can.

**Corrected 2026-11-15 (3909, after 0319's `research/05-graphics-composition/cities-and-landmarks.md` 1.2-1.4, on `main`
since MC #1408) [C, L]:** the stock install has seven city cultures by four era slots: America, Asia, Africa, Europe,
Mediterranean and Polynesian (DLC_03's `Civ5ArtDefines_CityBuildings_Polynesia.xml`, `PolynesiaModels.fpk`), plus Ruins
(`City_Ruins.gr2` in all four slots): 24 culture sets plus Ruins, not 20 + 1. The culture is the owner's
`ArtStyleType` (SOUTH_AMERICA, ASIAN, MIDDLE_EAST, EUROPEAN, GRECO_ROMAN, POLYNESIAN, BARBARIAN in that order), not
its `ArtStylePrefix`: Polynesia draws the `Poly_*` set in all four city frames, never `Asia_*` (0319's texture hashes).
So the four catalogue cities 3907's index names `CITY_ASIAN_*` are the Polynesian sets, and the catalogue holds the
African, European, Mediterranean and Polynesian sets (16 of 24 + Ruins); America, Asia and Ruins are not in it. Era
slot: `Eras.CityArtType`, else `ArtPrefix`; Ancient, Classical, Medieval -> slot 0, Postmodern, Future -> slot 3.
Size: the DLL's population buckets (1, 2, 3, 5, 8, 13, 21, 34, 55). (Replaced: "5 cultures ... 20 city sets, plus
City_Ruins", "The culture is the owner's ArtStylePrefix [I: by name]", "the era mapping ... is [I]", "how the
population chooses the buildings shown is not known [I]" in part: the bucket is known, the block placement is not.)
Block instances drawn per city (3909, `unit-draws.md` 8) are the same in all four eras: African 33 / 98 (population 1 /
20), European 25 / 98, Mediterranean 34 / 83, Polynesian 32 / 95.

## 5. The catalogue run [L, 2026-11-15 00:11-01:16, c5h-c, six game processes]

Scenario files `tools/scenario/scenarios/units_land.json` (16 frames), `units_sea.json` (4), `cities.json` (4), made by
`make_catalogue.py` from the layout `tools/scenario/layouts/tiny_cat_0009.json`; driven by `tools/scenario/batch.sh`,
one game process per batch. Numeric index: `shell/evidence/0301/3907/index.csv` (object, art row, owner, era, plot,
cell, result, Visual Checks id and SHA-256 of the shot, capture file, size and SHA-256, export folder, turn, zoom);
`reach.csv` beside it. Pictures, captures and exports stay on F: (D141): `F:/c5-64bit-work/0301/3907/` `runs/`
(frame records and shots), `captures/`, `exports/<scenario>/<frame>/` (`rdc-export --select all`), `xml/`
(conversions). Folders named `*_discarded`/`runs_proc1_test` hold the retaken attempts.

- **Studios:** land, plots x 38-44 step 2 by y 9-13 step 2, camera on (41,11); sea, x 26-32 by y 7-11, camera (29,9);
  cities, x 37-46 step 3 by y 9 and 12, camera (42,10). Zoom: clamp at the minimum (8 wheel ticks in) then 2 ticks
  out = level 15.0 for units, 3 out = 17.0 for cities. At 13.0 the edge cells of a 4 x 3 unit grid were cut by the
  frame; at 15.0 for cities the bottom-right city (perspective). Each frame: up to 12 units, or 8 cities (4 art
  styles x population 1 and 20), each alone on its plot, interface hidden.
- **Units:** 229 objects in 20 frames, every one placed `ok` at its plot (no unit moved, none embarked or refused;
  air units and the atomic bomb stand on land plots; the unit's team colour is its owner's). Owners: the active
  player (Polynesia) for the 213 direct rows; era variants on Carthage (Renaissance), Greece (Industrial, also for the
  `_LATE` rows), Portugal (Modern), Ragusa (Postmodern); the `_AFRI` settler on Msoura. The
  Modern caravan is drawn as a truck (era variation reached) [L, the shot]; which row the EXE drew for the other
  variants is not checked (section 7).
- **Cities:** 16 sets x 2 sizes = 32 cities in 4 frames (eras Ancient, Renaissance, Industrial, Modern set with
  `Teams[t]:SetCurrentEra` on the owners: Msoura AFRICAN, Polynesia ASIAN (the active player: a NewEraPopup opened
  and was closed by the runner), Portugal EUROPEAN, Greece MEDITERRANEAN). `city:GetArtStyleType()` reads
  ARTSTYLE_BARBARIAN for the city-state Msoura, ARTSTYLE_POLYNESIAN, ARTSTYLE_EUROPEAN and ARTSTYLE_GRECO_ROMAN
  for the others: the EXE's city set follows the civilization's `ArtStylePrefix`, not this value [I]. Every city
  claims its ring of plots, so culture borders are drawn around them. (Corrected 2026-11-15, 3909 after 0319: the set
  follows the owner's `ArtStyleType`; Polynesia's cities are the Polynesian sets; see section 4. Replaced: "follows
  the civilization's `ArtStylePrefix`, not this value [I]".)
- **Popups met (D114):** CityStateDiploPopup at a first contact with a city-state (4 frames) and NewEraPopup when the active player's era
  was set (3 city frames);
  all closed by `popups.lua` before the shot; no leader screen once the diplomacy handler was armed.
- **Frames per game process (the 0302 guard at its default floor of 100 MB):** unit frames: 6 of 6 in each of three
  processes (largest free block after the first capture about 330-760 MB at the trigger, lowest during a frame
  200-330 MB); city frames cost about 178 MB each: the third city capture of a fresh process was refused (243 MB <
  100 + 178), and after four unit frames the fifth (a city frame) was refused (199 MB < 100 + 128); the guard never let the EXE fail. 24 catalogue captures took six processes. Plan 6 unit
  frames or 2 city frames per process.

## 6. Sizes and times (indications, workstation, outside a timing window; COMMON 23)

- Per frame (place, camera, hide, popups, state, shot, capture, clear): median 18.6 s (17.0-21.8, n = 24).
- Per game process: prepare 0.3-1 min, launch to in game 2.5-3 min, stop 0.4 min.
- Captures: 12 units at zoom 15, 120-138 MB; 12 ships 91-113 MB; 8 cities at zoom 17, 129-148 MB; P4 views 90-104
  MB. The harness reports the file 3.0-3.8 s after the trigger.
- `rdc-export --select all`: units 54-86 s, ships 39-46 s, cities 136-180 s (median 65 s, n = 24); 242-340 MB of
  export each (median 269 MB), plus the conversion's zip (about 61 MB) and xml (about 4 MB).
- The catalogue on F: 15.0 GB with the discarded attempts and the P4 captures (2026-11-15 01:17).

## 7. Not verified

- Which art row the EXE draws for an era-variant or cultural-variant placement (the reach table names the row the
  database offers; the capture's textures could tell, not checked).
- The `_LATE` rows' era.
- (Lead's note, 2026-11-15 01:20: P5 was answered `[L]` by 3901 on capture 3701 stock A, `offline-scene-harness.md`
  section 8: unit draws by shader hash, `UnitShader_Skinned_CODE`, material 90; the bone palette is VS t27, a 262,144 B
  dynamic buffer, 98 float4 per instance. The lead below agrees with it.)
- P5 (unit draws told apart, bone palette without a replay) is 3901's; it was not answered here. A lead for it
  [I, 2026-11-15, export of units_land L01]: of the depth-bound draws, 98 share vertex shader `29a0b061` and pixel
  shader `60d270e1`, have no material name in the 2903 table (which lists terrain, water and fog materials only), and
  bind at VS t27 a dynamic 262,144-byte R32G32B32A32 buffer (16,384 float4) beside a 192-byte VS constant buffer: the
  shape of a shared bone-matrix buffer. A second family (`0783e181`/`95325aff`, 46 draws) and `7ec4e9a5` (21 draws)
  were not looked at. `--select materials:` cannot pick unit draws (their materials are not in the table), so the
  catalogue exports `all`.
  (Settled 2026-11-15 by 3909, `research/05-graphics-composition/unit-draws.md` 1 and 8, names by SHA-256 against
  the EXE's blobs of `material_table.txt`: `29a0b061`/`60d270e1` is `UnitShader_Skinned_CODE` pass 0 (material 90);
  `0783e181`/`95325aff` is `ForestShader` (material 44, the trees); `7ec4e9a5` is `BuildingShader_CODE_Instanced`
  pass 0 (materials 4/5, city blocks and other buildings). `material_passes.csv` lists 58 material passes only, which
  is why the export names none of them; `material_table.txt` names them all.)
- Whether `SetPlotType` / `SetTerrainType` are drawn after a reload or a camera move.

## 8. Pose sequences (3908) [L, 2026-11-15 01:42-02:15, c5h-f]

Written by the implementer of 3908 (account B), 2026-11-15. Setup as at the top of this file except: instance **c5h-f**
(`F:/c5-instances`), save **`C5H-0301-SCN_0009`** = a copy of c5h-c's `c5h-9028-tiny-t0008` (SHA-256
b2ceb6632d9766bd817819261d70b168556cc6219ce709326deaa101dcc29163, checked on the copy), 32-bit DLL `i0ship-26234371-ia`
(SHA-256 3c2f47da..., `provision.py set-dll pin`; deps/i0.lock 26234371e), VT present (D94: `install/VT` in c5h-f's
install), RenderDoc 1.47 preloaded by the 0302+ wrapper (`wrapper.py install`, restored by hash after). Scenario
`tools/scenario/scenarios/poses_tiny.json`; reader `tools/scenario/bones.py`; numbers in `shell/evidence/0301/3908/`
(`poses_index.csv` one row per capture, skinned draw and figure; `poses_summary.csv` per figure group).

- **Units:** `UNIT_WARRIOR` (infantry; 12 figures in three meshes of 1962, 2022 and 1914 indices, 20 bones each) and
  `UNIT_HORSEMAN` (mounted; 5 figures, rider mesh 2004 indices / 21 bones and horse mesh 2121 / 30 bones), the active
  player's (Polynesia), at zoom level 11.0 (the minimum), on the flat unowned plots x 41-44, y 10-13 (studio
  [40,10,45,13]: features, improvements and units removed). Target of the attacks: `UNIT_BARBARIAN_WARRIOR` of player
  63 (always at war: no declaration popup; meshes 2451, 2517, 2052). No popup opened (records `popups_*` empty).
- **Animations need two game options off.** The save has `GAMEOPTION_QUICK_COMBAT` and `GAMEOPTION_QUICK_MOVEMENT` on
  (and `SinglePlayerQuickCombatEnabled = 1`, `...QuickMovementEnabled = 1` in the instance's UserSettings.ini): units
  then jump and combat has no animation. The in-game options menu's own call
  `Network.SendGameOptions({{"GAMEOPTION_QUICK_COMBAT", false}, {"GAMEOPTION_QUICK_MOVEMENT", false}})` turns both off for
  the running game (`Game.IsOption` reads false about a second later; UserSettings.ini is not written). It does not
  persist into the next launch of the save, so a scenario with `"animations": true` sends it at every `run`.
- **Actions** (`lua/action.lua`, a frame's `action` just before its capture): the unit woken, `SetMoves(MaxMoves())`,
  `PushMission(MISSION_MOVE_TO, x, y)` (onto an enemy's plot this is the melee attack) or `MISSION_RANGE_ATTACK`, then its
  moves used up and asleep again so that no auto-selection moves the camera. The game core moves the unit at once
  (the warrior reached (42,11) of (43,11) with its one move, the horseman (43,12) of (44,12)); the EXE plays the walk or
  the combat afterwards, and that is what the captures hold.
- **What the captures hold [C, the conversions].** Each capture's VS t27 palette (`offline-scene-harness.md` 8) is
  read at every draw whose input layout has BLENDWEIGHT (`bones.py scan`; the input layout uses
  `D3D11_APPEND_ALIGNED_ELEMENT`, so offsets are summed from the formats). Only material 90 (UnitShader_Skinned_CODE)
  appeared: pass 1 once and pass 0 twice per figure group per frame. A figure's bone count is the mesh's largest
  BLENDINDICES with a weight > 0, plus 1; the rows past it are not cleared from earlier frames (a whole-buffer
  `Map(WRITE_DISCARD)`), so "non-zero rows" overstates it. **Instance ids and the draw order change from frame to
  frame** (the warrior's figures were ids 0-11 in one frame and 10-21 in the next): `bones.py index` matches a figure
  with the previous frame's figure of the same mesh and pass whose root bone is nearest.

| Sequence | Frames (requested / written) | Present-to-Present (ms) | What the bone rows show |
|---|---|---|---|
| P01 idle: warrior (42,12), horseman (43,12) | 4 / 4 (4677-4683) | 1470, 1437, 1249 | idle: rows change by 0.1-4.1 between frames, roots move < 0.4 world units |
| P02 warrior moves (41,11) to (42,11) | 3 / 2 (10652, 10654; guard) | 1416 | the walk: in the second frame 4 figures are 34-35 units on their way (a plot is 64), the rest 2-25 |
| P03 horseman moves (41,12) to (43,12) | 3 / 3 (1318-1322; the guard stopped after 2, the third had already started: 0302's stated limit) | 1127, 1207 | the ride: every figure 52-63 units further, then 40-42 more onto (43,12); rows change by about 54, then 40-45 |
| P04 warrior attacks the barbarian on (43,11) | 4 / 4 (639-645) | 967, 978, 979 | both sides animate (rows change by 3-19); in the fourth frame the warrior's figures step 7.5 units forward |
| P05 horseman attacks the barbarian on (43,12), capture 1.5 s after the order | 4 / 2 (816, 818; guard) | 1566 | the charge: the riders' and horses' roots are already on (43,12) in the first frame; rows change by 8-17 in the second |
| P05 first try, capture right after the order | 4 / 3 (681-685; guard) | 1255, 1223 | the defenders react (rows 8.5 then 2); the horsemen stay on (42,12) at idle level for 3.4 s: when the EXE starts the charge varies |

- **Frame times:** two captured frames of one request are 0.97-1.57 s apart (n = 12 steps), about P6's 1.1-1.25 s but
  wider: the step is the time RenderDoc takes to write the previous capture (90-113 MB here, onto F:).
- **Frames per game process at the 100 MB floor:** 4 + 2 (P01 then P02, guard after the sixth), 4 + 3 (P04, P03: the third frame of P03 had started before the guard decided),
  3 (P05 try), 2 (P05 retry, after a 1.5 s wait): the guard's frame-cost prediction (75-175 MB here) stops a series
  earlier than 0302's Tiny figure of 7; a sequence of 4 fits only as the first of a process. Never lowered.
- **Library:** `F:/c5-64bit-work/scene-library/poses/<sequence>/`: the captures, `<capture>.bones.json` (frame number,
  Present time, every skinned draw with its instance ids and per figure the 98 rows) and the shots' pictures stay in
  the Visual Checks feed (DIAGNOSTIC; ids in the frame records).

## 9. Full scenes, stock side and bundles (3908) [L, 2026-11-15 02:00-03:10, c5h-f]

Setup as section 8. Scenario `tools/scenario/scenarios/scenes_tiny.json`: four named cameras on the save as loaded
(nothing placed), each a frame with a `scene` key and no objects; library writer `tools/scenario/stocklib.py`.

| Scene / camera | Centre plot, zoom (level) | What is in view | Capture | Export (`--select all`) | Visual Checks |
|---|---|---|---|---|---|
| coast / coast_c1 | (51,12), min-2 (15.0) | the bay south of Honolulu, its coast and waves, the culture border, the river mouth | 114,267,180 B | 273,830,848 B, 58 s | vc-20261115-020925-workstation-342b |
| river / river_c1 | (52,15), min-2 (15.0) | the river east of Honolulu from source to mouth, the city, the bay | 101,921,707 B | 259,787,164 B, 65 s | vc-20261115-020352-workstation-1900 |
| city / city_c1 | (51,14), min-3 (17.0) | Honolulu (population 1) and its plots | 87,911,188 B | 229,958,458 B, 65 s | vc-20261115-020944-workstation-1482 |
| highlights / highlights_c1 | (50,13), min-2 (15.0) | Polynesia's warrior selected in move mode: its movement range (MovementRangeBorder), the culture border | 94,379,477 B | 254,911,142 B, 54 s | vc-20261115-023537-workstation-3f06 |

- **Library** (`offline-scene-harness.md` 11): `F:/c5-64bit-work/scene-library/stock/<scene>/` holds `<camera>.png`,
  `<camera>.rdc` (moved there from the run's capture folder), `<camera>.export/`, `<camera>.camera.json` (how the runner
  set the camera, and the EXE's views, projection, target, position and zoom level read by `gt_reader`), and
  `state.json` (per camera: save, turn, pins, instance, the plots' state record, the stimulus, the shot's and capture's
  SHA-256 and the frame record). 1.4 GB for the four (2026-11-15 02:37).
- **Cameras that did not work:** (53,13) at min-4 (level 19.0) is an unrevealed plot under the clouds and too far
  (each wheel tick out is 2 zoom levels: min-2 = 15, min-3 = 17, min-4 = 19). At turn 8-9 only Honolulu's surroundings
  are revealed, so all four views share that patch.
- **The movement range (three retakes) [L].** Selecting the unit (`UI.SelectUnit`) draws the selection mark; the move
  interface mode alone (`UI.SetInterfaceMode(INTERFACEMODE_MOVE_TO)`) draws no range, because VP's `WorldView.lua` asks
  for it only on a mouse-over (`ShowMovementRangeIndicator` -> `Events.ShowMovementRange(player, unit)`, answered by the
  engine with `AddUnitMoveHexRangeHex` events that WorldView turns into MovementRangeBorder highlights). **Highlights
  sent before `ContextPtr:SetHide(true)` of InGame are gone after it** (tries 2 and 3: `ShowMovementRange` and a
  MovementRangeBorder ring sent from the InGame state, then the hide: nothing drawn). With the interface hidden first,
  `Events.ShowMovementRange` from the InGame state plus a ring-1 MovementRangeBorder from the WorldView state
  (`lua/highlight.lua`, 0167's diagnostic way) draws the range: the outline drawn reaches about two plots, the game's
  own range for a unit with two moves (the ring-1 set lies inside it) [I: which of the two calls drew which part is not
  separated]. The runner now hides the interface before a stimulus. **The order, not the hide, decides [L]:** the
  first highlights bundle (below) sent the same stimulus with the interface shown and never hidden, and its dumps show
  the selection ring but no range; with the hide first the bundle shows the range. So with InGame shown something in it
  (its mouse-over or interface-mode handlers [I]) takes the range away again; with InGame hidden the range stays.
  Without the range, the selection mark is a glowing yellow ring on the unit's plot; with it, a blue disc.
- **The turn moves on during a run [L for the setting, I for the cause].** `status.lua` reads turn 8 at the first frame of
  a game process and 9 at later ones: the instance's UserSettings.ini has `SinglePlayerAutoEndTurnEnabled = 1`, and
  `quiet.lua` leaves no unit wanting orders, so the human turn ends and the AIs play turn 8 out. The plots' state record
  of each frame is taken at that frame; whether an AI unit moved into a view between frames was not checked. This also
  holds for 3907's catalogue (same runner).
- **Bundles (item 3) from the joined arm.** 3904's recorder (team branch at fa44ed5d, merged here) built from this branch:
  `server/build_server.py --src C:/c5-64bit-work/0168/3957/src-x64f` (fork 04b1211b8, read-only) `--deps
  C:/c5-64bit-work/0168/3957/deps-release --config release --offline` (155 s): server.exe da79371b...; layout = the pin
  layout `x64-04b1211b-pa803776`'s bin with this server.exe (DLL 74588135...); proxy 69d7918b...; composite
  (`shell\build.cmd`) 11c0213b...; checker `recorder_test.exe` (`build_scenebundle_tests.py --only recorder_test`).
  Driver: 3904's `recorder_live.py` imported unchanged by `shell/evidence/0301/3908/scripts/bundle_live.py` (owner
  3908, save copy `C5H-0301-SCNB_0009` of `C5H-0301-SCN_0009`), one game process per scene
  (`scripts/live_scene.sh`): prep (backups by SHA-256, set-proxy, composite, D33 minus Overlay, keys
  `render_test_record=F:/c5-64bit-work/scene-library/bundles/<id>`, `_after=1500`, `_frames=8`, `_dump=all`), run, and
  beside the run `runner.py camera` (new): waits for in game, then sets the scene's camera (and stimulus) as `run` does,
  without a shot or capture. The server publishes from in game at about 18-30 frames a second (3904's Tiny render.log),
  so published frame 1500 comes about 50-80 s after in game; the camera is set 6-10 s after in game.

| Scene / camera | Bundle (`F:/c5-64bit-work/scene-library/bundles/`) | Camera set / span start | Span (published 1499 at its first frame) | Checker |
|---|---|---|---|---|
| coast / coast_c1 | `C5H-0301-SCNB_0009_20261115-023727_workstation` | 02:39:42 / 02:40:27 | frames 1686-1730, 8 dumped, 1 overlay file (7 deduplicated) | `scenebundle: OK` |
| river / river_c1 | `C5H-0301-SCNB_0009_20261115-025156_workstation` | 02:54:33 / 02:55:13 | 1779-1818 | OK |
| city / city_c1 | `C5H-0301-SCNB_0009_20261115-025751_workstation` | 03:00:07 / 03:00:50 | 1594-1633 | OK |
| highlights / highlights_c1 | `C5H-0301-SCNB_0009_20261115-030936_workstation` | 03:12:13 / 03:12:55 | 1623-1660 | OK |

- Each bundle: 23 members, about 281 MB (269 MiB): the C5HF field of load 3, the water tap, feed.bin (5-8 drains and
  2 tables), frames, cameras, the water clock, the overlay, 8 colour and 8 depth dumps at 2554 x 1411; 0 record
  errors; stamped by `recorder_test.exe --stamp` (save `C5H-0301-SCNB_0009`, its SHA-256, turn 8 = the load's
  `Game.GetGameTurn`, pins, repository commit, release) and checked. The runs: launch to in game 1:37-1:50, 3 checks
  of the focus guard 0, `startup_watch_ok`, safe stop rc 0, restore by hash and `provision.py verify c5h-f` rc 0 (the
  coast run's verify gave rc 1: it started from the scenario runner's prepared instance, see below).
- **Camera check [L, pictures only]:** the first dump of each bundle, set beside the stock shot of its camera, shows
  the same framing (culture border, bay and river at the same places of the screen). Visual Checks (DIAGNOSTIC):
  coast `vc-20261115-024355-workstation-be12`, river `vc-20261115-025803-workstation-7e1d`, highlights
  `vc-20261115-031528-workstation-47b3`; the city's was not looked at. The recorded cameras (`cameras.bin`) were not
  compared with the stock `<camera>.camera.json` numerically.
- **What the bundle pictures do not hold, by design of the arm:** units, cities' buildings, fog and clouds and other
  EXE systems are not in the server's world target in this arm (D33 minus Overlay); the comparison with the stock shot
  (3906) is limited to what the server draws.
- **The turn:** the bundles' state is turn 8 at load; `runner.py camera` runs `quiet.lua`, so with auto end turn the AIs
  play while the span is recorded about 40 s later (as on the stock side). Not checked whether anything in view moved.
- **Trap: 3904's driver restores to its own prep-time state.** Its prep backs up the standard files as it finds them. After
  the scenario runner's `prepare` the front end is the runner's patched copy, so the driver's restore puts the patched
  `MainMenu.lua` back, and the next run's front-end prepare refuses it ("does not match install.manifest.tsv: not the
  original copy"; the river's first run, 02:44). Restore the scenario runner first (`runner.py restore`), then run the
  driver: every later run started from the original instance and ended with verify rc 0.
- **Sizes and space (2026-11-15 03:16):** the library on F: holds `poses/` 1.6 GB, `stock/` 1.4 GB, these 4 bundles
  1.1 GB; the run records, conversions and discarded attempts in `F:/c5-64bit-work/0301/3908/` about 2 GB; the joined
  arm's build trees `F:/c5-64bit-work/0301/3908/build/`.

## 10. Not verified (3908)

- Which of `Events.ShowMovementRange` and the WorldView ring-1 MovementRangeBorder drew which part of the range.
- Whether an AI unit moved into a scene's view during the turn that auto end turn starts (stock frames after the first of
  a process, and every bundle).
- The recorded cameras of the bundles against the stock camera files, numerically (pictures only, above).
- Why the horseman's charge did not start within 3.4 s in P05's first try but had started 2.8 s after the order in the
  retry.

## 11. Landmarks and wonders (3910)

Written by the implementer of 3910 (account B, Opus 5.5, lead-0301), 2026-11-15. Setup of every measurement here:
instance c5h-d (Goldberg, on F:), 32-bit DLL `i0ship-26234371-ia` (deps/i0.lock 26234371e, SHA-256 3C2F47DA...EA06C)
named on the command line, CP/VP/EUI, Vibrant Textures present (`install/Assets/DLC/VT`), RenderDoc 1.47 preloaded,
save `C5H-0301-CAT2_0009` (a copy of `c5h-9029-tiny-t0008`, SHA-256 b2ceb663...dcc29163, Tiny 56x36, turn 9, the same
bytes as 3907's source), studios of `tools/scenario/layouts/tiny_cat_0009.json`. Times are workstation indications
outside a timing window (COMMON 23).

### How it runs

- Driver: `shell/evidence/0301/3910/driver/run3910.py` = 3907's runner (`tools/scenario/runner.py`, a4c0f5f3) imported
  unchanged, with two more object kinds (`landmark`, `wonder`), its own `place` / `clear` / `state` Lua, preludes, a
  camera check and popup closing before every shot. To be folded into `tools/scenario/` (a runner change, 3907's or
  the lead's). Scenario files: `tools/scenario/scenarios/landmarks_land.json` (46 frames), `landmarks_sea.json` (3),
  `wonders.json` (6), generated by `driver/gen_landmarks.py` from a copy of the instance's `Civ5DebugDatabase.db`
  (SHA-256 278395f2...c258); the plan of every landmark group and its reason when not placed: `plan.csv`,
  `wonders_plan.csv`; the numeric index: `index.csv` (all in `shell/evidence/0301/3910/`).
- Batches: one game process per batch of 3-6 frames (`shell/evidence/0301/3910/scripts/`: `batch2.sh`,
  `series4.sh`, `final.sh`), each frame shot (Visual Checks, DIAGNOSTIC), captured (RenderDoc, one frame) and later
  exported (`rdc-export --select all`). Preludes per process: the diplomacy handler armed first, `reveal_techs`,
  `reveal_wide`, popups closed until quiet.
- Not verified: which model each RANDOM, `_HEAVY`, `_HILL`, `_WATER` and trading-post placement drew (needs a
  draw-to-model match on the exports, not done); the orange highlighted hexes on cloves / pepper plots (Lanc22); the
  shots were checked by eye on 9 of 55 frames (Lanc01, 02, 06, 07, 22, Fanc01, Fanc03, Sanc01, W01).

### Result (2026-11-15 00:52-02:55 EST)

- 55 frames, 604 objects, every one placed `ok` in the game state (index.csv): 46 land frames (530 landmark
  objects), 3 sea frames (28), 6 wonder frames (46 cities, one wonder each). Every frame: camera on target (x within
  0.001 world units), interface hidden by Lua, popups closed, one harness shot (Visual Checks, DIAGNOSTIC, issue
  3910), one RenderDoc capture (55 captures, 95-134 MB, median 113 MB, 6.3 GB), the state record per plot.
- Reach: 539 of 580 landmark groups placed, holding 496 of the 514 models; 41 groups (18 models) not placeable, each
  with its reason in `plan.csv`; 46 of 76 world wonders placed, the 30 with `DisplayPosition` 0 are never drawn
  (`wonders_plan.csv`). Placed is not the same as drawn as its own model: RANDOM groups draw one model per plot by
  the EXE's choice, and the feature-bound groups (natural wonders, marsh, flood plains) did not draw (below).
- Exports: `rdc-export --select all` for all 55 captures, rc 0: b02's six inline (129-163 s), the other 49 in one
  pass after the game batches (`driver/export_pass.py`, 02:55-04:15: 47-135 s, median 101 s; median 311 MB, 15.1 GB;
  a land frame holds about 1,230 draws, its textures about 232 MB of the export). On F: the folder is 29.8 GB in all.
- Time per frame without its export: median 19.9 s (18.3-35.4, n 48: placement, camera, settle, shot, capture,
  clear); a game process start to "in game" about 3.5-4 min on F: (c5h-d). Indications, COMMON 23.
- Files on F: (D141, kept): `F:/c5-64bit-work/0301/3910/` `runs/` (frame records and shots), `captures/`,
  `exports/`, `crash/` (the c5h-a minidump), `runs_b01_superseded/` and `exports_b01_superseded/` (the first take of
  Lanc01-06, with clouds).

### What places a landmark [L unless marked]

- `ArtDefine_Landmarks` (667 rows, 514 models) falls into 580 groups (ImprovementType, ResourceType, Era, State); a
  group's models are drawn together (polder layers, farm fields) or one per plot at random (LayoutHandler RANDOM: the
  generator places one copy per model, which and by what rule is the EXE's [I]).
- Lua on a save copy: `plot:SetImprovementType`, `SetResourceType(r, n)`, `SetFeatureType`, `SetImprovementPillaged`
  are drawn at once (probe 2026-11-15 00:25-00:30, c5h-d).
- **Era art follows the active team's era** on unowned plots: `Teams[active]:SetCurrentEra(ERA_INDUSTRIAL)` drew the
  Industrial mine, trading post, farm and plantation (probe p02 against p01). Each frame sets the era on its objects.
- **A resource is drawn only when the active team can see it**: `CvPlot::updateLayout` sends the engine
  `getResourceType(eActiveTeam)`, `NO_RESOURCE` without the reveal tech [C]. The prelude `reveal_techs.lua` gives the
  active team the 11 reveal techs (Bronze Working, Animal Husbandry, Chemistry, Dynamite, Industrialization, Atomic
  Theory, Trapping, Pottery, The Wheel, Archaeology, Writing); before that, iron and cow were not drawn and the mine on
  iron drew as the generic mine.
- Quantity picks the `_HEAVY` art: iron with quantity 8 drew a different, larger mine than quantity 2 (probe 00:30);
  the threshold is not measured [I]. `_MARSH`, `_WATER`, `_HILL`, `SUPERHORSE` and `SUGAR` are placed by the rule in
  `gen_landmarks.py` (marsh feature, sea plot, hill plot, horse quantity 8, sugar on flood plains) [I: which art the EXE
  drew is in the shots and captures, not checked row by row].
- **Half-built (UnderConstruction)**: `updateLayout` sends state 1 when the plot has no improvement and build progress
  for a build that makes one, on a visible plot [C]. `plot:ChangeBuildProgress(build, 1, player, true)` alone did not
  redraw; a road set and removed after it marks the layout dirty and the scaffolded model appears [L].
- `ART_DEF_IMPROVEMENT_TRADING_POST` (the tag of `IMPROVEMENT_TRADING_POST`) has no row; the game draws one of
  `TRADING_POST1-4` [L: a trading post drew; which variant, I].
- Not placeable without changing the database (no game type names the tag): `MONOLITH_AMER`, `MONOLITH_ASIA`,
  `CHATEAU` (old; `IMPROVEMENT_CHATEAU` uses `CHATEAU_NEW`), `MOTTE_BAILEY`, `WAJO`, `SALTWORKS`, `PONTOON_BRIDGE`,
  `HADRIANS_WALL_1-3`, the features `EVEREST`, `TITICACA`, `VICTORIA_FALLS` (no game feature) and `FORD` (river
  crossings, the EXE's); `LAKE_FISH` needs a lake plot (none in the studios); 8 rows have no model (farm on wheat,
  maize, rice; Moai built and half-built). Totals from `plan.csv`: 539 of 580 groups placed, holding 496 of the 514
  models.

### What places a wonder [C, DLL source; L for the placement]

- 76 world wonders (`BuildingClasses.MaxGlobalInstances = 1`); only the 46 with `Buildings.DisplayPosition > 0` are
  drawn: `CvCityBuildings::SetNumRealBuilding` sends the engine its deferred `WONDER_CREATED` command only for those,
  and `CvCity::setupWonderGraphics` at load likewise. The other 30 have no model in the world (`wonders_plan.csv`).
- Placement: `Players[0]:InitCity(x, y)` (Polynesia, the active player, art style ASIAN), population 3, then
  `city:SetNumRealBuilding(b, 1)`; eight cities to a frame in the city studio. The half-built wonder appears only at
  the city's turn processing (`CvCity::doProduction`) and is not catalogued.

### Problems met and how the driver handles them [L]

- **Lua `plot:SetOwner(p, -1, ...)` crashed the DLL** (c5h-a, 2026-11-15 00:17:44; run-and-drive-the-game.md section 3,
  "Lua calls that are unsafe"): no plot owner is set by the driver.
- **Queued popups**: the 11 techs queue 11 TechAwardPopups shown one after the other; dequeuing alone let the next
  appear over the map in two shots (amended as fail). The driver closes TechAwardPopup and NewEraPopup the way their
  Continue button does (`UIManager:DequeuePopup` + `Events.SerialEventGameMessagePopupProcessed`) in the prelude
  until three rounds find none, and before every shot until two rounds find none.
- **`UI.LookAt` ignored**: twice the camera did not move (target x unchanged on six plots tried, 00:34 and 00:44); the
  same call worked a minute later. The driver checks the camera target's x against the plot's world x (`HexToWorld`)
  and tries up to four times; `camera_on_target` is in every record.
- **Terrain splats are not redrawn**: removing a feature (an oasis) left its pond in the terrain, and removed mines
  left ground decals for later frames on the same plots in the same process. Feature frames (`F...`) are generated
  after the others and run last in a process; every batch starts a new game process. What a frame inherited from the
  earlier frames of its process is named by the batch list in the issue log.
- **Features set by Lua are not catalogue-grade**: marsh and flood plains are terrain and were not redrawn (Fanc01:
  the coal and iron mines drew, no marsh); in Fanc03 the volcano plot showed rubble and Uluru's plot a small pond (an
  earlier frame's oasis splat, same process) instead of their models. Natural wonders and the `_MARSH` / `SUGAR`
  variants are therefore placed in the game state (records say so) but their pictures are not references; a natural
  wonder needs a map generated with it (map script or WorldBuilder), not a Lua feature change [I].
- **Clouds**: at zoom min-2 the unexplored plots east of the land studio covered its top-right cell (Lanc02). From
  batch 2 the prelude `reveal_wide.lua` reveals the studios grown by 5 plots.
- Build progress left by a half-built object kept drawing its scaffold after the clear: `clear` resets it and the
  pillage flag.
- **Never edit a bash script while it runs** (bash reads it as it goes): an edit of `batch.sh` during b02 broke its
  stop line and left the game up; the next prepare refused the running instance. Copies are made instead.

### Frames per game process (capture guard at the default 100 MB floor) [L, c5h-d, Tiny, 2026-11-15]

| Batch | Between two captures | Area drawn | Captures before the guard stopped |
|---|---|---|---|
| b01 (00:52-01:01) | 66-91 s (the frame's `rdc-export` ran in between) | studio revealed, clouds east | 6 of 6, no stop |
| b02 (01:13-01:30) | 129-163 s (export in between) | wide reveal (567 more plots visible) | 6 of 6, no stop |
| c03 (01:38-01:40) | about 20 s (no export) | wide reveal | 4; stopped before the 5th |
| d01 (01:45-01:47) | about 20 s | wide reveal | 3; stopped before the 4th |

Reading [I]: a frame of twelve landmarks costs more of the 32-bit address space than 0302's Tiny frames (7 per
process), and without a pause the space does not come back between captures (renderdoc-capture.md 10.4: the block
recovers when the game idles). Captures are 91-116 MB each. The series ran 4-frame batches; a frame the guard
refused keeps its shot (Visual Checks) and is captured again in a later batch.

## 12. The exit crash of RenderDoc runs is the `-ia` DLL, not RenderDoc (ticket 0483, 2026-11-17)

**Symptom.** 0371's two stock runs on c5h-c (pids 47460, 70296, 2026-11-17 03:09 and 03:13) with RenderDoc 1.47
preloaded ended 0xC0000005 shortly after `UI.ExitGame()` over the Lua channel, with no box, no Goldberg crash line, no
`crashlogs` record and no Windows Error Reporting record; the captures and the run's work were written. The preload
looked guilty: of 114 preloaded launches in the instances' `harness.log` (2026-11-11 to 2026-11-17,
`server/evidence/0483/preload_exits.txt`, `preload_exits.py`), the 108 ended by the harness stop (a terminate,
exit 0xF where recorded) never crashed, and every one ended by `UI.ExitGame()` whose exit code was recorded did.
*Corrected 2026-11-17 by 0483: the issue's first reading ("RenderDoc's hooks at process shutdown") was wrong; the
terminate never unloads the game DLL, which is why the stop's runs were clean.*

**Cause [C, cdb; L].** All these runs had the pin's instance-aware build `i0ship-26234371-ia` (3C2F47DA...) in the CP
folder. Run A (c5h-f, 04:26-04:31, stock, the `-ia` pin, **no preload**, save copy `C5H-0483-TINY_0008` of
`C5H-0366-TINY_0008`, b2ceb663..., `UI.ExitGame()` under an invasive cdb attached by `shell/evidence/0164/cdb_watch.py`):
the EXE unloads the CP DLL, then its main thread allocates a `std::string` (`CivilizationV_DX11+0x125c2c` ->
MSVCP90 `basic_string` -> `std::_Allocate` -> MSVCP90's `operator new` import slot) and faults at
`<Unloaded_CvGameCore_Expansion2.dll>+0xdb1d20`, which the `-ia` PDB names `ThunkOperatorNew<5>` (MemoryImports):
the stale-slot fault of `../03-dll-engine-boundary/proxy-design.md` 21 and `../03-dll-engine-boundary/headless-host.md`
5 (`server/evidence/0483/runA-crash-stack.txt`). Run B (c5h-f, 04:38-04:42, the 0371 set: `-ia` pin, RenderDoc
preloaded, one capture of 107 MB written, `UI.ExitGame()` through `lua.try_lua`, no debugger): 0xC0000005 about 8 s after
the request (process gone between 7.5 and 8 s), no box. So every `UI.ExitGame()` (or Exit to Windows, or a mod
change) of a stock game with an `-ia` DLL ends this way, with or without RenderDoc; the plain pin `i0ship-26234371`
has no MemoryImports (0164 s2, 0484) and is not affected. A first guess that the fault happens during DLL shutdown (where
the unhandled-exception filters are skipped) is not what cdb shows: the EXE unloads the game DLL itself while the process
still runs (`Unload module ... CvGameCore_Expansion2.dll` before the fault, threads still alive); why its own filter and
Goldberg's record nothing is as in 0164 (the EXE's filter does not record a plain AV).

**Harmless, and named [L].** The run's work is on disk before the exit (saves, captures, logs). Nothing to change in the
capture route: RenderDoc runs may end by `UI.ExitGame()` or by the harness stop; use the stop (`cli.py -i NAME stop`,
after the newest autosave has stopped changing) when a clean exit code matters. Since 0483, `tools/harness/lua.py`
writes a `lua_exit_request` line (pid, time) to `run\harness.log` before it sends a chunk that calls `UI.ExitGame`, and
the dialog watcher (`dialogwatch.known_exit_cause`) gives such a crash its own signature, `exe-crash: 0xC0000005
EXCEPTION_ACCESS_VIOLATION at the game DLL's unload after UI.ExitGame (-ia MemoryImports; known, harmless: 0483)`
(ledger `cfc8ac3165a7`, bug 0483), and a text that names the cause, when the CP DLL carries `VP_MEMIMPORTS` and a
request for that pid is at most 300 s old; an `-ia` DLL without a recorded request gets a hint only and the old
signature. Run B's watcher note carried the new signature [L, 04:42:02]. The earlier signature `exe-crash: 0xC0000005
EXCEPTION_ACCESS_VIOLATION (no proxy crash record)` (3 sightings) mixes two of these exits with c5h-mpk's unrelated
crash of 01:16 (pid 35632, no preload, 56 s after launch, during the start-up; Goldberg's `crashes.txt` recorded
`@address = 0x010F9612`, no stack; cause not investigated, no ticket unless it repeats, MC #3682). (Corrected 2026-11-18 by the MC from ticket 0534: its cause is in `installer-notes.md` 14.9 item 4: the EXE loaded the x64 DLL from the DLC folder, fixed by 2606's rename; `crash-ledger-audit.md` row 7.)

**Not verified.** That 0306's runs (2026-11-15, `-ia` pin, `UI.ExitGame()`) crashed the same way: no exit code was
recorded then (the dialog watcher records exit codes since 2026-11-16). The 1 s from request to end that 0371 reported
against run B's 8 s: run B's figure is measured (0.5 s polling).
