# The shell architecture against the pure 64-bit port's claims

Written 2026-11-10 from `sources/REVIEW.md` and the published review page. The port's claims are
as stated by its author (a private, unpublished re-implementation run with Vox Populi and 25 mods).
The shell column is what the validated design can reach and why.

| Claim for the port | Shell architecture | Verdict |
|---|---|---|
| Visually and gameplay-wise indistinguishable from the original | UI, menus, popups, leader screens, audio identical by construction (same EXE); gameplay is the same DLL. The world is a re-implementation; textures and shader bytecode may be tapped (Gate 2 pending), the terrain bake pipeline and Granny animation cannot | UI exact; world approximate at first, parity is a long tail |
| Save-compatible, VP mods, 25 mods | Save streams and the Lua VM stay in the EXE; Lua UI mods unchanged; x64 DLL must serialize identically (`size_t` trap); DLL-level mods need an x64 build | Equal, same trap the port had to solve |
| Never crashes, hundreds of turns past victory | Game-state growth leaves the 32-bit process (56% of growth bytes, 99.9% of new blocks). A 32-bit shell remains at ~1.3-1.7 GB in play with fixed EXE buffers, fragmentation and the 32-bit Lua VM, but ~2.5 GB headroom instead of a wall at ~t320 | Large improvement, not a proof |
| Launch, mod menu, load in 10-20 s | (Corrected 2026-11-10 from LOADTIME.md; replaced "EXE ~89 s stays, DLL ~16 s, Landmark-off ~39 s".) Load is 141-157 s process start to WaitForNetPlayers; the EXE's own work (~96 s incl. 37 s of DxDiag report, DirectShow wait and file reads before/around the load) stays; the DLL's ~17 s mostly go with a warm server and `ValidateGameDatabase = 0` (~10-14 s); Landmark off saves 30.8 s (farm-field layout); config alone reaches ~105 s [I] (`02-exe-internals/load-sequence.md`) | Not reachable; floor ~60-80 s only with the modpack and EXE-side work [I] |
| 240 fps with 2x frame generation | Shell presents ~280 fps with the world off; server renders at its own rate; frame generation needs app integration and the swap chain is the EXE's | Raw fps comparable; no frame generation |
| Smooth while the AI plays (12 ms p95 vs 200-270 ms freezes) | Original freezes: the EXE takes the game-core lock 63x per frame while the DLL runs the AI, plus the DLL-to-UI queue flood. With the DLL out of process the lock disappears structurally; queue processing stays. B4 with the DLL in-process still showed ~200 ms spikes | Likely much smoother; unproven until the proxy exists |
| AI turns under 3 s at turn 290 (large, 10 civs) | Same x64 DLL, same caching freedom; adds ~1 s/turn IPC for 1.1-1.2 M UI-Lua reads before batching/caching | Late-game parity plausible |
| Early game End Turn 10-17x faster (7 s -> 0.4-0.7 s) | At turn 2 the AI computes almost nothing; the 7 s is EXE-side work (autosave, UI refresh, Lua) the shell keeps | Uncertain |
| Aggressive caching up to 10 GB | Server caches without limit; EXE-side work cannot be cached beyond the remaining 32-bit headroom | Half |

## What the shell has that the port does not

- Ships as a patch on the original EXE, assets and DLL. The only reason the architecture exists.
- Ships in increments, each usable and reversible.
- The server is the headless host: the same binary runs AI tests, replays and DLL unit tests in CI.
- Zero UI porting: EUI and every Lua UI mod run in the EXE.

## Structural limits of the shell

- A 32-bit process remains, ~1.0 GB committed at the main menu before any world exists.
- EXE load pipeline, Lua VM, save streams and UI renderer stay 32-bit and closed.
- Two processes to keep coherent; server restart demonstrated for the texture, not for game state.
- Injector coexistence requires inline hooks or drawing from the EXE's render path.
- Multiplayer with original clients is not a goal in either design. *Corrected 2026-11-10 by the librarian; it read
  "is lost in both designs (x87 vs SSE2)". The 32-bit DLL already uses SSE arithmetic (0 x87 arithmetic
  instructions) [C, epic 1600]; the remaining known drift sources are C runtime maths functions (msvcr90 vs UCRT) and
  address-ordered squad moves, so turn-for-turn agreement is to be measured, not ruled out (`decision-log.md`
  "Correction 2026-11-10 04:17", `../03-dll-engine-boundary/x64-dll-port.md` sections 5 and 9). The non-goal stays
  until measured and until the owner decides.*

Sources: `sources/REVIEW.md`, `sources/shell-architecture-review.html`, `sources/shell-gates/GATES.md`, `sources/shell-gates/STUBS.md`, `sources/memory-notes/civ5-load-time-breakdown.md`.
