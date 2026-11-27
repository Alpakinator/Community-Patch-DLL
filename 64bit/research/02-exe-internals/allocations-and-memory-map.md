# Engine allocations and the EXE's memory map

Sources: `shell-gates/alloc-trace-README.md`, `shell-gates/GATES.md` ("Owners of the blocks", Gate 1 tables),
`memory-notes/civ5-exe-disable-systems.md`, `memory-notes/civ5-alloc-hook-instrument.md`,
`memory-notes/civ5-memory-investigation-log.md` (ceiling section, MemoryImports section, item 30, YieldIconManager
A/B), `memory-notes/civ5-64bit-architecture-options.md`, `shell-gates/stubs/terrain.md`, `shell-gates/TOOLING.md`,
`exe-world-subsystems/allocsites.txt`, `exe-world-subsystems/imports.txt`.

DX11 VAs at ImageBase 0x400000 (TimeDateStamp 0x546CD0A8) unless marked. Trace logs print RVAs (`EXE+0x...`); add
0x400000. [C] confirmed, [I] inferred, [L] measured live.

## 1. Image layout [C]

- PE characteristics 0x0122: `IMAGE_FILE_LARGE_ADDRESS_AWARE` set.
- Sections (Ghidra import, TOOLING.md): `.text` from 0x401000 (5.8 MB; the disassembly range used is
  0x401000-0x9D5D30), `.rdata`, `.data` from 0xB94000 (**86 MB virtual**; holds the static world-system objects, the
  4 MB double-buffered queue, the pose pool, the view globals), `.shader`, `.rsrc`, `.reloc`. 21,009 functions after
  auto-analysis. Only 45 RTTI descriptors; names come from profiler strings and allocator `__FILE__` paths.
- `.text` raw data starts at file offset 0x400 and VA 0x401000, which is why `crashes.log` "Location (in file)"
  needs + 0xC00 to become an RVA (`addresses.md`).
- Imports (`imports.txt`, 921 symbols from 30 libraries): the Visual C++ 2008 runtime (the C library's allocation
  calls among them), Direct3D 9 and 11 with DXGI, the D3DX 9 and 11 helper libraries (the D3DX math thunks are used by
  the camera code), Steam, Lua 5.1, the game's two own libraries (localization and game database), the Miles audio
  library, zlib, and the usual Windows system libraries (kernel, user, graphics, shell, COM, networking, version,
  debug help). The import tables of KERNEL32 and the C runtime start at RVA 0x5D6244 and 0x5D664C; the single slots
  the product and the instruments use are named where they are used (`addresses.md`). (Import list replaced by a
  description, 2026-11-18, D83 audit.)
- `allocsites.txt` (1,624 call sites with a pushed source-file string): 281 call the engine allocator 0x82C090 with
  a `.\App\...cpp` path and line; the most frequent file strings are OpenSSL sources (`.\ssl\...`, `.\crypto\...`,
  hundreds of sites through other allocators such as 0x9394C0 with 784 sites) and `.\shaderautogen\ShaderDefDX11.cpp`
  (94). That the EXE statically links OpenSSL is an inference from those strings [I].

## 2. The engine allocator [C]

| VA (DX11) | What | Callers |
|---|---|---|
| **0x82C090** | `operator new(size, count, file, line, tag, tag2)`, cdecl. Reads only `size`: `if (!size) size = 1;` small-block allocator (SBA) if it takes it, else `malloc(size)` through import slot 0x9D67E0. Its prologue is the byte pattern the UI-draw hook checks before it hooks (`proxy/src/proxy_uidraw.cpp`; prologue bytes replaced by this pointer, 2026-11-18, D83 audit) | 1,536 direct: 915 pass all six args, **176 pass size only** (plain `operator new(size)` folded onto the same code); `count` is 1 at every site seen |
| **0x813A80** | FFastVector's `FMALLOCALIGNED(size, align, file, line, tag, tag2)`. `align != 0` -> helper 0x813AB0 (SBA, else `_aligned_malloc` via 0x9D66E0); `align == 0` -> `_aligned_malloc(size, 0)` directly | 749 sites; 388 name `FireWorks\FFastVector.h` line 85, tag 0x2B |
| 0x82C0E0 | `FSBADeallocate(p) \|\| free(p)`; no direct callers - inlined at ~650 sites (679 calls of FSBADeallocate, 651 through the `free` slot 0x9D67E4) | - |
| 0x813CF0 | aligned free -> `_aligned_free` slot 0x9D66D8 | - |

