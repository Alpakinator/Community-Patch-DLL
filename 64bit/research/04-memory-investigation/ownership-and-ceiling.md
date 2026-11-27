# Ownership and the ceiling

Where the 4 GB address space goes, who owns it, and how much of it anyone can name. All figures are
measured unless marked "est".

## 1. The ceiling: 4025 MB of 4095.9, not "3.5 GB"

`CivilizationV_DX11.exe` is large-address-aware (PE characteristics `0x0122`, `IMAGE_FILE_LARGE_ADDRESS_AWARE`
set). Under WOW64 on x64 an LAA process gets the full 4 GB: `Committed + Reserved + Free` = **4095.9 MB in
every sample of every run** (the missing ~0.1 MB is the 64 KB null guard plus a top guard).

Reference point: GameId 62, RunId 1789016075, turn 314 (peak committed of the run that died at t317,
20,340-plot map), measured 2026-09-15:

| | MB at t314 | t0 -> t314 |
|---|---|---|
| Committed | 3514.7 | 1945.4 -> 3514.7, grows |
| Reserved, never committed | 510.6 | 491.9 -> 510.6, **flat** |
| Free | 70.5, in **713** holes, largest **4.0 MB** | 1658.6 -> 70.5, consumed |
| total | 4095.8 | = `lpMaximumApplicationAddress - lpMinimumApplicationAddress` |

The process used **98.3% of every address it was allowed** (it dies at ~4025 MB of address space). There is no
missing headroom; the "3.5 GB" seen in Task Manager is only the committed part.

Earlier ceiling figures, for reconciliation: 3534.3 MB committed+free at GameId 2's t366 death (4095.9 minus
561.6 reserved, 2026-09-07, pre-fix); ~3575 MB used for run 15's death-turn prediction (4096 minus 521 MB
reserved). Reserved ranges 460-563 MB across runs; it is a flat tax present from turn 0 in all of them.

### What is inside the 510.6 MB reserved

| inside the 510.6 MB reserved (t314) | MB | note |
|---|---|---|
| heap segment reserve | 143.8 | 131.1 at t0, flat |
| ...of which heap index 10 alone | 72.0 | reserves 72 MB to hold 14.3 MB busy |
| everything non-heap | 366.9 | 360.8 at t0, **flat from turn 0** |

Decomposed at the two ledger points (2026-09-15; heap reserve from `MemHeapDetail.UncommittedKB`, DLL reserves
from source, the rest from the 2026-09-10 VMMap snap-02 read as size minus committed):

| | small t7 (GameId 77) | huge t344 (GameId 62) | |
|---|---|---|---|
| heap segment reserve | 120.9 | 142.5 | CRT 11.9 / 38.5, **heap 10 72.9 / 73.2**, others 36.1 / 30.8 |
| DLL minidump reserve | 16.0 | 16.0 | exact; release-build `MINIDUMP_EMERGENCY_RESERVE_BYTES` (64 MB in debug) |
| DLL hook node pool, unused | 26.2 | 1.2 | shrinks as the pool fills |
| unowned 128 MB reservation | 127.4 | 127.4 | est, snap-02 |
| thread stacks | 42.3 | 42.3 | est, 146 regions, 48.6 MB reserved, 6.4 committed |
| sections + image tails | 23.5 | 23.5 | est |
| driver write-combined buffers | 30.4 | 30.4 | est |
| **unattributed residual** | **114.2** | **180.3** | |
| total `ReservedKB` | 500.9 | 563.6 | |

Hypotheses answered: thread stacks are real but minor (~70 MB reserved, flat). "The CRT got too small an arena"
is the opposite of the truth: at t314 the CRT heap (index 4) owns **2223.8 MB** of address space (1906.4 busy +
233.7 free-list + 37.5 overhead + 46.2 uncommitted). An NT heap has no fixed arena; it fails only when there is
no 64 KB-aligned contiguous run left for a new segment.

**Charging rule (the owner, 2026-09-15):** reserve with a known owner is charged to that owner (minidump reserve and
node pool to the DLL, write-combined reserve to the driver), never to the generic reserved slice.

### Free but shattered: 70.5 MB

At death 70.5 MB was free in 713 holes, largest 3.97 MB, nothing above 4 MB in the histogram. Of the free
space, 42.1 MB sat in holes <= 1 MB - too small even for the 1.27 MB DangerPlots arrays. At t344 539 of 624
holes are <= 64 KB (17.9 MB, mean 34 KB): `VirtualAlloc` reserves on a 64 KB boundary, so those are dead for the
life of the process, and they were already there at turn 0.

