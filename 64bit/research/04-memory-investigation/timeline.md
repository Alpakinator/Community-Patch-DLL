# Timeline of the memory investigation

Numbered items are the log's own chronology (section 2 of `civ5-memory-investigation-log.md`); the
date is when the item happened. Runs 1-5 are dated only as "between 2026-09-03 and 2026-09-05" in the
sources. GameIds are explained in instruments-and-tooling.md.

## 2026-09-03 to 2026-09-05 - finding the leak

| # | date | what | conclusion |
|---|---|---|---|
| 1 | 2026-09-03 | The owner proposes a 64-bit proxy-process facade | Judged blocked by closed 32-bit static libraries; measure first. (Superseded 2026-11-09, see the end.) |
| 2 | 09-03..05 | `MemoryDiagnostics` built (address-space walk, heap walk, replay census), branch `memory-diagnostics` | The per-turn instrument everything else rests on |
| 3 | 09-03..05 | Run 1, standard map, 188 turns (GameId 1) | No fragmentation, nowhere near the wall; later the control run |
| 4 | 09-03..05 | Run 2, same game to turn 333 | Same verdict; ~1.06 MB/turn |
| 5 | 09-03..05 | Run 3, huge map, 14 majors, crashed t235 (GameId 2) | First real failure. Diagnosed then as "exhaustion, fragmentation as executioner" - **wrong for this run** (it was the leak; retracted 2026-09-06) |
| 6 | 09-03..05 | Diagnostics extended (fine size classes, top-96 blocks, census, entity counts, every turn) | |
| 7 | 09-03..05 | Run 4, reload of t235 played to t366 | The reload returned 1536.6 MB: **it is a leak**; overturned run 3's framing |
| 8 | 2026-09-05 | Block-level tracking added (`MemBlockSizes`, `MemNewBlocks`, `MemBlockWindow`, `MemAnchors`) | |
| 9 | 2026-09-05 | Run 5, reload of t356 to t390, game finished | Block fingerprint `0x2C302C30` ("0,0,") named `CvPlayer::GetBestSettlePlot` CitySites logging |
| 10 | 2026-09-05/06 | `vp-dll-dev` skill built | Runs driven autonomously from here on |

## 2026-09-06 - proving it, and that there is no second leak

| # | what | conclusion |
|---|---|---|
| 11 | Runs 6/7, AILog A/B, 30 turns each from one save (GameId 25) | 152 retained blocks vs 159 files written: mechanism confirmed. Side finding: AI logging changes AI decisions |
| 12 | Run 8, load-baseline at t46 | 2.6 MB committed of non-serialized residue over 30 turns |
| 13 | Run 9, 200 turns fresh, AILog=0 (GameId 40, 99 min) | Growth linear, not superlinear: heap busy ~0.49 MB/turn in all four 50-turn phases |
| 14 | Run 10, load-baseline at t200 | 15.8 MB live-heap residue vs 13.5 MB after 30 turns: a fixed offset, **no second leak**. The earlier "870 MB unexplained, factor of 76" claim retracted |

## 2026-09-07 - the fix and the first budget

| # | what | conclusion |
|---|---|---|
| 15 | Run 11, fix verified (GameId 47, 224 turns, huge map, AILog=0) | Commit **a26506ae8** (`DeleteLog` after `Close` at four sites): heap 4.05 -> 0.63 MB/turn, reload gap 1170.7 -> 14.7 MB |
| 16 | Run 12, census expanded by 27 rows; `MemHeapDetail`, `MemTopBlockSamples` added | Analytic census names only 40 of 1700 MB (2.4%) and structurally cannot do better; block contents show 32% of the heap is 15 blocks >16 MB allocated before turn 1 (engine, not DLL) |

## 2026-09-08 - attribution

| # | what | conclusion |
|---|---|---|
| 17 | Run 13, UMDH/gflags | Hard negative: ust active, every block reads `BackTrace0` (LFH). Page heap crashed the game. Registry reverted. **Do not reopen** |
| 18 | Run 14, the DLL's global `operator new` hook (`MemoryHooks`), GameId 47 t240-250 | DLL = **8.9% of heap bytes, 39.8% of blocks** (151.8 of 1710.1 MB); first-played-turn spike 93% not DLL |

## 2026-09-09 to 2026-09-11 - outside views

| date | what | conclusion |
|---|---|---|
| 09-09 | External route: `TurnByTurn` mutex freeze, 1 Hz `VirtualQueryEx` timeline, VMMap, Scroll Lock marker | Engine-side experiments need no DLL change |
| 09-10 | Item 19: Run 15, GameId 62 (180x113 = 20,340 plots) crashed t317, reloaded to a t344 victory | Exhaustion with fragmentation as executioner, correct **here**: growth 2.476 MB/turn predicts t~346, fragmentation cost ~29 turns. DLL = 99.9% of block growth; one DangerPlots site 71%. Hook node pool saturated at t259 |
| 09-10 | UI settings via VMMap (yield icons, zoom) | No UI state moves the 32-bit address space (Image/Mapped +0.00 MB) |
| 09-10 | Two crashes read from `crashes.log` | Sub-2GB largest block ~1-1.6 MB at both; framed as "the wall is sub-2GB" (sharpened 09-15: it is *always* at that level, a fixed toll) |
| 09-10 | 23:39 minidump | EXE access violation through an unmapped pointer, zero DLL or Lua frames; NVIDIA driver images unloaded/reloaded 65 times |
| 09-11 | Textures | Pixel data lives in VRAM (1368 MB dedicated); ~62 MB system RAM; the "texture block" was a 4 MB file-read buffer |
| 09-11 | vmmap64 vs vmmap32 | vmmap64 misreports the WOW64 heap as Private Data; the process is **73.6% heap** |
| 09-11 | Lua three-tier profiler | Lua ~83 MB on a small map; two UI lines hold ~75% of it |