Other paths [C]: `realloc` (slot 0x9D66DC) at 4 sites, incl. a file-read buffer at 0x7CA5AE and two small-vector
growers; 234 direct `malloc` callers; 46 direct `_aligned_malloc`; 31 `HeapAlloc`; 2 `VirtualAlloc`. No `memset`
in the EXE fills with 0x25 or 0x26 (677 memset-thunk calls checked).

Same three allocator functions, byte-identical, in all builds: DX11 0x82C090 / 0x813A80 / 0x813AB0; DX9
(0x546CCB59) 0x81CAE0 / 0x81CB80 / 0x81CBB0; Tablet (0x546CD5F8) 0x814CA0 / 0x814A10 / 0x814A40.

**The DLL allocates through the EXE too** (`civ5-alloc-hook-instrument.md`): FireWorks' `FMemHooks.obj`
(in `FirePlace/lib/FireWorksWin32.obj`) replaces the DLL's global operators with
`FSBABind(); p = sgpfSBAAllocate(n); if (!p) p = malloc(n);` and the matching delete, i.e. DLL `new` goes to the
host EXE's small-block allocator first. The DLL and the EXE share one CRT heap.

## 3. How allocations are attributed without patching `.text`

CEG constraint (PR #13379's EXE-interop notes, cited in GATES.md and alloc-trace-README.md): Steam's CEG hashes
~80% of `.text` and two thirds of `.rdata` in the loaded image; a patched byte crashes the game later, far from
the change. Every instrument therefore writes import slots or heap data only.

- **MemoryImports** (DLL, 2026-09-15): rewrites every module's MSVCR90 import slots (`malloc`, `calloc`, `realloc`,
  `free`, `_aligned_malloc`, `_aligned_free`, operator new/delete variants) to per-module thunks
  (`template<int SLOT>`), installed from `DllGetGameContext`; a heap walk at install marks every pre-existing live
  block as `PreDLL (EXE side)`. Result, GameId 80 turn 7, CRT heap 1,169.6 MB walk-verified: **EXE 927.0 MB** (561.3
  in blocks > 1 MB, 365.6 under), PreDLL 155.3, DLL via operator new 83.3, Unknown 1.7 (0.15%), MSVCP90 strings
  1.0, DLL direct 0.8, CvGameDatabase/CvLocalization 0.4. It records the module only, not EXE return addresses.
  Traps: one thunk kind per allocator (routing `_aligned_free` to `free` killed the game in `RtlFreeHeap` in 70 s);
  aligned blocks must be keyed on their base `((void**)p)[-1]`.
- **EngineAllocTrace** (DLL, 2026-11-09, `EngineAllocTrace.{h,cpp}`): chains thunks in front of the EXE's five
  msvcr90 slots (malloc, _aligned_malloc, realloc, free, _aligned_free), each replaced with one aligned interlocked
  exchange. At a request >= threshold it checks its return address: if it is the known spot inside 0x82C090
  (`+0x45`), the aligned helper (`+0x52`) or aligned new (`+0x24`), the frame is fixed and it reads the real caller,
  file, line, tag, tag2 and count/align (`api=new` / `anew`); otherwise the caller of malloc is the caller. Scans up
  to 384 stack words for EXE code addresses (keeps 16; the EXE has no frame pointers, so some are stale). Failed
  requests are logged at any size. Verification before patching: TimeDateStamp, byte patterns of all three
  allocators, the aligned-new -> helper call, and the absolute operands of the three `call [slot]` instructions.
  Opt-in `<install>\crashlogs\allocTrace.enabled` (content = threshold, default 1M, min 4 KB) or `VP_ALLOCTRACE=1`;
  log `crashlogs\allocTrace.log` (A/R/F/T lines); `summarize.py` groups it. Offline self-test calls the EXE's own
  allocators in a test process (EXE mapped with `DONT_RESOLVE_DLL_REFERENCES`): passed for DX11, DX9 and Tablet,
  `/Od` and `/Ox /Ob2`, 2026-11-09.