The dust barely grew: 25.6 MB in holes <= 1 MB at turn 0, 42.1 MB at turn 317 (only ~16 MB of small-hole
fragmentation in 317 turns). What collapsed was the single 1622 MB hole of turn 0, eaten by committed growth.
**Growth spent the large holes; fragmentation only decided the leftovers were worthless.**

### Heap slack: 332.6 MB, the only pool that grows

Inside committed at t314: free-list 280.6 + overhead 52.0 = **332.6 MB** (against 61.5 + 16.8 at t0); live data
2248.1 of 3514.7. About **8% of the whole address space is memory the allocator holds and the program does not
use** - and unlike the reserved tax, it is attackable (see leaks-and-growth.md, "free-list tax").

### A cheap death predictor

A single **164.53 MB** CRT-heap block exists from turn 40 onward in GameId 62 (body a repeated `0x26` fill; the
next two are 88.16 MB of zeros and 82.27 MB of `0x25` fill). `LargestFreeKB` fell below 164.5 MB around turn
160 and never recovered, so from midgame that block could never be reallocated larger. Not proven to be the
failing call (no minidump), but logging `LargestFreeKB` against `max(MemTopBlocks.Bytes)` per turn would have
crossed 157 turns before death. Not built.

## 2. The sub-2GB half: saturated from turn 0 (a fixed toll)

**Correction recorded 2026-09-15:** the 2026-09-10 reading that "the wall is sub-2GB exhaustion" and that the
low half runs down was wrong in its dynamics. `LargestFreeLowKB` is **1.06 MB at turn 0 and 1.06 MB at turn
317** in GameId 62, with `CommittedLowKB` pinned at 1596-1657 MB throughout. The low half is a fixed toll paid
before the first turn; it explains why only ~2.4 GB of the 4 GB is available for growth, but the variable that
moves toward death is the high half.

Measured states:
- Throughout a game: committed ~1660 MB, reserved ~365 MB, 15-25 MB free, largest hole 0.5-3.0 MB.
- Small map t7: committed 1615.6, reserved 395.9, free 36.5, largest hole 8.9 MB. Between the two ledger points low
  committed rose 25 MB while total committed rose 897 MB.
- At the 2026-09-10 crashes: 1640 / 1634 MB committed, 24.2 / 26.1 MB free, largest 1.06 / 1.62 MB.
- The mods/graphics runs (GameId 118): claimed below 2 GB flat at ~2,022 MB from turn 0 to 350, 2,019.6-2,022.5 MB
  in every arm.

Consequence: any code that is not LAA-clean (pointer in a signed int, pointer compare with `<`) has zero headroom
from turn 1. "LAA is load-bearing": without the 4 GB patch the game would not run.

### Placed once: the 2026-09-10 VMMap capture (snap-02, PID 31892, turn-390 game, 2486.6 MB committed)

VMMap "Heap ID" = stats.db HeapIndex + 1.

| below 2GB, snap-02 | committed MB | reserved MB |
|---|---|---|
| CRT heap (ID 5) | 805.8 of 1396.2 | heaps together 96.3 |
| process heap (ID 1) | 112.7 of 177.7 | |
| engine pool (ID 2) | 60.8, all | |
| Lua (ID 13) | 50.9 of 109.0 | |
| heap ID 11 + small | 18.8 | |
| images | 455.9 | 2.7 |
| private data | 102.3 of 206.1 | **219.4** |
| shareable + mapped files | 51.5 | 19.8 |
| thread stacks | 6.4 | 42.3 |
| **total** | **1665.1** | **380.4** |

- **The 128 MB unowned reservation:** private, 0.6 MB committed, at 0x10020000 on 2026-09-10 (just above the DLL's
  preferred ImageBase 0x10000000) and at **0x1A020000** on 2026-09-17 - same size, different base. A third of the
  low-half reserve. Naming it needs a `VirtualAlloc` breakpoint with a 128 MB size condition at startup. **Open.**
- **The DLL takes 24-56 MB of the low half itself:** the 16 MB minidump reserve (found at 0x42da0000) and the hook's
  8 MB bucket table (0x39dd0000); its 32 MB node pool could not be pinned down. Since 2026-09-15 the instrument's
  tables are allocated `MEM_TOP_DOWN` so they no longer sit below 2 GB.
