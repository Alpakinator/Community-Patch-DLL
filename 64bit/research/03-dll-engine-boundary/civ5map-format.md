# The .Civ5Map format and the 32-bit library's reader (ticket 0343) and writer (ticket 0326)

Sections 1-7: the reader (0343). Sections 8-10 (2026-11-16, ticket 0326): the writer `Save`, its re-save A/B against
the 32-bit library, the six WorldBuilder Lua methods on the product path and the live checks.

Written 2026-11-15 by the implementer of queue ticket 0343 (account B). The behaviour of `CvWorldBuilderMap::Load`
and its helpers in the 32-bit closed library (`CvWorldBuilderMap/lib/CvWorldBuilderMapWin32.obj` of the DLL fork),
established from that object's disassembly (llvm-objdump 19.1.7, scratch only, nothing committed) and the record
layouts of the fork's headers (`CvWorldBuilderMap/include/*.h`), and checked by the A/B of section 6. This file is
the description the x64 reader (`tools/x64-dll/closedlib-stubs/CvWorldBuilderMap/CvWorldBuilderMapReader.cpp`) was
written from (D83). All values little endian. [C] = from the disassembly and the A/B; [L] = from the files.
Earlier description of the header and the player record (one-off 9069): `tools/stressmap/civ5map.py`.

## 1. Versions [L, C]

The first byte is the version in the low 7 bits and 0x80 for a scenario file. The library reads versions 0-12
(Load0 ... Load10, Load11Plus for 11 and 12); 13 and above are refused.

| version | files of the Steam install (2026-11-15, 92 maps incl. `Maps/Civ5MapmapState_Turn000`) | read by the x64 reader |
|---|---|---|
| 6 | 1 (`Tutorials/Maps/UnitBenchmark`) | yes |
| 7 | 13 (`Assets/Maps/*` except M_AncientLake, 5 tutorials) | yes |
| 9 | 1 (`Tutorials/Maps/LateGameTest`) | yes |
| 10 | 1 (`Tutorials/Maps/Tutorial_3_ImprovingCities`) | yes |
| 11 | 12 (the 5 DLC_SP_Maps, 6 scenarios, M_AncientLake) | yes |
| 12 | 64 (all Brave New World era maps) | yes |
| 0-5, 8 | none (also none in the Hormigas pack or PTWE v3) | refused (returns false) |

Every file of the install is a scenario file (flag 0x80). Hormigas Enhanced Map Pack v1: 4 maps, PTWE v3: 4 maps,
all version 12.

## 2. Load and PreviewPlayableCivCount [C]

`Load(path, gameTypes, bScenario=true)`: open (the library through `FIFileSystem`, the x64 reader with Win32), read
9 bytes (version byte, u32 width, u32 height; fewer = false), refuse width x height above 32768 (unsigned 32-bit
product), store width and height, empty the map name and description, zero the 64-byte world type, then call the
version's reader with `scenario = (byte0 & 0x80) && bScenario`. On success, if `bScenario` and the file is not a
scenario file, `ClearScenarioData(false)`. A failed load leaves the object partly written (the DLL does not use it).

`PreviewPlayableCivCount(path)`: read 10 bytes (else 0); 0 unless scenario; 0 for versions 0-2; versions 3 and 4:
load the file into a fresh map with empty game types and count the players with `m_bPlayable`; version 5 and up:
byte 9 (the playable-civilisation count of the header).

## 3. Versions 9-12 (the common layout) [C]

After the 9 bytes:

1. u8 playable-civilisation count (read, not stored).
2. Map header: u32 flags, then 7 u32 section lengths: terrain list, feature list, natural-wonder list, resource list,
   mod data, map name, description. Fail if fewer than 32 bytes, if the lengths sum to 0 or above 0x100000. The flags
   replace the default 0x11 (world wrap, transferable) set before the read.
3. The 7 sections in one block: four type lists (each a run of NUL-terminated names; the last byte of each list is
   forced to NUL; a list of length 0 stays empty), the mod data (section 5; length 0 = no mod data, the types are
   reset), the map name and the description (each a C string at the start of its section).
4. Version 11 and 12 only: u32 length (fail above 64) and that many bytes of world size type (`WORLDSIZE_HUGE`; PTWE
   has length 0).
5. width x height plot records of 8 bytes: terrain, resource, feature, flags (river bits 0x01-0x20, major start
   0x40, minor start 0x80), height (0 flat, 1 hills, 2 mountains), continent, natural wonder, resource amount.
   Fewer bytes = false.
