# Triage of the implicit 64->32 truncation sites (issue 1003)

Written 2026-11-10 by worker 1003 of team 1000. DLL fork: census at `x64` f3d2f5669 (1002's and 1004's fixes
included), fix committed as **0831b4b67** (detached, a worktree `Community-Patch-DLL-x64-w1003` beside the DLL source clone).
Line numbers in the table are at f3d2f5669. Evidence and scripts: `tools/x64-dll/evidence/2026-11-10-w1003/`.
Confidence: `[C]` confirmed by build, disassembly or test program, `[I]` inferred from reading.

## 1. Result

| Check | Value |
|---|---|
| Census: clang-cl 19.1.7 `-m64 /MD /DVP_OUT_OF_PROCESS -Wshorten-64-to-32`, other flags as `evidence/2026-11-10-probe/probe-pch.bat` | 173/173 TUs compile; 948 warnings, **794 unique sites in 73 files** [C] |
| Same census at 8e26515e8 (1002 only) | the same 794 sites (same file, column, types and source text); only 6 CvGame.cpp lines moved by one [C] |
| Same set as the probe (b25fd45c0) | yes: 794 = 794; the only differences are 3 CvGlobals.cpp line numbers shifted by 1002's insertions [C] |
| Sites removed by 1004's save-width pins | **0**. 1004's sites wrote a 64-bit value into the stream (no narrowing, so no `-Wshorten` warning); its pins add narrowings by explicit cast, which do not warn either. No row is marked "fixed by 1004" [C] |
| Class (a) benign by construction | **773** |
| Class (b) correct, an explicit cast would only add clarity | **19** |
| Class (c) real risk | **2** sites, 1 fixed, 1 needs no change; plus **one systematic Lua item (L1)** that covers all 492 `lua_Integer` sites and is fixed in `luaconf.h`, not at the sites (section 3) |
| Fix | `CvDatabaseUtility.cpp:125` `unsigned int iNumBytes` -> `size_t` (fork 0831b4b67) [C: x64 recompile, 0 warnings in that TU] |
| x64 compile after the fix | 173/173 (172 unchanged TUs from the f3d2f5669 run + the fixed TU recompiled) [C] |
| x86 `python build_vp_clang.py --config debug` after the fix | compiles and links `clang-output\Debug\CvGameCore_Expansion2.dll`, exit 0 [C]. x86 code unchanged: `size_t` is `unsigned int` on x86, the edited line keeps its line number [I: by type identity; objects not compared] |

By type: 492 `lua_Integer` (`long long`) -> `int`/`uint` (`lua_tointeger` 450 lines plus `luaL_checkinteger`/`luaL_optinteger`
and the `GET_TABLE_VALUE` macro); 295 `size_t`/`size_type` -> `int`/`uint`/`unsigned long`/`DWORD`; 7 `ptrdiff_t`-class
(`FFastVector.h` pointer differences, one `std::count`). No site narrows a pointer, handle, time, file size or hash
[C: every non-Lua site read, list in section 6].

Where the sites are: 497 in `Lua\CvLua*.cpp`, 4 in `CvMapGenerator.cpp` (Lua map-script arguments), 1 in
`CvGameCoreDLLUtil\include\CvLuaArgTemplates.h:19` (`CvLuaArgs::toValue<int>`), 292 elsewhere.

Sites in files this task does not own (listed, not edited, all class (a) or (b)): epic 1300 `CvTacticalAI.cpp` 53,
`CvHomelandAI.cpp` 3; 1004 `CvGame.cpp` 7, `CvEspionageClasses.cpp` 9, `CvAStar.cpp` 3; 1002 `CvGlobals.cpp` 3. None is
inside the three save writers issue 1008 is changing (`CvCity.cpp:32427`, `CvEconomicAI.cpp:4765`, `CvGame.cpp:10887`).

## 2. Classes

- **(a) benign by construction (773).** The 64-bit value is the size of a game-data or entity container (infos,
  plots, units, deals, spies, path nodes), a `size_t` loop index over one, a string length of an identifier or SQL text,
  or a Lua argument that is a game id, enum, count or flag. All are far below 2^31. 60 are RNG bounds
  (`urandLimitExclusive(v.size(), ...)`, `getAsyncRandNum`, `shuffleArray`): the value is the same on both builds, so
  the draw is the same; `CvSeeder(T)` already does `static_cast<uint>` and `CvRandom::get` takes `unsigned long`, which
  is 32 bits on both builds (LLP64) [C: `CvRandom.h:41-92`].
- **(b) clarity cast only (19).** 12 `size() - 1` (or `size() - ui`) expressions: on an empty container the 64-bit
  result is `SIZE_MAX`, which narrows to -1, exactly as the 32-bit `0xFFFFFFFF` does on x86; the loops and sentinels
  that rely on -1 work on both. 6 `FFastVector.h` pointer differences into `INT`/`UINT` indices (capacity is `UINT`). 1
  `std::count` result. Casting them would make the intent visible but changes no behaviour.
- **(c) real risk (2 sites + L1).**