- **Why not an inline hook at 0x82C090**: (1) the first instruction `push esi` is 1 byte, so a 5-byte jump cannot be
  written atomically for a thread between instructions 1 and 2; (2) CEG; (3) the import route also sees the 234
  direct `malloc`, 46 `_aligned_malloc` callers and `realloc`.
- Limits: misses everything allocated before the game DLL loads (EXE start-up to main menu, e.g. the 8 x 24 MB
  FluidFOW blocks); does not see `HeapAlloc`/`VirtualAlloc`, msvcr90-internal or other modules' allocations; if the
  engine unloads the DLL, the EXE's slots point at unmapped code (shared hazard with MemoryImports and
  EngineQueueGuard; `Uninstall` on `DLL_PROCESS_DETACH` restores the trace's own slots).
  **Confirmed for MemoryImports [C, dump, 2026-11-14, queue ticket 0164]:** MemoryImports has no uninstall; after a mod
  change unloads an `-ia` game DLL, CvGameDatabase's `malloc`/`_malloc_crt`/`realloc`/`free`/`operator new`/`delete`/
  `delete[]` slots still point into the freed image, and the base game DLL's static initialisers end the EXE through
  `operator new` (`../03-dll-engine-boundary/proxy-design.md` section 21). With `VP_MEMIMPORTS=0` the `-ia` build's Lua
  allocator hook (`lua_setallocf`, no switch, no uninstall) ends the EXE the same way at the next Lua allocation; a run
  that changes the mod set uses the plain build.

## 4. Named large blocks

Live trace, huge save `VP8P-HUGE_0350` t350 (GATES.md, 2026-11-09): **117 engine allocations >= 1 MB** during the
load.

| Owner (file:line) | Size | Notes | Gone with |
|---|---|---|---|
| `FluidParticles.cpp:718` | **48.24 MB** | aligned 128, `cells x 4096`, main thread during load, largest live engine block, not reallocated over three turns | FluidFOW = 1 (zero FluidParticles allocations in B4) |
| `FluidParticles.cpp:723` | **24.12 MB** | aligned 128, `cells x 2048`; with :718 the 2:1 pair | FluidFOW = 1 |
| `FluidParticles.cpp:719` | 1.51 MB | `cells x 128` | FluidFOW = 1 |
| FluidFOW per-thread particle blocks, `FluidParticles.cpp` site 0x68F795 (line 0x26F) | **8 x 24 MB** (`new(0x1800000)`) | loop over the job-thread count (`MaxSimultaneousThreads = 8`), constant, map-independent, allocated at app start-up (present at the main menu, invisible to the trace) | FluidFOW = 1 |
| `UIManager.cpp:3580` (0xDFC, site 0x6C7EC1) | 6 x 4 MB | both configs | - |
| FFastVector (`FFastVector.h:85`) | 15-19 MB | both configs | - |
| `Terrain.h:79` | 8.7 MB (GATES.md) / 9.1 MB (terrain.md) | the GridHeightmap tile pool [I] | only by skipping the bake (Terrain option c3) |
| CommandStream | 14 MB (8 + 6) | - | - |
| D3D11Renderer | 5.4 MB (GATES); `D3D11Renderer.cpp:1982` 5.6 MB in CreateTerrain (terrain.md) | - | - |
| `cvLandmarkLibrary.cpp:475` | 3.9 MB | landmark art library | not by the bit (mod reload rebuilds it) |
| `granny_file.cpp` | 4 MB live in >= 1 MB blocks, 14 MB churn | unit Granny models | true Unit absence only |
| FDataField | 2.8 MB | in CreateTerrain | c3 |

CreateTerrain's blocks >= 1 MB total 21.6 MB on the huge save (`Terrain.h:79` 9.1, `D3D11Renderer.cpp:1982` 5.6,
FFastVector 4.1, FDataField 2.8), plus decal chunks of 0x7FFF8 bytes and render caches (terrain.md).