- Images below 2 GB (from the 09-10 23:39 dump module list): 162 modules, 459.0 MB, **456.4 MB below 2 GB**. NVIDIA
  user-mode driver 209.0, EXE 101.1 (its `.data` alone 86 MB virtual; loaded at 0x00160000, not 0x400000), Windows
  and other 92.5 (incl. ~7 MB Firaxis CvGameDatabase/CvLocalization), Steam 24.5, DLL 20.8, D3D/DXGI 8.3, VC
  runtimes 2.5. **The driver is ~13% of everything committed below 2 GB.**
- Calibration caveat (2026-09-15): stats.db "private non-heap" ran ~100 MB above VMMap; did **not** reproduce on
  2026-09-17 (5.7 MB).

## 3. Owner split (donuts, 2026-09-15)

Committed memory by owner; the sum closes to committed within 0.02 MB.

| owner | small t7 (GameId 77) | huge t344 (GameId 62, 20,340 plots) |
|---|---|---|
| DLL | 127.5 MB, 5.3% | 295.3 MB, 9.0% |
| Lua | 99.6 MB, 4.2% | 141.2 MB, 4.3% |
| video driver (process memory, not VRAM) | ~298.7 MB, 12.5% | ~298.7 MB, 9.1% |
| EXE and everything else | 1871.5 MB, 78.1% | 2559.1 MB, 77.7% |

- **DLL** = hook tags (database 60.0 both, fixed; danger plots 7.5 -> 60.4; loaded-save data 0 -> 43.8; untagged 15.8
  -> 39.0; AI 3.3 -> 21.6; turn/pathfinder 0.7 -> 11.0; map gen 5.7 -> 0) + image 20.8 + hook side table 13.8 / 38.8.
  Without the instrument: 113.7 / 256.5 MB.
- **Lua** = heap 12 busy + heap 12 slack (18.1 / 45.3 MB).
- **Video driver** = NVIDIA images 209.0 + D3D/DXGI images 8.1 + GPU-mapped write-combine private ~65.6 (est, it
  moves: 44.2 in another snapshot) + NVIDIA shader cache `.nvph` 16.0 (est). Identical in both donuts because nothing
  measured them at either point.
- **EXE and everything else** = EXE image 101.1, engine blocks >= 1 MB (643.9 / 996.3), engine blocks < 1 MB (442.5 /
  614.9), CRT heap slack (38.0 / 188.5, shared with the DLL, unsplittable), engine pool heap 61, Windows process heap
  incl. slack (165.4 / 178.6), other images (123.2 / 119.5), unnamed private (~224.5 / ~228.3), small heaps + shared
  sections + stacks + OS files (~71).

Whole-4 GB pie at a third point (GameId 64, t317, 2026-09-17, 4095.9 MB): DLL 338.8 (8.3%, ~128 MB of it the
diagnostics instrument), Lua 109.5 (2.7%), driver 327.0 (8.0%), EXE+rest 2018.5 (49.3%), reserved 419.0 (10.2%),
headroom 883.0 (21.6%; 29.8 MB below 2 GB, largest hole there 5.56 MB).

Other DLL-share figures and their denominators (do not mix them): **8.9% of heap bytes / 39.8% of heap blocks**
(GameId 47 t250, 151.8 of 1710.1 MB, hook, 2026-09-08); 6.27% of heap at small t7 (95,154 KB of 1,518,251 KB);
6.3% of heap bytes / 17.8% of blocks at small t7 vs **10.8% / 47.7%** at 20k t344, where the DLL owns 67% of the CRT
heap's blocks (1.73 M of 2.59 M). The DLL's weight grows with scale in blocks far more than bytes.

## 4. Coverage audit: how much can be named (2026-09-14)

Committed memory by level of knowledge (GameId 77 t7 small map; GameId 62 RunId 1789075474 t344):

| level | small t7 (2397 MB) | 20k t344 (3294 MB) | what is in it |
|---|---|---|---|
| line of code | 174.5 MB, **7.3%** | 331.7 MB, **10.1%** | DLL (hook) + Lua (heap 12) |
| owner | 617.1 MB, 25.7% | 814.2 MB, 24.7% | images, mapped files, heap free-list + overhead, hook side table |
| contents | 704.5 MB, 29.4% | 1056.9 MB, 32.1% | engine blocks >= 1 MB in the CRT heap, engine pool heap |
| size only | 901.3 MB, 37.6% | 1091.6 MB, **33.1%** | engine blocks < 1 MB (442.5 / 614.9), process heap (148.2 / 160.5), private non-heap (297.6 / 301.4), small heaps |

