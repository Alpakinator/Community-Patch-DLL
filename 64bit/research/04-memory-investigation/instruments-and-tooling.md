# Instruments and tooling

What was built to measure the process, what each instrument can and cannot see, what was ruled out, and
the operating rules that cost time to learn. Code lives in the Community-Patch-DLL repo on branch
`memory-diagnostics` (later state `f181104a2`, which also carries the Lua channel, MemoryImports and
EngineQueueGuard; the shell-gates branch `claude/shell-gates-d8e422` merges it with Release-5.4.8).

## 1. DLL-side instruments

### MemoryDiagnostics - the per-turn census (built 2026-09-03..05)

`CvGameCoreDLL_Expansion2/MemoryDiagnostics.{h,cpp}`, called as `MemoryDiagnostics::LogTurn()` first inside the
`MOD_SQLITE_LOGGING` block of `CvGame::LogGameState` (once per turn, end-of-turn path). Writes `Mem*` tables to stats.db.
Define `MEMORY_DIAGNOSTICS_HEAP_WALK_INTERVAL`, default 1 (every turn; 0 disables heap walking only). Heap walk 79-265 ms even at
2.9 M blocks (125 -> ~344 ms with the MemoryImports census on). `GD_INT_CACHE_OPTIONAL` lets a DLL dropped into an older database
fall back instead of asserting.

| group | tables |
|---|---|
| address space | `MemAddressSpace` (totals, sub-2GB split incl. `CommittedLowKB`, `LargestFreeLowKB`), `MemFreeBlockHistogram` (whole-4GB only), `MemRegions` (64 largest committed regions; reserved regions not itemised) |
| heap | `MemHeapSummary`, `MemHeapSizeClass` (18 classes), `MemTopBlocks` (96 largest), `MemHeapDetail` (per heap, role bitmask by address identity), `MemTopBlockSamples` (40 largest, leading 8 words) |
| leak window | `MemBlockSizes`, `MemNewBlocks`, `MemBlockWindow`, `MemAnchors` |
| model/context | `MemGameState` (analytic census, a null result), `MemSizeof`, `MemEntityCounts`, `MemReplayData` |

Design constraints: **nothing may allocate inside the `HeapWalk` loop** (destinations are preallocated PODs). The instrument agrees with
the crash handler's `VirtualQuery` walk to the kilobyte. Heap membership must be tested by **address identity during the walk**, not
`HeapValidate` (which returned TRUE for blocks it does not own - retracted 2026-09-07).

### MemoryHooks - allocation-site attribution (2026-09-08)

`MemoryHooks.{h,cpp}` replaces the global `operator new`/`delete` in the DLL and records per allocation the caller's return address and a
subsystem tag (16 `MEMHOOK_SCOPE` points). Tables `MemHookSummary` (incl. `HookLive`, the runtime proof it won the link, and `OverheadKB`),
`MemHookTags`, `MemHookSites` (top 64 sites as RVAs). Facts that make or break it:

1. FireWorks (`FirePlace/lib/FireWorksWin32.obj`, member `FMemHooks.obj`) already defines the global operators, and they route through the
   host EXE's **Small Block Allocator** first (`FSBABind`/`FSBAAllocate`/`FSBADeallocate`, then `malloc`/`free`). A replacement built on
   `malloc`/`free` would hand SBA pointers to `free()` and corrupt the heap.
2. The hook forwards to FireWorks' byte-identical six-argument "tracked" twins (`??2@YAPAXIHPBDHHH@Z` and friends), which survive the replacement.
3. **Link order decides whether it runs at all:** lld-link with `/FORCE:MULTIPLE` keeps the first definition; `build_vp_clang.py` has a
   `LINK_FIRST` list putting `MemoryHooks.obj` first. Check the four `duplicate symbol` warnings list MemoryHooks.cpp first, and `HookLive = 1`.