Smaller fixed allocations worth knowing: TerrainSystem's three dynamic buffers via 0x8BD0C0 (0x80000, **0x400000**,
0x80000); TerrainRenderer 0x29D1C bytes (`TerrainSystem.cpp:0x3c2`, site 0x5BCF1E); render batches 0x2080 bytes
(`D3D11Renderer.cpp:0xf82`); GameViewState 0x8D0 bytes (`Civ5App.cpp` 0xF1F at 0x4AFAAC and 0xF3E at 0x4AFFA9).

### The FluidFOW grid (the old "0x26 / 0x25" 2:1 pair)

The memory investigation had found a 118.75 MB block filled with 0x26 and a 59.38 MB block filled with 0x25 on a
128x80 map (164.53 / 82.27 MB on the 180x113 map of run 15). Owner, live-confirmed 2026-11-09: the FluidFOW particle
grid. Static detail (alloc-trace-README.md) [C]:
- Function 0x729C40, called once per game from the FluidFOW init 0x5B6AF0, itself called at **0x468EFD** in the
  per-game startup only when bit 15 (FluidFOW, 0x8000) of `[app+0x940]` is set.
- `cells = W x H`, 64-unit cells over the world box at 0xD90FCC..0xD90FDC, `W = (maxX-minX)/64 + 3`,
  `H = (maxY-minY)/64 + 23`. Allocations back to back: 0x729E0C line 718 aligned 128 `cells x 4096`; 0x729E4B line
  719 `cells x 128`; 0x729E8A line 723 aligned 128 `cells x 2048`; 0x729ECF line 724 `cells x 32`; 0x729F0B line 726
  `cells x 4`. All freed together at 0x70F790. The grid descriptor `[esi+0x904]` is allocated at 0x68F84C (same object
  as the 8 x 24 MB blocks).
- 118.75 MB / 4096 = 59.375 MB / 2048 = exactly 30,400 cells; 164.53 / 82.27 MB -> 42,120 cells.
- Open: the cell formula does not fit both maps cleanly (42,120 fits 180x113 as 260 x 162; 30,400 does not fit
  128x80); the 0x26/0x25 contents were not reproduced (both blocks ~90% zero right after the load; the old census was a
  late game, the content is simulation state); run 15 saw the 164 MB block "from turn 40".
- Ruled out as owners: the constant-size FFastVector pools at 0x590F00 (75 / 72.5 / 37.5 MB, Nexus terrain editor
  mode, `NexusTerrainEditor.cpp`, constructed at 0x57DFA0); `cvTerrainBlend` / `cvTerrainSampler` job arrays.

### The engine YieldIconManager block

2026-10-08 A/B (n=5 loads per build, ABBA, VP8P-HUGE_0350): with vaderkos' `exe-api-normalization` branch and
`Exe.TryDisableEngineYieldIconManager()` called from VP's YieldIconManager.lua, one single **61,149,008-byte** block
(owner `PreDLL (EXE side)`, head `00000004 1101000A ...`, usually starting just under 0x80000000) is absent
(100/100 snapshots with it, 0/100 without): -61.8 +-3.1 MB heap in use, claimed 4 GB -67 +-33 MB, below 2 GB no change.
The draw list is byte-identical either way: VP's Lua YieldIconManager draws the yield icons, the engine only fires
`Events.ShowHexYield`. What the block holds is not identified.

## 5. FluidFOW, measured

| Measurement | FluidFOW = 0 | FluidFOW = 1 | Delta |
|---|---|---|---|
| Main menu, EXE started directly, no mods, 2 min settle, `PrivateMemorySize64` (2026-11-09) | **1,269 MB** | **1,048 MB** | **-221** (8 x 24 MB prediction + overhead) |
| Huge save t350 in game, plain 5.4.8 DLL (PROBES.md) | 1,985 | 1,691 | -294 |
| Huge save, earlier hand-loaded run (memory note) | - | 1,707 | - |
| Huge save t350 in game, 32-bit pin `i0ship-26234371-ia`, **FOWLevel 3**, two launches each (3957, 2026-11-15) | 1,996.7 | 1,713.6 | **-283** (largest free +276) |
| The same at **FOWLevel 2** (stock config, FluidFOW = 0) against FluidFOW = 1 | 1,716.6 | 1,713.6 | **-3** (noise 13-26) |

