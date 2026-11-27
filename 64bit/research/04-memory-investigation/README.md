# 04 - Memory investigation (2026-09-03 to 2026-11-09)

Scope: the investigation into why 32-bit Civilization V with Vox Populi dies of out-of-memory on large
late-game saves - what was fixed, what was measured, what was debunked and which instruments exist.
It is the evidence base for the 64-bit server project: it says how much memory the DLL actually owns,
what the EXE keeps no matter what, and which "memory hogs" from player folklore are not.

Everything here is synthesised from the verbatim notes in `research/sources/memory-notes/` (the main
log is `civ5-memory-investigation-log.md`, 148 KB) and `research/sources/shell-gates/GATES.md`. Where a
note later corrected itself, these files record the correction, not the superseded claim.

## What every agent must know

1. The process dies at **4025 MB of address space out of 4095.9 MB** (LAA works, WOW64 gives the full 4 GB); "~3.5 GB" is only the committed figure. The rest is 510.6 MB reserved-never-committed (flat from turn 0) plus 70.5 MB free but shattered into 713 holes (largest 4.0 MB).
2. **The EXE and everything else own ~78% of committed memory, fixed from turn 0** (78.1% small map t7, 77.7% 20k-plot map t344). The DLL is **5-9%**, Lua ~4%, the video driver ~9-12%.
3. **The sub-2GB half is saturated from turn 0** (~1.6 GB committed, ~380 MB reserved, largest hole ~1 MB) and does not move over a game - a fixed toll, not a falling curve (corrected 2026-09-15). The variable that runs out is the high half.
4. **The leak is closed.** It was AI settle-plot logging (`CitySites_<civ>_<turn>.txt` buffers retained by the log manager); fixed in commit **a26506ae8**, heap growth **4.05 -> 0.63 MB/turn** on a huge map, reload gap 1170.7 -> 14.7 MB (a fixed offset, not a rate).
5. **Post-fix growth on the 20,340-plot map is 2.476 MB/turn committed**; the death turn follows from that growth against a flat ceiling to within 3% (predicted ~346, died 317; fragmentation cost ~29 turns).
6. **The DLL owns 56% of growth bytes and 99.9% of new heap blocks**; one call site (DangerPlots per-plot vectors, 7,555 blocks/turn by tag) is 71% of all block growth. *(Qualified 2026-11-10, epic 1300: that holds for GameId 62's reload run t317-344, where the danger map refills from empty after the load. In fresh headless games (t20-200, Standard and Huge) the largest site is the per-player replay history `CvPlayer::m_ReplayData` (RVA `00114AE3`, one map node per dataset per alive player per turn), at 42-52% of DLL block growth; it is live data; DangerPlots is second. See leaks-and-growth.md section 2.)* Block count drives a free-list tax (29% of per-turn committed cost).
7. **Fragmentation is peak-driven, not churn-driven:** 76 tech-tree cycles + 7 min open churned 584 MB and moved heap free lists +0.22 MB; one AI turn moved them +27.19 MB and claimed 4 new segments.
8. **The process is 73.6% heap** (32-bit `vmmap.exe` only - `vmmap64.exe` silently misclassifies WOW64 heaps as Private Data). The CRT heap is 99.85% attributed: EXE 927 MB, DLL ~85 MB. Textures are in VRAM, not the address space.
9. **No UI myth moves the sub-2GB half** (yield icons, tech tree, zoom, leader screens, mods, video quality). The "units on screen" crash at 400-450 units is a 1 MB EXE render-buffer overflow (EUI unit flags), not memory.
10. **Do not redo:** UMDH/gflags (zero stacks, LFH - ruled out), the analytic `sizeof` census (caps at 2.4%), per-plot allocation storm, replay data, "turn-0 committed as a baseline", per-context Lua duplication, std::string as the hidden mass.

## Files

| file | contents |
|---|---|
| [timeline.md](timeline.md) | dated chronology, log items 1-31 plus the 2026-10 and 2026-11-09 follow-ups, each with its conclusion |
| [ownership-and-ceiling.md](ownership-and-ceiling.md) | the 4096 MB arithmetic, reserved/free/slack, sub-2GB placement, owner split, coverage audit, MemoryImports attribution, Lua's heap, textures in VRAM |
| [leaks-and-growth.md](leaks-and-growth.md) | the CitySites log leak and its fix, post-fix growth and who owns it, the free-list tax, first-played-turn spike, recommended in-place fixes |
| [myths-and-measurements.md](myths-and-measurements.md) | Memory Myths (yield icons, tech tree, fragmentation dosed, mods/graphics n=2 and n=5, leader screens, zoom, units on screen), OOM-G88 saves, FluidFOW owner of the 2:1 block pair |
| [increment-1-acceptance.md](increment-1-acceptance.md) | D78: the EXE's private bytes and largest free block per AI turn through the proxy (pin `x64-76992d23-p1607d42`) against the stock route, huge save from turn 350 to the game's end (2026-11-13, one-off 9043); section 4 (ticket 0322, 2026-11-15): the same save under scripted human play through the proxy, 80 turns: private +0.84, largest free block -2.02 MB/turn, D78 holds |
| [increment-2-acceptance.md](increment-2-acceptance.md) | Ticket 0325 (2026-11-16): increment 2's full-game soak, a new Standard Communitu game (Settler) played to turn 300 by the HI lane (posted mouse and keyboard, read-only judge; `shell/evidence/0325/soak_0325.py`) on the D33 product path (`vp64helper on`) beside a stock arm (stock EXE + i0-ship 32-bit DLL): no crash, box, server restart or report; picking 70 of 70; 0 gray frames in 11,668 1 s lines; EXE private +0.45 MB/turn and largest free block -0.65 against stock +0.54 / -0.72, server.exe +0.17 (workstation indications); the final save loads and plays on stock (D10); the human's posted paths for every end-of-turn blocker met (production automation, Future Tech, policies, pantheon, promotions, World Congress, leader scenes that silence the Lua channel) |
| [server-reload-memory.md](server-reload-memory.md) | bug 0121 (2026-11-15): server.exe's private bytes over several loads of one save in one EXE process on the pinned release layout `x64-04b1211b-pfdee656`: flat after the first reload (huge +84 then a plateau, huge after 10 AI turns +59 then back, Tiny +80 then +0.6 MB/reload, render=0 control +5); the first step is the D3D11 driver's pools and 10-45 MB on the heap's free list, nothing kept per reload; the old +289..+432 MB step does not reproduce; most of the server's private bytes are the driver's committed, never-written blocks; per-load peaks, owner walks, tools in evidence/0121/ |
| [instruments-and-tooling.md](instruments-and-tooling.md) | DLL instruments (MemoryDiagnostics, alloc hook, MemoryImports, Lua tiers, snapshots, RunId), stats.db GameId map, external tools, what was ruled out, save rules |

## Open items carried forward

- The **128 MB unowned reservation** below 2 GB (0x10020000 on 2026-09-10, 0x1A020000 on 2026-09-17) - not named.
- **Heap index 10** reserves ~72-73 MB to hold ~14 MB - unidentified.
- The **61 MB "PreDLL (EXE side)" block** that disappears when the engine's C++ YieldIconManager is unhooked (2026-10-08) - contents unidentified.
- The 0x26/0x25-filled 2:1 block pair is attributed to the FluidFOW particle grid by size ratio and absence under FluidFOW=1, but the fill bytes were not reproduced and the sizes do not line up across games (see myths-and-measurements.md).
- A second, rarer units-on-screen crash at ~400 units (EXE reads `[global+0x9C] = 0xFFFFFFFF`) - cause open.
- **Through the proxy (increment 1)** the EXE starts 175 MB lower at turn 350 of the huge save (1,839 against 2,014 MB
  private) but grew about 30 MB per AI turn on the pins of 2026-11-11 against 2.41 MB on the stock route, and ran out of
  address space at turn 420 (bug 0102). The growth is save-body-sized heap blocks (11-13 and 17-19 MB) kept by the proxy's
  own RETURN ring (`KeepReturn`, 64 vectors per thread that never shrank), not the EXE's save buffers [C, 9041, 2026-11-13];
  fixed in the proxy (`03-dll-engine-boundary/proxy-design.md` sections 18.7-18.8). *(Replaced: "the EXE's save-body-sized
  buffers, released on the stock route and kept through the proxy; why is open", 9036.)* **Acceptance (D78, 9043,
  2026-11-13) [L]:** on the moved pin `x64-76992d23-p1607d42`, huge save from turn 350 to the game's end (Egypt
  defeated: turn 436 through the proxy, 425 on the stock route), the EXE grows +0.70 MB per AI turn through the proxy
  against +1.63 on the stock route over the common turns 351-426, its largest free block falls 1.91 against 2.70 MB per
  turn; straight-line wall about turn 1,060 against 730 (`increment-1-acceptance.md`).
- The recommended DangerPlots fix is done (2026-11-10, epic 1300, leaks-and-growth.md section 4); the "three cheap signals" (next-step 12) - no source records them as done.

Sources: all files in `research/sources/memory-notes/` named in each file's Sources line; `research/sources/shell-gates/GATES.md`.