4. A VirtualAlloc'd side hash table keyed by address (never a block header, never the heap); one frame via `_ReturnAddress()` (no stack walk:
   release builds omit frame pointers). Resolve RVAs with `llvm-symbolizer` against ImageBase 0x10000000 and take the **outermost** inline frame;
   `/OPT:ICF` can alias the demangled type name.

Knobs: `VP_MEMHOOK=0` disables; **`VP_MEMHOOK_NODES_K`** sizes the node pool (user environment variable - Steam relaunches the exe). The original
2 M-node pool **saturated at t259 on the 20,340-plot map** (`UntrackedAllocs` 1.11 billion by t317), so that run's hook rows past ~t255 are floors;
raised to 6 M nodes / 4 M buckets on 2026-09-15. Cost: ~26 MB committed (40.75 MB at G62 t314, ~16 turns of life at 2.476 MB/turn - quote it with any
death turn from an instrumented run); no A/B against a hookless build was ever run. Blind spots: `std::string` bodies (allocated in msvcp90),
the `CvDllGameContext` private heap, everything the engine allocates.

### MemoryImports - CRT import-slot attribution (2026-09-15)

`MemoryImports.{h,cpp}` rewrites each module's MSVCR90 import slots (`malloc`, `calloc`, `realloc`, `free`, `_aligned_malloc`, `_aligned_free`,
`operator new/new[]/delete/delete[]`) to per-module thunks, installed from `DllGetGameContext()`; blocks go into MemoryHooks' table with a module id.
A startup heap walk marks every pre-existing block as `PreDLL (EXE side)` (16 MB `MEM_TOP_DOWN` set). Census tables `MemBlockOwners`,
`MemBlockOwnerHeap`, `MemBlockContent`, `MemHeapClassDetail`; summary `MemImportSummary`, `MemImportModules`. Result: CRT heap **99.85% attributed**,
EXE 927 MB, DLL ~85 MB (see ownership-and-ceiling.md). Knobs `VP_MEMCENSUS=0`, `VP_MEMCENSUS_STRIDE`. Traps (each cost a crash or a build):
**one thunk kind per allocator, never per signature** (`_aligned_free` routed to `free` killed the game in `RtlFreeHeap` in 70 s); key aligned blocks on
their base `((void**)p)[-1]` (`AlignedUntracked` must stay 0); trust the census over table-side totals (they drift above reality). All instrument tables
are now `MEM_TOP_DOWN` so they stay out of the sub-2GB half.

### Lua memory profiler - three tiers (2026-09-11)

| tier | where | what |
|---|---|---|
| 0 | `MemoryDiagnostics::SampleLua()` | `lua_gc(LUA_GCCOUNT)` on a cached probe thread; one number |
| 1 | `InstallLuaAllocHook()` (swaps `lua_Alloc` via `lua_setallocf`, from `LuaSupport::InitLuaFramework()`) | sizes, counts, peak, churn, histogram; tables `MemLua`, `MemLuaAlloc`, `MemLuaSizeClass`, `MemLuaBigSizes` |
| 2 | rebuilt `lua51_Win32.dll` (stock Lua 5.1.5 from `vox-deorum-lua51`, `lmemprof.c` + 3-line hook in `lmem.c`) | per Lua source line; writes `C:\Users\Public\lua_memprof.csv` (`VP_LUAPROF_OUT`) |