*Added 2026-11-15 by issue 3957 (`research/05-graphics-composition/fog-of-war.md` 8.3) [L]: the rows above were taken
at the instances' FOWLevel 3 (the setting was not named then). FluidFOW's whole cost is a FOWLevel-3 cost: at FOWLevel 2
(mask, FOW CULL and cloud planes, no particles) it costs nothing measurable, so the per-thread blocks and the grid are
allocated only for the particles [L, I which allocation is skipped: not traced].*

At 8 job threads the per-thread blocks are 192 MB; on the huge save the grid adds 73.9 MB. Visible cost: the map
under the units is flat deep red, so FluidFOW = 1 alone is not a usable player setting (the terrain pass samples the
fluid-FOW output) [L].

## 6. Map-size constants

`MAX_SUPPORTED_MAP_X` / `MAX_SUPPORTED_MAP_Y` Defines (default 152 x 96; not set by the base DB or VP) size some
world buffers by the supported maximum, not the actual map: the GridHeightmap capacity +0x9C is set from them at app
Startup (0x476018, via 0x64C620); the consumer formula in 0x6D10D0 is undecoded [C call, I formula].

### 6.1 The largest map the EXE loads: about 20,000-21,800 plots (issue 9073, 2026-11-16) [L; cause I]

Measured 2026-11-16 on c5h-i and c5h-h (workstation): the stock DX11 EXE (`04cfdc71...`) with the 32-bit 43-civ DLL
`i0ship-26234371-m43-ia` (204fbfcc...), VP 5.4.8 + EUI, loading turn-0 saves written by the release-43 server
(Tectonic v6 copies from `tools/stressmap/tectonic_stress.py`, 42 majors + 20 city states, Huge):

| map | plots | stock load |
|---|---|---|
| 198 x 101 | 19,998 | loads (153 s), 902 MB free at turn 0 |
| 162 x 124 | 20,088 | loads (144 s), 873 MB free |
| 180 x 112 | 20,160 | loads (140 s), 938 MB free; played turns 0-3 and 250-260 |
| 187 x 117 | 21,879 | **fatal: Stack Overflow 0xC00000FD** at file offset `+0x00525d37` (VA 0x926937), 107 s into the load |
| 189 x 126 | 23,814 | the same, 91 s into the load |
| 225 x 141 | 31,725 | the same, 171 s into the load; 1,343 MB free, largest block 1,128 MB |

- **The limit is the plot count, not a width or a height**: 198 wide and 124 high each load at about 20,000 plots,
  187 x 117 does not. Somewhere between 20,160 and 21,879 plots the load overflows a thread's stack. Address space is
  not the cause (over 1.1 GB in one block at the crash).
- The stack in `crashes.log`: thread start in the EXE's runtime (`+0x5245ad`), d3d11 and the NVIDIA driver, then EXE
  `+0x8a351`, `+0x8a3b9`, `+0x1f2600`, `+0x8a42c`; the faulting address is in the runtime's code next to the thread
  start, which reads like a stack probe (`_chkstk`) for a large stack buffer [I]. A per-plot buffer of about 48-52
  bytes on a 1 MB default thread stack would end exactly in this range (1 MiB / 20,160 = 52 bytes; / 21,879 = 48
  bytes) [I, not decompiled]. Minidumps: `F:/c5-64bit-work/9071/stress/cal/{stock-t0-1.50-ac,stock-t0-1.35-ac,
  size-1.25}/crash/`.
- Not the `MAX_SUPPORTED_MAP_X/Y` defaults above (152 x 96 = 14,592): maps well past them load.
- Map scripts reach it: Tectonic v6's own largest choice (Huge at "Gigantic", 120 %, 180 x 112) is just under it;
  a size option past about 125 % on Huge, or a custom WorldBuilder map of that plot count, crashes the stock game in
  the load. Whether the increment 1 client (the same EXE, which still draws the world) has the same limit is not
  measured; a renderer in the server would not have it [I].

