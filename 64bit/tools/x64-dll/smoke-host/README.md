# tools/x64-dll/smoke-host: the x64 smoke host (issue 1007)

A minimal x64 console program that loads the x64 DLL, runs `DllMain` and the boot entry points the game calls
(`research/sources/civ5-headless/engine-contract.md` section 3), each in its own SEH guard, and writes
`smoke-report.md`: steps reached, the first exception with a symbolized stack, the first call of every host export
and engine-interface method in order, call counts, and the closed-library stub log. Not a game run. Written
2026-11-10 by worker 1007 of team 1000.

## Use

All of it in one command: `python tools/x64-dll/build_all.py --src <fork source> --out <dir>` (issue 1405; see
`../README.md` for what `--src` must be). By hand:

```
python tools/x64-dll/closedlib-stubs/build_stubs.py --src <fork> --out <stubdir> --config debug   # stubs + lua51_x64.dll/.lib
python tools/x64-dll/cvgamedatabase/build_db.py --src <fork> --out <stubdir> --arch x64 --config debug   # CvGameDatabase_x64.dll/.lib
python tools/x64-dll/cvlocalization/build_loc.py --src <fork> --out <stubdir> --arch x64 --config debug --db-dir <stubdir>   # CvLocalization_x64.dll/.lib
python tools/x64-dll/build.py --src <fork> --config debug --out <dlldir> --stubs <stubdir>
python tools/x64-dll/smoke-host/build_host.py --src <fork> --out <hostdir> --stubs <stubdir>
python tools/x64-dll/smoke-host/run_smoke.py --host <hostdir> --dll-dir <dlldir> --dep-dir <stubdir> --run <rundir> --db <gameplay db>
python tools/x64-dll/smoke-host/run_smoke.py ... --crash-to-dll-filter     # let the DLL's own crash handler take the crash
```