Self-checks: `MemLua.AllocFn` must change from `00CFE440` (the EXE's allocator) to an address in the DLL; Tier 1 and Tier 0 agree to the KB; Tier 2's
`<untracked frees>` and `<map full drops>` must be 0. Seed Tier 1 from `lua_gc` (41.7 MB exists before the hook). The rebuilt DLL must export the identical
123 symbols and import MSVCR90; `clang-cl` needs `-m32`. Stock DLL backed up as `<Civ5>\lua51_Win32.dll.stock`. Since 2026-09-14 Lua's size is
also readable without any tier: heap index 12.

### On-demand snapshots and the Lua channel (2026-09-16/17)

- `MemoryDiagnostics::PollSnapshotRequest`: event `Local\VPMemSnapshot` triggers a snapshot into nine `MemSnap*` tables keyed by SnapSeq + Label (plus
  `MemSnapHeapFree`, 2026-09-17; `MemSnapModules`). Option `heaps=0` (DLL d1484d63) skips the heap walk - added after a heap walk hung forever in
  `RtlWalkHeap` during the units test.
- `LuaSupport::PollExternalLuaRequest`: event `Local\VPLuaExec` runs Lua in any Lua state from outside (FireTuner without the GUI). **Never create or
  free Lua threads at run time from the DLL** (the first version did, per request, and crashed the game in `lua_next`); reuse
  `MemoryDiagnostics::GetProbeLuaState()`. `Game`/`Players` are not in `_LOADED._G`. While a leader screen is up `CvGame::update` does not run, so
  the channel and snapshots are dead.
- `SQLITE_LOGGING` ships as 0 in the MODS-folder database and gates both `LogTurn()` and the snapshot poll.

### RunId stamping

Every logged row is stamped with a RunId identifying the game process (commit *Stamp every logged row with a RunId identifying the game process*,
2026-09-07). Real RunIds are large process-derived integers; `run_dictionary` rows numbered 100000+ are the older inferred backfill (games up to 42).

### EXE-side hooks built by the DLL (pointers only; detail belongs elsewhere)

- `EngineQueueGuard.{h,cpp}` (2026-09-21): patches the EXE's `InterlockedExchangeAdd` import and diverts message-queue records that would overrun a
  4 MB buffer; opt-in capture via `crashlogs\queueguard.capture`. Branch `engine-queue-guard` (DX11, DX9, Tablet).
- `EngineAllocTrace.{h,cpp}` (2026-11-09, shell gates): thunks on the EXE's msvcr90 import slots that read file/line/tags from the engine allocator's
  frame; opt-in `crashlogs\allocTrace.enabled`. Misses allocations made before the DLL loads.

## 2. stats.db

Written by the `MOD_SQLITE_LOGGING` logger to `Documents\My Games\Sid Meier's Civilization 5\cache\stats.db` (the repo-root copy is a manual
snapshot and goes stale). Rules:
- **Always filter by GameId.** Sessions of one game overlap; split by `rowid` where the turn sequence stops advancing (strict decrease for
  `MemNewBlocks`); never `GROUP BY Turn`.
- **Changing a table's columns drops the whole table** and its history (`RegisterTable` drops on schema mismatch; adding `CommittedRegions` destroyed
  pre-2026-09-05 `MemAddressSpace`). Add a new table instead.
- **No duplicate column names** (case-insensitive, including GameId/Turn/RunId): `RegisterTable` refuses the table and the release-build assert
  dialogs crash the game.

### GameId map

| GameId | what |
|---|---|
| 1 | standard map, healthy control run to t332 (~1.06 MB/turn, 95.7% of free memory in one block) |
| 2 | huge 128x80, 14 majors, pre-fix; three sessions: t0-235 (crash), reload 235-366, reload 356-390 (finished; final row is a post-teardown snapshot). Turn 235 appears twice - the most valuable comparison in the DB |
| 25 | 79x53 small map, t0-46; the AILog A/B pair and the t46 load-baseline (turns 15-46 appear three times) |
| 36-39, 43 | throwaway probes |
| 40 | small-map reference: fresh t0-200 with AILog=0, then a 199 -> 200 load-baseline |
| **47** | **the post-fix reference run**: huge 128x80, 12 majors / 37 alive, AILog=0, fix in, one session t0-224 (RunId 1788799471); later reloads (1788818637 t224-226; 1788825028 t227-228 = the good census/block-sample run); hook run t240-250; later extended to t319 |
| **62** | **the 20,340-plot map** (180x113, 20 majors): RunId 1789016075 t0-317 crashed (OOM, no minidump); 1789075474 t317-344 reload to a science victory. Use the reload for hook attribution; compare t317 pre-crash with t318 |
| 64 | huge Industrial-era start (logged Turn = game turn - 250), Memory Myths runs 1789622091 and fragmentation run 1789663915 |
| 74, 76, 77 | small-map Lua profiler runs (77 = the Tier 2 profile, ledger point "small t7") |
| 78-80 | MemoryImports builds (80 = the 99.85% result) |
| **88** | **20,340 plots, 20 majors, observer mode**: the OOM test saves (OOM run 1 RunId 1789964844, run 6 RunId 1789975147; queue-overflow loads 1789971440 etc.) |
| 118 | `VP8P-HUGE_0350` baseline (128x80, 8 majors, t350) for the mods/graphics myths |