## 7. Address space: the facts the shell has to beat

From the memory investigation log (GameId 62, RunId 1789016075, turn 314, the peak sample of a run that died at
t317), [L]:

| | MB at t314 | t0 -> t314 |
|---|---|---|
| Committed | 3,514.7 | 1,945.4 -> 3,514.7, grows |
| Reserved, never committed | 510.6 | 491.9 -> 510.6, **flat** |
| Free | 70.5 in 713 holes, largest 4.0 MB | 1,658.6 -> 70.5 |
| Total | 4,095.8 | = `lpMaximumApplicationAddress - lpMinimumApplicationAddress` |

- **The process dies at 4,025 MB of address space out of 4,095.9**, not at 3.5 GB: 98.3% of every address it may use.
  WOW64 on x64 gives an LAA process the full 4 GB (4,095.9 MB in every sample of all 38 runs; no 2 GB kernel split).
  The run that died (GameId 2, t366): committed 3,496.5 + free 37.8 = 3,534.3 MB, i.e. 4,095.9 minus 561.6 reserved.
- Inside the 510.6 MB reserved: heap segment reserve 143.8 (heap index 10 alone reserves 72 MB to hold 14.3 MB busy);
  non-heap 366.9, flat from turn 0: ~73 thread stacks at 1 MB reserve each, a **128 MB ownerless reservation at
  0x10020000**, driver reserves, ~48 MB of the instruments.
- The CRT heap (index 4) owns 2,223.8 MB of address space at t314 (1,906.4 busy + 233.7 free list + 37.5 overhead +
  46.2 uncommitted). An NT heap has no fixed arena; it fails when no 64 KB-aligned contiguous run is left for a new
  segment. Heap slack inside committed: 332.6 MB at t314 (free list 280.6 + overhead 52.0).
- The process is **73.6% CRT heap** (measured with the 32-bit `vmmap.exe`; `vmmap64.exe` misclassifies a WOW64
  target).
- Owner split (09-15, from the memory index): DLL 5-9%, Lua 4%, driver ~9-12%, **EXE + rest 78%**, fixed from turn 0.
  The DLL held 295 MB at huge t344 (architecture note).
- The sub-2 GB half is saturated from turn 0 (~24 MB free, 0.81 MB largest hole; "LAA is load-bearing"); Gate 1
  measured 26-31 MB free below 2 GB in both the all-on and the B4 configs, so config savings land above 2 GB.
- Textures live in VRAM; their system-RAM cost is ~62 MB, about 2.4% of commit.
- Gate 1 baseline (instrumented DLL, huge save): main menu 1,615 MB private / 1,583 free / 1,541 largest free / 31
  below 2 GB; in play t350 2,122 / 1,094 / 1,054 / 26; t353 2,211 / 1,001 / 901 / 26. B4 at t353: 1,757 MB private,
  1,281 MB largest free, ~1,480 MB free.
- Load-time OOM path seen once (2026-09-21): the EXE copied graphics data (`movntdq`, logged as EXE+0x4ba4da) into a
  NULL buffer after a ~110 KB (0x1B070) allocation failed, 52 MB free, largest block 1.26 MB - the second load of a
  late save in one process starts from the first load's fragmented address space.

## 8. What leaves the EXE in a shell

Config only (B4): ~440-454 MB, two thirds FluidFOW. With Landmark/River/Arrows as well: -497 MB vs all-on
(instrumented). Still resident: Terrain (bake + heightmap, ~22 MB of large blocks), Unit (Granny models; true absence
needs `ReloadUnitSystem = 0`), Landmark app-level parts rebuilt by `ReloadLandmarkSystem`, LeaderHead, Audio, UI
(UIManager 24 MB, CommandStream 14 MB) and the DLL itself if it stays in-process (~300 MB at t344 would leave with
the full architecture). The 78% EXE share is mostly not world-system memory that config can reach; see
`../04-memory-investigation/` for the full ledger.