6. Only when the scenario is read: a 128-byte scenario header = the game description (84 bytes: 64-byte default game
   speed, u32 starting turn, u32 max turns, u32 target score, i32 start year, u8 players, u8 city states, u8 teams,
   1 pad) and 11 u32 section lengths. The description is stored even when fewer than 128 bytes come (then false).
   Then one block of size = the 11 lengths + 5 x relation bytes + visibility bytes + (version 10 and up) players x
   256; fail above 0x800000 or on a short read. Relation bytes = ceil(teams x (teams - 1) / 2 / 8); visibility bytes =
   ceil(teams x plots / 8). Its contents in order:
   improvement list, unit-type list, tech list, policy list, building list, promotion list, the unit records, the
   unit names, the city records, victory types, game options (two type groups: `[signed length][name][NUL]`
   entries, `Contains` walks them), then the five team relations (in contact, at war, permanent war or peace, open
   borders, defensive pacts; triangular bit matrices), city-state influence (players x 64 i32, the rows of
   `m_aiCityStateInfluence`), visibility (team-major, then plot index).
   - **Version 9 quirk:** the block's size does not count the influence, but the reader still consumes players x 256
     bytes of influence before the visibility, so the last players x 256 visibility bytes come from past the end of
     its block (heap contents, different from run to run: LateGameTest gave 2,429 / 2,419 / 1,721 set bits in three
     runs). The x64 reader reads zeros there.
7. teams x 64 team records (32-byte name, 256-bit tech set), players x 436 player records, city states x 436
   player records (layout: `civ5map.py`), plots x 8 scenario plot records (u16 city handle, u16 unit stack handle,
   u8 culture owner, u8 improvement, u8 route, u8 route owner; 0xFF/0xFFFF = none). Short read = false.

Element allocators (units, unit names, cities) are stored as `[i32 free-list head][entries]`, entry count =
(length - 4) / record size, a free entry holding the next free index in its first 4 bytes. Record sizes in the file
(the 32-bit layouts): Unit 84 (version 12), UnitV2 48 (9-11), UnitName 64, City 136 (12), CityV2 104 (9-11).
Versions 9-11 are converted to the current records: free entries (walked from the head **while the index is above
0**, so a free entry 0 is converted like a used one) keep only the next index; a UnitV2 becomes a Unit with the
stacked-unit handle kept, the **custom name handle set to none**, the type byte zero-extended, owner, direction,
flags, health, experience copied, promotions 0-255 copied; a CityV2 becomes a City with owner, flags, population,
health, name (`strcpy_s`, 64) and buildings 0-255.

Type translation (after the reads; the file's own lists against the game's `CvWorldBuilderMapTypeDesc`, built by the
header's `BuildLookupTable`, names interned case-insensitively so equal names compare equal without case):
- if any of terrain, feature, natural wonder, resource differs: every plot's terrain (an ID not below the file's
  list size becomes 0), feature, natural wonder and resource (out of the list: 0xFF) take the game's IDs;
- scenario only: if improvements or buildings differ, per plot the improvement is translated (0xFF out of the
  list) and, if buildings differ, the buildings of the plot's city (a set of 512; bit i moves to bit lut[i] for i
  below min(file list size, 512) when lut[i] < 512; other bits cleared); if techs differ, every team's techs (256);
  if policies differ, every major player's policies (256; city states are not translated); if units or promotions
  differ and there are unit records, every unit reached from a plot's unit stack (following the stacked-unit
  handles) gets its type translated (out of the list: 0xFFFFFFFF) and, if promotions differ, its promotions (512).

## 4. Versions 6 and 7 [C]

As section 3 with these differences: flags = file flags | 0x18 (old goodie rules, transferable); version 6 has 6
map lengths (no map name section: terrain, feature, wonder, resource, mod data, description) and a 28-byte header;
no world type; when the scenario is read `ClearScenarioData(false)` runs first; the scenario header is 120 bytes
(84 + 9 lengths: improvements, unit types, techs, policies, buildings, units, cities, victory types, game options;
no promotion list, no unit names, no influence); block = the 9 lengths + relations + visibility; units are OldUnit
(12 bytes: u16 stacked handle, u16 experience, float health, u8 type, owner, direction, bit 0 fortified, bit 1
embarked) converted to Unit (health = (int)(health x 100000), custom name none, promotions none); cities OldCity
(100 bytes: 56-byte name, u32 population (low 16 bits kept), u32 culture, u8 owner, bool localized name, 256-bit
buildings **not** carried over) converted to City (health 100000, localized-name flag from the bool); players and
city states are OldPlayer records of 304 bytes (256-bit policies, civilisation type 64, colour 64, era 64, handicap
60, u32 culture, gold, start x, start y, u8 team, bool playable) built into Player with **policies zeroed**, leader
and civilisation names empty, the four strings copied as C strings and then **culture set to 0**. Unit types are
translated for every unit on a plot's stack **whether or not the lists differ** (so a type beyond the file's list
becomes 0xFFFFFFFF); there is no promotion translation.