| Site | What | Action |
|---|---|---|
| `CvDatabaseUtility.cpp:125` `Initialize2DArray` | allocation size of a 2D `int` table, includes `iCount1 * sizeof(int*)` (8 on x64), narrowed to `unsigned int`. Values are DB row counts (hundreds x tens), so it cannot overflow in practice, but it is a memory size | **fixed**: `size_t iNumBytes` (fork 0831b4b67) |
| `CvPlayer.cpp:45842` `operationCount = player.m_AIOperations.size()` | reaches the save stream | none: `operationCount` is declared `uint32`, so the record is 4 bytes on both builds; the narrowing is the width pin (1004's census is silent here) |
| **L1** every `lua_tointeger` / `luaL_checkinteger` / `luaL_optinteger` site (492) | number->integer conversion differs from the game's Lua on x64 (section 3) | fix in `ThirdPartyLibs/Lua51/include/luaconf.h` together with issue 1005's Lua build; not edited here (section 3.3) |

## 3. Lua: `lua_Integer` width and conversion

### 3.1 What is unaffected [C]

- **Values the DLL pushes.** `lua_pushinteger(L, int)` widens to `lua_Integer` and stores a `double`; every `int` is
  exact. `lua_pushnumber` sites push `int`, `float` or ratios; unchanged.
- **Round trips of in-range integers.** An `int` pushed and read back with `lua_tointeger` is exact on both builds.
- **No 64-bit value crosses the Lua boundary.** No Lua method pushes or reads a 64-bit integer (`grep` for
  `long long`, `__int64`, `uint64`, `getSeed`, `RandSeed` in `Lua\*.cpp`: none). `Game.GetRandSeed` does not exist in
  the DLL's Lua API; `CvRandom::m_ullRandomSeed` is not exposed. Hashes reach Lua only as `(int)FStringHash(...)`
  (`CvLuaEnums.cpp:378`), negative values included, exact as doubles. Unit/city ids are `int`.

### 3.2 What differs: conversion of non-integral and out-of-range numbers (L1)

The game's `lua51_Win32.dll` (SHA-256 f58ceffe...0e35, identical to `lua51_Win32.dll.stock`) implements
`lua_tointeger` with `fld qword; fistp dword` (RVA 0x1610, ImageBase 0x10000000) [C: `llvm-objdump`]. `fistp` rounds
with the x87 rounding mode (round to nearest, ties to even, by default) and stores the integer-indefinite value
0x80000000 for NaN and anything outside `int` range. A stock Lua 5.1.4 built for x64 with the fork's `luaconf.h` takes
the non-x86 branch: `lua_Integer` is `ptrdiff_t` (64-bit) and `lua_number2integer` is a C cast (truncation toward
zero); the DLL then narrows the 64-bit result to `int` (the 492 warnings). Measured with a test program built by
clang-cl 19.1.7 for both targets (`evidence/2026-11-10-w1003/luaconv/`) [C]:

| Lua number | x86 game (`fistp`) | x64 stock luaconf + DLL narrowing | x64 proposed (`_mm_cvtsd_si32`) |
|---|---|---|---|
| 1.5 / 3.5 / 2.7 | 2 / 4 / 3 | 1 / 3 / 2 | 2 / 4 / 3 |
| -1.5 / -2.7 | -2 / -3 | -1 / -2 | -2 / -3 |
| 2.5 / -2.5 / 2.2 | 2 / -2 / 2 | 2 / -2 / 2 | 2 / -2 / 2 |
| 2147483647.6 | -2147483648 | 2147483647 | -2147483648 |
| 3e9 / 4294967295 / 4294967296 | -2147483648 (all) | -1294967296 / -1 / 0 | -2147483648 (all) |
| -2147483649 / -3e9 | -2147483648 | 2147483647 / 1294967296 | -2147483648 |
| 1e15 / 1e20 / NaN | -2147483648 | -1530494976 / 0 / 0 | -2147483648 |

The proposed conversion matches the game on all 27 inputs [C]. Integral values in `int` range agree in all three
columns.

Why it matters: mod and UI Lua routinely passes computed numbers (`x * 1.5`, `x / 2`) to DLL methods that read them
with `lua_tointeger` (`ChangeGold`, `SetBaseYieldRate`, ...). With the stock x64 conversion every such call that
receives a fractional value rounds differently from the 32-bit game, so the x64 server's game state drifts from the
x86 game for the same save and inputs. Out-of-range and NaN arguments give different garbage. Lua-internal conversions
(`lua_number2int`: table array indexing, `string.format('%d')`, `string.rep` counts) have the same split [I: same
macro].

### 3.3 Recommended fix (for issue 1005 and the coordinator; not edited here)

Edit the fork's `ThirdPartyLibs/Lua51/include/luaconf.h` under `_WIN64` only, and build the x64 `lua51.lib` from the
same header (1005 already matches the fork's `luaconf.h`):

```c
/* line 143 */
#if defined(_WIN64)
#define LUA_INTEGER	int	/* the game's 32-bit Lua: lua_Integer is 32 bits */
#else
#define LUA_INTEGER	ptrdiff_t
#endif

/* before the "this option always works" #else near line 576 */
#elif defined(_WIN64) && defined(LUA_NUMBER_DOUBLE)
#include <emmintrin.h>
/* cvtsd2si: MXCSR rounding (nearest-even by default), 0x80000000 on NaN/overflow, like x87 fistp dword */
#define lua_number2int(i,d)	((i)=_mm_cvtsd_si32(_mm_set_sd(d)))
#define lua_number2integer(i,n)	lua_number2int(i, n)
```

Effects: the 492 Lua sites stop narrowing (the warning count drops to 302); conversion semantics equal the game's
[C for the conversion, test above; I for the full Lua build]; the x86 build is untouched (the DLL's x86 build uses the
game's `lua51_Win32.dll` and never expands these macros [C: 1002's grep]). The header and the library **must** change
together: a DLL compiled with `LUA_INTEGER int` against a `lua51.lib` built with `ptrdiff_t` (or the reverse) passes
and returns the wrong width at every `lua_tointeger`/`lua_pushinteger` call. Not done here because 1005 owns the Lua
build and is working on `luaconf.h` (1002 handed it the `luaconf.h:557-563` finding). Assumption [I]: the game runs
with the x87 rounding mode at its default (nearest); nothing in the DLL changes it (`_controlfp`/`fldcw`: not in the
DLL's sources), the EXE and D3D were not checked.

## 4. Warning policy recommendation

**Off for the DLL's upstream sources, on (as an error) for code this project adds, and a census gate instead of the
warning.**

- Do not enable `-Wshorten-64-to-32` for the whole x64 build: 794 warnings (302 after the `luaconf.h` fix), all but
  two benign, would bury new ones; and silencing them with casts is ~790 touched lines, each a future merge conflict
  with upstream Vox Populi.
- Enable `-Werror=shorten-64-to-32` for files the project writes for x64: the closed-library replacements and stubs
  (`tools/x64-dll/closedlib-stubs/`, the Lua build), the server and proxy code, and any new DLL file.
- Keep this census as a scripted check (`build.py --shorten`, issue 1001's script when it lands): compile with the
  warning, key each site by (file, column, from-type, to-type, source text) so line shifts do not count, and fail on any
  site not in the committed baseline `evidence/2026-11-10-w1003/sites.tsv`. New sites then get triaged one at a time.
  `parse.py` here produces that key set.

## 5. Related findings (not `-Wshorten` sites)

- `%u` with `size_t` in log text (e.g. `CvGameCoreUtils.cpp:2268`, handed over by 1002): on x64 the 8-byte vararg slot
  is read as 32 bits, so the value is cut above 4 G and later arguments do not shift. Log text only; not changed.
  `clang -Wformat` does not see these (the DLL's format functions carry no format attribute) [I: `-Wformat` is on by default and the census shows none].
- `FStringHash` (every hard-coded hash enum, e.g. `AUTOMATE_TRADE = 0x4bdc68d8` in `CvEnums.h:1936`) returns `uint`
  from the closed FireWorks library; its x64 replacement (1006) must reproduce the exact CRC32 (see
  `save-layout-audit.md` section 5).
- Hash-order and STL drift (unordered containers in saves, `random_shuffle`, heap ties) is 1004's
  `save-layout-audit.md` section 6 and issue 1008; not repeated here.

## 6. The table (794 sites)

`L1` = Lua number conversion, section 3.2. Expression text is the source line at f3d2f5669, cut at 110 characters.

| # | Site (file:line:col) | From -> to | Expression | Class | Note |
|---|---|---|---|---|---|
| 1 | `CvAchievementInfo.cpp:58:32` | size_type (unsigned long long) -> int | `return m_paAchievementEntries.size();` | a | container size returned as `int`/`uint` (game-data or entity container) |
| 2 | `CvAIOperation.cpp:238:25` | size_t (unsigned long long) -> int | `int iNumUnits = pArmy->GetNumSlotsFilled();` | a | `size_t` loop index or count passed to an `int` parameter; bounded by game data |
| 3 | `CvAIOperation.cpp:428:59` | unsigned long long -> int | `pThisArmy->AddUnit(pUnit->GetID(), freeSlotInfo[iIndex].first, freeSlotInfo[iIndex].second.m_requiredSlot);` | a | `size_t` loop index or count passed to an `int` parameter; bounded by game data |
| 4 | `CvAIOperation.cpp:523:48` | size_t (unsigned long long) -> int | `OperationSlot opSlot(m_iID, pArmy->GetID(), iI);` | a | `size_t` loop index or count passed to an `int` parameter; bounded by game data |
| 5 | `CvArea.cpp:321:33` | size_type (unsigned long long) -> int | `colFall = occupiedCols.size()-1;` | b | `size() - 1` (or `- ui`): on an empty container wraps to `SIZE_MAX`, narrows to -1 on both builds; correct by modular wrap, an explicit `(int)` before the subtraction would say so |
| 6 | `CvArea.cpp:352:33` | size_type (unsigned long long) -> int | `rowFall = occupiedRows.size()-1;` | b | `size() - 1` (or `- ui`): on an empty container wraps to `SIZE_MAX`, narrows to -1 on both builds; correct by modular wrap, an explicit `(int)` before the subtraction would say so |
| 7 | `CvArmyAI.cpp:657:47` | size_t (unsigned long long) -> int | `pThisOperation->UnitWasRemoved(GetID(), iI);` | a | `size_t` loop index or count passed to an `int` parameter; bounded by game data |
| 8 | `CvArmyAI.cpp:665:12` | size_t (unsigned long long) -> int | `return iI;` | a | `size_t` loop index or count passed to an `int` parameter; bounded by game data |
| 9 | `CvAStar.cpp:3286:22` | size_type (unsigned long long) -> int | `return path.vPlots.size();` | a | container size returned as `int`/`uint` (game-data or entity container); 1004 file, list only |
| 10 | `CvAStar.cpp:3459:29` | size_type (unsigned long long) -> int | `int iPathLen = path.vPlots.size();` | a | container size of game data or entities; 1004 file, list only |
| 11 | `CvAStar.cpp:3521:30` | size_type (unsigned long long) -> int | `int iNumNodes = path.vPlots.size();` | a | container size of game data or entities; 1004 file, list only |
| 12 | `CvBarbarians.cpp:784:59` | size_type (unsigned long long) -> uint (unsigned int) | `uint uSwap = kGame.urandLimitExclusive(vPotentialPlots.size(), CvSeeder::fromRaw(0xfee11010).mix(pLoopPlot-...` | a | RNG bound = container size; same value on both builds, so the same draw |
| 13 | `CvBarbarians.cpp:795:67` | size_type (unsigned long long) -> uint (unsigned int) | `uint uSwap = kGame.urandLimitExclusive(vPotentialCoastalPlots.size(), CvSeeder::fromRaw(0x183f902e).mix(pLo...` | a | RNG bound = container size; same value on both builds, so the same draw |
| 14 | `CvBarbarians.cpp:804:46` | size_type (unsigned long long) -> int | `int iMaximumCamps = vPotentialPlots.size() / std::max(theMap.getWorldInfo().getFogTilesPerBarbarianCamp(), 1);` | a | container size of game data or entities |
| 15 | `CvBarbarians.cpp:941:58` | size_type (unsigned long long) -> uint (unsigned int) | `uint uIndex = kGame.urandLimitExclusive(vRelevantPlots.size(), CvSeeder::fromRaw(0xe4ddfef8).mix(iNumCampsI...` | a | RNG bound = container size; same value on both builds, so the same draw |
| 16 | `CvBarbarians.cpp:1274:88` | size_type (unsigned long long) -> uint (unsigned int) | `uint uIndex = bPrioritySpawn ? GC.getGame().urandLimitExclusive(vPrioritySpawnPlots.size(), CvSeeder::fromR...` | a | RNG bound = container size; same value on both builds, so the same draw |
| 17 | `CvBarbarians.cpp:1274:234` | size_type (unsigned long long) -> uint (unsigned int) | `uint uIndex = bPrioritySpawn ? GC.getGame().urandLimitExclusive(vPrioritySpawnPlots.size(), CvSeeder::fromR...` | a | RNG bound = container size; same value on both builds, so the same draw |
| 18 | `CvBarbarians.cpp:1538:98` | size_type (unsigned long long) -> int | `int iNumCandidates = /*5*/ range(GD_INT_GET(BARBARIAN_UNIT_SPAWN_NUM_CANDIDATES), 1, candidates.size());` | a | container size of game data or entities |
| 19 | `CvBeliefClasses.cpp:2031:27` | size_type (unsigned long long) -> int | `return m_paBeliefEntries.size();` | a | container size returned as `int`/`uint` (game-data or entity container) |
| 20 | `CvBeliefClasses.cpp:2135:27` | size_type (unsigned long long) -> int | `return m_ReligionBeliefs.size();` | a | container size returned as `int`/`uint` (game-data or entity container) |
| 21 | `CvBuilderTaskingAI.cpp:539:48` | size_type (unsigned long long) -> int | `int iNumSteps = aiMovingForwardCostsWithRoute.size();` | a | container size of game data or entities |
| 22 | `CvBuilderTaskingAI.cpp:1823:81` | size_type (unsigned long long) -> int | `iRailroadTotalMaintenance = m_plannedRoutePlots[plannedRouteRailroad].size() * iRailroadMaintenance;` | a | container size of game data or entities |
| 23 | `CvBuildingClasses.cpp:1162:43` | const size_t (const unsigned long long) -> int | `pResourceTypes->Bind(1, szBuildingType, lenBuildingType, false);` | a | string length (identifier, SQL text, log text) |
| 24 | `CvBuildingClasses.cpp:1195:37` | const size_t (const unsigned long long) -> int | `pResults->Bind(1, szBuildingType, lenBuildingType, false);` | a | string length (identifier, SQL text, log text) |
| 25 | `CvBuildingClasses.cpp:1951:43` | const size_t (const unsigned long long) -> int | `pResourceTypes->Bind(1, szBuildingType, lenBuildingType, false);` | a | string length (identifier, SQL text, log text) |
| 26 | `CvBuildingClasses.cpp:4479:31` | size_type (unsigned long long) -> uint (unsigned int) | `return m_viLocalResourceAnds.size();` | a | container size returned as `int`/`uint` (game-data or entity container) |
| 27 | `CvBuildingClasses.cpp:4490:30` | size_type (unsigned long long) -> uint (unsigned int) | `return m_viLocalResourceOrs.size();` | a | container size returned as `int`/`uint` (game-data or entity container) |
| 28 | `CvBuildingClasses.cpp:4515:34` | size_type (unsigned long long) -> uint (unsigned int) | `return m_viResourceMonopolyAnds.size();` | a | container size returned as `int`/`uint` (game-data or entity container) |
| 29 | `CvBuildingClasses.cpp:4526:33` | size_type (unsigned long long) -> uint (unsigned int) | `return m_viResourceMonopolyOrs.size();` | a | container size returned as `int`/`uint` (game-data or entity container) |
| 30 | `CvBuildingClasses.cpp:5290:29` | size_type (unsigned long long) -> int | `return m_paBuildingEntries.size();` | a | container size returned as `int`/`uint` (game-data or entity container) |
| 31 | `CvCity.cpp:6640:67` | size_type (unsigned long long) -> int | `int iGrab = GC.getGame().randRangeInclusive(0, GWIDs.size() - 1, CvSeeder::fromRaw(0xd3270c47).mix(GET_PLAY...` | b | `size() - 1` (or `- ui`): on an empty container wraps to `SIZE_MAX`, narrows to -1 on both builds; correct by modular wrap, an explicit `(int)` before the subtraction would say so |
| 32 | `CvCity.cpp:10397:78` | size_type (unsigned long long) -> uint (unsigned int) | `uint uVectorIndex = GC.getGame().urandLimitExclusive(veValidLuxuryResources.size(), plot()->GetPseudoRandom...` | a | RNG bound = container size; same value on both builds, so the same draw |
| 33 | `CvCity.cpp:13568:74` | size_type (unsigned long long) -> uint (unsigned int) | `uint uChoice = GC.getGame().urandLimitExclusive(vPossibleResources.size(), plot()->GetPseudoRandomSeed().mi...` | a | RNG bound = container size; same value on both builds, so the same draw |
| 34 | `CvCity.cpp:23327:57` | size_type (unsigned long long) -> int | `iTempYield += (GetYieldPerTile(eYield) * GetPlotList().size() * 100).Truncate();` | a | container size of game data or entities |
| 35 | `CvCity.cpp:28149:66` | size_type (unsigned long long) -> uint (unsigned int) | `uint uPickedIndex = GC.getGame().urandLimitExclusive(aiPlotList.size(), CvSeeder(getFoodTimes100()).mix(GET...` | a | RNG bound = container size; same value on both builds, so the same draw |
| 36 | `CvCity.cpp:31890:70` | size_type (unsigned long long) -> uint (unsigned int) | `uint uChoice = GC.getGame().urandLimitExclusive(vePossibleFranchise.size(), CvSeeder::fromRaw(0x8f321ace).m...` | a | RNG bound = container size; same value on both builds, so the same draw |
| 37 | `CvCityConnections.cpp:262:51` | size_type (unsigned long long) -> int | `return m_plotsWithIndustrialConnectionToCapital.size();` | a | container size returned as `int`/`uint` (game-data or entity container) |
| 38 | `CvCityConnections.cpp:264:40` | size_type (unsigned long long) -> int | `return m_plotsWithConnectionToCapital.size();` | a | container size returned as `int`/`uint` (game-data or entity container) |
| 39 | `CvCitySpecializationAI.cpp:141:39` | size_type (unsigned long long) -> int | `return m_paCitySpecializationEntries.size();` | a | container size returned as `int`/`uint` (game-data or entity container) |
| 40 | `CvCityStrategyAI.cpp:212:35` | size_type (unsigned long long) -> int | `return m_paAICityStrategyEntries.size();` | a | container size returned as `int`/`uint` (game-data or entity container) |
| 41 | `CvCityStrategyAI.cpp:3503:87` | size_type (unsigned long long) -> int | `iFlatYield += (pkBuildingInfo->GetYieldChangePerTile(eYield) * pCity->GetPlotList().size()).Truncate();` | a | container size of game data or entities |
| 42 | `CvContractClasses.cpp:202:29` | size_type (unsigned long long) -> int | `return m_paContractEntries.size();` | a | container size returned as `int`/`uint` (game-data or entity container) |
| 43 | `CvContractClasses.cpp:738:27` | size_type (unsigned long long) -> int | `return m_ActiveContracts.size();` | a | container size returned as `int`/`uint` (game-data or entity container) |
| 44 | `CvContractClasses.cpp:742:29` | size_type (unsigned long long) -> int | `return m_InactiveContracts.size();` | a | container size returned as `int`/`uint` (game-data or entity container) |
| 45 | `CvCorporationClasses.cpp:125:34` | size_type (unsigned long long) -> uint (unsigned int) | `return m_viResourceMonopolyAnds.size();` | a | container size returned as `int`/`uint` (game-data or entity container) |
| 46 | `CvCorporationClasses.cpp:136:33` | size_type (unsigned long long) -> uint (unsigned int) | `return m_viResourceMonopolyOrs.size();` | a | container size returned as `int`/`uint` (game-data or entity container) |
| 47 | `CvCorporationClasses.cpp:388:32` | size_type (unsigned long long) -> int | `return m_paCorporationEntries.size();` | a | container size returned as `int`/`uint` (game-data or entity container) |
| 48 | `CvCorporationClasses.cpp:1750:30` | size_type (unsigned long long) -> int | `return m_ActiveCorporations.size();` | a | container size returned as `int`/`uint` (game-data or entity container) |
| 49 | `CvCultureClasses.cpp:120:43` | size_type (unsigned long long) -> int | `int iNumGreatWorks = m_CurrentGreatWorks.size();` | a | container size of game data or entities |
| 50 | `CvDatabaseUtility.cpp:125:50` | unsigned long long -> unsigned int | `unsigned int iNumBytes = iCount1 * sizeof(int*) + iCount1 * iCount2 * sizeof(int);` | c | memory size of a 2D int table (includes `sizeof(int*)`, 8 on x64). Values are DB-row counts, far below 2^32, but it is an allocation size: **fixed**, `unsigned int` -> `size_t` (same type on x86) |
| 51 | `CvDealClasses.cpp:225:23` | size_type (unsigned long long) -> int | `return m_TradedItems.size();` | a | container size returned as `int`/`uint` (game-data or entity container) |
| 52 | `CvDealClasses.cpp:5422:46` | size_type (unsigned long long) -> int | `int start = latest ? m_ProposedDeals.size() - 1 : 0;` | b | `size() - 1` (or `- ui`): on an empty container wraps to `SIZE_MAX`, narrows to -1 on both builds; correct by modular wrap, an explicit `(int)` before the subtraction would say so |
| 53 | `CvDealClasses.cpp:5423:42` | size_type (unsigned long long) -> int | `int end = latest ? -1 : m_ProposedDeals.size();` | a | container size of game data or entities |
| 54 | `CvDealClasses.cpp:6374:41` | size_type (unsigned long long) -> int | `for (int i = m_HistoricalDeals.size() - 1; i >= 0; --i)` | b | `size() - 1` (or `- ui`): on an empty container wraps to `SIZE_MAX`, narrows to -1 on both builds; correct by modular wrap, an explicit `(int)` before the subtraction would say so |
| 55 | `CvDiplomacyAI.cpp:22378:61` | size_type (unsigned long long) -> int | `int iFriendLimit = iBaseFriendLimit * vValidPlayers.size() / 8;` | a | container size of game data or entities |
| 56 | `CvDiplomacyAI.cpp:41153:62` | size_type (unsigned long long) -> int | `int iIndex = GC.getGame().getAsyncRandNum(veValidGreetings.size(), "Diplomacy AI: Picking random Greeting f...` | a | RNG bound = container size; same value on both builds, so the same draw |
| 57 | `CvDiplomacyAI.cpp:41703:60` | size_type (unsigned long long) -> int | `int iIndex = GC.getGame().getAsyncRandNum(veValidInsults.size(), "Picking random insult for AI to give to h...` | a | RNG bound = container size; same value on both builds, so the same draw |
| 58 | `CvDllContext.cpp:1191:26` | size_type (unsigned long long) -> int | `int size = path.vPlots.size();` | a | path length returned across the EXE interface; small |
| 59 | `CvDllPreGame.cpp:212:37` | size_type (unsigned long long) -> uint (unsigned int) | `return CvPreGame::GetGameOptions().size();` | a | option count returned across the EXE interface; small |
| 60 | `CvDllPreGame.cpp:234:36` | size_type (unsigned long long) -> uint (unsigned int) | `return CvPreGame::GetMapOptions().size();` | a | option count returned across the EXE interface; small |
| 61 | `CvEconomicAI.cpp:240:31` | size_type (unsigned long long) -> int | `return m_paAIStrategyEntries.size();` | a | container size returned as `int`/`uint` (game-data or entity container) |
| 62 | `CvEmphasisClasses.cpp:88:29` | size_type (unsigned long long) -> int | `return m_paEmphasisEntries.size();` | a | container size returned as `int`/`uint` (game-data or entity container) |
| 63 | `CvEspionageClasses.cpp:106:36` | size_type (unsigned long long) -> int | `return m_paSpyPassiveBonusEntries.size();` | a | container size returned as `int`/`uint` (game-data or entity container); 1004 file, list only |
| 64 | `CvEspionageClasses.cpp:209:44` | size_type (unsigned long long) -> int | `return m_paSpyPassiveBonusDiplomatEntries.size();` | a | container size returned as `int`/`uint` (game-data or entity container); 1004 file, list only |
| 65 | `CvEspionageClasses.cpp:706:52` | size_type (unsigned long long) -> uint (unsigned int) | `MoveSpyTo(pOtherPlayerCapital, m_aSpyList.size() - 1, /*bAsDiplomat*/ true, /*bForce*/ true);` | b | `size() - 1` (or `- ui`): on an empty container wraps to `SIZE_MAX`, narrows to -1 on both builds; correct by modular wrap, an explicit `(int)` before the subtraction would say so; 1004 file, list only |
| 66 | `CvEspionageClasses.cpp:1393:91` | size_type (unsigned long long) -> uint (unsigned int) | `uint uGrab = GC.getGame().urandLimitExclusive(m_aaPlayerStealableTechList[eTargetPlayer].size(), CvSeeder::...` | a | RNG bound = container size; same value on both builds, so the same draw; 1004 file, list only |
| 67 | `CvEspionageClasses.cpp:2003:72` | size_type (unsigned long long) -> uint (unsigned int) | `uint uiTargetSlot = GC.getGame().urandLimitExclusive(aiMajorCivIndex.size(), pCity->plot()->GetPseudoRandom...` | a | RNG bound = container size; same value on both builds, so the same draw; 1004 file, list only |
| 68 | `CvEspionageClasses.cpp:2354:72` | size_type (unsigned long long) -> uint (unsigned int) | `uint uiTargetSlot = GC.getGame().urandLimitExclusive(aiMajorCivIndex.size(), pCity->plot()->GetPseudoRandom...` | a | RNG bound = container size; same value on both builds, so the same draw; 1004 file, list only |
| 69 | `CvEspionageClasses.cpp:2511:34` | size_type (unsigned long long) -> uint (unsigned int) | `uint uiSpyNameSize = vSpyNames.size();` | a | container size of game data or entities; 1004 file, list only |
| 70 | `CvEspionageClasses.cpp:4272:20` | size_type (unsigned long long) -> int | `return m_aSpyList.size();` | a | container size returned as `int`/`uint` (game-data or entity container); 1004 file, list only |
| 71 | `CvEspionageClasses.cpp:4453:46` | size_type (unsigned long long) -> int | `return m_aaPlayerStealableTechList[ePlayer].size();` | a | container size returned as `int`/`uint` (game-data or entity container); 1004 file, list only |
| 72 | `CvEventLog.cpp:102:56` | size_type (unsigned long long) -> int | `int iValue = (m_aEvents.size() - m_iEventsBeginIndex) + m_iEventsEndIndex;` | a | container size of game data or entities |
| 73 | `CvFlavorManager.cpp:274:60` | std::size_t (unsigned long long) -> int | `aiTempFlavors[iI] = pkNewLeaderHeadInfo->getFlavorValue(iI) - pkOldLeaderHeadInfo->getFlavorValue(iI);` | a | `size_t` loop index or count passed to an `int` parameter; bounded by game data |
| 74 | `CvFlavorManager.cpp:274:102` | std::size_t (unsigned long long) -> int | `aiTempFlavors[iI] = pkNewLeaderHeadInfo->getFlavorValue(iI) - pkOldLeaderHeadInfo->getFlavorValue(iI);` | a | `size_t` loop index or count passed to an `int` parameter; bounded by game data |
| 75 | `CvGame.cpp:5973:68` | size_t (unsigned long long) -> int | `if(!GC.GetGameDatabase()->Execute(*m_pDiploResponseQuery, szSQL, strlen(szSQL)))` | a | string length (identifier, SQL text, log text); 1004 file, list only |
| 76 | `CvGame.cpp:6000:63` | size_t (unsigned long long) -> int | `if (!GC.GetGameDatabase()->Execute(*tempDatabase, callTemp, strlen(callTemp)))` | a | string length (identifier, SQL text, log text); 1004 file, list only |
| 77 | `CvGame.cpp:9865:53` | size_type (unsigned long long) -> uint (unsigned int) | `uint uWinner = urandLimitExclusive(aaiGameWinners.size(), CvSeeder(aaiGameWinners.size()));` | a | RNG bound; `CvSeeder(T)` itself does `static_cast<uint>`, so the seed is the same on both builds; 1004 file, list only |
| 78 | `CvGame.cpp:10455:30` | size_type (unsigned long long) -> uint (unsigned int) | `return m_listReplayMessages.size();` | a | container size returned as `int`/`uint` (game-data or entity container); 1004 file, list only |
| 79 | `CvGame.cpp:13214:61` | size_type (unsigned long long) -> uint (unsigned int) | `uint uValue = GC.getGame().urandLimitExclusive(tempPlayers.size(), pPlot->GetPseudoRandomSeed());` | a | RNG bound = container size; same value on both builds, so the same draw; 1004 file, list only |
| 80 | `CvGame.cpp:13243:61` | size_type (unsigned long long) -> uint (unsigned int) | `uint uValue = GC.getGame().urandLimitExclusive(tempPlayers.size(), pPlot->GetPseudoRandomSeed());` | a | RNG bound = container size; same value on both builds, so the same draw; 1004 file, list only |
| 81 | `CvGame.cpp:13431:54` | size_type (unsigned long long) -> uint (unsigned int) | `uint uiIndex = urandLimitExclusive(vDeadSeaScrolls.size(), CvSeeder::fromRaw(0x5afa8766).mix(iNumExistingHi...` | a | RNG bound = container size; same value on both builds, so the same draw; 1004 file, list only |
| 82 | `CvGameCoreUtils.cpp:1564:14` | size_t (unsigned long long) -> int | `int iLen = strlen(szName);` | a | string length (identifier, SQL text, log text) |
| 83 | `CvGameCoreUtils.cpp:1677:16` | size_t (unsigned long long) -> int | `*iNumBools = strlen(szString);` | a | string length (identifier, SQL text, log text) |
| 84 | `CvGameCoreUtils.cpp:1947:18` | size_t (unsigned long long) -> UINT (unsigned int) | `UINT uiLength = strlen(pszGUID);` | a | string length (identifier, SQL text, log text) |
| 85 | `CvGameCoreUtils.cpp:1985:18` | size_t (unsigned long long) -> UINT (unsigned int) | `UINT uiLength = strlen(pszGUID);` | a | string length (identifier, SQL text, log text) |
| 86 | `CvGameCoreUtils.h:299:30` | size_t (unsigned long long) -> unsigned long | `size_t iJ = (rand.get(iNum - iI, "shuffle") + iI);` | a | `shuffleArray`: `size_t` bound passed to `CvRandom::get(unsigned long)` (32-bit on both, LLP64); array lengths are small, so the same RNG draw on both builds |
| 87 | `CvGlobals.cpp:2973:38` | size_t (unsigned long long) -> DWORD (unsigned long) | `WriteFile(hCrashlogs, szCrashInfo, strlen(szCrashInfo), NULL, NULL);` | a | string length (identifier, SQL text, log text); 1002 file, list only |
| 88 | `CvGlobals.cpp:2976:39` | size_t (unsigned long long) -> DWORD (unsigned long) | `WriteFile(hCrashlogs, szStackTrace, strlen(szStackTrace), NULL, NULL);` | a | string length (identifier, SQL text, log text); 1002 file, list only |
| 89 | `CvGlobals.cpp:7735:31` | size_type (unsigned long long) -> int | `return m_paPlayerOptionInfos.size();` | a | container size returned as `int`/`uint` (game-data or entity container); 1002 file, list only |
| 90 | `CvGrandStrategyAI.cpp:99:36` | size_type (unsigned long long) -> int | `return m_paAIGrandStrategyEntries.size();` | a | container size returned as `int`/`uint` (game-data or entity container) |
| 91 | `CvHomelandAI.cpp:6042:32` | size_type (unsigned long long) -> int | `int iMaxCaravans = vpCaravans.size();` | a | container size of game data or entities; epic 1300 file, list only |
| 92 | `CvHomelandAI.cpp:6043:36` | size_type (unsigned long long) -> int | `int iMaxCargoShips = vpCargoShips.size();` | a | container size of game data or entities; epic 1300 file, list only |
| 93 | `CvHomelandAI.cpp:6358:62` | size_type (unsigned long long) -> int | `int iPossibleSites = (m_TargetedAntiquitySites.size() / 5) + 1;` | a | container size of game data or entities; epic 1300 file, list only |
| 94 | `CvImprovementClasses.cpp:486:46` | const size_t (const unsigned long long) -> int | `pResourceTypes->Bind(1, szImprovementType, lenImprovementType, false);` | a | string length (identifier, SQL text, log text) |
| 95 | `CvImprovementClasses.cpp:502:46` | const size_t (const unsigned long long) -> int | `pYieldResults->Bind(1, szImprovementType, lenImprovementType, false);` | a | string length (identifier, SQL text, log text) |
| 96 | `CvImprovementClasses.cpp:598:40` | const size_t (const unsigned long long) -> int | `pResults->Bind(1, szImprovementType, lenImprovementType, false);` | a | string length (identifier, SQL text, log text) |
| 97 | `CvImprovementClasses.cpp:628:40` | const size_t (const unsigned long long) -> int | `pResults->Bind(1, szImprovementType, lenImprovementType, false);` | a | string length (identifier, SQL text, log text) |
| 98 | `CvImprovementClasses.cpp:659:40` | const size_t (const unsigned long long) -> int | `pResults->Bind(1, szImprovementType, lenImprovementType, false);` | a | string length (identifier, SQL text, log text) |
| 99 | `CvImprovementClasses.cpp:690:40` | const size_t (const unsigned long long) -> int | `pResults->Bind(1, szImprovementType, lenImprovementType, false);` | a | string length (identifier, SQL text, log text) |
| 100 | `CvImprovementClasses.cpp:722:40` | const size_t (const unsigned long long) -> int | `pResults->Bind(1, szImprovementType, lenImprovementType, false);` | a | string length (identifier, SQL text, log text) |
| 101 | `CvImprovementClasses.cpp:749:40` | const size_t (const unsigned long long) -> int | `pResults->Bind(1, szImprovementType, lenImprovementType, false);` | a | string length (identifier, SQL text, log text) |
| 102 | `CvImprovementClasses.cpp:779:40` | const size_t (const unsigned long long) -> int | `pResults->Bind(1, szImprovementType, lenImprovementType, false);` | a | string length (identifier, SQL text, log text) |
| 103 | `CvImprovementClasses.cpp:810:40` | const size_t (const unsigned long long) -> int | `pResults->Bind(1, szImprovementType, lenImprovementType, false);` | a | string length (identifier, SQL text, log text) |
| 104 | `CvImprovementClasses.cpp:841:40` | const size_t (const unsigned long long) -> int | `pResults->Bind(1, szImprovementType, lenImprovementType, false);` | a | string length (identifier, SQL text, log text) |
| 105 | `CvImprovementClasses.cpp:1845:32` | size_type (unsigned long long) -> int | `return m_paImprovementEntries.size();` | a | container size returned as `int`/`uint` (game-data or entity container) |
| 106 | `CvInfos.cpp:2116:22` | size_type (unsigned long long) -> int | `return m_vCityNames.size();` | a | container size returned as `int`/`uint` (game-data or entity container) |
| 107 | `CvInfos.cpp:2121:21` | size_type (unsigned long long) -> int | `return m_vSpyNames.size();` | a | container size returned as `int`/`uint` (game-data or entity container) |
| 108 | `CvInfos.cpp:4587:37` | size_t (unsigned long long) -> int | `pResults->Bind(1, szHandicapType, strlen(szHandicapType), false);` | a | string length (identifier, SQL text, log text) |
| 109 | `CvInfos.cpp:4631:37` | size_t (unsigned long long) -> int | `pResults->Bind(1, szHandicapType, strlen(szHandicapType), false);` | a | string length (identifier, SQL text, log text) |
| 110 | `CvInfos.cpp:7173:36` | size_t (unsigned long long) -> int | `pResults->Bind(1, szFeatureType, strlen(szFeatureType), false);` | a | string length (identifier, SQL text, log text) |
| 111 | `CvInfos.cpp:7614:36` | size_t (unsigned long long) -> int | `pResults->Bind(1, szTerrainType, strlen(szTerrainType), false);` | a | string length (identifier, SQL text, log text) |
| 112 | `CvInfos.cpp:8720:32` | size_type (unsigned long long) -> int | `return m_vctAnimationPathType.size();` | a | container size returned as `int`/`uint` (game-data or entity container) |
| 113 | `CvInfos.cpp:8992:19` | size_type (unsigned long long) -> int | `return m_vEraVOs.size();` | a | container size returned as `int`/`uint` (game-data or entity container) |
| 114 | `CvInfos.cpp:9724:37` | const size_t (const unsigned long long) -> int | `pEventTypes->Bind(1, szEventType, lenEventType, false);` | a | string length (identifier, SQL text, log text) |
| 115 | `CvInfos.cpp:10448:43` | const size_t (const unsigned long long) -> int | `pEventChoiceTypes->Bind(1, szEventType, lenEventChoiceType, false);` | a | string length (identifier, SQL text, log text) |
| 116 | `CvInfos.cpp:10480:43` | const size_t (const unsigned long long) -> int | `pEventChoiceTypes->Bind(1, szEventType, lenEventChoiceType, false);` | a | string length (identifier, SQL text, log text) |
| 117 | `CvInfos.cpp:11267:41` | const size_t (const unsigned long long) -> int | `pCityEventTypes->Bind(1, szEventType, lenCityEventChoiceType, false);` | a | string length (identifier, SQL text, log text) |
| 118 | `CvInfos.cpp:12339:47` | const size_t (const unsigned long long) -> int | `pCityEventChoiceTypes->Bind(1, szEventType, lenCityEventChoiceType, false);` | a | string length (identifier, SQL text, log text) |
| 119 | `CvInfos.cpp:12372:47` | const size_t (const unsigned long long) -> int | `pCityEventChoiceTypes->Bind(1, szEventType, lenCityEventChoiceType, false);` | a | string length (identifier, SQL text, log text) |
| 120 | `CvMapGenerator.cpp:120:38` | lua_Integer (long long) -> int | `pArgs->pkMapInitData->m_iGridW = lua_tointeger(L, -1);` | a | L1 |
| 121 | `CvMapGenerator.cpp:125:38` | lua_Integer (long long) -> int | `pArgs->pkMapInitData->m_iGridH = lua_tointeger(L, -1);` | a | L1 |
| 122 | `CvMapGenerator.cpp:139:44` | lua_Integer (long long) -> int | `pArgs->pkMapInitData->m_iTopLatitude = lua_tointeger(L, -1);` | a | L1 |
| 123 | `CvMapGenerator.cpp:143:47` | lua_Integer (long long) -> int | `pArgs->pkMapInitData->m_iBottomLatitude = lua_tointeger(L, -1);` | a | L1 |
| 124 | `CvMilitaryAI.cpp:230:31` | size_type (unsigned long long) -> int | `return m_paAIStrategyEntries.size();` | a | container size returned as `int`/`uint` (game-data or entity container) |
| 125 | `CvMinorCivAI.cpp:1705:58` | size_type (unsigned long long) -> uint (unsigned int) | `uint iNumQuests = pMinorAI->m_QuestsGiven[eLoopPlayer].size();` | a | container size of game data or entities |
| 126 | `CvMinorCivAI.cpp:4832:69` | size_type (unsigned long long) -> uint (unsigned int) | `uint uRand = GC.getGame().urandLimitExclusive(vValidPersonalities.size(), CvSeeder::fromRaw(0xdf912135).mix...` | a | RNG bound = container size; same value on both builds, so the same draw |
| 127 | `CvMinorCivAI.cpp:5784:68` | size_type (unsigned long long) -> uint (unsigned int) | `uint uRoll = GC.getGame().urandLimitExclusive(veUniqueLuxuries.size(), pCityPlot->GetPseudoRandomSeed()); /...` | a | RNG bound = container size; same value on both builds, so the same draw |
| 128 | `CvMinorCivAI.cpp:7445:25` | size_type (unsigned long long) -> int | `return setGlobalQuests.size();` | a | container size returned as `int`/`uint` (game-data or entity container) |
| 129 | `CvMinorCivAI.cpp:7461:32` | size_type (unsigned long long) -> int | `return m_QuestsGiven[ePlayer].size();` | a | container size returned as `int`/`uint` (game-data or entity container) |
| 130 | `CvMinorCivAI.cpp:8469:70` | size_type (unsigned long long) -> uint (unsigned int) | `uint uRandIndex = GC.getGame().urandLimitExclusive(veValidResources.size(), CvSeeder::fromRaw(0xc74fb6bb).m...` | a | RNG bound = container size; same value on both builds, so the same draw |
| 131 | `CvMinorCivAI.cpp:8633:70` | size_type (unsigned long long) -> uint (unsigned int) | `uint uRandIndex = GC.getGame().urandLimitExclusive(veValidBuildings.size(), CvSeeder::fromRaw(0x2a8e7e4a).m...` | a | RNG bound = container size; same value on both builds, so the same draw |
| 132 | `CvMinorCivAI.cpp:8739:70` | size_type (unsigned long long) -> uint (unsigned int) | `uint uRandIndex = GC.getGame().urandLimitExclusive(veValidBuildings.size(), CvSeeder::fromRaw(0xaf9ed611).m...` | a | RNG bound = container size; same value on both builds, so the same draw |
| 133 | `CvMinorCivAI.cpp:8838:68` | size_type (unsigned long long) -> uint (unsigned int) | `uint uRandIndex = GC.getGame().urandLimitExclusive(veValidTargets.size(), CvSeeder::fromRaw(0x6dd4e9e9).mix...` | a | RNG bound = container size; same value on both builds, so the same draw |
| 134 | `CvMinorCivAI.cpp:8933:66` | size_type (unsigned long long) -> uint (unsigned int) | `uint uRandIndex = GC.getGame().urandLimitExclusive(veValidUnits.size(), CvSeeder::fromRaw(0x9d8964da).mix(m...` | a | RNG bound = container size; same value on both builds, so the same draw |
| 135 | `CvMinorCivAI.cpp:9298:70` | size_type (unsigned long long) -> uint (unsigned int) | `uint uRandIndex = GC.getGame().urandLimitExclusive(veValidBuildings.size(), CvSeeder::fromRaw(0x371c04bb).m...` | a | RNG bound = container size; same value on both builds, so the same draw |
| 136 | `CvMinorCivAI.cpp:9607:71` | size_type (unsigned long long) -> uint (unsigned int) | `uint uRandIndex = GC.getGame().urandLimitExclusive(aValidSpyMissions.size(), CvSeeder::fromRaw(0x4f472d67)....` | a | RNG bound = container size; same value on both builds, so the same draw |
| 137 | `CvMinorCivAI.cpp:10153:68` | size_type (unsigned long long) -> uint (unsigned int) | `uint uRandIndex = GC.getGame().urandLimitExclusive(veValidTargets.size(), CvSeeder::fromRaw(0x31b18457).mix...` | a | RNG bound = container size; same value on both builds, so the same draw |
| 138 | `CvMinorCivAI.cpp:10288:68` | size_type (unsigned long long) -> uint (unsigned int) | `uint uRandIndex = GC.getGame().urandLimitExclusive(veValidTargets.size(), CvSeeder::fromRaw(0x695bf7f6).mix...` | a | RNG bound = container size; same value on both builds, so the same draw |
| 139 | `CvMinorCivAI.cpp:17072:22` | size_type (unsigned long long) -> int | `return m_vCityNames.size();` | a | container size returned as `int`/`uint` (game-data or entity container) |
| 140 | `CvNotificationClasses.cpp:64:33` | size_type (unsigned long long) -> int | `return m_paNotificationEntries.size();` | a | container size returned as `int`/`uint` (game-data or entity container) |
| 141 | `CvPlayer.cpp:2765:63` | size_type (unsigned long long) -> uint (unsigned int) | `uint uRandIndex = GC.getGame().urandLimitExclusive(vPlots.size(), CvSeeder::fromRaw(0xf302652d).mix(GetID()));` | a | RNG bound = container size; same value on both builds, so the same draw |
| 142 | `CvPlayer.cpp:2867:73` | size_type (unsigned long long) -> uint (unsigned int) | `uint uIndex = GC.getGame().urandLimitExclusive(vPotentialSpawnPlots.size(), CvSeeder::fromRaw(0x6ca5d27a).m...` | a | RNG bound = container size; same value on both builds, so the same draw |
| 143 | `CvPlayer.cpp:4866:65` | size_type (unsigned long long) -> uint (unsigned int) | `uint iRand = GC.getGame().urandLimitExclusive(availableNames.size(), CvSeeder(GetID()).mix(GetNumCitiesFoun...` | a | RNG bound = container size; same value on both builds, so the same draw |
| 144 | `CvPlayer.cpp:13356:66` | size_type (unsigned long long) -> uint (unsigned int) | `uint uRand = GC.getGame().urandLimitExclusive(avValidGoodies.size(), CvSeeder::fromRaw(0x8d9dbeaa).mix(pPlo...` | a | RNG bound = container size; same value on both builds, so the same draw |
| 145 | `CvPlayer.cpp:13791:79` | size_type (unsigned long long) -> uint (unsigned int) | `uint uiIndex = GC.getGame().urandLimitExclusive(vPrioritizedArchaeologyTiles.size(), seedOne);` | a | RNG bound = container size; same value on both builds, so the same draw |
| 146 | `CvPlayer.cpp:13811:65` | size_type (unsigned long long) -> uint (unsigned int) | `uint uiIndex = GC.getGame().urandLimitExclusive(vResourceTiles.size(), seedOne);` | a | RNG bound = container size; same value on both builds, so the same draw |
| 147 | `CvPlayer.cpp:15667:52` | size_t (unsigned long long) -> int | `iMultiplier += pUnitEntry->GetProductionTraits(iI);` | a | `size_t` loop index or count passed to an `int` parameter; bounded by game data |
| 148 | `CvPlayer.cpp:15674:57` | size_t (unsigned long long) -> int | `iTempMod = pkSpecialUnitInfo->getProductionTraits(iI);` | a | `size_t` loop index or count passed to an `int` parameter; bounded by game data |
| 149 | `CvPlayer.cpp:18503:65` | size_type (unsigned long long) -> uint (unsigned int) | `uint uRoll = GC.getGame().urandLimitExclusive(vePossibleTechs.size(), GET_PLAYER(pConqueredCity->getOwner()...` | a | RNG bound = container size; same value on both builds, so the same draw |
| 150 | `CvPlayer.cpp:31173:73` | size_type (unsigned long long) -> uint (unsigned int) | `uint uChoice = GC.getGame().urandLimitExclusive(vPossibleSpecialists.size(), GetPseudoRandomSeed().mix(GC.g...` | a | RNG bound = container size; same value on both builds, so the same draw |
| 151 | `CvPlayer.cpp:38872:37` | size_type (unsigned long long) -> int | `return m_vResourcesWGlobalMonopoly.size();` | a | container size returned as `int`/`uint` (game-data or entity container) |
| 152 | `CvPlayer.cpp:42200:110` | size_t (unsigned long long) -> int | `if (!pNewOperation->PreconditionsAreMet(pMuster ? pMuster->plot() : NULL, pTarget ? pTarget->plot() : NULL,...` | a | `size_t` loop index or count passed to an `int` parameter; bounded by game data |
| 153 | `CvPlayer.cpp:42537:36` | size_type (unsigned long long) -> int | `for (int i = viYieldHistory.size()-1; i >= 0; i--)` | b | `size() - 1` (or `- ui`): on an empty container wraps to `SIZE_MAX`, narrows to -1 on both builds; correct by modular wrap, an explicit `(int)` before the subtraction would say so |
| 154 | `CvPlayer.cpp:45842:43` | size_type (unsigned long long) -> uint32 (unsigned int) | `operationCount = player.m_AIOperations.size();` | c | reaches the save stream: count of AI operations, declared `uint32`, so 4 bytes on both builds; the narrowing *is* the width pin (issue 1004 census silent). No change |
| 155 | `CvPlayer.cpp:46606:19` | size_type (unsigned long long) -> int | `return m_aiPlots.size();` | a | container size returned as `int`/`uint` (game-data or entity container) |
| 156 | `CvPlayer.cpp:49758:32` | size_type (unsigned long long) -> int | `int iSetSize = currentUnits.size();` | a | container size of game data or entities |
| 157 | `CvPlayerAI.cpp:2309:47` | size_type (unsigned long long) -> int | `int iNumCitiesToConsider = vCitiesToConsider.size();` | a | container size of game data or entities |
| 158 | `CvPlot.cpp:8681:76` | size_type (unsigned long long) -> uint (unsigned int) | `uint uChoice = GC.getGame().urandLimitExclusive(vPossibleResources.size(), GetPseudoRandomSeed().mix(GC.get...` | a | RNG bound = container size; same value on both builds, so the same draw |
| 159 | `CvPlot.cpp:9047:66` | size_type (unsigned long long) -> uint (unsigned int) | `int iRandomIndex = GC.getGame().urandLimitExclusive(aSpawnPlots.size(), GET_PLAYER(ePlayer).GetPseudoRandom...` | a | RNG bound = container size; same value on both builds, so the same draw |
| 160 | `CvPlotManager.cpp:363:20` | size_type (unsigned long long) -> int | `return pkUnits->size();` | a | container size returned as `int`/`uint` (game-data or entity container) |
| 161 | `CvPolicyAI.cpp:597:27` | size_t (unsigned long long) -> int | `iBestBranchIdx = i;` | a | `size_t` loop index or count passed to an `int` parameter; bounded by game data |
| 162 | `CvPolicyClasses.cpp:3946:27` | size_type (unsigned long long) -> int | `return m_paPolicyEntries.size();` | a | container size returned as `int`/`uint` (game-data or entity container) |
| 163 | `CvPolicyClasses.cpp:3975:33` | size_type (unsigned long long) -> int | `return m_paPolicyBranchEntries.size();` | a | container size returned as `int`/`uint` (game-data or entity container) |
| 164 | `CvPreGame.cpp:493:19` | const unsigned long long -> int | `int maxPlayers = std::min(slotStatus.size(), slotClaims.size());` | a | container size of game data or entities |
| 165 | `CvPreGame.cpp:1028:70` | size_type (unsigned long long) -> mapped_type (unsigned int) | `s_GameOptionsHash[FStringHash(szOptionName)] = s_GameOptions.size() - 1;` | a | option index stored in a lookup map; small |
| 166 | `CvPreGame.cpp:1052:55` | size_t (unsigned long long) -> mapped_type (unsigned int) | `s_GameOptionsHash[FStringHash(kOption.GetName())] = i;` | a | option index stored in a lookup map; small |
| 167 | `CvPreGame.cpp:1252:17` | size_t (unsigned long long) -> int | `if(mapScript[i] == '.')` | a | `size_t` loop index or count passed to an `int` parameter; bounded by game data |
| 168 | `CvPreGame.cpp:1903:55` | size_t (unsigned long long) -> mapped_type (unsigned int) | `s_GameOptionsHash[FStringHash(kOption.GetName())] = i;` | a | option index stored in a lookup map; small |
| 169 | `CvPreGame.cpp:2004:55` | size_t (unsigned long long) -> mapped_type (unsigned int) | `s_GameOptionsHash[FStringHash(kOption.GetName())] = i;` | a | option index stored in a lookup map; small |
| 170 | `CvPreGame.cpp:2562:72` | size_type (unsigned long long) -> uint (unsigned int) | `uint uRand = GC.getGame().urandLimitExclusive(vHumanCivsNotChosen.size(), CvSeeder::fromRaw(0x3153de36).mix...` | a | RNG bound = container size; same value on both builds, so the same draw |
| 171 | `CvPreGame.cpp:2570:63` | size_type (unsigned long long) -> uint (unsigned int) | `uint uRand = GC.getGame().urandLimitExclusive(vHumanCivs.size(), CvSeeder::fromRaw(0x298ac75d).mix(iCounter));` | a | RNG bound = container size; same value on both builds, so the same draw |
| 172 | `CvPreGame.cpp:2580:69` | size_type (unsigned long long) -> uint (unsigned int) | `uint uRand = GC.getGame().urandLimitExclusive(vAICivsNotChosen.size(), CvSeeder::fromRaw(0x9f2b8561).mix(iC...` | a | RNG bound = container size; same value on both builds, so the same draw |
| 173 | `CvPreGame.cpp:2588:60` | size_type (unsigned long long) -> uint (unsigned int) | `uint uRand = GC.getGame().urandLimitExclusive(vAICivs.size(), CvSeeder::fromRaw(0x52094553).mix(iCounter));` | a | RNG bound = container size; same value on both builds, so the same draw |
| 174 | `CvPreGame.cpp:2657:72` | size_type (unsigned long long) -> uint (unsigned int) | `uint uRand = GC.getGame().urandLimitExclusive(vAvailableCityStates.size(), CvSeeder::fromRaw(0xd73596c3).mi...` | a | RNG bound = container size; same value on both builds, so the same draw |
| 175 | `CvPreGame.cpp:2772:68` | size_type (unsigned long long) -> uint (unsigned int) | `uint uRand = GC.getGame().urandLimitExclusive(vValidCategories.size(), CvSeeder::fromRaw(0x95c6b165).mix(iC...` | a | RNG bound = container size; same value on both builds, so the same draw |
| 176 | `CvPreGame.cpp:2781:57` | size_type (unsigned long long) -> uint (unsigned int) | `uRand = GC.getGame().urandLimitExclusive(vCultured.size(), CvSeeder::fromRaw(0x5ac0d892).mix(iCounter));` | a | RNG bound = container size; same value on both builds, so the same draw |
| 177 | `CvPreGame.cpp:2789:61` | size_type (unsigned long long) -> uint (unsigned int) | `uRand = GC.getGame().urandLimitExclusive(vMilitaristic.size(), CvSeeder::fromRaw(0x7fde469e).mix(iCounter));` | a | RNG bound = container size; same value on both builds, so the same draw |
| 178 | `CvPreGame.cpp:2797:57` | size_type (unsigned long long) -> uint (unsigned int) | `uRand = GC.getGame().urandLimitExclusive(vMaritime.size(), CvSeeder::fromRaw(0x9a763898).mix(iCounter));` | a | RNG bound = container size; same value on both builds, so the same draw |
| 179 | `CvPreGame.cpp:2805:59` | size_type (unsigned long long) -> uint (unsigned int) | `uRand = GC.getGame().urandLimitExclusive(vMercantile.size(), CvSeeder::fromRaw(0xef6c2156).mix(iCounter));` | a | RNG bound = container size; same value on both builds, so the same draw |
| 180 | `CvPreGame.cpp:2813:58` | size_type (unsigned long long) -> uint (unsigned int) | `uRand = GC.getGame().urandLimitExclusive(vReligious.size(), CvSeeder::fromRaw(0x0d3e0560).mix(iCounter));` | a | RNG bound = container size; same value on both builds, so the same draw |
| 181 | `CvPreGame.cpp:3484:30` | size_type (unsigned long long) -> int | `kQuery.Bind(1, w.c_str(), w.size(), false);` | a | string length (identifier, SQL text, log text) |
| 182 | `CvPreGame.cpp:3522:16` | size_t (unsigned long long) -> int | `*iNumBools = strlen(szString);` | a | string length (identifier, SQL text, log text) |
| 183 | `CvPreGame.cpp:3731:22` | size_type (unsigned long long) -> uint (unsigned int) | `return s_AllowedDLC.size();` | a | container size returned as `int`/`uint` (game-data or entity container) |
| 184 | `CvProjectClasses.cpp:555:28` | size_type (unsigned long long) -> int | `return m_paProjectEntries.size();` | a | container size returned as `int`/`uint` (game-data or entity container) |
| 185 | `CvPromotionClasses.cpp:3654:30` | size_type (unsigned long long) -> int | `return m_paPromotionEntries.size();` | a | container size returned as `int`/`uint` (game-data or entity container) |
| 186 | `CvPromotionClasses.cpp:3974:41` | size_type (unsigned long long) -> uint (unsigned int) | `uint uNumChoices = aPossiblePromotions.size();` | a | container size of game data or entities |
| 187 | `CvReligionClasses.cpp:147:29` | size_type (unsigned long long) -> int | `return m_paReligionEntries.size();` | a | container size returned as `int`/`uint` (game-data or entity container) |
| 188 | `CvReligionClasses.cpp:1086:64` | size_type (unsigned long long) -> uint (unsigned int) | `index = GC.getGame().urandLimitExclusive(availableReligions.size(), CvSeeder(ePlayer));` | a | RNG bound = container size; same value on both builds, so the same draw |
| 189 | `CvReligionClasses.cpp:1960:34` | size_t (unsigned long long) -> int | `m_religionIndex[eReligion] = iI;` | a | `size_t` loop index or count passed to an `int` parameter; bounded by game data |
| 190 | `CvReligionClasses.cpp:11438:62` | size_type (unsigned long long) -> uint (unsigned int) | `return choices[GC.getGame().urandLimitExclusive(choices.size(), CvSeeder(pCity->plot()->GetPseudoRandomSeed...` | a | RNG bound = container size; same value on both builds, so the same draw |
| 191 | `CvReligionClasses.cpp:11441:61` | size_type (unsigned long long) -> uint (unsigned int) | `return choices[GC.getGame().urandLimitExclusive(choices.size(), CvSeeder(pCity->plot()->GetPseudoRandomSeed...` | a | RNG bound = container size; same value on both builds, so the same draw |
| 192 | `CvReplayInfo.cpp:161:38` | size_type (unsigned long long) -> unsigned int | `uiDataSet = m_dataSetMap.size() - 1;` | b | `size() - 1` (or `- ui`): on an empty container wraps to `SIZE_MAX`, narrows to -1 on both builds; correct by modular wrap, an explicit `(int)` before the subtraction would say so |
| 193 | `CvReplayInfo.cpp:397:30` | size_type (unsigned long long) -> uint (unsigned int) | `return m_listReplayMessages.size();` | a | container size returned as `int`/`uint` (game-data or entity container) |
| 194 | `CvReplayInfo.cpp:437:22` | size_type (unsigned long long) -> unsigned int | `return m_dataSetMap.size();` | a | container size returned as `int`/`uint` (game-data or entity container) |
| 195 | `CvReplayMessage.cpp:119:17` | size_type (unsigned long long) -> unsigned int | `return m_Plots.size();` | a | container size returned as `int`/`uint` (game-data or entity container) |
| 196 | `CvTacticalAI.cpp:1722:33` | _Iter_diff_t<...> (long long) -> int | `int iNumPlotNumAlreadySet = std::count(checkedPlotList.begin(), checkedPlotList.end(), pUnitPlot);` | b | `std::count` returns `ptrdiff_t`; count of plots in a vector; epic 1300 file, list only |
| 197 | `CvTacticalAI.cpp:2745:42` | size_type (unsigned long long) -> int | `it->SetAuxIntData(it->GetAuxIntData() + myUnits.size());` | a | container size of game data or entities; epic 1300 file, list only |
| 198 | `CvTacticalAI.cpp:7032:35` | size_t (unsigned long long) -> int | `iRefScore += ref->GetAssignment(i).Score();` | a | `size_t` loop index or count passed to an `int` parameter; bounded by game data; epic 1300 file, list only |
| 199 | `CvTacticalAI.cpp:7033:39` | size_t (unsigned long long) -> int | `iOtherScore += other->GetAssignment(i).Score();` | a | `size_t` loop index or count passed to an `int` parameter; bounded by game data; epic 1300 file, list only |
| 200 | `CvTacticalAI.cpp:7049:26` | size_t (unsigned long long) -> int | `if (ref->GetAssignment(i) == other->GetAssignment(i))` | a | `size_t` loop index or count passed to an `int` parameter; bounded by game data; epic 1300 file, list only |
| 201 | `CvTacticalAI.cpp:7049:53` | size_t (unsigned long long) -> int | `if (ref->GetAssignment(i) == other->GetAssignment(i))` | a | `size_t` loop index or count passed to an `int` parameter; bounded by game data; epic 1300 file, list only |
| 202 | `CvTacticalAI.cpp:7068:28` | size_t (unsigned long long) -> int | `if (ref->GetAssignment(A) == other->GetAssignment(B) && ref->GetAssignment(B) == other->GetAssignment(A))` | a | `size_t` loop index or count passed to an `int` parameter; bounded by game data; epic 1300 file, list only |
| 203 | `CvTacticalAI.cpp:7068:55` | size_t (unsigned long long) -> int | `if (ref->GetAssignment(A) == other->GetAssignment(B) && ref->GetAssignment(B) == other->GetAssignment(A))` | a | `size_t` loop index or count passed to an `int` parameter; bounded by game data; epic 1300 file, list only |
| 204 | `CvTacticalAI.cpp:7068:80` | size_t (unsigned long long) -> int | `if (ref->GetAssignment(A) == other->GetAssignment(B) && ref->GetAssignment(B) == other->GetAssignment(A))` | a | `size_t` loop index or count passed to an `int` parameter; bounded by game data; epic 1300 file, list only |
| 205 | `CvTacticalAI.cpp:7068:107` | size_t (unsigned long long) -> int | `if (ref->GetAssignment(A) == other->GetAssignment(B) && ref->GetAssignment(B) == other->GetAssignment(A))` | a | `size_t` loop index or count passed to an `int` parameter; bounded by game data; epic 1300 file, list only |
| 206 | `CvTacticalAI.cpp:7083:36` | size_t (unsigned long long) -> int | `bool CAB = ref->GetAssignment(A) == other->GetAssignment(C) &&` | a | `size_t` loop index or count passed to an `int` parameter; bounded by game data; epic 1300 file, list only |
| 207 | `CvTacticalAI.cpp:7083:63` | size_t (unsigned long long) -> int | `bool CAB = ref->GetAssignment(A) == other->GetAssignment(C) &&` | a | `size_t` loop index or count passed to an `int` parameter; bounded by game data; epic 1300 file, list only |
| 208 | `CvTacticalAI.cpp:7084:26` | size_t (unsigned long long) -> int | `ref->GetAssignment(B) == other->GetAssignment(A) &&` | a | `size_t` loop index or count passed to an `int` parameter; bounded by game data; epic 1300 file, list only |
| 209 | `CvTacticalAI.cpp:7084:53` | size_t (unsigned long long) -> int | `ref->GetAssignment(B) == other->GetAssignment(A) &&` | a | `size_t` loop index or count passed to an `int` parameter; bounded by game data; epic 1300 file, list only |
| 210 | `CvTacticalAI.cpp:7085:26` | size_t (unsigned long long) -> int | `ref->GetAssignment(C) == other->GetAssignment(B);` | a | `size_t` loop index or count passed to an `int` parameter; bounded by game data; epic 1300 file, list only |
| 211 | `CvTacticalAI.cpp:7085:53` | size_t (unsigned long long) -> int | `ref->GetAssignment(C) == other->GetAssignment(B);` | a | `size_t` loop index or count passed to an `int` parameter; bounded by game data; epic 1300 file, list only |
| 212 | `CvTacticalAI.cpp:7086:36` | size_t (unsigned long long) -> int | `bool BCA = ref->GetAssignment(A) == other->GetAssignment(B) &&` | a | `size_t` loop index or count passed to an `int` parameter; bounded by game data; epic 1300 file, list only |
| 213 | `CvTacticalAI.cpp:7086:63` | size_t (unsigned long long) -> int | `bool BCA = ref->GetAssignment(A) == other->GetAssignment(B) &&` | a | `size_t` loop index or count passed to an `int` parameter; bounded by game data; epic 1300 file, list only |
| 214 | `CvTacticalAI.cpp:7087:26` | size_t (unsigned long long) -> int | `ref->GetAssignment(B) == other->GetAssignment(C) &&` | a | `size_t` loop index or count passed to an `int` parameter; bounded by game data; epic 1300 file, list only |
| 215 | `CvTacticalAI.cpp:7087:53` | size_t (unsigned long long) -> int | `ref->GetAssignment(B) == other->GetAssignment(C) &&` | a | `size_t` loop index or count passed to an `int` parameter; bounded by game data; epic 1300 file, list only |
| 216 | `CvTacticalAI.cpp:7088:26` | size_t (unsigned long long) -> int | `ref->GetAssignment(C) == other->GetAssignment(A);` | a | `size_t` loop index or count passed to an `int` parameter; bounded by game data; epic 1300 file, list only |
| 217 | `CvTacticalAI.cpp:7088:53` | size_t (unsigned long long) -> int | `ref->GetAssignment(C) == other->GetAssignment(A);` | a | `size_t` loop index or count passed to an `int` parameter; bounded by game data; epic 1300 file, list only |
| 218 | `CvTacticalAI.cpp:7104:37` | size_t (unsigned long long) -> int | `bool DABC = ref->GetAssignment(A) == other->GetAssignment(D) &&` | a | `size_t` loop index or count passed to an `int` parameter; bounded by game data; epic 1300 file, list only |
| 219 | `CvTacticalAI.cpp:7104:64` | size_t (unsigned long long) -> int | `bool DABC = ref->GetAssignment(A) == other->GetAssignment(D) &&` | a | `size_t` loop index or count passed to an `int` parameter; bounded by game data; epic 1300 file, list only |
| 220 | `CvTacticalAI.cpp:7105:26` | size_t (unsigned long long) -> int | `ref->GetAssignment(B) == other->GetAssignment(A) &&` | a | `size_t` loop index or count passed to an `int` parameter; bounded by game data; epic 1300 file, list only |
| 221 | `CvTacticalAI.cpp:7105:53` | size_t (unsigned long long) -> int | `ref->GetAssignment(B) == other->GetAssignment(A) &&` | a | `size_t` loop index or count passed to an `int` parameter; bounded by game data; epic 1300 file, list only |
| 222 | `CvTacticalAI.cpp:7106:26` | size_t (unsigned long long) -> int | `ref->GetAssignment(C) == other->GetAssignment(B) &&` | a | `size_t` loop index or count passed to an `int` parameter; bounded by game data; epic 1300 file, list only |
| 223 | `CvTacticalAI.cpp:7106:53` | size_t (unsigned long long) -> int | `ref->GetAssignment(C) == other->GetAssignment(B) &&` | a | `size_t` loop index or count passed to an `int` parameter; bounded by game data; epic 1300 file, list only |
| 224 | `CvTacticalAI.cpp:7107:26` | size_t (unsigned long long) -> int | `ref->GetAssignment(D) == other->GetAssignment(C);` | a | `size_t` loop index or count passed to an `int` parameter; bounded by game data; epic 1300 file, list only |
| 225 | `CvTacticalAI.cpp:7107:53` | size_t (unsigned long long) -> int | `ref->GetAssignment(D) == other->GetAssignment(C);` | a | `size_t` loop index or count passed to an `int` parameter; bounded by game data; epic 1300 file, list only |
| 226 | `CvTacticalAI.cpp:7108:37` | size_t (unsigned long long) -> int | `bool CDAB = ref->GetAssignment(A) == other->GetAssignment(C) &&` | a | `size_t` loop index or count passed to an `int` parameter; bounded by game data; epic 1300 file, list only |
| 227 | `CvTacticalAI.cpp:7108:64` | size_t (unsigned long long) -> int | `bool CDAB = ref->GetAssignment(A) == other->GetAssignment(C) &&` | a | `size_t` loop index or count passed to an `int` parameter; bounded by game data; epic 1300 file, list only |
| 228 | `CvTacticalAI.cpp:7109:26` | size_t (unsigned long long) -> int | `ref->GetAssignment(B) == other->GetAssignment(D) &&` | a | `size_t` loop index or count passed to an `int` parameter; bounded by game data; epic 1300 file, list only |
| 229 | `CvTacticalAI.cpp:7109:53` | size_t (unsigned long long) -> int | `ref->GetAssignment(B) == other->GetAssignment(D) &&` | a | `size_t` loop index or count passed to an `int` parameter; bounded by game data; epic 1300 file, list only |
| 230 | `CvTacticalAI.cpp:7110:26` | size_t (unsigned long long) -> int | `ref->GetAssignment(C) == other->GetAssignment(A) &&` | a | `size_t` loop index or count passed to an `int` parameter; bounded by game data; epic 1300 file, list only |
| 231 | `CvTacticalAI.cpp:7110:53` | size_t (unsigned long long) -> int | `ref->GetAssignment(C) == other->GetAssignment(A) &&` | a | `size_t` loop index or count passed to an `int` parameter; bounded by game data; epic 1300 file, list only |
| 232 | `CvTacticalAI.cpp:7111:26` | size_t (unsigned long long) -> int | `ref->GetAssignment(D) == other->GetAssignment(B);` | a | `size_t` loop index or count passed to an `int` parameter; bounded by game data; epic 1300 file, list only |
| 233 | `CvTacticalAI.cpp:7111:53` | size_t (unsigned long long) -> int | `ref->GetAssignment(D) == other->GetAssignment(B);` | a | `size_t` loop index or count passed to an `int` parameter; bounded by game data; epic 1300 file, list only |
| 234 | `CvTacticalAI.cpp:7112:37` | size_t (unsigned long long) -> int | `bool BCDA = ref->GetAssignment(A) == other->GetAssignment(B) &&` | a | `size_t` loop index or count passed to an `int` parameter; bounded by game data; epic 1300 file, list only |
| 235 | `CvTacticalAI.cpp:7112:64` | size_t (unsigned long long) -> int | `bool BCDA = ref->GetAssignment(A) == other->GetAssignment(B) &&` | a | `size_t` loop index or count passed to an `int` parameter; bounded by game data; epic 1300 file, list only |
| 236 | `CvTacticalAI.cpp:7113:26` | size_t (unsigned long long) -> int | `ref->GetAssignment(B) == other->GetAssignment(C) &&` | a | `size_t` loop index or count passed to an `int` parameter; bounded by game data; epic 1300 file, list only |
| 237 | `CvTacticalAI.cpp:7113:53` | size_t (unsigned long long) -> int | `ref->GetAssignment(B) == other->GetAssignment(C) &&` | a | `size_t` loop index or count passed to an `int` parameter; bounded by game data; epic 1300 file, list only |
| 238 | `CvTacticalAI.cpp:7114:26` | size_t (unsigned long long) -> int | `ref->GetAssignment(C) == other->GetAssignment(D) &&` | a | `size_t` loop index or count passed to an `int` parameter; bounded by game data; epic 1300 file, list only |
| 239 | `CvTacticalAI.cpp:7114:53` | size_t (unsigned long long) -> int | `ref->GetAssignment(C) == other->GetAssignment(D) &&` | a | `size_t` loop index or count passed to an `int` parameter; bounded by game data; epic 1300 file, list only |
| 240 | `CvTacticalAI.cpp:7115:26` | size_t (unsigned long long) -> int | `ref->GetAssignment(D) == other->GetAssignment(A);` | a | `size_t` loop index or count passed to an `int` parameter; bounded by game data; epic 1300 file, list only |
| 241 | `CvTacticalAI.cpp:7115:53` | size_t (unsigned long long) -> int | `ref->GetAssignment(D) == other->GetAssignment(A);` | a | `size_t` loop index or count passed to an `int` parameter; bounded by game data; epic 1300 file, list only |
| 242 | `CvTacticalAI.cpp:9280:27` | size_t (unsigned long long) -> int | `int iNumAvailableUnits = GetNumAvailableUnits();` | a | `size_t` loop index or count passed to an `int` parameter; bounded by game data; epic 1300 file, list only |
| 243 | `CvTacticalAI.cpp:12415:39` | size_t (unsigned long long) -> int | `int iOldPauseCount = iUsedPositions / 500;` | a | `size_t` loop index or count passed to an `int` parameter; bounded by game data; epic 1300 file, list only |
| 244 | `CvTacticalAI.cpp:12417:39` | size_t (unsigned long long) -> int | `int iNewPauseCount = iUsedPositions / 500;` | a | `size_t` loop index or count passed to an `int` parameter; bounded by game data; epic 1300 file, list only |
| 245 | `CvTacticalAI.cpp:12436:42` | size_t (unsigned long long) -> int | `int iStartingUnits = initialPosition->GetNumAvailableUnits();` | a | `size_t` loop index or count passed to an `int` parameter; bounded by game data; epic 1300 file, list only |
| 246 | `CvTacticalAI.cpp:12845:39` | size_t (unsigned long long) -> int | `int iOldPauseCount = iUsedPositions / 500;` | a | `size_t` loop index or count passed to an `int` parameter; bounded by game data; epic 1300 file, list only |
| 247 | `CvTacticalAI.cpp:12847:39` | size_t (unsigned long long) -> int | `int iNewPauseCount = iUsedPositions / 500;` | a | `size_t` loop index or count passed to an `int` parameter; bounded by game data; epic 1300 file, list only |
| 248 | `CvTacticalAI.cpp:12871:42` | size_t (unsigned long long) -> int | `int iAvailableUnits = initialPosition->GetNumAvailableUnits();` | a | `size_t` loop index or count passed to an `int` parameter; bounded by game data; epic 1300 file, list only |
| 249 | `CvTacticalAnalysisMap.cpp:639:27` | size_type (unsigned long long) -> int | `return m_vDominanceZones.size();` | a | container size returned as `int`/`uint` (game-data or entity container) |
| 250 | `CvTacticalAnalysisMap.cpp:715:45` | size_type (unsigned long long) -> mapped_type (int) | `m_IdLookup[iZoneID] = m_vDominanceZones.size();` | a | zone index stored in a lookup map; small |
| 251 | `CvTacticalAnalysisMap.cpp:774:55` | size_type (unsigned long long) -> mapped_type (int) | `m_IdLookup[newZone.GetZoneID()] = m_vDominanceZones.size();` | a | zone index stored in a lookup map; small |
| 252 | `CvTacticalAnalysisMap.cpp:1094:50` | size_t (unsigned long long) -> mapped_type (int) | `m_IdLookup[m_vDominanceZones[i].GetZoneID()] = i;` | a | zone index stored in a lookup map; small |
| 253 | `CvTacticalAnalysisMap.cpp:1140:45` | size_t (unsigned long long) -> int | `CvPlot* pPlot = GC.getMap().plotByIndex(i);` | a | `size_t` loop index or count passed to an `int` parameter; bounded by game data |
| 254 | `CvTechAI.cpp:263:49` | size_type (unsigned long long) -> int | `int iDenominator = aLeaderWithNumTechs.size() - 1;` | b | `size() - 1` (or `- ui`): on an empty container wraps to `SIZE_MAX`, narrows to -1 on both builds; correct by modular wrap, an explicit `(int)` before the subtraction would say so |
| 255 | `CvTechClasses.cpp:608:25` | size_type (unsigned long long) -> int | `return m_paTechEntries.size();` | a | container size returned as `int`/`uint` (game-data or entity container) |
| 256 | `CvTechClasses.cpp:1817:32` | size_type (unsigned long long) -> int | `int iNumEntries = aiTechCosts.size();` | a | container size of game data or entities |
| 257 | `CvTradeClasses.cpp:793:52` | size_type (unsigned long long) -> int | `iNewTradeRouteIndex = m_aTradeConnections.size() - 1;` | b | `size() - 1` (or `- ui`): on an empty container wraps to `SIZE_MAX`, narrows to -1 on both builds; correct by modular wrap, an explicit `(int)` before the subtraction would say so |
| 258 | `CvTradeClasses.cpp:2319:76` | size_t (unsigned long long) -> int | `gDLL->TradeVisuals_NewRoute(TEMPORARY_POPUPROUTE_ID,eOriginPlayer,type,n,plotsX,plotsY);` | a | plot count passed to the EXE interface (`gDLL`); small |
| 259 | `CvTradeClasses.cpp:4543:68` | size_type (unsigned long long) -> int | `int nPlots = pTrade->GetTradeConnection(iRouteIndex).m_aPlotList.size();` | a | container size of game data or entities |
| 260 | `CvTradeClasses.cpp:6326:65` | size_type (unsigned long long) -> int | `int iPrioPercent = ((aTradeConnectionList.size() - ui) * 100) / aTradeConnectionList.size();` | b | `size() - 1` (or `- ui`): on an empty container wraps to `SIZE_MAX`, narrows to -1 on both builds; correct by modular wrap, an explicit `(int)` before the subtraction would say so |
| 261 | `CvTraitClasses.cpp:3834:26` | size_type (unsigned long long) -> int | `return m_paTraitEntries.size();` | a | container size returned as `int`/`uint` (game-data or entity container) |
| 262 | `CvTraitClasses.cpp:5880:55` | size_type (unsigned long long) -> int | `int iNumUniqueResourcesGiven = m_aUniqueLuxuryAreas.size();` | a | container size of game data or entities |
| 263 | `CvTraitClasses.cpp:5927:53` | size_type (unsigned long long) -> int | `int iSize = m_aiRandomGreatPersonProgressFromKills.size();` | a | container size of game data or entities |
| 264 | `CvTraitClasses.cpp:5933:85` | size_type (unsigned long long) -> uint (unsigned int) | `uChoice = GC.getGame().urandLimitExclusive(m_aiRandomGreatPersonProgressFromKills.size(), additionalSeed.mi...` | a | RNG bound = container size; same value on both builds, so the same draw |
| 265 | `CvTraitClasses.cpp:6101:60` | size_t (unsigned long long) -> uint (unsigned int) | `if (m_ppaaiYieldChangePerImprovementBuilt[iImprovement][iYield] != 0)` | a | `size_t` loop index or count passed to an `int` parameter; bounded by game data |
| 266 | `CvTraitClasses.cpp:6609:69` | size_type (unsigned long long) -> uint (unsigned int) | `uint uChoice = GC.getGame().urandLimitExclusive(vPossibleResources.size(), CvSeeder(pCity->plot()->GetPseud...` | a | RNG bound = container size; same value on both builds, so the same draw |
| 267 | `CvTraitClasses.cpp:6744:55` | size_type (unsigned long long) -> int | `int iNumUniqueResourcesGiven = m_aUniqueLuxuryAreas.size();` | a | container size of game data or entities |
| 268 | `CvTreasury.cpp:725:51` | size_type (unsigned long long) -> int | `int iIndex = m_GoldChangeForTurnTimes100.size() - 1;` | b | `size() - 1` (or `- ui`): on an empty container wraps to `SIZE_MAX`, narrows to -1 on both builds; correct by modular wrap, an explicit `(int)` before the subtraction would say so |
| 269 | `CvUnit.cpp:634:66` | size_type (unsigned long long) -> uint (unsigned int) | `uint uRoll = GC.getGame().urandLimitExclusive(vfPossibleUnits.size(), CvSeeder(iID));` | a | RNG bound = container size; same value on both builds, so the same draw |
| 270 | `CvUnit.cpp:658:67` | size_type (unsigned long long) -> uint (unsigned int) | `uint uRoll = GC.getGame().urandLimitExclusive(vfPossibleUnits.size(), CvSeeder(iID));` | a | RNG bound = container size; same value on both builds, so the same draw |
| 271 | `CvUnit.cpp:29807:37` | size_type (unsigned long long) -> unsigned int | `kPlotArray.reserve(m_unitMoveLocs.size());` | a | container size of game data or entities |
| 272 | `CvUnit.cpp:30554:51` | size_t (unsigned long long) -> int | `CvPlot* pTestPlot = m_kLastPath.GetPlotByIndex(iIndex);` | a | `size_t` loop index or count passed to an `int` parameter; bounded by game data |
| 273 | `CvUnit.cpp:31592:46` | size_t (unsigned long long) -> int | `CvPlot* pPlot = m_kLastPath.GetPlotByIndex(i);` | a | `size_t` loop index or count passed to an `int` parameter; bounded by game data |
| 274 | `CvUnit.cpp:31663:40` | size_type (unsigned long long) -> int | `int iCount = vInflictedPlagues.size() + 1;` | a | container size of game data or entities |
| 275 | `CvUnit.cpp:31942:77` | size_type (unsigned long long) -> uint (unsigned int) | `advance(itChosenPlot, GC.getGame().urandLimitExclusive(aNumFriendlies.size(), CvSeeder(plot()->GetPseudoRan...` | a | RNG bound = container size; same value on both builds, so the same draw |
| 276 | `CvUnitClasses.cpp:598:33` | const size_t (const unsigned long long) -> int | `pResults->Bind(1, szUnitType, lenUnitType, false);` | a | string length (identifier, SQL text, log text) |
| 277 | `CvUnitClasses.cpp:625:33` | const size_t (const unsigned long long) -> int | `pResults->Bind(1, szUnitType, lenUnitType, false);` | a | string length (identifier, SQL text, log text) |
| 278 | `CvUnitClasses.cpp:648:33` | const size_t (const unsigned long long) -> int | `pResults->Bind(1, szUnitType, lenUnitType, false);` | a | string length (identifier, SQL text, log text) |
| 279 | `CvUnitClasses.cpp:2067:25` | size_type (unsigned long long) -> int | `return m_paUnitEntries.size();` | a | container size returned as `int`/`uint` (game-data or entity container) |
| 280 | `CvVotingClasses.cpp:3881:20` | size_type (unsigned long long) -> int | `return m_vMembers.size();` | a | container size returned as `int`/`uint` (game-data or entity container) |
| 281 | `CvVotingClasses.cpp:9703:26` | size_type (unsigned long long) -> int | `return m_vActiveLeagues.size();` | a | container size returned as `int`/`uint` (game-data or entity container) |
| 282 | `CvVotingClasses.cpp:14593:41` | size_type (unsigned long long) -> int | `return m_paLeagueSpecialSessionEntries.size();` | a | container size returned as `int`/`uint` (game-data or entity container) |
| 283 | `CvVotingClasses.cpp:14657:31` | size_type (unsigned long long) -> int | `return m_paLeagueNameEntries.size();` | a | container size returned as `int`/`uint` (game-data or entity container) |
| 284 | `CvVotingClasses.cpp:14806:40` | size_type (unsigned long long) -> int | `return m_paLeagueProjectRewardEntries.size();` | a | container size returned as `int`/`uint` (game-data or entity container) |
| 285 | `CvVotingClasses.cpp:14906:34` | size_type (unsigned long long) -> int | `return m_paLeagueProjectEntries.size();` | a | container size returned as `int`/`uint` (game-data or entity container) |
| 286 | `CvVotingClasses.cpp:15265:31` | size_type (unsigned long long) -> int | `return m_paResolutionEntries.size();` | a | container size returned as `int`/`uint` (game-data or entity container) |
| 287 | `Lua/CvLuaCity.cpp:850:21` | lua_Integer (long long) -> const int | `const int iIndex = lua_tointeger(L, 2);` | a | L1 |
| 288 | `Lua/CvLuaCity.cpp:1026:20` | lua_Integer (long long) -> const int | `const int iUnit = lua_tointeger(L, 2);` | a | L1 |
| 289 | `Lua/CvLuaCity.cpp:1067:24` | lua_Integer (long long) -> const int | `const int iBuilding = lua_tointeger(L, 2);` | a | L1 |
| 290 | `Lua/CvLuaCity.cpp:1082:23` | lua_Integer (long long) -> const int | `const int iProject = lua_tointeger(L, 2);` | a | L1 |
| 291 | `Lua/CvLuaCity.cpp:1095:23` | lua_Integer (long long) -> const int | `const int iProcess = lua_tointeger(L, 2);` | a | L1 |
| 292 | `Lua/CvLuaCity.cpp:1635:90` | size_type (unsigned long long) -> int | `iYieldTimes100 += (pkBuildingInfo->GetYieldChangePerTile(eYield) * pCity->GetPlotList().size() * 100).Trunc...` | a | container size of game data or entities |
| 293 | `Lua/CvLuaCity.cpp:1814:17` | lua_Integer (long long) -> int | `order.iData1 = lua_tointeger(L, 3);` | a | L1 |
| 294 | `Lua/CvLuaCity.cpp:1815:17` | lua_Integer (long long) -> int | `order.iData2 = lua_tointeger(L, 4);` | a | L1 |
| 295 | `Lua/CvLuaCity.cpp:2748:16` | lua_Integer (long long) -> const int | `const int x = lua_tointeger(L, 2);` | a | L1 |
| 296 | `Lua/CvLuaCity.cpp:2749:16` | lua_Integer (long long) -> const int | `const int y = lua_tointeger(L, 3);` | a | L1 |
| 297 | `Lua/CvLuaCity.cpp:2832:17` | lua_Integer (long long) -> const int | `const int iX = lua_tointeger(L, 2);` | a | L1 |
| 298 | `Lua/CvLuaCity.cpp:2833:17` | lua_Integer (long long) -> const int | `const int iY = lua_tointeger(L, 3);` | a | L1 |
| 299 | `Lua/CvLuaCity.cpp:2890:17` | lua_Integer (long long) -> const int | `const int iX = lua_tointeger(L, 2);` | a | L1 |
| 300 | `Lua/CvLuaCity.cpp:2891:17` | lua_Integer (long long) -> const int | `const int iY = lua_tointeger(L, 3);` | a | L1 |
| 301 | `Lua/CvLuaCity.cpp:2935:15` | lua_Integer (long long) -> int | `int iValue = lua_tointeger(L, 2);` | a | L1 |
| 302 | `Lua/CvLuaCity.cpp:2948:16` | lua_Integer (long long) -> int | `int iChange = lua_tointeger(L, 2);` | a | L1 |
| 303 | `Lua/CvLuaCity.cpp:3104:15` | lua_Integer (long long) -> int | `int iValue = lua_tointeger(L, 2);` | a | L1 |
| 304 | `Lua/CvLuaCity.cpp:3188:16` | lua_Integer (long long) -> int | `int iChange = lua_tointeger(L, 2);` | a | L1 |
| 305 | `Lua/CvLuaCity.cpp:3205:16` | lua_Integer (long long) -> int | `int iChange = lua_tointeger(L, 2);` | a | L1 |
| 306 | `Lua/CvLuaCity.cpp:3222:16` | lua_Integer (long long) -> int | `int iChange = lua_tointeger(L, 2);` | a | L1 |
| 307 | `Lua/CvLuaCity.cpp:3290:16` | lua_Integer (long long) -> int | `int iChange = lua_tointeger(L, 2);` | a | L1 |
| 308 | `Lua/CvLuaCity.cpp:3313:16` | lua_Integer (long long) -> int | `int iChange = lua_tointeger(L, 2);` | a | L1 |
| 309 | `Lua/CvLuaCity.cpp:3573:16` | lua_Integer (long long) -> int | `int iChange = lua_tointeger(L, 2);` | a | L1 |
| 310 | `Lua/CvLuaCity.cpp:3678:17` | lua_Integer (long long) -> int | `int iPercent = lua_tointeger(L, 4);` | a | L1 |
| 311 | `Lua/CvLuaCity.cpp:4640:21` | lua_Integer (long long) -> const int | `const int iValue = lua_tointeger(L, 3);` | a | L1 |
| 312 | `Lua/CvLuaCity.cpp:4908:24` | lua_Integer (long long) -> const int | `const int iResource = lua_tointeger(L, 2);` | a | L1 |
| 313 | `Lua/CvLuaCity.cpp:4920:21` | lua_Integer (long long) -> const int | `const int iIndex = lua_tointeger(L, 2);` | a | L1 |
| 314 | `Lua/CvLuaCity.cpp:4937:21` | lua_Integer (long long) -> const int | `const int iIndex = lua_tointeger(L, 2);` | a | L1 |
| 315 | `Lua/CvLuaCity.cpp:4938:24` | lua_Integer (long long) -> const int | `const int iNewValue = lua_tointeger(L, 3);` | a | L1 |
| 316 | `Lua/CvLuaCity.cpp:4951:21` | lua_Integer (long long) -> const int | `const int iIndex = lua_tointeger(L, 2);` | a | L1 |
| 317 | `Lua/CvLuaCity.cpp:4952:22` | lua_Integer (long long) -> const int | `const int iChange = lua_tointeger(L, 3);` | a | L1 |
| 318 | `Lua/CvLuaCity.cpp:4965:21` | lua_Integer (long long) -> const int | `const int iIndex = lua_tointeger(L, 2);` | a | L1 |
| 319 | `Lua/CvLuaCity.cpp:4982:21` | lua_Integer (long long) -> const int | `const int iIndex = lua_tointeger(L, 2);` | a | L1 |
| 320 | `Lua/CvLuaCity.cpp:4983:24` | lua_Integer (long long) -> const int | `const int iNewValue = lua_tointeger(L, 3);` | a | L1 |
| 321 | `Lua/CvLuaCity.cpp:5183:22` | lua_Integer (long long) -> const int | `const int iChange = lua_tointeger(L, 3);` | a | L1 |
| 322 | `Lua/CvLuaCity.cpp:5278:21` | lua_Integer (long long) -> const int | `const int iFocus = lua_tointeger(L, 2);` | a | L1 |
| 323 | `Lua/CvLuaCity.cpp:5538:21` | lua_Integer (long long) -> const int | `const int iIndex = lua_tointeger(L, 2);` | a | L1 |
| 324 | `Lua/CvLuaCity.cpp:5589:25` | lua_Integer (long long) -> const int | `const int iNewValue = lua_tointeger(L, 3);` | a | L1 |
| 325 | `Lua/CvLuaCity.cpp:5623:25` | lua_Integer (long long) -> const int | `const int iNewValue = lua_tointeger(L, 3);` | a | L1 |
| 326 | `Lua/CvLuaCity.cpp:5685:20` | lua_Integer (long long) -> const int | `const int iSlot = lua_tointeger(L, 3);` | a | L1 |
| 327 | `Lua/CvLuaCity.cpp:5696:20` | lua_Integer (long long) -> const int | `const int iSlot = lua_tointeger(L, 3);` | a | L1 |
| 328 | `Lua/CvLuaCity.cpp:5697:30` | lua_Integer (long long) -> const int | `const int iGreatWorkIndex = lua_tointeger(L, 4);` | a | L1 |
| 329 | `Lua/CvLuaCity.cpp:5744:22` | lua_Integer (long long) -> const int | `const int iData1		= lua_tointeger(L, 3);` | a | L1 |
| 330 | `Lua/CvLuaCity.cpp:5745:22` | lua_Integer (long long) -> const int | `const int iData2		= lua_tointeger(L, 4);` | a | L1 |
| 331 | `Lua/CvLuaCity.cpp:5759:19` | lua_Integer (long long) -> const int | `const int iNum = lua_tointeger(L, 2);` | a | L1 |
| 332 | `Lua/CvLuaCity.cpp:5778:19` | lua_Integer (long long) -> const int | `const int iNum = lua_tointeger(L, 2);` | a | L1 |
| 333 | `Lua/CvLuaCity.cpp:5801:29` | lua_Integer (long long) -> const int | `const int eBuildingClass = lua_tointeger(L, 2);` | a | L1 |
| 334 | `Lua/CvLuaCity.cpp:5802:21` | lua_Integer (long long) -> const int | `const int eYield = lua_tointeger(L, 3);` | a | L1 |
| 335 | `Lua/CvLuaCity.cpp:5834:29` | lua_Integer (long long) -> const int | `const int eBuildingClass = lua_tointeger(L, 2);` | a | L1 |
| 336 | `Lua/CvLuaCity.cpp:5835:21` | lua_Integer (long long) -> const int | `const int eYield = lua_tointeger(L, 3);` | a | L1 |
| 337 | `Lua/CvLuaCity.cpp:5908:24` | lua_Integer (long long) -> const int | `const int iResource = lua_tointeger(L, 2);` | a | L1 |
| 338 | `Lua/CvLuaCity.cpp:6010:21` | lua_Integer (long long) -> const int | `const int eYield = lua_tointeger(L, 2);` | a | L1 |
| 339 | `Lua/CvLuaCity.cpp:6046:21` | lua_Integer (long long) -> const int | `const int eYield = lua_tointeger(L, 2);` | a | L1 |
| 340 | `Lua/CvLuaCity.cpp:6187:29` | lua_Integer (long long) -> const int | `const int eBuildingClass = lua_tointeger(L, 2);` | a | L1 |
| 341 | `Lua/CvLuaCity.cpp:6188:21` | lua_Integer (long long) -> const int | `const int eYield = lua_tointeger(L, 3);` | a | L1 |
| 342 | `Lua/CvLuaCity.cpp:6189:22` | lua_Integer (long long) -> const int | `const int iChange = lua_tointeger(L, 4);` | a | L1 |
| 343 | `Lua/CvLuaCity.cpp:6208:18` | lua_Integer (long long) -> int | `int iUnitType = lua_tointeger(L, 2);` | a | L1 |
| 344 | `Lua/CvLuaCity.cpp:6415:20` | lua_Integer (long long) -> const int | `const int iFood = lua_tointeger(L, 2);` | a | L1 |
| 345 | `Lua/CvLuaCity.cpp:6558:24` | lua_Integer (long long) -> const int | `const int iCooldown = lua_tointeger(L, 3);` | a | L1 |
| 346 | `Lua/CvLuaCity.cpp:6580:24` | lua_Integer (long long) -> const int | `const int iCooldown = lua_tointeger(L, 3);` | a | L1 |
| 347 | `Lua/CvLuaCity.cpp:6600:25` | lua_Integer (long long) -> const int | `const int uiSpyIndex = lua_tointeger(L, 4);` | a | L1 |
| 348 | `Lua/CvLuaCity.cpp:6618:21` | lua_Integer (long long) -> const int | `const int iValue = lua_tointeger(L, 2);` | a | L1 |
| 349 | `Lua/CvLuaCity.cpp:6625:21` | lua_Integer (long long) -> const int | `const int iValue = lua_tointeger(L, 2);` | a | L1 |
| 350 | `Lua/CvLuaCity.cpp:6668:21` | lua_Integer (long long) -> const int | `const int iValue = lua_tointeger(L, 2);` | a | L1 |
| 351 | `Lua/CvLuaCity.cpp:6676:21` | lua_Integer (long long) -> const int | `const int iValue = lua_tointeger(L, 2);` | a | L1 |
| 352 | `Lua/CvLuaCity.cpp:6692:21` | lua_Integer (long long) -> const int | `const int iValue = lua_tointeger(L, 2);` | a | L1 |
| 353 | `Lua/CvLuaCity.cpp:6708:21` | lua_Integer (long long) -> const int | `const int iValue = lua_tointeger(L, 2);` | a | L1 |
| 354 | `Lua/CvLuaCity.cpp:6715:21` | lua_Integer (long long) -> const int | `const int iValue = lua_tointeger(L, 2);` | a | L1 |
| 355 | `Lua/CvLuaCity.cpp:6728:21` | lua_Integer (long long) -> const int | `const int iValue = lua_tointeger(L, 2);` | a | L1 |
| 356 | `Lua/CvLuaCity.cpp:6735:21` | lua_Integer (long long) -> const int | `const int iValue = lua_tointeger(L, 2);` | a | L1 |
| 357 | `Lua/CvLuaCity.cpp:6754:21` | lua_Integer (long long) -> const int | `const int iValue = lua_tointeger(L, 2);` | a | L1 |
| 358 | `Lua/CvLuaCity.cpp:6761:21` | lua_Integer (long long) -> const int | `const int iValue = lua_tointeger(L, 2);` | a | L1 |
| 359 | `Lua/CvLuaCity.cpp:6775:21` | lua_Integer (long long) -> const int | `const int iValue = lua_tointeger(L, 2);` | a | L1 |
| 360 | `Lua/CvLuaCity.cpp:6791:21` | lua_Integer (long long) -> const int | `const int iValue = lua_tointeger(L, 2);` | a | L1 |
| 361 | `Lua/CvLuaCity.cpp:6798:21` | lua_Integer (long long) -> const int | `const int iValue = lua_tointeger(L, 2);` | a | L1 |
| 362 | `Lua/CvLuaCity.cpp:6811:21` | lua_Integer (long long) -> const int | `const int iValue = lua_tointeger(L, 2);` | a | L1 |
| 363 | `Lua/CvLuaCity.cpp:6818:21` | lua_Integer (long long) -> const int | `const int iValue = lua_tointeger(L, 2);` | a | L1 |
| 364 | `Lua/CvLuaCity.cpp:6846:21` | lua_Integer (long long) -> const int | `const int iValue = lua_tointeger(L, 2);` | a | L1 |
| 365 | `Lua/CvLuaCity.cpp:6864:21` | lua_Integer (long long) -> const int | `const int iValue = lua_tointeger(L, 3);` | a | L1 |
| 366 | `Lua/CvLuaCity.cpp:6881:21` | lua_Integer (long long) -> const int | `const int iValue = lua_tointeger(L, 3);` | a | L1 |
| 367 | `Lua/CvLuaCity.cpp:6898:21` | lua_Integer (long long) -> const int | `const int iValue = lua_tointeger(L, 3);` | a | L1 |
| 368 | `Lua/CvLuaCity.cpp:6916:21` | lua_Integer (long long) -> const int | `const int iValue = lua_tointeger(L, 3);` | a | L1 |
| 369 | `Lua/CvLuaCity.cpp:6933:21` | lua_Integer (long long) -> const int | `const int iValue = lua_tointeger(L, 3);` | a | L1 |
| 370 | `Lua/CvLuaCity.cpp:6950:21` | lua_Integer (long long) -> const int | `const int iValue = lua_tointeger(L, 3);` | a | L1 |
| 371 | `Lua/CvLuaCity.cpp:6967:21` | lua_Integer (long long) -> const int | `const int iValue = lua_tointeger(L, 3);` | a | L1 |
| 372 | `Lua/CvLuaCity.cpp:6984:21` | lua_Integer (long long) -> const int | `const int iValue = lua_tointeger(L, 3);` | a | L1 |
| 373 | `Lua/CvLuaCity.cpp:7005:22` | size_t (unsigned long long) -> const int | `const int iLength = lua_objlen(L, 9);` | a | Lua table length |
| 374 | `Lua/CvLuaCity.cpp:7009:27` | lua_Integer (long long) -> const int | `const int iUnitCombat = lua_tointeger(L, -1);` | a | L1 |
| 375 | `Lua/CvLuaDeal.cpp:144:21` | lua_Integer (long long) -> const int | `const int iData1 = lua_tointeger(L, 5);` | a | L1 |
| 376 | `Lua/CvLuaDeal.cpp:145:21` | lua_Integer (long long) -> const int | `const int iData2 = lua_tointeger(L, 6);` | a | L1 |
| 377 | `Lua/CvLuaDeal.cpp:146:21` | lua_Integer (long long) -> const int | `const int iData3 = lua_tointeger(L, 7);` | a | L1 |
| 378 | `Lua/CvLuaDeal.cpp:160:21` | lua_Integer (long long) -> const int | `const int iData1 = lua_tointeger(L, 5);` | a | L1 |
| 379 | `Lua/CvLuaDeal.cpp:161:21` | lua_Integer (long long) -> const int | `const int iData2 = lua_tointeger(L, 6);` | a | L1 |
| 380 | `Lua/CvLuaDeal.cpp:162:21` | lua_Integer (long long) -> const int | `const int iData3 = lua_tointeger(L, 7);` | a | L1 |
| 381 | `Lua/CvLuaDeal.cpp:233:22` | lua_Integer (long long) -> const int | `const int iAmount = lua_tointeger(L, 3);` | a | L1 |
| 382 | `Lua/CvLuaDeal.cpp:243:22` | lua_Integer (long long) -> const int | `const int iAmount = lua_tointeger(L, 3);` | a | L1 |
| 383 | `Lua/CvLuaDeal.cpp:244:24` | lua_Integer (long long) -> const int | `const int iDuration = lua_tointeger(L, 4);` | a | L1 |
| 384 | `Lua/CvLuaDeal.cpp:264:22` | lua_Integer (long long) -> const int | `const int iAmount = lua_tointeger(L, 4);` | a | L1 |
| 385 | `Lua/CvLuaDeal.cpp:265:24` | lua_Integer (long long) -> const int | `const int iDuration = lua_tointeger(L, 5);` | a | L1 |
| 386 | `Lua/CvLuaDeal.cpp:275:22` | lua_Integer (long long) -> const int | `const int iCityID = lua_tointeger(L, 3);` | a | L1 |
| 387 | `Lua/CvLuaDeal.cpp:294:24` | lua_Integer (long long) -> const int | `const int iDuration = lua_tointeger(L, 3);` | a | L1 |
| 388 | `Lua/CvLuaDeal.cpp:304:24` | lua_Integer (long long) -> const int | `const int iDuration = lua_tointeger(L, 3);` | a | L1 |
| 389 | `Lua/CvLuaDeal.cpp:314:24` | lua_Integer (long long) -> const int | `const int iDuration = lua_tointeger(L, 3);` | a | L1 |
| 390 | `Lua/CvLuaDeal.cpp:324:24` | lua_Integer (long long) -> const int | `const int iDuration = lua_tointeger(L, 3);` | a | L1 |
| 391 | `Lua/CvLuaDeal.cpp:335:24` | lua_Integer (long long) -> const int | `const int iDuration = lua_tointeger(L, 4);` | a | L1 |
| 392 | `Lua/CvLuaDeal.cpp:364:28` | lua_Integer (long long) -> const int | `const int iResolutionID = lua_tointeger(L, 3);` | a | L1 |
| 393 | `Lua/CvLuaDeal.cpp:365:26` | lua_Integer (long long) -> const int | `const int iVoteChoice = lua_tointeger(L, 4);` | a | L1 |
| 394 | `Lua/CvLuaDeal.cpp:366:24` | lua_Integer (long long) -> const int | `const int iNumVotes = lua_tointeger(L, 5);` | a | L1 |
| 395 | `Lua/CvLuaDeal.cpp:406:28` | lua_Integer (long long) -> const int | `const int iResolutionID = lua_tointeger(L, 3);` | a | L1 |
| 396 | `Lua/CvLuaDeal.cpp:407:26` | lua_Integer (long long) -> const int | `const int iVoteChoice = lua_tointeger(L, 4);` | a | L1 |
| 397 | `Lua/CvLuaDeal.cpp:408:24` | lua_Integer (long long) -> const int | `const int iNumVotes = lua_tointeger(L, 5);` | a | L1 |
| 398 | `Lua/CvLuaFractal.cpp:101:25` | lua_Integer (long long) -> const int | `const int iNumSeeds		= lua_tointeger(L, 2);` | a | L1 |
| 399 | `Lua/CvLuaFractal.cpp:103:26` | lua_Integer (long long) -> const int | `const int iBlendRidge	= lua_tointeger(L, 4);` | a | L1 |
| 400 | `Lua/CvLuaFractal.cpp:104:26` | lua_Integer (long long) -> const int | `const int iBlendFract	= lua_tointeger(L, 5);` | a | L1 |
| 401 | `Lua/CvLuaFractal.cpp:144:21` | lua_Integer (long long) -> const int | `const int iNewXs = lua_tointeger(L, 1);` | a | L1 |
| 402 | `Lua/CvLuaFractal.cpp:145:21` | lua_Integer (long long) -> const int | `const int iNewYs = lua_tointeger(L, 2);` | a | L1 |
| 403 | `Lua/CvLuaFractal.cpp:146:21` | lua_Integer (long long) -> const int | `const int iGrain = lua_tointeger(L, 3);` | a | L1 |
| 404 | `Lua/CvLuaFractal.cpp:152:73` | lua_Integer (long long) -> const int | `const int iFracXExp = lua_isnil(L, 5)? CvFractal::DEFAULT_FRAC_X_EXP : lua_tointeger(L, 5);` | a | L1 |
| 405 | `Lua/CvLuaFractal.cpp:153:73` | lua_Integer (long long) -> const int | `const int iFracYExp = lua_isnil(L, 6)? CvFractal::DEFAULT_FRAC_Y_EXP : lua_tointeger(L, 6);` | a | L1 |
| 406 | `Lua/CvLuaFractal.cpp:165:21` | lua_Integer (long long) -> const int | `const int iNewXs = lua_tointeger(L, 1);` | a | L1 |
| 407 | `Lua/CvLuaFractal.cpp:166:21` | lua_Integer (long long) -> const int | `const int iNewYs = lua_tointeger(L, 2);` | a | L1 |
| 408 | `Lua/CvLuaFractal.cpp:167:21` | lua_Integer (long long) -> const int | `const int iGrain = lua_tointeger(L, 3);` | a | L1 |
| 409 | `Lua/CvLuaFractal.cpp:204:20` | lua_Integer (long long) -> int | `int percentage = lua_tointeger(L, 2);` | a | L1 |
| 410 | `Lua/CvLuaFractal.cpp:211:11` | lua_Integer (long long) -> int | `int x = lua_tointeger(L, 2);` | a | L1 |
| 411 | `Lua/CvLuaFractal.cpp:212:11` | lua_Integer (long long) -> int | `int y = lua_tointeger(L, 3);` | a | L1 |
| 412 | `Lua/CvLuaGame.cpp:536:22` | lua_Integer (long long) -> const int | `const int iAction = lua_tointeger(L, 1);` | a | L1 |
| 413 | `Lua/CvLuaGame.cpp:547:22` | lua_Integer (long long) -> const int | `const int iAction = lua_tointeger(L, 1);` | a | L1 |
| 414 | `Lua/CvLuaGame.cpp:576:21` | lua_Integer (long long) -> const int | `const int iCount = lua_tointeger(L, 4);` | a | L1 |
| 415 | `Lua/CvLuaGame.cpp:596:23` | lua_Integer (long long) -> const int | `const int eMessage	= lua_tointeger(L, 1);` | a | L1 |
| 416 | `Lua/CvLuaGame.cpp:611:23` | lua_Integer (long long) -> const int | `const int eMessage = lua_tointeger(L, 1);` | a | L1 |
| 417 | `Lua/CvLuaGame.cpp:628:22` | lua_Integer (long long) -> const int | `const int iData			= lua_tointeger(L, 3);` | a | L1 |
| 418 | `Lua/CvLuaGame.cpp:888:23` | lua_Integer (long long) -> const int | `const int iTurn = lua_tointeger(L, 1);` | a | L1 |
| 419 | `Lua/CvLuaGame.cpp:1118:15` | lua_Integer (long long) -> int | `iNumNukes = lua_tointeger(L, 1); // The correct Game.ChangeNoNukesCount() usage` | a | L1 |
| 420 | `Lua/CvLuaGame.cpp:1120:15` | lua_Integer (long long) -> int | `iNumNukes = lua_tointeger(L, 2); // The incorrect (but as used by the game core .lua files) Game:ChangeNoNu...` | a | L1 |
| 421 | `Lua/CvLuaGame.cpp:1137:15` | lua_Integer (long long) -> int | `iNumNukes = lua_tointeger(L, 1); // The correct Game.ChangeNukesExploded() usage` | a | L1 |
| 422 | `Lua/CvLuaGame.cpp:1139:15` | lua_Integer (long long) -> int | `iNumNukes = lua_tointeger(L, 2); // The incorrect (but as used by the game core .lua files) Game:ChangeNuke...` | a | L1 |
| 423 | `Lua/CvLuaGame.cpp:1384:17` | lua_Integer (long long) -> int | `int iPlayer = lua_tointeger(L, 1);` | a | L1 |
| 424 | `Lua/CvLuaGame.cpp:1695:22` | lua_Integer (long long) -> const int | `const int max_num = luaL_checkinteger(L, 1);` | a | L1 |
| 425 | `Lua/CvLuaGame.cpp:1918:21` | lua_Integer (long long) -> const int | `const int iMajor = lua_tointeger(L, 1);` | a | L1 |
| 426 | `Lua/CvLuaGame.cpp:1919:21` | lua_Integer (long long) -> const int | `const int iMinor = lua_tointeger(L, 2);` | a | L1 |
| 427 | `Lua/CvLuaGame.cpp:1948:21` | lua_Integer (long long) -> const int | `const int iBully = lua_tointeger(L, 1);` | a | L1 |
| 428 | `Lua/CvLuaGame.cpp:1949:21` | lua_Integer (long long) -> const int | `const int iMinor = lua_tointeger(L, 2);` | a | L1 |
| 429 | `Lua/CvLuaGame.cpp:1958:21` | lua_Integer (long long) -> const int | `const int iBully = lua_tointeger(L, 1);` | a | L1 |
| 430 | `Lua/CvLuaGame.cpp:1959:21` | lua_Integer (long long) -> const int | `const int iMinor = lua_tointeger(L, 2);` | a | L1 |
| 431 | `Lua/CvLuaGame.cpp:1967:21` | lua_Integer (long long) -> const int | `const int iBully = lua_tointeger(L, 1);` | a | L1 |
| 432 | `Lua/CvLuaGame.cpp:1968:21` | lua_Integer (long long) -> const int | `const int iMinor = lua_tointeger(L, 2);` | a | L1 |
| 433 | `Lua/CvLuaGame.cpp:1977:21` | lua_Integer (long long) -> const int | `const int iMajor = lua_tointeger(L, 1);` | a | L1 |
| 434 | `Lua/CvLuaGame.cpp:1978:21` | lua_Integer (long long) -> const int | `const int iMinor = lua_tointeger(L, 2);` | a | L1 |
| 435 | `Lua/CvLuaGame.cpp:1985:21` | lua_Integer (long long) -> const int | `const int iMajor = lua_tointeger(L, 1);` | a | L1 |
| 436 | `Lua/CvLuaGame.cpp:1986:21` | lua_Integer (long long) -> const int | `const int iMinor = lua_tointeger(L, 2);` | a | L1 |
| 437 | `Lua/CvLuaGame.cpp:2017:22` | lua_Integer (long long) -> const int | `const int iPlayer = lua_tointeger(L, 1);` | a | L1 |
| 438 | `Lua/CvLuaGame.cpp:2018:23` | lua_Integer (long long) -> const int | `const int eMessage = lua_tointeger(L, 2);` | a | L1 |
| 439 | `Lua/CvLuaGame.cpp:2019:21` | lua_Integer (long long) -> const int | `const int iData1 = lua_tointeger(L, 3);` | a | L1 |
| 440 | `Lua/CvLuaGame.cpp:2752:63` | lua_Integer (long long) -> int | `GC.getGame().GetGameReligions()->SetMinimumFaithNextPantheon(lua_tointeger(L, 1));` | a | L1 |
| 441 | `Lua/CvLuaGame.cpp:3130:25` | lua_Integer (long long) -> const int | `const int iFoundYear = lua_tointeger(L, 2);` | a | L1 |
| 442 | `Lua/CvLuaGame.cpp:3161:21` | lua_Integer (long long) -> int | `int iAchievement = lua_tointeger(L, 1);` | a | L1 |
| 443 | `Lua/CvLuaGame.cpp:3168:19` | lua_Integer (long long) -> int | `int iSteamStat = lua_tointeger(L, 1);` | a | L1 |
| 444 | `Lua/CvLuaGame.cpp:3238:22` | lua_Integer (long long) -> const int | `const int iValue = lua_tointeger(L, 3);` | a | L1 |
| 445 | `Lua/CvLuaGame.cpp:3332:21` | lua_Integer (long long) -> const int | `const int iIndex = lua_tointeger(L, 1);` | a | L1 |
| 446 | `Lua/CvLuaGame.cpp:3345:21` | lua_Integer (long long) -> const int | `const int iIndex = lua_tointeger(L, 1);` | a | L1 |
| 447 | `Lua/CvLuaGame.cpp:3357:21` | lua_Integer (long long) -> const int | `const int iIndex = lua_tointeger(L, 1);` | a | L1 |
| 448 | `Lua/CvLuaGame.cpp:3370:21` | lua_Integer (long long) -> const int | `const int iIndex = lua_tointeger(L, 1);` | a | L1 |
| 449 | `Lua/CvLuaGame.cpp:3383:21` | lua_Integer (long long) -> const int | `const int iIndex = lua_tointeger(L, 1);` | a | L1 |
| 450 | `Lua/CvLuaGame.cpp:3395:21` | lua_Integer (long long) -> const int | `const int iIndex = lua_tointeger(L, 1);` | a | L1 |
| 451 | `Lua/CvLuaGame.cpp:3407:21` | lua_Integer (long long) -> const int | `const int iIndex = lua_tointeger(L, 1);` | a | L1 |
| 452 | `Lua/CvLuaGame.cpp:3420:21` | lua_Integer (long long) -> const int | `const int iIndex = lua_tointeger(L, 1);` | a | L1 |
| 453 | `Lua/CvLuaGame.cpp:3433:21` | lua_Integer (long long) -> const int | `const int iIndex = lua_tointeger(L, 1);` | a | L1 |
| 454 | `Lua/CvLuaGame.cpp:3446:21` | lua_Integer (long long) -> const int | `const int iIndex = lua_tointeger(L, 1);` | a | L1 |
| 455 | `Lua/CvLuaGame.cpp:3459:21` | lua_Integer (long long) -> const int | `const int iIndex = lua_tointeger(L, 1);` | a | L1 |
| 456 | `Lua/CvLuaGame.cpp:3541:26` | lua_Integer (long long) -> const int | `const int iRouteIndex = lua_tointeger(L, 1);` | a | L1 |
| 457 | `Lua/CvLuaGame.cpp:3642:11` | lua_Integer (long long) -> int | `iPlotX = lua_tointeger(L,1);` | a | L1 |
| 458 | `Lua/CvLuaGame.cpp:3643:11` | lua_Integer (long long) -> int | `iPlotY = lua_tointeger(L,2);` | a | L1 |
| 459 | `Lua/CvLuaGame.cpp:3656:11` | lua_Integer (long long) -> int | `iPlotX = lua_tointeger(L,1);` | a | L1 |
| 460 | `Lua/CvLuaGame.cpp:3657:11` | lua_Integer (long long) -> int | `iPlotY = lua_tointeger(L,2);` | a | L1 |
| 461 | `Lua/CvLuaGame.cpp:4078:15` | lua_Integer (long long) -> int | `int iValue = lua_tointeger(L, 3);` | a | L1 |
| 462 | `Lua/CvLuaGame.cpp:4219:13` | lua_Integer (long long) -> int | `int year = lua_tointeger(L, 1);` | a | L1 |
| 463 | `Lua/CvLuaGame.cpp:4231:14` | lua_Integer (long long) -> int | `int value = lua_tointeger(L, 1);` | a | L1 |
| 464 | `Lua/CvLuaLeague.cpp:210:22` | lua_Integer (long long) -> const int | `const int iChoice = lua_tointeger(L, 4);` | a | L1 |
| 465 | `Lua/CvLuaLeague.cpp:221:28` | lua_Integer (long long) -> const int | `const int iResolutionID = lua_tointeger(L, 2);` | a | L1 |
| 466 | `Lua/CvLuaLeague.cpp:233:28` | lua_Integer (long long) -> const int | `const int iResolutionID = lua_tointeger(L, 2);` | a | L1 |
| 467 | `Lua/CvLuaLeague.cpp:267:22` | lua_Integer (long long) -> const int | `const int iChoice = lua_tointeger(L, 3);` | a | L1 |
| 468 | `Lua/CvLuaLeague.cpp:552:24` | lua_Integer (long long) -> const int | `const int iProposal = lua_tointeger(L, 2);` | a | L1 |
| 469 | `Lua/CvLuaLeague.cpp:563:24` | lua_Integer (long long) -> const int | `const int iProposal = lua_tointeger(L, 2);` | a | L1 |
| 470 | `Lua/CvLuaLeague.cpp:565:24` | lua_Integer (long long) -> const int | `const int iNumVotes = lua_tointeger(L, 4);` | a | L1 |
| 471 | `Lua/CvLuaLeague.cpp:566:22` | lua_Integer (long long) -> const int | `const int iChoice = lua_tointeger(L, 5);` | a | L1 |
| 472 | `Lua/CvLuaLeague.cpp:576:24` | lua_Integer (long long) -> const int | `const int iProposal = lua_tointeger(L, 2);` | a | L1 |
| 473 | `Lua/CvLuaLeague.cpp:578:24` | lua_Integer (long long) -> const int | `const int iNumVotes = lua_tointeger(L, 4);` | a | L1 |
| 474 | `Lua/CvLuaLeague.cpp:579:22` | lua_Integer (long long) -> const int | `const int iChoice = lua_tointeger(L, 5);` | a | L1 |
| 475 | `Lua/CvLuaLeague.cpp:590:24` | lua_Integer (long long) -> const int | `const int iNumVotes = lua_tointeger(L, 3);` | a | L1 |
| 476 | `Lua/CvLuaLeague.cpp:757:28` | lua_Integer (long long) -> const int | `const int iResolutionID = lua_tointeger(L, 3);` | a | L1 |
| 477 | `Lua/CvLuaLeague.cpp:758:30` | lua_Integer (long long) -> const int | `const int iProposerChoice = lua_tointeger(L, 4);` | a | L1 |
| 478 | `Lua/CvLuaLeague.cpp:772:28` | lua_Integer (long long) -> const int | `const int iResolutionID = lua_tointeger(L, 4);` | a | L1 |
| 479 | `Lua/CvLuaLeague.cpp:773:30` | lua_Integer (long long) -> const int | `const int iProposerChoice = lua_tointeger(L, 5);` | a | L1 |
| 480 | `Lua/CvLuaLeague.cpp:808:20` | lua_Integer (long long) -> const int | `const int iTier = lua_tointeger(L, 2);` | a | L1 |
| 481 | `Lua/CvLuaMap.cpp:101:8` | lua_Integer (long long) -> int | `it = lua_tointeger(L, 2);` | a | L1 |
| 482 | `Lua/CvLuaMap.cpp:217:16` | lua_Integer (long long) -> const int | `const int x = lua_tointeger(L, 1);` | a | L1 |
| 483 | `Lua/CvLuaMap.cpp:218:16` | lua_Integer (long long) -> const int | `const int y = lua_tointeger(L, 2);` | a | L1 |
| 484 | `Lua/CvLuaMap.cpp:232:21` | lua_Integer (long long) -> const int | `const int iIndex = lua_tointeger(L, 1);` | a | L1 |
| 485 | `Lua/CvLuaMap.cpp:254:8` | lua_Integer (long long) -> int | `dx = lua_tointeger(L, 1);` | a | L1 |
| 486 | `Lua/CvLuaMap.cpp:255:8` | lua_Integer (long long) -> int | `dy = lua_tointeger(L, 2);` | a | L1 |
| 487 | `Lua/CvLuaMap.cpp:259:7` | lua_Integer (long long) -> int | `x = lua_tointeger(L, 1);` | a | L1 |
| 488 | `Lua/CvLuaMap.cpp:260:7` | lua_Integer (long long) -> int | `y = lua_tointeger(L, 2);` | a | L1 |
| 489 | `Lua/CvLuaMap.cpp:261:8` | lua_Integer (long long) -> int | `dx = lua_tointeger(L, 3);` | a | L1 |
| 490 | `Lua/CvLuaMap.cpp:262:8` | lua_Integer (long long) -> int | `dy = lua_tointeger(L, 4);` | a | L1 |
| 491 | `Lua/CvLuaMap.cpp:273:18` | lua_Integer (long long) -> const int | `const int iID = lua_tointeger(L, 1);` | a | L1 |
| 492 | `Lua/CvLuaMap.cpp:335:10` | lua_Integer (long long) -> int | `int x = lua_tointeger(L, 1);` | a | L1 |
| 493 | `Lua/CvLuaMap.cpp:336:10` | lua_Integer (long long) -> int | `int y = lua_tointeger(L, 2);` | a | L1 |
| 494 | `Lua/CvLuaMap.cpp:365:22` | lua_Integer (long long) -> const int | `const int max_num = luaL_checkinteger(L, 1);` | a | L1 |
| 495 | `Lua/CvLuaMap.cpp:388:12` | lua_Integer (long long) -> int | `int iX1 = lua_tointeger(L, 1);` | a | L1 |
| 496 | `Lua/CvLuaMap.cpp:389:12` | lua_Integer (long long) -> int | `int iY1 = lua_tointeger(L, 2);` | a | L1 |
| 497 | `Lua/CvLuaMap.cpp:390:12` | lua_Integer (long long) -> int | `int iX2 = lua_tointeger(L, 3);` | a | L1 |
| 498 | `Lua/CvLuaMap.cpp:391:12` | lua_Integer (long long) -> int | `int iY2 = lua_tointeger(L, 4);` | a | L1 |
| 499 | `Lua/CvLuaMap.cpp:401:11` | lua_Integer (long long) -> int | `int iX = lua_tointeger(L, 1);` | a | L1 |
| 500 | `Lua/CvLuaMap.cpp:402:11` | lua_Integer (long long) -> int | `int iY = lua_tointeger(L, 2);` | a | L1 |
| 501 | `Lua/CvLuaMap.cpp:403:19` | lua_Integer (long long) -> int | `int iDeltaHexX = lua_tointeger(L, 3);` | a | L1 |
| 502 | `Lua/CvLuaMap.cpp:404:19` | lua_Integer (long long) -> int | `int iDeltaHexY = lua_tointeger(L, 4);` | a | L1 |
| 503 | `Lua/CvLuaMap.cpp:405:15` | lua_Integer (long long) -> int | `int iRange = lua_tointeger(L, 5);` | a | L1 |
| 504 | `Lua/CvLuaMap.cpp:413:11` | lua_Integer (long long) -> int | `int iX = lua_tointeger(L, 1);` | a | L1 |
| 505 | `Lua/CvLuaMap.cpp:414:11` | lua_Integer (long long) -> int | `int iY = lua_tointeger(L, 2);` | a | L1 |
| 506 | `Lua/CvLuaMap.cpp:469:14` | lua_Integer (long long) -> int | `int iHint = lua_tointeger(L, 1);` | a | L1 |
| 507 | `Lua/CvLuaMap.cpp:482:18` | lua_Integer (long long) -> int | `int iLandmass = lua_tointeger(L, 1);` | a | L1 |
| 508 | `Lua/CvLuaPlayer.cpp:1545:22` | lua_Integer (long long) -> const int | `const int iDomain = lua_tointeger(L, index);` | a | L1 |
| 509 | `Lua/CvLuaPlayer.cpp:1585:16` | lua_Integer (long long) -> const int | `const int x = lua_tointeger(L, 2);` | a | L1 |
| 510 | `Lua/CvLuaPlayer.cpp:1586:16` | lua_Integer (long long) -> const int | `const int y = lua_tointeger(L, 3);` | a | L1 |
| 511 | `Lua/CvLuaPlayer.cpp:1670:20` | lua_Integer (long long) -> const int | `const int iUnit = lua_tointeger(L, idx);` | a | L1 |
| 512 | `Lua/CvLuaPlayer.cpp:1683:16` | lua_Integer (long long) -> const int | `const int x = lua_tointeger(L, 3);` | a | L1 |
| 513 | `Lua/CvLuaPlayer.cpp:1684:16` | lua_Integer (long long) -> const int | `const int y = lua_tointeger(L, 4);` | a | L1 |
| 514 | `Lua/CvLuaPlayer.cpp:1703:26` | lua_Integer (long long) -> const int | `const int iNameOffset = lua_tointeger(L, 3);` | a | L1 |
| 515 | `Lua/CvLuaPlayer.cpp:1704:16` | lua_Integer (long long) -> const int | `const int x = lua_tointeger(L, 4);` | a | L1 |
| 516 | `Lua/CvLuaPlayer.cpp:1705:16` | lua_Integer (long long) -> const int | `const int y = lua_tointeger(L, 5);` | a | L1 |
| 517 | `Lua/CvLuaPlayer.cpp:1724:16` | lua_Integer (long long) -> const int | `const int x = lua_tointeger(L, 4);` | a | L1 |
| 518 | `Lua/CvLuaPlayer.cpp:1725:16` | lua_Integer (long long) -> const int | `const int y = lua_tointeger(L, 5);` | a | L1 |
| 519 | `Lua/CvLuaPlayer.cpp:1989:22` | lua_Integer (long long) -> const int | `const int iCityID = lua_tointeger(L, 2);` | a | L1 |
| 520 | `Lua/CvLuaPlayer.cpp:1990:21` | lua_Integer (long long) -> const int | `const int iPlotX = lua_tointeger(L, 3);` | a | L1 |
| 521 | `Lua/CvLuaPlayer.cpp:1991:21` | lua_Integer (long long) -> const int | `const int iPlotY = lua_tointeger(L, 4);` | a | L1 |
| 522 | `Lua/CvLuaPlayer.cpp:2013:22` | lua_Integer (long long) -> const int | `const int iCityID = lua_tointeger(L, 2);` | a | L1 |
| 523 | `Lua/CvLuaPlayer.cpp:2014:26` | lua_Integer (long long) -> const int | `const int iSpecialist = lua_tointeger(L, 3);` | a | L1 |
| 524 | `Lua/CvLuaPlayer.cpp:2033:22` | lua_Integer (long long) -> const int | `const int iCityID = lua_tointeger(L, 2);` | a | L1 |
| 525 | `Lua/CvLuaPlayer.cpp:2037:18` | lua_Integer (long long) -> const int | `const int iR1 = lua_tointeger(L, 3);` | a | L1 |
| 526 | `Lua/CvLuaPlayer.cpp:2038:18` | lua_Integer (long long) -> const int | `const int iR2 = lua_tointeger(L, 4);` | a | L1 |
| 527 | `Lua/CvLuaPlayer.cpp:2040:18` | lua_Integer (long long) -> const int | `const int iR3 = lua_tointeger(L, 5);` | a | L1 |
| 528 | `Lua/CvLuaPlayer.cpp:2041:18` | lua_Integer (long long) -> const int | `const int iR4 = lua_tointeger(L, 6);` | a | L1 |
| 529 | `Lua/CvLuaPlayer.cpp:2141:18` | lua_Integer (long long) -> int | `int iDiscover = lua_tointeger(L, 2);` | a | L1 |
| 530 | `Lua/CvLuaPlayer.cpp:2364:16` | lua_Integer (long long) -> int | `int iChange = lua_tointeger(L, 2);` | a | L1 |
| 531 | `Lua/CvLuaPlayer.cpp:2386:21` | lua_Integer (long long) -> const int | `const int iValue = lua_tointeger(L, 3);` | a | L1 |
| 532 | `Lua/CvLuaPlayer.cpp:2542:24` | lua_Integer (long long) -> const int | `const int iQuantity = lua_tointeger(L, 4);` | a | L1 |
| 533 | `Lua/CvLuaPlayer.cpp:2559:17` | lua_Integer (long long) -> const int | `const int iX = lua_tointeger(L, 2);` | a | L1 |
| 534 | `Lua/CvLuaPlayer.cpp:2560:17` | lua_Integer (long long) -> const int | `const int iY = lua_tointeger(L, 3);` | a | L1 |
| 535 | `Lua/CvLuaPlayer.cpp:2587:24` | lua_Integer (long long) -> const int | `const int iBuilding = lua_tointeger(L, 2);` | a | L1 |
| 536 | `Lua/CvLuaPlayer.cpp:2601:23` | lua_Integer (long long) -> const int | `const int iProject = lua_tointeger(L, 2);` | a | L1 |
| 537 | `Lua/CvLuaPlayer.cpp:2949:21` | lua_Integer (long long) -> const int | `const int iValue = lua_tointeger(L, 2);` | a | L1 |
| 538 | `Lua/CvLuaPlayer.cpp:2959:21` | lua_Integer (long long) -> const int | `const int iValue = lua_tointeger(L, 2);` | a | L1 |
| 539 | `Lua/CvLuaPlayer.cpp:3369:21` | lua_Integer (long long) -> const int | `const int iValue = lua_tointeger(L, 2);` | a | L1 |
| 540 | `Lua/CvLuaPlayer.cpp:3431:15` | lua_Integer (long long) -> int | `int iValue = lua_tointeger(L, 2);` | a | L1 |
| 541 | `Lua/CvLuaPlayer.cpp:3470:15` | lua_Integer (long long) -> int | `int iValue = lua_tointeger(L, 2);` | a | L1 |
| 542 | `Lua/CvLuaPlayer.cpp:3480:15` | lua_Integer (long long) -> int | `int iValue = lua_tointeger(L, 2);` | a | L1 |
| 543 | `Lua/CvLuaPlayer.cpp:3759:18` | lua_Integer (long long) -> int | `int iNewValue = lua_tointeger(L, 2);` | a | L1 |
| 544 | `Lua/CvLuaPlayer.cpp:3774:16` | lua_Integer (long long) -> int | `int iChange = lua_tointeger(L, 2);` | a | L1 |
| 545 | `Lua/CvLuaPlayer.cpp:4212:23` | lua_Integer (long long) -> int | `int iBaseInfluence = lua_tointeger(L, 2);` | a | L1 |
| 546 | `Lua/CvLuaPlayer.cpp:4233:23` | lua_Integer (long long) -> int | `int iBaseInfluence = lua_tointeger(L, 3);` | a | L1 |
| 547 | `Lua/CvLuaPlayer.cpp:4274:11` | lua_Integer (long long) -> int | `int iX = lua_tointeger(L, 2);` | a | L1 |
| 548 | `Lua/CvLuaPlayer.cpp:4275:11` | lua_Integer (long long) -> int | `int iY = lua_tointeger(L, 3);` | a | L1 |
| 549 | `Lua/CvLuaPlayer.cpp:4295:11` | lua_Integer (long long) -> int | `int iX = lua_tointeger(L, 2);` | a | L1 |
| 550 | `Lua/CvLuaPlayer.cpp:4296:11` | lua_Integer (long long) -> int | `int iY = lua_tointeger(L, 3);` | a | L1 |
| 551 | `Lua/CvLuaPlayer.cpp:4365:15` | lua_Integer (long long) -> int | `int iValue = lua_tointeger(L, 2);` | a | L1 |
| 552 | `Lua/CvLuaPlayer.cpp:5019:21` | lua_Integer (long long) -> const int | `const int iValue = lua_tointeger(L, 3);` | a | L1 |
| 553 | `Lua/CvLuaPlayer.cpp:5027:22` | lua_Integer (long long) -> const int | `const int iChange = lua_tointeger(L, 3);` | a | L1 |
| 554 | `Lua/CvLuaPlayer.cpp:5324:28` | size_type (unsigned long long) -> int | `for (int i = astrToolTips.size(); i > 0; i--)` | a | container size of game data or entities |
| 555 | `Lua/CvLuaPlayer.cpp:5346:28` | size_type (unsigned long long) -> int | `for (int i = astrToolTips.size(); i > 0; i--)` | a | container size of game data or entities |
| 556 | `Lua/CvLuaPlayer.cpp:7784:18` | lua_Integer (long long) -> const int | `const int iID = lua_tointeger(L, 2);` | a | L1 |
| 557 | `Lua/CvLuaPlayer.cpp:7796:21` | lua_Integer (long long) -> const int | `const int iLevel = lua_tointeger(L, 2);` | a | L1 |
| 558 | `Lua/CvLuaPlayer.cpp:7824:21` | lua_Integer (long long) -> const int | `const int iLevel = lua_tointeger(L, 3);` | a | L1 |
| 559 | `Lua/CvLuaPlayer.cpp:7825:21` | lua_Integer (long long) -> const int | `const int iIndex = lua_tointeger(L, 4);` | a | L1 |
| 560 | `Lua/CvLuaPlayer.cpp:7906:21` | lua_Integer (long long) -> const int | `const int iValue = lua_tointeger(L, 2);` | a | L1 |
| 561 | `Lua/CvLuaPlayer.cpp:7915:21` | lua_Integer (long long) -> const int | `const int iValue = lua_tointeger(L, 2);` | a | L1 |
| 562 | `Lua/CvLuaPlayer.cpp:7966:21` | lua_Integer (long long) -> const int | `const int iTurns = lua_tointeger(L, 2);` | a | L1 |
| 563 | `Lua/CvLuaPlayer.cpp:8090:16` | lua_Integer (long long) -> const int | `const int x = lua_tointeger(L, 3);` | a | L1 |
| 564 | `Lua/CvLuaPlayer.cpp:8091:16` | lua_Integer (long long) -> const int | `const int y = lua_tointeger(L, 4);` | a | L1 |
| 565 | `Lua/CvLuaPlayer.cpp:8811:16` | lua_Integer (long long) -> int | `int iChange = lua_tointeger(L, 2);` | a | L1 |
| 566 | `Lua/CvLuaPlayer.cpp:8826:16` | lua_Integer (long long) -> int | `int iChange = lua_tointeger(L, 2);` | a | L1 |
| 567 | `Lua/CvLuaPlayer.cpp:8841:16` | lua_Integer (long long) -> int | `int iChange = lua_tointeger(L, 2);` | a | L1 |
| 568 | `Lua/CvLuaPlayer.cpp:8856:16` | lua_Integer (long long) -> int | `int iChange = lua_tointeger(L, 2);` | a | L1 |
| 569 | `Lua/CvLuaPlayer.cpp:8871:16` | lua_Integer (long long) -> int | `int iChange = lua_tointeger(L, 2);` | a | L1 |
| 570 | `Lua/CvLuaPlayer.cpp:8943:27` | lua_Integer (long long) -> const int | `const int iPersonality = lua_tointeger(L, 2);` | a | L1 |
| 571 | `Lua/CvLuaPlayer.cpp:9060:16` | lua_Integer (long long) -> int | `int iChange = lua_tointeger(L, 3);` | a | L1 |
| 572 | `Lua/CvLuaPlayer.cpp:9194:16` | lua_Integer (long long) -> int | `int iChange = lua_tointeger(L, 3);` | a | L1 |
| 573 | `Lua/CvLuaPlayer.cpp:9204:15` | lua_Integer (long long) -> int | `int iValue = lua_tointeger(L, 3);` | a | L1 |
| 574 | `Lua/CvLuaPlayer.cpp:9233:20` | lua_Integer (long long) -> const int | `const int iTurn = lua_tointeger(L, 4);` | a | L1 |
| 575 | `Lua/CvLuaPlayer.cpp:9310:24` | lua_Integer (long long) -> const int | `const int iGameTurn = lua_tointeger(L, 4);` | a | L1 |
| 576 | `Lua/CvLuaPlayer.cpp:9532:20` | lua_Integer (long long) -> const int | `const int iGold = lua_tointeger(L, 3);` | a | L1 |
| 577 | `Lua/CvLuaPlayer.cpp:9901:21` | lua_Integer (long long) -> const int | `const int iValue = lua_tointeger(L, 3);` | a | L1 |
| 578 | `Lua/CvLuaPlayer.cpp:9988:17` | lua_Integer (long long) -> const int | `const int iX = lua_tointeger(L, 3);` | a | L1 |
| 579 | `Lua/CvLuaPlayer.cpp:9989:17` | lua_Integer (long long) -> const int | `const int iY = lua_tointeger(L, 4);` | a | L1 |
| 580 | `Lua/CvLuaPlayer.cpp:10549:22` | lua_Integer (long long) -> const int | `const int iChange = lua_tointeger(L, 3);` | a | L1 |
| 581 | `Lua/CvLuaPlayer.cpp:10640:21` | lua_Integer (long long) -> const int | `const int iExtra = lua_tointeger(L, 3);` | a | L1 |
| 582 | `Lua/CvLuaPlayer.cpp:10700:26` | lua_Integer (long long) -> const int | `const int iExperience = lua_tointeger(L, 2);` | a | L1 |
| 583 | `Lua/CvLuaPlayer.cpp:10710:26` | lua_Integer (long long) -> const int | `const int iExperience = lua_tointeger(L, 2);` | a | L1 |
| 584 | `Lua/CvLuaPlayer.cpp:10748:26` | lua_Integer (long long) -> const int | `const int iExperience = lua_tointeger(L, 2);` | a | L1 |
| 585 | `Lua/CvLuaPlayer.cpp:10758:26` | lua_Integer (long long) -> const int | `const int iExperience = lua_tointeger(L, 2);` | a | L1 |
| 586 | `Lua/CvLuaPlayer.cpp:10874:7` | lua_Integer (long long) -> int | `i = lua_tointeger(L, -1);` | a | L1 |
| 587 | `Lua/CvLuaPlayer.cpp:10917:11` | lua_Integer (long long) -> int | `int id = lua_tointeger(L, 2);` | a | L1 |
| 588 | `Lua/CvLuaPlayer.cpp:10951:7` | lua_Integer (long long) -> int | `i = lua_tointeger(L, -1);` | a | L1 |
| 589 | `Lua/CvLuaPlayer.cpp:10994:21` | lua_Integer (long long) -> int | `int iSquadNumber = lua_tointeger(L, lua_upvalueindex(2));` | a | L1 |
| 590 | `Lua/CvLuaPlayer.cpp:11003:7` | lua_Integer (long long) -> int | `i = lua_tointeger(L, -1);` | a | L1 |
| 591 | `Lua/CvLuaPlayer.cpp:11025:11` | lua_Integer (long long) -> int | `int id = lua_tointeger(L, 2);` | a | L1 |
| 592 | `Lua/CvLuaPlayer.cpp:11261:35` | size_type (unsigned long long) -> int | `lua_createtable(L, 0, replayData.size());` | a | Lua table size hint |
| 593 | `Lua/CvLuaPlayer.cpp:11269:40` | size_type (unsigned long long) -> int | `lua_createtable(L, it->second.size() - 1, 1);` | a | Lua table size hint |
| 594 | `Lua/CvLuaPlayer.cpp:12378:17` | lua_Integer (long long) -> int | `iExtraData = lua_tointeger(L, 8);` | a | L1 |
| 595 | `Lua/CvLuaPlayer.cpp:12383:40` | lua_Integer (long long) -> int | `lua_tointeger(L, 5),` | a | L1 |
| 596 | `Lua/CvLuaPlayer.cpp:12384:40` | lua_Integer (long long) -> int | `lua_tointeger(L, 6),` | a | L1 |
| 597 | `Lua/CvLuaPlayer.cpp:12385:40` | lua_Integer (long long) -> int | `lua_tointeger(L, 7),` | a | L1 |
| 598 | `Lua/CvLuaPlayer.cpp:12404:17` | lua_Integer (long long) -> int | `iExtraData = lua_tointeger(L, 8);` | a | L1 |
| 599 | `Lua/CvLuaPlayer.cpp:12409:40` | lua_Integer (long long) -> int | `lua_tointeger(L, 5),` | a | L1 |
| 600 | `Lua/CvLuaPlayer.cpp:12410:40` | lua_Integer (long long) -> int | `lua_tointeger(L, 6),` | a | L1 |
| 601 | `Lua/CvLuaPlayer.cpp:12411:40` | lua_Integer (long long) -> int | `lua_tointeger(L, 7),` | a | L1 |
| 602 | `Lua/CvLuaPlayer.cpp:12425:16` | lua_Integer (long long) -> int | `int iIndex = lua_tointeger(L, 2);` | a | L1 |
| 603 | `Lua/CvLuaPlayer.cpp:12447:15` | lua_Integer (long long) -> int | `int iIndex = lua_tointeger(L, 2);` | a | L1 |
| 604 | `Lua/CvLuaPlayer.cpp:12456:15` | lua_Integer (long long) -> int | `int iIndex = lua_tointeger(L, 2);` | a | L1 |
| 605 | `Lua/CvLuaPlayer.cpp:12466:15` | lua_Integer (long long) -> int | `int iIndex = lua_tointeger(L, 2);` | a | L1 |
| 606 | `Lua/CvLuaPlayer.cpp:12476:15` | lua_Integer (long long) -> int | `int iIndex = lua_tointeger(L, 2);` | a | L1 |
| 607 | `Lua/CvLuaPlayer.cpp:12486:15` | lua_Integer (long long) -> int | `int iIndex = lua_tointeger(L, 2);` | a | L1 |
| 608 | `Lua/CvLuaPlayer.cpp:12961:24` | lua_Integer (long long) -> const int | `const int iNumTechs = lua_tointeger(L, 2);` | a | L1 |
| 609 | `Lua/CvLuaPlayer.cpp:12977:27` | lua_Integer (long long) -> const int | `const int iNumPolicies = lua_tointeger(L, 2);` | a | L1 |
| 610 | `Lua/CvLuaPlayer.cpp:12986:27` | lua_Integer (long long) -> const int | `const int iNumPolicies = lua_tointeger(L, 2);` | a | L1 |
| 611 | `Lua/CvLuaPlayer.cpp:13002:25` | lua_Integer (long long) -> const int | `const int iNumTenets = lua_tointeger(L, 2);` | a | L1 |
| 612 | `Lua/CvLuaPlayer.cpp:13012:25` | lua_Integer (long long) -> const int | `const int iNumTenets = lua_tointeger(L, 2);` | a | L1 |
| 613 | `Lua/CvLuaPlayer.cpp:13029:30` | lua_Integer (long long) -> const int | `const int iNumGreatPeople = lua_tointeger(L, 2);` | a | L1 |
| 614 | `Lua/CvLuaPlayer.cpp:13038:30` | lua_Integer (long long) -> const int | `const int iNumGreatPeople = lua_tointeger(L, 2);` | a | L1 |
| 615 | `Lua/CvLuaPlayer.cpp:13054:25` | lua_Integer (long long) -> const int | `const int iNumBoosts = lua_tointeger(L, 2);` | a | L1 |
| 616 | `Lua/CvLuaPlayer.cpp:13063:25` | lua_Integer (long long) -> const int | `const int iNumBoosts = lua_tointeger(L, 2);` | a | L1 |
| 617 | `Lua/CvLuaPlayer.cpp:13079:30` | lua_Integer (long long) -> const int | `const int iNumGreatPeople = lua_tointeger(L, 2);` | a | L1 |
| 618 | `Lua/CvLuaPlayer.cpp:13088:30` | lua_Integer (long long) -> const int | `const int iNumGreatPeople = lua_tointeger(L, 2);` | a | L1 |
| 619 | `Lua/CvLuaPlayer.cpp:13517:24` | lua_Integer (long long) -> int | `int iPromotionIndex = lua_tointeger(L, 2);` | a | L1 |
| 620 | `Lua/CvLuaPlayer.cpp:13528:24` | lua_Integer (long long) -> int | `int iPromotionIndex = lua_tointeger(L, 2);` | a | L1 |
| 621 | `Lua/CvLuaPlayer.cpp:13539:24` | lua_Integer (long long) -> int | `int iPromotionIndex = lua_tointeger(L, 2);` | a | L1 |
| 622 | `Lua/CvLuaPlayer.cpp:13540:21` | lua_Integer (long long) -> const int | `const int iValue = lua_tointeger(L, 3);` | a | L1 |
| 623 | `Lua/CvLuaPlayer.cpp:13568:17` | lua_Integer (long long) -> const int | `const int iX = lua_tointeger(L, 2);` | a | L1 |
| 624 | `Lua/CvLuaPlayer.cpp:13569:17` | lua_Integer (long long) -> const int | `const int iY = lua_tointeger(L, 3);` | a | L1 |
| 625 | `Lua/CvLuaPlayer.cpp:16758:4` | lua_Integer (long long) -> int | `GET_TABLE_VALUE("VictoryCompetitiveness", pDiplo->m_iVictoryCompetitiveness);` | a | L1 |
| 626 | `Lua/CvLuaPlayer.cpp:16759:4` | lua_Integer (long long) -> int | `GET_TABLE_VALUE("WonderCompetitiveness", pDiplo->m_iWonderCompetitiveness);` | a | L1 |
| 627 | `Lua/CvLuaPlayer.cpp:16760:4` | lua_Integer (long long) -> int | `GET_TABLE_VALUE("MinorCivCompetitiveness", pDiplo->m_iMinorCivCompetitiveness);` | a | L1 |
| 628 | `Lua/CvLuaPlayer.cpp:16761:4` | lua_Integer (long long) -> int | `GET_TABLE_VALUE("Boldness", pDiplo->m_iBoldness);` | a | L1 |
| 629 | `Lua/CvLuaPlayer.cpp:16763:4` | lua_Integer (long long) -> int | `GET_TABLE_VALUE("DiploBalance", pDiplo->m_iDiploBalance);` | a | L1 |
| 630 | `Lua/CvLuaPlayer.cpp:16764:4` | lua_Integer (long long) -> int | `GET_TABLE_VALUE("WarmongerHate", pDiplo->m_iWarmongerHate);` | a | L1 |
| 631 | `Lua/CvLuaPlayer.cpp:16765:4` | lua_Integer (long long) -> int | `GET_TABLE_VALUE("Friendliness", pDiplo->m_iDoFWillingness);` | a | L1 |
| 632 | `Lua/CvLuaPlayer.cpp:16766:4` | lua_Integer (long long) -> int | `GET_TABLE_VALUE("DenounceWillingness", pDiplo->m_iDenounceWillingness);` | a | L1 |
| 633 | `Lua/CvLuaPlayer.cpp:16767:4` | lua_Integer (long long) -> int | `GET_TABLE_VALUE("WorkWithWillingness", pDiplo->m_iWorkWithWillingness);` | a | L1 |
| 634 | `Lua/CvLuaPlayer.cpp:16768:4` | lua_Integer (long long) -> int | `GET_TABLE_VALUE("WorkAgainstWillingness", pDiplo->m_iWorkAgainstWillingness);` | a | L1 |
| 635 | `Lua/CvLuaPlayer.cpp:16769:4` | lua_Integer (long long) -> int | `GET_TABLE_VALUE("Loyalty", pDiplo->m_iLoyalty);` | a | L1 |
| 636 | `Lua/CvLuaPlayer.cpp:16770:4` | lua_Integer (long long) -> int | `GET_TABLE_VALUE("Forgiveness", pDiplo->m_iForgiveness);` | a | L1 |
| 637 | `Lua/CvLuaPlayer.cpp:16771:4` | lua_Integer (long long) -> int | `GET_TABLE_VALUE("Neediness", pDiplo->m_iNeediness);` | a | L1 |
| 638 | `Lua/CvLuaPlayer.cpp:16772:4` | lua_Integer (long long) -> int | `GET_TABLE_VALUE("Meanness", pDiplo->m_iMeanness);` | a | L1 |
| 639 | `Lua/CvLuaPlayer.cpp:16774:4` | lua_Integer (long long) -> int | `GET_TABLE_VALUE("Chattiness", pDiplo->m_iChattiness);` | a | L1 |
| 640 | `Lua/CvLuaPlayer.cpp:16777:4` | lua_Integer (long long) -> int | `GET_TABLE_VALUE("WarBias", pDiplo->m_aiMajorCivApproachBiases[CIV_APPROACH_WAR]);` | a | L1 |
| 641 | `Lua/CvLuaPlayer.cpp:16778:4` | lua_Integer (long long) -> int | `GET_TABLE_VALUE("HostileBias", pDiplo->m_aiMajorCivApproachBiases[CIV_APPROACH_HOSTILE]);` | a | L1 |
| 642 | `Lua/CvLuaPlayer.cpp:16779:4` | lua_Integer (long long) -> int | `GET_TABLE_VALUE("DeceptiveBias", pDiplo->m_aiMajorCivApproachBiases[CIV_APPROACH_DECEPTIVE]);` | a | L1 |
| 643 | `Lua/CvLuaPlayer.cpp:16780:4` | lua_Integer (long long) -> int | `GET_TABLE_VALUE("GuardedBias", pDiplo->m_aiMajorCivApproachBiases[CIV_APPROACH_GUARDED]);` | a | L1 |
| 644 | `Lua/CvLuaPlayer.cpp:16781:4` | lua_Integer (long long) -> int | `GET_TABLE_VALUE("AfraidBias", pDiplo->m_aiMajorCivApproachBiases[CIV_APPROACH_AFRAID]);` | a | L1 |
| 645 | `Lua/CvLuaPlayer.cpp:16782:4` | lua_Integer (long long) -> int | `GET_TABLE_VALUE("FriendlyBias", pDiplo->m_aiMajorCivApproachBiases[CIV_APPROACH_FRIENDLY]);` | a | L1 |
| 646 | `Lua/CvLuaPlayer.cpp:16783:4` | lua_Integer (long long) -> int | `GET_TABLE_VALUE("NeutralBias", pDiplo->m_aiMajorCivApproachBiases[CIV_APPROACH_NEUTRAL]);` | a | L1 |
| 647 | `Lua/CvLuaPlayer.cpp:16786:4` | lua_Integer (long long) -> int | `GET_TABLE_VALUE("MinorCivWarBias", pDiplo->m_iMinorCivWarBias);` | a | L1 |
| 648 | `Lua/CvLuaPlayer.cpp:16787:4` | lua_Integer (long long) -> int | `GET_TABLE_VALUE("MinorCivHostileBias", pDiplo->m_iMinorCivHostileBias);` | a | L1 |
| 649 | `Lua/CvLuaPlayer.cpp:16788:4` | lua_Integer (long long) -> int | `GET_TABLE_VALUE("MinorCivNeutralBias", pDiplo->m_iMinorCivNeutralBias);` | a | L1 |
| 650 | `Lua/CvLuaPlayer.cpp:16789:4` | lua_Integer (long long) -> int | `GET_TABLE_VALUE("MinorCivFriendlyBias", pDiplo->m_iMinorCivFriendlyBias);` | a | L1 |
| 651 | `Lua/CvLuaPlayer.cpp:16873:24` | lua_Integer (long long) -> const int | `const int iDuration = lua_tointeger(L, 5);` | a | L1 |
| 652 | `Lua/CvLuaPlayer.cpp:16976:18` | lua_Integer (long long) -> int | `int iSpyIndex = lua_tointeger(L, 3);` | a | L1 |
| 653 | `Lua/CvLuaPlayer.cpp:17000:18` | lua_Integer (long long) -> int | `int iSpyIndex = lua_tointeger(L, 2);` | a | L1 |
| 654 | `Lua/CvLuaPlayer.cpp:17013:18` | lua_Integer (long long) -> int | `int iSpyIndex = lua_tointeger(L, 3);` | a | L1 |
| 655 | `Lua/CvLuaPlayer.cpp:17324:18` | lua_Integer (long long) -> int | `int iSpyIndex = lua_tointeger(L, 2);` | a | L1 |
| 656 | `Lua/CvLuaPlayer.cpp:17333:18` | lua_Integer (long long) -> int | `int iSpyIndex = lua_tointeger(L, 2);` | a | L1 |
| 657 | `Lua/CvLuaPlayer.cpp:17343:18` | lua_Integer (long long) -> int | `int iSpyIndex = lua_tointeger(L, 2);` | a | L1 |
| 658 | `Lua/CvLuaPlayer.cpp:17353:18` | lua_Integer (long long) -> int | `int iSpyIndex = lua_tointeger(L, 2);` | a | L1 |
| 659 | `Lua/CvLuaPlayer.cpp:17364:18` | lua_Integer (long long) -> int | `int iSpyIndex = lua_tointeger(L, 2);` | a | L1 |
| 660 | `Lua/CvLuaPlayer.cpp:17375:18` | lua_Integer (long long) -> int | `int iSpyIndex = lua_tointeger(L, 2);` | a | L1 |
| 661 | `Lua/CvLuaPlayer.cpp:17387:20` | lua_Integer (long long) -> uint (unsigned int) | `uint uiSpyIndex = luaL_checkinteger(L, 2);` | a | L1; Lua arg into `uint` (spy index): small non-negative id |
| 662 | `Lua/CvLuaPlayer.cpp:17476:15` | lua_Integer (long long) -> uint (unsigned int) | `uint spyID = luaL_checkinteger(L, 3);` | a | L1; Lua arg into `uint` (spy index): small non-negative id |
| 663 | `Lua/CvLuaPlayer.cpp:17487:16` | lua_Integer (long long) -> int | `int iPlayer = lua_tointeger(L, 2);` | a | L1 |
| 664 | `Lua/CvLuaPlayer.cpp:17502:65` | size_type (unsigned long long) -> int | `for(int i = pkPlayerEspionage->m_aIntrigueNotificationMessages.size(); i > 0; i--)` | a | container size of game data or entities |
| 665 | `Lua/CvLuaPlayer.cpp:17530:16` | lua_Integer (long long) -> int | `int iPlayer = lua_tointeger(L, 2);` | a | L1 |
| 666 | `Lua/CvLuaPlayer.cpp:17543:16` | lua_Integer (long long) -> int | `int iPlayer = lua_tointeger(L, 2);` | a | L1 |
| 667 | `Lua/CvLuaPlayer.cpp:17762:21` | lua_Integer (long long) -> const int | `const int iValue = lua_tointeger(L, 3);` | a | L1 |
| 668 | `Lua/CvLuaPlayer.cpp:18133:29` | lua_Integer (long long) -> const int | `const int iBuildingClass = lua_tointeger(L, 2);` | a | L1 |
| 669 | `Lua/CvLuaPlayer.cpp:18156:21` | lua_Integer (long long) -> const int | `const int iValue = lua_tointeger(L, 2);` | a | L1 |
| 670 | `Lua/CvLuaPlayer.cpp:18164:21` | lua_Integer (long long) -> const int | `const int iValue = lua_tointeger(L, 2);` | a | L1 |
| 671 | `Lua/CvLuaPlayer.cpp:18179:21` | lua_Integer (long long) -> const int | `const int iValue = lua_tointeger(L, 2);` | a | L1 |
| 672 | `Lua/CvLuaPlayer.cpp:18187:21` | lua_Integer (long long) -> const int | `const int iValue = lua_tointeger(L, 2);` | a | L1 |
| 673 | `Lua/CvLuaPlayer.cpp:18202:21` | lua_Integer (long long) -> const int | `const int iValue = lua_tointeger(L, 2);` | a | L1 |
| 674 | `Lua/CvLuaPlayer.cpp:18247:21` | lua_Integer (long long) -> const int | `const int iValue = lua_tointeger(L, 2);` | a | L1 |
| 675 | `Lua/CvLuaPlayer.cpp:18255:21` | lua_Integer (long long) -> const int | `const int iValue = lua_tointeger(L, 2);` | a | L1 |
| 676 | `Lua/CvLuaPlayer.cpp:18270:21` | lua_Integer (long long) -> const int | `const int iValue = lua_tointeger(L, 2);` | a | L1 |
| 677 | `Lua/CvLuaPlayer.cpp:18293:21` | lua_Integer (long long) -> const int | `const int iValue = lua_tointeger(L, 2);` | a | L1 |
| 678 | `Lua/CvLuaPlayer.cpp:18301:21` | lua_Integer (long long) -> const int | `const int iValue = lua_tointeger(L, 2);` | a | L1 |
| 679 | `Lua/CvLuaPlayer.cpp:18317:21` | lua_Integer (long long) -> const int | `const int iValue = lua_tointeger(L, 2);` | a | L1 |
| 680 | `Lua/CvLuaPlayer.cpp:18325:21` | lua_Integer (long long) -> const int | `const int iValue = lua_tointeger(L, 2);` | a | L1 |
| 681 | `Lua/CvLuaPlayer.cpp:18341:21` | lua_Integer (long long) -> const int | `const int iValue = lua_tointeger(L, 2);` | a | L1 |
| 682 | `Lua/CvLuaPlayer.cpp:18349:21` | lua_Integer (long long) -> const int | `const int iValue = lua_tointeger(L, 2);` | a | L1 |
| 683 | `Lua/CvLuaPlayer.cpp:18365:21` | lua_Integer (long long) -> const int | `const int iValue = lua_tointeger(L, 2);` | a | L1 |
| 684 | `Lua/CvLuaPlayer.cpp:18373:21` | lua_Integer (long long) -> const int | `const int iValue = lua_tointeger(L, 2);` | a | L1 |
| 685 | `Lua/CvLuaPlayer.cpp:18389:21` | lua_Integer (long long) -> const int | `const int iValue = lua_tointeger(L, 2);` | a | L1 |
| 686 | `Lua/CvLuaPlayer.cpp:18428:18` | lua_Integer (long long) -> const int | `const int iID = lua_tointeger(L, 2);` | a | L1 |
| 687 | `Lua/CvLuaPlayer.cpp:18436:18` | lua_Integer (long long) -> const int | `const int iID = lua_tointeger(L, 2);` | a | L1 |
| 688 | `Lua/CvLuaPlayer.cpp:18437:21` | lua_Integer (long long) -> const int | `const int iValue = lua_tointeger(L, 3);` | a | L1 |
| 689 | `Lua/CvLuaPlayer.cpp:18468:18` | lua_Integer (long long) -> const int | `const int iID = lua_tointeger(L, 2);` | a | L1 |
| 690 | `Lua/CvLuaPlayer.cpp:18490:21` | lua_Integer (long long) -> const int | `const int iValue = lua_tointeger(L, 2);` | a | L1 |
| 691 | `Lua/CvLuaPlayer.cpp:18531:21` | lua_Integer (long long) -> const int | `const int iValue = lua_tointeger(L, 3);` | a | L1 |
| 692 | `Lua/CvLuaPlayer.cpp:18587:21` | lua_Integer (long long) -> const int | `const int iYield = lua_tointeger(L, 3);` | a | L1 |
| 693 | `Lua/CvLuaPlayer.cpp:18619:20` | lua_Integer (long long) -> const int | `const int Turns = lua_tointeger(L, 2);` | a | L1 |
| 694 | `Lua/CvLuaPlayer.cpp:18644:25` | lua_Integer (long long) -> const int | `const int uiSpyIndex = lua_tointeger(L, 3);` | a | L1 |
| 695 | `Lua/CvLuaPlayer.cpp:18780:21` | lua_Integer (long long) -> int | `int iNewCooldown = lua_tointeger(L, 3);` | a | L1 |
| 696 | `Lua/CvLuaPlayer.cpp:18788:21` | lua_Integer (long long) -> int | `int iNewCooldown = lua_tointeger(L, 3);` | a | L1 |
| 697 | `Lua/CvLuaPlayer.cpp:19351:22` | size_t (unsigned long long) -> const int | `const int iLength = lua_objlen(L, 9);` | a | Lua table length |
| 698 | `Lua/CvLuaPlayer.cpp:19355:27` | lua_Integer (long long) -> const int | `const int iUnitCombat = lua_tointeger(L, -1);` | a | L1 |
| 699 | `Lua/CvLuaPlayer.cpp:19403:30` | lua_Integer (long long) -> const int | `const int iGrandStrategy = lua_tointeger(L, 2);` | a | L1 |
| 700 | `Lua/CvLuaPlayer.cpp:19518:15` | size_t (unsigned long long) -> int | `int iLen = lua_objlen(L, 2);` | a | Lua table length |
| 701 | `Lua/CvLuaPlayer.cpp:19526:28` | lua_Integer (long long) -> const int | `const int iStrategy = lua_tointeger(L, -1);` | a | L1 |
| 702 | `Lua/CvLuaPlayer.cpp:19604:15` | size_t (unsigned long long) -> int | `int iLen = lua_objlen(L, 2);` | a | Lua table length |
| 703 | `Lua/CvLuaPlayer.cpp:19612:28` | lua_Integer (long long) -> const int | `const int iStrategy = lua_tointeger(L, -1);` | a | L1 |
| 704 | `Lua/CvLuaPlot.cpp:397:26` | lua_Integer (long long) -> const int | `const int featureType = lua_tointeger(L, 2);` | a | L1 |
| 705 | `Lua/CvLuaPlot.cpp:469:26` | lua_Integer (long long) -> const int | `const int featureType = lua_tointeger(L, 2);` | a | L1 |
| 706 | `Lua/CvLuaPlot.cpp:479:26` | lua_Integer (long long) -> const int | `const int terrainType = lua_tointeger(L, 2);` | a | L1 |
| 707 | `Lua/CvLuaPlot.cpp:1468:31` | lua_Integer (long long) -> const int | `const int iAcquiringCityID = lua_tointeger(L, 3);` | a | L1 |
| 708 | `Lua/CvLuaPlot.cpp:1888:22` | lua_Integer (long long) -> const int | `const int iChange = lua_tointeger(L, 3);` | a | L1 |
| 709 | `Lua/CvLuaPlot.cpp:1889:28` | lua_Integer (long long) -> const int | `const int eSeeInvisible = lua_tointeger(L, 4);` | a | L1 |
| 710 | `Lua/CvLuaPlot.cpp:2064:21` | lua_Integer (long long) -> const int | `const int iIndex = lua_tointeger(L, 2);` | a | L1 |
| 711 | `Lua/CvLuaPlot.cpp:2075:23` | lua_Integer (long long) -> const int | `const int iLayerID = luaL_optinteger(L, 2, -1);` | a | L1 |
| 712 | `Lua/CvLuaPlot.cpp:2085:21` | lua_Integer (long long) -> const int | `const int iIndex = lua_tointeger(L, 2);` | a | L1 |
| 713 | `Lua/CvLuaPlot.cpp:2086:23` | lua_Integer (long long) -> const int | `const int iLayerID = luaL_optinteger(L, 3, -1);` | a | L1 |
| 714 | `Lua/CvLuaPlot.cpp:2155:15` | lua_Integer (long long) -> int | `int iRange = lua_tointeger(L, 4);` | a | L1 |
| 715 | `Lua/CvLuaPlot.cpp:2157:21` | lua_Integer (long long) -> int | `int iSeeThrough = luaL_optinteger(L, 6, 0);` | a | L1 |
| 716 | `Lua/CvLuaPlot.cpp:2192:30` | lua_Integer (long long) -> const int | `const int iContinentType = lua_tointeger(L, 2);` | a | L1 |
| 717 | `Lua/CvLuaPlot.cpp:2346:18` | lua_Integer (long long) -> const int | `const int iID = lua_tointeger(L, idx);` | a | L1 |
| 718 | `Lua/CvLuaUnit.cpp:744:22` | lua_Integer (long long) -> const int | `const int iData1		= lua_tointeger(L, 3);` | a | L1 |
| 719 | `Lua/CvLuaUnit.cpp:745:22` | lua_Integer (long long) -> const int | `const int iData2		= lua_tointeger(L, 4);` | a | L1 |
| 720 | `Lua/CvLuaUnit.cpp:777:24` | lua_Integer (long long) -> const int | `const int iMaxTurns = lua_tointeger(L, 3);` | a | L1 |
| 721 | `Lua/CvLuaUnit.cpp:1179:24` | lua_Integer (long long) -> const int | `const int iStrength = lua_tointeger(L, 2);` | a | L1 |
| 722 | `Lua/CvLuaUnit.cpp:1180:32` | lua_Integer (long long) -> const int | `const int iOpponentStrength = lua_tointeger(L, 3);` | a | L1 |
| 723 | `Lua/CvLuaUnit.cpp:1181:29` | lua_Integer (long long) -> const int | `const int iCurrentDamage = lua_tointeger(L, 4);` | a | L1 |
| 724 | `Lua/CvLuaUnit.cpp:1196:24` | lua_Integer (long long) -> const int | `const int iStrength = lua_tointeger(L, 2);` | a | L1 |
| 725 | `Lua/CvLuaUnit.cpp:1197:32` | lua_Integer (long long) -> const int | `const int iOpponentStrength = lua_tointeger(L, 3);` | a | L1 |
| 726 | `Lua/CvLuaUnit.cpp:1217:24` | lua_Integer (long long) -> const int | `const int iStrength = lua_tointeger(L, 2);` | a | L1 |
| 727 | `Lua/CvLuaUnit.cpp:1239:17` | lua_Integer (long long) -> const int | `const int iX = lua_tointeger(L, 3);` | a | L1 |
| 728 | `Lua/CvLuaUnit.cpp:1240:17` | lua_Integer (long long) -> const int | `const int iY = lua_tointeger(L, 4);` | a | L1 |
| 729 | `Lua/CvLuaUnit.cpp:1264:17` | lua_Integer (long long) -> const int | `const int iX = lua_tointeger(L, 3);` | a | L1 |
| 730 | `Lua/CvLuaUnit.cpp:1265:17` | lua_Integer (long long) -> const int | `const int iY = lua_tointeger(L, 4);` | a | L1 |
| 731 | `Lua/CvLuaUnit.cpp:1276:17` | lua_Integer (long long) -> const int | `const int iX = lua_tointeger(L, 2);` | a | L1 |
| 732 | `Lua/CvLuaUnit.cpp:1277:17` | lua_Integer (long long) -> const int | `const int iY = lua_tointeger(L, 3);` | a | L1 |
| 733 | `Lua/CvLuaUnit.cpp:1589:16` | lua_Integer (long long) -> const int | `const int x = lua_tointeger(L, 3);` | a | L1 |
| 734 | `Lua/CvLuaUnit.cpp:1590:16` | lua_Integer (long long) -> const int | `const int y = lua_tointeger(L, 4);` | a | L1 |
| 735 | `Lua/CvLuaUnit.cpp:1613:16` | lua_Integer (long long) -> const int | `const int x = lua_tointeger(L, 3);` | a | L1 |
| 736 | `Lua/CvLuaUnit.cpp:1614:16` | lua_Integer (long long) -> const int | `const int y = lua_tointeger(L, 4);` | a | L1 |
| 737 | `Lua/CvLuaUnit.cpp:1647:16` | lua_Integer (long long) -> const int | `const int x = lua_tointeger(L, 2);` | a | L1 |
| 738 | `Lua/CvLuaUnit.cpp:1648:16` | lua_Integer (long long) -> const int | `const int y = lua_tointeger(L, 3);` | a | L1 |
| 739 | `Lua/CvLuaUnit.cpp:1665:16` | lua_Integer (long long) -> const int | `const int x = lua_tointeger(L, 2);` | a | L1 |
| 740 | `Lua/CvLuaUnit.cpp:1666:16` | lua_Integer (long long) -> const int | `const int y = lua_tointeger(L, 3);` | a | L1 |
| 741 | `Lua/CvLuaUnit.cpp:1670:18` | lua_Integer (long long) -> const int | `const int sx = lua_tointeger(L, 4);` | a | L1 |
| 742 | `Lua/CvLuaUnit.cpp:1671:18` | lua_Integer (long long) -> const int | `const int sy = lua_tointeger(L, 5);` | a | L1 |
| 743 | `Lua/CvLuaUnit.cpp:1691:16` | lua_Integer (long long) -> const int | `const int x = lua_tointeger(L, 2);` | a | L1 |
| 744 | `Lua/CvLuaUnit.cpp:1692:16` | lua_Integer (long long) -> const int | `const int y = lua_tointeger(L, 3);` | a | L1 |
| 745 | `Lua/CvLuaUnit.cpp:1720:16` | lua_Integer (long long) -> const int | `const int x = lua_tointeger(L, 3);` | a | L1 |
| 746 | `Lua/CvLuaUnit.cpp:1721:16` | lua_Integer (long long) -> const int | `const int y = lua_tointeger(L, 4);` | a | L1 |
| 747 | `Lua/CvLuaUnit.cpp:1744:16` | lua_Integer (long long) -> const int | `const int x = lua_tointeger(L, 3);` | a | L1 |
| 748 | `Lua/CvLuaUnit.cpp:1745:16` | lua_Integer (long long) -> const int | `const int y = lua_tointeger(L, 4);` | a | L1 |
| 749 | `Lua/CvLuaUnit.cpp:2198:22` | lua_Integer (long long) -> const int | `const int iUnitId = lua_tointeger(L, 3);` | a | L1 |
| 750 | `Lua/CvLuaUnit.cpp:2210:22` | lua_Integer (long long) -> const int | `const int iUnitId = lua_tointeger(L, 2);` | a | L1 |
| 751 | `Lua/CvLuaUnit.cpp:2243:28` | lua_Integer (long long) -> const int | `const int iLeaderUnitId = lua_tointeger(L, 3);` | a | L1 |
| 752 | `Lua/CvLuaUnit.cpp:2255:28` | lua_Integer (long long) -> const int | `const int iLeaderUnitId = lua_tointeger(L, 3);` | a | L1 |
| 753 | `Lua/CvLuaUnit.cpp:2655:24` | lua_Integer (long long) -> const int | `const int iNewValue = lua_tointeger(L, 2);` | a | L1 |
| 754 | `Lua/CvLuaUnit.cpp:3039:21` | lua_Integer (long long) -> const int | `const int iValue = lua_tointeger(L, 2);` | a | L1 |
| 755 | `Lua/CvLuaUnit.cpp:3048:21` | lua_Integer (long long) -> const int | `const int iValue = lua_tointeger(L, 2);` | a | L1 |
| 756 | `Lua/CvLuaUnit.cpp:3108:22` | lua_Integer (long long) -> const int | `const int iCombat = lua_tointeger(L, 2);` | a | L1 |
| 757 | `Lua/CvLuaUnit.cpp:3334:24` | lua_Integer (long long) -> const int | `const int iStrength = lua_tointeger(L, 2);` | a | L1 |
| 758 | `Lua/CvLuaUnit.cpp:4584:22` | lua_Integer (long long) -> const int | `const int iChange = lua_tointeger(L, 2);` | a | L1 |
| 759 | `Lua/CvLuaUnit.cpp:4665:24` | lua_Integer (long long) -> const int | `const int iNewValue = lua_tointeger(L, 2);` | a | L1 |
| 760 | `Lua/CvLuaUnit.cpp:4695:16` | lua_Integer (long long) -> const int | `const int x = lua_tointeger(L, 2);` | a | L1 |
| 761 | `Lua/CvLuaUnit.cpp:4696:16` | lua_Integer (long long) -> const int | `const int y = lua_tointeger(L, 3);` | a | L1 |
| 762 | `Lua/CvLuaUnit.cpp:4710:16` | lua_Integer (long long) -> const int | `const int x = lua_tointeger(L, 2);` | a | L1 |
| 763 | `Lua/CvLuaUnit.cpp:4711:16` | lua_Integer (long long) -> const int | `const int y = lua_tointeger(L, 3);` | a | L1 |
| 764 | `Lua/CvLuaUnit.cpp:4813:24` | lua_Integer (long long) -> const int | `const int iNewValue = lua_tointeger(L, 2);` | a | L1 |
| 765 | `Lua/CvLuaUnit.cpp:4825:22` | lua_Integer (long long) -> const int | `const int iChange = lua_tointeger(L, 2);` | a | L1 |
| 766 | `Lua/CvLuaUnit.cpp:4846:24` | lua_Integer (long long) -> const int | `const int iNewValue = lua_tointeger(L, 2);` | a | L1 |
| 767 | `Lua/CvLuaUnit.cpp:4856:22` | lua_Integer (long long) -> const int | `const int iChange = lua_tointeger(L, 2);` | a | L1 |
| 768 | `Lua/CvLuaUnit.cpp:4895:24` | lua_Integer (long long) -> const int | `const int iNewValue = lua_tointeger(L, 2);` | a | L1 |
| 769 | `Lua/CvLuaUnit.cpp:4906:22` | lua_Integer (long long) -> const int | `const int iChange = lua_tointeger(L, 2);` | a | L1 |
| 770 | `Lua/CvLuaUnit.cpp:4931:32` | lua_Integer (long long) -> const int | `const int iNewValueTimes100 = lua_tointeger(L, 2);` | a | L1 |
| 771 | `Lua/CvLuaUnit.cpp:4942:30` | lua_Integer (long long) -> const int | `const int iChangeTimes100 = lua_tointeger(L, 2);` | a | L1 |
| 772 | `Lua/CvLuaUnit.cpp:4967:24` | lua_Integer (long long) -> const int | `const int iNewLevel = lua_tointeger(L, 2);` | a | L1 |
| 773 | `Lua/CvLuaUnit.cpp:4977:22` | lua_Integer (long long) -> const int | `const int iChange = lua_tointeger(L, 2);` | a | L1 |
| 774 | `Lua/CvLuaUnit.cpp:6032:23` | lua_Integer (long long) -> const int | `const int iNewData = lua_tointeger(L, 2);` | a | L1 |
| 775 | `Lua/CvLuaUnit.cpp:6285:24` | lua_Integer (long long) -> const int | `const int iReligion = lua_tointeger(L, 2);` | a | L1 |
| 776 | `Lua/CvLuaUnit.cpp:6295:24` | lua_Integer (long long) -> const int | `const int iStrength = lua_tointeger(L, 2);` | a | L1 |
| 777 | `Lua/CvLuaUnit.cpp:6305:27` | lua_Integer (long long) -> const int | `const int iSpreadsLeft = lua_tointeger(L, 2);` | a | L1 |
| 778 | `Lua/CvLuaUnit.cpp:6353:16` | lua_Integer (long long) -> const int | `const int x = lua_tointeger(L, 2);` | a | L1 |
| 779 | `Lua/CvLuaUnit.cpp:6354:16` | lua_Integer (long long) -> const int | `const int y = lua_tointeger(L, 3);` | a | L1 |
| 780 | `Lua/CvLuaUnit.cpp:6472:23` | lua_Integer (long long) -> const int | `const int iMission = lua_tointeger(L, 2);` | a | L1 |
| 781 | `Lua/CvLuaUnit.cpp:6473:21` | lua_Integer (long long) -> const int | `const int iData1 = lua_tointeger(L, 3);` | a | L1 |
| 782 | `Lua/CvLuaUnit.cpp:6474:21` | lua_Integer (long long) -> const int | `const int iData2 = lua_tointeger(L, 4);` | a | L1 |
| 783 | `Lua/CvLuaUnit.cpp:6500:14` | lua_Integer (long long) -> int | `int iTurn = lua_tointeger(L, 2);` | a | L1 |
| 784 | `CvGameCoreDLLUtil/include/CvLuaArgTemplates.h:19:10` | lua_Integer (long long) -> int | `return lua_tointeger(L, idx);` | a | L1 |
| 785 | `CvGameCoreDLLUtil/include/CvWeightedVector.h:118:18` | size_type (unsigned long long) -> unsigned int | `return m_items.size();` | a | container size returned as `int`/`uint` (game-data or entity container) |
| 786 | `CvGameCoreDLLUtil/include/CvWeightedVector.h:142:18` | size_type (unsigned long long) -> int | `return m_items.size();` | a | container size returned as `int`/`uint` (game-data or entity container) |
| 787 | `CvGameCoreDLLUtil/include/CvWeightedVector.h:210:59` | size_type (unsigned long long) -> uint (unsigned int) | `uint uChoice = GC.getGame().urandLimitExclusive(m_items.size(), seed);` | a | RNG bound = container size; same value on both builds, so the same draw |
| 788 | `CvWorldBuilderMap/include/CvWorldBuilderMapTypeDesc.h:56:38` | size_type (unsigned long long) -> uint (unsigned int) | `uint Size() const { return asTypes.size(); };` | a | container size returned as `int`/`uint` (game-data or entity container) |
| 789 | `FirePlace/include/FireWorks/FFastVector.h:893:36` | long long -> INT (int) | `INT iIndex = ( pNode + 1 ) - &mVec[ 0 ];` | b | pointer difference (`ptrdiff_t`) narrowed to an `INT`/`UINT` element index; FFastVector capacity is `UINT` |
| 790 | `FirePlace/include/FireWorks/FFastVector.h:901:36` | long long -> INT (int) | `INT iIndex = ( pNode - 1 ) - &mVec[ 0 ];` | b | pointer difference (`ptrdiff_t`) narrowed to an `INT`/`UINT` element index; FFastVector capacity is `UINT` |
| 791 | `FirePlace/include/FireWorks/FFastVector.h:909:36` | long long -> INT (int) | `INT iIndex = ( pNode + 1 ) - &mVec[ 0 ];` | b | pointer difference (`ptrdiff_t`) narrowed to an `INT`/`UINT` element index; FFastVector capacity is `UINT` |
| 792 | `FirePlace/include/FireWorks/FFastVector.h:916:36` | long long -> INT (int) | `INT iIndex = ( pNode - 1 ) - &mVec[ 0 ];` | b | pointer difference (`ptrdiff_t`) narrowed to an `INT`/`UINT` element index; FFastVector capacity is `UINT` |
| 793 | `FirePlace/include/FireWorks/FFastVector.h:1007:25` | long long -> INT (int) | `INT iIndex = ptBefore - &mVec[ 0 ];` | b | pointer difference (`ptrdiff_t`) narrowed to an `INT`/`UINT` element index; FFastVector capacity is `UINT` |
| 794 | `FirePlace/include/FireWorks/FFastVector.h:1038:33` | long long -> UINT (unsigned int) | `UINT uDeletePos = tData - &mVec[ 0 ];` | b | pointer difference (`ptrdiff_t`) narrowed to an `INT`/`UINT` element index; FFastVector capacity is `UINT` |