## 3. External tools

- **TurnByTurn mutex** freezes turn advancement from outside (needed because loads auto-advance).
- **1 Hz `VirtualQueryEx` timeline** from a second process reproduces `SampleAddressSpace` exactly; clamp the last region to the 4 GB window. Must
  split low (below 2 GB) from high - a whole-4GB "largest free" of ~1 GB is meaningless if the low half is at 1.6 MB.
- **VMMap: always the 32-bit `vmmap.exe`, never `vmmap64.exe`.** vmmap64 cannot read WOW64 heaps, shows a modal error and still writes a snapshot with
  exit code 0 in which Heap is ~0 and a 137 TB free region appears. Verify a snapshot by a non-trivial `Heap` line. `vmmap.exe` deletes `vmmap64.exe`
  as it runs; it returns before the file is written (poll the size); ~4 s per snapshot with the game stalled. VMMap "Heap ID" = HeapIndex + 1.
- **VRAM:** `\GPU Process Memory(*)` counters (`Dedicated Usage` + `Shared Usage`, not Local/Non Local); `nvidia-smi` cannot do per-process VRAM under WDDM.
  In-DLL `IDXGIAdapter3::QueryVideoMemoryInfo` was proposed, not built.
- **Scroll Lock marker** via `GetAsyncKeyState(VK_SCROLL)` from the watcher.
- **cdb** (x86) at `C:\Program Files (x86)\Windows Kits\10\Debuggers\x86\cdb.exe` for minidumps; the EXE has export symbols only (no PDB).
- Myth tooling in the Community-Patch-DLL repo under `.claude/skills/civ5-game-ui/experiments/`: `memory-myths/`, `mod-graphics-cost/`
  (`night_batch.py`, `protocols/ui_myths.py`, `protocols/units_myth.py`, `analyze_n5.py`, `analyze_units.py`, `arena_probe.py`, `render-buffer/`, `yim/`),
  `oom-test/` (`oom_drive.py`, README), `queue-capture/`; `myth_watch.py` (1 Hz timeline, hang guard `--hang-timeout`, stall capture).
- PowerShell gotcha: `continue`/`break` inside `try {}` escapes the block; an empty `catch {}` hid it. Never write an empty catch.

## 4. Ruled out - do not redo

- **UMDH / gflags (2026-09-08): never again.** With `+ust` verified active (PEB32 NtGlobalFlag `0x1000` read via `dd @$peb+0x68`, every heap flagged
  `HEAP_CAPTURE_STACK_BACKTRACES`, trace DB with 1400 stacks / 1,280,024 lookups), a full 23-minute frozen UMDH walk returned **`BackTrace0` on every block**.
  Cause: the **Low Fragmentation Heap**, active on every heap; LFH subsegment blocks carry no ust index. Page heap crashed the game during startup (its
  `/size` range is parsed as decimal). Traps: `!gflag` lies for WOW64 targets; the registry is not a check; "Stack Trace Database is full" is a lie when
  `_NT_SYMBOL_PATH` is empty; an `srv*...msdl` path freezes the game while fetching PDBs. Registry reverted (`GlobalFlag = 0`). Use the DLL hook instead.