`--db` (issue 1404): the gameplay database, a copy of a VP 5.4.8 `Civ5DebugDatabase.db` (default in `build_all.py`:
`<cache root>\testdb\vp548-cdrift-Civ5DebugDatabase.db`, see `../README.md`; never the file in `My Games`). It is
copied into `<rundir>\db\` and opened by the host through `CvGameDatabase_x64.dll`; without it the host passes a
zeroed block, which only the logging stubs of epic 1000 survive (the run_smoke.py help); build_all.py always passes it.

`--dep-dir` (issue 1405; `--lua-dir` is its older name and still works, repeatable): every `*.dll` in it, with its
PDB, is copied next to the DLL in `<rundir>\dll` (`lua51_x64.dll`, `CvGameDatabase_x64.dll` and, since issue 1703,
`CvLocalization_x64.dll`); the host loads the DLL with `LOAD_WITH_ALTERED_SEARCH_PATH`, so the DLL's imports resolve there.
The host never initialises the localization dictionary: the DLL's text lookups (e.g. in `CacheGameDatabaseData`)
return the key, which is all the smoke steps need (18/18, 2026-11-10, issue 1703).
`build_host.py` compiles the host's sources with `/WX`, the fork's headers as external headers.

`build_host.py` uses `tools/x64-dll/build.py`'s toolchain, `/MD`, defines (minus `_USRDLL`, `_WINDLL`,
`CVGAMECOREDLL_EXPORTS`) and include dirs; 5 s. `run_smoke.py` copies the DLL and PDB into `<rundir>\dll`, runs the
host in a fresh `<rundir>\work` (the working directory: `Logs\`, `cache\`, `crashlogs\`, `stubcalls.log`,
`smoke-report.md`, `interface-slots.tsv` all land there), then symbolizes every `module+0xRVA` in the report with
`llvm-symbolizer` and the PDBs. Nothing is written outside `<rundir>`; the cache folder the host answers is
`<rundir>\work\cache\`.

## Files

| File | What |
|---|---|
| `gen_ifaces.py` | Reads `ICvEngineUtility1-4`, `ICvUserInterface1-2`, `ICvEngineScriptSystem1`, `FIFileSystem`, `FILogFileMgr`, `FILogFile` from the fork's headers and generates typed logging overrides of every pure virtual (`HostIfaceHit` + zero/null/empty return) plus a slot resolver per interface |
| `src/host_ifaces.cpp` | The objects handed to the DLL; hand-written: `QueryInterface`/`Destroy`, `GetScriptSystem`, `GetCacheFolderPath`, `CreateLuaThread` (returns NULL, see below), the log manager and log files (real files in `Logs\`); vtable slot decoding |
| `src/host_exports.cpp` | The 22 host exports (`__declspec(dllexport)`; the 22nd under its x64 decoration via `/EXPORT`) |
| `src/host_lua.cpp` | The host's Lua (issue 1009): main state in the shared `lua51_x64.dll`, `CreateLuaThread`/`FreeLuaThread` as the headless host |
| `src/host_main.cpp` | Boot steps, SEH guards, vectored first-chance recorder, x64 unwinder, report writer |
| `build_host.py`, `run_smoke.py` | Build and run (above) |

Design points:

- **Typed overrides, not untyped "return 0" thunks.** On x64 a method returning `float` returns in `xmm0` and one
  returning `CvString` by value takes a hidden result pointer; a generic thunk would corrupt both. The generated class
  derives from the real interface, so the compiler lays out the vtable and every return is correct.
- **Slot numbers come from the compiler.** For each method, `&Class::Method` is an MS-ABI vcall thunk
  (`??_9Class@@$B<off>AA`); the host decodes the `[rax+disp]` displacement (`HostSlotFromThunk`). All 458 methods
  of the six interfaces decode, unique, 0 problems [C]. `interface-slots.tsv` in the work dir lists them all.
- **Lua (issue 1009):** the host links `lua51_x64.lib` with `/DELAYLOAD:lua51_x64.dll`, loads `lua51_x64.dll` by full
  path (default: beside `--dll`; `--lua` to override) before the DLL, creates the main state (`luaL_newstate`,
  `luaL_openlibs`, `print` -> `Logs\Lua.log`, globals `Threads` and `IncludeFileList`) and answers `CreateLuaThread`
  like the headless host: a registry-anchored `lua_newthread` whose globals are `{StateName = name}` with
  `__index = _G`, `Threads[thread] = env` (rawset), then `ICvScriptSystemUtility1::RegisterScriptLibraries(thread)`
  in a protected call; `FreeLuaThread` clears the entry and unrefs. After the DLL load the host checks that
  `GetModuleHandle("lua51_x64.dll")` is its own module (one Lua copy). Not reproduced (the EXE's extras, not needed
  for the boot): the index-0 `ipairs`, hiding `debug` after boot, `include()`, the VFS and UI contexts. (Before
  issue 1009 the host had no Lua and `CreateLuaThread` returned NULL.)
- **No dbghelp**: System32 dbghelp loaded only the export table of the lld-linked DLL (`SymType` 5, no PDB) on
  2026-11-10; the stack is unwound in-process with `RtlLookupFunctionEntry`/`RtlVirtualUnwind` and symbolized
  offline by `run_smoke.py`.

## Result with Lua: the boot completes (issue 1009, 2026-11-10, 03:40 EST)

DLL: fork `x64` @ 47d7664e9, debug, built by `build.py` with the stubs of `build_stubs.py --lua dll` (my build,
`--out` in my scratchpad): 0 undefined symbols, no `/FORCE` of any kind (`/FORCE:MULTIPLE` dropped from the normal
link, 0 duplicate symbols) [C]. The DLL imports `lua51_x64.dll` and contains no Lua code (`lua51:lua_` objects in the
map: 0) [C]. `lua51_x64.dll`: 123 exports, all `lua_*`, `luaL_*`, `luaopen_*` and `luaL_openlibs` [C]. Evidence:
`../evidence/2026-11-10-smoke-lua/`.

| # | Step | Result [L] |
|---|---|---|
| 1 | Lua: load `lua51_x64.dll`, `luaL_newstate`, `luaL_openlibs` | ok, 2.5 ms |
| 2 | `LoadLibrary` (`DllMain`) | ok; `lua51_x64.dll` resolved to the host's module: one Lua copy |
| 3-7 | `DllGetGameContext`, `SetDLLIFace`, `SetGameDatabase`, `Init`, `SetEngineUserInterface` | ok |
| 8 | `GetScriptSystemUtility` | ok |
| 9 | `GetPreGame`, `setVersionString("403694 FINAL_RELEASE")`, `GetDLLGUID`, `GetDLLVersion` | ok ("1.0.0") |
| 10 | `GetGame`, `GetGameDeals`, `CreateDeal` (the EXE's scratch deal) | ok, deal 0 (`GetGameCoreLock`/`ReleaseGameCoreLock` called once) |
| 11 | pregame reset: `setGameMode(-1)`, `setGameType(0,0)`, `resetSlots`, `resetGame`, `resetPlayers` | ok |
| 12-14 | `GetDatabaseLoadUtility`, `FlushGameDatabaseData`, `PerformDatabasePostProcessing` | ok, true |
| 15 | `CacheGameDatabaseData` | **ok, true**, 18 ms: `CreateLuaThread("VP_LUAAPI")` -> `RegisterScriptLibraries` -> the DLL's embedded chunk; `Lua.log`: "VP_LUAAPI: Embedded script ran without errors!"; `FreeLuaThread` |
| 16 | `FreeLibrary` (`DllMain` PROCESS_DETACH) | ok |

No exception in the whole run. Phase A of `engine-contract.md` section 3 completes except the calls that need the
EXE's data: `loadFromIni` (needs an `FIGameIniParser`), the `setDLCAllowed` and last-session setup calls, and the
logging flags (`SetLogging` etc., log-only). **The next blocker is the database, not the boot**: with the stubs every
table is empty, so the DLL caches no infos (67 "is used in the DLL but does not exist in the database" warnings in
`Gamecore.log`, missing defines `AI_ATTEMPT_RUSH_OVER_X_TURNS_TO_BUILD` and
`AI_STRATEGY_NEED_IMPROVEMENT_CITY_RATIO`, missing table `Colors`, and `Civ5SavedGameDatabase.db` cannot be
created because `Database::Connection::Open` is a stub). Front end, mod activation and a new game (phases B and C)
need a real CvGameDatabase over SQLite and the game's built database.

Stub log (fixed flush, below): 26 stubs, one COUNT block written after the last stub call, 4,320 calls, including
`Localization::String::~String` 64 (= the 64 constructions) and `Database::Results::Release` 1 from unload.

## Result without Lua (issue 1007, 2026-11-10, 03:22 EST)

DLL: coordinator's debug build of fork `x64` @ f3d2f5669, linked with the stubs of issues 1005/1006, 0 unresolved,
no `/FORCE:UNRESOLVED` (build.py still passes `/FORCE:MULTIPLE`), 27,388,416 bytes, SHA-256
`63158239c517742705b0e3f0db778fac7576ba0426de540d9f62a9a1ad0a5564`. Evidence: `../evidence/2026-11-10-smoke/`
(`run3/`: normal run; `run4-crashfilter/`: crash through the DLL's handler).

| # | Step | Result [L] |
|---|---|---|
| 1 | `LoadLibrary` (`DllMain` PROCESS_ATTACH) | ok, 172 ms |
| 2 | `DllGetGameContext` | ok |
| 3 | `SetDLLIFace` | ok, true (QueryInterface(ICvEngineUtility4) -> host object) |
| 4 | `SetGameDatabase` | ok (zeroed 4 KB block as `Database::Connection*`; the stubs never read it) |
| 5 | `Init` | ok, 12 ms (`InitGlobals`, `GetCacheFolderPath`) |
| 6 | `QueryInterface(ICvGameContext2)` + `SetEngineUserInterface` | ok |
| 7 | `GetDatabaseLoadUtility` | ok |
| 8 | `FlushGameDatabaseData` | ok, true |
| 9 | `PerformDatabasePostProcessing` | ok, true |
| 10 | `CacheGameDatabaseData` | **access violation**, read of 0x20 |
| 11 | `FreeLibrary` (`DllMain` PROCESS_DETACH) | ok, after the crash |

The crash [L, symbolized C]: `index2adr` (`lapi.c:61`) <- `lua_pushvalue` (`lapi.c:230`) <-
`LuaSupport::InitLuaFramework` (`CvLuaSupport.cpp:92`) <- `CvDllDatabaseUtility::CacheGameDatabaseData`
(`CvDllDatabaseUtility.cpp:189`). Cause: `ICvEngineScriptSystem1::CreateLuaThread("VP_LUAAPI")` returned NULL
(the host has no Lua). Everything before it, the database caching itself included, ran on stub data: every table
empty, 67 `WarningMsg`s in `Gamecore.log` ("... is used in the DLL but does not exist in the database"), 4 asserts in
`CvAssert.log` (missing defines, missing `Colors` table, `Civ5SavedGameDatabase` cannot be created because the
`Database::Connection::Open` stub returns false). This is the expected end for a stub run.

What the DLL used, in order (first calls):

- Host exports: `FStringA_GetStringInitData` (12 calls) and `GetLogFileMgr` (221) during `DllMain`;
  `GetSmallObjectAllocator` (1) and `FSBADeallocate` (2) only at unload. Not called: `GetFileSystemMgr`,
  `FStringW_GetStringInitData`, `GetTempHeap`, `PeekLogFileMgr`, the 4 `FJobSet*`, `GetMemTrackerForDLL`,
  `GetSmallObjectAllocatorInitParams`, 7 other `FSBA*`, `_Init_locks::operator=`.
- Engine interface (slot in `ICvEngineUtility4`): `QueryInterface` 0, `InitGlobals` 122, `GetCacheFolderPath` 143,
  `ShouldValidateGameDatabase` 258, `GetScriptSystem` 3; script system `CreateLuaThread` 0; log manager
  `GetLog(LPCSTR,...)` slot 3 (221 calls); log file `Msg` 0, `WarningMsg` 2, `Close` 8. No `ICvUserInterface2`
  call. Log files opened: `CustomMods.log`, `xml.log`, `xml-perf.log`, `Gamecore.log`, `UnitMoveStats.txt` (at unload).
- Stub log: 25 distinct stubs, 23 before the flush with 4,174 calls (`Database::Results::Bind` and `Reset` 1,880 each,
  `Results(const char*)` 86, `Localization::String()` 64, ...); FireWorks only `FDebugHelper::GetInstance` and
  `LoadSymbols` (from `DllMain`). Full list: `run3/stubcalls.log`.

The DLL's x64 crash handler (issue 1002) [L] with `--crash-to-dll-filter`: `CustomFilter` (installed by
`CvGlobals::init` during `Init`) wrote `crashlogs\crashes.log` and an 89 MB minidump in the working directory. The log
has the right class ("read of 0x0000000000000020 (NULL or near-NULL object)"), 64-bit addresses, untruncated memory
sizes (maxMemory 137,438,953,344 KB), and the 8-byte stack scan finds the real return addresses
(`+0x140ac9b` `lua_pushvalue`, `+0xb8453` `InitLuaFramework`, `+0x6fac73` `CacheGameDatabaseData`) among stale
candidates. "Location (in file)" = RVA - 0xC00 (`+0x1409d20` for RVA `0x140a920`), and the x64 DLL's `.text` has
VirtualAddress 0x1000 and PointerToRawData 0x400, so the 0xC00 delta is right for the lld-linked x64 DLL too [C].

## Findings for other tasks

- **Vtable order is not declaration order** [C, compiler thunks]: in every interface with overloads, the MS ABI
  (clang-cl reproduces MSVC) puts an overload set's members in reverse declaration order: `IsModActivated` (slots 10/11
  swapped), `endTurnTimerLength` (187/188), `ICvUserInterface1::nextSelectedCitiesNode`, `CreateArgs`,
  `CallAccumulator`, all `FIFileSystem` overload pairs, `FILogFileMgr::GetLog`/`DeleteLog`; and
  `ICvUserInterface2::AddPopup` (new overload of a base method) lands before `PublishRemotePlayerTurnStart`. A
  generated proxy (I1-B/I1-C) must take slots from the compiler, never from header order. `interface-slots.tsv`
  (evidence `run3/`) is the x64 table: ICvEngineUtility4 260 methods (slots 0-259), ICvUserInterface2 152,
  ICvEngineScriptSystem1 13, FIFileSystem 15, FILogFileMgr 7 + destructor at slot 0, FILogFile 11 + destructor
  at slot 11.
- (Resolved by issue 1009: shared `lua51_x64.dll`, see the result with Lua above.) **The next blocker is the Lua environment**, not the database: `InitLuaFramework` needs a `lua_State` from the
  engine with the standard libraries and the engine's globals (`REGISTRY._LOADED._G`, `Threads`,
  `IncludeFileList`, `debug.getinfo`). The x64 DLL links only the lua51 members it references: no `luaopen_*` and no
  `luaL_openlibs` are in the debug DLL's map [C], so neither the server nor the DLL can open the standard libraries
  through the DLL's copy. With the one-Lua-copy rule this argues for a shared x64 `lua51` DLL used by both the server
  and the game DLL, as the 32-bit game does with `lua51_Win32.dll`, instead of a static `lua51.lib` inside the DLL
  [I; decision for the coordinator / I1-B].
- **Stub log flush order** [L], fixed by issue 1009: the only COUNT block was written before the last stub calls of
  the unload. Cause [C]: clang-cl 19.1.7 put the `#pragma init_seg(lib)` flusher's initialiser in `.CRT$XCU`, not
  `.CRT$XCL` (`llvm-objdump -h stublog.obj`); the stub libraries come last in the link, so it was registered last and
  ran first. The late callers (recorded with the new `late:` caller column): `gGlobals`' atexit destructor ->
  `CvGlobals::uninit` -> `Database::Results::Release`, and `CvPlayerAI` -> `CvPlayer` -> `CvDiplomacyAI` destructor ->
  `Localization::String::~String`. Fix: `atexit(ExitFlush)` from a `.CRT$XCB` initialiser, so it runs after every
  static destructor; any first call after the flush would still be marked `late:` with its callers (none since).
- **TempHeap x64 layout** [C, static_assert against `FTempHeap.h`]: lock +0x00 (align 16), `m_pMemHead` +0x10,
  `m_uiCurrentOffset` +0x18, `m_uiCapacity` +0x1c, `m_dwFlags` +0x20, size 0x30 (x86: +0x10/+0x14/+0x18/+0x1c, size
  0x20). Not exercised: `GetTempHeap` was not called in this run.
- `CvGame.cpp:14675` builds a `My Games\Sid Meier's Civilization 5\MODS` path; it was not reached in this run.

## Not verified

- Phases B and C (front end, mod activation, new game): need a real database (above).
- The release DLL (only the debug build was run).
- `GetFileSystemMgr`, `GetTempHeap`, `FStringW_GetStringInitData` and the small-block allocator path: exported and
  implemented but not called by this run.