Plus reserved 500.9 / 563.6 MB (only heap uncommitted 120.9 / 142.5 itemised) and free 1197.6 / 238.0. So "line of
code" covers 7-10% and "size only" about a third. The sub-2GB half is the least attributed part: only images (~456 MB)
are placed there per run.

Stable heap layout in every run: **4** = CRT heap (DLL + engine, role 6), **0** = process default heap, **12** = Lua,
**1** = engine pool (one ~58 MB structured block, ~700 blocks), **10** = unknown (57k small blocks, ~72 MB reserved),
nine more under 1 MB together.

*Added 2026-11-15 (issue 4204, `research/08-performance/metrics-ledger.md` section 5):* these indexes are the DLL's
`GetProcessHeaps` order inside the process and cannot be read from outside on the workstation's Windows 11 build: the
EXE's 32-bit PEB lists only the process heap. A walk from outside finds 13 NT heaps by their segment headers and names
them by fingerprint (CRT the largest, "heap 10" 21.6 MB committed / 77.5 MB reserved, Lua 100-108 MB in game on the
128 x 80 save, pin `i0ship-26234371-ia`) `[L]`. No heap of the engine pool's shape (~60 MB, one large block) exists in
those processes `[L]`.

DLL blind spots of the hook found in the audit: `CvDllGameContext` has its own `HeapCreate` heap (CvDllContext.cpp:231)
used by every `CvDll*` wrapper's class `operator new` (one of the small heaps, < 15 MB together); the crash handler's
16 MB reserve (CvGlobals.cpp:2109).

## 5. The CRT heap is 99.85% attributed (MemoryImports, 2026-09-15)

GameId 80 turn 7, CRT heap 1169.6 MB, walk-verified:

| owner | MB | what it is |
|---|---|---|
| CivilizationV_DX11.exe | **927.0** | 561.3 in blocks > 1 MB, 365.6 under |
| PreDLL (EXE side) | 155.3 | live before the DLL loaded (EXE-side by construction) |
| DLL via operator new | 83.3 | what MemoryHooks already reported |
| Unknown | **1.7** | 0.15% |
| MSVCP90 (all std::string bodies) | 1.0 | |
| CvGameCore_Expansion2.dll direct | 0.8 | its own malloc/_aligned_malloc |
| CvGameDatabase / CvLocalization | 0.4 | |

- **The 442 MB of "engine blocks under 1 MB" is the EXE's.** The DLL's entire CRT footprint is **~85 MB** (83.3 +
  0.8 + 1.0 + 0.4).
- **std::string is not the hidden mass:** 1.0 MB live against 110 MB of churn. The earlier claim that strings were
  the largest thing the hook could not see was wrong by two orders of magnitude.
- Outside the CRT heap (Lua heap, process heap, engine pool), 151.4 MB "Unknown" is correct: those never touch a CRT import.
- The EXE churns 12.2 GB through `_aligned_malloc` by turn 7 (D3D-shaped), invisible to every earlier instrument.
- The engine-blocks-under-1 MB mass is a load-time fixed cost, not growth: G77 t1->t7 -0.17 MB/turn; G62 t318->t344
  +0.28 MB/turn with blocks falling ~750/turn. Splitting it moves the floor, not the death turn.

### The big engine blocks (2026-09-07, GameId 47 t228; heap busy 1699.9 MB in 2,319,907 blocks)

32% of the heap is 15 blocks > 16 MB, all allocated before turn 1 (475.8 MB in 13 blocks at t0; 548.2 MB in 15 at t224).
Of the 817 MB in >= 1 MB blocks, the DLL owns 5.2 MB.

| block | contents | current reading |
|---|---|---|
| 8 x 24.00 MiB | floats `-620, -468, 11.88, 0, 0, 1.324` | **FluidFOW cloud particles**, one `new(0x1800000)` per job thread (`MaxSimultaneousThreads = 8`), map-independent (2026-11-09; the 09-07 "instanced transform buffers" reading is superseded) |
| 118.75 MB | byte 0x26 repeated | attributed 2026-11-09 to the FluidFOW particle grid (see myths-and-measurements.md for the caveats) |
| 59.38 MB | byte 0x25 repeated | same pair, 2:1 |
| 57.69 MB | structured, own heap (~700 blocks) | engine pool (heap index 1) |
| 48.24 / 32.01 / 24.12 MB | all zeros | reserved, never filled |
| 16.00 MB | normalised floats | colour/UV/quaternion-like |
| several 4 MB | ASCII `CivilzationV_Logo.dds`, `Container` | a file-read staging buffer (zeros after the name), **not** texture data |

