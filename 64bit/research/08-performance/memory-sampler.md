# Per-turn memory of a live EXE: method, turn source, first numbers (perf issue 7011)

Written 2026-11-12 by the performance team's cycle-2 worker. Tool: `perf/memsampler/memsampler.py` (commands and exit
codes in `perf/README.md`, section "memsampler"). First live use 2026-11-13 by one-off 9043 (section 5b). *(Replaced:
"Not yet run beside a live game: the live acceptance run is a separate step of the perf lead".)*

## 1. Why

One-off 9036 (bug 0102, 2026-11-11) found the 32-bit EXE out of address space at turn 420 of the huge save through
the proxy (commit 3420 MB, largest free block 12 MB, `std::bad_alloc`), and its last line before the usage-limit stop
was "EXE private memory through the proxy 1,965 -> 2,104 MB over three turns (~45 MB/turn) against ~2 MB/turn on the
stock route" (`docs/MC-STATE.md`, 2026-11-12 20:32). Its per-turn numbers came by hand from the harness's time-based
`memory.csv` (`tools/harness/memory.py`, the checklist's sampler) and the turn driver's log. No tool gave memory per
turn with an interval; this one does.

## 2. Where the turn comes from

Sources considered for a turn number "the game already writes", for a game the sampler did not start:

| source | verdict |
|---|---|
| Autosave file names `AutoSave_NNNN <date>.Civ5Save` | **used**. Written every turn on our instances (their `UserSettings.ini` has `TurnsBetweenAutosave = 1`; the game's own default is every 10 turns, 5 kept: ticket 5032, `saves-off-the-game-thread.md` 6b, corrected 2026-11-24; was "every turn by default"), in the instance's `ModdedSaves/single/auto` (modded game) or `Saves/single/auto`; the civ5-game-ui skill names them as the live progress source |
| Lua channel `Game.GetGameTurn()` (`tools/harness/turns.py` `turn_state`) | not used: it runs code in the game's Lua state and needs the harness's channel; a sampler must not drive the game |
| `stats.db` (`SQLITE_LOGGING`) | not used: the DLL buffers up to 1024 rows, so it lags the live game (civ5-game-ui skill, "stats.db lags the live game"), and a reader's shared lock can collide with the game's writes |
| server `server.log` "turn N done ... working set M MB" (longgames.py `x64_memory`) | offered (`--turn-log`), but only the server's direct mode writes it; in the proxy route (remote mode) `server.log` has no per-turn line [C, 9036's `D:/c5-64bit-work/9036/runs/proxy-350/server.log`, 215 lines, none per turn] |

**What an autosave marks [L, file times].** The modification times in c5h-a's `ModdedSaves/single/auto` after 9036's
proxy run (2026-11-11, huge save turns 350-361, harness `turns.py end` then `wait` each turn) alternate: `AutoSave_0359`
17:28:22, `AutoSave_Post_0360` 17:28:28, `AutoSave_0360` 17:28:51, `AutoSave_Post_0361` 17:28:57, `AutoSave_0361`
17:31:22. With the harness's turn times (run-and-drive-the-game.md, the 1114 run: `AutoSave_Post_0350` 04:17:45 a few
seconds after the forced end of turn 350, `AutoSave_0350` 04:22:53 when turn 351 became active after 314 s), the
reading is: `AutoSave_Post_N` = the human ended turn N (before the AI part), `AutoSave_N` = the AI part of turn N is
done and turn N+1 starts. The sampler's default marker is `AutoSave_N` ("turn N done"), the quiet point at the start of
the human's next turn; its row is the first sample after the file appears (within one `--every`). The constant offset
of one against `Game.GetGameTurn()` does not change a slope. `0ldAutoSave_*` files in `auto/prev` are the game's
rotation and are not read. A load rewrites the loaded turn's autosave within seconds
(`research/04-memory-investigation/instruments-and-tooling.md`), so a sampler started before the load gets that turn
as its first row (memory right after the load): the second-half slope exists for that reason.

If the game's autosave interval is not one turn, rows come every k turns; the slope is still per game turn.

## 3. What is read, and its cost

`tools/harness/memory.py` (imported, not copied): `OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ |
SYNCHRONIZE)` (same user, no administrator rights), `GetProcessMemoryInfo` (private bytes = PrivateUsage, working
set), a `VirtualQueryEx` walk (committed, reserved, free; 4 GB for a WOW64 process, 128 TB for a 64-bit one).
Virtual size = committed + reserved regions. No debugger, no thread suspension, no code in the game.
Cost of one read, 2026-11-12, this workstation [L]: 5.8 ms for a small 32-bit process (NVIDIA Web Helper, 29 MB
private), 0.85 ms for a 64-bit Python; the game has more regions, so expect more [I]. At the default 2 s that is well
under 1 % of one core, spent in the sampler's process; the walk only takes the target's address-space lock for each
query [I].

## 4. Statistics