## 5. Mod data [C]

`[map ModType][plot ModType][u32 n][n x u32 values][u32 string bytes][string bytes]`. A ModType is
`[u32 fields][u32 enums][fields x {u32 type, u32 name string id, u32 default}][enums x {u32 members, members x u32
string id}]`. Field types: 0 bool, 1 float, 2 string, 3 and above an enum (enum index = type - 3). Values: the map
entry's fields first, then plot i's fields at `mapFields + plotFields x i`. Getters: `GetFieldType` returns min(type,
3) or -1 out of range; string ids at or beyond the buffer size give ""; `GetFieldName`/`GetMember` out of range give
NULL; defaults out of range give false / 0 / 0.0 / ""; `IsFieldDefaultValue` compares the stored dword with the
default.

## 6. The A/B (`tools/x64-dll/civ5map/run_ab.py`) [L, 2026-11-15]

Three executables of one dump program (`wbdump.cpp`: every field the load produced, as a canonical image, length and
FNV-1a 64 hash per section, C strings up to their NUL): x86 + the closed library (reference), x86 + the x64 reader's
source, x64 + the x64 reader. Inputs: the 92 maps of the Steam install, the 4 Hormigas pack maps and the 4 PTWE v3
maps (read only), and 11 negative inputs (missing file, 8 truncations of the largest map, version 13, 200 x 200).
Three passes: the game's type lists from the VP 5.4.8 test database (`typedesc.py`; what the DLL does), the same
with `bScenario=false`, empty game lists (every type translated to "invalid"). Each map is also put through
`SwapPlayers(0, last)` and `ClearScenarioData(true)` after the load (what the DLL's `Preload` does).

Result 2026-11-15 05:13 EST, DLL source 04b1211b8: **PASS: 100 maps + 11 negative inputs, the reference loads all
100, 0 differing maps in all 6 comparisons**; one explained difference (section 3, the version 9 quirk:
`visibility`/`visibility-bits` of LateGameTest, where only the file part, `visibility-v9head`, is compared). The
comparison is sensitive: the first run (versions 6-10 not yet read, the version 6/7 flag bits missing, City names
compared with their bytes after the NUL) showed 24 differing maps. The x86 build of the reference needs the fork's
`clang.cpp` (`_ftol3`, `_ltod3`), `_SECURE_SCL=0` and `_HAS_ITERATOR_DEBUGGING=0` (the closed library's `std::vector`
is 16 bytes, so `CvWorldBuilderMapTypeDesc` is 160 bytes; the DLL gets the same from `CvGameCoreDLLPCH.h`).

Record layouts that differ on x64 (std::bitset aligns to 8): Player 436 -> 440, Unit 84 -> 88 (promotions at 24
instead of 20), OldCity 100 -> 104; ModType, ModEnum, the type group and the allocators hold pointers. The reader
converts the three records field-preserving; every other file record has one layout on both.

## 7. The product path: the Lua methods and the live game [L, 2026-11-15]

**The engine's WorldBuilder Lua methods.** `ICvWorldBuilderMapLoader1` has six methods taking an engine `lua_State*`.
The four a map start can need are served since 0343 (`proxy/src/proxy_lua.cpp` `WbLuaMethod`, `server/src/remote/
srv_remote_lua.cpp` `WbServe`; protocol in the comment there): the proxy registers the engine's state as an engine Lua
thread for the length of the call and sends the stack; the server runs the DLL's method on a scratch thread borrowed
for that engine thread (as `CreateLuaThread` makes one: globals fetched from the engine thread, `LoadFile` forwarded,
fetched functions called in the engine VM) and returns the values the method left; for `LoadModData` it starts from a
fresh `MapModData` table and the proxy sets the two fields the method writes (`Globals`, `Plots`) into the engine's
table. A Lua error in the DLL's method is logged and the call returns nothing (the 32-bit DLL would raise it).
~~`ScatterResources` and `ScatterGoodies` are the world builder's and stay loud defaults (ticket 0326).~~
*Corrected 2026-11-16 (ticket 0326):* `ScatterResources` and `ScatterGoodies` are served the same way (section 10). Seen in the
Hormigas game: `GetMapPreview` (once, at the start, from the front end) and the DLL's own `CreateLuaThread
("WorldBuilderMapLoader")` + `LoadFile(WorldBuilderRandomItems.lua)` (`InitMap`'s path, already served before 0343);
`LoadModData` and `AddRandomItems` were not called in this flow [L, server.log].

**The live game.** Layout `C:/c5-64bit-work/0343/bR2` (x64 DLL 0df58cca..., server.exe 0c918639..., proxy
cafa8787...; DLL pin 04b1211b8, this repository at 83eba391b, release configuration), instance c5h-e (F:), cp, vp, eui
and the Hormigas pack (`mappack.py apply`), started by `tools/stressmap/calibrate.py c5h-e --pack --turns 5`
(21 AI majors + the observer, Huge, Standard). Evidence `evidence/0343/` (per-turn CSVs, run.json, memory tables).

| run | route | load | t0: plots, majors, minors, units | t5: cities, units | EXE committed t0 -> t5 (MB) | EXE free t5 | server private t0 -> t5 |
|---|---|---|---|---|---|---|---|
| live1 05:37 | product path | 124 s | 16,920, 21, 33, 129 | 54, 76 | 2,316 -> 2,381 | 1,311 | 268 -> 282 |
| live2 05:46 | product path | 138 s | 16,920, 21, 33, 129 | 54, 77 | 2,306 -> 2,345 | 1,312 | 262 -> 280 |
| stock1 05:51 | stock (the proxy's off switch, 32-bit DLL 3c2f47da) | 122 s | 16,920, 21, 32, 127 | 53, 76 | 2,585 -> 2,623 | 931 | - |

Both product runs: the pack's own load hook ran (`Lua.log`: `MapPack highjacking - Setting Map`, `Map ID : 1`, a
`Civtype` line per civilisation), autoplay handed back at turn 5, safe stop; `server.log`: scratch Lua errors 0,
methods without remote treatment 0, no LOUD line in `vpproxy.log` or `server.log`, `CvAssert.log` empty, peak server
working set 339-340 MB. The Lua runtime errors in `Lua.log` (`TechButtonInclude.lua` `GetHelpTextForUnit` /
`GetHelpTextForBuilding`, `VPUI_core.lua:151` `DB`, the pack's `MapPackFunctions.lua:90` `Events`, `VPUI_loader.lua`)
are the same kinds on the stock route in the same flow (stock1) and are not the patch's. Plot data in game at turn 5,
product (live2) against stock (stock1), all 16,920 plots read over the Lua channel (`plotdump.py`, `cmpplots.py`):
terrain, plot type, river edges and natural wonders equal everywhere; 2 features and 8 resources differ, where the two
games differ (a city-state drawn differently, 32 against 33; start-plot resource balancing) [I]. The plot data the
reader produces is compared exactly by the A/B of section 6.

## 8. The writer: `Save` (ticket 0326) [C, 2026-11-16]

Established 2026-11-16 by the implementer of queue ticket 0326 from the 32-bit closed library's `Save` and its
helpers (same object, same tool, scratch only), and checked by the re-save A/B of section 9. The x64 writer
(`tools/x64-dll/closedlib-stubs/CvWorldBuilderMap/CvWorldBuilderMapWriter.cpp`) is written from this section (D83).

`Save(path, typeDesc, bScenario)` always writes **version 12** (the newest layout, section 3): first byte 0x8C when
`bScenario`, else 0x0C. The type lists it writes are the ones of `typeDesc` (the game's lists, which the plot and
scenario IDs in memory refer to after a load), never the file's own lists. Steps, each a separate write:

1. Open the path for writing through the engine's file system (`FFileInstance`, mode write). Not open: return false
   (nothing deleted).
2. 10 bytes: version byte, u32 width, u32 height, u8 playable count = the number of the first `byPlayerCount`
   players with `m_bPlayable` set when `bScenario`, else 0.
3. 32 bytes: u32 flags (`m_uiFlags` as it is), then the 7 lengths: terrain, feature, natural-wonder, resource list
   (each: the sum over the list's entries of name length + 1, a missing name counting as an empty one), mod data (0
   when both mod types, map and plot, have no field; else the mod data's serialized size, section 5), map name
   (length + 1), description (length + 1).
4. One block of the 7 sections: each list as its names, each followed by one NUL; the mod data (only when its length
   is not 0) as section 5's layout (`[map ModType][plot ModType][u32 n][n x u32][u32 string bytes][string bytes]`,
   the string bytes copied only when the buffer exists); the map name and the description as their bytes and a NUL.
   The block goes out in one write; fewer bytes written = failure.
5. u32 64, then the 64 bytes of the world type buffer as they are (bytes after its NUL included). These two writes are
   not checked.
6. width x height x 8 bytes of plot records as they are in memory. Fewer = failure.
7. Only when `bScenario`:
   - 128 bytes: the 84-byte game description as it is in memory, then 11 lengths: improvement, unit-type, tech,
     policy, building, promotion list (of `typeDesc`), units, unit names, cities (each allocator: 0 when it has no
     entry, else 4 + entries x 84 / 64 / 136), victory types, game options (the type groups' buffer sizes). **When
     the unit allocator has no entry, the unit-type and the promotion list lengths are written as 0** and those two
     lists are left out of the block.
   - One block, in this order: the six lists (the unit-type and promotion lists only when their length is not 0),
     units, unit names, cities (each, when it has entries, as `[i32 free-list head][entries]` with every entry,
     used or free, in the 32-bit record layout as it is in memory), victory types, game options (their buffers), the
     five team relations (in contact, at war, permanent war or peace, open borders, defensive pacts; each the first
     ceil(teams x (teams - 1) / 2 / 8) bytes of its bitset), city-state influence (players x 256 bytes: the first
     `byPlayerCount` rows of `m_aiCityStateInfluence`, 64 i32 each), visibility (the first ceil(teams x plots / 8)
     bytes of the bitset). Its size is exactly what the reader of section 3 expects for version 12. One write; fewer
     = failure.
   - teams x 64 bytes of team records, `byPlayerCount` x 436 player records, `byCityStateCount` x 436 city-state
     records, plots x 8 scenario plot records, each as it is in memory, each a write checked for its length.
8. Close; return true (the time taken is measured and not used).

On any checked failure after the open: close the file, **delete it** through the engine's file system, return false.
Teams, players and city states come from the game description's counts (`byTeamCount`, `byPlayerCount`,
`byCityStateCount`), not from the arrays' sizes. Nothing is validated: a count above the array size would write past
it (a loaded file never has one, section 3).

**x64 [C].** The writer converts the three records whose x64 layout differs back to the file layout: Player (440 ->
the first 436 bytes, offsets equal), Unit (88 -> 84: bytes 0-19, then the 64 promotion bytes from offset 24), and
writes every other record as it is. Free allocator entries go through the same conversion (their first 4 bytes are
the next free index). The file is written with Win32 (`CreateFileW`, `CREATE_ALWAYS`), not through the server's
`FIFileSystem`, which opens only files inside its run folder; a failure after the open deletes the file
(`DeleteFileW`).

**Where the DLL calls it.** `CvWorldBuilderMapLoader::Save(path, mapName)` (DLL source `CvWorldBuilderMapLoader.cpp`):
`Resize` to the game map, `ClearScenarioData()` unless the custom-mod option `LOG_MAP_STATE`, map name (the file title
when none is given), empty description, flags = world wrap only, world type, every plot's map record from `CvPlot`,
then `Save(path, the game's typeDesc, LOG_MAP_STATE)`. Callers: the engine's `ICvWorldBuilderMapLoader1::Save` (the
in-game **Save Map** button: `SaveMapMenu.lua` calls `UI.SaveMap(name)`), and `CvGame::LogMapState` with
`LOG_MAP_STATE` and logging on (turn 0 only, the relative path `Maps\Civ5MapmapState_Turn000.Civ5Map`). A relative
path is resolved against the process's working directory: the EXE's is the install folder (the 32-bit game wrote
`Maps/Civ5MapmapState_Turn000.Civ5Map` there on 2026-07-02 [L]); the server's is its own folder (`proxy_rt.cpp`), so
on the product path that log file goes to the server's folder and the save fails when it has no `Maps` folder [I].
The Save Map button passes the path the EXE builds: the file lands in `<user>\Maps\<name>.Civ5Map` on both paths [L, 2026-11-16, section 10].

## 9. The re-save A/B (`tools/x64-dll/civ5map/run_ab.py`, writer part) [L, 2026-11-16]

The A/B of section 6, extended: `wbdump --resave DIR` saves every loaded map in five cases and prints each written
file's length and hash; `run_ab.py` keeps the files and compares them byte for byte with the reference's
(`compare_saves`). The cases (`wbdump.cpp` `SaveCase`): **scen** = `Save(bScenario=true)` right after the load (the
re-save), **map** = `Save(false)` (no scenario part), **cleared** = `Save(true)` after `SwapPlayers(0, last)` and
`ClearScenarioData(true)` (the object the DLL keeps after its `Preload`), **dllflow** = the DLL's Save Map sequence
(`Resize` to the map, `ClearScenarioData()`, name, empty description, flags = world wrap only,
`SetWorldType("WORLDSIZE_SMALL")`, `Save(false)`), **resized** = `Resize` to half the width, a refused `Resize(0, 5)`, a
refused 65-character world type (the buffer cleared), `Save(true)`. Refusals of the writer, in every executable: a path
in a missing folder, an empty path and a NULL path each answer false and leave no file. The objects are built in
zero-filled storage (calloc + placement new), as the DLL's are statics: the fields the constructors leave alone are
then zero on both sides.

Result 2026-11-16 13:05 EST, DLL source 04b1211b8 (plain copy), debug configuration, types from the VP 5.4.8 test
database (`research/03-dll-engine-boundary/evidence/0326/`): **PASS**. Reader comparisons unchanged (0 differing maps
in all 7). Writer: game pass 489 of 500 re-saved files byte-identical in x86, x64 and x64 under ASan, 11 explained, 0
differ; no-scenario pass 510 of 510 identical; no-types pass 494 of 500 identical, 6 explained; refusals as expected in
all four executables. Confirmed on the release configuration the same day (13:43, `ab-summary-release.txt`): the same counts. The comparison is sensitive: before the explanation below was characterised, the 11 files were
reported with the differing sections (units, cities) and byte counts.

**Explained difference: bytes the 32-bit library leaves undefined [C].** Re-saves (case scen) of version 6-11
scenarios with units or cities: 1066Scenario, KoreaScenario, Americas, Mediterranean, Mesopotamia, LateGameTest and the
five tutorials (16 to 6,041 bytes each). The 32-bit library converts the legacy unit and city records (section 3:
UnitV2, CityV2; section 4: OldUnit, OldCity) into an array it takes from `FireMalloc` (the EXE's small-block allocator
or `malloc`: neither clears memory) and writes only part of each entry: a free entry only its first 4 bytes (the next
free index), a unit everything but byte 19 (the padding after the flags), a city everything but the name bytes after its
NUL (`strcpy_s`). The writer copies the entries as they are, so those bytes of the file are the allocator's earlier
contents: 0xFF in the A/B host, unknown in the game. The x64 reader converts into zero-filled memory, so the x64 writer
writes zeros there. `run_ab.py` `legacy_undefined` names exactly these bytes (free list walked as the library walks
it: from the head while the index is above 0) and requires every other byte to be equal. LateGameTest (version 9) also
carries the visibility tail of section 3 (`v9_tail_only`). Version 12 files (every Brave New World map, the Hormigas
pack, PTWE) have no such bytes: their records are copied verbatim. In play these bytes reach a file only when a game
started from a version 6-11 scenario is saved with its scenario part, which the DLL does only with the custom-mod
option `LOG_MAP_STATE` [I].

## 10. The six WorldBuilder Lua methods and the live checks (ticket 0326) [L, 2026-11-16]

**Lua methods.** `ScatterResources` and `ScatterGoodies` (each loads `WorldBuilderRandomItems.lua` into the engine's
state and calls `AddResourcesForWorldBuilderMap` or `AddGoodies` there; the DLL's methods return 0) are served like the
four of section 7: `ProxyHook_ICvWorldBuilderMapLoader2_ScatterResources/ScatterGoodies` (`proxy_lua.cpp`
`WbLuaMethod`) and `ServerHook_...` (`srv_remote_lua.cpp` `WbServe`, which already knew both). The generated proxy has
32 loud defaults left, none of `ICvWorldBuilderMapLoader1`. Not seen live: nothing in a map-script game, a `.Civ5Map`
start or the Save Map button calls them (the EXE holds their names beside `SetMapOption` and `GenerateMap` and has
`ScatterResourcesComplete` / `ScatterGoodiesComplete` strings [L, a byte search of the EXE]; which EXE screen calls
them is not established). Like `AddRandomItems`, they are covered by the mechanism, not by a live call.

**No loud default reachable without a game [L].** `test_proxy_rt.py`'s strict checks need an engine-to-DLL method
whose proxy body is the loud default, reached from the context without a game. With the six served, the generated
code's remaining engine-to-DLL loud defaults are the four `GetSyncArchive()` of `ICvCity1`, `ICvPlayer1`, `ICvPlot1`,
`ICvUnit1`; a loader path GetGame -> GetPlayer(0) -> GetSyncArchive fails before the call:
`ICvGameContext1::GetGame` returns NULL until the context's `Init` (the DLL's `GC.getGamePointer()`), which the loader
cannot call without an engine (`Init` runs `gDLL->InitGlobals()`). *Resolved the same day (MC return of 0326):* the
strict checks call the generated hook `ProxyHook_ICvPlayer3_GetSyncArchive` (body: `ProxyRt::Unimplemented`, `self`
unread) at its RVA in the shipped proxy, read from the proxy's PDB with `llvm-pdbutil dump --publics` and the DLL's
section table (`test_proxy_rt.py` `symbol_rva`, loader scenario `unimpl-rva`); nothing test-only in the proxy. A
debug-configuration server cannot run these checks: its x64 DLL imports `Database::Results::Bind(int, float)`, which
the server's write recording does not cover, so every boot ends with exit code 4 [L, 2026-11-16]: the suite needs a
release build. *Corrected 2026-11-25 (ticket 0752): the server now hooks that import, so a debug build runs these
checks too (`upstream-fix-batch-2.md`, "Fixed by ticket 0752"); replaced "the suite needs a release build".*

**Live, layout `C:/c5-64bit-work/0326/bR`** (release; x64 DLL c8b2b509..., server.exe cb97c5e0..., proxy 7f1e6453...;
DLL pin 04b1211b8, this repository at ec7115f6d; fallback 32-bit DLL i0ship-26234371-ia 3c2f47da), instance c5h-e (F:,
`align548` and `verify` ok first), cp, vp, eui and the Hormigas pack. Evidence `evidence/0326/`.
- **Hormigas 5-turn game** (`tools/stressmap/calibrate.py c5h-e --pack --turns 5`, 13:28-13:33): load 158.5 s;
  turn 0: 16,920 plots, 21 AI majors, 32 city states, 127 units; turn 5: 53 cities, 76 units (0343: 54 / 76-77);
  autoplay handed back at turn 5, safe stop rc 0; EXE committed 2,385 -> 2,413 MB, free 1,197 MB at turn 5; server
  private 267 -> 281 MB, peak working set 340 MB; `server.log`: scratch Lua errors 0, methods without remote treatment
  0, `GetMapPreview` and the loader's `CreateLuaThread("WorldBuilderMapLoader")` served; no LOUD line in `vpproxy.log`
  or `server.log`; no `CvAssert.log`. The server's last line reads "FAILED: the engine is gone ... PeerDied": the
  harness's stop ends the EXE by terminate (after the human-turn and settled-save checks), so the server sees its peer
  die; that is the normal end of a stopped run, not a fault.
- **Save Map, product against stock**: the turn-0 save of that game (`C0326-PRODUCT-T000`) loaded once on the product
  path and once on the stock 32-bit DLL (the same instance, `set-proxy --original`), then `UI.SaveMap(name)` over the
  Lua channel, what the in-game Save Map button calls. Both wrote `<user>\Maps\<name>.Civ5Map` (version 12, no
  scenario part, 180 x 94, flags world wrap, 137,337 / 137,335 bytes): every section equal (type lists, mod data none,
  description, world type `WORLDSIZE_HUGE`, all 16,920 plot records) except the map name, which is each arm's own file
  name with extension (`c0326savemapproduct.Civ5Map` / `c0326savemapstock.Civ5Map`): `savemap-ab.txt`. Both files load
  identically in the 32-bit library and the x64 reader, and the 32-bit library's re-save without the scenario part of
  the product's file equals that file byte for byte (`savemap-reload-closed-x86.txt`). So the EXE passes the name with
  its `.Civ5Map` extension as `szMapName` [L].
