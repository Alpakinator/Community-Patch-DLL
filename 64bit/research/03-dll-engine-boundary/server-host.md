# The x64 server, first slice: services, new-game sequence, measurements (epic 1500)

Written 2026-11-10 by the team-1500 coordinator. Code: `server/` (README there); evidence: `server/evidence/`. DLL: fork
`x64` @ e9d0c4837 (`deps/dll.lock`), x64, built by `server/build_server.py` (debug: SHA-256 `cd1bc388...be519e`;
release, full LTO: `015ef785...905782`). Database: VP 5.4.8 cdrift copy (`bd9b8aa1...addfadd6f`, `SQLITE_LOGGING` = 1).
Game data: the toolkit game folder (`...\headless_data\game\VP_MODPACK_5_4_8_NEUTRALS\Sid Meier's Civilization V`),
read in place. 32-bit reference: civ5-headless (`c5h`) run read-only as a tool with the official VP 5.4.8 x86 DLL
(`4e91c58a...3c3f82`), `--no-mods` (issue 1505, `server/evidence/1505/`). No civ5-headless code was used: the server
is written from `research/sources/civ5-headless/` and the DLL's headers; the 32-bit host's run outputs
(`newgame-report.json` steps, include logs) were read as data.

## 1. Result

| Item | Result (2026-11-10) |
|---|---|
| New game, headless, AI only | Tiny Continents (seeds 30017/40017) 10 and 50 turns; Standard Continents 80x52, 8 majors, 16 city-states (seeds 1001/2002) **200 turns**, 0 Lua errors, no exception [L] |
| Speed (release DLL) | Tiny 0.265 s/turn (50 turns 13.2 s); Standard **1.505 s/turn** (200 turns 301 s; 5,008 Updates); 32-bit host on the same specs: 0.22 and 1.48 s/turn [L] |
| Speed (debug DLL) | Tiny 0.55 s/turn; boot `CacheGameDatabaseData` 17-21 s debug, 11.5 s release [L] |
| Memory | peak working set 166 MB Tiny, 249 MB Standard 200 turns (32-bit host: 279 and 368 MB) [L] |
| A/A (same spec, seeds, build) | release: `stats.db` **identical** (15 tables, 9,257 rows, Tiny 50 turns). debug: identical except `ReligionBeliefYields.IsReligionOwner` from turn 36, caused by an uninitialised DLL member (section 5) [L] |
| x86 vs x64 behaviour | the games agree turn for turn until a combat result differs: **Tiny turn 34** (`MapUnitsState.unitCurrHP`, Persian Warrior 73 x86 vs 72 x64), **Standard turn 101** (American Spearman 37 vs 11); identical before that in all 15 tables (text keys compared as en_US text) [L]. Cause, corrected 2026-11-10 (epic 1800, measured [L]): NOT CRT maths. A stray `;` in `AttackKey::operator==` (`CvTacticalAI.h:1071`) lets the tactical attack cache return entries for a different prior damage on bucket collisions, and bucket placement differs between the builds; see `x64-dll-port.md` 9.5-9.6. (Replaced: "Cause [I]: CRT math in combat damage".) Drift reported, not fixed |
| Save written by the x64 DLL | yes (`--save-out`): pregame + body through the DLL's own writers; packed into a `.Civ5Save`, **the 32-bit host loads it and plays turns 50-55** with the official x86 DLL, no warning, no Lua error [L] |
| Round trip (headless-save-comparison.md part 1) | one input save (x86 Tiny turn 50) loaded and saved at once by the x86 build of the same sources (c5h) and by the x64 server: **pregame identical** (14,170 B); body 13,592,775 vs 13,592,883 B, differing only in the DLL version string, one localized string, and three values the two hosts' post-load sequences set differently (section 6) [L] |

## 2. Services the server supplies (contract)

Engine contract: `research/sources/civ5-headless/engine-contract.md` (sections in brackets). Every other method of the
six interfaces is a generated typed override that returns zero / NULL / empty (`tools/x64-dll/smoke-host/gen_ifaces.py`,
slots from the compiler); `server-report.txt` lists every method the DLL called with "server" or "stub", and since
issue 9007 (section 11) logs the first call of each stub and lists the stubs called under a heading of their own.