Per process and metric: ordinary least squares of the value on the turn number, slope in MB per turn, 95 % interval
`slope +- t(0.975, n-2) * se`, `se = sqrt(SSE / (n-2) / Sxx)` (t from the standard table to 30 degrees of freedom,
Cornish-Fisher above, error below 0.001; checked against `statistics.linear_regression` in the tests). Over all rows
and over the second half of the rows (`rows[n//2:]`, as longgames.py's `mem_summary`). The rows are a time series:
successive turns are not independent (heap growth carries over), so the interval is a guide, not a test; compare two
routes by their slopes over the same turns of the same save.

## 5. First numbers: 9036's proxy run, joined afterwards [L data, retro join, 2026-11-12]

`memsampler.py join --memory-csv D:/c5-64bit-work/9036/runs/proxy-350/memory.csv --saves-dir <c5h-a>/ModdedSaves/single/auto`
(164 samples every 5 s, 12 autosaves 350-361; the run: EXE through the proxy, `x64-76992d23` proxy layout, instance DLL
`i0ship-bbfacca1-ia`, huge save from turn 350, 2026-11-11 17:17-17:32, harness checklist sampler):

| metric (MB) | turn 350 | turn 361 | slope MB/turn, all 12 turns (95 %) | second half, turns 356-361 (95 %) |
|---|---|---|---|---|
| private bytes | 1952 | 2309 | +30.7 (+26.5..+34.9) | +20.8 (+18.0..+23.6) |
| working set | 1684 | 2630 | +53.1 (+25.2..+81.0) | +104 (-33..+242) |
| virtual size | 2678 | 3093 | +38.2 (+34.7..+41.6) | +29.1 (+17.8..+40.5) |
| free address space | 1418 | 1004 | -38.1 | -29.1 |
| largest free block | 1363 | 923 | -40.8 (-44.1..-37.4) | -31.6 (-42.0..-21.2) |

Mean 37.6 s per turn. So over twelve turns the proxy route's EXE grew about 31 MB of private bytes per turn (21 in the
second half), not 45: 9036's 45 MB/turn came from three turns. The tool's straight-line projection says the free
address space would be gone about 34 turns after turn 361 (second-half free-space slope -29 MB/turn); in 9034's run
the EXE still had 159 MB free at turn 420, 59 turns after 361 (9036's dump), so the loss slowed to about
(1004 - 159) / 59 = 14 MB per turn on average after turn 361 [I: two different runs of the same save]. Read the
projection as an order of magnitude only. The stock-route control (`D:/c5-64bit-work/9036/runs/stock-350/memory.csv`)
cannot be joined: the proxy run rewrote the same instance's autosaves 350-361 afterwards. A stock-against-proxy
comparison with this tool needs a fresh pair of runs (the live acceptance step).

## 5b. First live use (one-off 9043, 2026-11-13) [L]

Beside `tools/validation/proxy_long.py` on c5h-b, huge save `C5H-VP8P-HUGE_0350`, started by a wrapper that retries
`memsampler.py run --out D:/c5-64bit-work/9043/mem-<run> --instance c5h-b --server --every 2` while it exits 11 (no live
game in the harness state yet), so it attached about 10 s after the EXE started and before the load:

- Proxy route run `proxy-350` (pin layout `x64-76992d23-p1607d42`, 02:02-02:59): 86 EXE rows (turns 350-435) and 86
  server rows, **0 skipped turns**, ended by itself "process exited" (exit 0) when the harness stopped the game. The
  server child (`server.exe` under the EXE) was found when it appeared. Marker latency (row time minus the autosave's
  mtime): median 0.5 s, max 1.1 s. The in-turn peak of private bytes lies a median 18 MB (max 115 MB) above the row
  value (the row is the quiet point after the autosave).
- Stock route run `stock-350`/`stock-350b` (03:44-04:43): 75 EXE rows (350-424), 0 skipped, marker latency median
  0.6 s, max 1.7 s; it kept sampling the same process across the driver's restart at turn 412 (the continuous series
  the driver could not give). Private +1.82 (1.68-1.95) against the driver's +1.63 (1.50-1.76) over the same game
  (the sampler's first row is right after the load, which lifts its slope a little; its second half +1.38).
- Agreement with the driver's own per-turn reads (`exe_mem` after `turns.py wait`, labelled turn+1): private bytes
  +0.77 MB/turn (95 % CI 0.69-0.85) by the sampler against +0.68 (0.59-0.77) by the driver over the same game; largest
  free block -1.55 against -1.52. The two read at different moments of the turn (after the autosave against after the
  human turn is up), so the rows differ by a few MB; the slopes agree within their intervals.
- Its projection line ("free address space used up in about 6,514 turns at the second-half free-space slope") uses the
  second half only, where the largest free block was flat (-0.06 MB/turn): read it as "no wall in sight", not as a turn.
  The all-turn slope gives about turn 1,100-1,230 (`research/04-memory-investigation/increment-1-acceptance.md`).
- No dump, no lock, no measurable disturbance of the run; it costs nothing to keep it beside every long run.

## 5c. An AI-autoplay game (epic 0330 premise M1, issue 4203, 2026-11-15) [L]

Autoplay writes only `AutoSave_Post_N` (no `AutoSave_N`), so the sampler needs `--autosave-kind post` there. With it,
beside `tools/stressmap/calibrate.py` on the giant game (c5h-b, pin `i0ship-26234371-ia`, P7 stock seed 1), turns
0-39: 40 of 40 turns, 0 skipped; per-turn rows agree with the driver's own reads (median difference 0.0-0.15 MB, slopes
of free space and largest free block within 0.02 MB/turn). Details and evidence: `metrics-ledger.md` section 1, M1.

## 6. Known limits

- *(Answered 2026-11-13 by 5b: run live beside two driven games; replaced: "Not run beside a live game yet: marker
  latency, the cost of a read on the game's address space and the server discovery through the proxy are verified only
  with fake processes and a Python child in the tests".)* The cost of one read on the game was not timed live.
- `--instance` needs the game already in the harness state (exit 11 before); a retry loop around it is the way to
  start it together with a driver.
- A reload of an earlier save makes the turn go back: the sampler records the event and no rows until the turn passes
  the highest one already recorded.
- The x64 server's free address space is not reported (128 TB is never the limit); its private bytes, working set and
  virtual size are.