## 2026-09-14 to 2026-09-17 - ledger, ceiling, myths

| # | date | what | conclusion |
|---|---|---|---|
| 20 | 09-14 | Coverage audit (no run) | Line-of-code coverage 7.3% / 10.1%; heap index 12 is Lua's heap; the sub-2GB half is the least attributed part |
| 21 | 09-15 | Owner donuts + sub-2GB placement from the 09-10 VMMap capture | DLL 5.3% / 9.0%, EXE+rest ~78%; 128 MB unowned reservation at 0x10020000 |
| 22 | 09-15 | The ceiling question | Dies at 4025 of 4095.9 MB; reserved 510.6 MB, flat. **Corrects 09-10:** `LargestFreeLowKB` is flat at 1.06 MB from turn 0, it does not run down |
| 23 | 09-15 | Reserved memory decomposed and added to the ledger report as a slice | |
| 24 | 09-15 | `MemoryImports` (import-slot patching + ownership census) | CRT heap 99.85% attributed; EXE 927 MB, DLL ~85 MB; std::string bodies 1.0 MB |
| 25 | 09-16/17 | Memory Myths 1-2: yield icons, tech tree (GameId 64); on-demand snapshots and the Lua channel built | Both false for survival. The first live run crashed because the Lua channel created Lua threads at run time - never do that |
| 26 | 09-17 | Myth 3, fragmentation with doses | Churn fragments nothing; **fragmentation is peak-driven** (one AI turn +27.19 MB of free list). `MemSnapHeapFree` added. Corrects the 09-10 "tech tree -91.9 MB" record |

## 2026-09-19 to 2026-10-08 - OOM saves, myths with intervals, the render buffer

| # | date | what | conclusion |
|---|---|---|---|
| 27 | 09-19..21 | OOM test saves for GameId 88 (20,340 plots, 20 majors, observer mode) | Died of `DXGI_ERROR_DEVICE_REMOVED` 23 and 30 turns after loading t249/t250 (in turns 272 and 280). Crashes while loading late saves = the EXE's 4 MB message-queue overflow (not OOM, not the DLL). `EngineQueueGuard` built 09-21; branch `engine-queue-guard` 09-22 |
| 28 | 09-23/24 | Mods and graphics settings, n=2 per arm (GameId 118, `VP8P-HUGE_0350`) | Claimed below 2 GB 2,019.6-2,022.5 MB in every arm; min video quality -301 MB committed, all above 2 GB. "Nine Memory Myths" page |
| 29 | 09-28/29 | Real 95% intervals, leader screens, zoom; the n=5 batch (19/19 runs) and the myth 3/9 re-test (5/5) | No myth moves claimed below 2 GB; a leader screen's first visit +209.4 MB VRAM; ASLR gives ~±2.5 MB launch-to-launch noise below 2 GB |
| 30 | 09-29/30 | Myth 10, units on screen | Every load crashed at 400-450 Machine Guns in view: an EXE 1 MB render draw-list overflow, not memory (818-936 MB free above 2 GB) |
| 30 | 09-30 pm | Render-buffer follow-up | The records are EUI unit flags (13.2 per unit); with flags hidden 529 units survive; SUG no help; a second, rarer crash at 400 |
| 30 | 09-30 eve | Promotion flags | 240 B per promotion icon per unit in view |
| 31 | 10-01 | What the queue guard drops | 39,237 records (7,020,544 B) of terrain decals (farm fields, route curves); 793 of 1,316 farms load without fields |
| 30 | 10-06 | Buffer decoded from dumps + live capture | The 2D UI's per-frame draw list; 8 sprites per unit; tech tree +280,800 B |
| 30 | 10-06 eve | Window benchmark | InfoAddict Historical graph +498,880 B; Trade Route Overview scrolls safely |
| 30 | 10-07 | Yield icons by manager | **Correction:** VP's Lua YieldIconManager draws them, not the engine (the 10-06 line is withdrawn) |
| 30 | 10-08 | Engine C++ YieldIconManager unhooked, n=5 ABBA | Draw list identical; one 61.1 MB PreDLL block absent (-61.8 ±3.1 MB heap); below 2 GB unchanged |

## 2026-11-09 - follow-ups that close or revise earlier items

| what | conclusion |
|---|---|
| 64-bit architecture reassessed (thin-client study) | Supersedes the 2026-09-03 verdict: closed libs are 338 symbols; feasible, payoff capped by the EXE's 78%; 425-750 agent-hours |
| EXE `[DisableSystems]` probes on the huge save | 17 keys together save ~440 MB (FluidFOW alone -294); the 8 x 24 MB float blocks are FluidFOW cloud particles (not "instanced transforms", the 09-07 reading) |
| Shell gates (GATES.md) | Owner of the 0x26/0x25 2:1 heap pair named as the FluidFOW particle grid (`FluidParticles.cpp:718/723`); both gates GO WITH CONDITIONS |

Sources: `research/sources/memory-notes/civ5-memory-investigation-log.md` (sections 1-3), `civ5-memory-leak-findings.md`, `civ5-citysites-log-leak.md`, `civ5-umdh-tooling.md`, `n5-run-plan.md`, `units-myth-plan.md`, `stats-db-game-ids.md`, `civ5-64bit-architecture-options.md`, `civ5-exe-disable-systems.md`, `civ5-shell-gates.md`; `research/sources/shell-gates/GATES.md`.
