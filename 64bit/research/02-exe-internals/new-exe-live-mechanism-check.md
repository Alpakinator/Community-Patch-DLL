# Build 26577625 live: every patch mechanism class, code writes and a debugger (ticket 0592, D205, 2026-11-19/20)

The owner's authorization (2026-11-19 22:16, to the 0582 session; recorded as D205) [reworded for privacy]: this run and any further
series of runs needed to establish that the new EXE can be patched by all current classes of mechanisms without crashing
are authorized. If no problems occur, this is not needed for every single release; it only confirms that the change was
caused by the Lua remote-code-execution fix and not by added anti-tampering for some other reason. This file is the live counterpart
of `new-exe-build-26577625.md` B.3 (static). Tools and the first series' record: `tools/exewrite_check/` (`RESULT.md`).

**Answer [L]: build 26577625 has no anti-tamper.** Every mechanism class the project uses, plus in-memory writes to
`.text` and `.rdata`, an inline hook in a live EXE function, and a debugger with hardware and software breakpoints,
ran through 50 AI turns, a save, a load and 13 more turns with no crash and no tamper exit. The patched game was
identical to the unpatched control. The stock EXE (no probe, no debugger) ran clean. The same writes on build 4390913
ended the EXE with its DRM exit 0x8000DEAD within 80 s, so the test detects such a protection when there is one. The
only new behaviour seen live is the Lua sandbox: `_G.loadstring`, `io` and `os.execute` are nil, while `load` and
`dofile` remain. That is the Lua RCE fix the owner named.

## 1. Result per mechanism class [L]

Clean copy of c5h-new-b (runs R2, R3), 2026-11-19 23:53 - 2026-11-20 00:11. The counts are calls through each hook
over R2's 50 AI turns; "held" means the write or hook was still in place at every 5 s re-check up to the stop.

| Class (`new-exe-build-26577625.md` A.2) | What the probe did | Result on build 26577625 |
|---|---|---|
| Import slot | `KERNEL32!InterlockedExchangeAdd` and `KERNEL32!QueryPerformanceCounter` slots (found by name; RVAs 0x539414, 0x5393FC) -> counting thunks | held; 305,919 and 40,174 calls; no crash |
| vtable pointer of a heap object | the FCamera object inside GameViewState (`[g_Civ5App+0xC0]+0x48`): vptr -> a copy of its 3-entry vtable, GetViewProj counted | held; 5,338 calls |
| `.data` / heap write | config mask `[g_Civ5App+0x940]` rewritten with its own value every second | 349 writes; no effect on the game |
| Hook in another module | MinHook inline hook on `d3dx9_43!D3DXMatrixMultiply` | held; 25.8 M calls |
| Call into the EXE | the EXE's export `PeekLogFileMgr` every 10 s | 37 calls |
| **Inline hook in EXE `.text`** (new) | MinHook 5-byte jump over `GameViewState::UpdateViews` (RVA 0xC66F0) | held; 5,274 calls |
| **`.text` byte** (new) | one unreachable `int3` padding byte (RVA 0x102148) -> 0x90 | held |
| **`.rdata` byte** (new) | one alignment byte (RVA 0x549A1F) -> 0x5A | held |
| **Debugger** (new) | cdb attached; `ba e1` (debug registers on every thread) on UpdateViews and `bp` (an int3 written into code) on `FCamera::GetViewProj` (RVA 0x44AA20) | 4,000 / 4,001 hits in 285 s, no `SetContext failed`, the game unharmed, 3 more AI turns under the debugger, normal stop |
| Lua channel (test-only, every new-build run, controls included) | the EXE's `lua51!lua_setfield` import slot filtered: the EXE's `_G.loadstring = nil` replaced by a C `loadstring` over `luaL_loadbuffer` | the EXE set it at +23 s after start; the harness's channel worked |

Targets are found at run time: g_Civ5App by VP's PR #13379 signature, UpdateViews and GetViewProj by their
`exe_camera.h` patterns, the bytes by rule (int3 run after a `ret`, 16-aligned, no relocation; zero alignment run before
a referenced object, outside the data directories), slots by import name (`tools/exewrite_check/exeprobe.cpp`).

## 2. Runs