| Service | Contract as implemented | Source |
|---|---|---|
| 22 host exports | `GetLogFileMgr`, `GetFileSystemMgr`, `FStringA/W_GetStringInitData`, `GetTempHeap` (8 MB block, x64 layout), the FSBA set (`FSBAAllocate` NULL = use malloc), `_Init_locks::operator=` under its x64 decoration | `srv_exports.cpp` (from the smoke host) |
| Lua | one main state in the shared `lua51_x64.dll` (checked: the DLL's import binds the same module); `CreateLuaThread`: registry-anchored thread, globals `{StateName}` over `_G`, `Threads[thread]`; the DLL's libraries registered nested in the first thread (VP_LUAAPI) and once in the main state after boot (the engine's InGame state), so later threads (Map Script) find them through `_G` [6.7, 10.4] | `srv_lua.cpp` |
| EXE Lua extras | `ipairs` visits index 0 [10.5]; `debug` removed after boot [10.5]; `include(name)` runs the file found through the VFS in the caller's environment (every call, as the 32-bit host), returns `{path}` [10.3]; `IncludeFileList` = the Lua winners; `table.fill(v, n)` and `table.fill(t, v, n)` (indices 1..n), `table.count`, `math.clamp` (found missing by scanning the game's Lua, 2026-11-10; not in the contract) | `srv_lua.cpp` |
| Engine Lua tables | `GameInfo.T` (rows by ID or Type; `T()`, `T("sql where")`, `T{col=val}` iterate in rowid order; booleans for `boolean` columns; `#GameInfo.T` = row count, needed by VP's `AssignStartingPlots.lua`, so each table is a userdata), `GameDefines` (Defines), `DB.Query`, `DB.CreateQuery`, `Locale` (since 2026-11-10, epic 1700: `Lookup`/`ConvertTextKey` with arguments over the real CvLocalization, whole numbers pushed as int, others as double, as the 32-bit host; `HasTextKey`, `ToUpper`/`ToLower`, `Length`, `Compare`, `ToNumber`, `GetCurrentLanguage`, `IsNilOrWhitespace`/`IsNilOrEmpty` [I: the EXE's own behaviour of these is not decompiled]; was: keys returned unchanged), `MapModData`, `Modding` (no mods; `GetActivatedModEntryPoints` returns an empty iterator), `Events`/`LuaEvents` (no-op) | `srv_lua.cpp` |
| `LoadFile(L, path)` | relative path (`Assets\Maps\Continents.lua`) or name, through the VFS **by file name first** (the highest-priority package's copy wins; the literal relative path only when the index has no such name; an absolute path as it is); runs the chunk in the thread. *Corrected 2026-11-13 (bug 0100, section 14); was: a relative path that existed under the game folder was taken literally before the name lookup, so the server ran the base game's `Assets\Maps\Communitu_79a.lua` where c5h runs VP's* | `srv_lua.cpp`, `srv_vfs.cpp` |
| VFS | read-only index of the game folder: base `Assets` (rank -1) < DLC packages by their `.Civ5Pkg` `<Priority>`; inside a package the file found last in a case-insensitive walk wins (VP over CP, `UI` over `Mods`); `DLC\Expansion` (G&K) and `DLC\Tablet` left out. Same winners as the 32-bit host for every include of the map script (`server/evidence/1505/*/include-resolution.log`) [10.3] | `srv_vfs.cpp` |
| `CallFunction`, `CallCFunction` | protected calls; errors logged to `Logs\Lua.log` with a stack trace and counted (a Lua error during map generation fails the run) | `srv_ifaces.cpp` |
| `GameEvents` dispatch | `CallHook` value always false; `CallTestAll` / `CallTestAny` / `CallAccumulator` with the measured semantics; handled = at least one listener; never-indexed name: false [10.1]. No listeners exist in these runs (no add-ins with a modpack and `--no-mods`, as the 32-bit host) | `srv_lua.cpp` |
| Args object | `Push` returns the 0-based index [10.1]; typed getters | `srv_lua.cpp` |
| Game-core lock | a real recursive lock; `HasGameCoreLock` = this thread's depth > 0 [1.3]; `TryGameCoreLock` | `srv_ifaces.cpp` |
| Thread role | the new game on the main thread, every `Update` on a separate core thread; `IsGameCoreThread` true only on the core thread inside `Update`; `IsGameCoreExecuting` false [1.2, 2] | `srv_main.cpp`, `srv_ifaces.cpp` |
| Net loopback | **Corrected 2026-11-10 (issue 9007, section 11): every `send*` the DLL calls that has a `Response*` (36 methods) is looped back**, through `ICvNetMessageHandler3`; was: `sendPlayerOption` -> `ResponsePlayerOption(active player, ...)`; `sendMinorPledgeProtection`, `sendMinorBullyGold`, `sendMinorBullyUnit`, `sendMinorGiftTileImprovement`, `sendMinorBuyout` -> their `Response*` (ICvNetMessageHandler2) only, other sends counted as "unhandled" or dropped by stubs (`sendMoveGreatWorks`, epic 2200). Queued, delivered on the main thread after the current top-level call (after each Update, after `SendPlayerOptions`, after arming) [1.4, 1.5, 6.4] | `srv_ifaces.cpp` |
| Player options | the `PlayerOptions` default, plus `PLAYEROPTION_SAFE_AUTOMATION` true (the 32-bit host's reference profile) [1.4] | `srv_ifaces.cpp` |
| Turn signalling | `CanAdvanceTurn` true; `SendAICivsProcessed` / `allAICivsProcessedThisTurn` (false in the same Update, true from the next) [6.3] | `srv_ifaces.cpp` |
| New-game answers | `GetEvaluatedMapScriptPath` copies the path [5.3]; `GetDone` true from shutdown [8]; `GetAutorun` false; `IsLoadedGame` (UI) true after a load | `srv_ifaces.cpp` |
| File system | `FIFileSystem::Create` opens a real file, only inside the run folder: `CvGame::readSaveGameDB` writes the save's embedded `Civ5SavedGameDatabase.db` through it; with the zero stub every section after the game section is read misaligned (`CvMap::InitPlots` bad_alloc) [C source, L]. Since ticket 0488 (2026-11-17, section 18) the check compares normalised paths (separators, case, 8.3 names), and a write create that is refused or fails ends the load (headless: exit 2 with the reason in `server-report.txt`; remote: `ReportAndExit(1)`, the D118 path). (Was: only the file's path was normalised, so a `run_dir` with `/` refused every file and the load asked for 115 GB.) | `srv_ifaces.cpp` |
| Log files | `GetLog` returns per-name log objects; `CvAssert.log`, `Database.log`, `Gamecore.log`, `CustomMods.log`, `xml.log` are written, the AI logs only with `--dll-logs` (the logging flags are set as the reference, 15, either way) | `srv_ifaces.cpp` |
| Cache folder | `<run>\cache\` (stats.db, `Civ5SavedGameDatabase.db`) | `srv_main.cpp` |

**Stubbed and why it is fine for this slice** [L counts, Standard 200]: the `Gameplay*`, `Publish*`, `TradeVisuals_*`
renderer notifications, `DoTurn`, advisor getters, `AutoSave` (no file), `sendPlayerInfo`, `openSlot`, `FlushTurnReminders`,
all `ICvUserInterface2` except `IsLoadedGame` (nothing selected, no popups). This is the 32-bit host's known-harmless
set (its `stubCalls` lists the same names, `server/evidence/1505/*/host-report.json`). `stripSpecialCharacters` (927 calls)
is a stub in both. **CvLocalization is real since 2026-11-10 (epic 1700, `x64-dll-port.md` section 11)**: at boot, before
`Init`, the server opens a run copy of `Localization-Merged.db` and calls `Localization::Initialize(<game>\Locale\icudt44l.dat,
db, "Languages", "en_US", false)` as the 32-bit host does (`server/src/srv_loc.cpp`, `Logs\Localization.log`,
`--loc-db`); the x64 `stats.db` holds no `TXT_KEY_` value any more. (Was: stubbed, `Lookup` returned the key, `compare_stats.py
--loc-db` translated.) Not done: `setDLCAllowed` (the reference
allows 11 DLC; pregame only, no effect on these games), the NetInitInfo `Write`/`Read` round trip (`Commit` on a fresh
info, which snapshots pregame in its constructor), the UI frame and the 32-bit host's UI emulation profiles (proved
result-neutral for these specs by issue 1505: profiles on/off give identical `stats.db`).

## 3. New-game sequence as implemented

The 32-bit host's steps for a plain VP DLL (its `newgame-report.json` `steps`, `server/evidence/1505/tiny-a/`), which
follow `engine-contract.md` 5.4. `srv_main.cpp`, phases in `server.log`:

1. **Boot** [3]: VFS; Lua; load `CvGameDatabase_x64.dll`, `Connection::Open(run copy, READWRITE)`; `LoadLibrary(DLL)`;
   `DllGetGameContext`, `SetDLLIFace`, `SetGameDatabase`, `Init`, `SetEngineUserInterface`, `GetScriptSystemUtility`;
   `setVersionString("403694 FINAL_RELEASE")`; `SetLogging` + AI, AI perf, builder AI, player/city split (flags 15);
   scratch deal; pregame reset; `FlushGameDatabaseData`, `PerformDatabasePostProcessing`, `CacheGameDatabaseData`.
2. **Lua**: engine tables, the DLL's libraries in the main state, `debug` hidden.
3. **Pregame**: `resetGame`, `resetPlayers`, `setPlayable` 0..62, `setGameType(SINGLE_PLAYER, NEW)`, both pregame seeds =
   the map seed, `setActivePlayer(0)`, game name, nickname, `clearDLCAllowed`, slot 0 taken/claimed/empty names,
   `setCivilization(-1 or spec)` + `setTeamType` for 0..21, world size (resets slots), city-state count, no WB scenario,
   no random script, map script, speed (twice), era, handicap of slot 0, victories, max turns, `ResetGameOptions`,
   `ResetMapOptions`, spec game/map options, slot status 0 taken, 1..majors-1 computer, rest closed.
   *(Note 2026-11-15, issue 4253: the slot loops above no longer stop at a compiled 22 ("0..21"). They run to the DLL's
   `MAX_MAJOR_CIVS`, which the server reads from `Defines` in the database phase, right after
   `PerformDatabasePostProcessing`; 43 with the 43-major DLL. The same step checks the player totals against the
   server's compiled values; a mismatch is `FAILED`, exit 2. `majors=` above the DLL's major slots is now a loud
   failure; before, those slots were silently left closed. Remote mode makes the same check in its post-processing
   hook. Details: `player-limits.md` 8.2.)*
4. **Start**: `setPersistSettings(false)`, `VerifyHandicap(0)`, `setActivePlayer(0)`, NetInitInfo create/`Commit`/destroy
   (`GetEvaluatedMapScriptPath`), world size (no reset), climate 0, sea level 1, era, `CacheGameDatabaseData`, pregame
   seeds = spec (map, sync), **`closeInactiveSlots`** (VP draws civs and city-states from the map seed here),
   `Game.Init(handicap)`, `CacheGameDatabaseData`, leader per major (the civ's `Civilization_Leaders` row).
5. **Init2** (map generation on the main thread, Lua map script), then `LaterInit` 0..63, `InitTacticalAnalysisMap`,
   pregame `DYNAMIC_TURNS`/`SIMULTANEOUS_TURNS`/`PITBOSS` = 0, `SetGraphicsInitialized(true)`, `SetupGraphical` 0..63,
   `DoGameStarted`, `NetMessageStaticsReset`, `SendPlayerOptions(true)`, `Clear*Deltas`, deliver 12 responses,
   `SelectSettler`, player 0 `RebroadcastNotifications`, `SetPausePlayer(0)`.
6. **Arm** [7]: Lua `Game.SetOption(QUICK_COMBAT/QUICK_MOVEMENT, true); Game.SetAIAutoPlay(N, 0)` (observer slot
   becomes active), deliver 12 responses, `SetPausePlayer(-1)`.
7. **Turn loop** [6]: `Update` on the core thread (the `CvDllGame` wrapper takes the lock), net delivery on the main
   thread, until the target turn; then 10 idle Updates (the reference's default). Updates per turn: Tiny 14 at turn 0,
   13 after; Standard 5,008 for 200 turns: the same counts as the 32-bit host [L].
8. **Shutdown** [8]: `setGameStarted(false)`, `Game.Uninit`, `GetDone` true, context `Uninit`; the process exits.

Checks that the sequence reproduces the reference [L]: same civs and leaders per slot (Tiny: 35/29, 13/6, 39/35, 37/41;
Standard: Shoshone, Maya, Babylon, America, Venice, Songhai, Japan, England), the same net traffic on Tiny (36 player
options + 1 pledge; Standard: 36 + 77 pledges + 1 bully gold against the reference's 36 + 75 + 1, the games having
diverged after turn 100), the same Update count (Standard 5,008), `stats.db` identical to turn 33 (Tiny) / 100 (Standard).

**Load** (`--load SAVE.Civ5Save`; saves.md section 4): `resetGame`, `resetPlayers`, `clearDLCAllowed`,
`PreGame::read(stream, true)`, `setGameType(SINGLE_PLAYER, LOADED)`, `setLoadFileName(save path)`,
`CacheGameDatabaseData`, `Game::Read`, `Map::Read`, 64 teams, 64 players, **`Map::RecalculateLandmasses`** (since issue
0452, 2026-11-16, section 17), `ReadSupportingClassData` (the body must be used up exactly; it is: 13,553,328 of
13,553,328 bytes), then step 5's game-started steps without `SelectSettler`. *(Corrected 2026-11-16, issue 0452: the
list had no `RecalculateLandmasses`; the server and c5h never made the call, so every headless game from a save played
with the plots' water flags all false. The EXE makes it at this point [C]; section 17.)*

## 4. How a turn costs (release, Standard, 2026-11-10) [L]

`turn-times.tsv`: turns 1-100 about 0.6-1.3 s, 100-200 about 1.3-2.4 s; boot 16.6 s (of which `CacheGameDatabaseData`
11.5 s, map generation 4.75 s). Calls per 200 turns: `IsGameCoreThread` 8.9 M, `GetScriptSystem` 6.0 M,
`HasGameCoreLock` 4.9 M, lock pairs 1.87 M, `CreateArgs` 1.86 M, `CallTestAll` 1.71 M, `CallHook` 132 k
(`server/evidence/1503/std200/server-report.txt`).

## 5. A/A determinism and a DLL bug it found

- Release, Tiny 50 turns, two processes: `stats.db` identical, all 15 tables [L] (`server/evidence/1504/aa-tiny50-release.txt`).
- Debug, same: identical except `ReligionBeliefYields.IsReligionOwner` from turn 36 (30 rows); debug vs release differ in
  the same column only [L]. Cause [C source]: `CvReligion::CvReligion(eReligion, eFounder, pHolyCity, bPantheon)`
  (`CvReligionClasses.cpp:185`) leaves `m_iHolyCityX/Y` uninitialised when `pHolyCity` is NULL (every pantheon);
  `PlayerOwnsReligion` (`CvEconomicAI.cpp:2245`) then looks up `plot(garbage)`. The fields are serialized, so the bytes of
  a save holding a pantheon are not deterministic either [I]. Pre-existing in the upstream DLL, on x86 too [I]; not fixed
  (one-line initialiser; a DLL change for the fork's `x64` branch and the increment-0 branch, MC's call).

## 6. Saves

- **Writer**: `--save-out NAME` writes `NAME.pregame.bin` (`ICvPreGame1::write`) and `NAME.body.bin` (Game, Map, 64
  teams, 64 players, supporting data) through the closed library's replacement `FMemoryStream` (linked from
  `FireWorks.lib`, the code the DLL itself uses). `server/tools/civ5save.py pack` adds a header and the zlib body
  (Z_SYNC_FLUSH, no final block, as saves.md 2). The header is taken from a save of the same game (the server does not
  write one yet).
- **Cross-load** [L]: the x64 Tiny turn-50 save, packed with the x86 turn-50 save's header, loads in the 32-bit host
  (official x86 DLL) and plays turns 50-55: status passed, no load warning, 0 Lua errors
  (`server/evidence/1506/x86-loads-x64-*`).
- **Self round trip** [L]: an x64 save (Tiny turn 2) loads in the server; the body is used up exactly.
- **Round trip x86 vs x64** (headless-save-comparison.md part 1): input `Final_0050` of the 32-bit Tiny run. Arm A: the
  32-bit host with the x86 release build of the D18 sources (issue 1008's `src-sorted`, SHA-256 `356d9c5f...cb92`,
  version string `w1004-saveident`), `--lua-probe server/tools/probes/save-now.lua`. Arm B: the x64 server, `--load`,
  `--save-at-start`. Result [L] (`server/evidence/1506/`): pregame identical (14,170 B). Body 13,592,775 vs 13,592,883 B;
  **re-run 2026-11-10 with the real CvLocalization (`server/evidence/1704/`): 13,592,775 vs 13,592,880 B, 49 differing
  regions (was 50); the replay text difference is gone, the 105 B left are the version string and the option list;
  the save must be loaded from its original path, the pregame stores the load file name.** Epic 1500's result: the 108-byte difference is the version string (+17), one replay text (+3: "Session 2 starts" vs
  `TXT_KEY_MISC_RELOAD`, CvLocalization stubbed) and player 0's option list (+88: 23 entries vs 12, i.e. the x64 load
  delivered the 12 `ResponsePlayerOption` hash entries, the 32-bit host's resave holds one); everything else differs only
  in values of equal length: one int per player (50 vs 0) and one flag [L]. **Corrected 2026-11-10 (issue 2402,
  section 12) [L]: all three are host-made and now attributed: the option list = the server delivered hash keys where the
  EXE and the 32-bit host deliver the engine index (server fixed); the flag = `CvUnit::m_iEverSelectedCount` of the unit
  the 32-bit host selects after a load (server now selects it too); the per-player int = `m_iTurnSliceMightRecomputed`,
  set by the 32-bit host's Demographics profile (c5h `--no-profiles`). Same-source x86/x64 resaves are now whole-file
  identical across the two hosts.** (Replaced: "Attribution ... is [I] ...; a writer-identity test with one host for
  both builds is still to do.")
- Loading in reach: yes. Missing for a full product save: the `.Civ5Save` header writer (type names, DLC list, mods,
  DLL GUID; saves.md 2.1) and zlib in the server (no zlib in `deps/third-party.lock`; `civ5save.py` uses Python's).
  **Done 2026-11-10 (issue 2001, section 9):** the server reads and writes the whole `.Civ5Save` natively, with a header
  it builds and zlib 1.2.3; the paragraph above described the state before.

## 7. Next blocker (named exactly)

For the server as a product slice, none in the DLL boundary: a 200-turn Standard game plays. The next items, in order:
(1) ~~**CvLocalization** is still a closed-library stub~~ done 2026-11-10 (epic 1700): `stats.db` text equals x86 up to
the combat drift, the replay text in the round trip is gone (`server/evidence/1704/`); (2) **x86/x64 combat drift** from turn 34 (Tiny) / 101
(Standard), corrected 2026-11-10 (epic 1800): the cause is the tactical attack cache key bug (`x64-dll-port.md` 9.5-9.6), not CRT maths; the one-character fix is the owner's decision (tracker 0077) because it changes today's 32-bit AI;
(3) the **uninitialised pantheon holy-city coordinates** (section 5); (4) ~~the `.Civ5Save` header writer and zlib for
saves written without a reference header~~ done 2026-11-10 (issue 2001, section 9); (5) the transport (epic 1200) to attach the shell: the server keeps the
two-thread model and the lock for it; (6) added 2026-11-10 (epic 2000, section 10): the huge save plays 350 -> 400
in the server, but x86 and x64 first differ in turn 354 (`MilitarySummary.RecNavyOffensive`, Spain) **with and without
the cache-key fix**: the cause is not attributed yet (host post-load state or a second build difference).
**Resolved 2026-11-10: epic 2200 found the server's missing `sendMoveGreatWorks` loopback; issue 9007 put it and
every other loopback into `server/` and measured x86 = x64 for 350-400 with the key fix (section 11).**

## 8. Not verified

- The x86/x64 drift cause is [I] (combat HP is the first differing value in both maps; no instrumented comparison of the
  damage formula was run).
- The real game's load sequence and therefore which host's post-load state is "right" (section 6).
- Map scripts other than Continents; Small, Large and Huge maps; game options; more than 200 turns; bug 0078 (not met:
  no turn-160 save was loaded).
- `IsGameCoreThread`/lock semantics against UI-thread calls during an Update (no UI thread calls the DLL in this slice).

## 9. Native container reader and writer (issue 2001, 2026-11-10)

Code: `server/src/srv_civ5save.{h,cpp}` (container + zlib, no DLL dependency), `server/src/srv_save.cpp` (header from
the game, native load), `server/src/civ5save-tool/` (`civ5save.exe`, built by `build_server.py`, also alone with
`--civ5save-only`). Evidence: `server/evidence/2001/`. Measured 2026-11-10 08:30-08:51 EST. DLL for the live tests:
the THROWAWAY team-2000 build `srv-fixes` (fork x64 e9d0c4837 + bug-fix cherry-picks, release, SHA-256
`6a44abb7...2b486a`); `server.exe` `d944252f...10183`, `civ5save.exe` `7d3b606f...85efdd` (release).

- **zlib version, level and call sequence** [C] (corpus, and zlib 1.2.3 `deflate.c`): the game's body is zlib **1.2.3**
  (the EXE imports the install's `zlib1.dll`, SHA-256 `b0953725...1d9dde`, version 1.2.3, measured by the coordinator
  08:22), `deflateInit` level 6 (Z_DEFAULT_COMPRESSION), windowBits 15, memLevel 8, all input at once with Z_NO_FLUSH,
  then Z_SYNC_FLUSH, no `Z_FINISH` (no final block, no Adler-32), into **one 64 KiB output buffer that is written out
  as a piece only when full** (and the rest at the end). The Z_SYNC_FLUSH call continues in the buffer the Z_NO_FLUSH
  calls left partly filled. When the last data block does not fit into what is left of that buffer, 1.2.3's
  `FLUSH_BLOCK` returns `need_more` before the sync marker; the next Z_SYNC_FLUSH call then runs `deflate_slow` with
  nothing pending and `FLUSH_BLOCK` emits an **empty static block (10 bits)** before the empty stored block (1.2.3's
  own comment, `deflate.c` around line 800: "we don't have to output an empty block here, this will be done at next
  call"). Example `Saves\single\auto\AutoSave_Post_0001 BC-3960`: the game's stream ends `type 2 data block, type 1
  10-bit block, type 0 stored block`; a fresh-buffer flush gives no type 1 block and one byte less
  (`autosave1-blocks-*.txt`). This explains the 29 one-to-two-byte tail differences the coordinator saw with Python's
  zlib 1.2.13 (which cannot set the output buffer; newer zlib also skips the empty block when no symbols are pending
  [I]). With a fresh output buffer for the flush call, zlib 1.2.3 gave exactly the same 125/155 as 1.2.13.
- **Corpus** [C]: all 155 `.Civ5Save` under `My Games\...\ModdedSaves` and `...\Saves` (read in place, read only):
  native unpack + repack (header re-serialised from the parsed fields, pregame, body inflated and deflated) is byte
  identical for **154**; the header re-serialisation is identical for all 155 (`corpus-155.txt/.json`).
  The one left, `Saves\single\auto\prev\AutoSave_Post_0026 BC-2960.Civ5Save` (277,320 B, 2026-09-06), is a **truncated
  file**, not a deflate difference: its body is exactly 4 x 65,536 B, has no sync marker and ends in the middle of a
  match symbol (the last block's distance code is cut off); its last block starts at the same bit as ours
  (2,077,669) but holds more symbols than the inflatable data, so the game had more body after the cut. No writer can
  reproduce it from what it holds (`autosave26-blocks-*.txt`). How it was cut (process ended after the 4th full
  buffer was written, before the rest) is [I].
- **Pregame length** [L]: the reader finds the pregame as before (the first u32 2 after the header followed by a
  piece chain to the end of the file whose first piece starts with 0x78). The server's load now also requires that
  `PreGame::read` uses up exactly those bytes; it did for the huge save (13,798 of 13,798), the server's Tiny save and
  a 32-bit host save (14,005 of 14,005).
- **Header writer** [L]: turn = `ICvPreGame1::gameTurn`, game type = `gameType`; types = the active player's
  civilization and handicap (pregame), start era (pregame `era`), the active player's `GetCurrentEra`, `gameSpeed`,
  `GetWorldInfo`, `mapScriptName`; names = `leaderNameKey` / `civilizationDescriptionKey` of the active player (empty
  unless set; which pregame field the EXE writes for a custom name is [I]); colour = the player's `GetPlayerColor`
  type; DLL GUID and version string = `ICvGameContext1::GetDLLGUID` / `GetDLLVersion`; constants `1.0.3.279 (403694)`,
  `403694`, save version 8. DLC and mods: a loaded game carries the loaded header's lists. **Huge save test**
  (`E2000-HUGE_0350.Civ5Save`, SHA-256 `c499d3c6...5188c3`, loaded and saved at once with `--save-at-start`): the
  generated 884-byte header equals the original **byte for byte**, DLL version string included (both `1.0.0`)
  (`huge-*.header.json`, `huge-load-resave.server-report.txt`).
- **New-game DLC rule** [C] for the list, [I] for the reason: the packages `Assets\DLC\<folder>\*.Civ5Pkg` **with a
  `<SteamApp>` element and without `<Upgrade>`**, folder then file name order (case-insensitive), minus the DLC the
  active mods block (VP: DLC_SP_Maps); each GUID, 1, the en_US `<Name>`. From the Steam install (read only) this gives
  exactly the huge save's 13 entries, the two expansions and "Upgrade 1" included (`dlc-rule-steam.json`).
  **Correction of saves.md 2** (a verbatim source, not edited): its rule "keyed packages" would add the three
  "Vibrant Textures" packages (`Assets\DLC\VT`, keyed but no `<SteamApp>`), which the real game leaves out; the
  saves.md rule described the toolkit folder, where it lists 18 (the 32-bit host's saves). The server applies the
  SteamApp rule to its game folder with `save_dlc_exclude` (run_server `--dlc-exclude`); on the toolkit folder that is
  the 13 plus "VP Modpack" (the modpack package carries `<SteamApp>235580`), mods empty (filled by run_server since
  0303, below). **run_server default since
  0197 (2026-11-14)**: DLC_SP_Maps and the "VP Modpack" GUID `{b5932ae4-0f4f-498f-9333-e2d31b20e095}` (every modpack
  folder seen, 5.2.3 / 5.4.7 / 5.4.8, carries it; VT shares it but has no `<SteamApp>`), so a direct-mode new game's
  save lists the 13 only; a save that lists the modpack is refused by the front end, `CanLoadSavedGame` 2 (0175,
  `release-driver.md` W5.11). Overridable (`--dlc-exclude ""` lists all). Test `server/tests/tools/test_dlc_exclude.py` [L, 2026-11-14 20:00,
  server.exe `543ed459...` from 0196's release build, the descriptor's toolkit folder, Tiny Continents 30017/40017
  `--turns 0 --save-at-start`]: default 13 entries, no "VP Modpack"; `--dlc-exclude <DLC_SP_Maps>` 14, "VP Modpack"
  last (`evidence/0197/test-live.log`). Not checked in a front end (no instance place on 0197).
  (Replaced 2026-11-14: "default DLC_SP_Maps".)
- **New-game mod list (issue 0303, 2026-11-14)** [C for the server, L for run_server]: the server itself still writes a
  new game's mod list **empty** (`srv_save.cpp`, "a new game: the DLC rule below, no mods"; unchanged, no server
  rebuild). The real game with Vox Populi as mods, loading such a save, turns the mods off and aborts (0175,
  `release-driver.md` W5.11). **`run_server.py` fills it since 0303**: after the server ends, a new game's
  `--save-at-start` / `--save-out` `NAME.Civ5Save` gets the release descriptor's mods, `[guid, version, name]` =
  `['d1b6328c-ff44-4b0d-aad7-c657f83610cd', 151, '(1) Community Patch']`, `['8411a7a8-dad3-4622-a18e-fcc18324c799',
  17, '(2) Vox Populi']` for 5.4.8 (name = the descriptor's mod folder; the same triples as 0175's `refsave.py`
  `release_mods`, so that stage finds them equal and copies the save unchanged). Method: the header is uncompressed
  and nothing in the file points into it, so the file is rewritten as the old header with its mod block (u32 count;
  per mod string id, u32 version, string name) replaced, then the old pregame and body bytes as written: no unpack and
  re-deflate (0175 used `civ5save.exe unpack`/`pack`, byte-exact too; the splice needs no `civ5save.exe` and cannot
  change the body). Only an empty list is filled (a header that already lists mods is kept); a loaded game keeps its
  header's list; `--save-mods none` keeps the empty list; exit 5 when the header cannot be read. Proof [C]: the
  descriptor's 32-bit `E2000-HUGE_0350.Civ5Save` (`c499d3c6...`) lists exactly these triples, and with its mod block
  emptied and filled again by `set_save_mods` it equals the original **byte for byte** (`test_save_mods.py`
  `test_reference_save_round_trip`). Live [L, 2026-11-14 20:48-20:49 EST, pinned layout `x64-04b1211b-pa803776`
  bin, the descriptor's toolkit folder, `server/specs/continents-tiny-fixed-seed.json` 30017/40017 `--turns 0
  --save-at-start s0`]: `s0.Civ5Save` 182,676 B (`c9f2566c...`), header 888 B with the two mods (mod block 680..813;
  the same game with `--save-mods none`: header 759 B, block 680..684, 4 bytes), DLC list the 13 entries of 0197 (no
  "VP Modpack"); the save's pregame and inflated body equal the server's own `s0.pregame.bin` / `s0.body.bin`; that
  save loaded by the server (`--load`, native reader: pregame 12,648 B and body 12,272,765 B used up exactly) plays
  0 -> 2, `result: passed`, Lua errors 0, and its `--save-out` keeps the two mods (`evidence/0303/test-live.log`).
  **Not checked in a game instance** (the ticket had no instance place): whether the EXE with VP as mods now loads such
  a save is [I] from 0175's cause; 0175's own run of its refsave fix is the first EXE check. Side observation [L, not
  investigated]: the `none` and the default run of the same seeds wrote pregames equal and bodies of equal length
  (12,272,765 B) that differ from byte 5,835; so two direct-mode new games of one spec are not byte-identical saves.
- **32-bit host loads a server save** [L]: Tiny Continents 30017/40017, x64 server to turn 5, `--save-out t5`
  (`t5.Civ5Save` 240,597 B, header 793 B, written entirely by the server), then `c5h run t5.Civ5Save --turns 8
  --no-mods --dll <official VP 5.4.8 x86 DLL 4e91c58a...3c3f82> --game-dir <toolkit>`: status passed, turns 5-8, no
  load warnings, 0 Lua errors (`x86-loads-x64-t5.*`). The reverse: the server loaded the host's `Final_0008` natively
  (pregame and body used up exactly) and played to turn 10 (`x64-loads-x86-final8.server-report.txt`).
- **zlib 1.2.3 CVEs** (note, no fix) [I], not checked against NVD in this issue: inflate-side CVE-2016-9840/9841/9842
  (pointer arithmetic and shifts in inftrees/inffast/inflateMark, fixed in 1.2.9) and CVE-2022-37434 (only through
  `inflateGetHeader`, which the server does not call); deflate-side CVE-2018-25032 (fixed 1.2.12) concerns our own
  bodies. Inflating untrusted `.Civ5Save` files is therefore a residual risk, the same the game itself has (it ships
  1.2.3). Bounds in the container reader: strings over 1 MiB, short headers and piece lengths past the end are rejected.
- **Not verified**: the real game loading a server-written `.Civ5Save` (only the 32-bit host); a header for a game
  with a custom leader or civilization name; a new game's header against a real-game new game (the huge-save test
  used the loaded lists); the server on a game folder other than the toolkit copy; Python's zlib behaviour above
  (1.2.13 inferred from its source history, not run with a controlled buffer).

## 10. The huge late-game save in the x64 server (epic 2000, issues 2002-2007, 2026-11-10)

Measured 2026-11-10 08:53-09:15 EST by the team-2000 coordinator. Evidence: `server/evidence/2003/` (runs), `2004/`
(x86 comparison and the scripts used), `2005/` (round trip, cross-load). Input: a copy of the owner's `VP8P-HUGE_0350.Civ5Save`
(Huge Continents 128x80, turn 350, Community Patch + Vox Populi, 2,227,934 B, SHA-256 `c499d3c6...5188c3`) as
`%TEMP%\claude\e2000\inputs\E2000-HUGE_0350.Civ5Save` (the round trip needs the same path on both sides, section 6).
**Every run here used THROWAWAY DLL builds** (fork branches, never pushed): `throwaway/2000-fixes` = `x64` e9d0c4837 +
the i0-bugfixes commits 040b063ea (0080) and 27176dbfe (0078) cherry-picked (dca293967, 31dc8e774);
`throwaway/2000-fixkey` = that + the epic 1800 `AttackKey::operator==` fix (1c0f10f1d). Release builds: x64 through
`build_server.py --unlocked` (DLL in `e2000\w2001\srv\bin`, SHA-256 `6a44abb7...`, and `e2000\srv-fixkey\bin`), x86
through the fork's `build_vp_clang.py --config release` (clang-cl, LTO; fixes `cc908616...73ab`, fixkey `9379abe6...7cfd`).

- **Database (issue 2002)** [L]: the pinned VP 5.4.8 database `vp548-cdrift-Civ5DebugDatabase.db`, SHA-256
  `bd9b8aa1...addfadd6f` (= civ5-headless `pinned\vp-modpack-5.4.8-cdrift\Civ5DebugDatabase.db`), the server's default.
  Why: it is the database the 32-bit host used in one-off 9005 to load and play this save (`hh-command.json` of
  `c5-i0\runs\9005-0080-huge-fix`), the save carries the VP DLL GUID, and both hosts here use it, so the comparison has
  one database. It was built from VP 5.4.8 in **modpack** form; the save was made with the **mods** form (EUI only for
  the UI). That both forms give the same gameplay rows is [I], not compared row by row. The owner's game-built
  `My Games\...\cache\Civ5DebugDatabase.db` (6.8 MB, 2026-11-10 02:05) is too small to be a VP database and was not used.
- **The 32-bit host needs the save's mods** [L]: `c5h run SAVE --no-mods` refuses this save ("the save's mods ... are
  not the activated mods that affect saved games"). The reference runs therefore use `--mod
  d1b6328c-ff44-4b0d-aad7-c657f83610cd:151 --mod 8411a7a8-dad3-4622-a18e-fcc18324c799:17`, `--mods-dir` a scratch copy of
  `MODS\(1) Community Patch` and `(2) Vox Populi`, `--no-addins` (the server runs no add-ins), `--db` the database above,
  `--game-dir` the toolkit copy (the server's), `--detail summary` (`server/evidence/2004/c5h.sh`). The host generates a
  mods registry in its own cache (`civ5-headless\cache\db\modsdb\A2AD77E09F369ECB\`).
- **Load and play (issue 2003)** [L]: the x64 server loads the save natively (pregame 13,798 B and body 37,694,481 B used
  up exactly; `CacheGameDatabaseData` 10.0 s; load 0.35 s) and **plays 350 -> 400**: 0 Lua errors, no exception, 1,488
  Updates (29-30 per turn), through turn 355, where bug 0080 ends the stock DLL in the 32-bit host (fixed in these
  builds). Not tried: the pinned, unfixed DLL past turn 355 in the server.

| Huge save, release, 2026-11-10 | x64 server | 32-bit host (x86 build, same source) |
|---|---|---|
| quiet pair, 350 -> 360, one process at a time (09:11-09:15) | 90.1 s, **9.0 s/turn**, peak working set **323 MB** | 104.2 s, **10.4 s/turn**, 340 MB |
| `fixes` pair 350 -> 400 (08:53-09:06; in parallel with each other, other runs and a DLL build) | 544.1 s, 10.9 s/turn, 366 MB | 666.9 s, 13.3 s/turn, 445 MB |
| `fixkey` pair 350 -> 400 (08:58-09:09; in parallel) | 537.6 s, 10.8 s/turn, 396 MB | 630.0 s, 12.6 s/turn, 376 MB |

  One-off 9005's 32-bit run of the same save (DLL `i0-bugfixes`, release, 350 -> 361, alone): 15.8 s/turn, 445 MB.
  Until issue 2003 the server's result line divided the loop time by the target turn, not by the turns played
  ("1.361 s/turn" for this run); fixed in `srv_main.cpp`.
- **x86 vs x64, turn by turn (issue 2004)** [L]: `compare_stats.py` (`IsReligionOwner` excluded). **Both pairs agree on
  turns 350-353 in all 15 tables and first differ in turn 354**: `MilitarySummary` for Spain, `RecNavyOffensive` -27
  (x86) vs 0 (x64), hence `RecTotal` -6 vs 21, the same values in the `fixes` and the `fixkey` pair; in the `fixes` pair
  also Aksum and Samarkand `Danger` 115 vs 114 and two Germany "Won Battle" instant yields only on x86 (a combat); from
  turn 355 every table differs (`2004/*-t354.txt`, `*.compare.json`). So **the epic 1800 cache-key fix does not move the
  first difference on this save**: something else differs. `RecNavyOffensive` is `m_iRecNavalUnits`
  (`CvMilitaryAI.cpp:1822`); 0 on x64 means its numerator (naval defence weight from `IsExposedToEnemy(.., ARMY_TYPE_NAVAL)`
  per city, offence weight from `HavePreferredAttackTarget`, `iNavalPercent`) was 0 there and not on x86 [C source];
  which input differs was not instrumented. **Correction 2026-11-10 (epic 2200, `x64-dll-port.md` 9.7) [L]: the cause is a
  server gap, neither candidate below: the server drops `sendMoveGreatWorks`, through which VP sends Egypt's turn-353
  vassal tax on Spain (NetMessageExt case 1004); the 32-bit host delivers it. With the loopback added (trial patch) and the
  key fix, x86 and x64 agree on every table 350-400.** (Replaced: "Attribution, not settled".) Earlier text, kept:
  Attribution, **not settled** [I], two candidates: (a) the hosts' post-load
  state differs (round trip below: player 0's option list, one per-player int the 32-bit host sets to the turn, one
  flag; a new game has none of these, and new games agree 0-200 with the key fix, `x64-dll-port.md` 9.6); (b) a second
  x86/x64 difference that new games up to Standard turn 200 do not reach (late-game AI on a Huge map). Next step:
  instrument the turn-354 `CvMilitaryAI::UpdateRecommendedArmySize` inputs for Spain in both builds (the
  `tools/drift/instrument.py` method), or make the server's post-load sequence equal the 32-bit host's and rerun.
- **Round trip (issue 2005; headless-save-comparison.md part 1)** [L]: the save loaded and saved at once by the x86 build
  in the 32-bit host (`--lua-probe save-now.lua`) and by the x64 build in the server (`--save-at-start`). **Pregame
  identical (14,227 B).** Header 1,038 vs 884 B: only the DLC list differs (the 32-bit host writes its own game folder's
  17 packages, the server carries the save's 13; host policy, not the DLL). Body 37,733,945 vs 37,734,033 B: with the
  88-byte insertion aligned, **18 same-length runs**, the same three kinds as epic 1500's Tiny round trip (section 6):
  player 0's option-list count 12 vs 23 plus the 88 bytes of the 11 extra entries (the server delivers the 12
  `ResponsePlayerOption`s after the load), one flag (2 vs 1), and per major player one int = 350 (the turn) in the
  32-bit host's save and 0 in the server's, one player with a second int (5 and 350 vs 0 and -1)
  (`2005/x86-vs-x64-afterload.shiftdiff.txt`). **Corrected 2026-11-10 (issue 2402, section 12) [L]: the attribution
  is now measured (option keys, `m_iEverSelectedCount`, `m_iTurnSliceMightRecomputed` and `m_iEconomicMight` of the
  might cache; the DLC list = c5h's game-folder rule), and with two server changes, c5h `--no-profiles` and a c5h game
  folder holding the save's DLC set, the same-source x86 and x64 resaves of this save are whole-file identical.**
  (Replaced: "Attribution ... is [I] as in section 6; ... Writer identity with one host for both builds: still not
  possible.")
- **The 32-bit host loads the server's huge save** [L]: the server's `--save-at-start` `.Civ5Save` (header generated,
  section 9) in `c5h run` with the x86 `fixes` DLL: passed, turns 350-352, 0 Lua errors, no load warning (`2005/`).
- **The two diplomacy asserts (issue 2007)** [L, C]: `CvAssert.log` of every server run holds `CvGame.cpp:5975` "Failed
  to generate diplo response query" and `:6069` "Could not find diplomacy response" (in release builds too: VP's `ASSERT`
  is on in release). **The 32-bit host logs the same two** on the same save (9005's run and `2003/x86-*.CvAssert.log`),
  so it is not a server gap against the reference. Cause [C]: `CvGame::GetDiploResponse` joins `Diplomacy_Responses` with
  `Language_en_US`, a view that exists only in `Localization-Merged.db`; the EXE attaches that database to the game
  database (`ATTACH ? AS Localization`, "Attach Loc Database": strings of the DX11 EXE); neither headless host does, so
  AI diplomacy response text is empty in both. Attaching it in the server would follow the EXE but move the server
  away from the 32-bit reference; not done (decision for the epic owner).

## 11. Net loopback for every send, and the audit of unserved engine calls (issue 9007, 2026-11-10)

Written 2026-11-10 by the one-off 9007 agent (background agent of the MC). Code: `server/src/srv_ifaces.cpp` (loopback,
three host answers, first-call log), `server/src/srv_main.cpp` (report section), `server/run_server.py`
(`--fail-on-unserved`, `--unserved-allow`), `server/unserved-allowlist.txt` (allow-list, one reason per entry),
`server/tools/audit_engine_calls.py` (the static audit). Evidence: `tools/drift/evidence/9007/`. DLL sources read: the
fork at the locked commit e9d0c4837 (file:line below are at that commit). [C] below = read in the DLL source.

### 11.1 What changed in the server

- **Loopback** [C]: every `send*` of `ICvEngineUtility1-4` that the DLL calls (or that the server already overrode)
  and that has a matching `Response*` in `ICvNetMessageHandler1-3` is queued with its arguments and delivered through
  the DLL's handler (queried as `ICvNetMessageHandler3`, which derives from 1 and 2) in send order, on the main thread,
  after the current top-level call returns and before the next `Update` (engine-contract 6.4; the timing of the six
  pairs epic 1500 had, unchanged). The sender of a message without a player argument is the local player: the active
  player at send time (the 32-bit reference does the same for `sendMinorGiftGold` and `sendDoTask`); `sendPlayerOption`
  keeps epic 1500's active player at delivery. 36 send methods are looped back now (35 names, `sendPurchase` has two
overloads; was 6): the six, plus
  `sendMoveGreatWorks` (VP's NetMessageExt channel, epic 2200), `sendSetSwappableGreatWork`, `sendSwapGreatWorks`,
  `sendDoTask`, `sendLaunch`, `SendFaithPurchase`, `SendFoundPantheon`, `sendGreatPersonChoice`,
  `sendFaithGreatPersonChoice`, `sendMayaBonusChoice`, `sendGoodyChoice`, `sendMinorGiftGold`, `sendPushMission`,
  `sendSwapUnits`, `sendAutoMission`, `sendDoCommand`, `sendPushOrder`, `sendPopOrder`, `sendSwapOrder`, both
  `sendPurchase`, `sendFromUIDiploEvent`, `sendResearch`, `sendChangeWar`, `sendUpdatePolicies` and the five
  `SendLeague*` votes/proposals. Before, 18 of these were overridden only to be counted as "unhandled" and
  `sendMoveGreatWorks`, `sendDoTask`, `sendLaunch`, `SendFaithPurchase`, `sendSetSwappableGreatWork`,
  `sendSwapGreatWorks`, the selection sends and the purchase sends fell to generated stubs. The trial patch of epic
  2200 (`tools/drift/server-2200-sendMoveGreatWorks.patch`) is replaced by this and deleted.
- **Three host answers** [C for the DLL use, I for the EXE value]: `IsHost` true (the EXE of a local game is the
  host; was the stub's false), `IsPlayerConnected(p)` = pregame slot `p` is `SS_TAKEN` (the 32-bit reference's
  measured answer), `getTurnsPerMinute` 240 (Civ4's value, as the 32-bit reference): `CvGame::getMinutesPlayed`
  (`CvGame.cpp:4997-4999`) divides by it when a game ends (`CvGame.cpp:7881`), so **the stub's 0 was an integer
  division by zero at any victory**; no server run had reached a game end.
- **Loud stubs**: the first call of any method the server does not implement writes `engine call not served by the
  server (generated stub, answers zero/NULL): <Class::method>, first call` to `server.log`; every call is counted;
  `server-report.txt` gets the line `engine calls not served by the server: N methods` and, at its end, one
  `unserved<TAB>Class::method<TAB>calls` line per such method. `run_server.py` prints those outside
  `server/unserved-allowlist.txt` as `UNEXPECTED`, and with `--fail-on-unserved` exits 3 when there is one. The
  generated code is unchanged: `HostIfaceHit`, which every generated override calls, logs when the method is not
  marked implemented (marking is by vtable comparison at start, as before).

### 11.2 Audit: every send method of the engine's net interface [C]

The list to audit against is `proxy/model/model.json` (epic 2100) plus a scan of the DLL for calls through local
interface pointers, which the model misses (11.3). "Who": AI = reachable for a computer player; human = only for the
local human (UI selection, `isHuman(...)` guards, Lua UI functions); both. "Server now": loop = queued and delivered to
the named `Response*` (H1/H2/H3 = the handler interface level); stub = generated, logged on first call and counted.
Sends without a `Response*` have no message coming back in the engine contract (6.3, 6.4).

| Method | What the DLL uses it for | Who, where (DLL file:line) | Server now (was) |
|---|---|---|---|
| `sendPlayerOption` | player options at game start / autoplay switch | both, `CvGame.cpp:6269` | loop H1 `ResponsePlayerOption(active at delivery)` (loop) |
| `sendMinorPledgeProtection` | pledge to protect a city-state | both, `CvGame.cpp:11812` | loop H2 (loop) |
| `sendMinorBullyGold`, `sendMinorBullyUnit` | bully a city-state | both, `CvGame.cpp:11851`, `:11867` | loop H2 (loop) |
| `sendMinorGiftTileImprovement` | improvement gift | both, `CvGame.cpp:11834` | loop H2 (loop) |
| `sendMinorBuyout` | buy out a city-state | both, `CvGame.cpp:11880`, `:11902` | loop H2 (loop) |
| `sendMoveGreatWorks` | great-work moves (human) and VP's NetMessageExt channel: vassal tax 1004, liberate vassal 1005, minor bully annex, event choices, faith purchase flag, deal renewal | both: AI tax `CvDiplomacyAI.cpp:26869-27120`, liberate `:35348`, annex via `CvGame::DoMinorBullyAnnex` `:36737`; sends `CvDllNetMessageExt.cpp:107-144`, `CvCultureClasses.cpp:2323` (human) | loop H3 (stub: **dropped, epic 2200's turn-354 cause**) |
| `sendDoTask` | city task: city screen (human), capital relocation annexes a puppet capital / AI stops razing it | both: `CvGame.cpp:2910` (human), `CvPlayer.cpp:11645` (any player), `:11650` (AI) | loop H1 `ResponseCityDoTask(active player, ...)` (stub). Note [C]: the send carries no player, so the response acts on the *active* player's city with that id; for an AI capital move that is a DLL bug (no-op in autoplay, the observer has no cities; possibly a wrong city in a human game). The 32-bit reference loops it back the same way |
| `sendLaunch` | launch the spaceship when the last part is built | both, `CvCity.cpp:30333`; AI also launches itself (`CvPlayerAI::AI_launch`, `CvPlayerAI.cpp:150`, `:1283`) | loop H1 `ResponseLaunchSpaceship` (stub). The 32-bit reference drops it: a spaceship launch may happen one AI turn-step earlier in the server than in the reference [I] |
| `SendFaithPurchase` | clears the automatic faith purchase when it became invalid | human (`CvReligionClasses.cpp:743` returns for non-humans), `:880` | loop H2 (stub) |
| `sendSetSwappableGreatWork`, `sendSwapGreatWorks` | great-work swap offers | human (`isHuman(ISHUMAN_AI_TOURISM)`), `CvCultureClasses.cpp:1545-1590`, `:2386-2439` | loop H3 (stub) |
| `sendPushMission`, `sendSwapUnits`, `sendAutoMission`, `sendDoCommand` | unit orders of the local player | human: selection `CvGame.cpp:2816-2860`; Lua `EndTurnsForReadyUnits(true)` `CvPlayer.cpp:10959-10984`; linked units `CvUnit.cpp:15549-15691`; scenario map units `CvWorldBuilderMapLoader.cpp:730` | loop H1 with the active player (`sendPushMission`, `sendDoCommand` counted; others stub) |
| `sendPushOrder`, `sendPopOrder`, `sendSwapOrder`, `sendPurchase` (both overloads) | city production orders of the local player | human, `CvGame.cpp:2898-2949` | loop H1 / H2 with the active player (`sendPushOrder` counted; others stub) |
| `sendMinorGiftGold` | gold gift to a city-state | human (Lua `Game.DoMinorGiftGold`), `CvGame.cpp:11822` | loop H1 with the active player (counted) |
| `sendFromUIDiploEvent` | diplomacy screen choices | human, `CvGame.cpp:5667` | loop H1 with the active player (counted) |
| `SendFoundPantheon`, `sendGreatPersonChoice`, `sendFaithGreatPersonChoice`, `sendMayaBonusChoice`, `sendGoodyChoice`, `sendResearch`, `sendChangeWar`, `sendUpdatePolicies`, `SendLeagueVoteEnact/Repeal/Abstain`, `SendLeagueProposeEnact/Repeal` | — | not called by the DLL (the UI Lua sends these through the EXE's `Network.*`) | loop (counted): kept because the arguments map one to one (the reference loops the first five back) |
| `sendPlayerInfo` | pregame slot broadcast | engine, `CvPreGame.cpp:831` (63 per new game) | stub, allow-listed: no response |
| `sendPlayerInitialAIProcessed` | turn signalling | `CvPlayer.cpp:49326` (active player only) | stub, allow-listed: no response (6.3) |
| `BeginSendBundle`, `EndSendBundle` | batching around player options / slot broadcast | `CvGame.cpp:6260/6272`, `CvPreGame.cpp:813/833` | stub, allow-listed: the queue keeps the order |
| `sendTurnComplete`, `sendTurnUnready`, `sendAutoMoves` | end of the local human's turn, unit automation | human (`isLocalPlayer`), `CvGame.cpp:2257-3821`, `:8979`, `:9123`, `:9154`, `CvPlayer.cpp:33913`, `:49462` | stub, **not** allow-listed: no response; the EXE's turn machinery (with `HasReceivedTurnComplete`, `HasSentTurnComplete`); never called in autoplay (engine-contract 6.3); a human game needs it from the EXE through the proxy |
| `sendNetDealAccepted`, `sendNetDemandAccepted` | the AI accepts a human's deal / demand | human (`isHuman(ISHUMAN_AI_DIPLOMACY)` at `CvDealAI.cpp:224`), `:231`, `:428` | stub, not allow-listed: how the EXE turns this into `ResponsePlayerDealFinalized` (which player acts, accepted flag) is not known; **the riskiest stub for a human game without the proxy** |
| `sendUnitSyncCheck`, `sendPlotSyncCheck`, `sendCitySyncCheck`, `sendPlayerSyncCheck` | multiplayer out-of-sync checks | only under `isNetworkMultiPlayer()` (`CvPlayer.cpp:78`, `CvCity.cpp:64`, ...) | stub, not allow-listed: no response; multiplayer |
| `sendChat`, `sendTurnReminder`, `netDisconnect`, `sendPbemTurn` | chat, hot-seat / PBEM, multiplayer | `CvGame.cpp:14530`, `CvPlayer.cpp:33268`, `:33580` (MP only), `CvGame.cpp:1820`, `:8622` | stub, not allow-listed: multiplayer |
| `sendGameInfo`, `sendExtendedGame`, `SendFoundReligion`, `SendEnhanceReligion`, `SendMoveSpy`, `SendStageCoup`, `sendPing`, `sendPause`, `sendIgnoreWarning`, `SendPledgeMinorProtection`, `SendMinorNoUnitSpawning`, `SendLiberateMinor`, `SendDiploVote`, `sendAdvancedStartAction`, `sendMinorCivQuestNoInterest`, `sendMinorCivQuestCompleted`, `sendMinorCivIntrusion`, `sendMinorCivEnterTerritory`, `sendBarbarianRansom`, `sendGiftUnit`, `sendReturnCivilian`, `SendUpdateCityCitizens`, `SendSellBuilding`, `SendRenameCity`, `SendRenameUnit`, `sendPlayerHurry`, `sendCityBuyPlot`, `sendSetCityAIFocus`, `sendSetCityAvoidGrowth`, `sendChangeIdeology`, `SendLeagueEditName`, `sendRandomNumberGeneratorSyncCheck` | — | not called by the DLL (scan of `gDLL->` / `GC.getDLLIFace()->` and local `ICvEngineUtility*` pointers) | stub, not allow-listed: a call would be a new DLL path and fails `--fail-on-unserved` |
| `SendAICivsProcessed` | turn signalling | `CvGame.cpp:13945` | server (6.3), unchanged |

For the human game through the proxy (D21, `proxy-design.md`): the model's policy is to *post* every send to the
EXE, whose own net layer answers through `Response*`; the server's loopback is the headless replacement of that layer
and must be off (or become the post queue) when the shell is attached, or a message would be applied twice [I].

### 11.3 Audit: the other engine methods the DLL calls that the server leaves to generated stubs [C]

`python server/tools/audit_engine_calls.py --out FILE` lists them with every call site (the per-method table:
`tools/drift/evidence/9007/engine-calls.tsv`). At the locked commit the DLL calls **285 engine-side methods**
(`ICvEngineUtility1-4` 165, `ICvUserInterface1-2` 105, file system and log files 15; 269 with a call site found by the
scan, the other 16 reached through macros such as `FFILESYSTEM` or only listed by the model); the server serves **52**
(was 37 before this issue), the other **233 are generated stubs** (224 of them with a call site). **The model misses
11 methods** the DLL calls through local pointers (`ICvUserInterface2* pkIFace = ...`): `HasReceivedTurnAllComplete`,
`GameplayUnitCreated`, `GameplayUnitGarrison`, `isPopupQueued`, `getLastSelectedUnit`, `setLastSelectedUnit`,
`SetSelectedUnitRevealingNewPlots`, `updateEndTurnTimer`, `waitingForRemotePlayers`, `setForcePopup`,
`IsModalStackEmpty` (`CvDiplomacyRequests.cpp:620`); the model marks them unused, so `proxy/model` would answer them
with a default (a note for the proxy, not changed here).

| Group | Methods (stub answer) | DLL use, who | Verdict |
|---|---|---|---|
| Renderer and sound | `Gameplay*` (49, incl. `GameplayDiplomacyAILeaderMessage`, 108 call sites), `TradeVisuals_*` (5), `DoMapSetup`, `DoTurn`, `PublishActivePlayer`, `NotifySpecificAILeaderInGame`, `DoClearDiplomacyTradeTable`, `FlushTurnReminders` | graphics, leader scenes, AI-to-human diplomacy screen; both | stub, allow-listed headless; human game: posted to the EXE (proxy) |
| Steam | `UnlockAchievement`, `IsAchievementUnlocked`, `GetSteamStat`, `SetSteamStat`, `IncrementSteamStat*`, `RecordVictoryInformation`, `RecordLeaderboardScore`, `IsDLCValid` (false) | achievement code only (`CvAchievementUnlocker.cpp`, `CvBuildingClasses.cpp:7180-7220`, `CvCity.cpp:34135`, `CvPlayer.cpp:28839`, `:49305`) | stub, allow-listed |
| Scenario mods | `IsModActivated` (false) | only the official scenario ids `CIV5_*_SCENARIO*_MODID` (25 sites, e.g. `CvGame.cpp:6757-6769`) | stub, allow-listed: none is active |
| Host and network | `IsHost`, `IsPlayerConnected`, `getTurnsPerMinute` | sync checks (MP), `readPlayerSlotInfo` (`CvPreGame.cpp:3099`), notifications (`CvNotifications.cpp:181`), `getMinutesPlayed` at game end | **now served** (11.1) |
| Network, multiplayer | `IsPitbossHost`, `IsPlayerKicked`, `IsPlayerHotJoining`, `getAssignedNetworkID`, `HasReceivedTurnComplete`, `HasReceivedTurnAllComplete`, `HasReceivedTurnAllCompleteFromAllPlayers`, `HasSentTurnComplete`, `ReseatConnectedPlayer`, `openSlot`, `closeSlot`, `getPassword` | MP, pitboss, turn handshake | stub; false/0 is right for a local AI game; the first five and the slot calls allow-listed |
| Saves and front end | `AutoSave`, `SaveGame`, `LoadGame`, `QuickSave`, `QuickLoad`, `saveReplay`, `RestartGame`, `GameplayOpenOptionsScreen`, `GameplaySearchForPediaEntry` | autosave (both), the rest human UI | `AutoSave`, `saveReplay` allow-listed (the server writes saves on request); the others not (human) |
| Engine housekeeping | `InitGlobals`, `UninitGlobals`, `stripSpecialCharacters` (string unchanged), `getSecsPerTurn` (0.0) | boot/shutdown; names of cities and units (TXT keys); combat mission time `CvUnitCombat.cpp:3563` | allow-listed; the 32-bit reference answers the same |
| Settings and input | `GetAdvisor*Interrupt` (false), `SetAdvisor*`, `GetTutorialLevel`, `TunerEverConnected`, `IsProcessingGameCoreMessages`, `altKey`, `shiftKey` | user settings; Lua UI | allow-listed |
| The active player's screen | 104 `ICvUserInterface1-2` methods: selection (`getSelectionPlot`, `GetHeadSelectedUnit`, `IsUnitSelected`, `isCitySelected`, ...), messages, popups, notifications, dirty flags, end-turn button, camera, `GetScratchDeal` (NULL; `CvDealAI.cpp:6157`), `GetOfferTradeRepeatCount`, `GetStartDiploRepeatCount` (0), `IsModalStackEmpty` (false: no diplomacy request is shown, `CvDiplomacyRequests.cpp:620`) | the local human's UI; in autoplay the active player is the observer | stub; allow-listed by name where an AI-only run calls it (the 32-bit reference's harmless set); human game: forwarded by the proxy (D21) |
| File system, logs | `FIFileSystem::Exist/GetLength`, `FILogFile::Open/Close`, `FILogFileMgr::DeleteLog` | not called in these runs | stub, not allow-listed |

Riskiest stubs left [I]: (1) `sendNetDealAccepted`/`sendNetDemandAccepted` and the turn-complete family for any
human game that is not routed through the EXE; (2) `IsModalStackEmpty` = false in a human game would hold AI
diplomacy requests forever if the proxy answered it with the model's default (the model marks it unused); (3)
`GetScratchDeal` = NULL is dereferenced at `CvDealAI.cpp:6159` when the AI offers peace to the active human (a crash,
not a wrong answer, if a headless human game ever reached it). None of them is reachable in an AI-only game; all are
logged, and all except the screen getters fail `--fail-on-unserved`. `GetScratchDeal` is deliberately not
allow-listed.

### 11.4 Measurements (2026-11-10 10:24-10:41 EST) [L]

Server release build of this branch from a `git archive` of e9d0c4837 (`server.exe` `13b4e7a9...a486e2`, locked DLL
`829c84e9...c61366`); evidence `tools/drift/evidence/9007/` (README there has every hash). Huge-save pairs with
`tools/drift/run_huge.py --srv-arg=--fail-on-unserved` on a copy of the save (`c499d3c6...5188c3`) and THROWAWAY DLLs
(the epic 2000 builds of fork commits 31dc8e774 `fixes` and 1c0f10f1d `fixkey`, branches `throwaway/9007-*` as local
aliases, never pushed). Runs were made in parallel; the times are not speed measurements.

| Run | Result | Net loopback delivered | Unserved methods (all on the allow-list) |
|---|---|---|---|
| Tiny Continents 50, locked DLL | passed, 0 Lua errors | 37 (36 PlayerOption, 1 Pledge) | 77 |
| Standard Continents 200, locked DLL | passed, 0 Lua errors | 114 (36, 77 Pledge, 1 BullyGold) | 89 |
| Huge 350-400, `fixkey` | passed | 64 (36, 24 Pledge, 1 BullyGold, **3 MoveGreatWorks**) | 85 |
| Huge 350-400, `fixes` | passed | 69 (36, 28 Pledge, 2 BullyGold, 3 MoveGreatWorks) | 88 |

- **x86 vs x64, huge save 350-400, key fix: all 14 stats tables identical (682,731 rows each)**: epic 2200's trial
  result reproduced with the loopback in `server/` (no patch). Without the key fix: first difference turn 354 (Aksum
  `Danger` 115/114 and a German "Won Battle" yield only on x86, the `AttackKey` mechanism of `x64-dll-port.md` 9.5);
  Spain's `RecNavyOffensive` agrees.
- **This server vs main's server**, same locked DLL: Tiny 50 identical (9,257 rows), Standard 200 identical
  (369,127 rows). The new loopbacks and host answers change nothing in these new games (no new send fired in them).
- The unserved lists (`unserved-*.txt`): the union is the renderer, Steam, settings, screen and lobby groups of 11.3.
  The loads add `closeSlot`, `getAssignedNetworkID`, city capture / wonder / golden-age visuals, trade-route visuals and
  messages; only new games call `sendPlayerInfo`, `InsertIntoSelectionList`, `makeSelectionListDirty`. No send with a
  `Response*` and no unexpected method was hit in any run.
- **Builds** [L]: `server/build_server.py --src <git archive of e9d0c4837> --out <new dir> --config release --offline`
  (10:33-10:41): build_all steps stubs, cvgamedatabase, cvlocalization, dll, host, smoke all rc 0 (476 s; dll 422 s),
  0 unresolved, narrowing gate 798 = baseline, then the server step: OK in 496 s. (The first such build, 10:16, failed
  at the server step on a compile error of this change, `ResponseCityPurchase` hidden by the H2 overload; fixed.)
- One allow-list fix during the runs: the first Tiny run failed `--fail-on-unserved` on
  `ICvUserInterface1::AddPlotMessage` (the pattern `Add*Message` only matches a prefix); now listed by name.

### 11.5 Not verified

- The EXE's own behaviour for each loopback (that it delivers every one of these in single player, and the sender id
  for sends without a player): from the engine contract and the 32-bit reference, not traced in the EXE.
- Of the 30 newly looped sends only `sendMoveGreatWorks` fired in these four runs; the other 29 are compiled and
  type-checked against the headers, not exercised. A human-driven run (proxy) or a Lua probe would exercise them.
- `sendLaunch` loopback against the 32-bit reference (which drops it): no game here reached a spaceship.
- `IsHost` true, `IsPlayerConnected`, `getTurnsPerMinute` 240: only shown not to change these games; no game end was
  reached.
- The UI methods the model misses (11.3) were found by the scan for `ICvUserInterface2* x =` locals; other indirections
  (members, function arguments) are not scanned; the run-time log would still name them.
- Correction 2026-11-10 (epic 2300, worker 2301) [C]: of the 11 methods in 11.3, `HasReceivedTurnAllComplete`
  (`CvPlayer.cpp:34130`), `isPopupQueued` and `setForcePopup` (`:34283-34285`) occur only in commented-out code; 8 are
  real calls. The same local-pointer scan also finds `ICvEngineUtility1::Destroy` (through `pOldDll`). The proxy
  generator crosses every engine method regardless.

## 12. Remote mode: the server behind the proxy (epic 2300, 2026-11-10)

`server.exe --remote --section <name> --config <cfg>` (code `server/src/remote/`, README "Remote mode"; design and
results `proxy-design.md` sections 11-12). Differences from headless mode [C code, L runs]:
- The DLL's engine interfaces are the generated `RemoteGen_<I>` objects that forward to the proxy; hand-written
  subclasses keep server-local: the game-core lock (the same recursive lock per thread), `IsGameCoreThread` (true only
  inside the dispatch of `Update`), `IsGameCoreExecuting` (false). File system, log files and the 22 host exports are
  the headless ones; the engine's cache folder (`GetCacheFolderPath`, forwarded) is a second write root of the file
  system (a load writes the save's database there).
- The net loopback of section 11 does not run: the `send*` calls cross and the engine loops them back.
- The database: the server copies the file the proxy names (`vpproxy.ini db=`) into `<run>\db\` and opens it
  (`proxy-design.md` 11.7).
- Lua: one scratch `lua_State` per service thread with the DLL's libraries; the DLL's own threads (`CreateLuaThread`)
  are scratch threads whose globals come from the engine thread (`proxy-design.md` 11.5).
- Reports: `server-report.txt` (unserved and unimplemented methods), `proxy-calls.tsv`, `proxy-calls-per-turn.tsv`;
  `fail_on_unexpected=1` exits with code 4. No `turn-times.tsv` (the host times turns).
- Allow-list (issue 9008): `unexpected_allow=<file>` (written by `run_server.py --remote-config --remote-allow FILE`;
  the harness passes `proxy/tools/remote-allowlist.txt`): an unserved method the list names is logged ("allow-listed
  by ...") and does not end the run under `fail_on_unexpected=1`; the entries are printed at the top of the report.
  A key matches the method's key, the key without its signature, or with a trailing `*` a prefix. Before 9008 the
  harness never set `fail_on_unexpected=1` (it passed the direct mode's `--fail-on-unserved`, which `--remote-config`
  ignored; `run_server.py` now refuses the other mode's flag).
- Result: the headless host through the proxy and this mode gives `stats.db` identical to the direct server for Tiny
  0-50, the Tiny turn-50 save 50-60 and the huge save 350-360 (`server/evidence/2300/`); strict re-runs of the Tiny
  proofs: `server/evidence/9008/`.

## 13. The first call of a delay-loaded import loses float arguments (issue 9008, 2026-11-10) [C disassembly, L run]

The server links `lua51_x64.dll`, `CvGameDatabase_x64.dll` and `CvLocalization_x64.dll` with `/DELAYLOAD`
(`server/build_server.py`; lld-link **19.1.7**, `delayimp.lib` of MSVC 14.43). LLD's x64 delay-load thunk
(`__tailMerge_<dll>`, disassembled in the 9008 `server.exe` at 0x1400d8762) is `push rcx, rdx, r8, r9; sub rsp, 0x48;
movdqa [rsp], xmm0; movdqa [rsp+0x10], xmm1; ... [rsp+0x30], xmm3; call __delayLoadHelper2; movdqa xmm0..3 back; ...;
jmp rax`. It reserves no 32-byte home area for the callee, so `__delayLoadHelper2` (MSVC code, which may spill
registers into its home area) can overwrite the saved xmm0 and xmm1 [the spill itself is inferred from the observed
value, not traced]. The first call of each delay-loaded
function therefore reaches the function with xmm0/xmm1 replaced by whatever the helper spilled; later calls go straight
through the bound IAT and are correct.

Seen [L, 2026-11-10, runs `diag-t1*` of 9008]: in remote mode the server's first `lua_pushnumber(S, 1.0)` (the engine
thread id stored as the upvalue of a scratch thread's `__index`, `srv_remote_lua.cpp` CreateLuaThread) stored the bits
of the `lua_State*` the caller kept in a non-volatile register (read back as 1.06e-311 = 0x000001F3'04E30A60, the
thread's address); a second `lua_pushnumber` right after stored 7.0 correctly; a warm-up call earlier moved the
corruption to the warm-up. The scratch thread's globals then asked the proxy for engine thread id 0: the "served call
failed: status 16: LuaGetGlobal: unknown engine Lua thread id" of every epic 2300 run (proxy-design 12, corrected).

Fix in remote mode (9008): `RemoteMain` calls `__HrLoadAllImportsForDll` for each of the three libraries right after
loading it, so the thunk never runs (`server.log`: "delay-loaded imports of ... bound up front"). **Open, not in 9008's
files: the direct server (`srv_main.cpp`) has the same latent bug** for the first call of any delay-loaded function
with a float or double argument (`lua_pushnumber`; CvLocalization's number formatting); the fix is the same three calls
after its `LoadLibraryExW` of each library. Whether a direct run ever pushed a wrong first number was not checked
(direct runs matched the 32-bit reference and the proxied runs on every stats.db table, so if it happened it did not
reach game state there) [I]. The x86 proxy imports `lua51_Win32.dll` normally (no delay load). A newer LLD may reserve
the home area; not checked.

## 12. Cross-host writer identity: the 32-bit host and the server resave alike (issue 2402, 2026-11-10)

Measured 2026-11-10 12:20-12:42 EST by team-2400 worker A. Evidence: `tools/d10/evidence/2402/` (its README has every
hash and command; the tool is `xhost.py`). This is D24 part 1 (writer identity): the x86 DLL in the 32-bit host (c5h,
used as a tool) against the x64 DLL in the server. Both DLLs are built from the same source, so only host differences
can remain.

**Result [L]: with the changes below, the two builds' resaves of the same input are whole-file identical on all three
inputs.** Header, pregame and inflated body all match, the SHA-256 is the same, and nothing is masked. The inputs:
Tiny Continents turn 20 (13,135,893 B body), Standard Continents turn 20 (16,877,579 B) and the huge save at turn 350
(37,733,946 B). Both builds carry the same version string (`1.0.0`), so no exception was needed.

Builds: fork 3cfa98347 (x64 merged with i0-ship) **plus a one-line DLL fix** (12.1), on the THROWAWAY branch
`throwaway-2400-a-fix`, never pushed. x86 `d0d4d405...3ebea2` (`build_vp_clang.py --config release`); x64
`8a52094a...b2015d` (`tools/x64-dll/build.py --unlocked --shorten`); server.exe with the two server changes
`06058acb...dc1111`.

### 12.1 A DLL format break in 3cfa98347 (found here; not a host difference) [C, L]

The x64 build of 3cfa98347 cannot load a save written by the x86 build of the same commit: `CvPlayer::Read` throws in
`m_ReplayData` (`unfixed-x64-loads-x86-tiny.server.log`). Cause: i0-ship's `CvReplayTurnData` serializer
(`CvPlayer.cpp:42575-42611` at 3cfa98347, `operator<<` and `operator>>`) streams the entry count as `size_t`. That is
4 bytes on x86 and 8 bytes on x64 (`FDataStream::Write(const UINT64&)`). The x64 port keeps every container count at
32 bits (`FStlContainerSerialization.h`, "32-bit in the save format"), and this new type bypassed that. It is the only
`size_t` count the merge added (diff e9d0c4837..3cfa98347). The fix used here is `unsigned int count` in both operators,
which leaves the x86 bytes unchanged. The real fix belongs on the fork's x64 and ship branches (coordinator informed
2026-11-10 12:27).

### 12.2 The four host differences: cause, which value the EXE writes, fix

| Difference (epics 1500, 2000) | Cause | The EXE | Fix |
|---|---|---|---|
| Player 0's option list `CvPlayer::m_aOptions`: 12 entries vs 23 (+88 B) | Both hosts answer `SendPlayerOptions(true)` with 12 `ResponsePlayerOption` calls (`CvGame.cpp:6249-6274` sends `FStringHash(type)`). c5h delivers the **engine's option index** for the 11 options the EXE knows (PlayerOptions rows 0..10) and the hash only for row 11 (`host/engine/HHEngineServices.cpp:191-205`, measured in trace S3b). The server delivered the hash for all 12 (`server/src/srv_ifaces.cpp`, `sendPlayerOption`). `CvPlayer::setOption` (`CvPlayer.cpp:38199-38213`) updates by key, so the saved index keys stayed and 11 hash keys were appended [C] | **Index keys** [C by dump]: the real-EXE huge save holds `{0..10, -542730216}` in both its option lists (body offsets 14,784,263 and 25,620,289). With hash keys, the server's `isOption(PLAYEROPTION_SAFE_AUTOMATION)` and the other enum-index lookups never matched | **Server change** (`srv_ifaces.cpp`): deliver `GetInfoTypeForHash(hash)` when it is 0..10, else the hash |
| One `CvUnit::m_iEverSelectedCount`, 2 vs 1 (the "flag") | After a load, c5h's first in-game frame calls `UpdateSelectionList` (`host/setup/HHNewGame.cpp`, `HHGameStartedSteps`; its UI answers `DoAutoUnitCycle` true). `CvGame::updateSelectionList` (`CvGame.cpp:2011-2057`) calls `cycleUnits(true)`, then `findUnit(0,0,active,true)`, then `selectUnit`, then `IncrementFirstTimeSelected` (`CvUnit.cpp:29780-29789`). For a loaded game the server called nothing (`srv_main.cpp`, `PhaseGameStarted`), and its UI stub answers `DoAutoUnitCycle` false [C, L] | Selects a unit after a load [I]: the UI shows a selected unit. c5h's load sequence is reconstructed, not traced (saves.md 4) | **Server change** (`srv_main.cpp`): for a loaded game, `SelectSettler` on turn 0 and `CycleUnits(true)` otherwise. That is what `updateSelectionList` does when nothing is selected |
| One int per player = the turn (350, 20) vs the saved value; one player also 5 vs 0 | c5h's UI emulation profile `base-demographics` runs Demographics.lua `BuildTables` behind the load screen. That calls `GetMilitaryMight` for every major. The lazy cache `CvPlayer::updateMightStatistics` (`CvPlayer.cpp:32556-32563`) then sets the serialized `m_iTurnSliceMightRecomputed` to the turn and recomputes the mights (one player's `m_iEconomicMight` goes from 0 to 5). The server runs no UI Lua [C; L: gone with `--no-profiles`] | The EXE's Demographics context does the same behind the load screen [I] (c5h's profile). With the shell attached, the EXE's UI Lua would do it through the proxy | **c5h option** `--no-profiles`: neither host runs UI Lua |
| Header DLC list: c5h 17 entries vs the save's 13 | c5h writes the keyed, non-upgrade packages of its game folder (`host/setup/HHSaveGame.cpp:382-456`). On the toolkit folder that adds "VP Modpack" (`Assets/DLC/VP_MODPACK_5_4_8_NEUTRALS`) and the three "Vibrant Textures" packages (`Assets/DLC/VT`). The server keeps the loaded save's list (section 9). Inputs that c5h wrote on the same folder already carry its list, so only foreign saves differ [C, L] | Not established for a loaded game [I]. The real game leaves VT out (no `<SteamApp>`, section 9), so on the owner's install both readings give the save's 13 | **c5h input**: for the huge save, `--game-dir` = a copy of the toolkit folder without `Assets/DLC/VT` and `Assets/DLC/VP_MODPACK_5_4_8_NEUTRALS` (125 MB, in scratch) |

Control [L] (`tiny-origsrv`): the unchanged server, with the fixed DLL and c5h `--no-profiles`, still shows exactly the
first two rows (count 12 vs 23 with the 88 B, and one byte 2 vs 1). The changed server shows neither (`tiny-2`). With
profiles on, the might values are the only difference left (`tiny-1`). On the huge save with the toolkit folder, the DLC
list is the only difference (`huge-2`: pregame and body identical).

Side effect of the option fix [L]: a new Tiny game played to turn 50 by x86 and x64 with the changed server still gives
identical `stats.db` files (15 tables, 9,257 rows, `pair-tiny50-compare.txt`). A human player's `isOption` answers now
match the EXE's.

### 12.3 Commands (for `tools/d10/run_d10.py`)

Both arms load the input from the **same absolute path**: the pregame stores the load file name, and c5h and
`run_server.py` both pass `Path.resolve()`.

- x86, c5h (working directory `%USERPROFILE%/Documents/civ5-headless`): `cmd /c c5h.cmd run --load SAVE --turns T+1
  --no-save-final --lua-probe tools/i0/save-now.lua --dll X86 --detail summary --run-dir RUN --game-dir GAMEDIR
  --no-profiles`. For a save without mods, add `--no-mods`. For a save with mods, add `--mods-dir MODS --no-addins --db
  civ5-headless/pinned/vp-modpack-5.4.8-cdrift/Civ5DebugDatabase.db` and `--mod <id>:<version>` for each mod in the
  header. The resave is `RUN/saves/AfterLoad.Civ5Save`, and T is the header's turn. GAMEDIR is the toolkit folder for
  c5h-made inputs and the 13-DLC copy for the huge save (a game folder whose DLC rule yields the input's list).
- x64, server: `python server/run_server.py --load SAVE --turns T --run RUN --bin BIN --save-at-start AfterLoad`.
  `--turns T` means no turn is played. The resave is `RUN/AfterLoad.Civ5Save`.
- Compare: `python tools/d10/evidence/2402/xhost.py diff A B`, or `xhost.py resave` to run both arms and the diff.
- Mods: the fork's source-tree folders `(1) Community Patch` (v151) and `(2) Vox Populi` (v17) give a mods registry
  identical to epic 2000's MODS copy in every file and md5 (2,134 `ModFiles` rows; only the evaluated path differs)
  [C]. With the pinned database they only provide the registry and the VFS.

### 12.4 Not verified

- The EXE's own load sequence (the selection after a load, Demographics behind the load screen, the DLC list it writes
  for a loaded game): taken from c5h's reconstruction and from source, not traced.
- Saves with a human player whose `PLAYEROPTION_*` settings matter, other map sizes, saves with D18 against a DLL
  without it, debug builds.
- Standard and huge play with the changed server (only Tiny 50 was compared).

## 14. The map script and every relative path resolve by file name (bug 0100, 2026-11-13)

**Defect [C, L].** `VfsResolve` (`server/src/srv_vfs.cpp`) took a relative path that existed under the game folder
literally and used the index only as a fallback. The pregame's map script `Assets\Maps\Communitu_79a.lua` (spec
`{"map": {"script": "Assets/Maps/Communitu_79a.lua"}}`) therefore loaded the base game's old copy, which calls
`Plot_GetFertility` from VP's `AssignStartingPlots.lua` and fails map generation (one-off 9034, 2026-11-11, quiet
testbed job `20261111-140107-9729`), while c5h loaded VP's `Assets\DLC\VP_MODPACK_5_4_8_NEUTRALS\Mods\(2) Vox
Populi\Mapscripts\Communitu_79a.lua` and played the game. Given the package path instead, the server ran VP's copy but the
two hosts still had different maps from turn 0 (`../06-tooling-and-workflows/quiet-testbed.md` 14.7). Scripts that exist
once (Continents, Pangaea, Fractal) did not show it. `GetEvaluatedMapScriptPath` (`srv_ifaces.cpp`) copies the path, as
c5h's does; it was not the cause and is unchanged.

**The rules compared.**

| Who | Relative path such as `Assets\Maps\X.lua` | Bare name | Absolute path | Source |
|---|---|---|---|---|
| The engine (EXE) | through the VFS with `include` precedence: one file per name, the higher package wins [M in the source] | same | - | `research/sources/civ5-headless/engine-contract.md` 10.3 and the new-game trace ("`LoadFile` resolves the relative path through the VFS (`include` precedence)") |
| c5h | 2. VFS by name (key = file name, lower case, `.lua` dropped) before 3. the literal path under the game folder; its comment: the real game loads the activated mods' `Highlands.lua` for `Assets\Maps\Highlands.lua` | VFS | as it is (re-rooted to `--mods-dir`) | `~/Documents/civ5-headless/host/lua/HHScriptSystem.cpp` `ResolveFile`, `host/setup/HHVfs.cpp` `Key` (read 2026-11-13) [C] |
| c5h's spec | `map.script` must equal a `MapScripts.FileName` or be a unique path suffix of one; the pregame keeps that string and `LoadFile` resolves it | - | - | `python/civ5headless/spec.py` `_find_file` [C]; why a package path is refused there (`SpecError: ... matches 0 entries`) |
| Server before 0100 | literal path if the file exists, else by name | index | as it is, else by name | `srv_vfs.cpp` at 0388c13 |
| **Server since 0100** | **by name through the index first, the literal path only when the index has no such name** (a file outside `Assets`, or in `DLC\Expansion`/`DLC\Tablet`, which the index leaves out) | index | as it is, else by name (unchanged) | `srv_vfs.cpp` |

**Which callers changed, and why all of them.** `VfsResolve` serves `LoadFile` (the map script; the world-builder loader's
`WorldBuilderRandomItems.lua`, a bare name) and `include`. The rule changed for every relative path, not only the map
script, because the knowledge base states the engine's rule for `LoadFile` as a whole (engine-contract 10.3: relative
paths through the VFS, include precedence) and c5h, which matches the real game turn for turn, applies it to every
`LoadFile`. Bare names (all includes seen so far) resolve as before; only a relative path whose name a package also
holds now gets the package's copy. `run_server.py` passes `map.script` unchanged, so a spec may name the script by base
path (as c5h's spec requires) or by package path; both now load the same file.

**Unit test [C, 2026-11-13].** `python server/tests/run_vfs_test.py [--vfs-src FILE] [--out DIR]` compiles
`srv_vfs.cpp` alone (a stand-in `srv_common.h`, clang-cl x64) against a fake game folder with `Communitu_79a.lua` in the
base game, a priority-5 and a priority-10 package, and runs 17 cases (11 positive, 6 negative: a name only in a
left-out package, missing relative/bare/absolute names, empty and null). Unfixed `srv_vfs.cpp` (0388c13): 14 passed, 3
failed, first failure `FAIL relative base path, name in two packages + base name="Assets/Maps/Communitu_79a.lua"
want=vp got=base`; fixed: 17 passed. About 5 s with the vcvars cache. Output kept in
`D:/c5-64bit-work/4000/0100/vfs-test-{unfixed,fixed}.txt`.

**Live check [L, 2026-11-13 02:44-02:46 EST, workstation].** Spec `server/tests/specs/communitu79a-large-5005-6006.json`
(Large, `Assets/Maps/Communitu_79a.lua`, seeds 5005/6006), one turn, AI logging off, toolkit game folder
`VP_MODPACK_5_4_8_NEUTRALS`, default pinned databases. `python tools/drift/run_pair.py --spec SPEC --turns 1 --out OUT
--tag large-communitu --only x86` (c5h, x86 DLL pin `i0ship-bbfacca1`), then `--only x64 --srv-bin` the pin's `bin`
(`x64-76992d23`, server.exe `3a35330a...`, unfixed) and a copy of it with only server.exe replaced by the fixed build
(`D:/c5-64bit-work/4000/0100/build/bin/server.exe`, SHA-256 `4cd69816b804223737c61797c2b68362688ed625d95e69f29f7e3ecde052bdfb`,
built 2026-11-13 02:44 from this branch with `--deps` of 9036's pin build; DLL `fa75f483...` in both), each through
`quietlock.py run --who 4000`; then `python server/tests/check_map_script_pair.py --x64-run OUT/large-communitu-x64
--x86-run OUT/large-communitu-x86`. Results (`D:/c5-64bit-work/4000/0100/checks.txt`, `compare-{fixed,unfixed}.txt`):
- c5h loaded `Assets\DLC\VP_MODPACK_5_4_8_NEUTRALS\Mods\(2) Vox Populi\Mapscripts\Communitu_79a.lua` (`host-report.json`).
- Unfixed server: `LoadFile("Assets\Maps\Communitu_79a.lua") -> <game>\Assets\Maps\Communitu_79a.lua`, then
  `CallFunction error: Assets\Maps\Communitu_79a.lua:547: attempt to call method 'Plot_GetFertility' (a nil value)`,
  run_server exit 2; checker FAILED (checks 2-5).
- Fixed server: `LoadFile` -> VP's copy, the same file as c5h's; exit 0; `MapUnitsState` turn 0 (30 starting units with
  their plots) and `MapPlotsState` turns 0-1 (191 rows) equal; `server/tools/compare_stats.py` on the two stats.db:
  IDENTICAL in every table (302 rows). Same map on both hosts.
- `MapPlotsState` logs only changed plots: turn 0 is one sentinel row for any map (the unfixed run, which generated no
  map, has the same row), so it is no map comparison by itself; the checker uses the starting units of turn 0.
Not verified: the game played past turn 1 in the server (the 9034 pair rerun is quiet-testbed.md 14.8 item 3).

## 15. Remote mode: a failure before the attach is reported and the server exits (bug 0128, issue 3309, 2026-11-13)

**Defect [L, C code].** In remote mode (section 12) `RemoteMain` (`server/src/remote/srv_remote_main.cpp`) loads the
allow-list, Lua, the database layer and the localization, then attaches to the proxy's section, and only then calls
`SrvRemote::Init` (`srv_rt.cpp`), which initialises the critical sections of the call counters (`s_csCount`), the
unserved-method table (`s_csUnserved`) and the stream sessions. Every failure before that point ends in
`SrvRemote::ReportAndExit(1, ...)`, whose `WriteReport` called `UnservedCount()` (enters `s_csUnserved`), `ReportCounts`
and `WriteCallTables` (enter `s_csCount`). On a zeroed `CRITICAL_SECTION` that faults: server.log shows `EXCEPTION
0xC0000005 in remote mode at ntdll.dll+0x1a12d (write of 0x24)` [L]. The unhandled-exception filter then called
`ReportAndExit(2, ...)`, which sees the exit already in progress and sleeps forever (it waits for "the other thread"),
so the server never exited and wrote no report, and the proxy waited out its attach timeout (60 s; one-off 9045 met
it as 42 of 55 `test_proxy_rt` failures after 0123 made `loc_db` default to the game's database, whose fake game folder
has no `Locale/icudt44l.dat`). The same held for every pre-attach failure, not only the localization.

**Fix.** `srv_remote_main.cpp`: a flag set right after `SrvRemote::Init`; before it, `WriteReport` writes the report
without the Init-owned parts (no unserved count, no `ReportCounts`, no `proxy-calls.tsv` / `proxy-calls-per-turn.tsv`)
and says so in the line `report: written before the attach (SrvRemote::Init did not run): no call counts, no call
tables`. Second, the filter: an exception on the thread that is already inside `ReportAndExit` ends the process at once
with exit 2 and the line `remote: LOUD: an exception while writing the report; exit code 2 without the report`, so a
future fault in the report can no longer hang the server. Nothing after the attach changed.

**Test [L, 2026-11-13 11:50-11:54 EST].** `python server/tests/run_preinit_report_test.py --bin <build>/bin --out DIR`
(started through `quietlock.py run`) runs `server.exe --remote` without a proxy, with a server.cfg that fails at each
pre-attach point: `loc_icu` (loc_db an SQLite file, ICU data missing: 0128's own case), `loc_db` (missing), `lua`
(lua_dll missing), `allow` (unexpected_allow missing), `attach` (no section; connect_timeout_ms 1500). Per case it
checks exit code 1 within 30 s, `result: FAILED: <reason>` and the "before the attach" line in server-report.txt,
`remote: exit code 1 (` and no exception line in server.log, and no proxy-calls.tsv.
- Unfixed (team-3300 wave-2 build `D:/c5-64bit-work/3300/w6/b-b1`, server.exe SHA-256 4461958a...a874): **0 of 5**;
  every case faulted (the line above, 0.05-1.5 s after the start), hung, was killed at 30 s, and wrote no report.
- Fixed (build `D:/c5-64bit-work/3300/w9/b1`, the sources of commit e126aba; its build id says b0d001f dirty because the build started just before that commit; DLL pin 76992d235 from 9036's
  `src-76992d23` and `deps`, server.exe SHA-256 faacf2b9...b172): **5 of 5**, exit 1 in 0.2-1.8 s, e.g. `result:
  FAILED: localization: Localization::Initialize failed (ICU data ...\Locale\icudt44l.dat, language en_US)`.
Outputs: `D:/c5-64bit-work/3300/w9/preinit-{control,fixed}.txt` and the run folders beside them (not committed).
The proxy side of 0128 (the test's fake game folder, `test_proxy_rt.py`) is one-off 9045's. The proxy notices a server
that exits before it attaches (`proxy_rt.cpp`: "boot FAILED: the server exited with code ... before it attached"), so
with the fix the 60 s wait becomes the server's own start-up time [I: not run through the proxy here].

## 16. Remote mode: the server's RETURN ring releases large payloads (issue 0119, 2026-11-13)

**Defect [C code, 2026-11-13].** `KeepReturn` (`server/src/remote/srv_rt.cpp`, called by `SrvRt::End` for every call the
server makes to the proxy that returns Ok) copies the RETURN payload out of the channel's data area into the next of 64
per-thread `std::vector<uint8_t>` slots (the payload must survive a nested call made while it is decoded). It did
`b.assign(payload)`, and `assign` never gives capacity back: a slot that once held a large RETURN kept that capacity for
the life of the thread, and a slot that grew by a little grew to 1.5 times its old capacity (MSVC growth). The 64 slots
were a `new[]` array that was never freed when the thread ended. The same pattern as the proxy's ring before issue 9041
(`proxy-design.md` section 18.7). It is the server's own 64-bit memory, not the EXE's address space. Which RETURNs the
server receives that are large is not measured [I]: they are engine answers and `kRtE_StreamRead` chunks (a pull
session fetches exactly what the DLL reads, `PullStream::Ensure`), so the huge-save pattern of the unit test below is the
proxy's pattern, used as a stress case, not a measurement of the server.

**Fix.** `KeepReturn` applies the rule of `ProxyLogic::ReturnRing` (`proxy/src/proxy_rt_logic.h`; not linked into the
server, which does not build the proxy's logic file): a slot whose buffer is above 64 KB is released before it is reused,
and a payload above 64 KB gets a buffer of exactly its size. The slots are a function-local `thread_local` array of 64
vectors, destroyed when the thread ends. A payload stays valid for the next 63 calls of its thread, as before.

**Unit test [C, offline, 2026-11-13].** `python -I server/tests/run_rt_ring_test.py [--rt-src FILE | --rev REV] [--out
DIR]` (about 10 s; started through `quietlock.py run`) copies `KeepReturn` out of `srv_rt.cpp` (from its signature line to
the first lone `}`), compiles it with `server/tests/test_rt_ring.cpp` and `ipc/src/marshal.cpp` (the real
`vpipc::Reader`), clang-cl x64 /O2, and counts the live bytes of a replaced global `operator new`/`delete`. Each scenario
runs on a new thread (a fresh ring); the counter is read again after the thread has ended. Results:

| Check | Fixed (this branch) | Unfixed (`--rev b1d9198f`) |
|---|---|---|
| one 12,662,545-byte RETURN (the huge save's map body), then 64 small: held bytes at most 64 x 64 KB | 0.001 MiB | **12.078 MiB** |
| 20 AI turns of the huge-save RETURN pattern (proxy unit test `RingSim`), then 64 small: at most 64 x 64 KB | 1.152 MiB | **476.8 MiB** |
| peak during the 20 turns below 64 MB | 13.4 MiB | **476.8 MiB** |
| a slot that held 4 MiB takes a 5 MiB payload: buffer of exactly 5 MiB; released when the slot is reused | 5,242,880, then 1,000 B held | **6,291,456** (1.5x), 6,293,939 B held |
| bytes left allocated after the thread ended (scenario 1 / scenario 2) | 0 / 0 | **12,664,836 / 499,955,415** |
| every copy intact for the next 63 calls; reader on the copy, length unchanged; empty payload gives NULL | pass | pass |
| total | **10 pass, 0 fail (exit 0)** | **3 pass, 7 fail (exit 1)** |

Build and suites [C, 2026-11-13 21:03-21:07 EST]: `server/build_server.py --src D:/c5-64bit-work/9045/src-x64f --deps
D:/c5-64bit-work/9045/bF/deps --out D:/c5-64bit-work/0119/b1 --config release --offline` (D96 pins) OK, server and proxy;
on that build `proxy/tools/test_proxy_rt.py` 79/79 (unit 76/76, shell unit 152/152; its loader cases load and unload the
proxy, so they pass through the new `DLL_THREAD_DETACH`/`DLL_PROCESS_DETACH` code), `run_vfs_test.py` 17/17,
`run_power_test.py --server-exe` OK. Not run: a game or a live server session with the fix [I: no live measurement of the
server's memory].

`proxy_local.cpp` (same issue): the per-thread `ThreadLocal` (the local-wrapper free lists, the thread's own wrappers,
the canonical-handle cache) was never freed when an EXE thread ended (`proxy-design.md` 18.7, "Not changed"). Now
`ProxyLocal::FreeThread()` deletes it on `DLL_THREAD_DETACH` and on the unloading thread at `DLL_PROCESS_DETACH`, after
`FreeReturnRing` and `FreeThreadState`. No behaviour changes: no other thread can reach a thread's `ThreadLocal`; the
pooled wrappers in its free lists stay registered in `g_local` as before (wrappers are never freed); a wrapper the ended
thread still held is released, if ever, on another thread, which takes the foreign-release path with or without this.
Not measured live [I]: the size this saves per ended thread (maps of a few entries).


## 17. A load recalculates the landmasses: the EXE's body reader, the server and c5h (issue 0452, 2026-11-16)

Found by ticket 4655 (epic 0437, branch `epic-0437-turn-study`, `research/08-performance/turn-time-study.md` there,
section "Why the headless hosts and the clients diverge"): the plots' water flags (`CvPlot::m_bIsAdjacentToLand`,
`m_bIsLake`, `m_bIsFreshwater`) are not in the save; `CvMap::recalculateLandmasses` (CvMap.cpp at 26234371e) starts with
`updateWaterFlags` on every plot; the EXE calls `ICvMap1::RecalculateLandmasses()` once per load, and neither headless
host did. Readers with the default `bUseCachedValue = true` then read false everywhere, and the game diverged from the
client's at turn T's first AI moves. New games are not affected (map generation runs `recalculateAreas`) [C].

### 17.1 Where the EXE makes the call [C, disassembly of the DX11 EXE, 2026-11-16]

The EXE's save-body reader (one function, DX11 build TimeDateStamp 0x546CD0A8, at 0x71BFE0) calls, in this order:
`ICvGame1::Read` (vtable +0x12C), `ICvMap1::Read` (+0x44), `ICvTeam1::Read` (+0x40) for teams 0-63, `ICvPlayer1::Read`
(+0xBC) for players 0-63, **`ICvMap1::RecalculateLandmasses` (+0x40 on the map)**, `ICvGame1::ReadSupportingClassData`
(+0x134; taken when a version value of the body is at least 2, or is 1 with a flag set; otherwise a second
`ReadSupportingClassData` from another stream follows below), `ICvMap1::Validate` (+0x4C), then
`ICvGame1::LogGameState(true)` (+0xD8). Slots from `proxy/model/interfaces.json` (`slot.x86`). The map object of the
`Read` and of the `RecalculateLandmasses` call is the same one; `Validate` is called on a map pointer fetched again.
This matches the counts of 0438's S4 run (c5h-b `Logs/VP64/run/20261116-165806-31924-1/proxy-calls.tsv`: one call each of
`RecalculateLandmasses`, `Validate`, `LogGameState`). Method: `llvm-objdump -d` of the analysed copy
(`%USERPROFILE%/tools/ghidra-projects/bin/CivilizationV_DX11.exe`, never the installed EXE), windows where the four vtable
offsets appear together (one hit). Nothing of the listing is kept (D83).

`LogGameState(true)` writes the load-time statistics rows (MapPlotsState, MapUnitsState, WorldStateLog, civ_turn_era at
turn T): the reason a headless run's turn-T rows are missing against a client's (4655). `Validate`, `LogGameState`,
`InitScoreCalculation`, `SetFinalInitialized` and `SetBusyUnitUpdatesRemaining` stay unmade by both hosts; they did
not change identity on these saves (4655; below).

### 17.2 The change

- **Server, direct mode** (`server/src/srv_save.cpp` `LoadParts`): `GetMap()->RecalculateLandmasses()` after the 64
  player reads and before `ReadSupportingClassData`, as the EXE. Remote mode (S4, the product) never runs `LoadParts`:
  the EXE makes its own call through the proxy.
- **c5h** (MC #3279): one line in `host/setup/HHSaveGame.cpp` at the same point (`HH_STEP("Map.RecalculateLandmasses()",
  ...)`) and the load steps in its `docs/saves.md`; branch `fix-recalculate-landmasses-after-load` (60716ff, on 00ce9f6,
  the source of the in-use build) in `%USERPROFILE%/Documents/civ5-headless` and on the fork only. Built with the
  in-use toolchain (VC9) into a new folder beside the in-use one:
  `%USERPROFILE%/Documents/civ5-headless/out/Release/20261116-0452-landmasses/civ5headless.exe`, SHA-256
  `bb8d35426b1f9b7a1bcf9e662a3175d366b42bc83fdbdbc69e0d5c5d976165f7`. `out/latest.txt` still names the in-use build
  `20261027-120431` (`76237943...F361`); the switch is the MC's (17.5). Until then a run picks the fixed host with
  `c5h run --exe <that path>`.
- **Fallback for builds without the call** (old server builds, the in-use c5h): pass 4655's probe
  (`perf/evidence/4655/probe-waterfix.lua` on branch `epic-0437-turn-study`: `Plot:IsAdjacentToLand(false)` on every
  plot, which runs `updateWaterFlags`) as `--lua-probe` to `server/run_server.py` or c5h; it does not redo the landmass
  ids.

### 17.3 Results [L, 2026-11-16 23:35-00:10 EST, workstation]

Inputs: 0438's scenario saves `TS0438-M-T150` (Standard Communitu, `98fc454a...`) and `TS0438-H-T300` (Huge Continental
Drift, `308a96bb...`) from `F:/c5-64bit-work/0438/saves/`; the client database of 0438
(`F:/c5-64bit-work/0438/clientdb/stats-on/Civ5DebugDatabase.db`), the toolkit game folder, AI logging off. Server: the
x64 pin's layout `x64-04b1211b-pfdee656` with this ticket's server.exe (`fc68b75b...`, repository a91e0c3b3, release,
build `F:/c5-64bit-work/0452/layout-new`) and its proxy (`9afb4f30...`); c5h: the fixed exe above with i0-ship
`i0ship-26234371` (`f92881a3...`), `--no-addins --no-profiles`. References: 4650's windowed runs (F:/c5-64bit-work/4650/runs)
`S2i-M-1` (stock EXE, i0-ship instance-aware, 150 -> 175), `S4-M-1` (the patch, pin layout, 150 -> 175), `S2i-H-1` and
`S4-H-1` (300 -> 310). Comparison: `tools/d10/statscmp.py --from-turn T+1 --expect-turns T END` (the turn-T rows are
the client's `LogGameState(true)` at the load, 17.1, which neither host makes). Run folders and reports:
`F:/c5-64bit-work/0452/runs/`, `F:/c5-64bit-work/0452/cmp/`.

| Headless run (no probe) | against | result |
|---|---|---|
| server new, M 150 -> 175 (1.39 s/turn) | S2i-M-1 (windowed, x86) | IDENTICAL, 15 tables, 87,956 rows |
| server new, M 150 -> 175 | S4-M-1 (windowed, the patch) | IDENTICAL, 87,956 rows |
| server new, H 300 -> 310 | S2i-H-1 / S4-H-1 | IDENTICAL, 14 tables, 210,707 rows (each) |
| c5h fixed, M 150 -> 175 | S2i-M-1 | IDENTICAL, 87,956 rows |
| c5h fixed, H 300 -> 310 | S2i-H-1 | IDENTICAL, 210,707 rows |
| c5h fixed, M / H | server new, M / H, **from turn T** | IDENTICAL, 89,290 / 224,383 rows (headless = headless including the load turn) |

Before the change (4655): the same headless runs differed from the clients at turn T's 11th unit move.

Cached water flags at the arm (`server/tests/load/run_load_water_test.py`, read without recomputing): server new M
712 / 15 / 392 (adjacent to land / lake / fresh water, of 4,187 plots), H 1,907 / 62 / 796 (12,320 plots): the
client's counts (4655); the pin's server.exe (`5b254430...`) M 0 / 0 / 0.

**A load-then-save changes in the map section** [L]: the server loads M and saves before any turn
(`--save-at-start`): new and pin bodies (21,495,002 B each) differ in 7 bytes in 5 regions (body offsets 6,748,499,
6,838,527, 7,018,185, 7,063,034, 7,152,157). At those 5 offsets the save's own body (written by the client) equals the
pin's resave, not the new one. So the recalculation changes saved map values (the landmass part of the call, which
4655's probe did not do: the ids or counts it renumbers [I]). Not verified: whether the EXE's own load-then-save shows the
same 5 regions (a client resave of M would tell); play is unaffected over 25 / 10 turns (the table). Consequence: a
writer-identity test of a loaded save (D10 part 1) passes only when both hosts make the call or neither does.

**The product path is unchanged (parity pair)** [L, 23:48-23:57 EST]: S4 (the stock EXE on instance c5h-b, the proxy
and server.exe remote mode) with this ticket's layout (server `fc68b75b...`, proxy `9afb4f30...`, the pin's x64 DLL
`29f58e17...`; `provision.py set-proxy`, removed again with `--original`, `verify --quick` rc 0), 4650's recipe
(`perf/turnstudy/premises/clientrun.py channel --stats`, cp,vp,eui, M 150 -> 175, no dialog), against 4650's `S4-M-1`
(pin layout, server `5b254430...`, proxy `75c40cec...`): **IDENTICAL from turn 150 on, 15 tables, 90,457 rows**, the load
turn's rows included; and equally against `S2i-M-1`. Remote mode never runs the changed code (17.2); this shows it.

Suites (2026-11-17 00:07, `server/evidence/0452/suites-summary.txt`): `server/tests/load/run_load_water_test.py` new M
and H pass with the client's counts, the pin's server.exe fails it (exit 1, the negative case), a missing bin exits 2;
vfs, rt-ring, preinit, stdout, power, crashdump plain and ASan, player limits plain and ASan, feed plain and ASan, remote
feed plain and ASan: all pass. Not under ASan: `srv_save.cpp` itself (server.exe has no ASan build; the call is one DLL
call on objects the load already uses).

### 17.4 Which earlier headless results this touches (for the MC) [C for which runs load a save; I for the effect]

Every headless run that **loads a save and plays turns** (server direct mode `--load` / `load_save`, c5h `run --load`
or `loadgame`) played without the water flags until something refreshed a plot (readers with `bUseCachedValue =
false` refresh single plots). New games (map generation) and load-then-save without a turn are not affected in play;
a load-then-save does change in 5 regions of the map section (17.3). Both hosts lacked the call alike, so headless x86
= x64 identity results stay true as identity statements; the games were not the player's game from that save.

- **D10 save tests (`tools/d10/run_d10.py`)**: the huge game (350 -> 400 from `E2000-HUGE_0350`) and every continuation
  of an early save (tiny 25 -> 100, standard 50 -> 200, huge 375 -> 400). **Re-run** after the switch (17.5). Until
  both hosts switch together, a new server against the in-use c5h fails the continuations and the huge game (one plays
  with the flags, one without), and writer identity of loaded inputs (17.3's 5 regions).
- **Turn-time baselines and testbed series on the huge save**: `perf/turnbench` `--game huge` (350 -> 355), the
  testbed's `hostruns.py` / `headless_series.py` / `perf/capacity` on `TB-HUGE_0350`, the ledger's backfilled
  headless-x64 (42) and headless-x86 (78) runs (moved to `superseded/d151/` by 9071; `metrics-ledger.md` 8). A/B
  verdicts inside one series stay valid (both arms the same host); absolute huge-save numbers and their history in
  `docs/briefs/PERFORMANCE.md` describe the flagless game. **Re-take** the huge reference series after the switch.
  `standard` and `tiny` (new games) are not affected.
- **The x86/x64 drift on the huge save** (`tools/drift/run_huge.py`; section 10's first difference at turn 354):
  both hosts flagless; **re-run** if the drift numbers are used again.
- **Saves made by headless play from a loaded save**: 9073's stress save `TSstd-deity-T200` (played in 25-turn
  segments, each from the previous segment's save; `metrics-ledger.md` 8.4) and the superseded 43-player Earth TSL t400
  (8.2). Their histories after turn 25 (9073) / 0 (Earth TSL) were played mostly without water flags; a client that
  loads them recalculates, so the acceptance turns played from them are the player's game from that save. Remaking
  them is the MC's call (not needed for a stress input). The 0438 scenario saves (`TS0438-M-T150`, `TS0438-H-T300`)
  were new games and are not affected.
- **Epic 0437's S1/S3 arms** (the turn-time study): already traced by 4655; with the fixed hosts S1/S3 play the
  clients' game without the probe (17.3).
- Not affected: new-game runs of both hosts (`turnbench` standard/tiny, `run_pair.py` from specs, D10 direct new-game
  plays), `tools/i0/roundtrip.py` load-then-save identity between two DLLs in the same host.

### 17.5 Switching c5h (the MC's step at the merge) [C, grep of main at 41aff1983, 2026-11-16]

No tool passes c5h an `--exe`; the host is chosen in two places:
- **`%USERPROFILE%/Documents/civ5-headless/out/latest.txt`** (c5h's `config.find_exe`; today
  `...\out\Release\20261027-120431`): every workstation c5h run reads it: `tools/d10/run_d10.py`,
  `tools/drift/run_huge.py`, `tools/drift/run_pair.py`, `tools/i0/ab.py`, `tools/i0/roundtrip.py`,
  `perf/turnbench/turnbench.py`, `tools/release/stages/testdata.py` (snapshot and database build through `c5h.cmd`),
  `tools/release/stages/wholegames.py` (`c5h_exe()` reads the same file and hands the exe to the testbed job as
  `TESTBED_C5H_EXE`). Switch: write `%USERPROFILE%\Documents\civ5-headless\out\Release\20261116-0452-landmasses` into it
  (and, if wanted, check out the branch in the checkout; the exe folder does not depend on it).
- **`tools/testbed/bases.json`** (the `civ5-headless` base, `"map": {"out/Release/20261027-120431/civ5headless.exe":
  "bin/civ5headless.exe"}`): the exe the test machines get as `<base>/bin/civ5headless.exe`, used by
  `tools/testbed/jobs/hostruns.py` and `longgames.py`. Switch: the map's source path, then the base pushed again to oldpc
  and the laptop by the testbed's base sync.

## 18. A refused file create made a load ask for 115 GB; the run-folder check and the memory cap (ticket 0488, 2026-11-17)

**The incident** [L, lead-0437 #3791]: on 2026-11-17 05:34-05:38 EST a headless S3-on-H run of ticket 4664 on the
workstation (server.exe of 0452's layout, the pin's x64 DLL `29f58e17...`, save TS0438-H-T300) grew to about 131 GB
private and 37.8 GB working set in the load phase and ended after 230 s in a C++ exception. Its `server.cfg` had mixed
separators (`run_dir=C:/c5-64bit-work/4664/fc/work\host-r01-p1-S3`, from forward-slash `TESTBED_*` paths); the same load
with backslash paths takes 0.5 s. The machine recovered (commit 82.5 GB free of 127.2 GB at 05:42).

### 18.1 The chain [C, source of the server and of fork 04b1211b8, the incident's server.log, the pin's PDB]

1. **The run-folder check refused the create.** `SrvFile::InsideRun` (srv_ifaces.cpp before 0488) put only the file
   through `GetFullPathNameW`, which turns every `/` into `\`, and compared it with `_wcsnicmp` against `run_dir` as
   written. `C:\c5-64bit-work\...` never equals `C:/c5-64bit-work/...`, so every file inside a `/` run folder was
   refused. server.log: `FIFileSystem::Create(L"C:/c5-64bit-work/4664/fc/work\host-r01-p1-S3\cache\Civ5SavedGameDatabase.db")
   refused or failed (only files inside the run folder)`.
2. **The DLL went on.** `CvGame::readSaveGameDB` (CvGame.cpp:11110-11145) reads the database's u32 size, and only if
   `FFILESYSTEM.Create(..., modeWrite)` returns a file does it read the `size` bytes that follow; on NULL it asserts
   ("Cannot open Civ5SavedGameDatabase.db for write! Does something have this opened?", `CvAssert.log`) and returns with
   the database still in the stream. It is the DLL's only `FFILESYSTEM.Create` call.
3. **The map read the SQLite header as its size.** The server's load reads `Map::Read` from the same stream right after
   `Game::Read` (srv_save.cpp `LoadParts`). `CvMap::Serialize` (CvMap.cpp:1776-1800) reads `m_iGridWidth` and
   `m_iGridHeight` first: the bytes `SQLi` and `te f` of "SQLite format 3\0", i.e. 1,766,609,235 and 1,713,399,156;
   their product as a 32-bit int is **252,352,668 plots**.
4. **InitPlots asked for 115 GB.** `CvMap::InitPlots` (CvMap.cpp:597ff) does `FNEW(CvPlot[iNumPlots])` first;
   `sizeof(CvPlot)` is 456 B (llvm-pdbutil on the pin's PDB), so **115,072,816,608 B (115.1 GB)**. The workstation's
   commit limit (127.2 GB) let it through; the constructors then touched it plot by plot (working set to 37.8 GB, the
   rest paged: the 230 s) [I for the timing]. The next allocations follow: `NUM_YIELD_TYPES * iNumPlots` bytes, then
   `uint8[MAX_TEAMS * iNumPlots]` with 64 x 252,352,668 overflowing int to -1,029,298,432, which `new[]` cannot satisfy:
   the bad_alloc. The incident's frames symbolized with the pin's PDB: `__scrt_throw_std_bad_alloc` <- `operator new` <-
   `CvMap::InitPlots` (CvMap.cpp:609 of the build tree, the MAX_TEAMS array) <- `CvMap::Serialize` <- `CvMap::Read` <-
   `CvDllMap::Read`. The reported 131 GB against about 118 GB computed here is not explained [I: measured as a
   different counter].

The same chain follows from any failed create in a load (a locked or unwritable cache folder), not only from separators.
Possible upstream hardening, out of scope here (MC #3809): the DLL could fail loud when `readSaveGameDB` cannot write
the database (skip its bytes, or stop the load), and `CvMap::Serialize` could refuse implausible dimensions.

### 18.2 The fix

- **Normalised comparison** (`runfolder::Normalise` / `Inside`, the block "0488 RUN FOLDER" of srv_ifaces.cpp): both the
  file and each root go through `GetFullPathNameW` (separators, `.`/`..`, repeated separators), then `GetLongPathNameW`
  on the longest existing prefix (8.3 short names; a file not yet created keeps its own last parts), trailing
  separators removed, and are compared without case (`CompareStringOrdinal`, as the file system). Inside means the root
  is a whole-component prefix: `run` does not contain `runner\x`, the root itself is not inside.
- **A refused or failed write create is fatal** (srv_ifaces.cpp `CreateFailed`): the reason (refused: not inside the run
  folder nor the cache folder; failed: the `CreateFileW` error) goes to server.log as a LOUD line and is kept. Headless:
  `LoadParts` checks it right after `Game::Read`, before `Map::Read`, and the load fails (server-report.txt `load:
  FIFileSystem::Create("...Civ5SavedGameDatabase.db") failed: CreateFileW error 5; ... the load stops here (ticket
  0488)`, `FAILED: the save could not be read...`, exit 2). Remote mode: the hook ends the server with
  `SrvRemote::ReportAndExit(1, why)` inside the create, as the 4253 boot guard does: the proxy's failed call takes the
  D118/D119 path. A failed read-mode open stays an answer (no caller in the DLL today). [C]

### 18.3 The memory cap for headless runs (`tools/harness/jobcap.py`)

Every headless server.exe our tools start runs in a Windows job object with `JOB_OBJECT_LIMIT_PROCESS_MEMORY`: default
**16 GB per process**, else `C5_JOBCAP_GB` (GB; 0 = no cap, with a line), or the starter's own option. The child starts
suspended, joins the job, then runs (never a moment uncapped; if it cannot join it is ended before it ran and the start
fails). Children of the command join the job too. At the cap every further commit fails (bad_alloc), Windows posts
`JOB_OBJECT_MSG_PROCESS_MEMORY_LIMIT`, and jobcap gives the process 10 s to end by itself (server.exe writes its report
and exits 2), then ends it with exit 0xC0000017 (STATUS_NO_MEMORY). A game EXE (`CivilizationV*`) is never ended by it
(AGENTS.md 4): its allocations fail at the cap and the event is logged. Use: `python -I tools/harness/jobcap.py
[--cap-gb N] [--grace S] -- COMMAND`, or `jobcap.popen` / `jobcap.run` (Python 3.8, standard library only; loaded by path).

Starters changed (2026-11-17): `server/run_server.py` (`--mem-cap-gb`; every direct run, and so
`server/tests/load/run_load_water_test.py`), `tools/testbed/jobs/hostruns.py` (opts `mem_cap_gb`; the x64 server and
c5h; the record's `memCap`). Not changed here, named for their owners: `perf/turnstudy/runner/headlessarm.py` (0437's,
on its branch: `subprocess.Popen(cmd, ...)` -> `jobcap.popen(cmd, None, ...)`), `proxy/tools/hutest/build_hutest.py`
(0463's: the `hutest_host.exe` run under `jobcap.run`; the proxy's remote server.exe is its child and joins the job).
A cross-reference line in `research/06-tooling-and-workflows/run-and-drive-the-game.md` follows when 0486 has merged.

### 18.4 Results [L, 2026-11-17 06:04-06:09 EST, workstation; server.exe of this branch `d791f34b...` with the pin's DLL `29f58e17...`, save TS0438-M-T150, one turn]

`python -I server/tests/runfolder/run_runfolder_test.py` (evidence `server/evidence/0488/`):

| Case | Server | Result |
|---|---|---|
| unit, clang-cl and MSVC ASan | the block | 28 cases each: mixed separators both ways and the incident's exact form, trailing and repeated separators, case, dots, a new file, 8.3 names both ways accepted; a `runner` sibling, `..` out, the root itself, another folder, empty and NULL refused; no ASan report; the canary caught |
| L1 `run_dir` and save with `/` | fixed | exit 0, passed, 13.9 s, the database written, no create line |
| L1s `run_dir` 8.3 and lower case | fixed | exit 0, passed |
| L2 the create fails (a folder named `Civ5SavedGameDatabase.db`) | fixed | exit 2 in 13.0 s, the report names the create and "the load stops here", no exception, peak working set 158 MB, job peak commit 174 MB |
| L3 `run_dir` with `/`, cap 4 GB | pin (before) | the create refused, the DLL's assert, `new CvPlot[...]` refused at once by the cap (`CvMap::InitPlots`, CvMap.cpp:600), exit 2 in 12.1 s, job peak commit 174 MB: the incident reproduced and held |

`tools/harness/tests/test_jobcap.py`: 9 passed (a child past a 256 MB cap is ended with 0xC0000017 after the grace, one
that ends itself keeps its code, a grandchild is capped, cap 0, the variable, a child that cannot join never runs, the
command line).

**Not verified:** the remote-mode hook live (built, not exercised: no remote load with a refused create was run); the
L3 reproduction used the M save (the H save of the incident fails in the same place: the dimensions come from the
SQLite header, not the save).

## 19. Remote mode: an EXE crash that ends with exit code 255 is proven by the proxy's crash record (ticket 0519, 2026-11-17, account B)

The EXE-crash report of ticket 0418 (`error-report-exe-crash.md`) decides on the EXE's exit code alone. On a machine
where Windows Error Reporting shows its "has stopped working" box, a crashed EXE stays alive and frozen until the
player presses "Close program", and then ends with **exit code 255** (0x000000FF), which `IsCrashCode` calls "not a
crash code": no report and no popup, although the proxy's chained filter (0419) had already written
`<run>\exe-crash.txt` (found by 0515 on the laptop, `research/02-exe-internals/crashes-huge-load.md` 8.1 and 8.4).

### 19.1 What the server does now (as built, `server/src/remote/srv_exe_crash.cpp` Work()) [C]

- An exit code that is a crash code: unchanged (0418).
- Any other exit code: the server reads `<run>\exe-crash.txt` (the run folder `SrvRunDir()`, the folder the proxy's
  filter writes into and the report already copies from). The record is **proof of a crash** when all of these hold:
  - its `process:` line is a decimal pid equal to the EXE's pid (the pid the server attached to);
  - its last-write time is not earlier than the EXE's creation time (`GetProcessTimes` on the handle kept since the
    attach). A record of an earlier EXE that had the same pid, or of another EXE, is "a stale record";
  - the exit code is **not 0**. Exit code 0 is the game's own clean end; a record with it means an exception the
    process survived (a filter returned "continue"), not a crash. This is logged and not reported.
- With proof, the report is the 0418 report (same folder, same helper start, same switch `exe_crash_report` /
  vpproxy.ini `pack=1`), with these differences:
  - `[failure]` `code:` is the record's exception code and name, read from its `code: 0x<hex> <name>` line; when that
    line is missing, from its first line `exception 0x<hex> <name> at ...`; when neither has a code, the exit code with
    the name "(the crash record names no exception code)".
  - Two lines follow it: `exit code: the EXE ended with 0x000000FF (255), not an exception code (after a crash, Windows
    Error Reporting's "has stopped working" box ends the EXE so when the player presses "Close program")` and
    `proof of the crash: the proxy's crash record: exe-crash.txt names this EXE (process <pid>), written <time> (local
    time, <s> s after the EXE started) (ticket 0519)`.
  - `[exe]` says `exit code: 0x000000FF (255; not an exception code: the crash is proven by the proxy's crash record,
    see [failure])`.
  - The clause returned to `ReportAndExit` (server-report.txt's `result:`) is `the EXE ended with exit code 0x000000FF
    (255) after a crash its proxy recorded: exception code 0xC0000005 (EXCEPTION_ACCESS_VIOLATION)`.
  - server.log: `remote: the EXE (pid N) ended with exit code 0x000000FF (255): not a crash code, but the proxy's crash
    record proves a crash: 0xC0000005 EXCEPTION_ACCESS_VIOLATION (<why>; ticket 0519)`, then the 0418 LOUD line.
- Without proof the old line stays, with the reason added: `...: not a crash code; no exe-crash report (ticket 0418;
  <why>; ticket 0519)`, where `<why>` is one of "no exe-crash.txt (the proxy's crash record) in the run folder",
  "exe-crash.txt names process N, not this EXE (pid M): a stale record, not taken as proof", "... was written <t>,
  before this EXE started <t>: a stale record ...", "exe-crash.txt has no "process:" line with a pid ...", or the
  proof followed by ", but exit code 0 is the game's own clean end".
- The record is read with the report's own capped reader (256 KB); every text from it is scrubbed (the report's
  privacy rule) and the code name is cut at 80 printable characters.

### 19.2 The optional "peer hung" trigger: not taken (decision of this ticket) [C for the code, I for the cost]

The ticket left open a second trigger: on vpipc's "peer hung" event (no heartbeat for 5 s, which is what the server sees
while WER's box holds the EXE frozen), write the report and start the popup at once when a record of the peer exists, so
the player would see the patch's popup beside Windows' box. Not taken, because:
- The player already gets the patch's popup: after "Close program" the EXE ends, the channel fails and Work() now
  reports. What the hung trigger would add is the popup some seconds to minutes earlier, beside a box that already says
  the game stopped working.
- A report written while the EXE is still alive is less complete: the run's logs (vpproxy.log, the DLL's logs inside
  the server) can still grow, and the report's times would read "still running".
- It needs state shared between the vpipc event thread and the exiting thread (one report per session, the EXE's end
  after the hung report must not write a second), in `srv_remote_main.cpp`, outside this ticket's files; the "peer hung"
  event is also raised by long legitimate stalls (0515's huge loads logged it), where only the record keeps it quiet.
- WER's box itself is not the patch's to decide; on machines without it (the workstation and oldpc, 8.1) the EXE ends at
  once and the hung trigger would never fire.
If a later ticket wants the earlier popup, the guard is ready: the same `RecordProvesCrash` check, plus a once-flag that
Work() reads.

### 19.3 Tests [L offline, 2026-11-17, workstation]

`server/tests/remote/test_exe_crash_report.py` (0418's suite) gained 11 unit cases and 3 end-to-end cases. In the unit
cases the child "EXE" runs 4 s; once the peer prints its pid, the test (standing in for the proxy) writes the record in
the proxy's format (`proxy_crash_filter.cpp` FormatRecord's lines, CRLF) into the run folder:

| Case | Exit | Record | Result checked |
|---|---|---|---|
| rec-255 | 255 | this pid | one report, `code: 0xC0000005 EXCEPTION_ACCESS_VIOLATION`, the exit-code and proof lines, `[exe]` exit code 255, the record copied and quoted, nothing unscrubbed, the helper once with `--options`, the three service threads return the clause |
| norec-255 | 255 | none | no report, no helper, "no exe-crash.txt" |
| stale-pid | 255 | pid + 4 | none, "not this EXE (pid" |
| stale-time | 255 | this pid, written 1 h before | none, "before this EXE started" |
| rec-nopid | 255 | `process: (none)` | none, "has no "process:" line" |
| rec-0 | 0 | this pid | none, "but exit code 0 is the game's own clean end" |
| rec-firstline | 255 | no `code:` line | the code from the first line (0xC0000409 STATUS_STACK_BUFFER_OVERRUN) |
| rec-nocode | 255 | no code at all | `code: 0x000000FF (the crash record names no exception code)` |
| rec-pack-off | 255 | this pid, pack=0 | no report, no popup, LOUD "says pack=0", the clause still returned |
| rec-c5 | 0xC0000005 | this pid | as before: the exit code's code, the record quoted |

End to end (the real server.exe of this branch, `exe_crash_standin.cpp` as the EXE): `rec255` (the stand-in's new
`record <run dir>` mode writes the record with its own pid, then `TerminateProcess(255)`): server exit 3, one report with
the record's code, the proof lines and the record copied, the helper once, `result:` names exit code 255 and the
recorded crash; the real `vp64helper.exe popup --no-ui` reads that report as "The game itself crashed ...". `norec255`
and `rec255-pack0`: no report, no helper.

Build `C:/c5-64bit-work/0519/build`: `server/build_server.py --src` a `git archive` of the fork at the pin 4f4daac70,
`--deps` a copy of 0516's release deps of that pin, `--config release --offline` (140 s; server.exe `00677901...`,
proxy `f3a0743d...`, vp64helper.exe `917f87d9...`). Run through `quietlock.py run`:
`python -I server/tests/remote/test_exe_crash_report.py --out C:/c5-64bit-work/0519/e2e --build-out C:/c5-64bit-work/0519/build --asan`
(22:52-22:56 EST): **185/185**: 3 builds, 74 unit checks under clang-cl, 24 end-to-end checks,
4 real-helper checks, and 80 ASan checks (the same 74 unit checks under MSVC `/fsanitize=address`, the canary caught,
no AddressSanitizer report). The unit part alone before it (22:47): 76/76. Output:
`server/evidence/0519/test_exe_crash_report-e2e-asan.txt`.

### 19.4 Live check (the laptop, through the testbed) [see 19.5]

Driver `server/evidence/0519/live_0519.py`, a headed testbed job on the laptop (the host where WER shows its box): the
D33 product path from `product_path.py`'s prep (not changed), the real `vp64helper.exe` beside the proxy, vpproxy.ini
`server_cfg_extra` with `exe_crash_report=1` (TEST ONLY: the test layout says `pack=0`; the switch takes the same branch
of `ReportEnabled` as the shipped `pack=1`), the save `TB-TINY_0008`. The fault: a thread created in the EXE's process
at address 0 (`CreateRemoteThread` from the job's process), an execute access violation that nothing handles, so the
process's top-level filter (the proxy's, 0419) writes the record and hands on, and WER follows. No byte of the EXE is
changed (D4). The mechanism was checked on the workstation first [L, 2026-11-17 23:05 EST, hidden desktop, error
boxes off]: a 64-bit Python's `CreateRemoteThread(start = 0)` into a 32-bit process **without CFG** (a probe built like the
EXE: `CivilizationV_DX11.exe`'s DllCharacteristics are 0x8140, no `GUARD_CF`) gives an access violation at 0x00000000
that the process's top-level filter sees ("filter ran: code 0xC0000005 at 0x00000000"), and the process ends with
0xC0000005. Into a 32-bit process **with** CFG (SysWOW64 `PING.EXE`, 0xC140) the same call ends the process at once with
0xC0000409 (CFG's fail fast on the thread's start address), which no filter sees: the method only works because the EXE
has no CFG. Then WER's box (copied evidence first, a picture), "Close program" under the MC's standing word for this
ticket's own test crash (D155), the EXE's exit code read from a handle the job opened before the fault, the report
folder, the patch's popup (window class `VP64HelperPopup`, a picture), its Close (`cli.py dismiss --patch-popup`), the
restore by hash.

### 19.5 Results of the live check

**First run failed at the prep (2026-11-18 04:22 EST):** laptop job `20261117-225838-5f71` stopped with exit 2 because
`C:\c5-testbed\base\saves\TB-TINY_0008.Civ5Save` does not exist on the laptop. The laptop's base holds only
`TB-HUGE_0350` (`tools/testbed/bases.json` "saves"); the jobs that used a Tiny save there shipped their own copy. tb1 was
restored and verified. **Rerun:** the driver's `--saves-from-bin` takes the save from the job's own files (`saves/`,
`REL548-TINY_0008` = `TB-TINY_0008`, SHA-256 `b2ceb663...`, checked before the prep). Shipped: server.exe `00677901...`
(built from 7061200ac) and proxy `f3a0743d...` of this branch, vp64helper.exe `917f87d9...`, the pin's x64 DLL
`2d737bd5...` and libraries (layout `x64-4f4daac7-pf8e9f42`), a composite built from this branch.

**Rerun: 9/9 facts, exit 0** [L, laptop, job `20261118-051814-ea8b`, 2026-11-18 06:04-06:07 EST, instance tb1 (the
laptop's own Steam EXE), D33 product path, save copy `TBPP-TINY_0008`]:

| Time (EST) | What happened |
|---|---|
| 06:04:44.231 | The EXE (pid 1772) started; in game 104 s after the launch; server.exe ran under it. |
| 06:06:43.356 | The job's remote thread at address 0 (thread 16188). |
| 06:06:43.360 | The proxy's `exe-crash.txt`: `exception 0xC0000005 EXCEPTION_ACCESS_VIOLATION at an address in no module (0x00000000), thread 16188`, `process: 1772`. |
| server 62.377 s | `vpipc: peer pid 1772 hung: no heartbeat for 5026 ms` (WER holds the EXE; no report yet: fact checked while the box was up). |
| 06:07:00 | The dialog watcher's marker: `WerFault.exe`, title "Sid Meier's Civilization V", the one button "Close program" (kind `unknown`). Evidence copied, picture taken, then "Close program" pressed (`cli.py dismiss` with the MC's word). |
| 06:07:13.060 | The EXE ended with **exit code 0x000000FF (255)** (read from the job's own handle). |
| server 87.353 s | `remote: the EXE (pid 1772) ended with exit code 0x000000FF (255): not a crash code, but the proxy's crash record proves a crash: 0xC0000005 EXCEPTION_ACCESS_VIOLATION (exe-crash.txt names this EXE (process 1772), written 06:06:43.360 (local time, 119.1 s after the EXE started); ticket 0519)`, then the LOUD exe-crash line. |
| server 87.758 s | `exe-crash: report: written to ...\VP64\reports\20261118-060713 (report.txt and 12 attachment line(s))`; 87.766 s the helper started; the report took 413 ms. |
| 06:07:14.846 | The patch's popup (window class `VP64HelperPopup`) up, about 1.8 s after the EXE's end; its text: "The game itself crashed (the game's own process, not the 64-bit server), so the game had to end.", the summary line, the report folder, the hand-in and public-issue lines, buttons "Open folder" and "Close". Closed with `cli.py dismiss --button Close --patch-popup` (rc 0, no popup left). |

- report.txt `[failure]`: `stage: exe-crash`, `code: 0xC0000005 EXCEPTION_ACCESS_VIOLATION`, the exit-code line (255),
  the proof line (process 1772, 119.1 s after the EXE started), the EXE ran 148.8 s, the save load began 58.2 s and
  kRt_Boot 86.4 s before the EXE was seen gone, the record's first line quoted; `exe-crash.txt` copied with 11 more
  attachments (tails of server.log, vpproxy.log, render.log, stubcalls.log, Lua.log; vpproxy.ini, server.cfg,
  VP64Options.ini, popup-summary.txt).
- server-report.txt `result: FAILED: the engine is gone: channel 2 serve() returned PeerDied; the EXE ended with exit code
  0x000000FF (255) after a crash its proxy recorded: exception code 0xC0000005 (EXCEPTION_ACCESS_VIOLATION)`; server exit 3.
- Restore: server_cfg_extra unset, the helper copy and the report folder removed (moved into the job's output),
  `product_path.py` restore ok, `provision.py verify` rc 0.
- Pictures (Visual Checks, issue 0519, both DIAGNOSTIC: a TEST ONLY fault and switch): `vc-20261118-060701-laptop-87e1`
  (WER's box) and `vc-20261118-060715-laptop-eaa3` (the patch's popup on the laptop's desktop after the EXE's end).
- Evidence: `server/evidence/0519/live-laptop-ea8b/` (live_0519.json with the facts, steps.jsonl, the marker at the box,
  server.log, vpproxy.log, exe-crash.txt, server.cfg, server-report.txt, the report folder).

### 19.6 Not verified

- A natural EXE crash (0515's weld-loop sites) through this path live: the live check used an injected fault; the server's
  path after the EXE's end does not depend on the fault's kind [I], and the record's format is the same filter's.
- The shipped `pack=1` layout live: the live run turned the report on with `exe_crash_report=1` (same branch of
  `ReportEnabled`); `pack=1` was covered offline (unit and end to end).
- The hung trigger (19.2) is not built.
- dialogwatch.py's ledger still counts WER's box with exit 255 as `crash: false` (left to a follow-up after epic 0398,
  the MC's log line on 0519).
