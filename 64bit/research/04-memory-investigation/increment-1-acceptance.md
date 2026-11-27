# Increment 1's memory acceptance (decision D78): the EXE through the proxy against the stock route, huge save

One-off 9043, 2026-11-13. D78 (MC on the Architect's advice, 2026-11-12): the per-turn slope of the EXE's private bytes
and of its largest free address block over the huge save from turn 350, proxy route with bug 0102's fix against the
stock route on the same machine and save, with the 7011 memory sampler. *Amended by the MC 2026-11-13 03:31: the window
is "from turn 350 to the game's own end, at least 80 AI turns"* (the 100 turns of the first text cannot be played:
section 1).

## 1. Setup

| | proxy route | stock route |
|---|---|---|
| run | `proxy-350`, 2026-11-13 02:01-03:00 EST | `stock-350` + `stock-350b` (the same game process, see 2.2), 2026-11-13 03:43-04:44 EST |
| instance | c5h-b (Goldberg copy under `%USERPROFILE%\c5-instances`, workstation) | the same |
| save | `C5H-VP8P-HUGE_0350` (Huge Continents 128x80, VP 5.4.8 mods form, turn 350, SHA-256 c499d3c6...88c3) | the same |
| DLL side | the EXE with the x86 proxy 21C6492A (repo 1607d42b7) and server.exe C71268B0 + x64 DLL FA75F483 (pin 76992d235), layout `<pins_dir>/dlls/x64-76992d23-p1607d42` (`deps/dll.lock`), `local_answers=1`, `lua_mirror=0` | the EXE with the 32-bit pin DLL `i0ship-bbfacca1-ia` (e589306e, `deps/i0.lock`) |
| driver | `tools/validation/proxy_long.py c5h-b <run> --load C5H-VP8P-HUGE_0350 --max-turns 105 --extend --wait-chunk 120 --checklist-every 0` (one AI turn per step: `turns.py end` + `wait`; no checklist, no dump) | the same with `--stock` |
| sampling | the driver's read after each turn (`exe_mem`, `tools/harness/memory.py`) and the 7011 sampler (`perf/memsampler/memsampler.py run --instance c5h-b --server --every 2`) beside it | the same |

Memory only: no quiet-machine lock (other teams' builds and games ran on the workstation), no timing claim. Statistics:
ordinary least squares of the value on the turn, 95 % interval from Student's t (`D:/c5-64bit-work/9043/slope.py`, which
reproduces 9036's 2.41 MB/turn for its stock run; the sampler computes its own). The driver's row after the AI part of
turn t is labelled t+1 (9036's convention).

**The game's own end [L].** On the proxy route the human player (Egypt) was **defeated at turn 436** ("Your
civilization has been overwhelmed by its many foes", game state 1 GAMESTATE_OVER, winner -1; end screen
`D:/c5-64bit-work/9043/runs/proxy-350/end-0436.png`, no dialog). There was no Diplomatic victory at turn 420 as in the
headless runs (`06-tooling-and-workflows/quiet-testbed.md`). "Just one more turn" is not offered to a dead player
(`EndGameMenu.lua`: `m_bAllowBack` only when `player:IsAlive()`); the driver's `--extend` called `OnBack()` in the
EndGameMenu state and `Network.SendExtendedGame()` in Main and the state stayed 1. So this save gives 86 AI turns from
turn 350 on the proxy route. A seat switch to a living civ (`Game.ChangeActivePlayer`, VP Lua) was offered and declined
by the MC (it changes the game).

## 2. Results

### 2.1 Proxy route, `proxy-350` [L]

86 AI turns, 351-436, ended by the game (human defeated), safe stop through the game-finished path of
`turns.stop_readiness` (bug 0105, its first live use: "game finished (game state 1 over) at turn 436, newest save
settled", rc 0). 0 LOUD lines, 0 borrowed-argument lines, 0 exceptions in `vpproxy.log`; the server's report: 0 failed
outbound calls, 0 methods without remote treatment.

| metric (driver rows, turns 351-437, n 87) | first | last | slope MB/AI turn (95 % CI) | R^2 |
|---|---|---|---|---|
| EXE private bytes | 1,938.5 | 1,985.1 | **+0.678 (+0.592..+0.765)** | 0.74 |
| EXE committed | 2,246.3 | 2,292.5 | +0.676 (+0.590..+0.762) | 0.74 |
| largest free block | 1,438.8 | 1,254.6 | **-1.519 (-1.778..-1.261)** | 0.62 |
| free address space | 1,480.4 | 1,303.5 | -1.664 (-1.887..-1.440) | 0.72 |

Sampler (rows at each `AutoSave_N`, turns 350-435, n 86): private +0.77 (0.69-0.85), largest free block -1.55
(-1.81..-1.28); second half (turns 393-435): private +0.74 (0.50-0.97), largest free block -0.06 (-0.12..+0.01). The
smallest largest-free-block at any 2-s tick was 1,157.6 MB, the smallest free space 1,247.8 MB. The largest free block
falls in steps (1,439 -> 1,383 -> 1,302 -> 1,287 -> 1,255 MB, the last step at turn 384, flat from there to 436). The
server's private bytes 248 -> 317 MB (+0.65 MB/turn; 64-bit, not a limit). EXE private right after the load 1,841 MB.

Over 9036's window (turns 353-380, n 28) the same run gives private +0.67 (0.28-1.06); 9041's `ring-350` (same proxy
code but the old server.exe, 2026-11-13 00:59) gave +2.01 (1.55-2.48) there. Two runs of the same route differ by
about 1.3 MB/turn over 28 turns [L]: a 28-turn slope carries that much run-to-run spread [I: what differs between the
runs, the server binary or the game's course, was not separated].

**When the EXE would run out [I, straight line].** 9036's abort (bug 0102) came with 159 MB free and a 12 MB largest
free block. On the all-turn slopes: the largest free block reaches 12 MB at about **turn 1,230** (1,136 at the 95 %
bound of the slope), the free space reaches 159 MB at about turn 1,100 (1,043). The second half is flatter still. The
game itself ends at 436. Through the proxy, the huge save's EXE does not run out of address space within this game.

### 2.2 Stock route, `stock-350` + `stock-350b` [L]

One game process (pid 33540), 75 AI turns, 351-425, ended by the game: **the human (Egypt) defeated at turn 425**, the
same "overwhelmed by its many foes" screen (`runs/stock-350b/end-0425.png`), no dialog, safe stop through the
game-finished path (rc 0). At turn 412 a leader screen held the AI turn (the leader-screen handler stalled, see
`06-tooling-and-workflows/run-and-drive-the-game.md` 12.2); the driver ended (wait exit 6) and 9043 ended the driver
process (not the game), closed the five screens by hand with the refusing answer, and drove the same game on from turn
413 with a continuation script (`D:/c5-64bit-work/9043/cont.py`, the same steps as the driver). The sampler ran
through without a break; the driver row of the held turn is dropped.

| metric (driver rows, turns 351-426, n 75) | first | last | slope MB/AI turn (95 % CI) |
|---|---|---|---|
| EXE private bytes | 2,094.8 | 2,236.7 | **+1.631 (+1.503..+1.759)** |
| largest free block | 1,137.1 | 856.8 | **-2.701 (-2.960..-2.443)** |
| free address space | 1,182.8 | 977.8 | -2.185 (-2.396..-1.974) |

Sampler (turns 350-424, n 75, 0 skipped): private +1.82 (1.68-1.95), largest free block -2.70 (-2.96..-2.43); second
half (387-424): +1.38 and -1.69. Smallest largest free block at any tick 823.8 MB. Over 9036's window (353-380) this run
gives +2.39 (1.95-2.83), 9036's own stock run +2.41 (2.01-2.81): the stock route repeats [L].

**When the EXE would run out [I, straight line]:** largest free block at 12 MB about **turn 730** (711 at the 95 %
bound), free space at 159 MB about turn 790 (768).

## 3. Reading

| over the common turns 351-426 (n 75) | proxy route | stock route | difference |
|---|---|---|---|
| EXE private bytes, slope MB/AI turn | **+0.70 (0.59-0.80)** | **+1.63 (1.50-1.76)** | the stock route grows 0.94 (0.78-1.10) MB/turn more |
| EXE private at turn 351 / 426 | 1,939 / 1,965 MB | 2,095 / 2,237 MB | proxy 156 MB lower at the start, 272 MB at the end |
| largest free block, slope | **-1.91 (-2.21..-1.60)** | **-2.70 (-2.96..-2.44)** | |
| largest free block at turn 351 / 426 | 1,439 / 1,255 MB | 1,137 / 857 MB | proxy 300-400 MB more |
| straight-line turn of the 12 MB largest block | about 1,060 (987) | about 730 (711) | |

- **D78 is met [L]:** through the proxy (pin `x64-76992d23-p1607d42`) the EXE grows less per AI turn than on the
  stock route (0.70 against 1.63 MB, intervals apart), starts lower and keeps a larger free block all game. Bug 0102's
  growth (24.3 MB/turn, `03-dll-engine-boundary/proxy-design.md` 18.6) is gone over the whole game, not only 30 turns.
- Neither route runs out of address space in this game; the straight-line wall of the stock route lies about 300 turns
  after the game's end, the proxy route's about 600 (on its own 86 turns: about turn 1,230). The 'wall past turn 700'
  of D78 holds for both [I]: these are projections of a game that ends at 425-436.
- The window is 75 turns on the stock route, below the MC's amended "at least 80" (the stock game ends 11 turns
  earlier); the common window is 75 turns.
- **The two routes play different games [L]:** Egypt falls at turn 436 through the proxy and at turn 425 on the stock
  route, from the same save. The stock route's turn-412 answers were given by hand (refusals, as the handler gives),
  which can change the game from there; whether the routes had already differed before turn 412 was not checked (both
  runs overwrite the same instance's autosaves; a comparison needs the saves kept per run). This belongs to bug 0116
  (x64 and x86 games diverge on the current pins), not to D78. 9036's turn-418 save, played through the proxy, ended
  in Egypt's defeat at turn 437 (`03-dll-engine-boundary/proxy-design.md` 18.9).
- Why the proxy route grows less: the DLL's own heap growth (56 % of the EXE's growth bytes on the stock route,
  `README.md` item 6) lives in the server (server private +0.65 MB/turn, 248 -> 317 MB) [I: by the numbers; not
  attributed by a census].
- Run-to-run spread on one route: the proxy route over 353-380 gave +0.67 here and +2.01 in 9041's `ring-350` (old
  server binary, same proxy code); slopes over 28 turns carry about 1 MB/turn of spread. The 75-turn figures are the
  acceptance numbers.

## Sources

Runs and logs in `D:/c5-64bit-work/9043/` (not committed): `runs/<run>/run.json` (per-turn rows), `memory.csv`,
`run.log`, `vpproxy.log`, `server-report.txt`, `end*.png`; `mem-<run>/` (sampler: `samples.csv`, `per-turn.csv`,
`summary.txt`). Tracker 9043; pins in `deps/dll.lock` and `deps/i0.lock`.

## 4. The human game through the proxy (ticket 0322, 2026-11-15)

The huge save under **scripted human play** through the proxy (`03-dll-engine-boundary/proxy-design.md` 26.5): run
`huge80b`, c5h-h (Goldberg copy on F:, workstation), server and proxy from repository 2d59c2326 with the x64 DLL of the
pin 04b1211b8 (DLL `74588135...`, server `7de634bc...`, proxy `e8345b78...`), thin client (`ServerRenderer = 0`),
`local_answers=1 lua_mirror=0`, save `C5H-VP8P-HUGE_0350` (`c499d3c6...88c3`), turns 350-429 (80 human turns, Egypt
alive at the end), 2026-11-15 02:49-03:51 EST. Every 10 turns the city screen, tech tree, a leader screen, the
notification list and a save; every turn the human's orders (51-73 ready units, all but one skipped). Sampler
`perf/memsampler/memsampler.py run --instance c5h-h --server --every 2` beside it (rows at each end-of-turn autosave);
the driver's own read after each turn (`tools/humanplay/memslope.py`, OLS, 95 % interval from Student's t, the row after
the AI part of turn t labelled t+1). Memory only: no quiet lock; other teams' games and builds ran on the workstation
(8 game instances registered at 00:59), so the numbers are an indication of the slope, as D78's were.

| metric | driver rows, turns 351-430 (n 80) | sampler, turns 350-429 (n 80) | D78 stock route (AI play) | D78 proxy route (AI play) |
|---|---|---|---|---|
| EXE private bytes, MB/turn | **+0.84 (+0.71..+0.97)**, 1,875 -> 1,989 MB | +0.91 (+0.76..+1.06) | +1.63 (+1.50..+1.76) | +0.70 (+0.59..+0.80) |
| largest free block, MB/turn | **-2.02 (-2.27..-1.78)**, 1,438 -> 1,203 MB | -2.01 (-2.25..-1.77) | -2.70 (-2.96..-2.44) | -1.91 (-2.21..-1.60) |
| server private bytes, MB/turn | +0.60 (+0.52..+0.67), 249 -> 304 MB | +0.62 (+0.54..+0.71) | - | +0.65 |

Over D78's common window (turns 351-426, n 76): private +0.86 (+0.72..+1.00), largest free block -2.07 (-2.33..-1.80).
Smallest largest free block at any 2-s tick 1,151.6 MB, smallest free address space 1,250.9 MB.

- **D78 holds under human play [L]:** neither slope is worse than the stock route's (private 0.84 against 1.63, intervals
  apart; largest free block -2.02 against -2.70, intervals apart). Against the proxy route under AI play the private
  slope is 0.14 MB/turn higher and the largest free block 0.11 MB/turn steeper, both inside each other's intervals.
- **What the human part adds [I, from the per-turn rows]:** the largest free block falls in steps, as under AI play
  (1,438 -> 1,364 at the first round of screens at turn 350, -32 at 355, -80 at 364, -32 at 380, flat between); the
  first step (-74 MB, private +28 MB) came with the first round of screens (tech tree, leader screen, city screen), the
  EXE loading their art once. The private bytes move by -25..+28 MB from turn to turn (selections pan the camera over
  the map). Not attributed by a census.
- The Tiny games (no D78 criterion; same build): `tiny150` private +0.53 (+0.49..+0.58), largest free -0.66
  (-0.72..-0.60) over 150 turns; `tiny150b` +0.40 and -0.52 over 147.

Sources: `C:/c5-64bit-work/0322/ev/huge80b/run.json`, `memory.csv`, `C:/c5-64bit-work/0322/ev/mem-huge80b/` (sampler:
`samples.csv`, `per-turn.csv`, `summary.txt`); kept on F: after the ticket (`F:/c5-64bit-work/0322/`).
