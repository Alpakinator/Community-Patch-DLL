# Instrumentation in the DLL

The experiment DLL carries several opt-in instruments that reach into the EXE from inside the process. All of them
respect the CEG rule: they write import slots, heap data or vtables, never EXE code bytes, and they verify the EXE
build (PE TimeDateStamp plus byte patterns) before touching anything; any other build is left alone and the log says
why. Summary of where each lives and how it is switched on.

## 1. Marker files and switches

Install folder = `C:\Program Files (x86)\Steam\steamapps\common\Sid Meier's Civilization V` (the game's working
directory, which holds `crashlogs\crashes.log` and the minidumps). User folder = `%USERPROFILE%\Documents\My Games\Sid
Meier's Civilization 5`.

| Marker / switch | Where | Turns on | Output |
|---|---|---|---|
| `allocTrace.enabled` (optional content: threshold `1M`, `512K`, `262144`, `0x100000`; default 1 MB, min 4 KB) | `<install>\crashlogs\` | EngineAllocTrace | `crashlogs\allocTrace.log` (appended across launches) |
| env `VP_ALLOCTRACE=1` / `=0`, `VP_ALLOCTRACE_THRESHOLD=...` | game environment (lost when Steam relaunches the game) | on / force off / threshold override | |
| `queueguard.capture` | `<install>\crashlogs\` | capture of every record EngineQueueGuard diverts (memory-diagnostics working tree only) | `crashlogs\queueguard-dropped-<date>-<time>.txt` (29 MB for one load) |
| env `VP_QUEUEGUARD_CAPTURE=1` | game environment (may not survive Steam's relaunch) | same | |
| env `VP_QUEUEGUARD=0` | game environment | disables the guard itself (the guard is on by default where built in) | |
| `shell.helper` (first line = absolute DLL path) | `<install>\crashlogs\` per GATES.md and alloc-trace-README; compositor-README says `<My Games>\...\crashlogs\` - see note | ShellHelperLoader loads that DLL once | `crashlogs\shell.log` |
| `luaexec.enabled` | `<USER_DIR>\cache\` (`game_session.py luaexec on`) | external Lua channel | answers to `vp_lua.py` |
| env `VP_LUAEXEC=1` / `=0` | game environment | channel on / force off | |
| `memsnap_request.txt` (label) + event `Local\VPMemSnapshot` | cache folder (per civ5-game-ui SKILL) | one full memory snapshot | ten `MemSnap*` tables in `stats.db` |
| `nodumps.please`, `nopopups.please` | `<install>\crashlogs\` | named as existing markers in alloc-trace-README; their effect is not described in the sources (by name: no minidumps, no crash popups - unverified) | |

Note on `shell.helper`: the compositor README was written before the live run; GATES.md (final, after the live run)
and the alloc-trace README both say the install folder, and the live runs used it. Rule from GATES.md's end state: no
marker files left in `<install>\crashlogs` after an experiment; move instrument logs out.

## 2. The instruments

### EngineQueueGuard (`CvGameCoreDLL_Expansion2/EngineQueueGuard.{h,cpp}`)

- Problem: the EXE's double-buffered 4 MB game-to-UI message queue has no capacity check; a late huge-map load
  overruns it (crash at `CivilizationV_DX11+0x2a59a5`, a hang, or lucky survival).
- Fix (2026-09-21, the owner chose [reworded for privacy] to drop the items that exceed the buffer): verifies the EXE (TimeDateStamp 0x546CD0A8 + byte patterns
  of a writer, the reserve function, the swap and the dispatcher, cross-checked against the import table), then
  points the EXE's `kernel32!InterlockedExchangeAdd` import slot at a thunk. For the four buffer size fields it
  reserves with CAS and diverts a record that would not fit to a 64 KB scratch area; every other caller is
  forwarded.
- Reports to `crashlogs\queueguard.log` (status at every launch - `active`, or `off: <reason>` meaning the EXE is
  not recognised and late loads are unprotected - and one line per load that dropped) and a line in `crashes.log`
  (`UI message queue guard: active, N records (K KB) dropped, 0 too large`). `ReportDrops` runs from
  `CvGame::update`.
- What it drops (10-01 capture): terrain decals - farm-field polygons and route curves (793 of 1,316 farms loaded
  without fields on the reference save). The capture facility gives each diverted record a slot in a 32 MB
  never-reused area with thread, buffer, fill, flip counter and up to 24 stack words. Tools and README:
  `.claude/skills/civ5-game-ui/experiments/queue-capture/`.
- Two versions exist:
  - branch `engine-queue-guard` (commits 2f954ff99 and ac7afabc2, on the DLL fork, off LoneGazebo master; PR
    not opened): **all three EXEs** (DX11, DX9 0x546CCB59, Tablet 0x546CD5F8) via a per-build table, **gated on
    BIN_HOOKS**: `Install(MOD_BIN_HOOKS)` runs from `CvDllDatabaseUtility::CacheGameDatabaseData` right after
    `gCustomMods.preloadCache()` (the option is not readable in `DllGetGameContext`). Verified on the 280 load in all
    three EXEs. No Lua channel on that branch.
  - the memory-diagnostics working tree / `f181104a2` lineage: single-EXE (DX11) version installed from
    `DllGetGameContext`, plus the capture code. GATES.md describes the shell-gates DLL installing "after
    `MemoryImports::Install` ... and `EngineQueueGuard::Install`" from `DllGetGameContext`, i.e. this lineage.
    Whether the shell-gates copy is BIN_HOOKS-gated is not stated in the sources.

### EngineAllocTrace (`CvGameCoreDLL_Expansion2/EngineAllocTrace.{h,cpp}`, commit 0a7795fdf)

- Logs every EXE allocation of >= threshold (default 1 MB) made by `CivilizationV_DX11.exe` (also DX9 and Tablet),
  with the engine's own `file`/`line`/`tag` arguments, the calling EXE address and up to 16 EXE code addresses from
  the stack; also logs the free of each logged block, reallocs, and failed requests at **any** size (flushed at
  once - one of them may be the allocation that kills the process).
- Mechanism: chained thunks in five EXE msvcr90 import slots (`malloc`, `_aligned_malloc`, `realloc`, `free`,
  `_aligned_free`), each replaced with one aligned interlocked exchange - not an inline hook at 0x82c090 (CEG;
  non-atomic 5-byte write over a 1-byte first instruction; and the slots also see 234 direct `malloc` callers, 46
  `_aligned_malloc` callers and `realloc`). When the return address is the known spot inside the engine's
  `operator new` (+0x45), the aligned helper (+0x52) or aligned new (+0x24), it decodes the caller and arguments from
  the fixed frame (`api=new` / `anew`); otherwise `api=malloc` / `amalloc`.
- No heap use inside the thunks: a 64 KB `VirtualAlloc(MEM_TOP_DOWN)` buffer, `WriteFile` under one leaf critical
  section; a fixed 4096 x 8 block table. Flushed when half full, each second, at each game update (`CvGame::update`
  -> `OnGameUpdate`, which also writes a `T turn=` marker) and at unload. ~0.7 MB when on; nothing patched when off.
- Log lines: `#` header (`EngineAllocTrace active exe=DX11 timestamp=0x546cd0a8 base=... threshold=...`, or `off:
  <reason>`) and `# slots`, then `A` / `R` / `F` / `T` events with `seq tick tid api size ptr caller file line tag
  tag2 align stack age fail untracked`. Addresses print as `EXE+rva` (add 0x400000 for listing VAs).
- Analysis: `python experiments/shell-gates/alloc-trace/summarize.py "<install>\crashlogs\allocTrace.log"`
  ("largest live blocks", "2:1 pairs").
- Offline self-test (`selftest/run_selftest.py`, with `C:\Program Files\LLVM\bin` on PATH): maps each real EXE with
  `DONT_RESOLVE_DLL_REFERENCES`, calls the EXE's own allocators, checks the log; passed for DX11, DX9 and Tablet in
  `/Od` and `/Ox /Ob2` on 2026-11-09. Never touches a running game.
- Limits: misses everything before the game DLL loads (EXE start-up to main menu, e.g. FluidFOW's eight 24 MB
  particle blocks); does not see msvcr90-internal allocations, `HeapAlloc`/`VirtualAlloc` or other modules. Shares
  the unload hazard with MemoryImports and EngineQueueGuard (if the engine ever unloads the DLL on a mod-set change,
  EXE slots would point at unmapped code; `Uninstall` on `DLL_PROCESS_DETACH` puts its slots back).
- Result it produced: the 2:1 "0x26/0x25" heap pair belongs to the FluidFOW particle grid
  (`FluidParticles.cpp:718` 48.24 MB + `:723` 24.12 MB on the huge t350 save), gone with `FluidFOW = 1`.

### MemoryHooks and MemoryImports (`MemoryHooks.{h,cpp}`, `MemoryImports.{h,cpp}`)

- MemoryHooks (2026-09-08) replaces the DLL's global allocation operators and records the caller's return address
  and a subsystem tag per allocation (tables `MemHookSummary`, `MemHookTags`, `MemHookSites`). It wraps rather than
  reimplements, because DLL `new` goes to the EXE's Small Block Allocator first (`FSBABind(); sgpfSBAAllocate(n)`,
  else `malloc`) - a naive `malloc`/`free` replacement would pass SBA pointers to `free()`. FireWorks
  (`FirePlace/lib/FireWorksWin32.obj`, `FMemHooks.obj`) already defines the operators; lld-link with
  `/FORCE:MULTIPLE` keeps the first definition, so `build_vp_clang.py` emits `MemoryHooks.obj` from a `LINK_FIRST`
  list. Checks: `build.log` shows four `duplicate symbol` warnings with MemoryHooks.cpp first; `MemHookSummary.
  HookLive` (a per-turn runtime self-check) must be 1.
- MemoryImports (2026-09-15) patches **every module's CRT import slots** and feeds the same side table (tables
  `MemImportSummary`, `MemImportModules`; census `MemBlockOwners`, `MemBlockOwnerHeap`, `MemBlockContent`,
  `MemHeapClassDetail`): the CRT heap became 99.85% attributed (EXE 927 MB, DLL ~85 MB). Traps: one thunk kind per
  allocator, never per signature (`_aligned_free` routed to `free` killed the game in 70 s); aligned blocks keyed on
  their base; trust the census over table-side live totals. Its pools are `MEM_TOP_DOWN`. It never re-patches a
  module it has patched, so EngineAllocTrace can chain in front of it.
- Their on/off switch is not described in the sources as a marker file; the shell-gates experiment DLL ran with
  "MemoryImports/MemoryHooks + EngineAllocTrace on" and cost ~140 MB private over a plain 5.4.8 DLL - compare only
  deltas between runs of the same DLL.

### MemoryDiagnostics

- `MemoryDiagnostics::PollSnapshotRequest`: a full memory snapshot on request (event `Local\VPMemSnapshot`, label in
  `memsnap_request.txt`): address space split at 2 GB, every heap with segment commit below 2 GB, the per-block
  ownership census, hook and Lua totals, into ten `MemSnap*` tables keyed by SnapSeq + Label (`MemSnapHeapFree` holds
  the free entries per heap and size class - the fragmentation view). ~350 ms per snapshot on a turn-316 huge map.
  Driven by `myth_watch.py` (civ5-game-ui `experiments/memory-myths/`).
- `MemoryDiagnostics::GetProbeLuaState()`: the persistent Lua thread the channel reuses. **Never
  `CreateLuaThread`/`FreeLuaThread` from the DLL at run time**: the first channel did it per request and the game
  died on the second request ("invalid key to 'next'", dump `CvMiniDump_20260917_002305`).
- Related: the Lua memory profiler tiers (`game_session.py lua-profiler`, writes `C:\Users\Public\lua_memprof.csv`).

### The Lua channel (`LuaSupport::PollExternalLuaRequest`, `CvLuaSupport.h/.cpp`)

Called from the top of `CvGame::update`; event `Local\VPLuaExec`, request file in the cache folder; opt-in by
`luaexec.enabled` or `VP_LUAEXEC`. Chunks run inside `ICvEngineScriptSystem1::CallCFunction` (holding the Lua lock)
on the game-core thread. Use and hazards: run-and-drive-the-game.md section 3.

### ShellHelperLoader (`ShellHelperLoader.{h,cpp}`, commit 0a7795fdf)

Loads the DLL named on the first line of `crashlogs\shell.helper` once with `LoadLibraryW` from `DllGetGameContext`;
logs to `crashlogs\shell.log`. Used to inject the Gate 3 compositor (`shell_composite.dll`); see
`05-graphics-composition/shared-texture-and-compositor.md`.

### Other DLL-side switches seen

- `EXTERNAL_PAUSING` (compiled into every build): the `TurnByTurn` named mutex freezes the DLL (`CvGame::ExternalPause`).
- Stats logging into `cache\stats.db` (SQLite, batched up to 1,024 rows, `SQLITE_LOGGING_BATCHED_BUFFER_ROWS_MAX`;
  rows carry GameId and RunId - see stats-db-game-ids.md in the sources).

## 3. Where the code lives

| Branch / commit (Community-Patch-DLL checkout) | Contents |
|---|---|
| `f181104a2` (memory-diagnostics, 2026-10-07, pushed to `origin/memory-diagnostics`) | Lua channel, MemoryImports (+ MemoryHooks), EngineQueueGuard (DX11, DllGetGameContext install, capture code in the working tree), MemoryDiagnostics; also, by accident, the two skills' sources |
| `claude/shell-gates-d8e422` (worktree `.claude/worktrees/shell-gates-d8e422`, not pushed) | `f181104a2` merged with `Release-5.4.8` (eeb9305e1) + EngineAllocTrace and ShellHelperLoader (0a7795fdf) + experiments (`experiments/shell-gates/`: nofocus, compositor, alloc-trace, tools, tooling, camera, stubs); last commit a253135e7 (2026-11-10, STUBS.md) |
| `instance-aware-instruments` (440878983, worktree `%USERPROFILE%\Documents\GitHub\Community-Patch-DLL-instance-aware`, not pushed; added 2026-11-10, epic 1100) | `Release-5.4.8` + shell-gates `c60138b82` + per-instance names: with env `VP_INSTANCE=<id>` (`[A-Za-z0-9_-]{1,32}`, else legacy and a REJECTED log line) `Local\VPLuaExec(Done)`, `Local\VPMemSnapshot(Done)` and `TurnByTurn` get the suffix `_<id>`; unset = legacy names; effective names logged to `crashlogs\instruments.log`; release DLL SHA-256 `5fdf34321be1116e...` (17,452,544 B). Details: `../04-memory-investigation/instruments-and-tooling.md` section 6 |
| `engine-queue-guard` (2f954ff99, ac7afabc2; the DLL fork) | EngineQueueGuard only, all three EXEs, gated on BIN_HOOKS; 11 files, +484/-1 for the first commit |

All these instruments are installed from `DllGetGameContext` in the shell-gates DLL, in the order MemoryImports,
EngineQueueGuard, EngineAllocTrace (GATES / alloc-trace-README), except the BIN_HOOKS-gated queue guard on its own
branch (installed from `CacheGameDatabaseData`). New `.cpp` files need the `CPP` list in `build_vp_clang.py` and all
five `.vcxproj` files.

Sources: `research/sources/shell-gates/alloc-trace-README.md`, `GATES.md` (Gate 1 instrument notes, branch line),
`compositor-README.md` ("Loading the DLL into the real game"), `BRANCH-COMMITS.txt`,
`research/sources/memory-notes/civ5-exe-message-queue-overflow.md`, `civ5-alloc-hook-instrument.md`,
`civ5-game-ui-skill.md`, `civ5-vp-dll-dev-skill.md`, `civ5-shell-gates.md`, `civ5-exe-disable-systems.md`,
`research/sources/skills/civ5-game-ui-SKILL.md`, `vp-dll-dev-SKILL.md` (troubleshooting),
`research/sources/boundary-benchmark/REPORT.md` (ac7afabc2 commit id).