Blocks <= 1 KB are 94.9% of all blocks and 9.2% of bytes; the mass is the 4 KB-1 MB middle (~731 MB).

## 6. Lua has its own heap (82-99 MB; two lines hold ~75%)

- **Heap index 12 is Lua's dedicated Win32 heap** (VMMap Heap ID 13). Its `BusyKB` equals the Tier 1 `LiveKB` within
  11 KB every turn (GameIds 74, 76, 77), so Lua's size is readable from every run since 2026-09-07 (inferred from the
  identical layout for runs without Tier 1).
- Size: 81.6-83.5 MB small map t7; 82.9 MB on GameId 47 t228 (4.9% of a 1699.9 MB heap); **92.8 / 99.2 / 95.9 MB** on the
  20k map at t255 / t317 / t344 (blocks 381k -> 647k). **Correction 2026-09-14:** Lua is *not* map-independent as claimed
  on 09-11; its share still falls (5.5% -> 4.4%). 49.6 MB right after a load; the first played turn adds ~33 MB; 41.7 MB
  exists before `InitLuaFramework`. Heap 12 slack 18.1 MB at t7, 45.3 MB at G62 t344.
- **Two lines hold three quarters** (Tier 2, GameId 77 t7, complete attribution):

| site | live | share of Lua | what it is |
|---|---|---|---|
| `VPUI_core.lua:71` | 38.8 MB | ~52% | `local v = t[i]` in `GameInfoCache`'s iterator; the engine's C binding materialises each DB row |
| `EUI_core_library.lua:418` | 16.4-17.2 MB | ~22-23% | `cache[k] = v` in a `__index` memo: a fresh table per DB row, filed under both ID and Type, never evicted |

  Both are the game database copied into Lua tables by two caching layers unaware of each other (~55 MB of memoised XML).
  Not per-context duplication: across 129 / 477 / 553 sites in three chunks no line appears twice (one `Proto` per chunk
  process-wide, 69 contexts). Lua does not drive the 256-512 KB class. Churn (`Communitu_79a.lua` 351 MB in map gen,
  `InfoTooltipInclude.lua:1609` 658k allocations for 1.3 MB) is a CPU finding, not a memory one.

## 7. Textures live in VRAM (2026-09-11)

- A DDS-header content scan of all 2640 MB committed found **0.1 MB** of DDS-form data (61 of 62 hits false positives;
  scanner validated 460/460 against real files). None of the 28 Mapped File regions is a game asset (23.0 MB of locale
  tables, .mui stubs and an NVIDIA shader cache) despite 1290 MB of `.dds` and 2264 MB of `.fpk` on disk.
- GPU counters for the process: dedicated (VRAM) 1368.2 MB, shared (system RAM for GPU) 61.6 MB, total committed 1818.0.
  So textures cost ~62 MB of system RAM, ~2.4% of commit; VMMap shows 50.9 MB with Read/Write/WriteCombine.
- Texture names are resident and cheap: 37,549 `.dds` references, 11,383 distinct, 1.38 MB; UI XML 1.7 MB; Lua source 0.8 MB.
- The GPU has 16 GB and the game uses ~1.37 GB: VRAM is not a constraint. Track `Dedicated` + `Shared`, not `Local`/`Non Local`.

## 8. The 73.6% figure

Re-taken with the 32-bit `vmmap.exe` (2026-09-11): Heap **1898.5 MB** of 2579.1 MB total = **73.6%**; Private Data
166.8, Image 454.1. `vmmap64.exe` had reported Heap 0.4 / Private Data 1962.7 and inverted the conclusion. Arguments
that "the interesting memory is driver-side and unreachable" were an artifact of that bug. (VMMap's Heap category
covers every heap, not only the CRT heap; the sources call it "CRT heap"; on 2026-09-07 the shared CRT heap held 1391.6 MB, 81.9% of the process heap.)

Sources: `research/sources/memory-notes/civ5-memory-investigation-log.md` (section 1; section 3: "Where the ~3530 MB
process ceiling comes from", "The DLL owns 8.9%", "The big memory belongs to the renderer", "Where the textures actually
live", "vmmap64.exe silently mis-classifies", "Lua is a third...", "Coverage audit", "Owner donuts", "Why the ceiling is
~3.5 GB", "The 'engine blocks under 1 MB' are the EXE", "Memory Myths" pie), `civ5-lua-memory-profiling.md`,
`civ5-alloc-hook-instrument.md`, `civ5-exe-disable-systems.md`; `research/sources/shell-gates/GATES.md`.