| Run | Copy | Build | Probe | What happened |
|---|---|---|---|---|
| O1 2026-11-19 22:30 | c5h-a | 4390913 | the product's classes only | 10 AI turns, clean |
| **O2** 22:36 | c5h-a | 4390913 | all, with the code writes | **the EXE ended itself at +80 s, exit 0x8000DEAD**, during the load (positive control) |
| N0 22:43 | first c5h-new-b | 26577625 | Lua filter only | 50 turns (9 -> 59); then the debugger check, see section 3 |
| N1 22:58 | first c5h-new-b | 26577625 | all | 50 turns (8 -> 58), clean |
| N0b 23:06 | first c5h-new-b | 26577625 | Lua filter only | 50 turns (8 -> 58), clean; fingerprint = N1 |
| N2 23:12 | first c5h-new-b | 26577625 | all | N1's save loaded, 10 turns, debugger check (stays attached), 3 turns, clean |
| **R0** 23:30 | clean c5h-new-b | 26577625 | **none, no debugger** | the Tiny save loaded, 13 min alive (private 1.63 GB steady from 3 min), no dialog, stopped by `stop_nolua` (exit 15) |
| **R1** 23:47 | clean c5h-new-b | 26577625 | Lua filter only (control) | 50 turns (8 -> 58), clean |
| **R2** 23:53 | clean c5h-new-b | 26577625 | **all** | 50 turns (8 -> 58), clean; **fingerprint = R1** |
| **R3** 2026-11-20 00:00 | clean c5h-new-b | 26577625 | **all** | R2's autosave loaded (turn 57), 10 turns, debugger check, 3 turns under the debugger, clean |

Every new-build stop was the harness's (exit 15, no box). "First copy" = c5h-new-b as made at 22:39, which also held the
installed 64-bit patch's layout files in its mod folder, inert under the i0ship DLL (MC 23:12, epic 0590 W3); W3 remade it
clean at 23:26 (provision verify --full ok on W3's branch; main's `provision.py verify` still flags the new build's
re-signed `lua51_Win32.dll` as "not the stock Lua 5.1.4", a stale check, not a difference). The clean-copy runs R0-R3
repeat everything the conclusion relies on. W3's own smoke on the clean c5h-new-a (#7369, 23:15) adds a stock run with
turns: new game, 8 AI turns by the autoplay mod, no probe, no debugger, clean.

Side effect found afterwards (2026-11-20, MC-B): O2's exclusive `cp,vp` front end plus the DRM's kill during the mod
activation left c5h-a's `cache\Civ5ModsDatabase.db` with CP and VP enabled and **0 mods activated** (written 22:37), and
account B's scene player then failed its pre-check there. `frontend.py restore` puts back the front-end Lua files only,
never the mods database. No copy from before the claim existed, so a hash restore was not possible: a front-end-only
launch (`frontend.py prepare --mods cp,vp,eui`, no save) had the game itself re-activate CP, VP and EUI, the set c5h-b and
c5h-c hold; verify ok, released 00:23. Lesson: a run that changes the mod set should copy the mods database first and put
it back after, especially when the run may be killed.

Setup: Goldberg copies on F:, DLL `i0ship-e96457fd-ia`, mods CP + VP, save `C5H-0167-TINY_0008` (Tiny, turn 8),
launched by the harness (cloaked, frame cap 30), AI turns by `Game.SetAIAutoPlay(N)`, a dialog watcher on every launch,
claims through `pool.py`, front ends restored by hash after each run.

## 3. Control vs probe, and the debugger

- **Same game**: `tools/exewrite_check/fingerprint.py` at turn 58 (turn; per player score, gold, cities, population,
  units, techs, culture; a hash over every plot's owner, improvement and route and every unit's owner, type and
  position). R1 (control) = R2 (every mechanism) = N0b = N1: `hash=718739953`, every per-player value equal. The clean
  and the first copy give the same game.
- **Debugger**: build 4390913 ended the EXE with 0x8000DEAD within about 80 s of any debug register
  (`farm-field-double-free.md` 1.2). Build 26577625: N2 and R3 kept hardware and software breakpoints set for 285-290 s
  with 4,000 hits each and the game ran on. One crash in the first series came from the tool, root-caused (D185): after
  N0's check, cdb ran `bc *` and `qd` in one breakpoint command; one thread kept its debug register, whose next hit at
  UpdateViews (file offset 0xC5AF0 = RVA 0xC66F0) raised STATUS_SINGLE_STEP 0x80000004 with no debugger, and VP's
  handler showed "Game Crash" (2026-11-19 22:55:20, c5h-new-b `run\crash\20261119_225527_pid31020`). The "address space
  nearly exhausted" text in that box is VP's Sub2G figure (32 MB free below 2 GB; 1,593 MB free in all, the EXE is
  large-address-aware), the same as old-build boxes, not the cause. `debugcheck.py` now stays attached until the game
  ends; N2 and R3 ran that way, clean. No run without a debugger crashed.

## 4. What is not covered

Steam (no Steam launch by rule; Goldberg only), multiplayer, games longer than 70 turns, EUI (not loaded; its
`loadstring` use is in `new-exe-build-26577625.md` A.2b), a stock run with turns longer than W3's 8 (the instance DLL's
Lua channel needs `loadstring` until a DLL with ticket 0591's change exists, and mods548 instances have no autoplay mod).
Using `.text`/`.rdata` writes in the product still needs the owner's OK per fix (D200).

Sources: `tools/exewrite_check/` (probe, runner pieces, RESULT.md); local logs `C:\c5-64bit-work\0592\runs\` (probe
logs per pid, cdb logs, fingerprints, R0 watch); thread 0590 #7352, #7369, #7381 (W3).
