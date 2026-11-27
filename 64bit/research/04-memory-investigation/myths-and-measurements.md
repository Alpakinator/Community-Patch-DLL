# Myths and measurements

Player folklore about what eats memory, tested with the investigation's instruments, plus the OOM test
saves and the resolution of the 2:1 heap-block pair. **Headline: no UI setting or screen moves the
sub-2GB half, and only one (min video quality) moves committed memory by hundreds of MB - all of it
above 2 GB.** The "Nine Memory Myths" page (https://claude.ai/artifact/RbRE5QVfBzzK9L49jZnuNL, v9-v12)
carries the published version; the myth numbers below follow the log.

## 1. First pass: UI settings vs address space (2026-09-10, VMMap)

Turn-390 game, 2487 MB committed, 8 VMMap snapshots + 1 Hz timeline over 6 minutes; yields on/off, zoom out and back.
Image / Mapped File / Shareable **+0.00 MB** in every state; Heap +8.9 and Private +8.0 MB monotonic (noise: private
wanders 12-25 MB with no input, sd ~4 MB); largest free hole unchanged at 1055.9 MB. Zooming fully out mapped no new asset
file. Caveats: warm process; GPU memory invisible from inside. (The same session's "tech tree moved the largest hole
-91.9 MB" record is corrected below.)

## 2. Myths 1-2: yield icons and the tech tree (2026-09-17, GameId 64)

Save `Dido_0316` (128x80, 14 majors, game turn 316), RunId 1789622091, Lua-driven protocol, 30 s settle, 2 DLL snapshots per state.

| | yield icons (4 toggles, net) | tech tree (3 visits, 1 scrolled, net) | one played AI turn |
|---|---|---|---|
| address space claimed | **0.000 MB** (exact) | **+15.81 MB** - one new 16 MB Windows process-heap segment, above 2 GB, made during the scroll | +133.4 MB |
| largest free block below 2 GB | 5.56 -> 5.56 MB | 5.56 -> 5.56 MB | unchanged |
| committed excl. driver buffers | +0.31 MB (floor 0.80) | +1.68 MB (floor 0.97) | +93.9 MB |
| heap blocks kept | +105 (noise) | **+5,729** (+5,476 / 1.14 MB Unknown in the process heap; DLL 0, CRT 0) | +147,217 (DLL 90%) |
| VRAM dedicated | -0.01 MB | +0.01 MB | +150 MB |

- **Yield icons: false.** Nothing retained; each toggle wakes ~13-17 UI scripts (~0.25 MB Lua churn).
- **Tech tree: mostly false.** Cold open keeps ~800 process-heap blocks (0.17 MB), a warm reopen nothing, a scroll ~4,600 blocks
  (0.95 MB) and triggers the 16 MB segment - a threshold event the next ~15 MB of process-heap growth would trigger anyway. While open
  the EXE churns ~0.5 MB/s extra. The tree's Lua state (~1.15 MB) is built at load whether or not it is opened.
- Whole-4 GB decomposition (n=5 analysis): tech tree +15.81 = committed +0.05 + reserved +15.76 (the segment is claimed and left uncommitted).
- Method: the **video driver commits/decommits write-combined GPU buffers (41-64 MB) inside its reservations**, setting a ~16 MB committed
  noise floor - use committed excluding driver buffers. VMMap itself perturbs the process.

## 3. Myth 3: fragmentation, dosed (2026-09-17) - peak-driven, not churn-driven

Folklore: "the tech tree churns 100-200 MB and the fragmentation crashes the game". RunId 1789663915 (GameId 64 from `AutoSave_Post_0317`):
50 yield toggles, 75 open / sweep 9 scroll positions / close cycles (blocks of 5, 20, 50), tree held open 7 minutes, idle controls between
blocks; 5,862 Lua actions, 28 snapshots in 60 min; then turn 318 -> 319 measured separately.

**The churn is real:** idle the process pushes 1.83 MB/s through the CRT (35,400 allocations/s); the tree adds 3.6 / 4.5 / 4.9 MB per cycle
and +0.51 MB/s while open; total **+584 MB and ~12.0 M allocations beyond idle** in the hour. One AI turn churns **3,172 MB** (1,099 CRT +
1,946 DLL `new` + 127 Lua).

**It fragments nothing:**

| | tree work (76 cycles + 7 min open) | one AI turn (318 -> 319) | average turn, continuous game |
|---|---|---|---|
| heap free lists | **+0.22 MB** (113.15 -> 113.37) | **+27.19 MB** (CRT +22.44, Lua +5.02) | +1.49 MB |
| free entries | +2,055 (of 560k) | +105,949 | +3,734 |
| new heap segments | 1 (16 MB, into a hole freed 6 min earlier) | 4 (3 CRT, 1 Lua) | - |
| claimed address space | -15.6 net | **+64.32 MB** | +2.58 MB |
| largest free hole | 832.44 MB, unchanged | **-47.44 MB** | -1.22 MB |
| below 2 GB | nothing outside idle flicker | free -0.53 MB | - |

Mechanism: a screen that allocates and frees the same sizes every frame reuses the entries the previous frame freed; free lists grow only when a
**peak** forces new segments and is then freed. A fresh process at t318 holds 113 MB of free list vs 159 MB in the same game played continuously
to t317 (182.5 MB by t329, from 59.6 MB at t250). The only lasting tree cost: ~40 KB / ~200 process-heap blocks per cycle.

**Correction of 2026-09-10:** the -91.9 MB largest-hole move at a tech-tree open did not reproduce in 79 opens across two sessions, nor did the
crash. That crash was an EXE access violation through a wild pointer (not a failed allocation), at a sub-2GB state (1.62 MB largest block) no
worse than this run's 0.88-1.00 MB. The tree may have triggered that AV; fragmentation was not the mechanism.

Measurement note: table `MemSnapHeapFree` was built for this. A heap's largest free entry saturates at **508 KB** (larger requests go to
VirtualAlloc), so it is a constant, not a signal; use the size-class split.

## 4. Mods and graphics settings (n=2, 2026-09-23/24) and the n=5 batch (2026-09-28/29)

Baseline save `VP8P-HUGE_0350` (GameId 118, Continents 128x80, 8 majors, turn 350; InfoAddict edited to `AffectsSavedGames 0`); VP from
`MODS/`, launched click-free by `vp_modgame.py`.

**How the intervals were computed (n=5, from 2026-09-29):** in-process effects = state minus the do-nothing control immediately before its block,
t-interval with 4 df; launch arms = arm minus a pooled control of n=9 (base + strategic view + uimyths turn-1), night-blocked OLS, df=25; committed
reported excluding the driver's write-combined buffers (halves noise, sd 11.6 -> 6.7 MB). The n=2 page's half-range floors were 1.5-3x too narrow
(below 2 GB: 1.65 vs ±2.55 MB). Key noise facts: **claimed below 2 GB carries ~±2.5 MB of launch-to-launch noise from ASLR** (~20 game DLLs re-based
every launch; a reboot re-bases 157 system DLLs and moved the level 6.6 MB) - between-launch comparisons below 2 GB resolve only ~±3 MB at n=5 and
must block by boot; in-process comparisons resolve ±0.5 (or ±0.01). The 16 MB process-heap segment is a threshold event that lands on whatever block
is running - report as k of n. A 32 MB reservation is sometimes released on its own; a 1.25 MB block comes and goes around the idle step.

Results (95% intervals; **no myth moves claimed below 2 GB** - every interval spans 0):

| arm | result |
|---|---|
| Minimum video quality | -433 MB VRAM, **-292 ±6 MB committed, all above 2 GB** (n=2: -301, 296.7 above the line); +113,220 ±3,294 heap blocks allocated before the DLL loads |
| Single Unit Graphics | -99,083 ±28 EXE blocks, -16.8 ±6.0 MB committed |
| InfoAddict | +3.83 MB Lua, +11.8 ±6.0 committed, +29.5k unattributed blocks (its n=2 +2.17 MB address space not established) |
| Strategic view | -5.0 ±2.4 committed (-4.2 ±1.1 kept), +1.4 VRAM kept |
| Yield icons | +3.0 VRAM on the first toggle, kept; nothing else |
| Tech tree open, churn | nothing |
| Leader screen, first visit (myth 7) | **+209.4 ±0.2 MB VRAM**, kept after closing, identical at leader quality 0 and 3; 0 below 2 GB. Myth 7 false. (The n=2 leader-quality arm never opened a leader; its null was an artefact) |
| Zoom fully out vs the load's zoom (myth 9), with units | +77.4 ±0.2 VRAM, +15 ±2 committed, +4.81 ±0.05 over 4 GB, 0.00 below 2 GB |
| One AI turn (n=31) | +0.18 ±0.30 below 2 GB, +97.7 over 4 GB, +97.9 committed, +5.2 VRAM |

Re-test of myths 3 and 9 (2026-09-29 afternoon, scenario `treezoom`, 5/5): tree **first open** +0.88 ±0.29 MB committed, 0.00 ±0.01 address space,
VRAM +0.03; **zoom with all 844 units killed** fully in (43 plots) -> fully out (533): VRAM +77.41 ±0.01 (kept), committed +4.45 ±0.88, address space
-0.25 ±0.42 - so the earlier +15 committed / +4.8 address space was the units coming into view. Killing the 844 units frees **44.5 ±2.4 MB committed**
(24.4 DLL heap, -168k blocks), no address space or VRAM.

## 5. Myth 10: units on screen (2026-09-29/30) - a real crash, not memory

Protocol: kill all units, camera fully out over the capital (VP CameraView's 533-plot set, calibrated to be the screen edges), groups of 25 on screen /
off screen / barbarians in fog, then +10 UNIT_MACHINE_GUN per step to 529 or death. 5 loads, DLL d1484d63 with `heaps=0`.

- **Memory (n=5):** creation ~20 KB of EXE allocations and ~100 blocks per unit whether on screen, off screen or in fog; on-screen ramp **105 ±1 KB EXE
  allocations / 85 ±16 KB committed per land unit**, address space +222 ±117 KB/unit in heap-segment steps, **below 2 GB +0.9 ±2.4 KB/unit (nothing), VRAM 0**,
  Lua +1.4 KB/unit, embarked ~16 KB.
- **Every load crashed at 400-450 Machine Guns in view**, with **818-936 MB free above 2 GB** in one piece. Cause: an overflow of the EXE's per-frame 2D UI
  draw list - a bump allocator (EXE 0xc7fdc0: base + offset, offset += size, **no bounds check**) with capacity **0x100000 = 1 MB**, double-buffered (two 1 MB
  bases). A DLL-side guard is impossible (the buffer is the EXE's); an EXE hook like EngineQueueGuard could bound it.
- **The records are EUI unit flags:** empty screen fully out 194,864 B/frame; each unit in view adds **1,902 B = 13.2 records** -> full at ~449; crashed placing
  441-450 with the probe watching the offset pass capacity (1,053,456 B, 4,556 records). **Flags hidden: 11 B/unit, 529 units fill 20%, no crash.** Single Unit
  Graphics: byte-identical fill (3D models never use this buffer), no help - it does cut committed per land unit ~84 -> ~21 KB. Only units in view count.
- **Decoded (2026-10-06):** record kinds 1 = sprite 240 B, 2 = text label 144 B (one per label, any length), 0x882 = nine-patch panel 352 B, 3 = line 48 B. Each unit
  = 8 sprites (4 x 64x64 flag layers, 2 x 32x32 unit icon + shadow, 2 x 16x16 promotion icons) = 1,920 B; a flag without promotion icons 1,440 B.
- **Promotion flags (EUI `PromotionFlags`):** 240 B per promotion icon per unit in view; 13 icons (cap) ~4,650 B/unit -> full at ~185 units; option off = 0.
- Other screens draw on top of the world UI: tech tree **+280,800 B**; strategic view 359,616 B total; InfoAddict Historical Data graph **+498,880 B** (~61 KB per civ
  line; a 20-civ game would pass 1 MB by arithmetic, untested); Trade Route Overview scrolls only visible rows (no crash with 624 routes). Yield icons: VP's Lua
  YieldIconManager draws them (**corrected 2026-10-07**; the 10-06 "drawn by the engine" line is withdrawn) - 300,720 B fully out.
- **A second, rarer crash:** 2 of 8 loads died placing 391-400 at ~90% fill, EXE reading `[global+0x9C] = 0xFFFFFFFF`, then a sound-thread fault on the same field -
  not the overrun. **Cause open.**
- Engine C++ YieldIconManager unhooked (2026-10-08, n=5 ABBA): draw list identical; **-61.8 ±3.1 MB heap** from the first snapshot - one 61,149,008-byte "PreDLL (EXE
  side)" block present in 100/100 A snapshots and 0/100 B; claimed 4 GB -67 ±33, below 2 GB -1.7 ±2.3 (no change). Contents not identified.

## 6. The OOM-G88 test saves (2026-09-19..21)

GameId 88: 180x113 = 20,340 plots, 20 majors, observer mode from turn 0 (every load keeps autoplaying).

- **Run 1** (RunId 1789964844): loaded t249, died mid-turn 272, 23 turns / 86 min later. Committed jumped 3379 -> 3522 MB (+143), GPU shared 52 -> 178 MB, free 180 -> 86
  MB (below 2 GB 22.7 MB free, largest 0.88 MB), then the EXE's modal **"Render Error: FGXRenderer11::Present failed hr=DXGI_ERROR_DEVICE_REMOVED"**. No exception, no
  DLL frame, no TDR in the System log - the in-process 32-bit driver failing under address-space exhaustion is the consistent reading, **not proven** (DeviceRemovedReason
  not captured). The DLL writes no crash record for this path. ~8.6 MB/turn committed after the load.
- **Run 6** (RunId 1789975147): loaded `OOM-G88_0250` (queue 99.9%, survived), died of the same box in turn 280, 30 turns after the load, at 62 MB free (44-46 MB for the last
  hour, largest block dipping to 1.0 MB; one ~11 MB allocation took it 16 -> 10 MB), committed 3439 MB.
- **Saves:** 21 copies in `Saves/single` named `OOM-G88_<TTTT> <year>` (crash point `OOM-G88_0272 AD-1804 crashpoint`, every multiple of 10 from 270 to 80); manifest with
  sha256 in `SaveArchive/OOM-G88/manifest.json`; run 6's branch in `SaveArchive/G88-branch-run6-T250-280/`.
- **The reproducible OOM test:** load `OOM-G88_0250`, autoplay, expect the DEVICE_REMOVED box after ~20-30 turns - *if* the load survives. Late loads of this game overflow the
  EXE's 4 MB game-to-UI message queue (load-only; normal turns stay under 3.6%); `OOM-G88_0240` overflowed to 178% and hung. EngineQueueGuard (2026-09-21) diverts what does
  not fit (t280 loaded, 39,128 records / 6.8 MB dropped); the 2026-10-01 capture showed the dropped records are terrain decals (farm fields, route curves). Details belong to the
  EXE-internals section; see `civ5-exe-message-queue-overflow.md`.

## 7. The 2:1 block pair and FluidFOW (resolved 2026-11-09, with caveats)

Long-open question: what are the giant pattern-filled heap blocks - 118.75 MB of byte 0x26 and 59.38 MB of byte 0x25 (GameId 47, t228, 2026-09-07), and in GameId 62 a
164.53 MB 0x26-fill block with an 82.27 MB 0x25-fill block (2026-09-15)?

**Answer (GATES.md, live-confirmed): the FluidFOW particle grid.** An allocation trace (EngineAllocTrace, thunks on the EXE's msvcr90 import slots, threshold 1 MB) on a load
of the huge t350 save found `FluidParticles.cpp:718` **48.24 MB** + `:723` **24.12 MB** (+ `:719` 1.51 MB), aligned 128, one call chain, main thread, during the load - the
largest live engine blocks, exactly 2:1, sized `cells x 4096` / `cells x 2048` (118.75 MB / 4096 = 59.375 MB / 2048 = 30,400 cells). With `[DisableSystems] FluidFOW = 1`
there are **zero** FluidParticles allocations (the whole 73.9 MB set is gone). Separately the **8 x 24 MB float blocks are FluidFOW cloud particles**, one per job thread
(`MaxSimultaneousThreads = 8`), map-independent; FluidFOW=1 saved -221 MB at the main menu and -294 MB in game on the huge save, but renders the map flat deep red, so it is
not a player-usable setting.

Caveats (still open):
- The 0x26/0x25 fill was **not reproduced**: right after the load both blocks are ~90% zero (the old census was late-game; contents are simulation state).
- The sizes do not line up cleanly: GATES.md calls the 118.75/59.38 pair "the other (180x113) game", but the log recorded 118.75/59.38 on GameId 47 (128x80) and
  164.53/82.27 on GameId 62 (180x113), while the trace saw 48.24/24.12 on another 128x80 map at t350. GameId 47's census also listed separate all-zero 48.24 and 24.12 MB
  blocks - the same sizes as the traced FluidParticles pair. The cell formula `(W/64+3)x(H/64+23)` does not fit both maps; growth over time or reallocation is unconfirmed
  ("not reallocated over three turns on this map").

Sources: `research/sources/memory-notes/civ5-memory-investigation-log.md` (items 25-31; section 3 "UI settings do not move the 32-bit address space", "Memory Myths",
"Fragmentation: churn does not cause it, turns do", "The big memory belongs to the renderer", "Why the ceiling is ~3.5 GB"), `n5-run-plan.md`, `units-myth-plan.md`,
`stats-db-game-ids.md`, `civ5-exe-disable-systems.md`, `civ5-shell-gates.md`; `research/sources/shell-gates/GATES.md` ("Owners of the 118.75 MB (0x26) and 59.38 MB
(0x25) blocks").