- `RtlCaptureStackBackTrace` (FPO release builds return plausible junk).
- The analytic `sizeof` census (caps at 2.4%: AI objects are thin handles over containers it cannot see).
- `HeapValidate` as a membership test.
- Turn-0 committed as a baseline (map generation cost 2351 MB vs a t199 load at 1945 MB) - compare load costs to load costs.
- Committed-memory deltas for effects under ~50 MB (committed swings ±15-20 MB turn to turn; use heap tables, or committed excluding driver buffers).

## 5. Save rules

- **`ModdedSaves/` saves never load under the modpack paradigm** (VP as DLC in `Assets/DLC/VP_MODPACK`): the mod list in the header crashes the reconcile.
  Modpack games save to `Saves/single/auto/`.
- **Killing the game mid-autosave corrupts the save.** `AutoSave_Post_NNNN` is written at the same instant as that turn's stats.db row; waiting lets the
  game write `target+1`, which the kill then truncates. Prune autosaves past the target after the process dies; check `final_save_settled`.
- **A load rewrites the loaded turn's autosave** within seconds - archive before reloading.
- Filter crash-dialog detection by owning PID.
- Industrial-era starts log Turn = game turn - 250 (GameId 64). Observer-mode games keep autoplaying on every load (`m_iAIAutoPlay` is serialized).
- For the mods/graphics arms InfoAddict was edited to `AffectsSavedGames 0`, so one baseline save serves arms with and without it.
- Ask for runs with headroom: a save already at ~40 MB free dies before producing a useful series; 10-20 clean turns are enough.

Sources: `research/sources/memory-notes/civ5-memory-investigation-log.md` (sections 2, 3, 4, 5, 7), `civ5-alloc-hook-instrument.md`,
`civ5-lua-memory-profiling.md`, `civ5-umdh-tooling.md`, `stats-db-game-ids.md`, `civ5-modpack-save-rules.md`, `n5-run-plan.md`,
`units-myth-plan.md`; `research/sources/shell-gates/GATES.md` (instrument notes).

## 6. Per-instance names (instance-aware-instruments, 2026-11-10)

Issue 1107 (epic 1100). So that several game processes can run on one machine without answering each other's
instrument requests, the instruments' fixed kernel-object names (the DLL rows of
`06-tooling-and-workflows/headless-and-parallel-testing.md` section 4.6) take a per-instance suffix.

**Where:** Community-Patch-DLL branch `instance-aware-instruments` (not pushed), worktree
`%USERPROFILE%\Documents\GitHub\Community-Patch-DLL-instance-aware`. Cut from tag `Release-5.4.8` (7307aefdf), then
`de37b9198` = `--no-ff` merge of `claude/shell-gates-d8e422` at its tip `c60138b82` (the whole instrument lineage plus
`experiments/`, `.claude/skills/`, `.mcp.json` and the `MEMORY_DIAGNOSTICS_HEAP_WALK_INTERVAL` define in
`CoreDefineChanges.sql`, all as on shell-gates), then the change itself, commit `440878983`. The shell-gates tip is now
`c60138b82`, two experiments-only commits (GATE2.md, LOADTIME.md) past the `a253135e7` named in
`06-tooling-and-workflows/instrumentation-in-the-dll.md` section 3.

**Naming rule** [C, source]: environment variable `VP_INSTANCE=<id>`, `<id>` matching `[A-Za-z0-9_-]{1,32}`:

| Legacy name (unset) | With `VP_INSTANCE=<id>` | Code |
|---|---|---|
| `Local\VPLuaExec` | `Local\VPLuaExec_<id>` | `CvLuaSupport.cpp` `PollExternalLuaRequest` |
| `Local\VPLuaExecDone` | `Local\VPLuaExecDone_<id>` | same |
| `Local\VPMemSnapshot` | `Local\VPMemSnapshot_<id>` | `MemoryDiagnostics.cpp` `PollSnapshotRequest` |
| `Local\VPMemSnapshotDone` | `Local\VPMemSnapshotDone_<id>` | same |
| `TurnByTurn` (no `Local\`) | `TurnByTurn_<id>` | `CvGame.cpp` `ExternalPause` (EXTERNAL_PAUSING), one-line change |

- Unset keeps the legacy names exactly. Empty, longer than 32 characters or any other character: rejected, legacy
  names, and the log line says `VP_INSTANCE REJECTED (<n> chars; must be 1-32 of [A-Za-z0-9_-])`.
- Read once per DLL load (`InstanceNames::Get()` from `DllGetGameContext`, or on first use; thread-safe once).
- The effective names are logged once per load to `crashlogs\instruments.log` (new file; relative to the current
  directory = the install folder, like `queueguard.log`, `allocTrace.log`, `shell.log`; no `crashlogs` folder, no
  log) and to OutputDebugString. The Lua channel had no log of its own, hence the new file. Line format:
  `2026-11-10 02:47:28  pid 30264  instance names: VP_INSTANCE=inst1; Local\VPLuaExec_inst1 Local\VPLuaExecDone_inst1 Local\VPMemSnapshot_inst1 Local\VPMemSnapshotDone_inst1 TurnByTurn_inst1`
- Code: new header-only `CvGameCoreDLL_Expansion2/InstanceNames.h` (no new .cpp, so no `build_vp_clang.py` or
  `.vcxproj` change); the name constants in `CvLuaSupport.cpp` and `MemoryDiagnostics.cpp` are gone, replaced by
  `InstanceNames::Get().sz...`.
- Client side: `tools/harness/lua.py` `channel_names()` builds the same Lua channel names (its own client mutex
  `Local\VPLuaExecClient_<id>` exists only on the client side). Existing tools (`vp_lua.py`, `myth_watch.py`, any
  TurnByTurn holder) keep working against a process started without `VP_INSTANCE`, and need the suffix for one
  started with it.

**Build** (2026-11-10 02:48-02:58 EST, `build_vp_clang.py --config release` with the PATH prefix of
`06-tooling-and-workflows/build-and-install-dll.md`): clean; the only warnings are the four expected `duplicate symbol`
lines for the global operators with `MemoryHooks.cpp` first, and LNK4099 (no PDB for Firaxis libs). Embedded version
`Release-5.4.8-22-g440878983 Clean`. Output kept at
`%USERPROFILE%\Documents\GitHub\Community-Patch-DLL-instance-aware\clang-output\Release-instance-aware-440878983\`:

| File | Bytes | SHA-256 |
|---|---|---|
| `CvGameCore_Expansion2.dll` | 17,452,544 | `5fdf34321be1116eb6f8fdba94fa98acfaf5bee5dc1d315ab43d2fe796623787` |
| `CvGameCore_Expansion2.pdb` | 95,309,824 | `71b4a8eebac638ec04b00fe37392b829bdb81dbc374658a822e7ddb09c8ea981` |

Installed nowhere; 1106 copies it into instance folders. It is a 5.4.8 DLL (5.4.8 mods and saves only).

**Verified** (2026-11-10): [C] the built DLL holds the five format strings `...%s%s` and `crashlogs\instruments.log`,
and none of the legacy literals `TurnByTurn\0`, `Local\VPLuaExec\0`, `Local\VPMemSnapshot\0`. [C] A standalone 32-bit
test program compiled with clang-cl against the same `InstanceNames.h` (scratch, not committed) gave names identical
to `lua.py channel_names()` for VP_INSTANCE unset, empty, `inst1`, `A-b_9`, 32 and 33 characters, `bad id`, `a/b`,
`a.b` and a non-ASCII letter, and wrote one log line per process. **Not verified:** nothing in a running game (no
launches in 1107); the live check of the log line and of the channel under suffixed names is 1108's.
